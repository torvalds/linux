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
#include <linux/efi.h>
#include <linux/gpio_keys.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_data/lp855x.h>
#include <linux/platform_device.h>
#include <linux/power/bq24190_charger.h>
#include <linux/reboot.h>
#include <linux/rmi.h>
#include <linux/serdev.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
/* For gpio_get_desc() which is EXPORT_SYMBOL_GPL() */
#include "../../gpio/gpiolib.h"
#include "../../gpio/gpiolib-acpi.h"

/*
 * Helper code to get Linux IRQ numbers given a description of the IRQ source
 * (either IOAPIC index, or GPIO chip name + pin-number).
 */
enum x86_acpi_irq_type {
	X86_ACPI_IRQ_TYPE_NONE,
	X86_ACPI_IRQ_TYPE_APIC,
	X86_ACPI_IRQ_TYPE_GPIOINT,
	X86_ACPI_IRQ_TYPE_PMIC,
};

struct x86_acpi_irq_data {
	char *chip;   /* GPIO chip label (GPIOINT) or PMIC ACPI path (PMIC) */
	enum x86_acpi_irq_type type;
	enum irq_domain_bus_token domain;
	int index;
	int trigger;  /* ACPI_EDGE_SENSITIVE / ACPI_LEVEL_SENSITIVE */
	int polarity; /* ACPI_ACTIVE_HIGH / ACPI_ACTIVE_LOW / ACPI_ACTIVE_BOTH */
};

static int gpiochip_find_match_label(struct gpio_chip *gc, void *data)
{
	return gc->label && !strcmp(gc->label, data);
}

static int x86_android_tablet_get_gpiod(char *label, int pin, struct gpio_desc **desc)
{
	struct gpio_desc *gpiod;
	struct gpio_chip *chip;

	chip = gpiochip_find(label, gpiochip_find_match_label);
	if (!chip) {
		pr_err("error cannot find GPIO chip %s\n", label);
		return -ENODEV;
	}

	gpiod = gpiochip_get_desc(chip, pin);
	if (IS_ERR(gpiod)) {
		pr_err("error %ld getting GPIO %s %d\n", PTR_ERR(gpiod), label, pin);
		return PTR_ERR(gpiod);
	}

	*desc = gpiod;
	return 0;
}

static int x86_acpi_irq_helper_get(const struct x86_acpi_irq_data *data)
{
	struct irq_fwspec fwspec = { };
	struct irq_domain *domain;
	struct acpi_device *adev;
	struct gpio_desc *gpiod;
	unsigned int irq_type;
	acpi_handle handle;
	acpi_status status;
	int irq, ret;

	switch (data->type) {
	case X86_ACPI_IRQ_TYPE_APIC:
		/*
		 * The DSDT may already reference the GSI in a device skipped by
		 * acpi_quirk_skip_i2c_client_enumeration(). Unregister the GSI
		 * to avoid EBUSY errors in this case.
		 */
		acpi_unregister_gsi(data->index);
		irq = acpi_register_gsi(NULL, data->index, data->trigger, data->polarity);
		if (irq < 0)
			pr_err("error %d getting APIC IRQ %d\n", irq, data->index);

		return irq;
	case X86_ACPI_IRQ_TYPE_GPIOINT:
		/* Like acpi_dev_gpio_irq_get(), but without parsing ACPI resources */
		ret = x86_android_tablet_get_gpiod(data->chip, data->index, &gpiod);
		if (ret)
			return ret;

		irq = gpiod_to_irq(gpiod);
		if (irq < 0) {
			pr_err("error %d getting IRQ %s %d\n", irq, data->chip, data->index);
			return irq;
		}

		irq_type = acpi_dev_get_irq_type(data->trigger, data->polarity);
		if (irq_type != IRQ_TYPE_NONE && irq_type != irq_get_trigger_type(irq))
			irq_set_irq_type(irq, irq_type);

		return irq;
	case X86_ACPI_IRQ_TYPE_PMIC:
		status = acpi_get_handle(NULL, data->chip, &handle);
		if (ACPI_FAILURE(status)) {
			pr_err("error could not get %s handle\n", data->chip);
			return -ENODEV;
		}

		adev = acpi_fetch_acpi_dev(handle);
		if (!adev) {
			pr_err("error could not get %s adev\n", data->chip);
			return -ENODEV;
		}

		fwspec.fwnode = acpi_fwnode_handle(adev);
		domain = irq_find_matching_fwspec(&fwspec, data->domain);
		if (!domain) {
			pr_err("error could not find IRQ domain for %s\n", data->chip);
			return -ENODEV;
		}

		return irq_create_mapping(domain, data->index);
	default:
		return 0;
	}
}

struct x86_i2c_client_info {
	struct i2c_board_info board_info;
	char *adapter_path;
	struct x86_acpi_irq_data irq_data;
};

struct x86_serdev_info {
	const char *ctrl_hid;
	const char *ctrl_uid;
	const char *ctrl_devname;
	/*
	 * ATM the serdev core only supports of or ACPI matching; and sofar all
	 * Android x86 tablets DSDTs have usable serdev nodes, but sometimes
	 * under the wrong controller. So we just tie the existing serdev ACPI
	 * node to the right controller.
	 */
	const char *serdev_hid;
};

struct x86_dev_info {
	char *invalid_aei_gpiochip;
	const char * const *modules;
	const struct software_node *bat_swnode;
	struct gpiod_lookup_table * const *gpiod_lookup_tables;
	const struct x86_i2c_client_info *i2c_client_info;
	const struct platform_device_info *pdev_info;
	const struct x86_serdev_info *serdev_info;
	int i2c_client_count;
	int pdev_count;
	int serdev_count;
	int (*init)(void);
	void (*exit)(void);
};

/* Generic / shared charger / battery settings */
static const char * const tusb1211_chg_det_psy[] = { "tusb1211-charger-detect" };
static const char * const bq24190_psy[] = { "bq24190-charger" };
static const char * const bq25890_psy[] = { "bq25890-charger" };

static const struct property_entry fg_bq24190_supply_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", bq24190_psy),
	{ }
};

static const struct software_node fg_bq24190_supply_node = {
	.properties = fg_bq24190_supply_props,
};

static const struct property_entry fg_bq25890_supply_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", bq25890_psy),
	{ }
};

static const struct software_node fg_bq25890_supply_node = {
	.properties = fg_bq25890_supply_props,
};

/* LiPo HighVoltage (max 4.35V) settings used by most devs with a HV bat. */
static const struct property_entry generic_lipo_hv_4v35_battery_props[] = {
	PROPERTY_ENTRY_STRING("compatible", "simple-battery"),
	PROPERTY_ENTRY_STRING("device-chemistry", "lithium-ion"),
	PROPERTY_ENTRY_U32("precharge-current-microamp", 256000),
	PROPERTY_ENTRY_U32("charge-term-current-microamp", 128000),
	PROPERTY_ENTRY_U32("constant-charge-current-max-microamp", 1856000),
	PROPERTY_ENTRY_U32("constant-charge-voltage-max-microvolt", 4352000),
	PROPERTY_ENTRY_U32("factory-internal-resistance-micro-ohms", 150000),
	{ }
};

static const struct software_node generic_lipo_hv_4v35_battery_node = {
	.properties = generic_lipo_hv_4v35_battery_props,
};

/* For enabling the bq24190 5V boost based on id-pin */
static struct regulator_consumer_supply intel_int3496_consumer = {
	.supply = "vbus",
	.dev_name = "intel-int3496",
};

static const struct regulator_init_data bq24190_vbus_init_data = {
	.constraints = {
		.name = "bq24190_vbus",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.consumer_supplies = &intel_int3496_consumer,
	.num_consumer_supplies = 1,
};

static struct bq24190_platform_data bq24190_pdata = {
	.regulator_init_data = &bq24190_vbus_init_data,
};

static const char * const bq24190_modules[] __initconst = {
	"intel_crystal_cove_charger", /* For the bq24190 IRQ */
	"bq24190_charger",            /* For the Vbus regulator for intel-int3496 */
	NULL
};

/* Generic pdevs array and gpio-lookups for micro USB ID pin handling */
static const struct platform_device_info int3496_pdevs[] __initconst = {
	{
		/* For micro USB ID pin handling */
		.name = "intel-int3496",
		.id = PLATFORM_DEVID_NONE,
	},
};

static struct gpiod_lookup_table int3496_gpo2_pin22_gpios = {
	.dev_id = "intel-int3496",
	.table = {
		GPIO_LOOKUP("INT33FC:02", 22, "id", GPIO_ACTIVE_HIGH),
		{ }
	},
};

/* Asus ME176C and TF103C tablets shared data */
static struct gpio_keys_button asus_me176c_tf103c_lid = {
	.code = SW_LID,
	/* .gpio gets filled in by asus_me176c_tf103c_init() */
	.active_low = true,
	.desc = "lid_sw",
	.type = EV_SW,
	.wakeup = true,
	.debounce_interval = 50,
};

static const struct gpio_keys_platform_data asus_me176c_tf103c_lid_pdata __initconst = {
	.buttons = &asus_me176c_tf103c_lid,
	.nbuttons = 1,
	.name = "lid_sw",
};

static const struct platform_device_info asus_me176c_tf103c_pdevs[] __initconst = {
	{
		.name = "gpio-keys",
		.id = PLATFORM_DEVID_AUTO,
		.data = &asus_me176c_tf103c_lid_pdata,
		.size_data = sizeof(asus_me176c_tf103c_lid_pdata),
	},
	{
		/* For micro USB ID pin handling */
		.name = "intel-int3496",
		.id = PLATFORM_DEVID_NONE,
	},
};

static int __init asus_me176c_tf103c_init(void)
{
	struct gpio_desc *gpiod;
	int ret;

	ret = x86_android_tablet_get_gpiod("INT33FC:02", 12, &gpiod);
	if (ret < 0)
		return ret;
	asus_me176c_tf103c_lid.gpio = desc_to_gpio(gpiod);

	return 0;
}


/* Asus ME176C tablets have an Android factory img with everything hardcoded */
static const char * const asus_me176c_accel_mount_matrix[] = {
	"-1", "0", "0",
	"0", "1", "0",
	"0", "0", "1"
};

static const struct property_entry asus_me176c_accel_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", asus_me176c_accel_mount_matrix),
	{ }
};

static const struct software_node asus_me176c_accel_node = {
	.properties = asus_me176c_accel_props,
};

static const struct property_entry asus_me176c_bq24190_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", tusb1211_chg_det_psy),
	PROPERTY_ENTRY_REF("monitored-battery", &generic_lipo_hv_4v35_battery_node),
	PROPERTY_ENTRY_U32("ti,system-minimum-microvolt", 3600000),
	PROPERTY_ENTRY_BOOL("omit-battery-class"),
	PROPERTY_ENTRY_BOOL("disable-reset"),
	{ }
};

static const struct software_node asus_me176c_bq24190_node = {
	.properties = asus_me176c_bq24190_props,
};

static const struct property_entry asus_me176c_ug3105_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", bq24190_psy),
	PROPERTY_ENTRY_REF("monitored-battery", &generic_lipo_hv_4v35_battery_node),
	PROPERTY_ENTRY_U32("upisemi,rsns-microohm", 10000),
	{ }
};

static const struct software_node asus_me176c_ug3105_node = {
	.properties = asus_me176c_ug3105_props,
};

static const struct x86_i2c_client_info asus_me176c_i2c_clients[] __initconst = {
	{
		/* bq24297 battery charger */
		.board_info = {
			.type = "bq24190",
			.addr = 0x6b,
			.dev_name = "bq24297",
			.swnode = &asus_me176c_bq24190_node,
			.platform_data = &bq24190_pdata,
		},
		.adapter_path = "\\_SB_.I2C1",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_PMIC,
			.chip = "\\_SB_.I2C7.PMIC",
			.domain = DOMAIN_BUS_WAKEUP,
			.index = 0,
		},
	}, {
		/* ug3105 battery monitor */
		.board_info = {
			.type = "ug3105",
			.addr = 0x70,
			.dev_name = "ug3105",
			.swnode = &asus_me176c_ug3105_node,
		},
		.adapter_path = "\\_SB_.I2C1",
	}, {
		/* ak09911 compass */
		.board_info = {
			.type = "ak09911",
			.addr = 0x0c,
			.dev_name = "ak09911",
		},
		.adapter_path = "\\_SB_.I2C5",
	}, {
		/* kxtj21009 accel */
		.board_info = {
			.type = "kxtj21009",
			.addr = 0x0f,
			.dev_name = "kxtj21009",
			.swnode = &asus_me176c_accel_node,
		},
		.adapter_path = "\\_SB_.I2C5",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_APIC,
			.index = 0x44,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
		},
	}, {
		/* goodix touchscreen */
		.board_info = {
			.type = "GDIX1001:00",
			.addr = 0x14,
			.dev_name = "goodix_ts",
		},
		.adapter_path = "\\_SB_.I2C6",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_APIC,
			.index = 0x45,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
		},
	},
};

static const struct x86_serdev_info asus_me176c_serdevs[] __initconst = {
	{
		.ctrl_hid = "80860F0A",
		.ctrl_uid = "2",
		.ctrl_devname = "serial0",
		.serdev_hid = "BCM2E3A",
	},
};

static struct gpiod_lookup_table asus_me176c_goodix_gpios = {
	.dev_id = "i2c-goodix_ts",
	.table = {
		GPIO_LOOKUP("INT33FC:00", 60, "reset", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FC:02", 28, "irq", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct gpiod_lookup_table * const asus_me176c_gpios[] = {
	&int3496_gpo2_pin22_gpios,
	&asus_me176c_goodix_gpios,
	NULL
};

static const struct x86_dev_info asus_me176c_info __initconst = {
	.i2c_client_info = asus_me176c_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(asus_me176c_i2c_clients),
	.pdev_info = asus_me176c_tf103c_pdevs,
	.pdev_count = ARRAY_SIZE(asus_me176c_tf103c_pdevs),
	.serdev_info = asus_me176c_serdevs,
	.serdev_count = ARRAY_SIZE(asus_me176c_serdevs),
	.gpiod_lookup_tables = asus_me176c_gpios,
	.bat_swnode = &generic_lipo_hv_4v35_battery_node,
	.modules = bq24190_modules,
	.invalid_aei_gpiochip = "INT33FC:02",
	.init = asus_me176c_tf103c_init,
};

/* Asus TF103C tablets have an Android factory img with everything hardcoded */
static const char * const asus_tf103c_accel_mount_matrix[] = {
	"0", "-1", "0",
	"-1", "0", "0",
	"0", "0", "1"
};

static const struct property_entry asus_tf103c_accel_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", asus_tf103c_accel_mount_matrix),
	{ }
};

static const struct software_node asus_tf103c_accel_node = {
	.properties = asus_tf103c_accel_props,
};

static const struct property_entry asus_tf103c_touchscreen_props[] = {
	PROPERTY_ENTRY_STRING("compatible", "atmel,atmel_mxt_ts"),
	{ }
};

static const struct software_node asus_tf103c_touchscreen_node = {
	.properties = asus_tf103c_touchscreen_props,
};

static const struct property_entry asus_tf103c_battery_props[] = {
	PROPERTY_ENTRY_STRING("compatible", "simple-battery"),
	PROPERTY_ENTRY_STRING("device-chemistry", "lithium-ion-polymer"),
	PROPERTY_ENTRY_U32("precharge-current-microamp", 256000),
	PROPERTY_ENTRY_U32("charge-term-current-microamp", 128000),
	PROPERTY_ENTRY_U32("constant-charge-current-max-microamp", 2048000),
	PROPERTY_ENTRY_U32("constant-charge-voltage-max-microvolt", 4208000),
	PROPERTY_ENTRY_U32("factory-internal-resistance-micro-ohms", 150000),
	{ }
};

static const struct software_node asus_tf103c_battery_node = {
	.properties = asus_tf103c_battery_props,
};

static const struct property_entry asus_tf103c_bq24190_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", tusb1211_chg_det_psy),
	PROPERTY_ENTRY_REF("monitored-battery", &asus_tf103c_battery_node),
	PROPERTY_ENTRY_U32("ti,system-minimum-microvolt", 3600000),
	PROPERTY_ENTRY_BOOL("omit-battery-class"),
	PROPERTY_ENTRY_BOOL("disable-reset"),
	{ }
};

static const struct software_node asus_tf103c_bq24190_node = {
	.properties = asus_tf103c_bq24190_props,
};

static const struct property_entry asus_tf103c_ug3105_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", bq24190_psy),
	PROPERTY_ENTRY_REF("monitored-battery", &asus_tf103c_battery_node),
	PROPERTY_ENTRY_U32("upisemi,rsns-microohm", 5000),
	{ }
};

static const struct software_node asus_tf103c_ug3105_node = {
	.properties = asus_tf103c_ug3105_props,
};

static const struct x86_i2c_client_info asus_tf103c_i2c_clients[] __initconst = {
	{
		/* bq24297 battery charger */
		.board_info = {
			.type = "bq24190",
			.addr = 0x6b,
			.dev_name = "bq24297",
			.swnode = &asus_tf103c_bq24190_node,
			.platform_data = &bq24190_pdata,
		},
		.adapter_path = "\\_SB_.I2C1",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_PMIC,
			.chip = "\\_SB_.I2C7.PMIC",
			.domain = DOMAIN_BUS_WAKEUP,
			.index = 0,
		},
	}, {
		/* ug3105 battery monitor */
		.board_info = {
			.type = "ug3105",
			.addr = 0x70,
			.dev_name = "ug3105",
			.swnode = &asus_tf103c_ug3105_node,
		},
		.adapter_path = "\\_SB_.I2C1",
	}, {
		/* ak09911 compass */
		.board_info = {
			.type = "ak09911",
			.addr = 0x0c,
			.dev_name = "ak09911",
		},
		.adapter_path = "\\_SB_.I2C5",
	}, {
		/* kxtj21009 accel */
		.board_info = {
			.type = "kxtj21009",
			.addr = 0x0f,
			.dev_name = "kxtj21009",
			.swnode = &asus_tf103c_accel_node,
		},
		.adapter_path = "\\_SB_.I2C5",
	}, {
		/* atmel touchscreen */
		.board_info = {
			.type = "atmel_mxt_ts",
			.addr = 0x4a,
			.dev_name = "atmel_mxt_ts",
			.swnode = &asus_tf103c_touchscreen_node,
		},
		.adapter_path = "\\_SB_.I2C6",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 28,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
		},
	},
};

static struct gpiod_lookup_table * const asus_tf103c_gpios[] = {
	&int3496_gpo2_pin22_gpios,
	NULL
};

static const struct x86_dev_info asus_tf103c_info __initconst = {
	.i2c_client_info = asus_tf103c_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(asus_tf103c_i2c_clients),
	.pdev_info = asus_me176c_tf103c_pdevs,
	.pdev_count = ARRAY_SIZE(asus_me176c_tf103c_pdevs),
	.gpiod_lookup_tables = asus_tf103c_gpios,
	.bat_swnode = &asus_tf103c_battery_node,
	.modules = bq24190_modules,
	.invalid_aei_gpiochip = "INT33FC:02",
	.init = asus_me176c_tf103c_init,
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

static int __init chuwi_hi8_init(void)
{
	/*
	 * Avoid the acpi_unregister_gsi() call in x86_acpi_irq_helper_get()
	 * breaking the touchscreen + logging various errors when the Windows
	 * BIOS is used.
	 */
	if (acpi_dev_present("MSSL0001", NULL, 1))
		return -ENODEV;

	return 0;
}

static const struct x86_dev_info chuwi_hi8_info __initconst = {
	.i2c_client_info = chuwi_hi8_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(chuwi_hi8_i2c_clients),
	.init = chuwi_hi8_init,
};

#define CZC_EC_EXTRA_PORT	0x68
#define CZC_EC_ANDROID_KEYS	0x63

static int __init czc_p10t_init(void)
{
	/*
	 * The device boots up in "Windows 7" mode, when the home button sends a
	 * Windows specific key sequence (Left Meta + D) and the second button
	 * sends an unknown one while also toggling the Radio Kill Switch.
	 * This is a surprising behavior when the second button is labeled "Back".
	 *
	 * The vendor-supplied Android-x86 build switches the device to a "Android"
	 * mode by writing value 0x63 to the I/O port 0x68. This just seems to just
	 * set bit 6 on address 0x96 in the EC region; switching the bit directly
	 * seems to achieve the same result. It uses a "p10t_switcher" to do the
	 * job. It doesn't seem to be able to do anything else, and no other use
	 * of the port 0x68 is known.
	 *
	 * In the Android mode, the home button sends just a single scancode,
	 * which can be handled in Linux userspace more reasonably and the back
	 * button only sends a scancode without toggling the kill switch.
	 * The scancode can then be mapped either to Back or RF Kill functionality
	 * in userspace, depending on how the button is labeled on that particular
	 * model.
	 */
	outb(CZC_EC_ANDROID_KEYS, CZC_EC_EXTRA_PORT);
	return 0;
}

static const struct x86_dev_info czc_p10t __initconst = {
	.init = czc_p10t_init,
};

/* Lenovo Yoga Book X90F / X91F / X91L need manual instantiation of the fg client */
static const struct x86_i2c_client_info lenovo_yogabook_x9x_i2c_clients[] __initconst = {
	{
		/* BQ27542 fuel-gauge */
		.board_info = {
			.type = "bq27542",
			.addr = 0x55,
			.dev_name = "bq27542",
			.swnode = &fg_bq25890_supply_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C1",
	},
};

static const struct x86_dev_info lenovo_yogabook_x9x_info __initconst = {
	.i2c_client_info = lenovo_yogabook_x9x_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(lenovo_yogabook_x9x_i2c_clients),
};

/* Lenovo Yoga Tablet 2 1050F/L's Android factory img has everything hardcoded */
static const struct property_entry lenovo_yoga_tab2_830_1050_bq24190_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", tusb1211_chg_det_psy),
	PROPERTY_ENTRY_REF("monitored-battery", &generic_lipo_hv_4v35_battery_node),
	PROPERTY_ENTRY_BOOL("omit-battery-class"),
	PROPERTY_ENTRY_BOOL("disable-reset"),
	{ }
};

static const struct software_node lenovo_yoga_tab2_830_1050_bq24190_node = {
	.properties = lenovo_yoga_tab2_830_1050_bq24190_props,
};

/* This gets filled by lenovo_yoga_tab2_830_1050_init() */
static struct rmi_device_platform_data lenovo_yoga_tab2_830_1050_rmi_pdata = { };

static struct lp855x_platform_data lenovo_yoga_tab2_830_1050_lp8557_pdata = {
	.device_control = 0x86,
	.initial_brightness = 128,
};

static const struct x86_i2c_client_info lenovo_yoga_tab2_830_1050_i2c_clients[] __initconst = {
	{
		/* bq24292i battery charger */
		.board_info = {
			.type = "bq24190",
			.addr = 0x6b,
			.dev_name = "bq24292i",
			.swnode = &lenovo_yoga_tab2_830_1050_bq24190_node,
			.platform_data = &bq24190_pdata,
		},
		.adapter_path = "\\_SB_.I2C1",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 2,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
		},
	}, {
		/* BQ27541 fuel-gauge */
		.board_info = {
			.type = "bq27541",
			.addr = 0x55,
			.dev_name = "bq27541",
			.swnode = &fg_bq24190_supply_node,
		},
		.adapter_path = "\\_SB_.I2C1",
	}, {
		/* Synaptics RMI touchscreen */
		.board_info = {
			.type = "rmi4_i2c",
			.addr = 0x38,
			.dev_name = "rmi4_i2c",
			.platform_data = &lenovo_yoga_tab2_830_1050_rmi_pdata,
		},
		.adapter_path = "\\_SB_.I2C6",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_APIC,
			.index = 0x45,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
		},
	}, {
		/* LP8557 Backlight controller */
		.board_info = {
			.type = "lp8557",
			.addr = 0x2c,
			.dev_name = "lp8557",
			.platform_data = &lenovo_yoga_tab2_830_1050_lp8557_pdata,
		},
		.adapter_path = "\\_SB_.I2C3",
	},
};

static struct gpiod_lookup_table lenovo_yoga_tab2_830_1050_int3496_gpios = {
	.dev_id = "intel-int3496",
	.table = {
		GPIO_LOOKUP("INT33FC:02", 1, "mux", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("INT33FC:02", 24, "id", GPIO_ACTIVE_HIGH),
		{ }
	},
};

#define LENOVO_YOGA_TAB2_830_1050_CODEC_NAME "spi-10WM5102:00"

static struct gpiod_lookup_table lenovo_yoga_tab2_830_1050_codec_gpios = {
	.dev_id = LENOVO_YOGA_TAB2_830_1050_CODEC_NAME,
	.table = {
		GPIO_LOOKUP("gpio_crystalcove", 3, "reset", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FC:01", 23, "wlf,ldoena", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("arizona", 2, "wlf,spkvdd-ena", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("arizona", 4, "wlf,micd-pol", GPIO_ACTIVE_LOW),
		{ }
	},
};

static struct gpiod_lookup_table * const lenovo_yoga_tab2_830_1050_gpios[] = {
	&lenovo_yoga_tab2_830_1050_int3496_gpios,
	&lenovo_yoga_tab2_830_1050_codec_gpios,
	NULL
};

static int __init lenovo_yoga_tab2_830_1050_init(void);
static void lenovo_yoga_tab2_830_1050_exit(void);

static struct x86_dev_info lenovo_yoga_tab2_830_1050_info __initdata = {
	.i2c_client_info = lenovo_yoga_tab2_830_1050_i2c_clients,
	/* i2c_client_count gets set by lenovo_yoga_tab2_830_1050_init() */
	.pdev_info = int3496_pdevs,
	.pdev_count = ARRAY_SIZE(int3496_pdevs),
	.gpiod_lookup_tables = lenovo_yoga_tab2_830_1050_gpios,
	.bat_swnode = &generic_lipo_hv_4v35_battery_node,
	.modules = bq24190_modules,
	.invalid_aei_gpiochip = "INT33FC:02",
	.init = lenovo_yoga_tab2_830_1050_init,
	.exit = lenovo_yoga_tab2_830_1050_exit,
};

/*
 * The Lenovo Yoga Tablet 2 830 and 1050 (8" vs 10") versions use the same
 * mainboard, but they need some different treatment related to the display:
 * 1. The 830 uses a portrait LCD panel with a landscape touchscreen, requiring
 *    the touchscreen driver to adjust the touch-coords to match the LCD.
 * 2. Both use an TI LP8557 LED backlight controller. On the 1050 the LP8557's
 *    PWM input is connected to the PMIC's PWM output and everything works fine
 *    with the defaults programmed into the LP8557 by the BIOS.
 *    But on the 830 the LP8557's PWM input is connected to a PWM output coming
 *    from the LCD panel's controller. The Android code has a hack in the i915
 *    driver to write the non-standard DSI reg 0x9f with the desired backlight
 *    level to set the duty-cycle of the LCD's PWM output.
 *
 *    To avoid having to have a similar hack in the mainline kernel the LP8557
 *    entry in lenovo_yoga_tab2_830_1050_i2c_clients instead just programs the
 *    LP8557 to directly set the level, ignoring the PWM input. This means that
 *    the LP8557 i2c_client should only be instantiated on the 830.
 */
static int __init lenovo_yoga_tab2_830_1050_init_display(void)
{
	struct gpio_desc *gpiod;
	int ret;

	/* Use PMIC GPIO 10 bootstrap pin to differentiate 830 vs 1050 */
	ret = x86_android_tablet_get_gpiod("gpio_crystalcove", 10, &gpiod);
	if (ret)
		return ret;

	ret = gpiod_get_value_cansleep(gpiod);
	if (ret) {
		pr_info("detected Lenovo Yoga Tablet 2 1050F/L\n");
		lenovo_yoga_tab2_830_1050_info.i2c_client_count =
			ARRAY_SIZE(lenovo_yoga_tab2_830_1050_i2c_clients) - 1;
	} else {
		pr_info("detected Lenovo Yoga Tablet 2 830F/L\n");
		lenovo_yoga_tab2_830_1050_rmi_pdata.sensor_pdata.axis_align.swap_axes = true;
		lenovo_yoga_tab2_830_1050_rmi_pdata.sensor_pdata.axis_align.flip_y = true;
		lenovo_yoga_tab2_830_1050_info.i2c_client_count =
			ARRAY_SIZE(lenovo_yoga_tab2_830_1050_i2c_clients);
	}

	return 0;
}

/* SUS (INT33FC:02) pin 6 needs to be configured as pmu_clk for the audio codec */
static const struct pinctrl_map lenovo_yoga_tab2_830_1050_codec_pinctrl_map =
	PIN_MAP_MUX_GROUP(LENOVO_YOGA_TAB2_830_1050_CODEC_NAME, "codec_32khz_clk",
			  "INT33FC:02", "pmu_clk2_grp", "pmu_clk");

static struct pinctrl *lenovo_yoga_tab2_830_1050_codec_pinctrl;
static struct sys_off_handler *lenovo_yoga_tab2_830_1050_sys_off_handler;

static int __init lenovo_yoga_tab2_830_1050_init_codec(void)
{
	struct device *codec_dev;
	struct pinctrl *pinctrl;
	int ret;

	codec_dev = bus_find_device_by_name(&spi_bus_type, NULL,
					    LENOVO_YOGA_TAB2_830_1050_CODEC_NAME);
	if (!codec_dev) {
		pr_err("error cannot find %s device\n", LENOVO_YOGA_TAB2_830_1050_CODEC_NAME);
		return -ENODEV;
	}

	ret = pinctrl_register_mappings(&lenovo_yoga_tab2_830_1050_codec_pinctrl_map, 1);
	if (ret)
		goto err_put_device;

	pinctrl = pinctrl_get_select(codec_dev, "codec_32khz_clk");
	if (IS_ERR(pinctrl)) {
		ret = dev_err_probe(codec_dev, PTR_ERR(pinctrl), "selecting codec_32khz_clk\n");
		goto err_unregister_mappings;
	}

	/* We're done with the codec_dev now */
	put_device(codec_dev);

	lenovo_yoga_tab2_830_1050_codec_pinctrl = pinctrl;
	return 0;

err_unregister_mappings:
	pinctrl_unregister_mappings(&lenovo_yoga_tab2_830_1050_codec_pinctrl_map);
err_put_device:
	put_device(codec_dev);
	return ret;
}

/*
 * These tablet's DSDT does not set acpi_gbl_reduced_hardware, so acpi_power_off
 * gets used as pm_power_off handler. This causes "poweroff" on these tablets
 * to hang hard. Requiring pressing the powerbutton for 30 seconds *twice*
 * followed by a normal 3 second press to recover. Avoid this by doing an EFI
 * poweroff instead.
 */
static int lenovo_yoga_tab2_830_1050_power_off(struct sys_off_data *data)
{
	efi.reset_system(EFI_RESET_SHUTDOWN, EFI_SUCCESS, 0, NULL);

	return NOTIFY_DONE;
}

static int __init lenovo_yoga_tab2_830_1050_init(void)
{
	int ret;

	ret = lenovo_yoga_tab2_830_1050_init_display();
	if (ret)
		return ret;

	ret = lenovo_yoga_tab2_830_1050_init_codec();
	if (ret)
		return ret;

	/* SYS_OFF_PRIO_FIRMWARE + 1 so that it runs before acpi_power_off */
	lenovo_yoga_tab2_830_1050_sys_off_handler =
		register_sys_off_handler(SYS_OFF_MODE_POWER_OFF, SYS_OFF_PRIO_FIRMWARE + 1,
					 lenovo_yoga_tab2_830_1050_power_off, NULL);
	if (IS_ERR(lenovo_yoga_tab2_830_1050_sys_off_handler))
		return PTR_ERR(lenovo_yoga_tab2_830_1050_sys_off_handler);

	return 0;
}

static void lenovo_yoga_tab2_830_1050_exit(void)
{
	unregister_sys_off_handler(lenovo_yoga_tab2_830_1050_sys_off_handler);

	if (lenovo_yoga_tab2_830_1050_codec_pinctrl) {
		pinctrl_put(lenovo_yoga_tab2_830_1050_codec_pinctrl);
		pinctrl_unregister_mappings(&lenovo_yoga_tab2_830_1050_codec_pinctrl_map);
	}
}

/* Nextbook Ares 8 tablets have an Android factory img with everything hardcoded */
static const char * const nextbook_ares8_accel_mount_matrix[] = {
	"0", "-1", "0",
	"-1", "0", "0",
	"0", "0", "1"
};

static const struct property_entry nextbook_ares8_accel_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", nextbook_ares8_accel_mount_matrix),
	{ }
};

static const struct software_node nextbook_ares8_accel_node = {
	.properties = nextbook_ares8_accel_props,
};

static const struct property_entry nextbook_ares8_touchscreen_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 800),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1280),
	{ }
};

static const struct software_node nextbook_ares8_touchscreen_node = {
	.properties = nextbook_ares8_touchscreen_props,
};

static const struct x86_i2c_client_info nextbook_ares8_i2c_clients[] __initconst = {
	{
		/* Freescale MMA8653FC accel */
		.board_info = {
			.type = "mma8653",
			.addr = 0x1d,
			.dev_name = "mma8653",
			.swnode = &nextbook_ares8_accel_node,
		},
		.adapter_path = "\\_SB_.I2C3",
	}, {
		/* FT5416DQ9 touchscreen controller */
		.board_info = {
			.type = "edt-ft5x06",
			.addr = 0x38,
			.dev_name = "ft5416",
			.swnode = &nextbook_ares8_touchscreen_node,
		},
		.adapter_path = "\\_SB_.I2C4",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 3,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
		},
	},
};

static struct gpiod_lookup_table nextbook_ares8_int3496_gpios = {
	.dev_id = "intel-int3496",
	.table = {
		GPIO_LOOKUP("INT33FC:02", 1, "mux", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FC:02", 18, "id", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct gpiod_lookup_table * const nextbook_ares8_gpios[] = {
	&nextbook_ares8_int3496_gpios,
	NULL
};

static const struct x86_dev_info nextbook_ares8_info __initconst = {
	.i2c_client_info = nextbook_ares8_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(nextbook_ares8_i2c_clients),
	.pdev_info = int3496_pdevs,
	.pdev_count = ARRAY_SIZE(int3496_pdevs),
	.gpiod_lookup_tables = nextbook_ares8_gpios,
	.invalid_aei_gpiochip = "INT33FC:02",
};

/*
 * Whitelabel (sold as various brands) TM800A550L tablets.
 * These tablet's DSDT contains a whole bunch of bogus ACPI I2C devices
 * (removed through acpi_quirk_skip_i2c_client_enumeration()) and
 * the touchscreen fwnode has the wrong GPIOs.
 */
static const char * const whitelabel_tm800a550l_accel_mount_matrix[] = {
	"-1", "0", "0",
	"0", "1", "0",
	"0", "0", "1"
};

static const struct property_entry whitelabel_tm800a550l_accel_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", whitelabel_tm800a550l_accel_mount_matrix),
	{ }
};

static const struct software_node whitelabel_tm800a550l_accel_node = {
	.properties = whitelabel_tm800a550l_accel_props,
};

static const struct property_entry whitelabel_tm800a550l_goodix_props[] = {
	PROPERTY_ENTRY_STRING("firmware-name", "gt912-tm800a550l.fw"),
	PROPERTY_ENTRY_STRING("goodix,config-name", "gt912-tm800a550l.cfg"),
	PROPERTY_ENTRY_U32("goodix,main-clk", 54),
	{ }
};

static const struct software_node whitelabel_tm800a550l_goodix_node = {
	.properties = whitelabel_tm800a550l_goodix_props,
};

static const struct x86_i2c_client_info whitelabel_tm800a550l_i2c_clients[] __initconst = {
	{
		/* goodix touchscreen */
		.board_info = {
			.type = "GDIX1001:00",
			.addr = 0x14,
			.dev_name = "goodix_ts",
			.swnode = &whitelabel_tm800a550l_goodix_node,
		},
		.adapter_path = "\\_SB_.I2C2",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_APIC,
			.index = 0x44,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
		},
	}, {
		/* kxcj91008 accel */
		.board_info = {
			.type = "kxcj91008",
			.addr = 0x0f,
			.dev_name = "kxcj91008",
			.swnode = &whitelabel_tm800a550l_accel_node,
		},
		.adapter_path = "\\_SB_.I2C3",
	},
};

static struct gpiod_lookup_table whitelabel_tm800a550l_goodix_gpios = {
	.dev_id = "i2c-goodix_ts",
	.table = {
		GPIO_LOOKUP("INT33FC:01", 26, "reset", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FC:02", 3, "irq", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct gpiod_lookup_table * const whitelabel_tm800a550l_gpios[] = {
	&whitelabel_tm800a550l_goodix_gpios,
	NULL
};

static const struct x86_dev_info whitelabel_tm800a550l_info __initconst = {
	.i2c_client_info = whitelabel_tm800a550l_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(whitelabel_tm800a550l_i2c_clients),
	.gpiod_lookup_tables = whitelabel_tm800a550l_gpios,
};

/*
 * If the EFI bootloader is not Xiaomi's own signed Android loader, then the
 * Xiaomi Mi Pad 2 X86 tablet sets OSID in the DSDT to 1 (Windows), causing
 * a bunch of devices to be hidden.
 *
 * This takes care of instantiating the hidden devices manually.
 */
static const struct x86_i2c_client_info xiaomi_mipad2_i2c_clients[] __initconst = {
	{
		/* BQ27520 fuel-gauge */
		.board_info = {
			.type = "bq27520",
			.addr = 0x55,
			.dev_name = "bq27520",
			.swnode = &fg_bq25890_supply_node,
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
		/* Asus MeMO Pad 7 ME176C */
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "ME176C"),
		},
		.driver_data = (void *)&asus_me176c_info,
	},
	{
		/* Asus TF103C */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "TF103C"),
		},
		.driver_data = (void *)&asus_tf103c_info,
	},
	{
		/* Chuwi Hi8 (CWI509) */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hampoo"),
			DMI_MATCH(DMI_BOARD_NAME, "BYT-PA03C"),
			DMI_MATCH(DMI_SYS_VENDOR, "ilife"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S806"),
		},
		.driver_data = (void *)&chuwi_hi8_info,
	},
	{
		/* CZC P10T */
		.ident = "CZC ODEON TPC-10 (\"P10T\")",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "CZC"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ODEON*TPC-10"),
		},
		.driver_data = (void *)&czc_p10t,
	},
	{
		/* CZC P10T variant */
		.ident = "ViewSonic ViewPad 10",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ViewSonic"),
			DMI_MATCH(DMI_PRODUCT_NAME, "VPAD10"),
		},
		.driver_data = (void *)&czc_p10t,
	},
	{
		/* Lenovo Yoga Book X90F / X91F / X91L */
		.matches = {
			/* Non exact match to match all versions */
			DMI_MATCH(DMI_PRODUCT_NAME, "Lenovo YB1-X9"),
		},
		.driver_data = (void *)&lenovo_yogabook_x9x_info,
	},
	{
		/*
		 * Lenovo Yoga Tablet 2 830F/L or 1050F/L (The 8" and 10"
		 * Lenovo Yoga Tablet 2 use the same mainboard)
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corp."),
			DMI_MATCH(DMI_PRODUCT_NAME, "VALLEYVIEW C0 PLATFORM"),
			DMI_MATCH(DMI_BOARD_NAME, "BYT-T FFD8"),
			/* Partial match on beginning of BIOS version */
			DMI_MATCH(DMI_BIOS_VERSION, "BLADE_21"),
		},
		.driver_data = (void *)&lenovo_yoga_tab2_830_1050_info,
	},
	{
		/* Nextbook Ares 8 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "M890BAP"),
		},
		.driver_data = (void *)&nextbook_ares8_info,
	},
	{
		/* Whitelabel (sold as various brands) TM800A550L */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "Aptio CRB"),
			/* Above strings are too generic, also match on BIOS version */
			DMI_MATCH(DMI_BIOS_VERSION, "ZY-8-BI-PX4S70VTR400-X423B-005-D"),
		},
		.driver_data = (void *)&whitelabel_tm800a550l_info,
	},
	{
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
static int pdev_count;
static int serdev_count;
static struct i2c_client **i2c_clients;
static struct platform_device **pdevs;
static struct serdev_device **serdevs;
static struct gpiod_lookup_table * const *gpiod_lookup_tables;
static const struct software_node *bat_swnode;
static void (*exit_handler)(void);

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

static __init int x86_instantiate_serdev(const struct x86_serdev_info *info, int idx)
{
	struct acpi_device *ctrl_adev, *serdev_adev;
	struct serdev_device *serdev;
	struct device *ctrl_dev;
	int ret = -ENODEV;

	ctrl_adev = acpi_dev_get_first_match_dev(info->ctrl_hid, info->ctrl_uid, -1);
	if (!ctrl_adev) {
		pr_err("error could not get %s/%s ctrl adev\n",
		       info->ctrl_hid, info->ctrl_uid);
		return -ENODEV;
	}

	serdev_adev = acpi_dev_get_first_match_dev(info->serdev_hid, NULL, -1);
	if (!serdev_adev) {
		pr_err("error could not get %s serdev adev\n", info->serdev_hid);
		goto put_ctrl_adev;
	}

	/* get_first_physical_node() returns a weak ref, no need to put() it */
	ctrl_dev = acpi_get_first_physical_node(ctrl_adev);
	if (!ctrl_dev)	{
		pr_err("error could not get %s/%s ctrl physical dev\n",
		       info->ctrl_hid, info->ctrl_uid);
		goto put_serdev_adev;
	}

	/* ctrl_dev now points to the controller's parent, get the controller */
	ctrl_dev = device_find_child_by_name(ctrl_dev, info->ctrl_devname);
	if (!ctrl_dev) {
		pr_err("error could not get %s/%s %s ctrl dev\n",
		       info->ctrl_hid, info->ctrl_uid, info->ctrl_devname);
		goto put_serdev_adev;
	}

	serdev = serdev_device_alloc(to_serdev_controller(ctrl_dev));
	if (!serdev) {
		ret = -ENOMEM;
		goto put_serdev_adev;
	}

	ACPI_COMPANION_SET(&serdev->dev, serdev_adev);
	acpi_device_set_enumerated(serdev_adev);

	ret = serdev_device_add(serdev);
	if (ret) {
		dev_err(&serdev->dev, "error %d adding serdev\n", ret);
		serdev_device_put(serdev);
		goto put_serdev_adev;
	}

	serdevs[idx] = serdev;

put_serdev_adev:
	acpi_dev_put(serdev_adev);
put_ctrl_adev:
	acpi_dev_put(ctrl_adev);
	return ret;
}

static void x86_android_tablet_cleanup(void)
{
	int i;

	for (i = 0; i < serdev_count; i++) {
		if (serdevs[i])
			serdev_device_remove(serdevs[i]);
	}

	kfree(serdevs);

	for (i = 0; i < pdev_count; i++)
		platform_device_unregister(pdevs[i]);

	kfree(pdevs);

	for (i = 0; i < i2c_client_count; i++)
		i2c_unregister_device(i2c_clients[i]);

	kfree(i2c_clients);

	if (exit_handler)
		exit_handler();

	for (i = 0; gpiod_lookup_tables && gpiod_lookup_tables[i]; i++)
		gpiod_remove_lookup_table(gpiod_lookup_tables[i]);

	software_node_unregister(bat_swnode);
}

static __init int x86_android_tablet_init(void)
{
	const struct x86_dev_info *dev_info;
	const struct dmi_system_id *id;
	struct gpio_chip *chip;
	int i, ret = 0;

	id = dmi_first_match(x86_android_tablet_ids);
	if (!id)
		return -ENODEV;

	dev_info = id->driver_data;

	/*
	 * The broken DSDTs on these devices often also include broken
	 * _AEI (ACPI Event Interrupt) handlers, disable these.
	 */
	if (dev_info->invalid_aei_gpiochip) {
		chip = gpiochip_find(dev_info->invalid_aei_gpiochip,
				     gpiochip_find_match_label);
		if (!chip) {
			pr_err("error cannot find GPIO chip %s\n", dev_info->invalid_aei_gpiochip);
			return -ENODEV;
		}
		acpi_gpiochip_free_interrupts(chip);
	}

	/*
	 * Since this runs from module_init() it cannot use -EPROBE_DEFER,
	 * instead pre-load any modules which are listed as requirements.
	 */
	for (i = 0; dev_info->modules && dev_info->modules[i]; i++)
		request_module(dev_info->modules[i]);

	bat_swnode = dev_info->bat_swnode;
	if (bat_swnode) {
		ret = software_node_register(bat_swnode);
		if (ret)
			return ret;
	}

	gpiod_lookup_tables = dev_info->gpiod_lookup_tables;
	for (i = 0; gpiod_lookup_tables && gpiod_lookup_tables[i]; i++)
		gpiod_add_lookup_table(gpiod_lookup_tables[i]);

	if (dev_info->init) {
		ret = dev_info->init();
		if (ret < 0) {
			x86_android_tablet_cleanup();
			return ret;
		}
		exit_handler = dev_info->exit;
	}

	i2c_clients = kcalloc(dev_info->i2c_client_count, sizeof(*i2c_clients), GFP_KERNEL);
	if (!i2c_clients) {
		x86_android_tablet_cleanup();
		return -ENOMEM;
	}

	i2c_client_count = dev_info->i2c_client_count;
	for (i = 0; i < i2c_client_count; i++) {
		ret = x86_instantiate_i2c_client(dev_info, i);
		if (ret < 0) {
			x86_android_tablet_cleanup();
			return ret;
		}
	}

	pdevs = kcalloc(dev_info->pdev_count, sizeof(*pdevs), GFP_KERNEL);
	if (!pdevs) {
		x86_android_tablet_cleanup();
		return -ENOMEM;
	}

	pdev_count = dev_info->pdev_count;
	for (i = 0; i < pdev_count; i++) {
		pdevs[i] = platform_device_register_full(&dev_info->pdev_info[i]);
		if (IS_ERR(pdevs[i])) {
			x86_android_tablet_cleanup();
			return PTR_ERR(pdevs[i]);
		}
	}

	serdevs = kcalloc(dev_info->serdev_count, sizeof(*serdevs), GFP_KERNEL);
	if (!serdevs) {
		x86_android_tablet_cleanup();
		return -ENOMEM;
	}

	serdev_count = dev_info->serdev_count;
	for (i = 0; i < serdev_count; i++) {
		ret = x86_instantiate_serdev(&dev_info->serdev_info[i], i);
		if (ret < 0) {
			x86_android_tablet_cleanup();
			return ret;
		}
	}

	return 0;
}

module_init(x86_android_tablet_init);
module_exit(x86_android_tablet_cleanup);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("X86 Android tablets DSDT fixups driver");
MODULE_LICENSE("GPL");
