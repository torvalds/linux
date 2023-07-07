// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD MP2 1.1 descriptor interfaces
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */

#include <linux/hid-sensor-ids.h>

#include "amd_sfh_interface.h"
#include "../hid_descriptor/amd_sfh_hid_desc.h"
#include "../hid_descriptor/amd_sfh_hid_report_desc.h"

#define SENSOR_PROP_REPORTING_STATE_ALL_EVENTS_ENUM			0x41
#define SENSOR_PROP_POWER_STATE_D0_FULL_POWER_ENUM			0x51
#define HID_DEFAULT_REPORT_INTERVAL					0x50
#define HID_DEFAULT_MIN_VALUE						0X7F
#define HID_DEFAULT_MAX_VALUE						0x80
#define HID_DEFAULT_SENSITIVITY						0x7F
#define HID_USAGE_SENSOR_PROPERTY_CONNECTION_TYPE_PC_INTEGRATED_ENUM	0x01
/* state enums */
#define HID_USAGE_SENSOR_STATE_READY_ENUM				0x02
#define HID_USAGE_SENSOR_STATE_INITIALIZING_ENUM			0x05
#define HID_USAGE_SENSOR_EVENT_DATA_UPDATED_ENUM			0x04

static int get_report_desc(int sensor_idx, u8 *rep_desc)
{
	switch (sensor_idx) {
	case ACCEL_IDX: /* accelerometer */
		memset(rep_desc, 0, sizeof(accel3_report_descriptor));
		memcpy(rep_desc, accel3_report_descriptor,
		       sizeof(accel3_report_descriptor));
		break;
	case GYRO_IDX: /* gyroscope */
		memset(rep_desc, 0, sizeof(gyro3_report_descriptor));
		memcpy(rep_desc, gyro3_report_descriptor,
		       sizeof(gyro3_report_descriptor));
		break;
	case MAG_IDX: /* magnetometer */
		memset(rep_desc, 0, sizeof(comp3_report_descriptor));
		memcpy(rep_desc, comp3_report_descriptor,
		       sizeof(comp3_report_descriptor));
		break;
	case ALS_IDX: /* ambient light sensor */
		memset(rep_desc, 0, sizeof(als_report_descriptor));
		memcpy(rep_desc, als_report_descriptor,
		       sizeof(als_report_descriptor));
		break;
	case HPD_IDX: /* HPD sensor */
		memset(rep_desc, 0, sizeof(hpd_report_descriptor));
		memcpy(rep_desc, hpd_report_descriptor,
		       sizeof(hpd_report_descriptor));
		break;
	}
	return 0;
}

static void get_common_features(struct common_feature_property *common, int report_id)
{
	common->report_id = report_id;
	common->connection_type = HID_USAGE_SENSOR_PROPERTY_CONNECTION_TYPE_PC_INTEGRATED_ENUM;
	common->report_state = SENSOR_PROP_REPORTING_STATE_ALL_EVENTS_ENUM;
	common->power_state = SENSOR_PROP_POWER_STATE_D0_FULL_POWER_ENUM;
	common->sensor_state = HID_USAGE_SENSOR_STATE_INITIALIZING_ENUM;
	common->report_interval =  HID_DEFAULT_REPORT_INTERVAL;
}

static u8 get_feature_rep(int sensor_idx, int report_id, u8 *feature_report)
{
	struct magno_feature_report magno_feature;
	struct accel3_feature_report acc_feature;
	struct gyro_feature_report gyro_feature;
	struct hpd_feature_report hpd_feature;
	struct als_feature_report als_feature;
	u8 report_size = 0;

	if (!feature_report)
		return report_size;

	switch (sensor_idx) {
	case ACCEL_IDX: /* accelerometer */
		get_common_features(&acc_feature.common_property, report_id);
		acc_feature.accel_change_sesnitivity = HID_DEFAULT_SENSITIVITY;
		acc_feature.accel_sensitivity_min = HID_DEFAULT_MIN_VALUE;
		acc_feature.accel_sensitivity_max = HID_DEFAULT_MAX_VALUE;
		memcpy(feature_report, &acc_feature, sizeof(acc_feature));
		report_size = sizeof(acc_feature);
		break;
	case GYRO_IDX: /* gyroscope */
		get_common_features(&gyro_feature.common_property, report_id);
		gyro_feature.gyro_change_sesnitivity = HID_DEFAULT_SENSITIVITY;
		gyro_feature.gyro_sensitivity_min = HID_DEFAULT_MIN_VALUE;
		gyro_feature.gyro_sensitivity_max = HID_DEFAULT_MAX_VALUE;
		memcpy(feature_report, &gyro_feature, sizeof(gyro_feature));
		report_size = sizeof(gyro_feature);
		break;
	case MAG_IDX: /* magnetometer */
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
	case ALS_IDX:  /* ambient light sensor */
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
	}
	return report_size;
}

static void get_common_inputs(struct common_input_property *common, int report_id)
{
	common->report_id = report_id;
	common->sensor_state = HID_USAGE_SENSOR_STATE_READY_ENUM;
	common->event_type = HID_USAGE_SENSOR_EVENT_DATA_UPDATED_ENUM;
}

static int float_to_int(u32 flt32_val)
{
	int fraction, shift, mantissa, sign, exp, zeropre;

	mantissa = flt32_val & GENMASK(22, 0);
	sign = (flt32_val & BIT(31)) ? -1 : 1;
	exp = (flt32_val & ~BIT(31)) >> 23;

	if (!exp && !mantissa)
		return 0;

	exp -= 127;
	if (exp < 0) {
		exp = -exp;
		zeropre = (((BIT(23) + mantissa) * 100) >> 23) >> exp;
		return zeropre >= 50 ? sign : 0;
	}

	shift = 23 - exp;
	flt32_val = BIT(exp) + (mantissa >> shift);
	fraction = mantissa & GENMASK(shift - 1, 0);

	return (((fraction * 100) >> shift) >= 50) ? sign * (flt32_val + 1) : sign * flt32_val;
}

static u8 get_input_rep(u8 current_index, int sensor_idx, int report_id,
			struct amd_input_data *in_data)
{
	struct amd_mp2_dev *mp2 = container_of(in_data, struct amd_mp2_dev, in_data);
	u8 *input_report = in_data->input_report[current_index];
	struct magno_input_report magno_input;
	struct accel3_input_report acc_input;
	struct gyro_input_report gyro_input;
	struct als_input_report als_input;
	struct hpd_input_report hpd_input;
	struct sfh_accel_data accel_data;
	struct sfh_gyro_data gyro_data;
	struct sfh_mag_data mag_data;
	struct sfh_als_data als_data;
	struct hpd_status hpdstatus;
	void __iomem *sensoraddr;
	u8 report_size = 0;

	if (!input_report)
		return report_size;

	switch (sensor_idx) {
	case ACCEL_IDX: /* accelerometer */
		sensoraddr = mp2->vsbase + (ACCEL_IDX * SENSOR_DATA_MEM_SIZE_DEFAULT) +
			     OFFSET_SENSOR_DATA_DEFAULT;
		memcpy_fromio(&accel_data, sensoraddr, sizeof(struct sfh_accel_data));
		get_common_inputs(&acc_input.common_property, report_id);
		acc_input.in_accel_x_value = float_to_int(accel_data.acceldata.x) / 100;
		acc_input.in_accel_y_value = float_to_int(accel_data.acceldata.y) / 100;
		acc_input.in_accel_z_value = float_to_int(accel_data.acceldata.z) / 100;
		memcpy(input_report, &acc_input, sizeof(acc_input));
		report_size = sizeof(acc_input);
		break;
	case GYRO_IDX: /* gyroscope */
		sensoraddr = mp2->vsbase + (GYRO_IDX * SENSOR_DATA_MEM_SIZE_DEFAULT) +
			     OFFSET_SENSOR_DATA_DEFAULT;
		memcpy_fromio(&gyro_data, sensoraddr, sizeof(struct sfh_gyro_data));
		get_common_inputs(&gyro_input.common_property, report_id);
		gyro_input.in_angel_x_value = float_to_int(gyro_data.gyrodata.x) / 1000;
		gyro_input.in_angel_y_value = float_to_int(gyro_data.gyrodata.y) / 1000;
		gyro_input.in_angel_z_value = float_to_int(gyro_data.gyrodata.z) / 1000;
		memcpy(input_report, &gyro_input, sizeof(gyro_input));
		report_size = sizeof(gyro_input);
		break;
	case MAG_IDX: /* magnetometer */
		sensoraddr = mp2->vsbase + (MAG_IDX * SENSOR_DATA_MEM_SIZE_DEFAULT) +
			     OFFSET_SENSOR_DATA_DEFAULT;
		memcpy_fromio(&mag_data, sensoraddr, sizeof(struct sfh_mag_data));
		get_common_inputs(&magno_input.common_property, report_id);
		magno_input.in_magno_x = float_to_int(mag_data.magdata.x) / 100;
		magno_input.in_magno_y = float_to_int(mag_data.magdata.y) / 100;
		magno_input.in_magno_z = float_to_int(mag_data.magdata.z) / 100;
		magno_input.in_magno_accuracy = mag_data.accuracy / 100;
		memcpy(input_report, &magno_input, sizeof(magno_input));
		report_size = sizeof(magno_input);
		break;
	case ALS_IDX:
		sensoraddr = mp2->vsbase + (ALS_IDX * SENSOR_DATA_MEM_SIZE_DEFAULT) +
			     OFFSET_SENSOR_DATA_DEFAULT;
		memcpy_fromio(&als_data, sensoraddr, sizeof(struct sfh_als_data));
		get_common_inputs(&als_input.common_property, report_id);
		als_input.illuminance_value = float_to_int(als_data.lux);
		report_size = sizeof(als_input);
		memcpy(input_report, &als_input, sizeof(als_input));
		break;
	case HPD_IDX:
		get_common_inputs(&hpd_input.common_property, report_id);
		hpdstatus.val = readl(mp2->mmio + AMD_C2P_MSG(4));
		hpd_input.human_presence = hpdstatus.shpd.presence;
		report_size = sizeof(hpd_input);
		memcpy(input_report, &hpd_input, sizeof(hpd_input));
		break;
	}
	return report_size;
}

static u32 get_desc_size(int sensor_idx, int descriptor_name)
{
	switch (sensor_idx) {
	case ACCEL_IDX:
		switch (descriptor_name) {
		case descr_size:
			return sizeof(accel3_report_descriptor);
		case input_size:
			return sizeof(struct accel3_input_report);
		case feature_size:
			return sizeof(struct accel3_feature_report);
		}
		break;
	case GYRO_IDX:
		switch (descriptor_name) {
		case descr_size:
			return sizeof(gyro3_report_descriptor);
		case input_size:
			return sizeof(struct gyro_input_report);
		case feature_size:
			return sizeof(struct gyro_feature_report);
		}
		break;
	case MAG_IDX:
		switch (descriptor_name) {
		case descr_size:
			return sizeof(comp3_report_descriptor);
		case input_size:
			return sizeof(struct magno_input_report);
		case feature_size:
			return sizeof(struct magno_feature_report);
		}
		break;
	case ALS_IDX:
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
	}

	return 0;
}

void amd_sfh1_1_set_desc_ops(struct amd_mp2_ops *mp2_ops)
{
	mp2_ops->get_rep_desc = get_report_desc;
	mp2_ops->get_feat_rep = get_feature_rep;
	mp2_ops->get_desc_sz = get_desc_size;
	mp2_ops->get_in_rep = get_input_rep;
}
