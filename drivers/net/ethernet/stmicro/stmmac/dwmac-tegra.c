// SPDX-License-Identifier: GPL-2.0-only
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/stmmac.h>
#include <linux/clk.h>

#include "stmmac_platform.h"

static const char *const mgbe_clks[] = {
	"rx-pcs", "tx", "tx-pcs", "mac-divider", "mac", "mgbe", "ptp-ref", "mac"
};

struct tegra_mgbe {
	struct device *dev;

	struct clk_bulk_data *clks;

	struct reset_control *rst_mac;
	struct reset_control *rst_pcs;

	void __iomem *hv;
	void __iomem *regs;
	void __iomem *xpcs;

	struct mii_bus *mii;
};

#define XPCS_WRAP_UPHY_RX_CONTROL 0x801c
#define XPCS_WRAP_UPHY_RX_CONTROL_RX_SW_OVRD BIT(31)
#define XPCS_WRAP_UPHY_RX_CONTROL_RX_PCS_PHY_RDY BIT(10)
#define XPCS_WRAP_UPHY_RX_CONTROL_RX_CDR_RESET BIT(9)
#define XPCS_WRAP_UPHY_RX_CONTROL_RX_CAL_EN BIT(8)
#define XPCS_WRAP_UPHY_RX_CONTROL_RX_SLEEP (BIT(7) | BIT(6))
#define XPCS_WRAP_UPHY_RX_CONTROL_AUX_RX_IDDQ BIT(5)
#define XPCS_WRAP_UPHY_RX_CONTROL_RX_IDDQ BIT(4)
#define XPCS_WRAP_UPHY_RX_CONTROL_RX_DATA_EN BIT(0)
#define XPCS_WRAP_UPHY_HW_INIT_CTRL 0x8020
#define XPCS_WRAP_UPHY_HW_INIT_CTRL_TX_EN BIT(0)
#define XPCS_WRAP_UPHY_HW_INIT_CTRL_RX_EN BIT(2)
#define XPCS_WRAP_UPHY_STATUS 0x8044
#define XPCS_WRAP_UPHY_STATUS_TX_P_UP BIT(0)
#define XPCS_WRAP_IRQ_STATUS 0x8050
#define XPCS_WRAP_IRQ_STATUS_PCS_LINK_STS BIT(6)

#define XPCS_REG_ADDR_SHIFT 10
#define XPCS_REG_ADDR_MASK 0x1fff
#define XPCS_ADDR 0x3fc

#define MGBE_WRAP_COMMON_INTR_ENABLE	0x8704
#define MAC_SBD_INTR			BIT(2)
#define MGBE_WRAP_AXI_ASID0_CTRL	0x8400
#define MGBE_SID			0x6

static int __maybe_unused tegra_mgbe_suspend(struct device *dev)
{
	struct tegra_mgbe *mgbe = get_stmmac_bsp_priv(dev);
	int err;

	err = stmmac_suspend(dev);
	if (err)
		return err;

	clk_bulk_disable_unprepare(ARRAY_SIZE(mgbe_clks), mgbe->clks);

	return reset_control_assert(mgbe->rst_mac);
}

static int __maybe_unused tegra_mgbe_resume(struct device *dev)
{
	struct tegra_mgbe *mgbe = get_stmmac_bsp_priv(dev);
	u32 value;
	int err;

	err = clk_bulk_prepare_enable(ARRAY_SIZE(mgbe_clks), mgbe->clks);
	if (err < 0)
		return err;

	err = reset_control_deassert(mgbe->rst_mac);
	if (err < 0)
		return err;

	/* Enable common interrupt at wrapper level */
	writel(MAC_SBD_INTR, mgbe->regs + MGBE_WRAP_COMMON_INTR_ENABLE);

	/* Program SID */
	writel(MGBE_SID, mgbe->hv + MGBE_WRAP_AXI_ASID0_CTRL);

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_STATUS);
	if ((value & XPCS_WRAP_UPHY_STATUS_TX_P_UP) == 0) {
		value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_HW_INIT_CTRL);
		value |= XPCS_WRAP_UPHY_HW_INIT_CTRL_TX_EN;
		writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_HW_INIT_CTRL);
	}

	err = readl_poll_timeout(mgbe->xpcs + XPCS_WRAP_UPHY_HW_INIT_CTRL, value,
				 (value & XPCS_WRAP_UPHY_HW_INIT_CTRL_TX_EN) == 0,
				 500, 500 * 2000);
	if (err < 0) {
		dev_err(mgbe->dev, "timeout waiting for TX lane to become enabled\n");
		clk_bulk_disable_unprepare(ARRAY_SIZE(mgbe_clks), mgbe->clks);
		return err;
	}

	err = stmmac_resume(dev);
	if (err < 0)
		clk_bulk_disable_unprepare(ARRAY_SIZE(mgbe_clks), mgbe->clks);

	return err;
}

static int mgbe_uphy_lane_bringup_serdes_up(struct net_device *ndev, void *mgbe_data)
{
	struct tegra_mgbe *mgbe = (struct tegra_mgbe *)mgbe_data;
	u32 value;
	int err;

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value |= XPCS_WRAP_UPHY_RX_CONTROL_RX_SW_OVRD;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value &= ~XPCS_WRAP_UPHY_RX_CONTROL_RX_IDDQ;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value &= ~XPCS_WRAP_UPHY_RX_CONTROL_AUX_RX_IDDQ;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value &= ~XPCS_WRAP_UPHY_RX_CONTROL_RX_SLEEP;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value |= XPCS_WRAP_UPHY_RX_CONTROL_RX_CAL_EN;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);

	err = readl_poll_timeout(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL, value,
				 (value & XPCS_WRAP_UPHY_RX_CONTROL_RX_CAL_EN) == 0,
				 1000, 1000 * 2000);
	if (err < 0) {
		dev_err(mgbe->dev, "timeout waiting for RX calibration to become enabled\n");
		return err;
	}

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value |= XPCS_WRAP_UPHY_RX_CONTROL_RX_DATA_EN;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value |= XPCS_WRAP_UPHY_RX_CONTROL_RX_CDR_RESET;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value &= ~XPCS_WRAP_UPHY_RX_CONTROL_RX_CDR_RESET;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value |= XPCS_WRAP_UPHY_RX_CONTROL_RX_PCS_PHY_RDY;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);

	err = readl_poll_timeout(mgbe->xpcs + XPCS_WRAP_IRQ_STATUS, value,
				 value & XPCS_WRAP_IRQ_STATUS_PCS_LINK_STS,
				 500, 500 * 2000);
	if (err < 0) {
		dev_err(mgbe->dev, "timeout waiting for link to become ready\n");
		return err;
	}

	/* clear status */
	writel(value, mgbe->xpcs + XPCS_WRAP_IRQ_STATUS);

	return 0;
}

static void mgbe_uphy_lane_bringup_serdes_down(struct net_device *ndev, void *mgbe_data)
{
	struct tegra_mgbe *mgbe = (struct tegra_mgbe *)mgbe_data;
	u32 value;

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value |= XPCS_WRAP_UPHY_RX_CONTROL_RX_SW_OVRD;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value &= ~XPCS_WRAP_UPHY_RX_CONTROL_RX_DATA_EN;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value |= XPCS_WRAP_UPHY_RX_CONTROL_RX_SLEEP;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value |= XPCS_WRAP_UPHY_RX_CONTROL_AUX_RX_IDDQ;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
	value |= XPCS_WRAP_UPHY_RX_CONTROL_RX_IDDQ;
	writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_RX_CONTROL);
}

static int tegra_mgbe_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat;
	struct stmmac_resources res;
	struct tegra_mgbe *mgbe;
	int irq, err, i;
	u32 value;

	mgbe = devm_kzalloc(&pdev->dev, sizeof(*mgbe), GFP_KERNEL);
	if (!mgbe)
		return -ENOMEM;

	mgbe->dev = &pdev->dev;

	memset(&res, 0, sizeof(res));

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	mgbe->hv = devm_platform_ioremap_resource_byname(pdev, "hypervisor");
	if (IS_ERR(mgbe->hv))
		return PTR_ERR(mgbe->hv);

	mgbe->regs = devm_platform_ioremap_resource_byname(pdev, "mac");
	if (IS_ERR(mgbe->regs))
		return PTR_ERR(mgbe->regs);

	mgbe->xpcs = devm_platform_ioremap_resource_byname(pdev, "xpcs");
	if (IS_ERR(mgbe->xpcs))
		return PTR_ERR(mgbe->xpcs);

	res.addr = mgbe->regs;
	res.irq = irq;

	mgbe->clks = devm_kcalloc(&pdev->dev, ARRAY_SIZE(mgbe_clks),
				  sizeof(*mgbe->clks), GFP_KERNEL);
	if (!mgbe->clks)
		return -ENOMEM;

	for (i = 0; i <  ARRAY_SIZE(mgbe_clks); i++)
		mgbe->clks[i].id = mgbe_clks[i];

	err = devm_clk_bulk_get(mgbe->dev, ARRAY_SIZE(mgbe_clks), mgbe->clks);
	if (err < 0)
		return err;

	err = clk_bulk_prepare_enable(ARRAY_SIZE(mgbe_clks), mgbe->clks);
	if (err < 0)
		return err;

	/* Perform MAC reset */
	mgbe->rst_mac = devm_reset_control_get(&pdev->dev, "mac");
	if (IS_ERR(mgbe->rst_mac)) {
		err = PTR_ERR(mgbe->rst_mac);
		goto disable_clks;
	}

	err = reset_control_assert(mgbe->rst_mac);
	if (err < 0)
		goto disable_clks;

	usleep_range(2000, 4000);

	err = reset_control_deassert(mgbe->rst_mac);
	if (err < 0)
		goto disable_clks;

	/* Perform PCS reset */
	mgbe->rst_pcs = devm_reset_control_get(&pdev->dev, "pcs");
	if (IS_ERR(mgbe->rst_pcs)) {
		err = PTR_ERR(mgbe->rst_pcs);
		goto disable_clks;
	}

	err = reset_control_assert(mgbe->rst_pcs);
	if (err < 0)
		goto disable_clks;

	usleep_range(2000, 4000);

	err = reset_control_deassert(mgbe->rst_pcs);
	if (err < 0)
		goto disable_clks;

	plat = stmmac_probe_config_dt(pdev, res.mac);
	if (IS_ERR(plat)) {
		err = PTR_ERR(plat);
		goto disable_clks;
	}

	plat->has_xgmac = 1;
	plat->tso_en = 1;
	plat->pmt = 1;
	plat->bsp_priv = mgbe;

	if (!plat->mdio_node)
		plat->mdio_node = of_get_child_by_name(pdev->dev.of_node, "mdio");

	if (!plat->mdio_bus_data) {
		plat->mdio_bus_data = devm_kzalloc(&pdev->dev, sizeof(*plat->mdio_bus_data),
						   GFP_KERNEL);
		if (!plat->mdio_bus_data) {
			err = -ENOMEM;
			goto remove;
		}
	}

	plat->mdio_bus_data->needs_reset = true;

	value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_STATUS);
	if ((value & XPCS_WRAP_UPHY_STATUS_TX_P_UP) == 0) {
		value = readl(mgbe->xpcs + XPCS_WRAP_UPHY_HW_INIT_CTRL);
		value |= XPCS_WRAP_UPHY_HW_INIT_CTRL_TX_EN;
		writel(value, mgbe->xpcs + XPCS_WRAP_UPHY_HW_INIT_CTRL);
	}

	err = readl_poll_timeout(mgbe->xpcs + XPCS_WRAP_UPHY_HW_INIT_CTRL, value,
				 (value & XPCS_WRAP_UPHY_HW_INIT_CTRL_TX_EN) == 0,
				 500, 500 * 2000);
	if (err < 0) {
		dev_err(mgbe->dev, "timeout waiting for TX lane to become enabled\n");
		goto remove;
	}

	plat->serdes_powerup = mgbe_uphy_lane_bringup_serdes_up;
	plat->serdes_powerdown = mgbe_uphy_lane_bringup_serdes_down;

	/* Tx FIFO Size - 128KB */
	plat->tx_fifo_size = 131072;
	/* Rx FIFO Size - 192KB */
	plat->rx_fifo_size = 196608;

	/* Enable common interrupt at wrapper level */
	writel(MAC_SBD_INTR, mgbe->regs + MGBE_WRAP_COMMON_INTR_ENABLE);

	/* Program SID */
	writel(MGBE_SID, mgbe->hv + MGBE_WRAP_AXI_ASID0_CTRL);

	plat->serdes_up_after_phy_linkup = 1;

	err = stmmac_dvr_probe(&pdev->dev, plat, &res);
	if (err < 0)
		goto remove;

	return 0;

remove:
	stmmac_remove_config_dt(pdev, plat);
disable_clks:
	clk_bulk_disable_unprepare(ARRAY_SIZE(mgbe_clks), mgbe->clks);

	return err;
}

static void tegra_mgbe_remove(struct platform_device *pdev)
{
	struct tegra_mgbe *mgbe = get_stmmac_bsp_priv(&pdev->dev);

	clk_bulk_disable_unprepare(ARRAY_SIZE(mgbe_clks), mgbe->clks);

	stmmac_pltfr_remove(pdev);
}

static const struct of_device_id tegra_mgbe_match[] = {
	{ .compatible = "nvidia,tegra234-mgbe", },
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_mgbe_match);

static SIMPLE_DEV_PM_OPS(tegra_mgbe_pm_ops, tegra_mgbe_suspend, tegra_mgbe_resume);

static struct platform_driver tegra_mgbe_driver = {
	.probe = tegra_mgbe_probe,
	.remove_new = tegra_mgbe_remove,
	.driver = {
		.name = "tegra-mgbe",
		.pm		= &tegra_mgbe_pm_ops,
		.of_match_table = tegra_mgbe_match,
	},
};
module_platform_driver(tegra_mgbe_driver);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra MGBE driver");
MODULE_LICENSE("GPL");
