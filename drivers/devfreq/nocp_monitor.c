/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/hrtimer.h>
#include <linux/kobject.h>

#include "noc_probe.h"
#include "nocp_monitor.h"

static inline struct bw_monitor_member *get_member(enum monitor_member id);

static LIST_HEAD(monitor_list);

/* start nocp counter */
static void nocp_monitor_start(void)
{
	struct nocp_info *tmp_nocp;

	list_for_each_entry(tmp_nocp, &monitor_list, mon_list) {
		stop_nocp(tmp_nocp);
		set_env_nocp(tmp_nocp);
		start_nocp(tmp_nocp);
	}
}

/* get nocp counter */
static void nocp_monitor_get_cnt(unsigned long *monitor_cnt, unsigned long *us)
{
	struct nocp_info *tmp_nocp;
	struct bw_monitor_member *member;

	list_for_each_entry(tmp_nocp, &monitor_list, mon_list) {
		member = get_member(tmp_nocp->id);
		monitor_cnt[tmp_nocp->id] =
			member->cnt[BW_MON_DATA_EVENT];
		us[tmp_nocp->id] = div64_u64(member->ns, 1000);
	}
}

/* monitor noc probe and get counter */
static void nocp_monitor(unsigned long *monitor_cnt)
{
	struct nocp_info *tmp_nocp;
	unsigned int val0, val1, val2, val3;
	struct bw_monitor_member *member;

	if (nocp_monitor_get_authority(&monitor_list) == BW_MON_ACCEPT) {
		list_for_each_entry(tmp_nocp, &monitor_list, mon_list) {
			member = get_member(tmp_nocp->id);
			stop_nocp(tmp_nocp);
			get_cnt_nocp(tmp_nocp, &val0, &val1, &val2, &val3);
			member->cnt[BW_MON_DATA_EVENT] = ((val1 << 16) | val0);
			member->cnt[BW_MON_CYCLE_CNT] = ((val3 << 16) | val2);
			if (monitor_cnt)
				monitor_cnt[tmp_nocp->id] =
					member->cnt[BW_MON_DATA_EVENT];
			start_nocp(tmp_nocp);
		}
	}
}

static struct bw_monitor_t nocp_mon = {
	.start = nocp_monitor_start,
	.get_cnt = nocp_monitor_get_cnt,
	.monitor = nocp_monitor,
	.monitor_ip = BW_MON_NOCP,
};

static inline struct bw_monitor_member *get_member(enum monitor_member id)
{
	return &nocp_mon.member[id];
}

/* get monitor authority. it decides based on monitor mode. */
enum bw_monitor_authority nocp_monitor_get_authority(struct list_head *target_list)
{
	struct nocp_info *tmp_nocp;
	enum bw_monitor_authority authority = BW_MON_ACCEPT;
	ktime_t reset_time, read_time, t;
	unsigned long long ns = 0;
	struct bw_monitor_member *member;

	if ((nocp_mon.mode == BW_MON_STANDALONE) ||
		(nocp_mon.mode == BW_MON_USERCTRL)) {
		if (target_list != &monitor_list)
			authority = BW_MON_REJECT;

		list_for_each_entry(tmp_nocp, target_list, mon_list) {
			member = get_member(tmp_nocp->id);
			if (!ns) {
				read_time = ktime_get();
				t = ktime_sub(read_time, member->reset_time);
				member->ns = ktime_to_ns(t);
				member->reset_time = ktime_get();
				ns =  member->ns;
				reset_time =   member->reset_time;
			} else {
				member->ns = ns;
				member->reset_time = reset_time;
			}
		}
	}

	return authority;
}

/* nocp updates its counter */
void nocp_monitor_update_cnt(enum monitor_member id,
		unsigned long evt, unsigned long cycle)
{
	ktime_t read_time, t;
	struct bw_monitor_member *member;

	if (nocp_mon.mode == BW_MON_BUSFREQ) {
		member = get_member(id);
		spin_lock(&nocp_mon.bw_mon_lock);
		read_time = ktime_get();
		t = ktime_sub(read_time, member->reset_time);
		member->ns = ktime_to_ns(t);
		member->reset_time = ktime_get();

		member->cnt[BW_MON_DATA_EVENT] = evt;
		member->cnt[BW_MON_CYCLE_CNT] = cycle;
		spin_unlock(&nocp_mon.bw_mon_lock);
	}
}

/* register noc probes */
void nocp_monitor_regist_list(struct list_head *target_list)
{
	struct nocp_info *tmp_nocp;
	struct bw_monitor_member *member;

	list_for_each_entry(tmp_nocp, target_list, list) {
		list_add(&tmp_nocp->mon_list, &monitor_list);
		member = get_member(tmp_nocp->id);
		member->name = tmp_nocp->name;
	}
}

/* nocp monitor init and register it into bandwidth monitor */
static int __init nocp_monitor_init(void)
{
	register_bw_monitor(&nocp_mon);

	return 0;
}
late_initcall(nocp_monitor_init);
