/* SPDX-License-Identifier: GPL-2.0+ */
//
// OWL common clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#ifndef _OWL_COMMON_H_
#define _OWL_COMMON_H_

#include <linux/clk-provider.h>
#include <linux/regmap.h>

struct device_node;
struct platform_device;

struct owl_clk_common {
	struct regmap			*regmap;
	struct clk_hw			hw;
};

struct owl_clk_desc {
	struct owl_clk_common		**clks;
	unsigned long			num_clks;
	struct clk_hw_onecell_data	*hw_clks;
	const struct owl_reset_map	*resets;
	unsigned long			num_resets;
	struct regmap			*regmap;
};

static inline struct owl_clk_common *
	hw_to_owl_clk_common(const struct clk_hw *hw)
{
	return container_of(hw, struct owl_clk_common, hw);
}

int owl_clk_regmap_init(struct platform_device *pdev,
			struct owl_clk_desc *desc);
int owl_clk_probe(struct device *dev, struct clk_hw_onecell_data *hw_clks);

#endif /* _OWL_COMMON_H_ */
