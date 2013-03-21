/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Parts of this file are based on Ralink's 2.6.21 BSP
 *
 * Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/mipsregs.h>
#include <asm/mach-ralink/ralink_regs.h>
#include <asm/mach-ralink/mt7620.h>

#include "common.h"

/* does the board have sdram or ddram */
static int dram_type;

/* the pll dividers */
static u32 mt7620_clk_divider[] = { 2, 3, 4, 8 };

static struct ralink_pinmux_grp mode_mux[] = {
	{
		.name = "i2c",
		.mask = MT7620_GPIO_MODE_I2C,
		.gpio_first = 1,
		.gpio_last = 2,
	}, {
		.name = "spi",
		.mask = MT7620_GPIO_MODE_SPI,
		.gpio_first = 3,
		.gpio_last = 6,
	}, {
		.name = "uartlite",
		.mask = MT7620_GPIO_MODE_UART1,
		.gpio_first = 15,
		.gpio_last = 16,
	}, {
		.name = "wdt",
		.mask = MT7620_GPIO_MODE_WDT,
		.gpio_first = 17,
		.gpio_last = 17,
	}, {
		.name = "mdio",
		.mask = MT7620_GPIO_MODE_MDIO,
		.gpio_first = 22,
		.gpio_last = 23,
	}, {
		.name = "rgmii1",
		.mask = MT7620_GPIO_MODE_RGMII1,
		.gpio_first = 24,
		.gpio_last = 35,
	}, {
		.name = "spi refclk",
		.mask = MT7620_GPIO_MODE_SPI_REF_CLK,
		.gpio_first = 37,
		.gpio_last = 39,
	}, {
		.name = "jtag",
		.mask = MT7620_GPIO_MODE_JTAG,
		.gpio_first = 40,
		.gpio_last = 44,
	}, {
		/* shared lines with jtag */
		.name = "ephy",
		.mask = MT7620_GPIO_MODE_EPHY,
		.gpio_first = 40,
		.gpio_last = 44,
	}, {
		.name = "nand",
		.mask = MT7620_GPIO_MODE_JTAG,
		.gpio_first = 45,
		.gpio_last = 59,
	}, {
		.name = "rgmii2",
		.mask = MT7620_GPIO_MODE_RGMII2,
		.gpio_first = 60,
		.gpio_last = 71,
	}, {
		.name = "wled",
		.mask = MT7620_GPIO_MODE_WLED,
		.gpio_first = 72,
		.gpio_last = 72,
	}, {0}
};

static struct ralink_pinmux_grp uart_mux[] = {
	{
		.name = "uartf",
		.mask = MT7620_GPIO_MODE_UARTF,
		.gpio_first = 7,
		.gpio_last = 14,
	}, {
		.name = "pcm uartf",
		.mask = MT7620_GPIO_MODE_PCM_UARTF,
		.gpio_first = 7,
		.gpio_last = 14,
	}, {
		.name = "pcm i2s",
		.mask = MT7620_GPIO_MODE_PCM_I2S,
		.gpio_first = 7,
		.gpio_last = 14,
	}, {
		.name = "i2s uartf",
		.mask = MT7620_GPIO_MODE_I2S_UARTF,
		.gpio_first = 7,
		.gpio_last = 14,
	}, {
		.name = "pcm gpio",
		.mask = MT7620_GPIO_MODE_PCM_GPIO,
		.gpio_first = 11,
		.gpio_last = 14,
	}, {
		.name = "gpio uartf",
		.mask = MT7620_GPIO_MODE_GPIO_UARTF,
		.gpio_first = 7,
		.gpio_last = 10,
	}, {
		.name = "gpio i2s",
		.mask = MT7620_GPIO_MODE_GPIO_I2S,
		.gpio_first = 7,
		.gpio_last = 10,
	}, {
		.name = "gpio",
		.mask = MT7620_GPIO_MODE_GPIO,
	}, {0}
};

struct ralink_pinmux rt_gpio_pinmux = {
	.mode = mode_mux,
	.uart = uart_mux,
	.uart_shift = MT7620_GPIO_MODE_UART0_SHIFT,
	.uart_mask = MT7620_GPIO_MODE_UART0_MASK,
};

void __init ralink_clk_init(void)
{
	unsigned long cpu_rate, sys_rate;
	u32 c0 = rt_sysc_r32(SYSC_REG_CPLL_CONFIG0);
	u32 c1 = rt_sysc_r32(SYSC_REG_CPLL_CONFIG1);
	u32 swconfig = (c0 >> CPLL_SW_CONFIG_SHIFT) & CPLL_SW_CONFIG_MASK;
	u32 cpu_clk = (c1 >> CPLL_CPU_CLK_SHIFT) & CPLL_CPU_CLK_MASK;

	if (cpu_clk) {
		cpu_rate = 480000000;
	} else if (!swconfig) {
		cpu_rate = 600000000;
	} else {
		u32 m = (c0 >> CPLL_MULT_RATIO_SHIFT) & CPLL_MULT_RATIO;
		u32 d = (c0 >> CPLL_DIV_RATIO_SHIFT) & CPLL_DIV_RATIO;

		cpu_rate = ((40 * (m + 24)) / mt7620_clk_divider[d]) * 1000000;
	}

	if (dram_type == SYSCFG0_DRAM_TYPE_SDRAM)
		sys_rate = cpu_rate / 4;
	else
		sys_rate = cpu_rate / 3;

	ralink_clk_add("cpu", cpu_rate);
	ralink_clk_add("10000100.timer", 40000000);
	ralink_clk_add("10000500.uart", 40000000);
	ralink_clk_add("10000c00.uartlite", 40000000);
}

void __init ralink_of_remap(void)
{
	rt_sysc_membase = plat_of_remap_node("ralink,mt7620a-sysc");
	rt_memc_membase = plat_of_remap_node("ralink,mt7620a-memc");

	if (!rt_sysc_membase || !rt_memc_membase)
		panic("Failed to remap core resources");
}

void prom_soc_init(struct ralink_soc_info *soc_info)
{
	void __iomem *sysc = (void __iomem *) KSEG1ADDR(MT7620_SYSC_BASE);
	unsigned char *name = NULL;
	u32 n0;
	u32 n1;
	u32 rev;
	u32 cfg0;

	n0 = __raw_readl(sysc + SYSC_REG_CHIP_NAME0);
	n1 = __raw_readl(sysc + SYSC_REG_CHIP_NAME1);

	if (n0 == MT7620N_CHIP_NAME0 && n1 == MT7620N_CHIP_NAME1) {
		name = "MT7620N";
		soc_info->compatible = "ralink,mt7620n-soc";
	} else if (n0 == MT7620A_CHIP_NAME0 && n1 == MT7620A_CHIP_NAME1) {
		name = "MT7620A";
		soc_info->compatible = "ralink,mt7620a-soc";
	} else {
		panic("mt7620: unknown SoC, n0:%08x n1:%08x\n", n0, n1);
	}

	rev = __raw_readl(sysc + SYSC_REG_CHIP_REV);

	snprintf(soc_info->sys_type, RAMIPS_SYS_TYPE_LEN,
		"Ralink %s ver:%u eco:%u",
		name,
		(rev >> CHIP_REV_VER_SHIFT) & CHIP_REV_VER_MASK,
		(rev & CHIP_REV_ECO_MASK));

	cfg0 = __raw_readl(sysc + SYSC_REG_SYSTEM_CONFIG0);
	dram_type = (cfg0 >> SYSCFG0_DRAM_TYPE_SHIFT) & SYSCFG0_DRAM_TYPE_MASK;
}
