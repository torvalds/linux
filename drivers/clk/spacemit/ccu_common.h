/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 SpacemiT Technology Co. Ltd
 * Copyright (c) 2024-2025 Haylen Chu <heylenay@4d2.org>
 */

#ifndef _CCU_COMMON_H_
#define _CCU_COMMON_H_

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

struct ccu_common {
	struct regmap *regmap;
	struct regmap *lock_regmap;

	union {
		/* For DDN and MIX */
		struct {
			u32 reg_ctrl;
			u32 reg_fc;
			u32 mask_fc;
		};

		/* For PLL */
		struct {
			u32 reg_swcr1;
			u32 reg_swcr2;
			u32 reg_swcr3;
		};
	};

	struct clk_hw hw;
};

static inline struct ccu_common *hw_to_ccu_common(struct clk_hw *hw)
{
	return container_of(hw, struct ccu_common, hw);
}

struct spacemit_ccu_data {
	const char *reset_name;
	struct clk_hw **hws;
	size_t num;
};

#define ccu_read(c, reg)						\
	({								\
		u32 tmp;						\
		regmap_read((c)->regmap, (c)->reg_##reg, &tmp);		\
		tmp;							\
	 })
#define ccu_update(c, reg, mask, val) \
	regmap_update_bits((c)->regmap, (c)->reg_##reg, mask, val)

int spacemit_ccu_probe(struct platform_device *pdev, const char *compat);

#endif /* _CCU_COMMON_H_ */
