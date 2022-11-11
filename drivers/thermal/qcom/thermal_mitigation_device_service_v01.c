// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/soc/qcom/qmi.h>

#include "thermal_mitigation_device_service_v01.h"

static struct qmi_elem_info tmd_mitigation_dev_id_type_v01_ei[] = {
	{
		.data_type      = QMI_STRING,
		.elem_len       = QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01 + 1,
		.elem_size      = sizeof(char),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(
					struct tmd_mitigation_dev_id_type_v01,
					mitigation_dev_id),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info tmd_mitigation_dev_list_type_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct tmd_mitigation_dev_id_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(
					struct tmd_mitigation_dev_list_type_v01,
					mitigation_dev_id),
		.ei_array      = tmd_mitigation_dev_id_type_v01_ei,
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(
					struct tmd_mitigation_dev_list_type_v01,
					max_mitigation_level),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info tmd_get_mitigation_device_list_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info tmd_get_mitigation_device_list_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(
			struct tmd_get_mitigation_device_list_resp_msg_v01,
			resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(
			struct tmd_get_mitigation_device_list_resp_msg_v01,
				mitigation_device_list_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(
			struct tmd_get_mitigation_device_list_resp_msg_v01,
				mitigation_device_list_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = QMI_TMD_MITIGATION_DEV_LIST_MAX_V01,
		.elem_size      = sizeof(
				struct tmd_mitigation_dev_list_type_v01),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(
			struct tmd_get_mitigation_device_list_resp_msg_v01,
				mitigation_device_list),
		.ei_array      = tmd_mitigation_dev_list_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(
			struct tmd_get_mitigation_device_list_resp_msg_v01,
				mitigation_device_list_ext01_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(
			struct tmd_get_mitigation_device_list_resp_msg_v01,
				mitigation_device_list_ext01_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = QMI_TMD_MITIGATION_DEV_LIST_EXT01_MAX_V01,
		.elem_size      = sizeof(
				struct tmd_mitigation_dev_list_type_v01),
		.array_type     = VAR_LEN_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(
			struct tmd_get_mitigation_device_list_resp_msg_v01,
				mitigation_device_list_ext01),
		.ei_array      = tmd_mitigation_dev_list_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info tmd_set_mitigation_level_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct tmd_mitigation_dev_id_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(
				struct tmd_set_mitigation_level_req_msg_v01,
					mitigation_dev_id),
		.ei_array      = tmd_mitigation_dev_id_type_v01_ei,
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(
				struct tmd_set_mitigation_level_req_msg_v01,
					mitigation_level),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info tmd_set_mitigation_level_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(
			struct tmd_set_mitigation_level_resp_msg_v01,
				resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info tmd_get_mitigation_level_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct tmd_mitigation_dev_id_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(
				struct tmd_get_mitigation_level_req_msg_v01,
					mitigation_device),
		.ei_array      = tmd_mitigation_dev_id_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info tmd_get_mitigation_level_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(
				struct tmd_get_mitigation_level_resp_msg_v01,
					resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(
				struct tmd_get_mitigation_level_resp_msg_v01,
					current_mitigation_level_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(
				struct tmd_get_mitigation_level_resp_msg_v01,
					current_mitigation_level),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(
				struct tmd_get_mitigation_level_resp_msg_v01,
					requested_mitigation_level_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(
				struct tmd_get_mitigation_level_resp_msg_v01,
					requested_mitigation_level),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info
	tmd_register_notification_mitigation_level_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct tmd_mitigation_dev_id_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(
		struct tmd_register_notification_mitigation_level_req_msg_v01,
				mitigation_device),
		.ei_array      = tmd_mitigation_dev_id_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info
	tmd_register_notification_mitigation_level_resp_msg_v01_ei[]
									= {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(
		struct tmd_register_notification_mitigation_level_resp_msg_v01,
				resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info
	tmd_deregister_notification_mitigation_level_req_msg_v01_ei[]
									= {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct tmd_mitigation_dev_id_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct
		tmd_deregister_notification_mitigation_level_req_msg_v01,
				mitigation_device),
		.ei_array      = tmd_mitigation_dev_id_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info
	tmd_deregister_notification_mitigation_level_resp_msg_v01_ei[]
									= {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct
		tmd_deregister_notification_mitigation_level_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

struct qmi_elem_info tmd_mitigation_level_report_ind_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct tmd_mitigation_dev_id_type_v01),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(
				struct tmd_mitigation_level_report_ind_msg_v01,
					mitigation_device),
		.ei_array      = tmd_mitigation_dev_id_type_v01_ei,
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type       = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(
				struct tmd_mitigation_level_report_ind_msg_v01,
					   current_mitigation_level),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type       = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};

