/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#ifndef _MTK_VCODEC_DEC_HW_H_
#define _MTK_VCODEC_DEC_HW_H_

#include <linux/io.h>
#include <linux/platform_device.h>

#include "mtk_vcodec_drv.h"

#define VDEC_HW_ACTIVE 0x10
#define VDEC_IRQ_CFG 0x11
#define VDEC_IRQ_CLR 0x10
#define VDEC_IRQ_CFG_REG 0xa4

/**
 * enum mtk_vdec_hw_reg_idx - subdev hardware register base index
 * @VDEC_HW_SYS : vdec soc register index
 * @VDEC_HW_MISC: vdec misc register index
 * @VDEC_HW_MAX : vdec supported max register index
 */
enum mtk_vdec_hw_reg_idx {
	VDEC_HW_SYS,
	VDEC_HW_MISC,
	VDEC_HW_MAX
};

/**
 * struct mtk_vdec_hw_dev - vdec hardware driver data
 * @plat_dev: platform device
 * @main_dev: main device
 * @reg_base: mapped address of MTK Vcodec registers.
 *
 * @curr_ctx: the context that is waiting for codec hardware
 *
 * @dec_irq : decoder irq resource
 * @pm      : power management control
 * @hw_idx  : each hardware index
 */
struct mtk_vdec_hw_dev {
	struct platform_device *plat_dev;
	struct mtk_vcodec_dev *main_dev;
	void __iomem *reg_base[VDEC_HW_MAX];

	struct mtk_vcodec_ctx *curr_ctx;

	int dec_irq;
	struct mtk_vcodec_pm pm;
	int hw_idx;
};

#endif /* _MTK_VCODEC_DEC_HW_H_ */
