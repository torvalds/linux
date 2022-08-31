/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CORESIGHT_QMI_H
#define _CORESIGHT_QMI_H

#include <linux/soc/qcom/qmi.h>

#define CORESIGHT_QMI_SVC_ID			(0x33)
#define CORESIGHT_QMI_VERSION			(1)

#define CORESIGHT_QMI_GET_ETM_REQ_V01		(0x002B)
#define CORESIGHT_QMI_GET_ETM_RESP_V01		(0x002B)
#define CORESIGHT_QMI_SET_ETM_REQ_V01		(0x002C)
#define CORESIGHT_QMI_SET_ETM_RESP_V01		(0x002C)
#define CORESIGHT_QMI_ETR_ASSIGN_REQ_V01		(0x0042)
#define CORESIGHT_QMI_ETR_ASSIGN_RESP_V01		(0x0042)

#define CORESIGHT_QMI_GET_ETM_REQ_MAX_LEN	(0)
#define CORESIGHT_QMI_GET_ETM_RESP_MAX_LEN	(14)
#define CORESIGHT_QMI_SET_ETM_REQ_MAX_LEN	(7)
#define CORESIGHT_QMI_SET_ETM_RESP_MAX_LEN	(7)
#define CORESIGHT_QMI_ETR_ASSIGN_REQ_MAX_LEN	(36)
#define CORESIGHT_QMI_ETR_ASSIGN_RESP_MAX_LEN	(7)

#define TIMEOUT_MS				(10000)

enum coresight_etm_state_enum_type_v01 {
	/* To force a 32 bit signed enum. Do not change or use */
	CORESIGHT_ETM_STATE_ENUM_TYPE_MIN_ENUM_VAL_V01 = INT_MIN,
	CORESIGHT_ETM_STATE_DISABLED_V01 = 0,
	CORESIGHT_ETM_STATE_ENABLED_V01 = 1,
	CORESIGHT_ETM_STATE_ENUM_TYPE_MAX_ENUM_VAL_01 = INT_MAX,
};

struct coresight_get_etm_req_msg_v01 {
	/*
	 * This element is a placeholder to prevent declaration of
	 * empty struct. Do not change.
	 */
	char __placeholder;
};

struct coresight_get_etm_resp_msg_v01 {
	/* Mandatory */
	/* QMI result Code */
	struct qmi_response_type_v01 resp;

	/* Optional */
	/* ETM output state, must be set to true if state is being passed */
	uint8_t state_valid;
	/* Present when result code is QMI_RESULT_SUCCESS */
	enum coresight_etm_state_enum_type_v01 state;
};

struct coresight_set_etm_req_msg_v01 {
	/* Mandatory */
	/* ETM output state */
	enum coresight_etm_state_enum_type_v01 state;
};

struct coresight_set_etm_resp_msg_v01 {
	/* Mandatory */
	struct qmi_response_type_v01 resp;
};

struct coresight_etr_assign_req_msg_v01 {
	u32 etr_id;
	u32 subsys_id;
	u64 buffer_base;
	u64 buffer_size;
};

struct coresight_etr_assign_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

static struct qmi_elem_info coresight_etr_assign_req_msg_v01_ei[] = {
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len  = 1,
		.elem_size = 4,
		.array_type  = NO_ARRAY,
		.tlv_type  = 0x01,
		.offset    = offsetof(struct coresight_etr_assign_req_msg_v01,
				      etr_id),
		.ei_array  = NULL,
	},
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len  = 1,
		.elem_size = 4,
		.array_type  = NO_ARRAY,
		.tlv_type  = 0x02,
		.offset    = offsetof(struct coresight_etr_assign_req_msg_v01,
				      subsys_id),
		.ei_array  = NULL,
	},
	{
		.data_type = QMI_UNSIGNED_8_BYTE,
		.elem_len  = 1,
		.elem_size = 8,
		.array_type  = NO_ARRAY,
		.tlv_type  = 0x03,
		.offset    = offsetof(struct coresight_etr_assign_req_msg_v01,
				      buffer_base),
		.ei_array  = NULL,
	},
	{
		.data_type = QMI_UNSIGNED_8_BYTE,
		.elem_len  = 1,
		.elem_size = 8,
		.array_type  = NO_ARRAY,
		.tlv_type  = 0x04,
		.offset    = offsetof(struct coresight_etr_assign_req_msg_v01,
				      buffer_size),
		.ei_array  = NULL,
	},
	{
		.data_type = QMI_EOTI,
		.elem_len  = 0,
		.elem_size = 0,
		.array_type  = NO_ARRAY,
		.tlv_type  = 0,
		.offset    = 0,
		.ei_array  = NULL,
	},
};

static struct qmi_elem_info coresight_etr_assign_resp_msg_v01_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len  = 1,
		.elem_size = sizeof(struct qmi_response_type_v01),
		.array_type  = NO_ARRAY,
		.tlv_type  = 0x02,
		.offset    = offsetof(struct coresight_etr_assign_resp_msg_v01,
				      resp),
		.ei_array  = qmi_response_type_v01_ei,
	},
	{
		.data_type = QMI_EOTI,
		.elem_len  = 0,
		.elem_size = 0,
		.array_type  = NO_ARRAY,
		.tlv_type  = 0,
		.offset    = 0,
		.ei_array  = NULL,
	},
};

static struct qmi_elem_info coresight_set_etm_req_msg_v01_ei[] = {
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len  = 1,
		.elem_size = sizeof(enum coresight_etm_state_enum_type_v01),
		.array_type  = NO_ARRAY,
		.tlv_type  = 0x01,
		.offset    = offsetof(struct coresight_set_etm_req_msg_v01,
				      state),
		.ei_array  = NULL,
	},
	{
		.data_type = QMI_EOTI,
		.elem_len  = 0,
		.elem_size = 0,
		.array_type  = NO_ARRAY,
		.tlv_type  = 0,
		.offset    = 0,
		.ei_array  = NULL,
	},
};

static struct qmi_elem_info coresight_set_etm_resp_msg_v01_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len  = 1,
		.elem_size = sizeof(struct qmi_response_type_v01),
		.array_type  = NO_ARRAY,
		.tlv_type  = 0x02,
		.offset    = offsetof(struct coresight_set_etm_resp_msg_v01,
				      resp),
		.ei_array  = qmi_response_type_v01_ei,
	},
	{
		.data_type = QMI_EOTI,
		.elem_len  = 0,
		.elem_size = 0,
		.array_type  = NO_ARRAY,
		.tlv_type  = 0,
		.offset    = 0,
		.ei_array  = NULL,
	},
};

#endif
