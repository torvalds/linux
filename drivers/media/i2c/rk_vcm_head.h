/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef RK_VCM_HEAD_H
#define RK_VCM_HEAD_H

/*
 * Focus position values:
 * 65 logical positions ( 0 - 64 )
 * where 64 is the setting for infinity and 0 for macro
 */
#define VCMDRV_MAX_LOG			64U

#define OF_CAMERA_VCMDRV_START_CURRENT	"rockchip,vcm-start-current"
#define OF_CAMERA_VCMDRV_RATED_CURRENT	"rockchip,vcm-rated-current"
#define OF_CAMERA_VCMDRV_STEP_MODE	"rockchip,vcm-step-mode"

#define RK_VIDIOC_VCM_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 0, struct rk_cam_vcm_tim)

#ifdef CONFIG_COMPAT
#define RK_VIDIOC_COMPAT_VCM_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 0, struct rk_cam_compat_vcm_tim)
#endif

struct rk_cam_vcm_tim {
	struct timeval vcm_start_t;
	struct timeval vcm_end_t;
};

#ifdef CONFIG_COMPAT
struct rk_cam_compat_vcm_tim {
	struct compat_timeval vcm_start_t;
	struct compat_timeval vcm_end_t;
};
#endif

#endif /* RK_VCM_HEAD_H */

