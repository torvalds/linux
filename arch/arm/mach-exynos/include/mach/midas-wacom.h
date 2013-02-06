/*
 * linux/arch/arm/mach-exynos/include/mach/midas-wacom.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MIDAS_WACOM_H
#define __MIDAS_WACOM_H __FILE__

#include <linux/wacom_i2c.h>

void midas_wacom_init(void);

#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
extern void midas_wacom_request_qos(void *data);
#else
#define midas_wacom_request_qos	NULL
#endif

#endif /* __MIDAS_WACOM_H */
