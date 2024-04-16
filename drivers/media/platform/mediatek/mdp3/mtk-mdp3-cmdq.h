/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MTK_MDP3_CMDQ_H__
#define __MTK_MDP3_CMDQ_H__

#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/soc/mediatek/mtk-cmdq.h>
#include "mtk-img-ipi.h"

struct platform_device *mdp_get_plat_device(struct platform_device *pdev);

struct mdp_cmdq_param {
	struct img_config *config;
	struct img_ipi_frameparam *param;
	const struct v4l2_rect *composes[IMG_MAX_HW_OUTPUTS];

	void (*cmdq_cb)(struct cmdq_cb_data data);
	void *cb_data;
	void *mdp_ctx;
};

struct mdp_cmdq_cmd {
	struct work_struct auto_release_work;
	struct cmdq_pkt pkt;
	s32 *event;
	struct mdp_dev *mdp;
	void (*user_cmdq_cb)(struct cmdq_cb_data data);
	void *user_cb_data;
	struct mdp_comp *comps;
	void *mdp_ctx;
	u8 num_comps;
};

struct mdp_dev;

int mdp_cmdq_send(struct mdp_dev *mdp, struct mdp_cmdq_param *param);

#endif  /* __MTK_MDP3_CMDQ_H__ */
