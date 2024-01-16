// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for HiSilicon STB SoCs
 *
 * Copyright (C) 2016-2017 HiSilicon Co., Ltd. http://www.hisilicon.com
 *
 * Authors: Ruqiang Ju <juruqiang@hisilicon.com>
 *          Jianguo Sun <sunjianguo1@huawei.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/reset.h>

#include "pcie-designware.h"

#define to_histb_pcie(x)	dev_get_drvdata((x)->dev)

#define PCIE_SYS_CTRL0			0x0000
#define PCIE_SYS_CTRL1			0x0004
#define PCIE_SYS_CTRL7			0x001C
#define PCIE_SYS_CTRL13			0x0034
#define PCIE_SYS_CTRL15			0x003C
#define PCIE_SYS_CTRL16			0x0040
#define PCIE_SYS_CTRL17			0x0044

#define PCIE_SYS_STAT0			0x0100
#define PCIE_SYS_STAT4			0x0110

#define PCIE_RDLH_LINK_UP		BIT(5)
#define PCIE_XMLH_LINK_UP		BIT(15)
#define PCIE_ELBI_SLV_DBI_ENABLE	BIT(21)
#define PCIE_APP_LTSSM_ENABLE		BIT(11)

#define PCIE_DEVICE_TYPE_MASK		GENMASK(31, 28)
#define PCIE_WM_EP			0
#define PCIE_WM_LEGACY			BIT(1)
#define PCIE_WM_RC			BIT(30)

#define PCIE_LTSSM_STATE_MASK		GENMASK(5, 0)
#define PCIE_LTSSM_STATE_ACTIVE		0x11

struct histb_pcie {
	struct dw_pcie *pci;
	struct clk *aux_clk;
	struct clk *pipe_clk;
	struct clk *sys_clk;
	struct clk *bus_clk;
	struct phy *phy;
	struct reset_control *soft_reset;
	struct reset_control *sys_reset;
	struct reset_control *bus_reset;
	void __iomem *ctrl;
	int reset_gpio;
	struct regulator *vpcie;
};

static u32 histb_pcie_readl(struct histb_pcie *histb_pcie, u32 reg)
{
	return readl(histb_pcie->ctrl + reg);
}

static void histb_pcie_writel(struct histb_pcie *histb_pcie, u32 reg, u32 val)
{
	writel(val, histb_pcie->ctrl + reg);
}

static void histb_pcie_dbi_w_mode(struct dw_pcie_rp *pp, bool enable)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct histb_pcie *hipcie = to_histb_pcie(pci);
	u32 val;

	val = histb_pcie_readl(hipcie, PCIE_SYS_CTRL0);
	if (enable)
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
	histb_pcie_writel(hipcie, PCIE_SYS_CTRL0, val);
}

static void histb_pcie_dbi_r_mode(struct dw_pcie_rp *pp, bool enable)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct histb_pcie *hipcie = to_histb_pcie(pci);
	u32 val;

	val = histb_pcie_readl(hipcie, PCIE_SYS_CTRL1);
	if (enable)
		val |= PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val &= ~PCIE_ELBI_SLV_DBI_ENABLE;
	histb_pcie_writel(hipcie, PCIE_SYS_CTRL1, val);
}

static u32 histb_pcie_read_dbi(struct dw_pcie *pci, void __iomem *base,
			       u32 reg, size_t size)
{
	u32 val;

	histb_pcie_dbi_r_mode(&pci->pp, true);
	dw_pcie_read(base + reg, size, &val);
	histb_pcie_dbi_r_mode(&pci->pp, false);

	return val;
}

static void histb_pcie_write_dbi(struct dw_pcie *pci, void __iomem *base,
				 u32 reg, size_t size, u32 val)
{
	histb_pcie_dbi_w_mode(&pci->pp, true);
	dw_pcie_write(base + reg, size, val);
	histb_pcie_dbi_w_mode(&pci->pp, false);
}

static int histb_pcie_rd_own_conf(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 *val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);

	if (PCI_SLOT(devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*val = dw_pcie_read_dbi(pci, where, size);
	return PCIBIOS_SUCCESSFUL;
}

static int histb_pcie_wr_own_conf(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);

	if (PCI_SLOT(devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	dw_pcie_write_dbi(pci, where, size, val);
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops histb_pci_ops = {
	.read = histb_pcie_rd_own_conf,
	.write = histb_pcie_wr_own_conf,
};

static int histb_pcie_link_up(struct dw_pcie *pci)
{
	struct histb_pcie *hipcie = to_histb_pcie(pci);
	u32 regval;
	u32 status;

	regval = histb_pcie_readl(hipcie, PCIE_SYS_STAT0);
	status = histb_pcie_readl(hipcie, PCIE_SYS_STAT4);
	status &= PCIE_LTSSM_STATE_MASK;
	if ((regval & PCIE_XMLH_LINK_UP) && (regval & PCIE_RDLH_LINK_UP) &&
	    (status == PCIE_LTSSM_STATE_ACTIVE))
		return 1;

	return 0;
}

static int histb_pcie_start_link(struct dw_pcie *pci)
{
	struct histb_pcie *hipcie = to_histb_pcie(pci);
	u32 regval;

	/* assert LTSSM enable */
	regval = histb_pcie_readl(hipcie, PCIE_SYS_CTRL7);
	regval |= PCIE_APP_LTSSM_ENABLE;
	histb_pcie_writel(hipcie, PCIE_SYS_CTRL7, regval);

	return 0;
}

static int histb_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct histb_pcie *hipcie = to_histb_pcie(pci);
	u32 regval;

	pp->bridge->ops = &histb_pci_ops;

	/* PCIe RC work mode */
	regval = histb_pcie_readl(hipcie, PCIE_SYS_CTRL0);
	regval &= ~PCIE_DEVICE_TYPE_MASK;
	regval |= PCIE_WM_RC;
	histb_pcie_writel(hipcie, PCIE_SYS_CTRL0, regval);

	return 0;
}

static const struct dw_pcie_host_ops histb_pcie_host_ops = {
	.host_init = histb_pcie_host_init,
};

static void histb_pcie_host_disable(struct histb_pcie *hipcie)
{
	reset_control_assert(hipcie->soft_reset);
	reset_control_assert(hipcie->sys_reset);
	reset_control_assert(hipcie->bus_reset);

	clk_disable_unprepare(hipcie->aux_clk);
	clk_disable_unprepare(hipcie->pipe_clk);
	clk_disable_unprepare(hipcie->sys_clk);
	clk_disable_unprepare(hipcie->bus_clk);

	if (gpio_is_valid(hipcie->reset_gpio))
		gpio_set_value_cansleep(hipcie->reset_gpio, 0);

	if (hipcie->vpcie)
		regulator_disable(hipcie->vpcie);
}

static int histb_pcie_host_enable(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct histb_pcie *hipcie = to_histb_pcie(pci);
	struct device *dev = pci->dev;
	int ret;

	/* power on PCIe device if have */
	if (hipcie->vpcie) {
		ret = regulator_enable(hipcie->vpcie);
		if (ret) {
			dev_err(dev, "failed to enable regulator: %d\n", ret);
			return ret;
		}
	}

	if (gpio_is_valid(hipcie->reset_gpio))
		gpio_set_value_cansleep(hipcie->reset_gpio, 1);

	ret = clk_prepare_enable(hipcie->bus_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable bus clk\n");
		goto err_bus_clk;
	}

	ret = clk_prepare_enable(hipcie->sys_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable sys clk\n");
		goto err_sys_clk;
	}

	ret = clk_prepare_enable(hipcie->pipe_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable pipe clk\n");
		goto err_pipe_clk;
	}

	ret = clk_prepare_enable(hipcie->aux_clk);
	if (ret) {
		dev_err(dev, "cannot prepare/enable aux clk\n");
		goto err_aux_clk;
	}

	reset_control_assert(hipcie->soft_reset);
	reset_control_deassert(hipcie->soft_reset);

	reset_control_assert(hipcie->sys_reset);
	reset_control_deassert(hipcie->sys_reset);

	reset_control_assert(hipcie->bus_reset);
	reset_control_deassert(hipcie->bus_reset);

	return 0;

err_aux_clk:
	clk_disable_unprepare(hipcie->pipe_clk);
err_pipe_clk:
	clk_disable_unprepare(hipcie->sys_clk);
err_sys_clk:
	clk_disable_unprepare(hipcie->bus_clk);
err_bus_clk:
	if (hipcie->vpcie)
		regulator_disable(hipcie->vpcie);

	return ret;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.read_dbi = histb_pcie_read_dbi,
	.write_dbi = histb_pcie_write_dbi,
	.link_up = histb_pcie_link_up,
	.start_link = histb_pcie_start_link,
};

static int histb_pcie_probe(struct platform_device *pdev)
{
	struct histb_pcie *hipcie;
	struct dw_pcie *pci;
	struct dw_pcie_rp *pp;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	enum of_gpio_flags of_flags;
	unsigned long flag = GPIOF_DIR_OUT;
	int ret;

	hipcie = devm_kzalloc(dev, sizeof(*hipcie), GFP_KERNEL);
	if (!hipcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	hipcie->pci = pci;
	pp = &pci->pp;
	pci->dev = dev;
	pci->ops = &dw_pcie_ops;

	hipcie->ctrl = devm_platform_ioremap_resource_byname(pdev, "control");
	if (IS_ERR(hipcie->ctrl)) {
		dev_err(dev, "cannot get control reg base\n");
		return PTR_ERR(hipcie->ctrl);
	}

	pci->dbi_base = devm_platform_ioremap_resource_byname(pdev, "rc-dbi");
	if (IS_ERR(pci->dbi_base)) {
		dev_err(dev, "cannot get rc-dbi base\n");
		return PTR_ERR(pci->dbi_base);
	}

	hipcie->vpcie = devm_regulator_get_optional(dev, "vpcie");
	if (IS_ERR(hipcie->vpcie)) {
		if (PTR_ERR(hipcie->vpcie) != -ENODEV)
			return PTR_ERR(hipcie->vpcie);
		hipcie->vpcie = NULL;
	}

	hipcie->reset_gpio = of_get_named_gpio_flags(np,
				"reset-gpios", 0, &of_flags);
	if (of_flags & OF_GPIO_ACTIVE_LOW)
		flag |= GPIOF_ACTIVE_LOW;
	if (gpio_is_valid(hipcie->reset_gpio)) {
		ret = devm_gpio_request_one(dev, hipcie->reset_gpio,
				flag, "PCIe device power control");
		if (ret) {
			dev_err(dev, "unable to request gpio\n");
			return ret;
		}
	}

	hipcie->aux_clk = devm_clk_get(dev, "aux");
	if (IS_ERR(hipcie->aux_clk)) {
		dev_err(dev, "Failed to get PCIe aux clk\n");
		return PTR_ERR(hipcie->aux_clk);
	}

	hipcie->pipe_clk = devm_clk_get(dev, "pipe");
	if (IS_ERR(hipcie->pipe_clk)) {
		dev_err(dev, "Failed to get PCIe pipe clk\n");
		return PTR_ERR(hipcie->pipe_clk);
	}

	hipcie->sys_clk = devm_clk_get(dev, "sys");
	if (IS_ERR(hipcie->sys_clk)) {
		dev_err(dev, "Failed to get PCIEe sys clk\n");
		return PTR_ERR(hipcie->sys_clk);
	}

	hipcie->bus_clk = devm_clk_get(dev, "bus");
	if (IS_ERR(hipcie->bus_clk)) {
		dev_err(dev, "Failed to get PCIe bus clk\n");
		return PTR_ERR(hipcie->bus_clk);
	}

	hipcie->soft_reset = devm_reset_control_get(dev, "soft");
	if (IS_ERR(hipcie->soft_reset)) {
		dev_err(dev, "couldn't get soft reset\n");
		return PTR_ERR(hipcie->soft_reset);
	}

	hipcie->sys_reset = devm_reset_control_get(dev, "sys");
	if (IS_ERR(hipcie->sys_reset)) {
		dev_err(dev, "couldn't get sys reset\n");
		return PTR_ERR(hipcie->sys_reset);
	}

	hipcie->bus_reset = devm_reset_control_get(dev, "bus");
	if (IS_ERR(hipcie->bus_reset)) {
		dev_err(dev, "couldn't get bus reset\n");
		return PTR_ERR(hipcie->bus_reset);
	}

	hipcie->phy = devm_phy_get(dev, "phy");
	if (IS_ERR(hipcie->phy)) {
		dev_info(dev, "no pcie-phy found\n");
		hipcie->phy = NULL;
		/* fall through here!
		 * if no pcie-phy found, phy init
		 * should be done under boot!
		 */
	} else {
		phy_init(hipcie->phy);
	}

	pp->ops = &histb_pcie_host_ops;

	platform_set_drvdata(pdev, hipcie);

	ret = histb_pcie_host_enable(pp);
	if (ret) {
		dev_err(dev, "failed to enable host\n");
		return ret;
	}

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int histb_pcie_remove(struct platform_device *pdev)
{
	struct histb_pcie *hipcie = platform_get_drvdata(pdev);

	histb_pcie_host_disable(hipcie);

	if (hipcie->phy)
		phy_exit(hipcie->phy);

	return 0;
}

static const struct of_device_id histb_pcie_of_match[] = {
	{ .compatible = "hisilicon,hi3798cv200-pcie", },
	{},
};
MODULE_DEVICE_TABLE(of, histb_pcie_of_match);

static struct platform_driver histb_pcie_platform_driver = {
	.probe	= histb_pcie_probe,
	.remove	= histb_pcie_remove,
	.driver = {
		.name = "histb-pcie",
		.of_match_table = histb_pcie_of_match,
	},
};
module_platform_driver(histb_pcie_platform_driver);

MODULE_DESCRIPTION("HiSilicon STB PCIe host controller driver");
MODULE_LICENSE("GPL v2");
