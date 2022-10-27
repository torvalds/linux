/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_FEC_H
#define _RKISPP_FEC_H

#include "hw.h"

struct rkispp_fec_dev {
	struct rkispp_hw_dev *hw;
	struct v4l2_device v4l2_dev;
	struct video_device vfd;
	struct mutex apilock;
	struct completion cmpl;
	struct list_head list;
};

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISPP_FEC)
int rkispp_register_fec(struct rkispp_hw_dev *hw);
void rkispp_unregister_fec(struct rkispp_hw_dev *hw);
void rkispp_fec_irq(struct rkispp_hw_dev *hw);
#else
static inline int rkispp_register_fec(struct rkispp_hw_dev *hw) { return 0; }
static inline void rkispp_unregister_fec(struct rkispp_hw_dev *hw) {  }
static inline void rkispp_fec_irq(struct rkispp_hw_dev *hw) {  }
#endif

#endif
