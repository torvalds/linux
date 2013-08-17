/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BW_MONITOR_H
#define _BW_MONITOR_H

#include <linux/ktime.h>
#include <linux/spinlock.h>
#include <linux/kobject.h>

enum monitor_member {
	MFC0,
	MFC1,
	GSC23,
	S3D0,
	S3D1,
	ISP0,
	ISP1,
	TRP_EAGLE,
	TRP_KFC,
	TRP_CCI,
	TRP_G2D,
	G3D0,
	G3D1,
	GEN,
	FSYS,
	MEM0_0,
	MEM0_1,
	MEM1_0,
	MEM1_1,
	CCI,
	C2C,
	GSC0,
	GSC1,
	DISP0,
	DISP1,
	BW_MON_LIST
};

enum nocp_usage {
	NOCP_USAGE_MIF,
	NOCP_USAGE_INT,
	NOCP_USAGE_MONITOR,
	NOCP_NO_USAGE,
};

enum bw_monitor_counter {
	BW_MON_DATA_EVENT,
	BW_MON_CYCLE_CNT,
	MAX_BW_MON_CNT
};

enum bw_monitor_mode {
	BW_MON_OFF,
	BW_MON_BUSFREQ,
	BW_MON_STANDALONE,
	BW_MON_USERCTRL,
};

enum bw_monitor_log {
	BW_MON_LOG_EVENT,
	BW_MON_LOG_CYCLE,
	BW_MON_LOG_BW,
};

enum bw_monitor_authority {
	BW_MON_REJECT,
	BW_MON_ACCEPT,
};

enum bw_monitor_ip {
	BW_MON_NOCP,
	BW_MON_PPMU,
	BW_MON_IP_MAX,
};

struct bw_monitor_member {
	unsigned long cnt[MAX_BW_MON_CNT];
	unsigned long long ns;
	ktime_t reset_time;
	const char *name;
};

struct bw_monitor_t {
	struct bw_monitor_member member[BW_MON_LIST];
	enum bw_monitor_mode mode;
	enum bw_monitor_log log;
	enum bw_monitor_ip monitor_ip;
	spinlock_t bw_mon_lock;
	void (*start)(void);
	void (*get_cnt)(unsigned long *monitor_cnt, unsigned long *us);
	void (*monitor)(unsigned long *monitor_cnt);
};

#ifdef CONFIG_SAMSUNG_BW_MONITOR
void register_bw_monitor(struct bw_monitor_t *monitor);
void bw_monitor_config(enum bw_monitor_mode mode, enum bw_monitor_log log);
void bw_monitor_get_cnt(unsigned long *monitor_cnt, unsigned long *us);
void bw_monitor(unsigned long *monitor_cnt);
void bw_monitor_create_sysfs(struct kobject *kobj);
#else
#define register_bw_monitor(a) do {} while (0)
#define bw_monitor_config(a, b) do {} while (0)
#define bw_monitor_get_cnt(a, b) do {} while (0)
#define bw_monitor(a) do {} while (0)
#define bw_monitor_create_sysfs(a) do {} while (0)
#endif

#endif /* _BW_MONITOR_H */
