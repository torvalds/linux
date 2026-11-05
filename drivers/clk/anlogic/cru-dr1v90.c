// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2025 Anlogic, Inc.
 * Copyright (C) 2026 Junhui Liu <junhui.liu@pigmoral.tech>
 */

#include <linux/array_size.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "cru_dr1.h"

#include <dt-bindings/clock/anlogic,dr1v90-cru.h>

static const struct clk_div_table cru_div_table_24[] = {
	{ 0xFFFFFF, 1 },  { 0x555555, 2 },  { 0x249249, 3 },  { 0x111111, 4 },
	{ 0x084210, 5 },  { 0x041041, 6 },  { 0x020408, 7 },  { 0x010101, 8 },
	{ 0x008040, 9 },  { 0x004010, 10 }, { 0x002004, 11 }, { 0x001001, 12 },
	{ 0x000800, 13 }, { 0x000400, 14 }, { 0x000200, 15 }, { 0x000100, 16 },
	{ 0x000080, 17 }, { 0x000040, 18 }, { 0x000020, 19 }, { 0x000010, 20 },
	{ 0x000008, 21 }, { 0x000004, 22 }, { 0x000002, 23 }, { 0x000001, 24 },
	{ /* sentinel */ }
};

static const struct clk_div_table cru_div_table_32[] = {
	{ 0xFFFFFFFF, 1 },  { 0x55555555, 2 },	{ 0x24924924, 3 },
	{ 0x11111111, 4 },  { 0x08421084, 5 },	{ 0x04104104, 6 },
	{ 0x02040810, 7 },  { 0x01010101, 8 },	{ 0x00804020, 9 },
	{ 0x00401004, 10 }, { 0x00200400, 11 }, { 0x00100100, 12 },
	{ 0x00080040, 13 }, { 0x00040010, 14 }, { 0x00020004, 15 },
	{ 0x00010001, 16 }, { 0x00008000, 17 }, { 0x00004000, 18 },
	{ 0x00002000, 19 }, { 0x00001000, 20 }, { 0x00000800, 21 },
	{ 0x00000400, 22 }, { 0x00000200, 23 }, { 0x00000100, 24 },
	{ 0x00000080, 25 }, { 0x00000040, 26 }, { 0x00000020, 27 },
	{ 0x00000010, 28 }, { 0x00000008, 29 }, { 0x00000004, 30 },
	{ 0x00000002, 31 }, { 0x00000001, 32 }, { /* sentinel */ }
};

CLK_FIXED_FACTOR_FW_NAME(osc_div2, "osc_div2", "osc", 2, 1, 0);

CRU_PLL_NM_DEFINE(cpu_pll, CRU_PARENT_NAME(osc), 0x120);
CRU_PLL_C_DEFINE(cpu_pll_4x, CRU_PARENT_HW(cpu_pll), 0x14c);

CRU_DIV_DEFINE(cpu_4x_div1, CRU_PARENT_HW(cpu_pll_4x), 0x010, 0, 24,
	       cru_div_table_24, CLK_DIVIDER_READ_ONLY);
CRU_DIV_DEFINE(cpu_4x_div2, CRU_PARENT_HW(cpu_pll_4x), 0x014, 0, 24,
	       cru_div_table_24, CLK_DIVIDER_READ_ONLY);
CRU_DIV_DEFINE(cpu_4x_div4, CRU_PARENT_HW(cpu_pll_4x), 0x018, 0, 24,
	       cru_div_table_24, CLK_DIVIDER_READ_ONLY);

CRU_PLL_NM_DEFINE(io_pll, CRU_PARENT_NAME(osc), 0x220);
CRU_PLL_C_DEFINE(io_1000m, CRU_PARENT_HW(io_pll), 0x248);
CRU_PLL_C_DEFINE(io_400m, CRU_PARENT_HW(io_pll), 0x24c);
CRU_PLL_C_DEFINE(io_25m, CRU_PARENT_HW(io_pll), 0x250);
CRU_PLL_C_DEFINE(io_80m, CRU_PARENT_HW(io_pll), 0x254);

CRU_DIV_DEFINE(io_400m_div2, CRU_PARENT_HW(io_400m), 0x020, 0, 32,
	       cru_div_table_32, CLK_DIVIDER_READ_ONLY);
CRU_DIV_DEFINE(io_400m_div4, CRU_PARENT_HW(io_400m), 0x024, 0, 32,
	       cru_div_table_32, CLK_DIVIDER_READ_ONLY);
CRU_DIV_DEFINE(io_400m_div8, CRU_PARENT_HW(io_400m), 0x028, 0, 32,
	       cru_div_table_32, CLK_DIVIDER_READ_ONLY);
CRU_DIV_DEFINE(io_400m_div16, CRU_PARENT_HW(io_400m), 0x02c, 0, 32,
	       cru_div_table_32, CLK_DIVIDER_READ_ONLY);

CRU_DIV_GATE_DEFINE(qspi, CRU_PARENT_HW(io_1000m), 0x030, 0, 6, NULL, 0, 2);
CRU_DIV_GATE_DEFINE(spi, CRU_PARENT_HW(io_1000m), 0x030, 8, 6, NULL, 0, 4);
CRU_DIV_GATE_DEFINE(smc, CRU_PARENT_HW(io_1000m), 0x030, 16, 6, NULL, 0, 4);
CRU_DIV_DEFINE(sdio, CRU_PARENT_HW(io_400m), 0x030, 24, 6, NULL, 0);

CRU_DIV_GATE_DEFINE(gpio_db, CRU_PARENT_HW(io_25m), 0x034, 0, 6, NULL, 0, 1);
CRU_DIV_GATE_DEFINE(efuse, CRU_PARENT_HW(io_25m), 0x034, 8, 6, NULL, 0, 1);
CRU_DIV_GATE_DEFINE(tvs, CRU_PARENT_HW(io_25m), 0x034, 16, 6, NULL, 0, 1);
CRU_DIV_GATE_DEFINE(trng, CRU_PARENT_HW(io_25m), 0x034, 24, 7, NULL, 0, 1);

CRU_DIV_GATE_DEFINE(osc_div, CRU_PARENT_NAME(osc), 0x038, 0, 6, NULL, 0, 1);
CRU_DIV_GATE_DEFINE(pwm, CRU_PARENT_NAME(osc), 0x038, 8, 12, NULL, 0, 1);

CRU_DIV_GATE_DEFINE(fclk0, CRU_PARENT_HW(io_400m), 0x03c, 0, 6, NULL, 0, 1);
CRU_DIV_GATE_DEFINE(fclk1, CRU_PARENT_HW(io_400m), 0x03c, 8, 6, NULL, 0, 1);
CRU_DIV_GATE_DEFINE(fclk2, CRU_PARENT_HW(io_400m), 0x03c, 16, 6, NULL, 0, 1);
CRU_DIV_GATE_DEFINE(fclk3, CRU_PARENT_HW(io_400m), 0x03c, 24, 6, NULL, 0, 1);

static const struct clk_parent_data wdt_parents[] = {
	CRU_PARENT_HW(osc_div2),
	CRU_PARENT_NAME(wdt_ext)
};
CRU_MUX_DEFINE(wdt_sel, wdt_parents, 0x040, 1, 1);

static const struct clk_parent_data efuse_parents[] = {
	CRU_PARENT_NAME(osc),
	CRU_PARENT_DIV_HW(efuse)
};
CRU_MUX_DEFINE(efuse_sel, efuse_parents, 0x040, 2, 1);

static const struct clk_parent_data can_parents[] = {
	CRU_PARENT_HW(io_80m),
	CRU_PARENT_NAME(can_ext)
};
CRU_MUX_DEFINE(can_sel, can_parents, 0x040, 3, 1);

static const struct clk_parent_data cpu_parents[] = {
	CRU_PARENT_HW(cpu_4x_div1),
	CRU_PARENT_HW(cpu_4x_div2)
};
CRU_MUX_DEFINE(cpu_sel, cpu_parents, 0x040, 5, 1);

CRU_GATE_DEFINE(can0, CRU_PARENT_HW(can_sel), 0x08c, 20, CLK_GATE_SET_TO_DISABLE);
CRU_GATE_DEFINE(can1, CRU_PARENT_HW(can_sel), 0x08c, 21, CLK_GATE_SET_TO_DISABLE);

static const struct cru_clk dr1v90_cru_clks[] = {
	[CLK_OSC_DIV2]		= { &osc_div2.hw,	NULL },
	[CLK_CPU_PLL]		= { &cpu_pll.hw,	&cpu_pll.reg },
	[CLK_CPU_PLL_4X]	= { &cpu_pll_4x.hw,	&cpu_pll_4x.reg },
	[CLK_CPU_4X]		= { &cpu_4x_div1.hw,	&cpu_4x_div1.reg },
	[CLK_CPU_2X]		= { &cpu_4x_div2.hw,	&cpu_4x_div2.reg },
	[CLK_CPU_1X]		= { &cpu_4x_div4.hw,	&cpu_4x_div4.reg },
	[CLK_IO_PLL]		= { &io_pll.hw,		&io_pll.reg },
	[CLK_IO_1000M]		= { &io_1000m.hw,	&io_1000m.reg },
	[CLK_IO_400M]		= { &io_400m.hw,	&io_400m.reg },
	[CLK_IO_25M]		= { &io_25m.hw,		&io_25m.reg },
	[CLK_IO_80M]		= { &io_80m.hw,		&io_80m.reg },
	[CLK_IO_400M_DIV2]	= { &io_400m_div2.hw,	&io_400m_div2.reg },
	[CLK_IO_400M_DIV4]	= { &io_400m_div4.hw,	&io_400m_div4.reg },
	[CLK_IO_400M_DIV8]	= { &io_400m_div8.hw,	&io_400m_div8.reg },
	[CLK_IO_400M_DIV16]	= { &io_400m_div16.hw,	&io_400m_div16.reg },
	[CLK_QSPI]		= { &qspi.divider.hw,	&qspi.divider.reg },
	[CLK_SPI]		= { &spi.divider.hw,	&spi.divider.reg },
	[CLK_SMC]		= { &smc.divider.hw,	&smc.divider.reg },
	[CLK_SDIO]		= { &sdio.hw,		&sdio.reg },
	[CLK_GPIO_DB]		= { &gpio_db.divider.hw, &gpio_db.divider.reg },
	[CLK_EFUSE]		= { &efuse.divider.hw,	&efuse.divider.reg },
	[CLK_TVS]		= { &tvs.divider.hw,	&tvs.divider.reg },
	[CLK_TRNG]		= { &trng.divider.hw,	&trng.divider.reg },
	[CLK_OSC_DIV]		= { &osc_div.divider.hw, &osc_div.divider.reg },
	[CLK_PWM]		= { &pwm.divider.hw,	&pwm.divider.reg },
	[CLK_FCLK0]		= { &fclk0.divider.hw,	&fclk0.divider.reg },
	[CLK_FCLK1]		= { &fclk1.divider.hw,	&fclk1.divider.reg },
	[CLK_FCLK2]		= { &fclk2.divider.hw,	&fclk2.divider.reg },
	[CLK_FCLK3]		= { &fclk3.divider.hw,	&fclk3.divider.reg },
	[CLK_WDT_SEL]		= { &wdt_sel.hw,	&wdt_sel.reg },
	[CLK_EFUSE_SEL]		= { &efuse_sel.hw,	&efuse_sel.reg },
	[CLK_CAN_SEL]		= { &can_sel.hw,	&can_sel.reg },
	[CLK_CPU_SEL]		= { &cpu_sel.hw,	&cpu_sel.reg },
	[CLK_CAN0]		= { &can0.hw,		&can0.reg },
	[CLK_CAN1]		= { &can1.hw,		&can1.reg }
};

static int dr1v90_cru_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *base;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	ret = dr1_cru_clk_register(dev, base, dr1v90_cru_clks,
				   ARRAY_SIZE(dr1v90_cru_clks));
	if (ret)
		return dev_err_probe(dev, ret, "failed to register clocks\n");

	ret = dr1_cru_reset_register(dev, base);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register resets\n");

	return 0;
}

static const struct of_device_id dr1v90_cru_ids[] = {
	{ .compatible = "anlogic,dr1v90-cru" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dr1v90_cru_ids);

static struct platform_driver dr1v90_cru_driver = {
	.driver = {
		.name = "dr1v90-cru",
		.of_match_table = dr1v90_cru_ids,
	},
	.probe = dr1v90_cru_probe,
};
module_platform_driver(dr1v90_cru_driver);

MODULE_AUTHOR("Fushan Zeng <fushan.zeng@anlogic.com>");
MODULE_AUTHOR("Junhui Liu <junhui.liu@pigmoral.tech>");
MODULE_DESCRIPTION("Anlogic DR1V90 CRU driver");
MODULE_IMPORT_NS("CLK_ANLOGIC");
MODULE_LICENSE("GPL");
