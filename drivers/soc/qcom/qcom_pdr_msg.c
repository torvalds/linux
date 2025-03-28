// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/soc/qcom/qmi.h>

#include "pdr_internal.h"

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

const struct qmi_elem_info servreg_get_domain_list_req_ei[] = {
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
EXPORT_SYMBOL_GPL(servreg_get_domain_list_req_ei);

const struct qmi_elem_info servreg_get_domain_list_resp_ei[] = {
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
EXPORT_SYMBOL_GPL(servreg_get_domain_list_resp_ei);

const struct qmi_elem_info servreg_register_listener_req_ei[] = {
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
EXPORT_SYMBOL_GPL(servreg_register_listener_req_ei);

const struct qmi_elem_info servreg_register_listener_resp_ei[] = {
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
EXPORT_SYMBOL_GPL(servreg_register_listener_resp_ei);

const struct qmi_elem_info servreg_restart_pd_req_ei[] = {
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
EXPORT_SYMBOL_GPL(servreg_restart_pd_req_ei);

const struct qmi_elem_info servreg_restart_pd_resp_ei[] = {
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
EXPORT_SYMBOL_GPL(servreg_restart_pd_resp_ei);

const struct qmi_elem_info servreg_state_updated_ind_ei[] = {
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
EXPORT_SYMBOL_GPL(servreg_state_updated_ind_ei);

const struct qmi_elem_info servreg_set_ack_req_ei[] = {
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
EXPORT_SYMBOL_GPL(servreg_set_ack_req_ei);

const struct qmi_elem_info servreg_set_ack_resp_ei[] = {
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
EXPORT_SYMBOL_GPL(servreg_set_ack_resp_ei);

const struct qmi_elem_info servreg_loc_pfr_req_ei[] = {
	{
		.data_type = QMI_STRING,
		.elem_len = SERVREG_NAME_LENGTH + 1,
		.elem_size = sizeof(char),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = 0x01,
		.offset = offsetof(struct servreg_loc_pfr_req, service)
	},
	{
		.data_type = QMI_STRING,
		.elem_len = SERVREG_NAME_LENGTH + 1,
		.elem_size = sizeof(char),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct servreg_loc_pfr_req, reason)
	},
	{}
};
EXPORT_SYMBOL_GPL(servreg_loc_pfr_req_ei);

const struct qmi_elem_info servreg_loc_pfr_resp_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof_field(struct servreg_loc_pfr_resp, rsp),
		.tlv_type = 0x02,
		.offset = offsetof(struct servreg_loc_pfr_resp, rsp),
		.ei_array = qmi_response_type_v01_ei,
	},
	{}
};
EXPORT_SYMBOL_GPL(servreg_loc_pfr_resp_ei);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Protection Domain messages data");
