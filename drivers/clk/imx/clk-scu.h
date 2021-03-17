/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2018 NXP
 *   Dong Aisheng <aisheng.dong@nxp.com>
 */

#ifndef __IMX_CLK_SCU_H
#define __IMX_CLK_SCU_H

#include <linux/firmware/imx/sci.h>

int imx_clk_scu_init(void);

struct clk_hw *__imx_clk_scu(const char *name, const char * const *parents,
			     int num_parents, u32 rsrc_id, u8 clk_type);

static inline struct clk_hw *imx_clk_scu(const char *name, u32 rsrc_id,
					 u8 clk_type)
{
	return __imx_clk_scu(name, NULL, 0, rsrc_id, clk_type);
}

static inline struct clk_hw *imx_clk_scu2(const char *name, const char * const *parents,
					  int num_parents, u32 rsrc_id, u8 clk_type)
{
	return __imx_clk_scu(name, parents, num_parents, rsrc_id, clk_type);
}

struct clk_hw *imx_clk_lpcg_scu(const char *name, const char *parent_name,
				unsigned long flags, void __iomem *reg,
				u8 bit_idx, bool hw_gate);
#endif
