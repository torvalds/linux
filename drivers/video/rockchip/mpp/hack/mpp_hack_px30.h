/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */
#ifndef __ROCKCHIP_MPP_HACK_PX30_H__
#define __ROCKCHIP_MPP_HACK_PX30_H__

#ifdef CONFIG_CPU_PX30
int px30_workaround_combo_init(struct mpp_dev *mpp);
int px30_workaround_combo_switch_grf(struct mpp_dev *mpp);
#else
static inline int px30_workaround_combo_init(struct mpp_dev *mpp)
{
	return 0;
}

static inline int px30_workaround_combo_switch_grf(struct mpp_dev *mpp)
{
	return 0;
}
#endif

#endif
