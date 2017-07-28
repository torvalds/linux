/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
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
#ifndef _MTK_CEC_H
#define _MTK_CEC_H

#include <linux/types.h>

struct device;

void mtk_cec_set_hpd_event(struct device *dev,
			   void (*hotplug_event)(bool hpd, struct device *dev),
			   struct device *hdmi_dev);
bool mtk_cec_hpd_high(struct device *dev);

#endif /* _MTK_CEC_H */
