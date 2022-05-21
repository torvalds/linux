// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-hi-lo.h>
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

static DECLARE_RWSEM(cxl_dpa_rwsem);

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
	struct cxl_dport *dport;
	int single_port_map[1];

	cxlsd = cxl_switch_decoder_alloc(port, 1);
	if (IS_ERR(cxlsd))
		return PTR_ERR(cxlsd);

	device_lock_assert(&port->dev);

	dport = list_first_entry(&port->dports, typeof(*dport), list);
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

static void __iomem *map_hdm_decoder_regs(struct cxl_port *port,
					  void __iomem *crb)
{
	struct cxl_component_reg_map map;

	cxl_probe_component_regs(&port->dev, crb, &map);
	if (!map.hdm_decoder.valid) {
		dev_err(&port->dev, "HDM decoder registers invalid\n");
		return IOMEM_ERR_PTR(-ENXIO);
	}

	return crb + map.hdm_decoder.offset;
}

/**
 * devm_cxl_setup_hdm - map HDM decoder component registers
 * @port: cxl_port to map
 */
struct cxl_hdm *devm_cxl_setup_hdm(struct cxl_port *port)
{
	struct device *dev = &port->dev;
	void __iomem *crb, *hdm;
	struct cxl_hdm *cxlhdm;

	cxlhdm = devm_kzalloc(dev, sizeof(*cxlhdm), GFP_KERNEL);
	if (!cxlhdm)
		return ERR_PTR(-ENOMEM);

	cxlhdm->port = port;
	crb = devm_cxl_iomap_block(dev, port->component_reg_phys,
				   CXL_COMPONENT_REG_BLOCK_SIZE);
	if (!crb) {
		dev_err(dev, "No component registers mapped\n");
		return ERR_PTR(-ENXIO);
	}

	hdm = map_hdm_decoder_regs(port, crb);
	if (IS_ERR(hdm))
		return ERR_CAST(hdm);
	cxlhdm->regs.hdm_decoder = hdm;

	parse_hdm_decoder_caps(cxlhdm);
	if (cxlhdm->decoder_count == 0) {
		dev_err(dev, "Spec violation. Caps invalid\n");
		return ERR_PTR(-ENXIO);
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

static int init_hdm_decoder(struct cxl_port *port, struct cxl_decoder *cxld,
			    int *target_map, void __iomem *hdm, int which)
{
	u64 size, base;
	int i, rc;
	u32 ctrl;
	union {
		u64 value;
		unsigned char target_id[8];
	} target_list;

	ctrl = readl(hdm + CXL_HDM_DECODER0_CTRL_OFFSET(which));
	base = ioread64_hi_lo(hdm + CXL_HDM_DECODER0_BASE_LOW_OFFSET(which));
	size = ioread64_hi_lo(hdm + CXL_HDM_DECODER0_SIZE_LOW_OFFSET(which));

	if (!(ctrl & CXL_HDM_DECODER0_CTRL_COMMITTED))
		size = 0;
	if (base == U64_MAX || size == U64_MAX) {
		dev_warn(&port->dev, "decoder%d.%d: Invalid resource range\n",
			 port->id, cxld->id);
		return -ENXIO;
	}

	cxld->hpa_range = (struct range) {
		.start = base,
		.end = base + size - 1,
	};

	/* switch decoders are always enabled if committed */
	if (ctrl & CXL_HDM_DECODER0_CTRL_COMMITTED) {
		cxld->flags |= CXL_DECODER_F_ENABLE;
		if (ctrl & CXL_HDM_DECODER0_CTRL_LOCK)
			cxld->flags |= CXL_DECODER_F_LOCK;
		if (FIELD_GET(CXL_HDM_DECODER0_CTRL_TYPE, ctrl))
			cxld->target_type = CXL_DECODER_EXPANDER;
		else
			cxld->target_type = CXL_DECODER_ACCELERATOR;
	} else {
		/* unless / until type-2 drivers arrive, assume type-3 */
		if (FIELD_GET(CXL_HDM_DECODER0_CTRL_TYPE, ctrl) == 0) {
			ctrl |= CXL_HDM_DECODER0_CTRL_TYPE;
			writel(ctrl, hdm + CXL_HDM_DECODER0_CTRL_OFFSET(which));
		}
		cxld->target_type = CXL_DECODER_EXPANDER;
	}
	rc = cxl_to_ways(FIELD_GET(CXL_HDM_DECODER0_CTRL_IW_MASK, ctrl),
			 &cxld->interleave_ways);
	if (rc) {
		dev_warn(&port->dev,
			 "decoder%d.%d: Invalid interleave ways (ctrl: %#x)\n",
			 port->id, cxld->id, ctrl);
		return rc;
	}
	rc = cxl_to_granularity(FIELD_GET(CXL_HDM_DECODER0_CTRL_IG_MASK, ctrl),
				&cxld->interleave_granularity);
	if (rc)
		return rc;

	if (is_endpoint_decoder(&cxld->dev))
		return 0;

	target_list.value =
		ioread64_hi_lo(hdm + CXL_HDM_DECODER0_TL_LOW(which));
	for (i = 0; i < cxld->interleave_ways; i++)
		target_map[i] = target_list.target_id[i];

	return 0;
}

/**
 * devm_cxl_enumerate_decoders - add decoder objects per HDM register set
 * @cxlhdm: Structure to populate with HDM capabilities
 */
int devm_cxl_enumerate_decoders(struct cxl_hdm *cxlhdm)
{
	void __iomem *hdm = cxlhdm->regs.hdm_decoder;
	struct cxl_port *port = cxlhdm->port;
	int i, committed;
	u32 ctrl;

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

	for (i = 0; i < cxlhdm->decoder_count; i++) {
		int target_map[CXL_DECODER_MAX_INTERLEAVE] = { 0 };
		int rc, target_count = cxlhdm->target_count;
		struct cxl_decoder *cxld;

		if (is_cxl_endpoint(port)) {
			struct cxl_endpoint_decoder *cxled;

			cxled = cxl_endpoint_decoder_alloc(port);
			if (IS_ERR(cxled)) {
				dev_warn(&port->dev,
					 "Failed to allocate the decoder\n");
				return PTR_ERR(cxled);
			}
			cxld = &cxled->cxld;
		} else {
			struct cxl_switch_decoder *cxlsd;

			cxlsd = cxl_switch_decoder_alloc(port, target_count);
			if (IS_ERR(cxlsd)) {
				dev_warn(&port->dev,
					 "Failed to allocate the decoder\n");
				return PTR_ERR(cxlsd);
			}
			cxld = &cxlsd->cxld;
		}

		rc = init_hdm_decoder(port, cxld, target_map, hdm, i);
		if (rc) {
			put_device(&cxld->dev);
			return rc;
		}
		rc = add_hdm_decoder(port, cxld, target_map);
		if (rc) {
			dev_warn(&port->dev,
				 "Failed to add decoder to port\n");
			return rc;
		}
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_enumerate_decoders, CXL);
