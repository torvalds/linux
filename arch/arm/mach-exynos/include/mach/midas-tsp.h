/*
 * linux/arch/arm/mach-exynos/include/mach/midas-tsp.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MIDAS_TSP_H
#define __MIDAS_TSP_H __FILE__

#if defined(CONFIG_TOUCHSCREEN_MELFAS)
#include <linux/platform_data/mms_ts.h>
#else
#if defined(CONFIG_TOUCHSCREEN_MELFAS_NOTE)
#include <linux/platform_data/mms152_ts.h>
#else
#include <linux/melfas_ts.h>
#endif
#endif

extern int melfas_power(int on);
void melfas_set_touch_i2c(void);
void melfas_set_touch_i2c_to_gpio(void);
void midas_tsp_set_platdata(struct melfas_tsi_platform_data *pdata);
void midas_tsp_init(void);
int is_melfas_vdd_on(void);
int melfas_mux_fw_flash(bool to_gpios);
void midas_tsp_set_lcdtype(int lcd_type);

#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
extern void midas_tsp_request_qos(void *data);
#else
#define midas_tsp_request_qos	NULL
#endif

#endif /* __MIDAS_TSP_H */
