/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MTK_MDP3_CFG_H__
#define __MTK_MDP3_CFG_H__

#include <linux/types.h>

extern const struct mtk_mdp_driver_data mt8183_mdp_driver_data;

struct mdp_dev;
enum mtk_mdp_comp_id;

s32 mdp_cfg_get_id_inner(struct mdp_dev *mdp_dev, enum mtk_mdp_comp_id id);
enum mtk_mdp_comp_id mdp_cfg_get_id_public(struct mdp_dev *mdp_dev, s32 id);

#endif  /* __MTK_MDP3_CFG_H__ */
