/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Shared psy info for X86 tablets which ship with Android as the factory image
 * and which have broken DSDT tables. The factory kernels shipped on these
 * devices typically have a bunch of things hardcoded, rather than specified
 * in their DSDT.
 *
 * Copyright (C) 2021-2023 Hans de Goede <hdegoede@redhat.com>
 */
#ifndef __PDX86_SHARED_PSY_INFO_H
#define __PDX86_SHARED_PSY_INFO_H

struct bq24190_platform_data;
struct gpiod_lookup_table;
struct platform_device_info;
struct software_node;

extern const char * const tusb1211_chg_det_psy[];
extern const char * const bq24190_psy[];
extern const char * const bq25890_psy[];

extern const struct software_node fg_bq24190_supply_node;
extern const struct software_node fg_bq25890_supply_node;
extern const struct software_node generic_lipo_hv_4v35_battery_node;

extern struct bq24190_platform_data bq24190_pdata;
extern const char * const bq24190_modules[];

extern const struct platform_device_info int3496_pdevs[];
extern struct gpiod_lookup_table int3496_reference_gpios;

#endif
