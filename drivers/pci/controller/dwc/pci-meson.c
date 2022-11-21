// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Amlogic MESON SoCs
 *
 * Copyright (c) 2018 Amlogic, inc.
 * Author: Yue Wang <yue.wang@amlogic.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/resource.h>
#include <linux/types.h>
#include <linux/phy/phy.h>
#include <linux/module.h>

#include "pcie-designware.h"

#define to_meson_pcie(x) dev_get_drvdata((x)->dev)

#define PCIE_CAP_MAX_PAYLOAD_SIZE(x)	((x) << 5)
#define PCIE_CAP_MAX_READ_REQ_SIZE(x)	((x) << 12)

/* PCIe specific config registers */
#define PCIE_CFG0			0x0
#define APP_LTSSM_ENABLE		BIT(7)

#define PCIE_CFG_STATUS12		0x30
#define IS_SMLH_LINK_UP(x)		((x) & (1 << 6))
#define IS_RDLH_LINK_UP(x)		((x) & (1 << 16))
#define IS_LTSSM_UP(x)			((((x) >> 10) & 0x1f) == 0x11)

#define PCIE_CFG_STATUS17		0x44
#define PM_CURRENT_STATE(x)		(((x) >> 7) & 0x1)

#define WAIT_LINKUP_TIMEOUT		4000
#define PORT_CLK_RATE			100000000UL
#define MAX_PAYLOAD_SIZE		256
#define MAX_READ_REQ_SIZE		256
#define PCIE_RESET_DELAY		500
#define PCIE_SHARED_RESET		1
#define PCIE_NORMAL_RESET		0

enum pcie_data_rate {
	PCIE_GEN1,
	PCIE_GEN2,
	PCIE_GEN3,
	PCIE_GEN4
};

struct meson_pcie_clk_res {
	struct clk *clk;
	struct clk *port_clk;
	struct clk *general_clk;
};

struct meson_pcie_rc_reset {
	struct reset_control *port;
	struct reset_control *apb;
};

struct meson_pcie {
	struct dw_pcie pci;
	void __iomem *cfg_base;
	struct meson_pcie_clk_res clk_res;
	struct meson_pcie_rc_reset mrst;
	struct gpio_desc *reset_gpio;
	struct phy *phy;
};

static struct reset_control *meson_pcie_get_reset(struct meson_pcie *mp,
						  const char *id,
						  u32 reset_type)
{
	struct device *dev = mp->pci.dev;
	struct reset_control *reset;

	if (reset_type == PCIE_SHARED_RESET)
		reset = devm_reset_control_get_shared(dev, id);
	else
		reset = devm_reset_control_get(dev, id);

	return reset;
}

static int meson_pcie_get_resets(struct meson_pcie *mp)
{
	struct meson_pcie_rc_reset *mrst = &mp->mrst;

	mrst->port = meson_pcie_get_reset(mp, "port", PCIE_NORMAL_RESET);
	if (IS_ERR(mrst->port))
		return PTR_ERR(mrst->port);
	reset_control_deassert(mrst->port);

	mrst->apb = meson_pcie_get_reset(mp, "apb", PCIE_SHARED_RESET);
	if (IS_ERR(mrst->apb))
		return PTR_ERR(mrst->apb);
	reset_control_deassert(mrst->apb);

	return 0;
}

static int meson_pcie_get_mems(struct platform_device *pdev,
			       struct meson_pcie *mp)
{
	struct dw_pcie *pci = &mp->pci;

	pci->dbi_base = devm_platform_ioremap_resource_byname(pdev, "elbi");
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	mp->cfg_base = devm_platform_ioremap_resource_byname(pdev, "cfg");
	if (IS_ERR(mp->cfg_base))
		return PTR_ERR(mp->cfg_base);

	return 0;
}

static int meson_pcie_power_on(struct meson_pcie *mp)
{
	int ret = 0;

	ret = phy_init(mp->phy);
	if (ret)
		return ret;

	ret = phy_power_on(mp->phy);
	if (ret) {
		phy_exit(mp->phy);
		return ret;
	}

	return 0;
}

static void meson_pcie_power_off(struct meson_pcie *mp)
{
	phy_power_off(mp->phy);
	phy_exit(mp->phy);
}

static int meson_pcie_reset(struct meson_pcie *mp)
{
	struct meson_pcie_rc_reset *mrst = &mp->mrst;
	int ret = 0;

	ret = phy_reset(mp->phy);
	if (ret)
		return ret;

	reset_control_assert(mrst->port);
	reset_control_assert(mrst->apb);
	udelay(PCIE_RESET_DELAY);
	reset_control_deassert(mrst->port);
	reset_control_deassert(mrst->apb);
	udelay(PCIE_RESET_DELAY);

	return 0;
}

static inline struct clk *meson_pcie_probe_clock(struct device *dev,
						 const char *id, u64 rate)
{
	struct clk *clk;
	int ret;

	clk = devm_clk_get(dev, id);
	if (IS_ERR(clk))
		return clk;

	if (rate) {
		ret = clk_set_rate(clk, rate);
		if (ret) {
			dev_err(dev, "set clk rate failed, ret = %d\n", ret);
			return ERR_PTR(ret);
		}
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(dev, "couldn't enable clk\n");
		return ERR_PTR(ret);
	}

	devm_add_action_or_reset(dev,
				 (void (*) (void *))clk_disable_unprepare,
				 clk);

	return clk;
}

static int meson_pcie_probe_clocks(struct meson_pcie *mp)
{
	struct device *dev = mp->pci.dev;
	struct meson_pcie_clk_res *res = &mp->clk_res;

	res->port_clk = meson_pcie_probe_clock(dev, "port", PORT_CLK_RATE);
	if (IS_ERR(res->port_clk))
		return PTR_ERR(res->port_clk);

	res->general_clk = meson_pcie_probe_clock(dev, "general", 0);
	if (IS_ERR(res->general_clk))
		return PTR_ERR(res->general_clk);

	res->clk = meson_pcie_probe_clock(dev, "pclk", 0);
	if (IS_ERR(res->clk))
		return PTR_ERR(res->clk);

	return 0;
}

static inline u32 meson_cfg_readl(struct meson_pcie *mp, u32 reg)
{
	return readl(mp->cfg_base + reg);
}

static inline void meson_cfg_writel(struct meson_pcie *mp, u32 val, u32 reg)
{
	writel(val, mp->cfg_base + reg);
}

static void meson_pcie_assert_reset(struct meson_pcie *mp)
{
	gpiod_set_value_cansleep(mp->reset_gpio, 1);
	udelay(500);
	gpiod_set_value_cansleep(mp->reset_gpio, 0);
}

static void meson_pcie_ltssm_enable(struct meson_pcie *mp)
{
	u32 val;

	val = meson_cfg_readl(mp, PCIE_CFG0);
	val |= APP_LTSSM_ENABLE;
	meson_cfg_writel(mp, val, PCIE_CFG0);
}

static int meson_size_to_payload(struct meson_pcie *mp, int size)
{
	struct device *dev = mp->pci.dev;

	/*
	 * dwc supports 2^(val+7) payload size, which val is 0~5 default to 1.
	 * So if input size is not 2^order alignment or less than 2^7 or bigger
	 * than 2^12, just set to default size 2^(1+7).
	 */
	if (!is_power_of_2(size) || size < 128 || size > 4096) {
		dev_warn(dev, "payload size %d, set to default 256\n", size);
		return 1;
	}

	return fls(size) - 8;
}

static void meson_set_max_payload(struct meson_pcie *mp, int size)
{
	struct dw_pcie *pci = &mp->pci;
	u32 val;
	u16 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	int max_payload_size = meson_size_to_payload(mp, size);

	val = dw_pcie_readl_dbi(pci, offset + PCI_EXP_DEVCTL);
	val &= ~PCI_EXP_DEVCTL_PAYLOAD;
	dw_pcie_writel_dbi(pci, offset + PCI_EXP_DEVCTL, val);

	val = dw_pcie_readl_dbi(pci, offset + PCI_EXP_DEVCTL);
	val |= PCIE_CAP_MAX_PAYLOAD_SIZE(max_payload_size);
	dw_pcie_writel_dbi(pci, offset + PCI_EXP_DEVCTL, val);
}

static void meson_set_max_rd_req_size(struct meson_pcie *mp, int size)
{
	struct dw_pcie *pci = &mp->pci;
	u32 val;
	u16 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	int max_rd_req_size = meson_size_to_payload(mp, size);

	val = dw_pcie_readl_dbi(pci, offset + PCI_EXP_DEVCTL);
	val &= ~PCI_EXP_DEVCTL_READRQ;
	dw_pcie_writel_dbi(pci, offset + PCI_EXP_DEVCTL, val);

	val = dw_pcie_readl_dbi(pci, offset + PCI_EXP_DEVCTL);
	val |= PCIE_CAP_MAX_READ_REQ_SIZE(max_rd_req_size);
	dw_pcie_writel_dbi(pci, offset + PCI_EXP_DEVCTL, val);
}

static int meson_pcie_start_link(struct dw_pcie *pci)
{
	struct meson_pcie *mp = to_meson_pcie(pci);

	meson_pcie_ltssm_enable(mp);
	meson_pcie_assert_reset(mp);

	return 0;
}

static int meson_pcie_rd_own_conf(struct pci_bus *bus, u32 devfn,
				  int where, int size, u32 *val)
{
	int ret;

	ret = pci_generic_config_read(bus, devfn, where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	/*
	 * There is a bug in the MESON AXG PCIe controller whereby software
	 * cannot program the PCI_CLASS_DEVICE register, so we must fabricate
	 * the return value in the config accessors.
	 */
	if ((where & ~3) == PCI_CLASS_REVISION) {
		if (size <= 2)
			*val = (*val & ((1 << (size * 8)) - 1)) << (8 * (where & 3));
		*val &= ~0xffffff00;
		*val |= PCI_CLASS_BRIDGE_PCI_NORMAL << 8;
		if (size <= 2)
			*val = (*val >> (8 * (where & 3))) & ((1 << (size * 8)) - 1);
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops meson_pci_ops = {
	.map_bus = dw_pcie_own_conf_map_bus,
	.read = meson_pcie_rd_own_conf,
	.write = pci_generic_config_write,
};

static int meson_pcie_link_up(struct dw_pcie *pci)
{
	struct meson_pcie *mp = to_meson_pcie(pci);
	struct device *dev = pci->dev;
	u32 speed_okay = 0;
	u32 cnt = 0;
	u32 state12, state17, smlh_up, ltssm_up, rdlh_up;

	do {
		state12 = meson_cfg_readl(mp, PCIE_CFG_STATUS12);
		state17 = meson_cfg_readl(mp, PCIE_CFG_STATUS17);
		smlh_up = IS_SMLH_LINK_UP(state12);
		rdlh_up = IS_RDLH_LINK_UP(state12);
		ltssm_up = IS_LTSSM_UP(state12);

		if (PM_CURRENT_STATE(state17) < PCIE_GEN3)
			speed_okay = 1;

		if (smlh_up)
			dev_dbg(dev, "smlh_link_up is on\n");
		if (rdlh_up)
			dev_dbg(dev, "rdlh_link_up is on\n");
		if (ltssm_up)
			dev_dbg(dev, "ltssm_up is on\n");
		if (speed_okay)
			dev_dbg(dev, "speed_okay\n");

		if (smlh_up && rdlh_up && ltssm_up && speed_okay)
			return 1;

		cnt++;

		udelay(10);
	} while (cnt < WAIT_LINKUP_TIMEOUT);

	dev_err(dev, "error: wait linkup timeout\n");
	return 0;
}

static int meson_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct meson_pcie *mp = to_meson_pcie(pci);

	pp->bridge->ops = &meson_pci_ops;

	meson_set_max_payload(mp, MAX_PAYLOAD_SIZE);
	meson_set_max_rd_req_size(mp, MAX_READ_REQ_SIZE);

	return 0;
}

static const struct dw_pcie_host_ops meson_pcie_host_ops = {
	.host_init = meson_pcie_host_init,
};

static const struct dw_pcie_ops dw_pcie_ops = {
	.link_up = meson_pcie_link_up,
	.start_link = meson_pcie_start_link,
};

static int meson_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct meson_pcie *mp;
	int ret;

	mp = devm_kzalloc(dev, sizeof(*mp), GFP_KERNEL);
	if (!mp)
		return -ENOMEM;

	pci = &mp->pci;
	pci->dev = dev;
	pci->ops = &dw_pcie_ops;
	pci->pp.ops = &meson_pcie_host_ops;
	pci->num_lanes = 1;

	mp->phy = devm_phy_get(dev, "pcie");
	if (IS_ERR(mp->phy)) {
		dev_err(dev, "get phy failed, %ld\n", PTR_ERR(mp->phy));
		return PTR_ERR(mp->phy);
	}

	mp->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(mp->reset_gpio)) {
		dev_err(dev, "get reset gpio failed\n");
		return PTR_ERR(mp->reset_gpio);
	}

	ret = meson_pcie_get_resets(mp);
	if (ret) {
		dev_err(dev, "get reset resource failed, %d\n", ret);
		return ret;
	}

	ret = meson_pcie_get_mems(pdev, mp);
	if (ret) {
		dev_err(dev, "get memory resource failed, %d\n", ret);
		return ret;
	}

	ret = meson_pcie_power_on(mp);
	if (ret) {
		dev_err(dev, "phy power on failed, %d\n", ret);
		return ret;
	}

	ret = meson_pcie_reset(mp);
	if (ret) {
		dev_err(dev, "reset failed, %d\n", ret);
		goto err_phy;
	}

	ret = meson_pcie_probe_clocks(mp);
	if (ret) {
		dev_err(dev, "init clock resources failed, %d\n", ret);
		goto err_phy;
	}

	platform_set_drvdata(pdev, mp);

	ret = dw_pcie_host_init(&pci->pp);
	if (ret < 0) {
		dev_err(dev, "Add PCIe port failed, %d\n", ret);
		goto err_phy;
	}

	return 0;

err_phy:
	meson_pcie_power_off(mp);
	return ret;
}

static const struct of_device_id meson_pcie_of_match[] = {
	{
		.compatible = "amlogic,axg-pcie",
	},
	{
		.compatible = "amlogic,g12a-pcie",
	},
	{},
};
MODULE_DEVICE_TABLE(of, meson_pcie_of_match);

static struct platform_driver meson_pcie_driver = {
	.probe = meson_pcie_probe,
	.driver = {
		.name = "meson-pcie",
		.of_match_table = meson_pcie_of_match,
	},
};

module_platform_driver(meson_pcie_driver);

MODULE_AUTHOR("Yue Wang <yue.wang@amlogic.com>");
MODULE_DESCRIPTION("Amlogic PCIe Controller driver");
MODULE_LICENSE("GPL v2");
