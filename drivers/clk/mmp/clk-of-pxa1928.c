// SPDX-License-Identifier: GPL-2.0-only
/*
 * pxa1928 clock framework source file
 *
 * Copyright (C) 2015 Linaro, Ltd.
 * Rob Herring <robh@kernel.org>
 *
 * Based on drivers/clk/mmp/clk-of-mmp2.c:
 * Copyright (C) 2012 Marvell
 * Chao Xie <xiechao.mail@gmail.com>
 */
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <dt-bindings/clock/marvell,pxa1928.h>

#include "clk.h"
#include "reset.h"

#define MPMU_UART_PLL	0x14

#define APBC_NR_CLKS	48
#define APMU_NR_CLKS	96

struct pxa1928_clk_unit {
	struct mmp_clk_unit unit;
	void __iomem *mpmu_base;
	void __iomem *apmu_base;
	void __iomem *apbc_base;
	void __iomem *apbcp_base;
};

static struct mmp_param_fixed_rate_clk fixed_rate_clks[] = {
	{0, "clk32", NULL, 0, 32768},
	{0, "vctcxo", NULL, 0, 26000000},
	{0, "pll1_624", NULL, 0, 624000000},
	{0, "pll5p", NULL, 0, 832000000},
	{0, "pll5", NULL, 0, 1248000000},
	{0, "usb_pll", NULL, 0, 480000000},
};

static struct mmp_param_fixed_factor_clk fixed_factor_clks[] = {
	{0, "pll1_d2", "pll1_624", 1, 2, 0},
	{0, "pll1_d9", "pll1_624", 1, 9, 0},
	{0, "pll1_d12", "pll1_624", 1, 12, 0},
	{0, "pll1_d16", "pll1_624", 1, 16, 0},
	{0, "pll1_d20", "pll1_624", 1, 20, 0},
	{0, "pll1_416", "pll1_624", 2, 3, 0},
	{0, "vctcxo_d2", "vctcxo", 1, 2, 0},
	{0, "vctcxo_d4", "vctcxo", 1, 4, 0},
};

static struct mmp_clk_factor_masks uart_factor_masks = {
	.factor = 2,
	.num_mask = 0x1fff,
	.den_mask = 0x1fff,
	.num_shift = 16,
	.den_shift = 0,
};

static struct u32_fract uart_factor_tbl[] = {
	{ .numerator = 832, .denominator = 234 },	/* 58.5MHZ */
	{ .numerator =   1, .denominator =   1 },	/* 26MHZ */
};

static void pxa1928_pll_init(struct pxa1928_clk_unit *pxa_unit)
{
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_fixed_rate_clks(unit, fixed_rate_clks,
					ARRAY_SIZE(fixed_rate_clks));

	mmp_register_fixed_factor_clks(unit, fixed_factor_clks,
					ARRAY_SIZE(fixed_factor_clks));

	mmp_clk_register_factor("uart_pll", "pll1_416",
				CLK_SET_RATE_PARENT,
				pxa_unit->mpmu_base + MPMU_UART_PLL,
				&uart_factor_masks, uart_factor_tbl,
				ARRAY_SIZE(uart_factor_tbl), NULL);
}

static DEFINE_SPINLOCK(uart0_lock);
static DEFINE_SPINLOCK(uart1_lock);
static DEFINE_SPINLOCK(uart2_lock);
static DEFINE_SPINLOCK(uart3_lock);
static const char *uart_parent_names[] = {"uart_pll", "vctcxo"};

static DEFINE_SPINLOCK(ssp0_lock);
static DEFINE_SPINLOCK(ssp1_lock);
static const char *ssp_parent_names[] = {"vctcxo_d4", "vctcxo_d2", "vctcxo", "pll1_d12"};

static DEFINE_SPINLOCK(reset_lock);

static struct mmp_param_mux_clk apbc_mux_clks[] = {
	{0, "uart0_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, PXA1928_CLK_UART0 * 4, 4, 3, 0, &uart0_lock},
	{0, "uart1_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, PXA1928_CLK_UART1 * 4, 4, 3, 0, &uart1_lock},
	{0, "uart2_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, PXA1928_CLK_UART2 * 4, 4, 3, 0, &uart2_lock},
	{0, "uart3_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, PXA1928_CLK_UART3 * 4, 4, 3, 0, &uart3_lock},
	{0, "ssp0_mux", ssp_parent_names, ARRAY_SIZE(ssp_parent_names), CLK_SET_RATE_PARENT, PXA1928_CLK_SSP0 * 4, 4, 3, 0, &ssp0_lock},
	{0, "ssp1_mux", ssp_parent_names, ARRAY_SIZE(ssp_parent_names), CLK_SET_RATE_PARENT, PXA1928_CLK_SSP1 * 4, 4, 3, 0, &ssp1_lock},
};

static struct mmp_param_gate_clk apbc_gate_clks[] = {
	{PXA1928_CLK_TWSI0, "twsi0_clk", "vctcxo", CLK_SET_RATE_PARENT, PXA1928_CLK_TWSI0 * 4, 0x3, 0x3, 0x0, 0, &reset_lock},
	{PXA1928_CLK_TWSI1, "twsi1_clk", "vctcxo", CLK_SET_RATE_PARENT, PXA1928_CLK_TWSI1 * 4, 0x3, 0x3, 0x0, 0, &reset_lock},
	{PXA1928_CLK_TWSI2, "twsi2_clk", "vctcxo", CLK_SET_RATE_PARENT, PXA1928_CLK_TWSI2 * 4, 0x3, 0x3, 0x0, 0, &reset_lock},
	{PXA1928_CLK_TWSI3, "twsi3_clk", "vctcxo", CLK_SET_RATE_PARENT, PXA1928_CLK_TWSI3 * 4, 0x3, 0x3, 0x0, 0, &reset_lock},
	{PXA1928_CLK_TWSI4, "twsi4_clk", "vctcxo", CLK_SET_RATE_PARENT, PXA1928_CLK_TWSI4 * 4, 0x3, 0x3, 0x0, 0, &reset_lock},
	{PXA1928_CLK_TWSI5, "twsi5_clk", "vctcxo", CLK_SET_RATE_PARENT, PXA1928_CLK_TWSI5 * 4, 0x3, 0x3, 0x0, 0, &reset_lock},
	{PXA1928_CLK_GPIO, "gpio_clk", "vctcxo", CLK_SET_RATE_PARENT, PXA1928_CLK_GPIO * 4, 0x3, 0x3, 0x0, 0, &reset_lock},
	{PXA1928_CLK_KPC, "kpc_clk", "clk32", CLK_SET_RATE_PARENT, PXA1928_CLK_KPC * 4, 0x3, 0x3, 0x0, MMP_CLK_GATE_NEED_DELAY, NULL},
	{PXA1928_CLK_RTC, "rtc_clk", "clk32", CLK_SET_RATE_PARENT, PXA1928_CLK_RTC * 4, 0x83, 0x83, 0x0, MMP_CLK_GATE_NEED_DELAY, NULL},
	{PXA1928_CLK_PWM0, "pwm0_clk", "vctcxo", CLK_SET_RATE_PARENT, PXA1928_CLK_PWM0 * 4, 0x3, 0x3, 0x0, 0, &reset_lock},
	{PXA1928_CLK_PWM1, "pwm1_clk", "vctcxo", CLK_SET_RATE_PARENT, PXA1928_CLK_PWM1 * 4, 0x3, 0x3, 0x0, 0, &reset_lock},
	{PXA1928_CLK_PWM2, "pwm2_clk", "vctcxo", CLK_SET_RATE_PARENT, PXA1928_CLK_PWM2 * 4, 0x3, 0x3, 0x0, 0, &reset_lock},
	{PXA1928_CLK_PWM3, "pwm3_clk", "vctcxo", CLK_SET_RATE_PARENT, PXA1928_CLK_PWM3 * 4, 0x3, 0x3, 0x0, 0, &reset_lock},
	/* The gate clocks has mux parent. */
	{PXA1928_CLK_UART0, "uart0_clk", "uart0_mux", CLK_SET_RATE_PARENT, PXA1928_CLK_UART0 * 4, 0x3, 0x3, 0x0, 0, &uart0_lock},
	{PXA1928_CLK_UART1, "uart1_clk", "uart1_mux", CLK_SET_RATE_PARENT, PXA1928_CLK_UART1 * 4, 0x3, 0x3, 0x0, 0, &uart1_lock},
	{PXA1928_CLK_UART2, "uart2_clk", "uart2_mux", CLK_SET_RATE_PARENT, PXA1928_CLK_UART2 * 4, 0x3, 0x3, 0x0, 0, &uart2_lock},
	{PXA1928_CLK_UART3, "uart3_clk", "uart3_mux", CLK_SET_RATE_PARENT, PXA1928_CLK_UART3 * 4, 0x3, 0x3, 0x0, 0, &uart3_lock},
	{PXA1928_CLK_SSP0, "ssp0_clk", "ssp0_mux", CLK_SET_RATE_PARENT, PXA1928_CLK_SSP0 * 4, 0x3, 0x3, 0x0, 0, &ssp0_lock},
	{PXA1928_CLK_SSP1, "ssp1_clk", "ssp1_mux", CLK_SET_RATE_PARENT, PXA1928_CLK_SSP1 * 4, 0x3, 0x3, 0x0, 0, &ssp1_lock},
};

static void pxa1928_apb_periph_clk_init(struct pxa1928_clk_unit *pxa_unit)
{
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_mux_clks(unit, apbc_mux_clks, pxa_unit->apbc_base,
				ARRAY_SIZE(apbc_mux_clks));

	mmp_register_gate_clks(unit, apbc_gate_clks, pxa_unit->apbc_base,
				ARRAY_SIZE(apbc_gate_clks));
}

static DEFINE_SPINLOCK(sdh0_lock);
static DEFINE_SPINLOCK(sdh1_lock);
static DEFINE_SPINLOCK(sdh2_lock);
static DEFINE_SPINLOCK(sdh3_lock);
static DEFINE_SPINLOCK(sdh4_lock);
static const char *sdh_parent_names[] = {"pll1_624", "pll5p", "pll5", "pll1_416"};

static DEFINE_SPINLOCK(usb_lock);

static struct mmp_param_mux_clk apmu_mux_clks[] = {
	{0, "sdh_mux", sdh_parent_names, ARRAY_SIZE(sdh_parent_names), CLK_SET_RATE_PARENT, PXA1928_CLK_SDH0 * 4, 8, 2, 0, &sdh0_lock},
};

static struct mmp_param_div_clk apmu_div_clks[] = {
	{0, "sdh_div", "sdh_mux", 0, PXA1928_CLK_SDH0 * 4, 10, 4, CLK_DIVIDER_ONE_BASED, &sdh0_lock},
};

static struct mmp_param_gate_clk apmu_gate_clks[] = {
	{PXA1928_CLK_USB, "usb_clk", "usb_pll", 0, PXA1928_CLK_USB * 4, 0x9, 0x9, 0x0, 0, &usb_lock},
	{PXA1928_CLK_HSIC, "hsic_clk", "usb_pll", 0, PXA1928_CLK_HSIC * 4, 0x9, 0x9, 0x0, 0, &usb_lock},
	/* The gate clocks has mux parent. */
	{PXA1928_CLK_SDH0, "sdh0_clk", "sdh_div", CLK_SET_RATE_PARENT, PXA1928_CLK_SDH0 * 4, 0x1b, 0x1b, 0x0, 0, &sdh0_lock},
	{PXA1928_CLK_SDH1, "sdh1_clk", "sdh_div", CLK_SET_RATE_PARENT, PXA1928_CLK_SDH1 * 4, 0x1b, 0x1b, 0x0, 0, &sdh1_lock},
	{PXA1928_CLK_SDH2, "sdh2_clk", "sdh_div", CLK_SET_RATE_PARENT, PXA1928_CLK_SDH2 * 4, 0x1b, 0x1b, 0x0, 0, &sdh2_lock},
	{PXA1928_CLK_SDH3, "sdh3_clk", "sdh_div", CLK_SET_RATE_PARENT, PXA1928_CLK_SDH3 * 4, 0x1b, 0x1b, 0x0, 0, &sdh3_lock},
	{PXA1928_CLK_SDH4, "sdh4_clk", "sdh_div", CLK_SET_RATE_PARENT, PXA1928_CLK_SDH4 * 4, 0x1b, 0x1b, 0x0, 0, &sdh4_lock},
};

static void pxa1928_axi_periph_clk_init(struct pxa1928_clk_unit *pxa_unit)
{
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_mux_clks(unit, apmu_mux_clks, pxa_unit->apmu_base,
				ARRAY_SIZE(apmu_mux_clks));

	mmp_register_div_clks(unit, apmu_div_clks, pxa_unit->apmu_base,
				ARRAY_SIZE(apmu_div_clks));

	mmp_register_gate_clks(unit, apmu_gate_clks, pxa_unit->apmu_base,
				ARRAY_SIZE(apmu_gate_clks));
}

static void pxa1928_clk_reset_init(struct device_node *np,
				struct pxa1928_clk_unit *pxa_unit)
{
	struct mmp_clk_reset_cell *cells;
	int i, base, nr_resets;

	nr_resets = ARRAY_SIZE(apbc_gate_clks);
	cells = kcalloc(nr_resets, sizeof(*cells), GFP_KERNEL);
	if (!cells)
		return;

	base = 0;
	for (i = 0; i < nr_resets; i++) {
		cells[base + i].clk_id = apbc_gate_clks[i].id;
		cells[base + i].reg =
			pxa_unit->apbc_base + apbc_gate_clks[i].offset;
		cells[base + i].flags = 0;
		cells[base + i].lock = apbc_gate_clks[i].lock;
		cells[base + i].bits = 0x4;
	}

	mmp_clk_reset_register(np, cells, nr_resets);
}

static void __init pxa1928_mpmu_clk_init(struct device_node *np)
{
	struct pxa1928_clk_unit *pxa_unit;

	pxa_unit = kzalloc(sizeof(*pxa_unit), GFP_KERNEL);
	if (!pxa_unit)
		return;

	pxa_unit->mpmu_base = of_iomap(np, 0);
	if (!pxa_unit->mpmu_base) {
		pr_err("failed to map mpmu registers\n");
		kfree(pxa_unit);
		return;
	}

	pxa1928_pll_init(pxa_unit);
}
CLK_OF_DECLARE(pxa1928_mpmu_clk, "marvell,pxa1928-mpmu", pxa1928_mpmu_clk_init);

static void __init pxa1928_apmu_clk_init(struct device_node *np)
{
	struct pxa1928_clk_unit *pxa_unit;

	pxa_unit = kzalloc(sizeof(*pxa_unit), GFP_KERNEL);
	if (!pxa_unit)
		return;

	pxa_unit->apmu_base = of_iomap(np, 0);
	if (!pxa_unit->apmu_base) {
		pr_err("failed to map apmu registers\n");
		kfree(pxa_unit);
		return;
	}

	mmp_clk_init(np, &pxa_unit->unit, APMU_NR_CLKS);

	pxa1928_axi_periph_clk_init(pxa_unit);
}
CLK_OF_DECLARE(pxa1928_apmu_clk, "marvell,pxa1928-apmu", pxa1928_apmu_clk_init);

static void __init pxa1928_apbc_clk_init(struct device_node *np)
{
	struct pxa1928_clk_unit *pxa_unit;

	pxa_unit = kzalloc(sizeof(*pxa_unit), GFP_KERNEL);
	if (!pxa_unit)
		return;

	pxa_unit->apbc_base = of_iomap(np, 0);
	if (!pxa_unit->apbc_base) {
		pr_err("failed to map apbc registers\n");
		kfree(pxa_unit);
		return;
	}

	mmp_clk_init(np, &pxa_unit->unit, APBC_NR_CLKS);

	pxa1928_apb_periph_clk_init(pxa_unit);
	pxa1928_clk_reset_init(np, pxa_unit);
}
CLK_OF_DECLARE(pxa1928_apbc_clk, "marvell,pxa1928-apbc", pxa1928_apbc_clk_init);
