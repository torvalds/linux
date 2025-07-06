// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Arm Ltd.
 * Based on the D1 CCU driver:
 *   Copyright (c) 2020 huangzhenwei@allwinnertech.com
 *   Copyright (C) 2021 Samuel Holland <samuel@sholland.org>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "ccu_common.h"
#include "ccu_reset.h"

#include "ccu_gate.h"
#include "ccu_mp.h"

#include "ccu-sun55i-a523-r.h"

static const struct clk_parent_data r_ahb_apb_parents[] = {
	{ .fw_name = "hosc" },
	{ .fw_name = "losc" },
	{ .fw_name = "iosc" },
	{ .fw_name = "pll-periph" },
	{ .fw_name = "pll-audio" },
};
static SUNXI_CCU_M_DATA_WITH_MUX(r_ahb_clk, "r-ahb",
				 r_ahb_apb_parents, 0x000,
				 0, 5,	/* M */
				 24, 3,	/* mux */
				 0);

static SUNXI_CCU_M_DATA_WITH_MUX(r_apb0_clk, "r-apb0",
				 r_ahb_apb_parents, 0x00c,
				 0, 5,	/* M */
				 24, 3,	/* mux */
				 0);

static SUNXI_CCU_M_DATA_WITH_MUX(r_apb1_clk, "r-apb1",
				 r_ahb_apb_parents, 0x010,
				 0, 5,	/* M */
				 24, 3,	/* mux */
				 0);

static SUNXI_CCU_MP_DATA_WITH_MUX_GATE(r_cpu_timer0, "r-timer0",
				       r_ahb_apb_parents, 0x100,
				       0, 0,	/* no M */
				       1, 3,	/* P */
				       4, 3,	/* mux */
				       BIT(0),
				      0);
static SUNXI_CCU_MP_DATA_WITH_MUX_GATE(r_cpu_timer1, "r-timer1",
				       r_ahb_apb_parents, 0x104,
				       0, 0,	/* no M */
				       1, 3,	/* P */
				       4, 3,	/* mux */
				       BIT(0),
				       0);
static SUNXI_CCU_MP_DATA_WITH_MUX_GATE(r_cpu_timer2, "r-timer2",
				       r_ahb_apb_parents, 0x108,
				       0, 0,	/* no M */
				       1, 3,	/* P */
				       4, 3,	/* mux */
				       BIT(0),
				       0);

static SUNXI_CCU_GATE_HW(bus_r_timer_clk, "bus-r-timer", &r_ahb_clk.common.hw,
			 0x11c, BIT(0), 0);
static SUNXI_CCU_GATE_HW(bus_r_twd_clk,	"bus-r-twd", &r_apb0_clk.common.hw,
			 0x12c, BIT(0), 0);

static const struct clk_parent_data r_pwmctrl_parents[] = {
	{ .fw_name = "hosc" },
	{ .fw_name = "losc" },
	{ .fw_name = "iosc" },
};
static SUNXI_CCU_MUX_DATA_WITH_GATE(r_pwmctrl_clk, "r-pwmctrl",
				  r_pwmctrl_parents, 0x130,
				  24, 2,	/* mux */
				  BIT(31),
				  0);
static SUNXI_CCU_GATE_HW(bus_r_pwmctrl_clk, "bus-r-pwmctrl",
			 &r_apb0_clk.common.hw, 0x13c, BIT(0), 0);

/* SPI clock is /M/N (same as new MMC?) */
static SUNXI_CCU_GATE_HW(bus_r_spi_clk, "bus-r-spi",
			 &r_ahb_clk.common.hw, 0x15c, BIT(0), 0);
static SUNXI_CCU_GATE_HW(bus_r_spinlock_clk, "bus-r-spinlock",
			 &r_ahb_clk.common.hw, 0x16c, BIT(0), 0);
static SUNXI_CCU_GATE_HW(bus_r_msgbox_clk, "bus-r-msgbox",
			 &r_ahb_clk.common.hw, 0x17c, BIT(0), 0);
static SUNXI_CCU_GATE_HW(bus_r_uart0_clk, "bus-r-uart0",
			 &r_apb1_clk.common.hw, 0x18c, BIT(0), 0);
static SUNXI_CCU_GATE_HW(bus_r_uart1_clk, "bus-r-uart1",
			 &r_apb1_clk.common.hw, 0x18c, BIT(1), 0);
static SUNXI_CCU_GATE_HW(bus_r_i2c0_clk, "bus-r-i2c0",
			 &r_apb1_clk.common.hw, 0x19c, BIT(0), 0);
static SUNXI_CCU_GATE_HW(bus_r_i2c1_clk, "bus-r-i2c1",
			 &r_apb1_clk.common.hw, 0x19c, BIT(1), 0);
static SUNXI_CCU_GATE_HW(bus_r_i2c2_clk, "bus-r-i2c2",
			 &r_apb1_clk.common.hw, 0x19c, BIT(2), 0);
static SUNXI_CCU_GATE_HW(bus_r_ppu0_clk, "bus-r-ppu0",
			 &r_apb0_clk.common.hw, 0x1ac, BIT(0), 0);
static SUNXI_CCU_GATE_HW(bus_r_ppu1_clk, "bus-r-ppu1",
			 &r_apb0_clk.common.hw, 0x1ac, BIT(1), 0);
static SUNXI_CCU_GATE_HW(bus_r_cpu_bist_clk, "bus-r-cpu-bist",
			 &r_apb0_clk.common.hw, 0x1bc, BIT(0), 0);

static const struct clk_parent_data r_ir_rx_parents[] = {
	{ .fw_name = "losc" },
	{ .fw_name = "hosc" },
};
static SUNXI_CCU_M_DATA_WITH_MUX_GATE(r_ir_rx_clk, "r-ir-rx",
				      r_ir_rx_parents, 0x1c0,
				      0, 5,	/* M */
				      24, 2,	/* mux */
				      BIT(31),	/* gate */
				      0);
static SUNXI_CCU_GATE_HW(bus_r_ir_rx_clk, "bus-r-ir-rx",
			 &r_apb0_clk.common.hw, 0x1cc, BIT(0), 0);

static SUNXI_CCU_GATE_HW(bus_r_dma_clk, "bus-r-dma",
			 &r_apb0_clk.common.hw, 0x1dc, BIT(0), 0);
static SUNXI_CCU_GATE_HW(bus_r_rtc_clk, "bus-r-rtc",
			 &r_apb0_clk.common.hw, 0x20c, BIT(0), 0);
static SUNXI_CCU_GATE_HW(bus_r_cpucfg_clk, "bus-r-cpucfg",
			 &r_apb0_clk.common.hw, 0x22c, BIT(0), 0);

static struct ccu_common *sun55i_a523_r_ccu_clks[] = {
	&r_ahb_clk.common,
	&r_apb0_clk.common,
	&r_apb1_clk.common,
	&r_cpu_timer0.common,
	&r_cpu_timer1.common,
	&r_cpu_timer2.common,
	&bus_r_timer_clk.common,
	&bus_r_twd_clk.common,
	&r_pwmctrl_clk.common,
	&bus_r_pwmctrl_clk.common,
	&bus_r_spi_clk.common,
	&bus_r_spinlock_clk.common,
	&bus_r_msgbox_clk.common,
	&bus_r_uart0_clk.common,
	&bus_r_uart1_clk.common,
	&bus_r_i2c0_clk.common,
	&bus_r_i2c1_clk.common,
	&bus_r_i2c2_clk.common,
	&bus_r_ppu0_clk.common,
	&bus_r_ppu1_clk.common,
	&bus_r_cpu_bist_clk.common,
	&r_ir_rx_clk.common,
	&bus_r_ir_rx_clk.common,
	&bus_r_dma_clk.common,
	&bus_r_rtc_clk.common,
	&bus_r_cpucfg_clk.common,
};

static struct clk_hw_onecell_data sun55i_a523_r_hw_clks = {
	.num	= CLK_NUMBER,
	.hws	= {
		[CLK_R_AHB]		= &r_ahb_clk.common.hw,
		[CLK_R_APB0]		= &r_apb0_clk.common.hw,
		[CLK_R_APB1]		= &r_apb1_clk.common.hw,
		[CLK_R_TIMER0]		= &r_cpu_timer0.common.hw,
		[CLK_R_TIMER1]		= &r_cpu_timer1.common.hw,
		[CLK_R_TIMER2]		= &r_cpu_timer2.common.hw,
		[CLK_BUS_R_TIMER]	= &bus_r_timer_clk.common.hw,
		[CLK_BUS_R_TWD]		= &bus_r_twd_clk.common.hw,
		[CLK_R_PWMCTRL]		= &r_pwmctrl_clk.common.hw,
		[CLK_BUS_R_PWMCTRL]	= &bus_r_pwmctrl_clk.common.hw,
		[CLK_BUS_R_SPI]		= &bus_r_spi_clk.common.hw,
		[CLK_BUS_R_SPINLOCK]	= &bus_r_spinlock_clk.common.hw,
		[CLK_BUS_R_MSGBOX]	= &bus_r_msgbox_clk.common.hw,
		[CLK_BUS_R_UART0]	= &bus_r_uart0_clk.common.hw,
		[CLK_BUS_R_UART1]	= &bus_r_uart1_clk.common.hw,
		[CLK_BUS_R_I2C0]	= &bus_r_i2c0_clk.common.hw,
		[CLK_BUS_R_I2C1]	= &bus_r_i2c1_clk.common.hw,
		[CLK_BUS_R_I2C2]	= &bus_r_i2c2_clk.common.hw,
		[CLK_BUS_R_PPU0]	= &bus_r_ppu0_clk.common.hw,
		[CLK_BUS_R_PPU1]	= &bus_r_ppu1_clk.common.hw,
		[CLK_BUS_R_CPU_BIST]	= &bus_r_cpu_bist_clk.common.hw,
		[CLK_R_IR_RX]		= &r_ir_rx_clk.common.hw,
		[CLK_BUS_R_IR_RX]	= &bus_r_ir_rx_clk.common.hw,
		[CLK_BUS_R_DMA]		= &bus_r_dma_clk.common.hw,
		[CLK_BUS_R_RTC]		= &bus_r_rtc_clk.common.hw,
		[CLK_BUS_R_CPUCFG]	= &bus_r_cpucfg_clk.common.hw,
	},
};

static struct ccu_reset_map sun55i_a523_r_ccu_resets[] = {
	[RST_BUS_R_TIMER]	= { 0x11c, BIT(16) },
	[RST_BUS_R_TWD]		= { 0x12c, BIT(16) },
	[RST_BUS_R_PWMCTRL]	= { 0x13c, BIT(16) },
	[RST_BUS_R_SPI]		= { 0x15c, BIT(16) },
	[RST_BUS_R_SPINLOCK]	= { 0x16c, BIT(16) },
	[RST_BUS_R_MSGBOX]	= { 0x17c, BIT(16) },
	[RST_BUS_R_UART0]	= { 0x18c, BIT(16) },
	[RST_BUS_R_UART1]	= { 0x18c, BIT(17) },
	[RST_BUS_R_I2C0]	= { 0x19c, BIT(16) },
	[RST_BUS_R_I2C1]	= { 0x19c, BIT(17) },
	[RST_BUS_R_I2C2]	= { 0x19c, BIT(18) },
	[RST_BUS_R_PPU1]	= { 0x1ac, BIT(17) },
	[RST_BUS_R_IR_RX]	= { 0x1cc, BIT(16) },
	[RST_BUS_R_RTC]		= { 0x20c, BIT(16) },
	[RST_BUS_R_CPUCFG]	= { 0x22c, BIT(16) },
};

static const struct sunxi_ccu_desc sun55i_a523_r_ccu_desc = {
	.ccu_clks	= sun55i_a523_r_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun55i_a523_r_ccu_clks),

	.hw_clks	= &sun55i_a523_r_hw_clks,

	.resets		= sun55i_a523_r_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun55i_a523_r_ccu_resets),
};

static int sun55i_a523_r_ccu_probe(struct platform_device *pdev)
{
	void __iomem *reg;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	return devm_sunxi_ccu_probe(&pdev->dev, reg, &sun55i_a523_r_ccu_desc);
}

static const struct of_device_id sun55i_a523_r_ccu_ids[] = {
	{ .compatible = "allwinner,sun55i-a523-r-ccu" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun55i_a523_r_ccu_ids);

static struct platform_driver sun55i_a523_r_ccu_driver = {
	.probe	= sun55i_a523_r_ccu_probe,
	.driver	= {
		.name			= "sun55i-a523-r-ccu",
		.suppress_bind_attrs	= true,
		.of_match_table		= sun55i_a523_r_ccu_ids,
	},
};
module_platform_driver(sun55i_a523_r_ccu_driver);

MODULE_IMPORT_NS("SUNXI_CCU");
MODULE_DESCRIPTION("Support for the Allwinner A523 PRCM CCU");
MODULE_LICENSE("GPL");
