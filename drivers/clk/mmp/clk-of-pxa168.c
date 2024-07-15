// SPDX-License-Identifier: GPL-2.0-only
/*
 * pxa168 clock framework source file
 *
 * Copyright (C) 2012 Marvell
 * Chao Xie <xiechao.mail@gmail.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/of_address.h>

#include <dt-bindings/clock/marvell,pxa168.h>

#include "clk.h"
#include "reset.h"

#define APBC_UART0	0x0
#define APBC_UART1	0x4
#define APBC_GPIO	0x8
#define APBC_PWM0	0xc
#define APBC_PWM1	0x10
#define APBC_PWM2	0x14
#define APBC_PWM3	0x18
#define APBC_RTC	0x28
#define APBC_TWSI0	0x2c
#define APBC_KPC	0x30
#define APBC_TIMER	0x34
#define APBC_AIB	0x3c
#define APBC_SW_JTAG	0x40
#define APBC_ONEWIRE	0x48
#define APBC_TWSI1	0x6c
#define APBC_UART2	0x70
#define APBC_AC97	0x84
#define APBC_SSP0	0x81c
#define APBC_SSP1	0x820
#define APBC_SSP2	0x84c
#define APBC_SSP3	0x858
#define APBC_SSP4	0x85c
#define APMU_DISP0	0x4c
#define APMU_CCIC0	0x50
#define APMU_SDH0	0x54
#define APMU_SDH1	0x58
#define APMU_USB	0x5c
#define APMU_DFC	0x60
#define APMU_DMA	0x64
#define APMU_BUS	0x6c
#define APMU_GC		0xcc
#define APMU_SMC	0xd4
#define APMU_XD		0xdc
#define APMU_SDH2	0xe0
#define APMU_SDH3	0xe4
#define APMU_CF		0xf0
#define APMU_MSP	0xf4
#define APMU_CMU	0xf8
#define APMU_FE		0xfc
#define APMU_PCIE	0x100
#define APMU_EPD	0x104
#define MPMU_UART_PLL	0x14

#define NR_CLKS		200

struct pxa168_clk_unit {
	struct mmp_clk_unit unit;
	void __iomem *mpmu_base;
	void __iomem *apmu_base;
	void __iomem *apbc_base;
};

static struct mmp_param_fixed_rate_clk fixed_rate_clks[] = {
	{PXA168_CLK_CLK32, "clk32", NULL, 0, 32768},
	{PXA168_CLK_VCTCXO, "vctcxo", NULL, 0, 26000000},
	{PXA168_CLK_PLL1, "pll1", NULL, 0, 624000000},
	{PXA168_CLK_USB_PLL, "usb_pll", NULL, 0, 480000000},
};

static struct mmp_param_fixed_factor_clk fixed_factor_clks[] = {
	{PXA168_CLK_PLL1_2, "pll1_2", "pll1", 1, 2, 0},
	{PXA168_CLK_PLL1_4, "pll1_4", "pll1_2", 1, 2, 0},
	{PXA168_CLK_PLL1_8, "pll1_8", "pll1_4", 1, 2, 0},
	{PXA168_CLK_PLL1_16, "pll1_16", "pll1_8", 1, 2, 0},
	{PXA168_CLK_PLL1_6, "pll1_6", "pll1_2", 1, 3, 0},
	{PXA168_CLK_PLL1_12, "pll1_12", "pll1_6", 1, 2, 0},
	{PXA168_CLK_PLL1_24, "pll1_24", "pll1_12", 1, 2, 0},
	{PXA168_CLK_PLL1_48, "pll1_48", "pll1_24", 1, 2, 0},
	{PXA168_CLK_PLL1_96, "pll1_96", "pll1_48", 1, 2, 0},
	{PXA168_CLK_PLL1_192, "pll1_192", "pll1_96", 1, 2, 0},
	{PXA168_CLK_PLL1_13, "pll1_13", "pll1", 1, 13, 0},
	{PXA168_CLK_PLL1_13_1_5, "pll1_13_1_5", "pll1_13", 1, 5, 0},
	{PXA168_CLK_PLL1_2_1_5, "pll1_2_1_5", "pll1_2", 1, 5, 0},
	{PXA168_CLK_PLL1_3_16, "pll1_3_16", "pll1", 3, 16, 0},
	{PXA168_CLK_PLL1_2_1_10, "pll1_2_1_10", "pll1_2", 1, 10, 0},
	{PXA168_CLK_PLL1_2_3_16, "pll1_2_3_16", "pll1_2", 3, 16, 0},
	{PXA168_CLK_CLK32_2, "clk32_2", "clk32", 1, 2, 0},
};

static struct mmp_clk_factor_masks uart_factor_masks = {
	.factor = 2,
	.num_mask = 0x1fff,
	.den_mask = 0x1fff,
	.num_shift = 16,
	.den_shift = 0,
};

static struct mmp_clk_factor_tbl uart_factor_tbl[] = {
	{.num = 8125, .den = 1536},	/*14.745MHZ */
};

static void pxa168_pll_init(struct pxa168_clk_unit *pxa_unit)
{
	struct clk *clk;
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_fixed_rate_clks(unit, fixed_rate_clks,
					ARRAY_SIZE(fixed_rate_clks));

	mmp_register_fixed_factor_clks(unit, fixed_factor_clks,
					ARRAY_SIZE(fixed_factor_clks));

	clk = mmp_clk_register_factor("uart_pll", "pll1_4",
				CLK_SET_RATE_PARENT,
				pxa_unit->mpmu_base + MPMU_UART_PLL,
				&uart_factor_masks, uart_factor_tbl,
				ARRAY_SIZE(uart_factor_tbl), NULL);
	mmp_clk_add(unit, PXA168_CLK_UART_PLL, clk);
}

static DEFINE_SPINLOCK(twsi0_lock);
static DEFINE_SPINLOCK(twsi1_lock);
static const char * const twsi_parent_names[] = {"pll1_2_1_10", "pll1_2_1_5"};

static DEFINE_SPINLOCK(kpc_lock);
static const char * const kpc_parent_names[] = {"clk32", "clk32_2", "pll1_24"};

static DEFINE_SPINLOCK(pwm0_lock);
static DEFINE_SPINLOCK(pwm1_lock);
static DEFINE_SPINLOCK(pwm2_lock);
static DEFINE_SPINLOCK(pwm3_lock);
static const char * const pwm_parent_names[] = {"pll1_48", "clk32"};

static DEFINE_SPINLOCK(uart0_lock);
static DEFINE_SPINLOCK(uart1_lock);
static DEFINE_SPINLOCK(uart2_lock);
static const char * const uart_parent_names[] = {"pll1_2_3_16", "uart_pll"};

static DEFINE_SPINLOCK(ssp0_lock);
static DEFINE_SPINLOCK(ssp1_lock);
static DEFINE_SPINLOCK(ssp2_lock);
static DEFINE_SPINLOCK(ssp3_lock);
static DEFINE_SPINLOCK(ssp4_lock);
static const char * const ssp_parent_names[] = {"pll1_96", "pll1_48", "pll1_24", "pll1_12"};

static DEFINE_SPINLOCK(timer_lock);
static const char * const timer_parent_names[] = {"pll1_48", "clk32", "pll1_96", "pll1_192"};

static DEFINE_SPINLOCK(reset_lock);

static struct mmp_param_mux_clk apbc_mux_clks[] = {
	{0, "twsi0_mux", twsi_parent_names, ARRAY_SIZE(twsi_parent_names), CLK_SET_RATE_PARENT, APBC_TWSI0, 4, 3, 0, &twsi0_lock},
	{0, "twsi1_mux", twsi_parent_names, ARRAY_SIZE(twsi_parent_names), CLK_SET_RATE_PARENT, APBC_TWSI1, 4, 3, 0, &twsi1_lock},
	{0, "kpc_mux", kpc_parent_names, ARRAY_SIZE(kpc_parent_names), CLK_SET_RATE_PARENT, APBC_KPC, 4, 3, 0, &kpc_lock},
	{0, "pwm0_mux", pwm_parent_names, ARRAY_SIZE(pwm_parent_names), CLK_SET_RATE_PARENT, APBC_PWM0, 4, 3, 0, &pwm0_lock},
	{0, "pwm1_mux", pwm_parent_names, ARRAY_SIZE(pwm_parent_names), CLK_SET_RATE_PARENT, APBC_PWM1, 4, 3, 0, &pwm1_lock},
	{0, "pwm2_mux", pwm_parent_names, ARRAY_SIZE(pwm_parent_names), CLK_SET_RATE_PARENT, APBC_PWM2, 4, 3, 0, &pwm2_lock},
	{0, "pwm3_mux", pwm_parent_names, ARRAY_SIZE(pwm_parent_names), CLK_SET_RATE_PARENT, APBC_PWM3, 4, 3, 0, &pwm3_lock},
	{0, "uart0_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, APBC_UART0, 4, 3, 0, &uart0_lock},
	{0, "uart1_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, APBC_UART1, 4, 3, 0, &uart1_lock},
	{0, "uart2_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, APBC_UART2, 4, 3, 0, &uart2_lock},
	{0, "ssp0_mux", ssp_parent_names, ARRAY_SIZE(ssp_parent_names), CLK_SET_RATE_PARENT, APBC_SSP0, 4, 3, 0, &ssp0_lock},
	{0, "ssp1_mux", ssp_parent_names, ARRAY_SIZE(ssp_parent_names), CLK_SET_RATE_PARENT, APBC_SSP1, 4, 3, 0, &ssp1_lock},
	{0, "ssp2_mux", ssp_parent_names, ARRAY_SIZE(ssp_parent_names), CLK_SET_RATE_PARENT, APBC_SSP2, 4, 3, 0, &ssp2_lock},
	{0, "ssp3_mux", ssp_parent_names, ARRAY_SIZE(ssp_parent_names), CLK_SET_RATE_PARENT, APBC_SSP3, 4, 3, 0, &ssp3_lock},
	{0, "ssp4_mux", ssp_parent_names, ARRAY_SIZE(ssp_parent_names), CLK_SET_RATE_PARENT, APBC_SSP4, 4, 3, 0, &ssp4_lock},
	{0, "timer_mux", timer_parent_names, ARRAY_SIZE(timer_parent_names), CLK_SET_RATE_PARENT, APBC_TIMER, 4, 3, 0, &timer_lock},
};

static struct mmp_param_gate_clk apbc_gate_clks[] = {
	{PXA168_CLK_TWSI0, "twsi0_clk", "twsi0_mux", CLK_SET_RATE_PARENT, APBC_TWSI0, 0x3, 0x3, 0x0, 0, &twsi0_lock},
	{PXA168_CLK_TWSI1, "twsi1_clk", "twsi1_mux", CLK_SET_RATE_PARENT, APBC_TWSI1, 0x3, 0x3, 0x0, 0, &twsi1_lock},
	{PXA168_CLK_GPIO, "gpio_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_GPIO, 0x1, 0x1, 0x0, 0, &reset_lock},
	{PXA168_CLK_KPC, "kpc_clk", "kpc_mux", CLK_SET_RATE_PARENT, APBC_KPC, 0x3, 0x3, 0x0, MMP_CLK_GATE_NEED_DELAY, &kpc_lock},
	{PXA168_CLK_RTC, "rtc_clk", "clk32", CLK_SET_RATE_PARENT, APBC_RTC, 0x83, 0x83, 0x0, MMP_CLK_GATE_NEED_DELAY, NULL},
	{PXA168_CLK_PWM0, "pwm0_clk", "pwm0_mux", CLK_SET_RATE_PARENT, APBC_PWM0, 0x3, 0x3, 0x0, 0, &pwm0_lock},
	{PXA168_CLK_PWM1, "pwm1_clk", "pwm1_mux", CLK_SET_RATE_PARENT, APBC_PWM1, 0x3, 0x3, 0x0, 0, &pwm1_lock},
	{PXA168_CLK_PWM2, "pwm2_clk", "pwm2_mux", CLK_SET_RATE_PARENT, APBC_PWM2, 0x3, 0x3, 0x0, 0, &pwm2_lock},
	{PXA168_CLK_PWM3, "pwm3_clk", "pwm3_mux", CLK_SET_RATE_PARENT, APBC_PWM3, 0x3, 0x3, 0x0, 0, &pwm3_lock},
	{PXA168_CLK_UART0, "uart0_clk", "uart0_mux", CLK_SET_RATE_PARENT, APBC_UART0, 0x3, 0x3, 0x0, 0, &uart0_lock},
	{PXA168_CLK_UART1, "uart1_clk", "uart1_mux", CLK_SET_RATE_PARENT, APBC_UART1, 0x3, 0x3, 0x0, 0, &uart1_lock},
	{PXA168_CLK_UART2, "uart2_clk", "uart2_mux", CLK_SET_RATE_PARENT, APBC_UART2, 0x3, 0x3, 0x0, 0, &uart2_lock},
	{PXA168_CLK_SSP0, "ssp0_clk", "ssp0_mux", CLK_SET_RATE_PARENT, APBC_SSP0, 0x3, 0x3, 0x0, 0, &ssp0_lock},
	{PXA168_CLK_SSP1, "ssp1_clk", "ssp1_mux", CLK_SET_RATE_PARENT, APBC_SSP1, 0x3, 0x3, 0x0, 0, &ssp1_lock},
	{PXA168_CLK_SSP2, "ssp2_clk", "ssp2_mux", CLK_SET_RATE_PARENT, APBC_SSP2, 0x3, 0x3, 0x0, 0, &ssp2_lock},
	{PXA168_CLK_SSP3, "ssp3_clk", "ssp3_mux", CLK_SET_RATE_PARENT, APBC_SSP3, 0x3, 0x3, 0x0, 0, &ssp3_lock},
	{PXA168_CLK_SSP4, "ssp4_clk", "ssp4_mux", CLK_SET_RATE_PARENT, APBC_SSP4, 0x3, 0x3, 0x0, 0, &ssp4_lock},
	{PXA168_CLK_TIMER, "timer_clk", "timer_mux", CLK_SET_RATE_PARENT, APBC_TIMER, 0x3, 0x3, 0x0, 0, &timer_lock},
};

static void pxa168_apb_periph_clk_init(struct pxa168_clk_unit *pxa_unit)
{
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_mux_clks(unit, apbc_mux_clks, pxa_unit->apbc_base,
				ARRAY_SIZE(apbc_mux_clks));

	mmp_register_gate_clks(unit, apbc_gate_clks, pxa_unit->apbc_base,
				ARRAY_SIZE(apbc_gate_clks));

}

static DEFINE_SPINLOCK(dfc_lock);
static const char * const dfc_parent_names[] = {"pll1_4", "pll1_8"};

static DEFINE_SPINLOCK(sdh0_lock);
static DEFINE_SPINLOCK(sdh1_lock);
static DEFINE_SPINLOCK(sdh2_lock);
static DEFINE_SPINLOCK(sdh3_lock);
static const char * const sdh_parent_names[] = {"pll1_13", "pll1_12", "pll1_8"};

static DEFINE_SPINLOCK(usb_lock);

static DEFINE_SPINLOCK(disp0_lock);
static const char * const disp_parent_names[] = {"pll1", "pll1_2"};

static DEFINE_SPINLOCK(ccic0_lock);
static const char * const ccic_parent_names[] = {"pll1_4", "pll1_8"};
static const char * const ccic_phy_parent_names[] = {"pll1_6", "pll1_12"};

static struct mmp_param_mux_clk apmu_mux_clks[] = {
	{0, "dfc_mux", dfc_parent_names, ARRAY_SIZE(dfc_parent_names), CLK_SET_RATE_PARENT, APMU_DFC, 6, 1, 0, &dfc_lock},
	{0, "sdh0_mux", sdh_parent_names, ARRAY_SIZE(sdh_parent_names), CLK_SET_RATE_PARENT, APMU_SDH0, 6, 2, 0, &sdh0_lock},
	{0, "sdh1_mux", sdh_parent_names, ARRAY_SIZE(sdh_parent_names), CLK_SET_RATE_PARENT, APMU_SDH1, 6, 2, 0, &sdh1_lock},
	{0, "sdh2_mux", sdh_parent_names, ARRAY_SIZE(sdh_parent_names), CLK_SET_RATE_PARENT, APMU_SDH2, 6, 2, 0, &sdh2_lock},
	{0, "sdh3_mux", sdh_parent_names, ARRAY_SIZE(sdh_parent_names), CLK_SET_RATE_PARENT, APMU_SDH3, 6, 2, 0, &sdh3_lock},
	{0, "disp0_mux", disp_parent_names, ARRAY_SIZE(disp_parent_names), CLK_SET_RATE_PARENT, APMU_DISP0, 6, 1, 0, &disp0_lock},
	{0, "ccic0_mux", ccic_parent_names, ARRAY_SIZE(ccic_parent_names), CLK_SET_RATE_PARENT, APMU_CCIC0, 6, 1, 0, &ccic0_lock},
	{0, "ccic0_phy_mux", ccic_phy_parent_names, ARRAY_SIZE(ccic_phy_parent_names), CLK_SET_RATE_PARENT, APMU_CCIC0, 7, 1, 0, &ccic0_lock},
};

static struct mmp_param_div_clk apmu_div_clks[] = {
	{0, "ccic0_sphy_div", "ccic0_mux", CLK_SET_RATE_PARENT, APMU_CCIC0, 10, 5, 0, &ccic0_lock},
};

static struct mmp_param_gate_clk apmu_gate_clks[] = {
	{PXA168_CLK_DFC, "dfc_clk", "dfc_mux", CLK_SET_RATE_PARENT, APMU_DFC, 0x19b, 0x19b, 0x0, 0, &dfc_lock},
	{PXA168_CLK_USB, "usb_clk", "usb_pll", 0, APMU_USB, 0x9, 0x9, 0x0, 0, &usb_lock},
	{PXA168_CLK_SPH, "sph_clk", "usb_pll", 0, APMU_USB, 0x12, 0x12, 0x0, 0, &usb_lock},
	{PXA168_CLK_SDH0, "sdh0_clk", "sdh0_mux", CLK_SET_RATE_PARENT, APMU_SDH0, 0x12, 0x12, 0x0, 0, &sdh0_lock},
	{PXA168_CLK_SDH1, "sdh1_clk", "sdh1_mux", CLK_SET_RATE_PARENT, APMU_SDH1, 0x12, 0x12, 0x0, 0, &sdh1_lock},
	{PXA168_CLK_SDH2, "sdh2_clk", "sdh2_mux", CLK_SET_RATE_PARENT, APMU_SDH2, 0x12, 0x12, 0x0, 0, &sdh2_lock},
	{PXA168_CLK_SDH3, "sdh3_clk", "sdh3_mux", CLK_SET_RATE_PARENT, APMU_SDH3, 0x12, 0x12, 0x0, 0, &sdh3_lock},
	/* SDH0/1 and 2/3 AXI clocks are also gated by common bits in SDH0 and SDH2 registers */
	{PXA168_CLK_SDH01_AXI, "sdh01_axi_clk", NULL, CLK_SET_RATE_PARENT, APMU_SDH0, 0x9, 0x9, 0x0, 0, &sdh0_lock},
	{PXA168_CLK_SDH23_AXI, "sdh23_axi_clk", NULL, CLK_SET_RATE_PARENT, APMU_SDH2, 0x9, 0x9, 0x0, 0, &sdh2_lock},
	{PXA168_CLK_DISP0, "disp0_clk", "disp0_mux", CLK_SET_RATE_PARENT, APMU_DISP0, 0x1b, 0x1b, 0x0, 0, &disp0_lock},
	{PXA168_CLK_CCIC0, "ccic0_clk", "ccic0_mux", CLK_SET_RATE_PARENT, APMU_CCIC0, 0x1b, 0x1b, 0x0, 0, &ccic0_lock},
	{PXA168_CLK_CCIC0_PHY, "ccic0_phy_clk", "ccic0_phy_mux", CLK_SET_RATE_PARENT, APMU_CCIC0, 0x24, 0x24, 0x0, 0, &ccic0_lock},
	{PXA168_CLK_CCIC0_SPHY, "ccic0_sphy_clk", "ccic0_sphy_div", CLK_SET_RATE_PARENT, APMU_CCIC0, 0x300, 0x300, 0x0, 0, &ccic0_lock},
};

static void pxa168_axi_periph_clk_init(struct pxa168_clk_unit *pxa_unit)
{
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_mux_clks(unit, apmu_mux_clks, pxa_unit->apmu_base,
				ARRAY_SIZE(apmu_mux_clks));

	mmp_register_div_clks(unit, apmu_div_clks, pxa_unit->apmu_base,
				ARRAY_SIZE(apmu_div_clks));

	mmp_register_gate_clks(unit, apmu_gate_clks, pxa_unit->apmu_base,
				ARRAY_SIZE(apmu_gate_clks));
}

static void pxa168_clk_reset_init(struct device_node *np,
				struct pxa168_clk_unit *pxa_unit)
{
	struct mmp_clk_reset_cell *cells;
	int i, nr_resets;

	nr_resets = ARRAY_SIZE(apbc_gate_clks);
	cells = kcalloc(nr_resets, sizeof(*cells), GFP_KERNEL);
	if (!cells)
		return;

	for (i = 0; i < nr_resets; i++) {
		cells[i].clk_id = apbc_gate_clks[i].id;
		cells[i].reg = pxa_unit->apbc_base + apbc_gate_clks[i].offset;
		cells[i].flags = 0;
		cells[i].lock = apbc_gate_clks[i].lock;
		cells[i].bits = 0x4;
	}

	mmp_clk_reset_register(np, cells, nr_resets);
}

static void __init pxa168_clk_init(struct device_node *np)
{
	struct pxa168_clk_unit *pxa_unit;

	pxa_unit = kzalloc(sizeof(*pxa_unit), GFP_KERNEL);
	if (!pxa_unit)
		return;

	pxa_unit->mpmu_base = of_iomap(np, 0);
	if (!pxa_unit->mpmu_base) {
		pr_err("failed to map mpmu registers\n");
		kfree(pxa_unit);
		return;
	}

	pxa_unit->apmu_base = of_iomap(np, 1);
	if (!pxa_unit->apmu_base) {
		pr_err("failed to map apmu registers\n");
		kfree(pxa_unit);
		return;
	}

	pxa_unit->apbc_base = of_iomap(np, 2);
	if (!pxa_unit->apbc_base) {
		pr_err("failed to map apbc registers\n");
		kfree(pxa_unit);
		return;
	}

	mmp_clk_init(np, &pxa_unit->unit, NR_CLKS);

	pxa168_pll_init(pxa_unit);

	pxa168_apb_periph_clk_init(pxa_unit);

	pxa168_axi_periph_clk_init(pxa_unit);

	pxa168_clk_reset_init(np, pxa_unit);
}

CLK_OF_DECLARE(pxa168_clk, "marvell,pxa168-clock", pxa168_clk_init);
