// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2013-2015, 2017-2018, The Linux Foundation. All rights reserved.

#include <linux/stddef.h>
#include <linux/soc/qcom/qmi.h>

#include "memshare_qmi_msg.h"

struct qmi_elem_info mem_alloc_req_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_req_msg_v01, num_bytes),
		.tlv_type       = 0x01,
		.offset         = offsetof(struct mem_alloc_req_msg_v01, num_bytes),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_req_msg_v01, block_alignment_valid),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_alloc_req_msg_v01, block_alignment_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_req_msg_v01, block_alignment),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_alloc_req_msg_v01, block_alignment),
	},
	{
		.data_type      = QMI_EOTI,
	},
};

struct qmi_elem_info mem_alloc_resp_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_SIGNED_2_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_resp_msg_v01, resp),
		.tlv_type       = 0x01,
		.offset         = offsetof(struct mem_alloc_resp_msg_v01, resp),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_resp_msg_v01, handle_valid),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_alloc_resp_msg_v01, handle_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_resp_msg_v01, handle),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_alloc_resp_msg_v01, handle),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_resp_msg_v01, num_bytes_valid),
		.tlv_type       = 0x11,
		.offset         = offsetof(struct mem_alloc_resp_msg_v01, num_bytes_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_resp_msg_v01, num_bytes),
		.tlv_type       = 0x11,
		.offset         = offsetof(struct mem_alloc_resp_msg_v01, num_bytes),
	},
	{
		.data_type      = QMI_EOTI,
	},
};

struct qmi_elem_info mem_free_req_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_free_req_msg_v01, handle),
		.tlv_type       = 0x01,
		.offset         = offsetof(struct mem_free_req_msg_v01, handle),
	},
	{
		.data_type      = QMI_EOTI,
	},
};

struct qmi_elem_info mem_free_resp_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_SIGNED_2_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_free_resp_msg_v01, resp),
		.tlv_type       = 0x01,
		.offset         = offsetof(struct mem_free_resp_msg_v01, resp),
	},
	{
		.data_type      = QMI_EOTI,
	},
};

struct qmi_elem_info dhms_mem_alloc_addr_info_type_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct dhms_mem_alloc_addr_info_type_v01, phy_addr),
		.tlv_type       = QMI_COMMON_TLV_TYPE,
		.offset         = offsetof(struct dhms_mem_alloc_addr_info_type_v01, phy_addr),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct dhms_mem_alloc_addr_info_type_v01, num_bytes),
		.tlv_type       = QMI_COMMON_TLV_TYPE,
		.offset         = offsetof(struct dhms_mem_alloc_addr_info_type_v01, num_bytes),
	},
	{
		.data_type      = QMI_EOTI,
	},
};

struct qmi_elem_info mem_alloc_generic_req_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_generic_req_msg_v01, num_bytes),
		.tlv_type       = 0x01,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01, num_bytes),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_generic_req_msg_v01, client_id),
		.tlv_type       = 0x02,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01, client_id),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_generic_req_msg_v01, proc_id),
		.tlv_type       = 0x03,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01, proc_id),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_generic_req_msg_v01, sequence_id),
		.tlv_type       = 0x04,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01, sequence_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_generic_req_msg_v01,
					       alloc_contiguous_valid),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01,
					   alloc_contiguous_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_generic_req_msg_v01,
					       alloc_contiguous),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01, alloc_contiguous),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_generic_req_msg_v01,
					       block_alignment_valid),
		.tlv_type       = 0x11,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01,
					   block_alignment_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_generic_req_msg_v01,
					       block_alignment),
		.tlv_type       = 0x11,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01, block_alignment),
	},
	{
		.data_type      = QMI_EOTI,
	},
};

struct qmi_elem_info mem_alloc_generic_resp_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_generic_resp_msg_v01, resp),
		.tlv_type       = 0x02,
		.offset         = offsetof(struct mem_alloc_generic_resp_msg_v01, resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_generic_resp_msg_v01,
					       sequence_id_valid),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_alloc_generic_resp_msg_v01, sequence_id_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_generic_resp_msg_v01, sequence_id),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_alloc_generic_resp_msg_v01, sequence_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_generic_resp_msg_v01,
					       dhms_mem_alloc_addr_info_valid),
		.tlv_type       = 0x11,
		.offset         = offsetof(struct mem_alloc_generic_resp_msg_v01,
					   dhms_mem_alloc_addr_info_valid),
	},
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_alloc_generic_resp_msg_v01,
					       dhms_mem_alloc_addr_info_len),
		.tlv_type       = 0x11,
		.offset         = offsetof(struct mem_alloc_generic_resp_msg_v01,
					   dhms_mem_alloc_addr_info_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = MAX_ARR_CNT_V01,
		.elem_size      = sizeof_field(struct mem_alloc_generic_resp_msg_v01,
					       dhms_mem_alloc_addr_info),
		.array_type     = VAR_LEN_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct mem_alloc_generic_resp_msg_v01,
					   dhms_mem_alloc_addr_info),
		.ei_array       = dhms_mem_alloc_addr_info_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
	},
};

struct qmi_elem_info mem_free_generic_req_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_free_generic_req_msg_v01,
					       dhms_mem_alloc_addr_info_len),
		.tlv_type       = 0x01,
		.offset         = offsetof(struct mem_free_generic_req_msg_v01,
					   dhms_mem_alloc_addr_info_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = MAX_ARR_CNT_V01,
		.elem_size      = sizeof_field(struct mem_free_generic_req_msg_v01,
					       dhms_mem_alloc_addr_info),
		.array_type     = VAR_LEN_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct mem_free_generic_req_msg_v01,
					   dhms_mem_alloc_addr_info),
		.ei_array	= dhms_mem_alloc_addr_info_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_free_generic_req_msg_v01, client_id_valid),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_free_generic_req_msg_v01, client_id_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_free_generic_req_msg_v01, client_id),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_free_generic_req_msg_v01, client_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_free_generic_req_msg_v01, proc_id_valid),
		.tlv_type       = 0x11,
		.offset         = offsetof(struct mem_free_generic_req_msg_v01, proc_id_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_free_generic_req_msg_v01, proc_id),
		.tlv_type       = 0x11,
		.offset         = offsetof(struct mem_free_generic_req_msg_v01, proc_id),
	},
	{
		.data_type      = QMI_EOTI,
	},
};

struct qmi_elem_info mem_free_generic_resp_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_free_generic_resp_msg_v01, resp),
		.tlv_type       = 0x02,
		.offset         = offsetof(struct mem_free_generic_resp_msg_v01, resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
	},
};

struct qmi_elem_info mem_query_size_req_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_query_size_req_msg_v01, client_id),
		.tlv_type       = 0x01,
		.offset         = offsetof(struct mem_query_size_req_msg_v01, client_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_query_size_req_msg_v01, proc_id_valid),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_query_size_req_msg_v01, proc_id_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_query_size_req_msg_v01, proc_id),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_query_size_req_msg_v01, proc_id),
	},
	{
		.data_type      = QMI_EOTI,
	},
};

struct qmi_elem_info mem_query_size_resp_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_query_size_rsp_msg_v01, resp),
		.tlv_type       = 0x02,
		.offset         = offsetof(struct mem_query_size_rsp_msg_v01, resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_query_size_rsp_msg_v01, size_valid),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_query_size_rsp_msg_v01, size_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof_field(struct mem_query_size_rsp_msg_v01, size),
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_query_size_rsp_msg_v01, size),
	},
	{
		.data_type      = QMI_EOTI,
	},
};
