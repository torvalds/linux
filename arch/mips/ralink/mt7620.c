// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Parts of this file are based on Ralink's 2.6.21 BSP
 *
 * Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2013 John Crispin <john@phrozen.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bug.h>

#include <asm/mipsregs.h>
#include <asm/mach-ralink/ralink_regs.h>
#include <asm/mach-ralink/mt7620.h>

#include "common.h"

/* analog */
#define PMU0_CFG		0x88
#define PMU_SW_SET		BIT(28)
#define A_DCDC_EN		BIT(24)
#define A_SSC_PERI		BIT(19)
#define A_SSC_GEN		BIT(18)
#define A_SSC_M			0x3
#define A_SSC_S			16
#define A_DLY_M			0x7
#define A_DLY_S			8
#define A_VTUNE_M		0xff

/* digital */
#define PMU1_CFG		0x8C
#define DIG_SW_SEL		BIT(25)

/* clock scaling */
#define CLKCFG_FDIV_MASK	0x1f00
#define CLKCFG_FDIV_USB_VAL	0x0300
#define CLKCFG_FFRAC_MASK	0x001f
#define CLKCFG_FFRAC_USB_VAL	0x0003

/* EFUSE bits */
#define EFUSE_MT7688		0x100000

/* DRAM type bit */
#define DRAM_TYPE_MT7628_MASK	0x1

/* does the board have sdram or ddram */
static int dram_type;

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
	unsigned long pcmi2s_rate;

	xtal_rate = mt7620_get_xtal_rate();

#define RFMT(label)	label ":%lu.%03luMHz "
#define RINT(x)		((x) / 1000000)
#define RFRAC(x)	(((x) / 1000) % 1000)

	if (is_mt76x8()) {
		if (xtal_rate == MHZ(40))
			cpu_rate = MHZ(580);
		else
			cpu_rate = MHZ(575);
		dram_rate = sys_rate = cpu_rate / 3;
		periph_rate = MHZ(40);
		pcmi2s_rate = MHZ(480);

		ralink_clk_add("10000d00.uartlite", periph_rate);
		ralink_clk_add("10000e00.uartlite", periph_rate);
	} else {
		cpu_pll_rate = mt7620_get_cpu_pll_rate(xtal_rate);
		pll_rate = mt7620_get_pll_rate(xtal_rate, cpu_pll_rate);

		cpu_rate = mt7620_get_cpu_rate(pll_rate);
		dram_rate = mt7620_get_dram_rate(pll_rate);
		sys_rate = mt7620_get_sys_rate(cpu_rate);
		periph_rate = mt7620_get_periph_rate(xtal_rate);
		pcmi2s_rate = periph_rate;

		pr_debug(RFMT("XTAL") RFMT("CPU_PLL") RFMT("PLL"),
			 RINT(xtal_rate), RFRAC(xtal_rate),
			 RINT(cpu_pll_rate), RFRAC(cpu_pll_rate),
			 RINT(pll_rate), RFRAC(pll_rate));

		ralink_clk_add("10000500.uart", periph_rate);
	}

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
	ralink_clk_add("10000900.i2c", periph_rate);
	ralink_clk_add("10000a00.i2s", pcmi2s_rate);
	ralink_clk_add("10000b00.spi", sys_rate);
	ralink_clk_add("10000b40.spi", sys_rate);
	ralink_clk_add("10000c00.uartlite", periph_rate);
	ralink_clk_add("10000d00.uart1", periph_rate);
	ralink_clk_add("10000e00.uart2", periph_rate);
	ralink_clk_add("10180000.wmac", xtal_rate);

	if (IS_ENABLED(CONFIG_USB) && !is_mt76x8()) {
		/*
		 * When the CPU goes into sleep mode, the BUS clock will be
		 * too low for USB to function properly. Adjust the busses
		 * fractional divider to fix this
		 */
		u32 val = rt_sysc_r32(SYSC_REG_CPU_SYS_CLKCFG);

		val &= ~(CLKCFG_FDIV_MASK | CLKCFG_FFRAC_MASK);
		val |= CLKCFG_FDIV_USB_VAL | CLKCFG_FFRAC_USB_VAL;

		rt_sysc_w32(val, SYSC_REG_CPU_SYS_CLKCFG);
	}
}

void __init ralink_of_remap(void)
{
	rt_sysc_membase = plat_of_remap_node("ralink,mt7620a-sysc");
	rt_memc_membase = plat_of_remap_node("ralink,mt7620a-memc");

	if (!rt_sysc_membase || !rt_memc_membase)
		panic("Failed to remap core resources");
}

static __init void
mt7620_dram_init(struct ralink_soc_info *soc_info)
{
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
}

static __init void
mt7628_dram_init(struct ralink_soc_info *soc_info)
{
	switch (dram_type) {
	case SYSCFG0_DRAM_TYPE_DDR1_MT7628:
		pr_info("Board has DDR1\n");
		soc_info->mem_size_min = MT7620_DDR1_SIZE_MIN;
		soc_info->mem_size_max = MT7620_DDR1_SIZE_MAX;
		break;

	case SYSCFG0_DRAM_TYPE_DDR2_MT7628:
		pr_info("Board has DDR2\n");
		soc_info->mem_size_min = MT7620_DDR2_SIZE_MIN;
		soc_info->mem_size_max = MT7620_DDR2_SIZE_MAX;
		break;
	default:
		BUG();
	}
}

void __init prom_soc_init(struct ralink_soc_info *soc_info)
{
	void __iomem *sysc = (void __iomem *) KSEG1ADDR(MT7620_SYSC_BASE);
	unsigned char *name = NULL;
	u32 n0;
	u32 n1;
	u32 rev;
	u32 cfg0;
	u32 pmu0;
	u32 pmu1;
	u32 bga;

	n0 = __raw_readl(sysc + SYSC_REG_CHIP_NAME0);
	n1 = __raw_readl(sysc + SYSC_REG_CHIP_NAME1);
	rev = __raw_readl(sysc + SYSC_REG_CHIP_REV);
	bga = (rev >> CHIP_REV_PKG_SHIFT) & CHIP_REV_PKG_MASK;

	if (n0 == MT7620_CHIP_NAME0 && n1 == MT7620_CHIP_NAME1) {
		if (bga) {
			ralink_soc = MT762X_SOC_MT7620A;
			name = "MT7620A";
			soc_info->compatible = "ralink,mt7620a-soc";
		} else {
			ralink_soc = MT762X_SOC_MT7620N;
			name = "MT7620N";
			soc_info->compatible = "ralink,mt7620n-soc";
		}
	} else if (n0 == MT7620_CHIP_NAME0 && n1 == MT7628_CHIP_NAME1) {
		u32 efuse = __raw_readl(sysc + SYSC_REG_EFUSE_CFG);

		if (efuse & EFUSE_MT7688) {
			ralink_soc = MT762X_SOC_MT7688;
			name = "MT7688";
		} else {
			ralink_soc = MT762X_SOC_MT7628AN;
			name = "MT7628AN";
		}
		soc_info->compatible = "ralink,mt7628an-soc";
	} else {
		panic("mt762x: unknown SoC, n0:%08x n1:%08x\n", n0, n1);
	}

	snprintf(soc_info->sys_type, RAMIPS_SYS_TYPE_LEN,
		"MediaTek %s ver:%u eco:%u",
		name,
		(rev >> CHIP_REV_VER_SHIFT) & CHIP_REV_VER_MASK,
		(rev & CHIP_REV_ECO_MASK));

	cfg0 = __raw_readl(sysc + SYSC_REG_SYSTEM_CONFIG0);
	if (is_mt76x8()) {
		dram_type = cfg0 & DRAM_TYPE_MT7628_MASK;
	} else {
		dram_type = (cfg0 >> SYSCFG0_DRAM_TYPE_SHIFT) &
			    SYSCFG0_DRAM_TYPE_MASK;
		if (dram_type == SYSCFG0_DRAM_TYPE_UNKNOWN)
			dram_type = SYSCFG0_DRAM_TYPE_SDRAM;
	}

	soc_info->mem_base = MT7620_DRAM_BASE;
	if (is_mt76x8())
		mt7628_dram_init(soc_info);
	else
		mt7620_dram_init(soc_info);

	pmu0 = __raw_readl(sysc + PMU0_CFG);
	pmu1 = __raw_readl(sysc + PMU1_CFG);

	pr_info("Analog PMU set to %s control\n",
		(pmu0 & PMU_SW_SET) ? ("sw") : ("hw"));
	pr_info("Digital PMU set to %s control\n",
		(pmu1 & DIG_SW_SEL) ? ("sw") : ("hw"));
}
