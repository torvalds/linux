// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012, The Linux Foundation. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/build_bug.h>
#include <linux/cpu_pm.h>
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
#include <linux/property.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/panic_notifier.h>

#include "coresight-etm-perf.h"
#include "coresight-priv.h"
#include "coresight-syscfg.h"
#include "coresight-trace-id.h"

/*
 * Mutex used to lock all sysfs enable and disable actions and loading and
 * unloading devices by the Coresight core.
 */
DEFINE_MUTEX(coresight_mutex);
static DEFINE_PER_CPU(struct coresight_device *, csdev_sink);

static DEFINE_RAW_SPINLOCK(coresight_dev_lock);
static DEFINE_PER_CPU(struct coresight_device *, csdev_source);
static DEFINE_PER_CPU(bool, percpu_pm_failed);

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
 * When losing synchronisation a new barrier packet needs to be inserted at the
 * beginning of the data collected in a buffer.  That way the decoder knows that
 * it needs to look for another sync sequence.
 */
const u32 coresight_barrier_pkt[4] = {0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff};
EXPORT_SYMBOL_GPL(coresight_barrier_pkt);

/* List maintains the device index */
static LIST_HEAD(coresight_dev_idx_list);

static const struct cti_assoc_op *cti_assoc_ops;

static struct coresight_node *
coresight_path_first_node(struct coresight_path *path)
{
	if (list_empty(&path->path_list))
		return NULL;

	return list_first_entry(&path->path_list, struct coresight_node, link);
}

static struct coresight_node *
coresight_path_last_node(struct coresight_path *path)
{
	if (list_empty(&path->path_list))
		return NULL;

	return list_last_entry(&path->path_list, struct coresight_node, link);
}

void coresight_set_cti_ops(const struct cti_assoc_op *cti_op)
{
	cti_assoc_ops = cti_op;
}
EXPORT_SYMBOL_GPL(coresight_set_cti_ops);

void coresight_remove_cti_ops(void)
{
	cti_assoc_ops = NULL;
}
EXPORT_SYMBOL_GPL(coresight_remove_cti_ops);

void coresight_set_percpu_sink(int cpu, struct coresight_device *csdev)
{
	per_cpu(csdev_sink, cpu) = csdev;
}
EXPORT_SYMBOL_GPL(coresight_set_percpu_sink);

struct coresight_device *coresight_get_percpu_sink(int cpu)
{
	return per_cpu(csdev_sink, cpu);
}
EXPORT_SYMBOL_GPL(coresight_get_percpu_sink);

static void coresight_set_percpu_source(struct coresight_device *csdev)
{
	if (!csdev || !coresight_is_percpu_source(csdev))
		return;

	guard(raw_spinlock_irqsave)(&coresight_dev_lock);

	/* Expect no device to be set yet */
	WARN_ON(per_cpu(csdev_source, csdev->cpu));
	per_cpu(csdev_source, csdev->cpu) = csdev;
}

static void coresight_clear_percpu_source(struct coresight_device *csdev)
{
	if (!csdev || !coresight_is_percpu_source(csdev))
		return;

	/* Clear percpu_pm_failed */
	per_cpu(percpu_pm_failed, csdev->cpu) = false;

	guard(raw_spinlock_irqsave)(&coresight_dev_lock);

	/* The per-CPU pointer should contain the same csdev */
	WARN_ON(per_cpu(csdev_source, csdev->cpu) != csdev);
	per_cpu(csdev_source, csdev->cpu) = NULL;
}

struct coresight_device *coresight_get_percpu_source_ref(int cpu)
{
	struct coresight_device *csdev;

	if (WARN_ON(cpu < 0))
		return NULL;

	guard(raw_spinlock_irqsave)(&coresight_dev_lock);

	csdev = per_cpu(csdev_source, cpu);
	if (!csdev)
		return NULL;

	/*
	 * Holding a reference to the csdev->dev ensures that the
	 * coresight_device is live for the caller. The path building
	 * logic can safely either build a path to the sink or fail
	 * if the device is being unregistered (if there was a race).
	 * The caller can skip the "source" device, if no path could
	 * be built.
	 */
	get_device(&csdev->dev);

	return csdev;
}

void coresight_put_percpu_source_ref(struct coresight_device *csdev)
{
	if (!csdev || !coresight_is_percpu_source(csdev))
		return;

	guard(raw_spinlock_irqsave)(&coresight_dev_lock);

	/*
	 * TODO: coresight_device_release() is invoked to release resources when
	 * the device's refcount reaches zero. It then calls free_percpu(),
	 * which acquires pcpu_lock — a sleepable lock when PREEMPT_RT is
	 * enabled. Since the raw spinlock coresight_dev_lock is held, this can
	 * lead to a potential "scheduling while atomic" issue.
	 */
	put_device(&csdev->dev);
}

struct coresight_device *coresight_get_source(struct coresight_path *path)
{
	struct coresight_device *csdev;
	struct coresight_node *nd;

	if (!path)
		return NULL;

	nd = coresight_path_first_node(path);
	if (!nd)
		return NULL;

	csdev = nd->csdev;
	if (!coresight_is_device_source(csdev))
		return NULL;

	return csdev;
}

/**
 * coresight_blocks_source - checks whether the connection matches the source
 * of path if connection is bound to specific source.
 * @src:	The source device of the trace path
 * @conn:	The connection of one outport
 *
 * Return false if the connection doesn't have a source binded or source of the
 * path matches the source binds to connection.
 */
static bool coresight_blocks_source(struct coresight_device *src,
				    struct coresight_connection *conn)
{
	return conn->filter_src_fwnode && (conn->filter_src_dev != src);
}

static struct coresight_connection *
coresight_find_out_connection(struct coresight_device *csdev,
			      struct coresight_device *out_dev,
			      struct coresight_device *trace_src)
{
	int i;
	struct coresight_connection *conn;

	for (i = 0; i < csdev->pdata->nr_outconns; i++) {
		conn = csdev->pdata->out_conns[i];
		if (coresight_blocks_source(trace_src, conn))
			continue;
		if (conn->dest_dev == out_dev)
			return conn;
	}

	dev_err(&csdev->dev,
		"couldn't find output connection, csdev: %s, out_dev: %s\n",
		dev_name(&csdev->dev), dev_name(&out_dev->dev));

	return ERR_PTR(-ENODEV);
}

static u32 coresight_read_claim_tags_unlocked(struct coresight_device *csdev)
{
	return FIELD_GET(CORESIGHT_CLAIM_MASK,
			 csdev_access_relaxed_read32(&csdev->access, CORESIGHT_CLAIMCLR));
}

static void coresight_set_self_claim_tag_unlocked(struct coresight_device *csdev)
{
	csdev_access_relaxed_write32(&csdev->access, CORESIGHT_CLAIM_SELF_HOSTED,
				     CORESIGHT_CLAIMSET);
	isb();
}

void coresight_clear_self_claim_tag(struct csdev_access *csa)
{
	if (csa->io_mem)
		CS_UNLOCK(csa->base);
	coresight_clear_self_claim_tag_unlocked(csa);
	if (csa->io_mem)
		CS_LOCK(csa->base);
}
EXPORT_SYMBOL_GPL(coresight_clear_self_claim_tag);

void coresight_clear_self_claim_tag_unlocked(struct csdev_access *csa)
{
	csdev_access_relaxed_write32(csa, CORESIGHT_CLAIM_SELF_HOSTED,
				     CORESIGHT_CLAIMCLR);
	isb();
}
EXPORT_SYMBOL_GPL(coresight_clear_self_claim_tag_unlocked);

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
int coresight_claim_device_unlocked(struct coresight_device *csdev)
{
	int tag;
	struct csdev_access *csa;

	if (WARN_ON(!csdev))
		return -EINVAL;

	csa = &csdev->access;
	tag = coresight_read_claim_tags_unlocked(csdev);

	switch (tag) {
	case CORESIGHT_CLAIM_FREE:
		coresight_set_self_claim_tag_unlocked(csdev);
		if (coresight_read_claim_tags_unlocked(csdev) == CORESIGHT_CLAIM_SELF_HOSTED)
			return 0;

		/* There was a race setting the tag, clean up and fail */
		coresight_clear_self_claim_tag_unlocked(csa);
		dev_dbg(&csdev->dev, "Busy: Couldn't set self claim tag");
		return -EBUSY;

	case CORESIGHT_CLAIM_EXTERNAL:
		/* External debug is an expected state, so log and report BUSY */
		dev_dbg(&csdev->dev, "Busy: Claimed by external debugger");
		return -EBUSY;

	default:
	case CORESIGHT_CLAIM_SELF_HOSTED:
	case CORESIGHT_CLAIM_INVALID:
		/*
		 * Warn here because we clear a lingering self hosted tag
		 * on probe, so other tag combinations are impossible.
		 */
		dev_err_once(&csdev->dev, "Invalid claim tag state: %x", tag);
		return -EBUSY;
	}
}
EXPORT_SYMBOL_GPL(coresight_claim_device_unlocked);

int coresight_claim_device(struct coresight_device *csdev)
{
	int rc;

	if (WARN_ON(!csdev))
		return -EINVAL;

	CS_UNLOCK(csdev->access.base);
	rc = coresight_claim_device_unlocked(csdev);
	CS_LOCK(csdev->access.base);

	return rc;
}
EXPORT_SYMBOL_GPL(coresight_claim_device);

/*
 * coresight_disclaim_device_unlocked : Clear the claim tag for the device.
 * Called with CS_UNLOCKed for the component.
 */
void coresight_disclaim_device_unlocked(struct coresight_device *csdev)
{

	if (WARN_ON(!csdev))
		return;

	if (coresight_read_claim_tags_unlocked(csdev) == CORESIGHT_CLAIM_SELF_HOSTED)
		coresight_clear_self_claim_tag_unlocked(&csdev->access);
	else
		/*
		 * The external agent may have not honoured our claim
		 * and has manipulated it. Or something else has seriously
		 * gone wrong in our driver.
		 */
		dev_WARN_ONCE(&csdev->dev, 1, "External agent took claim tag");
}
EXPORT_SYMBOL_GPL(coresight_disclaim_device_unlocked);

void coresight_disclaim_device(struct coresight_device *csdev)
{
	if (WARN_ON(!csdev))
		return;

	CS_UNLOCK(csdev->access.base);
	coresight_disclaim_device_unlocked(csdev);
	CS_LOCK(csdev->access.base);
}
EXPORT_SYMBOL_GPL(coresight_disclaim_device);

/*
 * Add a helper as an output device. This function takes the @coresight_mutex
 * because it's assumed that it's called from the helper device, outside of the
 * core code where the mutex would already be held. Don't add new calls to this
 * from inside the core code, instead try to add the new helper to the DT and
 * ACPI where it will be picked up and linked automatically.
 */
void coresight_add_helper(struct coresight_device *csdev,
			  struct coresight_device *helper)
{
	int i;
	struct coresight_connection conn = {};
	struct coresight_connection *new_conn;

	mutex_lock(&coresight_mutex);
	conn.dest_fwnode = fwnode_handle_get(dev_fwnode(&helper->dev));
	conn.dest_dev = helper;
	conn.dest_port = conn.src_port = -1;
	conn.src_dev = csdev;

	/*
	 * Check for duplicates because this is called every time a helper
	 * device is re-loaded. Existing connections will get re-linked
	 * automatically.
	 */
	for (i = 0; i < csdev->pdata->nr_outconns; ++i)
		if (csdev->pdata->out_conns[i]->dest_fwnode == conn.dest_fwnode)
			goto unlock;

	new_conn = coresight_add_out_conn(csdev->dev.parent, csdev->pdata,
					  &conn);
	if (!IS_ERR(new_conn))
		coresight_add_in_conn(new_conn);

unlock:
	mutex_unlock(&coresight_mutex);
}
EXPORT_SYMBOL_GPL(coresight_add_helper);

static int coresight_enable_sink(struct coresight_device *csdev,
				 enum cs_mode mode,
				 struct coresight_path *path)
{
	return sink_ops(csdev)->enable(csdev, mode, path);
}

static void coresight_disable_sink(struct coresight_device *csdev)
{
	sink_ops(csdev)->disable(csdev);
}

static int coresight_enable_link(struct coresight_device *csdev,
				 struct coresight_device *parent,
				 struct coresight_device *child,
				 struct coresight_device *source)
{
	int link_subtype;
	struct coresight_connection *inconn, *outconn;

	if (!parent || !child)
		return -EINVAL;

	inconn = coresight_find_out_connection(parent, csdev, source);
	outconn = coresight_find_out_connection(csdev, child, source);
	link_subtype = csdev->subtype.link_subtype;

	if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_MERG && IS_ERR(inconn))
		return PTR_ERR(inconn);
	if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_SPLIT && IS_ERR(outconn))
		return PTR_ERR(outconn);

	return link_ops(csdev)->enable(csdev, inconn, outconn);
}

static void coresight_disable_link(struct coresight_device *csdev,
				   struct coresight_device *parent,
				   struct coresight_device *child,
				   struct coresight_device *source)
{
	struct coresight_connection *inconn, *outconn;

	if (!parent || !child)
		return;

	inconn = coresight_find_out_connection(parent, csdev, source);
	outconn = coresight_find_out_connection(csdev, child, source);

	link_ops(csdev)->disable(csdev, inconn, outconn);
}

static bool coresight_is_helper(struct coresight_device *csdev)
{
	return csdev->type == CORESIGHT_DEV_TYPE_HELPER;
}

static int coresight_enable_helper(struct coresight_device *csdev,
				   enum cs_mode mode,
				   struct coresight_path *path)
{
	return helper_ops(csdev)->enable(csdev, mode, path);
}

static void coresight_disable_helper(struct coresight_device *csdev,
				     struct coresight_path *path)
{
	helper_ops(csdev)->disable(csdev, path);
}

static void coresight_disable_helpers(struct coresight_device *csdev,
				      struct coresight_path *path)
{
	int i;
	struct coresight_device *helper;

	for (i = 0; i < csdev->pdata->nr_outconns; ++i) {
		helper = csdev->pdata->out_conns[i]->dest_dev;
		if (helper && coresight_is_helper(helper))
			coresight_disable_helper(helper, path);
	}
}

/*
 * coresight_enable_source() and coresight_disable_source() only enable and
 * disable the source, but do nothing for the associated helpers, which are
 * controlled as part of the path.
 */
int coresight_enable_source(struct coresight_device *csdev,
			    struct perf_event *event, enum cs_mode mode,
			    struct coresight_path *path)
{
	int ret;

	if (!coresight_is_device_source(csdev))
		return -EINVAL;

	ret = source_ops(csdev)->enable(csdev, event, mode, path);
	if (ret)
		return ret;

	/*
	 * Update the path pointer until after the source is enabled to avoid
	 * races where multiple paths attempt to enable the same source.
	 *
	 * Do not set the path pointer here for per-CPU sources; set it locally
	 * on the CPU instead. Otherwise, there is a window where the path is
	 * enabled but the pointer is not yet set, causing CPU PM notifiers to
	 * miss PM operations due to reading a NULL pointer.
	 */
	if (!coresight_is_percpu_source(csdev))
		csdev->path = path;

	return 0;
}

void coresight_disable_source(struct coresight_device *csdev, void *data)
{
	if (!coresight_is_device_source(csdev))
		return;

	if (!coresight_is_percpu_source(csdev))
		csdev->path = NULL;

	source_ops(csdev)->disable(csdev, data);
}
EXPORT_SYMBOL_GPL(coresight_disable_source);

void coresight_pause_source(struct coresight_device *csdev)
{
	if (!coresight_is_percpu_source(csdev))
		return;

	if (source_ops(csdev)->pause_perf)
		source_ops(csdev)->pause_perf(csdev);
}
EXPORT_SYMBOL_GPL(coresight_pause_source);

int coresight_resume_source(struct coresight_device *csdev)
{
	if (!coresight_is_percpu_source(csdev))
		return -EOPNOTSUPP;

	if (!source_ops(csdev)->resume_perf)
		return -EOPNOTSUPP;

	return source_ops(csdev)->resume_perf(csdev);
}
EXPORT_SYMBOL_GPL(coresight_resume_source);

/*
 * Callers must fetch nodes from the path and pass @from and @to to the path
 * enable/disable functions. Walk the path from @from to locate @to. If @to
 * is found, it indicates @from and @to are in order. Otherwise, they are out
 * of order.
 */
static bool coresight_path_nodes_in_order(struct coresight_path *path,
					  struct coresight_node *from,
					  struct coresight_node *to)
{
	struct coresight_node *nd;

	if (WARN_ON_ONCE(!from || !to))
		return false;

	nd = from;
	list_for_each_entry_from(nd, &path->path_list, link) {
		if (nd == to)
			return true;
	}

	return false;
}

static void coresight_disable_path_from_to(struct coresight_path *path,
					   struct coresight_node *from,
					   struct coresight_node *to)
{
	u32 type;
	struct coresight_device *csdev, *parent, *child;
	struct coresight_node *nd;

	if (!coresight_path_nodes_in_order(path, from, to))
		return;

	nd = from;
	list_for_each_entry_from(nd, &path->path_list, link) {
		csdev = nd->csdev;
		type = csdev->type;

		/*
		 * ETF devices are tricky... They can be a link or a sink,
		 * depending on how they are configured.  If an ETF has been
		 * selected as a sink it will be configured as a sink, otherwise
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
			break;
		case CORESIGHT_DEV_TYPE_LINK:
			parent = list_prev_entry(nd, link)->csdev;
			child = list_next_entry(nd, link)->csdev;
			coresight_disable_link(csdev, parent, child,
					       coresight_get_source(path));
			break;
		default:
			break;
		}

		/* Disable all helpers adjacent along the path last */
		coresight_disable_helpers(csdev, path);

		/* Iterate up to and including @to */
		if (nd == to)
			break;
	}
}

void coresight_disable_path(struct coresight_path *path)
{
	coresight_disable_path_from_to(path,
				       coresight_path_first_node(path),
				       coresight_path_last_node(path));
}
EXPORT_SYMBOL_GPL(coresight_disable_path);

static int coresight_enable_helpers(struct coresight_device *csdev,
				    enum cs_mode mode,
				    struct coresight_path *path)
{
	int i, ret = 0;
	struct coresight_device *helper;

	for (i = 0; i < csdev->pdata->nr_outconns; ++i) {
		helper = csdev->pdata->out_conns[i]->dest_dev;
		if (!helper || !coresight_is_helper(helper))
			continue;

		ret = coresight_enable_helper(helper, mode, path);
		if (ret)
			goto err;
	}

	return 0;

err:
	while (i--) {
		helper = csdev->pdata->out_conns[i]->dest_dev;
		if (helper && coresight_is_helper(helper))
			coresight_disable_helper(helper, path);
	}

	return ret;
}

static int coresight_enable_path_from_to(struct coresight_path *path,
					 enum cs_mode mode,
					 struct coresight_node *from,
					 struct coresight_node *to)
{
	int ret = 0;
	u32 type;
	struct coresight_node *nd;
	struct coresight_device *csdev, *parent, *child;

	if (!coresight_path_nodes_in_order(path, from, to))
		return -EINVAL;

	nd = to;
	list_for_each_entry_from_reverse(nd, &path->path_list, link) {
		csdev = nd->csdev;
		type = csdev->type;

		/* Enable all helpers adjacent to the path first */
		ret = coresight_enable_helpers(csdev, mode, path);
		if (ret)
			goto err_disable_path;
		/*
		 * ETF devices are tricky... They can be a link or a sink,
		 * depending on how they are configured.  If an ETF has been
		 * selected as a sink it will be configured as a sink, otherwise
		 * go ahead with the link configuration.
		 */
		if (type == CORESIGHT_DEV_TYPE_LINKSINK)
			type = (csdev == coresight_get_sink(path)) ?
						CORESIGHT_DEV_TYPE_SINK :
						CORESIGHT_DEV_TYPE_LINK;

		switch (type) {
		case CORESIGHT_DEV_TYPE_SINK:
			ret = coresight_enable_sink(csdev, mode, path);
			/*
			 * Sink is the first component turned on. If we
			 * failed to enable the sink, there are no components
			 * that need disabling. Disabling the path here
			 * would mean we could disrupt an existing session.
			 */
			if (ret) {
				coresight_disable_helpers(csdev, path);
				goto out;
			}
			break;
		case CORESIGHT_DEV_TYPE_SOURCE:
			/* sources are enabled from either sysFS or Perf */
			break;
		case CORESIGHT_DEV_TYPE_LINK:
			parent = list_prev_entry(nd, link)->csdev;
			child = list_next_entry(nd, link)->csdev;
			ret = coresight_enable_link(csdev, parent, child,
						    coresight_get_source(path));
			if (ret)
				goto err_disable_helpers;
			break;
		default:
			ret = -EINVAL;
			goto err_disable_helpers;
		}

		/* Iterate down to and including @from */
		if (nd == from)
			break;
	}

out:
	return ret;
err_disable_helpers:
	coresight_disable_helpers(csdev, path);
err_disable_path:
	/* No device is actually enabled */
	if (nd == to)
		goto out;

	/* Fetch the previous node, the last successfully enabled one */
	nd = list_next_entry(nd, link);
	coresight_disable_path_from_to(path, nd, to);
	goto out;
}

int coresight_enable_path(struct coresight_path *path, enum cs_mode mode)
{
	return coresight_enable_path_from_to(path, mode,
					     coresight_path_first_node(path),
					     coresight_path_last_node(path));
}

struct coresight_device *coresight_get_sink(struct coresight_path *path)
{
	struct coresight_device *csdev;
	struct coresight_node *nd;

	if (!path)
		return NULL;

	nd = coresight_path_last_node(path);
	if (!nd)
		return NULL;

	csdev = nd->csdev;
	if (csdev->type != CORESIGHT_DEV_TYPE_SINK &&
	    csdev->type != CORESIGHT_DEV_TYPE_LINKSINK)
		return NULL;

	return csdev;
}
EXPORT_SYMBOL_GPL(coresight_get_sink);

u32 coresight_get_sink_id(struct coresight_device *csdev)
{
	if (!csdev->ea)
		return 0;

	/*
	 * See function etm_perf_add_symlink_sink() to know where
	 * this comes from.
	 */
	return (u32) (unsigned long) csdev->ea->var;
}

static int coresight_sink_by_id(struct device *dev, const void *data)
{
	struct coresight_device *csdev = to_coresight_device(dev);

	if (csdev->type == CORESIGHT_DEV_TYPE_SINK ||
	    csdev->type == CORESIGHT_DEV_TYPE_LINKSINK) {
		if (coresight_get_sink_id(csdev) == *(u32 *)data)
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

/**
 * coresight_get_ref- Helper function to increase reference count to module
 * and device.
 *
 * @csdev: The coresight device to get a reference on.
 *
 * Return true in successful case and power up the device.
 * Return false when failed to get reference of module.
 */
static bool coresight_get_ref(struct coresight_device *csdev)
{
	struct device *dev = &csdev->dev;
	struct device *parent = csdev->dev.parent;
	struct device_driver *drv;

	/* Make sure csdev can't go away */
	get_device(dev);

	/* Make sure parent device can't go away */
	get_device(parent);

	/* Make sure the driver can't be removed */
	drv = parent->driver;
	if (!drv || !try_module_get(drv->owner))
		goto err_module;

	/* Make sure the device is powered on */
	pm_runtime_get_sync(parent);
	return true;

err_module:
	put_device(parent);
	put_device(dev);
	return false;
}

/**
 * coresight_put_ref- Helper function to decrease reference count to module
 * and device. Power off the device.
 *
 * @csdev: The coresight device to decrement a reference from.
 */
static void coresight_put_ref(struct coresight_device *csdev)
{
	struct device *dev = &csdev->dev;
	struct device *parent = csdev->dev.parent;
	struct device_driver *drv = parent->driver;

	pm_runtime_put(parent);
	if (drv)
		module_put(drv->owner);
	put_device(parent);
	put_device(dev);
}

/*
 * coresight_grab_device - Power up this device and any of the helper
 * devices connected to it for trace operation. Since the helper devices
 * don't appear on the trace path, they should be handled along with the
 * master device.
 */
static int coresight_grab_device(struct coresight_device *csdev)
{
	int i;

	for (i = 0; i < csdev->pdata->nr_outconns; i++) {
		struct coresight_device *child;

		child = csdev->pdata->out_conns[i]->dest_dev;
		if (child && coresight_is_helper(child))
			if (!coresight_get_ref(child))
				goto err;
	}
	if (coresight_get_ref(csdev))
		return 0;
err:
	for (i--; i >= 0; i--) {
		struct coresight_device *child;

		child = csdev->pdata->out_conns[i]->dest_dev;
		if (child && coresight_is_helper(child))
			coresight_put_ref(child);
	}
	return -ENODEV;
}

/*
 * coresight_drop_device - Release this device and any of the helper
 * devices connected to it.
 */
static void coresight_drop_device(struct coresight_device *csdev)
{
	int i;

	coresight_put_ref(csdev);
	for (i = 0; i < csdev->pdata->nr_outconns; i++) {
		struct coresight_device *child;

		child = csdev->pdata->out_conns[i]->dest_dev;
		if (child && coresight_is_helper(child))
			coresight_put_ref(child);
	}
}

/*
 * coresight device will read their existing or alloc a trace ID, if their trace_id
 * callback is set.
 *
 * Return 0 if the trace_id callback is not set.
 * Return the result of the trace_id callback if it is set. The return value
 * will be the trace_id if successful, and an error number if it fails.
 */
static int coresight_get_trace_id(struct coresight_device *csdev,
				  enum cs_mode mode,
				  struct coresight_device *sink)
{
	if (coresight_ops(csdev)->trace_id)
		return coresight_ops(csdev)->trace_id(csdev, mode, sink);

	return 0;
}

/*
 * Call this after creating the path and before enabling it. This leaves
 * the trace ID set on the path, or it remains 0 if it couldn't be assigned.
 */
int coresight_path_assign_trace_id(struct coresight_path *path,
				   enum cs_mode mode)
{
	struct coresight_device *sink = coresight_get_sink(path);
	struct coresight_node *nd;
	int trace_id;

	list_for_each_entry(nd, &path->path_list, link) {
		/* Assign a trace ID to the path for the first device that wants to do it */
		trace_id = coresight_get_trace_id(nd->csdev, mode, sink);

		/* 0 means the device has no ID assignment, so keep searching */
		if (trace_id == 0)
			continue;

		if (!IS_VALID_CS_TRACE_ID(trace_id))
			return -EINVAL;

		path->trace_id = trace_id;
		return 0;
	}

	return -EINVAL;
}

/**
 * _coresight_build_path - recursively build a path from a @csdev to a sink.
 * @csdev:	The device to start from.
 * @source:	The trace source device of the path.
 * @sink:	The final sink we want in this path.
 * @path:	The list to add devices to.
 *
 * The tree of Coresight device is traversed until @sink is found.
 * From there the sink is added to the list along with all the devices that led
 * to that point - the end result is a list from source to sink. In that list
 * the source is the first device and the sink the last one.
 */
static int _coresight_build_path(struct coresight_device *csdev,
				 struct coresight_device *source,
				 struct coresight_device *sink,
				 struct coresight_path *path)
{
	int i, ret;
	bool found = false;
	struct coresight_node *node;

	/* The sink has been found.  Enqueue the element */
	if (csdev == sink)
		goto out;

	if (coresight_is_percpu_source(csdev) && coresight_is_percpu_sink(sink) &&
	    sink == per_cpu(csdev_sink, csdev->cpu)) {
		if (_coresight_build_path(sink, source, sink, path) == 0) {
			found = true;
			goto out;
		}
	}

	/* Not a sink - recursively explore each port found on this element */
	for (i = 0; i < csdev->pdata->nr_outconns; i++) {
		struct coresight_device *child_dev;

		child_dev = csdev->pdata->out_conns[i]->dest_dev;

		if (coresight_blocks_source(source, csdev->pdata->out_conns[i]))
			continue;

		if (child_dev &&
		    _coresight_build_path(child_dev, source, sink, path) == 0) {
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
	ret = coresight_grab_device(csdev);
	if (ret)
		return ret;

	node = kzalloc_obj(struct coresight_node);
	if (!node)
		return -ENOMEM;

	node->csdev = csdev;
	list_add(&node->link, &path->path_list);

	return 0;
}

struct coresight_path *coresight_build_path(struct coresight_device *source,
				       struct coresight_device *sink)
{
	struct coresight_path *path;
	int rc;

	if (!sink)
		return ERR_PTR(-EINVAL);

	path = kzalloc_obj(struct coresight_path);
	if (!path)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&path->path_list);

	rc = _coresight_build_path(source, source, sink, path);
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
void coresight_release_path(struct coresight_path *path)
{
	struct coresight_device *csdev;
	struct coresight_node *nd, *next;

	list_for_each_entry_safe(nd, next, &path->path_list, link) {
		csdev = nd->csdev;

		coresight_drop_device(csdev);
		list_del(&nd->link);
		kfree(nd);
	}

	kfree(path);
}

/* return true if the device is a suitable type for a default sink */
static bool coresight_is_def_sink_type(struct coresight_device *csdev)
{
	/* sink & correct subtype */
	if (((csdev->type == CORESIGHT_DEV_TYPE_SINK) ||
	     (csdev->type == CORESIGHT_DEV_TYPE_LINKSINK)) &&
	    (csdev->subtype.sink_subtype >= CORESIGHT_DEV_SUBTYPE_SINK_BUFFER))
		return true;
	return false;
}

/**
 * coresight_select_best_sink - return the best sink for use as default from
 * the two provided.
 *
 * @sink:	current best sink.
 * @depth:      search depth where current sink was found.
 * @new_sink:	new sink for comparison with current sink.
 * @new_depth:  search depth where new sink was found.
 *
 * Sinks prioritised according to coresight_dev_subtype_sink, with only
 * subtypes CORESIGHT_DEV_SUBTYPE_SINK_BUFFER or higher being used.
 *
 * Where two sinks of equal priority are found, the sink closest to the
 * source is used (smallest search depth).
 *
 * return @new_sink & update @depth if better than @sink, else return @sink.
 */
static struct coresight_device *
coresight_select_best_sink(struct coresight_device *sink, int *depth,
			   struct coresight_device *new_sink, int new_depth)
{
	bool update = false;

	if (!sink) {
		/* first found at this level */
		update = true;
	} else if (new_sink->subtype.sink_subtype >
		   sink->subtype.sink_subtype) {
		/* found better sink */
		update = true;
	} else if ((new_sink->subtype.sink_subtype ==
		    sink->subtype.sink_subtype) &&
		   (*depth > new_depth)) {
		/* found same but closer sink */
		update = true;
	}

	if (update)
		*depth = new_depth;
	return update ? new_sink : sink;
}

/**
 * coresight_find_sink - recursive function to walk trace connections from
 * source to find a suitable default sink.
 *
 * @csdev: source / current device to check.
 * @depth: [in] search depth of calling dev, [out] depth of found sink.
 *
 * This will walk the connection path from a source (ETM) till a suitable
 * sink is encountered and return that sink to the original caller.
 *
 * If current device is a plain sink return that & depth, otherwise recursively
 * call child connections looking for a sink. Select best possible using
 * coresight_select_best_sink.
 *
 * return best sink found, or NULL if not found at this node or child nodes.
 */
static struct coresight_device *
coresight_find_sink(struct coresight_device *csdev, int *depth)
{
	int i, curr_depth = *depth + 1, found_depth = 0;
	struct coresight_device *found_sink = NULL;

	if (coresight_is_def_sink_type(csdev)) {
		found_depth = curr_depth;
		found_sink = csdev;
		if (csdev->type == CORESIGHT_DEV_TYPE_SINK)
			goto return_def_sink;
		/* look past LINKSINK for something better */
	}

	/*
	 * Not a sink we want - or possible child sink may be better.
	 * recursively explore each port found on this element.
	 */
	for (i = 0; i < csdev->pdata->nr_outconns; i++) {
		struct coresight_device *child_dev, *sink = NULL;
		int child_depth = curr_depth;

		child_dev = csdev->pdata->out_conns[i]->dest_dev;
		if (child_dev)
			sink = coresight_find_sink(child_dev, &child_depth);

		if (sink)
			found_sink = coresight_select_best_sink(found_sink,
								&found_depth,
								sink,
								child_depth);
	}

return_def_sink:
	/* return found sink and depth */
	if (found_sink)
		*depth = found_depth;
	return found_sink;
}

/**
 * coresight_find_default_sink: Find a sink suitable for use as a
 * default sink.
 *
 * @csdev: starting source to find a connected sink.
 *
 * Walks connections graph looking for a suitable sink to enable for the
 * supplied source. Uses CoreSight device subtypes and distance from source
 * to select the best sink.
 *
 * If a sink is found, then the default sink for this device is set and
 * will be automatically used in future.
 *
 * Used in cases where the CoreSight user (perf / sysfs) has not selected a
 * sink.
 */
struct coresight_device *
coresight_find_default_sink(struct coresight_device *csdev)
{
	int depth = 0;

	/* look for a default sink if we have not found for this device */
	if (!csdev->def_sink) {
		if (coresight_is_percpu_source(csdev))
			csdev->def_sink = per_cpu(csdev_sink, csdev->cpu);
		if (!csdev->def_sink)
			csdev->def_sink = coresight_find_sink(csdev, &depth);
	}
	return csdev->def_sink;
}
EXPORT_SYMBOL_GPL(coresight_find_default_sink);

static int coresight_remove_sink_ref(struct device *dev, void *data)
{
	struct coresight_device *sink = data;
	struct coresight_device *source = to_coresight_device(dev);

	if (source->def_sink == sink)
		source->def_sink = NULL;
	return 0;
}

/**
 * coresight_clear_default_sink: Remove all default sink references to the
 * supplied sink.
 *
 * If supplied device is a sink, then check all the bus devices and clear
 * out all the references to this sink from the coresight_device def_sink
 * parameter.
 *
 * @csdev: coresight sink - remove references to this from all sources.
 */
static void coresight_clear_default_sink(struct coresight_device *csdev)
{
	if ((csdev->type == CORESIGHT_DEV_TYPE_SINK) ||
	    (csdev->type == CORESIGHT_DEV_TYPE_LINKSINK)) {
		bus_for_each_dev(&coresight_bustype, NULL, csdev,
				 coresight_remove_sink_ref);
	}
}

static void coresight_device_release(struct device *dev)
{
	struct coresight_device *csdev = to_coresight_device(dev);

	fwnode_handle_put(csdev->dev.fwnode);
	free_percpu(csdev->perf_sink_id_map.cpu_map);
	kfree(csdev);
}

static int coresight_orphan_match(struct device *dev, void *data)
{
	int i, ret = 0;
	bool still_orphan = false;
	struct coresight_device *dst_csdev = data;
	struct coresight_device *src_csdev = to_coresight_device(dev);
	struct coresight_connection *conn;
	bool fixup_self = (src_csdev == dst_csdev);

	/* Move on to another component if no connection is orphan */
	if (!src_csdev->orphan)
		return 0;
	/*
	 * Circle through all the connections of that component.  If we find
	 * an orphan connection whose name matches @dst_csdev, link it.
	 */
	for (i = 0; i < src_csdev->pdata->nr_outconns; i++) {
		conn = src_csdev->pdata->out_conns[i];

		/* Fix filter source device before skip the port */
		if (conn->filter_src_fwnode && !conn->filter_src_dev) {
			if (dst_csdev &&
			    (conn->filter_src_fwnode == dst_csdev->dev.fwnode) &&
			    !WARN_ON_ONCE(!coresight_is_device_source(dst_csdev)))
				conn->filter_src_dev = dst_csdev;
			else
				still_orphan = true;
		}

		/* Skip the port if it's already connected. */
		if (conn->dest_dev)
			continue;

		/*
		 * If we are at the "new" device, which triggered this search,
		 * we must find the remote device from the fwnode in the
		 * connection.
		 */
		if (fixup_self)
			dst_csdev = coresight_find_csdev_by_fwnode(
				conn->dest_fwnode);

		/* Does it match this newly added device? */
		if (dst_csdev && conn->dest_fwnode == dst_csdev->dev.fwnode) {
			ret = coresight_make_links(src_csdev, conn, dst_csdev);
			if (ret)
				return ret;

			/*
			 * Install the device connection. This also indicates that
			 * the links are operational on both ends.
			 */
			conn->dest_dev = dst_csdev;
			conn->src_dev = src_csdev;

			ret = coresight_add_in_conn(conn);
			if (ret)
				return ret;
		} else {
			/* This component still has an orphan */
			still_orphan = true;
		}
	}

	src_csdev->orphan = still_orphan;

	/*
	 * Returning '0' in case we didn't encounter any error,
	 * ensures that all known component on the bus will be checked.
	 */
	return 0;
}

static int coresight_fixup_orphan_conns(struct coresight_device *csdev)
{
	return bus_for_each_dev(&coresight_bustype, NULL,
			 csdev, coresight_orphan_match);
}

static int coresight_clear_filter_source(struct device *dev, void *data)
{
	int i;
	struct coresight_device *source = data;
	struct coresight_device *csdev = to_coresight_device(dev);

	for (i = 0; i < csdev->pdata->nr_outconns; ++i) {
		if (csdev->pdata->out_conns[i]->filter_src_dev == source)
			csdev->pdata->out_conns[i]->filter_src_dev = NULL;
	}
	return 0;
}

static void coresight_remove_conns(struct coresight_device *csdev)
{
	int i, j;
	struct coresight_connection *conn;

	if (coresight_is_device_source(csdev))
		bus_for_each_dev(&coresight_bustype, NULL, csdev,
				 coresight_clear_filter_source);

	for (i = 0; i < csdev->pdata->nr_outconns; i++) {
		conn = csdev->pdata->out_conns[i];
		if (conn->filter_src_fwnode) {
			conn->filter_src_dev = NULL;
			fwnode_handle_put(conn->filter_src_fwnode);
		}

		if (!conn->dest_dev)
			continue;

		/* Remove sysfs links for the output connection */
		coresight_remove_links(csdev, conn);

		/*
		 * Remove the input connection references from the destination
		 * device for each output connection.
		 */
		for (j = 0; j < conn->dest_dev->pdata->nr_inconns; ++j)
			if (conn->dest_dev->pdata->in_conns[j] == conn) {
				conn->dest_dev->pdata->in_conns[j] = NULL;
				break;
			}
	}

	/*
	 * For all input connections, remove references to this device.
	 * Connection objects are shared so modifying this device's input
	 * connections affects the other device's output connection.
	 */
	for (i = 0; i < csdev->pdata->nr_inconns; ++i) {
		conn = csdev->pdata->in_conns[i];
		/* Input conns array is sparse */
		if (!conn)
			continue;

		conn->src_dev->orphan = true;
		coresight_remove_links(conn->src_dev, conn);
		conn->dest_dev = NULL;
	}

	coresight_remove_conns_sysfs_group(csdev);
}

/**
 * coresight_timeout_action - loop until a bit has changed to a specific register
 *                  state, with a callback after every trial.
 * @csa: coresight device access for the device
 * @offset: Offset of the register from the base of the device.
 * @position: the position of the bit of interest.
 * @value: the value the bit should have.
 * @cb: Call back after each trial.
 *
 * Return: 0 as soon as the bit has taken the desired state or -EAGAIN if
 * TIMEOUT_US has elapsed, which ever happens first.
 */
int coresight_timeout_action(struct csdev_access *csa, u32 offset,
		      int position, int value,
			  coresight_timeout_cb_t cb)
{
	int i;
	u32 val;

	for (i = TIMEOUT_US; i > 0; i--) {
		val = csdev_access_read32(csa, offset);
		/* waiting on the bit to go from 0 to 1 */
		if (value) {
			if (val & BIT(position))
				return 0;
		/* waiting on the bit to go from 1 to 0 */
		} else {
			if (!(val & BIT(position)))
				return 0;
		}
		if (cb)
			cb(csa, offset, position, value);
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
EXPORT_SYMBOL_GPL(coresight_timeout_action);

int coresight_timeout(struct csdev_access *csa, u32 offset,
		      int position, int value)
{
	return coresight_timeout_action(csa, offset, position, value, NULL);
}
EXPORT_SYMBOL_GPL(coresight_timeout);

u32 coresight_relaxed_read32(struct coresight_device *csdev, u32 offset)
{
	return csdev_access_relaxed_read32(&csdev->access, offset);
}

u32 coresight_read32(struct coresight_device *csdev, u32 offset)
{
	return csdev_access_read32(&csdev->access, offset);
}

void coresight_relaxed_write32(struct coresight_device *csdev,
			       u32 val, u32 offset)
{
	csdev_access_relaxed_write32(&csdev->access, val, offset);
}

void coresight_write32(struct coresight_device *csdev, u32 val, u32 offset)
{
	csdev_access_write32(&csdev->access, val, offset);
}

u64 coresight_relaxed_read64(struct coresight_device *csdev, u32 offset)
{
	return csdev_access_relaxed_read64(&csdev->access, offset);
}

u64 coresight_read64(struct coresight_device *csdev, u32 offset)
{
	return csdev_access_read64(&csdev->access, offset);
}

void coresight_relaxed_write64(struct coresight_device *csdev,
			       u64 val, u32 offset)
{
	csdev_access_relaxed_write64(&csdev->access, val, offset);
}

void coresight_write64(struct coresight_device *csdev, u64 val, u32 offset)
{
	csdev_access_write64(&csdev->access, val, offset);
}

/*
 * coresight_release_platform_data: Release references to the devices connected
 * to the output port of this device.
 */
void coresight_release_platform_data(struct device *dev,
				     struct coresight_platform_data *pdata)
{
	int i;
	struct coresight_connection **conns = pdata->out_conns;

	for (i = 0; i < pdata->nr_outconns; i++) {
		/*
		 * Drop the refcount and clear the handle as this device
		 * is going away
		 */
		fwnode_handle_put(conns[i]->dest_fwnode);
		conns[i]->dest_fwnode = NULL;
		devm_kfree(dev, conns[i]);
	}
	devm_kfree(dev, pdata->out_conns);
	devm_kfree(dev, pdata->in_conns);
	devm_kfree(dev, pdata);
}

static struct coresight_device *
coresight_init_device(struct coresight_desc *desc)
{
	struct coresight_device *csdev;

	csdev = kzalloc_obj(*csdev);
	if (!csdev)
		return ERR_PTR(-ENOMEM);

	csdev->pdata = desc->pdata;
	csdev->type = desc->type;
	csdev->subtype = desc->subtype;
	csdev->ops = desc->ops;
	csdev->access = desc->access;
	csdev->orphan = true;

	if (desc->flags & CORESIGHT_DESC_CPU_BOUND) {
		csdev->cpu = desc->cpu;
	} else {
		/* A per-CPU source or sink must set CPU_BOUND flag */
		if (coresight_is_percpu_source(csdev) ||
		    coresight_is_percpu_sink(csdev)) {
			kfree(csdev);
			return ERR_PTR(-EINVAL);
		}

		csdev->cpu = -1;
	}

	csdev->dev.type = &coresight_dev_type[desc->type];
	csdev->dev.groups = desc->groups;
	csdev->dev.parent = desc->dev;
	csdev->dev.release = coresight_device_release;
	csdev->dev.bus = &coresight_bustype;

	return csdev;
}

struct coresight_device *coresight_register(struct coresight_desc *desc)
{
	int ret;
	struct coresight_device *csdev;
	bool registered = false;

	csdev = coresight_init_device(desc);
	if (IS_ERR(csdev)) {
		ret = PTR_ERR(csdev);
		goto err_out;
	}

	if (csdev->type == CORESIGHT_DEV_TYPE_SINK ||
	    csdev->type == CORESIGHT_DEV_TYPE_LINKSINK) {
		raw_spin_lock_init(&csdev->perf_sink_id_map.lock);
		csdev->perf_sink_id_map.cpu_map = alloc_percpu(atomic_t);
		if (!csdev->perf_sink_id_map.cpu_map) {
			kfree(csdev);
			ret = -ENOMEM;
			goto err_out;
		}
	}

	/*
	 * Hold the reference to our parent device. This will be
	 * dropped only in coresight_device_release().
	 */
	csdev->dev.fwnode = fwnode_handle_get(dev_fwnode(desc->dev));
	dev_set_name(&csdev->dev, "%s", desc->name);

	/*
	 * Make sure the device registration and the connection fixup
	 * are synchronised, so that we don't see uninitialised devices
	 * on the coresight bus while trying to resolve the connections.
	 */
	mutex_lock(&coresight_mutex);

	ret = device_register(&csdev->dev);
	if (ret) {
		put_device(&csdev->dev);
		/*
		 * All resources are free'd explicitly via
		 * coresight_device_release(), triggered from put_device().
		 */
		goto out_unlock;
	}

	/* Device is now registered */
	registered = true;

	ret = etm_perf_add_symlink_sink(csdev);
	if (ret && ret != -EOPNOTSUPP)
		goto out_unlock;

	ret = coresight_create_conns_sysfs_group(csdev);
	if (ret)
		goto out_unlock;

	ret = coresight_fixup_orphan_conns(csdev);
	if (ret)
		goto out_unlock;

	coresight_set_percpu_source(csdev);
	mutex_unlock(&coresight_mutex);

	if (cti_assoc_ops && cti_assoc_ops->add)
		cti_assoc_ops->add(csdev);

	return csdev;

out_unlock:
	mutex_unlock(&coresight_mutex);

	/* Unregister the device if needed */
	if (registered) {
		coresight_unregister(csdev);
		return ERR_PTR(ret);
	}

err_out:
	coresight_release_platform_data(desc->dev, desc->pdata);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(coresight_register);

void coresight_unregister(struct coresight_device *csdev)
{
	/* Remove references of that device in the topology */
	if (cti_assoc_ops && cti_assoc_ops->remove)
		cti_assoc_ops->remove(csdev);

	mutex_lock(&coresight_mutex);
	coresight_clear_percpu_source(csdev);
	etm_perf_del_symlink_sink(csdev);
	coresight_remove_conns(csdev);
	coresight_clear_default_sink(csdev);
	coresight_release_platform_data(csdev->dev.parent, csdev->pdata);
	device_unregister(&csdev->dev);
	mutex_unlock(&coresight_mutex);
}
EXPORT_SYMBOL_GPL(coresight_unregister);

static struct coresight_dev_list *
coresight_allocate_device_list(const char *prefix)
{
	struct coresight_dev_list *list;

	/* Check if have already allocated */
	list_for_each_entry(list, &coresight_dev_idx_list, node) {
		if (!strcmp(list->pfx, prefix))
			return list;
	}

	list = kzalloc(sizeof(*list), GFP_KERNEL);
	if (!list)
		return NULL;

	list->pfx = kstrdup(prefix, GFP_KERNEL);
	if (!list->pfx) {
		kfree(list);
		return NULL;
	}

	list_add(&list->node, &coresight_dev_idx_list);
	return list;
}

static int coresight_allocate_device_idx(struct coresight_dev_list *list,
					 struct device *dev)
{
	struct fwnode_handle **fwnode_list;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	int idx;

	for (idx = 0; idx < list->nr_idx; idx++)
		if (list->fwnode_list[idx] == fwnode)
			return idx;

	/* Make space for the new entry */
	idx = list->nr_idx;
	fwnode_list = krealloc_array(list->fwnode_list,
				     idx + 1, sizeof(*list->fwnode_list),
				     GFP_KERNEL);
	if (!fwnode_list)
		return -ENOMEM;

	fwnode_list[idx] = fwnode;
	list->fwnode_list = fwnode_list;
	list->nr_idx = idx + 1;

	return idx;
}

static bool coresight_compare_type(enum coresight_dev_type type_a,
				   union coresight_dev_subtype subtype_a,
				   enum coresight_dev_type type_b,
				   union coresight_dev_subtype subtype_b)
{
	if (type_a != type_b)
		return false;

	switch (type_a) {
	case CORESIGHT_DEV_TYPE_SINK:
		return subtype_a.sink_subtype == subtype_b.sink_subtype;
	case CORESIGHT_DEV_TYPE_LINK:
		return subtype_a.link_subtype == subtype_b.link_subtype;
	case CORESIGHT_DEV_TYPE_LINKSINK:
		return subtype_a.link_subtype == subtype_b.link_subtype &&
		       subtype_a.sink_subtype == subtype_b.sink_subtype;
	case CORESIGHT_DEV_TYPE_SOURCE:
		return subtype_a.source_subtype == subtype_b.source_subtype;
	case CORESIGHT_DEV_TYPE_HELPER:
		return subtype_a.helper_subtype == subtype_b.helper_subtype;
	default:
		return false;
	}
}

struct coresight_device *
coresight_find_input_type(struct coresight_platform_data *pdata,
			  enum coresight_dev_type type,
			  union coresight_dev_subtype subtype)
{
	int i;
	struct coresight_connection *conn;

	for (i = 0; i < pdata->nr_inconns; ++i) {
		conn = pdata->in_conns[i];
		if (conn &&
		    coresight_compare_type(type, subtype, conn->src_dev->type,
					   conn->src_dev->subtype))
			return conn->src_dev;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(coresight_find_input_type);

struct coresight_device *
coresight_find_output_type(struct coresight_platform_data *pdata,
			   enum coresight_dev_type type,
			   union coresight_dev_subtype subtype)
{
	int i;
	struct coresight_connection *conn;

	for (i = 0; i < pdata->nr_outconns; ++i) {
		conn = pdata->out_conns[i];
		if (conn->dest_dev &&
		    coresight_compare_type(type, subtype, conn->dest_dev->type,
					   conn->dest_dev->subtype))
			return conn->dest_dev;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(coresight_find_output_type);

bool coresight_loses_context_with_cpu(struct device *dev)
{
	return fwnode_property_present(dev_fwnode(dev),
				       "arm,coresight-loses-context-with-cpu");
}
EXPORT_SYMBOL_GPL(coresight_loses_context_with_cpu);

/*
 * coresight_alloc_device_name - Get an index for a given device in the list
 * specific to a driver (presented by the prefix string). An index is allocated
 * for a device and is tracked with the fwnode_handle to prevent allocating
 * duplicate indices for the same device (e.g, if we defer probing of
 * a device due to dependencies), in case the index is requested again.
 */
char *coresight_alloc_device_name(const char *prefix, struct device *dev)
{
	struct coresight_dev_list *list;
	char *name = NULL;
	int idx;

	mutex_lock(&coresight_mutex);

	list = coresight_allocate_device_list(prefix);
	if (!list)
		goto done;

	idx = coresight_allocate_device_idx(list, dev);

	/*
	 * If index allocation fails, the device list is not released here;
	 * it is instead freed later by coresight_release_device_list() when
	 * the coresight_core module is unloaded.
	 */
	if (idx < 0)
		goto done;

	name = devm_kasprintf(dev, GFP_KERNEL, "%s%d", list->pfx, idx);
done:
	mutex_unlock(&coresight_mutex);
	return name;
}
EXPORT_SYMBOL_GPL(coresight_alloc_device_name);

static void coresight_release_device_list(void)
{
	struct coresight_dev_list *list, *next;
	int i;

	/*
	 * Here is no need to take coresight_mutex; this is during core module
	 * unloading, no race condition with other modules.
	 */

	list_for_each_entry_safe(list, next, &coresight_dev_idx_list, node) {
		for (i = 0; i < list->nr_idx; i++)
			list->fwnode_list[i] = NULL;
		list->nr_idx = 0;
		list_del(&list->node);

		kfree(list->pfx);
		kfree(list->fwnode_list);
		kfree(list);
	}
}

static struct coresight_path *coresight_cpu_get_active_path(enum cs_mode mode)
{
	struct coresight_device *source;
	bool is_active = false;

	source = coresight_get_percpu_source_ref(smp_processor_id());
	if (!source)
		return NULL;

	if (coresight_get_mode(source) & mode)
		is_active = true;

	coresight_put_percpu_source_ref(source);

	/*
	 * It is expected to run in atomic context or with the CPU lock held for
	 * sysfs mode, so it cannot be preempted to disable the path. Here
	 * returns the active path pointer without concern that its state may
	 * change. Since the build path has taken a reference on the component,
	 * the path can be safely used by the caller.
	 */
	return is_active ? source->path : NULL;
}

/* Return: 1 if PM is required, 0 if skip, or a negative error */
static int coresight_pm_is_needed(struct coresight_path *path)
{
	struct coresight_device *source, *sink;

	if (this_cpu_read(percpu_pm_failed))
		return -EIO;

	if (!path)
		return 0;

	source = coresight_get_source(path);
	sink = coresight_get_sink(path);
	if (!source || !sink)
		return 0;

	/* pm_save_disable() and pm_restore_enable() must be paired */
	if (coresight_ops(source)->pm_save_disable &&
	    coresight_ops(source)->pm_restore_enable)
		return 1;

	/*
	 * It is not permitted that the source has no callbacks while the sink
	 * does, as the sink cannot be disabled without disabling the source,
	 * which may lead to lockups. Fix this by enabling self-hosted PM
	 * mode for ETM (see etm4_probe()).
	 */
	if (coresight_ops(sink)->pm_save_disable &&
	    coresight_ops(sink)->pm_restore_enable) {
		pr_warn_once("coresight PM failed: source has no PM callbacks; "
			     "cannot safely control sink\n");
		return -EINVAL;
	}

	return 0;
}

static int coresight_pm_device_save(struct coresight_device *csdev)
{
	if (!csdev || !coresight_ops(csdev)->pm_save_disable)
		return 0;

	return coresight_ops(csdev)->pm_save_disable(csdev);
}

static void coresight_pm_device_restore(struct coresight_device *csdev)
{
	if (!csdev || !coresight_ops(csdev)->pm_restore_enable)
		return;

	coresight_ops(csdev)->pm_restore_enable(csdev);
}

static int coresight_pm_save(struct coresight_path *path)
{
	struct coresight_device *source = coresight_get_source(path);
	struct coresight_node *from, *to;
	int ret;

	ret = coresight_pm_device_save(source);
	if (ret)
		return ret;

	from = coresight_path_first_node(path);
	/* Disable up to the node before sink */
	to = list_prev_entry(coresight_path_last_node(path), link);
	coresight_disable_path_from_to(path, from, to);

	/*
	 * Save the sink. Most sinks do not implement a save callback to avoid
	 * latency from memory copying. We assume the sink's save and restore
	 * always succeed.
	 */
	coresight_pm_device_save(coresight_get_sink(path));
	return 0;
}

static void coresight_pm_restore(struct coresight_path *path)
{
	struct coresight_device *source = coresight_get_source(path);
	struct coresight_device *sink = coresight_get_sink(path);
	struct coresight_node *from, *to;
	int ret;

	coresight_pm_device_restore(sink);

	from = coresight_path_first_node(path);
	/* Enable up to the node before sink */
	to = list_prev_entry(coresight_path_last_node(path), link);
	ret = coresight_enable_path_from_to(path, coresight_get_mode(source),
					    from, to);
	if (ret)
		goto path_failed;

	coresight_pm_device_restore(source);
	return;

path_failed:
	coresight_pm_device_save(sink);

	pr_err("Failed in coresight PM restore on CPU%d: %d\n",
	       smp_processor_id(), ret);

	/*
	 * Once PM fails on a CPU, set percpu_pm_failed and leave it set until
	 * reboot. This prevents repeated partial transitions during idle
	 * entry and exit.
	 */
	this_cpu_write(percpu_pm_failed, true);
}

static int coresight_cpu_pm_notify(struct notifier_block *nb, unsigned long cmd,
				   void *v)
{
	struct coresight_path *path =
		coresight_cpu_get_active_path(CS_MODE_SYSFS | CS_MODE_PERF);
	int ret;

	ret = coresight_pm_is_needed(path);
	if (ret <= 0)
		return ret ? NOTIFY_BAD : NOTIFY_DONE;

	switch (cmd) {
	case CPU_PM_ENTER:
		if (coresight_pm_save(path))
			return NOTIFY_BAD;
		break;
	case CPU_PM_EXIT:
	case CPU_PM_ENTER_FAILED:
		coresight_pm_restore(path);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static struct notifier_block coresight_cpu_pm_nb = {
	.notifier_call = coresight_cpu_pm_notify,
};

static int coresight_dying_cpu(unsigned int cpu)
{
	struct coresight_path *path;

	/*
	 * The perf event layer will disable PMU events in the CPU
	 * hotplug. Here only handles SYSFS case.
	 */
	path = coresight_cpu_get_active_path(CS_MODE_SYSFS);
	if (!path)
		return 0;

	coresight_disable_sysfs(coresight_get_source(path));
	return 0;
}

static int __init coresight_pm_setup(void)
{
	int ret;

	ret = cpu_pm_register_notifier(&coresight_cpu_pm_nb);
	if (ret)
		return ret;

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ARM_CORESIGHT_ONLINE,
					"arm/coresight-core:dying",
					NULL, coresight_dying_cpu);
	if (ret)
		cpu_pm_unregister_notifier(&coresight_cpu_pm_nb);

	return ret;
}

static void coresight_pm_cleanup(void)
{
	cpuhp_remove_state_nocalls(CPUHP_AP_ARM_CORESIGHT_ONLINE);
	cpu_pm_unregister_notifier(&coresight_cpu_pm_nb);
}

const struct bus_type coresight_bustype = {
	.name	= "coresight",
};

static int coresight_panic_sync(struct device *dev, void *data)
{
	int mode;
	struct coresight_device *csdev;

	/* Run through panic sync handlers for all enabled devices */
	csdev = container_of(dev, struct coresight_device, dev);
	mode = coresight_get_mode(csdev);

	if ((mode == CS_MODE_SYSFS) || (mode == CS_MODE_PERF)) {
		if (panic_ops(csdev))
			panic_ops(csdev)->sync(csdev);
	}

	return 0;
}

static int coresight_panic_cb(struct notifier_block *self,
			       unsigned long v, void *p)
{
	bus_for_each_dev(&coresight_bustype, NULL, NULL,
				 coresight_panic_sync);

	return 0;
}

static struct notifier_block coresight_notifier = {
	.notifier_call = coresight_panic_cb,
};

static int __init coresight_init(void)
{
	int ret;

	ret = bus_register(&coresight_bustype);
	if (ret)
		return ret;

	ret = etm_perf_init();
	if (ret)
		goto exit_bus_unregister;

	/* Register function to be called for panic */
	ret = atomic_notifier_chain_register(&panic_notifier_list,
					     &coresight_notifier);
	if (ret)
		goto exit_perf;

	/* initialise the coresight syscfg API */
	ret = cscfg_init();
	if (ret)
		goto exit_notifier;

	ret = coresight_pm_setup();
	if (!ret)
		return 0;

	cscfg_exit();
exit_notifier:
	atomic_notifier_chain_unregister(&panic_notifier_list,
					     &coresight_notifier);
exit_perf:
	etm_perf_exit();
exit_bus_unregister:
	bus_unregister(&coresight_bustype);
	return ret;
}

static void __exit coresight_exit(void)
{
	coresight_pm_cleanup();
	cscfg_exit();
	atomic_notifier_chain_unregister(&panic_notifier_list,
					     &coresight_notifier);
	etm_perf_exit();
	bus_unregister(&coresight_bustype);
	coresight_release_device_list();
}

module_init(coresight_init);
module_exit(coresight_exit);

int coresight_init_driver_with_owner(const char *drv, struct amba_driver *amba_drv,
				     struct platform_driver *pdev_drv, struct module *owner,
				     const char *mod_name)
{
	int ret;

	ret = __amba_driver_register(amba_drv, owner);
	if (ret) {
		pr_err("%s: error registering AMBA driver\n", drv);
		return ret;
	}

	ret = __platform_driver_register(pdev_drv, owner, mod_name);
	if (!ret)
		return 0;

	pr_err("%s: error registering platform driver\n", drv);
	amba_driver_unregister(amba_drv);
	return ret;
}
EXPORT_SYMBOL_GPL(coresight_init_driver_with_owner);

void coresight_remove_driver(struct amba_driver *amba_drv,
			     struct platform_driver *pdev_drv)
{
	amba_driver_unregister(amba_drv);
	platform_driver_unregister(pdev_drv);
}
EXPORT_SYMBOL_GPL(coresight_remove_driver);

int coresight_etm_get_trace_id(struct coresight_device *csdev, enum cs_mode mode,
			       struct coresight_device *sink)
{
	int cpu, trace_id;

	if (csdev->type != CORESIGHT_DEV_TYPE_SOURCE)
		return -EINVAL;

	cpu = csdev->cpu;
	switch (mode) {
	case CS_MODE_SYSFS:
		trace_id = coresight_trace_id_get_cpu_id(cpu);
		break;
	case CS_MODE_PERF:
		if (WARN_ON(!sink))
			return -EINVAL;

		trace_id = coresight_trace_id_get_cpu_id_map(cpu, &sink->perf_sink_id_map);
		break;
	default:
		trace_id = -EINVAL;
		break;
	}

	if (!IS_VALID_CS_TRACE_ID(trace_id))
		dev_err(&csdev->dev,
			"Failed to allocate trace ID on CPU%d\n", cpu);

	return trace_id;
}
EXPORT_SYMBOL_GPL(coresight_etm_get_trace_id);

/*
 * Attempt to find and enable programming clock (pclk) and trace clock (atclk)
 * for the given device.
 *
 * For ACPI devices, clocks are controlled by firmware, so bail out early in
 * this case. Also, skip enabling pclk if the clock is managed by the AMBA
 * bus driver instead.
 *
 * atclk is an optional clock, it will be only enabled when it is existed.
 * Otherwise, a NULL pointer will be returned to caller.
 *
 * Returns: '0' on Success; Error code otherwise.
 */
int coresight_get_enable_clocks(struct device *dev, struct clk **pclk,
				struct clk **atclk)
{
	WARN_ON(!pclk);

	if (has_acpi_companion(dev))
		return 0;

	if (!dev_is_amba(dev)) {
		/*
		 * "apb_pclk" is the default clock name for an Arm Primecell
		 * peripheral, while "apb" is used only by the CTCU driver.
		 *
		 * For easier maintenance, CoreSight drivers should use
		 * "apb_pclk" as the programming clock name.
		 */
		*pclk = devm_clk_get_optional_enabled(dev, "apb_pclk");
		if (!*pclk)
			*pclk = devm_clk_get_optional_enabled(dev, "apb");
		if (IS_ERR(*pclk))
			return PTR_ERR(*pclk);
	}

	/* Initialization of atclk is skipped if it is a NULL pointer. */
	if (atclk) {
		*atclk = devm_clk_get_optional_enabled(dev, "atclk");
		if (IS_ERR(*atclk))
			return PTR_ERR(*atclk);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(coresight_get_enable_clocks);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Pratik Patel <pratikp@codeaurora.org>");
MODULE_AUTHOR("Mathieu Poirier <mathieu.poirier@linaro.org>");
MODULE_DESCRIPTION("Arm CoreSight tracer driver");
