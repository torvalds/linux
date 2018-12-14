/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2018 NXP
 *   Dong Aisheng <aisheng.dong@nxp.com>
 */

#ifndef __IMX_CLK_SCU_H
#define __IMX_CLK_SCU_H

#include <linux/firmware/imx/sci.h>

int imx_clk_scu_init(void);
struct clk_hw *imx_clk_scu(const char *name, u32 rsrc_id, u8 clk_type);

struct clk_hw *imx_clk_lpcg_scu(const char *name, const char *parent_name,
				unsigned long flags, void __iomem *reg,
				u8 bit_idx, bool hw_gate);
#endif
