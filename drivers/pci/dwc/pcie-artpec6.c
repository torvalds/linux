/*
 * PCIe host controller driver for Axis ARTPEC-6 SoC
 *
 * Author: Niklas Cassel <niklas.cassel@axis.com>
 *
 * Based on work done by Phil Edworthy <phil@edworthys.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/signal.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pcie-designware.h"

#define to_artpec6_pcie(x)	dev_get_drvdata((x)->dev)

struct artpec6_pcie {
	struct dw_pcie		*pci;
	struct regmap		*regmap;	/* DT axis,syscon-pcie */
	void __iomem		*phy_base;	/* DT phy */
};

/* PCIe Port Logic registers (memory-mapped) */
#define PL_OFFSET			0x700
#define PCIE_PHY_DEBUG_R0		(PL_OFFSET + 0x28)
#define PCIE_PHY_DEBUG_R1		(PL_OFFSET + 0x2c)

#define MISC_CONTROL_1_OFF		(PL_OFFSET + 0x1bc)
#define  DBI_RO_WR_EN			1

/* ARTPEC-6 specific registers */
#define PCIECFG				0x18
#define  PCIECFG_DBG_OEN		(1 << 24)
#define  PCIECFG_CORE_RESET_REQ		(1 << 21)
#define  PCIECFG_LTSSM_ENABLE		(1 << 20)
#define  PCIECFG_CLKREQ_B		(1 << 11)
#define  PCIECFG_REFCLK_ENABLE		(1 << 10)
#define  PCIECFG_PLL_ENABLE		(1 << 9)
#define  PCIECFG_PCLK_ENABLE		(1 << 8)
#define  PCIECFG_RISRCREN		(1 << 4)
#define  PCIECFG_MODE_TX_DRV_EN		(1 << 3)
#define  PCIECFG_CISRREN		(1 << 2)
#define  PCIECFG_MACRO_ENABLE		(1 << 0)

#define NOCCFG				0x40
#define NOCCFG_ENABLE_CLK_PCIE		(1 << 4)
#define NOCCFG_POWER_PCIE_IDLEACK	(1 << 3)
#define NOCCFG_POWER_PCIE_IDLE		(1 << 2)
#define NOCCFG_POWER_PCIE_IDLEREQ	(1 << 1)

#define PHY_STATUS			0x118
#define PHY_COSPLLLOCK			(1 << 0)

#define ARTPEC6_CPU_TO_BUS_ADDR		0x0fffffff

static u32 artpec6_pcie_readl(struct artpec6_pcie *artpec6_pcie, u32 offset)
{
	u32 val;

	regmap_read(artpec6_pcie->regmap, offset, &val);
	return val;
}

static void artpec6_pcie_writel(struct artpec6_pcie *artpec6_pcie, u32 offset, u32 val)
{
	regmap_write(artpec6_pcie->regmap, offset, val);
}

static int artpec6_pcie_establish_link(struct artpec6_pcie *artpec6_pcie)
{
	struct dw_pcie *pci = artpec6_pcie->pci;
	struct pcie_port *pp = &pci->pp;
	u32 val;
	unsigned int retries;

	/* Hold DW core in reset */
	val = artpec6_pcie_readl(artpec6_pcie, PCIECFG);
	val |= PCIECFG_CORE_RESET_REQ;
	artpec6_pcie_writel(artpec6_pcie, PCIECFG, val);

	val = artpec6_pcie_readl(artpec6_pcie, PCIECFG);
	val |=  PCIECFG_RISRCREN |	/* Receiver term. 50 Ohm */
		PCIECFG_MODE_TX_DRV_EN |
		PCIECFG_CISRREN |	/* Reference clock term. 100 Ohm */
		PCIECFG_MACRO_ENABLE;
	val |= PCIECFG_REFCLK_ENABLE;
	val &= ~PCIECFG_DBG_OEN;
	val &= ~PCIECFG_CLKREQ_B;
	artpec6_pcie_writel(artpec6_pcie, PCIECFG, val);
	usleep_range(5000, 6000);

	val = artpec6_pcie_readl(artpec6_pcie, NOCCFG);
	val |= NOCCFG_ENABLE_CLK_PCIE;
	artpec6_pcie_writel(artpec6_pcie, NOCCFG, val);
	usleep_range(20, 30);

	val = artpec6_pcie_readl(artpec6_pcie, PCIECFG);
	val |= PCIECFG_PCLK_ENABLE | PCIECFG_PLL_ENABLE;
	artpec6_pcie_writel(artpec6_pcie, PCIECFG, val);
	usleep_range(6000, 7000);

	val = artpec6_pcie_readl(artpec6_pcie, NOCCFG);
	val &= ~NOCCFG_POWER_PCIE_IDLEREQ;
	artpec6_pcie_writel(artpec6_pcie, NOCCFG, val);

	retries = 50;
	do {
		usleep_range(1000, 2000);
		val = artpec6_pcie_readl(artpec6_pcie, NOCCFG);
		retries--;
	} while (retries &&
		(val & (NOCCFG_POWER_PCIE_IDLEACK | NOCCFG_POWER_PCIE_IDLE)));

	retries = 50;
	do {
		usleep_range(1000, 2000);
		val = readl(artpec6_pcie->phy_base + PHY_STATUS);
		retries--;
	} while (retries && !(val & PHY_COSPLLLOCK));

	/* Take DW core out of reset */
	val = artpec6_pcie_readl(artpec6_pcie, PCIECFG);
	val &= ~PCIECFG_CORE_RESET_REQ;
	artpec6_pcie_writel(artpec6_pcie, PCIECFG, val);
	usleep_range(100, 200);

	/*
	 * Enable writing to config regs. This is required as the Synopsys
	 * driver changes the class code. That register needs DBI write enable.
	 */
	dw_pcie_writel_dbi(pci, MISC_CONTROL_1_OFF, DBI_RO_WR_EN);

	pp->io_base &= ARTPEC6_CPU_TO_BUS_ADDR;
	pp->mem_base &= ARTPEC6_CPU_TO_BUS_ADDR;
	pp->cfg0_base &= ARTPEC6_CPU_TO_BUS_ADDR;
	pp->cfg1_base &= ARTPEC6_CPU_TO_BUS_ADDR;

	/* setup root complex */
	dw_pcie_setup_rc(pp);

	/* assert LTSSM enable */
	val = artpec6_pcie_readl(artpec6_pcie, PCIECFG);
	val |= PCIECFG_LTSSM_ENABLE;
	artpec6_pcie_writel(artpec6_pcie, PCIECFG, val);

	/* check if the link is up or not */
	if (!dw_pcie_wait_for_link(pci))
		return 0;

	dev_dbg(pci->dev, "DEBUG_R0: 0x%08x, DEBUG_R1: 0x%08x\n",
		dw_pcie_readl_dbi(pci, PCIE_PHY_DEBUG_R0),
		dw_pcie_readl_dbi(pci, PCIE_PHY_DEBUG_R1));

	return -ETIMEDOUT;
}

static void artpec6_pcie_enable_interrupts(struct artpec6_pcie *artpec6_pcie)
{
	struct dw_pcie *pci = artpec6_pcie->pci;
	struct pcie_port *pp = &pci->pp;

	if (IS_ENABLED(CONFIG_PCI_MSI))
		dw_pcie_msi_init(pp);
}

static void artpec6_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct artpec6_pcie *artpec6_pcie = to_artpec6_pcie(pci);

	artpec6_pcie_establish_link(artpec6_pcie);
	artpec6_pcie_enable_interrupts(artpec6_pcie);
}

static struct dw_pcie_host_ops artpec6_pcie_host_ops = {
	.host_init = artpec6_pcie_host_init,
};

static irqreturn_t artpec6_pcie_msi_handler(int irq, void *arg)
{
	struct artpec6_pcie *artpec6_pcie = arg;
	struct dw_pcie *pci = artpec6_pcie->pci;
	struct pcie_port *pp = &pci->pp;

	return dw_handle_msi_irq(pp);
}

static int artpec6_add_pcie_port(struct artpec6_pcie *artpec6_pcie,
				 struct platform_device *pdev)
{
	struct dw_pcie *pci = artpec6_pcie->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = pci->dev;
	int ret;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		pp->msi_irq = platform_get_irq_byname(pdev, "msi");
		if (pp->msi_irq <= 0) {
			dev_err(dev, "failed to get MSI irq\n");
			return -ENODEV;
		}

		ret = devm_request_irq(dev, pp->msi_irq,
				       artpec6_pcie_msi_handler,
				       IRQF_SHARED | IRQF_NO_THREAD,
				       "artpec6-pcie-msi", artpec6_pcie);
		if (ret) {
			dev_err(dev, "failed to request MSI irq\n");
			return ret;
		}
	}

	pp->root_bus_nr = -1;
	pp->ops = &artpec6_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static const struct dw_pcie_ops dw_pcie_ops = {
};

static int artpec6_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct artpec6_pcie *artpec6_pcie;
	struct resource *dbi_base;
	struct resource *phy_base;
	int ret;

	artpec6_pcie = devm_kzalloc(dev, sizeof(*artpec6_pcie), GFP_KERNEL);
	if (!artpec6_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;

	artpec6_pcie->pci = pci;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
	pci->dbi_base = devm_ioremap_resource(dev, dbi_base);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	phy_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy");
	artpec6_pcie->phy_base = devm_ioremap_resource(dev, phy_base);
	if (IS_ERR(artpec6_pcie->phy_base))
		return PTR_ERR(artpec6_pcie->phy_base);

	artpec6_pcie->regmap =
		syscon_regmap_lookup_by_phandle(dev->of_node,
						"axis,syscon-pcie");
	if (IS_ERR(artpec6_pcie->regmap))
		return PTR_ERR(artpec6_pcie->regmap);

	platform_set_drvdata(pdev, artpec6_pcie);

	ret = artpec6_add_pcie_port(artpec6_pcie, pdev);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct of_device_id artpec6_pcie_of_match[] = {
	{ .compatible = "axis,artpec6-pcie", },
	{},
};

static struct platform_driver artpec6_pcie_driver = {
	.probe = artpec6_pcie_probe,
	.driver = {
		.name	= "artpec6-pcie",
		.of_match_table = artpec6_pcie_of_match,
	},
};
builtin_platform_driver(artpec6_pcie_driver);
