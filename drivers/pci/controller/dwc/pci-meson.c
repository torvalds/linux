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

#include "pcie-designware.h"

#define to_meson_pcie(x) dev_get_drvdata((x)->dev)

/* External local bus interface registers */
#define PLR_OFFSET			0x700
#define PCIE_PORT_LINK_CTRL_OFF		(PLR_OFFSET + 0x10)
#define FAST_LINK_MODE			BIT(7)
#define LINK_CAPABLE_MASK		GENMASK(21, 16)
#define LINK_CAPABLE_X1			BIT(16)

#define PCIE_GEN2_CTRL_OFF		(PLR_OFFSET + 0x10c)
#define NUM_OF_LANES_MASK		GENMASK(12, 8)
#define NUM_OF_LANES_X1			BIT(8)
#define DIRECT_SPEED_CHANGE		BIT(17)

#define TYPE1_HDR_OFFSET		0x0
#define PCIE_STATUS_COMMAND		(TYPE1_HDR_OFFSET + 0x04)
#define PCI_IO_EN			BIT(0)
#define PCI_MEM_SPACE_EN		BIT(1)
#define PCI_BUS_MASTER_EN		BIT(2)

#define PCIE_BASE_ADDR0			(TYPE1_HDR_OFFSET + 0x10)
#define PCIE_BASE_ADDR1			(TYPE1_HDR_OFFSET + 0x14)

#define PCIE_CAP_OFFSET			0x70
#define PCIE_DEV_CTRL_DEV_STUS		(PCIE_CAP_OFFSET + 0x08)
#define PCIE_CAP_MAX_PAYLOAD_MASK	GENMASK(7, 5)
#define PCIE_CAP_MAX_PAYLOAD_SIZE(x)	((x) << 5)
#define PCIE_CAP_MAX_READ_REQ_MASK	GENMASK(14, 12)
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
#define MESON_PCIE_PHY_POWERUP		0x1c
#define PCIE_RESET_DELAY		500
#define PCIE_SHARED_RESET		1
#define PCIE_NORMAL_RESET		0

enum pcie_data_rate {
	PCIE_GEN1,
	PCIE_GEN2,
	PCIE_GEN3,
	PCIE_GEN4
};

struct meson_pcie_mem_res {
	void __iomem *elbi_base;
	void __iomem *cfg_base;
	void __iomem *phy_base;
};

struct meson_pcie_clk_res {
	struct clk *clk;
	struct clk *mipi_gate;
	struct clk *port_clk;
	struct clk *general_clk;
};

struct meson_pcie_rc_reset {
	struct reset_control *phy;
	struct reset_control *port;
	struct reset_control *apb;
};

struct meson_pcie {
	struct dw_pcie pci;
	struct meson_pcie_mem_res mem_res;
	struct meson_pcie_clk_res clk_res;
	struct meson_pcie_rc_reset mrst;
	struct gpio_desc *reset_gpio;
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

	mrst->phy = meson_pcie_get_reset(mp, "phy", PCIE_SHARED_RESET);
	if (IS_ERR(mrst->phy))
		return PTR_ERR(mrst->phy);
	reset_control_deassert(mrst->phy);

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

static void __iomem *meson_pcie_get_mem(struct platform_device *pdev,
					struct meson_pcie *mp,
					const char *id)
{
	struct device *dev = mp->pci.dev;
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, id);

	return devm_ioremap_resource(dev, res);
}

static void __iomem *meson_pcie_get_mem_shared(struct platform_device *pdev,
					       struct meson_pcie *mp,
					       const char *id)
{
	struct device *dev = mp->pci.dev;
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, id);
	if (!res) {
		dev_err(dev, "No REG resource %s\n", id);
		return ERR_PTR(-ENXIO);
	}

	return devm_ioremap(dev, res->start, resource_size(res));
}

static int meson_pcie_get_mems(struct platform_device *pdev,
			       struct meson_pcie *mp)
{
	mp->mem_res.elbi_base = meson_pcie_get_mem(pdev, mp, "elbi");
	if (IS_ERR(mp->mem_res.elbi_base))
		return PTR_ERR(mp->mem_res.elbi_base);

	mp->mem_res.cfg_base = meson_pcie_get_mem(pdev, mp, "cfg");
	if (IS_ERR(mp->mem_res.cfg_base))
		return PTR_ERR(mp->mem_res.cfg_base);

	/* Meson SoC has two PCI controllers use same phy register*/
	mp->mem_res.phy_base = meson_pcie_get_mem_shared(pdev, mp, "phy");
	if (IS_ERR(mp->mem_res.phy_base))
		return PTR_ERR(mp->mem_res.phy_base);

	return 0;
}

static void meson_pcie_power_on(struct meson_pcie *mp)
{
	writel(MESON_PCIE_PHY_POWERUP, mp->mem_res.phy_base);
}

static void meson_pcie_reset(struct meson_pcie *mp)
{
	struct meson_pcie_rc_reset *mrst = &mp->mrst;

	reset_control_assert(mrst->phy);
	udelay(PCIE_RESET_DELAY);
	reset_control_deassert(mrst->phy);
	udelay(PCIE_RESET_DELAY);

	reset_control_assert(mrst->port);
	reset_control_assert(mrst->apb);
	udelay(PCIE_RESET_DELAY);
	reset_control_deassert(mrst->port);
	reset_control_deassert(mrst->apb);
	udelay(PCIE_RESET_DELAY);
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

	res->mipi_gate = meson_pcie_probe_clock(dev, "pcie_mipi_en", 0);
	if (IS_ERR(res->mipi_gate))
		return PTR_ERR(res->mipi_gate);

	res->general_clk = meson_pcie_probe_clock(dev, "pcie_general", 0);
	if (IS_ERR(res->general_clk))
		return PTR_ERR(res->general_clk);

	res->clk = meson_pcie_probe_clock(dev, "pcie", 0);
	if (IS_ERR(res->clk))
		return PTR_ERR(res->clk);

	return 0;
}

static inline void meson_elb_writel(struct meson_pcie *mp, u32 val, u32 reg)
{
	writel(val, mp->mem_res.elbi_base + reg);
}

static inline u32 meson_elb_readl(struct meson_pcie *mp, u32 reg)
{
	return readl(mp->mem_res.elbi_base + reg);
}

static inline u32 meson_cfg_readl(struct meson_pcie *mp, u32 reg)
{
	return readl(mp->mem_res.cfg_base + reg);
}

static inline void meson_cfg_writel(struct meson_pcie *mp, u32 val, u32 reg)
{
	writel(val, mp->mem_res.cfg_base + reg);
}

static void meson_pcie_assert_reset(struct meson_pcie *mp)
{
	gpiod_set_value_cansleep(mp->reset_gpio, 0);
	udelay(500);
	gpiod_set_value_cansleep(mp->reset_gpio, 1);
}

static void meson_pcie_init_dw(struct meson_pcie *mp)
{
	u32 val;

	val = meson_cfg_readl(mp, PCIE_CFG0);
	val |= APP_LTSSM_ENABLE;
	meson_cfg_writel(mp, val, PCIE_CFG0);

	val = meson_elb_readl(mp, PCIE_PORT_LINK_CTRL_OFF);
	val &= ~LINK_CAPABLE_MASK;
	meson_elb_writel(mp, val, PCIE_PORT_LINK_CTRL_OFF);

	val = meson_elb_readl(mp, PCIE_PORT_LINK_CTRL_OFF);
	val |= LINK_CAPABLE_X1 | FAST_LINK_MODE;
	meson_elb_writel(mp, val, PCIE_PORT_LINK_CTRL_OFF);

	val = meson_elb_readl(mp, PCIE_GEN2_CTRL_OFF);
	val &= ~NUM_OF_LANES_MASK;
	meson_elb_writel(mp, val, PCIE_GEN2_CTRL_OFF);

	val = meson_elb_readl(mp, PCIE_GEN2_CTRL_OFF);
	val |= NUM_OF_LANES_X1 | DIRECT_SPEED_CHANGE;
	meson_elb_writel(mp, val, PCIE_GEN2_CTRL_OFF);

	meson_elb_writel(mp, 0x0, PCIE_BASE_ADDR0);
	meson_elb_writel(mp, 0x0, PCIE_BASE_ADDR1);
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
	u32 val;
	int max_payload_size = meson_size_to_payload(mp, size);

	val = meson_elb_readl(mp, PCIE_DEV_CTRL_DEV_STUS);
	val &= ~PCIE_CAP_MAX_PAYLOAD_MASK;
	meson_elb_writel(mp, val, PCIE_DEV_CTRL_DEV_STUS);

	val = meson_elb_readl(mp, PCIE_DEV_CTRL_DEV_STUS);
	val |= PCIE_CAP_MAX_PAYLOAD_SIZE(max_payload_size);
	meson_elb_writel(mp, val, PCIE_DEV_CTRL_DEV_STUS);
}

static void meson_set_max_rd_req_size(struct meson_pcie *mp, int size)
{
	u32 val;
	int max_rd_req_size = meson_size_to_payload(mp, size);

	val = meson_elb_readl(mp, PCIE_DEV_CTRL_DEV_STUS);
	val &= ~PCIE_CAP_MAX_READ_REQ_MASK;
	meson_elb_writel(mp, val, PCIE_DEV_CTRL_DEV_STUS);

	val = meson_elb_readl(mp, PCIE_DEV_CTRL_DEV_STUS);
	val |= PCIE_CAP_MAX_READ_REQ_SIZE(max_rd_req_size);
	meson_elb_writel(mp, val, PCIE_DEV_CTRL_DEV_STUS);
}

static inline void meson_enable_memory_space(struct meson_pcie *mp)
{
	/* Set the RC Bus Master, Memory Space and I/O Space enables */
	meson_elb_writel(mp, PCI_IO_EN | PCI_MEM_SPACE_EN | PCI_BUS_MASTER_EN,
			 PCIE_STATUS_COMMAND);
}

static int meson_pcie_establish_link(struct meson_pcie *mp)
{
	struct dw_pcie *pci = &mp->pci;
	struct pcie_port *pp = &pci->pp;

	meson_pcie_init_dw(mp);
	meson_set_max_payload(mp, MAX_PAYLOAD_SIZE);
	meson_set_max_rd_req_size(mp, MAX_READ_REQ_SIZE);

	dw_pcie_setup_rc(pp);
	meson_enable_memory_space(mp);

	meson_pcie_assert_reset(mp);

	return dw_pcie_wait_for_link(pci);
}

static void meson_pcie_enable_interrupts(struct meson_pcie *mp)
{
	if (IS_ENABLED(CONFIG_PCI_MSI))
		dw_pcie_msi_init(&mp->pci.pp);
}

static int meson_pcie_rd_own_conf(struct pcie_port *pp, int where, int size,
				  u32 *val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	int ret;

	ret = dw_pcie_read(pci->dbi_base + where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	/*
	 * There is a bug in the MESON AXG PCIe controller whereby software
	 * cannot program the PCI_CLASS_DEVICE register, so we must fabricate
	 * the return value in the config accessors.
	 */
	if (where == PCI_CLASS_REVISION && size == 4)
		*val = (PCI_CLASS_BRIDGE_PCI << 16) | (*val & 0xffff);
	else if (where == PCI_CLASS_DEVICE && size == 2)
		*val = PCI_CLASS_BRIDGE_PCI;
	else if (where == PCI_CLASS_DEVICE && size == 1)
		*val = PCI_CLASS_BRIDGE_PCI & 0xff;
	else if (where == PCI_CLASS_DEVICE + 1 && size == 1)
		*val = (PCI_CLASS_BRIDGE_PCI >> 8) & 0xff;

	return PCIBIOS_SUCCESSFUL;
}

static int meson_pcie_wr_own_conf(struct pcie_port *pp, int where,
				  int size, u32 val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	return dw_pcie_write(pci->dbi_base + where, size, val);
}

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
	int ret;

	ret = meson_pcie_establish_link(mp);
	if (ret)
		return ret;

	meson_pcie_enable_interrupts(mp);

	return 0;
}

static const struct dw_pcie_host_ops meson_pcie_host_ops = {
	.rd_own_conf = meson_pcie_rd_own_conf,
	.wr_own_conf = meson_pcie_wr_own_conf,
	.host_init = meson_pcie_host_init,
};

static int meson_add_pcie_port(struct meson_pcie *mp,
			       struct platform_device *pdev)
{
	struct dw_pcie *pci = &mp->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		pp->msi_irq = platform_get_irq(pdev, 0);
		if (pp->msi_irq < 0) {
			dev_err(dev, "failed to get MSI IRQ\n");
			return pp->msi_irq;
		}
	}

	pp->ops = &meson_pcie_host_ops;
	pci->dbi_base = mp->mem_res.elbi_base;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.link_up = meson_pcie_link_up,
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

	meson_pcie_power_on(mp);
	meson_pcie_reset(mp);

	ret = meson_pcie_probe_clocks(mp);
	if (ret) {
		dev_err(dev, "init clock resources failed, %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, mp);

	ret = meson_add_pcie_port(mp, pdev);
	if (ret < 0) {
		dev_err(dev, "Add PCIe port failed, %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id meson_pcie_of_match[] = {
	{
		.compatible = "amlogic,axg-pcie",
	},
	{},
};

static struct platform_driver meson_pcie_driver = {
	.probe = meson_pcie_probe,
	.driver = {
		.name = "meson-pcie",
		.of_match_table = meson_pcie_of_match,
	},
};

builtin_platform_driver(meson_pcie_driver);
