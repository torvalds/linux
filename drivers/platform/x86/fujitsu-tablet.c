/*
 * Copyright (C) 2006-2012 Robert Gerlach <khnz@gmx.de>
 * Copyright (C) 2005-2006 Jan Rychter <jan@rychter.com>
 *
 * You can redistribute and/or modify this program under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place Suite 330, Boston, MA 02111-1307, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/dmi.h>

#define MODULENAME "fujitsu-tablet"

#define ACPI_FUJITSU_CLASS "fujitsu"

#define INVERT_TABLET_MODE_BIT      0x01
#define INVERT_DOCK_STATE_BIT       0x02
#define FORCE_TABLET_MODE_IF_UNDOCK 0x04

#define KEYMAP_LEN 16

static const struct acpi_device_id fujitsu_ids[] = {
	{ .id = "FUJ02BD" },
	{ .id = "FUJ02BF" },
	{ .id = "" }
};

struct fujitsu_config {
	unsigned short keymap[KEYMAP_LEN];
	unsigned int quirks;
};

static unsigned short keymap_Lifebook_Tseries[KEYMAP_LEN] __initconst = {
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_SCROLLDOWN,
	KEY_SCROLLUP,
	KEY_DIRECTION,
	KEY_LEFTCTRL,
	KEY_BRIGHTNESSUP,
	KEY_BRIGHTNESSDOWN,
	KEY_BRIGHTNESS_ZERO,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_LEFTALT
};

static unsigned short keymap_Lifebook_U810[KEYMAP_LEN] __initconst = {
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_PROG1,
	KEY_PROG2,
	KEY_DIRECTION,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_UP,
	KEY_DOWN,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_LEFTCTRL,
	KEY_LEFTALT
};

static unsigned short keymap_Stylistic_Tseries[KEYMAP_LEN] __initconst = {
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_PRINT,
	KEY_BACKSPACE,
	KEY_SPACE,
	KEY_ENTER,
	KEY_BRIGHTNESSUP,
	KEY_BRIGHTNESSDOWN,
	KEY_DOWN,
	KEY_UP,
	KEY_SCROLLUP,
	KEY_SCROLLDOWN,
	KEY_LEFTCTRL,
	KEY_LEFTALT
};

static unsigned short keymap_Stylistic_ST5xxx[KEYMAP_LEN] __initconst = {
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_MAIL,
	KEY_DIRECTION,
	KEY_ESC,
	KEY_ENTER,
	KEY_BRIGHTNESSUP,
	KEY_BRIGHTNESSDOWN,
	KEY_DOWN,
	KEY_UP,
	KEY_SCROLLUP,
	KEY_SCROLLDOWN,
	KEY_LEFTCTRL,
	KEY_LEFTALT
};

static struct {
	struct input_dev *idev;
	struct fujitsu_config config;
	unsigned long prev_keymask;

	char phys[21];

	int irq;
	int io_base;
	int io_length;
} fujitsu;

static u8 fujitsu_ack(void)
{
	return inb(fujitsu.io_base + 2);
}

static u8 fujitsu_status(void)
{
	return inb(fujitsu.io_base + 6);
}

static u8 fujitsu_read_register(const u8 addr)
{
	outb(addr, fujitsu.io_base);
	return inb(fujitsu.io_base + 4);
}

static void fujitsu_send_state(void)
{
	int state;
	int dock, tablet_mode;

	state = fujitsu_read_register(0xdd);

	dock = state & 0x02;
	if (fujitsu.config.quirks & INVERT_DOCK_STATE_BIT)
		dock = !dock;

	if ((fujitsu.config.quirks & FORCE_TABLET_MODE_IF_UNDOCK) && (!dock)) {
		tablet_mode = 1;
	} else{
		tablet_mode = state & 0x01;
		if (fujitsu.config.quirks & INVERT_TABLET_MODE_BIT)
			tablet_mode = !tablet_mode;
	}

	input_report_switch(fujitsu.idev, SW_DOCK, dock);
	input_report_switch(fujitsu.idev, SW_TABLET_MODE, tablet_mode);
	input_sync(fujitsu.idev);
}

static void fujitsu_reset(void)
{
	int timeout = 50;

	fujitsu_ack();

	while ((fujitsu_status() & 0x02) && (--timeout))
		msleep(20);

	fujitsu_send_state();
}

static int __devinit input_fujitsu_setup(struct device *parent,
					 const char *name, const char *phys)
{
	struct input_dev *idev;
	int error;
	int i;

	idev = input_allocate_device();
	if (!idev)
		return -ENOMEM;

	idev->dev.parent = parent;
	idev->phys = phys;
	idev->name = name;
	idev->id.bustype = BUS_HOST;
	idev->id.vendor  = 0x1734;	/* Fujitsu Siemens Computer GmbH */
	idev->id.product = 0x0001;
	idev->id.version = 0x0101;

	idev->keycode = fujitsu.config.keymap;
	idev->keycodesize = sizeof(fujitsu.config.keymap[0]);
	idev->keycodemax = ARRAY_SIZE(fujitsu.config.keymap);

	__set_bit(EV_REP, idev->evbit);

	for (i = 0; i < ARRAY_SIZE(fujitsu.config.keymap); i++)
		if (fujitsu.config.keymap[i])
			input_set_capability(idev, EV_KEY, fujitsu.config.keymap[i]);

	input_set_capability(idev, EV_MSC, MSC_SCAN);

	input_set_capability(idev, EV_SW, SW_DOCK);
	input_set_capability(idev, EV_SW, SW_TABLET_MODE);

	error = input_register_device(idev);
	if (error) {
		input_free_device(idev);
		return error;
	}

	fujitsu.idev = idev;
	return 0;
}

static void input_fujitsu_remove(void)
{
	input_unregister_device(fujitsu.idev);
}

static irqreturn_t fujitsu_interrupt(int irq, void *dev_id)
{
	unsigned long keymask, changed;
	unsigned int keycode;
	int pressed;
	int i;

	if (unlikely(!(fujitsu_status() & 0x01)))
		return IRQ_NONE;

	fujitsu_send_state();

	keymask  = fujitsu_read_register(0xde);
	keymask |= fujitsu_read_register(0xdf) << 8;
	keymask ^= 0xffff;

	changed = keymask ^ fujitsu.prev_keymask;
	if (changed) {
		fujitsu.prev_keymask = keymask;

		for_each_set_bit(i, &changed, KEYMAP_LEN) {
			keycode = fujitsu.config.keymap[i];
			pressed = keymask & changed & BIT(i);

			if (pressed)
				input_event(fujitsu.idev, EV_MSC, MSC_SCAN, i);

			input_report_key(fujitsu.idev, keycode, pressed);
			input_sync(fujitsu.idev);
		}
	}

	fujitsu_ack();
	return IRQ_HANDLED;
}

static void __devinit fujitsu_dmi_common(const struct dmi_system_id *dmi)
{
	pr_info("%s\n", dmi->ident);
	memcpy(fujitsu.config.keymap, dmi->driver_data,
			sizeof(fujitsu.config.keymap));
}

static int __devinit fujitsu_dmi_lifebook(const struct dmi_system_id *dmi)
{
	fujitsu_dmi_common(dmi);
	fujitsu.config.quirks |= INVERT_TABLET_MODE_BIT;
	return 1;
}

static int __devinit fujitsu_dmi_stylistic(const struct dmi_system_id *dmi)
{
	fujitsu_dmi_common(dmi);
	fujitsu.config.quirks |= FORCE_TABLET_MODE_IF_UNDOCK;
	fujitsu.config.quirks |= INVERT_DOCK_STATE_BIT;
	return 1;
}

static struct dmi_system_id dmi_ids[] __initconst = {
	{
		.callback = fujitsu_dmi_lifebook,
		.ident = "Fujitsu Siemens P/T Series",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LIFEBOOK")
		},
		.driver_data = keymap_Lifebook_Tseries
	},
	{
		.callback = fujitsu_dmi_lifebook,
		.ident = "Fujitsu Lifebook T Series",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LifeBook T")
		},
		.driver_data = keymap_Lifebook_Tseries
	},
	{
		.callback = fujitsu_dmi_stylistic,
		.ident = "Fujitsu Siemens Stylistic T Series",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Stylistic T")
		},
		.driver_data = keymap_Stylistic_Tseries
	},
	{
		.callback = fujitsu_dmi_lifebook,
		.ident = "Fujitsu LifeBook U810",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LifeBook U810")
		},
		.driver_data = keymap_Lifebook_U810
	},
	{
		.callback = fujitsu_dmi_stylistic,
		.ident = "Fujitsu Siemens Stylistic ST5xxx Series",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "STYLISTIC ST5")
		},
		.driver_data = keymap_Stylistic_ST5xxx
	},
	{
		.callback = fujitsu_dmi_stylistic,
		.ident = "Fujitsu Siemens Stylistic ST5xxx Series",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Stylistic ST5")
		},
		.driver_data = keymap_Stylistic_ST5xxx
	},
	{
		.callback = fujitsu_dmi_lifebook,
		.ident = "Unknown (using defaults)",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, ""),
			DMI_MATCH(DMI_PRODUCT_NAME, "")
		},
		.driver_data = keymap_Lifebook_Tseries
	},
	{ NULL }
};

static acpi_status __devinit
fujitsu_walk_resources(struct acpi_resource *res, void *data)
{
	switch (res->type) {
	case ACPI_RESOURCE_TYPE_IRQ:
		fujitsu.irq = res->data.irq.interrupts[0];
		return AE_OK;

	case ACPI_RESOURCE_TYPE_IO:
		fujitsu.io_base = res->data.io.minimum;
		fujitsu.io_length = res->data.io.address_length;
		return AE_OK;

	case ACPI_RESOURCE_TYPE_END_TAG:
		if (fujitsu.irq && fujitsu.io_base)
			return AE_OK;
		else
			return AE_NOT_FOUND;

	default:
		return AE_ERROR;
	}
}

static int __devinit acpi_fujitsu_add(struct acpi_device *adev)
{
	acpi_status status;
	int error;

	if (!adev)
		return -EINVAL;

	status = acpi_walk_resources(adev->handle, METHOD_NAME__CRS,
			fujitsu_walk_resources, NULL);
	if (ACPI_FAILURE(status) || !fujitsu.irq || !fujitsu.io_base)
		return -ENODEV;

	sprintf(acpi_device_name(adev), "Fujitsu %s", acpi_device_hid(adev));
	sprintf(acpi_device_class(adev), "%s", ACPI_FUJITSU_CLASS);

	snprintf(fujitsu.phys, sizeof(fujitsu.phys),
			"%s/input0", acpi_device_hid(adev));

	error = input_fujitsu_setup(&adev->dev,
		acpi_device_name(adev), fujitsu.phys);
	if (error)
		return error;

	if (!request_region(fujitsu.io_base, fujitsu.io_length, MODULENAME)) {
		input_fujitsu_remove();
		return -EBUSY;
	}

	fujitsu_reset();

	error = request_irq(fujitsu.irq, fujitsu_interrupt,
			IRQF_SHARED, MODULENAME, fujitsu_interrupt);
	if (error) {
		release_region(fujitsu.io_base, fujitsu.io_length);
		input_fujitsu_remove();
		return error;
	}

	return 0;
}

static int __devexit acpi_fujitsu_remove(struct acpi_device *adev, int type)
{
	free_irq(fujitsu.irq, fujitsu_interrupt);
	release_region(fujitsu.io_base, fujitsu.io_length);
	input_fujitsu_remove();
	return 0;
}

static int acpi_fujitsu_resume(struct acpi_device *adev)
{
	fujitsu_reset();
	return 0;
}

static struct acpi_driver acpi_fujitsu_driver = {
	.name  = MODULENAME,
	.class = "hotkey",
	.ids   = fujitsu_ids,
	.ops   = {
		.add    = acpi_fujitsu_add,
		.remove	= acpi_fujitsu_remove,
		.resume = acpi_fujitsu_resume,
	}
};

static int __init fujitsu_module_init(void)
{
	int error;

	dmi_check_system(dmi_ids);

	error = acpi_bus_register_driver(&acpi_fujitsu_driver);
	if (error)
		return error;

	return 0;
}

static void __exit fujitsu_module_exit(void)
{
	acpi_bus_unregister_driver(&acpi_fujitsu_driver);
}

module_init(fujitsu_module_init);
module_exit(fujitsu_module_exit);

MODULE_AUTHOR("Robert Gerlach <khnz@gmx.de>");
MODULE_DESCRIPTION("Fujitsu tablet pc extras driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("2.5");

MODULE_DEVICE_TABLE(acpi, fujitsu_ids);
