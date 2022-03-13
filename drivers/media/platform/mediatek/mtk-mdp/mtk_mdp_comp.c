// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include "mtk_mdp_comp.h"


void mtk_mdp_comp_clock_on(struct device *dev, struct mtk_mdp_comp *comp)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(comp->clk); i++) {
		if (IS_ERR(comp->clk[i]))
			continue;
		err = clk_prepare_enable(comp->clk[i]);
		if (err)
			dev_err(dev,
			"failed to enable clock, err %d. type:%d i:%d\n",
				err, comp->type, i);
	}
}

void mtk_mdp_comp_clock_off(struct device *dev, struct mtk_mdp_comp *comp)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(comp->clk); i++) {
		if (IS_ERR(comp->clk[i]))
			continue;
		clk_disable_unprepare(comp->clk[i]);
	}
}

int mtk_mdp_comp_init(struct device *dev, struct device_node *node,
		      struct mtk_mdp_comp *comp,
		      enum mtk_mdp_comp_type comp_type)
{
	int ret;
	int i;

	comp->dev_node = of_node_get(node);
	comp->type = comp_type;

	for (i = 0; i < ARRAY_SIZE(comp->clk); i++) {
		comp->clk[i] = of_clk_get(node, i);
		if (IS_ERR(comp->clk[i])) {
			if (PTR_ERR(comp->clk[i]) != -EPROBE_DEFER)
				dev_err(dev, "Failed to get clock\n");
			ret = PTR_ERR(comp->clk[i]);
			goto put_dev;
		}

		/* Only RDMA needs two clocks */
		if (comp->type != MTK_MDP_RDMA)
			break;
	}

	return 0;

put_dev:
	of_node_put(comp->dev_node);

	return ret;
}

void mtk_mdp_comp_deinit(struct device *dev, struct mtk_mdp_comp *comp)
{
	of_node_put(comp->dev_node);
}
