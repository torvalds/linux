/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Parts of this file are based on Ralink's 2.6.21 BSP
 *
 * Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/mipsregs.h>
#include <asm/mach-ralink/ralink_regs.h>
#include <asm/mach-ralink/rt3883.h>

#include "common.h"

static struct ralink_pinmux_grp mode_mux[] = {
	{
		.name = "i2c",
		.mask = RT3883_GPIO_MODE_I2C,
		.gpio_first = RT3883_GPIO_I2C_SD,
		.gpio_last = RT3883_GPIO_I2C_SCLK,
	}, {
		.name = "spi",
		.mask = RT3883_GPIO_MODE_SPI,
		.gpio_first = RT3883_GPIO_SPI_CS0,
		.gpio_last = RT3883_GPIO_SPI_MISO,
	}, {
		.name = "uartlite",
		.mask = RT3883_GPIO_MODE_UART1,
		.gpio_first = RT3883_GPIO_UART1_TXD,
		.gpio_last = RT3883_GPIO_UART1_RXD,
	}, {
		.name = "jtag",
		.mask = RT3883_GPIO_MODE_JTAG,
		.gpio_first = RT3883_GPIO_JTAG_TDO,
		.gpio_last = RT3883_GPIO_JTAG_TCLK,
	}, {
		.name = "mdio",
		.mask = RT3883_GPIO_MODE_MDIO,
		.gpio_first = RT3883_GPIO_MDIO_MDC,
		.gpio_last = RT3883_GPIO_MDIO_MDIO,
	}, {
		.name = "ge1",
		.mask = RT3883_GPIO_MODE_GE1,
		.gpio_first = RT3883_GPIO_GE1_TXD0,
		.gpio_last = RT3883_GPIO_GE1_RXCLK,
	}, {
		.name = "ge2",
		.mask = RT3883_GPIO_MODE_GE2,
		.gpio_first = RT3883_GPIO_GE2_TXD0,
		.gpio_last = RT3883_GPIO_GE2_RXCLK,
	}, {
		.name = "pci",
		.mask = RT3883_GPIO_MODE_PCI,
		.gpio_first = RT3883_GPIO_PCI_AD0,
		.gpio_last = RT3883_GPIO_PCI_AD31,
	}, {
		.name = "lna a",
		.mask = RT3883_GPIO_MODE_LNA_A,
		.gpio_first = RT3883_GPIO_LNA_PE_A0,
		.gpio_last = RT3883_GPIO_LNA_PE_A2,
	}, {
		.name = "lna g",
		.mask = RT3883_GPIO_MODE_LNA_G,
		.gpio_first = RT3883_GPIO_LNA_PE_G0,
		.gpio_last = RT3883_GPIO_LNA_PE_G2,
	}, {0}
};

static struct ralink_pinmux_grp uart_mux[] = {
	{
		.name = "uartf",
		.mask = RT3883_GPIO_MODE_UARTF,
		.gpio_first = RT3883_GPIO_7,
		.gpio_last = RT3883_GPIO_14,
	}, {
		.name = "pcm uartf",
		.mask = RT3883_GPIO_MODE_PCM_UARTF,
		.gpio_first = RT3883_GPIO_7,
		.gpio_last = RT3883_GPIO_14,
	}, {
		.name = "pcm i2s",
		.mask = RT3883_GPIO_MODE_PCM_I2S,
		.gpio_first = RT3883_GPIO_7,
		.gpio_last = RT3883_GPIO_14,
	}, {
		.name = "i2s uartf",
		.mask = RT3883_GPIO_MODE_I2S_UARTF,
		.gpio_first = RT3883_GPIO_7,
		.gpio_last = RT3883_GPIO_14,
	}, {
		.name = "pcm gpio",
		.mask = RT3883_GPIO_MODE_PCM_GPIO,
		.gpio_first = RT3883_GPIO_11,
		.gpio_last = RT3883_GPIO_14,
	}, {
		.name = "gpio uartf",
		.mask = RT3883_GPIO_MODE_GPIO_UARTF,
		.gpio_first = RT3883_GPIO_7,
		.gpio_last = RT3883_GPIO_10,
	}, {
		.name = "gpio i2s",
		.mask = RT3883_GPIO_MODE_GPIO_I2S,
		.gpio_first = RT3883_GPIO_7,
		.gpio_last = RT3883_GPIO_10,
	}, {
		.name = "gpio",
		.mask = RT3883_GPIO_MODE_GPIO,
	}, {0}
};

static struct ralink_pinmux_grp pci_mux[] = {
	{
		.name = "pci-dev",
		.mask = 0,
		.gpio_first = RT3883_GPIO_PCI_AD0,
		.gpio_last = RT3883_GPIO_PCI_AD31,
	}, {
		.name = "pci-host2",
		.mask = 1,
		.gpio_first = RT3883_GPIO_PCI_AD0,
		.gpio_last = RT3883_GPIO_PCI_AD31,
	}, {
		.name = "pci-host1",
		.mask = 2,
		.gpio_first = RT3883_GPIO_PCI_AD0,
		.gpio_last = RT3883_GPIO_PCI_AD31,
	}, {
		.name = "pci-fnc",
		.mask = 3,
		.gpio_first = RT3883_GPIO_PCI_AD0,
		.gpio_last = RT3883_GPIO_PCI_AD31,
	}, {
		.name = "pci-gpio",
		.mask = 7,
		.gpio_first = RT3883_GPIO_PCI_AD0,
		.gpio_last = RT3883_GPIO_PCI_AD31,
	}, {0}
};

static void rt3883_wdt_reset(void)
{
	u32 t;

	/* enable WDT reset output on GPIO 2 */
	t = rt_sysc_r32(RT3883_SYSC_REG_SYSCFG1);
	t |= RT3883_SYSCFG1_GPIO2_AS_WDT_OUT;
	rt_sysc_w32(t, RT3883_SYSC_REG_SYSCFG1);
}

struct ralink_pinmux rt_gpio_pinmux = {
	.mode = mode_mux,
	.uart = uart_mux,
	.uart_shift = RT3883_GPIO_MODE_UART0_SHIFT,
	.uart_mask = RT3883_GPIO_MODE_UART0_MASK,
	.wdt_reset = rt3883_wdt_reset,
	.pci = pci_mux,
	.pci_shift = RT3883_GPIO_MODE_PCI_SHIFT,
	.pci_mask = RT3883_GPIO_MODE_PCI_MASK,
};

void __init ralink_clk_init(void)
{
	unsigned long cpu_rate, sys_rate;
	u32 syscfg0;
	u32 clksel;
	u32 ddr2;

	syscfg0 = rt_sysc_r32(RT3883_SYSC_REG_SYSCFG0);
	clksel = ((syscfg0 >> RT3883_SYSCFG0_CPUCLK_SHIFT) &
		RT3883_SYSCFG0_CPUCLK_MASK);
	ddr2 = syscfg0 & RT3883_SYSCFG0_DRAM_TYPE_DDR2;

	switch (clksel) {
	case RT3883_SYSCFG0_CPUCLK_250:
		cpu_rate = 250000000;
		sys_rate = (ddr2) ? 125000000 : 83000000;
		break;
	case RT3883_SYSCFG0_CPUCLK_384:
		cpu_rate = 384000000;
		sys_rate = (ddr2) ? 128000000 : 96000000;
		break;
	case RT3883_SYSCFG0_CPUCLK_480:
		cpu_rate = 480000000;
		sys_rate = (ddr2) ? 160000000 : 120000000;
		break;
	case RT3883_SYSCFG0_CPUCLK_500:
		cpu_rate = 500000000;
		sys_rate = (ddr2) ? 166000000 : 125000000;
		break;
	}

	ralink_clk_add("cpu", cpu_rate);
	ralink_clk_add("10000100.timer", sys_rate);
	ralink_clk_add("10000120.watchdog", sys_rate);
	ralink_clk_add("10000500.uart", 40000000);
	ralink_clk_add("10000b00.spi", sys_rate);
	ralink_clk_add("10000c00.uartlite", 40000000);
	ralink_clk_add("10100000.ethernet", sys_rate);
}

void __init ralink_of_remap(void)
{
	rt_sysc_membase = plat_of_remap_node("ralink,rt3883-sysc");
	rt_memc_membase = plat_of_remap_node("ralink,rt3883-memc");

	if (!rt_sysc_membase || !rt_memc_membase)
		panic("Failed to remap core resources");
}

void prom_soc_init(struct ralink_soc_info *soc_info)
{
	void __iomem *sysc = (void __iomem *) KSEG1ADDR(RT3883_SYSC_BASE);
	const char *name;
	u32 n0;
	u32 n1;
	u32 id;

	n0 = __raw_readl(sysc + RT3883_SYSC_REG_CHIPID0_3);
	n1 = __raw_readl(sysc + RT3883_SYSC_REG_CHIPID4_7);
	id = __raw_readl(sysc + RT3883_SYSC_REG_REVID);

	if (n0 == RT3883_CHIP_NAME0 && n1 == RT3883_CHIP_NAME1) {
		soc_info->compatible = "ralink,rt3883-soc";
		name = "RT3883";
	} else {
		panic("rt3883: unknown SoC, n0:%08x n1:%08x", n0, n1);
	}

	snprintf(soc_info->sys_type, RAMIPS_SYS_TYPE_LEN,
		"Ralink %s ver:%u eco:%u",
		name,
		(id >> RT3883_REVID_VER_ID_SHIFT) & RT3883_REVID_VER_ID_MASK,
		(id & RT3883_REVID_ECO_ID_MASK));

	soc_info->mem_base = RT3883_SDRAM_BASE;
	soc_info->mem_size_min = RT3883_MEM_SIZE_MIN;
	soc_info->mem_size_max = RT3883_MEM_SIZE_MAX;
}
