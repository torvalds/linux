/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/io.h>
#include <linux/list.h>
#include <linux/kernel.h>

#include <mach/map.h>

#include "noc_probe.h"
#include "nocp_monitor.h"

void start_nocp(struct nocp_info *tmp_nocp)
{
	unsigned int tmp;

	tmp = __raw_readl(tmp_nocp->va_base + NOCP_CON);
	tmp |= (1 << 3);
	__raw_writel(tmp, tmp_nocp->va_base + NOCP_CON);
}

void stop_nocp(struct nocp_info *tmp_nocp)
{
	unsigned int tmp;

	tmp = __raw_readl(tmp_nocp->va_base + NOCP_CON);
	tmp &= ~(1 << 3);
	__raw_writel(tmp, tmp_nocp->va_base + NOCP_CON);
}

void get_cnt_nocp(struct nocp_info *tmp_nocp, unsigned int *cnt_val0,
	unsigned int *cnt_val1, unsigned int *cnt_val2, unsigned int *cnt_val3)
{
	*cnt_val0 = __raw_readl(tmp_nocp->va_base + NOCP_CNT_0_VAL);
	*cnt_val1 = __raw_readl(tmp_nocp->va_base + NOCP_CNT_1_VAL);
	*cnt_val2 = __raw_readl(tmp_nocp->va_base + NOCP_CNT_2_VAL);
	*cnt_val3 = __raw_readl(tmp_nocp->va_base + NOCP_CNT_3_VAL);
}

void set_env_nocp(struct nocp_info *tmp_nocp)
{
	unsigned int tmp;

	/* Set event for each counter */
	__raw_writel((NOCP_EVT_BYTE & NOCP_EVT_OFFSET),
			tmp_nocp->va_base + NOCP_INT_0_EVENT);
	__raw_writel((NOCP_EVT_CHAIN & NOCP_EVT_OFFSET),
			tmp_nocp->va_base + NOCP_INT_1_EVENT);
	__raw_writel((NOCP_EVT_CYCLE & NOCP_EVT_OFFSET),
			tmp_nocp->va_base + NOCP_INT_2_EVENT);
	__raw_writel((NOCP_EVT_CHAIN & NOCP_EVT_OFFSET),
			tmp_nocp->va_base + NOCP_INT_3_EVENT);

	/* Set ALARM mode for each counter */
	__raw_writel(NOCP_ALARM_MODE_MIN_MAX,
			tmp_nocp->va_base + NOCP_CNT_0_ALARM_MODE);
	__raw_writel(NOCP_ALARM_MODE_MIN_MAX,
			tmp_nocp->va_base + NOCP_CNT_1_ALARM_MODE);
	__raw_writel(NOCP_ALARM_MODE_MIN_MAX,
			tmp_nocp->va_base + NOCP_CNT_2_ALARM_MODE);
	__raw_writel(NOCP_ALARM_MODE_MIN_MAX,
			tmp_nocp->va_base + NOCP_CNT_3_ALARM_MODE);

	/* STAT PERIOD SET with 0 */
	__raw_writel(0x0, tmp_nocp->va_base + NOCP_PERIOD);

	/* ALARM MIN/MAX SET with 0 */
	__raw_writel(0x0, tmp_nocp->va_base + NOCP_ALARM_MIN);
	__raw_writel(0x0, tmp_nocp->va_base + NOCP_ALARM_MAX);

	tmp = __raw_readl(tmp_nocp->va_base + NOCP_CON);
	tmp &= ~NOCP_CON_STAT_EN;
	tmp |= NOCP_CON_ALARM_EN;
	__raw_writel(tmp, tmp_nocp->va_base + NOCP_CON);

	/* Set GlobalEn is Set */
	tmp = __raw_readl(tmp_nocp->va_base + NOCP_ACT);
	tmp |= NOCP_ACT_GLOBAL_EN;
	__raw_writel(tmp, tmp_nocp->va_base + NOCP_ACT);
}

static unsigned int start_nocp_list(struct list_head *target_list)
{
	struct nocp_info *tmp_nocp;

	list_for_each_entry(tmp_nocp, target_list, list)
		start_nocp(tmp_nocp);

	return 0;
}

static unsigned int stop_nocp_list(struct list_head *target_list)
{
	struct nocp_info *tmp_nocp;

	list_for_each_entry(tmp_nocp, target_list, list)
		stop_nocp(tmp_nocp);

	return 0;
}

static struct nocp_cnt *check_get_cnt_done(struct list_head *target_list, struct nocp_cnt *nocp_cnt)
{
	struct nocp_info *tmp_nocp;
	unsigned int cnt_val0;
	unsigned int cnt_val1;
	unsigned int cnt_val2;
	unsigned int cnt_val3;
	unsigned int list_cnt = 0;
	unsigned int max_cycle_cnt = 0;

	nocp_cnt->total_byte_cnt = 0;
	nocp_cnt->cycle_cnt = 0;

	list_for_each_entry(tmp_nocp, target_list, list) {
		list_cnt++;

		get_cnt_nocp(tmp_nocp, &cnt_val0, &cnt_val1, &cnt_val2, &cnt_val3);

		if (tmp_nocp->weight)
			nocp_cnt->total_byte_cnt +=
				((cnt_val1 << 16) | cnt_val0) * tmp_nocp->weight;
		else
			nocp_cnt->total_byte_cnt += ((cnt_val1 << 16) | cnt_val0);

		nocp_cnt->cycle_cnt = ((cnt_val3 << 16) | cnt_val2);

		nocp_monitor_update_cnt(tmp_nocp->id,
			((cnt_val1 << 16) | cnt_val0), nocp_cnt->cycle_cnt);

		if (nocp_cnt->cycle_cnt < max_cycle_cnt)
			nocp_cnt->cycle_cnt = max_cycle_cnt;
		else
			max_cycle_cnt = nocp_cnt->cycle_cnt;
	}

	if (!nocp_cnt->total_byte_cnt && !nocp_cnt->cycle_cnt)
		return nocp_cnt;

	return nocp_cnt;
}

static unsigned int set_nocp_packet_evt(struct list_head *target_list)
{
	struct nocp_info *tmp_nocp;

	list_for_each_entry(tmp_nocp, target_list, list)
		set_env_nocp(tmp_nocp);

	return 0;
}

unsigned int nocp_get_aver_cnt(struct list_head *target_list, struct nocp_cnt *nocp_cnt)
{
	if (nocp_monitor_get_authority(target_list) == BW_MON_ACCEPT) {
		stop_nocp_list(target_list);
		check_get_cnt_done(target_list, nocp_cnt);
		start_nocp_list(target_list);
	}
	return 0;
}

unsigned int regist_nocp(struct list_head *target_list,
		 struct nocp_info **nocp_info,
		 unsigned int nr_nocp, enum nocp_usage usage)
{
	struct nocp_info *tmp_nocp = NULL;

	for (; nr_nocp > 0; nr_nocp--, nocp_info++) {
		tmp_nocp = *nocp_info;
		tmp_nocp->va_base = ioremap(tmp_nocp->pa_base, SZ_512);
		list_add(&tmp_nocp->list, target_list);
	}

	if (usage != NOCP_USAGE_MONITOR) {
		/* Setting event for counter */
		set_nocp_packet_evt(target_list);

		/* Start nocp */
		start_nocp_list(target_list);
		nocp_monitor_regist_list(target_list);
	}
	return 0;
}

unsigned int resume_nocp(struct list_head *target_list)
{
	/* Setting event for counter */
	set_nocp_packet_evt(target_list);

	/* Start nocp */
	start_nocp_list(target_list);

	return 0;
}

unsigned int exit_nocp(struct list_head *target_list)
{
	struct nocp_info *tmp_nocp;

	list_for_each_entry(tmp_nocp, target_list, list)
		iounmap(tmp_nocp->va_base);

	return 0;
}
