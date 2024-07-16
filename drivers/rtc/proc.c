// SPDX-License-Identifier: GPL-2.0
/*
 * RTC subsystem, proc interface
 *
 * Copyright (C) 2005-06 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * based on arch/arm/common/rtctime.c
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rtc-core.h"

#define NAME_SIZE	10

#if defined(CONFIG_RTC_HCTOSYS_DEVICE)
static bool is_rtc_hctosys(struct rtc_device *rtc)
{
	int size;
	char name[NAME_SIZE];

	size = snprintf(name, NAME_SIZE, "rtc%d", rtc->id);
	if (size >= NAME_SIZE)
		return false;

	return !strncmp(name, CONFIG_RTC_HCTOSYS_DEVICE, NAME_SIZE);
}
#else
static bool is_rtc_hctosys(struct rtc_device *rtc)
{
	return (rtc->id == 0);
}
#endif

static int rtc_proc_show(struct seq_file *seq, void *offset)
{
	int err;
	struct rtc_device *rtc = seq->private;
	const struct rtc_class_ops *ops = rtc->ops;
	struct rtc_wkalrm alrm;
	struct rtc_time tm;

	err = rtc_read_time(rtc, &tm);
	if (err == 0) {
		seq_printf(seq,
			   "rtc_time\t: %ptRt\n"
			   "rtc_date\t: %ptRd\n",
			   &tm, &tm);
	}

	err = rtc_read_alarm(rtc, &alrm);
	if (err == 0) {
		seq_printf(seq, "alrm_time\t: %ptRt\n", &alrm.time);
		seq_printf(seq, "alrm_date\t: %ptRd\n", &alrm.time);
		seq_printf(seq, "alarm_IRQ\t: %s\n",
			   alrm.enabled ? "yes" : "no");
		seq_printf(seq, "alrm_pending\t: %s\n",
			   alrm.pending ? "yes" : "no");
		seq_printf(seq, "update IRQ enabled\t: %s\n",
			   (rtc->uie_rtctimer.enabled) ? "yes" : "no");
		seq_printf(seq, "periodic IRQ enabled\t: %s\n",
			   (rtc->pie_enabled) ? "yes" : "no");
		seq_printf(seq, "periodic IRQ frequency\t: %d\n",
			   rtc->irq_freq);
		seq_printf(seq, "max user IRQ frequency\t: %d\n",
			   rtc->max_user_freq);
	}

	seq_printf(seq, "24hr\t\t: yes\n");

	if (ops->proc)
		ops->proc(rtc->dev.parent, seq);

	return 0;
}

void rtc_proc_add_device(struct rtc_device *rtc)
{
	if (is_rtc_hctosys(rtc))
		proc_create_single_data("driver/rtc", 0, NULL, rtc_proc_show,
					rtc);
}

void rtc_proc_del_device(struct rtc_device *rtc)
{
	if (is_rtc_hctosys(rtc))
		remove_proc_entry("driver/rtc", NULL);
}
