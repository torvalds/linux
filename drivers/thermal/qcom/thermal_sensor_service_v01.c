// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/soc/qcom/qmi.h>
#include "thermal_sensor_service_v01.h"

static struct qmi_elem_info ts_sensor_type_v01_ei[] = {
	{
		.data_type      = QMI_STRING,
		.elem_len       = QMI_TS_SENSOR_ID_LENGTH_MAX_V01 + 1,
		.elem_size      = sizeof(char),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct ts_sensor_type_v01,
					   sensor_id),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.array_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info ts_get_sensor_list_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.array_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info ts_get_sensor_list_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         =
			offsetof(struct ts_get_sensor_list_resp_msg_v01, resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         =
			offsetof(struct ts_get_sensor_list_resp_msg_v01,
					   sensor_list_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         =
			offsetof(struct ts_get_sensor_list_resp_msg_v01,
					   sensor_list_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = QMI_TS_SENSOR_LIST_MAX_V01,
		.elem_size      = sizeof(struct ts_sensor_type_v01),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x10,
		.offset         =
			offsetof(struct ts_get_sensor_list_resp_msg_v01,
					   sensor_list),
		.ei_array      = ts_sensor_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         =
			offsetof(struct ts_get_sensor_list_resp_msg_v01,
					   sensor_list_ext01_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         =
			offsetof(struct ts_get_sensor_list_resp_msg_v01,
					   sensor_list_ext01_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = QMI_TS_SENSOR_LIST_EXT01_MAX_V01,
		.elem_size      = sizeof(struct ts_sensor_type_v01),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x11,
		.offset         =
			offsetof(struct ts_get_sensor_list_resp_msg_v01,
					   sensor_list_ext01),
		.ei_array      = ts_sensor_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.array_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info ts_register_notification_temp_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct ts_sensor_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(
			struct ts_register_notification_temp_req_msg_v01,
			sensor_id),
		.ei_array      = ts_sensor_type_v01_ei,
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(
			struct ts_register_notification_temp_req_msg_v01,
			send_current_temp_report),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(
			struct ts_register_notification_temp_req_msg_v01,
			temp_threshold_high_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(
			struct ts_register_notification_temp_req_msg_v01,
			temp_threshold_high),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(
			struct ts_register_notification_temp_req_msg_v01,
			temp_threshold_low_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(
			struct ts_register_notification_temp_req_msg_v01,
			temp_threshold_low),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(
			struct ts_register_notification_temp_req_msg_v01,
			seq_num_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(
			struct ts_register_notification_temp_req_msg_v01,
			seq_num),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.array_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info ts_register_notification_temp_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(
			struct ts_register_notification_temp_resp_msg_v01,
			resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.array_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info ts_temp_report_ind_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct ts_sensor_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct ts_temp_report_ind_msg_v01,
					   sensor_id),
		.ei_array      = ts_sensor_type_v01_ei,
	},
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum ts_temp_report_type_enum_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct ts_temp_report_ind_msg_v01,
					   report_type),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct ts_temp_report_ind_msg_v01,
					   temp_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct ts_temp_report_ind_msg_v01,
					   temp),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct ts_temp_report_ind_msg_v01,
					   seq_num_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct ts_temp_report_ind_msg_v01,
					   seq_num),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.array_type       = QMI_COMMON_TLV_TYPE,
	},
};
