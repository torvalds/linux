// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-hi-lo.h>
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
	struct cxl_decoder *cxld;
	struct cxl_dport *dport;
	int single_port_map[1];

	cxld = cxl_switch_decoder_alloc(port, 1);
	if (IS_ERR(cxld))
		return PTR_ERR(cxld);

	device_lock_assert(&port->dev);

	dport = list_first_entry(&port->dports, typeof(*dport), list);
	single_port_map[0] = dport->port_id;

	return add_hdm_decoder(port, cxld, single_port_map);
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

static int to_interleave_granularity(u32 ctrl)
{
	int val = FIELD_GET(CXL_HDM_DECODER0_CTRL_IG_MASK, ctrl);

	return 256 << val;
}

static int to_interleave_ways(u32 ctrl)
{
	int val = FIELD_GET(CXL_HDM_DECODER0_CTRL_IW_MASK, ctrl);

	switch (val) {
	case 0 ... 4:
		return 1 << val;
	case 8 ... 10:
		return 3 << (val - 8);
	default:
		return 0;
	}
}

static int init_hdm_decoder(struct cxl_port *port, struct cxl_decoder *cxld,
			    int *target_map, void __iomem *hdm, int which)
{
	u64 size, base;
	u32 ctrl;
	int i;
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

	cxld->decoder_range = (struct range) {
		.start = base,
		.end = base + size - 1,
	};

	/* switch decoders are always enabled if committed */
	if (ctrl & CXL_HDM_DECODER0_CTRL_COMMITTED) {
		cxld->flags |= CXL_DECODER_F_ENABLE;
		if (ctrl & CXL_HDM_DECODER0_CTRL_LOCK)
			cxld->flags |= CXL_DECODER_F_LOCK;
	}
	cxld->interleave_ways = to_interleave_ways(ctrl);
	if (!cxld->interleave_ways) {
		dev_warn(&port->dev,
			 "decoder%d.%d: Invalid interleave ways (ctrl: %#x)\n",
			 port->id, cxld->id, ctrl);
		return -ENXIO;
	}
	cxld->interleave_granularity = to_interleave_granularity(ctrl);

	if (FIELD_GET(CXL_HDM_DECODER0_CTRL_TYPE, ctrl))
		cxld->target_type = CXL_DECODER_EXPANDER;
	else
		cxld->target_type = CXL_DECODER_ACCELERATOR;

	if (is_cxl_endpoint(to_cxl_port(cxld->dev.parent)))
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
	int i, committed, failed;
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

	for (i = 0, failed = 0; i < cxlhdm->decoder_count; i++) {
		int target_map[CXL_DECODER_MAX_INTERLEAVE] = { 0 };
		int rc, target_count = cxlhdm->target_count;
		struct cxl_decoder *cxld;

		if (is_cxl_endpoint(port))
			cxld = cxl_endpoint_decoder_alloc(port);
		else
			cxld = cxl_switch_decoder_alloc(port, target_count);
		if (IS_ERR(cxld)) {
			dev_warn(&port->dev,
				 "Failed to allocate the decoder\n");
			return PTR_ERR(cxld);
		}

		rc = init_hdm_decoder(port, cxld, target_map,
				      cxlhdm->regs.hdm_decoder, i);
		if (rc) {
			put_device(&cxld->dev);
			failed++;
			continue;
		}
		rc = add_hdm_decoder(port, cxld, target_map);
		if (rc) {
			dev_warn(&port->dev,
				 "Failed to add decoder to port\n");
			return rc;
		}
	}

	if (failed == cxlhdm->decoder_count) {
		dev_err(&port->dev, "No valid decoders found\n");
		return -ENXIO;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_enumerate_decoders, CXL);
