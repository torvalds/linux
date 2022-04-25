// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#include <linux/memregion.h>
#include <linux/genalloc.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/idr.h>
#include <cxlmem.h>
#include <cxl.h>
#include "core.h"

/**
 * DOC: cxl core region
 *
 * CXL Regions represent mapped memory capacity in system physical address
 * space. Whereas the CXL Root Decoders identify the bounds of potential CXL
 * Memory ranges, Regions represent the active mapped capacity by the HDM
 * Decoder Capability structures throughout the Host Bridges, Switches, and
 * Endpoints in the topology.
 *
 * Region configuration has ordering constraints. UUID may be set at any time
 * but is only visible for persistent regions.
 * 1. Interleave granularity
 * 2. Interleave size
 */

/*
 * All changes to the interleave configuration occur with this lock held
 * for write.
 */
static DECLARE_RWSEM(cxl_region_rwsem);

static struct cxl_region *to_cxl_region(struct device *dev);

static ssize_t uuid_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;
	ssize_t rc;

	rc = down_read_interruptible(&cxl_region_rwsem);
	if (rc)
		return rc;
	rc = sysfs_emit(buf, "%pUb\n", &p->uuid);
	up_read(&cxl_region_rwsem);

	return rc;
}

static int is_dup(struct device *match, void *data)
{
	struct cxl_region_params *p;
	struct cxl_region *cxlr;
	uuid_t *uuid = data;

	if (!is_cxl_region(match))
		return 0;

	lockdep_assert_held(&cxl_region_rwsem);
	cxlr = to_cxl_region(match);
	p = &cxlr->params;

	if (uuid_equal(&p->uuid, uuid)) {
		dev_dbg(match, "already has uuid: %pUb\n", uuid);
		return -EBUSY;
	}

	return 0;
}

static ssize_t uuid_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t len)
{
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;
	uuid_t temp;
	ssize_t rc;

	if (len != UUID_STRING_LEN + 1)
		return -EINVAL;

	rc = uuid_parse(buf, &temp);
	if (rc)
		return rc;

	if (uuid_is_null(&temp))
		return -EINVAL;

	rc = down_write_killable(&cxl_region_rwsem);
	if (rc)
		return rc;

	if (uuid_equal(&p->uuid, &temp))
		goto out;

	rc = -EBUSY;
	if (p->state >= CXL_CONFIG_ACTIVE)
		goto out;

	rc = bus_for_each_dev(&cxl_bus_type, NULL, &temp, is_dup);
	if (rc < 0)
		goto out;

	uuid_copy(&p->uuid, &temp);
out:
	up_write(&cxl_region_rwsem);

	if (rc)
		return rc;
	return len;
}
static DEVICE_ATTR_RW(uuid);

static umode_t cxl_region_visible(struct kobject *kobj, struct attribute *a,
				  int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cxl_region *cxlr = to_cxl_region(dev);

	if (a == &dev_attr_uuid.attr && cxlr->mode != CXL_DECODER_PMEM)
		return 0;
	return a->mode;
}

static ssize_t interleave_ways_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;
	ssize_t rc;

	rc = down_read_interruptible(&cxl_region_rwsem);
	if (rc)
		return rc;
	rc = sysfs_emit(buf, "%d\n", p->interleave_ways);
	up_read(&cxl_region_rwsem);

	return rc;
}

static ssize_t interleave_ways_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(dev->parent);
	struct cxl_decoder *cxld = &cxlrd->cxlsd.cxld;
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;
	int rc, val;
	u8 iw;

	rc = kstrtoint(buf, 0, &val);
	if (rc)
		return rc;

	rc = ways_to_cxl(val, &iw);
	if (rc)
		return rc;

	/*
	 * Even for x3, x9, and x12 interleaves the region interleave must be a
	 * power of 2 multiple of the host bridge interleave.
	 */
	if (!is_power_of_2(val / cxld->interleave_ways) ||
	    (val % cxld->interleave_ways)) {
		dev_dbg(&cxlr->dev, "invalid interleave: %d\n", val);
		return -EINVAL;
	}

	rc = down_write_killable(&cxl_region_rwsem);
	if (rc)
		return rc;
	if (p->state >= CXL_CONFIG_INTERLEAVE_ACTIVE) {
		rc = -EBUSY;
		goto out;
	}

	p->interleave_ways = val;
out:
	up_write(&cxl_region_rwsem);
	if (rc)
		return rc;
	return len;
}
static DEVICE_ATTR_RW(interleave_ways);

static ssize_t interleave_granularity_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;
	ssize_t rc;

	rc = down_read_interruptible(&cxl_region_rwsem);
	if (rc)
		return rc;
	rc = sysfs_emit(buf, "%d\n", p->interleave_granularity);
	up_read(&cxl_region_rwsem);

	return rc;
}

static ssize_t interleave_granularity_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(dev->parent);
	struct cxl_decoder *cxld = &cxlrd->cxlsd.cxld;
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;
	int rc, val;
	u16 ig;

	rc = kstrtoint(buf, 0, &val);
	if (rc)
		return rc;

	rc = granularity_to_cxl(val, &ig);
	if (rc)
		return rc;

	/*
	 * Disallow region granularity less than root granularity to
	 * simplify the implementation. Otherwise, region's with a
	 * granularity less than the root interleave result in needing
	 * multiple endpoints to support a single slot in the
	 * interleave.
	 */
	if (val < cxld->interleave_granularity)
		return -EINVAL;

	rc = down_write_killable(&cxl_region_rwsem);
	if (rc)
		return rc;
	if (p->state >= CXL_CONFIG_INTERLEAVE_ACTIVE) {
		rc = -EBUSY;
		goto out;
	}

	p->interleave_granularity = val;
out:
	up_write(&cxl_region_rwsem);
	if (rc)
		return rc;
	return len;
}
static DEVICE_ATTR_RW(interleave_granularity);

static struct attribute *cxl_region_attrs[] = {
	&dev_attr_uuid.attr,
	&dev_attr_interleave_ways.attr,
	&dev_attr_interleave_granularity.attr,
	NULL,
};

static const struct attribute_group cxl_region_group = {
	.attrs = cxl_region_attrs,
	.is_visible = cxl_region_visible,
};

static const struct attribute_group *region_groups[] = {
	&cxl_base_attribute_group,
	&cxl_region_group,
	NULL,
};

static void cxl_region_release(struct device *dev)
{
	struct cxl_region *cxlr = to_cxl_region(dev);

	memregion_free(cxlr->id);
	kfree(cxlr);
}

static const struct device_type cxl_region_type = {
	.name = "cxl_region",
	.release = cxl_region_release,
	.groups = region_groups
};

bool is_cxl_region(struct device *dev)
{
	return dev->type == &cxl_region_type;
}
EXPORT_SYMBOL_NS_GPL(is_cxl_region, CXL);

static struct cxl_region *to_cxl_region(struct device *dev)
{
	if (dev_WARN_ONCE(dev, dev->type != &cxl_region_type,
			  "not a cxl_region device\n"))
		return NULL;

	return container_of(dev, struct cxl_region, dev);
}

static void unregister_region(void *dev)
{
	device_unregister(dev);
}

static struct lock_class_key cxl_region_key;

static struct cxl_region *cxl_region_alloc(struct cxl_root_decoder *cxlrd, int id)
{
	struct cxl_region *cxlr;
	struct device *dev;

	cxlr = kzalloc(sizeof(*cxlr), GFP_KERNEL);
	if (!cxlr) {
		memregion_free(id);
		return ERR_PTR(-ENOMEM);
	}

	dev = &cxlr->dev;
	device_initialize(dev);
	lockdep_set_class(&dev->mutex, &cxl_region_key);
	dev->parent = &cxlrd->cxlsd.cxld.dev;
	device_set_pm_not_required(dev);
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_region_type;
	cxlr->id = id;

	return cxlr;
}

/**
 * devm_cxl_add_region - Adds a region to a decoder
 * @cxlrd: root decoder
 * @id: memregion id to create, or memregion_free() on failure
 * @mode: mode for the endpoint decoders of this region
 * @type: select whether this is an expander or accelerator (type-2 or type-3)
 *
 * This is the second step of region initialization. Regions exist within an
 * address space which is mapped by a @cxlrd.
 *
 * Return: 0 if the region was added to the @cxlrd, else returns negative error
 * code. The region will be named "regionZ" where Z is the unique region number.
 */
static struct cxl_region *devm_cxl_add_region(struct cxl_root_decoder *cxlrd,
					      int id,
					      enum cxl_decoder_mode mode,
					      enum cxl_decoder_type type)
{
	struct cxl_port *port = to_cxl_port(cxlrd->cxlsd.cxld.dev.parent);
	struct cxl_decoder *cxld = &cxlrd->cxlsd.cxld;
	struct cxl_region_params *p;
	struct cxl_region *cxlr;
	struct device *dev;
	int rc;

	cxlr = cxl_region_alloc(cxlrd, id);
	if (IS_ERR(cxlr))
		return cxlr;
	p = &cxlr->params;
	cxlr->mode = mode;
	cxlr->type = type;
	p->interleave_granularity = cxld->interleave_granularity;

	dev = &cxlr->dev;
	rc = dev_set_name(dev, "region%d", id);
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	rc = devm_add_action_or_reset(port->uport, unregister_region, cxlr);
	if (rc)
		return ERR_PTR(rc);

	dev_dbg(port->uport, "%s: created %s\n",
		dev_name(&cxlrd->cxlsd.cxld.dev), dev_name(dev));
	return cxlr;

err:
	put_device(dev);
	return ERR_PTR(rc);
}

static ssize_t create_pmem_region_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(dev);

	return sysfs_emit(buf, "region%u\n", atomic_read(&cxlrd->region_id));
}

static ssize_t create_pmem_region_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(dev);
	struct cxl_region *cxlr;
	int id, rc;

	rc = sscanf(buf, "region%d\n", &id);
	if (rc != 1)
		return -EINVAL;

	rc = memregion_alloc(GFP_KERNEL);
	if (rc < 0)
		return rc;

	if (atomic_cmpxchg(&cxlrd->region_id, id, rc) != id) {
		memregion_free(rc);
		return -EBUSY;
	}

	cxlr = devm_cxl_add_region(cxlrd, id, CXL_DECODER_PMEM,
				   CXL_DECODER_EXPANDER);
	if (IS_ERR(cxlr))
		return PTR_ERR(cxlr);

	return len;
}
DEVICE_ATTR_RW(create_pmem_region);

static struct cxl_region *
cxl_find_region_by_name(struct cxl_root_decoder *cxlrd, const char *name)
{
	struct cxl_decoder *cxld = &cxlrd->cxlsd.cxld;
	struct device *region_dev;

	region_dev = device_find_child_by_name(&cxld->dev, name);
	if (!region_dev)
		return ERR_PTR(-ENODEV);

	return to_cxl_region(region_dev);
}

static ssize_t delete_region_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(dev);
	struct cxl_port *port = to_cxl_port(dev->parent);
	struct cxl_region *cxlr;

	cxlr = cxl_find_region_by_name(cxlrd, buf);
	if (IS_ERR(cxlr))
		return PTR_ERR(cxlr);

	devm_release_action(port->uport, unregister_region, cxlr);
	put_device(&cxlr->dev);

	return len;
}
DEVICE_ATTR_WO(delete_region);
