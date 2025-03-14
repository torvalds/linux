// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/delay.h>

#include "cxlmem.h"
#include "core.h"

/**
 * DOC: cxl core hdm
 *
 * Compute Express Link Host Managed Device Memory, starting with the
 * CXL 2.0 specification, is managed by an array of HDM Decoder register
 * instances per CXL port and per CXL endpoint. Define common helpers
 * for enumerating these registers and capabilities.
 */

DECLARE_RWSEM(cxl_dpa_rwsem);

static int add_hdm_decoder(struct cxl_port *port, struct cxl_decoder *cxld,
			   int *target_map)
{
	int rc;

	rc = cxl_decoder_add_locked(cxld, target_map);
	if (rc) {
		put_device(&cxld->dev);
		dev_err(&port->dev, "Failed to add decoder\n");
		return rc;
	}

	rc = cxl_decoder_autoremove(&port->dev, cxld);
	if (rc)
		return rc;

	dev_dbg(&cxld->dev, "Added to port %s\n", dev_name(&port->dev));

	return 0;
}

/*
 * Per the CXL specification (8.2.5.12 CXL HDM Decoder Capability Structure)
 * single ported host-bridges need not publish a decoder capability when a
 * passthrough decode can be assumed, i.e. all transactions that the uport sees
 * are claimed and passed to the single dport. Disable the range until the first
 * CXL region is enumerated / activated.
 */
int devm_cxl_add_passthrough_decoder(struct cxl_port *port)
{
	struct cxl_switch_decoder *cxlsd;
	struct cxl_dport *dport = NULL;
	int single_port_map[1];
	unsigned long index;
	struct cxl_hdm *cxlhdm = dev_get_drvdata(&port->dev);

	/*
	 * Capability checks are moot for passthrough decoders, support
	 * any and all possibilities.
	 */
	cxlhdm->interleave_mask = ~0U;
	cxlhdm->iw_cap_mask = ~0UL;

	cxlsd = cxl_switch_decoder_alloc(port, 1);
	if (IS_ERR(cxlsd))
		return PTR_ERR(cxlsd);

	device_lock_assert(&port->dev);

	xa_for_each(&port->dports, index, dport)
		break;
	single_port_map[0] = dport->port_id;

	return add_hdm_decoder(port, &cxlsd->cxld, single_port_map);
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_add_passthrough_decoder, "CXL");

static void parse_hdm_decoder_caps(struct cxl_hdm *cxlhdm)
{
	u32 hdm_cap;

	hdm_cap = readl(cxlhdm->regs.hdm_decoder + CXL_HDM_DECODER_CAP_OFFSET);
	cxlhdm->decoder_count = cxl_hdm_decoder_count(hdm_cap);
	cxlhdm->target_count =
		FIELD_GET(CXL_HDM_DECODER_TARGET_COUNT_MASK, hdm_cap);
	if (FIELD_GET(CXL_HDM_DECODER_INTERLEAVE_11_8, hdm_cap))
		cxlhdm->interleave_mask |= GENMASK(11, 8);
	if (FIELD_GET(CXL_HDM_DECODER_INTERLEAVE_14_12, hdm_cap))
		cxlhdm->interleave_mask |= GENMASK(14, 12);
	cxlhdm->iw_cap_mask = BIT(1) | BIT(2) | BIT(4) | BIT(8);
	if (FIELD_GET(CXL_HDM_DECODER_INTERLEAVE_3_6_12_WAY, hdm_cap))
		cxlhdm->iw_cap_mask |= BIT(3) | BIT(6) | BIT(12);
	if (FIELD_GET(CXL_HDM_DECODER_INTERLEAVE_16_WAY, hdm_cap))
		cxlhdm->iw_cap_mask |= BIT(16);
}

static bool should_emulate_decoders(struct cxl_endpoint_dvsec_info *info)
{
	struct cxl_hdm *cxlhdm;
	void __iomem *hdm;
	u32 ctrl;
	int i;

	if (!info)
		return false;

	cxlhdm = dev_get_drvdata(&info->port->dev);
	hdm = cxlhdm->regs.hdm_decoder;

	if (!hdm)
		return true;

	/*
	 * If HDM decoders are present and the driver is in control of
	 * Mem_Enable skip DVSEC based emulation
	 */
	if (!info->mem_enabled)
		return false;

	/*
	 * If any decoders are committed already, there should not be any
	 * emulated DVSEC decoders.
	 */
	for (i = 0; i < cxlhdm->decoder_count; i++) {
		ctrl = readl(hdm + CXL_HDM_DECODER0_CTRL_OFFSET(i));
		dev_dbg(&info->port->dev,
			"decoder%d.%d: committed: %ld base: %#x_%.8x size: %#x_%.8x\n",
			info->port->id, i,
			FIELD_GET(CXL_HDM_DECODER0_CTRL_COMMITTED, ctrl),
			readl(hdm + CXL_HDM_DECODER0_BASE_HIGH_OFFSET(i)),
			readl(hdm + CXL_HDM_DECODER0_BASE_LOW_OFFSET(i)),
			readl(hdm + CXL_HDM_DECODER0_SIZE_HIGH_OFFSET(i)),
			readl(hdm + CXL_HDM_DECODER0_SIZE_LOW_OFFSET(i)));
		if (FIELD_GET(CXL_HDM_DECODER0_CTRL_COMMITTED, ctrl))
			return false;
	}

	return true;
}

/**
 * devm_cxl_setup_hdm - map HDM decoder component registers
 * @port: cxl_port to map
 * @info: cached DVSEC range register info
 */
struct cxl_hdm *devm_cxl_setup_hdm(struct cxl_port *port,
				   struct cxl_endpoint_dvsec_info *info)
{
	struct cxl_register_map *reg_map = &port->reg_map;
	struct device *dev = &port->dev;
	struct cxl_hdm *cxlhdm;
	int rc;

	cxlhdm = devm_kzalloc(dev, sizeof(*cxlhdm), GFP_KERNEL);
	if (!cxlhdm)
		return ERR_PTR(-ENOMEM);
	cxlhdm->port = port;
	dev_set_drvdata(dev, cxlhdm);

	/* Memory devices can configure device HDM using DVSEC range regs. */
	if (reg_map->resource == CXL_RESOURCE_NONE) {
		if (!info || !info->mem_enabled) {
			dev_err(dev, "No component registers mapped\n");
			return ERR_PTR(-ENXIO);
		}

		cxlhdm->decoder_count = info->ranges;
		return cxlhdm;
	}

	if (!reg_map->component_map.hdm_decoder.valid) {
		dev_dbg(&port->dev, "HDM decoder registers not implemented\n");
		/* unique error code to indicate no HDM decoder capability */
		return ERR_PTR(-ENODEV);
	}

	rc = cxl_map_component_regs(reg_map, &cxlhdm->regs,
				    BIT(CXL_CM_CAP_CAP_ID_HDM));
	if (rc) {
		dev_err(dev, "Failed to map HDM capability.\n");
		return ERR_PTR(rc);
	}

	parse_hdm_decoder_caps(cxlhdm);
	if (cxlhdm->decoder_count == 0) {
		dev_err(dev, "Spec violation. Caps invalid\n");
		return ERR_PTR(-ENXIO);
	}

	/*
	 * Now that the hdm capability is parsed, decide if range
	 * register emulation is needed and fixup cxlhdm accordingly.
	 */
	if (should_emulate_decoders(info)) {
		dev_dbg(dev, "Fallback map %d range register%s\n", info->ranges,
			info->ranges > 1 ? "s" : "");
		cxlhdm->decoder_count = info->ranges;
	}

	return cxlhdm;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_setup_hdm, "CXL");

static void __cxl_dpa_debug(struct seq_file *file, struct resource *r, int depth)
{
	unsigned long long start = r->start, end = r->end;

	seq_printf(file, "%*s%08llx-%08llx : %s\n", depth * 2, "", start, end,
		   r->name);
}

void cxl_dpa_debug(struct seq_file *file, struct cxl_dev_state *cxlds)
{
	struct resource *p1, *p2;

	guard(rwsem_read)(&cxl_dpa_rwsem);
	for (p1 = cxlds->dpa_res.child; p1; p1 = p1->sibling) {
		__cxl_dpa_debug(file, p1, 0);
		for (p2 = p1->child; p2; p2 = p2->sibling)
			__cxl_dpa_debug(file, p2, 1);
	}
}
EXPORT_SYMBOL_NS_GPL(cxl_dpa_debug, "CXL");

/* See request_skip() kernel-doc */
static resource_size_t __adjust_skip(struct cxl_dev_state *cxlds,
				     const resource_size_t skip_base,
				     const resource_size_t skip_len,
				     const char *requester)
{
	const resource_size_t skip_end = skip_base + skip_len - 1;

	for (int i = 0; i < cxlds->nr_partitions; i++) {
		const struct resource *part_res = &cxlds->part[i].res;
		resource_size_t adjust_start, adjust_end, size;

		adjust_start = max(skip_base, part_res->start);
		adjust_end = min(skip_end, part_res->end);

		if (adjust_end < adjust_start)
			continue;

		size = adjust_end - adjust_start + 1;

		if (!requester)
			__release_region(&cxlds->dpa_res, adjust_start, size);
		else if (!__request_region(&cxlds->dpa_res, adjust_start, size,
					   requester, 0))
			return adjust_start - skip_base;
	}

	return skip_len;
}
#define release_skip(c, b, l) __adjust_skip((c), (b), (l), NULL)

/*
 * Must be called in a context that synchronizes against this decoder's
 * port ->remove() callback (like an endpoint decoder sysfs attribute)
 */
static void __cxl_dpa_release(struct cxl_endpoint_decoder *cxled)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_port *port = cxled_to_port(cxled);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct resource *res = cxled->dpa_res;
	resource_size_t skip_start;

	lockdep_assert_held_write(&cxl_dpa_rwsem);

	/* save @skip_start, before @res is released */
	skip_start = res->start - cxled->skip;
	__release_region(&cxlds->dpa_res, res->start, resource_size(res));
	if (cxled->skip)
		release_skip(cxlds, skip_start, cxled->skip);
	cxled->skip = 0;
	cxled->dpa_res = NULL;
	put_device(&cxled->cxld.dev);
	port->hdm_end--;
}

static void cxl_dpa_release(void *cxled)
{
	guard(rwsem_write)(&cxl_dpa_rwsem);
	__cxl_dpa_release(cxled);
}

/*
 * Must be called from context that will not race port device
 * unregistration, like decoder sysfs attribute methods
 */
static void devm_cxl_dpa_release(struct cxl_endpoint_decoder *cxled)
{
	struct cxl_port *port = cxled_to_port(cxled);

	lockdep_assert_held_write(&cxl_dpa_rwsem);
	devm_remove_action(&port->dev, cxl_dpa_release, cxled);
	__cxl_dpa_release(cxled);
}

/**
 * request_skip() - Track DPA 'skip' in @cxlds->dpa_res resource tree
 * @cxlds: CXL.mem device context that parents @cxled
 * @cxled: Endpoint decoder establishing new allocation that skips lower DPA
 * @skip_base: DPA < start of new DPA allocation (DPAnew)
 * @skip_len: @skip_base + @skip_len == DPAnew
 *
 * DPA 'skip' arises from out-of-sequence DPA allocation events relative
 * to free capacity across multiple partitions. It is a wasteful event
 * as usable DPA gets thrown away, but if a deployment has, for example,
 * a dual RAM+PMEM device, wants to use PMEM, and has unallocated RAM
 * DPA, the free RAM DPA must be sacrificed to start allocating PMEM.
 * See third "Implementation Note" in CXL 3.1 8.2.4.19.13 "Decoder
 * Protection" for more details.
 *
 * A 'skip' always covers the last allocated DPA in a previous partition
 * to the start of the current partition to allocate.  Allocations never
 * start in the middle of a partition, and allocations are always
 * de-allocated in reverse order (see cxl_dpa_free(), or natural devm
 * unwind order from forced in-order allocation).
 *
 * If @cxlds->nr_partitions was guaranteed to be <= 2 then the 'skip'
 * would always be contained to a single partition. Given
 * @cxlds->nr_partitions may be > 2 it results in cases where the 'skip'
 * might span "tail capacity of partition[0], all of partition[1], ...,
 * all of partition[N-1]" to support allocating from partition[N]. That
 * in turn interacts with the partition 'struct resource' boundaries
 * within @cxlds->dpa_res whereby 'skip' requests need to be divided by
 * partition. I.e. this is a quirk of using a 'struct resource' tree to
 * detect range conflicts while also tracking partition boundaries in
 * @cxlds->dpa_res.
 */
static int request_skip(struct cxl_dev_state *cxlds,
			struct cxl_endpoint_decoder *cxled,
			const resource_size_t skip_base,
			const resource_size_t skip_len)
{
	resource_size_t skipped = __adjust_skip(cxlds, skip_base, skip_len,
						dev_name(&cxled->cxld.dev));

	if (skipped == skip_len)
		return 0;

	dev_dbg(cxlds->dev,
		"%s: failed to reserve skipped space (%pa %pa %pa)\n",
		dev_name(&cxled->cxld.dev), &skip_base, &skip_len, &skipped);

	release_skip(cxlds, skip_base, skipped);

	return -EBUSY;
}

static int __cxl_dpa_reserve(struct cxl_endpoint_decoder *cxled,
			     resource_size_t base, resource_size_t len,
			     resource_size_t skipped)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_port *port = cxled_to_port(cxled);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct device *dev = &port->dev;
	struct resource *res;
	int rc;

	lockdep_assert_held_write(&cxl_dpa_rwsem);

	if (!len) {
		dev_warn(dev, "decoder%d.%d: empty reservation attempted\n",
			 port->id, cxled->cxld.id);
		return -EINVAL;
	}

	if (cxled->dpa_res) {
		dev_dbg(dev, "decoder%d.%d: existing allocation %pr assigned\n",
			port->id, cxled->cxld.id, cxled->dpa_res);
		return -EBUSY;
	}

	if (port->hdm_end + 1 != cxled->cxld.id) {
		/*
		 * Assumes alloc and commit order is always in hardware instance
		 * order per expectations from 8.2.5.12.20 Committing Decoder
		 * Programming that enforce decoder[m] committed before
		 * decoder[m+1] commit start.
		 */
		dev_dbg(dev, "decoder%d.%d: expected decoder%d.%d\n", port->id,
			cxled->cxld.id, port->id, port->hdm_end + 1);
		return -EBUSY;
	}

	if (skipped) {
		rc = request_skip(cxlds, cxled, base - skipped, skipped);
		if (rc)
			return rc;
	}
	res = __request_region(&cxlds->dpa_res, base, len,
			       dev_name(&cxled->cxld.dev), 0);
	if (!res) {
		dev_dbg(dev, "decoder%d.%d: failed to reserve allocation\n",
			port->id, cxled->cxld.id);
		if (skipped)
			release_skip(cxlds, base - skipped, skipped);
		return -EBUSY;
	}
	cxled->dpa_res = res;
	cxled->skip = skipped;

	/*
	 * When allocating new capacity, ->part is already set, when
	 * discovering decoder settings at initial enumeration, ->part
	 * is not set.
	 */
	if (cxled->part < 0)
		for (int i = 0; cxlds->nr_partitions; i++)
			if (resource_contains(&cxlds->part[i].res, res)) {
				cxled->part = i;
				break;
			}

	if (cxled->part < 0)
		dev_warn(dev, "decoder%d.%d: %pr does not map any partition\n",
			 port->id, cxled->cxld.id, res);

	port->hdm_end++;
	get_device(&cxled->cxld.dev);
	return 0;
}

static int add_dpa_res(struct device *dev, struct resource *parent,
		       struct resource *res, resource_size_t start,
		       resource_size_t size, const char *type)
{
	int rc;

	*res = (struct resource) {
		.name = type,
		.start = start,
		.end =  start + size - 1,
		.flags = IORESOURCE_MEM,
	};
	if (resource_size(res) == 0) {
		dev_dbg(dev, "DPA(%s): no capacity\n", res->name);
		return 0;
	}
	rc = request_resource(parent, res);
	if (rc) {
		dev_err(dev, "DPA(%s): failed to track %pr (%d)\n", res->name,
			res, rc);
		return rc;
	}

	dev_dbg(dev, "DPA(%s): %pr\n", res->name, res);

	return 0;
}

static const char *cxl_mode_name(enum cxl_partition_mode mode)
{
	switch (mode) {
	case CXL_PARTMODE_RAM:
		return "ram";
	case CXL_PARTMODE_PMEM:
		return "pmem";
	default:
		return "";
	};
}

/* if this fails the caller must destroy @cxlds, there is no recovery */
int cxl_dpa_setup(struct cxl_dev_state *cxlds, const struct cxl_dpa_info *info)
{
	struct device *dev = cxlds->dev;

	guard(rwsem_write)(&cxl_dpa_rwsem);

	if (cxlds->nr_partitions)
		return -EBUSY;

	if (!info->size || !info->nr_partitions) {
		cxlds->dpa_res = DEFINE_RES_MEM(0, 0);
		cxlds->nr_partitions = 0;
		return 0;
	}

	cxlds->dpa_res = DEFINE_RES_MEM(0, info->size);

	for (int i = 0; i < info->nr_partitions; i++) {
		const struct cxl_dpa_part_info *part = &info->part[i];
		int rc;

		cxlds->part[i].perf.qos_class = CXL_QOS_CLASS_INVALID;
		cxlds->part[i].mode = part->mode;

		/* Require ordered + contiguous partitions */
		if (i) {
			const struct cxl_dpa_part_info *prev = &info->part[i - 1];

			if (prev->range.end + 1 != part->range.start)
				return -EINVAL;
		}
		rc = add_dpa_res(dev, &cxlds->dpa_res, &cxlds->part[i].res,
				 part->range.start, range_len(&part->range),
				 cxl_mode_name(part->mode));
		if (rc)
			return rc;
		cxlds->nr_partitions++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cxl_dpa_setup);

int devm_cxl_dpa_reserve(struct cxl_endpoint_decoder *cxled,
				resource_size_t base, resource_size_t len,
				resource_size_t skipped)
{
	struct cxl_port *port = cxled_to_port(cxled);
	int rc;

	down_write(&cxl_dpa_rwsem);
	rc = __cxl_dpa_reserve(cxled, base, len, skipped);
	up_write(&cxl_dpa_rwsem);

	if (rc)
		return rc;

	return devm_add_action_or_reset(&port->dev, cxl_dpa_release, cxled);
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_dpa_reserve, "CXL");

resource_size_t cxl_dpa_size(struct cxl_endpoint_decoder *cxled)
{
	guard(rwsem_read)(&cxl_dpa_rwsem);
	if (cxled->dpa_res)
		return resource_size(cxled->dpa_res);

	return 0;
}

resource_size_t cxl_dpa_resource_start(struct cxl_endpoint_decoder *cxled)
{
	resource_size_t base = -1;

	lockdep_assert_held(&cxl_dpa_rwsem);
	if (cxled->dpa_res)
		base = cxled->dpa_res->start;

	return base;
}

int cxl_dpa_free(struct cxl_endpoint_decoder *cxled)
{
	struct cxl_port *port = cxled_to_port(cxled);
	struct device *dev = &cxled->cxld.dev;

	guard(rwsem_write)(&cxl_dpa_rwsem);
	if (!cxled->dpa_res)
		return 0;
	if (cxled->cxld.region) {
		dev_dbg(dev, "decoder assigned to: %s\n",
			dev_name(&cxled->cxld.region->dev));
		return -EBUSY;
	}
	if (cxled->cxld.flags & CXL_DECODER_F_ENABLE) {
		dev_dbg(dev, "decoder enabled\n");
		return -EBUSY;
	}
	if (cxled->cxld.id != port->hdm_end) {
		dev_dbg(dev, "expected decoder%d.%d\n", port->id,
			port->hdm_end);
		return -EBUSY;
	}

	devm_cxl_dpa_release(cxled);
	return 0;
}

int cxl_dpa_set_part(struct cxl_endpoint_decoder *cxled,
		     enum cxl_partition_mode mode)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct device *dev = &cxled->cxld.dev;
	int part;

	guard(rwsem_write)(&cxl_dpa_rwsem);
	if (cxled->cxld.flags & CXL_DECODER_F_ENABLE)
		return -EBUSY;

	for (part = 0; part < cxlds->nr_partitions; part++)
		if (cxlds->part[part].mode == mode)
			break;

	if (part >= cxlds->nr_partitions) {
		dev_dbg(dev, "unsupported mode: %d\n", mode);
		return -EINVAL;
	}

	if (!resource_size(&cxlds->part[part].res)) {
		dev_dbg(dev, "no available capacity for mode: %d\n", mode);
		return -ENXIO;
	}

	cxled->part = part;
	return 0;
}

static int __cxl_dpa_alloc(struct cxl_endpoint_decoder *cxled, unsigned long long size)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct device *dev = &cxled->cxld.dev;
	struct resource *res, *prev = NULL;
	resource_size_t start, avail, skip, skip_start;
	struct resource *p, *last;
	int part;

	guard(rwsem_write)(&cxl_dpa_rwsem);
	if (cxled->cxld.region) {
		dev_dbg(dev, "decoder attached to %s\n",
			dev_name(&cxled->cxld.region->dev));
		return -EBUSY;
	}

	if (cxled->cxld.flags & CXL_DECODER_F_ENABLE) {
		dev_dbg(dev, "decoder enabled\n");
		return -EBUSY;
	}

	part = cxled->part;
	if (part < 0) {
		dev_dbg(dev, "partition not set\n");
		return -EBUSY;
	}

	res = &cxlds->part[part].res;
	for (p = res->child, last = NULL; p; p = p->sibling)
		last = p;
	if (last)
		start = last->end + 1;
	else
		start = res->start;

	/*
	 * To allocate at partition N, a skip needs to be calculated for all
	 * unallocated space at lower partitions indices.
	 *
	 * If a partition has any allocations, the search can end because a
	 * previous cxl_dpa_alloc() invocation is assumed to have accounted for
	 * all previous partitions.
	 */
	skip_start = CXL_RESOURCE_NONE;
	for (int i = part; i; i--) {
		prev = &cxlds->part[i - 1].res;
		for (p = prev->child, last = NULL; p; p = p->sibling)
			last = p;
		if (last) {
			skip_start = last->end + 1;
			break;
		}
		skip_start = prev->start;
	}

	avail = res->end - start + 1;
	if (skip_start == CXL_RESOURCE_NONE)
		skip = 0;
	else
		skip = res->start - skip_start;

	if (size > avail) {
		dev_dbg(dev, "%pa exceeds available %s capacity: %pa\n", &size,
			res->name, &avail);
		return -ENOSPC;
	}

	return __cxl_dpa_reserve(cxled, start, size, skip);
}

int cxl_dpa_alloc(struct cxl_endpoint_decoder *cxled, unsigned long long size)
{
	struct cxl_port *port = cxled_to_port(cxled);
	int rc;

	rc = __cxl_dpa_alloc(cxled, size);
	if (rc)
		return rc;

	return devm_add_action_or_reset(&port->dev, cxl_dpa_release, cxled);
}

static void cxld_set_interleave(struct cxl_decoder *cxld, u32 *ctrl)
{
	u16 eig;
	u8 eiw;

	/*
	 * Input validation ensures these warns never fire, but otherwise
	 * suppress unititalized variable usage warnings.
	 */
	if (WARN_ONCE(ways_to_eiw(cxld->interleave_ways, &eiw),
		      "invalid interleave_ways: %d\n", cxld->interleave_ways))
		return;
	if (WARN_ONCE(granularity_to_eig(cxld->interleave_granularity, &eig),
		      "invalid interleave_granularity: %d\n",
		      cxld->interleave_granularity))
		return;

	u32p_replace_bits(ctrl, eig, CXL_HDM_DECODER0_CTRL_IG_MASK);
	u32p_replace_bits(ctrl, eiw, CXL_HDM_DECODER0_CTRL_IW_MASK);
	*ctrl |= CXL_HDM_DECODER0_CTRL_COMMIT;
}

static void cxld_set_type(struct cxl_decoder *cxld, u32 *ctrl)
{
	u32p_replace_bits(ctrl,
			  !!(cxld->target_type == CXL_DECODER_HOSTONLYMEM),
			  CXL_HDM_DECODER0_CTRL_HOSTONLY);
}

static void cxlsd_set_targets(struct cxl_switch_decoder *cxlsd, u64 *tgt)
{
	struct cxl_dport **t = &cxlsd->target[0];
	int ways = cxlsd->cxld.interleave_ways;

	*tgt = FIELD_PREP(GENMASK(7, 0), t[0]->port_id);
	if (ways > 1)
		*tgt |= FIELD_PREP(GENMASK(15, 8), t[1]->port_id);
	if (ways > 2)
		*tgt |= FIELD_PREP(GENMASK(23, 16), t[2]->port_id);
	if (ways > 3)
		*tgt |= FIELD_PREP(GENMASK(31, 24), t[3]->port_id);
	if (ways > 4)
		*tgt |= FIELD_PREP(GENMASK_ULL(39, 32), t[4]->port_id);
	if (ways > 5)
		*tgt |= FIELD_PREP(GENMASK_ULL(47, 40), t[5]->port_id);
	if (ways > 6)
		*tgt |= FIELD_PREP(GENMASK_ULL(55, 48), t[6]->port_id);
	if (ways > 7)
		*tgt |= FIELD_PREP(GENMASK_ULL(63, 56), t[7]->port_id);
}

/*
 * Per CXL 2.0 8.2.5.12.20 Committing Decoder Programming, hardware must set
 * committed or error within 10ms, but just be generous with 20ms to account for
 * clock skew and other marginal behavior
 */
#define COMMIT_TIMEOUT_MS 20
static int cxld_await_commit(void __iomem *hdm, int id)
{
	u32 ctrl;
	int i;

	for (i = 0; i < COMMIT_TIMEOUT_MS; i++) {
		ctrl = readl(hdm + CXL_HDM_DECODER0_CTRL_OFFSET(id));
		if (FIELD_GET(CXL_HDM_DECODER0_CTRL_COMMIT_ERROR, ctrl)) {
			ctrl &= ~CXL_HDM_DECODER0_CTRL_COMMIT;
			writel(ctrl, hdm + CXL_HDM_DECODER0_CTRL_OFFSET(id));
			return -EIO;
		}
		if (FIELD_GET(CXL_HDM_DECODER0_CTRL_COMMITTED, ctrl))
			return 0;
		fsleep(1000);
	}

	return -ETIMEDOUT;
}

static int cxl_decoder_commit(struct cxl_decoder *cxld)
{
	struct cxl_port *port = to_cxl_port(cxld->dev.parent);
	struct cxl_hdm *cxlhdm = dev_get_drvdata(&port->dev);
	void __iomem *hdm = cxlhdm->regs.hdm_decoder;
	int id = cxld->id, rc;
	u64 base, size;
	u32 ctrl;

	if (cxld->flags & CXL_DECODER_F_ENABLE)
		return 0;

	if (cxl_num_decoders_committed(port) != id) {
		dev_dbg(&port->dev,
			"%s: out of order commit, expected decoder%d.%d\n",
			dev_name(&cxld->dev), port->id,
			cxl_num_decoders_committed(port));
		return -EBUSY;
	}

	/*
	 * For endpoint decoders hosted on CXL memory devices that
	 * support the sanitize operation, make sure sanitize is not in-flight.
	 */
	if (is_endpoint_decoder(&cxld->dev)) {
		struct cxl_endpoint_decoder *cxled =
			to_cxl_endpoint_decoder(&cxld->dev);
		struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
		struct cxl_memdev_state *mds =
			to_cxl_memdev_state(cxlmd->cxlds);

		if (mds && mds->security.sanitize_active) {
			dev_dbg(&cxlmd->dev,
				"attempted to commit %s during sanitize\n",
				dev_name(&cxld->dev));
			return -EBUSY;
		}
	}

	down_read(&cxl_dpa_rwsem);
	/* common decoder settings */
	ctrl = readl(hdm + CXL_HDM_DECODER0_CTRL_OFFSET(cxld->id));
	cxld_set_interleave(cxld, &ctrl);
	cxld_set_type(cxld, &ctrl);
	base = cxld->hpa_range.start;
	size = range_len(&cxld->hpa_range);

	writel(upper_32_bits(base), hdm + CXL_HDM_DECODER0_BASE_HIGH_OFFSET(id));
	writel(lower_32_bits(base), hdm + CXL_HDM_DECODER0_BASE_LOW_OFFSET(id));
	writel(upper_32_bits(size), hdm + CXL_HDM_DECODER0_SIZE_HIGH_OFFSET(id));
	writel(lower_32_bits(size), hdm + CXL_HDM_DECODER0_SIZE_LOW_OFFSET(id));

	if (is_switch_decoder(&cxld->dev)) {
		struct cxl_switch_decoder *cxlsd =
			to_cxl_switch_decoder(&cxld->dev);
		void __iomem *tl_hi = hdm + CXL_HDM_DECODER0_TL_HIGH(id);
		void __iomem *tl_lo = hdm + CXL_HDM_DECODER0_TL_LOW(id);
		u64 targets;

		cxlsd_set_targets(cxlsd, &targets);
		writel(upper_32_bits(targets), tl_hi);
		writel(lower_32_bits(targets), tl_lo);
	} else {
		struct cxl_endpoint_decoder *cxled =
			to_cxl_endpoint_decoder(&cxld->dev);
		void __iomem *sk_hi = hdm + CXL_HDM_DECODER0_SKIP_HIGH(id);
		void __iomem *sk_lo = hdm + CXL_HDM_DECODER0_SKIP_LOW(id);

		writel(upper_32_bits(cxled->skip), sk_hi);
		writel(lower_32_bits(cxled->skip), sk_lo);
	}

	writel(ctrl, hdm + CXL_HDM_DECODER0_CTRL_OFFSET(id));
	up_read(&cxl_dpa_rwsem);

	port->commit_end++;
	rc = cxld_await_commit(hdm, cxld->id);
	if (rc) {
		dev_dbg(&port->dev, "%s: error %d committing decoder\n",
			dev_name(&cxld->dev), rc);
		cxld->reset(cxld);
		return rc;
	}
	cxld->flags |= CXL_DECODER_F_ENABLE;

	return 0;
}

static int commit_reap(struct device *dev, void *data)
{
	struct cxl_port *port = to_cxl_port(dev->parent);
	struct cxl_decoder *cxld;

	if (!is_switch_decoder(dev) && !is_endpoint_decoder(dev))
		return 0;

	cxld = to_cxl_decoder(dev);
	if (port->commit_end == cxld->id &&
	    ((cxld->flags & CXL_DECODER_F_ENABLE) == 0)) {
		port->commit_end--;
		dev_dbg(&port->dev, "reap: %s commit_end: %d\n",
			dev_name(&cxld->dev), port->commit_end);
	}

	return 0;
}

void cxl_port_commit_reap(struct cxl_decoder *cxld)
{
	struct cxl_port *port = to_cxl_port(cxld->dev.parent);

	lockdep_assert_held_write(&cxl_region_rwsem);

	/*
	 * Once the highest committed decoder is disabled, free any other
	 * decoders that were pinned allocated by out-of-order release.
	 */
	port->commit_end--;
	dev_dbg(&port->dev, "reap: %s commit_end: %d\n", dev_name(&cxld->dev),
		port->commit_end);
	device_for_each_child_reverse_from(&port->dev, &cxld->dev, NULL,
					   commit_reap);
}
EXPORT_SYMBOL_NS_GPL(cxl_port_commit_reap, "CXL");

static void cxl_decoder_reset(struct cxl_decoder *cxld)
{
	struct cxl_port *port = to_cxl_port(cxld->dev.parent);
	struct cxl_hdm *cxlhdm = dev_get_drvdata(&port->dev);
	void __iomem *hdm = cxlhdm->regs.hdm_decoder;
	int id = cxld->id;
	u32 ctrl;

	if ((cxld->flags & CXL_DECODER_F_ENABLE) == 0)
		return;

	if (port->commit_end == id)
		cxl_port_commit_reap(cxld);
	else
		dev_dbg(&port->dev,
			"%s: out of order reset, expected decoder%d.%d\n",
			dev_name(&cxld->dev), port->id, port->commit_end);

	down_read(&cxl_dpa_rwsem);
	ctrl = readl(hdm + CXL_HDM_DECODER0_CTRL_OFFSET(id));
	ctrl &= ~CXL_HDM_DECODER0_CTRL_COMMIT;
	writel(ctrl, hdm + CXL_HDM_DECODER0_CTRL_OFFSET(id));

	writel(0, hdm + CXL_HDM_DECODER0_SIZE_HIGH_OFFSET(id));
	writel(0, hdm + CXL_HDM_DECODER0_SIZE_LOW_OFFSET(id));
	writel(0, hdm + CXL_HDM_DECODER0_BASE_HIGH_OFFSET(id));
	writel(0, hdm + CXL_HDM_DECODER0_BASE_LOW_OFFSET(id));
	up_read(&cxl_dpa_rwsem);

	cxld->flags &= ~CXL_DECODER_F_ENABLE;

	/* Userspace is now responsible for reconfiguring this decoder */
	if (is_endpoint_decoder(&cxld->dev)) {
		struct cxl_endpoint_decoder *cxled;

		cxled = to_cxl_endpoint_decoder(&cxld->dev);
		cxled->state = CXL_DECODER_STATE_MANUAL;
	}
}

static int cxl_setup_hdm_decoder_from_dvsec(
	struct cxl_port *port, struct cxl_decoder *cxld, u64 *dpa_base,
	int which, struct cxl_endpoint_dvsec_info *info)
{
	struct cxl_endpoint_decoder *cxled;
	u64 len;
	int rc;

	if (!is_cxl_endpoint(port))
		return -EOPNOTSUPP;

	cxled = to_cxl_endpoint_decoder(&cxld->dev);
	len = range_len(&info->dvsec_range[which]);
	if (!len)
		return -ENOENT;

	cxld->target_type = CXL_DECODER_HOSTONLYMEM;
	cxld->commit = NULL;
	cxld->reset = NULL;
	cxld->hpa_range = info->dvsec_range[which];

	/*
	 * Set the emulated decoder as locked pending additional support to
	 * change the range registers at run time.
	 */
	cxld->flags |= CXL_DECODER_F_ENABLE | CXL_DECODER_F_LOCK;
	port->commit_end = cxld->id;

	rc = devm_cxl_dpa_reserve(cxled, *dpa_base, len, 0);
	if (rc) {
		dev_err(&port->dev,
			"decoder%d.%d: Failed to reserve DPA range %#llx - %#llx\n (%d)",
			port->id, cxld->id, *dpa_base, *dpa_base + len - 1, rc);
		return rc;
	}
	*dpa_base += len;
	cxled->state = CXL_DECODER_STATE_AUTO;

	return 0;
}

static int init_hdm_decoder(struct cxl_port *port, struct cxl_decoder *cxld,
			    int *target_map, void __iomem *hdm, int which,
			    u64 *dpa_base, struct cxl_endpoint_dvsec_info *info)
{
	struct cxl_endpoint_decoder *cxled = NULL;
	u64 size, base, skip, dpa_size, lo, hi;
	bool committed;
	u32 remainder;
	int i, rc;
	u32 ctrl;
	union {
		u64 value;
		unsigned char target_id[8];
	} target_list;

	if (should_emulate_decoders(info))
		return cxl_setup_hdm_decoder_from_dvsec(port, cxld, dpa_base,
							which, info);

	ctrl = readl(hdm + CXL_HDM_DECODER0_CTRL_OFFSET(which));
	lo = readl(hdm + CXL_HDM_DECODER0_BASE_LOW_OFFSET(which));
	hi = readl(hdm + CXL_HDM_DECODER0_BASE_HIGH_OFFSET(which));
	base = (hi << 32) + lo;
	lo = readl(hdm + CXL_HDM_DECODER0_SIZE_LOW_OFFSET(which));
	hi = readl(hdm + CXL_HDM_DECODER0_SIZE_HIGH_OFFSET(which));
	size = (hi << 32) + lo;
	committed = !!(ctrl & CXL_HDM_DECODER0_CTRL_COMMITTED);
	cxld->commit = cxl_decoder_commit;
	cxld->reset = cxl_decoder_reset;

	if (!committed)
		size = 0;
	if (base == U64_MAX || size == U64_MAX) {
		dev_warn(&port->dev, "decoder%d.%d: Invalid resource range\n",
			 port->id, cxld->id);
		return -ENXIO;
	}

	if (info)
		cxled = to_cxl_endpoint_decoder(&cxld->dev);
	cxld->hpa_range = (struct range) {
		.start = base,
		.end = base + size - 1,
	};

	/* decoders are enabled if committed */
	if (committed) {
		cxld->flags |= CXL_DECODER_F_ENABLE;
		if (ctrl & CXL_HDM_DECODER0_CTRL_LOCK)
			cxld->flags |= CXL_DECODER_F_LOCK;
		if (FIELD_GET(CXL_HDM_DECODER0_CTRL_HOSTONLY, ctrl))
			cxld->target_type = CXL_DECODER_HOSTONLYMEM;
		else
			cxld->target_type = CXL_DECODER_DEVMEM;

		guard(rwsem_write)(&cxl_region_rwsem);
		if (cxld->id != cxl_num_decoders_committed(port)) {
			dev_warn(&port->dev,
				 "decoder%d.%d: Committed out of order\n",
				 port->id, cxld->id);
			return -ENXIO;
		}

		if (size == 0) {
			dev_warn(&port->dev,
				 "decoder%d.%d: Committed with zero size\n",
				 port->id, cxld->id);
			return -ENXIO;
		}
		port->commit_end = cxld->id;
	} else {
		if (cxled) {
			struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
			struct cxl_dev_state *cxlds = cxlmd->cxlds;

			/*
			 * Default by devtype until a device arrives that needs
			 * more precision.
			 */
			if (cxlds->type == CXL_DEVTYPE_CLASSMEM)
				cxld->target_type = CXL_DECODER_HOSTONLYMEM;
			else
				cxld->target_type = CXL_DECODER_DEVMEM;
		} else {
			/* To be overridden by region type at commit time */
			cxld->target_type = CXL_DECODER_HOSTONLYMEM;
		}

		if (!FIELD_GET(CXL_HDM_DECODER0_CTRL_HOSTONLY, ctrl) &&
		    cxld->target_type == CXL_DECODER_HOSTONLYMEM) {
			ctrl |= CXL_HDM_DECODER0_CTRL_HOSTONLY;
			writel(ctrl, hdm + CXL_HDM_DECODER0_CTRL_OFFSET(which));
		}
	}
	rc = eiw_to_ways(FIELD_GET(CXL_HDM_DECODER0_CTRL_IW_MASK, ctrl),
			  &cxld->interleave_ways);
	if (rc) {
		dev_warn(&port->dev,
			 "decoder%d.%d: Invalid interleave ways (ctrl: %#x)\n",
			 port->id, cxld->id, ctrl);
		return rc;
	}
	rc = eig_to_granularity(FIELD_GET(CXL_HDM_DECODER0_CTRL_IG_MASK, ctrl),
				 &cxld->interleave_granularity);
	if (rc) {
		dev_warn(&port->dev,
			 "decoder%d.%d: Invalid interleave granularity (ctrl: %#x)\n",
			 port->id, cxld->id, ctrl);
		return rc;
	}

	dev_dbg(&port->dev, "decoder%d.%d: range: %#llx-%#llx iw: %d ig: %d\n",
		port->id, cxld->id, cxld->hpa_range.start, cxld->hpa_range.end,
		cxld->interleave_ways, cxld->interleave_granularity);

	if (!cxled) {
		lo = readl(hdm + CXL_HDM_DECODER0_TL_LOW(which));
		hi = readl(hdm + CXL_HDM_DECODER0_TL_HIGH(which));
		target_list.value = (hi << 32) + lo;
		for (i = 0; i < cxld->interleave_ways; i++)
			target_map[i] = target_list.target_id[i];

		return 0;
	}

	if (!committed)
		return 0;

	dpa_size = div_u64_rem(size, cxld->interleave_ways, &remainder);
	if (remainder) {
		dev_err(&port->dev,
			"decoder%d.%d: invalid committed configuration size: %#llx ways: %d\n",
			port->id, cxld->id, size, cxld->interleave_ways);
		return -ENXIO;
	}
	lo = readl(hdm + CXL_HDM_DECODER0_SKIP_LOW(which));
	hi = readl(hdm + CXL_HDM_DECODER0_SKIP_HIGH(which));
	skip = (hi << 32) + lo;
	rc = devm_cxl_dpa_reserve(cxled, *dpa_base + skip, dpa_size, skip);
	if (rc) {
		dev_err(&port->dev,
			"decoder%d.%d: Failed to reserve DPA range %#llx - %#llx\n (%d)",
			port->id, cxld->id, *dpa_base,
			*dpa_base + dpa_size + skip - 1, rc);
		return rc;
	}
	*dpa_base += dpa_size + skip;

	cxled->state = CXL_DECODER_STATE_AUTO;

	return 0;
}

static void cxl_settle_decoders(struct cxl_hdm *cxlhdm)
{
	void __iomem *hdm = cxlhdm->regs.hdm_decoder;
	int committed, i;
	u32 ctrl;

	if (!hdm)
		return;

	/*
	 * Since the register resource was recently claimed via request_region()
	 * be careful about trusting the "not-committed" status until the commit
	 * timeout has elapsed.  The commit timeout is 10ms (CXL 2.0
	 * 8.2.5.12.20), but double it to be tolerant of any clock skew between
	 * host and target.
	 */
	for (i = 0, committed = 0; i < cxlhdm->decoder_count; i++) {
		ctrl = readl(hdm + CXL_HDM_DECODER0_CTRL_OFFSET(i));
		if (ctrl & CXL_HDM_DECODER0_CTRL_COMMITTED)
			committed++;
	}

	/* ensure that future checks of committed can be trusted */
	if (committed != cxlhdm->decoder_count)
		msleep(20);
}

/**
 * devm_cxl_enumerate_decoders - add decoder objects per HDM register set
 * @cxlhdm: Structure to populate with HDM capabilities
 * @info: cached DVSEC range register info
 */
int devm_cxl_enumerate_decoders(struct cxl_hdm *cxlhdm,
				struct cxl_endpoint_dvsec_info *info)
{
	void __iomem *hdm = cxlhdm->regs.hdm_decoder;
	struct cxl_port *port = cxlhdm->port;
	int i;
	u64 dpa_base = 0;

	cxl_settle_decoders(cxlhdm);

	for (i = 0; i < cxlhdm->decoder_count; i++) {
		int target_map[CXL_DECODER_MAX_INTERLEAVE] = { 0 };
		int rc, target_count = cxlhdm->target_count;
		struct cxl_decoder *cxld;

		if (is_cxl_endpoint(port)) {
			struct cxl_endpoint_decoder *cxled;

			cxled = cxl_endpoint_decoder_alloc(port);
			if (IS_ERR(cxled)) {
				dev_warn(&port->dev,
					 "Failed to allocate decoder%d.%d\n",
					 port->id, i);
				return PTR_ERR(cxled);
			}
			cxld = &cxled->cxld;
		} else {
			struct cxl_switch_decoder *cxlsd;

			cxlsd = cxl_switch_decoder_alloc(port, target_count);
			if (IS_ERR(cxlsd)) {
				dev_warn(&port->dev,
					 "Failed to allocate decoder%d.%d\n",
					 port->id, i);
				return PTR_ERR(cxlsd);
			}
			cxld = &cxlsd->cxld;
		}

		rc = init_hdm_decoder(port, cxld, target_map, hdm, i,
				      &dpa_base, info);
		if (rc) {
			dev_warn(&port->dev,
				 "Failed to initialize decoder%d.%d\n",
				 port->id, i);
			put_device(&cxld->dev);
			return rc;
		}
		rc = add_hdm_decoder(port, cxld, target_map);
		if (rc) {
			dev_warn(&port->dev,
				 "Failed to add decoder%d.%d\n", port->id, i);
			return rc;
		}
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_enumerate_decoders, "CXL");
