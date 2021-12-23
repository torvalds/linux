// SPDX-License-Identifier: GPL-2.0+
/*
 * DMI based code to deal with broken DSDTs on X86 tablets which ship with
 * Android as (part of) the factory image. The factory kernels shipped on these
 * devices typically have a bunch of things hardcoded, rather than specified
 * in their DSDT.
 *
 * Copyright (C) 2021 Hans de Goede <hdegoede@redhat.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/string.h>
/* For gpio_get_desc() which is EXPORT_SYMBOL_GPL() */
#include "../../gpio/gpiolib.h"

/*
 * Helper code to get Linux IRQ numbers given a description of the IRQ source
 * (either IOAPIC index, or GPIO chip name + pin-number).
 */
enum x86_acpi_irq_type {
	X86_ACPI_IRQ_TYPE_NONE,
	X86_ACPI_IRQ_TYPE_APIC,
	X86_ACPI_IRQ_TYPE_GPIOINT,
};

struct x86_acpi_irq_data {
	char *chip;   /* GPIO chip label (GPIOINT) */
	enum x86_acpi_irq_type type;
	int index;
	int trigger;  /* ACPI_EDGE_SENSITIVE / ACPI_LEVEL_SENSITIVE */
	int polarity; /* ACPI_ACTIVE_HIGH / ACPI_ACTIVE_LOW / ACPI_ACTIVE_BOTH */
};

static int x86_acpi_irq_helper_gpiochip_find(struct gpio_chip *gc, void *data)
{
	return gc->label && !strcmp(gc->label, data);
}

static int x86_acpi_irq_helper_get(const struct x86_acpi_irq_data *data)
{
	struct gpio_desc *gpiod;
	struct gpio_chip *chip;
	unsigned int irq_type;
	int irq, ret;

	switch (data->type) {
	case X86_ACPI_IRQ_TYPE_APIC:
		irq = acpi_register_gsi(NULL, data->index, data->trigger, data->polarity);
		if (irq < 0)
			pr_err("error %d getting APIC IRQ %d\n", irq, data->index);

		return irq;
	case X86_ACPI_IRQ_TYPE_GPIOINT:
		/* Like acpi_dev_gpio_irq_get(), but without parsing ACPI resources */
		chip = gpiochip_find(data->chip, x86_acpi_irq_helper_gpiochip_find);
		if (!chip)
			return -EPROBE_DEFER;

		gpiod = gpiochip_get_desc(chip, data->index);
		if (IS_ERR(gpiod)) {
			ret = PTR_ERR(gpiod);
			pr_err("error %d getting GPIO %s %d\n", ret, data->chip, data->index);
			return ret;
		}

		irq = gpiod_to_irq(gpiod);
		if (irq < 0) {
			pr_err("error %d getting IRQ %s %d\n", irq, data->chip, data->index);
			return irq;
		}

		irq_type = acpi_dev_get_irq_type(data->trigger, data->polarity);
		if (irq_type != IRQ_TYPE_NONE && irq_type != irq_get_trigger_type(irq))
			irq_set_irq_type(irq, irq_type);

		return irq;
	default:
		return 0;
	}
}

struct x86_i2c_client_info {
	struct i2c_board_info board_info;
	char *adapter_path;
	struct x86_acpi_irq_data irq_data;
};

struct x86_dev_info {
	const struct x86_i2c_client_info *i2c_client_info;
	int i2c_client_count;
};

/*
 * When booted with the BIOS set to Android mode the Chuwi Hi8 (CWI509) DSDT
 * contains a whole bunch of bogus ACPI I2C devices and is missing entries
 * for the touchscreen and the accelerometer.
 */
static const struct property_entry chuwi_hi8_gsl1680_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1665),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1140),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-chuwi-hi8.fw"),
	{ }
};

static const struct software_node chuwi_hi8_gsl1680_node = {
	.properties = chuwi_hi8_gsl1680_props,
};

static const char * const chuwi_hi8_mount_matrix[] = {
	"1", "0", "0",
	"0", "-1", "0",
	"0", "0", "1"
};

static const struct property_entry chuwi_hi8_bma250e_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", chuwi_hi8_mount_matrix),
	{ }
};

static const struct software_node chuwi_hi8_bma250e_node = {
	.properties = chuwi_hi8_bma250e_props,
};

static const struct x86_i2c_client_info chuwi_hi8_i2c_clients[] __initconst = {
	{
		/* Silead touchscreen */
		.board_info = {
			.type = "gsl1680",
			.addr = 0x40,
			.swnode = &chuwi_hi8_gsl1680_node,
		},
		.adapter_path = "\\_SB_.I2C4",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_APIC,
			.index = 0x44,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
		},
	}, {
		/* BMA250E accelerometer */
		.board_info = {
			.type = "bma250e",
			.addr = 0x18,
			.swnode = &chuwi_hi8_bma250e_node,
		},
		.adapter_path = "\\_SB_.I2C3",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 23,
			.trigger = ACPI_LEVEL_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
		},
	},
};

static const struct x86_dev_info chuwi_hi8_info __initconst = {
	.i2c_client_info = chuwi_hi8_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(chuwi_hi8_i2c_clients),
};

/*
 * If the EFI bootloader is not Xiaomi's own signed Android loader, then the
 * Xiaomi Mi Pad 2 X86 tablet sets OSID in the DSDT to 1 (Windows), causing
 * a bunch of devices to be hidden.
 *
 * This takes care of instantiating the hidden devices manually.
 */
static const char * const bq27520_suppliers[] = { "bq25890-charger" };

static const struct property_entry bq27520_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", bq27520_suppliers),
	{ }
};

static const struct software_node bq27520_node = {
	.properties = bq27520_props,
};

static const struct x86_i2c_client_info xiaomi_mipad2_i2c_clients[] __initconst = {
	{
		/* BQ27520 fuel-gauge */
		.board_info = {
			.type = "bq27520",
			.addr = 0x55,
			.dev_name = "bq27520",
			.swnode = &bq27520_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C1",
	}, {
		/* KTD2026 RGB notification LED controller */
		.board_info = {
			.type = "ktd2026",
			.addr = 0x30,
			.dev_name = "ktd2026",
		},
		.adapter_path = "\\_SB_.PCI0.I2C3",
	},
};

static const struct x86_dev_info xiaomi_mipad2_info __initconst = {
	.i2c_client_info = xiaomi_mipad2_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(xiaomi_mipad2_i2c_clients),
};

static const struct dmi_system_id x86_android_tablet_ids[] __initconst = {
	{
		/* Chuwi Hi8 (CWI509) */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hampoo"),
			DMI_MATCH(DMI_BOARD_NAME, "BYT-PA03C"),
			DMI_MATCH(DMI_SYS_VENDOR, "ilife"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S806"),
		},
		.driver_data = (void *)&chuwi_hi8_info,
	}, {
		/* Xiaomi Mi Pad 2 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Xiaomi Inc"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Mipad2"),
		},
		.driver_data = (void *)&xiaomi_mipad2_info,
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, x86_android_tablet_ids);

static int i2c_client_count;
static struct i2c_client **i2c_clients;

static __init int x86_instantiate_i2c_client(const struct x86_dev_info *dev_info,
					     int idx)
{
	const struct x86_i2c_client_info *client_info = &dev_info->i2c_client_info[idx];
	struct i2c_board_info board_info = client_info->board_info;
	struct i2c_adapter *adap;
	acpi_handle handle;
	acpi_status status;

	board_info.irq = x86_acpi_irq_helper_get(&client_info->irq_data);
	if (board_info.irq < 0)
		return board_info.irq;

	status = acpi_get_handle(NULL, client_info->adapter_path, &handle);
	if (ACPI_FAILURE(status)) {
		pr_err("Error could not get %s handle\n", client_info->adapter_path);
		return -ENODEV;
	}

	adap = i2c_acpi_find_adapter_by_handle(handle);
	if (!adap) {
		pr_err("error could not get %s adapter\n", client_info->adapter_path);
		return -ENODEV;
	}

	i2c_clients[idx] = i2c_new_client_device(adap, &board_info);
	put_device(&adap->dev);
	if (IS_ERR(i2c_clients[idx]))
		return dev_err_probe(&adap->dev, PTR_ERR(i2c_clients[idx]),
				      "creating I2C-client %d\n", idx);

	return 0;
}

static void x86_android_tablet_cleanup(void)
{
	int i;

	for (i = 0; i < i2c_client_count; i++)
		i2c_unregister_device(i2c_clients[i]);

	kfree(i2c_clients);
}

static __init int x86_android_tablet_init(void)
{
	const struct x86_dev_info *dev_info;
	const struct dmi_system_id *id;
	int i, ret = 0;

	id = dmi_first_match(x86_android_tablet_ids);
	if (!id)
		return -ENODEV;

	dev_info = id->driver_data;

	i2c_client_count = dev_info->i2c_client_count;

	i2c_clients = kcalloc(i2c_client_count, sizeof(*i2c_clients), GFP_KERNEL);
	if (!i2c_clients)
		return -ENOMEM;

	for (i = 0; i < dev_info->i2c_client_count; i++) {
		ret = x86_instantiate_i2c_client(dev_info, i);
		if (ret < 0) {
			x86_android_tablet_cleanup();
			break;
		}
	}

	return ret;
}

module_init(x86_android_tablet_init);
module_exit(x86_android_tablet_cleanup);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com");
MODULE_DESCRIPTION("X86 Android tablets DSDT fixups driver");
MODULE_LICENSE("GPL");
