/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
