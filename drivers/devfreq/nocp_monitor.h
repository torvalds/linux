/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _NOCP_MONITOR_H
#define _NOCP_MONITOR_H

#include <plat/monitor.h>

#ifdef CONFIG_SAMSUNG_NOCP_MONITOR
void nocp_monitor_update_cnt(enum monitor_member id, unsigned long evt, unsigned long cycle);
void nocp_monitor_regist_list(struct list_head *target_list);
enum bw_monitor_authority nocp_monitor_get_authority(struct list_head *target_list);
#else
#define nocp_monitor_update_cnt(a, b, c) do {} while (0)
#define nocp_monitor_regist_list(a) do {} while (0)
#define nocp_monitor_get_authority(a) (BW_MON_ACCEPT)
#endif

#endif /* _NOCP_MONITOR_H */
