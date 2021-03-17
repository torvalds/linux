/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Baikal-T1 CCU PLL interface driver
 */
#ifndef __CLK_BT1_CCU_PLL_H__
#define __CLK_BT1_CCU_PLL_H__

#include <linux/clk-provider.h>
#include <linux/spinlock.h>
#include <linux/regmap.h>
#include <linux/bits.h>
#include <linux/of.h>

/*
 * struct ccu_pll_init_data - CCU PLL initialization data
 * @id: Clock private identifier.
 * @name: Clocks name.
 * @parent_name: Clocks parent name in a fw node.
 * @base: PLL registers base address with respect to the sys_regs base.
 * @sys_regs: Baikal-T1 System Controller registers map.
 * @np: Pointer to the node describing the CCU PLLs.
 * @flags: PLL clock flags.
 */
struct ccu_pll_init_data {
	unsigned int id;
	const char *name;
	const char *parent_name;
	unsigned int base;
	struct regmap *sys_regs;
	struct device_node *np;
	unsigned long flags;
};

/*
 * struct ccu_pll - CCU PLL descriptor
 * @hw: clk_hw of the PLL.
 * @id: Clock private identifier.
 * @reg_ctl: PLL control register base.
 * @reg_ctl1: PLL control1 register base.
 * @sys_regs: Baikal-T1 System Controller registers map.
 * @lock: PLL state change spin-lock.
 */
struct ccu_pll {
	struct clk_hw hw;
	unsigned int id;
	unsigned int reg_ctl;
	unsigned int reg_ctl1;
	struct regmap *sys_regs;
	spinlock_t lock;
};
#define to_ccu_pll(_hw) container_of(_hw, struct ccu_pll, hw)

static inline struct clk_hw *ccu_pll_get_clk_hw(struct ccu_pll *pll)
{
	return pll ? &pll->hw : NULL;
}

struct ccu_pll *ccu_pll_hw_register(const struct ccu_pll_init_data *init);

void ccu_pll_hw_unregister(struct ccu_pll *pll);

#endif /* __CLK_BT1_CCU_PLL_H__ */
