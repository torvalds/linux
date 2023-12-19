/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2014, 2019-2021, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved. */

#ifndef __QCOM_CLK_REGMAP_H__
#define __QCOM_CLK_REGMAP_H__

#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include "vdd-class.h"
#include <soc/qcom/crm.h>

struct regmap;

/**
 * struct clk_regmap_ops - Operations for clk_regmap.
 *
 * @list_registers: Queries the hardware to get the current register contents.
 *		    This callback is optional.
 *
 * @list_rate:  On success, return the nth supported frequency for a given
 *		clock that is below rate_max. Return -ENXIO in case there is
 *		no frequency table.
 *
 * @set_flags: Set custom flags which deal with hardware specifics. Returns 0
 *		on success, error otherwise.
 *
 * @calc_pll: On success returns pll output frequency. Returns 0
 *		on success, error otherwise.
 * @set_crm_rate: Set crmc/crmb clk frequency. Returns 0
 *		on success, error otherwise.
 */
struct clk_regmap_ops {
	void	(*list_registers)(struct seq_file *f,
				  struct clk_hw *hw);
	long	(*list_rate)(struct clk_hw *hw, unsigned int n,
			     unsigned long rate_max);
	int	(*set_flags)(struct clk_hw *clk, unsigned long flags);
	unsigned long	(*calc_pll)(struct clk_hw *hw, u32 l, u64 a);
	unsigned long	(*set_crm_rate)(struct clk_hw *hw, enum crm_drv_type client_type,
					u32 client_idx, u32 pwr_st, unsigned long rate);
};

/**
 * struct clk_regmap - regmap supporting clock
 * @hw:		handle between common and hardware-specific interfaces
 * @regmap:	regmap to use for regmap helpers and/or by providers
 * @enable_reg: register when using regmap enable/disable ops
 * @enable_mask: mask when using regmap enable/disable ops
 * @enable_is_inverted: flag to indicate set enable_mask bits to disable
 *                      when using clock_enable_regmap and friends APIs.
 * @vdd_data:	struct containing vdd-class data for this clock
 * @ops: operations this clk_regmap supports
 * @crm: clk crm regmap
 */

struct clk_regmap {
	struct clk_hw hw;
	struct regmap *regmap;
	unsigned int enable_reg;
	unsigned int enable_mask;
	bool enable_is_inverted;
	struct clk_vdd_class_data vdd_data;
	struct clk_regmap_ops *ops;
	struct list_head list_node;
	struct device *dev;
	struct clk_crm *crm;
	u8 crm_vcd;
#define QCOM_CLK_IS_CRITICAL BIT(0)
#define QCOM_CLK_BOOT_CRITICAL BIT(1)
	unsigned long flags;
};

static inline struct clk_regmap *to_clk_regmap(struct clk_hw *hw)
{
	return container_of(hw, struct clk_regmap, hw);
}

int clk_is_enabled_regmap(struct clk_hw *hw);
int clk_enable_regmap(struct clk_hw *hw);
void clk_disable_regmap(struct clk_hw *hw);
int clk_prepare_regmap(struct clk_hw *hw);
void clk_unprepare_regmap(struct clk_hw *hw);
int clk_pre_change_regmap(struct clk_hw *hw, unsigned long cur_rate,
			unsigned long new_rate);
int clk_post_change_regmap(struct clk_hw *hw, unsigned long old_rate,
			unsigned long cur_rate);
int devm_clk_register_regmap(struct device *dev, struct clk_regmap *rclk);
void devm_clk_regmap_list_node(struct device *dev, struct clk_regmap *rclk);

bool clk_is_regmap_clk(struct clk_hw *hw);

int clk_runtime_get_regmap(struct clk_regmap *rclk);
void clk_runtime_put_regmap(struct clk_regmap *rclk);
void clk_restore_critical_clocks(struct device *dev);

struct clk_register_data {
	char *name;
	u32 offset;
};

#endif
