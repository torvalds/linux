// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/soc/qcom/qmi.h>
#include <linux/types.h>

#include "system_health_monitor_v01.h"

struct qmi_elem_info hmon_register_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct hmon_register_req_msg_v01,
					   name_valid),
	},
	{
		.data_type      = QMI_STRING,
		.elem_len       = 255,
		.elem_size      = sizeof(char),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct hmon_register_req_msg_v01,
					   name),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct hmon_register_req_msg_v01,
					   timeout_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint32_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct hmon_register_req_msg_v01,
					   timeout),
	},
	{}
};

struct qmi_elem_info hmon_register_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct hmon_register_resp_msg_v01, resp),
		.tlv_type       = 0x02,
		.offset         = offsetof(struct hmon_register_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

struct qmi_elem_info hmon_health_check_ind_msg_v01_ei[] = {
	{}
};

struct qmi_elem_info hmon_health_check_complete_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct hmon_health_check_complete_req_msg_v01,
					   result_valid),
	},
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum hmon_check_result_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct hmon_health_check_complete_req_msg_v01,
					   result),
	},
	{}
};

struct qmi_elem_info hmon_health_check_complete_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct hmon_health_check_complete_resp_msg_v01,
					   resp),
		.tlv_type       = 0x02,
		.offset         = offsetof(struct hmon_health_check_complete_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};
