/* SPDX-License-Identifier: GPL-2.0 */
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: Tiffany Lin <tiffany.lin@mediatek.com>
*/

#ifndef _MTK_VCODEC_ENC_PM_H_
#define _MTK_VCODEC_ENC_PM_H_

#include "mtk_vcodec_enc_drv.h"

int mtk_vcodec_init_enc_clk(struct mtk_vcodec_enc_dev *dev);
int mtk_vcodec_enc_pw_on(struct mtk_vcodec_pm *pm);
void mtk_vcodec_enc_pw_off(struct mtk_vcodec_pm *pm);
void mtk_vcodec_enc_clock_on(struct mtk_vcodec_pm *pm);
void mtk_vcodec_enc_clock_off(struct mtk_vcodec_pm *pm);

#endif /* _MTK_VCODEC_ENC_PM_H_ */
