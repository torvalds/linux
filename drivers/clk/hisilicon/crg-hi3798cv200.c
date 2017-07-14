/*
 * Hi3798CV200 Clock and Reset Generator Driver
 *
 * Copyright (c) 2016 HiSilicon Technologies Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <dt-bindings/clock/histb-clock.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "clk.h"
#include "crg.h"
#include "reset.h"

/* hi3798CV200 core CRG */
#define HI3798CV200_INNER_CLK_OFFSET	64
#define HI3798CV200_FIXED_24M	65
#define HI3798CV200_FIXED_25M	66
#define HI3798CV200_FIXED_50M	67
#define HI3798CV200_FIXED_75M	68
#define HI3798CV200_FIXED_100M	69
#define HI3798CV200_FIXED_150M	70
#define HI3798CV200_FIXED_200M	71
#define HI3798CV200_FIXED_250M	72
#define HI3798CV200_FIXED_300M	73
#define HI3798CV200_FIXED_400M	74
#define HI3798CV200_MMC_MUX	75
#define HI3798CV200_ETH_PUB_CLK	76
#define HI3798CV200_ETH_BUS_CLK	77
#define HI3798CV200_ETH_BUS0_CLK	78
#define HI3798CV200_ETH_BUS1_CLK	79
#define HI3798CV200_COMBPHY1_MUX	80
#define HI3798CV200_FIXED_12M	81
#define HI3798CV200_FIXED_48M	82
#define HI3798CV200_FIXED_60M	83

#define HI3798CV200_CRG_NR_CLKS		128

static const struct hisi_fixed_rate_clock hi3798cv200_fixed_rate_clks[] = {
	{ HISTB_OSC_CLK, "clk_osc", NULL, 0, 24000000, },
	{ HISTB_APB_CLK, "clk_apb", NULL, 0, 100000000, },
	{ HISTB_AHB_CLK, "clk_ahb", NULL, 0, 200000000, },
	{ HI3798CV200_FIXED_12M, "12m", NULL, 0, 12000000, },
	{ HI3798CV200_FIXED_24M, "24m", NULL, 0, 24000000, },
	{ HI3798CV200_FIXED_25M, "25m", NULL, 0, 25000000, },
	{ HI3798CV200_FIXED_48M, "48m", NULL, 0, 48000000, },
	{ HI3798CV200_FIXED_50M, "50m", NULL, 0, 50000000, },
	{ HI3798CV200_FIXED_60M, "60m", NULL, 0, 60000000, },
	{ HI3798CV200_FIXED_75M, "75m", NULL, 0, 75000000, },
	{ HI3798CV200_FIXED_100M, "100m", NULL, 0, 100000000, },
	{ HI3798CV200_FIXED_150M, "150m", NULL, 0, 150000000, },
	{ HI3798CV200_FIXED_200M, "200m", NULL, 0, 200000000, },
	{ HI3798CV200_FIXED_250M, "250m", NULL, 0, 250000000, },
};

static const char *const mmc_mux_p[] = {
		"100m", "50m", "25m", "200m", "150m" };
static u32 mmc_mux_table[] = {0, 1, 2, 3, 6};

static const char *const comphy1_mux_p[] = {
		"100m", "25m"};
static u32 comphy1_mux_table[] = {2, 3};

static struct hisi_mux_clock hi3798cv200_mux_clks[] = {
	{ HI3798CV200_MMC_MUX, "mmc_mux", mmc_mux_p, ARRAY_SIZE(mmc_mux_p),
		CLK_SET_RATE_PARENT, 0xa0, 8, 3, 0, mmc_mux_table, },
	{ HI3798CV200_COMBPHY1_MUX, "combphy1_mux",
		comphy1_mux_p, ARRAY_SIZE(comphy1_mux_p),
		CLK_SET_RATE_PARENT, 0x188, 10, 2, 0, comphy1_mux_table, },
};

static const struct hisi_gate_clock hi3798cv200_gate_clks[] = {
	/* UART */
	{ HISTB_UART2_CLK, "clk_uart2", "75m",
		CLK_SET_RATE_PARENT, 0x68, 4, 0, },
	/* I2C */
	{ HISTB_I2C0_CLK, "clk_i2c0", "clk_apb",
		CLK_SET_RATE_PARENT, 0x6C, 4, 0, },
	{ HISTB_I2C1_CLK, "clk_i2c1", "clk_apb",
		CLK_SET_RATE_PARENT, 0x6C, 8, 0, },
	{ HISTB_I2C2_CLK, "clk_i2c2", "clk_apb",
		CLK_SET_RATE_PARENT, 0x6C, 12, 0, },
	{ HISTB_I2C3_CLK, "clk_i2c3", "clk_apb",
		CLK_SET_RATE_PARENT, 0x6C, 16, 0, },
	{ HISTB_I2C4_CLK, "clk_i2c4", "clk_apb",
		CLK_SET_RATE_PARENT, 0x6C, 20, 0, },
	/* SPI */
	{ HISTB_SPI0_CLK, "clk_spi0", "clk_apb",
		CLK_SET_RATE_PARENT, 0x70, 0, 0, },
	/* SDIO */
	{ HISTB_SDIO0_BIU_CLK, "clk_sdio0_biu", "200m",
			CLK_SET_RATE_PARENT, 0x9c, 0, 0, },
	{ HISTB_SDIO0_CIU_CLK, "clk_sdio0_ciu", "mmc_mux",
		CLK_SET_RATE_PARENT, 0x9c, 1, 0, },
	/* EMMC */
	{ HISTB_MMC_BIU_CLK, "clk_mmc_biu", "200m",
		CLK_SET_RATE_PARENT, 0xa0, 0, 0, },
	{ HISTB_MMC_CIU_CLK, "clk_mmc_ciu", "mmc_mux",
		CLK_SET_RATE_PARENT, 0xa0, 1, 0, },
	/* PCIE*/
	{ HISTB_PCIE_BUS_CLK, "clk_pcie_bus", "200m",
		CLK_SET_RATE_PARENT, 0x18c, 0, 0, },
	{ HISTB_PCIE_SYS_CLK, "clk_pcie_sys", "100m",
		CLK_SET_RATE_PARENT, 0x18c, 1, 0, },
	{ HISTB_PCIE_PIPE_CLK, "clk_pcie_pipe", "250m",
		CLK_SET_RATE_PARENT, 0x18c, 2, 0, },
	{ HISTB_PCIE_AUX_CLK, "clk_pcie_aux", "24m",
		CLK_SET_RATE_PARENT, 0x18c, 3, 0, },
	/* Ethernet */
	{ HI3798CV200_ETH_PUB_CLK, "clk_pub", NULL,
		CLK_SET_RATE_PARENT, 0xcc, 5, 0, },
	{ HI3798CV200_ETH_BUS_CLK, "clk_bus", "clk_pub",
		CLK_SET_RATE_PARENT, 0xcc, 0, 0, },
	{ HI3798CV200_ETH_BUS0_CLK, "clk_bus_m0", "clk_bus",
		CLK_SET_RATE_PARENT, 0xcc, 1, 0, },
	{ HI3798CV200_ETH_BUS1_CLK, "clk_bus_m1", "clk_bus",
		CLK_SET_RATE_PARENT, 0xcc, 2, 0, },
	{ HISTB_ETH0_MAC_CLK, "clk_mac0", "clk_bus_m0",
		CLK_SET_RATE_PARENT, 0xcc, 3, 0, },
	{ HISTB_ETH0_MACIF_CLK, "clk_macif0", "clk_bus_m0",
		CLK_SET_RATE_PARENT, 0xcc, 24, 0, },
	{ HISTB_ETH1_MAC_CLK, "clk_mac1", "clk_bus_m1",
		CLK_SET_RATE_PARENT, 0xcc, 4, 0, },
	{ HISTB_ETH1_MACIF_CLK, "clk_macif1", "clk_bus_m1",
		CLK_SET_RATE_PARENT, 0xcc, 25, 0, },
	/* COMBPHY1 */
	{ HISTB_COMBPHY1_CLK, "clk_combphy1", "combphy1_mux",
		CLK_SET_RATE_PARENT, 0x188, 8, 0, },
	/* USB2 */
	{ HISTB_USB2_BUS_CLK, "clk_u2_bus", "clk_ahb",
		CLK_SET_RATE_PARENT, 0xb8, 0, 0, },
	{ HISTB_USB2_PHY_CLK, "clk_u2_phy", "60m",
		CLK_SET_RATE_PARENT, 0xb8, 4, 0, },
	{ HISTB_USB2_12M_CLK, "clk_u2_12m", "12m",
		CLK_SET_RATE_PARENT, 0xb8, 2, 0 },
	{ HISTB_USB2_48M_CLK, "clk_u2_48m", "48m",
		CLK_SET_RATE_PARENT, 0xb8, 1, 0 },
	{ HISTB_USB2_UTMI_CLK, "clk_u2_utmi", "60m",
		CLK_SET_RATE_PARENT, 0xb8, 5, 0 },
	{ HISTB_USB2_PHY1_REF_CLK, "clk_u2_phy1_ref", "24m",
		CLK_SET_RATE_PARENT, 0xbc, 0, 0 },
	{ HISTB_USB2_PHY2_REF_CLK, "clk_u2_phy2_ref", "24m",
		CLK_SET_RATE_PARENT, 0xbc, 2, 0 },
};

static struct hisi_clock_data *hi3798cv200_clk_register(
				struct platform_device *pdev)
{
	struct hisi_clock_data *clk_data;
	int ret;

	clk_data = hisi_clk_alloc(pdev, HI3798CV200_CRG_NR_CLKS);
	if (!clk_data)
		return ERR_PTR(-ENOMEM);

	ret = hisi_clk_register_fixed_rate(hi3798cv200_fixed_rate_clks,
				     ARRAY_SIZE(hi3798cv200_fixed_rate_clks),
				     clk_data);
	if (ret)
		return ERR_PTR(ret);

	ret = hisi_clk_register_mux(hi3798cv200_mux_clks,
				ARRAY_SIZE(hi3798cv200_mux_clks),
				clk_data);
	if (ret)
		goto unregister_fixed_rate;

	ret = hisi_clk_register_gate(hi3798cv200_gate_clks,
				ARRAY_SIZE(hi3798cv200_gate_clks),
				clk_data);
	if (ret)
		goto unregister_mux;

	ret = of_clk_add_provider(pdev->dev.of_node,
			of_clk_src_onecell_get, &clk_data->clk_data);
	if (ret)
		goto unregister_gate;

	return clk_data;

unregister_fixed_rate:
	hisi_clk_unregister_fixed_rate(hi3798cv200_fixed_rate_clks,
				ARRAY_SIZE(hi3798cv200_fixed_rate_clks),
				clk_data);

unregister_mux:
	hisi_clk_unregister_mux(hi3798cv200_mux_clks,
				ARRAY_SIZE(hi3798cv200_mux_clks),
				clk_data);
unregister_gate:
	hisi_clk_unregister_gate(hi3798cv200_gate_clks,
				ARRAY_SIZE(hi3798cv200_gate_clks),
				clk_data);
	return ERR_PTR(ret);
}

static void hi3798cv200_clk_unregister(struct platform_device *pdev)
{
	struct hisi_crg_dev *crg = platform_get_drvdata(pdev);

	of_clk_del_provider(pdev->dev.of_node);

	hisi_clk_unregister_gate(hi3798cv200_gate_clks,
				ARRAY_SIZE(hi3798cv200_gate_clks),
				crg->clk_data);
	hisi_clk_unregister_mux(hi3798cv200_mux_clks,
				ARRAY_SIZE(hi3798cv200_mux_clks),
				crg->clk_data);
	hisi_clk_unregister_fixed_rate(hi3798cv200_fixed_rate_clks,
				ARRAY_SIZE(hi3798cv200_fixed_rate_clks),
				crg->clk_data);
}

static const struct hisi_crg_funcs hi3798cv200_crg_funcs = {
	.register_clks = hi3798cv200_clk_register,
	.unregister_clks = hi3798cv200_clk_unregister,
};

/* hi3798CV200 sysctrl CRG */

#define HI3798CV200_SYSCTRL_NR_CLKS 16

static const struct hisi_gate_clock hi3798cv200_sysctrl_gate_clks[] = {
	{ HISTB_IR_CLK, "clk_ir", "100m",
		CLK_SET_RATE_PARENT, 0x48, 4, 0, },
	{ HISTB_TIMER01_CLK, "clk_timer01", "24m",
		CLK_SET_RATE_PARENT, 0x48, 6, 0, },
	{ HISTB_UART0_CLK, "clk_uart0", "75m",
		CLK_SET_RATE_PARENT, 0x48, 10, 0, },
};

static struct hisi_clock_data *hi3798cv200_sysctrl_clk_register(
					struct platform_device *pdev)
{
	struct hisi_clock_data *clk_data;
	int ret;

	clk_data = hisi_clk_alloc(pdev, HI3798CV200_SYSCTRL_NR_CLKS);
	if (!clk_data)
		return ERR_PTR(-ENOMEM);

	ret = hisi_clk_register_gate(hi3798cv200_sysctrl_gate_clks,
				ARRAY_SIZE(hi3798cv200_sysctrl_gate_clks),
				clk_data);
	if (ret)
		return ERR_PTR(ret);

	ret = of_clk_add_provider(pdev->dev.of_node,
			of_clk_src_onecell_get, &clk_data->clk_data);
	if (ret)
		goto unregister_gate;

	return clk_data;

unregister_gate:
	hisi_clk_unregister_gate(hi3798cv200_sysctrl_gate_clks,
				ARRAY_SIZE(hi3798cv200_sysctrl_gate_clks),
				clk_data);
	return ERR_PTR(ret);
}

static void hi3798cv200_sysctrl_clk_unregister(struct platform_device *pdev)
{
	struct hisi_crg_dev *crg = platform_get_drvdata(pdev);

	of_clk_del_provider(pdev->dev.of_node);

	hisi_clk_unregister_gate(hi3798cv200_sysctrl_gate_clks,
				ARRAY_SIZE(hi3798cv200_sysctrl_gate_clks),
				crg->clk_data);
}

static const struct hisi_crg_funcs hi3798cv200_sysctrl_funcs = {
	.register_clks = hi3798cv200_sysctrl_clk_register,
	.unregister_clks = hi3798cv200_sysctrl_clk_unregister,
};

static const struct of_device_id hi3798cv200_crg_match_table[] = {
	{ .compatible = "hisilicon,hi3798cv200-crg",
		.data = &hi3798cv200_crg_funcs },
	{ .compatible = "hisilicon,hi3798cv200-sysctrl",
		.data = &hi3798cv200_sysctrl_funcs },
	{ }
};
MODULE_DEVICE_TABLE(of, hi3798cv200_crg_match_table);

static int hi3798cv200_crg_probe(struct platform_device *pdev)
{
	struct hisi_crg_dev *crg;

	crg = devm_kmalloc(&pdev->dev, sizeof(*crg), GFP_KERNEL);
	if (!crg)
		return -ENOMEM;

	crg->funcs = of_device_get_match_data(&pdev->dev);
	if (!crg->funcs)
		return -ENOENT;

	crg->rstc = hisi_reset_init(pdev);
	if (!crg->rstc)
		return -ENOMEM;

	crg->clk_data = crg->funcs->register_clks(pdev);
	if (IS_ERR(crg->clk_data)) {
		hisi_reset_exit(crg->rstc);
		return PTR_ERR(crg->clk_data);
	}

	platform_set_drvdata(pdev, crg);
	return 0;
}

static int hi3798cv200_crg_remove(struct platform_device *pdev)
{
	struct hisi_crg_dev *crg = platform_get_drvdata(pdev);

	hisi_reset_exit(crg->rstc);
	crg->funcs->unregister_clks(pdev);
	return 0;
}

static struct platform_driver hi3798cv200_crg_driver = {
	.probe          = hi3798cv200_crg_probe,
	.remove		= hi3798cv200_crg_remove,
	.driver         = {
		.name   = "hi3798cv200-crg",
		.of_match_table = hi3798cv200_crg_match_table,
	},
};

static int __init hi3798cv200_crg_init(void)
{
	return platform_driver_register(&hi3798cv200_crg_driver);
}
core_initcall(hi3798cv200_crg_init);

static void __exit hi3798cv200_crg_exit(void)
{
	platform_driver_unregister(&hi3798cv200_crg_driver);
}
module_exit(hi3798cv200_crg_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("HiSilicon Hi3798CV200 CRG Driver");
