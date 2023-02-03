/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QCOM_PDR_HELPER_INTERNAL__
#define __QCOM_PDR_HELPER_INTERNAL__

#include <linux/soc/qcom/pdr.h>

#define SERVREG_LOCATOR_SERVICE				0x40
#define SERVREG_NOTIFIER_SERVICE			0x42

#define SERVREG_REGISTER_LISTENER_REQ			0x20
#define SERVREG_GET_DOMAIN_LIST_REQ			0x21
#define SERVREG_STATE_UPDATED_IND_ID			0x22
#define SERVREG_SET_ACK_REQ				0x23
#define SERVREG_RESTART_PD_REQ				0x24

#define SERVREG_DOMAIN_LIST_LENGTH			32
#define SERVREG_RESTART_PD_REQ_MAX_LEN			67
#define SERVREG_REGISTER_LISTENER_REQ_LEN		71
#define SERVREG_SET_ACK_REQ_LEN				72
#define SERVREG_GET_DOMAIN_LIST_REQ_MAX_LEN		74
#define SERVREG_STATE_UPDATED_IND_MAX_LEN		79
#define SERVREG_GET_DOMAIN_LIST_RESP_MAX_LEN		2389

struct servreg_location_entry {
	char name[SERVREG_NAME_LENGTH + 1];
	u8 service_data_valid;
	u32 service_data;
	u32 instance;
};

static const struct qmi_elem_info servreg_location_entry_ei[] = {
	{
		.data_type      = QMI_STRING,
		.elem_len       = SERVREG_NAME_LENGTH + 1,
		.elem_size      = sizeof(char),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct servreg_location_entry,
					   name),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct servreg_location_entry,
					   instance),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct servreg_location_entry,
					   service_data_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct servreg_location_entry,
					   service_data),
	},
	{}
};

struct servreg_get_domain_list_req {
	char service_name[SERVREG_NAME_LENGTH + 1];
	u8 domain_offset_valid;
	u32 domain_offset;
};

static const struct qmi_elem_info servreg_get_domain_list_req_ei[] = {
	{
		.data_type      = QMI_STRING,
		.elem_len       = SERVREG_NAME_LENGTH + 1,
		.elem_size      = sizeof(char),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct servreg_get_domain_list_req,
					   service_name),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct servreg_get_domain_list_req,
					   domain_offset_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct servreg_get_domain_list_req,
					   domain_offset),
	},
	{}
};

struct servreg_get_domain_list_resp {
	struct qmi_response_type_v01 resp;
	u8 total_domains_valid;
	u16 total_domains;
	u8 db_rev_count_valid;
	u16 db_rev_count;
	u8 domain_list_valid;
	u32 domain_list_len;
	struct servreg_location_entry domain_list[SERVREG_DOMAIN_LIST_LENGTH];
};

static const struct qmi_elem_info servreg_get_domain_list_resp_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct servreg_get_domain_list_resp,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct servreg_get_domain_list_resp,
					   total_domains_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_2_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u16),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct servreg_get_domain_list_resp,
					   total_domains),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct servreg_get_domain_list_resp,
					   db_rev_count_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_2_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u16),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct servreg_get_domain_list_resp,
					   db_rev_count),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct servreg_get_domain_list_resp,
					   domain_list_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct servreg_get_domain_list_resp,
					   domain_list_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = SERVREG_DOMAIN_LIST_LENGTH,
		.elem_size      = sizeof(struct servreg_location_entry),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct servreg_get_domain_list_resp,
					   domain_list),
		.ei_array      = servreg_location_entry_ei,
	},
	{}
};

struct servreg_register_listener_req {
	u8 enable;
	char service_path[SERVREG_NAME_LENGTH + 1];
};

static const struct qmi_elem_info servreg_register_listener_req_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct servreg_register_listener_req,
					   enable),
	},
	{
		.data_type      = QMI_STRING,
		.elem_len       = SERVREG_NAME_LENGTH + 1,
		.elem_size      = sizeof(char),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct servreg_register_listener_req,
					   service_path),
	},
	{}
};

struct servreg_register_listener_resp {
	struct qmi_response_type_v01 resp;
	u8 curr_state_valid;
	enum servreg_service_state curr_state;
};

static const struct qmi_elem_info servreg_register_listener_resp_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct servreg_register_listener_resp,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct servreg_register_listener_resp,
					   curr_state_valid),
	},
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum servreg_service_state),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct servreg_register_listener_resp,
					   curr_state),
	},
	{}
};

struct servreg_restart_pd_req {
	char service_path[SERVREG_NAME_LENGTH + 1];
};

static const struct qmi_elem_info servreg_restart_pd_req_ei[] = {
	{
		.data_type      = QMI_STRING,
		.elem_len       = SERVREG_NAME_LENGTH + 1,
		.elem_size      = sizeof(char),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct servreg_restart_pd_req,
					   service_path),
	},
	{}
};

struct servreg_restart_pd_resp {
	struct qmi_response_type_v01 resp;
};

static const struct qmi_elem_info servreg_restart_pd_resp_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct servreg_restart_pd_resp,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

struct servreg_state_updated_ind {
	enum servreg_service_state curr_state;
	char service_path[SERVREG_NAME_LENGTH + 1];
	u16 transaction_id;
};

static const struct qmi_elem_info servreg_state_updated_ind_ei[] = {
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct servreg_state_updated_ind,
					   curr_state),
	},
	{
		.data_type      = QMI_STRING,
		.elem_len       = SERVREG_NAME_LENGTH + 1,
		.elem_size      = sizeof(char),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct servreg_state_updated_ind,
					   service_path),
	},
	{
		.data_type      = QMI_UNSIGNED_2_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u16),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x03,
		.offset         = offsetof(struct servreg_state_updated_ind,
					   transaction_id),
	},
	{}
};

struct servreg_set_ack_req {
	char service_path[SERVREG_NAME_LENGTH + 1];
	u16 transaction_id;
};

static const struct qmi_elem_info servreg_set_ack_req_ei[] = {
	{
		.data_type      = QMI_STRING,
		.elem_len       = SERVREG_NAME_LENGTH + 1,
		.elem_size      = sizeof(char),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct servreg_set_ack_req,
					   service_path),
	},
	{
		.data_type      = QMI_UNSIGNED_2_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u16),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct servreg_set_ack_req,
					   transaction_id),
	},
	{}
};

struct servreg_set_ack_resp {
	struct qmi_response_type_v01 resp;
};

static const struct qmi_elem_info servreg_set_ack_resp_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct servreg_set_ack_resp,
					   resp),
		.ei_array       = qmi_response_type_v01_ei,
	},
	{}
};

#endif
