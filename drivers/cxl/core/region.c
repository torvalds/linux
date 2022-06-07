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
 * 3. Decoder targets
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

static const struct attribute_group *get_cxl_region_target_group(void);

static ssize_t interleave_ways_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(dev->parent);
	struct cxl_decoder *cxld = &cxlrd->cxlsd.cxld;
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;
	int rc, val, save;
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

	save = p->interleave_ways;
	p->interleave_ways = val;
	rc = sysfs_update_group(&cxlr->dev.kobj, get_cxl_region_target_group());
	if (rc)
		p->interleave_ways = save;
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

static ssize_t resource_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;
	u64 resource = -1ULL;
	ssize_t rc;

	rc = down_read_interruptible(&cxl_region_rwsem);
	if (rc)
		return rc;
	if (p->res)
		resource = p->res->start;
	rc = sysfs_emit(buf, "%#llx\n", resource);
	up_read(&cxl_region_rwsem);

	return rc;
}
static DEVICE_ATTR_RO(resource);

static int alloc_hpa(struct cxl_region *cxlr, resource_size_t size)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(cxlr->dev.parent);
	struct cxl_region_params *p = &cxlr->params;
	struct resource *res;
	u32 remainder = 0;

	lockdep_assert_held_write(&cxl_region_rwsem);

	/* Nothing to do... */
	if (p->res && resource_size(res) == size)
		return 0;

	/* To change size the old size must be freed first */
	if (p->res)
		return -EBUSY;

	if (p->state >= CXL_CONFIG_INTERLEAVE_ACTIVE)
		return -EBUSY;

	/* ways, granularity and uuid (if PMEM) need to be set before HPA */
	if (!p->interleave_ways || !p->interleave_granularity ||
	    (cxlr->mode == CXL_DECODER_PMEM && uuid_is_null(&p->uuid)))
		return -ENXIO;

	div_u64_rem(size, SZ_256M * p->interleave_ways, &remainder);
	if (remainder)
		return -EINVAL;

	res = alloc_free_mem_region(cxlrd->res, size, SZ_256M,
				    dev_name(&cxlr->dev));
	if (IS_ERR(res)) {
		dev_dbg(&cxlr->dev, "failed to allocate HPA: %ld\n",
			PTR_ERR(res));
		return PTR_ERR(res);
	}

	p->res = res;
	p->state = CXL_CONFIG_INTERLEAVE_ACTIVE;

	return 0;
}

static void cxl_region_iomem_release(struct cxl_region *cxlr)
{
	struct cxl_region_params *p = &cxlr->params;

	if (device_is_registered(&cxlr->dev))
		lockdep_assert_held_write(&cxl_region_rwsem);
	if (p->res) {
		remove_resource(p->res);
		kfree(p->res);
		p->res = NULL;
	}
}

static int free_hpa(struct cxl_region *cxlr)
{
	struct cxl_region_params *p = &cxlr->params;

	lockdep_assert_held_write(&cxl_region_rwsem);

	if (!p->res)
		return 0;

	if (p->state >= CXL_CONFIG_ACTIVE)
		return -EBUSY;

	cxl_region_iomem_release(cxlr);
	p->state = CXL_CONFIG_IDLE;
	return 0;
}

static ssize_t size_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t len)
{
	struct cxl_region *cxlr = to_cxl_region(dev);
	u64 val;
	int rc;

	rc = kstrtou64(buf, 0, &val);
	if (rc)
		return rc;

	rc = down_write_killable(&cxl_region_rwsem);
	if (rc)
		return rc;

	if (val)
		rc = alloc_hpa(cxlr, val);
	else
		rc = free_hpa(cxlr);
	up_write(&cxl_region_rwsem);

	if (rc)
		return rc;

	return len;
}

static ssize_t size_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;
	u64 size = 0;
	ssize_t rc;

	rc = down_read_interruptible(&cxl_region_rwsem);
	if (rc)
		return rc;
	if (p->res)
		size = resource_size(p->res);
	rc = sysfs_emit(buf, "%#llx\n", size);
	up_read(&cxl_region_rwsem);

	return rc;
}
static DEVICE_ATTR_RW(size);

static struct attribute *cxl_region_attrs[] = {
	&dev_attr_uuid.attr,
	&dev_attr_interleave_ways.attr,
	&dev_attr_interleave_granularity.attr,
	&dev_attr_resource.attr,
	&dev_attr_size.attr,
	NULL,
};

static const struct attribute_group cxl_region_group = {
	.attrs = cxl_region_attrs,
	.is_visible = cxl_region_visible,
};

static size_t show_targetN(struct cxl_region *cxlr, char *buf, int pos)
{
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_endpoint_decoder *cxled;
	int rc;

	rc = down_read_interruptible(&cxl_region_rwsem);
	if (rc)
		return rc;

	if (pos >= p->interleave_ways) {
		dev_dbg(&cxlr->dev, "position %d out of range %d\n", pos,
			p->interleave_ways);
		rc = -ENXIO;
		goto out;
	}

	cxled = p->targets[pos];
	if (!cxled)
		rc = sysfs_emit(buf, "\n");
	else
		rc = sysfs_emit(buf, "%s\n", dev_name(&cxled->cxld.dev));
out:
	up_read(&cxl_region_rwsem);

	return rc;
}

static int match_free_decoder(struct device *dev, void *data)
{
	struct cxl_decoder *cxld;
	int *id = data;

	if (!is_switch_decoder(dev))
		return 0;

	cxld = to_cxl_decoder(dev);

	/* enforce ordered allocation */
	if (cxld->id != *id)
		return 0;

	if (!cxld->region)
		return 1;

	(*id)++;

	return 0;
}

static struct cxl_decoder *cxl_region_find_decoder(struct cxl_port *port,
						   struct cxl_region *cxlr)
{
	struct device *dev;
	int id = 0;

	dev = device_find_child(&port->dev, &id, match_free_decoder);
	if (!dev)
		return NULL;
	/*
	 * This decoder is pinned registered as long as the endpoint decoder is
	 * registered, and endpoint decoder unregistration holds the
	 * cxl_region_rwsem over unregister events, so no need to hold on to
	 * this extra reference.
	 */
	put_device(dev);
	return to_cxl_decoder(dev);
}

static struct cxl_region_ref *alloc_region_ref(struct cxl_port *port,
					       struct cxl_region *cxlr)
{
	struct cxl_region_ref *cxl_rr;
	int rc;

	cxl_rr = kzalloc(sizeof(*cxl_rr), GFP_KERNEL);
	if (!cxl_rr)
		return NULL;
	cxl_rr->port = port;
	cxl_rr->region = cxlr;
	xa_init(&cxl_rr->endpoints);

	rc = xa_insert(&port->regions, (unsigned long)cxlr, cxl_rr, GFP_KERNEL);
	if (rc) {
		dev_dbg(&cxlr->dev,
			"%s: failed to track region reference: %d\n",
			dev_name(&port->dev), rc);
		kfree(cxl_rr);
		return NULL;
	}

	return cxl_rr;
}

static void free_region_ref(struct cxl_region_ref *cxl_rr)
{
	struct cxl_port *port = cxl_rr->port;
	struct cxl_region *cxlr = cxl_rr->region;
	struct cxl_decoder *cxld = cxl_rr->decoder;

	dev_WARN_ONCE(&cxlr->dev, cxld->region != cxlr, "region mismatch\n");
	if (cxld->region == cxlr) {
		cxld->region = NULL;
		put_device(&cxlr->dev);
	}

	xa_erase(&port->regions, (unsigned long)cxlr);
	xa_destroy(&cxl_rr->endpoints);
	kfree(cxl_rr);
}

static int cxl_rr_ep_add(struct cxl_region_ref *cxl_rr,
			 struct cxl_endpoint_decoder *cxled)
{
	int rc;
	struct cxl_port *port = cxl_rr->port;
	struct cxl_region *cxlr = cxl_rr->region;
	struct cxl_decoder *cxld = cxl_rr->decoder;
	struct cxl_ep *ep = cxl_ep_load(port, cxled_to_memdev(cxled));

	rc = xa_insert(&cxl_rr->endpoints, (unsigned long)cxled, ep,
			 GFP_KERNEL);
	if (rc)
		return rc;
	cxl_rr->nr_eps++;

	if (!cxld->region) {
		cxld->region = cxlr;
		get_device(&cxlr->dev);
	}

	return 0;
}

/**
 * cxl_port_attach_region() - track a region's interest in a port by endpoint
 * @port: port to add a new region reference 'struct cxl_region_ref'
 * @cxlr: region to attach to @port
 * @cxled: endpoint decoder used to create or further pin a region reference
 * @pos: interleave position of @cxled in @cxlr
 *
 * The attach event is an opportunity to validate CXL decode setup
 * constraints and record metadata needed for programming HDM decoders,
 * in particular decoder target lists.
 *
 * The steps are:
 * - validate that there are no other regions with a higher HPA already
 *   associated with @port
 * - establish a region reference if one is not already present
 *   - additionally allocate a decoder instance that will host @cxlr on
 *     @port
 * - pin the region reference by the endpoint
 * - account for how many entries in @port's target list are needed to
 *   cover all of the added endpoints.
 */
static int cxl_port_attach_region(struct cxl_port *port,
				  struct cxl_region *cxlr,
				  struct cxl_endpoint_decoder *cxled, int pos)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_ep *ep = cxl_ep_load(port, cxlmd);
	struct cxl_region_ref *cxl_rr = NULL, *iter;
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_decoder *cxld = NULL;
	unsigned long index;
	int rc = -EBUSY;

	lockdep_assert_held_write(&cxl_region_rwsem);

	xa_for_each(&port->regions, index, iter) {
		struct cxl_region_params *ip = &iter->region->params;

		if (iter->region == cxlr)
			cxl_rr = iter;
		if (ip->res->start > p->res->start) {
			dev_dbg(&cxlr->dev,
				"%s: HPA order violation %s:%pr vs %pr\n",
				dev_name(&port->dev),
				dev_name(&iter->region->dev), ip->res, p->res);
			return -EBUSY;
		}
	}

	if (cxl_rr) {
		struct cxl_ep *ep_iter;
		int found = 0;

		cxld = cxl_rr->decoder;
		xa_for_each(&cxl_rr->endpoints, index, ep_iter) {
			if (ep_iter == ep)
				continue;
			if (ep_iter->next == ep->next) {
				found++;
				break;
			}
		}

		/*
		 * If this is a new target or if this port is direct connected
		 * to this endpoint then add to the target count.
		 */
		if (!found || !ep->next)
			cxl_rr->nr_targets++;
	} else {
		cxl_rr = alloc_region_ref(port, cxlr);
		if (!cxl_rr) {
			dev_dbg(&cxlr->dev,
				"%s: failed to allocate region reference\n",
				dev_name(&port->dev));
			return -ENOMEM;
		}
	}

	if (!cxld) {
		if (port == cxled_to_port(cxled))
			cxld = &cxled->cxld;
		else
			cxld = cxl_region_find_decoder(port, cxlr);
		if (!cxld) {
			dev_dbg(&cxlr->dev, "%s: no decoder available\n",
				dev_name(&port->dev));
			goto out_erase;
		}

		if (cxld->region) {
			dev_dbg(&cxlr->dev, "%s: %s already attached to %s\n",
				dev_name(&port->dev), dev_name(&cxld->dev),
				dev_name(&cxld->region->dev));
			rc = -EBUSY;
			goto out_erase;
		}

		cxl_rr->decoder = cxld;
	}

	rc = cxl_rr_ep_add(cxl_rr, cxled);
	if (rc) {
		dev_dbg(&cxlr->dev,
			"%s: failed to track endpoint %s:%s reference\n",
			dev_name(&port->dev), dev_name(&cxlmd->dev),
			dev_name(&cxld->dev));
		goto out_erase;
	}

	return 0;
out_erase:
	if (cxl_rr->nr_eps == 0)
		free_region_ref(cxl_rr);
	return rc;
}

static struct cxl_region_ref *cxl_rr_load(struct cxl_port *port,
					  struct cxl_region *cxlr)
{
	return xa_load(&port->regions, (unsigned long)cxlr);
}

static void cxl_port_detach_region(struct cxl_port *port,
				   struct cxl_region *cxlr,
				   struct cxl_endpoint_decoder *cxled)
{
	struct cxl_region_ref *cxl_rr;
	struct cxl_ep *ep;

	lockdep_assert_held_write(&cxl_region_rwsem);

	cxl_rr = cxl_rr_load(port, cxlr);
	if (!cxl_rr)
		return;

	ep = xa_erase(&cxl_rr->endpoints, (unsigned long)cxled);
	if (ep) {
		struct cxl_ep *ep_iter;
		unsigned long index;
		int found = 0;

		cxl_rr->nr_eps--;
		xa_for_each(&cxl_rr->endpoints, index, ep_iter) {
			if (ep_iter->next == ep->next) {
				found++;
				break;
			}
		}
		if (!found)
			cxl_rr->nr_targets--;
	}

	if (cxl_rr->nr_eps == 0)
		free_region_ref(cxl_rr);
}

static int cxl_region_attach(struct cxl_region *cxlr,
			     struct cxl_endpoint_decoder *cxled, int pos)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(cxlr->dev.parent);
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_port *ep_port, *root_port, *iter;
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_dport *dport;
	int i, rc = -ENXIO;

	if (cxled->mode == CXL_DECODER_DEAD) {
		dev_dbg(&cxlr->dev, "%s dead\n", dev_name(&cxled->cxld.dev));
		return -ENODEV;
	}

	/* all full of members, or interleave config not established? */
	if (p->state > CXL_CONFIG_INTERLEAVE_ACTIVE) {
		dev_dbg(&cxlr->dev, "region already active\n");
		return -EBUSY;
	} else if (p->state < CXL_CONFIG_INTERLEAVE_ACTIVE) {
		dev_dbg(&cxlr->dev, "interleave config missing\n");
		return -ENXIO;
	}

	if (pos < 0 || pos >= p->interleave_ways) {
		dev_dbg(&cxlr->dev, "position %d out of range %d\n", pos,
			p->interleave_ways);
		return -ENXIO;
	}

	if (p->targets[pos] == cxled)
		return 0;

	if (p->targets[pos]) {
		struct cxl_endpoint_decoder *cxled_target = p->targets[pos];
		struct cxl_memdev *cxlmd_target = cxled_to_memdev(cxled_target);

		dev_dbg(&cxlr->dev, "position %d already assigned to %s:%s\n",
			pos, dev_name(&cxlmd_target->dev),
			dev_name(&cxled_target->cxld.dev));
		return -EBUSY;
	}

	for (i = 0; i < p->interleave_ways; i++) {
		struct cxl_endpoint_decoder *cxled_target;
		struct cxl_memdev *cxlmd_target;

		cxled_target = p->targets[pos];
		if (!cxled_target)
			continue;

		cxlmd_target = cxled_to_memdev(cxled_target);
		if (cxlmd_target == cxlmd) {
			dev_dbg(&cxlr->dev,
				"%s already specified at position %d via: %s\n",
				dev_name(&cxlmd->dev), pos,
				dev_name(&cxled_target->cxld.dev));
			return -EBUSY;
		}
	}

	ep_port = cxled_to_port(cxled);
	root_port = cxlrd_to_port(cxlrd);
	dport = cxl_find_dport_by_dev(root_port, ep_port->host_bridge);
	if (!dport) {
		dev_dbg(&cxlr->dev, "%s:%s invalid target for %s\n",
			dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev),
			dev_name(cxlr->dev.parent));
		return -ENXIO;
	}

	if (cxlrd->calc_hb(cxlrd, pos) != dport) {
		dev_dbg(&cxlr->dev, "%s:%s invalid target position for %s\n",
			dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev),
			dev_name(&cxlrd->cxlsd.cxld.dev));
		return -ENXIO;
	}

	if (cxled->cxld.target_type != cxlr->type) {
		dev_dbg(&cxlr->dev, "%s:%s type mismatch: %d vs %d\n",
			dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev),
			cxled->cxld.target_type, cxlr->type);
		return -ENXIO;
	}

	if (!cxled->dpa_res) {
		dev_dbg(&cxlr->dev, "%s:%s: missing DPA allocation.\n",
			dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev));
		return -ENXIO;
	}

	if (resource_size(cxled->dpa_res) * p->interleave_ways !=
	    resource_size(p->res)) {
		dev_dbg(&cxlr->dev,
			"%s:%s: decoder-size-%#llx * ways-%d != region-size-%#llx\n",
			dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev),
			(u64)resource_size(cxled->dpa_res), p->interleave_ways,
			(u64)resource_size(p->res));
		return -EINVAL;
	}

	for (iter = ep_port; !is_cxl_root(iter);
	     iter = to_cxl_port(iter->dev.parent)) {
		rc = cxl_port_attach_region(iter, cxlr, cxled, pos);
		if (rc)
			goto err;
	}

	p->targets[pos] = cxled;
	cxled->pos = pos;
	p->nr_targets++;

	if (p->nr_targets == p->interleave_ways)
		p->state = CXL_CONFIG_ACTIVE;

	return 0;

err:
	for (iter = ep_port; !is_cxl_root(iter);
	     iter = to_cxl_port(iter->dev.parent))
		cxl_port_detach_region(iter, cxlr, cxled);
	return rc;
}

static void cxl_region_detach(struct cxl_endpoint_decoder *cxled)
{
	struct cxl_port *iter, *ep_port = cxled_to_port(cxled);
	struct cxl_region *cxlr = cxled->cxld.region;
	struct cxl_region_params *p;

	lockdep_assert_held_write(&cxl_region_rwsem);

	if (!cxlr)
		return;

	p = &cxlr->params;
	get_device(&cxlr->dev);

	for (iter = ep_port; !is_cxl_root(iter);
	     iter = to_cxl_port(iter->dev.parent))
		cxl_port_detach_region(iter, cxlr, cxled);

	if (cxled->pos < 0 || cxled->pos >= p->interleave_ways ||
	    p->targets[cxled->pos] != cxled) {
		struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);

		dev_WARN_ONCE(&cxlr->dev, 1, "expected %s:%s at position %d\n",
			      dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev),
			      cxled->pos);
		goto out;
	}

	if (p->state == CXL_CONFIG_ACTIVE)
		p->state = CXL_CONFIG_INTERLEAVE_ACTIVE;
	p->targets[cxled->pos] = NULL;
	p->nr_targets--;

	/* notify the region driver that one of its targets has departed */
	up_write(&cxl_region_rwsem);
	device_release_driver(&cxlr->dev);
	down_write(&cxl_region_rwsem);
out:
	put_device(&cxlr->dev);
}

void cxl_decoder_kill_region(struct cxl_endpoint_decoder *cxled)
{
	down_write(&cxl_region_rwsem);
	cxled->mode = CXL_DECODER_DEAD;
	cxl_region_detach(cxled);
	up_write(&cxl_region_rwsem);
}

static int attach_target(struct cxl_region *cxlr, const char *decoder, int pos)
{
	struct device *dev;
	int rc;

	dev = bus_find_device_by_name(&cxl_bus_type, NULL, decoder);
	if (!dev)
		return -ENODEV;

	if (!is_endpoint_decoder(dev)) {
		put_device(dev);
		return -EINVAL;
	}

	rc = down_write_killable(&cxl_region_rwsem);
	if (rc)
		goto out;
	down_read(&cxl_dpa_rwsem);
	rc = cxl_region_attach(cxlr, to_cxl_endpoint_decoder(dev), pos);
	up_read(&cxl_dpa_rwsem);
	up_write(&cxl_region_rwsem);
out:
	put_device(dev);
	return rc;
}

static int detach_target(struct cxl_region *cxlr, int pos)
{
	struct cxl_region_params *p = &cxlr->params;
	int rc;

	rc = down_write_killable(&cxl_region_rwsem);
	if (rc)
		return rc;

	if (pos >= p->interleave_ways) {
		dev_dbg(&cxlr->dev, "position %d out of range %d\n", pos,
			p->interleave_ways);
		rc = -ENXIO;
		goto out;
	}

	if (!p->targets[pos]) {
		rc = 0;
		goto out;
	}

	cxl_region_detach(p->targets[pos]);
	rc = 0;
out:
	up_write(&cxl_region_rwsem);
	return rc;
}

static size_t store_targetN(struct cxl_region *cxlr, const char *buf, int pos,
			    size_t len)
{
	int rc;

	if (sysfs_streq(buf, "\n"))
		rc = detach_target(cxlr, pos);
	else
		rc = attach_target(cxlr, buf, pos);

	if (rc < 0)
		return rc;
	return len;
}

#define TARGET_ATTR_RW(n)                                              \
static ssize_t target##n##_show(                                       \
	struct device *dev, struct device_attribute *attr, char *buf)  \
{                                                                      \
	return show_targetN(to_cxl_region(dev), buf, (n));             \
}                                                                      \
static ssize_t target##n##_store(struct device *dev,                   \
				 struct device_attribute *attr,        \
				 const char *buf, size_t len)          \
{                                                                      \
	return store_targetN(to_cxl_region(dev), buf, (n), len);       \
}                                                                      \
static DEVICE_ATTR_RW(target##n)

TARGET_ATTR_RW(0);
TARGET_ATTR_RW(1);
TARGET_ATTR_RW(2);
TARGET_ATTR_RW(3);
TARGET_ATTR_RW(4);
TARGET_ATTR_RW(5);
TARGET_ATTR_RW(6);
TARGET_ATTR_RW(7);
TARGET_ATTR_RW(8);
TARGET_ATTR_RW(9);
TARGET_ATTR_RW(10);
TARGET_ATTR_RW(11);
TARGET_ATTR_RW(12);
TARGET_ATTR_RW(13);
TARGET_ATTR_RW(14);
TARGET_ATTR_RW(15);

static struct attribute *target_attrs[] = {
	&dev_attr_target0.attr,
	&dev_attr_target1.attr,
	&dev_attr_target2.attr,
	&dev_attr_target3.attr,
	&dev_attr_target4.attr,
	&dev_attr_target5.attr,
	&dev_attr_target6.attr,
	&dev_attr_target7.attr,
	&dev_attr_target8.attr,
	&dev_attr_target9.attr,
	&dev_attr_target10.attr,
	&dev_attr_target11.attr,
	&dev_attr_target12.attr,
	&dev_attr_target13.attr,
	&dev_attr_target14.attr,
	&dev_attr_target15.attr,
	NULL,
};

static umode_t cxl_region_target_visible(struct kobject *kobj,
					 struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;

	if (n < p->interleave_ways)
		return a->mode;
	return 0;
}

static const struct attribute_group cxl_region_target_group = {
	.attrs = target_attrs,
	.is_visible = cxl_region_target_visible,
};

static const struct attribute_group *get_cxl_region_target_group(void)
{
	return &cxl_region_target_group;
}

static const struct attribute_group *region_groups[] = {
	&cxl_base_attribute_group,
	&cxl_region_group,
	&cxl_region_target_group,
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
	struct cxl_region *cxlr = to_cxl_region(dev);

	device_del(dev);
	cxl_region_iomem_release(cxlr);
	put_device(dev);
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

static ssize_t region_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);
	ssize_t rc;

	rc = down_read_interruptible(&cxl_region_rwsem);
	if (rc)
		return rc;

	if (cxld->region)
		rc = sysfs_emit(buf, "%s\n", dev_name(&cxld->region->dev));
	else
		rc = sysfs_emit(buf, "\n");
	up_read(&cxl_region_rwsem);

	return rc;
}
DEVICE_ATTR_RO(region);

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

MODULE_IMPORT_NS(CXL);
