/*
 * RTC subsystem, proc interface
 *
 * Copyright (C) 2005-06 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * based on arch/arm/common/rtctime.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct class_device *rtc_dev = NULL;
static DEFINE_MUTEX(rtc_lock);

static int rtc_proc_show(struct seq_file *seq, void *offset)
{
	int err;
	struct class_device *class_dev = seq->private;
	const struct rtc_class_ops *ops = to_rtc_device(class_dev)->ops;
	struct rtc_wkalrm alrm;
	struct rtc_time tm;

	err = rtc_read_time(class_dev, &tm);
	if (err == 0) {
		seq_printf(seq,
			"rtc_time\t: %02d:%02d:%02d\n"
			"rtc_date\t: %04d-%02d-%02d\n",
			tm.tm_hour, tm.tm_min, tm.tm_sec,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
	}

	err = rtc_read_alarm(class_dev, &alrm);
	if (err == 0) {
		seq_printf(seq, "alrm_time\t: ");
		if ((unsigned int)alrm.time.tm_hour <= 24)
			seq_printf(seq, "%02d:", alrm.time.tm_hour);
		else
			seq_printf(seq, "**:");
		if ((unsigned int)alrm.time.tm_min <= 59)
			seq_printf(seq, "%02d:", alrm.time.tm_min);
		else
			seq_printf(seq, "**:");
		if ((unsigned int)alrm.time.tm_sec <= 59)
			seq_printf(seq, "%02d\n", alrm.time.tm_sec);
		else
			seq_printf(seq, "**\n");

		seq_printf(seq, "alrm_date\t: ");
		if ((unsigned int)alrm.time.tm_year <= 200)
			seq_printf(seq, "%04d-", alrm.time.tm_year + 1900);
		else
			seq_printf(seq, "****-");
		if ((unsigned int)alrm.time.tm_mon <= 11)
			seq_printf(seq, "%02d-", alrm.time.tm_mon + 1);
		else
			seq_printf(seq, "**-");
		if (alrm.time.tm_mday && (unsigned int)alrm.time.tm_mday <= 31)
			seq_printf(seq, "%02d\n", alrm.time.tm_mday);
		else
			seq_printf(seq, "**\n");
		seq_printf(seq, "alarm_IRQ\t: %s\n",
				alrm.enabled ? "yes" : "no");
		seq_printf(seq, "alrm_pending\t: %s\n",
				alrm.pending ? "yes" : "no");
	}

	seq_printf(seq, "24hr\t\t: yes\n");

	if (ops->proc)
		ops->proc(class_dev->dev, seq);

	return 0;
}

static int rtc_proc_open(struct inode *inode, struct file *file)
{
	struct class_device *class_dev = PDE(inode)->data;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	return single_open(file, rtc_proc_show, class_dev);
}

static int rtc_proc_release(struct inode *inode, struct file *file)
{
	int res = single_release(inode, file);
	module_put(THIS_MODULE);
	return res;
}

static struct file_operations rtc_proc_fops = {
	.open		= rtc_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= rtc_proc_release,
};

static int rtc_proc_add_device(struct class_device *class_dev,
					struct class_interface *class_intf)
{
	mutex_lock(&rtc_lock);
	if (rtc_dev == NULL) {
		struct proc_dir_entry *ent;

		rtc_dev = class_dev;

		ent = create_proc_entry("driver/rtc", 0, NULL);
		if (ent) {
			struct rtc_device *rtc = to_rtc_device(class_dev);

			ent->proc_fops = &rtc_proc_fops;
			ent->owner = rtc->owner;
			ent->data = class_dev;

			dev_dbg(class_dev->dev, "rtc intf: proc\n");
		}
		else
			rtc_dev = NULL;
	}
	mutex_unlock(&rtc_lock);

	return 0;
}

static void rtc_proc_remove_device(struct class_device *class_dev,
					struct class_interface *class_intf)
{
	mutex_lock(&rtc_lock);
	if (rtc_dev == class_dev) {
		remove_proc_entry("driver/rtc", NULL);
		rtc_dev = NULL;
	}
	mutex_unlock(&rtc_lock);
}

static struct class_interface rtc_proc_interface = {
	.add = &rtc_proc_add_device,
	.remove = &rtc_proc_remove_device,
};

static int __init rtc_proc_init(void)
{
	return rtc_interface_register(&rtc_proc_interface);
}

static void __exit rtc_proc_exit(void)
{
	class_interface_unregister(&rtc_proc_interface);
}

subsys_initcall(rtc_proc_init);
module_exit(rtc_proc_exit);

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("RTC class proc interface");
MODULE_LICENSE("GPL");
