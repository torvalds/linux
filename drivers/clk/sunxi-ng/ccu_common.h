/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 Maxime Ripard. All rights reserved.
 */

#ifndef _COMMON_H_
#define _COMMON_H_

#include <linux/compiler.h>
#include <linux/clk-provider.h>

#define CCU_FEATURE_FRACTIONAL		BIT(0)
#define CCU_FEATURE_VARIABLE_PREDIV	BIT(1)
#define CCU_FEATURE_FIXED_PREDIV	BIT(2)
#define CCU_FEATURE_FIXED_POSTDIV	BIT(3)
#define CCU_FEATURE_ALL_PREDIV		BIT(4)
#define CCU_FEATURE_LOCK_REG		BIT(5)
#define CCU_FEATURE_MMC_TIMING_SWITCH	BIT(6)
#define CCU_FEATURE_SIGMA_DELTA_MOD	BIT(7)
#define CCU_FEATURE_KEY_FIELD		BIT(8)

/* MMC timing mode switch bit */
#define CCU_MMC_NEW_TIMING_MODE		BIT(30)

struct device_node;

struct ccu_common {
	void __iomem	*base;
	u16		reg;
	u16		lock_reg;
	u32		prediv;

	unsigned long	features;
	spinlock_t	*lock;
	struct clk_hw	hw;
};

static inline struct ccu_common *hw_to_ccu_common(struct clk_hw *hw)
{
	return container_of(hw, struct ccu_common, hw);
}

struct sunxi_ccu_desc {
	struct ccu_common		**ccu_clks;
	unsigned long			num_ccu_clks;

	struct clk_hw_onecell_data	*hw_clks;

	struct ccu_reset_map		*resets;
	unsigned long			num_resets;
};

void ccu_helper_wait_for_lock(struct ccu_common *common, u32 lock);

struct ccu_pll_nb {
	struct notifier_block	clk_nb;
	struct ccu_common	*common;

	u32	enable;
	u32	lock;
};

#define to_ccu_pll_nb(_nb) container_of(_nb, struct ccu_pll_nb, clk_nb)

int ccu_pll_notifier_register(struct ccu_pll_nb *pll_nb);

int devm_sunxi_ccu_probe(struct device *dev, void __iomem *reg,
			 const struct sunxi_ccu_desc *desc);
void of_sunxi_ccu_probe(struct device_node *node, void __iomem *reg,
			const struct sunxi_ccu_desc *desc);

#endif /* _COMMON_H_ */
