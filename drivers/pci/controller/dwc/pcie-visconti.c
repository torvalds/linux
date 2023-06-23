// SPDX-License-Identifier: GPL-2.0
/*
 * DWC PCIe RC driver for Toshiba Visconti ARM SoC
 *
 * Copyright (C) 2021 Toshiba Electronic Device & Storage Corporation
 * Copyright (C) 2021 TOSHIBA CORPORATION
 *
 * Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/types.h>

#include "pcie-designware.h"
#include "../../pci.h"

struct visconti_pcie {
	struct dw_pcie pci;
	void __iomem *ulreg_base;
	void __iomem *smu_base;
	void __iomem *mpu_base;
	struct clk *refclk;
	struct clk *coreclk;
	struct clk *auxclk;
};

#define PCIE_UL_REG_S_PCIE_MODE		0x00F4
#define  PCIE_UL_REG_S_PCIE_MODE_EP	0x00
#define  PCIE_UL_REG_S_PCIE_MODE_RC	0x04

#define PCIE_UL_REG_S_PERSTN_CTRL	0x00F8
#define  PCIE_UL_IOM_PCIE_PERSTN_I_EN	BIT(3)
#define  PCIE_UL_DIRECT_PERSTN_EN	BIT(2)
#define  PCIE_UL_PERSTN_OUT		BIT(1)
#define  PCIE_UL_DIRECT_PERSTN		BIT(0)
#define  PCIE_UL_REG_S_PERSTN_CTRL_INIT	(PCIE_UL_IOM_PCIE_PERSTN_I_EN | \
					 PCIE_UL_DIRECT_PERSTN_EN | \
					 PCIE_UL_DIRECT_PERSTN)

#define PCIE_UL_REG_S_PHY_INIT_02	0x0104
#define  PCIE_UL_PHY0_SRAM_EXT_LD_DONE	BIT(0)

#define PCIE_UL_REG_S_PHY_INIT_03	0x0108
#define  PCIE_UL_PHY0_SRAM_INIT_DONE	BIT(0)

#define PCIE_UL_REG_S_INT_EVENT_MASK1	0x0138
#define  PCIE_UL_CFG_PME_INT		BIT(0)
#define  PCIE_UL_CFG_LINK_EQ_REQ_INT	BIT(1)
#define  PCIE_UL_EDMA_INT0		BIT(2)
#define  PCIE_UL_EDMA_INT1		BIT(3)
#define  PCIE_UL_EDMA_INT2		BIT(4)
#define  PCIE_UL_EDMA_INT3		BIT(5)
#define  PCIE_UL_S_INT_EVENT_MASK1_ALL  (PCIE_UL_CFG_PME_INT | \
					 PCIE_UL_CFG_LINK_EQ_REQ_INT | \
					 PCIE_UL_EDMA_INT0 | \
					 PCIE_UL_EDMA_INT1 | \
					 PCIE_UL_EDMA_INT2 | \
					 PCIE_UL_EDMA_INT3)

#define PCIE_UL_REG_S_SB_MON		0x0198
#define PCIE_UL_REG_S_SIG_MON		0x019C
#define  PCIE_UL_CORE_RST_N_MON		BIT(0)

#define PCIE_UL_REG_V_SII_DBG_00	0x0844
#define PCIE_UL_REG_V_SII_GEN_CTRL_01	0x0860
#define  PCIE_UL_APP_LTSSM_ENABLE	BIT(0)

#define PCIE_UL_REG_V_PHY_ST_00		0x0864
#define  PCIE_UL_SMLH_LINK_UP		BIT(0)

#define PCIE_UL_REG_V_PHY_ST_02		0x0868
#define  PCIE_UL_S_DETECT_ACT		0x01
#define  PCIE_UL_S_L0			0x11

#define PISMU_CKON_PCIE			0x0038
#define  PISMU_CKON_PCIE_AUX_CLK	BIT(1)
#define  PISMU_CKON_PCIE_MSTR_ACLK	BIT(0)

#define PISMU_RSOFF_PCIE		0x0538
#define  PISMU_RSOFF_PCIE_ULREG_RST_N	BIT(1)
#define  PISMU_RSOFF_PCIE_PWR_UP_RST_N	BIT(0)

#define PCIE_MPU_REG_MP_EN		0x0
#define  MPU_MP_EN_DISABLE		BIT(0)

/* Access registers in PCIe ulreg */
static void visconti_ulreg_writel(struct visconti_pcie *pcie, u32 val, u32 reg)
{
	writel_relaxed(val, pcie->ulreg_base + reg);
}

static u32 visconti_ulreg_readl(struct visconti_pcie *pcie, u32 reg)
{
	return readl_relaxed(pcie->ulreg_base + reg);
}

/* Access registers in PCIe smu */
static void visconti_smu_writel(struct visconti_pcie *pcie, u32 val, u32 reg)
{
	writel_relaxed(val, pcie->smu_base + reg);
}

/* Access registers in PCIe mpu */
static void visconti_mpu_writel(struct visconti_pcie *pcie, u32 val, u32 reg)
{
	writel_relaxed(val, pcie->mpu_base + reg);
}

static u32 visconti_mpu_readl(struct visconti_pcie *pcie, u32 reg)
{
	return readl_relaxed(pcie->mpu_base + reg);
}

static int visconti_pcie_link_up(struct dw_pcie *pci)
{
	struct visconti_pcie *pcie = dev_get_drvdata(pci->dev);
	void __iomem *addr = pcie->ulreg_base;
	u32 val = readl_relaxed(addr + PCIE_UL_REG_V_PHY_ST_02);

	return !!(val & PCIE_UL_S_L0);
}

static int visconti_pcie_start_link(struct dw_pcie *pci)
{
	struct visconti_pcie *pcie = dev_get_drvdata(pci->dev);
	void __iomem *addr = pcie->ulreg_base;
	u32 val;
	int ret;

	visconti_ulreg_writel(pcie, PCIE_UL_APP_LTSSM_ENABLE,
			      PCIE_UL_REG_V_SII_GEN_CTRL_01);

	ret = readl_relaxed_poll_timeout(addr + PCIE_UL_REG_V_PHY_ST_02,
					 val, (val & PCIE_UL_S_L0),
					 90000, 100000);
	if (ret)
		return ret;

	visconti_ulreg_writel(pcie, PCIE_UL_S_INT_EVENT_MASK1_ALL,
			      PCIE_UL_REG_S_INT_EVENT_MASK1);

	if (dw_pcie_link_up(pci)) {
		val = visconti_mpu_readl(pcie, PCIE_MPU_REG_MP_EN);
		visconti_mpu_writel(pcie, val & ~MPU_MP_EN_DISABLE,
				    PCIE_MPU_REG_MP_EN);
	}

	return 0;
}

static void visconti_pcie_stop_link(struct dw_pcie *pci)
{
	struct visconti_pcie *pcie = dev_get_drvdata(pci->dev);
	u32 val;

	val = visconti_ulreg_readl(pcie, PCIE_UL_REG_V_SII_GEN_CTRL_01);
	val &= ~PCIE_UL_APP_LTSSM_ENABLE;
	visconti_ulreg_writel(pcie, val, PCIE_UL_REG_V_SII_GEN_CTRL_01);

	val = visconti_mpu_readl(pcie, PCIE_MPU_REG_MP_EN);
	visconti_mpu_writel(pcie, val | MPU_MP_EN_DISABLE, PCIE_MPU_REG_MP_EN);
}

/*
 * In this SoC specification, the CPU bus outputs the offset value from
 * 0x40000000 to the PCIe bus, so 0x40000000 is subtracted from the CPU
 * bus address. This 0x40000000 is also based on io_base from DT.
 */
static u64 visconti_pcie_cpu_addr_fixup(struct dw_pcie *pci, u64 cpu_addr)
{
	struct dw_pcie_rp *pp = &pci->pp;

	return cpu_addr & ~pp->io_base;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.cpu_addr_fixup = visconti_pcie_cpu_addr_fixup,
	.link_up = visconti_pcie_link_up,
	.start_link = visconti_pcie_start_link,
	.stop_link = visconti_pcie_stop_link,
};

static int visconti_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct visconti_pcie *pcie = dev_get_drvdata(pci->dev);
	void __iomem *addr;
	int err;
	u32 val;

	visconti_smu_writel(pcie,
			    PISMU_CKON_PCIE_AUX_CLK | PISMU_CKON_PCIE_MSTR_ACLK,
			    PISMU_CKON_PCIE);
	ndelay(250);

	visconti_smu_writel(pcie, PISMU_RSOFF_PCIE_ULREG_RST_N,
			    PISMU_RSOFF_PCIE);
	visconti_ulreg_writel(pcie, PCIE_UL_REG_S_PCIE_MODE_RC,
			      PCIE_UL_REG_S_PCIE_MODE);

	val = PCIE_UL_REG_S_PERSTN_CTRL_INIT;
	visconti_ulreg_writel(pcie, val, PCIE_UL_REG_S_PERSTN_CTRL);
	udelay(100);

	val |= PCIE_UL_PERSTN_OUT;
	visconti_ulreg_writel(pcie, val, PCIE_UL_REG_S_PERSTN_CTRL);
	udelay(100);

	visconti_smu_writel(pcie, PISMU_RSOFF_PCIE_PWR_UP_RST_N,
			    PISMU_RSOFF_PCIE);

	addr = pcie->ulreg_base + PCIE_UL_REG_S_PHY_INIT_03;
	err = readl_relaxed_poll_timeout(addr, val,
					 (val & PCIE_UL_PHY0_SRAM_INIT_DONE),
					 100, 1000);
	if (err)
		return err;

	visconti_ulreg_writel(pcie, PCIE_UL_PHY0_SRAM_EXT_LD_DONE,
			      PCIE_UL_REG_S_PHY_INIT_02);

	addr = pcie->ulreg_base + PCIE_UL_REG_S_SIG_MON;
	return readl_relaxed_poll_timeout(addr, val,
					  (val & PCIE_UL_CORE_RST_N_MON), 100,
					  1000);
}

static const struct dw_pcie_host_ops visconti_pcie_host_ops = {
	.host_init = visconti_pcie_host_init,
};

static int visconti_get_resources(struct platform_device *pdev,
				  struct visconti_pcie *pcie)
{
	struct device *dev = &pdev->dev;

	pcie->ulreg_base = devm_platform_ioremap_resource_byname(pdev, "ulreg");
	if (IS_ERR(pcie->ulreg_base))
		return PTR_ERR(pcie->ulreg_base);

	pcie->smu_base = devm_platform_ioremap_resource_byname(pdev, "smu");
	if (IS_ERR(pcie->smu_base))
		return PTR_ERR(pcie->smu_base);

	pcie->mpu_base = devm_platform_ioremap_resource_byname(pdev, "mpu");
	if (IS_ERR(pcie->mpu_base))
		return PTR_ERR(pcie->mpu_base);

	pcie->refclk = devm_clk_get(dev, "ref");
	if (IS_ERR(pcie->refclk))
		return dev_err_probe(dev, PTR_ERR(pcie->refclk),
				     "Failed to get ref clock\n");

	pcie->coreclk = devm_clk_get(dev, "core");
	if (IS_ERR(pcie->coreclk))
		return dev_err_probe(dev, PTR_ERR(pcie->coreclk),
				     "Failed to get core clock\n");

	pcie->auxclk = devm_clk_get(dev, "aux");
	if (IS_ERR(pcie->auxclk))
		return dev_err_probe(dev, PTR_ERR(pcie->auxclk),
				     "Failed to get aux clock\n");

	return 0;
}

static int visconti_add_pcie_port(struct visconti_pcie *pcie,
				  struct platform_device *pdev)
{
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;

	pp->irq = platform_get_irq_byname(pdev, "intr");
	if (pp->irq < 0)
		return pp->irq;

	pp->ops = &visconti_pcie_host_ops;

	return dw_pcie_host_init(pp);
}

static int visconti_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct visconti_pcie *pcie;
	struct dw_pcie *pci;
	int ret;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = &pcie->pci;
	pci->dev = dev;
	pci->ops = &dw_pcie_ops;

	ret = visconti_get_resources(pdev, pcie);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, pcie);

	return visconti_add_pcie_port(pcie, pdev);
}

static const struct of_device_id visconti_pcie_match[] = {
	{ .compatible = "toshiba,visconti-pcie" },
	{},
};

static struct platform_driver visconti_pcie_driver = {
	.probe = visconti_pcie_probe,
	.driver = {
		.name = "visconti-pcie",
		.of_match_table = visconti_pcie_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(visconti_pcie_driver);
