/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef __ROCKCHIP_HPTIEMR_H__
#define __ROCKCHIP_HPTIEMR_H__

enum rk_hptimer_mode_t {
	RK_HPTIMER_NORM_MODE = 0,
	RK_HPTIMER_HARD_ADJUST_MODE = 1,
	RK_HPTIMER_SOFT_ADJUST_MODE = 2,
};

int rk_hptimer_is_enabled(void __iomem *base);
int rk_hptimer_get_mode(void __iomem *base);
u64 rk_hptimer_get_count(void __iomem *base);
int rk_hptimer_wait_mode(void __iomem *base, enum rk_hptimer_mode_t mode);
void rk_hptimer_do_soft_adjust(void __iomem *base);
void rk_hptimer_do_soft_adjust_no_wait(void __iomem *base);
void rk_hptimer_mode_init(void __iomem *base, enum rk_hptimer_mode_t mode);
#endif
