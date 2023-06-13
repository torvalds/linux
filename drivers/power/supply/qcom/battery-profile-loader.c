// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/err.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/power_supply.h>
#include "battery-profile-loader.h"

static int of_batterydata_read_batt_id_kohm(const struct device_node *np,
				const char *propname, struct batt_ids *batt_ids)
{
	struct property *prop;
	const __be32 *data;
	int num, i, *id_kohm = batt_ids->kohm;

	prop = of_find_property(np, "qcom,batt-id-kohm", NULL);
	if (!prop) {
		pr_err("%s: No battery id resistor found\n", np->name);
		return -EINVAL;
	} else if (!prop->value) {
		pr_err("%s: No battery id resistor value found, np->name\n",
						np->name);
		return -ENODATA;
	} else if (prop->length > MAX_BATT_ID_NUM * sizeof(__be32)) {
		pr_err("%s: Too many battery id resistors\n", np->name);
		return -EINVAL;
	}

	num = prop->length/sizeof(__be32);
	batt_ids->num = num;
	data = prop->value;
	for (i = 0; i < num; i++)
		*id_kohm++ = be32_to_cpup(data++);

	return 0;
}

struct device_node *of_batterydata_get_best_profile(
		const struct device_node *batterydata_container_node,
		int batt_id_kohm, const char *batt_type)
{
	struct batt_ids batt_ids;
	struct device_node *node, *best_node = NULL;
	const char *battery_type = NULL;
	int delta = 0, best_delta = 0, best_id_kohm = 0, id_range_pct,
		i = 0, rc = 0, limit = 0;
	bool in_range = false;

	/* read battery id range percentage for best profile */
	rc = of_property_read_u32(batterydata_container_node,
			"qcom,batt-id-range-pct", &id_range_pct);

	if (rc) {
		if (rc == -EINVAL) {
			id_range_pct = 0;
		} else {
			pr_err("failed to read battery id range\n");
			return ERR_PTR(-ENXIO);
		}
	}

	/*
	 * Find the battery data with a battery id resistor closest to this one
	 */
	for_each_child_of_node(batterydata_container_node, node) {
		if (batt_type != NULL) {
			rc = of_property_read_string(node, "qcom,battery-type",
							&battery_type);
			if (!rc && strcmp(battery_type, batt_type) == 0) {
				best_node = node;
				best_id_kohm = batt_id_kohm;
				break;
			}
		} else {
			rc = of_batterydata_read_batt_id_kohm(node,
							"qcom,batt-id-kohm",
							&batt_ids);
			if (rc)
				continue;
			for (i = 0; i < batt_ids.num; i++) {
				delta = abs(batt_ids.kohm[i] - batt_id_kohm);
				limit = (batt_ids.kohm[i] * id_range_pct) / 100;
				in_range = (delta <= limit);
				/*
				 * Check if the delta is the lowest one
				 * and also if the limits are in range
				 * before selecting the best node.
				 */
				if ((delta < best_delta || !best_node)
					&& in_range) {
					best_node = node;
					best_delta = delta;
					best_id_kohm = batt_ids.kohm[i];
				}
			}
		}
	}

	if (best_node == NULL) {
		pr_err("No battery data found\n");
		return best_node;
	}

	/* check that profile id is in range of the measured batt_id */
	if (abs(best_id_kohm - batt_id_kohm) >
			((best_id_kohm * id_range_pct) / 100)) {
		pr_err("out of range: profile id %d batt id %d pct %d\n",
			best_id_kohm, batt_id_kohm, id_range_pct);
		return NULL;
	}

	rc = of_property_read_string(best_node, "qcom,battery-type",
							&battery_type);
	if (!rc)
		pr_info("%s found\n", battery_type);
	else
		pr_info("%s found\n", best_node->name);

	return best_node;
}

struct device_node *of_batterydata_get_best_aged_profile(
		const struct device_node *batterydata_container_node,
		int batt_id_kohm, int batt_age_level, int *avail_age_level)
{
	struct batt_ids batt_ids;
	struct device_node *node, *best_node = NULL;
	const char *battery_type = NULL;
	int delta = 0, best_id_kohm = 0, id_range_pct, i = 0, rc = 0, limit = 0;
	u32 val;
	bool in_range = false;

	/* read battery id range percentage for best profile */
	rc = of_property_read_u32(batterydata_container_node,
			"qcom,batt-id-range-pct", &id_range_pct);

	if (rc) {
		if (rc == -EINVAL) {
			id_range_pct = 0;
		} else {
			pr_err("failed to read battery id range\n");
			return ERR_PTR(-ENXIO);
		}
	}

	/*
	 * Find the battery data with a battery id resistor closest to this one
	 */
	for_each_available_child_of_node(batterydata_container_node, node) {
		val = 0;
		of_property_read_u32(node, "qcom,batt-age-level", &val);
		rc = of_batterydata_read_batt_id_kohm(node,
						"qcom,batt-id-kohm", &batt_ids);
		if (rc)
			continue;
		for (i = 0; i < batt_ids.num; i++) {
			delta = abs(batt_ids.kohm[i] - batt_id_kohm);
			limit = (batt_ids.kohm[i] * id_range_pct) / 100;
			in_range = (delta <= limit);

			/*
			 * Check if the battery aging level matches and the
			 * limits are in range before selecting the best node.
			 */
			if ((batt_age_level == val || !best_node) && in_range) {
				best_node = node;
				best_id_kohm = batt_ids.kohm[i];
				*avail_age_level = val;
				break;
			}
		}
	}

	if (best_node == NULL) {
		pr_err("No battery data found\n");
		return best_node;
	}

	/* check that profile id is in range of the measured batt_id */
	if (abs(best_id_kohm - batt_id_kohm) >
			((best_id_kohm * id_range_pct) / 100)) {
		pr_err("out of range: profile id %d batt id %d pct %d\n",
			best_id_kohm, batt_id_kohm, id_range_pct);
		return NULL;
	}

	rc = of_property_read_string(best_node, "qcom,battery-type",
							&battery_type);
	if (!rc)
		pr_info("%s age level %d found\n", battery_type,
			*avail_age_level);
	else
		pr_info("%s age level %d found\n", best_node->name,
			*avail_age_level);

	return best_node;
}

int of_batterydata_get_aged_profile_count(
		const struct device_node *batterydata_node,
		int batt_id_kohm, int *count)
{
	struct device_node *node;
	int id_range_pct, i = 0, rc = 0, limit = 0, delta = 0;
	bool in_range = false;
	u32 batt_id;

	/* read battery id range percentage for best profile */
	rc = of_property_read_u32(batterydata_node,
			"qcom,batt-id-range-pct", &id_range_pct);
	if (rc) {
		if (rc == -EINVAL) {
			id_range_pct = 0;
		} else {
			pr_err("failed to read battery id range\n");
			return -ENXIO;
		}
	}

	for_each_available_child_of_node(batterydata_node, node) {
		if (!of_find_property(node, "qcom,batt-age-level", NULL))
			continue;

		if (!of_find_property(node, "qcom,soh-range", NULL))
			continue;

		rc = of_property_read_u32(node, "qcom,batt-id-kohm", &batt_id);
		if (rc)
			continue;

		delta = abs(batt_id_kohm - batt_id);
		limit = (batt_id_kohm * id_range_pct) / 100;
		in_range = (delta <= limit);

		if (!in_range) {
			pr_debug("not in range batt_id: %d\n", batt_id);
			continue;
		}

		i++;
	}

	if (i <= 1) {
		pr_err("Less number of profiles to support SOH\n");
		return -EINVAL;
	}

	*count = i;
	return 0;
}

int of_batterydata_read_soh_aged_profiles(
		const struct device_node *batterydata_node,
		int batt_id_kohm, struct soh_range *soh_data)
{
	struct device_node *node;
	u32 val, temp[2], i = 0;
	int rc, batt_id, id_range_pct, limit = 0, delta = 0;
	bool in_range = false;

	if (!batterydata_node || !soh_data)
		return -ENODEV;

	/* read battery id range percentage for best profile */
	rc = of_property_read_u32(batterydata_node,
			"qcom,batt-id-range-pct", &id_range_pct);
	if (rc) {
		if (rc == -EINVAL) {
			id_range_pct = 0;
		} else {
			pr_err("failed to read battery id range\n");
			return -ENXIO;
		}
	}

	for_each_available_child_of_node(batterydata_node, node) {
		rc = of_property_read_u32(node, "qcom,batt-age-level", &val);
		if (rc)
			continue;

		rc = of_property_read_u32(node, "qcom,batt-id-kohm", &batt_id);
		if (rc)
			continue;

		delta = abs(batt_id_kohm - batt_id);
		limit = (batt_id_kohm * id_range_pct) / 100;
		in_range = (delta <= limit);

		if (!in_range) {
			pr_debug("not in range batt_id: %d\n", batt_id);
			continue;
		}

		if (!of_find_property(node, "qcom,soh-range", NULL))
			continue;

		rc = of_property_count_elems_of_size(node, "qcom,soh-range",
						sizeof(u32));
		if (rc != 2) {
			pr_err("Incorrect element size for qcom,soh-range, rc=%d\n",
				rc);
			return -EINVAL;
		}

		rc = of_property_read_u32_array(node, "qcom,soh-range", temp,
						2);
		if (rc < 0) {
			pr_err("Error in reading qcom,soh-range, rc=%d\n", rc);
			return rc;
		}

		if (temp[0] > 100 || temp[1] > 100 || (temp[0] > temp[1])) {
			pr_err("Incorrect SOH range [%d %d]\n", temp[0],
				temp[1]);
			return -ERANGE;
		}

		pr_debug("batt_age_level: %d soh: [%d %d]\n", val, temp[0],
			temp[1]);
		soh_data[i].batt_age_level = val;
		soh_data[i].soh_min = temp[0];
		soh_data[i].soh_max = temp[1];
		i++;
	}

	return 0;
}

MODULE_LICENSE("GPL");
