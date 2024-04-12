// SPDX-License-Identifier: GPL-2.0+
/*
 *  Pvpanic Device Support
 *
 *  Copyright (C) 2013 Fujitsu.
 *  Copyright (C) 2018 ZTE.
 *  Copyright (C) 2021 Oracle.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/io.h>
#include <linux/kexec.h>
#include <linux/kstrtox.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/panic_notifier.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include <uapi/misc/pvpanic.h>

#include "pvpanic.h"

MODULE_AUTHOR("Mihai Carabas <mihai.carabas@oracle.com>");
MODULE_DESCRIPTION("pvpanic device driver");
MODULE_LICENSE("GPL");

struct pvpanic_instance {
	void __iomem *base;
	unsigned int capability;
	unsigned int events;
	struct sys_off_handler *sys_off;
	struct list_head list;
};

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
pvpanic_panic_notify(struct notifier_block *nb, unsigned long code, void *unused)
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

static int pvpanic_sys_off(struct sys_off_data *data)
{
	pvpanic_send_event(PVPANIC_SHUTDOWN);

	return NOTIFY_DONE;
}

static void pvpanic_synchronize_sys_off_handler(struct device *dev, struct pvpanic_instance *pi)
{
	/* The kernel core has logic to fall back to system halt if no
	 * sys_off_handler is registered.
	 * When the pvpanic sys_off_handler is disabled via sysfs the kernel
	 * should use that fallback logic, so the handler needs to be unregistered.
	 */

	struct sys_off_handler *sys_off;

	if (!(pi->events & PVPANIC_SHUTDOWN) == !pi->sys_off)
		return;

	if (!pi->sys_off) {
		sys_off = register_sys_off_handler(SYS_OFF_MODE_POWER_OFF, SYS_OFF_PRIO_LOW,
						   pvpanic_sys_off, NULL);
		if (IS_ERR(sys_off))
			dev_warn(dev, "Could not register sys_off_handler: %pe\n", sys_off);
		else
			pi->sys_off = sys_off;
	} else {
		unregister_sys_off_handler(pi->sys_off);
		pi->sys_off = NULL;
	}
}

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

	unregister_sys_off_handler(pi->sys_off);
}

static ssize_t capability_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pvpanic_instance *pi = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%x\n", pi->capability);
}
static DEVICE_ATTR_RO(capability);

static ssize_t events_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pvpanic_instance *pi = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%x\n", pi->events);
}

static ssize_t events_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct pvpanic_instance *pi = dev_get_drvdata(dev);
	unsigned int tmp;
	int err;

	err = kstrtouint(buf, 16, &tmp);
	if (err)
		return err;

	if ((tmp & pi->capability) != tmp)
		return -EINVAL;

	pi->events = tmp;
	pvpanic_synchronize_sys_off_handler(dev, pi);

	return count;
}
static DEVICE_ATTR_RW(events);

static struct attribute *pvpanic_dev_attrs[] = {
	&dev_attr_capability.attr,
	&dev_attr_events.attr,
	NULL
};

static const struct attribute_group pvpanic_dev_group = {
	.attrs = pvpanic_dev_attrs,
};

const struct attribute_group *pvpanic_dev_groups[] = {
	&pvpanic_dev_group,
	NULL
};
EXPORT_SYMBOL_GPL(pvpanic_dev_groups);

int devm_pvpanic_probe(struct device *dev, void __iomem *base)
{
	struct pvpanic_instance *pi;

	if (!base)
		return -EINVAL;

	pi = devm_kmalloc(dev, sizeof(*pi), GFP_KERNEL);
	if (!pi)
		return -ENOMEM;

	pi->base = base;
	pi->capability = PVPANIC_PANICKED | PVPANIC_CRASH_LOADED | PVPANIC_SHUTDOWN;

	/* initlize capability by RDPT */
	pi->capability &= ioread8(base);
	pi->events = pi->capability;

	pi->sys_off = NULL;
	pvpanic_synchronize_sys_off_handler(dev, pi);

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

	atomic_notifier_chain_register(&panic_notifier_list, &pvpanic_panic_nb);

	return 0;
}
module_init(pvpanic_init);

static void pvpanic_exit(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list, &pvpanic_panic_nb);

}
module_exit(pvpanic_exit);
