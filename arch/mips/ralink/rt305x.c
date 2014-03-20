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
#include <asm/mach-ralink/rt305x.h>

#include "common.h"

enum rt305x_soc_type rt305x_soc;

static struct ralink_pinmux_grp mode_mux[] = {
	{
		.name = "i2c",
		.mask = RT305X_GPIO_MODE_I2C,
		.gpio_first = RT305X_GPIO_I2C_SD,
		.gpio_last = RT305X_GPIO_I2C_SCLK,
	}, {
		.name = "spi",
		.mask = RT305X_GPIO_MODE_SPI,
		.gpio_first = RT305X_GPIO_SPI_EN,
		.gpio_last = RT305X_GPIO_SPI_CLK,
	}, {
		.name = "uartlite",
		.mask = RT305X_GPIO_MODE_UART1,
		.gpio_first = RT305X_GPIO_UART1_TXD,
		.gpio_last = RT305X_GPIO_UART1_RXD,
	}, {
		.name = "jtag",
		.mask = RT305X_GPIO_MODE_JTAG,
		.gpio_first = RT305X_GPIO_JTAG_TDO,
		.gpio_last = RT305X_GPIO_JTAG_TDI,
	}, {
		.name = "mdio",
		.mask = RT305X_GPIO_MODE_MDIO,
		.gpio_first = RT305X_GPIO_MDIO_MDC,
		.gpio_last = RT305X_GPIO_MDIO_MDIO,
	}, {
		.name = "sdram",
		.mask = RT305X_GPIO_MODE_SDRAM,
		.gpio_first = RT305X_GPIO_SDRAM_MD16,
		.gpio_last = RT305X_GPIO_SDRAM_MD31,
	}, {
		.name = "rgmii",
		.mask = RT305X_GPIO_MODE_RGMII,
		.gpio_first = RT305X_GPIO_GE0_TXD0,
		.gpio_last = RT305X_GPIO_GE0_RXCLK,
	}, {0}
};

static struct ralink_pinmux_grp uart_mux[] = {
	{
		.name = "uartf",
		.mask = RT305X_GPIO_MODE_UARTF,
		.gpio_first = RT305X_GPIO_7,
		.gpio_last = RT305X_GPIO_14,
	}, {
		.name = "pcm uartf",
		.mask = RT305X_GPIO_MODE_PCM_UARTF,
		.gpio_first = RT305X_GPIO_7,
		.gpio_last = RT305X_GPIO_14,
	}, {
		.name = "pcm i2s",
		.mask = RT305X_GPIO_MODE_PCM_I2S,
		.gpio_first = RT305X_GPIO_7,
		.gpio_last = RT305X_GPIO_14,
	}, {
		.name = "i2s uartf",
		.mask = RT305X_GPIO_MODE_I2S_UARTF,
		.gpio_first = RT305X_GPIO_7,
		.gpio_last = RT305X_GPIO_14,
	}, {
		.name = "pcm gpio",
		.mask = RT305X_GPIO_MODE_PCM_GPIO,
		.gpio_first = RT305X_GPIO_10,
		.gpio_last = RT305X_GPIO_14,
	}, {
		.name = "gpio uartf",
		.mask = RT305X_GPIO_MODE_GPIO_UARTF,
		.gpio_first = RT305X_GPIO_7,
		.gpio_last = RT305X_GPIO_10,
	}, {
		.name = "gpio i2s",
		.mask = RT305X_GPIO_MODE_GPIO_I2S,
		.gpio_first = RT305X_GPIO_7,
		.gpio_last = RT305X_GPIO_10,
	}, {
		.name = "gpio",
		.mask = RT305X_GPIO_MODE_GPIO,
	}, {0}
};

static void rt305x_wdt_reset(void)
{
	u32 t;

	/* enable WDT reset output on pin SRAM_CS_N */
	t = rt_sysc_r32(SYSC_REG_SYSTEM_CONFIG);
	t |= RT305X_SYSCFG_SRAM_CS0_MODE_WDT <<
		RT305X_SYSCFG_SRAM_CS0_MODE_SHIFT;
	rt_sysc_w32(t, SYSC_REG_SYSTEM_CONFIG);
}

struct ralink_pinmux rt_gpio_pinmux = {
	.mode = mode_mux,
	.uart = uart_mux,
	.uart_shift = RT305X_GPIO_MODE_UART0_SHIFT,
	.uart_mask = RT305X_GPIO_MODE_UART0_MASK,
	.wdt_reset = rt305x_wdt_reset,
};

static unsigned long rt5350_get_mem_size(void)
{
	void __iomem *sysc = (void __iomem *) KSEG1ADDR(RT305X_SYSC_BASE);
	unsigned long ret;
	u32 t;

	t = __raw_readl(sysc + SYSC_REG_SYSTEM_CONFIG);
	t = (t >> RT5350_SYSCFG0_DRAM_SIZE_SHIFT) &
		RT5350_SYSCFG0_DRAM_SIZE_MASK;

	switch (t) {
	case RT5350_SYSCFG0_DRAM_SIZE_2M:
		ret = 2;
		break;
	case RT5350_SYSCFG0_DRAM_SIZE_8M:
		ret = 8;
		break;
	case RT5350_SYSCFG0_DRAM_SIZE_16M:
		ret = 16;
		break;
	case RT5350_SYSCFG0_DRAM_SIZE_32M:
		ret = 32;
		break;
	case RT5350_SYSCFG0_DRAM_SIZE_64M:
		ret = 64;
		break;
	default:
		panic("rt5350: invalid DRAM size: %u", t);
		break;
	}

	return ret;
}

void __init ralink_clk_init(void)
{
	unsigned long cpu_rate, sys_rate, wdt_rate, uart_rate;
	unsigned long wmac_rate = 40000000;

	u32 t = rt_sysc_r32(SYSC_REG_SYSTEM_CONFIG);

	if (soc_is_rt305x() || soc_is_rt3350()) {
		t = (t >> RT305X_SYSCFG_CPUCLK_SHIFT) &
		     RT305X_SYSCFG_CPUCLK_MASK;
		switch (t) {
		case RT305X_SYSCFG_CPUCLK_LOW:
			cpu_rate = 320000000;
			break;
		case RT305X_SYSCFG_CPUCLK_HIGH:
			cpu_rate = 384000000;
			break;
		}
		sys_rate = uart_rate = wdt_rate = cpu_rate / 3;
	} else if (soc_is_rt3352()) {
		t = (t >> RT3352_SYSCFG0_CPUCLK_SHIFT) &
		     RT3352_SYSCFG0_CPUCLK_MASK;
		switch (t) {
		case RT3352_SYSCFG0_CPUCLK_LOW:
			cpu_rate = 384000000;
			break;
		case RT3352_SYSCFG0_CPUCLK_HIGH:
			cpu_rate = 400000000;
			break;
		}
		sys_rate = wdt_rate = cpu_rate / 3;
		uart_rate = 40000000;
	} else if (soc_is_rt5350()) {
		t = (t >> RT5350_SYSCFG0_CPUCLK_SHIFT) &
		     RT5350_SYSCFG0_CPUCLK_MASK;
		switch (t) {
		case RT5350_SYSCFG0_CPUCLK_360:
			cpu_rate = 360000000;
			sys_rate = cpu_rate / 3;
			break;
		case RT5350_SYSCFG0_CPUCLK_320:
			cpu_rate = 320000000;
			sys_rate = cpu_rate / 4;
			break;
		case RT5350_SYSCFG0_CPUCLK_300:
			cpu_rate = 300000000;
			sys_rate = cpu_rate / 3;
			break;
		default:
			BUG();
		}
		uart_rate = 40000000;
		wdt_rate = sys_rate;
	} else {
		BUG();
	}

	if (soc_is_rt3352() || soc_is_rt5350()) {
		u32 val = rt_sysc_r32(RT3352_SYSC_REG_SYSCFG0);

		if (!(val & RT3352_CLKCFG0_XTAL_SEL))
			wmac_rate = 20000000;
	}

	ralink_clk_add("cpu", cpu_rate);
	ralink_clk_add("10000b00.spi", sys_rate);
	ralink_clk_add("10000100.timer", wdt_rate);
	ralink_clk_add("10000120.watchdog", wdt_rate);
	ralink_clk_add("10000500.uart", uart_rate);
	ralink_clk_add("10000c00.uartlite", uart_rate);
	ralink_clk_add("10100000.ethernet", sys_rate);
	ralink_clk_add("10180000.wmac", wmac_rate);
}

void __init ralink_of_remap(void)
{
	rt_sysc_membase = plat_of_remap_node("ralink,rt3050-sysc");
	rt_memc_membase = plat_of_remap_node("ralink,rt3050-memc");

	if (!rt_sysc_membase || !rt_memc_membase)
		panic("Failed to remap core resources");
}

void prom_soc_init(struct ralink_soc_info *soc_info)
{
	void __iomem *sysc = (void __iomem *) KSEG1ADDR(RT305X_SYSC_BASE);
	unsigned char *name;
	u32 n0;
	u32 n1;
	u32 id;

	n0 = __raw_readl(sysc + SYSC_REG_CHIP_NAME0);
	n1 = __raw_readl(sysc + SYSC_REG_CHIP_NAME1);

	if (n0 == RT3052_CHIP_NAME0 && n1 == RT3052_CHIP_NAME1) {
		unsigned long icache_sets;

		icache_sets = (read_c0_config1() >> 22) & 7;
		if (icache_sets == 1) {
			rt305x_soc = RT305X_SOC_RT3050;
			name = "RT3050";
			soc_info->compatible = "ralink,rt3050-soc";
		} else {
			rt305x_soc = RT305X_SOC_RT3052;
			name = "RT3052";
			soc_info->compatible = "ralink,rt3052-soc";
		}
	} else if (n0 == RT3350_CHIP_NAME0 && n1 == RT3350_CHIP_NAME1) {
		rt305x_soc = RT305X_SOC_RT3350;
		name = "RT3350";
		soc_info->compatible = "ralink,rt3350-soc";
	} else if (n0 == RT3352_CHIP_NAME0 && n1 == RT3352_CHIP_NAME1) {
		rt305x_soc = RT305X_SOC_RT3352;
		name = "RT3352";
		soc_info->compatible = "ralink,rt3352-soc";
	} else if (n0 == RT5350_CHIP_NAME0 && n1 == RT5350_CHIP_NAME1) {
		rt305x_soc = RT305X_SOC_RT5350;
		name = "RT5350";
		soc_info->compatible = "ralink,rt5350-soc";
	} else {
		panic("rt305x: unknown SoC, n0:%08x n1:%08x", n0, n1);
	}

	id = __raw_readl(sysc + SYSC_REG_CHIP_ID);

	snprintf(soc_info->sys_type, RAMIPS_SYS_TYPE_LEN,
		"Ralink %s id:%u rev:%u",
		name,
		(id >> CHIP_ID_ID_SHIFT) & CHIP_ID_ID_MASK,
		(id & CHIP_ID_REV_MASK));

	soc_info->mem_base = RT305X_SDRAM_BASE;
	if (soc_is_rt5350()) {
		soc_info->mem_size = rt5350_get_mem_size();
	} else if (soc_is_rt305x() || soc_is_rt3350()) {
		soc_info->mem_size_min = RT305X_MEM_SIZE_MIN;
		soc_info->mem_size_max = RT305X_MEM_SIZE_MAX;
	} else if (soc_is_rt3352()) {
		soc_info->mem_size_min = RT3352_MEM_SIZE_MIN;
		soc_info->mem_size_max = RT3352_MEM_SIZE_MAX;
	}
}
