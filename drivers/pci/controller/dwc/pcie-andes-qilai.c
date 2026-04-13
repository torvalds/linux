// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the PCIe Controller in QiLai from Andes
 *
 * Copyright (C) 2026 Andes Technology Corporation
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>

#include "pcie-designware.h"

#define PCIE_INTR_CONTROL1			0x15c
#define PCIE_MSI_CTRL_INT_EN			BIT(28)

#define PCIE_LOGIC_COHERENCY_CONTROL3		0x8e8

/*
 * Refer to Table A4-5 (Memory type encoding) in the
 * AMBA AXI and ACE Protocol Specification.
 *
 * The selected value corresponds to the Memory type field:
 * "Write-back, Read and Write-allocate".
 *
 * The last three rows in the table A4-5 in
 * AMBA AXI and ACE Protocol Specification:
 * ARCACHE        AWCACHE        Memory type
 * ------------------------------------------------------------------
 * 1111 (0111)    0111           Write-back Read-allocate
 * 1011           1111 (1011)    Write-back Write-allocate
 * 1111           1111           Write-back Read and Write-allocate (selected)
 */
#define IOCP_ARCACHE				0b1111
#define IOCP_AWCACHE				0b1111

#define PCIE_CFG_MSTR_ARCACHE_MODE		GENMASK(6, 3)
#define PCIE_CFG_MSTR_AWCACHE_MODE		GENMASK(14, 11)
#define PCIE_CFG_MSTR_ARCACHE_VALUE		GENMASK(22, 19)
#define PCIE_CFG_MSTR_AWCACHE_VALUE		GENMASK(30, 27)

#define PCIE_GEN_CONTROL2			0x54
#define PCIE_CFG_LTSSM_EN			BIT(0)

#define PCIE_REGS_PCIE_SII_PM_STATE		0xc0
#define SMLH_LINK_UP				BIT(6)
#define RDLH_LINK_UP				BIT(7)

struct qilai_pcie {
	struct dw_pcie pci;
	void __iomem *apb_base;
};

#define to_qilai_pcie(_pci) container_of(_pci, struct qilai_pcie, pci)

static bool qilai_pcie_link_up(struct dw_pcie *pci)
{
	struct qilai_pcie *pcie = to_qilai_pcie(pci);
	u32 val;

	val = readl(pcie->apb_base + PCIE_REGS_PCIE_SII_PM_STATE);

	return FIELD_GET(SMLH_LINK_UP, val) && FIELD_GET(RDLH_LINK_UP, val);
}

static int qilai_pcie_start_link(struct dw_pcie *pci)
{
	struct qilai_pcie *pcie = to_qilai_pcie(pci);
	u32 val;

	val = readl(pcie->apb_base + PCIE_GEN_CONTROL2);
	val |= PCIE_CFG_LTSSM_EN;
	writel(val, pcie->apb_base + PCIE_GEN_CONTROL2);

	return 0;
}

static const struct dw_pcie_ops qilai_pcie_ops = {
	.link_up = qilai_pcie_link_up,
	.start_link = qilai_pcie_start_link,
};

/*
 * Set up the QiLai PCIe IOCP (IO Coherence Port) Read/Write Behaviors to the
 * Write-Back, Read and Write Allocate mode.
 *
 * The IOCP HW target is SoC last-level cache (L2 Cache), which serves as the
 * system cache. The IOCP HW helps maintain cache monitoring, ensuring that
 * the device can snoop data from/to the cache.
 */
static void qilai_pcie_iocp_cache_setup(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	u32 val;

	dw_pcie_dbi_ro_wr_en(pci);

	val = dw_pcie_readl_dbi(pci, PCIE_LOGIC_COHERENCY_CONTROL3);
	FIELD_MODIFY(PCIE_CFG_MSTR_ARCACHE_MODE, &val, IOCP_ARCACHE);
	FIELD_MODIFY(PCIE_CFG_MSTR_AWCACHE_MODE, &val, IOCP_AWCACHE);
	FIELD_MODIFY(PCIE_CFG_MSTR_ARCACHE_VALUE, &val, IOCP_ARCACHE);
	FIELD_MODIFY(PCIE_CFG_MSTR_AWCACHE_VALUE, &val, IOCP_AWCACHE);
	dw_pcie_writel_dbi(pci, PCIE_LOGIC_COHERENCY_CONTROL3, val);

	dw_pcie_dbi_ro_wr_dis(pci);
}

static void qilai_pcie_enable_msi(struct qilai_pcie *pcie)
{
	u32 val;

	val = readl(pcie->apb_base + PCIE_INTR_CONTROL1);
	val |= PCIE_MSI_CTRL_INT_EN;
	writel(val, pcie->apb_base + PCIE_INTR_CONTROL1);
}

static int qilai_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct qilai_pcie *pcie = to_qilai_pcie(pci);

	qilai_pcie_enable_msi(pcie);

	return 0;
}

static void qilai_pcie_host_post_init(struct dw_pcie_rp *pp)
{
	qilai_pcie_iocp_cache_setup(pp);
}

static const struct dw_pcie_host_ops qilai_pcie_host_ops = {
	.init = qilai_pcie_host_init,
	.post_init = qilai_pcie_host_post_init,
};

static int qilai_pcie_probe(struct platform_device *pdev)
{
	struct qilai_pcie *pcie;
	struct dw_pcie *pci;
	struct device *dev = &pdev->dev;
	int ret;

	pcie = devm_kzalloc(&pdev->dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	platform_set_drvdata(pdev, pcie);

	pci = &pcie->pci;
	pcie->pci.dev = dev;
	pcie->pci.ops = &qilai_pcie_ops;
	pcie->pci.pp.ops = &qilai_pcie_host_ops;
	pci->use_parent_dt_ranges = true;

	dw_pcie_cap_set(&pcie->pci, REQ_RES);

	pcie->apb_base = devm_platform_ioremap_resource_byname(pdev, "apb");
	if (IS_ERR(pcie->apb_base))
		return PTR_ERR(pcie->apb_base);

	pm_runtime_set_active(dev);
	pm_runtime_no_callbacks(dev);
	devm_pm_runtime_enable(dev);

	ret = dw_pcie_host_init(&pcie->pci.pp);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialize PCIe host\n");

	return 0;
}

static const struct of_device_id qilai_pcie_of_match[] = {
	{ .compatible = "andestech,qilai-pcie" },
	{},
};
MODULE_DEVICE_TABLE(of, qilai_pcie_of_match);

static struct platform_driver qilai_pcie_driver = {
	.probe = qilai_pcie_probe,
	.driver = {
		.name	= "qilai-pcie",
		.of_match_table = qilai_pcie_of_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

builtin_platform_driver(qilai_pcie_driver);

MODULE_AUTHOR("Randolph Lin <randolph@andestech.com>");
MODULE_DESCRIPTION("Andes QiLai PCIe driver");
MODULE_LICENSE("GPL");
