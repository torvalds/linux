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

static __init u32
mt7620_calc_rate(u32 ref_rate, u32 mul, u32 div)
{
	u64 t;

	t = ref_rate;
	t *= mul;
	do_div(t, div);

	return t;
}

#define MHZ(x)		((x) * 1000 * 1000)

static __init unsigned long
mt7620_get_xtal_rate(void)
{
	u32 reg;

	reg = rt_sysc_r32(SYSC_REG_SYSTEM_CONFIG0);
	if (reg & SYSCFG0_XTAL_FREQ_SEL)
		return MHZ(40);

	return MHZ(20);
}

static __init unsigned long
mt7620_get_periph_rate(unsigned long xtal_rate)
{
	u32 reg;

	reg = rt_sysc_r32(SYSC_REG_CLKCFG0);
	if (reg & CLKCFG0_PERI_CLK_SEL)
		return xtal_rate;

	return MHZ(40);
}

static const u32 mt7620_clk_divider[] __initconst = { 2, 3, 4, 8 };

static __init unsigned long
mt7620_get_cpu_pll_rate(unsigned long xtal_rate)
{
	u32 reg;
	u32 mul;
	u32 div;

	reg = rt_sysc_r32(SYSC_REG_CPLL_CONFIG0);
	if (reg & CPLL_CFG0_BYPASS_REF_CLK)
		return xtal_rate;

	if ((reg & CPLL_CFG0_SW_CFG) == 0)
		return MHZ(600);

	mul = (reg >> CPLL_CFG0_PLL_MULT_RATIO_SHIFT) &
	      CPLL_CFG0_PLL_MULT_RATIO_MASK;
	mul += 24;
	if (reg & CPLL_CFG0_LC_CURFCK)
		mul *= 2;

	div = (reg >> CPLL_CFG0_PLL_DIV_RATIO_SHIFT) &
	      CPLL_CFG0_PLL_DIV_RATIO_MASK;

	WARN_ON(div >= ARRAY_SIZE(mt7620_clk_divider));

	return mt7620_calc_rate(xtal_rate, mul, mt7620_clk_divider[div]);
}

static __init unsigned long
mt7620_get_pll_rate(unsigned long xtal_rate, unsigned long cpu_pll_rate)
{
	u32 reg;

	reg = rt_sysc_r32(SYSC_REG_CPLL_CONFIG1);
	if (reg & CPLL_CFG1_CPU_AUX1)
		return xtal_rate;

	if (reg & CPLL_CFG1_CPU_AUX0)
		return MHZ(480);

	return cpu_pll_rate;
}

static __init unsigned long
mt7620_get_cpu_rate(unsigned long pll_rate)
{
	u32 reg;
	u32 mul;
	u32 div;

	reg = rt_sysc_r32(SYSC_REG_CPU_SYS_CLKCFG);

	mul = reg & CPU_SYS_CLKCFG_CPU_FFRAC_MASK;
	div = (reg >> CPU_SYS_CLKCFG_CPU_FDIV_SHIFT) &
	      CPU_SYS_CLKCFG_CPU_FDIV_MASK;

	return mt7620_calc_rate(pll_rate, mul, div);
}

static const u32 mt7620_ocp_dividers[16] __initconst = {
	[CPU_SYS_CLKCFG_OCP_RATIO_2] = 2,
	[CPU_SYS_CLKCFG_OCP_RATIO_3] = 3,
	[CPU_SYS_CLKCFG_OCP_RATIO_4] = 4,
	[CPU_SYS_CLKCFG_OCP_RATIO_5] = 5,
	[CPU_SYS_CLKCFG_OCP_RATIO_10] = 10,
};

static __init unsigned long
mt7620_get_dram_rate(unsigned long pll_rate)
{
	if (dram_type == SYSCFG0_DRAM_TYPE_SDRAM)
		return pll_rate / 4;

	return pll_rate / 3;
}

static __init unsigned long
mt7620_get_sys_rate(unsigned long cpu_rate)
{
	u32 reg;
	u32 ocp_ratio;
	u32 div;

	reg = rt_sysc_r32(SYSC_REG_CPU_SYS_CLKCFG);

	ocp_ratio = (reg >> CPU_SYS_CLKCFG_OCP_RATIO_SHIFT) &
		    CPU_SYS_CLKCFG_OCP_RATIO_MASK;

	if (WARN_ON(ocp_ratio >= ARRAY_SIZE(mt7620_ocp_dividers)))
		return cpu_rate;

	div = mt7620_ocp_dividers[ocp_ratio];
	if (WARN(!div, "invalid divider for OCP ratio %u", ocp_ratio))
		return cpu_rate;

	return cpu_rate / div;
}

void __init ralink_clk_init(void)
{
	unsigned long xtal_rate;
	unsigned long cpu_pll_rate;
	unsigned long pll_rate;
	unsigned long cpu_rate;
	unsigned long sys_rate;
	unsigned long dram_rate;
	unsigned long periph_rate;

	xtal_rate = mt7620_get_xtal_rate();

	cpu_pll_rate = mt7620_get_cpu_pll_rate(xtal_rate);
	pll_rate = mt7620_get_pll_rate(xtal_rate, cpu_pll_rate);

	cpu_rate = mt7620_get_cpu_rate(pll_rate);
	dram_rate = mt7620_get_dram_rate(pll_rate);
	sys_rate = mt7620_get_sys_rate(cpu_rate);
	periph_rate = mt7620_get_periph_rate(xtal_rate);

#define RFMT(label)	label ":%lu.%03luMHz "
#define RINT(x)		((x) / 1000000)
#define RFRAC(x)	(((x) / 1000) % 1000)

	pr_debug(RFMT("XTAL") RFMT("CPU_PLL") RFMT("PLL"),
		 RINT(xtal_rate), RFRAC(xtal_rate),
		 RINT(cpu_pll_rate), RFRAC(cpu_pll_rate),
		 RINT(pll_rate), RFRAC(pll_rate));

	pr_debug(RFMT("CPU") RFMT("DRAM") RFMT("SYS") RFMT("PERIPH"),
		 RINT(cpu_rate), RFRAC(cpu_rate),
		 RINT(dram_rate), RFRAC(dram_rate),
		 RINT(sys_rate), RFRAC(sys_rate),
		 RINT(periph_rate), RFRAC(periph_rate));

#undef RFRAC
#undef RINT
#undef RFMT

	ralink_clk_add("cpu", cpu_rate);
	ralink_clk_add("10000100.timer", periph_rate);
	ralink_clk_add("10000120.watchdog", periph_rate);
	ralink_clk_add("10000500.uart", periph_rate);
	ralink_clk_add("10000b00.spi", sys_rate);
	ralink_clk_add("10000c00.uartlite", periph_rate);
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
		panic("mt7620: unknown SoC, n0:%08x n1:%08x", n0, n1);
	}

	rev = __raw_readl(sysc + SYSC_REG_CHIP_REV);

	snprintf(soc_info->sys_type, RAMIPS_SYS_TYPE_LEN,
		"Ralink %s ver:%u eco:%u",
		name,
		(rev >> CHIP_REV_VER_SHIFT) & CHIP_REV_VER_MASK,
		(rev & CHIP_REV_ECO_MASK));

	cfg0 = __raw_readl(sysc + SYSC_REG_SYSTEM_CONFIG0);
	dram_type = (cfg0 >> SYSCFG0_DRAM_TYPE_SHIFT) & SYSCFG0_DRAM_TYPE_MASK;

	switch (dram_type) {
	case SYSCFG0_DRAM_TYPE_SDRAM:
		pr_info("Board has SDRAM\n");
		soc_info->mem_size_min = MT7620_SDRAM_SIZE_MIN;
		soc_info->mem_size_max = MT7620_SDRAM_SIZE_MAX;
		break;

	case SYSCFG0_DRAM_TYPE_DDR1:
		pr_info("Board has DDR1\n");
		soc_info->mem_size_min = MT7620_DDR1_SIZE_MIN;
		soc_info->mem_size_max = MT7620_DDR1_SIZE_MAX;
		break;

	case SYSCFG0_DRAM_TYPE_DDR2:
		pr_info("Board has DDR2\n");
		soc_info->mem_size_min = MT7620_DDR2_SIZE_MIN;
		soc_info->mem_size_max = MT7620_DDR2_SIZE_MAX;
		break;
	default:
		BUG();
	}
	soc_info->mem_base = MT7620_DRAM_BASE;
}
