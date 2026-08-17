// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, Linaro Limited, All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/property.h>

#include "coresight-priv.h"
#include "coresight-trace-id.h"

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

static void coresight_source_get_refcnt(struct coresight_device *csdev)
{
	/*
	 * There could be multiple applications driving the software
	 * source. So keep the refcount for each such user when the
	 * source is already enabled.
	 *
	 * No need to increment the reference counter for other source
	 * types, as multiple enables are the same as a single enable.
	 */
	if (coresight_is_software_source(csdev))
		csdev->refcnt++;
}

static void coresight_source_put_refcnt(struct coresight_device *csdev)
{
	if (coresight_is_software_source(csdev))
		csdev->refcnt--;
}

static int coresight_enable_source_sysfs(struct coresight_device *csdev,
					 enum cs_mode mode,
					 struct coresight_path *path)
{
	int ret;

	/*
	 * Comparison with CS_MODE_SYSFS works without taking any device
	 * specific spinlock because the truthyness of that comparison can only
	 * change with coresight_mutex held, which we already have here.
	 */
	lockdep_assert_held(&coresight_mutex);
	if (coresight_get_mode(csdev) != CS_MODE_SYSFS) {
		ret = coresight_enable_source(csdev, NULL, mode, path);
		if (ret)
			return ret;
	}

	coresight_source_get_refcnt(csdev);

	return 0;
}

/**
 *  coresight_disable_source_sysfs - Drop the reference count by 1 for software
 *  sources. Disable the device if there are no users left.
 *
 *  @csdev: The coresight device to disable
 *  @data: Opaque data to pass on to the disable function of the source device.
 *         For example in perf mode this is a pointer to the struct perf_event.
 *
 *  Returns true if the device has been disabled.
 */
static bool coresight_disable_source_sysfs(struct coresight_device *csdev,
					   void *data)
{
	lockdep_assert_held(&coresight_mutex);
	if (coresight_get_mode(csdev) != CS_MODE_SYSFS)
		return false;

	coresight_source_put_refcnt(csdev);
	if (csdev->refcnt == 0) {
		coresight_disable_source(csdev, data);
		return true;
	}
	return false;
}

/**
 * coresight_find_activated_sysfs_sink - returns the first sink activated via
 * sysfs using connection based search starting from the source reference.
 *
 * @csdev: Coresight source device reference
 */
static struct coresight_device *
coresight_find_activated_sysfs_sink(struct coresight_device *csdev)
{
	int i;
	struct coresight_device *sink = NULL;

	if ((csdev->type == CORESIGHT_DEV_TYPE_SINK ||
	     csdev->type == CORESIGHT_DEV_TYPE_LINKSINK) &&
	     csdev->sysfs_sink_activated)
		return csdev;

	/*
	 * Recursively explore each port found on this element.
	 */
	for (i = 0; i < csdev->pdata->nr_outconns; i++) {
		struct coresight_device *child_dev;

		child_dev = csdev->pdata->out_conns[i]->dest_dev;
		if (child_dev)
			sink = coresight_find_activated_sysfs_sink(child_dev);
		if (sink)
			return sink;
	}

	return NULL;
}

/** coresight_validate_source - make sure a source has the right credentials to
 *  be used via sysfs.
 *  @csdev:	the device structure for a source.
 *  @function:	the function this was called from.
 *
 * Assumes the coresight_mutex is held.
 */
static int coresight_validate_source_sysfs(struct coresight_device *csdev,
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
	    subtype != CORESIGHT_DEV_SUBTYPE_SOURCE_SOFTWARE &&
	    subtype != CORESIGHT_DEV_SUBTYPE_SOURCE_TPDM &&
	    subtype != CORESIGHT_DEV_SUBTYPE_SOURCE_OTHERS) {
		dev_err(&csdev->dev, "wrong device subtype in %s\n", function);
		return -EINVAL;
	}

	if (coresight_is_percpu_source(csdev) && !cpu_online(csdev->cpu))
		return -ENODEV;

	return 0;
}

int coresight_enable_sysfs(struct coresight_device *csdev)
{
	int ret = 0;
	struct coresight_device *sink;
	struct coresight_path *path;

	mutex_lock(&coresight_mutex);

	ret = coresight_validate_source_sysfs(csdev, __func__);
	if (ret)
		goto out;

	/*
	 * mode == SYSFS implies that it's already enabled. Don't look at the
	 * refcount to determine this because we don't claim the source until
	 * coresight_enable_source() so can still race with Perf mode which
	 * doesn't hold coresight_mutex.
	 */
	if (coresight_get_mode(csdev) == CS_MODE_SYSFS) {
		coresight_source_get_refcnt(csdev);
		goto out;
	}

	sink = coresight_find_activated_sysfs_sink(csdev);
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

	ret = coresight_path_assign_trace_id(path, CS_MODE_SYSFS);
	if (ret)
		goto err_path;

	ret = coresight_enable_path(path, CS_MODE_SYSFS);
	if (ret)
		goto err_path;

	ret = coresight_enable_source_sysfs(csdev, CS_MODE_SYSFS, path);
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
EXPORT_SYMBOL_GPL(coresight_enable_sysfs);

void coresight_disable_sysfs(struct coresight_device *csdev)
{
	struct coresight_path *path;
	int ret;

	mutex_lock(&coresight_mutex);

	ret = coresight_validate_source_sysfs(csdev, __func__);
	if (ret)
		goto out;

	/*
	 * coresight_disable_source_sysfs() clears the 'csdev->path' pointer
	 * when disabling the source. Retrieve the path pointer here.
	 */
	path = csdev->path;

	if (!coresight_disable_source_sysfs(csdev, NULL))
		goto out;

	coresight_disable_path(path);
	coresight_release_path(path);

out:
	mutex_unlock(&coresight_mutex);
}
EXPORT_SYMBOL_GPL(coresight_disable_sysfs);

static ssize_t enable_sink_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct coresight_device *csdev = to_coresight_device(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", csdev->sysfs_sink_activated);
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

	csdev->sysfs_sink_activated = !!val;

	return size;

}
static DEVICE_ATTR_RW(enable_sink);

static ssize_t enable_source_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct coresight_device *csdev = to_coresight_device(dev);

	guard(mutex)(&coresight_mutex);
	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 coresight_get_mode(csdev) == CS_MODE_SYSFS);
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

	/*
	 * CoreSight hotplug callbacks in core layer control a activated path
	 * from its source to sink. Taking hotplug lock here protects a race
	 * condition with hotplug callbacks.
	 */
	guard(cpus_read_lock)();

	if (val) {
		ret = coresight_enable_sysfs(csdev);
		if (ret)
			return ret;
	} else {
		coresight_disable_sysfs(csdev);
	}

	return size;
}
static DEVICE_ATTR_RW(enable_source);

static ssize_t label_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	const char *str;
	int ret;

	ret = fwnode_property_read_string(dev_fwnode(dev), "label", &str);
	if (ret == 0)
		return sysfs_emit(buf, "%s\n", str);
	else
		return ret;
}
static DEVICE_ATTR_RO(label);

static umode_t label_is_visible(struct kobject *kobj,
				   struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);

	if (attr == &dev_attr_label.attr) {
		if (fwnode_property_present(dev_fwnode(dev), "label"))
			return attr->mode;
		else
			return 0;
	}

	return attr->mode;
}

static struct attribute *coresight_sink_attrs[] = {
	&dev_attr_enable_sink.attr,
	&dev_attr_label.attr,
	NULL,
};

static struct attribute_group coresight_sink_group = {
	.attrs = coresight_sink_attrs,
	.is_visible = label_is_visible,
};
__ATTRIBUTE_GROUPS(coresight_sink);

static struct attribute *coresight_source_attrs[] = {
	&dev_attr_enable_source.attr,
	&dev_attr_label.attr,
	NULL,
};

static struct attribute_group coresight_source_group = {
	.attrs = coresight_source_attrs,
	.is_visible = label_is_visible,
};
__ATTRIBUTE_GROUPS(coresight_source);

static struct attribute *coresight_link_attrs[] = {
	&dev_attr_label.attr,
	NULL,
};

static struct attribute_group coresight_link_group = {
	.attrs = coresight_link_attrs,
	.is_visible = label_is_visible,
};
__ATTRIBUTE_GROUPS(coresight_link);

static struct attribute *coresight_helper_attrs[] = {
	&dev_attr_label.attr,
	NULL,
};

static struct attribute_group coresight_helper_group = {
	.attrs = coresight_helper_attrs,
	.is_visible = label_is_visible,
};
__ATTRIBUTE_GROUPS(coresight_helper);

const struct device_type coresight_dev_type[] = {
	[CORESIGHT_DEV_TYPE_SINK] = {
		.name = "sink",
		.groups = coresight_sink_groups,
	},
	[CORESIGHT_DEV_TYPE_LINK] = {
		.name = "link",
		.groups = coresight_link_groups,
	},
	[CORESIGHT_DEV_TYPE_LINKSINK] = {
		.name = "linksink",
		.groups = coresight_sink_groups,
	},
	[CORESIGHT_DEV_TYPE_SOURCE] = {
		.name = "source",
		.groups = coresight_source_groups,
	},
	[CORESIGHT_DEV_TYPE_HELPER] = {
		.name = "helper",
		.groups = coresight_helper_groups,
	}
};
/* Ensure the enum matches the names and groups */
static_assert(ARRAY_SIZE(coresight_dev_type) == CORESIGHT_DEV_TYPE_MAX);

/*
 * Connections group - links attribute.
 * Count of created links between coresight components in the group.
 */
static ssize_t nr_links_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct coresight_device *csdev = to_coresight_device(dev);

	return sprintf(buf, "%d\n", csdev->nr_links);
}
static DEVICE_ATTR_RO(nr_links);

static struct attribute *coresight_conns_attrs[] = {
	&dev_attr_nr_links.attr,
	NULL,
};

static struct attribute_group coresight_conns_group = {
	.attrs = coresight_conns_attrs,
	.name = "connections",
};

/*
 * Create connections group for CoreSight devices.
 * This group will then be used to collate the sysfs links between
 * devices.
 */
int coresight_create_conns_sysfs_group(struct coresight_device *csdev)
{
	int ret = 0;

	if (!csdev)
		return -EINVAL;

	ret = sysfs_create_group(&csdev->dev.kobj, &coresight_conns_group);
	if (ret)
		return ret;

	csdev->has_conns_grp = true;
	return ret;
}

void coresight_remove_conns_sysfs_group(struct coresight_device *csdev)
{
	if (!csdev)
		return;

	if (csdev->has_conns_grp) {
		sysfs_remove_group(&csdev->dev.kobj, &coresight_conns_group);
		csdev->has_conns_grp = false;
	}
}

int coresight_add_sysfs_link(struct coresight_sysfs_link *info)
{
	int ret = 0;

	if (!info)
		return -EINVAL;
	if (!info->orig || !info->target ||
	    !info->orig_name || !info->target_name)
		return -EINVAL;
	if (!info->orig->has_conns_grp || !info->target->has_conns_grp)
		return -EINVAL;

	/* first link orig->target */
	ret = sysfs_add_link_to_group(&info->orig->dev.kobj,
				      coresight_conns_group.name,
				      &info->target->dev.kobj,
				      info->orig_name);
	if (ret)
		return ret;

	/* second link target->orig */
	ret = sysfs_add_link_to_group(&info->target->dev.kobj,
				      coresight_conns_group.name,
				      &info->orig->dev.kobj,
				      info->target_name);

	/* error in second link - remove first - otherwise inc counts */
	if (ret) {
		sysfs_remove_link_from_group(&info->orig->dev.kobj,
					     coresight_conns_group.name,
					     info->orig_name);
	} else {
		info->orig->nr_links++;
		info->target->nr_links++;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(coresight_add_sysfs_link);

void coresight_remove_sysfs_link(struct coresight_sysfs_link *info)
{
	if (!info)
		return;
	if (!info->orig || !info->target ||
	    !info->orig_name || !info->target_name)
		return;

	sysfs_remove_link_from_group(&info->orig->dev.kobj,
				     coresight_conns_group.name,
				     info->orig_name);

	sysfs_remove_link_from_group(&info->target->dev.kobj,
				     coresight_conns_group.name,
				     info->target_name);

	info->orig->nr_links--;
	info->target->nr_links--;
}
EXPORT_SYMBOL_GPL(coresight_remove_sysfs_link);

/*
 * coresight_make_links: Make a link for a connection from a @orig
 * device to @target, represented by @conn.
 *
 *   e.g, for devOrig[output_X] -> devTarget[input_Y] is represented
 *   as two symbolic links :
 *
 *	/sys/.../devOrig/out:X	-> /sys/.../devTarget/
 *	/sys/.../devTarget/in:Y	-> /sys/.../devOrig/
 *
 * The link names are allocated for a device where it appears. i.e, the
 * "out" link on the master and "in" link on the slave device.
 * The link info is stored in the connection record for avoiding
 * the reconstruction of names for removal.
 */
int coresight_make_links(struct coresight_device *orig,
			 struct coresight_connection *conn,
			 struct coresight_device *target)
{
	int ret = -ENOMEM;
	char *outs = NULL, *ins = NULL;
	struct coresight_sysfs_link *link = NULL;

	/* Helper devices aren't shown in sysfs */
	if (conn->dest_port == -1 && conn->src_port == -1)
		return 0;

	do {
		outs = devm_kasprintf(&orig->dev, GFP_KERNEL,
				      "out:%d", conn->src_port);
		if (!outs)
			break;
		ins = devm_kasprintf(&target->dev, GFP_KERNEL,
				     "in:%d", conn->dest_port);
		if (!ins)
			break;
		link = devm_kzalloc(&orig->dev,
				    sizeof(struct coresight_sysfs_link),
				    GFP_KERNEL);
		if (!link)
			break;

		link->orig = orig;
		link->target = target;
		link->orig_name = outs;
		link->target_name = ins;

		ret = coresight_add_sysfs_link(link);
		if (ret)
			break;

		conn->link = link;
		return 0;
	} while (0);

	return ret;
}

/*
 * coresight_remove_links: Remove the sysfs links for a given connection @conn,
 * from @orig device to @target device. See coresight_make_links() for more
 * details.
 */
void coresight_remove_links(struct coresight_device *orig,
			    struct coresight_connection *conn)
{
	if (!orig || !conn->link)
		return;

	coresight_remove_sysfs_link(conn->link);

	devm_kfree(&conn->dest_dev->dev, conn->link->target_name);
	devm_kfree(&orig->dev, conn->link->orig_name);
	devm_kfree(&orig->dev, conn->link);
	conn->link = NULL;
}
