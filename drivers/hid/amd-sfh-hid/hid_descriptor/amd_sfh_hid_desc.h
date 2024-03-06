/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HID report descriptors, structures and routines
 * Copyright 2020-2021 Advanced Micro Devices, Inc.
 * Authors: Nehal Bakulchandra Shah <Nehal-bakulchandra.shah@amd.com>
 *	    Sandeep Singh <Sandeep.singh@amd.com>
 *	    Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */

#ifndef AMD_SFH_HID_DESCRIPTOR_H
#define AMD_SFH_HID_DESCRIPTOR_H

enum desc_type {
	/* Report descriptor name */
	descr_size = 1,
	input_size,
	feature_size,
};

struct common_feature_property {
	/* common properties */
	u8 report_id;
	u8 connection_type;
	u8 report_state;
	u8 power_state;
	u8 sensor_state;
	u32 report_interval;
} __packed;

struct common_input_property {
	/* common properties */
	u8 report_id;
	u8 sensor_state;
	u8 event_type;
} __packed;

struct accel3_feature_report {
	struct common_feature_property common_property;
	/* properties specific to this sensor */
	u16 accel_change_sesnitivity;
	s16 accel_sensitivity_max;
	s16 accel_sensitivity_min;
} __packed;

struct accel3_input_report {
	struct	common_input_property common_property;
	/* values specific to this sensor */
	int in_accel_x_value;
	int in_accel_y_value;
	int in_accel_z_value;
	/* include if required to support the "shake" event */
	u8 in_accel_shake_detection;
} __packed;

struct gyro_feature_report {
	struct common_feature_property common_property;
	/* properties specific to this sensor */
	u16 gyro_change_sesnitivity;
	s16 gyro_sensitivity_max;
	s16 gyro_sensitivity_min;
} __packed;

struct gyro_input_report {
	struct	common_input_property common_property;
	/* values specific to this sensor */
	int in_angel_x_value;
	int in_angel_y_value;
	int in_angel_z_value;
} __packed;

struct magno_feature_report {
	struct common_feature_property common_property;
	/*properties specific to this sensor */
	u16 magno_headingchange_sensitivity;
	s16 heading_min;
	s16 heading_max;
	u16 flux_change_sensitivity;
	s16 flux_min;
	s16 flux_max;
} __packed;

struct magno_input_report {
	struct	common_input_property common_property;
	int in_magno_x;
	int in_magno_y;
	int in_magno_z;
	int in_magno_accuracy;
} __packed;

struct als_feature_report {
	struct common_feature_property common_property;
	/* properties specific to this sensor */
	u16 als_change_sesnitivity;
	s16 als_sensitivity_max;
	s16 als_sensitivity_min;
} __packed;

struct als_input_report {
	struct common_input_property common_property;
	/* values specific to this sensor */
	int illuminance_value;
	int light_color_temp;
	int chromaticity_x_value;
	int chromaticity_y_value;
} __packed;

struct hpd_feature_report {
	struct common_feature_property common_property;
} __packed;

struct hpd_input_report {
	struct common_input_property common_property;
	 /* values specific to human presence sensor */
	u8 human_presence;
} __packed;

#endif
