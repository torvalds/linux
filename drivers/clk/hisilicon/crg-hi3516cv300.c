// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hi3516CV300 Clock and Reset Generator Driver
 *
 * Copyright (c) 2016 HiSilicon Technologies Co., Ltd.
 */

#include <dt-bindings/clock/hi3516cv300-clock.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include "clk.h"
#include "crg.h"
#include "reset.h"

/* hi3516CV300 core CRG */
#define HI3516CV300_INNER_CLK_OFFSET	64
#define HI3516CV300_FIXED_3M		65
#define HI3516CV300_FIXED_6M		66
#define HI3516CV300_FIXED_24M		67
#define HI3516CV300_FIXED_49P5		68
#define HI3516CV300_FIXED_50M		69
#define HI3516CV300_FIXED_83P3M		70
#define HI3516CV300_FIXED_99M		71
#define HI3516CV300_FIXED_100M		72
#define HI3516CV300_FIXED_148P5M	73
#define HI3516CV300_FIXED_198M		74
#define HI3516CV300_FIXED_297M		75
#define HI3516CV300_UART_MUX		76
#define HI3516CV300_FMC_MUX		77
#define HI3516CV300_MMC0_MUX		78
#define HI3516CV300_MMC1_MUX		79
#define HI3516CV300_MMC2_MUX		80
#define HI3516CV300_MMC3_MUX		81
#define HI3516CV300_PWM_MUX		82
#define HI3516CV300_CRG_NR_CLKS		128

static const struct hisi_fixed_rate_clock hi3516cv300_fixed_rate_clks[] = {
	{ HI3516CV300_FIXED_3M, "3m", NULL, 0, 3000000, },
	{ HI3516CV300_FIXED_6M, "6m", NULL, 0, 6000000, },
	{ HI3516CV300_FIXED_24M, "24m", NULL, 0, 24000000, },
	{ HI3516CV300_FIXED_49P5, "49.5m", NULL, 0, 49500000, },
	{ HI3516CV300_FIXED_50M, "50m", NULL, 0, 50000000, },
	{ HI3516CV300_FIXED_83P3M, "83.3m", NULL, 0, 83300000, },
	{ HI3516CV300_FIXED_99M, "99m", NULL, 0, 99000000, },
	{ HI3516CV300_FIXED_100M, "100m", NULL, 0, 100000000, },
	{ HI3516CV300_FIXED_148P5M, "148.5m", NULL, 0, 148500000, },
	{ HI3516CV300_FIXED_198M, "198m", NULL, 0, 198000000, },
	{ HI3516CV300_FIXED_297M, "297m", NULL, 0, 297000000, },
	{ HI3516CV300_APB_CLK, "apb", NULL, 0, 50000000, },
};

static const char *const uart_mux_p[] = {"24m", "6m"};
static const char *const fmc_mux_p[] = {
	"24m", "83.3m", "148.5m", "198m", "297m"
};
static const char *const mmc_mux_p[] = {"49.5m"};
static const char *const mmc2_mux_p[] = {"99m", "49.5m"};
static const char *const pwm_mux_p[] = {"3m", "50m", "24m", "24m"};

static u32 uart_mux_table[] = {0, 1};
static u32 fmc_mux_table[] = {0, 1, 2, 3, 4};
static u32 mmc_mux_table[] = {0};
static u32 mmc2_mux_table[] = {0, 2};
static u32 pwm_mux_table[] = {0, 1, 2, 3};

static const struct hisi_mux_clock hi3516cv300_mux_clks[] = {
	{ HI3516CV300_UART_MUX, "uart_mux", uart_mux_p, ARRAY_SIZE(uart_mux_p),
		CLK_SET_RATE_PARENT, 0xe4, 19, 1, 0, uart_mux_table, },
	{ HI3516CV300_FMC_MUX, "fmc_mux", fmc_mux_p, ARRAY_SIZE(fmc_mux_p),
		CLK_SET_RATE_PARENT, 0xc0, 2, 3, 0, fmc_mux_table, },
	{ HI3516CV300_MMC0_MUX, "mmc0_mux", mmc_mux_p, ARRAY_SIZE(mmc_mux_p),
		CLK_SET_RATE_PARENT, 0xc4, 4, 2, 0, mmc_mux_table, },
	{ HI3516CV300_MMC1_MUX, "mmc1_mux", mmc_mux_p, ARRAY_SIZE(mmc_mux_p),
		CLK_SET_RATE_PARENT, 0xc4, 12, 2, 0, mmc_mux_table, },
	{ HI3516CV300_MMC2_MUX, "mmc2_mux", mmc2_mux_p, ARRAY_SIZE(mmc2_mux_p),
		CLK_SET_RATE_PARENT, 0xc4, 20, 2, 0, mmc2_mux_table, },
	{ HI3516CV300_MMC3_MUX, "mmc3_mux", mmc_mux_p, ARRAY_SIZE(mmc_mux_p),
		CLK_SET_RATE_PARENT, 0xc8, 4, 2, 0, mmc_mux_table, },
	{ HI3516CV300_PWM_MUX, "pwm_mux", pwm_mux_p, ARRAY_SIZE(pwm_mux_p),
		CLK_SET_RATE_PARENT, 0x38, 2, 2, 0, pwm_mux_table, },
};

static const struct hisi_gate_clock hi3516cv300_gate_clks[] = {

	{ HI3516CV300_UART0_CLK, "clk_uart0", "uart_mux", CLK_SET_RATE_PARENT,
		0xe4, 15, 0, },
	{ HI3516CV300_UART1_CLK, "clk_uart1", "uart_mux", CLK_SET_RATE_PARENT,
		0xe4, 16, 0, },
	{ HI3516CV300_UART2_CLK, "clk_uart2", "uart_mux", CLK_SET_RATE_PARENT,
		0xe4, 17, 0, },

	{ HI3516CV300_SPI0_CLK, "clk_spi0", "100m", CLK_SET_RATE_PARENT,
		0xe4, 13, 0, },
	{ HI3516CV300_SPI1_CLK, "clk_spi1", "100m", CLK_SET_RATE_PARENT,
		0xe4, 14, 0, },

	{ HI3516CV300_FMC_CLK, "clk_fmc", "fmc_mux", CLK_SET_RATE_PARENT,
		0xc0, 1, 0, },
	{ HI3516CV300_MMC0_CLK, "clk_mmc0", "mmc0_mux", CLK_SET_RATE_PARENT,
		0xc4, 1, 0, },
	{ HI3516CV300_MMC1_CLK, "clk_mmc1", "mmc1_mux", CLK_SET_RATE_PARENT,
		0xc4, 9, 0, },
	{ HI3516CV300_MMC2_CLK, "clk_mmc2", "mmc2_mux", CLK_SET_RATE_PARENT,
		0xc4, 17, 0, },
	{ HI3516CV300_MMC3_CLK, "clk_mmc3", "mmc3_mux", CLK_SET_RATE_PARENT,
		0xc8, 1, 0, },

	{ HI3516CV300_ETH_CLK, "clk_eth", NULL, 0, 0xec, 1, 0, },

	{ HI3516CV300_DMAC_CLK, "clk_dmac", NULL, 0, 0xd8, 5, 0, },
	{ HI3516CV300_PWM_CLK, "clk_pwm", "pwm_mux", CLK_SET_RATE_PARENT,
		0x38, 1, 0, },

	{ HI3516CV300_USB2_BUS_CLK, "clk_usb2_bus", NULL, 0, 0xb8, 0, 0, },
	{ HI3516CV300_USB2_OHCI48M_CLK, "clk_usb2_ohci48m", NULL, 0,
		0xb8, 1, 0, },
	{ HI3516CV300_USB2_OHCI12M_CLK, "clk_usb2_ohci12m", NULL, 0,
		0xb8, 2, 0, },
	{ HI3516CV300_USB2_OTG_UTMI_CLK, "clk_usb2_otg_utmi", NULL, 0,
		0xb8, 3, 0, },
	{ HI3516CV300_USB2_HST_PHY_CLK, "clk_usb2_hst_phy", NULL, 0,
		0xb8, 4, 0, },
	{ HI3516CV300_USB2_UTMI0_CLK, "clk_usb2_utmi0", NULL, 0, 0xb8, 5, 0, },
	{ HI3516CV300_USB2_PHY_CLK, "clk_usb2_phy", NULL, 0, 0xb8, 7, 0, },
};

static struct hisi_clock_data *hi3516cv300_clk_register(
		struct platform_device *pdev)
{
	struct hisi_clock_data *clk_data;
	int ret;

	clk_data = hisi_clk_alloc(pdev, HI3516CV300_CRG_NR_CLKS);
	if (!clk_data)
		return ERR_PTR(-ENOMEM);

	ret = hisi_clk_register_fixed_rate(hi3516cv300_fixed_rate_clks,
			ARRAY_SIZE(hi3516cv300_fixed_rate_clks), clk_data);
	if (ret)
		return ERR_PTR(ret);

	ret = hisi_clk_register_mux(hi3516cv300_mux_clks,
			ARRAY_SIZE(hi3516cv300_mux_clks), clk_data);
	if (ret)
		goto unregister_fixed_rate;

	ret = hisi_clk_register_gate(hi3516cv300_gate_clks,
			ARRAY_SIZE(hi3516cv300_gate_clks), clk_data);
	if (ret)
		goto unregister_mux;

	ret = of_clk_add_provider(pdev->dev.of_node,
			of_clk_src_onecell_get, &clk_data->clk_data);
	if (ret)
		goto unregister_gate;

	return clk_data;

unregister_gate:
	hisi_clk_unregister_gate(hi3516cv300_gate_clks,
				ARRAY_SIZE(hi3516cv300_gate_clks), clk_data);
unregister_mux:
	hisi_clk_unregister_mux(hi3516cv300_mux_clks,
			ARRAY_SIZE(hi3516cv300_mux_clks), clk_data);
unregister_fixed_rate:
	hisi_clk_unregister_fixed_rate(hi3516cv300_fixed_rate_clks,
			ARRAY_SIZE(hi3516cv300_fixed_rate_clks), clk_data);
	return ERR_PTR(ret);
}

static void hi3516cv300_clk_unregister(struct platform_device *pdev)
{
	struct hisi_crg_dev *crg = platform_get_drvdata(pdev);

	of_clk_del_provider(pdev->dev.of_node);

	hisi_clk_unregister_gate(hi3516cv300_gate_clks,
			ARRAY_SIZE(hi3516cv300_gate_clks), crg->clk_data);
	hisi_clk_unregister_mux(hi3516cv300_mux_clks,
			ARRAY_SIZE(hi3516cv300_mux_clks), crg->clk_data);
	hisi_clk_unregister_fixed_rate(hi3516cv300_fixed_rate_clks,
			ARRAY_SIZE(hi3516cv300_fixed_rate_clks), crg->clk_data);
}

static const struct hisi_crg_funcs hi3516cv300_crg_funcs = {
	.register_clks = hi3516cv300_clk_register,
	.unregister_clks = hi3516cv300_clk_unregister,
};

/* hi3516CV300 sysctrl CRG */
#define HI3516CV300_SYSCTRL_NR_CLKS 16

static const char *const wdt_mux_p[] __initconst = { "3m", "apb" };
static u32 wdt_mux_table[] = {0, 1};

static const struct hisi_mux_clock hi3516cv300_sysctrl_mux_clks[] = {
	{ HI3516CV300_WDT_CLK, "wdt", wdt_mux_p, ARRAY_SIZE(wdt_mux_p),
		CLK_SET_RATE_PARENT, 0x0, 23, 1, 0, wdt_mux_table, },
};

static struct hisi_clock_data *hi3516cv300_sysctrl_clk_register(
		struct platform_device *pdev)
{
	struct hisi_clock_data *clk_data;
	int ret;

	clk_data = hisi_clk_alloc(pdev, HI3516CV300_SYSCTRL_NR_CLKS);
	if (!clk_data)
		return ERR_PTR(-ENOMEM);

	ret = hisi_clk_register_mux(hi3516cv300_sysctrl_mux_clks,
			ARRAY_SIZE(hi3516cv300_sysctrl_mux_clks), clk_data);
	if (ret)
		return ERR_PTR(ret);


	ret = of_clk_add_provider(pdev->dev.of_node,
			of_clk_src_onecell_get, &clk_data->clk_data);
	if (ret)
		goto unregister_mux;

	return clk_data;

unregister_mux:
	hisi_clk_unregister_mux(hi3516cv300_sysctrl_mux_clks,
			ARRAY_SIZE(hi3516cv300_sysctrl_mux_clks), clk_data);
	return ERR_PTR(ret);
}

static void hi3516cv300_sysctrl_clk_unregister(struct platform_device *pdev)
{
	struct hisi_crg_dev *crg = platform_get_drvdata(pdev);

	of_clk_del_provider(pdev->dev.of_node);

	hisi_clk_unregister_mux(hi3516cv300_sysctrl_mux_clks,
			ARRAY_SIZE(hi3516cv300_sysctrl_mux_clks),
			crg->clk_data);
}

static const struct hisi_crg_funcs hi3516cv300_sysctrl_funcs = {
	.register_clks = hi3516cv300_sysctrl_clk_register,
	.unregister_clks = hi3516cv300_sysctrl_clk_unregister,
};

static const struct of_device_id hi3516cv300_crg_match_table[] = {
	{
		.compatible = "hisilicon,hi3516cv300-crg",
		.data = &hi3516cv300_crg_funcs
	},
	{
		.compatible = "hisilicon,hi3516cv300-sysctrl",
		.data = &hi3516cv300_sysctrl_funcs
	},
	{ }
};
MODULE_DEVICE_TABLE(of, hi3516cv300_crg_match_table);

static int hi3516cv300_crg_probe(struct platform_device *pdev)
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

static void hi3516cv300_crg_remove(struct platform_device *pdev)
{
	struct hisi_crg_dev *crg = platform_get_drvdata(pdev);

	hisi_reset_exit(crg->rstc);
	crg->funcs->unregister_clks(pdev);
}

static struct platform_driver hi3516cv300_crg_driver = {
	.probe          = hi3516cv300_crg_probe,
	.remove_new	= hi3516cv300_crg_remove,
	.driver         = {
		.name   = "hi3516cv300-crg",
		.of_match_table = hi3516cv300_crg_match_table,
	},
};

static int __init hi3516cv300_crg_init(void)
{
	return platform_driver_register(&hi3516cv300_crg_driver);
}
core_initcall(hi3516cv300_crg_init);

static void __exit hi3516cv300_crg_exit(void)
{
	platform_driver_unregister(&hi3516cv300_crg_driver);
}
module_exit(hi3516cv300_crg_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("HiSilicon Hi3516CV300 CRG Driver");
