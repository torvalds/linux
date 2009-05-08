/*
 *  acpi_ac.c - ACPI AC Adapter Driver ($Revision: 27 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#ifdef CONFIG_ACPI_PROCFS_POWER
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#endif
#ifdef CONFIG_ACPI_SYSFS_POWER
#include <linux/power_supply.h>
#endif
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define ACPI_AC_CLASS			"ac_adapter"
#define ACPI_AC_DEVICE_NAME		"AC Adapter"
#define ACPI_AC_FILE_STATE		"state"
#define ACPI_AC_NOTIFY_STATUS		0x80
#define ACPI_AC_STATUS_OFFLINE		0x00
#define ACPI_AC_STATUS_ONLINE		0x01
#define ACPI_AC_STATUS_UNKNOWN		0xFF

#define _COMPONENT		ACPI_AC_COMPONENT
ACPI_MODULE_NAME("ac");

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION("ACPI AC Adapter Driver");
MODULE_LICENSE("GPL");

#ifdef CONFIG_ACPI_PROCFS_POWER
extern struct proc_dir_entry *acpi_lock_ac_dir(void);
extern void *acpi_unlock_ac_dir(struct proc_dir_entry *acpi_ac_dir);
static int acpi_ac_open_fs(struct inode *inode, struct file *file);
#endif

static int acpi_ac_add(struct acpi_device *device);
static int acpi_ac_remove(struct acpi_device *device, int type);
static int acpi_ac_resume(struct acpi_device *device);

static const struct acpi_device_id ac_device_ids[] = {
	{"ACPI0003", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, ac_device_ids);

static struct acpi_driver acpi_ac_driver = {
	.name = "ac",
	.class = ACPI_AC_CLASS,
	.ids = ac_device_ids,
	.ops = {
		.add = acpi_ac_add,
		.remove = acpi_ac_remove,
		.resume = acpi_ac_resume,
		},
};

struct acpi_ac {
#ifdef CONFIG_ACPI_SYSFS_POWER
	struct power_supply charger;
#endif
	struct acpi_device * device;
	unsigned long long state;
};

#define to_acpi_ac(x) container_of(x, struct acpi_ac, charger);

#ifdef CONFIG_ACPI_PROCFS_POWER
static const struct file_operations acpi_ac_fops = {
	.owner = THIS_MODULE,
	.open = acpi_ac_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif
#ifdef CONFIG_ACPI_SYSFS_POWER
static int get_ac_property(struct power_supply *psy,
			   enum power_supply_property psp,
			   union power_supply_propval *val)
{
	struct acpi_ac *ac = to_acpi_ac(psy);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = ac->state;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static enum power_supply_property ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};
#endif
/* --------------------------------------------------------------------------
                               AC Adapter Management
   -------------------------------------------------------------------------- */

static int acpi_ac_get_state(struct acpi_ac *ac)
{
	acpi_status status = AE_OK;


	if (!ac)
		return -EINVAL;

	status = acpi_evaluate_integer(ac->device->handle, "_PSR", NULL, &ac->state);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Error reading AC Adapter state"));
		ac->state = ACPI_AC_STATUS_UNKNOWN;
		return -ENODEV;
	}

	return 0;
}

#ifdef CONFIG_ACPI_PROCFS_POWER
/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static struct proc_dir_entry *acpi_ac_dir;

static int acpi_ac_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_ac *ac = seq->private;


	if (!ac)
		return 0;

	if (acpi_ac_get_state(ac)) {
		seq_puts(seq, "ERROR: Unable to read AC Adapter state\n");
		return 0;
	}

	seq_puts(seq, "state:                   ");
	switch (ac->state) {
	case ACPI_AC_STATUS_OFFLINE:
		seq_puts(seq, "off-line\n");
		break;
	case ACPI_AC_STATUS_ONLINE:
		seq_puts(seq, "on-line\n");
		break;
	default:
		seq_puts(seq, "unknown\n");
		break;
	}

	return 0;
}

static int acpi_ac_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_ac_seq_show, PDE(inode)->data);
}

static int acpi_ac_add_fs(struct acpi_device *device)
{
	struct proc_dir_entry *entry = NULL;


	if (!acpi_device_dir(device)) {
		acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device),
						     acpi_ac_dir);
		if (!acpi_device_dir(device))
			return -ENODEV;
	}

	/* 'state' [R] */
	entry = proc_create_data(ACPI_AC_FILE_STATE,
				 S_IRUGO, acpi_device_dir(device),
				 &acpi_ac_fops, acpi_driver_data(device));
	if (!entry)
		return -ENODEV;
	return 0;
}

static int acpi_ac_remove_fs(struct acpi_device *device)
{

	if (acpi_device_dir(device)) {
		remove_proc_entry(ACPI_AC_FILE_STATE, acpi_device_dir(device));

		remove_proc_entry(acpi_device_bid(device), acpi_ac_dir);
		acpi_device_dir(device) = NULL;
	}

	return 0;
}
#endif

/* --------------------------------------------------------------------------
                                   Driver Model
   -------------------------------------------------------------------------- */

static void acpi_ac_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_ac *ac = data;
	struct acpi_device *device = NULL;


	if (!ac)
		return;

	device = ac->device;
	switch (event) {
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
	case ACPI_AC_NOTIFY_STATUS:
	case ACPI_NOTIFY_BUS_CHECK:
	case ACPI_NOTIFY_DEVICE_CHECK:
		acpi_ac_get_state(ac);
		acpi_bus_generate_proc_event(device, event, (u32) ac->state);
		acpi_bus_generate_netlink_event(device->pnp.device_class,
						  dev_name(&device->dev), event,
						  (u32) ac->state);
#ifdef CONFIG_ACPI_SYSFS_POWER
		kobject_uevent(&ac->charger.dev->kobj, KOBJ_CHANGE);
#endif
	}

	return;
}

static int acpi_ac_add(struct acpi_device *device)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_ac *ac = NULL;


	if (!device)
		return -EINVAL;

	ac = kzalloc(sizeof(struct acpi_ac), GFP_KERNEL);
	if (!ac)
		return -ENOMEM;

	ac->device = device;
	strcpy(acpi_device_name(device), ACPI_AC_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_AC_CLASS);
	device->driver_data = ac;

	result = acpi_ac_get_state(ac);
	if (result)
		goto end;

#ifdef CONFIG_ACPI_PROCFS_POWER
	result = acpi_ac_add_fs(device);
#endif
	if (result)
		goto end;
#ifdef CONFIG_ACPI_SYSFS_POWER
	ac->charger.name = acpi_device_bid(device);
	ac->charger.type = POWER_SUPPLY_TYPE_MAINS;
	ac->charger.properties = ac_props;
	ac->charger.num_properties = ARRAY_SIZE(ac_props);
	ac->charger.get_property = get_ac_property;
	power_supply_register(&ac->device->dev, &ac->charger);
#endif
	status = acpi_install_notify_handler(device->handle,
					     ACPI_ALL_NOTIFY, acpi_ac_notify,
					     ac);
	if (ACPI_FAILURE(status)) {
		result = -ENODEV;
		goto end;
	}

	printk(KERN_INFO PREFIX "%s [%s] (%s)\n",
	       acpi_device_name(device), acpi_device_bid(device),
	       ac->state ? "on-line" : "off-line");

      end:
	if (result) {
#ifdef CONFIG_ACPI_PROCFS_POWER
		acpi_ac_remove_fs(device);
#endif
		kfree(ac);
	}

	return result;
}

static int acpi_ac_resume(struct acpi_device *device)
{
	struct acpi_ac *ac;
	unsigned old_state;
	if (!device || !acpi_driver_data(device))
		return -EINVAL;
	ac = acpi_driver_data(device);
	old_state = ac->state;
	if (acpi_ac_get_state(ac))
		return 0;
#ifdef CONFIG_ACPI_SYSFS_POWER
	if (old_state != ac->state)
		kobject_uevent(&ac->charger.dev->kobj, KOBJ_CHANGE);
#endif
	return 0;
}

static int acpi_ac_remove(struct acpi_device *device, int type)
{
	acpi_status status = AE_OK;
	struct acpi_ac *ac = NULL;


	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	ac = acpi_driver_data(device);

	status = acpi_remove_notify_handler(device->handle,
					    ACPI_ALL_NOTIFY, acpi_ac_notify);
#ifdef CONFIG_ACPI_SYSFS_POWER
	if (ac->charger.dev)
		power_supply_unregister(&ac->charger);
#endif
#ifdef CONFIG_ACPI_PROCFS_POWER
	acpi_ac_remove_fs(device);
#endif

	kfree(ac);

	return 0;
}

static int __init acpi_ac_init(void)
{
	int result;

	if (acpi_disabled)
		return -ENODEV;

#ifdef CONFIG_ACPI_PROCFS_POWER
	acpi_ac_dir = acpi_lock_ac_dir();
	if (!acpi_ac_dir)
		return -ENODEV;
#endif

	result = acpi_bus_register_driver(&acpi_ac_driver);
	if (result < 0) {
#ifdef CONFIG_ACPI_PROCFS_POWER
		acpi_unlock_ac_dir(acpi_ac_dir);
#endif
		return -ENODEV;
	}

	return 0;
}

static void __exit acpi_ac_exit(void)
{

	acpi_bus_unregister_driver(&acpi_ac_driver);

#ifdef CONFIG_ACPI_PROCFS_POWER
	acpi_unlock_ac_dir(acpi_ac_dir);
#endif

	return;
}

module_init(acpi_ac_init);
module_exit(acpi_ac_exit);
