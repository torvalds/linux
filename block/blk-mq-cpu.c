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
static DEFINE_SPINLOCK(blk_mq_cpu_notify_lock);

static int blk_mq_main_cpu_notify(struct notifier_block *self,
				  unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long) hcpu;
	struct blk_mq_cpu_notifier *notify;

	spin_lock(&blk_mq_cpu_notify_lock);

	list_for_each_entry(notify, &blk_mq_cpu_notify_list, list)
		notify->notify(notify->data, action, cpu);

	spin_unlock(&blk_mq_cpu_notify_lock);
	return NOTIFY_OK;
}

void blk_mq_register_cpu_notifier(struct blk_mq_cpu_notifier *notifier)
{
	BUG_ON(!notifier->notify);

	spin_lock(&blk_mq_cpu_notify_lock);
	list_add_tail(&notifier->list, &blk_mq_cpu_notify_list);
	spin_unlock(&blk_mq_cpu_notify_lock);
}

void blk_mq_unregister_cpu_notifier(struct blk_mq_cpu_notifier *notifier)
{
	spin_lock(&blk_mq_cpu_notify_lock);
	list_del(&notifier->list);
	spin_unlock(&blk_mq_cpu_notify_lock);
}

void blk_mq_init_cpu_notifier(struct blk_mq_cpu_notifier *notifier,
			      void (*fn)(void *, unsigned long, unsigned int),
			      void *data)
{
	notifier->notify = fn;
	notifier->data = data;
}

void __init blk_mq_cpu_init(void)
{
	hotcpu_notifier(blk_mq_main_cpu_notify, 0);
}
