// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Shared psy info for X86 tablets which ship with Android as the factory image
 * and which have broken DSDT tables. The factory kernels shipped on these
 * devices typically have a bunch of things hardcoded, rather than specified
 * in their DSDT.
 *
 * Copyright (C) 2021-2023 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/gpio/machine.h>
#include <linux/platform_device.h>
#include <linux/power/bq24190_charger.h>
#include <linux/property.h>
#include <linux/regulator/machine.h>

#include "shared-psy-info.h"

/* Generic / shared charger / battery settings */
const char * const tusb1211_chg_det_psy[] = { "tusb1211-charger-detect" };
const char * const bq24190_psy[] = { "bq24190-charger" };
const char * const bq25890_psy[] = { "bq25890-charger-0" };

static const struct property_entry fg_bq24190_supply_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", bq24190_psy),
	{ }
};

const struct software_node fg_bq24190_supply_node = {
	.properties = fg_bq24190_supply_props,
};

static const struct property_entry fg_bq25890_supply_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", bq25890_psy),
	{ }
};

const struct software_node fg_bq25890_supply_node = {
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

const struct software_node generic_lipo_hv_4v35_battery_node = {
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

struct bq24190_platform_data bq24190_pdata = {
	.regulator_init_data = &bq24190_vbus_init_data,
};

const char * const bq24190_modules[] __initconst = {
	"intel_crystal_cove_charger", /* For the bq24190 IRQ */
	"bq24190_charger",            /* For the Vbus regulator for intel-int3496 */
	NULL
};

/* Generic pdevs array and gpio-lookups for micro USB ID pin handling */
const struct platform_device_info int3496_pdevs[] __initconst = {
	{
		/* For micro USB ID pin handling */
		.name = "intel-int3496",
		.id = PLATFORM_DEVID_NONE,
	},
};

struct gpiod_lookup_table int3496_reference_gpios = {
	.dev_id = "intel-int3496",
	.table = {
		GPIO_LOOKUP("INT33FC:01", 15, "vbus", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FC:02", 1, "mux", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FC:02", 18, "id", GPIO_ACTIVE_HIGH),
		{ }
	},
};
