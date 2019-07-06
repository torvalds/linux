/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 */

#ifndef __MTK_MDP_M2M_H__
#define __MTK_MDP_M2M_H__

void mtk_mdp_ctx_state_lock_set(struct mtk_mdp_ctx *ctx, u32 state);
int mtk_mdp_register_m2m_device(struct mtk_mdp_dev *mdp);
void mtk_mdp_unregister_m2m_device(struct mtk_mdp_dev *mdp);

#endif /* __MTK_MDP_M2M_H__ */
