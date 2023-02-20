// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2015, 2017-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/soc/qcom/qmi.h>
#include "heap_mem_ext_v01.h"

struct qmi_elem_info dhms_mem_alloc_addr_info_type_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint64_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
		.offset         = offsetof(struct
					dhms_mem_alloc_addr_info_type_v01,
					phy_addr),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint32_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
		.offset         = offsetof(struct
					dhms_mem_alloc_addr_info_type_v01,
					num_bytes),
	},
		{
		.data_type      = QMI_EOTI,
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};
EXPORT_SYMBOL(dhms_mem_alloc_addr_info_type_v01_ei);

struct qmi_elem_info mem_alloc_generic_req_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint32_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01,
					num_bytes),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint32_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01,
					client_id),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint32_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x03,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01,
					proc_id),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint32_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x04,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01,
					sequence_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01,
					alloc_contiguous_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01,
					alloc_contiguous),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01,
					block_alignment_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint32_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct mem_alloc_generic_req_msg_v01,
					block_alignment),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};
EXPORT_SYMBOL(mem_alloc_generic_req_msg_data_v01_ei);

struct qmi_elem_info mem_alloc_generic_resp_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct	qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct
						mem_alloc_generic_resp_msg_v01,
					resp),
		.ei_array		= qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct
						mem_alloc_generic_resp_msg_v01,
					sequence_id_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint32_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct
						mem_alloc_generic_resp_msg_v01,
					sequence_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct
						mem_alloc_generic_resp_msg_v01,
					dhms_mem_alloc_addr_info_valid),
	},
	{
		.data_type	    = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct
						mem_alloc_generic_resp_msg_v01,
					dhms_mem_alloc_addr_info_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = MAX_ARR_CNT_V01,
		.elem_size      = sizeof(struct
					dhms_mem_alloc_addr_info_type_v01),
		.array_type     = VAR_LEN_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct
						mem_alloc_generic_resp_msg_v01,
					dhms_mem_alloc_addr_info),
		.ei_array       = dhms_mem_alloc_addr_info_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};
EXPORT_SYMBOL(mem_alloc_generic_resp_msg_data_v01_ei);

struct qmi_elem_info mem_free_generic_req_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct mem_free_generic_req_msg_v01,
					dhms_mem_alloc_addr_info_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = MAX_ARR_CNT_V01,
		.elem_size      = sizeof(struct
					dhms_mem_alloc_addr_info_type_v01),
		.array_type     = VAR_LEN_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct mem_free_generic_req_msg_v01,
					dhms_mem_alloc_addr_info),
		.ei_array		= dhms_mem_alloc_addr_info_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_free_generic_req_msg_v01,
					client_id_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint32_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_free_generic_req_msg_v01,
					client_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct mem_free_generic_req_msg_v01,
					proc_id_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint32_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct mem_free_generic_req_msg_v01,
					proc_id),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};
EXPORT_SYMBOL(mem_free_generic_req_msg_data_v01_ei);

struct qmi_elem_info mem_free_generic_resp_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct	qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct
						mem_free_generic_resp_msg_v01,
					resp),
		.ei_array		= qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_EOTI,
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};
EXPORT_SYMBOL(mem_free_generic_resp_msg_data_v01_ei);

struct qmi_elem_info mem_query_size_req_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint32_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct mem_query_size_req_msg_v01,
					client_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_query_size_req_msg_v01,
					proc_id_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint32_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_query_size_req_msg_v01,
					proc_id),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};
EXPORT_SYMBOL(mem_query_size_req_msg_data_v01_ei);

struct qmi_elem_info mem_query_size_resp_msg_data_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct	qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct
						mem_query_size_rsp_msg_v01,
					resp),
		.ei_array		= qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(uint8_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_query_size_rsp_msg_v01,
					size_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(uint32_t),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct mem_query_size_rsp_msg_v01,
					size),
	},
	{
		.data_type      = QMI_EOTI,
		.array_type     = NO_ARRAY,
		.tlv_type       = QMI_COMMON_TLV_TYPE,
	},
};
EXPORT_SYMBOL(mem_query_size_resp_msg_data_v01_ei);

MODULE_LICENSE("GPL");
