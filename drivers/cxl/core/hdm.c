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

	cxlsd = cxl_switch_decoder_alloc(port, 1);
	if (IS_ERR(cxlsd))
		return PTR_ERR(cxlsd);

	device_lock_assert(&port->dev);

	xa_for_each(&port->dports, index, dport)
		break;
	single_port_map[0] = dport->port_id;

	return add_hdm_decoder(port, &cxlsd->cxld, single_port_map);
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_add_passthrough_decoder, CXL);

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
}

static int map_hdm_decoder_regs(struct cxl_port *port, void __iomem *crb,
				struct cxl_component_regs *regs)
{
	struct cxl_register_map map = {
		.host = &port->dev,
		.resource = port->component_reg_phys,
		.base = crb,
		.max_size = CXL_COMPONENT_REG_BLOCK_SIZE,
	};

	cxl_probe_component_regs(&port->dev, crb, &map.component_map);
	if (!map.component_map.hdm_decoder.valid) {
		dev_dbg(&port->dev, "HDM decoder registers not implemented\n");
		/* unique error code to indicate no HDM decoder capability */
		return -ENODEV;
	}

	return cxl_map_component_regs(&map, regs, BIT(CXL_CM_CAP_CAP_ID_HDM));
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
	struct device *dev = &port->dev;
	struct cxl_hdm *cxlhdm;
	void __iomem *crb;
	int rc;

	cxlhdm = devm_kzalloc(dev, sizeof(*cxlhdm), GFP_KERNEL);
	if (!cxlhdm)
		return ERR_PTR(-ENOMEM);
	cxlhdm->port = port;
	dev_set_drvdata(dev, cxlhdm);

	crb = ioremap(port->component_reg_phys, CXL_COMPONENT_REG_BLOCK_SIZE);
	if (!crb && info && info->mem_enabled) {
		cxlhdm->decoder_count = info->ranges;
		return cxlhdm;
	} else if (!crb) {
		dev_err(dev, "No component registers mapped\n");
		return ERR_PTR(-ENXIO);
	}

	rc = map_hdm_decoder_regs(port, crb, &cxlhdm->regs);
	iounmap(crb);
	if (rc)
		return ERR_PTR(rc);

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
EXPORT_SYMBOL_NS_GPL(devm_cxl_setup_hdm, CXL);

static void __cxl_dpa_debug(struct seq_file *file, struct resource *r, int depth)
{
	unsigned long long start = r->start, end = r->end;

	seq_printf(file, "%*s%08llx-%08llx : %s\n", depth * 2, "", start, end,
		   r->name);
}

void cxl_dpa_debug(struct seq_file *file, struct cxl_dev_state *cxlds)
{
	struct resource *p1, *p2;

	down_read(&cxl_dpa_rwsem);
	for (p1 = cxlds->dpa_res.child; p1; p1 = p1->sibling) {
		__cxl_dpa_debug(file, p1, 0);
		for (p2 = p1->child; p2; p2 = p2->sibling)
			__cxl_dpa_debug(file, p2, 1);
	}
	up_read(&cxl_dpa_rwsem);
}
EXPORT_SYMBOL_NS_GPL(cxl_dpa_debug, CXL);

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
		__release_region(&cxlds->dpa_res, skip_start, cxled->skip);
	cxled->skip = 0;
	cxled->dpa_res = NULL;
	put_device(&cxled->cxld.dev);
	port->hdm_end--;
}

static void cxl_dpa_release(void *cxled)
{
	down_write(&cxl_dpa_rwsem);
	__cxl_dpa_release(cxled);
	up_write(&cxl_dpa_rwsem);
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

static int __cxl_dpa_reserve(struct cxl_endpoint_decoder *cxled,
			     resource_size_t base, resource_size_t len,
			     resource_size_t skipped)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_port *port = cxled_to_port(cxled);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct device *dev = &port->dev;
	struct resource *res;

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
		res = __request_region(&cxlds->dpa_res, base - skipped, skipped,
				       dev_name(&cxled->cxld.dev), 0);
		if (!res) {
			dev_dbg(dev,
				"decoder%d.%d: failed to reserve skipped space\n",
				port->id, cxled->cxld.id);
			return -EBUSY;
		}
	}
	res = __request_region(&cxlds->dpa_res, base, len,
			       dev_name(&cxled->cxld.dev), 0);
	if (!res) {
		dev_dbg(dev, "decoder%d.%d: failed to reserve allocation\n",
			port->id, cxled->cxld.id);
		if (skipped)
			__release_region(&cxlds->dpa_res, base - skipped,
					 skipped);
		return -EBUSY;
	}
	cxled->dpa_res = res;
	cxled->skip = skipped;

	if (resource_contains(&cxlds->pmem_res, res))
		cxled->mode = CXL_DECODER_PMEM;
	else if (resource_contains(&cxlds->ram_res, res))
		cxled->mode = CXL_DECODER_RAM;
	else {
		dev_dbg(dev, "decoder%d.%d: %pr mixed\n", port->id,
			cxled->cxld.id, cxled->dpa_res);
		cxled->mode = CXL_DECODER_MIXED;
	}

	port->hdm_end++;
	get_device(&cxled->cxld.dev);
	return 0;
}

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
EXPORT_SYMBOL_NS_GPL(devm_cxl_dpa_reserve, CXL);

resource_size_t cxl_dpa_size(struct cxl_endpoint_decoder *cxled)
{
	resource_size_t size = 0;

	down_read(&cxl_dpa_rwsem);
	if (cxled->dpa_res)
		size = resource_size(cxled->dpa_res);
	up_read(&cxl_dpa_rwsem);

	return size;
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
	int rc;

	down_write(&cxl_dpa_rwsem);
	if (!cxled->dpa_res) {
		rc = 0;
		goto out;
	}
	if (cxled->cxld.region) {
		dev_dbg(dev, "decoder assigned to: %s\n",
			dev_name(&cxled->cxld.region->dev));
		rc = -EBUSY;
		goto out;
	}
	if (cxled->cxld.flags & CXL_DECODER_F_ENABLE) {
		dev_dbg(dev, "decoder enabled\n");
		rc = -EBUSY;
		goto out;
	}
	if (cxled->cxld.id != port->hdm_end) {
		dev_dbg(dev, "expected decoder%d.%d\n", port->id,
			port->hdm_end);
		rc = -EBUSY;
		goto out;
	}
	devm_cxl_dpa_release(cxled);
	rc = 0;
out:
	up_write(&cxl_dpa_rwsem);
	return rc;
}

int cxl_dpa_set_mode(struct cxl_endpoint_decoder *cxled,
		     enum cxl_decoder_mode mode)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct device *dev = &cxled->cxld.dev;
	int rc;

	switch (mode) {
	case CXL_DECODER_RAM:
	case CXL_DECODER_PMEM:
		break;
	default:
		dev_dbg(dev, "unsupported mode: %d\n", mode);
		return -EINVAL;
	}

	down_write(&cxl_dpa_rwsem);
	if (cxled->cxld.flags & CXL_DECODER_F_ENABLE) {
		rc = -EBUSY;
		goto out;
	}

	/*
	 * Only allow modes that are supported by the current partition
	 * configuration
	 */
	if (mode == CXL_DECODER_PMEM && !resource_size(&cxlds->pmem_res)) {
		dev_dbg(dev, "no available pmem capacity\n");
		rc = -ENXIO;
		goto out;
	}
	if (mode == CXL_DECODER_RAM && !resource_size(&cxlds->ram_res)) {
		dev_dbg(dev, "no available ram capacity\n");
		rc = -ENXIO;
		goto out;
	}

	cxled->mode = mode;
	rc = 0;
out:
	up_write(&cxl_dpa_rwsem);

	return rc;
}

int cxl_dpa_alloc(struct cxl_endpoint_decoder *cxled, unsigned long long size)
{
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	resource_size_t free_ram_start, free_pmem_start;
	struct cxl_port *port = cxled_to_port(cxled);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct device *dev = &cxled->cxld.dev;
	resource_size_t start, avail, skip;
	struct resource *p, *last;
	int rc;

	down_write(&cxl_dpa_rwsem);
	if (cxled->cxld.region) {
		dev_dbg(dev, "decoder attached to %s\n",
			dev_name(&cxled->cxld.region->dev));
		rc = -EBUSY;
		goto out;
	}

	if (cxled->cxld.flags & CXL_DECODER_F_ENABLE) {
		dev_dbg(dev, "decoder enabled\n");
		rc = -EBUSY;
		goto out;
	}

	for (p = cxlds->ram_res.child, last = NULL; p; p = p->sibling)
		last = p;
	if (last)
		free_ram_start = last->end + 1;
	else
		free_ram_start = cxlds->ram_res.start;

	for (p = cxlds->pmem_res.child, last = NULL; p; p = p->sibling)
		last = p;
	if (last)
		free_pmem_start = last->end + 1;
	else
		free_pmem_start = cxlds->pmem_res.start;

	if (cxled->mode == CXL_DECODER_RAM) {
		start = free_ram_start;
		avail = cxlds->ram_res.end - start + 1;
		skip = 0;
	} else if (cxled->mode == CXL_DECODER_PMEM) {
		resource_size_t skip_start, skip_end;

		start = free_pmem_start;
		avail = cxlds->pmem_res.end - start + 1;
		skip_start = free_ram_start;

		/*
		 * If some pmem is already allocated, then that allocation
		 * already handled the skip.
		 */
		if (cxlds->pmem_res.child &&
		    skip_start == cxlds->pmem_res.child->start)
			skip_end = skip_start - 1;
		else
			skip_end = start - 1;
		skip = skip_end - skip_start + 1;
	} else {
		dev_dbg(dev, "mode not set\n");
		rc = -EINVAL;
		goto out;
	}

	if (size > avail) {
		dev_dbg(dev, "%pa exceeds available %s capacity: %pa\n", &size,
			cxled->mode == CXL_DECODER_RAM ? "ram" : "pmem",
			&avail);
		rc = -ENOSPC;
		goto out;
	}

	rc = __cxl_dpa_reserve(cxled, start, size, skip);
out:
	up_write(&cxl_dpa_rwsem);

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

static int cxl_decoder_reset(struct cxl_decoder *cxld)
{
	struct cxl_port *port = to_cxl_port(cxld->dev.parent);
	struct cxl_hdm *cxlhdm = dev_get_drvdata(&port->dev);
	void __iomem *hdm = cxlhdm->regs.hdm_decoder;
	int id = cxld->id;
	u32 ctrl;

	if ((cxld->flags & CXL_DECODER_F_ENABLE) == 0)
		return 0;

	if (port->commit_end != id) {
		dev_dbg(&port->dev,
			"%s: out of order reset, expected decoder%d.%d\n",
			dev_name(&cxld->dev), port->id, port->commit_end);
		return -EBUSY;
	}

	down_read(&cxl_dpa_rwsem);
	ctrl = readl(hdm + CXL_HDM_DECODER0_CTRL_OFFSET(id));
	ctrl &= ~CXL_HDM_DECODER0_CTRL_COMMIT;
	writel(ctrl, hdm + CXL_HDM_DECODER0_CTRL_OFFSET(id));

	writel(0, hdm + CXL_HDM_DECODER0_SIZE_HIGH_OFFSET(id));
	writel(0, hdm + CXL_HDM_DECODER0_SIZE_LOW_OFFSET(id));
	writel(0, hdm + CXL_HDM_DECODER0_BASE_HIGH_OFFSET(id));
	writel(0, hdm + CXL_HDM_DECODER0_BASE_LOW_OFFSET(id));
	up_read(&cxl_dpa_rwsem);

	port->commit_end--;
	cxld->flags &= ~CXL_DECODER_F_ENABLE;

	/* Userspace is now responsible for reconfiguring this decoder */
	if (is_endpoint_decoder(&cxld->dev)) {
		struct cxl_endpoint_decoder *cxled;

		cxled = to_cxl_endpoint_decoder(&cxld->dev);
		cxled->state = CXL_DECODER_STATE_MANUAL;
	}

	return 0;
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
	if (rc)
		return rc;

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
EXPORT_SYMBOL_NS_GPL(devm_cxl_enumerate_decoders, CXL);
