// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#include <linux/memregion.h>
#include <linux/genalloc.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/memory.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/sort.h>
#include <linux/idr.h>
#include <linux/memory-tiers.h>
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

static struct cxl_region *to_cxl_region(struct device *dev);

#define __ACCESS_ATTR_RO(_level, _name) {				\
	.attr	= { .name = __stringify(_name), .mode = 0444 },		\
	.show	= _name##_access##_level##_show,			\
}

#define ACCESS_DEVICE_ATTR_RO(level, name)	\
	struct device_attribute dev_attr_access##level##_##name = __ACCESS_ATTR_RO(level, name)

#define ACCESS_ATTR_RO(level, attrib)					      \
static ssize_t attrib##_access##level##_show(struct device *dev,	      \
					  struct device_attribute *attr,      \
					  char *buf)			      \
{									      \
	struct cxl_region *cxlr = to_cxl_region(dev);			      \
									      \
	if (cxlr->coord[level].attrib == 0)				      \
		return -ENOENT;						      \
									      \
	return sysfs_emit(buf, "%u\n", cxlr->coord[level].attrib);	      \
}									      \
static ACCESS_DEVICE_ATTR_RO(level, attrib)

ACCESS_ATTR_RO(0, read_bandwidth);
ACCESS_ATTR_RO(0, read_latency);
ACCESS_ATTR_RO(0, write_bandwidth);
ACCESS_ATTR_RO(0, write_latency);

#define ACCESS_ATTR_DECLARE(level, attrib)	\
	(&dev_attr_access##level##_##attrib.attr)

static struct attribute *access0_coordinate_attrs[] = {
	ACCESS_ATTR_DECLARE(0, read_bandwidth),
	ACCESS_ATTR_DECLARE(0, write_bandwidth),
	ACCESS_ATTR_DECLARE(0, read_latency),
	ACCESS_ATTR_DECLARE(0, write_latency),
	NULL
};

ACCESS_ATTR_RO(1, read_bandwidth);
ACCESS_ATTR_RO(1, read_latency);
ACCESS_ATTR_RO(1, write_bandwidth);
ACCESS_ATTR_RO(1, write_latency);

static struct attribute *access1_coordinate_attrs[] = {
	ACCESS_ATTR_DECLARE(1, read_bandwidth),
	ACCESS_ATTR_DECLARE(1, write_bandwidth),
	ACCESS_ATTR_DECLARE(1, read_latency),
	ACCESS_ATTR_DECLARE(1, write_latency),
	NULL
};

#define ACCESS_VISIBLE(level)						\
static umode_t cxl_region_access##level##_coordinate_visible(		\
		struct kobject *kobj, struct attribute *a, int n)	\
{									\
	struct device *dev = kobj_to_dev(kobj);				\
	struct cxl_region *cxlr = to_cxl_region(dev);			\
									\
	if (a == &dev_attr_access##level##_read_latency.attr &&		\
	    cxlr->coord[level].read_latency == 0)			\
		return 0;						\
									\
	if (a == &dev_attr_access##level##_write_latency.attr &&	\
	    cxlr->coord[level].write_latency == 0)			\
		return 0;						\
									\
	if (a == &dev_attr_access##level##_read_bandwidth.attr &&	\
	    cxlr->coord[level].read_bandwidth == 0)			\
		return 0;						\
									\
	if (a == &dev_attr_access##level##_write_bandwidth.attr &&	\
	    cxlr->coord[level].write_bandwidth == 0)			\
		return 0;						\
									\
	return a->mode;							\
}

ACCESS_VISIBLE(0);
ACCESS_VISIBLE(1);

static const struct attribute_group cxl_region_access0_coordinate_group = {
	.name = "access0",
	.attrs = access0_coordinate_attrs,
	.is_visible = cxl_region_access0_coordinate_visible,
};

static const struct attribute_group *get_cxl_region_access0_group(void)
{
	return &cxl_region_access0_coordinate_group;
}

static const struct attribute_group cxl_region_access1_coordinate_group = {
	.name = "access1",
	.attrs = access1_coordinate_attrs,
	.is_visible = cxl_region_access1_coordinate_visible,
};

static const struct attribute_group *get_cxl_region_access1_group(void)
{
	return &cxl_region_access1_coordinate_group;
}

static ssize_t uuid_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;
	ssize_t rc;

	rc = down_read_interruptible(&cxl_region_rwsem);
	if (rc)
		return rc;
	if (cxlr->mode != CXL_DECODER_PMEM)
		rc = sysfs_emit(buf, "\n");
	else
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

static struct cxl_region_ref *cxl_rr_load(struct cxl_port *port,
					  struct cxl_region *cxlr)
{
	return xa_load(&port->regions, (unsigned long)cxlr);
}

static int cxl_region_invalidate_memregion(struct cxl_region *cxlr)
{
	if (!cpu_cache_has_invalidate_memregion()) {
		if (IS_ENABLED(CONFIG_CXL_REGION_INVALIDATION_TEST)) {
			dev_info_once(
				&cxlr->dev,
				"Bypassing cpu_cache_invalidate_memregion() for testing!\n");
			return 0;
		} else {
			dev_WARN(&cxlr->dev,
				 "Failed to synchronize CPU cache state\n");
			return -ENXIO;
		}
	}

	cpu_cache_invalidate_memregion(IORES_DESC_CXL);
	return 0;
}

static void cxl_region_decode_reset(struct cxl_region *cxlr, int count)
{
	struct cxl_region_params *p = &cxlr->params;
	int i;

	/*
	 * Before region teardown attempt to flush, evict any data cached for
	 * this region, or scream loudly about missing arch / platform support
	 * for CXL teardown.
	 */
	cxl_region_invalidate_memregion(cxlr);

	for (i = count - 1; i >= 0; i--) {
		struct cxl_endpoint_decoder *cxled = p->targets[i];
		struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
		struct cxl_port *iter = cxled_to_port(cxled);
		struct cxl_dev_state *cxlds = cxlmd->cxlds;
		struct cxl_ep *ep;

		if (cxlds->rcd)
			goto endpoint_reset;

		while (!is_cxl_root(to_cxl_port(iter->dev.parent)))
			iter = to_cxl_port(iter->dev.parent);

		for (ep = cxl_ep_load(iter, cxlmd); iter;
		     iter = ep->next, ep = cxl_ep_load(iter, cxlmd)) {
			struct cxl_region_ref *cxl_rr;
			struct cxl_decoder *cxld;

			cxl_rr = cxl_rr_load(iter, cxlr);
			cxld = cxl_rr->decoder;
			if (cxld->reset)
				cxld->reset(cxld);
			set_bit(CXL_REGION_F_NEEDS_RESET, &cxlr->flags);
		}

endpoint_reset:
		cxled->cxld.reset(&cxled->cxld);
		set_bit(CXL_REGION_F_NEEDS_RESET, &cxlr->flags);
	}

	/* all decoders associated with this region have been torn down */
	clear_bit(CXL_REGION_F_NEEDS_RESET, &cxlr->flags);
}

static int commit_decoder(struct cxl_decoder *cxld)
{
	struct cxl_switch_decoder *cxlsd = NULL;

	if (cxld->commit)
		return cxld->commit(cxld);

	if (is_switch_decoder(&cxld->dev))
		cxlsd = to_cxl_switch_decoder(&cxld->dev);

	if (dev_WARN_ONCE(&cxld->dev, !cxlsd || cxlsd->nr_targets > 1,
			  "->commit() is required\n"))
		return -ENXIO;
	return 0;
}

static int cxl_region_decode_commit(struct cxl_region *cxlr)
{
	struct cxl_region_params *p = &cxlr->params;
	int i, rc = 0;

	for (i = 0; i < p->nr_targets; i++) {
		struct cxl_endpoint_decoder *cxled = p->targets[i];
		struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
		struct cxl_region_ref *cxl_rr;
		struct cxl_decoder *cxld;
		struct cxl_port *iter;
		struct cxl_ep *ep;

		/* commit bottom up */
		for (iter = cxled_to_port(cxled); !is_cxl_root(iter);
		     iter = to_cxl_port(iter->dev.parent)) {
			cxl_rr = cxl_rr_load(iter, cxlr);
			cxld = cxl_rr->decoder;
			rc = commit_decoder(cxld);
			if (rc)
				break;
		}

		if (rc) {
			/* programming @iter failed, teardown */
			for (ep = cxl_ep_load(iter, cxlmd); ep && iter;
			     iter = ep->next, ep = cxl_ep_load(iter, cxlmd)) {
				cxl_rr = cxl_rr_load(iter, cxlr);
				cxld = cxl_rr->decoder;
				if (cxld->reset)
					cxld->reset(cxld);
			}

			cxled->cxld.reset(&cxled->cxld);
			goto err;
		}
	}

	return 0;

err:
	/* undo the targets that were successfully committed */
	cxl_region_decode_reset(cxlr, i);
	return rc;
}

static ssize_t commit_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t len)
{
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;
	bool commit;
	ssize_t rc;

	rc = kstrtobool(buf, &commit);
	if (rc)
		return rc;

	rc = down_write_killable(&cxl_region_rwsem);
	if (rc)
		return rc;

	/* Already in the requested state? */
	if (commit && p->state >= CXL_CONFIG_COMMIT)
		goto out;
	if (!commit && p->state < CXL_CONFIG_COMMIT)
		goto out;

	/* Not ready to commit? */
	if (commit && p->state < CXL_CONFIG_ACTIVE) {
		rc = -ENXIO;
		goto out;
	}

	/*
	 * Invalidate caches before region setup to drop any speculative
	 * consumption of this address space
	 */
	rc = cxl_region_invalidate_memregion(cxlr);
	if (rc)
		goto out;

	if (commit) {
		rc = cxl_region_decode_commit(cxlr);
		if (rc == 0)
			p->state = CXL_CONFIG_COMMIT;
	} else {
		p->state = CXL_CONFIG_RESET_PENDING;
		up_write(&cxl_region_rwsem);
		device_release_driver(&cxlr->dev);
		down_write(&cxl_region_rwsem);

		/*
		 * The lock was dropped, so need to revalidate that the reset is
		 * still pending.
		 */
		if (p->state == CXL_CONFIG_RESET_PENDING) {
			cxl_region_decode_reset(cxlr, p->interleave_ways);
			p->state = CXL_CONFIG_ACTIVE;
		}
	}

out:
	up_write(&cxl_region_rwsem);

	if (rc)
		return rc;
	return len;
}

static ssize_t commit_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;
	ssize_t rc;

	rc = down_read_interruptible(&cxl_region_rwsem);
	if (rc)
		return rc;
	rc = sysfs_emit(buf, "%d\n", p->state >= CXL_CONFIG_COMMIT);
	up_read(&cxl_region_rwsem);

	return rc;
}
static DEVICE_ATTR_RW(commit);

static umode_t cxl_region_visible(struct kobject *kobj, struct attribute *a,
				  int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cxl_region *cxlr = to_cxl_region(dev);

	/*
	 * Support tooling that expects to find a 'uuid' attribute for all
	 * regions regardless of mode.
	 */
	if (a == &dev_attr_uuid.attr && cxlr->mode != CXL_DECODER_PMEM)
		return 0444;
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
	unsigned int val, save;
	int rc;
	u8 iw;

	rc = kstrtouint(buf, 0, &val);
	if (rc)
		return rc;

	rc = ways_to_eiw(val, &iw);
	if (rc)
		return rc;

	/*
	 * Even for x3, x6, and x12 interleaves the region interleave must be a
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

	rc = granularity_to_eig(val, &ig);
	if (rc)
		return rc;

	/*
	 * When the host-bridge is interleaved, disallow region granularity !=
	 * root granularity. Regions with a granularity less than the root
	 * interleave result in needing multiple endpoints to support a single
	 * slot in the interleave (possible to support in the future). Regions
	 * with a granularity greater than the root interleave result in invalid
	 * DPA translations (invalid to support).
	 */
	if (cxld->interleave_ways > 1 && val != cxld->interleave_granularity)
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

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct cxl_region *cxlr = to_cxl_region(dev);

	return sysfs_emit(buf, "%s\n", cxl_decoder_mode_name(cxlr->mode));
}
static DEVICE_ATTR_RO(mode);

static int alloc_hpa(struct cxl_region *cxlr, resource_size_t size)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(cxlr->dev.parent);
	struct cxl_region_params *p = &cxlr->params;
	struct resource *res;
	u64 remainder = 0;

	lockdep_assert_held_write(&cxl_region_rwsem);

	/* Nothing to do... */
	if (p->res && resource_size(p->res) == size)
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

	div64_u64_rem(size, (u64)SZ_256M * p->interleave_ways, &remainder);
	if (remainder)
		return -EINVAL;

	res = alloc_free_mem_region(cxlrd->res, size, SZ_256M,
				    dev_name(&cxlr->dev));
	if (IS_ERR(res)) {
		dev_dbg(&cxlr->dev,
			"HPA allocation error (%ld) for size:%pap in %s %pr\n",
			PTR_ERR(res), &size, cxlrd->res->name, cxlrd->res);
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
		/*
		 * Autodiscovered regions may not have been able to insert their
		 * resource.
		 */
		if (p->res->parent)
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
	&dev_attr_commit.attr,
	&dev_attr_interleave_ways.attr,
	&dev_attr_interleave_granularity.attr,
	&dev_attr_resource.attr,
	&dev_attr_size.attr,
	&dev_attr_mode.attr,
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

static int check_commit_order(struct device *dev, void *data)
{
	struct cxl_decoder *cxld = to_cxl_decoder(dev);

	/*
	 * if port->commit_end is not the only free decoder, then out of
	 * order shutdown has occurred, block further allocations until
	 * that is resolved
	 */
	if (((cxld->flags & CXL_DECODER_F_ENABLE) == 0))
		return -EBUSY;
	return 0;
}

static int match_free_decoder(struct device *dev, const void *data)
{
	struct cxl_port *port = to_cxl_port(dev->parent);
	struct cxl_decoder *cxld;
	int rc;

	if (!is_switch_decoder(dev))
		return 0;

	cxld = to_cxl_decoder(dev);

	if (cxld->id != port->commit_end + 1)
		return 0;

	if (cxld->region) {
		dev_dbg(dev->parent,
			"next decoder to commit (%s) is already reserved (%s)\n",
			dev_name(dev), dev_name(&cxld->region->dev));
		return 0;
	}

	rc = device_for_each_child_reverse_from(dev->parent, dev, NULL,
						check_commit_order);
	if (rc) {
		dev_dbg(dev->parent,
			"unable to allocate %s due to out of order shutdown\n",
			dev_name(dev));
		return 0;
	}
	return 1;
}

static int match_auto_decoder(struct device *dev, const void *data)
{
	const struct cxl_region_params *p = data;
	struct cxl_decoder *cxld;
	struct range *r;

	if (!is_switch_decoder(dev))
		return 0;

	cxld = to_cxl_decoder(dev);
	r = &cxld->hpa_range;

	if (p->res && p->res->start == r->start && p->res->end == r->end)
		return 1;

	return 0;
}

static struct cxl_decoder *
cxl_region_find_decoder(struct cxl_port *port,
			struct cxl_endpoint_decoder *cxled,
			struct cxl_region *cxlr)
{
	struct device *dev;

	if (port == cxled_to_port(cxled))
		return &cxled->cxld;

	if (test_bit(CXL_REGION_F_AUTO, &cxlr->flags))
		dev = device_find_child(&port->dev, &cxlr->params,
					match_auto_decoder);
	else
		dev = device_find_child(&port->dev, NULL, match_free_decoder);
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

static bool auto_order_ok(struct cxl_port *port, struct cxl_region *cxlr_iter,
			  struct cxl_decoder *cxld)
{
	struct cxl_region_ref *rr = cxl_rr_load(port, cxlr_iter);
	struct cxl_decoder *cxld_iter = rr->decoder;

	/*
	 * Allow the out of order assembly of auto-discovered regions.
	 * Per CXL Spec 3.1 8.2.4.20.12 software must commit decoders
	 * in HPA order. Confirm that the decoder with the lesser HPA
	 * starting address has the lesser id.
	 */
	dev_dbg(&cxld->dev, "check for HPA violation %s:%d < %s:%d\n",
		dev_name(&cxld->dev), cxld->id,
		dev_name(&cxld_iter->dev), cxld_iter->id);

	if (cxld_iter->id > cxld->id)
		return true;

	return false;
}

static struct cxl_region_ref *
alloc_region_ref(struct cxl_port *port, struct cxl_region *cxlr,
		 struct cxl_endpoint_decoder *cxled)
{
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_region_ref *cxl_rr, *iter;
	unsigned long index;
	int rc;

	xa_for_each(&port->regions, index, iter) {
		struct cxl_region_params *ip = &iter->region->params;

		if (!ip->res || ip->res->start < p->res->start)
			continue;

		if (test_bit(CXL_REGION_F_AUTO, &cxlr->flags)) {
			struct cxl_decoder *cxld;

			cxld = cxl_region_find_decoder(port, cxled, cxlr);
			if (auto_order_ok(port, iter->region, cxld))
				continue;
		}
		dev_dbg(&cxlr->dev, "%s: HPA order violation %s:%pr vs %pr\n",
			dev_name(&port->dev),
			dev_name(&iter->region->dev), ip->res, p->res);

		return ERR_PTR(-EBUSY);
	}

	cxl_rr = kzalloc(sizeof(*cxl_rr), GFP_KERNEL);
	if (!cxl_rr)
		return ERR_PTR(-ENOMEM);
	cxl_rr->port = port;
	cxl_rr->region = cxlr;
	cxl_rr->nr_targets = 1;
	xa_init(&cxl_rr->endpoints);

	rc = xa_insert(&port->regions, (unsigned long)cxlr, cxl_rr, GFP_KERNEL);
	if (rc) {
		dev_dbg(&cxlr->dev,
			"%s: failed to track region reference: %d\n",
			dev_name(&port->dev), rc);
		kfree(cxl_rr);
		return ERR_PTR(rc);
	}

	return cxl_rr;
}

static void cxl_rr_free_decoder(struct cxl_region_ref *cxl_rr)
{
	struct cxl_region *cxlr = cxl_rr->region;
	struct cxl_decoder *cxld = cxl_rr->decoder;

	if (!cxld)
		return;

	dev_WARN_ONCE(&cxlr->dev, cxld->region != cxlr, "region mismatch\n");
	if (cxld->region == cxlr) {
		cxld->region = NULL;
		put_device(&cxlr->dev);
	}
}

static void free_region_ref(struct cxl_region_ref *cxl_rr)
{
	struct cxl_port *port = cxl_rr->port;
	struct cxl_region *cxlr = cxl_rr->region;

	cxl_rr_free_decoder(cxl_rr);
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

	if (ep) {
		rc = xa_insert(&cxl_rr->endpoints, (unsigned long)cxled, ep,
			       GFP_KERNEL);
		if (rc)
			return rc;
	}
	cxl_rr->nr_eps++;

	if (!cxld->region) {
		cxld->region = cxlr;
		get_device(&cxlr->dev);
	}

	return 0;
}

static int cxl_rr_alloc_decoder(struct cxl_port *port, struct cxl_region *cxlr,
				struct cxl_endpoint_decoder *cxled,
				struct cxl_region_ref *cxl_rr)
{
	struct cxl_decoder *cxld;

	cxld = cxl_region_find_decoder(port, cxled, cxlr);
	if (!cxld) {
		dev_dbg(&cxlr->dev, "%s: no decoder available\n",
			dev_name(&port->dev));
		return -EBUSY;
	}

	if (cxld->region) {
		dev_dbg(&cxlr->dev, "%s: %s already attached to %s\n",
			dev_name(&port->dev), dev_name(&cxld->dev),
			dev_name(&cxld->region->dev));
		return -EBUSY;
	}

	/*
	 * Endpoints should already match the region type, but backstop that
	 * assumption with an assertion. Switch-decoders change mapping-type
	 * based on what is mapped when they are assigned to a region.
	 */
	dev_WARN_ONCE(&cxlr->dev,
		      port == cxled_to_port(cxled) &&
			      cxld->target_type != cxlr->type,
		      "%s:%s mismatch decoder type %d -> %d\n",
		      dev_name(&cxled_to_memdev(cxled)->dev),
		      dev_name(&cxld->dev), cxld->target_type, cxlr->type);
	cxld->target_type = cxlr->type;
	cxl_rr->decoder = cxld;
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
 *
 * - validate that there are no other regions with a higher HPA already
 *   associated with @port
 * - establish a region reference if one is not already present
 *
 *   - additionally allocate a decoder instance that will host @cxlr on
 *     @port
 *
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
	struct cxl_region_ref *cxl_rr;
	bool nr_targets_inc = false;
	struct cxl_decoder *cxld;
	unsigned long index;
	int rc = -EBUSY;

	lockdep_assert_held_write(&cxl_region_rwsem);

	cxl_rr = cxl_rr_load(port, cxlr);
	if (cxl_rr) {
		struct cxl_ep *ep_iter;
		int found = 0;

		/*
		 * Walk the existing endpoints that have been attached to
		 * @cxlr at @port and see if they share the same 'next' port
		 * in the downstream direction. I.e. endpoints that share common
		 * upstream switch.
		 */
		xa_for_each(&cxl_rr->endpoints, index, ep_iter) {
			if (ep_iter == ep)
				continue;
			if (ep_iter->next == ep->next) {
				found++;
				break;
			}
		}

		/*
		 * New target port, or @port is an endpoint port that always
		 * accounts its own local decode as a target.
		 */
		if (!found || !ep->next) {
			cxl_rr->nr_targets++;
			nr_targets_inc = true;
		}
	} else {
		cxl_rr = alloc_region_ref(port, cxlr, cxled);
		if (IS_ERR(cxl_rr)) {
			dev_dbg(&cxlr->dev,
				"%s: failed to allocate region reference\n",
				dev_name(&port->dev));
			return PTR_ERR(cxl_rr);
		}
		nr_targets_inc = true;

		rc = cxl_rr_alloc_decoder(port, cxlr, cxled, cxl_rr);
		if (rc)
			goto out_erase;
	}
	cxld = cxl_rr->decoder;

	/*
	 * the number of targets should not exceed the target_count
	 * of the decoder
	 */
	if (is_switch_decoder(&cxld->dev)) {
		struct cxl_switch_decoder *cxlsd;

		cxlsd = to_cxl_switch_decoder(&cxld->dev);
		if (cxl_rr->nr_targets > cxlsd->nr_targets) {
			dev_dbg(&cxlr->dev,
				"%s:%s %s add: %s:%s @ %d overflows targets: %d\n",
				dev_name(port->uport_dev), dev_name(&port->dev),
				dev_name(&cxld->dev), dev_name(&cxlmd->dev),
				dev_name(&cxled->cxld.dev), pos,
				cxlsd->nr_targets);
			rc = -ENXIO;
			goto out_erase;
		}
	}

	rc = cxl_rr_ep_add(cxl_rr, cxled);
	if (rc) {
		dev_dbg(&cxlr->dev,
			"%s: failed to track endpoint %s:%s reference\n",
			dev_name(&port->dev), dev_name(&cxlmd->dev),
			dev_name(&cxld->dev));
		goto out_erase;
	}

	dev_dbg(&cxlr->dev,
		"%s:%s %s add: %s:%s @ %d next: %s nr_eps: %d nr_targets: %d\n",
		dev_name(port->uport_dev), dev_name(&port->dev),
		dev_name(&cxld->dev), dev_name(&cxlmd->dev),
		dev_name(&cxled->cxld.dev), pos,
		ep ? ep->next ? dev_name(ep->next->uport_dev) :
				      dev_name(&cxlmd->dev) :
			   "none",
		cxl_rr->nr_eps, cxl_rr->nr_targets);

	return 0;
out_erase:
	if (nr_targets_inc)
		cxl_rr->nr_targets--;
	if (cxl_rr->nr_eps == 0)
		free_region_ref(cxl_rr);
	return rc;
}

static void cxl_port_detach_region(struct cxl_port *port,
				   struct cxl_region *cxlr,
				   struct cxl_endpoint_decoder *cxled)
{
	struct cxl_region_ref *cxl_rr;
	struct cxl_ep *ep = NULL;

	lockdep_assert_held_write(&cxl_region_rwsem);

	cxl_rr = cxl_rr_load(port, cxlr);
	if (!cxl_rr)
		return;

	/*
	 * Endpoint ports do not carry cxl_ep references, and they
	 * never target more than one endpoint by definition
	 */
	if (cxl_rr->decoder == &cxled->cxld)
		cxl_rr->nr_eps--;
	else
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

static int check_last_peer(struct cxl_endpoint_decoder *cxled,
			   struct cxl_ep *ep, struct cxl_region_ref *cxl_rr,
			   int distance)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_region *cxlr = cxl_rr->region;
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_endpoint_decoder *cxled_peer;
	struct cxl_port *port = cxl_rr->port;
	struct cxl_memdev *cxlmd_peer;
	struct cxl_ep *ep_peer;
	int pos = cxled->pos;

	/*
	 * If this position wants to share a dport with the last endpoint mapped
	 * then that endpoint, at index 'position - distance', must also be
	 * mapped by this dport.
	 */
	if (pos < distance) {
		dev_dbg(&cxlr->dev, "%s:%s: cannot host %s:%s at %d\n",
			dev_name(port->uport_dev), dev_name(&port->dev),
			dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev), pos);
		return -ENXIO;
	}
	cxled_peer = p->targets[pos - distance];
	cxlmd_peer = cxled_to_memdev(cxled_peer);
	ep_peer = cxl_ep_load(port, cxlmd_peer);
	if (ep->dport != ep_peer->dport) {
		dev_dbg(&cxlr->dev,
			"%s:%s: %s:%s pos %d mismatched peer %s:%s\n",
			dev_name(port->uport_dev), dev_name(&port->dev),
			dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev), pos,
			dev_name(&cxlmd_peer->dev),
			dev_name(&cxled_peer->cxld.dev));
		return -ENXIO;
	}

	return 0;
}

static int check_interleave_cap(struct cxl_decoder *cxld, int iw, int ig)
{
	struct cxl_port *port = to_cxl_port(cxld->dev.parent);
	struct cxl_hdm *cxlhdm = dev_get_drvdata(&port->dev);
	unsigned int interleave_mask;
	u8 eiw;
	u16 eig;
	int high_pos, low_pos;

	if (!test_bit(iw, &cxlhdm->iw_cap_mask))
		return -ENXIO;
	/*
	 * Per CXL specification r3.1(8.2.4.20.13 Decoder Protection),
	 * if eiw < 8:
	 *   DPAOFFSET[51: eig + 8] = HPAOFFSET[51: eig + 8 + eiw]
	 *   DPAOFFSET[eig + 7: 0]  = HPAOFFSET[eig + 7: 0]
	 *
	 *   when the eiw is 0, all the bits of HPAOFFSET[51: 0] are used, the
	 *   interleave bits are none.
	 *
	 * if eiw >= 8:
	 *   DPAOFFSET[51: eig + 8] = HPAOFFSET[51: eig + eiw] / 3
	 *   DPAOFFSET[eig + 7: 0]  = HPAOFFSET[eig + 7: 0]
	 *
	 *   when the eiw is 8, all the bits of HPAOFFSET[51: 0] are used, the
	 *   interleave bits are none.
	 */
	ways_to_eiw(iw, &eiw);
	if (eiw == 0 || eiw == 8)
		return 0;

	granularity_to_eig(ig, &eig);
	if (eiw > 8)
		high_pos = eiw + eig - 1;
	else
		high_pos = eiw + eig + 7;
	low_pos = eig + 8;
	interleave_mask = GENMASK(high_pos, low_pos);
	if (interleave_mask & ~cxlhdm->interleave_mask)
		return -ENXIO;

	return 0;
}

static int cxl_port_setup_targets(struct cxl_port *port,
				  struct cxl_region *cxlr,
				  struct cxl_endpoint_decoder *cxled)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(cxlr->dev.parent);
	int parent_iw, parent_ig, ig, iw, rc, inc = 0, pos = cxled->pos;
	struct cxl_port *parent_port = to_cxl_port(port->dev.parent);
	struct cxl_region_ref *cxl_rr = cxl_rr_load(port, cxlr);
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_ep *ep = cxl_ep_load(port, cxlmd);
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_decoder *cxld = cxl_rr->decoder;
	struct cxl_switch_decoder *cxlsd;
	struct cxl_port *iter = port;
	u16 eig, peig;
	u8 eiw, peiw;

	/*
	 * While root level decoders support x3, x6, x12, switch level
	 * decoders only support powers of 2 up to x16.
	 */
	if (!is_power_of_2(cxl_rr->nr_targets)) {
		dev_dbg(&cxlr->dev, "%s:%s: invalid target count %d\n",
			dev_name(port->uport_dev), dev_name(&port->dev),
			cxl_rr->nr_targets);
		return -EINVAL;
	}

	cxlsd = to_cxl_switch_decoder(&cxld->dev);
	if (cxl_rr->nr_targets_set) {
		int i, distance = 1;
		struct cxl_region_ref *cxl_rr_iter;

		/*
		 * The "distance" between peer downstream ports represents which
		 * endpoint positions in the region interleave a given port can
		 * host.
		 *
		 * For example, at the root of a hierarchy the distance is
		 * always 1 as every index targets a different host-bridge. At
		 * each subsequent switch level those ports map every Nth region
		 * position where N is the width of the switch == distance.
		 */
		do {
			cxl_rr_iter = cxl_rr_load(iter, cxlr);
			distance *= cxl_rr_iter->nr_targets;
			iter = to_cxl_port(iter->dev.parent);
		} while (!is_cxl_root(iter));
		distance *= cxlrd->cxlsd.cxld.interleave_ways;

		for (i = 0; i < cxl_rr->nr_targets_set; i++)
			if (ep->dport == cxlsd->target[i]) {
				rc = check_last_peer(cxled, ep, cxl_rr,
						     distance);
				if (rc)
					return rc;
				goto out_target_set;
			}
		goto add_target;
	}

	if (is_cxl_root(parent_port)) {
		/*
		 * Root decoder IG is always set to value in CFMWS which
		 * may be different than this region's IG.  We can use the
		 * region's IG here since interleave_granularity_store()
		 * does not allow interleaved host-bridges with
		 * root IG != region IG.
		 */
		parent_ig = p->interleave_granularity;
		parent_iw = cxlrd->cxlsd.cxld.interleave_ways;
		/*
		 * For purposes of address bit routing, use power-of-2 math for
		 * switch ports.
		 */
		if (!is_power_of_2(parent_iw))
			parent_iw /= 3;
	} else {
		struct cxl_region_ref *parent_rr;
		struct cxl_decoder *parent_cxld;

		parent_rr = cxl_rr_load(parent_port, cxlr);
		parent_cxld = parent_rr->decoder;
		parent_ig = parent_cxld->interleave_granularity;
		parent_iw = parent_cxld->interleave_ways;
	}

	rc = granularity_to_eig(parent_ig, &peig);
	if (rc) {
		dev_dbg(&cxlr->dev, "%s:%s: invalid parent granularity: %d\n",
			dev_name(parent_port->uport_dev),
			dev_name(&parent_port->dev), parent_ig);
		return rc;
	}

	rc = ways_to_eiw(parent_iw, &peiw);
	if (rc) {
		dev_dbg(&cxlr->dev, "%s:%s: invalid parent interleave: %d\n",
			dev_name(parent_port->uport_dev),
			dev_name(&parent_port->dev), parent_iw);
		return rc;
	}

	iw = cxl_rr->nr_targets;
	rc = ways_to_eiw(iw, &eiw);
	if (rc) {
		dev_dbg(&cxlr->dev, "%s:%s: invalid port interleave: %d\n",
			dev_name(port->uport_dev), dev_name(&port->dev), iw);
		return rc;
	}

	/*
	 * Interleave granularity is a multiple of @parent_port granularity.
	 * Multiplier is the parent port interleave ways.
	 */
	rc = granularity_to_eig(parent_ig * parent_iw, &eig);
	if (rc) {
		dev_dbg(&cxlr->dev,
			"%s: invalid granularity calculation (%d * %d)\n",
			dev_name(&parent_port->dev), parent_ig, parent_iw);
		return rc;
	}

	rc = eig_to_granularity(eig, &ig);
	if (rc) {
		dev_dbg(&cxlr->dev, "%s:%s: invalid interleave: %d\n",
			dev_name(port->uport_dev), dev_name(&port->dev),
			256 << eig);
		return rc;
	}

	if (iw > 8 || iw > cxlsd->nr_targets) {
		dev_dbg(&cxlr->dev,
			"%s:%s:%s: ways: %d overflows targets: %d\n",
			dev_name(port->uport_dev), dev_name(&port->dev),
			dev_name(&cxld->dev), iw, cxlsd->nr_targets);
		return -ENXIO;
	}

	if (test_bit(CXL_REGION_F_AUTO, &cxlr->flags)) {
		if (cxld->interleave_ways != iw ||
		    cxld->interleave_granularity != ig ||
		    cxld->hpa_range.start != p->res->start ||
		    cxld->hpa_range.end != p->res->end ||
		    ((cxld->flags & CXL_DECODER_F_ENABLE) == 0)) {
			dev_err(&cxlr->dev,
				"%s:%s %s expected iw: %d ig: %d %pr\n",
				dev_name(port->uport_dev), dev_name(&port->dev),
				__func__, iw, ig, p->res);
			dev_err(&cxlr->dev,
				"%s:%s %s got iw: %d ig: %d state: %s %#llx:%#llx\n",
				dev_name(port->uport_dev), dev_name(&port->dev),
				__func__, cxld->interleave_ways,
				cxld->interleave_granularity,
				(cxld->flags & CXL_DECODER_F_ENABLE) ?
					"enabled" :
					"disabled",
				cxld->hpa_range.start, cxld->hpa_range.end);
			return -ENXIO;
		}
	} else {
		rc = check_interleave_cap(cxld, iw, ig);
		if (rc) {
			dev_dbg(&cxlr->dev,
				"%s:%s iw: %d ig: %d is not supported\n",
				dev_name(port->uport_dev),
				dev_name(&port->dev), iw, ig);
			return rc;
		}

		cxld->interleave_ways = iw;
		cxld->interleave_granularity = ig;
		cxld->hpa_range = (struct range) {
			.start = p->res->start,
			.end = p->res->end,
		};
	}
	dev_dbg(&cxlr->dev, "%s:%s iw: %d ig: %d\n", dev_name(port->uport_dev),
		dev_name(&port->dev), iw, ig);
add_target:
	if (cxl_rr->nr_targets_set == cxl_rr->nr_targets) {
		dev_dbg(&cxlr->dev,
			"%s:%s: targets full trying to add %s:%s at %d\n",
			dev_name(port->uport_dev), dev_name(&port->dev),
			dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev), pos);
		return -ENXIO;
	}
	if (test_bit(CXL_REGION_F_AUTO, &cxlr->flags)) {
		if (cxlsd->target[cxl_rr->nr_targets_set] != ep->dport) {
			dev_dbg(&cxlr->dev, "%s:%s: %s expected %s at %d\n",
				dev_name(port->uport_dev), dev_name(&port->dev),
				dev_name(&cxlsd->cxld.dev),
				dev_name(ep->dport->dport_dev),
				cxl_rr->nr_targets_set);
			return -ENXIO;
		}
	} else
		cxlsd->target[cxl_rr->nr_targets_set] = ep->dport;
	inc = 1;
out_target_set:
	cxl_rr->nr_targets_set += inc;
	dev_dbg(&cxlr->dev, "%s:%s target[%d] = %s for %s:%s @ %d\n",
		dev_name(port->uport_dev), dev_name(&port->dev),
		cxl_rr->nr_targets_set - 1, dev_name(ep->dport->dport_dev),
		dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev), pos);

	return 0;
}

static void cxl_port_reset_targets(struct cxl_port *port,
				   struct cxl_region *cxlr)
{
	struct cxl_region_ref *cxl_rr = cxl_rr_load(port, cxlr);
	struct cxl_decoder *cxld;

	/*
	 * After the last endpoint has been detached the entire cxl_rr may now
	 * be gone.
	 */
	if (!cxl_rr)
		return;
	cxl_rr->nr_targets_set = 0;

	cxld = cxl_rr->decoder;
	cxld->hpa_range = (struct range) {
		.start = 0,
		.end = -1,
	};
}

static void cxl_region_teardown_targets(struct cxl_region *cxlr)
{
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_endpoint_decoder *cxled;
	struct cxl_dev_state *cxlds;
	struct cxl_memdev *cxlmd;
	struct cxl_port *iter;
	struct cxl_ep *ep;
	int i;

	/*
	 * In the auto-discovery case skip automatic teardown since the
	 * address space is already active
	 */
	if (test_bit(CXL_REGION_F_AUTO, &cxlr->flags))
		return;

	for (i = 0; i < p->nr_targets; i++) {
		cxled = p->targets[i];
		cxlmd = cxled_to_memdev(cxled);
		cxlds = cxlmd->cxlds;

		if (cxlds->rcd)
			continue;

		iter = cxled_to_port(cxled);
		while (!is_cxl_root(to_cxl_port(iter->dev.parent)))
			iter = to_cxl_port(iter->dev.parent);

		for (ep = cxl_ep_load(iter, cxlmd); iter;
		     iter = ep->next, ep = cxl_ep_load(iter, cxlmd))
			cxl_port_reset_targets(iter, cxlr);
	}
}

static int cxl_region_setup_targets(struct cxl_region *cxlr)
{
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_endpoint_decoder *cxled;
	struct cxl_dev_state *cxlds;
	int i, rc, rch = 0, vh = 0;
	struct cxl_memdev *cxlmd;
	struct cxl_port *iter;
	struct cxl_ep *ep;

	for (i = 0; i < p->nr_targets; i++) {
		cxled = p->targets[i];
		cxlmd = cxled_to_memdev(cxled);
		cxlds = cxlmd->cxlds;

		/* validate that all targets agree on topology */
		if (!cxlds->rcd) {
			vh++;
		} else {
			rch++;
			continue;
		}

		iter = cxled_to_port(cxled);
		while (!is_cxl_root(to_cxl_port(iter->dev.parent)))
			iter = to_cxl_port(iter->dev.parent);

		/*
		 * Descend the topology tree programming / validating
		 * targets while looking for conflicts.
		 */
		for (ep = cxl_ep_load(iter, cxlmd); iter;
		     iter = ep->next, ep = cxl_ep_load(iter, cxlmd)) {
			rc = cxl_port_setup_targets(iter, cxlr, cxled);
			if (rc) {
				cxl_region_teardown_targets(cxlr);
				return rc;
			}
		}
	}

	if (rch && vh) {
		dev_err(&cxlr->dev, "mismatched CXL topologies detected\n");
		cxl_region_teardown_targets(cxlr);
		return -ENXIO;
	}

	return 0;
}

static int cxl_region_validate_position(struct cxl_region *cxlr,
					struct cxl_endpoint_decoder *cxled,
					int pos)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_region_params *p = &cxlr->params;
	int i;

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

		cxled_target = p->targets[i];
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

	return 0;
}

static int cxl_region_attach_position(struct cxl_region *cxlr,
				      struct cxl_root_decoder *cxlrd,
				      struct cxl_endpoint_decoder *cxled,
				      const struct cxl_dport *dport, int pos)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_switch_decoder *cxlsd = &cxlrd->cxlsd;
	struct cxl_decoder *cxld = &cxlsd->cxld;
	int iw = cxld->interleave_ways;
	struct cxl_port *iter;
	int rc;

	if (dport != cxlrd->cxlsd.target[pos % iw]) {
		dev_dbg(&cxlr->dev, "%s:%s invalid target position for %s\n",
			dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev),
			dev_name(&cxlrd->cxlsd.cxld.dev));
		return -ENXIO;
	}

	for (iter = cxled_to_port(cxled); !is_cxl_root(iter);
	     iter = to_cxl_port(iter->dev.parent)) {
		rc = cxl_port_attach_region(iter, cxlr, cxled, pos);
		if (rc)
			goto err;
	}

	return 0;

err:
	for (iter = cxled_to_port(cxled); !is_cxl_root(iter);
	     iter = to_cxl_port(iter->dev.parent))
		cxl_port_detach_region(iter, cxlr, cxled);
	return rc;
}

static int cxl_region_attach_auto(struct cxl_region *cxlr,
				  struct cxl_endpoint_decoder *cxled, int pos)
{
	struct cxl_region_params *p = &cxlr->params;

	if (cxled->state != CXL_DECODER_STATE_AUTO) {
		dev_err(&cxlr->dev,
			"%s: unable to add decoder to autodetected region\n",
			dev_name(&cxled->cxld.dev));
		return -EINVAL;
	}

	if (pos >= 0) {
		dev_dbg(&cxlr->dev, "%s: expected auto position, not %d\n",
			dev_name(&cxled->cxld.dev), pos);
		return -EINVAL;
	}

	if (p->nr_targets >= p->interleave_ways) {
		dev_err(&cxlr->dev, "%s: no more target slots available\n",
			dev_name(&cxled->cxld.dev));
		return -ENXIO;
	}

	/*
	 * Temporarily record the endpoint decoder into the target array. Yes,
	 * this means that userspace can view devices in the wrong position
	 * before the region activates, and must be careful to understand when
	 * it might be racing region autodiscovery.
	 */
	pos = p->nr_targets;
	p->targets[pos] = cxled;
	cxled->pos = pos;
	p->nr_targets++;

	return 0;
}

static int cmp_interleave_pos(const void *a, const void *b)
{
	struct cxl_endpoint_decoder *cxled_a = *(typeof(cxled_a) *)a;
	struct cxl_endpoint_decoder *cxled_b = *(typeof(cxled_b) *)b;

	return cxled_a->pos - cxled_b->pos;
}

static struct cxl_port *next_port(struct cxl_port *port)
{
	if (!port->parent_dport)
		return NULL;
	return port->parent_dport->port;
}

static int match_switch_decoder_by_range(struct device *dev,
					 const void *data)
{
	struct cxl_switch_decoder *cxlsd;
	const struct range *r1, *r2 = data;


	if (!is_switch_decoder(dev))
		return 0;

	cxlsd = to_cxl_switch_decoder(dev);
	r1 = &cxlsd->cxld.hpa_range;

	if (is_root_decoder(dev))
		return range_contains(r1, r2);
	return (r1->start == r2->start && r1->end == r2->end);
}

static int find_pos_and_ways(struct cxl_port *port, struct range *range,
			     int *pos, int *ways)
{
	struct cxl_switch_decoder *cxlsd;
	struct cxl_port *parent;
	struct device *dev;
	int rc = -ENXIO;

	parent = next_port(port);
	if (!parent)
		return rc;

	dev = device_find_child(&parent->dev, range,
				match_switch_decoder_by_range);
	if (!dev) {
		dev_err(port->uport_dev,
			"failed to find decoder mapping %#llx-%#llx\n",
			range->start, range->end);
		return rc;
	}
	cxlsd = to_cxl_switch_decoder(dev);
	*ways = cxlsd->cxld.interleave_ways;

	for (int i = 0; i < *ways; i++) {
		if (cxlsd->target[i] == port->parent_dport) {
			*pos = i;
			rc = 0;
			break;
		}
	}
	put_device(dev);

	return rc;
}

/**
 * cxl_calc_interleave_pos() - calculate an endpoint position in a region
 * @cxled: endpoint decoder member of given region
 *
 * The endpoint position is calculated by traversing the topology from
 * the endpoint to the root decoder and iteratively applying this
 * calculation:
 *
 *    position = position * parent_ways + parent_pos;
 *
 * ...where @position is inferred from switch and root decoder target lists.
 *
 * Return: position >= 0 on success
 *	   -ENXIO on failure
 */
static int cxl_calc_interleave_pos(struct cxl_endpoint_decoder *cxled)
{
	struct cxl_port *iter, *port = cxled_to_port(cxled);
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct range *range = &cxled->cxld.hpa_range;
	int parent_ways = 0, parent_pos = 0, pos = 0;
	int rc;

	/*
	 * Example: the expected interleave order of the 4-way region shown
	 * below is: mem0, mem2, mem1, mem3
	 *
	 *		  root_port
	 *                 /      \
	 *      host_bridge_0    host_bridge_1
	 *        |    |           |    |
	 *       mem0 mem1        mem2 mem3
	 *
	 * In the example the calculator will iterate twice. The first iteration
	 * uses the mem position in the host-bridge and the ways of the host-
	 * bridge to generate the first, or local, position. The second
	 * iteration uses the host-bridge position in the root_port and the ways
	 * of the root_port to refine the position.
	 *
	 * A trace of the calculation per endpoint looks like this:
	 * mem0: pos = 0 * 2 + 0    mem2: pos = 0 * 2 + 0
	 *       pos = 0 * 2 + 0          pos = 0 * 2 + 1
	 *       pos: 0                   pos: 1
	 *
	 * mem1: pos = 0 * 2 + 1    mem3: pos = 0 * 2 + 1
	 *       pos = 1 * 2 + 0          pos = 1 * 2 + 1
	 *       pos: 2                   pos = 3
	 *
	 * Note that while this example is simple, the method applies to more
	 * complex topologies, including those with switches.
	 */

	/* Iterate from endpoint to root_port refining the position */
	for (iter = port; iter; iter = next_port(iter)) {
		if (is_cxl_root(iter))
			break;

		rc = find_pos_and_ways(iter, range, &parent_pos, &parent_ways);
		if (rc)
			return rc;

		pos = pos * parent_ways + parent_pos;
	}

	dev_dbg(&cxlmd->dev,
		"decoder:%s parent:%s port:%s range:%#llx-%#llx pos:%d\n",
		dev_name(&cxled->cxld.dev), dev_name(cxlmd->dev.parent),
		dev_name(&port->dev), range->start, range->end, pos);

	return pos;
}

static int cxl_region_sort_targets(struct cxl_region *cxlr)
{
	struct cxl_region_params *p = &cxlr->params;
	int i, rc = 0;

	for (i = 0; i < p->nr_targets; i++) {
		struct cxl_endpoint_decoder *cxled = p->targets[i];

		cxled->pos = cxl_calc_interleave_pos(cxled);
		/*
		 * Record that sorting failed, but still continue to calc
		 * cxled->pos so that follow-on code paths can reliably
		 * do p->targets[cxled->pos] to self-reference their entry.
		 */
		if (cxled->pos < 0)
			rc = -ENXIO;
	}
	/* Keep the cxlr target list in interleave position order */
	sort(p->targets, p->nr_targets, sizeof(p->targets[0]),
	     cmp_interleave_pos, NULL);

	dev_dbg(&cxlr->dev, "region sort %s\n", rc ? "failed" : "successful");
	return rc;
}

static int cxl_region_attach(struct cxl_region *cxlr,
			     struct cxl_endpoint_decoder *cxled, int pos)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(cxlr->dev.parent);
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_port *ep_port, *root_port;
	struct cxl_dport *dport;
	int rc = -ENXIO;

	rc = check_interleave_cap(&cxled->cxld, p->interleave_ways,
				  p->interleave_granularity);
	if (rc) {
		dev_dbg(&cxlr->dev, "%s iw: %d ig: %d is not supported\n",
			dev_name(&cxled->cxld.dev), p->interleave_ways,
			p->interleave_granularity);
		return rc;
	}

	if (cxled->mode != cxlr->mode) {
		dev_dbg(&cxlr->dev, "%s region mode: %d mismatch: %d\n",
			dev_name(&cxled->cxld.dev), cxlr->mode, cxled->mode);
		return -EINVAL;
	}

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

	if (p->nr_targets >= p->interleave_ways) {
		dev_dbg(&cxlr->dev, "region already has %d endpoints\n",
			p->nr_targets);
		return -EINVAL;
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

	cxl_region_perf_data_calculate(cxlr, cxled);

	if (test_bit(CXL_REGION_F_AUTO, &cxlr->flags)) {
		int i;

		rc = cxl_region_attach_auto(cxlr, cxled, pos);
		if (rc)
			return rc;

		/* await more targets to arrive... */
		if (p->nr_targets < p->interleave_ways)
			return 0;

		/*
		 * All targets are here, which implies all PCI enumeration that
		 * affects this region has been completed. Walk the topology to
		 * sort the devices into their relative region decode position.
		 */
		rc = cxl_region_sort_targets(cxlr);
		if (rc)
			return rc;

		for (i = 0; i < p->nr_targets; i++) {
			cxled = p->targets[i];
			ep_port = cxled_to_port(cxled);
			dport = cxl_find_dport_by_dev(root_port,
						      ep_port->host_bridge);
			rc = cxl_region_attach_position(cxlr, cxlrd, cxled,
							dport, i);
			if (rc)
				return rc;
		}

		rc = cxl_region_setup_targets(cxlr);
		if (rc)
			return rc;

		/*
		 * If target setup succeeds in the autodiscovery case
		 * then the region is already committed.
		 */
		p->state = CXL_CONFIG_COMMIT;
		cxl_region_shared_upstream_bandwidth_update(cxlr);

		return 0;
	}

	rc = cxl_region_validate_position(cxlr, cxled, pos);
	if (rc)
		return rc;

	rc = cxl_region_attach_position(cxlr, cxlrd, cxled, dport, pos);
	if (rc)
		return rc;

	p->targets[pos] = cxled;
	cxled->pos = pos;
	p->nr_targets++;

	if (p->nr_targets == p->interleave_ways) {
		rc = cxl_region_setup_targets(cxlr);
		if (rc)
			return rc;
		p->state = CXL_CONFIG_ACTIVE;
		cxl_region_shared_upstream_bandwidth_update(cxlr);
	}

	cxled->cxld.interleave_ways = p->interleave_ways;
	cxled->cxld.interleave_granularity = p->interleave_granularity;
	cxled->cxld.hpa_range = (struct range) {
		.start = p->res->start,
		.end = p->res->end,
	};

	if (p->nr_targets != p->interleave_ways)
		return 0;

	/*
	 * Test the auto-discovery position calculator function
	 * against this successfully created user-defined region.
	 * A fail message here means that this interleave config
	 * will fail when presented as CXL_REGION_F_AUTO.
	 */
	for (int i = 0; i < p->nr_targets; i++) {
		struct cxl_endpoint_decoder *cxled = p->targets[i];
		int test_pos;

		test_pos = cxl_calc_interleave_pos(cxled);
		dev_dbg(&cxled->cxld.dev,
			"Test cxl_calc_interleave_pos(): %s test_pos:%d cxled->pos:%d\n",
			(test_pos == cxled->pos) ? "success" : "fail",
			test_pos, cxled->pos);
	}

	return 0;
}

static int cxl_region_detach(struct cxl_endpoint_decoder *cxled)
{
	struct cxl_port *iter, *ep_port = cxled_to_port(cxled);
	struct cxl_region *cxlr = cxled->cxld.region;
	struct cxl_region_params *p;
	int rc = 0;

	lockdep_assert_held_write(&cxl_region_rwsem);

	if (!cxlr)
		return 0;

	p = &cxlr->params;
	get_device(&cxlr->dev);

	if (p->state > CXL_CONFIG_ACTIVE) {
		cxl_region_decode_reset(cxlr, p->interleave_ways);
		p->state = CXL_CONFIG_ACTIVE;
	}

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

	if (p->state == CXL_CONFIG_ACTIVE) {
		p->state = CXL_CONFIG_INTERLEAVE_ACTIVE;
		cxl_region_teardown_targets(cxlr);
	}
	p->targets[cxled->pos] = NULL;
	p->nr_targets--;
	cxled->cxld.hpa_range = (struct range) {
		.start = 0,
		.end = -1,
	};

	/* notify the region driver that one of its targets has departed */
	up_write(&cxl_region_rwsem);
	device_release_driver(&cxlr->dev);
	down_write(&cxl_region_rwsem);
out:
	put_device(&cxlr->dev);
	return rc;
}

void cxl_decoder_kill_region(struct cxl_endpoint_decoder *cxled)
{
	down_write(&cxl_region_rwsem);
	cxled->mode = CXL_DECODER_DEAD;
	cxl_region_detach(cxled);
	up_write(&cxl_region_rwsem);
}

static int attach_target(struct cxl_region *cxlr,
			 struct cxl_endpoint_decoder *cxled, int pos,
			 unsigned int state)
{
	int rc = 0;

	if (state == TASK_INTERRUPTIBLE)
		rc = down_write_killable(&cxl_region_rwsem);
	else
		down_write(&cxl_region_rwsem);
	if (rc)
		return rc;

	down_read(&cxl_dpa_rwsem);
	rc = cxl_region_attach(cxlr, cxled, pos);
	up_read(&cxl_dpa_rwsem);
	up_write(&cxl_region_rwsem);
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

	rc = cxl_region_detach(p->targets[pos]);
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
	else {
		struct device *dev;

		dev = bus_find_device_by_name(&cxl_bus_type, NULL, buf);
		if (!dev)
			return -ENODEV;

		if (!is_endpoint_decoder(dev)) {
			rc = -EINVAL;
			goto out;
		}

		rc = attach_target(cxlr, to_cxl_endpoint_decoder(dev), pos,
				   TASK_INTERRUPTIBLE);
out:
		put_device(dev);
	}

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
	&cxl_region_access0_coordinate_group,
	&cxl_region_access1_coordinate_group,
	NULL,
};

static void cxl_region_release(struct device *dev)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(dev->parent);
	struct cxl_region *cxlr = to_cxl_region(dev);
	int id = atomic_read(&cxlrd->region_id);

	/*
	 * Try to reuse the recently idled id rather than the cached
	 * next id to prevent the region id space from increasing
	 * unnecessarily.
	 */
	if (cxlr->id < id)
		if (atomic_try_cmpxchg(&cxlrd->region_id, &id, cxlr->id)) {
			memregion_free(id);
			goto out;
		}

	memregion_free(cxlr->id);
out:
	put_device(dev->parent);
	kfree(cxlr);
}

const struct device_type cxl_region_type = {
	.name = "cxl_region",
	.release = cxl_region_release,
	.groups = region_groups
};

bool is_cxl_region(struct device *dev)
{
	return dev->type == &cxl_region_type;
}
EXPORT_SYMBOL_NS_GPL(is_cxl_region, "CXL");

static struct cxl_region *to_cxl_region(struct device *dev)
{
	if (dev_WARN_ONCE(dev, dev->type != &cxl_region_type,
			  "not a cxl_region device\n"))
		return NULL;

	return container_of(dev, struct cxl_region, dev);
}

static void unregister_region(void *_cxlr)
{
	struct cxl_region *cxlr = _cxlr;
	struct cxl_region_params *p = &cxlr->params;
	int i;

	device_del(&cxlr->dev);

	/*
	 * Now that region sysfs is shutdown, the parameter block is now
	 * read-only, so no need to hold the region rwsem to access the
	 * region parameters.
	 */
	for (i = 0; i < p->interleave_ways; i++)
		detach_target(cxlr, i);

	cxl_region_iomem_release(cxlr);
	put_device(&cxlr->dev);
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
	/*
	 * Keep root decoder pinned through cxl_region_release to fixup
	 * region id allocations
	 */
	get_device(dev->parent);
	device_set_pm_not_required(dev);
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_region_type;
	cxlr->id = id;

	return cxlr;
}

static bool cxl_region_update_coordinates(struct cxl_region *cxlr, int nid)
{
	int cset = 0;
	int rc;

	for (int i = 0; i < ACCESS_COORDINATE_MAX; i++) {
		if (cxlr->coord[i].read_bandwidth) {
			rc = 0;
			if (cxl_need_node_perf_attrs_update(nid))
				node_set_perf_attrs(nid, &cxlr->coord[i], i);
			else
				rc = cxl_update_hmat_access_coordinates(nid, cxlr, i);

			if (rc == 0)
				cset++;
		}
	}

	if (!cset)
		return false;

	rc = sysfs_update_group(&cxlr->dev.kobj, get_cxl_region_access0_group());
	if (rc)
		dev_dbg(&cxlr->dev, "Failed to update access0 group\n");

	rc = sysfs_update_group(&cxlr->dev.kobj, get_cxl_region_access1_group());
	if (rc)
		dev_dbg(&cxlr->dev, "Failed to update access1 group\n");

	return true;
}

static int cxl_region_perf_attrs_callback(struct notifier_block *nb,
					  unsigned long action, void *arg)
{
	struct cxl_region *cxlr = container_of(nb, struct cxl_region,
					       memory_notifier);
	struct memory_notify *mnb = arg;
	int nid = mnb->status_change_nid;
	int region_nid;

	if (nid == NUMA_NO_NODE || action != MEM_ONLINE)
		return NOTIFY_DONE;

	/*
	 * No need to hold cxl_region_rwsem; region parameters are stable
	 * within the cxl_region driver.
	 */
	region_nid = phys_to_target_node(cxlr->params.res->start);
	if (nid != region_nid)
		return NOTIFY_DONE;

	if (!cxl_region_update_coordinates(cxlr, nid))
		return NOTIFY_DONE;

	return NOTIFY_OK;
}

static int cxl_region_calculate_adistance(struct notifier_block *nb,
					  unsigned long nid, void *data)
{
	struct cxl_region *cxlr = container_of(nb, struct cxl_region,
					       adist_notifier);
	struct access_coordinate *perf;
	int *adist = data;
	int region_nid;

	/*
	 * No need to hold cxl_region_rwsem; region parameters are stable
	 * within the cxl_region driver.
	 */
	region_nid = phys_to_target_node(cxlr->params.res->start);
	if (nid != region_nid)
		return NOTIFY_OK;

	perf = &cxlr->coord[ACCESS_COORDINATE_CPU];

	if (mt_perf_to_adistance(perf, adist))
		return NOTIFY_OK;

	return NOTIFY_STOP;
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

	rc = devm_add_action_or_reset(port->uport_dev, unregister_region, cxlr);
	if (rc)
		return ERR_PTR(rc);

	dev_dbg(port->uport_dev, "%s: created %s\n",
		dev_name(&cxlrd->cxlsd.cxld.dev), dev_name(dev));
	return cxlr;

err:
	put_device(dev);
	return ERR_PTR(rc);
}

static ssize_t __create_region_show(struct cxl_root_decoder *cxlrd, char *buf)
{
	return sysfs_emit(buf, "region%u\n", atomic_read(&cxlrd->region_id));
}

static ssize_t create_pmem_region_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	return __create_region_show(to_cxl_root_decoder(dev), buf);
}

static ssize_t create_ram_region_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	return __create_region_show(to_cxl_root_decoder(dev), buf);
}

static struct cxl_region *__create_region(struct cxl_root_decoder *cxlrd,
					  enum cxl_decoder_mode mode, int id)
{
	int rc;

	switch (mode) {
	case CXL_DECODER_RAM:
	case CXL_DECODER_PMEM:
		break;
	default:
		dev_err(&cxlrd->cxlsd.cxld.dev, "unsupported mode %d\n", mode);
		return ERR_PTR(-EINVAL);
	}

	rc = memregion_alloc(GFP_KERNEL);
	if (rc < 0)
		return ERR_PTR(rc);

	if (atomic_cmpxchg(&cxlrd->region_id, id, rc) != id) {
		memregion_free(rc);
		return ERR_PTR(-EBUSY);
	}

	return devm_cxl_add_region(cxlrd, id, mode, CXL_DECODER_HOSTONLYMEM);
}

static ssize_t create_region_store(struct device *dev, const char *buf,
				   size_t len, enum cxl_decoder_mode mode)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(dev);
	struct cxl_region *cxlr;
	int rc, id;

	rc = sscanf(buf, "region%d\n", &id);
	if (rc != 1)
		return -EINVAL;

	cxlr = __create_region(cxlrd, mode, id);
	if (IS_ERR(cxlr))
		return PTR_ERR(cxlr);

	return len;
}

static ssize_t create_pmem_region_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	return create_region_store(dev, buf, len, CXL_DECODER_PMEM);
}
DEVICE_ATTR_RW(create_pmem_region);

static ssize_t create_ram_region_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	return create_region_store(dev, buf, len, CXL_DECODER_RAM);
}
DEVICE_ATTR_RW(create_ram_region);

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

	devm_release_action(port->uport_dev, unregister_region, cxlr);
	put_device(&cxlr->dev);

	return len;
}
DEVICE_ATTR_WO(delete_region);

static void cxl_pmem_region_release(struct device *dev)
{
	struct cxl_pmem_region *cxlr_pmem = to_cxl_pmem_region(dev);
	int i;

	for (i = 0; i < cxlr_pmem->nr_mappings; i++) {
		struct cxl_memdev *cxlmd = cxlr_pmem->mapping[i].cxlmd;

		put_device(&cxlmd->dev);
	}

	kfree(cxlr_pmem);
}

static const struct attribute_group *cxl_pmem_region_attribute_groups[] = {
	&cxl_base_attribute_group,
	NULL,
};

const struct device_type cxl_pmem_region_type = {
	.name = "cxl_pmem_region",
	.release = cxl_pmem_region_release,
	.groups = cxl_pmem_region_attribute_groups,
};

bool is_cxl_pmem_region(struct device *dev)
{
	return dev->type == &cxl_pmem_region_type;
}
EXPORT_SYMBOL_NS_GPL(is_cxl_pmem_region, "CXL");

struct cxl_pmem_region *to_cxl_pmem_region(struct device *dev)
{
	if (dev_WARN_ONCE(dev, !is_cxl_pmem_region(dev),
			  "not a cxl_pmem_region device\n"))
		return NULL;
	return container_of(dev, struct cxl_pmem_region, dev);
}
EXPORT_SYMBOL_NS_GPL(to_cxl_pmem_region, "CXL");

struct cxl_poison_context {
	struct cxl_port *port;
	enum cxl_decoder_mode mode;
	u64 offset;
};

static int cxl_get_poison_unmapped(struct cxl_memdev *cxlmd,
				   struct cxl_poison_context *ctx)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	u64 offset, length;
	int rc = 0;

	/*
	 * Collect poison for the remaining unmapped resources
	 * after poison is collected by committed endpoints.
	 *
	 * Knowing that PMEM must always follow RAM, get poison
	 * for unmapped resources based on the last decoder's mode:
	 *	ram: scan remains of ram range, then any pmem range
	 *	pmem: scan remains of pmem range
	 */

	if (ctx->mode == CXL_DECODER_RAM) {
		offset = ctx->offset;
		length = resource_size(&cxlds->ram_res) - offset;
		rc = cxl_mem_get_poison(cxlmd, offset, length, NULL);
		if (rc == -EFAULT)
			rc = 0;
		if (rc)
			return rc;
	}
	if (ctx->mode == CXL_DECODER_PMEM) {
		offset = ctx->offset;
		length = resource_size(&cxlds->dpa_res) - offset;
		if (!length)
			return 0;
	} else if (resource_size(&cxlds->pmem_res)) {
		offset = cxlds->pmem_res.start;
		length = resource_size(&cxlds->pmem_res);
	} else {
		return 0;
	}

	return cxl_mem_get_poison(cxlmd, offset, length, NULL);
}

static int poison_by_decoder(struct device *dev, void *arg)
{
	struct cxl_poison_context *ctx = arg;
	struct cxl_endpoint_decoder *cxled;
	struct cxl_memdev *cxlmd;
	u64 offset, length;
	int rc = 0;

	if (!is_endpoint_decoder(dev))
		return rc;

	cxled = to_cxl_endpoint_decoder(dev);
	if (!cxled->dpa_res || !resource_size(cxled->dpa_res))
		return rc;

	/*
	 * Regions are only created with single mode decoders: pmem or ram.
	 * Linux does not support mixed mode decoders. This means that
	 * reading poison per endpoint decoder adheres to the requirement
	 * that poison reads of pmem and ram must be separated.
	 * CXL 3.0 Spec 8.2.9.8.4.1
	 */
	if (cxled->mode == CXL_DECODER_MIXED) {
		dev_dbg(dev, "poison list read unsupported in mixed mode\n");
		return rc;
	}

	cxlmd = cxled_to_memdev(cxled);
	if (cxled->skip) {
		offset = cxled->dpa_res->start - cxled->skip;
		length = cxled->skip;
		rc = cxl_mem_get_poison(cxlmd, offset, length, NULL);
		if (rc == -EFAULT && cxled->mode == CXL_DECODER_RAM)
			rc = 0;
		if (rc)
			return rc;
	}

	offset = cxled->dpa_res->start;
	length = cxled->dpa_res->end - offset + 1;
	rc = cxl_mem_get_poison(cxlmd, offset, length, cxled->cxld.region);
	if (rc == -EFAULT && cxled->mode == CXL_DECODER_RAM)
		rc = 0;
	if (rc)
		return rc;

	/* Iterate until commit_end is reached */
	if (cxled->cxld.id == ctx->port->commit_end) {
		ctx->offset = cxled->dpa_res->end + 1;
		ctx->mode = cxled->mode;
		return 1;
	}

	return 0;
}

int cxl_get_poison_by_endpoint(struct cxl_port *port)
{
	struct cxl_poison_context ctx;
	int rc = 0;

	ctx = (struct cxl_poison_context) {
		.port = port
	};

	rc = device_for_each_child(&port->dev, &ctx, poison_by_decoder);
	if (rc == 1)
		rc = cxl_get_poison_unmapped(to_cxl_memdev(port->uport_dev),
					     &ctx);

	return rc;
}

struct cxl_dpa_to_region_context {
	struct cxl_region *cxlr;
	u64 dpa;
};

static int __cxl_dpa_to_region(struct device *dev, void *arg)
{
	struct cxl_dpa_to_region_context *ctx = arg;
	struct cxl_endpoint_decoder *cxled;
	struct cxl_region *cxlr;
	u64 dpa = ctx->dpa;

	if (!is_endpoint_decoder(dev))
		return 0;

	cxled = to_cxl_endpoint_decoder(dev);
	if (!cxled || !cxled->dpa_res || !resource_size(cxled->dpa_res))
		return 0;

	if (dpa > cxled->dpa_res->end || dpa < cxled->dpa_res->start)
		return 0;

	/*
	 * Stop the region search (return 1) when an endpoint mapping is
	 * found. The region may not be fully constructed so offering
	 * the cxlr in the context structure is not guaranteed.
	 */
	cxlr = cxled->cxld.region;
	if (cxlr)
		dev_dbg(dev, "dpa:0x%llx mapped in region:%s\n", dpa,
			dev_name(&cxlr->dev));
	else
		dev_dbg(dev, "dpa:0x%llx mapped in endpoint:%s\n", dpa,
			dev_name(dev));

	ctx->cxlr = cxlr;

	return 1;
}

struct cxl_region *cxl_dpa_to_region(const struct cxl_memdev *cxlmd, u64 dpa)
{
	struct cxl_dpa_to_region_context ctx;
	struct cxl_port *port;

	ctx = (struct cxl_dpa_to_region_context) {
		.dpa = dpa,
	};
	port = cxlmd->endpoint;
	if (port && is_cxl_endpoint(port) && cxl_num_decoders_committed(port))
		device_for_each_child(&port->dev, &ctx, __cxl_dpa_to_region);

	return ctx.cxlr;
}

static bool cxl_is_hpa_in_chunk(u64 hpa, struct cxl_region *cxlr, int pos)
{
	struct cxl_region_params *p = &cxlr->params;
	int gran = p->interleave_granularity;
	int ways = p->interleave_ways;
	u64 offset;

	/* Is the hpa in an expected chunk for its pos(-ition) */
	offset = hpa - p->res->start;
	offset = do_div(offset, gran * ways);
	if ((offset >= pos * gran) && (offset < (pos + 1) * gran))
		return true;

	dev_dbg(&cxlr->dev,
		"Addr trans fail: hpa 0x%llx not in expected chunk\n", hpa);

	return false;
}

u64 cxl_dpa_to_hpa(struct cxl_region *cxlr, const struct cxl_memdev *cxlmd,
		   u64 dpa)
{
	struct cxl_root_decoder *cxlrd = to_cxl_root_decoder(cxlr->dev.parent);
	u64 dpa_offset, hpa_offset, bits_upper, mask_upper, hpa;
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_endpoint_decoder *cxled = NULL;
	u16 eig = 0;
	u8 eiw = 0;
	int pos;

	for (int i = 0; i < p->nr_targets; i++) {
		cxled = p->targets[i];
		if (cxlmd == cxled_to_memdev(cxled))
			break;
	}
	if (!cxled || cxlmd != cxled_to_memdev(cxled))
		return ULLONG_MAX;

	pos = cxled->pos;
	ways_to_eiw(p->interleave_ways, &eiw);
	granularity_to_eig(p->interleave_granularity, &eig);

	/*
	 * The device position in the region interleave set was removed
	 * from the offset at HPA->DPA translation. To reconstruct the
	 * HPA, place the 'pos' in the offset.
	 *
	 * The placement of 'pos' in the HPA is determined by interleave
	 * ways and granularity and is defined in the CXL Spec 3.0 Section
	 * 8.2.4.19.13 Implementation Note: Device Decode Logic
	 */

	/* Remove the dpa base */
	dpa_offset = dpa - cxl_dpa_resource_start(cxled);

	mask_upper = GENMASK_ULL(51, eig + 8);

	if (eiw < 8) {
		hpa_offset = (dpa_offset & mask_upper) << eiw;
		hpa_offset |= pos << (eig + 8);
	} else {
		bits_upper = (dpa_offset & mask_upper) >> (eig + 8);
		bits_upper = bits_upper * 3;
		hpa_offset = ((bits_upper << (eiw - 8)) + pos) << (eig + 8);
	}

	/* The lower bits remain unchanged */
	hpa_offset |= dpa_offset & GENMASK_ULL(eig + 7, 0);

	/* Apply the hpa_offset to the region base address */
	hpa = hpa_offset + p->res->start;

	/* Root decoder translation overrides typical modulo decode */
	if (cxlrd->hpa_to_spa)
		hpa = cxlrd->hpa_to_spa(cxlrd, hpa);

	if (hpa < p->res->start || hpa > p->res->end) {
		dev_dbg(&cxlr->dev,
			"Addr trans fail: hpa 0x%llx not in region\n", hpa);
		return ULLONG_MAX;
	}

	/* Simple chunk check, by pos & gran, only applies to modulo decodes */
	if (!cxlrd->hpa_to_spa && (!cxl_is_hpa_in_chunk(hpa, cxlr, pos)))
		return ULLONG_MAX;

	return hpa;
}

static struct lock_class_key cxl_pmem_region_key;

static int cxl_pmem_region_alloc(struct cxl_region *cxlr)
{
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct device *dev;
	int i;

	guard(rwsem_read)(&cxl_region_rwsem);
	if (p->state != CXL_CONFIG_COMMIT)
		return -ENXIO;

	struct cxl_pmem_region *cxlr_pmem __free(kfree) =
		kzalloc(struct_size(cxlr_pmem, mapping, p->nr_targets), GFP_KERNEL);
	if (!cxlr_pmem)
		return -ENOMEM;

	cxlr_pmem->hpa_range.start = p->res->start;
	cxlr_pmem->hpa_range.end = p->res->end;

	/* Snapshot the region configuration underneath the cxl_region_rwsem */
	cxlr_pmem->nr_mappings = p->nr_targets;
	for (i = 0; i < p->nr_targets; i++) {
		struct cxl_endpoint_decoder *cxled = p->targets[i];
		struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
		struct cxl_pmem_region_mapping *m = &cxlr_pmem->mapping[i];

		/*
		 * Regions never span CXL root devices, so by definition the
		 * bridge for one device is the same for all.
		 */
		if (i == 0) {
			cxl_nvb = cxl_find_nvdimm_bridge(cxlmd->endpoint);
			if (!cxl_nvb)
				return -ENODEV;
			cxlr->cxl_nvb = cxl_nvb;
		}
		m->cxlmd = cxlmd;
		get_device(&cxlmd->dev);
		m->start = cxled->dpa_res->start;
		m->size = resource_size(cxled->dpa_res);
		m->position = i;
	}

	dev = &cxlr_pmem->dev;
	device_initialize(dev);
	lockdep_set_class(&dev->mutex, &cxl_pmem_region_key);
	device_set_pm_not_required(dev);
	dev->parent = &cxlr->dev;
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_pmem_region_type;
	cxlr_pmem->cxlr = cxlr;
	cxlr->cxlr_pmem = no_free_ptr(cxlr_pmem);

	return 0;
}

static void cxl_dax_region_release(struct device *dev)
{
	struct cxl_dax_region *cxlr_dax = to_cxl_dax_region(dev);

	kfree(cxlr_dax);
}

static const struct attribute_group *cxl_dax_region_attribute_groups[] = {
	&cxl_base_attribute_group,
	NULL,
};

const struct device_type cxl_dax_region_type = {
	.name = "cxl_dax_region",
	.release = cxl_dax_region_release,
	.groups = cxl_dax_region_attribute_groups,
};

static bool is_cxl_dax_region(struct device *dev)
{
	return dev->type == &cxl_dax_region_type;
}

struct cxl_dax_region *to_cxl_dax_region(struct device *dev)
{
	if (dev_WARN_ONCE(dev, !is_cxl_dax_region(dev),
			  "not a cxl_dax_region device\n"))
		return NULL;
	return container_of(dev, struct cxl_dax_region, dev);
}
EXPORT_SYMBOL_NS_GPL(to_cxl_dax_region, "CXL");

static struct lock_class_key cxl_dax_region_key;

static struct cxl_dax_region *cxl_dax_region_alloc(struct cxl_region *cxlr)
{
	struct cxl_region_params *p = &cxlr->params;
	struct cxl_dax_region *cxlr_dax;
	struct device *dev;

	down_read(&cxl_region_rwsem);
	if (p->state != CXL_CONFIG_COMMIT) {
		cxlr_dax = ERR_PTR(-ENXIO);
		goto out;
	}

	cxlr_dax = kzalloc(sizeof(*cxlr_dax), GFP_KERNEL);
	if (!cxlr_dax) {
		cxlr_dax = ERR_PTR(-ENOMEM);
		goto out;
	}

	cxlr_dax->hpa_range.start = p->res->start;
	cxlr_dax->hpa_range.end = p->res->end;

	dev = &cxlr_dax->dev;
	cxlr_dax->cxlr = cxlr;
	device_initialize(dev);
	lockdep_set_class(&dev->mutex, &cxl_dax_region_key);
	device_set_pm_not_required(dev);
	dev->parent = &cxlr->dev;
	dev->bus = &cxl_bus_type;
	dev->type = &cxl_dax_region_type;
out:
	up_read(&cxl_region_rwsem);

	return cxlr_dax;
}

static void cxlr_pmem_unregister(void *_cxlr_pmem)
{
	struct cxl_pmem_region *cxlr_pmem = _cxlr_pmem;
	struct cxl_region *cxlr = cxlr_pmem->cxlr;
	struct cxl_nvdimm_bridge *cxl_nvb = cxlr->cxl_nvb;

	/*
	 * Either the bridge is in ->remove() context under the device_lock(),
	 * or cxlr_release_nvdimm() is cancelling the bridge's release action
	 * for @cxlr_pmem and doing it itself (while manually holding the bridge
	 * lock).
	 */
	device_lock_assert(&cxl_nvb->dev);
	cxlr->cxlr_pmem = NULL;
	cxlr_pmem->cxlr = NULL;
	device_unregister(&cxlr_pmem->dev);
}

static void cxlr_release_nvdimm(void *_cxlr)
{
	struct cxl_region *cxlr = _cxlr;
	struct cxl_nvdimm_bridge *cxl_nvb = cxlr->cxl_nvb;

	scoped_guard(device, &cxl_nvb->dev) {
		if (cxlr->cxlr_pmem)
			devm_release_action(&cxl_nvb->dev, cxlr_pmem_unregister,
					    cxlr->cxlr_pmem);
	}
	cxlr->cxl_nvb = NULL;
	put_device(&cxl_nvb->dev);
}

/**
 * devm_cxl_add_pmem_region() - add a cxl_region-to-nd_region bridge
 * @cxlr: parent CXL region for this pmem region bridge device
 *
 * Return: 0 on success negative error code on failure.
 */
static int devm_cxl_add_pmem_region(struct cxl_region *cxlr)
{
	struct cxl_pmem_region *cxlr_pmem;
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct device *dev;
	int rc;

	rc = cxl_pmem_region_alloc(cxlr);
	if (rc)
		return rc;
	cxlr_pmem = cxlr->cxlr_pmem;
	cxl_nvb = cxlr->cxl_nvb;

	dev = &cxlr_pmem->dev;
	rc = dev_set_name(dev, "pmem_region%d", cxlr->id);
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	dev_dbg(&cxlr->dev, "%s: register %s\n", dev_name(dev->parent),
		dev_name(dev));

	scoped_guard(device, &cxl_nvb->dev) {
		if (cxl_nvb->dev.driver)
			rc = devm_add_action_or_reset(&cxl_nvb->dev,
						      cxlr_pmem_unregister,
						      cxlr_pmem);
		else
			rc = -ENXIO;
	}

	if (rc)
		goto err_bridge;

	/* @cxlr carries a reference on @cxl_nvb until cxlr_release_nvdimm */
	return devm_add_action_or_reset(&cxlr->dev, cxlr_release_nvdimm, cxlr);

err:
	put_device(dev);
err_bridge:
	put_device(&cxl_nvb->dev);
	cxlr->cxl_nvb = NULL;
	return rc;
}

static void cxlr_dax_unregister(void *_cxlr_dax)
{
	struct cxl_dax_region *cxlr_dax = _cxlr_dax;

	device_unregister(&cxlr_dax->dev);
}

static int devm_cxl_add_dax_region(struct cxl_region *cxlr)
{
	struct cxl_dax_region *cxlr_dax;
	struct device *dev;
	int rc;

	cxlr_dax = cxl_dax_region_alloc(cxlr);
	if (IS_ERR(cxlr_dax))
		return PTR_ERR(cxlr_dax);

	dev = &cxlr_dax->dev;
	rc = dev_set_name(dev, "dax_region%d", cxlr->id);
	if (rc)
		goto err;

	rc = device_add(dev);
	if (rc)
		goto err;

	dev_dbg(&cxlr->dev, "%s: register %s\n", dev_name(dev->parent),
		dev_name(dev));

	return devm_add_action_or_reset(&cxlr->dev, cxlr_dax_unregister,
					cxlr_dax);
err:
	put_device(dev);
	return rc;
}

static int match_root_decoder_by_range(struct device *dev,
				       const void *data)
{
	const struct range *r1, *r2 = data;
	struct cxl_root_decoder *cxlrd;

	if (!is_root_decoder(dev))
		return 0;

	cxlrd = to_cxl_root_decoder(dev);
	r1 = &cxlrd->cxlsd.cxld.hpa_range;
	return range_contains(r1, r2);
}

static int match_region_by_range(struct device *dev, const void *data)
{
	struct cxl_region_params *p;
	struct cxl_region *cxlr;
	const struct range *r = data;
	int rc = 0;

	if (!is_cxl_region(dev))
		return 0;

	cxlr = to_cxl_region(dev);
	p = &cxlr->params;

	down_read(&cxl_region_rwsem);
	if (p->res && p->res->start == r->start && p->res->end == r->end)
		rc = 1;
	up_read(&cxl_region_rwsem);

	return rc;
}

/* Establish an empty region covering the given HPA range */
static struct cxl_region *construct_region(struct cxl_root_decoder *cxlrd,
					   struct cxl_endpoint_decoder *cxled)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_port *port = cxlrd_to_port(cxlrd);
	struct range *hpa = &cxled->cxld.hpa_range;
	struct cxl_region_params *p;
	struct cxl_region *cxlr;
	struct resource *res;
	int rc;

	do {
		cxlr = __create_region(cxlrd, cxled->mode,
				       atomic_read(&cxlrd->region_id));
	} while (IS_ERR(cxlr) && PTR_ERR(cxlr) == -EBUSY);

	if (IS_ERR(cxlr)) {
		dev_err(cxlmd->dev.parent,
			"%s:%s: %s failed assign region: %ld\n",
			dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev),
			__func__, PTR_ERR(cxlr));
		return cxlr;
	}

	down_write(&cxl_region_rwsem);
	p = &cxlr->params;
	if (p->state >= CXL_CONFIG_INTERLEAVE_ACTIVE) {
		dev_err(cxlmd->dev.parent,
			"%s:%s: %s autodiscovery interrupted\n",
			dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev),
			__func__);
		rc = -EBUSY;
		goto err;
	}

	set_bit(CXL_REGION_F_AUTO, &cxlr->flags);

	res = kmalloc(sizeof(*res), GFP_KERNEL);
	if (!res) {
		rc = -ENOMEM;
		goto err;
	}

	*res = DEFINE_RES_MEM_NAMED(hpa->start, range_len(hpa),
				    dev_name(&cxlr->dev));
	rc = insert_resource(cxlrd->res, res);
	if (rc) {
		/*
		 * Platform-firmware may not have split resources like "System
		 * RAM" on CXL window boundaries see cxl_region_iomem_release()
		 */
		dev_warn(cxlmd->dev.parent,
			 "%s:%s: %s %s cannot insert resource\n",
			 dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev),
			 __func__, dev_name(&cxlr->dev));
	}

	p->res = res;
	p->interleave_ways = cxled->cxld.interleave_ways;
	p->interleave_granularity = cxled->cxld.interleave_granularity;
	p->state = CXL_CONFIG_INTERLEAVE_ACTIVE;

	rc = sysfs_update_group(&cxlr->dev.kobj, get_cxl_region_target_group());
	if (rc)
		goto err;

	dev_dbg(cxlmd->dev.parent, "%s:%s: %s %s res: %pr iw: %d ig: %d\n",
		dev_name(&cxlmd->dev), dev_name(&cxled->cxld.dev), __func__,
		dev_name(&cxlr->dev), p->res, p->interleave_ways,
		p->interleave_granularity);

	/* ...to match put_device() in cxl_add_to_region() */
	get_device(&cxlr->dev);
	up_write(&cxl_region_rwsem);

	return cxlr;

err:
	up_write(&cxl_region_rwsem);
	devm_release_action(port->uport_dev, unregister_region, cxlr);
	return ERR_PTR(rc);
}

int cxl_add_to_region(struct cxl_port *root, struct cxl_endpoint_decoder *cxled)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct range *hpa = &cxled->cxld.hpa_range;
	struct cxl_decoder *cxld = &cxled->cxld;
	struct device *cxlrd_dev, *region_dev;
	struct cxl_root_decoder *cxlrd;
	struct cxl_region_params *p;
	struct cxl_region *cxlr;
	bool attach = false;
	int rc;

	cxlrd_dev = device_find_child(&root->dev, &cxld->hpa_range,
				      match_root_decoder_by_range);
	if (!cxlrd_dev) {
		dev_err(cxlmd->dev.parent,
			"%s:%s no CXL window for range %#llx:%#llx\n",
			dev_name(&cxlmd->dev), dev_name(&cxld->dev),
			cxld->hpa_range.start, cxld->hpa_range.end);
		return -ENXIO;
	}

	cxlrd = to_cxl_root_decoder(cxlrd_dev);

	/*
	 * Ensure that if multiple threads race to construct_region() for @hpa
	 * one does the construction and the others add to that.
	 */
	mutex_lock(&cxlrd->range_lock);
	region_dev = device_find_child(&cxlrd->cxlsd.cxld.dev, hpa,
				       match_region_by_range);
	if (!region_dev) {
		cxlr = construct_region(cxlrd, cxled);
		region_dev = &cxlr->dev;
	} else
		cxlr = to_cxl_region(region_dev);
	mutex_unlock(&cxlrd->range_lock);

	rc = PTR_ERR_OR_ZERO(cxlr);
	if (rc)
		goto out;

	attach_target(cxlr, cxled, -1, TASK_UNINTERRUPTIBLE);

	down_read(&cxl_region_rwsem);
	p = &cxlr->params;
	attach = p->state == CXL_CONFIG_COMMIT;
	up_read(&cxl_region_rwsem);

	if (attach) {
		/*
		 * If device_attach() fails the range may still be active via
		 * the platform-firmware memory map, otherwise the driver for
		 * regions is local to this file, so driver matching can't fail.
		 */
		if (device_attach(&cxlr->dev) < 0)
			dev_err(&cxlr->dev, "failed to enable, range: %pr\n",
				p->res);
	}

	put_device(region_dev);
out:
	put_device(cxlrd_dev);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(cxl_add_to_region, "CXL");

static int is_system_ram(struct resource *res, void *arg)
{
	struct cxl_region *cxlr = arg;
	struct cxl_region_params *p = &cxlr->params;

	dev_dbg(&cxlr->dev, "%pr has System RAM: %pr\n", p->res, res);
	return 1;
}

static void shutdown_notifiers(void *_cxlr)
{
	struct cxl_region *cxlr = _cxlr;

	unregister_memory_notifier(&cxlr->memory_notifier);
	unregister_mt_adistance_algorithm(&cxlr->adist_notifier);
}

static int cxl_region_probe(struct device *dev)
{
	struct cxl_region *cxlr = to_cxl_region(dev);
	struct cxl_region_params *p = &cxlr->params;
	int rc;

	rc = down_read_interruptible(&cxl_region_rwsem);
	if (rc) {
		dev_dbg(&cxlr->dev, "probe interrupted\n");
		return rc;
	}

	if (p->state < CXL_CONFIG_COMMIT) {
		dev_dbg(&cxlr->dev, "config state: %d\n", p->state);
		rc = -ENXIO;
		goto out;
	}

	if (test_bit(CXL_REGION_F_NEEDS_RESET, &cxlr->flags)) {
		dev_err(&cxlr->dev,
			"failed to activate, re-commit region and retry\n");
		rc = -ENXIO;
		goto out;
	}

	/*
	 * From this point on any path that changes the region's state away from
	 * CXL_CONFIG_COMMIT is also responsible for releasing the driver.
	 */
out:
	up_read(&cxl_region_rwsem);

	if (rc)
		return rc;

	cxlr->memory_notifier.notifier_call = cxl_region_perf_attrs_callback;
	cxlr->memory_notifier.priority = CXL_CALLBACK_PRI;
	register_memory_notifier(&cxlr->memory_notifier);

	cxlr->adist_notifier.notifier_call = cxl_region_calculate_adistance;
	cxlr->adist_notifier.priority = 100;
	register_mt_adistance_algorithm(&cxlr->adist_notifier);

	rc = devm_add_action_or_reset(&cxlr->dev, shutdown_notifiers, cxlr);
	if (rc)
		return rc;

	switch (cxlr->mode) {
	case CXL_DECODER_PMEM:
		return devm_cxl_add_pmem_region(cxlr);
	case CXL_DECODER_RAM:
		/*
		 * The region can not be manged by CXL if any portion of
		 * it is already online as 'System RAM'
		 */
		if (walk_iomem_res_desc(IORES_DESC_NONE,
					IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY,
					p->res->start, p->res->end, cxlr,
					is_system_ram) > 0)
			return 0;
		return devm_cxl_add_dax_region(cxlr);
	default:
		dev_dbg(&cxlr->dev, "unsupported region mode: %d\n",
			cxlr->mode);
		return -ENXIO;
	}
}

static struct cxl_driver cxl_region_driver = {
	.name = "cxl_region",
	.probe = cxl_region_probe,
	.id = CXL_DEVICE_REGION,
};

int cxl_region_init(void)
{
	return cxl_driver_register(&cxl_region_driver);
}

void cxl_region_exit(void)
{
	cxl_driver_unregister(&cxl_region_driver);
}

MODULE_IMPORT_NS("CXL");
MODULE_IMPORT_NS("DEVMEM");
MODULE_ALIAS_CXL(CXL_DEVICE_REGION);
