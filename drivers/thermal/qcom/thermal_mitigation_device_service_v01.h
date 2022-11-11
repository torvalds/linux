/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef THERMAL_MITIGATION_DEVICE_SERVICE_V01_H
#define THERMAL_MITIGATION_DEVICE_SERVICE_V01_H

#define TMD_SERVICE_ID_V01 0x18
#define TMD_SERVICE_VERS_V01 0x01

#define QMI_TMD_GET_MITIGATION_DEVICE_LIST_RESP_V01 0x0020
#define QMI_TMD_GET_MITIGATION_LEVEL_REQ_V01 0x0022
#define QMI_TMD_GET_SUPPORTED_MSGS_REQ_V01 0x001E
#define QMI_TMD_SET_MITIGATION_LEVEL_REQ_V01 0x0021
#define QMI_TMD_REGISTER_NOTIFICATION_MITIGATION_LEVEL_RESP_V01 0x0023
#define QMI_TMD_GET_SUPPORTED_MSGS_RESP_V01 0x001E
#define QMI_TMD_SET_MITIGATION_LEVEL_RESP_V01 0x0021
#define QMI_TMD_DEREGISTER_NOTIFICATION_MITIGATION_LEVEL_RESP_V01 0x0024
#define QMI_TMD_MITIGATION_LEVEL_REPORT_IND_V01 0x0025
#define QMI_TMD_GET_MITIGATION_LEVEL_RESP_V01 0x0022
#define QMI_TMD_GET_SUPPORTED_FIELDS_REQ_V01 0x001F
#define QMI_TMD_GET_MITIGATION_DEVICE_LIST_REQ_V01 0x0020
#define QMI_TMD_REGISTER_NOTIFICATION_MITIGATION_LEVEL_REQ_V01 0x0023
#define QMI_TMD_DEREGISTER_NOTIFICATION_MITIGATION_LEVEL_REQ_V01 0x0024
#define QMI_TMD_GET_SUPPORTED_FIELDS_RESP_V01 0x001F

#define QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01 32
#define QMI_TMD_MITIGATION_DEV_LIST_MAX_V01 32
#define QMI_TMD_MITIGATION_DEV_LIST_EXT01_MAX_V01 64

struct tmd_mitigation_dev_id_type_v01 {
	char mitigation_dev_id[QMI_TMD_MITIGATION_DEV_ID_LENGTH_MAX_V01 + 1];
};

struct tmd_mitigation_dev_list_type_v01 {
	struct tmd_mitigation_dev_id_type_v01 mitigation_dev_id;
	uint8_t max_mitigation_level;
};

struct tmd_get_mitigation_device_list_req_msg_v01 {
	char placeholder;
};
#define TMD_GET_MITIGATION_DEVICE_LIST_REQ_MSG_V01_MAX_MSG_LEN 0
extern struct qmi_elem_info tmd_get_mitigation_device_list_req_msg_v01_ei[];

struct tmd_get_mitigation_device_list_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
	uint8_t mitigation_device_list_valid;
	uint32_t mitigation_device_list_len;
	struct tmd_mitigation_dev_list_type_v01
		mitigation_device_list[QMI_TMD_MITIGATION_DEV_LIST_MAX_V01];
	uint8_t mitigation_device_list_ext01_valid;
	uint32_t mitigation_device_list_ext01_len;
	struct tmd_mitigation_dev_list_type_v01
		mitigation_device_list_ext01[QMI_TMD_MITIGATION_DEV_LIST_EXT01_MAX_V01];
};
#define TMD_GET_MITIGATION_DEVICE_LIST_RESP_MSG_V01_MAX_MSG_LEN 3279
extern struct qmi_elem_info tmd_get_mitigation_device_list_resp_msg_v01_ei[];

struct tmd_set_mitigation_level_req_msg_v01 {
	struct tmd_mitigation_dev_id_type_v01 mitigation_dev_id;
	uint8_t mitigation_level;
};
#define TMD_SET_MITIGATION_LEVEL_REQ_MSG_V01_MAX_MSG_LEN 40
extern struct qmi_elem_info tmd_set_mitigation_level_req_msg_v01_ei[];

struct tmd_set_mitigation_level_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};
#define TMD_SET_MITIGATION_LEVEL_RESP_MSG_V01_MAX_MSG_LEN 7
extern struct qmi_elem_info tmd_set_mitigation_level_resp_msg_v01_ei[];

struct tmd_get_mitigation_level_req_msg_v01 {
	struct tmd_mitigation_dev_id_type_v01 mitigation_device;
};
#define TMD_GET_MITIGATION_LEVEL_REQ_MSG_V01_MAX_MSG_LEN 36
extern struct qmi_elem_info tmd_get_mitigation_level_req_msg_v01_ei[];

struct tmd_get_mitigation_level_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
	uint8_t current_mitigation_level_valid;
	uint8_t current_mitigation_level;
	uint8_t requested_mitigation_level_valid;
	uint8_t requested_mitigation_level;
};
#define TMD_GET_MITIGATION_LEVEL_RESP_MSG_V01_MAX_MSG_LEN 15
extern struct qmi_elem_info tmd_get_mitigation_level_resp_msg_v01_ei[];

struct tmd_register_notification_mitigation_level_req_msg_v01 {
	struct tmd_mitigation_dev_id_type_v01 mitigation_device;
};
#define TMD_REGISTER_NOTIFICATION_MITIGATION_LEVEL_REQ_MSG_V01_MAX_MSG_LEN 36
extern struct qmi_elem_info
		tmd_register_notification_mitigation_level_req_msg_v01_ei[];

struct tmd_register_notification_mitigation_level_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};
#define TMD_REGISTER_NOTIFICATION_MITIGATION_LEVEL_RESP_MSG_V01_MAX_MSG_LEN 7
extern struct qmi_elem_info
	tmd_register_notification_mitigation_level_resp_msg_v01_ei[];

struct tmd_deregister_notification_mitigation_level_req_msg_v01 {
	struct tmd_mitigation_dev_id_type_v01 mitigation_device;
};
#define TMD_DEREGISTER_NOTIFICATION_MITIGATION_LEVEL_REQ_MSG_V01_MAX_MSG_LEN 36
extern struct qmi_elem_info
	tmd_deregister_notification_mitigation_level_req_msg_v01_ei[];

struct tmd_deregister_notification_mitigation_level_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};
#define TMD_DEREGISTER_NOTIFICATION_MITIGATION_LEVEL_RESP_MSG_V01_MAX_MSG_LEN 7
extern struct qmi_elem_info
	tmd_deregister_notification_mitigation_level_resp_msg_v01_ei[];

struct tmd_mitigation_level_report_ind_msg_v01 {
	struct tmd_mitigation_dev_id_type_v01 mitigation_device;
	uint8_t current_mitigation_level;
};
#define TMD_MITIGATION_LEVEL_REPORT_IND_MSG_V01_MAX_MSG_LEN 40
extern struct qmi_elem_info tmd_mitigation_level_report_ind_msg_v01_ei[];

#endif
