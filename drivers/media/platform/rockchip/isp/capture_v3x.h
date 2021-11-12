/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021 Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_CAPTURE_V3X_H
#define _RKISP_CAPTURE_V3X_H

/* memory align for mpp */
#define RK_MPP_ALIGN 4096
//#define RKISP_STREAM_BP_EN 1

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V30)
int rkisp_register_stream_v30(struct rkisp_device *dev);
void rkisp_unregister_stream_v30(struct rkisp_device *dev);
void rkisp_mi_v30_isr(u32 mis_val, struct rkisp_device *dev);
void rkisp_mipi_v30_isr(u32 phy, u32 packet, u32 overflow, u32 state, struct rkisp_device *dev);
#else
static inline int rkisp_register_stream_v30(struct rkisp_device *dev) { return -EINVAL; }
static inline void rkisp_unregister_stream_v30(struct rkisp_device *dev) {}
static inline void rkisp_mi_v30_isr(u32 mis_val, struct rkisp_device *dev) {}
static inline void rkisp_mipi_v30_isr(u32 phy, u32 packet, u32 overflow, u32 state, struct rkisp_device *dev) {}
#endif

#endif
