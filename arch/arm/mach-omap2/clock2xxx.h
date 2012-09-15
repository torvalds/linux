/*
 * OMAP2 clock function prototypes and macros
 *
 * Copyright (C) 2005-2010 Texas Instruments, Inc.
 * Copyright (C) 2004-2010 Nokia Corporation
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CLOCK2XXX_H
#define __ARCH_ARM_MACH_OMAP2_CLOCK2XXX_H

#ifdef CONFIG_COMMON_CLK
#include <linux/clk-provider.h>
#include "clock.h"

unsigned long omap2_table_mpu_recalc(struct clk_hw *clk,
				     unsigned long parent_rate);
int omap2_select_table_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate);
long omap2_round_to_table_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate);
unsigned long omap2xxx_sys_clk_recalc(struct clk_hw *clk,
				      unsigned long parent_rate);
unsigned long omap2_osc_clk_recalc(struct clk_hw *clk,
				   unsigned long parent_rate);
unsigned long omap2_dpllcore_recalc(struct clk_hw *hw,
				    unsigned long parent_rate);
int omap2_reprogram_dpllcore(struct clk_hw *clk, unsigned long rate,
			     unsigned long parent_rate);
void omap2xxx_clkt_dpllcore_init(struct clk_hw *hw);
unsigned long omap2_clk_apll54_recalc(struct clk_hw *hw,
				      unsigned long parent_rate);
unsigned long omap2_clk_apll96_recalc(struct clk_hw *hw,
				      unsigned long parent_rate);
#else
unsigned long omap2_table_mpu_recalc(struct clk *clk);
int omap2_select_table_rate(struct clk *clk, unsigned long rate);
long omap2_round_to_table_rate(struct clk *clk, unsigned long rate);
unsigned long omap2xxx_sys_clk_recalc(struct clk *clk);
unsigned long omap2_osc_clk_recalc(struct clk *clk);
unsigned long omap2_dpllcore_recalc(struct clk *clk);
int omap2_reprogram_dpllcore(struct clk *clk, unsigned long rate);
void omap2xxx_clkt_dpllcore_init(struct clk *clk);
#endif
unsigned long omap2xxx_clk_get_core_rate(void);
u32 omap2xxx_get_apll_clkin(void);
u32 omap2xxx_get_sysclkdiv(void);
void omap2xxx_clk_prepare_for_reboot(void);
void omap2xxx_clkt_vps_check_bootloader_rates(void);
void omap2xxx_clkt_vps_late_init(void);

#ifdef CONFIG_SOC_OMAP2420
int omap2420_clk_init(void);
#else
#define omap2420_clk_init()	do { } while(0)
#endif

#ifdef CONFIG_SOC_OMAP2430
int omap2430_clk_init(void);
#else
#define omap2430_clk_init()	do { } while(0)
#endif

extern void __iomem *prcm_clksrc_ctrl;

#ifdef CONFIG_COMMON_CLK
extern struct clk_hw *dclk_hw;
int omap2_enable_osc_ck(struct clk_hw *hw);
void omap2_disable_osc_ck(struct clk_hw *hw);
int omap2_clk_apll96_enable(struct clk_hw *hw);
int omap2_clk_apll54_enable(struct clk_hw *hw);
void omap2_clk_apll96_disable(struct clk_hw *hw);
void omap2_clk_apll54_disable(struct clk_hw *hw);
#else
extern const struct clkops clkops_omap2430_i2chs_wait;
extern const struct clkops clkops_oscck;
extern const struct clkops clkops_apll96;
extern const struct clkops clkops_apll54;
#endif

#endif
