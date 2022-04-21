/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Tiffany Lin <tiffany.lin@mediatek.com>
 */

#ifndef _MTK_VCODEC_DEC_PM_H_
#define _MTK_VCODEC_DEC_PM_H_

#include "mtk_vcodec_drv.h"

int mtk_vcodec_init_dec_clk(struct platform_device *pdev, struct mtk_vcodec_pm *pm);

int mtk_vcodec_dec_pw_on(struct mtk_vcodec_dev *vdec_dev, int hw_idx);
void mtk_vcodec_dec_pw_off(struct mtk_vcodec_dev *vdec_dev, int hw_idx);
void mtk_vcodec_dec_clock_on(struct mtk_vcodec_dev *vdec_dev, int hw_idx);
void mtk_vcodec_dec_clock_off(struct mtk_vcodec_dev *vdec_dev, int hw_idx);

#endif /* _MTK_VCODEC_DEC_PM_H_ */
