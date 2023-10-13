/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef THERMAL_SENSOR_SERVICE_V01_H
#define THERMAL_SENSOR_SERVICE_V01_H

#define TS_SERVICE_ID_V01 0x17
#define TS_SERVICE_VERS_V01 0x01

#define QMI_TS_GET_SENSOR_LIST_RESP_V01 0x0020
#define QMI_TS_GET_SUPPORTED_MSGS_REQ_V01 0x001E
#define QMI_TS_GET_SUPPORTED_MSGS_RESP_V01 0x001E
#define QMI_TS_REGISTER_NOTIFICATION_TEMP_REQ_V01 0x0021
#define QMI_TS_REGISTER_NOTIFICATION_TEMP_RESP_V01 0x0021
#define QMI_TS_GET_SUPPORTED_FIELDS_RESP_V01 0x001F
#define QMI_TS_GET_SENSOR_LIST_REQ_V01 0x0020
#define QMI_TS_TEMP_REPORT_IND_V01 0x0022
#define QMI_TS_GET_SUPPORTED_FIELDS_REQ_V01 0x001F

#define QMI_TS_SENSOR_ID_LENGTH_MAX_V01 32
#define QMI_TS_SENSOR_LIST_MAX_V01 32
#define QMI_TS_SENSOR_LIST_EXT01_MAX_V01 64

struct ts_sensor_type_v01 {
	char sensor_id[QMI_TS_SENSOR_ID_LENGTH_MAX_V01 + 1];
};

struct ts_get_sensor_list_req_msg_v01 {
	char placeholder;
};
#define TS_GET_SENSOR_LIST_REQ_MSG_V01_MAX_MSG_LEN 0
extern struct qmi_elem_info ts_get_sensor_list_req_msg_v01_ei[];

struct ts_get_sensor_list_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
	u8 sensor_list_valid;
	u32 sensor_list_len;
	struct ts_sensor_type_v01 sensor_list[QMI_TS_SENSOR_LIST_MAX_V01];
	u8 sensor_list_ext01_valid;
	u32 sensor_list_ext01_len;
	struct ts_sensor_type_v01 sensor_list_ext01[QMI_TS_SENSOR_LIST_EXT01_MAX_V01];
};
#define TS_GET_SENSOR_LIST_RESP_MSG_V01_MAX_MSG_LEN 1067
extern struct qmi_elem_info ts_get_sensor_list_resp_msg_v01_ei[];

struct ts_register_notification_temp_req_msg_v01 {
	struct ts_sensor_type_v01 sensor_id;
	u8 send_current_temp_report;
	u8 temp_threshold_high_valid;
	int temp_threshold_high;
	u8 temp_threshold_low_valid;
	int temp_threshold_low;
	u8 seq_num_valid;
	u32 seq_num;
};
#define TS_REGISTER_NOTIFICATION_TEMP_REQ_MSG_V01_MAX_MSG_LEN 61
extern struct qmi_elem_info ts_register_notification_temp_req_msg_v01_ei[];

struct ts_register_notification_temp_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};
#define TS_REGISTER_NOTIFICATION_TEMP_RESP_MSG_V01_MAX_MSG_LEN 7
extern struct qmi_elem_info ts_register_notification_temp_resp_msg_v01_ei[];

enum ts_temp_report_type_enum_v01 {
	TS_TEMP_REPORT_TYPE_ENUM_MIN_VAL_V01 = INT_MIN,
	QMI_TS_TEMP_REPORT_CURRENT_TEMP_V01 = 0,
	QMI_TS_TEMP_REPORT_THRESHOLD_HIGH_V01 = 1,
	QMI_TS_TEMP_REPORT_THRESHOLD_LOW_V01 = 2,
	TS_TEMP_REPORT_TYPE_ENUM_MAX_VAL_V01 = INT_MAX,
};

struct ts_temp_report_ind_msg_v01 {
	struct ts_sensor_type_v01 sensor_id;
	enum ts_temp_report_type_enum_v01 report_type;
	u8 temp_valid;
	int temp;
	u8 seq_num_valid;
	u32 seq_num;
};
#define TS_TEMP_REPORT_IND_MSG_V01_MAX_MSG_LEN 57
extern struct qmi_elem_info ts_temp_report_ind_msg_v01_ei[];

#endif
