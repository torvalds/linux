/* Works like the fakephp driver used to, except a little better.
 *
 * - It's possible to remove devices with subordinate busses.
 * - New PCI devices that appear via any method, not just a fakephp triggered
 *   rescan, will be noticed.
 * - Devices that are removed via any method, not just a fakephp triggered
 *   removal, will also be noticed.
 *
 * Uses nothing from the pci-hotplug subsystem.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/slab.h>
#include "../pci.h"

struct legacy_slot {
	struct kobject		kobj;
	struct pci_dev		*dev;
	struct list_head	list;
};

static LIST_HEAD(legacy_list);

static ssize_t legacy_show(struct kobject *kobj, struct attribute *attr,
			   char *buf)
{
	struct legacy_slot *slot = container_of(kobj, typeof(*slot), kobj);
	strcpy(buf, "1\n");
	return 2;
}

static void remove_callback(void *data)
{
	pci_remove_bus_device((struct pci_dev *)data);
}

static ssize_t legacy_store(struct kobject *kobj, struct attribute *attr,
			    const char *buf, size_t len)
{
	struct legacy_slot *slot = container_of(kobj, typeof(*slot), kobj);
	unsigned long val;

	if (strict_strtoul(buf, 0, &val) < 0)
		return -EINVAL;

	if (val)
		pci_rescan_bus(slot->dev->bus);
	else
		sysfs_schedule_callback(&slot->dev->dev.kobj, remove_callback,
					slot->dev, THIS_MODULE);
	return len;
}

static struct attribute *legacy_attrs[] = {
	&(struct attribute){ .name = "power", .mode = 0644 },
	NULL,
};

static void legacy_release(struct kobject *kobj)
{
	struct legacy_slot *slot = container_of(kobj, typeof(*slot), kobj);

	pci_dev_put(slot->dev);
	kfree(slot);
}

static struct kobj_type legacy_ktype = {
	.sysfs_ops = &(const struct sysfs_ops){
		.store = legacy_store, .show = legacy_show
	},
	.release = &legacy_release,
	.default_attrs = legacy_attrs,
};

static int legacy_add_slot(struct pci_dev *pdev)
{
	struct legacy_slot *slot = kzalloc(sizeof(*slot), GFP_KERNEL);

	if (!slot)
		return -ENOMEM;

	if (kobject_init_and_add(&slot->kobj, &legacy_ktype,
				 &pci_slots_kset->kobj, "%s",
				 dev_name(&pdev->dev))) {
		dev_warn(&pdev->dev, "Failed to created legacy fake slot\n");
		return -EINVAL;
	}
	slot->dev = pci_dev_get(pdev);

	list_add(&slot->list, &legacy_list);

	return 0;
}

static int legacy_notify(struct notifier_block *nb,
			 unsigned long action, void *data)
{
	struct pci_dev *pdev = to_pci_dev(data);

	if (action == BUS_NOTIFY_ADD_DEVICE) {
		legacy_add_slot(pdev);
	} else if (action == BUS_NOTIFY_DEL_DEVICE) {
		struct legacy_slot *slot;

		list_for_each_entry(slot, &legacy_list, list)
			if (slot->dev == pdev)
				goto found;

		dev_warn(&pdev->dev, "Missing legacy fake slot?");
		return -ENODEV;
found:
		kobject_del(&slot->kobj);
		list_del(&slot->list);
		kobject_put(&slot->kobj);
	}

	return 0;
}

static struct notifier_block legacy_notifier = {
	.notifier_call = legacy_notify
};

static int __init init_legacy(void)
{
	struct pci_dev *pdev = NULL;

	/* Add existing devices */
	for_each_pci_dev(pdev)
		legacy_add_slot(pdev);

	/* Be alerted of any new ones */
	bus_register_notifier(&pci_bus_type, &legacy_notifier);
	return 0;
}
module_init(init_legacy);

static void __exit remove_legacy(void)
{
	struct legacy_slot *slot, *tmp;

	bus_unregister_notifier(&pci_bus_type, &legacy_notifier);

	list_for_each_entry_safe(slot, tmp, &legacy_list, list) {
		list_del(&slot->list);
		kobject_del(&slot->kobj);
		kobject_put(&slot->kobj);
	}
}
module_exit(remove_legacy);


MODULE_AUTHOR("Trent Piepho <xyzzy@speakeasy.org>");
MODULE_DESCRIPTION("Legacy version of the fakephp interface");
MODULE_LICENSE("GPL");
