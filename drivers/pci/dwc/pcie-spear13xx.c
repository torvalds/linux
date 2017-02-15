/*
 * PCIe host controller driver for ST Microelectronics SPEAr13xx SoCs
 *
 * SPEAr13xx PCIe Glue Layer Source Code
 *
 * Copyright (C) 2010-2014 ST Microelectronics
 * Pratyush Anand <pratyush.anand@gmail.com>
 * Mohit Kumar <mohit.kumar.dhaka@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/resource.h>

#include "pcie-designware.h"

struct spear13xx_pcie {
	struct pcie_port	pp;		/* DT dbi is pp.dbi_base */
	void __iomem		*app_base;
	struct phy		*phy;
	struct clk		*clk;
	bool			is_gen1;
};

struct pcie_app_reg {
	u32	app_ctrl_0;		/* cr0 */
	u32	app_ctrl_1;		/* cr1 */
	u32	app_status_0;		/* cr2 */
	u32	app_status_1;		/* cr3 */
	u32	msg_status;		/* cr4 */
	u32	msg_payload;		/* cr5 */
	u32	int_sts;		/* cr6 */
	u32	int_clr;		/* cr7 */
	u32	int_mask;		/* cr8 */
	u32	mst_bmisc;		/* cr9 */
	u32	phy_ctrl;		/* cr10 */
	u32	phy_status;		/* cr11 */
	u32	cxpl_debug_info_0;	/* cr12 */
	u32	cxpl_debug_info_1;	/* cr13 */
	u32	ven_msg_ctrl_0;		/* cr14 */
	u32	ven_msg_ctrl_1;		/* cr15 */
	u32	ven_msg_data_0;		/* cr16 */
	u32	ven_msg_data_1;		/* cr17 */
	u32	ven_msi_0;		/* cr18 */
	u32	ven_msi_1;		/* cr19 */
	u32	mst_rmisc;		/* cr20 */
};

/* CR0 ID */
#define APP_LTSSM_ENABLE_ID			3
#define DEVICE_TYPE_RC				(4 << 25)
#define MISCTRL_EN_ID				30
#define REG_TRANSLATION_ENABLE			31

/* CR3 ID */
#define XMLH_LINK_UP				(1 << 6)

/* CR6 */
#define MSI_CTRL_INT				(1 << 26)

#define EXP_CAP_ID_OFFSET			0x70

#define to_spear13xx_pcie(x)	container_of(x, struct spear13xx_pcie, pp)

static int spear13xx_pcie_establish_link(struct spear13xx_pcie *spear13xx_pcie)
{
	struct pcie_port *pp = &spear13xx_pcie->pp;
	struct pcie_app_reg *app_reg = spear13xx_pcie->app_base;
	u32 val;
	u32 exp_cap_off = EXP_CAP_ID_OFFSET;

	if (dw_pcie_link_up(pp)) {
		dev_err(pp->dev, "link already up\n");
		return 0;
	}

	dw_pcie_setup_rc(pp);

	/*
	 * this controller support only 128 bytes read size, however its
	 * default value in capability register is 512 bytes. So force
	 * it to 128 here.
	 */
	dw_pcie_cfg_read(pp->dbi_base + exp_cap_off + PCI_EXP_DEVCTL, 2, &val);
	val &= ~PCI_EXP_DEVCTL_READRQ;
	dw_pcie_cfg_write(pp->dbi_base + exp_cap_off + PCI_EXP_DEVCTL, 2, val);

	dw_pcie_cfg_write(pp->dbi_base + PCI_VENDOR_ID, 2, 0x104A);
	dw_pcie_cfg_write(pp->dbi_base + PCI_DEVICE_ID, 2, 0xCD80);

	/*
	 * if is_gen1 is set then handle it, so that some buggy card
	 * also works
	 */
	if (spear13xx_pcie->is_gen1) {
		dw_pcie_cfg_read(pp->dbi_base + exp_cap_off + PCI_EXP_LNKCAP,
					4, &val);
		if ((val & PCI_EXP_LNKCAP_SLS) != PCI_EXP_LNKCAP_SLS_2_5GB) {
			val &= ~((u32)PCI_EXP_LNKCAP_SLS);
			val |= PCI_EXP_LNKCAP_SLS_2_5GB;
			dw_pcie_cfg_write(pp->dbi_base + exp_cap_off +
				PCI_EXP_LNKCAP, 4, val);
		}

		dw_pcie_cfg_read(pp->dbi_base + exp_cap_off + PCI_EXP_LNKCTL2,
					2, &val);
		if ((val & PCI_EXP_LNKCAP_SLS) != PCI_EXP_LNKCAP_SLS_2_5GB) {
			val &= ~((u32)PCI_EXP_LNKCAP_SLS);
			val |= PCI_EXP_LNKCAP_SLS_2_5GB;
			dw_pcie_cfg_write(pp->dbi_base + exp_cap_off +
					PCI_EXP_LNKCTL2, 2, val);
		}
	}

	/* enable ltssm */
	writel(DEVICE_TYPE_RC | (1 << MISCTRL_EN_ID)
			| (1 << APP_LTSSM_ENABLE_ID)
			| ((u32)1 << REG_TRANSLATION_ENABLE),
			&app_reg->app_ctrl_0);

	return dw_pcie_wait_for_link(pp);
}

static irqreturn_t spear13xx_pcie_irq_handler(int irq, void *arg)
{
	struct spear13xx_pcie *spear13xx_pcie = arg;
	struct pcie_app_reg *app_reg = spear13xx_pcie->app_base;
	struct pcie_port *pp = &spear13xx_pcie->pp;
	unsigned int status;

	status = readl(&app_reg->int_sts);

	if (status & MSI_CTRL_INT) {
		BUG_ON(!IS_ENABLED(CONFIG_PCI_MSI));
		dw_handle_msi_irq(pp);
	}

	writel(status, &app_reg->int_clr);

	return IRQ_HANDLED;
}

static void spear13xx_pcie_enable_interrupts(struct spear13xx_pcie *spear13xx_pcie)
{
	struct pcie_port *pp = &spear13xx_pcie->pp;
	struct pcie_app_reg *app_reg = spear13xx_pcie->app_base;

	/* Enable MSI interrupt */
	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		dw_pcie_msi_init(pp);
		writel(readl(&app_reg->int_mask) |
				MSI_CTRL_INT, &app_reg->int_mask);
	}
}

static int spear13xx_pcie_link_up(struct pcie_port *pp)
{
	struct spear13xx_pcie *spear13xx_pcie = to_spear13xx_pcie(pp);
	struct pcie_app_reg *app_reg = spear13xx_pcie->app_base;

	if (readl(&app_reg->app_status_1) & XMLH_LINK_UP)
		return 1;

	return 0;
}

static void spear13xx_pcie_host_init(struct pcie_port *pp)
{
	struct spear13xx_pcie *spear13xx_pcie = to_spear13xx_pcie(pp);

	spear13xx_pcie_establish_link(spear13xx_pcie);
	spear13xx_pcie_enable_interrupts(spear13xx_pcie);
}

static struct pcie_host_ops spear13xx_pcie_host_ops = {
	.link_up = spear13xx_pcie_link_up,
	.host_init = spear13xx_pcie_host_init,
};

static int spear13xx_add_pcie_port(struct spear13xx_pcie *spear13xx_pcie,
				   struct platform_device *pdev)
{
	struct pcie_port *pp = &spear13xx_pcie->pp;
	struct device *dev = pp->dev;
	int ret;

	pp->irq = platform_get_irq(pdev, 0);
	if (!pp->irq) {
		dev_err(dev, "failed to get irq\n");
		return -ENODEV;
	}
	ret = devm_request_irq(dev, pp->irq, spear13xx_pcie_irq_handler,
			       IRQF_SHARED | IRQF_NO_THREAD,
			       "spear1340-pcie", spear13xx_pcie);
	if (ret) {
		dev_err(dev, "failed to request irq %d\n", pp->irq);
		return ret;
	}

	pp->root_bus_nr = -1;
	pp->ops = &spear13xx_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int spear13xx_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spear13xx_pcie *spear13xx_pcie;
	struct pcie_port *pp;
	struct device_node *np = dev->of_node;
	struct resource *dbi_base;
	int ret;

	spear13xx_pcie = devm_kzalloc(dev, sizeof(*spear13xx_pcie), GFP_KERNEL);
	if (!spear13xx_pcie)
		return -ENOMEM;

	spear13xx_pcie->phy = devm_phy_get(dev, "pcie-phy");
	if (IS_ERR(spear13xx_pcie->phy)) {
		ret = PTR_ERR(spear13xx_pcie->phy);
		if (ret == -EPROBE_DEFER)
			dev_info(dev, "probe deferred\n");
		else
			dev_err(dev, "couldn't get pcie-phy\n");
		return ret;
	}

	phy_init(spear13xx_pcie->phy);

	spear13xx_pcie->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(spear13xx_pcie->clk)) {
		dev_err(dev, "couldn't get clk for pcie\n");
		return PTR_ERR(spear13xx_pcie->clk);
	}
	ret = clk_prepare_enable(spear13xx_pcie->clk);
	if (ret) {
		dev_err(dev, "couldn't enable clk for pcie\n");
		return ret;
	}

	pp = &spear13xx_pcie->pp;
	pp->dev = dev;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
	pp->dbi_base = devm_ioremap_resource(dev, dbi_base);
	if (IS_ERR(pp->dbi_base)) {
		dev_err(dev, "couldn't remap dbi base %p\n", dbi_base);
		ret = PTR_ERR(pp->dbi_base);
		goto fail_clk;
	}
	spear13xx_pcie->app_base = pp->dbi_base + 0x2000;

	if (of_property_read_bool(np, "st,pcie-is-gen1"))
		spear13xx_pcie->is_gen1 = true;

	platform_set_drvdata(pdev, spear13xx_pcie);

	ret = spear13xx_add_pcie_port(spear13xx_pcie, pdev);
	if (ret < 0)
		goto fail_clk;

	return 0;

fail_clk:
	clk_disable_unprepare(spear13xx_pcie->clk);

	return ret;
}

static const struct of_device_id spear13xx_pcie_of_match[] = {
	{ .compatible = "st,spear1340-pcie", },
	{},
};

static struct platform_driver spear13xx_pcie_driver = {
	.probe		= spear13xx_pcie_probe,
	.driver = {
		.name	= "spear-pcie",
		.of_match_table = of_match_ptr(spear13xx_pcie_of_match),
	},
};

builtin_platform_driver(spear13xx_pcie_driver);
