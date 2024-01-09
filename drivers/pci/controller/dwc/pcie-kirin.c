// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Kirin Phone SoCs
 *
 * Copyright (C) 2017 HiSilicon Electronics Co., Ltd.
 *		https://www.huawei.com
 *
 * Author: Xiaowei Song <songxiaowei@huawei.com>
 */

#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/phy/phy.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/resource.h>
#include <linux/types.h>
#include "pcie-designware.h"

#define to_kirin_pcie(x) dev_get_drvdata((x)->dev)

/* PCIe ELBI registers */
#define SOC_PCIECTRL_CTRL0_ADDR		0x000
#define SOC_PCIECTRL_CTRL1_ADDR		0x004
#define PCIE_ELBI_SLV_DBI_ENABLE	(0x1 << 21)

/* info located in APB */
#define PCIE_APP_LTSSM_ENABLE	0x01c
#define PCIE_APB_PHY_STATUS0	0x400
#define PCIE_LINKUP_ENABLE	(0x8020)
#define PCIE_LTSSM_ENABLE_BIT	(0x1 << 11)

/* info located in sysctrl */
#define SCTRL_PCIE_CMOS_OFFSET	0x60
#define SCTRL_PCIE_CMOS_BIT	0x10
#define SCTRL_PCIE_ISO_OFFSET	0x44
#define SCTRL_PCIE_ISO_BIT	0x30
#define SCTRL_PCIE_HPCLK_OFFSET	0x190
#define SCTRL_PCIE_HPCLK_BIT	0x184000
#define SCTRL_PCIE_OE_OFFSET	0x14a
#define PCIE_DEBOUNCE_PARAM	0xF0F400
#define PCIE_OE_BYPASS		(0x3 << 28)

/*
 * Max number of connected PCI slots at an external PCI bridge
 *
 * This is used on HiKey 970, which has a PEX 8606 bridge with 4 connected
 * lanes (lane 0 upstream, and the other three lanes, one connected to an
 * in-board Ethernet adapter and the other two connected to M.2 and mini
 * PCI slots.
 *
 * Each slot has a different clock source and uses a separate PERST# pin.
 */
#define MAX_PCI_SLOTS		3

enum pcie_kirin_phy_type {
	PCIE_KIRIN_INTERNAL_PHY,
	PCIE_KIRIN_EXTERNAL_PHY
};

struct kirin_pcie {
	enum pcie_kirin_phy_type	type;

	struct dw_pcie	*pci;
	struct regmap   *apb;
	struct phy	*phy;
	void		*phy_priv;	/* only for PCIE_KIRIN_INTERNAL_PHY */

	/* DWC PERST# */
	int		gpio_id_dwc_perst;

	/* Per-slot PERST# */
	int		num_slots;
	int		gpio_id_reset[MAX_PCI_SLOTS];
	const char	*reset_names[MAX_PCI_SLOTS];

	/* Per-slot clkreq */
	int		n_gpio_clkreq;
	int		gpio_id_clkreq[MAX_PCI_SLOTS];
	const char	*clkreq_names[MAX_PCI_SLOTS];
};

/*
 * Kirin 960 PHY. Can't be split into a PHY driver without changing the
 * DT schema.
 */

#define REF_CLK_FREQ			100000000

/* PHY info located in APB */
#define PCIE_APB_PHY_CTRL0	0x0
#define PCIE_APB_PHY_CTRL1	0x4
#define PCIE_APB_PHY_STATUS0   0x400
#define PIPE_CLK_STABLE		BIT(19)
#define PHY_REF_PAD_BIT		BIT(8)
#define PHY_PWR_DOWN_BIT	BIT(22)
#define PHY_RST_ACK_BIT		BIT(16)

/* peri_crg ctrl */
#define CRGCTRL_PCIE_ASSERT_OFFSET	0x88
#define CRGCTRL_PCIE_ASSERT_BIT		0x8c000000

/* Time for delay */
#define REF_2_PERST_MIN		21000
#define REF_2_PERST_MAX		25000
#define PERST_2_ACCESS_MIN	10000
#define PERST_2_ACCESS_MAX	12000
#define PIPE_CLK_WAIT_MIN	550
#define PIPE_CLK_WAIT_MAX	600
#define TIME_CMOS_MIN		100
#define TIME_CMOS_MAX		105
#define TIME_PHY_PD_MIN		10
#define TIME_PHY_PD_MAX		11

struct hi3660_pcie_phy {
	struct device	*dev;
	void __iomem	*base;
	struct regmap	*crgctrl;
	struct regmap	*sysctrl;
	struct clk	*apb_sys_clk;
	struct clk	*apb_phy_clk;
	struct clk	*phy_ref_clk;
	struct clk	*aclk;
	struct clk	*aux_clk;
};

/* Registers in PCIePHY */
static inline void kirin_apb_phy_writel(struct hi3660_pcie_phy *hi3660_pcie_phy,
					u32 val, u32 reg)
{
	writel(val, hi3660_pcie_phy->base + reg);
}

static inline u32 kirin_apb_phy_readl(struct hi3660_pcie_phy *hi3660_pcie_phy,
				      u32 reg)
{
	return readl(hi3660_pcie_phy->base + reg);
}

static int hi3660_pcie_phy_get_clk(struct hi3660_pcie_phy *phy)
{
	struct device *dev = phy->dev;

	phy->phy_ref_clk = devm_clk_get(dev, "pcie_phy_ref");
	if (IS_ERR(phy->phy_ref_clk))
		return PTR_ERR(phy->phy_ref_clk);

	phy->aux_clk = devm_clk_get(dev, "pcie_aux");
	if (IS_ERR(phy->aux_clk))
		return PTR_ERR(phy->aux_clk);

	phy->apb_phy_clk = devm_clk_get(dev, "pcie_apb_phy");
	if (IS_ERR(phy->apb_phy_clk))
		return PTR_ERR(phy->apb_phy_clk);

	phy->apb_sys_clk = devm_clk_get(dev, "pcie_apb_sys");
	if (IS_ERR(phy->apb_sys_clk))
		return PTR_ERR(phy->apb_sys_clk);

	phy->aclk = devm_clk_get(dev, "pcie_aclk");
	if (IS_ERR(phy->aclk))
		return PTR_ERR(phy->aclk);

	return 0;
}

static int hi3660_pcie_phy_get_resource(struct hi3660_pcie_phy *phy)
{
	struct device *dev = phy->dev;
	struct platform_device *pdev;

	/* registers */
	pdev = container_of(dev, struct platform_device, dev);

	phy->base = devm_platform_ioremap_resource_byname(pdev, "phy");
	if (IS_ERR(phy->base))
		return PTR_ERR(phy->base);

	phy->crgctrl = syscon_regmap_lookup_by_compatible("hisilicon,hi3660-crgctrl");
	if (IS_ERR(phy->crgctrl))
		return PTR_ERR(phy->crgctrl);

	phy->sysctrl = syscon_regmap_lookup_by_compatible("hisilicon,hi3660-sctrl");
	if (IS_ERR(phy->sysctrl))
		return PTR_ERR(phy->sysctrl);

	return 0;
}

static int hi3660_pcie_phy_start(struct hi3660_pcie_phy *phy)
{
	struct device *dev = phy->dev;
	u32 reg_val;

	reg_val = kirin_apb_phy_readl(phy, PCIE_APB_PHY_CTRL1);
	reg_val &= ~PHY_REF_PAD_BIT;
	kirin_apb_phy_writel(phy, reg_val, PCIE_APB_PHY_CTRL1);

	reg_val = kirin_apb_phy_readl(phy, PCIE_APB_PHY_CTRL0);
	reg_val &= ~PHY_PWR_DOWN_BIT;
	kirin_apb_phy_writel(phy, reg_val, PCIE_APB_PHY_CTRL0);
	usleep_range(TIME_PHY_PD_MIN, TIME_PHY_PD_MAX);

	reg_val = kirin_apb_phy_readl(phy, PCIE_APB_PHY_CTRL1);
	reg_val &= ~PHY_RST_ACK_BIT;
	kirin_apb_phy_writel(phy, reg_val, PCIE_APB_PHY_CTRL1);

	usleep_range(PIPE_CLK_WAIT_MIN, PIPE_CLK_WAIT_MAX);
	reg_val = kirin_apb_phy_readl(phy, PCIE_APB_PHY_STATUS0);
	if (reg_val & PIPE_CLK_STABLE) {
		dev_err(dev, "PIPE clk is not stable\n");
		return -EINVAL;
	}

	return 0;
}

static void hi3660_pcie_phy_oe_enable(struct hi3660_pcie_phy *phy)
{
	u32 val;

	regmap_read(phy->sysctrl, SCTRL_PCIE_OE_OFFSET, &val);
	val |= PCIE_DEBOUNCE_PARAM;
	val &= ~PCIE_OE_BYPASS;
	regmap_write(phy->sysctrl, SCTRL_PCIE_OE_OFFSET, val);
}

static int hi3660_pcie_phy_clk_ctrl(struct hi3660_pcie_phy *phy, bool enable)
{
	int ret = 0;

	if (!enable)
		goto close_clk;

	ret = clk_set_rate(phy->phy_ref_clk, REF_CLK_FREQ);
	if (ret)
		return ret;

	ret = clk_prepare_enable(phy->phy_ref_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(phy->apb_sys_clk);
	if (ret)
		goto apb_sys_fail;

	ret = clk_prepare_enable(phy->apb_phy_clk);
	if (ret)
		goto apb_phy_fail;

	ret = clk_prepare_enable(phy->aclk);
	if (ret)
		goto aclk_fail;

	ret = clk_prepare_enable(phy->aux_clk);
	if (ret)
		goto aux_clk_fail;

	return 0;

close_clk:
	clk_disable_unprepare(phy->aux_clk);
aux_clk_fail:
	clk_disable_unprepare(phy->aclk);
aclk_fail:
	clk_disable_unprepare(phy->apb_phy_clk);
apb_phy_fail:
	clk_disable_unprepare(phy->apb_sys_clk);
apb_sys_fail:
	clk_disable_unprepare(phy->phy_ref_clk);

	return ret;
}

static int hi3660_pcie_phy_power_on(struct kirin_pcie *pcie)
{
	struct hi3660_pcie_phy *phy = pcie->phy_priv;
	int ret;

	/* Power supply for Host */
	regmap_write(phy->sysctrl,
		     SCTRL_PCIE_CMOS_OFFSET, SCTRL_PCIE_CMOS_BIT);
	usleep_range(TIME_CMOS_MIN, TIME_CMOS_MAX);

	hi3660_pcie_phy_oe_enable(phy);

	ret = hi3660_pcie_phy_clk_ctrl(phy, true);
	if (ret)
		return ret;

	/* ISO disable, PCIeCtrl, PHY assert and clk gate clear */
	regmap_write(phy->sysctrl,
		     SCTRL_PCIE_ISO_OFFSET, SCTRL_PCIE_ISO_BIT);
	regmap_write(phy->crgctrl,
		     CRGCTRL_PCIE_ASSERT_OFFSET, CRGCTRL_PCIE_ASSERT_BIT);
	regmap_write(phy->sysctrl,
		     SCTRL_PCIE_HPCLK_OFFSET, SCTRL_PCIE_HPCLK_BIT);

	ret = hi3660_pcie_phy_start(phy);
	if (ret)
		goto disable_clks;

	return 0;

disable_clks:
	hi3660_pcie_phy_clk_ctrl(phy, false);
	return ret;
}

static int hi3660_pcie_phy_init(struct platform_device *pdev,
				struct kirin_pcie *pcie)
{
	struct device *dev = &pdev->dev;
	struct hi3660_pcie_phy *phy;
	int ret;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	pcie->phy_priv = phy;
	phy->dev = dev;

	ret = hi3660_pcie_phy_get_clk(phy);
	if (ret)
		return ret;

	return hi3660_pcie_phy_get_resource(phy);
}

static int hi3660_pcie_phy_power_off(struct kirin_pcie *pcie)
{
	struct hi3660_pcie_phy *phy = pcie->phy_priv;

	/* Drop power supply for Host */
	regmap_write(phy->sysctrl, SCTRL_PCIE_CMOS_OFFSET, 0x00);

	hi3660_pcie_phy_clk_ctrl(phy, false);

	return 0;
}

/*
 * The non-PHY part starts here
 */

static const struct regmap_config pcie_kirin_regmap_conf = {
	.name = "kirin_pcie_apb",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int kirin_pcie_get_gpio_enable(struct kirin_pcie *pcie,
				      struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	char name[32];
	int ret, i;

	/* This is an optional property */
	ret = gpiod_count(dev, "hisilicon,clken");
	if (ret < 0)
		return 0;

	if (ret > MAX_PCI_SLOTS) {
		dev_err(dev, "Too many GPIO clock requests!\n");
		return -EINVAL;
	}

	pcie->n_gpio_clkreq = ret;

	for (i = 0; i < pcie->n_gpio_clkreq; i++) {
		pcie->gpio_id_clkreq[i] = of_get_named_gpio(dev->of_node,
						    "hisilicon,clken-gpios", i);
		if (pcie->gpio_id_clkreq[i] < 0)
			return pcie->gpio_id_clkreq[i];

		sprintf(name, "pcie_clkreq_%d", i);
		pcie->clkreq_names[i] = devm_kstrdup_const(dev, name,
							    GFP_KERNEL);
		if (!pcie->clkreq_names[i])
			return -ENOMEM;
	}

	return 0;
}

static int kirin_pcie_parse_port(struct kirin_pcie *pcie,
				 struct platform_device *pdev,
				 struct device_node *node)
{
	struct device *dev = &pdev->dev;
	struct device_node *parent, *child;
	int ret, slot, i;
	char name[32];

	for_each_available_child_of_node(node, parent) {
		for_each_available_child_of_node(parent, child) {
			i = pcie->num_slots;

			pcie->gpio_id_reset[i] = of_get_named_gpio(child,
							"reset-gpios", 0);
			if (pcie->gpio_id_reset[i] < 0)
				continue;

			pcie->num_slots++;
			if (pcie->num_slots > MAX_PCI_SLOTS) {
				dev_err(dev, "Too many PCI slots!\n");
				ret = -EINVAL;
				goto put_node;
			}

			ret = of_pci_get_devfn(child);
			if (ret < 0) {
				dev_err(dev, "failed to parse devfn: %d\n", ret);
				goto put_node;
			}

			slot = PCI_SLOT(ret);

			sprintf(name, "pcie_perst_%d", slot);
			pcie->reset_names[i] = devm_kstrdup_const(dev, name,
								GFP_KERNEL);
			if (!pcie->reset_names[i]) {
				ret = -ENOMEM;
				goto put_node;
			}
		}
	}

	return 0;

put_node:
	of_node_put(child);
	of_node_put(parent);
	return ret;
}

static long kirin_pcie_get_resource(struct kirin_pcie *kirin_pcie,
				    struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *child, *node = dev->of_node;
	void __iomem *apb_base;
	int ret;

	apb_base = devm_platform_ioremap_resource_byname(pdev, "apb");
	if (IS_ERR(apb_base))
		return PTR_ERR(apb_base);

	kirin_pcie->apb = devm_regmap_init_mmio(dev, apb_base,
						&pcie_kirin_regmap_conf);
	if (IS_ERR(kirin_pcie->apb))
		return PTR_ERR(kirin_pcie->apb);

	/* pcie internal PERST# gpio */
	kirin_pcie->gpio_id_dwc_perst = of_get_named_gpio(dev->of_node,
							  "reset-gpios", 0);
	if (kirin_pcie->gpio_id_dwc_perst == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (!gpio_is_valid(kirin_pcie->gpio_id_dwc_perst)) {
		dev_err(dev, "unable to get a valid gpio pin\n");
		return -ENODEV;
	}

	ret = kirin_pcie_get_gpio_enable(kirin_pcie, pdev);
	if (ret)
		return ret;

	/* Parse OF children */
	for_each_available_child_of_node(node, child) {
		ret = kirin_pcie_parse_port(kirin_pcie, pdev, child);
		if (ret)
			goto put_node;
	}

	return 0;

put_node:
	of_node_put(child);
	return ret;
}

static void kirin_pcie_sideband_dbi_w_mode(struct kirin_pcie *kirin_pcie,
					   bool on)
{
	u32 val;

	regmap_read(kirin_pcie->apb, SOC_PCIECTRL_CTRL0_ADDR, &val);
	if (on)
		val = val | PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val = val & ~PCIE_ELBI_SLV_DBI_ENABLE;

	regmap_write(kirin_pcie->apb, SOC_PCIECTRL_CTRL0_ADDR, val);
}

static void kirin_pcie_sideband_dbi_r_mode(struct kirin_pcie *kirin_pcie,
					   bool on)
{
	u32 val;

	regmap_read(kirin_pcie->apb, SOC_PCIECTRL_CTRL1_ADDR, &val);
	if (on)
		val = val | PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val = val & ~PCIE_ELBI_SLV_DBI_ENABLE;

	regmap_write(kirin_pcie->apb, SOC_PCIECTRL_CTRL1_ADDR, val);
}

static int kirin_pcie_rd_own_conf(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 *val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);

	if (PCI_SLOT(devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*val = dw_pcie_read_dbi(pci, where, size);
	return PCIBIOS_SUCCESSFUL;
}

static int kirin_pcie_wr_own_conf(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);

	if (PCI_SLOT(devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	dw_pcie_write_dbi(pci, where, size, val);
	return PCIBIOS_SUCCESSFUL;
}

static int kirin_pcie_add_bus(struct pci_bus *bus)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);
	struct kirin_pcie *kirin_pcie = to_kirin_pcie(pci);
	int i, ret;

	if (!kirin_pcie->num_slots)
		return 0;

	/* Send PERST# to each slot */
	for (i = 0; i < kirin_pcie->num_slots; i++) {
		ret = gpio_direction_output(kirin_pcie->gpio_id_reset[i], 1);
		if (ret) {
			dev_err(pci->dev, "PERST# %s error: %d\n",
				kirin_pcie->reset_names[i], ret);
		}
	}
	usleep_range(PERST_2_ACCESS_MIN, PERST_2_ACCESS_MAX);

	return 0;
}

static struct pci_ops kirin_pci_ops = {
	.read = kirin_pcie_rd_own_conf,
	.write = kirin_pcie_wr_own_conf,
	.add_bus = kirin_pcie_add_bus,
};

static u32 kirin_pcie_read_dbi(struct dw_pcie *pci, void __iomem *base,
			       u32 reg, size_t size)
{
	struct kirin_pcie *kirin_pcie = to_kirin_pcie(pci);
	u32 ret;

	kirin_pcie_sideband_dbi_r_mode(kirin_pcie, true);
	dw_pcie_read(base + reg, size, &ret);
	kirin_pcie_sideband_dbi_r_mode(kirin_pcie, false);

	return ret;
}

static void kirin_pcie_write_dbi(struct dw_pcie *pci, void __iomem *base,
				 u32 reg, size_t size, u32 val)
{
	struct kirin_pcie *kirin_pcie = to_kirin_pcie(pci);

	kirin_pcie_sideband_dbi_w_mode(kirin_pcie, true);
	dw_pcie_write(base + reg, size, val);
	kirin_pcie_sideband_dbi_w_mode(kirin_pcie, false);
}

static int kirin_pcie_link_up(struct dw_pcie *pci)
{
	struct kirin_pcie *kirin_pcie = to_kirin_pcie(pci);
	u32 val;

	regmap_read(kirin_pcie->apb, PCIE_APB_PHY_STATUS0, &val);
	if ((val & PCIE_LINKUP_ENABLE) == PCIE_LINKUP_ENABLE)
		return 1;

	return 0;
}

static int kirin_pcie_start_link(struct dw_pcie *pci)
{
	struct kirin_pcie *kirin_pcie = to_kirin_pcie(pci);

	/* assert LTSSM enable */
	regmap_write(kirin_pcie->apb, PCIE_APP_LTSSM_ENABLE,
		     PCIE_LTSSM_ENABLE_BIT);

	return 0;
}

static int kirin_pcie_host_init(struct dw_pcie_rp *pp)
{
	pp->bridge->ops = &kirin_pci_ops;

	return 0;
}

static int kirin_pcie_gpio_request(struct kirin_pcie *kirin_pcie,
				   struct device *dev)
{
	int ret, i;

	for (i = 0; i < kirin_pcie->num_slots; i++) {
		if (!gpio_is_valid(kirin_pcie->gpio_id_reset[i])) {
			dev_err(dev, "unable to get a valid %s gpio\n",
				kirin_pcie->reset_names[i]);
			return -ENODEV;
		}

		ret = devm_gpio_request(dev, kirin_pcie->gpio_id_reset[i],
					kirin_pcie->reset_names[i]);
		if (ret)
			return ret;
	}

	for (i = 0; i < kirin_pcie->n_gpio_clkreq; i++) {
		if (!gpio_is_valid(kirin_pcie->gpio_id_clkreq[i])) {
			dev_err(dev, "unable to get a valid %s gpio\n",
				kirin_pcie->clkreq_names[i]);
			return -ENODEV;
		}

		ret = devm_gpio_request(dev, kirin_pcie->gpio_id_clkreq[i],
					kirin_pcie->clkreq_names[i]);
		if (ret)
			return ret;

		ret = gpio_direction_output(kirin_pcie->gpio_id_clkreq[i], 0);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct dw_pcie_ops kirin_dw_pcie_ops = {
	.read_dbi = kirin_pcie_read_dbi,
	.write_dbi = kirin_pcie_write_dbi,
	.link_up = kirin_pcie_link_up,
	.start_link = kirin_pcie_start_link,
};

static const struct dw_pcie_host_ops kirin_pcie_host_ops = {
	.host_init = kirin_pcie_host_init,
};

static int kirin_pcie_power_off(struct kirin_pcie *kirin_pcie)
{
	int i;

	if (kirin_pcie->type == PCIE_KIRIN_INTERNAL_PHY)
		return hi3660_pcie_phy_power_off(kirin_pcie);

	for (i = 0; i < kirin_pcie->n_gpio_clkreq; i++)
		gpio_direction_output(kirin_pcie->gpio_id_clkreq[i], 1);

	phy_power_off(kirin_pcie->phy);
	phy_exit(kirin_pcie->phy);

	return 0;
}

static int kirin_pcie_power_on(struct platform_device *pdev,
			       struct kirin_pcie *kirin_pcie)
{
	struct device *dev = &pdev->dev;
	int ret;

	if (kirin_pcie->type == PCIE_KIRIN_INTERNAL_PHY) {
		ret = hi3660_pcie_phy_init(pdev, kirin_pcie);
		if (ret)
			return ret;

		ret = hi3660_pcie_phy_power_on(kirin_pcie);
		if (ret)
			return ret;
	} else {
		kirin_pcie->phy = devm_of_phy_get(dev, dev->of_node, NULL);
		if (IS_ERR(kirin_pcie->phy))
			return PTR_ERR(kirin_pcie->phy);

		ret = kirin_pcie_gpio_request(kirin_pcie, dev);
		if (ret)
			return ret;

		ret = phy_init(kirin_pcie->phy);
		if (ret)
			goto err;

		ret = phy_power_on(kirin_pcie->phy);
		if (ret)
			goto err;
	}

	/* perst assert Endpoint */
	usleep_range(REF_2_PERST_MIN, REF_2_PERST_MAX);

	if (!gpio_request(kirin_pcie->gpio_id_dwc_perst, "pcie_perst_bridge")) {
		ret = gpio_direction_output(kirin_pcie->gpio_id_dwc_perst, 1);
		if (ret)
			goto err;
	}

	usleep_range(PERST_2_ACCESS_MIN, PERST_2_ACCESS_MAX);

	return 0;
err:
	kirin_pcie_power_off(kirin_pcie);

	return ret;
}

static int kirin_pcie_remove(struct platform_device *pdev)
{
	struct kirin_pcie *kirin_pcie = platform_get_drvdata(pdev);

	dw_pcie_host_deinit(&kirin_pcie->pci->pp);

	kirin_pcie_power_off(kirin_pcie);

	return 0;
}

struct kirin_pcie_data {
	enum pcie_kirin_phy_type	phy_type;
};

static const struct kirin_pcie_data kirin_960_data = {
	.phy_type = PCIE_KIRIN_INTERNAL_PHY,
};

static const struct kirin_pcie_data kirin_970_data = {
	.phy_type = PCIE_KIRIN_EXTERNAL_PHY,
};

static const struct of_device_id kirin_pcie_match[] = {
	{ .compatible = "hisilicon,kirin960-pcie", .data = &kirin_960_data },
	{ .compatible = "hisilicon,kirin970-pcie", .data = &kirin_970_data },
	{},
};

static int kirin_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct kirin_pcie_data *data;
	struct kirin_pcie *kirin_pcie;
	struct dw_pcie *pci;
	int ret;

	if (!dev->of_node) {
		dev_err(dev, "NULL node\n");
		return -EINVAL;
	}

	data = of_device_get_match_data(dev);
	if (!data) {
		dev_err(dev, "OF data missing\n");
		return -EINVAL;
	}

	kirin_pcie = devm_kzalloc(dev, sizeof(struct kirin_pcie), GFP_KERNEL);
	if (!kirin_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &kirin_dw_pcie_ops;
	pci->pp.ops = &kirin_pcie_host_ops;
	kirin_pcie->pci = pci;
	kirin_pcie->type = data->phy_type;

	ret = kirin_pcie_get_resource(kirin_pcie, pdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, kirin_pcie);

	ret = kirin_pcie_power_on(pdev, kirin_pcie);
	if (ret)
		return ret;

	return dw_pcie_host_init(&pci->pp);
}

static struct platform_driver kirin_pcie_driver = {
	.probe			= kirin_pcie_probe,
	.remove	        	= kirin_pcie_remove,
	.driver			= {
		.name			= "kirin-pcie",
		.of_match_table		= kirin_pcie_match,
		.suppress_bind_attrs	= true,
	},
};
module_platform_driver(kirin_pcie_driver);

MODULE_DEVICE_TABLE(of, kirin_pcie_match);
MODULE_DESCRIPTION("PCIe host controller driver for Kirin Phone SoCs");
MODULE_AUTHOR("Xiaowei Song <songxiaowei@huawei.com>");
MODULE_LICENSE("GPL v2");
