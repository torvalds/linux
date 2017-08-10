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

#ifndef __MTK_MDP_M2M_H__
#define __MTK_MDP_M2M_H__

void mtk_mdp_ctx_state_lock_set(struct mtk_mdp_ctx *ctx, u32 state);
int mtk_mdp_register_m2m_device(struct mtk_mdp_dev *mdp);
void mtk_mdp_unregister_m2m_device(struct mtk_mdp_dev *mdp);

#endif /* __MTK_MDP_M2M_H__ */
