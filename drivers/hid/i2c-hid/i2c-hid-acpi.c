/*
 * HID over I2C ACPI Subclass
 *
 * Copyright (c) 2012 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright (c) 2012 Ecole Nationale de l'Aviation Civile, France
 * Copyright (c) 2012 Red Hat, Inc
 *
 * This code was forked out of the core code, which was partly based on
 * "USB HID support for Linux":
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2007-2008 Oliver Neukum
 *  Copyright (c) 2006-2010 Jiri Kosina
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm.h>

#include "i2c-hid.h"
#include "i2c-hid-acpi.h"

struct i2c_hid_acpi {
	struct i2chid_ops ops;
	struct acpi_device *adev;
};

static const struct acpi_device_id i2c_hid_acpi_blacklist[] = {
	/*
	 * The CHPN0001 ACPI device, which is used to describe the Chipone
	 * ICN8505 controller, has a _CID of PNP0C50 but is not HID compatible.
	 */
	{ .id = "CHPN0001" },
	/*
	 * The IDEA5002 ACPI device causes high interrupt usage and spurious
	 * wakeups from suspend.
	 */
	{ .id = "IDEA5002" },
	{ }
};

static void i2c_hid_acpi_restore_sequence(struct i2chid_ops *ops)
{
	struct i2c_hid_acpi *ihid_acpi = container_of(ops, struct i2c_hid_acpi, ops);

	i2c_hid_acpi_get_descriptor(ihid_acpi->adev);
}

/*
 * HP OmniBook X Flip 14 (board 8D9F), OmniBook 7 Flip 16 (8DA0) and
 * OmniBook X Flip 16 (8DA1): the touchscreen power resource
 * \_SB.PC00.I2C4.PTPL._ON writes the wrong value to the GPIROUTIOXAPIC bit
 * of the touchscreen interrupt pad, disconnecting it from the IO-APIC. The
 * level-triggered interrupt then never deasserts, the controller is read
 * continuously (~630 reads/s while idle) and it cannot deliver pen reports
 * at its native rate. Windows does not run _ON while the resource already
 * reports on, so it is not affected. Linux runs _ON at probe and on every
 * resume, so re-enable the routing with the firmware's own helper before
 * the HID reset.
 */
static const struct dmi_system_id i2c_hid_acpi_irq_route_dmi[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "8D9F"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "8DA0"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "8DA1"),
		},
	},
	{ }
};

static int i2c_hid_acpi_restore_irq_route(struct i2chid_ops *ops)
{
	struct i2c_hid_acpi *ihid_acpi = container_of(ops, struct i2c_hid_acpi, ops);
	union acpi_object args[2];
	struct acpi_object_list arg_list = { ARRAY_SIZE(args), args };
	unsigned long long pad;
	acpi_status status;

	status = acpi_evaluate_integer(NULL, "\\GPLI", NULL, &pad);
	if (ACPI_FAILURE(status))
		return 0;

	args[0].type = ACPI_TYPE_INTEGER;
	args[0].integer.value = pad;
	args[1].type = ACPI_TYPE_INTEGER;
	args[1].integer.value = 1;

	status = acpi_evaluate_object(NULL, "\\_SB.SGRA", &arg_list, NULL);
	if (ACPI_FAILURE(status))
		acpi_handle_warn(acpi_device_handle(ihid_acpi->adev),
				 "failed to restore interrupt routing: %s\n",
				 acpi_format_exception(status));

	/* Never fail probe or resume because of this workaround */
	return 0;
}

static void i2c_hid_acpi_shutdown_tail(struct i2chid_ops *ops)
{
	struct i2c_hid_acpi *ihid_acpi = container_of(ops, struct i2c_hid_acpi, ops);

	acpi_device_set_power(ihid_acpi->adev, ACPI_STATE_D3_COLD);
}

static int i2c_hid_acpi_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct i2c_hid_acpi *ihid_acpi;
	u16 hid_descriptor_address;
	int ret;

	if (acpi_match_device_ids(adev, i2c_hid_acpi_blacklist) == 0)
		return -ENODEV;

	ret = i2c_hid_acpi_get_descriptor(adev);
	if (ret < 0)
		return ret;
	hid_descriptor_address = ret;

	ihid_acpi = devm_kzalloc(dev, sizeof(*ihid_acpi), GFP_KERNEL);
	if (!ihid_acpi)
		return -ENOMEM;

	ihid_acpi->adev = adev;
	ihid_acpi->ops.shutdown_tail = i2c_hid_acpi_shutdown_tail;
	ihid_acpi->ops.restore_sequence = i2c_hid_acpi_restore_sequence;
	if (acpi_dev_hid_uid_match(ihid_acpi->adev, "ELAN2514", NULL) &&
	    dmi_check_system(i2c_hid_acpi_irq_route_dmi))
		ihid_acpi->ops.power_up = i2c_hid_acpi_restore_irq_route;

	acpi_device_fix_up_power(adev);

	return i2c_hid_core_probe(client, &ihid_acpi->ops,
				  hid_descriptor_address, 0);
}

static const struct acpi_device_id i2c_hid_acpi_match[] = {
	{ .id = "ACPI0C50" },
	{ .id = "PNP0C50" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, i2c_hid_acpi_match);

static struct i2c_driver i2c_hid_acpi_driver = {
	.driver = {
		.name	= "i2c_hid_acpi",
		.pm	= &i2c_hid_core_pm,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.acpi_match_table = i2c_hid_acpi_match,
	},

	.probe		= i2c_hid_acpi_probe,
	.remove		= i2c_hid_core_remove,
	.shutdown	= i2c_hid_core_shutdown,
};

module_i2c_driver(i2c_hid_acpi_driver);

MODULE_DESCRIPTION("HID over I2C ACPI driver");
MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@gmail.com>");
MODULE_LICENSE("GPL");
