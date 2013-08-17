/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/debugfs.h>

#include <plat/monitor.h>

#define MONITOR_DURATION 100
#define FIXED_POINT_OFFSET 8
#define FIXED_POINT_MASK ((1 << FIXED_POINT_OFFSET) - 1)

static struct bw_monitor_t *bw_mon[BW_MON_IP_MAX];
static enum bw_monitor_ip curr_ip;

static void bw_monitor_timer_fn(unsigned long dummy);
static DEFINE_TIMER(bw_monitor_timer, bw_monitor_timer_fn, 0, 0);

static struct completion bw_monitor_comp;

static struct bw_monitor_t *get_bw_monitor(void)
{
	return bw_mon[curr_ip];
}

/* register a bw monitor */
void register_bw_monitor(struct bw_monitor_t *mon)
{
	mon->log = BW_MON_LOG_BW;
	mon->mode = BW_MON_OFF;
	curr_ip = mon->monitor_ip;
	bw_mon[mon->monitor_ip] = mon;
}

/* configure monitor */
void bw_monitor_config(enum bw_monitor_mode mode, enum bw_monitor_log log)
{
	int i;
	struct bw_monitor_t *mon = get_bw_monitor();

	mon->mode = mode;
	mon->log = log;

	if (mode == BW_MON_OFF) {
		for (i = 0; i < BW_MON_LIST; i++) {
			mon->member[i].cnt[BW_MON_DATA_EVENT] = 0;
			mon->member[i].cnt[BW_MON_CYCLE_CNT] = 0;
		}
	} else {
		mon->start();
	}

	if (mode == BW_MON_STANDALONE)
		mod_timer(&bw_monitor_timer, jiffies + msecs_to_jiffies(MONITOR_DURATION));
}
EXPORT_SYMBOL(bw_monitor_config);

/* get monitored count */
void bw_monitor_get_cnt(unsigned long *monitor_cnt, unsigned long *us)
{
	struct bw_monitor_t *mon;

	if (monitor_cnt) {
		mon = get_bw_monitor();
		spin_lock(&mon->bw_mon_lock);
		mon->get_cnt(monitor_cnt, us);
		spin_unlock(&mon->bw_mon_lock);
	}
}
EXPORT_SYMBOL(bw_monitor_get_cnt);

/* configure monitor and get monitored count */
void bw_monitor(unsigned long *monitor_cnt)
{
	struct bw_monitor_t *mon = get_bw_monitor();

	mon->monitor(monitor_cnt);
}
EXPORT_SYMBOL(bw_monitor);

/* bandwidth monitoring timer */
static void bw_monitor_timer_fn(unsigned long dummy)
{
	struct bw_monitor_t *mon = get_bw_monitor();

	if (mon->mode == BW_MON_STANDALONE) {
		bw_monitor(NULL);
		mod_timer(&bw_monitor_timer,
				jiffies + msecs_to_jiffies(MONITOR_DURATION));
		complete(&bw_monitor_comp);
	}
}

/* calculate the system bandwidth based on data and cycle count */
static void bw_monitor_bw_calculation(int index,
	u32 *sat, u32 *freq, u32 *bw)
{
	struct bw_monitor_t *mon = get_bw_monitor();

	u64 ns = mon->member[index].ns;
	u64 busy = mon->member[index].cnt[BW_MON_DATA_EVENT];
	u32 total = mon->member[index].cnt[BW_MON_CYCLE_CNT];
	u64 s;
	u64 f;
	u64 b;

	if (mon->monitor_ip == BW_MON_NOCP)
		busy = busy * 8 / 128;

	s = (u64)busy * 100 * (1 << FIXED_POINT_OFFSET);
	s += total / 2;
	do_div(s, total);

	f = (u64)total * 1000 * (1 << FIXED_POINT_OFFSET);
	f += ns / 2;
	f = div64_u64(f, ns);

	b = (u64)busy * (128 / 8) * 1000 * (1 << FIXED_POINT_OFFSET);
	b += ns / 2;
	b = div64_u64(b, ns);

	*sat = s;
	*freq = f;
	*bw = b;
}

static int bw_monitor_open_show(struct seq_file *s, void *d)
{
	struct bw_monitor_t *mon = get_bw_monitor();
	u32 sat;
	u32 freq;
	u32 bw;
	int i;

	if (mon->mode == BW_MON_OFF) {
		seq_printf(s, "monitor off\n");
		return 0;
	}

	spin_lock(&mon->bw_mon_lock);
	switch (mon->log) {
	case BW_MON_LOG_CYCLE:
		seq_printf(s, "us %lld (cyc): ", div64_u64(mon->member[0].ns, 1000));
		for (i = 0; i < BW_MON_LIST; i++)
			seq_printf(s, "%-3ld ",
				mon->member[i].cnt[BW_MON_CYCLE_CNT]);
		seq_printf(s, "\n");
	case BW_MON_LOG_EVENT:
		seq_printf(s, "us %lld (evt): ",
			div64_u64(mon->member[0].ns, 1000));
		for (i = 0; i < BW_MON_LIST; i++)
			seq_printf(s, "%-8ld ",
				mon->member[i].cnt[BW_MON_DATA_EVENT]);
		seq_printf(s, "\n");
		break;
	case BW_MON_LOG_BW:
		for (i = 0; i < BW_MON_LIST; i++) {
			if (mon->member[i].name) {
				bw_monitor_bw_calculation(i, &sat, &freq, &bw);
				seq_printf(s, "%-10s %4u.%02u MBps %3u.%02u MHz %2u.%02u%% during %lld us\n",
					mon->member[i].name,
					bw >> FIXED_POINT_OFFSET,
					(bw & FIXED_POINT_MASK) * 100 / (1 << FIXED_POINT_OFFSET),
					freq >> FIXED_POINT_OFFSET,
					(freq & FIXED_POINT_MASK) * 100 / (1 << FIXED_POINT_OFFSET),
					sat >> FIXED_POINT_OFFSET,
					(sat & FIXED_POINT_MASK) * 100 / (1 << FIXED_POINT_OFFSET),
					div64_u64(mon->member[i].ns, 1000));
			}
		}
		break;
	default:
		break;
	}
	spin_unlock(&mon->bw_mon_lock);

	if (mon->mode == BW_MON_STANDALONE)
		wait_for_completion_timeout(&bw_monitor_comp, msecs_to_jiffies(1000));

	return 0;
}

static ssize_t store_bw_mon(struct device *device,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int mode, log;
	char *_mode;
	char *_log;

	sscanf(buf, "%d %d", &mode, &log);
	bw_monitor_config((enum bw_monitor_mode)mode, (enum bw_monitor_log)log);

	_mode = (mode == BW_MON_STANDALONE) ? "standalone" :
		((mode == BW_MON_USERCTRL) ? "userctrl" :
		((mode == BW_MON_OFF) ? "off" : "busfreq"));

	_log = (log == BW_MON_LOG_EVENT) ? "event" :
		((log == BW_MON_LOG_CYCLE) ? "cycle" : "bw");

	pr_info("bw monitor mode: %s, log: %s\n", _mode, _log);

	return count;
}

static DEVICE_ATTR(bw_mon, 0666, NULL, store_bw_mon);

static struct attribute *bw_mon_attri[] = {
	&dev_attr_bw_mon.attr,
	NULL,
};

static struct attribute_group bw_mon_attr_group = {
	.name = "bw_monitor",
	.attrs = bw_mon_attri,
};

void bw_monitor_create_sysfs(struct kobject *kobj)
{
	if (sysfs_create_group(kobj, &bw_mon_attr_group))
		pr_err("Failed to create attributes group.!\n");
}

static int  bw_monitor_open(struct inode *inode, struct file *file)
{
	return single_open(file,  bw_monitor_open_show, inode->i_private);
}

static const struct file_operations bw_monitor_fops = {
	.open		=  bw_monitor_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init bw_monitor_init(void)
{
	debugfs_create_file("bw_monitor", S_IRUGO,
			NULL, NULL, &bw_monitor_fops);

	init_completion(&bw_monitor_comp);

	return 0;
}
late_initcall(bw_monitor_init);
