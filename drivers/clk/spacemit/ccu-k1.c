// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 SpacemiT Technology Co. Ltd
 * Copyright (c) 2024-2025 Haylen Chu <heylenay@4d2.org>
 */

#include <linux/array_size.h>
#include <linux/auxiliary_bus.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/idr.h>
#include <linux/mfd/syscon.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <soc/spacemit/k1-syscon.h>

#include "ccu_common.h"
#include "ccu_pll.h"
#include "ccu_mix.h"
#include "ccu_ddn.h"

#include <dt-bindings/clock/spacemit,k1-syscon.h>

struct spacemit_ccu_data {
	const char *reset_name;
	struct clk_hw **hws;
	size_t num;
};

static DEFINE_IDA(auxiliary_ids);

/* APBS clocks start, APBS region contains and only contains all PLL clocks */

/*
 * PLL{1,2} must run at fixed frequencies to provide clocks in correct rates for
 * peripherals.
 */
static const struct ccu_pll_rate_tbl pll1_rate_tbl[] = {
	CCU_PLL_RATE(2457600000UL, 0x0050dd64, 0x330ccccd),
};

static const struct ccu_pll_rate_tbl pll2_rate_tbl[] = {
	CCU_PLL_RATE(3000000000UL, 0x0050dd66, 0x3fe00000),
};

static const struct ccu_pll_rate_tbl pll3_rate_tbl[] = {
	CCU_PLL_RATE(1600000000UL, 0x0050cd61, 0x43eaaaab),
	CCU_PLL_RATE(1800000000UL, 0x0050cd61, 0x4b000000),
	CCU_PLL_RATE(2000000000UL, 0x0050dd62, 0x2aeaaaab),
	CCU_PLL_RATE(2457600000UL, 0x0050dd64, 0x330ccccd),
	CCU_PLL_RATE(3000000000UL, 0x0050dd66, 0x3fe00000),
	CCU_PLL_RATE(3200000000UL, 0x0050dd67, 0x43eaaaab),
};

CCU_PLL_DEFINE(pll1, pll1_rate_tbl, APBS_PLL1_SWCR1, APBS_PLL1_SWCR3, MPMU_POSR, POSR_PLL1_LOCK,
	       CLK_SET_RATE_GATE);
CCU_PLL_DEFINE(pll2, pll2_rate_tbl, APBS_PLL2_SWCR1, APBS_PLL2_SWCR3, MPMU_POSR, POSR_PLL2_LOCK,
	       CLK_SET_RATE_GATE);
CCU_PLL_DEFINE(pll3, pll3_rate_tbl, APBS_PLL3_SWCR1, APBS_PLL3_SWCR3, MPMU_POSR, POSR_PLL3_LOCK,
	       CLK_SET_RATE_GATE);

CCU_FACTOR_GATE_DEFINE(pll1_d2, CCU_PARENT_HW(pll1), APBS_PLL1_SWCR2, BIT(1), 2, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d3, CCU_PARENT_HW(pll1), APBS_PLL1_SWCR2, BIT(2), 3, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d4, CCU_PARENT_HW(pll1), APBS_PLL1_SWCR2, BIT(3), 4, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d5, CCU_PARENT_HW(pll1), APBS_PLL1_SWCR2, BIT(4), 5, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d6, CCU_PARENT_HW(pll1), APBS_PLL1_SWCR2, BIT(5), 6, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d7, CCU_PARENT_HW(pll1), APBS_PLL1_SWCR2, BIT(6), 7, 1);
CCU_FACTOR_GATE_FLAGS_DEFINE(pll1_d8, CCU_PARENT_HW(pll1), APBS_PLL1_SWCR2, BIT(7), 8, 1,
		CLK_IS_CRITICAL);
CCU_FACTOR_GATE_DEFINE(pll1_d11_223p4, CCU_PARENT_HW(pll1), APBS_PLL1_SWCR2, BIT(15), 11, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d13_189, CCU_PARENT_HW(pll1), APBS_PLL1_SWCR2, BIT(16), 13, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d23_106p8, CCU_PARENT_HW(pll1), APBS_PLL1_SWCR2, BIT(20), 23, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d64_38p4, CCU_PARENT_HW(pll1), APBS_PLL1_SWCR2, BIT(0), 64, 1);
CCU_FACTOR_GATE_DEFINE(pll1_aud_245p7, CCU_PARENT_HW(pll1), APBS_PLL1_SWCR2, BIT(10), 10, 1);
CCU_FACTOR_GATE_DEFINE(pll1_aud_24p5, CCU_PARENT_HW(pll1), APBS_PLL1_SWCR2, BIT(11), 100, 1);

CCU_FACTOR_GATE_DEFINE(pll2_d1, CCU_PARENT_HW(pll2), APBS_PLL2_SWCR2, BIT(0), 1, 1);
CCU_FACTOR_GATE_DEFINE(pll2_d2, CCU_PARENT_HW(pll2), APBS_PLL2_SWCR2, BIT(1), 2, 1);
CCU_FACTOR_GATE_DEFINE(pll2_d3, CCU_PARENT_HW(pll2), APBS_PLL2_SWCR2, BIT(2), 3, 1);
CCU_FACTOR_GATE_DEFINE(pll2_d4, CCU_PARENT_HW(pll2), APBS_PLL2_SWCR2, BIT(3), 4, 1);
CCU_FACTOR_GATE_DEFINE(pll2_d5, CCU_PARENT_HW(pll2), APBS_PLL2_SWCR2, BIT(4), 5, 1);
CCU_FACTOR_GATE_DEFINE(pll2_d6, CCU_PARENT_HW(pll2), APBS_PLL2_SWCR2, BIT(5), 6, 1);
CCU_FACTOR_GATE_DEFINE(pll2_d7, CCU_PARENT_HW(pll2), APBS_PLL2_SWCR2, BIT(6), 7, 1);
CCU_FACTOR_GATE_DEFINE(pll2_d8, CCU_PARENT_HW(pll2), APBS_PLL2_SWCR2, BIT(7), 8, 1);

CCU_FACTOR_GATE_DEFINE(pll3_d1, CCU_PARENT_HW(pll3), APBS_PLL3_SWCR2, BIT(0), 1, 1);
CCU_FACTOR_GATE_DEFINE(pll3_d2, CCU_PARENT_HW(pll3), APBS_PLL3_SWCR2, BIT(1), 2, 1);
CCU_FACTOR_GATE_DEFINE(pll3_d3, CCU_PARENT_HW(pll3), APBS_PLL3_SWCR2, BIT(2), 3, 1);
CCU_FACTOR_GATE_DEFINE(pll3_d4, CCU_PARENT_HW(pll3), APBS_PLL3_SWCR2, BIT(3), 4, 1);
CCU_FACTOR_GATE_DEFINE(pll3_d5, CCU_PARENT_HW(pll3), APBS_PLL3_SWCR2, BIT(4), 5, 1);
CCU_FACTOR_GATE_DEFINE(pll3_d6, CCU_PARENT_HW(pll3), APBS_PLL3_SWCR2, BIT(5), 6, 1);
CCU_FACTOR_GATE_DEFINE(pll3_d7, CCU_PARENT_HW(pll3), APBS_PLL3_SWCR2, BIT(6), 7, 1);
CCU_FACTOR_GATE_DEFINE(pll3_d8, CCU_PARENT_HW(pll3), APBS_PLL3_SWCR2, BIT(7), 8, 1);

CCU_FACTOR_DEFINE(pll3_20, CCU_PARENT_HW(pll3_d8), 20, 1);
CCU_FACTOR_DEFINE(pll3_40, CCU_PARENT_HW(pll3_d8), 10, 1);
CCU_FACTOR_DEFINE(pll3_80, CCU_PARENT_HW(pll3_d8), 5, 1);

/* APBS clocks end */

/* MPMU clocks start */
CCU_GATE_DEFINE(pll1_d8_307p2, CCU_PARENT_HW(pll1_d8), MPMU_ACGR, BIT(13), 0);

CCU_FACTOR_DEFINE(pll1_d32_76p8, CCU_PARENT_HW(pll1_d8_307p2), 4, 1);

CCU_FACTOR_DEFINE(pll1_d40_61p44, CCU_PARENT_HW(pll1_d8_307p2), 5, 1);

CCU_FACTOR_DEFINE(pll1_d16_153p6, CCU_PARENT_HW(pll1_d8), 2, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d24_102p4, CCU_PARENT_HW(pll1_d8), MPMU_ACGR, BIT(12), 3, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d48_51p2, CCU_PARENT_HW(pll1_d8), MPMU_ACGR, BIT(7), 6, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d48_51p2_ap, CCU_PARENT_HW(pll1_d8), MPMU_ACGR, BIT(11), 6, 1);
CCU_FACTOR_GATE_DEFINE(pll1_m3d128_57p6, CCU_PARENT_HW(pll1_d8), MPMU_ACGR, BIT(8), 16, 3);
CCU_FACTOR_GATE_DEFINE(pll1_d96_25p6, CCU_PARENT_HW(pll1_d8), MPMU_ACGR, BIT(4), 12, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d192_12p8, CCU_PARENT_HW(pll1_d8), MPMU_ACGR, BIT(3), 24, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d192_12p8_wdt, CCU_PARENT_HW(pll1_d8), MPMU_ACGR, BIT(19), 24, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d384_6p4, CCU_PARENT_HW(pll1_d8), MPMU_ACGR, BIT(2), 48, 1);

CCU_FACTOR_DEFINE(pll1_d768_3p2, CCU_PARENT_HW(pll1_d384_6p4), 2, 1);
CCU_FACTOR_DEFINE(pll1_d1536_1p6, CCU_PARENT_HW(pll1_d384_6p4), 4, 1);
CCU_FACTOR_DEFINE(pll1_d3072_0p8, CCU_PARENT_HW(pll1_d384_6p4), 8, 1);

CCU_GATE_DEFINE(pll1_d6_409p6, CCU_PARENT_HW(pll1_d6), MPMU_ACGR, BIT(0), 0);
CCU_FACTOR_GATE_DEFINE(pll1_d12_204p8, CCU_PARENT_HW(pll1_d6), MPMU_ACGR, BIT(5), 2, 1);

CCU_GATE_DEFINE(pll1_d5_491p52, CCU_PARENT_HW(pll1_d5), MPMU_ACGR, BIT(21), 0);
CCU_FACTOR_GATE_DEFINE(pll1_d10_245p76, CCU_PARENT_HW(pll1_d5), MPMU_ACGR, BIT(18), 2, 1);

CCU_GATE_DEFINE(pll1_d4_614p4, CCU_PARENT_HW(pll1_d4), MPMU_ACGR, BIT(15), 0);
CCU_FACTOR_GATE_DEFINE(pll1_d52_47p26, CCU_PARENT_HW(pll1_d4), MPMU_ACGR, BIT(10), 13, 1);
CCU_FACTOR_GATE_DEFINE(pll1_d78_31p5, CCU_PARENT_HW(pll1_d4), MPMU_ACGR, BIT(6), 39, 2);

CCU_GATE_DEFINE(pll1_d3_819p2, CCU_PARENT_HW(pll1_d3), MPMU_ACGR, BIT(14), 0);

CCU_GATE_DEFINE(pll1_d2_1228p8, CCU_PARENT_HW(pll1_d2), MPMU_ACGR, BIT(16), 0);

CCU_GATE_DEFINE(slow_uart, CCU_PARENT_NAME(osc), MPMU_ACGR, BIT(1), CLK_IGNORE_UNUSED);
CCU_DDN_DEFINE(slow_uart1_14p74, pll1_d16_153p6, MPMU_SUCCR, 16, 13, 0, 13, 2, 0);
CCU_DDN_DEFINE(slow_uart2_48, pll1_d4_614p4, MPMU_SUCCR_1, 16, 13, 0, 13, 2, 0);

CCU_GATE_DEFINE(wdt_clk, CCU_PARENT_HW(pll1_d96_25p6), MPMU_WDTPCR, BIT(1), 0);

CCU_FACTOR_DEFINE(i2s_153p6, CCU_PARENT_HW(pll1_d8_307p2), 2, 1);

static const struct clk_parent_data i2s_153p6_base_parents[] = {
	CCU_PARENT_HW(i2s_153p6),
	CCU_PARENT_HW(pll1_d8_307p2),
};
CCU_MUX_DEFINE(i2s_153p6_base, i2s_153p6_base_parents, MPMU_FCCR, 29, 1, 0);

static const struct clk_parent_data i2s_sysclk_src_parents[] = {
	CCU_PARENT_HW(pll1_d96_25p6),
	CCU_PARENT_HW(i2s_153p6_base)
};
CCU_MUX_GATE_DEFINE(i2s_sysclk_src, i2s_sysclk_src_parents, MPMU_ISCCR, 30, 1, BIT(31), 0);

CCU_DDN_DEFINE(i2s_sysclk, i2s_sysclk_src, MPMU_ISCCR, 0, 15, 15, 12, 1, 0);

CCU_FACTOR_DEFINE(i2s_bclk_factor, CCU_PARENT_HW(i2s_sysclk), 2, 1);
/*
 * Divider of i2s_bclk always implies a 1/2 factor, which is
 * described by i2s_bclk_factor.
 */
CCU_DIV_GATE_DEFINE(i2s_bclk, CCU_PARENT_HW(i2s_bclk_factor), MPMU_ISCCR, 27, 2, BIT(29), 0);

static const struct clk_parent_data apb_parents[] = {
	CCU_PARENT_HW(pll1_d96_25p6),
	CCU_PARENT_HW(pll1_d48_51p2),
	CCU_PARENT_HW(pll1_d96_25p6),
	CCU_PARENT_HW(pll1_d24_102p4),
};
CCU_MUX_DEFINE(apb_clk, apb_parents, MPMU_APBCSCR, 0, 2, 0);

CCU_GATE_DEFINE(wdt_bus_clk, CCU_PARENT_HW(apb_clk), MPMU_WDTPCR, BIT(0), 0);

CCU_GATE_DEFINE(ripc_clk, CCU_PARENT_HW(apb_clk), MPMU_RIPCCR, 0x1, 0);
/* MPMU clocks end */

/* APBC clocks start */
static const struct clk_parent_data uart_clk_parents[] = {
	CCU_PARENT_HW(pll1_m3d128_57p6),
	CCU_PARENT_HW(slow_uart1_14p74),
	CCU_PARENT_HW(slow_uart2_48),
};
CCU_MUX_GATE_DEFINE(uart0_clk, uart_clk_parents, APBC_UART1_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(uart2_clk, uart_clk_parents, APBC_UART2_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(uart3_clk, uart_clk_parents, APBC_UART3_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(uart4_clk, uart_clk_parents, APBC_UART4_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(uart5_clk, uart_clk_parents, APBC_UART5_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(uart6_clk, uart_clk_parents, APBC_UART6_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(uart7_clk, uart_clk_parents, APBC_UART7_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(uart8_clk, uart_clk_parents, APBC_UART8_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(uart9_clk, uart_clk_parents, APBC_UART9_CLK_RST, 4, 3, BIT(1), 0);

CCU_GATE_DEFINE(gpio_clk, CCU_PARENT_NAME(vctcxo_24m), APBC_GPIO_CLK_RST, BIT(1), 0);

static const struct clk_parent_data pwm_parents[] = {
	CCU_PARENT_HW(pll1_d192_12p8),
	CCU_PARENT_NAME(osc),
};
CCU_MUX_GATE_DEFINE(pwm0_clk, pwm_parents, APBC_PWM0_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm1_clk, pwm_parents, APBC_PWM1_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm2_clk, pwm_parents, APBC_PWM2_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm3_clk, pwm_parents, APBC_PWM3_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm4_clk, pwm_parents, APBC_PWM4_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm5_clk, pwm_parents, APBC_PWM5_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm6_clk, pwm_parents, APBC_PWM6_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm7_clk, pwm_parents, APBC_PWM7_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm8_clk, pwm_parents, APBC_PWM8_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm9_clk, pwm_parents, APBC_PWM9_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm10_clk, pwm_parents, APBC_PWM10_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm11_clk, pwm_parents, APBC_PWM11_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm12_clk, pwm_parents, APBC_PWM12_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm13_clk, pwm_parents, APBC_PWM13_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm14_clk, pwm_parents, APBC_PWM14_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm15_clk, pwm_parents, APBC_PWM15_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm16_clk, pwm_parents, APBC_PWM16_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm17_clk, pwm_parents, APBC_PWM17_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm18_clk, pwm_parents, APBC_PWM18_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(pwm19_clk, pwm_parents, APBC_PWM19_CLK_RST, 4, 3, BIT(1), 0);

static const struct clk_parent_data ssp_parents[] = {
	CCU_PARENT_HW(pll1_d384_6p4),
	CCU_PARENT_HW(pll1_d192_12p8),
	CCU_PARENT_HW(pll1_d96_25p6),
	CCU_PARENT_HW(pll1_d48_51p2),
	CCU_PARENT_HW(pll1_d768_3p2),
	CCU_PARENT_HW(pll1_d1536_1p6),
	CCU_PARENT_HW(pll1_d3072_0p8),
};
CCU_MUX_GATE_DEFINE(ssp3_clk, ssp_parents, APBC_SSP3_CLK_RST, 4, 3, BIT(1), 0);

CCU_GATE_DEFINE(rtc_clk, CCU_PARENT_NAME(osc), APBC_RTC_CLK_RST,
		BIT(7) | BIT(1), 0);

static const struct clk_parent_data twsi_parents[] = {
	CCU_PARENT_HW(pll1_d78_31p5),
	CCU_PARENT_HW(pll1_d48_51p2),
	CCU_PARENT_HW(pll1_d40_61p44),
};
CCU_MUX_GATE_DEFINE(twsi0_clk, twsi_parents, APBC_TWSI0_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(twsi1_clk, twsi_parents, APBC_TWSI1_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(twsi2_clk, twsi_parents, APBC_TWSI2_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(twsi4_clk, twsi_parents, APBC_TWSI4_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(twsi5_clk, twsi_parents, APBC_TWSI5_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(twsi6_clk, twsi_parents, APBC_TWSI6_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(twsi7_clk, twsi_parents, APBC_TWSI7_CLK_RST, 4, 3, BIT(1), 0);
/*
 * APBC_TWSI8_CLK_RST has a quirk that reading always results in zero.
 * Combine functional and bus bits together as a gate to avoid sharing the
 * write-only register between different clock hardwares.
 */
CCU_GATE_DEFINE(twsi8_clk, CCU_PARENT_HW(pll1_d78_31p5), APBC_TWSI8_CLK_RST, BIT(1) | BIT(0), 0);

static const struct clk_parent_data timer_parents[] = {
	CCU_PARENT_HW(pll1_d192_12p8),
	CCU_PARENT_NAME(osc),
	CCU_PARENT_HW(pll1_d384_6p4),
	CCU_PARENT_NAME(vctcxo_3m),
	CCU_PARENT_NAME(vctcxo_1m),
};
CCU_MUX_GATE_DEFINE(timers1_clk, timer_parents, APBC_TIMERS1_CLK_RST, 4, 3, BIT(1), 0);
CCU_MUX_GATE_DEFINE(timers2_clk, timer_parents, APBC_TIMERS2_CLK_RST, 4, 3, BIT(1), 0);

CCU_GATE_DEFINE(aib_clk, CCU_PARENT_NAME(vctcxo_24m), APBC_AIB_CLK_RST, BIT(1), 0);

CCU_GATE_DEFINE(onewire_clk, CCU_PARENT_NAME(vctcxo_24m), APBC_ONEWIRE_CLK_RST, BIT(1), 0);

/*
 * When i2s_bclk is selected as the parent clock of sspa,
 * the hardware requires bit3 to be set
 */
CCU_GATE_DEFINE(sspa0_i2s_bclk, CCU_PARENT_HW(i2s_bclk), APBC_SSPA0_CLK_RST, BIT(3), 0);
CCU_GATE_DEFINE(sspa1_i2s_bclk, CCU_PARENT_HW(i2s_bclk), APBC_SSPA1_CLK_RST, BIT(3), 0);

static const struct clk_parent_data sspa0_parents[] = {
	CCU_PARENT_HW(pll1_d384_6p4),
	CCU_PARENT_HW(pll1_d192_12p8),
	CCU_PARENT_HW(pll1_d96_25p6),
	CCU_PARENT_HW(pll1_d48_51p2),
	CCU_PARENT_HW(pll1_d768_3p2),
	CCU_PARENT_HW(pll1_d1536_1p6),
	CCU_PARENT_HW(pll1_d3072_0p8),
	CCU_PARENT_HW(sspa0_i2s_bclk),
};
CCU_MUX_GATE_DEFINE(sspa0_clk, sspa0_parents, APBC_SSPA0_CLK_RST, 4, 3, BIT(1), 0);

static const struct clk_parent_data sspa1_parents[] = {
	CCU_PARENT_HW(pll1_d384_6p4),
	CCU_PARENT_HW(pll1_d192_12p8),
	CCU_PARENT_HW(pll1_d96_25p6),
	CCU_PARENT_HW(pll1_d48_51p2),
	CCU_PARENT_HW(pll1_d768_3p2),
	CCU_PARENT_HW(pll1_d1536_1p6),
	CCU_PARENT_HW(pll1_d3072_0p8),
	CCU_PARENT_HW(sspa1_i2s_bclk),
};
CCU_MUX_GATE_DEFINE(sspa1_clk, sspa1_parents, APBC_SSPA1_CLK_RST, 4, 3, BIT(1), 0);

CCU_GATE_DEFINE(dro_clk, CCU_PARENT_HW(apb_clk), APBC_DRO_CLK_RST, BIT(1), 0);
CCU_GATE_DEFINE(ir_clk, CCU_PARENT_HW(apb_clk), APBC_IR_CLK_RST, BIT(1), 0);
CCU_GATE_DEFINE(tsen_clk, CCU_PARENT_HW(apb_clk), APBC_TSEN_CLK_RST, BIT(1), 0);
CCU_GATE_DEFINE(ipc_ap2aud_clk, CCU_PARENT_HW(apb_clk), APBC_IPC_AP2AUD_CLK_RST, BIT(1), 0);

static const struct clk_parent_data can_parents[] = {
	CCU_PARENT_HW(pll3_20),
	CCU_PARENT_HW(pll3_40),
	CCU_PARENT_HW(pll3_80),
};
CCU_MUX_GATE_DEFINE(can0_clk, can_parents, APBC_CAN0_CLK_RST, 4, 3, BIT(1), 0);
CCU_GATE_DEFINE(can0_bus_clk, CCU_PARENT_NAME(vctcxo_24m), APBC_CAN0_CLK_RST, BIT(0), 0);

CCU_GATE_DEFINE(uart0_bus_clk, CCU_PARENT_HW(apb_clk), APBC_UART1_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(uart2_bus_clk, CCU_PARENT_HW(apb_clk), APBC_UART2_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(uart3_bus_clk, CCU_PARENT_HW(apb_clk), APBC_UART3_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(uart4_bus_clk, CCU_PARENT_HW(apb_clk), APBC_UART4_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(uart5_bus_clk, CCU_PARENT_HW(apb_clk), APBC_UART5_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(uart6_bus_clk, CCU_PARENT_HW(apb_clk), APBC_UART6_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(uart7_bus_clk, CCU_PARENT_HW(apb_clk), APBC_UART7_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(uart8_bus_clk, CCU_PARENT_HW(apb_clk), APBC_UART8_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(uart9_bus_clk, CCU_PARENT_HW(apb_clk), APBC_UART9_CLK_RST, BIT(0), 0);

CCU_GATE_DEFINE(gpio_bus_clk, CCU_PARENT_HW(apb_clk), APBC_GPIO_CLK_RST, BIT(0), 0);

CCU_GATE_DEFINE(pwm0_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM0_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm1_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM1_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm2_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM2_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm3_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM3_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm4_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM4_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm5_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM5_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm6_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM6_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm7_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM7_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm8_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM8_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm9_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM9_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm10_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM10_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm11_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM11_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm12_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM12_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm13_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM13_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm14_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM14_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm15_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM15_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm16_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM16_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm17_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM17_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm18_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM18_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(pwm19_bus_clk, CCU_PARENT_HW(apb_clk), APBC_PWM19_CLK_RST, BIT(0), 0);

CCU_GATE_DEFINE(ssp3_bus_clk, CCU_PARENT_HW(apb_clk), APBC_SSP3_CLK_RST, BIT(0), 0);

CCU_GATE_DEFINE(rtc_bus_clk, CCU_PARENT_HW(apb_clk), APBC_RTC_CLK_RST, BIT(0), 0);

CCU_GATE_DEFINE(twsi0_bus_clk, CCU_PARENT_HW(apb_clk), APBC_TWSI0_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(twsi1_bus_clk, CCU_PARENT_HW(apb_clk), APBC_TWSI1_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(twsi2_bus_clk, CCU_PARENT_HW(apb_clk), APBC_TWSI2_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(twsi4_bus_clk, CCU_PARENT_HW(apb_clk), APBC_TWSI4_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(twsi5_bus_clk, CCU_PARENT_HW(apb_clk), APBC_TWSI5_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(twsi6_bus_clk, CCU_PARENT_HW(apb_clk), APBC_TWSI6_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(twsi7_bus_clk, CCU_PARENT_HW(apb_clk), APBC_TWSI7_CLK_RST, BIT(0), 0);
/* Placeholder to workaround quirk of the register */
CCU_FACTOR_DEFINE(twsi8_bus_clk, CCU_PARENT_HW(apb_clk), 1, 1);

CCU_GATE_DEFINE(timers1_bus_clk, CCU_PARENT_HW(apb_clk), APBC_TIMERS1_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(timers2_bus_clk, CCU_PARENT_HW(apb_clk), APBC_TIMERS2_CLK_RST, BIT(0), 0);

CCU_GATE_DEFINE(aib_bus_clk, CCU_PARENT_HW(apb_clk), APBC_AIB_CLK_RST, BIT(0), 0);

CCU_GATE_DEFINE(onewire_bus_clk, CCU_PARENT_HW(apb_clk), APBC_ONEWIRE_CLK_RST, BIT(0), 0);

CCU_GATE_DEFINE(sspa0_bus_clk, CCU_PARENT_HW(apb_clk), APBC_SSPA0_CLK_RST, BIT(0), 0);
CCU_GATE_DEFINE(sspa1_bus_clk, CCU_PARENT_HW(apb_clk), APBC_SSPA1_CLK_RST, BIT(0), 0);

CCU_GATE_DEFINE(tsen_bus_clk, CCU_PARENT_HW(apb_clk), APBC_TSEN_CLK_RST, BIT(0), 0);

CCU_GATE_DEFINE(ipc_ap2aud_bus_clk, CCU_PARENT_HW(apb_clk), APBC_IPC_AP2AUD_CLK_RST, BIT(0), 0);
/* APBC clocks end */

/* APMU clocks start */
static const struct clk_parent_data pmua_aclk_parents[] = {
	CCU_PARENT_HW(pll1_d10_245p76),
	CCU_PARENT_HW(pll1_d8_307p2),
};
CCU_MUX_DIV_FC_DEFINE(pmua_aclk, pmua_aclk_parents, APMU_ACLK_CLK_CTRL, 1, 2, BIT(4), 0, 1, 0);

static const struct clk_parent_data cci550_clk_parents[] = {
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll1_d3_819p2),
	CCU_PARENT_HW(pll2_d3),
};
CCU_MUX_DIV_FC_DEFINE(cci550_clk, cci550_clk_parents, APMU_CCI550_CLK_CTRL, 8, 3, BIT(12), 0, 2,
		      CLK_IS_CRITICAL);

static const struct clk_parent_data cpu_c0_hi_clk_parents[] = {
	CCU_PARENT_HW(pll3_d2),
	CCU_PARENT_HW(pll3_d1),
};
CCU_MUX_DEFINE(cpu_c0_hi_clk, cpu_c0_hi_clk_parents, APMU_CPU_C0_CLK_CTRL, 13, 1, 0);
static const struct clk_parent_data cpu_c0_clk_parents[] = {
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll1_d3_819p2),
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d2_1228p8),
	CCU_PARENT_HW(pll3_d3),
	CCU_PARENT_HW(pll2_d3),
	CCU_PARENT_HW(cpu_c0_hi_clk),
};
CCU_MUX_FC_DEFINE(cpu_c0_core_clk, cpu_c0_clk_parents, APMU_CPU_C0_CLK_CTRL, BIT(12), 0, 3,
		  CLK_IS_CRITICAL);
CCU_DIV_DEFINE(cpu_c0_ace_clk, CCU_PARENT_HW(cpu_c0_core_clk), APMU_CPU_C0_CLK_CTRL, 6, 3,
	       CLK_IS_CRITICAL);
CCU_DIV_DEFINE(cpu_c0_tcm_clk, CCU_PARENT_HW(cpu_c0_core_clk), APMU_CPU_C0_CLK_CTRL, 9, 3,
	       CLK_IS_CRITICAL);

static const struct clk_parent_data cpu_c1_hi_clk_parents[] = {
	CCU_PARENT_HW(pll3_d2),
	CCU_PARENT_HW(pll3_d1),
};
CCU_MUX_DEFINE(cpu_c1_hi_clk, cpu_c1_hi_clk_parents, APMU_CPU_C1_CLK_CTRL, 13, 1, 0);
static const struct clk_parent_data cpu_c1_clk_parents[] = {
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll1_d3_819p2),
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d2_1228p8),
	CCU_PARENT_HW(pll3_d3),
	CCU_PARENT_HW(pll2_d3),
	CCU_PARENT_HW(cpu_c1_hi_clk),
};
CCU_MUX_FC_DEFINE(cpu_c1_core_clk, cpu_c1_clk_parents, APMU_CPU_C1_CLK_CTRL, BIT(12), 0, 3,
		  CLK_IS_CRITICAL);
CCU_DIV_DEFINE(cpu_c1_ace_clk, CCU_PARENT_HW(cpu_c1_core_clk), APMU_CPU_C1_CLK_CTRL, 6, 3,
	       CLK_IS_CRITICAL);

static const struct clk_parent_data jpg_parents[] = {
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d3_819p2),
	CCU_PARENT_HW(pll1_d2_1228p8),
	CCU_PARENT_HW(pll2_d4),
	CCU_PARENT_HW(pll2_d3),
};
CCU_MUX_DIV_GATE_FC_DEFINE(jpg_clk, jpg_parents, APMU_JPG_CLK_RES_CTRL, 5, 3, BIT(15), 2, 3,
			   BIT(1), 0);

static const struct clk_parent_data ccic2phy_parents[] = {
	CCU_PARENT_HW(pll1_d24_102p4),
	CCU_PARENT_HW(pll1_d48_51p2_ap),
};
CCU_MUX_GATE_DEFINE(ccic2phy_clk, ccic2phy_parents, APMU_CSI_CCIC2_CLK_RES_CTRL, 7, 1, BIT(5), 0);

static const struct clk_parent_data ccic3phy_parents[] = {
	CCU_PARENT_HW(pll1_d24_102p4),
	CCU_PARENT_HW(pll1_d48_51p2_ap),
};
CCU_MUX_GATE_DEFINE(ccic3phy_clk, ccic3phy_parents, APMU_CSI_CCIC2_CLK_RES_CTRL, 31, 1, BIT(30), 0);

static const struct clk_parent_data csi_parents[] = {
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll1_d3_819p2),
	CCU_PARENT_HW(pll2_d2),
	CCU_PARENT_HW(pll2_d3),
	CCU_PARENT_HW(pll2_d4),
	CCU_PARENT_HW(pll1_d2_1228p8),
};
CCU_MUX_DIV_GATE_FC_DEFINE(csi_clk, csi_parents, APMU_CSI_CCIC2_CLK_RES_CTRL, 20, 3, BIT(15),
			   16, 3, BIT(4), 0);

static const struct clk_parent_data camm_parents[] = {
	CCU_PARENT_HW(pll1_d8_307p2),
	CCU_PARENT_HW(pll2_d5),
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_NAME(vctcxo_24m),
};
CCU_MUX_DIV_GATE_DEFINE(camm0_clk, camm_parents, APMU_CSI_CCIC2_CLK_RES_CTRL, 23, 4, 8, 2,
			BIT(28), 0);
CCU_MUX_DIV_GATE_DEFINE(camm1_clk, camm_parents, APMU_CSI_CCIC2_CLK_RES_CTRL, 23, 4, 8, 2,
			BIT(6), 0);
CCU_MUX_DIV_GATE_DEFINE(camm2_clk, camm_parents, APMU_CSI_CCIC2_CLK_RES_CTRL, 23, 4, 8, 2,
			BIT(3), 0);

static const struct clk_parent_data isp_cpp_parents[] = {
	CCU_PARENT_HW(pll1_d8_307p2),
	CCU_PARENT_HW(pll1_d6_409p6),
};
CCU_MUX_DIV_GATE_DEFINE(isp_cpp_clk, isp_cpp_parents, APMU_ISP_CLK_RES_CTRL, 24, 2, 26, 1,
			BIT(28), 0);
static const struct clk_parent_data isp_bus_parents[] = {
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d8_307p2),
	CCU_PARENT_HW(pll1_d10_245p76),
};
CCU_MUX_DIV_GATE_FC_DEFINE(isp_bus_clk, isp_bus_parents, APMU_ISP_CLK_RES_CTRL, 18, 3, BIT(23),
			   21, 2, BIT(17), 0);
static const struct clk_parent_data isp_parents[] = {
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll1_d8_307p2),
};
CCU_MUX_DIV_GATE_FC_DEFINE(isp_clk, isp_parents, APMU_ISP_CLK_RES_CTRL, 4, 3, BIT(7), 8, 2,
			   BIT(1), 0);

static const struct clk_parent_data dpumclk_parents[] = {
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll1_d8_307p2),
};
CCU_MUX_DIV_GATE_SPLIT_FC_DEFINE(dpu_mclk, dpumclk_parents, APMU_LCD_CLK_RES_CTRL2,
				 APMU_LCD_CLK_RES_CTRL1, 1, 4, BIT(29), 5, 3, BIT(0), 0);

static const struct clk_parent_data dpuesc_parents[] = {
	CCU_PARENT_HW(pll1_d48_51p2_ap),
	CCU_PARENT_HW(pll1_d52_47p26),
	CCU_PARENT_HW(pll1_d96_25p6),
	CCU_PARENT_HW(pll1_d32_76p8),
};
CCU_MUX_GATE_DEFINE(dpu_esc_clk, dpuesc_parents, APMU_LCD_CLK_RES_CTRL1, 0, 2, BIT(2), 0);

static const struct clk_parent_data dpubit_parents[] = {
	CCU_PARENT_HW(pll1_d3_819p2),
	CCU_PARENT_HW(pll2_d2),
	CCU_PARENT_HW(pll2_d3),
	CCU_PARENT_HW(pll1_d2_1228p8),
	CCU_PARENT_HW(pll2_d4),
	CCU_PARENT_HW(pll2_d5),
	CCU_PARENT_HW(pll2_d7),
	CCU_PARENT_HW(pll2_d8),
};
CCU_MUX_DIV_GATE_FC_DEFINE(dpu_bit_clk, dpubit_parents, APMU_LCD_CLK_RES_CTRL1, 17, 3, BIT(31),
			   20, 3, BIT(16), 0);

static const struct clk_parent_data dpupx_parents[] = {
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll1_d8_307p2),
	CCU_PARENT_HW(pll2_d7),
	CCU_PARENT_HW(pll2_d8),
};
CCU_MUX_DIV_GATE_SPLIT_FC_DEFINE(dpu_pxclk, dpupx_parents, APMU_LCD_CLK_RES_CTRL2,
				 APMU_LCD_CLK_RES_CTRL1, 17, 4, BIT(30), 21, 3, BIT(16), 0);

CCU_GATE_DEFINE(dpu_hclk, CCU_PARENT_HW(pmua_aclk), APMU_LCD_CLK_RES_CTRL1,
		BIT(5), 0);

static const struct clk_parent_data dpu_spi_parents[] = {
	CCU_PARENT_HW(pll1_d8_307p2),
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d10_245p76),
	CCU_PARENT_HW(pll1_d11_223p4),
	CCU_PARENT_HW(pll1_d13_189),
	CCU_PARENT_HW(pll1_d23_106p8),
	CCU_PARENT_HW(pll2_d3),
	CCU_PARENT_HW(pll2_d5),
};
CCU_MUX_DIV_GATE_FC_DEFINE(dpu_spi_clk, dpu_spi_parents, APMU_LCD_SPI_CLK_RES_CTRL, 8, 3,
			   BIT(7), 12, 3, BIT(1), 0);
CCU_GATE_DEFINE(dpu_spi_hbus_clk, CCU_PARENT_HW(pmua_aclk), APMU_LCD_SPI_CLK_RES_CTRL, BIT(3), 0);
CCU_GATE_DEFINE(dpu_spi_bus_clk, CCU_PARENT_HW(pmua_aclk), APMU_LCD_SPI_CLK_RES_CTRL, BIT(5), 0);
CCU_GATE_DEFINE(dpu_spi_aclk, CCU_PARENT_HW(pmua_aclk), APMU_LCD_SPI_CLK_RES_CTRL, BIT(6), 0);

static const struct clk_parent_data v2d_parents[] = {
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d8_307p2),
	CCU_PARENT_HW(pll1_d4_614p4),
};
CCU_MUX_DIV_GATE_FC_DEFINE(v2d_clk, v2d_parents, APMU_LCD_CLK_RES_CTRL1, 9, 3, BIT(28), 12, 2,
			   BIT(8), 0);

static const struct clk_parent_data ccic_4x_parents[] = {
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll1_d3_819p2),
	CCU_PARENT_HW(pll2_d2),
	CCU_PARENT_HW(pll2_d3),
	CCU_PARENT_HW(pll2_d4),
	CCU_PARENT_HW(pll1_d2_1228p8),
};
CCU_MUX_DIV_GATE_FC_DEFINE(ccic_4x_clk, ccic_4x_parents, APMU_CCIC_CLK_RES_CTRL, 18, 3,
			   BIT(15), 23, 2, BIT(4), 0);

static const struct clk_parent_data ccic1phy_parents[] = {
	CCU_PARENT_HW(pll1_d24_102p4),
	CCU_PARENT_HW(pll1_d48_51p2_ap),
};
CCU_MUX_GATE_DEFINE(ccic1phy_clk, ccic1phy_parents, APMU_CCIC_CLK_RES_CTRL, 7, 1, BIT(5), 0);

CCU_GATE_DEFINE(sdh_axi_aclk, CCU_PARENT_HW(pmua_aclk), APMU_SDH0_CLK_RES_CTRL, BIT(3), 0);
static const struct clk_parent_data sdh01_parents[] = {
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll2_d8),
	CCU_PARENT_HW(pll2_d5),
	CCU_PARENT_HW(pll1_d11_223p4),
	CCU_PARENT_HW(pll1_d13_189),
	CCU_PARENT_HW(pll1_d23_106p8),
};
CCU_MUX_DIV_GATE_FC_DEFINE(sdh0_clk, sdh01_parents, APMU_SDH0_CLK_RES_CTRL, 8, 3, BIT(11), 5, 3,
			   BIT(4), 0);
CCU_MUX_DIV_GATE_FC_DEFINE(sdh1_clk, sdh01_parents, APMU_SDH1_CLK_RES_CTRL, 8, 3, BIT(11), 5, 3,
			   BIT(4), 0);
static const struct clk_parent_data sdh2_parents[] = {
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll2_d8),
	CCU_PARENT_HW(pll1_d3_819p2),
	CCU_PARENT_HW(pll1_d11_223p4),
	CCU_PARENT_HW(pll1_d13_189),
	CCU_PARENT_HW(pll1_d23_106p8),
};
CCU_MUX_DIV_GATE_FC_DEFINE(sdh2_clk, sdh2_parents, APMU_SDH2_CLK_RES_CTRL, 8, 3, BIT(11), 5, 3,
			   BIT(4), 0);

CCU_GATE_DEFINE(usb_axi_clk, CCU_PARENT_HW(pmua_aclk), APMU_USB_CLK_RES_CTRL, BIT(1), 0);
CCU_GATE_DEFINE(usb_p1_aclk, CCU_PARENT_HW(pmua_aclk), APMU_USB_CLK_RES_CTRL, BIT(5), 0);
CCU_GATE_DEFINE(usb30_clk, CCU_PARENT_HW(pmua_aclk), APMU_USB_CLK_RES_CTRL, BIT(8), 0);

static const struct clk_parent_data qspi_parents[] = {
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll2_d8),
	CCU_PARENT_HW(pll1_d8_307p2),
	CCU_PARENT_HW(pll1_d10_245p76),
	CCU_PARENT_HW(pll1_d11_223p4),
	CCU_PARENT_HW(pll1_d23_106p8),
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d13_189),
};
CCU_MUX_DIV_GATE_FC_DEFINE(qspi_clk, qspi_parents, APMU_QSPI_CLK_RES_CTRL, 9, 3, BIT(12), 6, 3,
			   BIT(4), 0);
CCU_GATE_DEFINE(qspi_bus_clk, CCU_PARENT_HW(pmua_aclk), APMU_QSPI_CLK_RES_CTRL, BIT(3), 0);
CCU_GATE_DEFINE(dma_clk, CCU_PARENT_HW(pmua_aclk), APMU_DMA_CLK_RES_CTRL, BIT(3), 0);

static const struct clk_parent_data aes_parents[] = {
	CCU_PARENT_HW(pll1_d12_204p8),
	CCU_PARENT_HW(pll1_d24_102p4),
};
CCU_MUX_GATE_DEFINE(aes_clk, aes_parents, APMU_AES_CLK_RES_CTRL, 6, 1, BIT(5), 0);

static const struct clk_parent_data vpu_parents[] = {
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d3_819p2),
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll3_d6),
	CCU_PARENT_HW(pll2_d3),
	CCU_PARENT_HW(pll2_d4),
	CCU_PARENT_HW(pll2_d5),
};
CCU_MUX_DIV_GATE_FC_DEFINE(vpu_clk, vpu_parents, APMU_VPU_CLK_RES_CTRL, 13, 3, BIT(21), 10, 3,
			   BIT(3), 0);

static const struct clk_parent_data gpu_parents[] = {
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d3_819p2),
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll3_d6),
	CCU_PARENT_HW(pll2_d3),
	CCU_PARENT_HW(pll2_d4),
	CCU_PARENT_HW(pll2_d5),
};
CCU_MUX_DIV_GATE_FC_DEFINE(gpu_clk, gpu_parents, APMU_GPU_CLK_RES_CTRL, 12, 3, BIT(15), 18, 3,
			   BIT(4), 0);

static const struct clk_parent_data emmc_parents[] = {
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll1_d52_47p26),
	CCU_PARENT_HW(pll1_d3_819p2),
};
CCU_MUX_DIV_GATE_FC_DEFINE(emmc_clk, emmc_parents, APMU_PMUA_EM_CLK_RES_CTRL, 8, 3, BIT(11),
			   6, 2, BIT(4), 0);
CCU_DIV_GATE_DEFINE(emmc_x_clk, CCU_PARENT_HW(pll1_d2_1228p8), APMU_PMUA_EM_CLK_RES_CTRL, 12,
		    3, BIT(15), 0);

static const struct clk_parent_data audio_parents[] = {
	CCU_PARENT_HW(pll1_aud_245p7),
	CCU_PARENT_HW(pll1_d8_307p2),
	CCU_PARENT_HW(pll1_d6_409p6),
};
CCU_MUX_DIV_GATE_FC_DEFINE(audio_clk, audio_parents, APMU_AUDIO_CLK_RES_CTRL, 4, 3, BIT(15),
			   7, 3, BIT(12), 0);

static const struct clk_parent_data hdmi_parents[] = {
	CCU_PARENT_HW(pll1_d6_409p6),
	CCU_PARENT_HW(pll1_d5_491p52),
	CCU_PARENT_HW(pll1_d4_614p4),
	CCU_PARENT_HW(pll1_d8_307p2),
};
CCU_MUX_DIV_GATE_FC_DEFINE(hdmi_mclk, hdmi_parents, APMU_HDMI_CLK_RES_CTRL, 1, 4, BIT(29), 5,
			   3, BIT(0), 0);

CCU_GATE_DEFINE(pcie0_master_clk, CCU_PARENT_HW(pmua_aclk), APMU_PCIE_CLK_RES_CTRL_0, BIT(2), 0);
CCU_GATE_DEFINE(pcie0_slave_clk, CCU_PARENT_HW(pmua_aclk), APMU_PCIE_CLK_RES_CTRL_0, BIT(1), 0);
CCU_GATE_DEFINE(pcie0_dbi_clk, CCU_PARENT_HW(pmua_aclk), APMU_PCIE_CLK_RES_CTRL_0, BIT(0), 0);

CCU_GATE_DEFINE(pcie1_master_clk, CCU_PARENT_HW(pmua_aclk), APMU_PCIE_CLK_RES_CTRL_1, BIT(2), 0);
CCU_GATE_DEFINE(pcie1_slave_clk, CCU_PARENT_HW(pmua_aclk), APMU_PCIE_CLK_RES_CTRL_1, BIT(1), 0);
CCU_GATE_DEFINE(pcie1_dbi_clk, CCU_PARENT_HW(pmua_aclk), APMU_PCIE_CLK_RES_CTRL_1, BIT(0), 0);

CCU_GATE_DEFINE(pcie2_master_clk, CCU_PARENT_HW(pmua_aclk), APMU_PCIE_CLK_RES_CTRL_2, BIT(2), 0);
CCU_GATE_DEFINE(pcie2_slave_clk, CCU_PARENT_HW(pmua_aclk), APMU_PCIE_CLK_RES_CTRL_2, BIT(1), 0);
CCU_GATE_DEFINE(pcie2_dbi_clk, CCU_PARENT_HW(pmua_aclk), APMU_PCIE_CLK_RES_CTRL_2, BIT(0), 0);

CCU_GATE_DEFINE(emac0_bus_clk, CCU_PARENT_HW(pmua_aclk), APMU_EMAC0_CLK_RES_CTRL, BIT(0), 0);
CCU_GATE_DEFINE(emac0_ptp_clk, CCU_PARENT_HW(pll2_d6), APMU_EMAC0_CLK_RES_CTRL, BIT(15), 0);
CCU_GATE_DEFINE(emac1_bus_clk, CCU_PARENT_HW(pmua_aclk), APMU_EMAC1_CLK_RES_CTRL, BIT(0), 0);
CCU_GATE_DEFINE(emac1_ptp_clk, CCU_PARENT_HW(pll2_d6), APMU_EMAC1_CLK_RES_CTRL, BIT(15), 0);

CCU_GATE_DEFINE(emmc_bus_clk, CCU_PARENT_HW(pmua_aclk), APMU_PMUA_EM_CLK_RES_CTRL, BIT(3), 0);
/* APMU clocks end */

static struct clk_hw *k1_ccu_pll_hws[] = {
	[CLK_PLL1]		= &pll1.common.hw,
	[CLK_PLL2]		= &pll2.common.hw,
	[CLK_PLL3]		= &pll3.common.hw,
	[CLK_PLL1_D2]		= &pll1_d2.common.hw,
	[CLK_PLL1_D3]		= &pll1_d3.common.hw,
	[CLK_PLL1_D4]		= &pll1_d4.common.hw,
	[CLK_PLL1_D5]		= &pll1_d5.common.hw,
	[CLK_PLL1_D6]		= &pll1_d6.common.hw,
	[CLK_PLL1_D7]		= &pll1_d7.common.hw,
	[CLK_PLL1_D8]		= &pll1_d8.common.hw,
	[CLK_PLL1_D11]		= &pll1_d11_223p4.common.hw,
	[CLK_PLL1_D13]		= &pll1_d13_189.common.hw,
	[CLK_PLL1_D23]		= &pll1_d23_106p8.common.hw,
	[CLK_PLL1_D64]		= &pll1_d64_38p4.common.hw,
	[CLK_PLL1_D10_AUD]	= &pll1_aud_245p7.common.hw,
	[CLK_PLL1_D100_AUD]	= &pll1_aud_24p5.common.hw,
	[CLK_PLL2_D1]		= &pll2_d1.common.hw,
	[CLK_PLL2_D2]		= &pll2_d2.common.hw,
	[CLK_PLL2_D3]		= &pll2_d3.common.hw,
	[CLK_PLL2_D4]		= &pll2_d4.common.hw,
	[CLK_PLL2_D5]		= &pll2_d5.common.hw,
	[CLK_PLL2_D6]		= &pll2_d6.common.hw,
	[CLK_PLL2_D7]		= &pll2_d7.common.hw,
	[CLK_PLL2_D8]		= &pll2_d8.common.hw,
	[CLK_PLL3_D1]		= &pll3_d1.common.hw,
	[CLK_PLL3_D2]		= &pll3_d2.common.hw,
	[CLK_PLL3_D3]		= &pll3_d3.common.hw,
	[CLK_PLL3_D4]		= &pll3_d4.common.hw,
	[CLK_PLL3_D5]		= &pll3_d5.common.hw,
	[CLK_PLL3_D6]		= &pll3_d6.common.hw,
	[CLK_PLL3_D7]		= &pll3_d7.common.hw,
	[CLK_PLL3_D8]		= &pll3_d8.common.hw,
	[CLK_PLL3_80]		= &pll3_80.common.hw,
	[CLK_PLL3_40]		= &pll3_40.common.hw,
	[CLK_PLL3_20]		= &pll3_20.common.hw,
};

static const struct spacemit_ccu_data k1_ccu_pll_data = {
	/* The PLL CCU implements no resets */
	.hws		= k1_ccu_pll_hws,
	.num		= ARRAY_SIZE(k1_ccu_pll_hws),
};

static struct clk_hw *k1_ccu_mpmu_hws[] = {
	[CLK_PLL1_307P2]	= &pll1_d8_307p2.common.hw,
	[CLK_PLL1_76P8]		= &pll1_d32_76p8.common.hw,
	[CLK_PLL1_61P44]	= &pll1_d40_61p44.common.hw,
	[CLK_PLL1_153P6]	= &pll1_d16_153p6.common.hw,
	[CLK_PLL1_102P4]	= &pll1_d24_102p4.common.hw,
	[CLK_PLL1_51P2]		= &pll1_d48_51p2.common.hw,
	[CLK_PLL1_51P2_AP]	= &pll1_d48_51p2_ap.common.hw,
	[CLK_PLL1_57P6]		= &pll1_m3d128_57p6.common.hw,
	[CLK_PLL1_25P6]		= &pll1_d96_25p6.common.hw,
	[CLK_PLL1_12P8]		= &pll1_d192_12p8.common.hw,
	[CLK_PLL1_12P8_WDT]	= &pll1_d192_12p8_wdt.common.hw,
	[CLK_PLL1_6P4]		= &pll1_d384_6p4.common.hw,
	[CLK_PLL1_3P2]		= &pll1_d768_3p2.common.hw,
	[CLK_PLL1_1P6]		= &pll1_d1536_1p6.common.hw,
	[CLK_PLL1_0P8]		= &pll1_d3072_0p8.common.hw,
	[CLK_PLL1_409P6]	= &pll1_d6_409p6.common.hw,
	[CLK_PLL1_204P8]	= &pll1_d12_204p8.common.hw,
	[CLK_PLL1_491]		= &pll1_d5_491p52.common.hw,
	[CLK_PLL1_245P76]	= &pll1_d10_245p76.common.hw,
	[CLK_PLL1_614]		= &pll1_d4_614p4.common.hw,
	[CLK_PLL1_47P26]	= &pll1_d52_47p26.common.hw,
	[CLK_PLL1_31P5]		= &pll1_d78_31p5.common.hw,
	[CLK_PLL1_819]		= &pll1_d3_819p2.common.hw,
	[CLK_PLL1_1228]		= &pll1_d2_1228p8.common.hw,
	[CLK_SLOW_UART]		= &slow_uart.common.hw,
	[CLK_SLOW_UART1]	= &slow_uart1_14p74.common.hw,
	[CLK_SLOW_UART2]	= &slow_uart2_48.common.hw,
	[CLK_WDT]		= &wdt_clk.common.hw,
	[CLK_RIPC]		= &ripc_clk.common.hw,
	[CLK_I2S_SYSCLK]	= &i2s_sysclk.common.hw,
	[CLK_I2S_BCLK]		= &i2s_bclk.common.hw,
	[CLK_APB]		= &apb_clk.common.hw,
	[CLK_WDT_BUS]		= &wdt_bus_clk.common.hw,
	[CLK_I2S_153P6]		= &i2s_153p6.common.hw,
	[CLK_I2S_153P6_BASE]	= &i2s_153p6_base.common.hw,
	[CLK_I2S_SYSCLK_SRC]	= &i2s_sysclk_src.common.hw,
	[CLK_I2S_BCLK_FACTOR]	= &i2s_bclk_factor.common.hw,
};

static const struct spacemit_ccu_data k1_ccu_mpmu_data = {
	.reset_name	= "mpmu-reset",
	.hws		= k1_ccu_mpmu_hws,
	.num		= ARRAY_SIZE(k1_ccu_mpmu_hws),
};

static struct clk_hw *k1_ccu_apbc_hws[] = {
	[CLK_UART0]		= &uart0_clk.common.hw,
	[CLK_UART2]		= &uart2_clk.common.hw,
	[CLK_UART3]		= &uart3_clk.common.hw,
	[CLK_UART4]		= &uart4_clk.common.hw,
	[CLK_UART5]		= &uart5_clk.common.hw,
	[CLK_UART6]		= &uart6_clk.common.hw,
	[CLK_UART7]		= &uart7_clk.common.hw,
	[CLK_UART8]		= &uart8_clk.common.hw,
	[CLK_UART9]		= &uart9_clk.common.hw,
	[CLK_GPIO]		= &gpio_clk.common.hw,
	[CLK_PWM0]		= &pwm0_clk.common.hw,
	[CLK_PWM1]		= &pwm1_clk.common.hw,
	[CLK_PWM2]		= &pwm2_clk.common.hw,
	[CLK_PWM3]		= &pwm3_clk.common.hw,
	[CLK_PWM4]		= &pwm4_clk.common.hw,
	[CLK_PWM5]		= &pwm5_clk.common.hw,
	[CLK_PWM6]		= &pwm6_clk.common.hw,
	[CLK_PWM7]		= &pwm7_clk.common.hw,
	[CLK_PWM8]		= &pwm8_clk.common.hw,
	[CLK_PWM9]		= &pwm9_clk.common.hw,
	[CLK_PWM10]		= &pwm10_clk.common.hw,
	[CLK_PWM11]		= &pwm11_clk.common.hw,
	[CLK_PWM12]		= &pwm12_clk.common.hw,
	[CLK_PWM13]		= &pwm13_clk.common.hw,
	[CLK_PWM14]		= &pwm14_clk.common.hw,
	[CLK_PWM15]		= &pwm15_clk.common.hw,
	[CLK_PWM16]		= &pwm16_clk.common.hw,
	[CLK_PWM17]		= &pwm17_clk.common.hw,
	[CLK_PWM18]		= &pwm18_clk.common.hw,
	[CLK_PWM19]		= &pwm19_clk.common.hw,
	[CLK_SSP3]		= &ssp3_clk.common.hw,
	[CLK_RTC]		= &rtc_clk.common.hw,
	[CLK_TWSI0]		= &twsi0_clk.common.hw,
	[CLK_TWSI1]		= &twsi1_clk.common.hw,
	[CLK_TWSI2]		= &twsi2_clk.common.hw,
	[CLK_TWSI4]		= &twsi4_clk.common.hw,
	[CLK_TWSI5]		= &twsi5_clk.common.hw,
	[CLK_TWSI6]		= &twsi6_clk.common.hw,
	[CLK_TWSI7]		= &twsi7_clk.common.hw,
	[CLK_TWSI8]		= &twsi8_clk.common.hw,
	[CLK_TIMERS1]		= &timers1_clk.common.hw,
	[CLK_TIMERS2]		= &timers2_clk.common.hw,
	[CLK_AIB]		= &aib_clk.common.hw,
	[CLK_ONEWIRE]		= &onewire_clk.common.hw,
	[CLK_SSPA0]		= &sspa0_clk.common.hw,
	[CLK_SSPA1]		= &sspa1_clk.common.hw,
	[CLK_DRO]		= &dro_clk.common.hw,
	[CLK_IR]		= &ir_clk.common.hw,
	[CLK_TSEN]		= &tsen_clk.common.hw,
	[CLK_IPC_AP2AUD]	= &ipc_ap2aud_clk.common.hw,
	[CLK_CAN0]		= &can0_clk.common.hw,
	[CLK_CAN0_BUS]		= &can0_bus_clk.common.hw,
	[CLK_UART0_BUS]		= &uart0_bus_clk.common.hw,
	[CLK_UART2_BUS]		= &uart2_bus_clk.common.hw,
	[CLK_UART3_BUS]		= &uart3_bus_clk.common.hw,
	[CLK_UART4_BUS]		= &uart4_bus_clk.common.hw,
	[CLK_UART5_BUS]		= &uart5_bus_clk.common.hw,
	[CLK_UART6_BUS]		= &uart6_bus_clk.common.hw,
	[CLK_UART7_BUS]		= &uart7_bus_clk.common.hw,
	[CLK_UART8_BUS]		= &uart8_bus_clk.common.hw,
	[CLK_UART9_BUS]		= &uart9_bus_clk.common.hw,
	[CLK_GPIO_BUS]		= &gpio_bus_clk.common.hw,
	[CLK_PWM0_BUS]		= &pwm0_bus_clk.common.hw,
	[CLK_PWM1_BUS]		= &pwm1_bus_clk.common.hw,
	[CLK_PWM2_BUS]		= &pwm2_bus_clk.common.hw,
	[CLK_PWM3_BUS]		= &pwm3_bus_clk.common.hw,
	[CLK_PWM4_BUS]		= &pwm4_bus_clk.common.hw,
	[CLK_PWM5_BUS]		= &pwm5_bus_clk.common.hw,
	[CLK_PWM6_BUS]		= &pwm6_bus_clk.common.hw,
	[CLK_PWM7_BUS]		= &pwm7_bus_clk.common.hw,
	[CLK_PWM8_BUS]		= &pwm8_bus_clk.common.hw,
	[CLK_PWM9_BUS]		= &pwm9_bus_clk.common.hw,
	[CLK_PWM10_BUS]		= &pwm10_bus_clk.common.hw,
	[CLK_PWM11_BUS]		= &pwm11_bus_clk.common.hw,
	[CLK_PWM12_BUS]		= &pwm12_bus_clk.common.hw,
	[CLK_PWM13_BUS]		= &pwm13_bus_clk.common.hw,
	[CLK_PWM14_BUS]		= &pwm14_bus_clk.common.hw,
	[CLK_PWM15_BUS]		= &pwm15_bus_clk.common.hw,
	[CLK_PWM16_BUS]		= &pwm16_bus_clk.common.hw,
	[CLK_PWM17_BUS]		= &pwm17_bus_clk.common.hw,
	[CLK_PWM18_BUS]		= &pwm18_bus_clk.common.hw,
	[CLK_PWM19_BUS]		= &pwm19_bus_clk.common.hw,
	[CLK_SSP3_BUS]		= &ssp3_bus_clk.common.hw,
	[CLK_RTC_BUS]		= &rtc_bus_clk.common.hw,
	[CLK_TWSI0_BUS]		= &twsi0_bus_clk.common.hw,
	[CLK_TWSI1_BUS]		= &twsi1_bus_clk.common.hw,
	[CLK_TWSI2_BUS]		= &twsi2_bus_clk.common.hw,
	[CLK_TWSI4_BUS]		= &twsi4_bus_clk.common.hw,
	[CLK_TWSI5_BUS]		= &twsi5_bus_clk.common.hw,
	[CLK_TWSI6_BUS]		= &twsi6_bus_clk.common.hw,
	[CLK_TWSI7_BUS]		= &twsi7_bus_clk.common.hw,
	[CLK_TWSI8_BUS]		= &twsi8_bus_clk.common.hw,
	[CLK_TIMERS1_BUS]	= &timers1_bus_clk.common.hw,
	[CLK_TIMERS2_BUS]	= &timers2_bus_clk.common.hw,
	[CLK_AIB_BUS]		= &aib_bus_clk.common.hw,
	[CLK_ONEWIRE_BUS]	= &onewire_bus_clk.common.hw,
	[CLK_SSPA0_BUS]		= &sspa0_bus_clk.common.hw,
	[CLK_SSPA1_BUS]		= &sspa1_bus_clk.common.hw,
	[CLK_TSEN_BUS]		= &tsen_bus_clk.common.hw,
	[CLK_IPC_AP2AUD_BUS]	= &ipc_ap2aud_bus_clk.common.hw,
	[CLK_SSPA0_I2S_BCLK]	= &sspa0_i2s_bclk.common.hw,
	[CLK_SSPA1_I2S_BCLK]	= &sspa1_i2s_bclk.common.hw,
};

static const struct spacemit_ccu_data k1_ccu_apbc_data = {
	.reset_name	= "apbc-reset",
	.hws		= k1_ccu_apbc_hws,
	.num		= ARRAY_SIZE(k1_ccu_apbc_hws),
};

static struct clk_hw *k1_ccu_apmu_hws[] = {
	[CLK_CCI550]		= &cci550_clk.common.hw,
	[CLK_CPU_C0_HI]		= &cpu_c0_hi_clk.common.hw,
	[CLK_CPU_C0_CORE]	= &cpu_c0_core_clk.common.hw,
	[CLK_CPU_C0_ACE]	= &cpu_c0_ace_clk.common.hw,
	[CLK_CPU_C0_TCM]	= &cpu_c0_tcm_clk.common.hw,
	[CLK_CPU_C1_HI]		= &cpu_c1_hi_clk.common.hw,
	[CLK_CPU_C1_CORE]	= &cpu_c1_core_clk.common.hw,
	[CLK_CPU_C1_ACE]	= &cpu_c1_ace_clk.common.hw,
	[CLK_CCIC_4X]		= &ccic_4x_clk.common.hw,
	[CLK_CCIC1PHY]		= &ccic1phy_clk.common.hw,
	[CLK_SDH_AXI]		= &sdh_axi_aclk.common.hw,
	[CLK_SDH0]		= &sdh0_clk.common.hw,
	[CLK_SDH1]		= &sdh1_clk.common.hw,
	[CLK_SDH2]		= &sdh2_clk.common.hw,
	[CLK_USB_P1]		= &usb_p1_aclk.common.hw,
	[CLK_USB_AXI]		= &usb_axi_clk.common.hw,
	[CLK_USB30]		= &usb30_clk.common.hw,
	[CLK_QSPI]		= &qspi_clk.common.hw,
	[CLK_QSPI_BUS]		= &qspi_bus_clk.common.hw,
	[CLK_DMA]		= &dma_clk.common.hw,
	[CLK_AES]		= &aes_clk.common.hw,
	[CLK_VPU]		= &vpu_clk.common.hw,
	[CLK_GPU]		= &gpu_clk.common.hw,
	[CLK_EMMC]		= &emmc_clk.common.hw,
	[CLK_EMMC_X]		= &emmc_x_clk.common.hw,
	[CLK_AUDIO]		= &audio_clk.common.hw,
	[CLK_HDMI]		= &hdmi_mclk.common.hw,
	[CLK_PMUA_ACLK]		= &pmua_aclk.common.hw,
	[CLK_PCIE0_MASTER]	= &pcie0_master_clk.common.hw,
	[CLK_PCIE0_SLAVE]	= &pcie0_slave_clk.common.hw,
	[CLK_PCIE0_DBI]		= &pcie0_dbi_clk.common.hw,
	[CLK_PCIE1_MASTER]	= &pcie1_master_clk.common.hw,
	[CLK_PCIE1_SLAVE]	= &pcie1_slave_clk.common.hw,
	[CLK_PCIE1_DBI]		= &pcie1_dbi_clk.common.hw,
	[CLK_PCIE2_MASTER]	= &pcie2_master_clk.common.hw,
	[CLK_PCIE2_SLAVE]	= &pcie2_slave_clk.common.hw,
	[CLK_PCIE2_DBI]		= &pcie2_dbi_clk.common.hw,
	[CLK_EMAC0_BUS]		= &emac0_bus_clk.common.hw,
	[CLK_EMAC0_PTP]		= &emac0_ptp_clk.common.hw,
	[CLK_EMAC1_BUS]		= &emac1_bus_clk.common.hw,
	[CLK_EMAC1_PTP]		= &emac1_ptp_clk.common.hw,
	[CLK_JPG]		= &jpg_clk.common.hw,
	[CLK_CCIC2PHY]		= &ccic2phy_clk.common.hw,
	[CLK_CCIC3PHY]		= &ccic3phy_clk.common.hw,
	[CLK_CSI]		= &csi_clk.common.hw,
	[CLK_CAMM0]		= &camm0_clk.common.hw,
	[CLK_CAMM1]		= &camm1_clk.common.hw,
	[CLK_CAMM2]		= &camm2_clk.common.hw,
	[CLK_ISP_CPP]		= &isp_cpp_clk.common.hw,
	[CLK_ISP_BUS]		= &isp_bus_clk.common.hw,
	[CLK_ISP]		= &isp_clk.common.hw,
	[CLK_DPU_MCLK]		= &dpu_mclk.common.hw,
	[CLK_DPU_ESC]		= &dpu_esc_clk.common.hw,
	[CLK_DPU_BIT]		= &dpu_bit_clk.common.hw,
	[CLK_DPU_PXCLK]		= &dpu_pxclk.common.hw,
	[CLK_DPU_HCLK]		= &dpu_hclk.common.hw,
	[CLK_DPU_SPI]		= &dpu_spi_clk.common.hw,
	[CLK_DPU_SPI_HBUS]	= &dpu_spi_hbus_clk.common.hw,
	[CLK_DPU_SPIBUS]	= &dpu_spi_bus_clk.common.hw,
	[CLK_DPU_SPI_ACLK]	= &dpu_spi_aclk.common.hw,
	[CLK_V2D]		= &v2d_clk.common.hw,
	[CLK_EMMC_BUS]		= &emmc_bus_clk.common.hw,
};

static const struct spacemit_ccu_data k1_ccu_apmu_data = {
	.reset_name	= "apmu-reset",
	.hws		= k1_ccu_apmu_hws,
	.num		= ARRAY_SIZE(k1_ccu_apmu_hws),
};

static const struct spacemit_ccu_data k1_ccu_rcpu_data = {
	.reset_name	= "rcpu-reset",
};

static const struct spacemit_ccu_data k1_ccu_rcpu2_data = {
	.reset_name	= "rcpu2-reset",
};

static const struct spacemit_ccu_data k1_ccu_apbc2_data = {
	.reset_name	= "apbc2-reset",
};

static int spacemit_ccu_register(struct device *dev,
				 struct regmap *regmap,
				 struct regmap *lock_regmap,
				 const struct spacemit_ccu_data *data)
{
	struct clk_hw_onecell_data *clk_data;
	int i, ret;

	/* Nothing to do if the CCU does not implement any clocks */
	if (!data->hws)
		return 0;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, data->num),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	for (i = 0; i < data->num; i++) {
		struct clk_hw *hw = data->hws[i];
		struct ccu_common *common;
		const char *name;

		if (!hw) {
			clk_data->hws[i] = ERR_PTR(-ENOENT);
			continue;
		}

		name = hw->init->name;

		common = hw_to_ccu_common(hw);
		common->regmap		= regmap;
		common->lock_regmap	= lock_regmap;

		ret = devm_clk_hw_register(dev, hw);
		if (ret) {
			dev_err(dev, "Cannot register clock %d - %s\n",
				i, name);
			return ret;
		}

		clk_data->hws[i] = hw;
	}

	clk_data->num = data->num;

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
	if (ret)
		dev_err(dev, "failed to add clock hardware provider (%d)\n", ret);

	return ret;
}

static void spacemit_cadev_release(struct device *dev)
{
	struct auxiliary_device *adev = to_auxiliary_dev(dev);

	ida_free(&auxiliary_ids, adev->id);
	kfree(to_spacemit_ccu_adev(adev));
}

static void spacemit_adev_unregister(void *data)
{
	struct auxiliary_device *adev = data;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

static int spacemit_ccu_reset_register(struct device *dev,
				       struct regmap *regmap,
				       const char *reset_name)
{
	struct spacemit_ccu_adev *cadev;
	struct auxiliary_device *adev;
	int ret;

	/* Nothing to do if the CCU does not implement a reset controller */
	if (!reset_name)
		return 0;

	cadev = kzalloc(sizeof(*cadev), GFP_KERNEL);
	if (!cadev)
		return -ENOMEM;

	cadev->regmap = regmap;

	adev = &cadev->adev;
	adev->name = reset_name;
	adev->dev.parent = dev;
	adev->dev.release = spacemit_cadev_release;
	adev->dev.of_node = dev->of_node;
	ret = ida_alloc(&auxiliary_ids, GFP_KERNEL);
	if (ret < 0)
		goto err_free_cadev;
	adev->id = ret;

	ret = auxiliary_device_init(adev);
	if (ret)
		goto err_free_aux_id;

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	return devm_add_action_or_reset(dev, spacemit_adev_unregister, adev);

err_free_aux_id:
	ida_free(&auxiliary_ids, adev->id);
err_free_cadev:
	kfree(cadev);

	return ret;
}

static int k1_ccu_probe(struct platform_device *pdev)
{
	struct regmap *base_regmap, *lock_regmap = NULL;
	const struct spacemit_ccu_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	base_regmap = device_node_to_regmap(dev->of_node);
	if (IS_ERR(base_regmap))
		return dev_err_probe(dev, PTR_ERR(base_regmap),
				     "failed to get regmap\n");

	/*
	 * The lock status of PLLs locate in MPMU region, while PLLs themselves
	 * are in APBS region. Reference to MPMU syscon is required to check PLL
	 * status.
	 */
	if (of_device_is_compatible(dev->of_node, "spacemit,k1-pll")) {
		struct device_node *mpmu = of_parse_phandle(dev->of_node,
							    "spacemit,mpmu", 0);
		if (!mpmu)
			return dev_err_probe(dev, -ENODEV,
					     "Cannot parse MPMU region\n");

		lock_regmap = device_node_to_regmap(mpmu);
		of_node_put(mpmu);

		if (IS_ERR(lock_regmap))
			return dev_err_probe(dev, PTR_ERR(lock_regmap),
					     "failed to get lock regmap\n");
	}

	data = of_device_get_match_data(dev);

	ret = spacemit_ccu_register(dev, base_regmap, lock_regmap, data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register clocks\n");

	ret = spacemit_ccu_reset_register(dev, base_regmap, data->reset_name);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register resets\n");

	return 0;
}

static const struct of_device_id of_k1_ccu_match[] = {
	{
		.compatible	= "spacemit,k1-pll",
		.data		= &k1_ccu_pll_data,
	},
	{
		.compatible	= "spacemit,k1-syscon-mpmu",
		.data		= &k1_ccu_mpmu_data,
	},
	{
		.compatible	= "spacemit,k1-syscon-apbc",
		.data		= &k1_ccu_apbc_data,
	},
	{
		.compatible	= "spacemit,k1-syscon-apmu",
		.data		= &k1_ccu_apmu_data,
	},
	{
		.compatible	= "spacemit,k1-syscon-rcpu",
		.data		= &k1_ccu_rcpu_data,
	},
	{
		.compatible	= "spacemit,k1-syscon-rcpu2",
		.data		= &k1_ccu_rcpu2_data,
	},
	{
		.compatible	= "spacemit,k1-syscon-apbc2",
		.data		= &k1_ccu_apbc2_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, of_k1_ccu_match);

static struct platform_driver k1_ccu_driver = {
	.driver = {
		.name		= "spacemit,k1-ccu",
		.of_match_table = of_k1_ccu_match,
	},
	.probe	= k1_ccu_probe,
};
module_platform_driver(k1_ccu_driver);

MODULE_DESCRIPTION("SpacemiT K1 CCU driver");
MODULE_AUTHOR("Haylen Chu <heylenay@4d2.org>");
MODULE_LICENSE("GPL");
