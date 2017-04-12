/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Houlong Wei <houlong.wei@mediatek.com>
 *         Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
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

#ifndef __MTK_MDP_VPU_H__
#define __MTK_MDP_VPU_H__

#include "mtk_mdp_ipi.h"


/**
 * struct mtk_mdp_vpu - VPU instance for MDP
 * @pdev	: pointer to the VPU platform device
 * @inst_addr	: VPU MDP instance address
 * @failure	: VPU execution result status
 * @vsi		: VPU shared information
 */
struct mtk_mdp_vpu {
	struct platform_device	*pdev;
	uint32_t		inst_addr;
	int32_t			failure;
	struct mdp_process_vsi	*vsi;
};

int mtk_mdp_vpu_register(struct platform_device *pdev);
int mtk_mdp_vpu_init(struct mtk_mdp_vpu *vpu);
int mtk_mdp_vpu_deinit(struct mtk_mdp_vpu *vpu);
int mtk_mdp_vpu_process(struct mtk_mdp_vpu *vpu);

#endif /* __MTK_MDP_VPU_H__ */
