/* linux/drivers/media/video/exynos/fimg2d/fimg2d_clk.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com/
 *
 * Samsung Graphics 2D driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __FIMG2D_CLK_H__
#define __FIMG2D_CLK_H__

#include <linux/pm_qos.h>

#include "fimg2d.h"

extern void enable_hlt(void);
extern void disable_hlt(void);
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
extern struct pm_qos_request exynos5_g2d_cpu_qos;
#endif

int fimg2d_clk_setup(struct fimg2d_control *ctrl);
void fimg2d_clk_release(struct fimg2d_control *ctrl);
void fimg2d_clk_on(struct fimg2d_control *ctrl);
void fimg2d_clk_off(struct fimg2d_control *ctrl);
void fimg2d_clk_save(struct fimg2d_control *ctrl);
void fimg2d_clk_restore(struct fimg2d_control *ctrl);

#endif /* __FIMG2D_CLK_H__ */
