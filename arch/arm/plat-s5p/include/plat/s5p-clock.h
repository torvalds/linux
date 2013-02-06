/* linux/arch/arm/plat-s5p/include/plat/s5p-clock.h
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Header file for s5p clock support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_S5P_CLOCK_H
#define __ASM_PLAT_S5P_CLOCK_H __FILE__

#include <linux/clk.h>

#define GET_DIV(clk, field) ((((clk) & field##_MASK) >> field##_SHIFT) + 1)

#define clk_fin_apll clk_ext_xtal_mux
#define clk_fin_bpll clk_ext_xtal_mux
#define clk_fin_gpll clk_ext_xtal_mux
#define clk_fin_cpll clk_ext_xtal_mux
#define clk_fin_mpll clk_ext_xtal_mux
#define clk_fin_epll clk_ext_xtal_mux
#define clk_fin_dpll clk_ext_xtal_mux
#define clk_fin_vpll clk_ext_xtal_mux
#define clk_fin_hpll clk_ext_xtal_mux

extern struct clk clk_ext_xtal_mux;
extern struct clk clk_xusbxti;
extern struct clk clk_48m;
extern struct clk s5p_clk_27m;
extern struct clk clk_fout_apll;
extern struct clk clk_fout_mpll;
extern struct clk clk_fout_epll;
extern struct clk clk_fout_dpll;
extern struct clk clk_fout_vpll;
extern struct clk clk_arm;
extern struct clk clk_vpll;

extern struct clksrc_sources clk_src_apll;
extern struct clksrc_sources clk_src_mpll;
extern struct clksrc_sources clk_src_epll;
extern struct clksrc_sources clk_src_dpll;

extern int s5p_gatectrl(void __iomem *reg, struct clk *clk, int enable);

/* Common EPLL operations for S5P platform */
extern int s5p_epll_enable(struct clk *clk, int enable);
extern unsigned long s5p_epll_get_rate(struct clk *clk);

/* SPDIF clk operations common for S5PC100/V210/C110 and Exynos4 */
extern int s5p_spdif_set_rate(struct clk *clk, unsigned long rate);
extern unsigned long s5p_spdif_get_rate(struct clk *clk);

extern struct clk_ops s5p_sclk_spdif_ops;
#endif /* __ASM_PLAT_S5P_CLOCK_H */
