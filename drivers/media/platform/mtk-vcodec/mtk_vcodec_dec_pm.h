/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Tiffany Lin <tiffany.lin@mediatek.com>
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

#ifndef _MTK_VCODEC_DEC_PM_H_
#define _MTK_VCODEC_DEC_PM_H_

#include "mtk_vcodec_drv.h"

int mtk_vcodec_init_dec_pm(struct mtk_vcodec_dev *dev);
void mtk_vcodec_release_dec_pm(struct mtk_vcodec_dev *dev);

void mtk_vcodec_dec_pw_on(struct mtk_vcodec_pm *pm);
void mtk_vcodec_dec_pw_off(struct mtk_vcodec_pm *pm);
void mtk_vcodec_dec_clock_on(struct mtk_vcodec_pm *pm);
void mtk_vcodec_dec_clock_off(struct mtk_vcodec_pm *pm);

#endif /* _MTK_VCODEC_DEC_PM_H_ */
