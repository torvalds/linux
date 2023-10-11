// SPDX-License-Identifier: GPL-2.0-only
/*
 * mmp2 clock framework source file
 *
 * Copyright (C) 2012 Marvell
 * Chao Xie <xiechao.mail@gmail.com>
 * Copyright (C) 2020 Lubomir Rintel <lkundrak@v3.sk>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/of_address.h>
#include <linux/clk.h>

#include <dt-bindings/clock/marvell,mmp2.h>
#include <dt-bindings/power/marvell,mmp2.h>

#include "clk.h"
#include "reset.h"

#define APBC_RTC	0x0
#define APBC_TWSI0	0x4
#define APBC_TWSI1	0x8
#define APBC_TWSI2	0xc
#define APBC_TWSI3	0x10
#define APBC_TWSI4	0x7c
#define APBC_TWSI5	0x80
#define APBC_KPC	0x18
#define APBC_TIMER	0x24
#define APBC_UART0	0x2c
#define APBC_UART1	0x30
#define APBC_UART2	0x34
#define APBC_UART3	0x88
#define APBC_GPIO	0x38
#define APBC_PWM0	0x3c
#define APBC_PWM1	0x40
#define APBC_PWM2	0x44
#define APBC_PWM3	0x48
#define APBC_SSP0	0x50
#define APBC_SSP1	0x54
#define APBC_SSP2	0x58
#define APBC_SSP3	0x5c
#define APBC_THERMAL0	0x90
#define APBC_THERMAL1	0x98
#define APBC_THERMAL2	0x9c
#define APBC_THERMAL3	0xa0
#define APMU_SDH0	0x54
#define APMU_SDH1	0x58
#define APMU_SDH2	0xe8
#define APMU_SDH3	0xec
#define APMU_SDH4	0x15c
#define APMU_USB	0x5c
#define APMU_DISP0	0x4c
#define APMU_DISP1	0x110
#define APMU_CCIC0	0x50
#define APMU_CCIC1	0xf4
#define APMU_USBHSIC0	0xf8
#define APMU_USBHSIC1	0xfc
#define APMU_GPU	0xcc
#define APMU_AUDIO	0x10c
#define APMU_CAMERA	0x1fc

#define MPMU_FCCR		0x8
#define MPMU_POSR		0x10
#define MPMU_UART_PLL		0x14
#define MPMU_PLL2_CR		0x34
#define MPMU_I2S0_PLL		0x40
#define MPMU_I2S1_PLL		0x44
#define MPMU_ACGR		0x1024
/* MMP3 specific below */
#define MPMU_PLL3_CR		0x50
#define MPMU_PLL3_CTRL1		0x58
#define MPMU_PLL1_CTRL		0x5c
#define MPMU_PLL_DIFF_CTRL	0x68
#define MPMU_PLL2_CTRL1		0x414

#define NR_CLKS		200

enum mmp2_clk_model {
	CLK_MODEL_MMP2,
	CLK_MODEL_MMP3,
};

struct mmp2_clk_unit {
	struct mmp_clk_unit unit;
	enum mmp2_clk_model model;
	struct genpd_onecell_data pd_data;
	struct generic_pm_domain *pm_domains[MMP2_NR_POWER_DOMAINS];
	void __iomem *mpmu_base;
	void __iomem *apmu_base;
	void __iomem *apbc_base;
};

static struct mmp_param_fixed_rate_clk fixed_rate_clks[] = {
	{MMP2_CLK_CLK32, "clk32", NULL, 0, 32768},
	{MMP2_CLK_VCTCXO, "vctcxo", NULL, 0, 26000000},
	{MMP2_CLK_USB_PLL, "usb_pll", NULL, 0, 480000000},
	{0, "i2s_pll", NULL, 0, 99666667},
};

static struct mmp_param_pll_clk pll_clks[] = {
	{MMP2_CLK_PLL1,   "pll1",   797330000, MPMU_FCCR,          0x4000, MPMU_POSR,     0},
	{MMP2_CLK_PLL2,   "pll2",           0, MPMU_PLL2_CR,       0x0300, MPMU_PLL2_CR, 10},
};

static struct mmp_param_pll_clk mmp3_pll_clks[] = {
	{MMP2_CLK_PLL2,   "pll1",   797330000, MPMU_FCCR,          0x4000, MPMU_POSR,     0,      26000000, MPMU_PLL1_CTRL,      25},
	{MMP2_CLK_PLL2,   "pll2",           0, MPMU_PLL2_CR,       0x0300, MPMU_PLL2_CR, 10,      26000000, MPMU_PLL2_CTRL1,     25},
	{MMP3_CLK_PLL1_P, "pll1_p",         0, MPMU_PLL_DIFF_CTRL, 0x0010, 0,             0,     797330000, MPMU_PLL_DIFF_CTRL,   0},
	{MMP3_CLK_PLL2_P, "pll2_p",         0, MPMU_PLL_DIFF_CTRL, 0x0100, MPMU_PLL2_CR, 10,      26000000, MPMU_PLL_DIFF_CTRL,   5},
	{MMP3_CLK_PLL3,   "pll3",           0, MPMU_PLL3_CR,       0x0300, MPMU_PLL3_CR, 10,      26000000, MPMU_PLL3_CTRL1,     25},
};

static struct mmp_param_fixed_factor_clk fixed_factor_clks[] = {
	{MMP2_CLK_PLL1_2, "pll1_2", "pll1", 1, 2, 0},
	{MMP2_CLK_PLL1_4, "pll1_4", "pll1_2", 1, 2, 0},
	{MMP2_CLK_PLL1_8, "pll1_8", "pll1_4", 1, 2, 0},
	{MMP2_CLK_PLL1_16, "pll1_16", "pll1_8", 1, 2, 0},
	{MMP2_CLK_PLL1_20, "pll1_20", "pll1_4", 1, 5, 0},
	{MMP2_CLK_PLL1_3, "pll1_3", "pll1", 1, 3, 0},
	{MMP2_CLK_PLL1_6, "pll1_6", "pll1_3", 1, 2, 0},
	{MMP2_CLK_PLL1_12, "pll1_12", "pll1_6", 1, 2, 0},
	{MMP2_CLK_PLL2_2, "pll2_2", "pll2", 1, 2, 0},
	{MMP2_CLK_PLL2_4, "pll2_4", "pll2_2", 1, 2, 0},
	{MMP2_CLK_PLL2_8, "pll2_8", "pll2_4", 1, 2, 0},
	{MMP2_CLK_PLL2_16, "pll2_16", "pll2_8", 1, 2, 0},
	{MMP2_CLK_PLL2_3, "pll2_3", "pll2", 1, 3, 0},
	{MMP2_CLK_PLL2_6, "pll2_6", "pll2_3", 1, 2, 0},
	{MMP2_CLK_PLL2_12, "pll2_12", "pll2_6", 1, 2, 0},
	{MMP2_CLK_VCTCXO_2, "vctcxo_2", "vctcxo", 1, 2, 0},
	{MMP2_CLK_VCTCXO_4, "vctcxo_4", "vctcxo_2", 1, 2, 0},
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
	{.num = 3521, .den = 689},	/*19.23MHZ */
};

static struct mmp_clk_factor_masks i2s_factor_masks = {
	.factor = 2,
	.num_mask = 0x7fff,
	.den_mask = 0x1fff,
	.num_shift = 0,
	.den_shift = 15,
	.enable_mask = 0xd0000000,
};

static struct mmp_clk_factor_tbl i2s_factor_tbl[] = {
	{.num = 24868, .den =  511},	/*  2.0480 MHz */
	{.num = 28003, .den =  793},	/*  2.8224 MHz */
	{.num = 24941, .den = 1025},	/*  4.0960 MHz */
	{.num = 28003, .den = 1586},	/*  5.6448 MHz */
	{.num = 31158, .den = 2561},	/*  8.1920 MHz */
	{.num = 16288, .den = 1845},	/* 11.2896 MHz */
	{.num = 20772, .den = 2561},	/* 12.2880 MHz */
	{.num =  8144, .den = 1845},	/* 22.5792 MHz */
	{.num = 10386, .den = 2561},	/* 24.5760 MHz */
};

static DEFINE_SPINLOCK(acgr_lock);

static struct mmp_param_gate_clk mpmu_gate_clks[] = {
	{MMP2_CLK_I2S0, "i2s0_clk", "i2s0_pll", CLK_SET_RATE_PARENT, MPMU_ACGR, 0x200000, 0x200000, 0x0, 0, &acgr_lock},
	{MMP2_CLK_I2S1, "i2s1_clk", "i2s1_pll", CLK_SET_RATE_PARENT, MPMU_ACGR, 0x100000, 0x100000, 0x0, 0, &acgr_lock},
};

static void mmp2_main_clk_init(struct mmp2_clk_unit *pxa_unit)
{
	struct clk *clk;
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_fixed_rate_clks(unit, fixed_rate_clks,
					ARRAY_SIZE(fixed_rate_clks));

	if (pxa_unit->model == CLK_MODEL_MMP3) {
		mmp_register_pll_clks(unit, mmp3_pll_clks,
					pxa_unit->mpmu_base,
					ARRAY_SIZE(mmp3_pll_clks));
	} else {
		mmp_register_pll_clks(unit, pll_clks,
					pxa_unit->mpmu_base,
					ARRAY_SIZE(pll_clks));
	}

	mmp_register_fixed_factor_clks(unit, fixed_factor_clks,
					ARRAY_SIZE(fixed_factor_clks));

	clk = mmp_clk_register_factor("uart_pll", "pll1_4",
				CLK_SET_RATE_PARENT,
				pxa_unit->mpmu_base + MPMU_UART_PLL,
				&uart_factor_masks, uart_factor_tbl,
				ARRAY_SIZE(uart_factor_tbl), NULL);
	mmp_clk_add(unit, MMP2_CLK_UART_PLL, clk);

	mmp_clk_register_factor("i2s0_pll", "pll1_4",
				CLK_SET_RATE_PARENT,
				pxa_unit->mpmu_base + MPMU_I2S0_PLL,
				&i2s_factor_masks, i2s_factor_tbl,
				ARRAY_SIZE(i2s_factor_tbl), NULL);
	mmp_clk_register_factor("i2s1_pll", "pll1_4",
				CLK_SET_RATE_PARENT,
				pxa_unit->mpmu_base + MPMU_I2S1_PLL,
				&i2s_factor_masks, i2s_factor_tbl,
				ARRAY_SIZE(i2s_factor_tbl), NULL);

	mmp_register_gate_clks(unit, mpmu_gate_clks, pxa_unit->mpmu_base,
				ARRAY_SIZE(mpmu_gate_clks));
}

static DEFINE_SPINLOCK(uart0_lock);
static DEFINE_SPINLOCK(uart1_lock);
static DEFINE_SPINLOCK(uart2_lock);
static const char * const uart_parent_names[] = {"uart_pll", "vctcxo"};

static DEFINE_SPINLOCK(ssp0_lock);
static DEFINE_SPINLOCK(ssp1_lock);
static DEFINE_SPINLOCK(ssp2_lock);
static DEFINE_SPINLOCK(ssp3_lock);
static const char * const ssp_parent_names[] = {"vctcxo_4", "vctcxo_2", "vctcxo", "pll1_16"};

static DEFINE_SPINLOCK(timer_lock);
static const char * const timer_parent_names[] = {"clk32", "vctcxo_4", "vctcxo_2", "vctcxo"};

static DEFINE_SPINLOCK(reset_lock);

static struct mmp_param_mux_clk apbc_mux_clks[] = {
	{0, "uart0_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, APBC_UART0, 4, 3, 0, &uart0_lock},
	{0, "uart1_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, APBC_UART1, 4, 3, 0, &uart1_lock},
	{0, "uart2_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, APBC_UART2, 4, 3, 0, &uart2_lock},
	{0, "uart3_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, APBC_UART3, 4, 3, 0, &uart2_lock},
	{0, "ssp0_mux", ssp_parent_names, ARRAY_SIZE(ssp_parent_names), CLK_SET_RATE_PARENT, APBC_SSP0, 4, 3, 0, &ssp0_lock},
	{0, "ssp1_mux", ssp_parent_names, ARRAY_SIZE(ssp_parent_names), CLK_SET_RATE_PARENT, APBC_SSP1, 4, 3, 0, &ssp1_lock},
	{0, "ssp2_mux", ssp_parent_names, ARRAY_SIZE(ssp_parent_names), CLK_SET_RATE_PARENT, APBC_SSP2, 4, 3, 0, &ssp2_lock},
	{0, "ssp3_mux", ssp_parent_names, ARRAY_SIZE(ssp_parent_names), CLK_SET_RATE_PARENT, APBC_SSP3, 4, 3, 0, &ssp3_lock},
	{0, "timer_mux", timer_parent_names, ARRAY_SIZE(timer_parent_names), CLK_SET_RATE_PARENT, APBC_TIMER, 4, 3, 0, &timer_lock},
};

static struct mmp_param_gate_clk apbc_gate_clks[] = {
	{MMP2_CLK_TWSI0, "twsi0_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_TWSI0, 0x7, 0x3, 0x0, 0, &reset_lock},
	{MMP2_CLK_TWSI1, "twsi1_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_TWSI1, 0x7, 0x3, 0x0, 0, &reset_lock},
	{MMP2_CLK_TWSI2, "twsi2_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_TWSI2, 0x7, 0x3, 0x0, 0, &reset_lock},
	{MMP2_CLK_TWSI3, "twsi3_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_TWSI3, 0x7, 0x3, 0x0, 0, &reset_lock},
	{MMP2_CLK_TWSI4, "twsi4_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_TWSI4, 0x7, 0x3, 0x0, 0, &reset_lock},
	{MMP2_CLK_TWSI5, "twsi5_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_TWSI5, 0x7, 0x3, 0x0, 0, &reset_lock},
	{MMP2_CLK_GPIO, "gpio_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_GPIO, 0x7, 0x3, 0x0, 0, &reset_lock},
	{MMP2_CLK_KPC, "kpc_clk", "clk32", CLK_SET_RATE_PARENT, APBC_KPC, 0x7, 0x3, 0x0, MMP_CLK_GATE_NEED_DELAY, &reset_lock},
	{MMP2_CLK_RTC, "rtc_clk", "clk32", CLK_SET_RATE_PARENT, APBC_RTC, 0x87, 0x83, 0x0, MMP_CLK_GATE_NEED_DELAY, &reset_lock},
	{MMP2_CLK_PWM0, "pwm0_clk", "pll1_48", CLK_SET_RATE_PARENT, APBC_PWM0, 0x7, 0x3, 0x0, 0, &reset_lock},
	{MMP2_CLK_PWM1, "pwm1_clk", "pll1_48", CLK_SET_RATE_PARENT, APBC_PWM1, 0x7, 0x3, 0x0, 0, &reset_lock},
	{MMP2_CLK_PWM2, "pwm2_clk", "pll1_48", CLK_SET_RATE_PARENT, APBC_PWM2, 0x7, 0x3, 0x0, 0, &reset_lock},
	{MMP2_CLK_PWM3, "pwm3_clk", "pll1_48", CLK_SET_RATE_PARENT, APBC_PWM3, 0x7, 0x3, 0x0, 0, &reset_lock},
	/* The gate clocks has mux parent. */
	{MMP2_CLK_UART0, "uart0_clk", "uart0_mux", CLK_SET_RATE_PARENT, APBC_UART0, 0x7, 0x3, 0x0, 0, &uart0_lock},
	{MMP2_CLK_UART1, "uart1_clk", "uart1_mux", CLK_SET_RATE_PARENT, APBC_UART1, 0x7, 0x3, 0x0, 0, &uart1_lock},
	{MMP2_CLK_UART2, "uart2_clk", "uart2_mux", CLK_SET_RATE_PARENT, APBC_UART2, 0x7, 0x3, 0x0, 0, &uart2_lock},
	{MMP2_CLK_UART3, "uart3_clk", "uart3_mux", CLK_SET_RATE_PARENT, APBC_UART3, 0x7, 0x3, 0x0, 0, &uart2_lock},
	{MMP2_CLK_SSP0, "ssp0_clk", "ssp0_mux", CLK_SET_RATE_PARENT, APBC_SSP0, 0x7, 0x3, 0x0, 0, &ssp0_lock},
	{MMP2_CLK_SSP1, "ssp1_clk", "ssp1_mux", CLK_SET_RATE_PARENT, APBC_SSP1, 0x7, 0x3, 0x0, 0, &ssp1_lock},
	{MMP2_CLK_SSP2, "ssp2_clk", "ssp2_mux", CLK_SET_RATE_PARENT, APBC_SSP2, 0x7, 0x3, 0x0, 0, &ssp2_lock},
	{MMP2_CLK_SSP3, "ssp3_clk", "ssp3_mux", CLK_SET_RATE_PARENT, APBC_SSP3, 0x7, 0x3, 0x0, 0, &ssp3_lock},
	{MMP2_CLK_TIMER, "timer_clk", "timer_mux", CLK_SET_RATE_PARENT, APBC_TIMER, 0x7, 0x3, 0x0, 0, &timer_lock},
	{MMP2_CLK_THERMAL0, "thermal0_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_THERMAL0, 0x7, 0x3, 0x0, MMP_CLK_GATE_NEED_DELAY, &reset_lock},
};

static struct mmp_param_gate_clk mmp3_apbc_gate_clks[] = {
	{MMP3_CLK_THERMAL1, "thermal1_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_THERMAL1, 0x7, 0x3, 0x0, MMP_CLK_GATE_NEED_DELAY, &reset_lock},
	{MMP3_CLK_THERMAL2, "thermal2_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_THERMAL2, 0x7, 0x3, 0x0, MMP_CLK_GATE_NEED_DELAY, &reset_lock},
	{MMP3_CLK_THERMAL3, "thermal3_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_THERMAL3, 0x7, 0x3, 0x0, MMP_CLK_GATE_NEED_DELAY, &reset_lock},
};

static void mmp2_apb_periph_clk_init(struct mmp2_clk_unit *pxa_unit)
{
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_mux_clks(unit, apbc_mux_clks, pxa_unit->apbc_base,
				ARRAY_SIZE(apbc_mux_clks));

	mmp_register_gate_clks(unit, apbc_gate_clks, pxa_unit->apbc_base,
				ARRAY_SIZE(apbc_gate_clks));

	if (pxa_unit->model == CLK_MODEL_MMP3) {
		mmp_register_gate_clks(unit, mmp3_apbc_gate_clks, pxa_unit->apbc_base,
					ARRAY_SIZE(mmp3_apbc_gate_clks));
	}
}

static DEFINE_SPINLOCK(sdh_lock);
static const char * const sdh_parent_names[] = {"pll1_4", "pll2", "usb_pll", "pll1"};
static struct mmp_clk_mix_config sdh_mix_config = {
	.reg_info = DEFINE_MIX_REG_INFO(4, 10, 2, 8, 32),
};

static DEFINE_SPINLOCK(usb_lock);
static DEFINE_SPINLOCK(usbhsic0_lock);
static DEFINE_SPINLOCK(usbhsic1_lock);

static DEFINE_SPINLOCK(disp0_lock);
static DEFINE_SPINLOCK(disp1_lock);
static const char * const disp_parent_names[] = {"pll1", "pll1_16", "pll2", "vctcxo"};

static DEFINE_SPINLOCK(ccic0_lock);
static DEFINE_SPINLOCK(ccic1_lock);
static const char * const ccic_parent_names[] = {"pll1_2", "pll1_16", "vctcxo"};

static DEFINE_SPINLOCK(gpu_lock);
static const char * const mmp2_gpu_gc_parent_names[] =  {"pll1_2", "pll1_3", "pll2_2", "pll2_3", "pll2", "usb_pll"};
static const u32 mmp2_gpu_gc_parent_table[] = { 0x0000,   0x0040,   0x0080,   0x00c0,   0x1000, 0x1040   };
static const char * const mmp2_gpu_bus_parent_names[] = {"pll1_4", "pll2",   "pll2_2", "usb_pll"};
static const u32 mmp2_gpu_bus_parent_table[] = { 0x0000,   0x0020,   0x0030,   0x4020   };
static const char * const mmp3_gpu_bus_parent_names[] = {"pll1_4", "pll1_6", "pll1_2", "pll2_2"};
static const char * const mmp3_gpu_gc_parent_names[] =  {"pll1",   "pll2",   "pll1_p", "pll2_p"};

static DEFINE_SPINLOCK(audio_lock);

static struct mmp_clk_mix_config ccic0_mix_config = {
	.reg_info = DEFINE_MIX_REG_INFO(4, 17, 2, 6, 32),
};
static struct mmp_clk_mix_config ccic1_mix_config = {
	.reg_info = DEFINE_MIX_REG_INFO(4, 16, 2, 6, 32),
};

static struct mmp_param_mux_clk apmu_mux_clks[] = {
	{MMP2_CLK_DISP0_MUX, "disp0_mux", disp_parent_names, ARRAY_SIZE(disp_parent_names), CLK_SET_RATE_PARENT, APMU_DISP0, 6, 2, 0, &disp0_lock},
	{MMP2_CLK_DISP1_MUX, "disp1_mux", disp_parent_names, ARRAY_SIZE(disp_parent_names), CLK_SET_RATE_PARENT, APMU_DISP1, 6, 2, 0, &disp1_lock},
};

static struct mmp_param_mux_clk mmp3_apmu_mux_clks[] = {
	{0, "gpu_bus_mux", mmp3_gpu_bus_parent_names, ARRAY_SIZE(mmp3_gpu_bus_parent_names),
									CLK_SET_RATE_PARENT, APMU_GPU, 4, 2, 0, &gpu_lock},
	{0, "gpu_3d_mux", mmp3_gpu_gc_parent_names, ARRAY_SIZE(mmp3_gpu_gc_parent_names),
									CLK_SET_RATE_PARENT, APMU_GPU, 6, 2, 0, &gpu_lock},
	{0, "gpu_2d_mux", mmp3_gpu_gc_parent_names, ARRAY_SIZE(mmp3_gpu_gc_parent_names),
									CLK_SET_RATE_PARENT, APMU_GPU, 12, 2, 0, &gpu_lock},
};

static struct mmp_param_div_clk apmu_div_clks[] = {
	{0, "disp0_div", "disp0_mux", CLK_SET_RATE_PARENT, APMU_DISP0, 8, 4, CLK_DIVIDER_ONE_BASED, &disp0_lock},
	{0, "disp0_sphy_div", "disp0_mux", CLK_SET_RATE_PARENT, APMU_DISP0, 15, 5, 0, &disp0_lock},
	{0, "disp1_div", "disp1_mux", CLK_SET_RATE_PARENT, APMU_DISP1, 8, 4, CLK_DIVIDER_ONE_BASED, &disp1_lock},
	{0, "ccic0_sphy_div", "ccic0_mix_clk", CLK_SET_RATE_PARENT, APMU_CCIC0, 10, 5, 0, &ccic0_lock},
	{0, "ccic1_sphy_div", "ccic1_mix_clk", CLK_SET_RATE_PARENT, APMU_CCIC1, 10, 5, 0, &ccic1_lock},
};

static struct mmp_param_div_clk mmp3_apmu_div_clks[] = {
	{0, "gpu_3d_div", "gpu_3d_mux", CLK_SET_RATE_PARENT, APMU_GPU, 24, 4, 0, &gpu_lock},
	{0, "gpu_2d_div", "gpu_2d_mux", CLK_SET_RATE_PARENT, APMU_GPU, 28, 4, 0, &gpu_lock},
};

static struct mmp_param_gate_clk apmu_gate_clks[] = {
	{MMP2_CLK_USB, "usb_clk", "usb_pll", 0, APMU_USB, 0x9, 0x9, 0x0, 0, &usb_lock},
	{MMP2_CLK_USBHSIC0, "usbhsic0_clk", "usb_pll", 0, APMU_USBHSIC0, 0x1b, 0x1b, 0x0, 0, &usbhsic0_lock},
	{MMP2_CLK_USBHSIC1, "usbhsic1_clk", "usb_pll", 0, APMU_USBHSIC1, 0x1b, 0x1b, 0x0, 0, &usbhsic1_lock},
	/* The gate clocks has mux parent. */
	{MMP2_CLK_SDH0, "sdh0_clk", "sdh_mix_clk", CLK_SET_RATE_PARENT, APMU_SDH0, 0x1b, 0x1b, 0x0, 0, &sdh_lock},
	{MMP2_CLK_SDH1, "sdh1_clk", "sdh_mix_clk", CLK_SET_RATE_PARENT, APMU_SDH1, 0x1b, 0x1b, 0x0, 0, &sdh_lock},
	{MMP2_CLK_SDH2, "sdh2_clk", "sdh_mix_clk", CLK_SET_RATE_PARENT, APMU_SDH2, 0x1b, 0x1b, 0x0, 0, &sdh_lock},
	{MMP2_CLK_SDH3, "sdh3_clk", "sdh_mix_clk", CLK_SET_RATE_PARENT, APMU_SDH3, 0x1b, 0x1b, 0x0, 0, &sdh_lock},
	{MMP2_CLK_DISP0, "disp0_clk", "disp0_div", CLK_SET_RATE_PARENT, APMU_DISP0, 0x12, 0x12, 0x0, 0, &disp0_lock},
	{MMP2_CLK_DISP0_LCDC, "disp0_lcdc_clk", "disp0_mux", CLK_SET_RATE_PARENT, APMU_DISP0, 0x09, 0x09, 0x0, 0, &disp0_lock},
	{MMP2_CLK_DISP0_SPHY, "disp0_sphy_clk", "disp0_sphy_div", CLK_SET_RATE_PARENT, APMU_DISP0, 0x1024, 0x1024, 0x0, 0, &disp0_lock},
	{MMP2_CLK_DISP1, "disp1_clk", "disp1_div", CLK_SET_RATE_PARENT, APMU_DISP1, 0x09, 0x09, 0x0, 0, &disp1_lock},
	{MMP2_CLK_CCIC_ARBITER, "ccic_arbiter", "vctcxo", CLK_SET_RATE_PARENT, APMU_CCIC0, 0x1800, 0x1800, 0x0, 0, &ccic0_lock},
	{MMP2_CLK_CCIC0, "ccic0_clk", "ccic0_mix_clk", CLK_SET_RATE_PARENT, APMU_CCIC0, 0x1b, 0x1b, 0x0, 0, &ccic0_lock},
	{MMP2_CLK_CCIC0_PHY, "ccic0_phy_clk", "ccic0_mix_clk", CLK_SET_RATE_PARENT, APMU_CCIC0, 0x24, 0x24, 0x0, 0, &ccic0_lock},
	{MMP2_CLK_CCIC0_SPHY, "ccic0_sphy_clk", "ccic0_sphy_div", CLK_SET_RATE_PARENT, APMU_CCIC0, 0x300, 0x300, 0x0, 0, &ccic0_lock},
	{MMP2_CLK_CCIC1, "ccic1_clk", "ccic1_mix_clk", CLK_SET_RATE_PARENT, APMU_CCIC1, 0x1b, 0x1b, 0x0, 0, &ccic1_lock},
	{MMP2_CLK_CCIC1_PHY, "ccic1_phy_clk", "ccic1_mix_clk", CLK_SET_RATE_PARENT, APMU_CCIC1, 0x24, 0x24, 0x0, 0, &ccic1_lock},
	{MMP2_CLK_CCIC1_SPHY, "ccic1_sphy_clk", "ccic1_sphy_div", CLK_SET_RATE_PARENT, APMU_CCIC1, 0x300, 0x300, 0x0, 0, &ccic1_lock},
	{MMP2_CLK_GPU_BUS, "gpu_bus_clk", "gpu_bus_mux", CLK_SET_RATE_PARENT, APMU_GPU, 0xa, 0xa, 0x0, MMP_CLK_GATE_NEED_DELAY, &gpu_lock},
	{MMP2_CLK_AUDIO, "audio_clk", "audio_mix_clk", CLK_SET_RATE_PARENT, APMU_AUDIO, 0x12, 0x12, 0x0, 0, &audio_lock},
};

static struct mmp_param_gate_clk mmp2_apmu_gate_clks[] = {
	{MMP2_CLK_GPU_3D, "gpu_3d_clk", "gpu_3d_mux", CLK_SET_RATE_PARENT, APMU_GPU, 0x5, 0x5, 0x0, MMP_CLK_GATE_NEED_DELAY, &gpu_lock},
};

static struct mmp_param_gate_clk mmp3_apmu_gate_clks[] = {
	{MMP3_CLK_SDH4, "sdh4_clk", "sdh_mix_clk", CLK_SET_RATE_PARENT, APMU_SDH4, 0x1b, 0x1b, 0x0, 0, &sdh_lock},
	{MMP3_CLK_GPU_3D, "gpu_3d_clk", "gpu_3d_div", CLK_SET_RATE_PARENT, APMU_GPU, 0x5, 0x5, 0x0, MMP_CLK_GATE_NEED_DELAY, &gpu_lock},
	{MMP3_CLK_GPU_2D, "gpu_2d_clk", "gpu_2d_div", CLK_SET_RATE_PARENT, APMU_GPU, 0x1c0000, 0x1c0000, 0x0, MMP_CLK_GATE_NEED_DELAY, &gpu_lock},
};

static void mmp2_axi_periph_clk_init(struct mmp2_clk_unit *pxa_unit)
{
	struct clk *clk;
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	sdh_mix_config.reg_info.reg_clk_ctrl = pxa_unit->apmu_base + APMU_SDH0;
	clk = mmp_clk_register_mix(NULL, "sdh_mix_clk", sdh_parent_names,
					ARRAY_SIZE(sdh_parent_names),
					CLK_SET_RATE_PARENT,
					&sdh_mix_config, &sdh_lock);

	ccic0_mix_config.reg_info.reg_clk_ctrl = pxa_unit->apmu_base + APMU_CCIC0;
	clk = mmp_clk_register_mix(NULL, "ccic0_mix_clk", ccic_parent_names,
					ARRAY_SIZE(ccic_parent_names),
					CLK_SET_RATE_PARENT,
					&ccic0_mix_config, &ccic0_lock);
	mmp_clk_add(unit, MMP2_CLK_CCIC0_MIX, clk);

	ccic1_mix_config.reg_info.reg_clk_ctrl = pxa_unit->apmu_base + APMU_CCIC1;
	clk = mmp_clk_register_mix(NULL, "ccic1_mix_clk", ccic_parent_names,
					ARRAY_SIZE(ccic_parent_names),
					CLK_SET_RATE_PARENT,
					&ccic1_mix_config, &ccic1_lock);
	mmp_clk_add(unit, MMP2_CLK_CCIC1_MIX, clk);

	mmp_register_mux_clks(unit, apmu_mux_clks, pxa_unit->apmu_base,
				ARRAY_SIZE(apmu_mux_clks));

	mmp_register_div_clks(unit, apmu_div_clks, pxa_unit->apmu_base,
				ARRAY_SIZE(apmu_div_clks));

	mmp_register_gate_clks(unit, apmu_gate_clks, pxa_unit->apmu_base,
				ARRAY_SIZE(apmu_gate_clks));

	if (pxa_unit->model == CLK_MODEL_MMP3) {
		mmp_register_mux_clks(unit, mmp3_apmu_mux_clks, pxa_unit->apmu_base,
					ARRAY_SIZE(mmp3_apmu_mux_clks));

		mmp_register_div_clks(unit, mmp3_apmu_div_clks, pxa_unit->apmu_base,
					ARRAY_SIZE(mmp3_apmu_div_clks));

		mmp_register_gate_clks(unit, mmp3_apmu_gate_clks, pxa_unit->apmu_base,
					ARRAY_SIZE(mmp3_apmu_gate_clks));
	} else {
		clk_register_mux_table(NULL, "gpu_3d_mux", mmp2_gpu_gc_parent_names,
					ARRAY_SIZE(mmp2_gpu_gc_parent_names),
					CLK_SET_RATE_PARENT,
					pxa_unit->apmu_base + APMU_GPU,
					0, 0x10c0, 0,
					mmp2_gpu_gc_parent_table, &gpu_lock);

		clk_register_mux_table(NULL, "gpu_bus_mux", mmp2_gpu_bus_parent_names,
					ARRAY_SIZE(mmp2_gpu_bus_parent_names),
					CLK_SET_RATE_PARENT,
					pxa_unit->apmu_base + APMU_GPU,
					0, 0x4030, 0,
					mmp2_gpu_bus_parent_table, &gpu_lock);

		mmp_register_gate_clks(unit, mmp2_apmu_gate_clks, pxa_unit->apmu_base,
					ARRAY_SIZE(mmp2_apmu_gate_clks));
	}
}

static void mmp2_clk_reset_init(struct device_node *np,
				struct mmp2_clk_unit *pxa_unit)
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

static void mmp2_pm_domain_init(struct device_node *np,
				struct mmp2_clk_unit *pxa_unit)
{
	if (pxa_unit->model == CLK_MODEL_MMP3) {
		pxa_unit->pm_domains[MMP2_POWER_DOMAIN_GPU]
			= mmp_pm_domain_register("gpu",
				pxa_unit->apmu_base + APMU_GPU,
				0x0600, 0x40003, 0x18000c, 0, &gpu_lock);
	} else {
		pxa_unit->pm_domains[MMP2_POWER_DOMAIN_GPU]
			= mmp_pm_domain_register("gpu",
				pxa_unit->apmu_base + APMU_GPU,
				0x8600, 0x00003, 0x00000c,
				MMP_PM_DOMAIN_NO_DISABLE, &gpu_lock);
	}
	pxa_unit->pd_data.num_domains++;

	pxa_unit->pm_domains[MMP2_POWER_DOMAIN_AUDIO]
		= mmp_pm_domain_register("audio",
			pxa_unit->apmu_base + APMU_AUDIO,
			0x600, 0x2, 0, 0, &audio_lock);
	pxa_unit->pd_data.num_domains++;

	if (pxa_unit->model == CLK_MODEL_MMP3) {
		pxa_unit->pm_domains[MMP3_POWER_DOMAIN_CAMERA]
			= mmp_pm_domain_register("camera",
				pxa_unit->apmu_base + APMU_CAMERA,
				0x600, 0, 0, 0, NULL);
		pxa_unit->pd_data.num_domains++;
	}

	pxa_unit->pd_data.domains = pxa_unit->pm_domains;
	of_genpd_add_provider_onecell(np, &pxa_unit->pd_data);
}

static void __init mmp2_clk_init(struct device_node *np)
{
	struct mmp2_clk_unit *pxa_unit;

	pxa_unit = kzalloc(sizeof(*pxa_unit), GFP_KERNEL);
	if (!pxa_unit)
		return;

	if (of_device_is_compatible(np, "marvell,mmp3-clock"))
		pxa_unit->model = CLK_MODEL_MMP3;
	else
		pxa_unit->model = CLK_MODEL_MMP2;

	pxa_unit->mpmu_base = of_iomap(np, 0);
	if (!pxa_unit->mpmu_base) {
		pr_err("failed to map mpmu registers\n");
		goto free_memory;
	}

	pxa_unit->apmu_base = of_iomap(np, 1);
	if (!pxa_unit->apmu_base) {
		pr_err("failed to map apmu registers\n");
		goto unmap_mpmu_region;
	}

	pxa_unit->apbc_base = of_iomap(np, 2);
	if (!pxa_unit->apbc_base) {
		pr_err("failed to map apbc registers\n");
		goto unmap_apmu_region;
	}

	mmp2_pm_domain_init(np, pxa_unit);

	mmp_clk_init(np, &pxa_unit->unit, NR_CLKS);

	mmp2_main_clk_init(pxa_unit);

	mmp2_apb_periph_clk_init(pxa_unit);

	mmp2_axi_periph_clk_init(pxa_unit);

	mmp2_clk_reset_init(np, pxa_unit);

	return;

unmap_apmu_region:
	iounmap(pxa_unit->apmu_base);
unmap_mpmu_region:
	iounmap(pxa_unit->mpmu_base);
free_memory:
	kfree(pxa_unit);
}

CLK_OF_DECLARE(mmp2_clk, "marvell,mmp2-clock", mmp2_clk_init);
CLK_OF_DECLARE(mmp3_clk, "marvell,mmp3-clock", mmp2_clk_init);
