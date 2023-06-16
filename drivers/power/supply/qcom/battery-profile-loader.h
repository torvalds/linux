/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013-2014, 2016-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __BATTERY_PROFILE_LOADER_H
#define __BATTERY_PROFILE_LOADER_H

#include <linux/of.h>

#define MAX_BATT_ID_NUM		4
#define DEGC_SCALE		10

struct batt_ids {
	int kohm[MAX_BATT_ID_NUM];
	int num;
};

/**
 * struct soh_range -
 * @batt_age_level:	Battery age level (e.g. 0, 1 etc.,)
 * @soh_min:		Minimum SOH (state of health) level that this battery
 *			profile can support.
 * @soh_max:		Maximum SOH (state of health) level that this battery
 *			profile can support.
 */
struct soh_range {
	int	batt_age_level;
	int	soh_min;
	int	soh_max;
};

/**
 * of_batterydata_get_best_profile() - Find matching battery data device node
 * @batterydata_container_node: pointer to the battery-data container device
 *		node containing the profile nodes.
 * @batt_id_kohm: Battery ID in KOhms for which we want to find the profile.
 * @batt_type: Battery type which we want to force load the profile.
 *
 * This routine returns a device_node pointer to the closest match battery data
 * from device tree based on the battery id reading.
 */
struct device_node *of_batterydata_get_best_profile(
		const struct device_node *batterydata_container_node,
		int batt_id_kohm, const char *batt_type);

/**
 * of_batterydata_get_best_aged_profile() - Find best aged battery profile
 * @batterydata_container_node: pointer to the battery-data container device
 *		node containing the profile nodes.
 * @batt_id_kohm: Battery ID in KOhms for which we want to find the profile.
 * @batt_age_level: Battery age level.
 * @avail_age_level: Available battery age level.
 *
 * This routine returns a device_node pointer to the closest match battery data
 * from device tree based on the battery id reading and age level.
 */
struct device_node *of_batterydata_get_best_aged_profile(
		const struct device_node *batterydata_container_node,
		int batt_id_kohm, int batt_age_level, int *avail_age_level);

/**
 * of_batterydata_get_aged_profile_count() - Gets the number of aged profiles
 * @batterydata_node: pointer to the battery-data container device
 *		node containing the profile nodes.
 * @batt_id_kohm: Battery ID in KOhms for which we want to find the profile.
 * @count: Number of aged profiles available to support SOH based profile
 * loading.
 *
 * This routine returns zero if valid number of aged profiles are available.
 */
int of_batterydata_get_aged_profile_count(
		const struct device_node *batterydata_node,
		int batt_id_kohm, int *count);

/**
 * of_batterydata_read_soh_aged_profiles() - Reads the data from aged profiles
 * @batterydata_node: pointer to the battery-data container device
 *		node containing the profile nodes.
 * @batt_id_kohm: Battery ID in KOhms for which we want to find the profile.
 * @soh_data: SOH data from the profile if it is found to be valid.
 *
 * This routine returns zero if SOH data of aged profiles is valid.
 */
int of_batterydata_read_soh_aged_profiles(
		const struct device_node *batterydata_node,
		int batt_id_kohm, struct soh_range *soh_data);

#endif /* __BATTERY_PROFILE_LOADER_H */
