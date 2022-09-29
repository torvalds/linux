/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Baikal-T1 CCU Dividers interface driver
 */
#ifndef __CLK_BT1_CCU_DIV_H__
#define __CLK_BT1_CCU_DIV_H__

#include <linux/clk-provider.h>
#include <linux/spinlock.h>
#include <linux/regmap.h>
#include <linux/bits.h>
#include <linux/of.h>

/*
 * CCU Divider private clock IDs
 * @CCU_SYS_XGMAC_CLK: CCU XGMAC internal clock
 */
#define CCU_SYS_XGMAC_CLK		-2

/*
 * CCU Divider private flags
 * @CCU_DIV_SKIP_ONE: Due to some reason divider can't be set to 1.
 *		      It can be 0 though, which is functionally the same.
 * @CCU_DIV_SKIP_ONE_TO_THREE: For some reason divider can't be within [1,3].
 *			       It can be either 0 or greater than 3.
 * @CCU_DIV_LOCK_SHIFTED: Find lock-bit at non-standard position.
 * @CCU_DIV_RESET_DOMAIN: Provide reset clock domain method.
 */
#define CCU_DIV_SKIP_ONE		BIT(1)
#define CCU_DIV_SKIP_ONE_TO_THREE	BIT(2)
#define CCU_DIV_LOCK_SHIFTED		BIT(3)
#define CCU_DIV_RESET_DOMAIN		BIT(4)

/*
 * enum ccu_div_type - CCU Divider types
 * @CCU_DIV_VAR: Clocks gate with variable divider.
 * @CCU_DIV_GATE: Clocks gate with fixed divider.
 * @CCU_DIV_FIXED: Ungateable clock with fixed divider.
 */
enum ccu_div_type {
	CCU_DIV_VAR,
	CCU_DIV_GATE,
	CCU_DIV_FIXED
};

/*
 * struct ccu_div_init_data - CCU Divider initialization data
 * @id: Clocks private identifier.
 * @name: Clocks name.
 * @parent_name: Parent clocks name in a fw node.
 * @base: Divider register base address with respect to the sys_regs base.
 * @sys_regs: Baikal-T1 System Controller registers map.
 * @np: Pointer to the node describing the CCU Dividers.
 * @type: CCU divider type (variable, fixed with and without gate).
 * @width: Divider width if it's variable.
 * @divider: Divider fixed value.
 * @flags: CCU Divider clock flags.
 * @features: CCU Divider private features.
 */
struct ccu_div_init_data {
	unsigned int id;
	const char *name;
	const char *parent_name;
	unsigned int base;
	struct regmap *sys_regs;
	struct device_node *np;
	enum ccu_div_type type;
	union {
		unsigned int width;
		unsigned int divider;
	};
	unsigned long flags;
	unsigned long features;
};

/*
 * struct ccu_div - CCU Divider descriptor
 * @hw: clk_hw of the divider.
 * @id: Clock private identifier.
 * @reg_ctl: Divider control register base address.
 * @sys_regs: Baikal-T1 System Controller registers map.
 * @lock: Divider state change spin-lock.
 * @mask: Divider field mask.
 * @divider: Divider fixed value.
 * @flags: Divider clock flags.
 * @features: CCU Divider private features.
 */
struct ccu_div {
	struct clk_hw hw;
	unsigned int id;
	unsigned int reg_ctl;
	struct regmap *sys_regs;
	spinlock_t lock;
	union {
		u32 mask;
		unsigned int divider;
	};
	unsigned long flags;
	unsigned long features;
};
#define to_ccu_div(_hw) container_of(_hw, struct ccu_div, hw)

static inline struct clk_hw *ccu_div_get_clk_hw(struct ccu_div *div)
{
	return div ? &div->hw : NULL;
}

struct ccu_div *ccu_div_hw_register(const struct ccu_div_init_data *init);

void ccu_div_hw_unregister(struct ccu_div *div);

int ccu_div_reset_domain(struct ccu_div *div);

#endif /* __CLK_BT1_CCU_DIV_H__ */
