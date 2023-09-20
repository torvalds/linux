/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Nuvoton Technology Corp.
 * Author: Chi-Fang Li <cfli0@nuvoton.com>
 */

#ifndef __DRV_CLK_NUVOTON_MA35D1_H
#define __DRV_CLK_NUVOTON_MA35D1_H

struct clk_hw *ma35d1_reg_clk_pll(struct device *dev, u32 id, u8 u8mode, const char *name,
				  struct clk_hw *parent_hw, void __iomem *base);

struct clk_hw *ma35d1_reg_adc_clkdiv(struct device *dev, const char *name,
				     struct clk_hw *parent_hw, spinlock_t *lock,
				     unsigned long flags, void __iomem *reg,
				     u8 shift, u8 width, u32 mask_bit);

#endif /* __DRV_CLK_NUVOTON_MA35D1_H */
