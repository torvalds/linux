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
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/dmi.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/acpi.h>

#define PREFIX "ACPI: "

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

static int ac_sleep_before_get_state_ms;

struct acpi_ac {
	struct power_supply charger;
	struct platform_device *pdev;
	unsigned long long state;
};

#define to_acpi_ac(x) container_of(x, struct acpi_ac, charger)

/* --------------------------------------------------------------------------
                               AC Adapter Management
   -------------------------------------------------------------------------- */

static int acpi_ac_get_state(struct acpi_ac *ac)
{
	acpi_status status;
	acpi_handle handle = ACPI_HANDLE(&ac->pdev->dev);

	status = acpi_evaluate_integer(handle, "_PSR", NULL,
				       &ac->state);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Error reading AC Adapter state"));
		ac->state = ACPI_AC_STATUS_UNKNOWN;
		return -ENODEV;
	}

	return 0;
}

/* --------------------------------------------------------------------------
                            sysfs I/F
   -------------------------------------------------------------------------- */
static int get_ac_property(struct power_supply *psy,
			   enum power_supply_property psp,
			   union power_supply_propval *val)
{
	struct acpi_ac *ac = to_acpi_ac(psy);

	if (!ac)
		return -ENODEV;

	if (acpi_ac_get_state(ac))
		return -ENODEV;

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

/* --------------------------------------------------------------------------
                                   Driver Model
   -------------------------------------------------------------------------- */

static void acpi_ac_notify_handler(acpi_handle handle, u32 event, void *data)
{
	struct acpi_ac *ac = data;
	struct acpi_device *adev;

	if (!ac)
		return;

	switch (event) {
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
	case ACPI_AC_NOTIFY_STATUS:
	case ACPI_NOTIFY_BUS_CHECK:
	case ACPI_NOTIFY_DEVICE_CHECK:
		/*
		 * A buggy BIOS may notify AC first and then sleep for
		 * a specific time before doing actual operations in the
		 * EC event handler (_Qxx). This will cause the AC state
		 * reported by the ACPI event to be incorrect, so wait for a
		 * specific time for the EC event handler to make progress.
		 */
		if (ac_sleep_before_get_state_ms > 0)
			msleep(ac_sleep_before_get_state_ms);

		acpi_ac_get_state(ac);
		adev = ACPI_COMPANION(&ac->pdev->dev);
		acpi_bus_generate_netlink_event(adev->pnp.device_class,
						dev_name(&ac->pdev->dev),
						event, (u32) ac->state);
		acpi_notifier_call_chain(adev, event, (u32) ac->state);
		kobject_uevent(&ac->charger.dev->kobj, KOBJ_CHANGE);
	}

	return;
}

static int thinkpad_e530_quirk(const struct dmi_system_id *d)
{
	ac_sleep_before_get_state_ms = 1000;
	return 0;
}

static struct dmi_system_id ac_dmi_table[] = {
	{
	.callback = thinkpad_e530_quirk,
	.ident = "thinkpad e530",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "32597CG"),
		},
	},
	{},
};

static int acpi_ac_probe(struct platform_device *pdev)
{
	int result = 0;
	struct acpi_ac *ac = NULL;
	struct acpi_device *adev;

	if (!pdev)
		return -EINVAL;

	adev = ACPI_COMPANION(&pdev->dev);
	if (!adev)
		return -ENODEV;

	ac = kzalloc(sizeof(struct acpi_ac), GFP_KERNEL);
	if (!ac)
		return -ENOMEM;

	strcpy(acpi_device_name(adev), ACPI_AC_DEVICE_NAME);
	strcpy(acpi_device_class(adev), ACPI_AC_CLASS);
	ac->pdev = pdev;
	platform_set_drvdata(pdev, ac);

	result = acpi_ac_get_state(ac);
	if (result)
		goto end;

	ac->charger.name = acpi_device_bid(adev);
	ac->charger.type = POWER_SUPPLY_TYPE_MAINS;
	ac->charger.properties = ac_props;
	ac->charger.num_properties = ARRAY_SIZE(ac_props);
	ac->charger.get_property = get_ac_property;
	result = power_supply_register(&pdev->dev, &ac->charger);
	if (result)
		goto end;

	result = acpi_install_notify_handler(ACPI_HANDLE(&pdev->dev),
			ACPI_ALL_NOTIFY, acpi_ac_notify_handler, ac);
	if (result) {
		power_supply_unregister(&ac->charger);
		goto end;
	}
	printk(KERN_INFO PREFIX "%s [%s] (%s)\n",
	       acpi_device_name(adev), acpi_device_bid(adev),
	       ac->state ? "on-line" : "off-line");

end:
	if (result)
		kfree(ac);

	dmi_check_system(ac_dmi_table);
	return result;
}

#ifdef CONFIG_PM_SLEEP
static int acpi_ac_resume(struct device *dev)
{
	struct acpi_ac *ac;
	unsigned old_state;

	if (!dev)
		return -EINVAL;

	ac = platform_get_drvdata(to_platform_device(dev));
	if (!ac)
		return -EINVAL;

	old_state = ac->state;
	if (acpi_ac_get_state(ac))
		return 0;
	if (old_state != ac->state)
		kobject_uevent(&ac->charger.dev->kobj, KOBJ_CHANGE);
	return 0;
}
#else
#define acpi_ac_resume NULL
#endif
static SIMPLE_DEV_PM_OPS(acpi_ac_pm_ops, NULL, acpi_ac_resume);

static int acpi_ac_remove(struct platform_device *pdev)
{
	struct acpi_ac *ac;

	if (!pdev)
		return -EINVAL;

	acpi_remove_notify_handler(ACPI_HANDLE(&pdev->dev),
			ACPI_ALL_NOTIFY, acpi_ac_notify_handler);

	ac = platform_get_drvdata(pdev);
	if (ac->charger.dev)
		power_supply_unregister(&ac->charger);

	kfree(ac);

	return 0;
}

static const struct acpi_device_id acpi_ac_match[] = {
	{ "ACPI0003", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, acpi_ac_match);

static struct platform_driver acpi_ac_driver = {
	.probe          = acpi_ac_probe,
	.remove         = acpi_ac_remove,
	.driver         = {
		.name   = "acpi-ac",
		.owner  = THIS_MODULE,
		.pm     = &acpi_ac_pm_ops,
		.acpi_match_table = ACPI_PTR(acpi_ac_match),
	},
};

static int __init acpi_ac_init(void)
{
	int result;

	if (acpi_disabled)
		return -ENODEV;

	result = platform_driver_register(&acpi_ac_driver);
	if (result < 0)
		return -ENODEV;

	return 0;
}

static void __exit acpi_ac_exit(void)
{
	platform_driver_unregister(&acpi_ac_driver);
}
module_init(acpi_ac_init);
module_exit(acpi_ac_exit);
