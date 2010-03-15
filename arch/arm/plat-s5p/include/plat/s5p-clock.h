/* linux/arch/arm/plat-s5p/include/plat/s5p-clock.h
 *
 * Copyright 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
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
#define clk_fin_mpll clk_ext_xtal_mux
#define clk_fin_epll clk_ext_xtal_mux
#define clk_fin_vpll clk_ext_xtal_mux

extern struct clk clk_ext_xtal_mux;
extern struct clk clk_48m;
extern struct clk clk_fout_apll;
extern struct clk clk_fout_mpll;
extern struct clk clk_fout_epll;
extern struct clk clk_arm;
extern struct clk clk_vpll;

extern struct clksrc_sources clk_src_apll;
extern struct clksrc_sources clk_src_mpll;
extern struct clksrc_sources clk_src_epll;

extern int s5p6440_clk48m_ctrl(struct clk *clk, int enable);
extern int s5p_gatectrl(void __iomem *reg, struct clk *clk, int enable);

#endif /* __ASM_PLAT_S5P_CLOCK_H */
