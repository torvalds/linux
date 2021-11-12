/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_CAPTURE_V1X_H
#define _RKISP_CAPTURE_V1X_H

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V1X)
int rkisp_register_stream_v1x(struct rkisp_device *dev);
void rkisp_unregister_stream_v1x(struct rkisp_device *dev);
void rkisp_mi_v1x_isr(u32 mis_val, struct rkisp_device *dev);
#else
static inline int rkisp_register_stream_v1x(struct rkisp_device *dev) { return -EINVAL; }
static inline void rkisp_unregister_stream_v1x(struct rkisp_device *dev) {}
static inline void rkisp_mi_v1x_isr(u32 mis_val, struct rkisp_device *dev) {}
#endif

#endif
