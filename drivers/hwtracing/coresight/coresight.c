/* Copyright (c) 2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/coresight.h>
#include <linux/of_platform.h>
#include <linux/delay.h>

#include "coresight-priv.h"

static DEFINE_MUTEX(coresight_mutex);

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

static int coresight_find_link_inport(struct coresight_device *csdev)
{
	int i;
	struct coresight_device *parent;
	struct coresight_connection *conn;

	parent = container_of(csdev->path_link.next,
			      struct coresight_device, path_link);

	for (i = 0; i < parent->nr_outport; i++) {
		conn = &parent->conns[i];
		if (conn->child_dev == csdev)
			return conn->child_port;
	}

	dev_err(&csdev->dev, "couldn't find inport, parent: %s, child: %s\n",
		dev_name(&parent->dev), dev_name(&csdev->dev));

	return -ENODEV;
}

static int coresight_find_link_outport(struct coresight_device *csdev)
{
	int i;
	struct coresight_device *child;
	struct coresight_connection *conn;

	child = container_of(csdev->path_link.prev,
			     struct coresight_device, path_link);

	for (i = 0; i < csdev->nr_outport; i++) {
		conn = &csdev->conns[i];
		if (conn->child_dev == child)
			return conn->outport;
	}

	dev_err(&csdev->dev, "couldn't find outport, parent: %s, child: %s\n",
		dev_name(&csdev->dev), dev_name(&child->dev));

	return -ENODEV;
}

static int coresight_enable_sink(struct coresight_device *csdev)
{
	int ret;

	if (!csdev->enable) {
		if (sink_ops(csdev)->enable) {
			ret = sink_ops(csdev)->enable(csdev);
			if (ret)
				return ret;
		}
		csdev->enable = true;
	}

	atomic_inc(csdev->refcnt);

	return 0;
}

static void coresight_disable_sink(struct coresight_device *csdev)
{
	if (atomic_dec_return(csdev->refcnt) == 0) {
		if (sink_ops(csdev)->disable) {
			sink_ops(csdev)->disable(csdev);
			csdev->enable = false;
		}
	}
}

static int coresight_enable_link(struct coresight_device *csdev)
{
	int ret;
	int link_subtype;
	int refport, inport, outport;

	inport = coresight_find_link_inport(csdev);
	outport = coresight_find_link_outport(csdev);
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
			if (ret)
				return ret;
		}
	}

	csdev->enable = true;

	return 0;
}

static void coresight_disable_link(struct coresight_device *csdev)
{
	int i, nr_conns;
	int link_subtype;
	int refport, inport, outport;

	inport = coresight_find_link_inport(csdev);
	outport = coresight_find_link_outport(csdev);
	link_subtype = csdev->subtype.link_subtype;

	if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_MERG) {
		refport = inport;
		nr_conns = csdev->nr_inport;
	} else if (link_subtype == CORESIGHT_DEV_SUBTYPE_LINK_SPLIT) {
		refport = outport;
		nr_conns = csdev->nr_outport;
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

static int coresight_enable_source(struct coresight_device *csdev)
{
	int ret;

	if (!coresight_source_is_unique(csdev)) {
		dev_warn(&csdev->dev, "traceID %d not unique\n",
			 source_ops(csdev)->trace_id(csdev));
		return -EINVAL;
	}

	if (!csdev->enable) {
		if (source_ops(csdev)->enable) {
			ret = source_ops(csdev)->enable(csdev);
			if (ret)
				return ret;
		}
		csdev->enable = true;
	}

	atomic_inc(csdev->refcnt);

	return 0;
}

static void coresight_disable_source(struct coresight_device *csdev)
{
	if (atomic_dec_return(csdev->refcnt) == 0) {
		if (source_ops(csdev)->disable) {
			source_ops(csdev)->disable(csdev);
			csdev->enable = false;
		}
	}
}

static int coresight_enable_path(struct list_head *path)
{
	int ret = 0;
	struct coresight_device *cd;

	/*
	 * At this point we have a full @path, from source to sink.  The
	 * sink is the first entry and the source the last one.  Go through
	 * all the components and enable them one by one.
	 */
	list_for_each_entry(cd, path, path_link) {
		if (cd == list_first_entry(path, struct coresight_device,
					   path_link)) {
			ret = coresight_enable_sink(cd);
		} else if (list_is_last(&cd->path_link, path)) {
			/*
			 * Don't enable the source just yet - this needs to
			 * happen at the very end when all links and sink
			 * along the path have been configured properly.
			 */
			;
		} else {
			ret = coresight_enable_link(cd);
		}
		if (ret)
			goto err;
	}

	return 0;
err:
	list_for_each_entry_continue_reverse(cd, path, path_link) {
		if (cd == list_first_entry(path, struct coresight_device,
					   path_link)) {
			coresight_disable_sink(cd);
		} else if (list_is_last(&cd->path_link, path)) {
			;
		} else {
			coresight_disable_link(cd);
		}
	}

	return ret;
}

static int coresight_disable_path(struct list_head *path)
{
	struct coresight_device *cd;

	list_for_each_entry_reverse(cd, path, path_link) {
		if (cd == list_first_entry(path, struct coresight_device,
					   path_link)) {
			coresight_disable_sink(cd);
		} else if (list_is_last(&cd->path_link, path)) {
			/*
			 * The source has already been stopped, no need
			 * to do it again here.
			 */
			;
		} else {
			coresight_disable_link(cd);
		}
	}

	return 0;
}

static int coresight_build_paths(struct coresight_device *csdev,
				 struct list_head *path,
				 bool enable)
{
	int i, ret = -EINVAL;
	struct coresight_connection *conn;

	list_add(&csdev->path_link, path);

	if ((csdev->type == CORESIGHT_DEV_TYPE_SINK ||
	    csdev->type == CORESIGHT_DEV_TYPE_LINKSINK) &&
	    csdev->activated) {
		if (enable)
			ret = coresight_enable_path(path);
		else
			ret = coresight_disable_path(path);
	} else {
		for (i = 0; i < csdev->nr_outport; i++) {
			conn = &csdev->conns[i];
			if (coresight_build_paths(conn->child_dev,
						    path, enable) == 0)
				ret = 0;
		}
	}

	if (list_first_entry(path, struct coresight_device, path_link) != csdev)
		dev_err(&csdev->dev, "wrong device in %s\n", __func__);

	list_del(&csdev->path_link);

	return ret;
}

int coresight_enable(struct coresight_device *csdev)
{
	int ret = 0;
	LIST_HEAD(path);

	mutex_lock(&coresight_mutex);
	if (csdev->type != CORESIGHT_DEV_TYPE_SOURCE) {
		ret = -EINVAL;
		dev_err(&csdev->dev, "wrong device type in %s\n", __func__);
		goto out;
	}
	if (csdev->enable)
		goto out;

	if (coresight_build_paths(csdev, &path, true)) {
		dev_err(&csdev->dev, "building path(s) failed\n");
		goto out;
	}

	if (coresight_enable_source(csdev))
		dev_err(&csdev->dev, "source enable failed\n");
out:
	mutex_unlock(&coresight_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(coresight_enable);

void coresight_disable(struct coresight_device *csdev)
{
	LIST_HEAD(path);

	mutex_lock(&coresight_mutex);
	if (csdev->type != CORESIGHT_DEV_TYPE_SOURCE) {
		dev_err(&csdev->dev, "wrong device type in %s\n", __func__);
		goto out;
	}
	if (!csdev->enable)
		goto out;

	coresight_disable_source(csdev);
	if (coresight_build_paths(csdev, &path, false))
		dev_err(&csdev->dev, "releasing path(s) failed\n");

out:
	mutex_unlock(&coresight_mutex);
}
EXPORT_SYMBOL_GPL(coresight_disable);

static ssize_t enable_sink_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct coresight_device *csdev = to_coresight_device(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", (unsigned)csdev->activated);
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

	return scnprintf(buf, PAGE_SIZE, "%u\n", (unsigned)csdev->enable);
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
};

static void coresight_device_release(struct device *dev)
{
	struct coresight_device *csdev = to_coresight_device(dev);

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
	for (i = 0; i < i_csdev->nr_outport; i++) {
		conn = &i_csdev->conns[i];

		/* We have found at least one orphan connection */
		if (conn->child_dev == NULL) {
			/* Does it match this newly added device? */
			if (!strcmp(dev_name(&csdev->dev), conn->child_name)) {
				conn->child_dev = csdev;
			} else {
				/* This component still has an orphan */
				still_orphan = true;
			}
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


static int coresight_name_match(struct device *dev, void *data)
{
	char *to_match;
	struct coresight_device *i_csdev;

	to_match = data;
	i_csdev = to_coresight_device(dev);

	if (to_match && !strcmp(to_match, dev_name(&i_csdev->dev)))
		return 1;

	return 0;
}

static void coresight_fixup_device_conns(struct coresight_device *csdev)
{
	int i;
	struct device *dev = NULL;
	struct coresight_connection *conn;

	for (i = 0; i < csdev->nr_outport; i++) {
		conn = &csdev->conns[i];
		dev = bus_find_device(&coresight_bustype, NULL,
				      (void *)conn->child_name,
				      coresight_name_match);

		if (dev) {
			conn->child_dev = to_coresight_device(dev);
		} else {
			csdev->orphan = true;
			conn->child_dev = NULL;
		}
	}
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

struct coresight_device *coresight_register(struct coresight_desc *desc)
{
	int i;
	int ret;
	int link_subtype;
	int nr_refcnts = 1;
	atomic_t *refcnts = NULL;
	struct coresight_device *csdev;
	struct coresight_connection *conns;

	csdev = kzalloc(sizeof(*csdev), GFP_KERNEL);
	if (!csdev) {
		ret = -ENOMEM;
		goto err_kzalloc_csdev;
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
		goto err_kzalloc_refcnts;
	}

	csdev->refcnt = refcnts;

	csdev->nr_inport = desc->pdata->nr_inport;
	csdev->nr_outport = desc->pdata->nr_outport;
	conns = kcalloc(csdev->nr_outport, sizeof(*conns), GFP_KERNEL);
	if (!conns) {
		ret = -ENOMEM;
		goto err_kzalloc_conns;
	}

	for (i = 0; i < csdev->nr_outport; i++) {
		conns[i].outport = desc->pdata->outports[i];
		conns[i].child_name = desc->pdata->child_names[i];
		conns[i].child_port = desc->pdata->child_ports[i];
	}

	csdev->conns = conns;

	csdev->type = desc->type;
	csdev->subtype = desc->subtype;
	csdev->ops = desc->ops;
	csdev->orphan = false;

	csdev->dev.type = &coresight_dev_type[desc->type];
	csdev->dev.groups = desc->groups;
	csdev->dev.parent = desc->dev;
	csdev->dev.release = coresight_device_release;
	csdev->dev.bus = &coresight_bustype;
	dev_set_name(&csdev->dev, "%s", desc->pdata->name);

	ret = device_register(&csdev->dev);
	if (ret)
		goto err_device_register;

	mutex_lock(&coresight_mutex);

	coresight_fixup_device_conns(csdev);
	coresight_fixup_orphan_conns(csdev);

	mutex_unlock(&coresight_mutex);

	return csdev;

err_device_register:
	kfree(conns);
err_kzalloc_conns:
	kfree(refcnts);
err_kzalloc_refcnts:
	kfree(csdev);
err_kzalloc_csdev:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(coresight_register);

void coresight_unregister(struct coresight_device *csdev)
{
	mutex_lock(&coresight_mutex);

	kfree(csdev->conns);
	device_unregister(&csdev->dev);

	mutex_unlock(&coresight_mutex);
}
EXPORT_SYMBOL_GPL(coresight_unregister);

MODULE_LICENSE("GPL v2");
