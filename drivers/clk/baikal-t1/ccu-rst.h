/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 BAIKAL ELECTRONICS, JSC
 *
 * Baikal-T1 CCU Resets interface driver
 */
#ifndef __CLK_BT1_CCU_RST_H__
#define __CLK_BT1_CCU_RST_H__

#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

struct ccu_rst_info;

/*
 * enum ccu_rst_type - CCU Reset types
 * @CCU_RST_TRIG: Self-deasserted reset signal.
 * @CCU_RST_DIR: Directly controlled reset signal.
 */
enum ccu_rst_type {
	CCU_RST_TRIG,
	CCU_RST_DIR,
};

/*
 * struct ccu_rst_init_data - CCU Resets initialization data
 * @sys_regs: Baikal-T1 System Controller registers map.
 * @np: Pointer to the node with the System CCU block.
 */
struct ccu_rst_init_data {
	struct regmap *sys_regs;
	struct device_node *np;
};

/*
 * struct ccu_rst - CCU Reset descriptor
 * @rcdev: Reset controller descriptor.
 * @sys_regs: Baikal-T1 System Controller registers map.
 * @rsts_info: Reset flag info (base address and mask).
 */
struct ccu_rst {
	struct reset_controller_dev rcdev;
	struct regmap *sys_regs;
	const struct ccu_rst_info *rsts_info;
};
#define to_ccu_rst(_rcdev) container_of(_rcdev, struct ccu_rst, rcdev)

#ifdef CONFIG_CLK_BT1_CCU_RST

struct ccu_rst *ccu_rst_hw_register(const struct ccu_rst_init_data *init);

void ccu_rst_hw_unregister(struct ccu_rst *rst);

#else

static inline
struct ccu_rst *ccu_rst_hw_register(const struct ccu_rst_init_data *init)
{
	return NULL;
}

static inline void ccu_rst_hw_unregister(struct ccu_rst *rst) {}

#endif

#endif /* __CLK_BT1_CCU_RST_H__ */
