/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 */

#ifndef __MTK_MDP_COMP_H__
#define __MTK_MDP_COMP_H__

/**
 * enum mtk_mdp_comp_type - the MDP component
 * @MTK_MDP_RDMA:	Read DMA
 * @MTK_MDP_RSZ:	Riszer
 * @MTK_MDP_WDMA:	Write DMA
 * @MTK_MDP_WROT:	Write DMA with rotation
 */
enum mtk_mdp_comp_type {
	MTK_MDP_RDMA,
	MTK_MDP_RSZ,
	MTK_MDP_WDMA,
	MTK_MDP_WROT,
	MTK_MDP_COMP_TYPE_MAX,
};

/**
 * struct mtk_mdp_comp - the MDP's function component data
 * @node:	list node to track sibing MDP components
 * @dev_node:	component device node
 * @clk:	clocks required for component
 * @larb_dev:	SMI device required for component
 * @type:	component type
 */
struct mtk_mdp_comp {
	struct list_head	node;
	struct device_node	*dev_node;
	struct clk		*clk[2];
	struct device		*larb_dev;
	enum mtk_mdp_comp_type	type;
};

int mtk_mdp_comp_init(struct device *dev, struct device_node *node,
		      struct mtk_mdp_comp *comp,
		      enum mtk_mdp_comp_type comp_type);
void mtk_mdp_comp_deinit(struct device *dev, struct mtk_mdp_comp *comp);
void mtk_mdp_comp_clock_on(struct device *dev, struct mtk_mdp_comp *comp);
void mtk_mdp_comp_clock_off(struct device *dev, struct mtk_mdp_comp *comp);


#endif /* __MTK_MDP_COMP_H__ */
