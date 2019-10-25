// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/stringhash.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/coresight.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>

#include "coresight-etm-perf.h"
#include "coresight-priv.h"

static DEFINE_MUTEX(coresight_mutex);

/**
 * struct coresight_node - elements of a path, from source to sink
 * @csdev:	Address of an element.
 * @link:	hook to the list.
 */
struct coresight_node {
	struct coresight_device *csdev;
	struct list_head link;
};

/*
 * When operating Coresight drivers from the sysFS interface, only a single
 * path can exist from a tracer (associated to a CPU) to a sink.
 */
static DEFINE_PER_CPU(struct list_head *, tracer_path);

/*
 * As of this writing only a single STM can be found in CS topologies.  Since
 * there is no way to know if we'll ever see more and what kind of
 * configuration they will enact, for the time being only define a single path
 * for STM.
 */
static struct list_head *stm_path;

/*
 * When losing synchronisation a new barrier packet needs to be inserted at the
 * beginning of the data collected in a buffer.  That way the decoder knows that
 * it needs to look for another sync sequence.
 */
const u32 barrier_pkt[4] = {0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff};

static int coresight_id_match(struct device *dev, void *data)
{
	int trace_id, i_trace_id;
	struct coresight_device *csdev, *i_csdev;

	csdev = data;
	i_csdev = to_coresight_device(dev);

	/*
	 * No need to care about oneself and components that are not
	 * sources or not enabled
	 */
	if (i_csdev == csdev || !i_csdev->enable ||
	    i_csdev->type != CORESIGHT_DEV_TYPE_SOURCE)
		return 0;

	/* Get the source ID for both compoment */
	trace_id = source_ops(csdev)->trace_id(csdev);
	i_trace_id = source_ops(i_csdev)->trace_id(i_csdev);

	/* All you need is one */
	if (trace_id == i_trace_id)
		return 1;

	return 0;
}

static int coresight_source_is_unique(struct coresight_device *csdev)
{
	int trace_id = source_ops(csdev)->trace_id(csdev);

	/* this shouldn't happen */
	if (trace_id < 0)
		return 0;

	return !bus_for_each_dev(&coresight_bustype, NULL,
				 csdev, coresight_id_match);
}

static int coresight_find_link_inport(struct coresight_device *csdev,
				      struct coresight_device *parent)
{
	int i;
	struct coresight_connection *conn;

	for (i = 0; i < parent->pdata->nr_outport; i++) {
		conn = &parent->pdata->conns[i];
		if (conn->child_dev == csdev)
			return conn->child_port;
	}

	dev_err(&csdev->dev, "couldn't find inport, parent: %s, child: %s\n",
		dev_name(&parent->dev), dev_name(&csdev->dev));

	return -ENODEV;
}

static int coresight_find_link_outport(struct coresight_device *csdev,
				       struct coresight_device *child)
{
	int i;
	struct coresight_connection *conn;

	for (i = 0; i < csdev->pdata->nr_outport; i++) {
		conn = &csdev->pdata->conns[i];
		if (conn->child_dev == child)
			return conn->outport;
	}

	dev_err(&csdev->dev, "couldn't find outport, parent: %s, child: %s\n",
		dev_name(&csdev->dev), dev_name(&child->dev));

	return -ENODEV;
}

static inline u32 coresight_read_claim_tags(void __iomem *base)
{
	return readl_relaxed(base + CORESIGHT_CLAIMCLR);
}

static inline bool coresight_is_claimed_self_hosted(void __iomem *base)
{
	return coresight_read_claim_tags(base) == CORESIGHT_CLAIM_SELF_HOSTED;
}

static inline bool coresight_is_claimed_any(void __iomem *base)
{
	return coresight_read_claim_tags(base) != 0;
}

static inline void coresight_set_claim_tags(void __iomem *base)
{
	writel_relaxed(CORESIGHT_CLAIM_SELF_HOSTED, base + CORESIGHT_CLAIMSET);
	isb();
}

static inline void coresight_clear_claim_tags(void __iomem *base)
{
	writel_relaxed(CORESIGHT_CLAIM_SELF_HOSTED, base + CORESIGHT_CLAIMCLR);
	isb();
}

/*
 * coresight_claim_device_unlocked : Claim the device for self-hosted usage
 * to prevent an external tool from touching this device. As per PSCI
 * standards, section "Preserving the execution context" => "Debug and Trace
 * save and Restore", DBGCLAIM[1] is reserved for Self-hosted debug/trace and
 * DBGCLAIM[0] is reserved for external tools.
 *
 * Called with CS_UNLOCKed for the component.
 * Returns : 0 on success
 */
int coresight_claim_device_unlocked(void __iomem *base)
{
	if (coresight_is_claimed_any(base))
		return -EBUSY;

	coresight_set_claim_tags(base);
	if (coresight_is_claimed_self_hosted(base))
		return 0;
	/* There was a race setting the tags, clean up and fail */
	coresight_clear_claim_tags(base);
	return -EBUSY;
}

int coresight_claim_device(void __iomem *base)
{
	int rc;

	CS_UNLOCK(base);
	rc = coresight_claim_device_unlocked(base);
	CS_LOCK(base);

	return rc;
}

/*
 * coresight_disclaim_device_unlocked : Clear the claim tags for the device.
 * Called with CS_UNLOCKed for the component.
 */
void coresight_disclaim_device_unlocked(void __iomem *base)
{

	if (coresight_is_claimed_self_hosted(base))
		coresight_clear_claim_tags(base);
	else
		/*
		 * The external agent may have not honoured our claim
		 * and has manipulated it. Or something else has seriously
		 * gone wrong in our driver.
		 */
		WARN_ON_ONCE(1);
}

void coresight_disclaim_device(void __iomem *base)
{
	CS_UNLOCK(base);
	coresight_disclaim_device_unlocked(base);
	CS_LOCK(base);
}

static int coresight_enable_sink(struct coresight_device *csdev,
				 u32 mode, void *data)
{
	int ret;

	/*
	 * We need to make sure the "new" session is compatible with the
	 * existing "mode" of operation.
	 */
	if (!sink_ops(csdev)->enable)
		return -EINVAL;

	ret = sink_ops(csdev)->enable(csdev, mode, data);
	if (ret)
		return ret;
	csdev->enable = true;

	return 0;
}

static void coresight_disable_sink(struct coresight_device *csdev)
{
	int ret;

	if (!sink_ops(csdev)->disable)
		return;

	ret = sink_ops(csdev)->disable(csdev);
	if (ret)
		return;
	csdev->enable = false;
}

static int coresight_enable_link(struct coresight_device *csdev,
				 struct coresight_device *parent,
				 struct coresight_device *child)
{
	int ret;
	int link_subtype;
	int refport, inport, outport;

	if (!parent || !child)
		return -EINVAL;

	inport = coresight_find_link_inport(csdev, parent);
	outport = coresight_find_link_outport(csdev, child);
	link_subtype = csdev->subtype.link_subtype;

	if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_MERG)
		refport = inport;
	else if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_SPLIT)
		refport = outport;
	else
		refport = 0;

	if (refport < 0)
		return refport;

	if (atomic_inc_return(&csdev->refcnt[refport]) == 1) {
		if (link_ops(csdev)->enable) {
			ret = link_ops(csdev)->enable(csdev, inport, outport);
			if (ret) {
				atomic_dec(&csdev->refcnt[refport]);
				return ret;
			}
		}
	}

	csdev->enable = true;

	return 0;
}

static void coresight_disable_link(struct coresight_device *csdev,
				   struct coresight_device *parent,
				   struct coresight_device *child)
{
	int i, nr_conns;
	int link_subtype;
	int refport, inport, outport;

	if (!parent || !child)
		return;

	inport = coresight_find_link_inport(csdev, parent);
	outport = coresight_find_link_outport(csdev, child);
	link_subtype = csdev->subtype.link_subtype;

	if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_MERG) {
		refport = inport;
		nr_conns = csdev->pdata->nr_inport;
	} else if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_SPLIT) {
		refport = outport;
		nr_conns = csdev->pdata->nr_outport;
	} else {
		refport = 0;
		nr_conns = 1;
	}

	if (atomic_dec_return(&csdev->refcnt[refport]) == 0) {
		if (link_ops(csdev)->disable)
			link_ops(csdev)->disable(csdev, inport, outport);
	}

	for (i = 0; i < nr_conns; i++)
		if (atomic_read(&csdev->refcnt[i]) != 0)
			return;

	csdev->enable = false;
}

static int coresight_enable_source(struct coresight_device *csdev, u32 mode)
{
	int ret;

	if (!coresight_source_is_unique(csdev)) {
		dev_warn(&csdev->dev, "traceID %d not unique\n",
			 source_ops(csdev)->trace_id(csdev));
		return -EINVAL;
	}

	if (!csdev->enable) {
		if (source_ops(csdev)->enable) {
			ret = source_ops(csdev)->enable(csdev, NULL, mode);
			if (ret)
				return ret;
		}
		csdev->enable = true;
	}

	atomic_inc(csdev->refcnt);

	return 0;
}

/**
 *  coresight_disable_source - Drop the reference count by 1 and disable
 *  the device if there are no users left.
 *
 *  @csdev - The coresight device to disable
 *
 *  Returns true if the device has been disabled.
 */
static bool coresight_disable_source(struct coresight_device *csdev)
{
	if (atomic_dec_return(csdev->refcnt) == 0) {
		if (source_ops(csdev)->disable)
			source_ops(csdev)->disable(csdev, NULL);
		csdev->enable = false;
	}
	return !csdev->enable;
}

/*
 * coresight_disable_path_from : Disable components in the given path beyond
 * @nd in the list. If @nd is NULL, all the components, except the SOURCE are
 * disabled.
 */
static void coresight_disable_path_from(struct list_head *path,
					struct coresight_node *nd)
{
	u32 type;
	struct coresight_device *csdev, *parent, *child;

	if (!nd)
		nd = list_first_entry(path, struct coresight_node, link);

	list_for_each_entry_continue(nd, path, link) {
		csdev = nd->csdev;
		type = csdev->type;

		/*
		 * ETF devices are tricky... They can be a link or a sink,
		 * depending on how they are configured.  If an ETF has been
		 * "activated" it will be configured as a sink, otherwise
		 * go ahead with the link configuration.
		 */
		if (type == CORESIGHT_DEV_TYPE_LINKSINK)
			type = (csdev == coresight_get_sink(path)) ?
						CORESIGHT_DEV_TYPE_SINK :
						CORESIGHT_DEV_TYPE_LINK;

		switch (type) {
		case CORESIGHT_DEV_TYPE_SINK:
			coresight_disable_sink(csdev);
			break;
		case CORESIGHT_DEV_TYPE_SOURCE:
			/*
			 * We skip the first node in the path assuming that it
			 * is the source. So we don't expect a source device in
			 * the middle of a path.
			 */
			WARN_ON(1);
			break;
		case CORESIGHT_DEV_TYPE_LINK:
			parent = list_prev_entry(nd, link)->csdev;
			child = list_next_entry(nd, link)->csdev;
			coresight_disable_link(csdev, parent, child);
			break;
		default:
			break;
		}
	}
}

void coresight_disable_path(struct list_head *path)
{
	coresight_disable_path_from(path, NULL);
}

int coresight_enable_path(struct list_head *path, u32 mode, void *sink_data)
{

	int ret = 0;
	u32 type;
	struct coresight_node *nd;
	struct coresight_device *csdev, *parent, *child;

	list_for_each_entry_reverse(nd, path, link) {
		csdev = nd->csdev;
		type = csdev->type;

		/*
		 * ETF devices are tricky... They can be a link or a sink,
		 * depending on how they are configured.  If an ETF has been
		 * "activated" it will be configured as a sink, otherwise
		 * go ahead with the link configuration.
		 */
		if (type == CORESIGHT_DEV_TYPE_LINKSINK)
			type = (csdev == coresight_get_sink(path)) ?
						CORESIGHT_DEV_TYPE_SINK :
						CORESIGHT_DEV_TYPE_LINK;

		switch (type) {
		case CORESIGHT_DEV_TYPE_SINK:
			ret = coresight_enable_sink(csdev, mode, sink_data);
			/*
			 * Sink is the first component turned on. If we
			 * failed to enable the sink, there are no components
			 * that need disabling. Disabling the path here
			 * would mean we could disrupt an existing session.
			 */
			if (ret)
				goto out;
			break;
		case CORESIGHT_DEV_TYPE_SOURCE:
			/* sources are enabled from either sysFS or Perf */
			break;
		case CORESIGHT_DEV_TYPE_LINK:
			parent = list_prev_entry(nd, link)->csdev;
			child = list_next_entry(nd, link)->csdev;
			ret = coresight_enable_link(csdev, parent, child);
			if (ret)
				goto err;
			break;
		default:
			goto err;
		}
	}

out:
	return ret;
err:
	coresight_disable_path_from(path, nd);
	goto out;
}

struct coresight_device *coresight_get_sink(struct list_head *path)
{
	struct coresight_device *csdev;

	if (!path)
		return NULL;

	csdev = list_last_entry(path, struct coresight_node, link)->csdev;
	if (csdev->type != CORESIGHT_DEV_TYPE_SINK &&
	    csdev->type != CORESIGHT_DEV_TYPE_LINKSINK)
		return NULL;

	return csdev;
}

static int coresight_enabled_sink(struct device *dev, const void *data)
{
	const bool *reset = data;
	struct coresight_device *csdev = to_coresight_device(dev);

	if ((csdev->type == CORESIGHT_DEV_TYPE_SINK ||
	     csdev->type == CORESIGHT_DEV_TYPE_LINKSINK) &&
	     csdev->activated) {
		/*
		 * Now that we have a handle on the sink for this session,
		 * disable the sysFS "enable_sink" flag so that possible
		 * concurrent perf session that wish to use another sink don't
		 * trip on it.  Doing so has no ramification for the current
		 * session.
		 */
		if (*reset)
			csdev->activated = false;

		return 1;
	}

	return 0;
}

/**
 * coresight_get_enabled_sink - returns the first enabled sink found on the bus
 * @deactivate:	Whether the 'enable_sink' flag should be reset
 *
 * When operated from perf the deactivate parameter should be set to 'true'.
 * That way the "enabled_sink" flag of the sink that was selected can be reset,
 * allowing for other concurrent perf sessions to choose a different sink.
 *
 * When operated from sysFS users have full control and as such the deactivate
 * parameter should be set to 'false', hence mandating users to explicitly
 * clear the flag.
 */
struct coresight_device *coresight_get_enabled_sink(bool deactivate)
{
	struct device *dev = NULL;

	dev = bus_find_device(&coresight_bustype, NULL, &deactivate,
			      coresight_enabled_sink);

	return dev ? to_coresight_device(dev) : NULL;
}

static int coresight_sink_by_id(struct device *dev, const void *data)
{
	struct coresight_device *csdev = to_coresight_device(dev);
	unsigned long hash;

	if (csdev->type == CORESIGHT_DEV_TYPE_SINK ||
	     csdev->type == CORESIGHT_DEV_TYPE_LINKSINK) {

		if (!csdev->ea)
			return 0;
		/*
		 * See function etm_perf_add_symlink_sink() to know where
		 * this comes from.
		 */
		hash = (unsigned long)csdev->ea->var;

		if ((u32)hash == *(u32 *)data)
			return 1;
	}

	return 0;
}

/**
 * coresight_get_sink_by_id - returns the sink that matches the id
 * @id: Id of the sink to match
 *
 * The name of a sink is unique, whether it is found on the AMBA bus or
 * otherwise.  As such the hash of that name can easily be used to identify
 * a sink.
 */
struct coresight_device *coresight_get_sink_by_id(u32 id)
{
	struct device *dev = NULL;

	dev = bus_find_device(&coresight_bustype, NULL, &id,
			      coresight_sink_by_id);

	return dev ? to_coresight_device(dev) : NULL;
}

/*
 * coresight_grab_device - Power up this device and any of the helper
 * devices connected to it for trace operation. Since the helper devices
 * don't appear on the trace path, they should be handled along with the
 * the master device.
 */
static void coresight_grab_device(struct coresight_device *csdev)
{
	int i;

	for (i = 0; i < csdev->pdata->nr_outport; i++) {
		struct coresight_device *child;

		child  = csdev->pdata->conns[i].child_dev;
		if (child && child->type == CORESIGHT_DEV_TYPE_HELPER)
			pm_runtime_get_sync(child->dev.parent);
	}
	pm_runtime_get_sync(csdev->dev.parent);
}

/*
 * coresight_drop_device - Release this device and any of the helper
 * devices connected to it.
 */
static void coresight_drop_device(struct coresight_device *csdev)
{
	int i;

	pm_runtime_put(csdev->dev.parent);
	for (i = 0; i < csdev->pdata->nr_outport; i++) {
		struct coresight_device *child;

		child  = csdev->pdata->conns[i].child_dev;
		if (child && child->type == CORESIGHT_DEV_TYPE_HELPER)
			pm_runtime_put(child->dev.parent);
	}
}

/**
 * _coresight_build_path - recursively build a path from a @csdev to a sink.
 * @csdev:	The device to start from.
 * @path:	The list to add devices to.
 *
 * The tree of Coresight device is traversed until an activated sink is
 * found.  From there the sink is added to the list along with all the
 * devices that led to that point - the end result is a list from source
 * to sink. In that list the source is the first device and the sink the
 * last one.
 */
static int _coresight_build_path(struct coresight_device *csdev,
				 struct coresight_device *sink,
				 struct list_head *path)
{
	int i;
	bool found = false;
	struct coresight_node *node;

	/* An activated sink has been found.  Enqueue the element */
	if (csdev == sink)
		goto out;

	/* Not a sink - recursively explore each port found on this element */
	for (i = 0; i < csdev->pdata->nr_outport; i++) {
		struct coresight_device *child_dev;

		child_dev = csdev->pdata->conns[i].child_dev;
		if (child_dev &&
		    _coresight_build_path(child_dev, sink, path) == 0) {
			found = true;
			break;
		}
	}

	if (!found)
		return -ENODEV;

out:
	/*
	 * A path from this element to a sink has been found.  The elements
	 * leading to the sink are already enqueued, all that is left to do
	 * is tell the PM runtime core we need this element and add a node
	 * for it.
	 */
	node = kzalloc(sizeof(struct coresight_node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	coresight_grab_device(csdev);
	node->csdev = csdev;
	list_add(&node->link, path);

	return 0;
}

struct list_head *coresight_build_path(struct coresight_device *source,
				       struct coresight_device *sink)
{
	struct list_head *path;
	int rc;

	if (!sink)
		return ERR_PTR(-EINVAL);

	path = kzalloc(sizeof(struct list_head), GFP_KERNEL);
	if (!path)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(path);

	rc = _coresight_build_path(source, sink, path);
	if (rc) {
		kfree(path);
		return ERR_PTR(rc);
	}

	return path;
}

/**
 * coresight_release_path - release a previously built path.
 * @path:	the path to release.
 *
 * Go through all the elements of a path and 1) removed it from the list and
 * 2) free the memory allocated for each node.
 */
void coresight_release_path(struct list_head *path)
{
	struct coresight_device *csdev;
	struct coresight_node *nd, *next;

	list_for_each_entry_safe(nd, next, path, link) {
		csdev = nd->csdev;

		coresight_drop_device(csdev);
		list_del(&nd->link);
		kfree(nd);
	}

	kfree(path);
	path = NULL;
}

/** coresight_validate_source - make sure a source has the right credentials
 *  @csdev:	the device structure for a source.
 *  @function:	the function this was called from.
 *
 * Assumes the coresight_mutex is held.
 */
static int coresight_validate_source(struct coresight_device *csdev,
				     const char *function)
{
	u32 type, subtype;

	type = csdev->type;
	subtype = csdev->subtype.source_subtype;

	if (type != CORESIGHT_DEV_TYPE_SOURCE) {
		dev_err(&csdev->dev, "wrong device type in %s\n", function);
		return -EINVAL;
	}

	if (subtype != CORESIGHT_DEV_SUBTYPE_SOURCE_PROC &&
	    subtype != CORESIGHT_DEV_SUBTYPE_SOURCE_SOFTWARE) {
		dev_err(&csdev->dev, "wrong device subtype in %s\n", function);
		return -EINVAL;
	}

	return 0;
}

int coresight_enable(struct coresight_device *csdev)
{
	int cpu, ret = 0;
	struct coresight_device *sink;
	struct list_head *path;
	enum coresight_dev_subtype_source subtype;

	subtype = csdev->subtype.source_subtype;

	mutex_lock(&coresight_mutex);

	ret = coresight_validate_source(csdev, __func__);
	if (ret)
		goto out;

	if (csdev->enable) {
		/*
		 * There could be multiple applications driving the software
		 * source. So keep the refcount for each such user when the
		 * source is already enabled.
		 */
		if (subtype == CORESIGHT_DEV_SUBTYPE_SOURCE_SOFTWARE)
			atomic_inc(csdev->refcnt);
		goto out;
	}

	/*
	 * Search for a valid sink for this session but don't reset the
	 * "enable_sink" flag in sysFS.  Users get to do that explicitly.
	 */
	sink = coresight_get_enabled_sink(false);
	if (!sink) {
		ret = -EINVAL;
		goto out;
	}

	path = coresight_build_path(csdev, sink);
	if (IS_ERR(path)) {
		pr_err("building path(s) failed\n");
		ret = PTR_ERR(path);
		goto out;
	}

	ret = coresight_enable_path(path, CS_MODE_SYSFS, NULL);
	if (ret)
		goto err_path;

	ret = coresight_enable_source(csdev, CS_MODE_SYSFS);
	if (ret)
		goto err_source;

	switch (subtype) {
	case CORESIGHT_DEV_SUBTYPE_SOURCE_PROC:
		/*
		 * When working from sysFS it is important to keep track
		 * of the paths that were created so that they can be
		 * undone in 'coresight_disable()'.  Since there can only
		 * be a single session per tracer (when working from sysFS)
		 * a per-cpu variable will do just fine.
		 */
		cpu = source_ops(csdev)->cpu_id(csdev);
		per_cpu(tracer_path, cpu) = path;
		break;
	case CORESIGHT_DEV_SUBTYPE_SOURCE_SOFTWARE:
		stm_path = path;
		break;
	default:
		/* We can't be here */
		break;
	}

out:
	mutex_unlock(&coresight_mutex);
	return ret;

err_source:
	coresight_disable_path(path);

err_path:
	coresight_release_path(path);
	goto out;
}
EXPORT_SYMBOL_GPL(coresight_enable);

void coresight_disable(struct coresight_device *csdev)
{
	int cpu, ret;
	struct list_head *path = NULL;

	mutex_lock(&coresight_mutex);

	ret = coresight_validate_source(csdev, __func__);
	if (ret)
		goto out;

	if (!csdev->enable || !coresight_disable_source(csdev))
		goto out;

	switch (csdev->subtype.source_subtype) {
	case CORESIGHT_DEV_SUBTYPE_SOURCE_PROC:
		cpu = source_ops(csdev)->cpu_id(csdev);
		path = per_cpu(tracer_path, cpu);
		per_cpu(tracer_path, cpu) = NULL;
		break;
	case CORESIGHT_DEV_SUBTYPE_SOURCE_SOFTWARE:
		path = stm_path;
		stm_path = NULL;
		break;
	default:
		/* We can't be here */
		break;
	}

	coresight_disable_path(path);
	coresight_release_path(path);

out:
	mutex_unlock(&coresight_mutex);
}
EXPORT_SYMBOL_GPL(coresight_disable);

static ssize_t enable_sink_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct coresight_device *csdev = to_coresight_device(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", csdev->activated);
}

static ssize_t enable_sink_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct coresight_device *csdev = to_coresight_device(dev);

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (val)
		csdev->activated = true;
	else
		csdev->activated = false;

	return size;

}
static DEVICE_ATTR_RW(enable_sink);

static ssize_t enable_source_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct coresight_device *csdev = to_coresight_device(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", csdev->enable);
}

static ssize_t enable_source_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	int ret = 0;
	unsigned long val;
	struct coresight_device *csdev = to_coresight_device(dev);

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (val) {
		ret = coresight_enable(csdev);
		if (ret)
			return ret;
	} else {
		coresight_disable(csdev);
	}

	return size;
}
static DEVICE_ATTR_RW(enable_source);

static struct attribute *coresight_sink_attrs[] = {
	&dev_attr_enable_sink.attr,
	NULL,
};
ATTRIBUTE_GROUPS(coresight_sink);

static struct attribute *coresight_source_attrs[] = {
	&dev_attr_enable_source.attr,
	NULL,
};
ATTRIBUTE_GROUPS(coresight_source);

static struct device_type coresight_dev_type[] = {
	{
		.name = "none",
	},
	{
		.name = "sink",
		.groups = coresight_sink_groups,
	},
	{
		.name = "link",
	},
	{
		.name = "linksink",
		.groups = coresight_sink_groups,
	},
	{
		.name = "source",
		.groups = coresight_source_groups,
	},
	{
		.name = "helper",
	},
};

static void coresight_device_release(struct device *dev)
{
	struct coresight_device *csdev = to_coresight_device(dev);

	fwnode_handle_put(csdev->dev.fwnode);
	kfree(csdev->refcnt);
	kfree(csdev);
}

static int coresight_orphan_match(struct device *dev, void *data)
{
	int i;
	bool still_orphan = false;
	struct coresight_device *csdev, *i_csdev;
	struct coresight_connection *conn;

	csdev = data;
	i_csdev = to_coresight_device(dev);

	/* No need to check oneself */
	if (csdev == i_csdev)
		return 0;

	/* Move on to another component if no connection is orphan */
	if (!i_csdev->orphan)
		return 0;
	/*
	 * Circle throuch all the connection of that component.  If we find
	 * an orphan connection whose name matches @csdev, link it.
	 */
	for (i = 0; i < i_csdev->pdata->nr_outport; i++) {
		conn = &i_csdev->pdata->conns[i];

		/* We have found at least one orphan connection */
		if (conn->child_dev == NULL) {
			/* Does it match this newly added device? */
			if (conn->child_fwnode == csdev->dev.fwnode)
				conn->child_dev = csdev;
			else
				/* This component still has an orphan */
				still_orphan = true;
		}
	}

	i_csdev->orphan = still_orphan;

	/*
	 * Returning '0' ensures that all known component on the
	 * bus will be checked.
	 */
	return 0;
}

static void coresight_fixup_orphan_conns(struct coresight_device *csdev)
{
	/*
	 * No need to check for a return value as orphan connection(s)
	 * are hooked-up with each newly added component.
	 */
	bus_for_each_dev(&coresight_bustype, NULL,
			 csdev, coresight_orphan_match);
}


static void coresight_fixup_device_conns(struct coresight_device *csdev)
{
	int i;

	for (i = 0; i < csdev->pdata->nr_outport; i++) {
		struct coresight_connection *conn = &csdev->pdata->conns[i];
		struct device *dev = NULL;

		dev = bus_find_device_by_fwnode(&coresight_bustype, conn->child_fwnode);
		if (dev) {
			conn->child_dev = to_coresight_device(dev);
			/* and put reference from 'bus_find_device()' */
			put_device(dev);
		} else {
			csdev->orphan = true;
			conn->child_dev = NULL;
		}
	}
}

static int coresight_remove_match(struct device *dev, void *data)
{
	int i;
	struct coresight_device *csdev, *iterator;
	struct coresight_connection *conn;

	csdev = data;
	iterator = to_coresight_device(dev);

	/* No need to check oneself */
	if (csdev == iterator)
		return 0;

	/*
	 * Circle throuch all the connection of that component.  If we find
	 * a connection whose name matches @csdev, remove it.
	 */
	for (i = 0; i < iterator->pdata->nr_outport; i++) {
		conn = &iterator->pdata->conns[i];

		if (conn->child_dev == NULL)
			continue;

		if (csdev->dev.fwnode == conn->child_fwnode) {
			iterator->orphan = true;
			conn->child_dev = NULL;
			/*
			 * Drop the reference to the handle for the remote
			 * device acquired in parsing the connections from
			 * platform data.
			 */
			fwnode_handle_put(conn->child_fwnode);
			/* No need to continue */
			break;
		}
	}

	/*
	 * Returning '0' ensures that all known component on the
	 * bus will be checked.
	 */
	return 0;
}

/*
 * coresight_remove_conns - Remove references to this given devices
 * from the connections of other devices.
 */
static void coresight_remove_conns(struct coresight_device *csdev)
{
	/*
	 * Another device will point to this device only if there is
	 * an output port connected to this one. i.e, if the device
	 * doesn't have at least one input port, there is no point
	 * in searching all the devices.
	 */
	if (csdev->pdata->nr_inport)
		bus_for_each_dev(&coresight_bustype, NULL,
				 csdev, coresight_remove_match);
}

/**
 * coresight_timeout - loop until a bit has changed to a specific state.
 * @addr: base address of the area of interest.
 * @offset: address of a register, starting from @addr.
 * @position: the position of the bit of interest.
 * @value: the value the bit should have.
 *
 * Return: 0 as soon as the bit has taken the desired state or -EAGAIN if
 * TIMEOUT_US has elapsed, which ever happens first.
 */

int coresight_timeout(void __iomem *addr, u32 offset, int position, int value)
{
	int i;
	u32 val;

	for (i = TIMEOUT_US; i > 0; i--) {
		val = __raw_readl(addr + offset);
		/* waiting on the bit to go from 0 to 1 */
		if (value) {
			if (val & BIT(position))
				return 0;
		/* waiting on the bit to go from 1 to 0 */
		} else {
			if (!(val & BIT(position)))
				return 0;
		}

		/*
		 * Delay is arbitrary - the specification doesn't say how long
		 * we are expected to wait.  Extra check required to make sure
		 * we don't wait needlessly on the last iteration.
		 */
		if (i - 1)
			udelay(1);
	}

	return -EAGAIN;
}

struct bus_type coresight_bustype = {
	.name	= "coresight",
};

static int __init coresight_init(void)
{
	return bus_register(&coresight_bustype);
}
postcore_initcall(coresight_init);

/*
 * coresight_release_platform_data: Release references to the devices connected
 * to the output port of this device.
 */
void coresight_release_platform_data(struct coresight_platform_data *pdata)
{
	int i;

	for (i = 0; i < pdata->nr_outport; i++) {
		if (pdata->conns[i].child_fwnode) {
			fwnode_handle_put(pdata->conns[i].child_fwnode);
			pdata->conns[i].child_fwnode = NULL;
		}
	}
}

struct coresight_device *coresight_register(struct coresight_desc *desc)
{
	int ret;
	int link_subtype;
	int nr_refcnts = 1;
	atomic_t *refcnts = NULL;
	struct coresight_device *csdev;

	csdev = kzalloc(sizeof(*csdev), GFP_KERNEL);
	if (!csdev) {
		ret = -ENOMEM;
		goto err_out;
	}

	if (desc->type == CORESIGHT_DEV_TYPE_LINK ||
	    desc->type == CORESIGHT_DEV_TYPE_LINKSINK) {
		link_subtype = desc->subtype.link_subtype;

		if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_MERG)
			nr_refcnts = desc->pdata->nr_inport;
		else if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_SPLIT)
			nr_refcnts = desc->pdata->nr_outport;
	}

	refcnts = kcalloc(nr_refcnts, sizeof(*refcnts), GFP_KERNEL);
	if (!refcnts) {
		ret = -ENOMEM;
		goto err_free_csdev;
	}

	csdev->refcnt = refcnts;

	csdev->pdata = desc->pdata;

	csdev->type = desc->type;
	csdev->subtype = desc->subtype;
	csdev->ops = desc->ops;
	csdev->orphan = false;

	csdev->dev.type = &coresight_dev_type[desc->type];
	csdev->dev.groups = desc->groups;
	csdev->dev.parent = desc->dev;
	csdev->dev.release = coresight_device_release;
	csdev->dev.bus = &coresight_bustype;
	/*
	 * Hold the reference to our parent device. This will be
	 * dropped only in coresight_device_release().
	 */
	csdev->dev.fwnode = fwnode_handle_get(dev_fwnode(desc->dev));
	dev_set_name(&csdev->dev, "%s", desc->name);

	ret = device_register(&csdev->dev);
	if (ret) {
		put_device(&csdev->dev);
		/*
		 * All resources are free'd explicitly via
		 * coresight_device_release(), triggered from put_device().
		 */
		goto err_out;
	}

	if (csdev->type == CORESIGHT_DEV_TYPE_SINK ||
	    csdev->type == CORESIGHT_DEV_TYPE_LINKSINK) {
		ret = etm_perf_add_symlink_sink(csdev);

		if (ret) {
			device_unregister(&csdev->dev);
			/*
			 * As with the above, all resources are free'd
			 * explicitly via coresight_device_release() triggered
			 * from put_device(), which is in turn called from
			 * function device_unregister().
			 */
			goto err_out;
		}
	}

	mutex_lock(&coresight_mutex);

	coresight_fixup_device_conns(csdev);
	coresight_fixup_orphan_conns(csdev);

	mutex_unlock(&coresight_mutex);

	return csdev;

err_free_csdev:
	kfree(csdev);
err_out:
	/* Cleanup the connection information */
	coresight_release_platform_data(desc->pdata);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(coresight_register);

void coresight_unregister(struct coresight_device *csdev)
{
	etm_perf_del_symlink_sink(csdev);
	/* Remove references of that device in the topology */
	coresight_remove_conns(csdev);
	coresight_release_platform_data(csdev->pdata);
	device_unregister(&csdev->dev);
}
EXPORT_SYMBOL_GPL(coresight_unregister);


/*
 * coresight_search_device_idx - Search the fwnode handle of a device
 * in the given dev_idx list. Must be called with the coresight_mutex held.
 *
 * Returns the index of the entry, when found. Otherwise, -ENOENT.
 */
static inline int coresight_search_device_idx(struct coresight_dev_list *dict,
					      struct fwnode_handle *fwnode)
{
	int i;

	for (i = 0; i < dict->nr_idx; i++)
		if (dict->fwnode_list[i] == fwnode)
			return i;
	return -ENOENT;
}

/*
 * coresight_alloc_device_name - Get an index for a given device in the
 * device index list specific to a driver. An index is allocated for a
 * device and is tracked with the fwnode_handle to prevent allocating
 * duplicate indices for the same device (e.g, if we defer probing of
 * a device due to dependencies), in case the index is requested again.
 */
char *coresight_alloc_device_name(struct coresight_dev_list *dict,
				  struct device *dev)
{
	int idx;
	char *name = NULL;
	struct fwnode_handle **list;

	mutex_lock(&coresight_mutex);

	idx = coresight_search_device_idx(dict, dev_fwnode(dev));
	if (idx < 0) {
		/* Make space for the new entry */
		idx = dict->nr_idx;
		list = krealloc(dict->fwnode_list,
				(idx + 1) * sizeof(*dict->fwnode_list),
				GFP_KERNEL);
		if (ZERO_OR_NULL_PTR(list)) {
			idx = -ENOMEM;
			goto done;
		}

		list[idx] = dev_fwnode(dev);
		dict->fwnode_list = list;
		dict->nr_idx = idx + 1;
	}

	name = devm_kasprintf(dev, GFP_KERNEL, "%s%d", dict->pfx, idx);
done:
	mutex_unlock(&coresight_mutex);
	return name;
}
EXPORT_SYMBOL_GPL(coresight_alloc_device_name);
