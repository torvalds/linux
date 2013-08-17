/*
 * linux/drivers/media/video/exynos/mfc/s5p_mfc_pm.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __S5P_MFC_PM_H
#define __S5P_MFC_PM_H __FILE__

int s5p_mfc_init_pm(struct s5p_mfc_dev *dev);
void s5p_mfc_final_pm(struct s5p_mfc_dev *dev);

int s5p_mfc_clock_on(void);
void s5p_mfc_clock_off(void);
int s5p_mfc_power_on(void);
int s5p_mfc_power_off(void);
int s5p_mfc_get_clk_ref_cnt(void);

#ifdef CONFIG_MFC_USE_BUS_DEVFREQ
void s5p_mfc_qos_on(struct s5p_mfc_ctx *ctx);
void s5p_mfc_qos_off(struct s5p_mfc_ctx *ctx);
#else
#define s5p_mfc_qos_on(ctx)	do {} while (0)
#define s5p_mfc_qos_off(ctx)	do {} while (0)
#endif

#endif /* __S5P_MFC_PM_H */
