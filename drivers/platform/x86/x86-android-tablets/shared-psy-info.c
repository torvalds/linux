// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Shared psy info for X86 tablets which ship with Android as the factory image
 * and which have broken DSDT tables. The factory kernels shipped on these
 * devices typically have a bunch of things hardcoded, rather than specified
 * in their DSDT.
 *
 * Copyright (C) 2021-2023 Hans de Goede <hansg@kernel.org>
 */

#include <linux/gpio/machine.h>
#include <linux/gpio/property.h>
#include <linux/platform_device.h>
#include <linux/power/bq24190_charger.h>
#include <linux/property.h>
#include <linux/regulator/machine.h>

#include "shared-psy-info.h"
#include "x86-android-tablets.h"

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

static const u32 generic_lipo_battery_ovc_cap_celcius[] = { 25 };

static const u32 generic_lipo_4v2_battery_ovc_cap_table0[] = {
	4200000, 100,
	4150000, 95,
	4110000, 90,
	4075000, 85,
	4020000, 80,
	3982500, 75,
	3945000, 70,
	3907500, 65,
	3870000, 60,
	3853333, 55,
	3836667, 50,
	3820000, 45,
	3803333, 40,
	3786667, 35,
	3770000, 30,
	3750000, 25,
	3730000, 20,
	3710000, 15,
	3690000, 10,
	3610000, 5,
	3350000, 0
};

static const u32 generic_lipo_hv_4v35_battery_ovc_cap_table0[] = {
	4300000, 100,
	4250000, 96,
	4200000, 91,
	4150000, 86,
	4110000, 82,
	4075000, 77,
	4020000, 73,
	3982500, 68,
	3945000, 64,
	3907500, 59,
	3870000, 55,
	3853333, 50,
	3836667, 45,
	3820000, 41,
	3803333, 36,
	3786667, 32,
	3770000, 27,
	3750000, 23,
	3730000, 18,
	3710000, 14,
	3690000, 9,
	3610000, 5,
	3350000, 0
};

/* Standard LiPo (max 4.2V) settings used by most devs with a LiPo battery */
static const struct property_entry generic_lipo_4v2_battery_props[] = {
	PROPERTY_ENTRY_STRING("compatible", "simple-battery"),
	PROPERTY_ENTRY_STRING("device-chemistry", "lithium-ion-polymer"),
	PROPERTY_ENTRY_U32("precharge-current-microamp", 256000),
	PROPERTY_ENTRY_U32("charge-term-current-microamp", 128000),
	PROPERTY_ENTRY_U32("constant-charge-current-max-microamp", 2048000),
	PROPERTY_ENTRY_U32("constant-charge-voltage-max-microvolt", 4208000),
	PROPERTY_ENTRY_U32("factory-internal-resistance-micro-ohms", 150000),
	PROPERTY_ENTRY_U32_ARRAY("ocv-capacity-celsius",
				 generic_lipo_battery_ovc_cap_celcius),
	PROPERTY_ENTRY_U32_ARRAY("ocv-capacity-table-0",
				 generic_lipo_4v2_battery_ovc_cap_table0),
	{ }
};

const struct software_node generic_lipo_4v2_battery_node = {
	.properties = generic_lipo_4v2_battery_props,
};

const struct software_node *generic_lipo_4v2_battery_swnodes[] = {
	&generic_lipo_4v2_battery_node,
	NULL
};

/* LiPo HighVoltage (max 4.35V) settings used by most devs with a HV battery */
static const struct property_entry generic_lipo_hv_4v35_battery_props[] = {
	PROPERTY_ENTRY_STRING("compatible", "simple-battery"),
	PROPERTY_ENTRY_STRING("device-chemistry", "lithium-ion"),
	PROPERTY_ENTRY_U32("precharge-current-microamp", 256000),
	PROPERTY_ENTRY_U32("charge-term-current-microamp", 128000),
	PROPERTY_ENTRY_U32("constant-charge-current-max-microamp", 1856000),
	PROPERTY_ENTRY_U32("constant-charge-voltage-max-microvolt", 4352000),
	PROPERTY_ENTRY_U32("factory-internal-resistance-micro-ohms", 150000),
	PROPERTY_ENTRY_U32_ARRAY("ocv-capacity-celsius",
				 generic_lipo_battery_ovc_cap_celcius),
	PROPERTY_ENTRY_U32_ARRAY("ocv-capacity-table-0",
				 generic_lipo_hv_4v35_battery_ovc_cap_table0),
	{ }
};

const struct software_node generic_lipo_hv_4v35_battery_node = {
	.properties = generic_lipo_hv_4v35_battery_props,
};

const struct software_node *generic_lipo_hv_4v35_battery_swnodes[] = {
	&generic_lipo_hv_4v35_battery_node,
	NULL
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

static const struct property_entry int3496_reference_props[] __initconst = {
	PROPERTY_ENTRY_GPIO("vbus-gpios", &baytrail_gpiochip_nodes[1], 15, GPIO_ACTIVE_HIGH),
	PROPERTY_ENTRY_GPIO("mux-gpios", &baytrail_gpiochip_nodes[2], 1, GPIO_ACTIVE_HIGH),
	PROPERTY_ENTRY_GPIO("id-gpios", &baytrail_gpiochip_nodes[2], 18, GPIO_ACTIVE_HIGH),
	{ }
};

/* Generic pdevs array and gpio-lookups for micro USB ID pin handling */
const struct platform_device_info int3496_pdevs[] __initconst = {
	{
		/* For micro USB ID pin handling */
		.name = "intel-int3496",
		.id = PLATFORM_DEVID_NONE,
		.properties = int3496_reference_props,
	},
};
