/*
 * CPU notifier helper code for blk-mq
 *
 * Copyright (C) 2013-2014 Jens Axboe
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/list.h>
#include <linux/llist.h>
#include <linux/smp.h>
#include <linux/cpu.h>

#include <linux/blk-mq.h>
#include "blk-mq.h"

static LIST_HEAD(blk_mq_cpu_notify_list);
static DEFINE_RAW_SPINLOCK(blk_mq_cpu_notify_lock);

static int blk_mq_main_cpu_notify(struct notifier_block *self,
				  unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long) hcpu;
	struct blk_mq_cpu_notifier *notify;
	int ret = NOTIFY_OK;

	raw_spin_lock(&blk_mq_cpu_notify_lock);

	list_for_each_entry(notify, &blk_mq_cpu_notify_list, list) {
		ret = notify->notify(notify->data, action, cpu);
		if (ret != NOTIFY_OK)
			break;
	}

	raw_spin_unlock(&blk_mq_cpu_notify_lock);
	return ret;
}

void blk_mq_register_cpu_notifier(struct blk_mq_cpu_notifier *notifier)
{
	BUG_ON(!notifier->notify);

	raw_spin_lock(&blk_mq_cpu_notify_lock);
	list_add_tail(&notifier->list, &blk_mq_cpu_notify_list);
	raw_spin_unlock(&blk_mq_cpu_notify_lock);
}

void blk_mq_unregister_cpu_notifier(struct blk_mq_cpu_notifier *notifier)
{
	raw_spin_lock(&blk_mq_cpu_notify_lock);
	list_del(&notifier->list);
	raw_spin_unlock(&blk_mq_cpu_notify_lock);
}

void blk_mq_init_cpu_notifier(struct blk_mq_cpu_notifier *notifier,
			      int (*fn)(void *, unsigned long, unsigned int),
			      void *data)
{
	notifier->notify = fn;
	notifier->data = data;
}

void __init blk_mq_cpu_init(void)
{
	hotcpu_notifier(blk_mq_main_cpu_notify, 0);
}
