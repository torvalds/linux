// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#include <linux/memregion.h>
#include <linux/genalloc.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/idr.h>
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
 */

static struct cxl_region *to_cxl_region(struct device *dev);

static void cxl_region_release(struct device *dev)
{
	struct cxl_region *cxlr = to_cxl_region(dev);

	memregion_free(cxlr->id);
	kfree(cxlr);
}

static const struct device_type cxl_region_type = {
	.name = "cxl_region",
	.release = cxl_region_release,
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
	struct cxl_region *cxlr;
	struct device *dev;
	int rc;

	cxlr = cxl_region_alloc(cxlrd, id);
	if (IS_ERR(cxlr))
		return cxlr;
	cxlr->mode = mode;
	cxlr->type = type;

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
