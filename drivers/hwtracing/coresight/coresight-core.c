// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012, 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include "coresight-common.h"
#include "coresight-syscfg.h"

#define MAX_SINK_NAME 25

static DEFINE_MUTEX(coresight_mutex);
static DEFINE_PER_CPU(struct coresight_device *, csdev_sink);

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
 * struct coresight_path - path from source to sink
 * @path:	Address of path list.
 * @link:	hook to the list.
 */
struct coresight_path {
	struct list_head *path;
	struct list_head link;
};

static LIST_HEAD(cs_active_paths);

/*
 * When losing synchronisation a new barrier packet needs to be inserted at the
 * beginning of the data collected in a buffer.  That way the decoder knows that
 * it needs to look for another sync sequence.
 */
const u32 coresight_barrier_pkt[4] = {0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff};
EXPORT_SYMBOL_GPL(coresight_barrier_pkt);

static struct coresight_device *coresight_get_source(struct list_head *path);

static const struct cti_assoc_op *cti_assoc_ops;

static const struct csr_set_atid_op *csr_set_atid_ops;

ssize_t coresight_simple_show_pair(struct device *_dev,
			      struct device_attribute *attr, char *buf)
{
	struct coresight_device *csdev = container_of(_dev, struct coresight_device, dev);
	struct cs_pair_attribute *cs_attr = container_of(attr, struct cs_pair_attribute, attr);
	u64 val;

	pm_runtime_get_sync(_dev->parent);
	val = csdev_access_relaxed_read_pair(&csdev->access, cs_attr->lo_off, cs_attr->hi_off);
	pm_runtime_put_sync(_dev->parent);
	return sysfs_emit(buf, "0x%llx\n", val);
}
EXPORT_SYMBOL_GPL(coresight_simple_show_pair);

ssize_t coresight_simple_show32(struct device *_dev,
			      struct device_attribute *attr, char *buf)
{
	struct coresight_device *csdev = container_of(_dev, struct coresight_device, dev);
	struct cs_off_attribute *cs_attr = container_of(attr, struct cs_off_attribute, attr);
	u64 val;

	pm_runtime_get_sync(_dev->parent);
	val = csdev_access_relaxed_read32(&csdev->access, cs_attr->off);
	pm_runtime_put_sync(_dev->parent);
	return sysfs_emit(buf, "0x%llx\n", val);
}
EXPORT_SYMBOL_GPL(coresight_simple_show32);

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

void coresight_set_csr_ops(const struct csr_set_atid_op *csr_op)
{
	csr_set_atid_ops = csr_op;
}
EXPORT_SYMBOL(coresight_set_csr_ops);

void coresight_remove_csr_ops(void)
{
	csr_set_atid_ops = NULL;
}
EXPORT_SYMBOL(coresight_remove_csr_ops);

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

	/* Get the source ID for both components */
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

/**
 * coresight_source_filter - checks whether the connection matches the source
 * of path if connection is binded to specific source.
 * @path:	The list of devices
 * @conn:	The connection of one outport
 *
 * Return zero if the connection doesn't have a source binded or source of the
 * path matches the source binds to connection.
 */
static int coresight_source_filter(struct list_head *path,
			struct coresight_connection *conn)
{
	int ret = 0;
	struct coresight_device *source = NULL;

	if (conn->source_name == NULL)
		return ret;

	source = coresight_get_source(path);
	if (source == NULL)
		return ret;

	return strcmp(conn->source_name, dev_name(&source->dev));
}

static int coresight_reset_sink(struct device *dev, void *data)
{
	struct coresight_device *csdev = to_coresight_device(dev);

	if ((csdev->type == CORESIGHT_DEV_TYPE_SINK ||
		csdev->type == CORESIGHT_DEV_TYPE_LINKSINK) &&
		csdev->activated)
		csdev->activated = false;

	return 0;
}

static void coresight_reset_all_sink(void)
{
	bus_for_each_dev(&coresight_bustype, NULL, NULL, coresight_reset_sink);
}

static int coresight_find_link_inport(struct coresight_device *csdev,
				      struct coresight_device *parent,
					struct list_head *path)
{
	int i;
	struct coresight_connection *conn;

	for (i = 0; i < parent->pdata->nr_outport; i++) {
		conn = &parent->pdata->conns[i];
		if (coresight_source_filter(path, conn))
			continue;
		if (conn->child_dev == csdev)
			return conn->child_port;
	}

	dev_err(&csdev->dev, "couldn't find inport, parent: %s, child: %s\n",
		dev_name(&parent->dev), dev_name(&csdev->dev));

	return -ENODEV;
}

static int coresight_find_link_outport(struct coresight_device *csdev,
				       struct coresight_device *child,
					struct list_head *path)
{
	int i;
	struct coresight_connection *conn;

	for (i = 0; i < csdev->pdata->nr_outport; i++) {
		conn = &csdev->pdata->conns[i];
		if (coresight_source_filter(path, conn))
			continue;
		if (conn->child_dev == child)
			return conn->outport;
	}

	dev_err(&csdev->dev, "couldn't find outport, parent: %s, child: %s\n",
		dev_name(&csdev->dev), dev_name(&child->dev));

	return -ENODEV;
}

static inline u32 coresight_read_claim_tags(struct coresight_device *csdev)
{
	return csdev_access_relaxed_read32(&csdev->access, CORESIGHT_CLAIMCLR);
}

static inline bool coresight_is_claimed_self_hosted(struct coresight_device *csdev)
{
	return coresight_read_claim_tags(csdev) == CORESIGHT_CLAIM_SELF_HOSTED;
}

static inline bool coresight_is_claimed_any(struct coresight_device *csdev)
{
	return coresight_read_claim_tags(csdev) != 0;
}

static inline void coresight_set_claim_tags(struct coresight_device *csdev)
{
	csdev_access_relaxed_write32(&csdev->access, CORESIGHT_CLAIM_SELF_HOSTED,
				     CORESIGHT_CLAIMSET);
	isb();
}

static inline void coresight_clear_claim_tags(struct coresight_device *csdev)
{
	csdev_access_relaxed_write32(&csdev->access, CORESIGHT_CLAIM_SELF_HOSTED,
				     CORESIGHT_CLAIMCLR);
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
int coresight_claim_device_unlocked(struct coresight_device *csdev)
{
	if (WARN_ON(!csdev))
		return -EINVAL;

	if (coresight_is_claimed_any(csdev))
		return -EBUSY;

	coresight_set_claim_tags(csdev);
	if (coresight_is_claimed_self_hosted(csdev))
		return 0;
	/* There was a race setting the tags, clean up and fail */
	coresight_clear_claim_tags(csdev);
	return -EBUSY;
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
 * coresight_disclaim_device_unlocked : Clear the claim tags for the device.
 * Called with CS_UNLOCKed for the component.
 */
void coresight_disclaim_device_unlocked(struct coresight_device *csdev)
{

	if (WARN_ON(!csdev))
		return;

	if (coresight_is_claimed_self_hosted(csdev))
		coresight_clear_claim_tags(csdev);
	else
		/*
		 * The external agent may have not honoured our claim
		 * and has manipulated it. Or something else has seriously
		 * gone wrong in our driver.
		 */
		WARN_ON_ONCE(1);
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

/* enable or disable an associated CTI device of the supplied CS device */
static int
coresight_control_assoc_ectdev(struct coresight_device *csdev, bool enable)
{
	int ect_ret = 0;
	struct coresight_device *ect_csdev = csdev->ect_dev;
	struct module *mod;

	if (!ect_csdev)
		return 0;
	if ((!ect_ops(ect_csdev)->enable) || (!ect_ops(ect_csdev)->disable))
		return 0;

	mod = ect_csdev->dev.parent->driver->owner;
	if (enable) {
		if (try_module_get(mod)) {
			ect_ret = ect_ops(ect_csdev)->enable(ect_csdev);
			if (ect_ret) {
				module_put(mod);
			} else {
				get_device(ect_csdev->dev.parent);
				csdev->ect_enabled = true;
			}
		} else
			ect_ret = -ENODEV;
	} else {
		if (csdev->ect_enabled) {
			ect_ret = ect_ops(ect_csdev)->disable(ect_csdev);
			put_device(ect_csdev->dev.parent);
			module_put(mod);
			csdev->ect_enabled = false;
		}
	}

	/* output warning if ECT enable is preventing trace operation */
	if (ect_ret)
		dev_info(&csdev->dev, "Associated ECT device (%s) %s failed\n",
			 dev_name(&ect_csdev->dev),
			 enable ? "enable" : "disable");
	return ect_ret;
}

/*
 * Set the associated ect / cti device while holding the coresight_mutex
 * to avoid a race with coresight_enable that may try to use this value.
 */
void coresight_set_assoc_ectdev_mutex(struct coresight_device *csdev,
				      struct coresight_device *ect_csdev)
{
	mutex_lock(&coresight_mutex);
	csdev->ect_dev = ect_csdev;
	mutex_unlock(&coresight_mutex);
}
EXPORT_SYMBOL_GPL(coresight_set_assoc_ectdev_mutex);

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

	ret = coresight_control_assoc_ectdev(csdev, true);
	if (ret)
		return ret;
	ret = sink_ops(csdev)->enable(csdev, mode, data);
	if (ret) {
		coresight_control_assoc_ectdev(csdev, false);
		return ret;
	}
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
	coresight_control_assoc_ectdev(csdev, false);
	csdev->enable = false;
	csdev->activated = false;
}

static int coresight_enable_link(struct coresight_device *csdev,
				 struct coresight_device *parent,
				 struct coresight_device *child,
				struct list_head *path)
{
	int ret = 0;
	int link_subtype;
	int inport, outport;

	if (!parent || !child)
		return -EINVAL;

	inport = coresight_find_link_inport(csdev, parent, path);
	outport = coresight_find_link_outport(csdev, child, path);
	link_subtype = csdev->subtype.link_subtype;

	if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_MERG && inport < 0)
		return inport;
	if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_SPLIT && outport < 0)
		return outport;

	if (link_ops(csdev)->enable) {
		ret = coresight_control_assoc_ectdev(csdev, true);
		if (!ret) {
			ret = link_ops(csdev)->enable(csdev, inport, outport);
			if (ret)
				coresight_control_assoc_ectdev(csdev, false);
		}
	}

	if (!ret)
		csdev->enable = true;

	return ret;
}

static void coresight_disable_link(struct coresight_device *csdev,
				   struct coresight_device *parent,
				   struct coresight_device *child,
					struct list_head *path)
{
	int i, nr_conns;
	int link_subtype;
	int inport, outport;

	if (!parent || !child)
		return;

	inport = coresight_find_link_inport(csdev, parent, path);
	outport = coresight_find_link_outport(csdev, child, path);
	link_subtype = csdev->subtype.link_subtype;

	if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_MERG) {
		nr_conns = csdev->pdata->nr_inport;
	} else if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_SPLIT) {
		nr_conns = csdev->pdata->nr_outport;
	} else {
		nr_conns = 1;
	}

	if (link_ops(csdev)->disable) {
		link_ops(csdev)->disable(csdev, inport, outport);
		coresight_control_assoc_ectdev(csdev, false);
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
			ret = coresight_control_assoc_ectdev(csdev, true);
			if (ret)
				return ret;
			ret = source_ops(csdev)->enable(csdev, NULL, mode);
			if (ret) {
				coresight_control_assoc_ectdev(csdev, false);
				return ret;
			}
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
 *  @csdev: The coresight device to disable
 *
 *  Returns true if the device has been disabled.
 */
static bool coresight_disable_source(struct coresight_device *csdev)
{
	if (atomic_dec_return(csdev->refcnt) == 0) {
		if (source_ops(csdev)->disable)
			source_ops(csdev)->disable(csdev, NULL);
		coresight_control_assoc_ectdev(csdev, false);
		csdev->enable = false;
	}
	return !csdev->enable;
}

static struct coresight_device *coresight_get_source(struct list_head *path)
{
	struct coresight_device *csdev;

	if (!path)
		return NULL;

	csdev = list_first_entry(path, struct coresight_node, link)->csdev;
	if (csdev->type != CORESIGHT_DEV_TYPE_SOURCE)
		return NULL;

	return csdev;
}

static int coresight_set_csr_atid(struct list_head *path,
			struct coresight_device *sink_csdev, bool enable)
{
	int i, num, ret = 0;
	struct coresight_device *src_csdev;
	u32 *atid;
	u32 atid_offset;

	src_csdev = coresight_get_source(path);
	if (!src_csdev) {
		ret = -EINVAL;
		return ret;
	}

	num = of_coresight_get_atid_number(src_csdev);
	if (num < 0)
		return num;

	atid = kcalloc(num, sizeof(*atid), GFP_ATOMIC);
	if (!atid)
		return -ENOMEM;

	ret = of_coresight_get_atid(src_csdev, atid, num);
	if (ret < 0) {
		kfree(atid);
		return ret;
	}

	if (csr_set_atid_ops) {
		ret = of_coresight_get_csr_atid_offset(sink_csdev, &atid_offset);
		if (!ret) {
			for (i = 0; i < num; i++) {
				ret = csr_set_atid_ops->set_atid(sink_csdev, atid_offset,
							atid[i], enable);
				if (ret < 0) {
					kfree(atid);
					return ret;
				}
			}
		}
	} else
		ret = -EINVAL;

	kfree(atid);
	return ret;
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
			if (csdev->type == CORESIGHT_DEV_TYPE_SINK &&
			csdev->subtype.sink_subtype ==
				CORESIGHT_DEV_SUBTYPE_SINK_SYSMEM)
				coresight_set_csr_atid(path, csdev, false);

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
			coresight_disable_link(csdev, parent, child, path);
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
EXPORT_SYMBOL_GPL(coresight_disable_path);

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

			if (csdev->type == CORESIGHT_DEV_TYPE_SINK) {
				ret = coresight_set_csr_atid(path, csdev, true);
				if (ret)
					dev_dbg(&csdev->dev, "Set csr atid register fail\n");
			}

			break;
		case CORESIGHT_DEV_TYPE_SOURCE:
			/* sources are enabled from either sysFS or Perf */
			break;
		case CORESIGHT_DEV_TYPE_LINK:
			parent = list_prev_entry(nd, link)->csdev;
			child = list_next_entry(nd, link)->csdev;
			ret = coresight_enable_link(csdev, parent, child, path);
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

static struct coresight_device *
coresight_find_enabled_sink(struct coresight_device *csdev)
{
	int i;
	struct coresight_device *sink = NULL;

	if ((csdev->type == CORESIGHT_DEV_TYPE_SINK ||
	     csdev->type == CORESIGHT_DEV_TYPE_LINKSINK) &&
	     csdev->activated)
		return csdev;

	/*
	 * Recursively explore each port found on this element.
	 */
	for (i = 0; i < csdev->pdata->nr_outport; i++) {
		struct coresight_device *child_dev;

		child_dev = csdev->pdata->conns[i].child_dev;
		if (child_dev)
			sink = coresight_find_enabled_sink(child_dev);
		if (sink)
			return sink;
	}

	return NULL;
}

/**
 * coresight_get_enabled_sink - returns the first enabled sink using
 * connection based search starting from the source reference
 *
 * @source: Coresight source device reference
 */
struct coresight_device *
coresight_get_enabled_sink(struct coresight_device *source)
{
	if (!source)
		return NULL;

	return coresight_find_enabled_sink(source);
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
 * coresight_get_enabled_sink_from_bus - returns the first enabled sink found on the bus
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
static struct coresight_device *coresight_get_enabled_sink_from_bus(bool deactivate)
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

/**
 * coresight_get_ref- Helper function to increase reference count to module
 * and device.
 *
 * @csdev: The coresight device to get a reference on.
 *
 * Return true in successful case and power up the device.
 * Return false when failed to get reference of module.
 */
static inline bool coresight_get_ref(struct coresight_device *csdev)
{
	struct device *dev = csdev->dev.parent;
	int ret;

	/* Make sure the driver can't be removed */
	if (!try_module_get(dev->driver->owner))
		return false;
	/* Make sure the device can't go away */
	get_device(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return false;
	}

	return true;
}

/**
 * coresight_put_ref- Helper function to decrease reference count to module
 * and device. Power off the device.
 *
 * @csdev: The coresight device to decrement a reference from.
 */
static inline void coresight_put_ref(struct coresight_device *csdev)
{
	struct device *dev = csdev->dev.parent;

	pm_runtime_put(dev);
	put_device(dev);
	module_put(dev->driver->owner);
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

	for (i = 0; i < csdev->pdata->nr_outport; i++) {
		struct coresight_device *child;

		child  = csdev->pdata->conns[i].child_dev;
		if (child && child->type == CORESIGHT_DEV_TYPE_HELPER)
			if (!coresight_get_ref(child))
				goto err;
	}
	if (coresight_get_ref(csdev))
		return 0;
err:
	for (i--; i >= 0; i--) {
		struct coresight_device *child;

		child  = csdev->pdata->conns[i].child_dev;
		if (child && child->type == CORESIGHT_DEV_TYPE_HELPER)
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
	for (i = 0; i < csdev->pdata->nr_outport; i++) {
		struct coresight_device *child;

		child  = csdev->pdata->conns[i].child_dev;
		if (child && child->type == CORESIGHT_DEV_TYPE_HELPER)
			coresight_put_ref(child);
	}
}

/**
 * _coresight_build_path - recursively build a path from a @csdev to a sink.
 * @csdev:	The device to start from.
 * @sink:	The final sink we want in this path.
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
				 struct list_head *path,
				struct coresight_device *source)
{
	int i, ret;
	bool found = false;
	struct coresight_node *node;

	/* An activated sink has been found.  Enqueue the element */
	if (csdev == sink)
		goto out;

	if (coresight_is_percpu_source(csdev) && coresight_is_percpu_sink(sink) &&
	    sink == per_cpu(csdev_sink, source_ops(csdev)->cpu_id(csdev))) {
		if (_coresight_build_path(sink, sink, path, source) == 0) {
			found = true;
			goto out;
		}
	}

	/* Not a sink - recursively explore each port found on this element */
	for (i = 0; i < csdev->pdata->nr_outport; i++) {
		struct coresight_device *child_dev;

		child_dev = csdev->pdata->conns[i].child_dev;
		if (csdev->pdata->conns[i].source_name &&
			strcmp(csdev->pdata->conns[i].source_name,
				dev_name(&source->dev)))
			continue;
		if (child_dev &&
		    _coresight_build_path(child_dev, sink, path, source) == 0) {
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

	node = kzalloc(sizeof(struct coresight_node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

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

	rc = _coresight_build_path(source, sink, path, source);
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

/* return true if the device is a suitable type for a default sink */
static inline bool coresight_is_def_sink_type(struct coresight_device *csdev)
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
	for (i = 0; i < csdev->pdata->nr_outport; i++) {
		struct coresight_device *child_dev, *sink = NULL;
		int child_depth = curr_depth;

		child_dev = csdev->pdata->conns[i].child_dev;
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
			csdev->def_sink = per_cpu(csdev_sink, source_ops(csdev)->cpu_id(csdev));
		if (!csdev->def_sink)
			csdev->def_sink = coresight_find_sink(csdev, &depth);
	}
	return csdev->def_sink;
}

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

static int coresight_validate_sink(struct coresight_device *source,
					struct coresight_device *sink)
{


	if (of_coresight_secure_node(sink) && !of_coresight_secure_node(source)) {
		dev_err(&sink->dev, "dont support this source: %s\n",
				dev_name(&source->dev));
		return -EINVAL;
	}

	return 0;
}

static int coresight_store_path(struct list_head *path)
{
	struct coresight_path *node;

	node = kzalloc(sizeof(struct coresight_path), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->path = path;
	list_add(&node->link, &cs_active_paths);

	return 0;
}

int coresight_enable(struct coresight_device *csdev)
{
	int ret = 0;
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

	if (csdev->def_sink) {
		sink = csdev->def_sink;
		sink->activated = true;
	} else
		sink = coresight_get_enabled_sink(csdev);

	if (!sink) {
		ret = -EINVAL;
		goto out;
	}

	ret = coresight_validate_sink(csdev, sink);
	if (ret)
		goto out;

	path = coresight_build_path(csdev, sink);
	if (IS_ERR(path)) {
		ret = PTR_ERR(path);
		pr_err("building path(s) failed %d\n", ret);
		goto out;
	}

	ret = coresight_enable_path(path, CS_MODE_SYSFS, NULL);
	if (ret)
		goto err_path;

	ret = coresight_enable_source(csdev, CS_MODE_SYSFS);
	if (ret)
		goto err_source;

	ret = coresight_store_path(path);
	if (ret)
		goto err_source;
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

static void __coresight_disable(struct coresight_device *csdev)
{
	int ret;
	struct list_head *path = NULL;
	struct coresight_path *cspath = NULL;
	struct coresight_path *cspath_next = NULL;
	struct coresight_device *src_csdev = NULL;

	ret = coresight_validate_source(csdev, __func__);
	if (ret)
		return;

	if (csdev->def_sink)
		csdev->def_sink = NULL;

	if (!csdev->enable || !coresight_disable_source(csdev))
		return;

	list_for_each_entry_safe(cspath, cspath_next, &cs_active_paths, link) {
		src_csdev = coresight_get_source(cspath->path);
		if (!src_csdev)
			continue;
		if (src_csdev == csdev) {
			path = cspath->path;
			list_del(&cspath->link);
			kfree(cspath);
		}
	}

	if (path == NULL)
		return;

	coresight_disable_path(path);
	coresight_release_path(path);

}

void coresight_disable(struct coresight_device *csdev)
{
	mutex_lock(&coresight_mutex);
	__coresight_disable(csdev);
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
	struct coresight_device *sink;
	struct coresight_device *csdev = to_coresight_device(dev);

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (val) {
		sink = coresight_get_enabled_sink_from_bus(false);
		if (sink && sink->type != csdev->type) {
			dev_err(&csdev->dev,
				"Another type sink is enabled.\n");
			return -EINVAL;
		}
		csdev->activated = true;
	} else
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

static ssize_t sink_name_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct coresight_device *csdev = to_coresight_device(dev);

	if (csdev->def_sink)
		return scnprintf(buf, PAGE_SIZE, "%s\n", dev_name(&csdev->def_sink->dev));
	else
		return scnprintf(buf, PAGE_SIZE, "\n");
}

static ssize_t sink_name_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	u32 hash;

	char sink_name[MAX_SINK_NAME] = "";
	struct coresight_device *new_sink, *current_sink;
	struct coresight_device *csdev = to_coresight_device(dev);

	if (size >= MAX_SINK_NAME)
		return -EINVAL;

	if (size == 0) {
		csdev->def_sink = NULL;
		return size;
	}

	if (sscanf(buf, "%s", sink_name) != 1)
		return -EINVAL;

	hash = hashlen_hash(hashlen_string(NULL, sink_name));
	new_sink = coresight_get_sink_by_id(hash);
	current_sink = coresight_get_enabled_sink_from_bus(false);

	if (!new_sink || (current_sink &&
				new_sink && current_sink->type !=
				new_sink->type)) {
		dev_err(&csdev->dev,
			"Sink name [%s] is invalid or another type sink is enabled.\n", sink_name);
		return -EINVAL;
	}

	csdev->def_sink = new_sink;

	return size;
}
static DEVICE_ATTR_RW(sink_name);

static struct attribute *coresight_sink_attrs[] = {
	&dev_attr_enable_sink.attr,
	NULL,
};
ATTRIBUTE_GROUPS(coresight_sink);

static struct attribute *coresight_source_attrs[] = {
	&dev_attr_enable_source.attr,
	&dev_attr_sink_name.attr,
	NULL,
};
ATTRIBUTE_GROUPS(coresight_source);

static struct device_type coresight_dev_type[] = {
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
	{
		.name = "ect",
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
	int i, ret = 0;
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

		/* Skip the port if FW doesn't describe it */
		if (!conn->child_fwnode)
			continue;
		/* We have found at least one orphan connection */
		if (conn->child_dev == NULL) {
			/* Does it match this newly added device? */
			if (conn->child_fwnode == csdev->dev.fwnode) {
				ret = coresight_make_links(i_csdev,
							   conn, csdev);
				if (ret)
					return ret;
			} else {
				/* This component still has an orphan */
				still_orphan = true;
			}
		}
	}

	i_csdev->orphan = still_orphan;

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


static int coresight_fixup_device_conns(struct coresight_device *csdev)
{
	int i, ret = 0;

	for (i = 0; i < csdev->pdata->nr_outport; i++) {
		struct coresight_connection *conn = &csdev->pdata->conns[i];

		if (!conn->child_fwnode)
			continue;
		conn->child_dev =
			coresight_find_csdev_by_fwnode(conn->child_fwnode);
		if (conn->child_dev && conn->child_dev->has_conns_grp) {
			ret = coresight_make_links(csdev, conn,
						   conn->child_dev);
			if (ret)
				break;
		} else {
			csdev->orphan = true;
		}
	}

	return ret;
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

		if (conn->child_dev == NULL || conn->child_fwnode == NULL)
			continue;

		if (csdev->dev.fwnode == conn->child_fwnode) {
			iterator->orphan = true;
			coresight_remove_links(iterator, conn);

			conn->child_dev = NULL;
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
 * coresight_timeout - loop until a bit has changed to a specific register
 *			state.
 * @csa: coresight device access for the device
 * @offset: Offset of the register from the base of the device.
 * @position: the position of the bit of interest.
 * @value: the value the bit should have.
 *
 * Return: 0 as soon as the bit has taken the desired state or -EAGAIN if
 * TIMEOUT_US has elapsed, which ever happens first.
 */
int coresight_timeout(struct csdev_access *csa, u32 offset,
		      int position, int value)
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
void coresight_release_platform_data(struct coresight_device *csdev,
				     struct coresight_platform_data *pdata)
{
	int i;
	struct coresight_connection *conns = pdata->conns;

	for (i = 0; i < pdata->nr_outport; i++) {
		/* If we have made the links, remove them now */
		if (csdev && conns[i].child_dev)
			coresight_remove_links(csdev, &conns[i]);
		/*
		 * Drop the refcount and clear the handle as this device
		 * is going away
		 */
		if (conns[i].child_fwnode) {
			fwnode_handle_put(conns[i].child_fwnode);
			pdata->conns[i].child_fwnode = NULL;
		}
	}
	if (csdev)
		coresight_remove_conns_sysfs_group(csdev);
}

struct coresight_device *coresight_register(struct coresight_desc *desc)
{
	int ret;
	int link_subtype;
	int nr_refcnts = 1;
	atomic_t *refcnts = NULL;
	struct coresight_device *csdev;
	bool registered = false;

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
		kfree(csdev);
		goto err_out;
	}

	csdev->refcnt = refcnts;

	csdev->pdata = desc->pdata;

	csdev->type = desc->type;
	csdev->subtype = desc->subtype;
	csdev->ops = desc->ops;
	csdev->access = desc->access;
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
			goto out_unlock;
		}
	}
	/* Device is now registered */
	registered = true;

	ret = coresight_create_conns_sysfs_group(csdev);
	if (!ret)
		ret = coresight_fixup_device_conns(csdev);
	if (!ret)
		ret = coresight_fixup_orphan_conns(csdev);

out_unlock:
	mutex_unlock(&coresight_mutex);
	/* Success */
	if (!ret) {
		if (cti_assoc_ops && cti_assoc_ops->add)
			cti_assoc_ops->add(csdev);
		return csdev;
	}

	/* Unregister the device if needed */
	if (registered) {
		coresight_unregister(csdev);
		return ERR_PTR(ret);
	}

err_out:
	/* Cleanup the connection information */
	coresight_release_platform_data(NULL, desc->pdata);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(coresight_register);

void coresight_unregister(struct coresight_device *csdev)
{
	etm_perf_del_symlink_sink(csdev);
	/* Remove references of that device in the topology */
	if (cti_assoc_ops && cti_assoc_ops->remove)
		cti_assoc_ops->remove(csdev);
	coresight_remove_conns(csdev);
	coresight_clear_default_sink(csdev);
	coresight_release_platform_data(csdev, csdev->pdata);
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

bool coresight_loses_context_with_cpu(struct device *dev)
{
	return fwnode_property_present(dev_fwnode(dev),
				       "arm,coresight-loses-context-with-cpu");
}
EXPORT_SYMBOL_GPL(coresight_loses_context_with_cpu);

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
	const char *coresight_name = NULL;
	struct fwnode_handle **list;
	struct device_node *node = dev->of_node;

	mutex_lock(&coresight_mutex);

	if (!of_property_read_string(node, "coresight-name", &coresight_name))
		name = devm_kasprintf(dev, GFP_KERNEL, "%s", coresight_name);
	else {
		idx = coresight_search_device_idx(dict, dev_fwnode(dev));
		if (idx < 0) {
			/* Make space for the new entry */
			idx = dict->nr_idx;
			list = krealloc_array(dict->fwnode_list,
					      idx + 1, sizeof(*dict->fwnode_list),
					      GFP_KERNEL);
			if (ZERO_OR_NULL_PTR(list))
				goto done;

			list[idx] = dev_fwnode(dev);
			dict->fwnode_list = list;
			dict->nr_idx = idx + 1;
		}

		name = devm_kasprintf(dev, GFP_KERNEL, "%s%d", dict->pfx, idx);
	}
done:
	mutex_unlock(&coresight_mutex);
	return name;
}
EXPORT_SYMBOL_GPL(coresight_alloc_device_name);

static ssize_t reset_source_sink_store(struct bus_type *bus,
				       const char *buf, size_t size)
{
	int ret = 0;
	unsigned long val;
	struct coresight_path *cspath = NULL;
	struct coresight_path *cspath_next = NULL;
	struct coresight_device *csdev;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&coresight_mutex);

	list_for_each_entry_safe(cspath, cspath_next, &cs_active_paths, link) {
		csdev = coresight_get_source(cspath->path);
		if (!csdev)
			continue;
		atomic_set(csdev->refcnt, 1);
		__coresight_disable(csdev);
	}

	/* Reset all activated sinks */
	coresight_reset_all_sink();

	mutex_unlock(&coresight_mutex);
	return size;
}
static BUS_ATTR_WO(reset_source_sink);

static struct attribute *coresight_reset_source_sink_attrs[] = {
	&bus_attr_reset_source_sink.attr,
	NULL,
};
ATTRIBUTE_GROUPS(coresight_reset_source_sink);

struct bus_type coresight_bustype = {
	.name		= "coresight",
	.bus_groups	= coresight_reset_source_sink_groups,
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

	/* initialise the coresight syscfg API */
	ret = cscfg_init();
	if (!ret)
		return 0;

	etm_perf_exit();
exit_bus_unregister:
	bus_unregister(&coresight_bustype);
	return ret;
}

static void __exit coresight_exit(void)
{
	cscfg_exit();
	etm_perf_exit();
	bus_unregister(&coresight_bustype);
}

module_init(coresight_init);
module_exit(coresight_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Pratik Patel <pratikp@codeaurora.org>");
MODULE_AUTHOR("Mathieu Poirier <mathieu.poirier@linaro.org>");
MODULE_DESCRIPTION("Arm CoreSight tracer driver");
