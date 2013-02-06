/*
 *  arch/arm/mach-exynos/include/mach/p4-input.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __P4_INPUT_H
#define __P4_INPUT_H __FILE__

void p4_tsp_init(u32 system_rev);
void p4_wacom_init(void);
void p4_key_init(void);
#if defined(CONFIG_TOUCHSCREEN_SYNAPTICS_S7301)
extern void synaptics_ts_charger_infom(bool en);
#endif

#if defined(CONFIG_TOUCHSCREEN_ATMEL_MXT1664S)
extern void ts_charger_infom(bool en);
#endif

#endif /* __P4_INPUT_H */
