// SPDX-License-Identifier: GPL-2.0+
/*
 *  Pvpanic Device Support
 *
 *  Copyright (C) 2013 Fujitsu.
 *  Copyright (C) 2018 ZTE.
 *  Copyright (C) 2021 Oracle.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/panic_notifier.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/list.h>

#include <uapi/misc/pvpanic.h>

#include "pvpanic.h"

MODULE_AUTHOR("Mihai Carabas <mihai.carabas@oracle.com>");
MODULE_DESCRIPTION("pvpanic device driver ");
MODULE_LICENSE("GPL");

static struct list_head pvpanic_list;
static spinlock_t pvpanic_lock;

static void
pvpanic_send_event(unsigned int event)
{
	struct pvpanic_instance *pi_cur;

	if (!spin_trylock(&pvpanic_lock))
		return;

	list_for_each_entry(pi_cur, &pvpanic_list, list) {
		if (event & pi_cur->capability & pi_cur->events)
			iowrite8(event, pi_cur->base);
	}
	spin_unlock(&pvpanic_lock);
}

static int
pvpanic_panic_notify(struct notifier_block *nb, unsigned long code,
		     void *unused)
{
	unsigned int event = PVPANIC_PANICKED;

	if (kexec_crash_loaded())
		event = PVPANIC_CRASH_LOADED;

	pvpanic_send_event(event);

	return NOTIFY_DONE;
}

/*
 * Call our notifier very early on panic, deferring the
 * action taken to the hypervisor.
 */
static struct notifier_block pvpanic_panic_nb = {
	.notifier_call = pvpanic_panic_notify,
	.priority = INT_MAX,
};

static void pvpanic_remove(void *param)
{
	struct pvpanic_instance *pi_cur, *pi_next;
	struct pvpanic_instance *pi = param;

	spin_lock(&pvpanic_lock);
	list_for_each_entry_safe(pi_cur, pi_next, &pvpanic_list, list) {
		if (pi_cur == pi) {
			list_del(&pi_cur->list);
			break;
		}
	}
	spin_unlock(&pvpanic_lock);
}

int devm_pvpanic_probe(struct device *dev, struct pvpanic_instance *pi)
{
	if (!pi || !pi->base)
		return -EINVAL;

	spin_lock(&pvpanic_lock);
	list_add(&pi->list, &pvpanic_list);
	spin_unlock(&pvpanic_lock);

	dev_set_drvdata(dev, pi);

	return devm_add_action_or_reset(dev, pvpanic_remove, pi);
}
EXPORT_SYMBOL_GPL(devm_pvpanic_probe);

static int pvpanic_init(void)
{
	INIT_LIST_HEAD(&pvpanic_list);
	spin_lock_init(&pvpanic_lock);

	atomic_notifier_chain_register(&panic_notifier_list,
				       &pvpanic_panic_nb);

	return 0;
}

static void pvpanic_exit(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &pvpanic_panic_nb);

}

module_init(pvpanic_init);
module_exit(pvpanic_exit);
