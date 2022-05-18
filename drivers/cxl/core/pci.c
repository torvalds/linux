// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <cxlpci.h>
#include <cxlmem.h>
#include <cxl.h>
#include "core.h"

/**
 * DOC: cxl core pci
 *
 * Compute Express Link protocols are layered on top of PCIe. CXL core provides
 * a set of helpers for CXL interactions which occur via PCIe.
 */

static unsigned short media_ready_timeout = 60;
module_param(media_ready_timeout, ushort, 0644);
MODULE_PARM_DESC(media_ready_timeout, "seconds to wait for media ready");

struct cxl_walk_context {
	struct pci_bus *bus;
	struct cxl_port *port;
	int type;
	int error;
	int count;
};

static int match_add_dports(struct pci_dev *pdev, void *data)
{
	struct cxl_walk_context *ctx = data;
	struct cxl_port *port = ctx->port;
	int type = pci_pcie_type(pdev);
	struct cxl_register_map map;
	struct cxl_dport *dport;
	u32 lnkcap, port_num;
	int rc;

	if (pdev->bus != ctx->bus)
		return 0;
	if (!pci_is_pcie(pdev))
		return 0;
	if (type != ctx->type)
		return 0;
	if (pci_read_config_dword(pdev, pci_pcie_cap(pdev) + PCI_EXP_LNKCAP,
				  &lnkcap))
		return 0;

	rc = cxl_find_regblock(pdev, CXL_REGLOC_RBI_COMPONENT, &map);
	if (rc)
		dev_dbg(&port->dev, "failed to find component registers\n");

	port_num = FIELD_GET(PCI_EXP_LNKCAP_PN, lnkcap);
	dport = devm_cxl_add_dport(port, &pdev->dev, port_num,
				   cxl_regmap_to_base(pdev, &map));
	if (IS_ERR(dport)) {
		ctx->error = PTR_ERR(dport);
		return PTR_ERR(dport);
	}
	ctx->count++;

	dev_dbg(&port->dev, "add dport%d: %s\n", port_num, dev_name(&pdev->dev));

	return 0;
}

/**
 * devm_cxl_port_enumerate_dports - enumerate downstream ports of the upstream port
 * @port: cxl_port whose ->uport is the upstream of dports to be enumerated
 *
 * Returns a positive number of dports enumerated or a negative error
 * code.
 */
int devm_cxl_port_enumerate_dports(struct cxl_port *port)
{
	struct pci_bus *bus = cxl_port_to_pci_bus(port);
	struct cxl_walk_context ctx;
	int type;

	if (!bus)
		return -ENXIO;

	if (pci_is_root_bus(bus))
		type = PCI_EXP_TYPE_ROOT_PORT;
	else
		type = PCI_EXP_TYPE_DOWNSTREAM;

	ctx = (struct cxl_walk_context) {
		.port = port,
		.bus = bus,
		.type = type,
	};
	pci_walk_bus(bus, match_add_dports, &ctx);

	if (ctx.count == 0)
		return -ENODEV;
	if (ctx.error)
		return ctx.error;
	return ctx.count;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_port_enumerate_dports, CXL);

/*
 * Wait up to @media_ready_timeout for the device to report memory
 * active.
 */
int cxl_await_media_ready(struct cxl_dev_state *cxlds)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec;
	bool active = false;
	u64 md_status;
	int rc, i;

	for (i = media_ready_timeout; i; i--) {
		u32 temp;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(0), &temp);
		if (rc)
			return rc;

		active = FIELD_GET(CXL_DVSEC_MEM_ACTIVE, temp);
		if (active)
			break;
		msleep(1000);
	}

	if (!active) {
		dev_err(&pdev->dev,
			"timeout awaiting memory active after %d seconds\n",
			media_ready_timeout);
		return -ETIMEDOUT;
	}

	md_status = readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);
	if (!CXLMDEV_READY(md_status))
		return -EIO;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_await_media_ready, CXL);

static int wait_for_valid(struct cxl_dev_state *cxlds)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec, rc;
	u32 val;

	/*
	 * Memory_Info_Valid: When set, indicates that the CXL Range 1 Size high
	 * and Size Low registers are valid. Must be set within 1 second of
	 * deassertion of reset to CXL device. Likely it is already set by the
	 * time this runs, but otherwise give a 1.5 second timeout in case of
	 * clock skew.
	 */
	rc = pci_read_config_dword(pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(0), &val);
	if (rc)
		return rc;

	if (val & CXL_DVSEC_MEM_INFO_VALID)
		return 0;

	msleep(1500);

	rc = pci_read_config_dword(pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(0), &val);
	if (rc)
		return rc;

	if (val & CXL_DVSEC_MEM_INFO_VALID)
		return 0;

	return -ETIMEDOUT;
}

static bool __cxl_hdm_decode_init(struct cxl_dev_state *cxlds,
				  struct cxl_endpoint_dvsec_info *info)
{
	struct cxl_register_map map;
	struct cxl_component_reg_map *cmap = &map.component_map;
	bool global_enable, retval = false;
	void __iomem *crb;
	u32 global_ctrl;

	/* map hdm decoder */
	crb = ioremap(cxlds->component_reg_phys, CXL_COMPONENT_REG_BLOCK_SIZE);
	if (!crb) {
		dev_dbg(cxlds->dev, "Failed to map component registers\n");
		return false;
	}

	cxl_probe_component_regs(cxlds->dev, crb, cmap);
	if (!cmap->hdm_decoder.valid) {
		dev_dbg(cxlds->dev, "Invalid HDM decoder registers\n");
		goto out;
	}

	global_ctrl = readl(crb + cmap->hdm_decoder.offset +
			    CXL_HDM_DECODER_CTRL_OFFSET);
	global_enable = global_ctrl & CXL_HDM_DECODER_ENABLE;

	/*
	 * Per CXL 2.0 Section 8.1.3.8.3 and 8.1.3.8.4 DVSEC CXL Range 1 Base
	 * [High,Low] when HDM operation is enabled the range register values
	 * are ignored by the device, but the spec also recommends matching the
	 * DVSEC Range 1,2 to HDM Decoder Range 0,1. So, non-zero info->ranges
	 * are expected even though Linux does not require or maintain that
	 * match.
	 */
	if (!global_enable && info->mem_enabled && info->ranges)
		goto out;

	retval = true;

	/*
	 * Permanently (for this boot at least) opt the device into HDM
	 * operation. Individual HDM decoders still need to be enabled after
	 * this point.
	 */
	if (!global_enable) {
		dev_dbg(cxlds->dev, "Enabling HDM decode\n");
		writel(global_ctrl | CXL_HDM_DECODER_ENABLE,
		       crb + cmap->hdm_decoder.offset +
			       CXL_HDM_DECODER_CTRL_OFFSET);
	}

out:
	iounmap(crb);
	return retval;
}

/**
 * cxl_hdm_decode_init() - Setup HDM decoding for the endpoint
 * @cxlds: Device state
 * @info: DVSEC Range cached enumeration
 *
 * Try to enable the endpoint's HDM Decoder Capability
 */
int cxl_hdm_decode_init(struct cxl_dev_state *cxlds,
			struct cxl_endpoint_dvsec_info *info)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int hdm_count, rc, i, ranges = 0;
	struct device *dev = &pdev->dev;
	int d = cxlds->cxl_dvsec;
	u16 cap, ctrl;

	if (!d) {
		dev_dbg(dev, "No DVSEC Capability\n");
		return -ENXIO;
	}

	rc = pci_read_config_word(pdev, d + CXL_DVSEC_CAP_OFFSET, &cap);
	if (rc)
		return rc;

	rc = pci_read_config_word(pdev, d + CXL_DVSEC_CTRL_OFFSET, &ctrl);
	if (rc)
		return rc;

	if (!(cap & CXL_DVSEC_MEM_CAPABLE)) {
		dev_dbg(dev, "Not MEM Capable\n");
		return -ENXIO;
	}

	/*
	 * It is not allowed by spec for MEM.capable to be set and have 0 legacy
	 * HDM decoders (values > 2 are also undefined as of CXL 2.0). As this
	 * driver is for a spec defined class code which must be CXL.mem
	 * capable, there is no point in continuing to enable CXL.mem.
	 */
	hdm_count = FIELD_GET(CXL_DVSEC_HDM_COUNT_MASK, cap);
	if (!hdm_count || hdm_count > 2)
		return -EINVAL;

	rc = wait_for_valid(cxlds);
	if (rc) {
		dev_dbg(dev, "Failure awaiting MEM_INFO_VALID (%d)\n", rc);
		return rc;
	}

	info->mem_enabled = FIELD_GET(CXL_DVSEC_MEM_ENABLE, ctrl);
	if (!info->mem_enabled)
		return 0;

	for (i = 0; i < hdm_count; i++) {
		u64 base, size;
		u32 temp;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_HIGH(i), &temp);
		if (rc)
			return rc;

		size = (u64)temp << 32;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(i), &temp);
		if (rc)
			return rc;

		size |= temp & CXL_DVSEC_MEM_SIZE_LOW_MASK;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_BASE_HIGH(i), &temp);
		if (rc)
			return rc;

		base = (u64)temp << 32;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_BASE_LOW(i), &temp);
		if (rc)
			return rc;

		base |= temp & CXL_DVSEC_MEM_BASE_LOW_MASK;

		info->dvsec_range[i] = (struct range) {
			.start = base,
			.end = base + size - 1
		};

		if (size)
			ranges++;
	}

	info->ranges = ranges;

	/*
	 * If DVSEC ranges are being used instead of HDM decoder registers there
	 * is no use in trying to manage those.
	 */
	if (!__cxl_hdm_decode_init(cxlds, info)) {
		dev_err(dev,
			"Legacy range registers configuration prevents HDM operation.\n");
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_hdm_decode_init, CXL);
