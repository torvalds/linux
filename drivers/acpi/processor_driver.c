/*
 * processor_driver.c - ACPI Processor Driver
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2004       Dominik Brodowski <linux@brodo.de>
 *  Copyright (C) 2004  Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *  			- Added processor hotplug support
 *  Copyright (C) 2013, Intel Corporation
 *                      Rafael J. Wysocki <rafael.j.wysocki@intel.com>
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
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/slab.h>
#include <linux/acpi.h>

#include <acpi/processor.h>

#include "internal.h"

#define ACPI_PROCESSOR_NOTIFY_PERFORMANCE 0x80
#define ACPI_PROCESSOR_NOTIFY_POWER	0x81
#define ACPI_PROCESSOR_NOTIFY_THROTTLING	0x82

#define _COMPONENT		ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME("processor_driver");

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION("ACPI Processor Driver");
MODULE_LICENSE("GPL");

static int acpi_processor_start(struct device *dev);
static int acpi_processor_stop(struct device *dev);

static const struct acpi_device_id processor_device_ids[] = {
	{ACPI_PROCESSOR_OBJECT_HID, 0},
	{ACPI_PROCESSOR_DEVICE_HID, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, processor_device_ids);

static struct device_driver acpi_processor_driver = {
	.name = "processor",
	.bus = &cpu_subsys,
	.acpi_match_table = processor_device_ids,
	.probe = acpi_processor_start,
	.remove = acpi_processor_stop,
};

static void acpi_processor_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_device *device = data;
	struct acpi_processor *pr;
	int saved;

	if (device->handle != handle)
		return;

	pr = acpi_driver_data(device);
	if (!pr)
		return;

	switch (event) {
	case ACPI_PROCESSOR_NOTIFY_PERFORMANCE:
		saved = pr->performance_platform_limit;
		acpi_processor_ppc_has_changed(pr, 1);
		if (saved == pr->performance_platform_limit)
			break;
		acpi_bus_generate_netlink_event(device->pnp.device_class,
						  dev_name(&device->dev), event,
						  pr->performance_platform_limit);
		break;
	case ACPI_PROCESSOR_NOTIFY_POWER:
		acpi_processor_cst_has_changed(pr);
		acpi_bus_generate_netlink_event(device->pnp.device_class,
						  dev_name(&device->dev), event, 0);
		break;
	case ACPI_PROCESSOR_NOTIFY_THROTTLING:
		acpi_processor_tstate_has_changed(pr);
		acpi_bus_generate_netlink_event(device->pnp.device_class,
						  dev_name(&device->dev), event, 0);
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}

	return;
}

static int __acpi_processor_start(struct acpi_device *device);

static int acpi_cpu_soft_notify(struct notifier_block *nfb,
					  unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct acpi_processor *pr = per_cpu(processors, cpu);
	struct acpi_device *device;

	/*
	 * CPU_STARTING and CPU_DYING must not sleep. Return here since
	 * acpi_bus_get_device() may sleep.
	 */
	if (action == CPU_STARTING || action == CPU_DYING)
		return NOTIFY_DONE;

	if (!pr || acpi_bus_get_device(pr->handle, &device))
		return NOTIFY_DONE;

	if (action == CPU_ONLINE) {
		/*
		 * CPU got physically hotplugged and onlined for the first time:
		 * Initialize missing things.
		 */
		if (pr->flags.need_hotplug_init) {
			int ret;

			pr_info("Will online and init hotplugged CPU: %d\n",
				pr->id);
			pr->flags.need_hotplug_init = 0;
			ret = __acpi_processor_start(device);
			WARN(ret, "Failed to start CPU: %d\n", pr->id);
		} else {
			/* Normal CPU soft online event. */
			acpi_processor_ppc_has_changed(pr, 0);
			acpi_processor_hotplug(pr);
			acpi_processor_reevaluate_tstate(pr, action);
			acpi_processor_tstate_has_changed(pr);
		}
	} else if (action == CPU_DEAD) {
		/* Invalidate flag.throttling after the CPU is offline. */
		acpi_processor_reevaluate_tstate(pr, action);
	}
	return NOTIFY_OK;
}

static struct notifier_block __refdata acpi_cpu_notifier = {
	    .notifier_call = acpi_cpu_soft_notify,
};

static int __acpi_processor_start(struct acpi_device *device)
{
	struct acpi_processor *pr = acpi_driver_data(device);
	acpi_status status;
	int result = 0;

	if (!pr)
		return -ENODEV;

	if (pr->flags.need_hotplug_init)
		return 0;

#ifdef CONFIG_CPU_FREQ
	acpi_processor_ppc_has_changed(pr, 0);
#endif
	acpi_processor_get_throttling_info(pr);

	if (pr->flags.throttling)
		pr->flags.limit = 1;

	if (!cpuidle_get_driver() || cpuidle_get_driver() == &acpi_idle_driver)
		acpi_processor_power_init(pr);

	pr->cdev = thermal_cooling_device_register("Processor", device,
						   &processor_cooling_ops);
	if (IS_ERR(pr->cdev)) {
		result = PTR_ERR(pr->cdev);
		goto err_power_exit;
	}

	dev_dbg(&device->dev, "registered as cooling_device%d\n",
		pr->cdev->id);

	result = sysfs_create_link(&device->dev.kobj,
				   &pr->cdev->device.kobj,
				   "thermal_cooling");
	if (result) {
		dev_err(&device->dev,
			"Failed to create sysfs link 'thermal_cooling'\n");
		goto err_thermal_unregister;
	}
	result = sysfs_create_link(&pr->cdev->device.kobj,
				   &device->dev.kobj,
				   "device");
	if (result) {
		dev_err(&pr->cdev->device,
			"Failed to create sysfs link 'device'\n");
		goto err_remove_sysfs_thermal;
	}

	status = acpi_install_notify_handler(device->handle, ACPI_DEVICE_NOTIFY,
					     acpi_processor_notify, device);
	if (ACPI_SUCCESS(status))
		return 0;

	sysfs_remove_link(&pr->cdev->device.kobj, "device");
 err_remove_sysfs_thermal:
	sysfs_remove_link(&device->dev.kobj, "thermal_cooling");
 err_thermal_unregister:
	thermal_cooling_device_unregister(pr->cdev);
 err_power_exit:
	acpi_processor_power_exit(pr);
	return result;
}

static int acpi_processor_start(struct device *dev)
{
	struct acpi_device *device = ACPI_COMPANION(dev);

	if (!device)
		return -ENODEV;

	return __acpi_processor_start(device);
}

static int acpi_processor_stop(struct device *dev)
{
	struct acpi_device *device = ACPI_COMPANION(dev);
	struct acpi_processor *pr;

	if (!device)
		return 0;

	acpi_remove_notify_handler(device->handle, ACPI_DEVICE_NOTIFY,
				   acpi_processor_notify);

	pr = acpi_driver_data(device);
	if (!pr)
		return 0;

	acpi_processor_power_exit(pr);

	if (pr->cdev) {
		sysfs_remove_link(&device->dev.kobj, "thermal_cooling");
		sysfs_remove_link(&pr->cdev->device.kobj, "device");
		thermal_cooling_device_unregister(pr->cdev);
		pr->cdev = NULL;
	}
	return 0;
}

/*
 * We keep the driver loaded even when ACPI is not running.
 * This is needed for the powernow-k8 driver, that works even without
 * ACPI, but needs symbols from this driver
 */

static int __init acpi_processor_driver_init(void)
{
	int result = 0;

	if (acpi_disabled)
		return 0;

	result = driver_register(&acpi_processor_driver);
	if (result < 0)
		return result;

	acpi_processor_syscore_init();
	register_hotcpu_notifier(&acpi_cpu_notifier);
	acpi_thermal_cpufreq_init();
	acpi_processor_ppc_init();
	acpi_processor_throttling_init();
	return 0;
}

static void __exit acpi_processor_driver_exit(void)
{
	if (acpi_disabled)
		return;

	acpi_processor_ppc_exit();
	acpi_thermal_cpufreq_exit();
	unregister_hotcpu_notifier(&acpi_cpu_notifier);
	acpi_processor_syscore_exit();
	driver_unregister(&acpi_processor_driver);
}

module_init(acpi_processor_driver_init);
module_exit(acpi_processor_driver_exit);

MODULE_ALIAS("processor");
