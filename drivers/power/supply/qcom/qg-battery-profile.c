// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"QG-K: %s: " fmt, __func__

#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <uapi/linux/qg-profile.h>
#include "qg-battery-profile.h"
#include "qg-profile-lib.h"
#include "qg-defs.h"

struct qg_battery_data {
	/* battery-data class node */
	dev_t				dev_no;
	struct class			*battery_class;
	struct device			*battery_device;
	struct cdev			battery_cdev;

	/* profile */
	struct device_node		*profile_node;
	struct profile_table_data	profile[TABLE_MAX];
};

struct tables {
	int table_index;
	char *table_name;
};

static struct tables table[] = {
	{TABLE_SOC_OCV1, "qcom,pc-temp-v1-lut"},
	{TABLE_SOC_OCV2, "qcom,pc-temp-v2-lut"},
	{TABLE_FCC1, "qcom,fcc1-temp-lut"},
	{TABLE_FCC2, "qcom,fcc2-temp-lut"},
	{TABLE_Z1, "qcom,pc-temp-z1-lut"},
	{TABLE_Z2, "qcom,pc-temp-z2-lut"},
	{TABLE_Z3, "qcom,pc-temp-z3-lut"},
	{TABLE_Z4, "qcom,pc-temp-z4-lut"},
	{TABLE_Z5, "qcom,pc-temp-z5-lut"},
	{TABLE_Z6, "qcom,pc-temp-z6-lut"},
	{TABLE_Y1, "qcom,pc-temp-y1-lut"},
	{TABLE_Y2, "qcom,pc-temp-y2-lut"},
	{TABLE_Y3, "qcom,pc-temp-y3-lut"},
	{TABLE_Y4, "qcom,pc-temp-y4-lut"},
	{TABLE_Y5, "qcom,pc-temp-y5-lut"},
	{TABLE_Y6, "qcom,pc-temp-y6-lut"},
};

static struct qg_battery_data *the_battery;

static void qg_battery_profile_free(void);

static int qg_battery_data_open(struct inode *inode, struct file *file)
{
	struct qg_battery_data *battery = container_of(inode->i_cdev,
				struct qg_battery_data, battery_cdev);

	file->private_data = battery;

	return 0;
}

static long qg_battery_data_ioctl(struct file *file, unsigned int cmd,
						unsigned long arg)
{
	struct qg_battery_data *battery = file->private_data;
	struct battery_params __user *bp_user =
				(struct battery_params __user *)arg;
	struct battery_params bp;
	int rc = 0, soc, ocv_uv, fcc_mah, var, slope;

	if (!battery->profile_node) {
		pr_err("Battery data not set!\n");
		return -EINVAL;
	}

	if (!bp_user) {
		pr_err("Invalid battery-params user pointer\n");
		return -EINVAL;
	}

	if (copy_from_user(&bp, bp_user, sizeof(bp))) {
		pr_err("Failed in copy_from_user\n");
		return -EFAULT;
	}

	switch (cmd) {
	case BPIOCXSOC:
		if (bp.table_index != TABLE_SOC_OCV1 &&
				bp.table_index != TABLE_SOC_OCV2) {
			pr_err("Invalid table index %d for SOC-OCV lookup\n",
					bp.table_index);
			rc = -EINVAL;
		} else {
			/* OCV is passed as deci-uV  - 10^-4 V */
			soc = qg_interpolate_soc(
					&battery->profile[bp.table_index],
					bp.batt_temp, UV_TO_DECIUV(bp.ocv_uv));
			soc = CAP(QG_MIN_SOC, QG_MAX_SOC, soc);
			rc = put_user(soc, &bp_user->soc);
			if (rc < 0) {
				pr_err("BPIOCXSOC: Failed rc=%d\n", rc);
				goto ret_err;
			}
			pr_debug("BPIOCXSOC: lut=%s ocv=%d batt_temp=%d soc=%d\n",
					battery->profile[bp.table_index].name,
					bp.ocv_uv, bp.batt_temp, soc);
		}
		break;
	case BPIOCXOCV:
		if (bp.table_index != TABLE_SOC_OCV1 &&
				bp.table_index != TABLE_SOC_OCV2) {
			pr_err("Invalid table index %d for SOC-OCV lookup\n",
					bp.table_index);
			rc = -EINVAL;
		} else {
			ocv_uv = qg_interpolate_var(
					&battery->profile[bp.table_index],
					bp.batt_temp, bp.soc);
			ocv_uv = DECIUV_TO_UV(ocv_uv);
			ocv_uv = CAP(QG_MIN_OCV_UV, QG_MAX_OCV_UV, ocv_uv);
			rc = put_user(ocv_uv, &bp_user->ocv_uv);
			if (rc < 0) {
				pr_err("BPIOCXOCV: Failed rc=%d\n", rc);
				goto ret_err;
			}
			pr_debug("BPIOCXOCV: lut=%s ocv=%d batt_temp=%d soc=%d\n",
					battery->profile[bp.table_index].name,
					ocv_uv, bp.batt_temp, bp.soc);
		}
		break;
	case BPIOCXFCC:
		if (bp.table_index != TABLE_FCC1 &&
				bp.table_index != TABLE_FCC2) {
			pr_err("Invalid table index %d for FCC lookup\n",
					bp.table_index);
			rc = -EINVAL;
		} else {
			fcc_mah = qg_interpolate_single_row_lut(
					&battery->profile[bp.table_index],
					bp.batt_temp, DEGC_SCALE);
			fcc_mah = CAP(QG_MIN_FCC_MAH, QG_MAX_FCC_MAH, fcc_mah);
			rc = put_user(fcc_mah, &bp_user->fcc_mah);
			if (rc) {
				pr_err("BPIOCXFCC: Failed rc=%d\n", rc);
				goto ret_err;
			}
			pr_debug("BPIOCXFCC: lut=%s batt_temp=%d fcc_mah=%d\n",
					battery->profile[bp.table_index].name,
					bp.batt_temp, fcc_mah);
		}
		break;
	case BPIOCXVAR:
		if (bp.table_index < TABLE_Z1 || bp.table_index >= TABLE_MAX) {
			pr_err("Invalid table index %d for VAR lookup\n",
					bp.table_index);
			rc = -EINVAL;
		} else {
			var = qg_interpolate_var(
					&battery->profile[bp.table_index],
					bp.batt_temp, bp.soc);
			var = CAP(QG_MIN_VAR, QG_MAX_VAR, var);
			rc = put_user(var, &bp_user->var);
			if (rc < 0) {
				pr_err("BPIOCXVAR: Failed rc=%d\n", rc);
				goto ret_err;
			}
			pr_debug("BPIOCXVAR: lut=%s var=%d batt_temp=%d soc=%d\n",
					battery->profile[bp.table_index].name,
					var, bp.batt_temp, bp.soc);
		}
		break;
	case BPIOCXSLOPE:
		if (bp.table_index != TABLE_SOC_OCV1 &&
				bp.table_index != TABLE_SOC_OCV2) {
			pr_err("Invalid table index %d for Slope lookup\n",
					bp.table_index);
			rc = -EINVAL;
		} else {
			slope = qg_interpolate_slope(
					&battery->profile[bp.table_index],
					bp.batt_temp, bp.soc);
			slope = CAP(QG_MIN_SLOPE, QG_MAX_SLOPE, slope);
			rc = put_user(slope, &bp_user->slope);
			if (rc) {
				pr_err("BPIOCXSLOPE: Failed rc=%d\n", rc);
				goto ret_err;
			}
			pr_debug("BPIOCXSLOPE: lut=%s soc=%d batt_temp=%d slope=%d\n",
					battery->profile[bp.table_index].name,
					bp.soc, bp.batt_temp, slope);
		}
		break;
	default:
		pr_err("IOCTL %d not supported\n", cmd);
		rc = -EINVAL;
	}
ret_err:
	return rc;
}

static int qg_battery_data_release(struct inode *inode, struct file *file)
{
	pr_debug("battery_data device closed\n");

	return 0;
}

static const struct file_operations qg_battery_data_fops = {
	.owner = THIS_MODULE,
	.open = qg_battery_data_open,
	.unlocked_ioctl = qg_battery_data_ioctl,
	.compat_ioctl = qg_battery_data_ioctl,
	.release = qg_battery_data_release,
};

static int get_length(struct device_node *node,
			int *length, char *prop_name, bool ignore_null)
{
	struct property *prop;

	prop = of_find_property(node, prop_name, NULL);
	if (!prop) {
		if (ignore_null) {
			*length = 1;
			return 0;
		}
		pr_err("Failed to find %s property\n", prop_name);
		return -ENODATA;
	} else if (!prop->value) {
		pr_err("Failed to find value for %s property\n", prop_name);
		return -ENODATA;
	}

	*length = prop->length / sizeof(u32);

	return 0;
}

static int qg_parse_battery_profile(struct qg_battery_data *battery)
{
	int i, j, k, rows = 0, cols = 0, lut_length = 0, rc = 0;
	struct device_node *node;
	struct property *prop;
	const __be32 *data;

	for (i = 0; i < TABLE_MAX; i++) {
		node = of_find_node_by_name(battery->profile_node,
						table[i].table_name);
		if (!node) {
			pr_err("%s table not found\n", table[i].table_name);
			rc = -ENODEV;
			goto cleanup;
		}

		rc = get_length(node, &cols, "qcom,lut-col-legend", false);
		if (rc < 0) {
			pr_err("Failed to get col-length for %s table rc=%d\n",
				table[i].table_name, rc);
			goto cleanup;
		}

		rc = get_length(node, &rows, "qcom,lut-row-legend", true);
		if (rc < 0) {
			pr_err("Failed to get row-length for %s table rc=%d\n",
				table[i].table_name, rc);
			goto cleanup;
		}

		rc = get_length(node, &lut_length, "qcom,lut-data", false);
		if (rc < 0) {
			pr_err("Failed to get lut-length for %s table rc=%d\n",
				table[i].table_name, rc);
			goto cleanup;
		}

		if (lut_length != cols * rows) {
			pr_err("Invalid lut-length for %s table\n",
					table[i].table_name);
			rc = -EINVAL;
			goto cleanup;
		}

		battery->profile[i].name = kzalloc(strlen(table[i].table_name)
						+ 1, GFP_KERNEL);
		if (!battery->profile[i].name) {
			rc = -ENOMEM;
			goto cleanup;
		}

		strscpy(battery->profile[i].name, table[i].table_name,
						strlen(table[i].table_name));
		battery->profile[i].rows = rows;
		battery->profile[i].cols = cols;

		if (rows != 1) {
			battery->profile[i].row_entries = kcalloc(rows,
				sizeof(*battery->profile[i].row_entries),
				GFP_KERNEL);
			if (!battery->profile[i].row_entries) {
				rc = -ENOMEM;
				goto cleanup;
			}
		}

		battery->profile[i].col_entries = kcalloc(cols,
				sizeof(*battery->profile[i].col_entries),
				GFP_KERNEL);
		if (!battery->profile[i].col_entries) {
			rc = -ENOMEM;
			goto cleanup;
		}

		battery->profile[i].data = kcalloc(rows,
				sizeof(*battery->profile[i].data), GFP_KERNEL);
		if (!battery->profile[i].data) {
			rc = -ENOMEM;
			goto cleanup;
		}

		for (j = 0; j < rows; j++) {
			battery->profile[i].data[j] = kcalloc(cols,
				sizeof(**battery->profile[i].data),
				GFP_KERNEL);
			if (!battery->profile[i].data[j]) {
				rc = -ENOMEM;
				goto cleanup;
			}
		}

		/* read profile data */
		rc = of_property_read_u32_array(node, "qcom,lut-col-legend",
					battery->profile[i].col_entries, cols);
		if (rc < 0) {
			pr_err("Failed to read cols values for table %s rc=%d\n",
					table[i].table_name, rc);
			goto cleanup;
		}

		if (rows != 1) {
			rc = of_property_read_u32_array(node,
					"qcom,lut-row-legend",
					battery->profile[i].row_entries, rows);
			if (rc < 0) {
				pr_err("Failed to read row values for table %s rc=%d\n",
						table[i].table_name, rc);
				goto cleanup;
			}
		}

		prop = of_find_property(node, "qcom,lut-data", NULL);
		if (!prop) {
			pr_err("Failed to find lut-data\n");
			rc = -EINVAL;
			goto cleanup;
		}
		data = prop->value;
		for (j = 0; j < rows; j++) {
			for (k = 0; k < cols; k++)
				battery->profile[i].data[j][k] =
						be32_to_cpup(data++);
		}

		pr_debug("Profile table %s parsed rows=%d cols=%d\n",
			battery->profile[i].name, battery->profile[i].rows,
			battery->profile[i].cols);
	}

	return 0;

cleanup:
	for (; i >= 0; i++) {
		kfree(battery->profile[i].name);
		kfree(battery->profile[i].row_entries);
		kfree(battery->profile[i].col_entries);
		for (j = 0; j < battery->profile[i].rows; j++) {
			if (battery->profile[i].data)
				kfree(battery->profile[i].data[j]);
		}
		kfree(battery->profile[i].data);
	}
	return rc;
}

int lookup_soc_ocv(u32 *soc, u32 ocv_uv, int batt_temp, bool charging)
{
	u8 table_index = charging ? TABLE_SOC_OCV1 : TABLE_SOC_OCV2;

	if (!the_battery || !the_battery->profile_node)
		return -ENODEV;

	*soc = qg_interpolate_soc(&the_battery->profile[table_index],
				batt_temp, UV_TO_DECIUV(ocv_uv));

	*soc = CAP(0, 100, DIV_ROUND_CLOSEST(*soc, 100));

	return 0;
}

int qg_get_nominal_capacity(u32 *nom_cap_uah, int batt_temp, bool charging)
{
	u8 table_index = charging ? TABLE_FCC1 : TABLE_FCC2;
	u32 fcc_mah;

	if (!the_battery || !the_battery->profile_node)
		return -ENODEV;

	fcc_mah = qg_interpolate_single_row_lut(
				&the_battery->profile[table_index],
					batt_temp, DEGC_SCALE);
	fcc_mah = CAP(QG_MIN_FCC_MAH, QG_MAX_FCC_MAH, fcc_mah);

	*nom_cap_uah = fcc_mah * 1000;

	return 0;
}

int qg_batterydata_init(struct device_node *profile_node)
{
	int rc = 0;
	struct qg_battery_data *battery;

	/*
	 * If a battery profile is already initialized, free the existing
	 * profile data and re-allocate and load the new profile. This is
	 * required for multi-profile load support.
	 */
	if (the_battery) {
		battery = the_battery;
		battery->profile_node = NULL;
		qg_battery_profile_free();
	} else {
		battery = kzalloc(sizeof(*battery), GFP_KERNEL);
		if (!battery)
			return -ENOMEM;
		/* char device to access battery-profile data */
		rc = alloc_chrdev_region(&battery->dev_no, 0, 1,
							"qg_battery");
		if (rc < 0) {
			pr_err("Failed to allocate chrdev rc=%d\n", rc);
			goto free_battery;
		}

		cdev_init(&battery->battery_cdev, &qg_battery_data_fops);
		rc = cdev_add(&battery->battery_cdev,
						battery->dev_no, 1);
		if (rc) {
			pr_err("Failed to add battery_cdev rc=%d\n", rc);
			goto unregister_chrdev;
		}

		battery->battery_class = class_create(THIS_MODULE,
							"qg_battery");
		if (IS_ERR_OR_NULL(battery->battery_class)) {
			pr_err("Failed to create qg-battery class\n");
			rc = -ENODEV;
			goto delete_cdev;
		}

		battery->battery_device = device_create(
						battery->battery_class,
						NULL, battery->dev_no,
						NULL, "qg_battery");
		if (IS_ERR_OR_NULL(battery->battery_device)) {
			pr_err("Failed to create battery_device device\n");
			rc = -ENODEV;
			goto destroy_class;
		}
		the_battery = battery;
	}

	battery->profile_node = profile_node;
	/* parse the battery profile */
	rc = qg_parse_battery_profile(battery);
	if (rc < 0) {
		pr_err("Failed to parse battery profile rc=%d\n", rc);
		goto destroy_device;
	}

	pr_info("QG Battery-profile loaded\n");

	return 0;

destroy_device:
	device_destroy(battery->battery_class, battery->dev_no);
destroy_class:
	class_destroy(battery->battery_class);
delete_cdev:
	cdev_del(&battery->battery_cdev);
unregister_chrdev:
	unregister_chrdev_region(battery->dev_no, 1);
free_battery:
	kfree(battery);
	return rc;
}

static void qg_battery_profile_free(void)
{
	int i, j;

	/* delete all the battery profile memory */
	for (i = 0; i < TABLE_MAX; i++) {
		kfree(the_battery->profile[i].name);
		kfree(the_battery->profile[i].row_entries);
		kfree(the_battery->profile[i].col_entries);
		for (j = 0; j < the_battery->profile[i].rows; j++) {
			if (the_battery->profile[i].data)
				kfree(the_battery->profile[i].data[j]);
		}
		kfree(the_battery->profile[i].data);
	}
}

void qg_batterydata_exit(void)
{
	if (the_battery) {
		/* unregister the device node */
		device_destroy(the_battery->battery_class, the_battery->dev_no);
		class_destroy(the_battery->battery_class);
		cdev_del(&the_battery->battery_cdev);
		unregister_chrdev_region(the_battery->dev_no, 1);
		qg_battery_profile_free();
	}

	kfree(the_battery);
	the_battery = NULL;
}
