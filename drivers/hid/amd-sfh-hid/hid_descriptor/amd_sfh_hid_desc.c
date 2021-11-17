// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  AMD SFH Report Descriptor generator
 *  Copyright 2020 Advanced Micro Devices, Inc.
 *  Authors: Nehal Bakulchandra Shah <Nehal-Bakulchandra.Shah@amd.com>
 *	     Sandeep Singh <sandeep.singh@amd.com>
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "amd_sfh_pcie.h"
#include "amd_sfh_hid_desc.h"
#include "amd_sfh_hid_report_desc.h"
#include "amd_sfh_hid.h"

#define	AMD_SFH_FW_MULTIPLIER (1000)
#define HID_USAGE_SENSOR_PROP_REPORTING_STATE_ALL_EVENTS_ENUM	0x41
#define HID_USAGE_SENSOR_PROP_POWER_STATE_D0_FULL_POWER_ENUM	0x51
#define HID_DEFAULT_REPORT_INTERVAL				0x50
#define HID_DEFAULT_MIN_VALUE					0X7F
#define HID_DEFAULT_MAX_VALUE					0x80
#define HID_DEFAULT_SENSITIVITY					0x7F
#define HID_USAGE_SENSOR_PROPERTY_CONNECTION_TYPE_PC_INTEGRATED_ENUM  0x01
/* state enums */
#define HID_USAGE_SENSOR_STATE_READY_ENUM                             0x02
#define HID_USAGE_SENSOR_STATE_INITIALIZING_ENUM                      0x05
#define HID_USAGE_SENSOR_EVENT_DATA_UPDATED_ENUM                      0x04

int get_report_descriptor(int sensor_idx, u8 *rep_desc)
{
	switch (sensor_idx) {
	case accel_idx: /* accel */
		memset(rep_desc, 0, sizeof(accel3_report_descriptor));
		memcpy(rep_desc, accel3_report_descriptor,
		       sizeof(accel3_report_descriptor));
		break;
	case gyro_idx: /* gyro */
		memset(rep_desc, 0, sizeof(gyro3_report_descriptor));
		memcpy(rep_desc, gyro3_report_descriptor,
		       sizeof(gyro3_report_descriptor));
		break;
	case mag_idx: /* Magnetometer */
		memset(rep_desc, 0, sizeof(comp3_report_descriptor));
		memcpy(rep_desc, comp3_report_descriptor,
		       sizeof(comp3_report_descriptor));
		break;
	case als_idx: /* ambient light sensor */
		memset(rep_desc, 0, sizeof(als_report_descriptor));
		memcpy(rep_desc, als_report_descriptor,
		       sizeof(als_report_descriptor));
		break;
	case HPD_IDX: /* HPD sensor */
		memset(rep_desc, 0, sizeof(hpd_report_descriptor));
		memcpy(rep_desc, hpd_report_descriptor,
		       sizeof(hpd_report_descriptor));
		break;
	default:
		break;
	}
	return 0;
}

u32 get_descr_sz(int sensor_idx, int descriptor_name)
{
	switch (sensor_idx) {
	case accel_idx:
		switch (descriptor_name) {
		case descr_size:
			return sizeof(accel3_report_descriptor);
		case input_size:
			return sizeof(struct accel3_input_report);
		case feature_size:
			return sizeof(struct accel3_feature_report);
		}
		break;
	case gyro_idx:
		switch (descriptor_name) {
		case descr_size:
			return sizeof(gyro3_report_descriptor);
		case input_size:
			return sizeof(struct gyro_input_report);
		case feature_size:
			return sizeof(struct gyro_feature_report);
		}
		break;
	case mag_idx:
		switch (descriptor_name) {
		case descr_size:
			return sizeof(comp3_report_descriptor);
		case input_size:
			return sizeof(struct magno_input_report);
		case feature_size:
			return sizeof(struct magno_feature_report);
		}
		break;
	case als_idx:
		switch (descriptor_name) {
		case descr_size:
			return sizeof(als_report_descriptor);
		case input_size:
			return sizeof(struct als_input_report);
		case feature_size:
			return sizeof(struct als_feature_report);
		}
		break;
	case HPD_IDX:
		switch (descriptor_name) {
		case descr_size:
			return sizeof(hpd_report_descriptor);
		case input_size:
			return sizeof(struct hpd_input_report);
		case feature_size:
			return sizeof(struct hpd_feature_report);
		}
		break;

	default:
		break;
	}
	return 0;
}

static void get_common_features(struct common_feature_property *common, int report_id)
{
	common->report_id = report_id;
	common->connection_type = HID_USAGE_SENSOR_PROPERTY_CONNECTION_TYPE_PC_INTEGRATED_ENUM;
	common->report_state = HID_USAGE_SENSOR_PROP_REPORTING_STATE_ALL_EVENTS_ENUM;
	common->power_state = HID_USAGE_SENSOR_PROP_POWER_STATE_D0_FULL_POWER_ENUM;
	common->sensor_state = HID_USAGE_SENSOR_STATE_INITIALIZING_ENUM;
	common->report_interval =  HID_DEFAULT_REPORT_INTERVAL;
}

u8 get_feature_report(int sensor_idx, int report_id, u8 *feature_report)
{
	struct accel3_feature_report acc_feature;
	struct gyro_feature_report gyro_feature;
	struct magno_feature_report magno_feature;
	struct hpd_feature_report hpd_feature;
	struct als_feature_report als_feature;
	u8 report_size = 0;

	if (!feature_report)
		return report_size;

	switch (sensor_idx) {
	case accel_idx: /* accel */
		get_common_features(&acc_feature.common_property, report_id);
		acc_feature.accel_change_sesnitivity = HID_DEFAULT_SENSITIVITY;
		acc_feature.accel_sensitivity_min = HID_DEFAULT_MIN_VALUE;
		acc_feature.accel_sensitivity_max = HID_DEFAULT_MAX_VALUE;
		memcpy(feature_report, &acc_feature, sizeof(acc_feature));
		report_size = sizeof(acc_feature);
		break;
	case gyro_idx: /* gyro */
		get_common_features(&gyro_feature.common_property, report_id);
		gyro_feature.gyro_change_sesnitivity = HID_DEFAULT_SENSITIVITY;
		gyro_feature.gyro_sensitivity_min = HID_DEFAULT_MIN_VALUE;
		gyro_feature.gyro_sensitivity_max = HID_DEFAULT_MAX_VALUE;
		memcpy(feature_report, &gyro_feature, sizeof(gyro_feature));
		report_size = sizeof(gyro_feature);
		break;
	case mag_idx: /* Magnetometer */
		get_common_features(&magno_feature.common_property, report_id);
		magno_feature.magno_headingchange_sensitivity = HID_DEFAULT_SENSITIVITY;
		magno_feature.heading_min = HID_DEFAULT_MIN_VALUE;
		magno_feature.heading_max = HID_DEFAULT_MAX_VALUE;
		magno_feature.flux_change_sensitivity = HID_DEFAULT_MIN_VALUE;
		magno_feature.flux_min = HID_DEFAULT_MIN_VALUE;
		magno_feature.flux_max = HID_DEFAULT_MAX_VALUE;
		memcpy(feature_report, &magno_feature, sizeof(magno_feature));
		report_size = sizeof(magno_feature);
		break;
	case als_idx:  /* ambient light sensor */
		get_common_features(&als_feature.common_property, report_id);
		als_feature.als_change_sesnitivity = HID_DEFAULT_SENSITIVITY;
		als_feature.als_sensitivity_min = HID_DEFAULT_MIN_VALUE;
		als_feature.als_sensitivity_max = HID_DEFAULT_MAX_VALUE;
		memcpy(feature_report, &als_feature, sizeof(als_feature));
		report_size = sizeof(als_feature);
		break;
	case HPD_IDX:  /* human presence detection sensor */
		get_common_features(&hpd_feature.common_property, report_id);
		memcpy(feature_report, &hpd_feature, sizeof(hpd_feature));
		report_size = sizeof(hpd_feature);
		break;

	default:
		break;
	}
	return report_size;
}

static void get_common_inputs(struct common_input_property *common, int report_id)
{
	common->report_id = report_id;
	common->sensor_state = HID_USAGE_SENSOR_STATE_READY_ENUM;
	common->event_type = HID_USAGE_SENSOR_EVENT_DATA_UPDATED_ENUM;
}

u8 get_input_report(u8 current_index, int sensor_idx, int report_id, struct amd_input_data *in_data)
{
	struct amd_mp2_dev *privdata = container_of(in_data, struct amd_mp2_dev, in_data);
	u32 *sensor_virt_addr = in_data->sensor_virt_addr[current_index];
	u8 *input_report = in_data->input_report[current_index];
	u8 supported_input = privdata->mp2_acs & GENMASK(3, 0);
	struct magno_input_report magno_input;
	struct accel3_input_report acc_input;
	struct gyro_input_report gyro_input;
	struct hpd_input_report hpd_input;
	struct als_input_report als_input;
	struct hpd_status hpdstatus;
	u8 report_size = 0;

	if (!sensor_virt_addr || !input_report)
		return report_size;

	switch (sensor_idx) {
	case accel_idx: /* accel */
		get_common_inputs(&acc_input.common_property, report_id);
		acc_input.in_accel_x_value = (int)sensor_virt_addr[0] / AMD_SFH_FW_MULTIPLIER;
		acc_input.in_accel_y_value = (int)sensor_virt_addr[1] / AMD_SFH_FW_MULTIPLIER;
		acc_input.in_accel_z_value =  (int)sensor_virt_addr[2] / AMD_SFH_FW_MULTIPLIER;
		memcpy(input_report, &acc_input, sizeof(acc_input));
		report_size = sizeof(acc_input);
		break;
	case gyro_idx: /* gyro */
		get_common_inputs(&gyro_input.common_property, report_id);
		gyro_input.in_angel_x_value = (int)sensor_virt_addr[0] / AMD_SFH_FW_MULTIPLIER;
		gyro_input.in_angel_y_value = (int)sensor_virt_addr[1] / AMD_SFH_FW_MULTIPLIER;
		gyro_input.in_angel_z_value =  (int)sensor_virt_addr[2] / AMD_SFH_FW_MULTIPLIER;
		memcpy(input_report, &gyro_input, sizeof(gyro_input));
		report_size = sizeof(gyro_input);
		break;
	case mag_idx: /* Magnetometer */
		get_common_inputs(&magno_input.common_property, report_id);
		magno_input.in_magno_x = (int)sensor_virt_addr[0] / AMD_SFH_FW_MULTIPLIER;
		magno_input.in_magno_y = (int)sensor_virt_addr[1] / AMD_SFH_FW_MULTIPLIER;
		magno_input.in_magno_z = (int)sensor_virt_addr[2] / AMD_SFH_FW_MULTIPLIER;
		magno_input.in_magno_accuracy = (u16)sensor_virt_addr[3] / AMD_SFH_FW_MULTIPLIER;
		memcpy(input_report, &magno_input, sizeof(magno_input));
		report_size = sizeof(magno_input);
		break;
	case als_idx: /* Als */
		get_common_inputs(&als_input.common_property, report_id);
		/* For ALS ,V2 Platforms uses C2P_MSG5 register instead of DRAM access method */
		if (supported_input == V2_STATUS)
			als_input.illuminance_value = (int)readl(privdata->mmio + AMD_C2P_MSG(5));
		else
			als_input.illuminance_value =
				(int)sensor_virt_addr[0] / AMD_SFH_FW_MULTIPLIER;
		report_size = sizeof(als_input);
		memcpy(input_report, &als_input, sizeof(als_input));
		break;
	case HPD_IDX: /* hpd */
		get_common_inputs(&hpd_input.common_property, report_id);
		hpdstatus.val = readl(privdata->mmio + AMD_C2P_MSG(4));
		hpd_input.human_presence = hpdstatus.shpd.human_presence_actual;
		report_size = sizeof(hpd_input);
		memcpy(input_report, &hpd_input, sizeof(hpd_input));
		break;
	default:
		break;
	}
	return report_size;
}
