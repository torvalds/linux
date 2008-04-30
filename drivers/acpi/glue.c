/*
 * Link physical devices with ACPI devices support
 *
 * Copyright (c) 2005 David Shaohua Li <shaohua.li@intel.com>
 * Copyright (c) 2005 Intel Corp.
 *
 * This file is released under the GPLv2.
 */
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/rwsem.h>
#include <linux/acpi.h>

#define ACPI_GLUE_DEBUG	0
#if ACPI_GLUE_DEBUG
#define DBG(x...) printk(PREFIX x)
#else
#define DBG(x...) do { } while(0)
#endif
static LIST_HEAD(bus_type_list);
static DECLARE_RWSEM(bus_type_sem);

int register_acpi_bus_type(struct acpi_bus_type *type)
{
	if (acpi_disabled)
		return -ENODEV;
	if (type && type->bus && type->find_device) {
		down_write(&bus_type_sem);
		list_add_tail(&type->list, &bus_type_list);
		up_write(&bus_type_sem);
		printk(KERN_INFO PREFIX "bus type %s registered\n",
		       type->bus->name);
		return 0;
	}
	return -ENODEV;
}

int unregister_acpi_bus_type(struct acpi_bus_type *type)
{
	if (acpi_disabled)
		return 0;
	if (type) {
		down_write(&bus_type_sem);
		list_del_init(&type->list);
		up_write(&bus_type_sem);
		printk(KERN_INFO PREFIX "ACPI bus type %s unregistered\n",
		       type->bus->name);
		return 0;
	}
	return -ENODEV;
}

static struct acpi_bus_type *acpi_get_bus_type(struct bus_type *type)
{
	struct acpi_bus_type *tmp, *ret = NULL;

	down_read(&bus_type_sem);
	list_for_each_entry(tmp, &bus_type_list, list) {
		if (tmp->bus == type) {
			ret = tmp;
			break;
		}
	}
	up_read(&bus_type_sem);
	return ret;
}

static int acpi_find_bridge_device(struct device *dev, acpi_handle * handle)
{
	struct acpi_bus_type *tmp;
	int ret = -ENODEV;

	down_read(&bus_type_sem);
	list_for_each_entry(tmp, &bus_type_list, list) {
		if (tmp->find_bridge && !tmp->find_bridge(dev, handle)) {
			ret = 0;
			break;
		}
	}
	up_read(&bus_type_sem);
	return ret;
}

/* Get device's handler per its address under its parent */
struct acpi_find_child {
	acpi_handle handle;
	acpi_integer address;
};

static acpi_status
do_acpi_find_child(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	acpi_status status;
	struct acpi_device_info *info;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_find_child *find = context;

	status = acpi_get_object_info(handle, &buffer);
	if (ACPI_SUCCESS(status)) {
		info = buffer.pointer;
		if (info->address == find->address)
			find->handle = handle;
		kfree(buffer.pointer);
	}
	return AE_OK;
}

acpi_handle acpi_get_child(acpi_handle parent, acpi_integer address)
{
	struct acpi_find_child find = { NULL, address };

	if (!parent)
		return NULL;
	acpi_walk_namespace(ACPI_TYPE_DEVICE, parent,
			    1, do_acpi_find_child, &find, NULL);
	return find.handle;
}

EXPORT_SYMBOL(acpi_get_child);

/* Link ACPI devices with physical devices */
static void acpi_glue_data_handler(acpi_handle handle,
				   u32 function, void *context)
{
	/* we provide an empty handler */
}

/* Note: a success call will increase reference count by one */
struct device *acpi_get_physical_device(acpi_handle handle)
{
	acpi_status status;
	struct device *dev;

	status = acpi_get_data(handle, acpi_glue_data_handler, (void **)&dev);
	if (ACPI_SUCCESS(status))
		return get_device(dev);
	return NULL;
}

EXPORT_SYMBOL(acpi_get_physical_device);

static int acpi_bind_one(struct device *dev, acpi_handle handle)
{
	struct acpi_device *acpi_dev;
	acpi_status status;

	if (dev->archdata.acpi_handle) {
		printk(KERN_WARNING PREFIX
		       "Drivers changed 'acpi_handle' for %s\n", dev->bus_id);
		return -EINVAL;
	}
	get_device(dev);
	status = acpi_attach_data(handle, acpi_glue_data_handler, dev);
	if (ACPI_FAILURE(status)) {
		put_device(dev);
		return -EINVAL;
	}
	dev->archdata.acpi_handle = handle;

	status = acpi_bus_get_device(handle, &acpi_dev);
	if (!ACPI_FAILURE(status)) {
		int ret;

		ret = sysfs_create_link(&dev->kobj, &acpi_dev->dev.kobj,
				"firmware_node");
		ret = sysfs_create_link(&acpi_dev->dev.kobj, &dev->kobj,
				"physical_node");
	}

	return 0;
}

static int acpi_unbind_one(struct device *dev)
{
	if (!dev->archdata.acpi_handle)
		return 0;
	if (dev == acpi_get_physical_device(dev->archdata.acpi_handle)) {
		struct acpi_device *acpi_dev;

		/* acpi_get_physical_device increase refcnt by one */
		put_device(dev);

		if (!acpi_bus_get_device(dev->archdata.acpi_handle,
					&acpi_dev)) {
			sysfs_remove_link(&dev->kobj, "firmware_node");
			sysfs_remove_link(&acpi_dev->dev.kobj, "physical_node");
		}

		acpi_detach_data(dev->archdata.acpi_handle,
				 acpi_glue_data_handler);
		dev->archdata.acpi_handle = NULL;
		/* acpi_bind_one increase refcnt by one */
		put_device(dev);
	} else {
		printk(KERN_ERR PREFIX
		       "Oops, 'acpi_handle' corrupt for %s\n", dev->bus_id);
	}
	return 0;
}

static int acpi_platform_notify(struct device *dev)
{
	struct acpi_bus_type *type;
	acpi_handle handle;
	int ret = -EINVAL;

	if (!dev->bus || !dev->parent) {
		/* bridge devices genernally haven't bus or parent */
		ret = acpi_find_bridge_device(dev, &handle);
		goto end;
	}
	type = acpi_get_bus_type(dev->bus);
	if (!type) {
		DBG("No ACPI bus support for %s\n", dev->bus_id);
		ret = -EINVAL;
		goto end;
	}
	if ((ret = type->find_device(dev, &handle)) != 0)
		DBG("Can't get handler for %s\n", dev->bus_id);
      end:
	if (!ret)
		acpi_bind_one(dev, handle);

#if ACPI_GLUE_DEBUG
	if (!ret) {
		struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

		acpi_get_name(dev->archdata.acpi_handle,
			      ACPI_FULL_PATHNAME, &buffer);
		DBG("Device %s -> %s\n", dev->bus_id, (char *)buffer.pointer);
		kfree(buffer.pointer);
	} else
		DBG("Device %s -> No ACPI support\n", dev->bus_id);
#endif

	return ret;
}

static int acpi_platform_notify_remove(struct device *dev)
{
	acpi_unbind_one(dev);
	return 0;
}

static int __init init_acpi_device_notify(void)
{
	if (acpi_disabled)
		return 0;
	if (platform_notify || platform_notify_remove) {
		printk(KERN_ERR PREFIX "Can't use platform_notify\n");
		return 0;
	}
	platform_notify = acpi_platform_notify;
	platform_notify_remove = acpi_platform_notify_remove;
	return 0;
}

arch_initcall(init_acpi_device_notify);


#if defined(CONFIG_RTC_DRV_CMOS) || defined(CONFIG_RTC_DRV_CMOS_MODULE)

#ifdef CONFIG_PM
static u32 rtc_handler(void *context)
{
	acpi_clear_event(ACPI_EVENT_RTC);
	acpi_disable_event(ACPI_EVENT_RTC, 0);
	return ACPI_INTERRUPT_HANDLED;
}

static inline void rtc_wake_setup(void)
{
	acpi_install_fixed_event_handler(ACPI_EVENT_RTC, rtc_handler, NULL);
}

static void rtc_wake_on(struct device *dev)
{
	acpi_clear_event(ACPI_EVENT_RTC);
	acpi_enable_event(ACPI_EVENT_RTC, 0);
}

static void rtc_wake_off(struct device *dev)
{
	acpi_disable_event(ACPI_EVENT_RTC, 0);
}
#else
#define rtc_wake_setup()	do{}while(0)
#define rtc_wake_on		NULL
#define rtc_wake_off		NULL
#endif

/* Every ACPI platform has a mc146818 compatible "cmos rtc".  Here we find
 * its device node and pass extra config data.  This helps its driver use
 * capabilities that the now-obsolete mc146818 didn't have, and informs it
 * that this board's RTC is wakeup-capable (per ACPI spec).
 */
#include <linux/mc146818rtc.h>

static struct cmos_rtc_board_info rtc_info;


/* PNP devices are registered in a subsys_initcall();
 * ACPI specifies the PNP IDs to use.
 */
#include <linux/pnp.h>

static int __init pnp_match(struct device *dev, void *data)
{
	static const char *ids[] = { "PNP0b00", "PNP0b01", "PNP0b02", };
	struct pnp_dev *pnp = to_pnp_dev(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(ids); i++) {
		if (compare_pnp_id(pnp->id, ids[i]) != 0)
			return 1;
	}
	return 0;
}

static struct device *__init get_rtc_dev(void)
{
	return bus_find_device(&pnp_bus_type, NULL, NULL, pnp_match);
}

static int __init acpi_rtc_init(void)
{
	struct device *dev = get_rtc_dev();

	if (dev) {
		rtc_wake_setup();
		rtc_info.wake_on = rtc_wake_on;
		rtc_info.wake_off = rtc_wake_off;

		/* workaround bug in some ACPI tables */
		if (acpi_gbl_FADT.month_alarm && !acpi_gbl_FADT.day_alarm) {
			DBG("bogus FADT month_alarm\n");
			acpi_gbl_FADT.month_alarm = 0;
		}

		rtc_info.rtc_day_alarm = acpi_gbl_FADT.day_alarm;
		rtc_info.rtc_mon_alarm = acpi_gbl_FADT.month_alarm;
		rtc_info.rtc_century = acpi_gbl_FADT.century;

		/* NOTE:  S4_RTC_WAKE is NOT currently useful to Linux */
		if (acpi_gbl_FADT.flags & ACPI_FADT_S4_RTC_WAKE)
			printk(PREFIX "RTC can wake from S4\n");


		dev->platform_data = &rtc_info;

		/* RTC always wakes from S1/S2/S3, and often S4/STD */
		device_init_wakeup(dev, 1);

		put_device(dev);
	} else
		DBG("RTC unavailable?\n");
	return 0;
}
/* do this between RTC subsys_initcall() and rtc_cmos driver_initcall() */
fs_initcall(acpi_rtc_init);

#endif
