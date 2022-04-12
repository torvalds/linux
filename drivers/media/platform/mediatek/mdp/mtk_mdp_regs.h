/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 */

#ifndef __MTK_MDP_REGS_H__
#define __MTK_MDP_REGS_H__


void mtk_mdp_hw_set_input_addr(struct mtk_mdp_ctx *ctx,
			       struct mtk_mdp_addr *addr);
void mtk_mdp_hw_set_output_addr(struct mtk_mdp_ctx *ctx,
				struct mtk_mdp_addr *addr);
void mtk_mdp_hw_set_in_size(struct mtk_mdp_ctx *ctx);
void mtk_mdp_hw_set_in_image_format(struct mtk_mdp_ctx *ctx);
void mtk_mdp_hw_set_out_size(struct mtk_mdp_ctx *ctx);
void mtk_mdp_hw_set_out_image_format(struct mtk_mdp_ctx *ctx);
void mtk_mdp_hw_set_rotation(struct mtk_mdp_ctx *ctx);
void mtk_mdp_hw_set_global_alpha(struct mtk_mdp_ctx *ctx);


#endif /* __MTK_MDP_REGS_H__ */
