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
#include <asm/mach-ralink/pinmux.h>

#include "common.h"

static struct rt2880_pmx_func i2c_func[] =  { FUNC("i2c", 0, 1, 2) };
static struct rt2880_pmx_func spi_func[] = { FUNC("spi", 0, 3, 4) };
static struct rt2880_pmx_func uartf_func[] = {
	FUNC("uartf", RT3883_GPIO_MODE_UARTF, 7, 8),
	FUNC("pcm uartf", RT3883_GPIO_MODE_PCM_UARTF, 7, 8),
	FUNC("pcm i2s", RT3883_GPIO_MODE_PCM_I2S, 7, 8),
	FUNC("i2s uartf", RT3883_GPIO_MODE_I2S_UARTF, 7, 8),
	FUNC("pcm gpio", RT3883_GPIO_MODE_PCM_GPIO, 11, 4),
	FUNC("gpio uartf", RT3883_GPIO_MODE_GPIO_UARTF, 7, 4),
	FUNC("gpio i2s", RT3883_GPIO_MODE_GPIO_I2S, 7, 4),
};
static struct rt2880_pmx_func uartlite_func[] = { FUNC("uartlite", 0, 15, 2) };
static struct rt2880_pmx_func jtag_func[] = { FUNC("jtag", 0, 17, 5) };
static struct rt2880_pmx_func mdio_func[] = { FUNC("mdio", 0, 22, 2) };
static struct rt2880_pmx_func lna_a_func[] = { FUNC("lna a", 0, 32, 3) };
static struct rt2880_pmx_func lna_g_func[] = { FUNC("lna a", 0, 35, 3) };
static struct rt2880_pmx_func pci_func[] = {
	FUNC("pci-dev", 0, 40, 32),
	FUNC("pci-host2", 1, 40, 32),
	FUNC("pci-host1", 2, 40, 32),
	FUNC("pci-fnc", 3, 40, 32)
};
static struct rt2880_pmx_func ge1_func[] = { FUNC("ge1", 0, 72, 12) };
static struct rt2880_pmx_func ge2_func[] = { FUNC("ge1", 0, 84, 12) };

static struct rt2880_pmx_group rt3883_pinmux_data[] = {
	GRP("i2c", i2c_func, 1, RT3883_GPIO_MODE_I2C),
	GRP("spi", spi_func, 1, RT3883_GPIO_MODE_SPI),
	GRP("uartf", uartf_func, RT3883_GPIO_MODE_UART0_MASK,
		RT3883_GPIO_MODE_UART0_SHIFT),
	GRP("uartlite", uartlite_func, 1, RT3883_GPIO_MODE_UART1),
	GRP("jtag", jtag_func, 1, RT3883_GPIO_MODE_JTAG),
	GRP("mdio", mdio_func, 1, RT3883_GPIO_MODE_MDIO),
	GRP("lna a", lna_a_func, 1, RT3883_GPIO_MODE_LNA_A),
	GRP("lna g", lna_g_func, 1, RT3883_GPIO_MODE_LNA_G),
	GRP("pci", pci_func, RT3883_GPIO_MODE_PCI_MASK,
		RT3883_GPIO_MODE_PCI_SHIFT),
	GRP("ge1", ge1_func, 1, RT3883_GPIO_MODE_GE1),
	GRP("ge2", ge2_func, 1, RT3883_GPIO_MODE_GE2),
	{ 0 }
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
	ralink_clk_add("10180000.wmac", 40000000);
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

	rt2880_pinmux_data = rt3883_pinmux_data;

	ralink_soc == RT3883_SOC;
}
