/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <mach/map.h>
#include "nocp_monitor.h"

#ifndef _NOC_PROBE_H
#define _NOC_PROBE_H

struct nocp_info {
	struct list_head list;
#ifdef CONFIG_SAMSUNG_NOCP_MONITOR
	struct list_head mon_list;
#endif
	const char *name;
	enum monitor_member id;
	unsigned long pa_base;
	void __iomem *va_base;
	unsigned int weight;
};

struct nocp_cnt {
	unsigned long total_byte_cnt;
	unsigned long cycle_cnt;
};

/* Register offset */
#define NOCP_CON		0x8
#define NOCP_ACT		0xC
#define NOCP_PERIOD		0x24
#define NOCP_STATGO		0x28
#define NOCP_ALARM_MIN		0x2C
#define NOCP_ALARM_MAX		0x30
#define NOCP_ALARM_STAT		0x34
#define NOCP_ALARM_CLR		0x38
#define NOCP_INT_0_EVENT	0x138
#define NOCP_CNT_0_ALARM_MODE	0x13C
#define NOCP_CNT_0_VAL		0x140
#define NOCP_INT_1_EVENT	0x14C
#define NOCP_CNT_1_ALARM_MODE	0x150
#define NOCP_CNT_1_VAL		0x154
#define NOCP_INT_2_EVENT	0x160
#define NOCP_CNT_2_ALARM_MODE	0x164
#define NOCP_CNT_2_VAL		0x168
#define NOCP_INT_3_EVENT	0x174
#define NOCP_CNT_3_ALARM_MODE	0x178
#define NOCP_CNT_3_VAL		0x17C

/* Register for NOCP_INT */
#define NOCP_EVT_OFFSET		0x1F
#define NOCP_EVT_CYCLE		0x1
#define NOCP_EVT_BUSY_CYCLE	0x4
#define NOCP_EVT_BYTE		0x8
#define NOCP_EVT_CHAIN		0x10

/* Register for NOCP_CNT_ALARM_MODE */
#define NOCP_ALARM_MODE_OFFSET	0x3
#define NOCP_ALARM_MODE_OFF	0x0
#define NOCP_ALARM_MODE_MIN	0x1
#define NOCP_ALARM_MODE_MAX	0x2
#define NOCP_ALARM_MODE_MIN_MAX	0x3

/* Register for NOCP_CON */
#define NOCP_CON_STAT_EN	(1 << 3)
#define NOCP_CON_ALARM_EN	(1 << 4)

/* Register for NOCP_ACT */
#define NOCP_ACT_GLOBAL_EN	(1 << 0)
#define NOCP_ACT_ACTIVE_EN	(1 << 1)

/* Register for NOCP_STATGO */
#define NOCP_STATGO_EN		(1 << 0)

/* Register for NOCP_ALARM_STAT */
#define NOCP_ALARM_STAT_MASK	0x1

/* Define API for nocp */
extern unsigned int regist_nocp(struct list_head *target_list,
			struct nocp_info **nocp_info,
			unsigned int nr_nocp, enum nocp_usage usage);

extern unsigned int exit_nocp(struct list_head *target_list);
extern unsigned int nocp_get_aver_cnt(struct list_head *target_list, struct nocp_cnt *nocp_cnt);
extern unsigned int resume_nocp(struct list_head *target_list);
extern void start_nocp(struct nocp_info *nocp);
extern void stop_nocp(struct nocp_info *nocp);
extern void get_cnt_nocp(struct nocp_info *nocp, unsigned int *cnt_val0,
	unsigned int *cnt_val1, unsigned int *cnt_val2, unsigned int *cnt_val3);
extern void set_env_nocp(struct nocp_info *nocp);

#endif /* _NOC_PROBE_H */
