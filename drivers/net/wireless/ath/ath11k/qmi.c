// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#include <linux/elf.h>

#include "qmi.h"
#include "core.h"
#include "debug.h"
#include <linux/of.h>
#include <linux/firmware.h>

#define SLEEP_CLOCK_SELECT_INTERNAL_BIT	0x02
#define HOST_CSTATE_BIT			0x04

static struct qmi_elem_info qmi_wlanfw_host_cap_req_msg_v01_ei[] = {
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   num_clients_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   num_clients),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x11,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   wake_msi_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x11,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   wake_msi),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x12,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   gpios_valid),
	},
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x12,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   gpios_len),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= QMI_WLFW_MAX_NUM_GPIO_V01,
		.elem_size	= sizeof(u32),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0x12,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   gpios),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x13,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   nm_modem_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x13,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   nm_modem),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x14,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   bdf_support_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x14,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   bdf_support),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x15,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   bdf_cache_support_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x15,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   bdf_cache_support),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x16,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   m3_support_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x16,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   m3_support),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x17,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   m3_cache_support_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x17,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   m3_cache_support),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x18,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   cal_filesys_support_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x18,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   cal_filesys_support),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x19,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   cal_cache_support_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x19,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   cal_cache_support),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x1A,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   cal_done_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x1A,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   cal_done),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x1B,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   mem_bucket_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x1B,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   mem_bucket),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x1C,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   mem_cfg_mode_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x1C,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_req_msg_v01,
					   mem_cfg_mode),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_host_cap_resp_msg_v01_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct qmi_wlanfw_host_cap_resp_msg_v01, resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_ind_register_req_msg_v01_ei[] = {
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   fw_ready_enable_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   fw_ready_enable),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x11,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   initiate_cal_download_enable_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x11,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   initiate_cal_download_enable),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x12,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   initiate_cal_update_enable_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x12,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   initiate_cal_update_enable),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x13,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   msa_ready_enable_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x13,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   msa_ready_enable),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x14,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   pin_connect_result_enable_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x14,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   pin_connect_result_enable),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x15,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   client_id_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x15,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   client_id),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x16,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   request_mem_enable_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x16,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   request_mem_enable),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x17,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   fw_mem_ready_enable_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x17,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   fw_mem_ready_enable),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x18,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   fw_init_done_enable_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x18,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   fw_init_done_enable),
	},

	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x19,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   rejuvenate_enable_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x19,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   rejuvenate_enable),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x1A,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   xo_cal_enable_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x1A,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   xo_cal_enable),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x1B,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   cal_done_enable_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x1B,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_req_msg_v01,
					   cal_done_enable),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_ind_register_resp_msg_v01_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_resp_msg_v01,
					   resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_resp_msg_v01,
					   fw_status_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_8_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u64),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_ind_register_resp_msg_v01,
					   fw_status),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_mem_cfg_s_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_8_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u64),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_mem_cfg_s_v01, offset),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_mem_cfg_s_v01, size),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_mem_cfg_s_v01, secure_flag),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_mem_seg_s_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_mem_seg_s_v01,
				  size),
	},
	{
		.data_type	= QMI_SIGNED_4_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	= sizeof(enum qmi_wlanfw_mem_type_enum_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_mem_seg_s_v01, type),
	},
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_mem_seg_s_v01, mem_cfg_len),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= QMI_WLANFW_MAX_NUM_MEM_CFG_V01,
		.elem_size	= sizeof(struct qmi_wlanfw_mem_cfg_s_v01),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_mem_seg_s_v01, mem_cfg),
		.ei_array	= qmi_wlanfw_mem_cfg_s_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_request_mem_ind_msg_v01_ei[] = {
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct qmi_wlanfw_request_mem_ind_msg_v01,
					   mem_seg_len),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= ATH11K_QMI_WLANFW_MAX_NUM_MEM_SEG_V01,
		.elem_size	= sizeof(struct qmi_wlanfw_mem_seg_s_v01),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct qmi_wlanfw_request_mem_ind_msg_v01,
					   mem_seg),
		.ei_array	= qmi_wlanfw_mem_seg_s_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_mem_seg_resp_s_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_8_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u64),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_mem_seg_resp_s_v01, addr),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_mem_seg_resp_s_v01, size),
	},
	{
		.data_type	= QMI_SIGNED_4_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	= sizeof(enum qmi_wlanfw_mem_type_enum_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_mem_seg_resp_s_v01, type),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_mem_seg_resp_s_v01, restore),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_respond_mem_req_msg_v01_ei[] = {
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct qmi_wlanfw_respond_mem_req_msg_v01,
					   mem_seg_len),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= ATH11K_QMI_WLANFW_MAX_NUM_MEM_SEG_V01,
		.elem_size	= sizeof(struct qmi_wlanfw_mem_seg_resp_s_v01),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct qmi_wlanfw_respond_mem_req_msg_v01,
					   mem_seg),
		.ei_array	= qmi_wlanfw_mem_seg_resp_s_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_respond_mem_resp_msg_v01_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct qmi_wlanfw_respond_mem_resp_msg_v01,
					   resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_cap_req_msg_v01_ei[] = {
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_rf_chip_info_s_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_rf_chip_info_s_v01,
					   chip_id),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_rf_chip_info_s_v01,
					   chip_family),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_rf_board_info_s_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_rf_board_info_s_v01,
					   board_id),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_soc_info_s_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_soc_info_s_v01, soc_id),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_fw_version_info_s_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_fw_version_info_s_v01,
					   fw_version),
	},
	{
		.data_type	= QMI_STRING,
		.elem_len	= ATH11K_QMI_WLANFW_MAX_TIMESTAMP_LEN_V01 + 1,
		.elem_size	= sizeof(char),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_fw_version_info_s_v01,
					   fw_build_timestamp),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_cap_resp_msg_v01_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct qmi_wlanfw_cap_resp_msg_v01, resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_cap_resp_msg_v01,
					   chip_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_wlanfw_rf_chip_info_s_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_cap_resp_msg_v01,
					   chip_info),
		.ei_array	= qmi_wlanfw_rf_chip_info_s_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x11,
		.offset		= offsetof(struct qmi_wlanfw_cap_resp_msg_v01,
					   board_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_wlanfw_rf_board_info_s_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x11,
		.offset		= offsetof(struct qmi_wlanfw_cap_resp_msg_v01,
					   board_info),
		.ei_array	= qmi_wlanfw_rf_board_info_s_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x12,
		.offset		= offsetof(struct qmi_wlanfw_cap_resp_msg_v01,
					   soc_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_wlanfw_soc_info_s_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x12,
		.offset		= offsetof(struct qmi_wlanfw_cap_resp_msg_v01,
					   soc_info),
		.ei_array	= qmi_wlanfw_soc_info_s_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x13,
		.offset		= offsetof(struct qmi_wlanfw_cap_resp_msg_v01,
					   fw_version_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_wlanfw_fw_version_info_s_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x13,
		.offset		= offsetof(struct qmi_wlanfw_cap_resp_msg_v01,
					   fw_version_info),
		.ei_array	= qmi_wlanfw_fw_version_info_s_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x14,
		.offset		= offsetof(struct qmi_wlanfw_cap_resp_msg_v01,
					   fw_build_id_valid),
	},
	{
		.data_type	= QMI_STRING,
		.elem_len	= ATH11K_QMI_WLANFW_MAX_BUILD_ID_LEN_V01 + 1,
		.elem_size	= sizeof(char),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x14,
		.offset		= offsetof(struct qmi_wlanfw_cap_resp_msg_v01,
					   fw_build_id),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x15,
		.offset		= offsetof(struct qmi_wlanfw_cap_resp_msg_v01,
					   num_macs_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x15,
		.offset		= offsetof(struct qmi_wlanfw_cap_resp_msg_v01,
					   num_macs),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_bdf_download_req_msg_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   valid),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   file_id_valid),
	},
	{
		.data_type	= QMI_SIGNED_4_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	= sizeof(enum qmi_wlanfw_cal_temp_id_enum_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   file_id),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x11,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   total_size_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x11,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   total_size),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x12,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   seg_id_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x12,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   seg_id),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x13,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   data_valid),
	},
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(u16),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x13,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   data_len),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= QMI_WLANFW_MAX_DATA_SIZE_V01,
		.elem_size	= sizeof(u8),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0x13,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   data),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x14,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   end_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x14,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   end),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x15,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   bdf_type_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x15,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_req_msg_v01,
					   bdf_type),
	},

	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_bdf_download_resp_msg_v01_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct qmi_wlanfw_bdf_download_resp_msg_v01,
					   resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_m3_info_req_msg_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_8_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u64),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct qmi_wlanfw_m3_info_req_msg_v01, addr),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct qmi_wlanfw_m3_info_req_msg_v01, size),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_m3_info_resp_msg_v01_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct qmi_wlanfw_m3_info_resp_msg_v01, resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_ce_tgt_pipe_cfg_s_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_ce_tgt_pipe_cfg_s_v01,
					   pipe_num),
	},
	{
		.data_type	= QMI_SIGNED_4_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	= sizeof(enum qmi_wlanfw_pipedir_enum_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_ce_tgt_pipe_cfg_s_v01,
					   pipe_dir),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_ce_tgt_pipe_cfg_s_v01,
					   nentries),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_ce_tgt_pipe_cfg_s_v01,
					   nbytes_max),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_ce_tgt_pipe_cfg_s_v01,
					   flags),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_ce_svc_pipe_cfg_s_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_ce_svc_pipe_cfg_s_v01,
					   service_id),
	},
	{
		.data_type	= QMI_SIGNED_4_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	= sizeof(enum qmi_wlanfw_pipedir_enum_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_ce_svc_pipe_cfg_s_v01,
					   pipe_dir),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_ce_svc_pipe_cfg_s_v01,
					   pipe_num),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_shadow_reg_cfg_s_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_2_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u16),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_shadow_reg_cfg_s_v01, id),
	},
	{
		.data_type	= QMI_UNSIGNED_2_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u16),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_shadow_reg_cfg_s_v01,
					   offset),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_shadow_reg_v2_cfg_s_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0,
		.offset		= offsetof(struct qmi_wlanfw_shadow_reg_v2_cfg_s_v01,
					   addr),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_wlan_mode_req_msg_v01_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u32),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x01,
		.offset		= offsetof(struct qmi_wlanfw_wlan_mode_req_msg_v01,
					   mode),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_wlan_mode_req_msg_v01,
					   hw_debug_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_wlan_mode_req_msg_v01,
					   hw_debug),
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_wlan_mode_resp_msg_v01_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct qmi_wlanfw_wlan_mode_resp_msg_v01,
					   resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_wlan_cfg_req_msg_v01_ei[] = {
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   host_version_valid),
	},
	{
		.data_type	= QMI_STRING,
		.elem_len	= QMI_WLANFW_MAX_STR_LEN_V01 + 1,
		.elem_size	= sizeof(char),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   host_version),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x11,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   tgt_cfg_valid),
	},
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x11,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   tgt_cfg_len),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= QMI_WLANFW_MAX_NUM_CE_V01,
		.elem_size	= sizeof(
				struct qmi_wlanfw_ce_tgt_pipe_cfg_s_v01),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0x11,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   tgt_cfg),
		.ei_array	= qmi_wlanfw_ce_tgt_pipe_cfg_s_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x12,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   svc_cfg_valid),
	},
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x12,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   svc_cfg_len),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= QMI_WLANFW_MAX_NUM_SVC_V01,
		.elem_size	= sizeof(struct qmi_wlanfw_ce_svc_pipe_cfg_s_v01),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0x12,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   svc_cfg),
		.ei_array	= qmi_wlanfw_ce_svc_pipe_cfg_s_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x13,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   shadow_reg_valid),
	},
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x13,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   shadow_reg_len),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= QMI_WLANFW_MAX_NUM_SHADOW_REG_V01,
		.elem_size	= sizeof(struct qmi_wlanfw_shadow_reg_cfg_s_v01),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0x13,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   shadow_reg),
		.ei_array	= qmi_wlanfw_shadow_reg_cfg_s_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x14,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   shadow_reg_v2_valid),
	},
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof(u8),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x14,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   shadow_reg_v2_len),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= QMI_WLANFW_MAX_NUM_SHADOW_REG_V2_V01,
		.elem_size	= sizeof(struct qmi_wlanfw_shadow_reg_v2_cfg_s_v01),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0x14,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_req_msg_v01,
					   shadow_reg_v2),
		.ei_array	= qmi_wlanfw_shadow_reg_v2_cfg_s_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_wlan_cfg_resp_msg_v01_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.array_type	= NO_ARRAY,
		.tlv_type	= 0x02,
		.offset		= offsetof(struct qmi_wlanfw_wlan_cfg_resp_msg_v01, resp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
	},
};

static struct qmi_elem_info qmi_wlanfw_mem_ready_ind_msg_v01_ei[] = {
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
	},
};

static struct qmi_elem_info qmi_wlanfw_fw_ready_ind_msg_v01_ei[] = {
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
	},
};

static struct qmi_elem_info qmi_wlanfw_cold_boot_cal_done_ind_msg_v01_ei[] = {
	{
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
	},
};

static int ath11k_qmi_host_cap_send(struct ath11k_base *ab)
{
	struct qmi_wlanfw_host_cap_req_msg_v01 req;
	struct qmi_wlanfw_host_cap_resp_msg_v01 resp;
	struct qmi_txn txn = {};
	int ret = 0;

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));

	req.num_clients_valid = 1;
	req.num_clients = 1;
	req.mem_cfg_mode = ab->qmi.target_mem_mode;
	req.mem_cfg_mode_valid = 1;
	req.bdf_support_valid = 1;
	req.bdf_support = 1;

	if (ab->bus_params.m3_fw_support) {
		req.m3_support_valid = 1;
		req.m3_support = 1;
		req.m3_cache_support_valid = 1;
		req.m3_cache_support = 1;
	} else {
		req.m3_support_valid = 0;
		req.m3_support = 0;
		req.m3_cache_support_valid = 0;
		req.m3_cache_support = 0;
	}

	req.cal_done_valid = 1;
	req.cal_done = ab->qmi.cal_done;

	if (ab->hw_params.internal_sleep_clock) {
		req.nm_modem_valid = 1;

		/* Notify firmware that this is non-qualcomm platform. */
		req.nm_modem |= HOST_CSTATE_BIT;

		/* Notify firmware about the sleep clock selection,
		 * nm_modem_bit[1] is used for this purpose. Host driver on
		 * non-qualcomm platforms should select internal sleep
		 * clock.
		 */
		req.nm_modem |= SLEEP_CLOCK_SELECT_INTERNAL_BIT;
	}

	ret = qmi_txn_init(&ab->qmi.handle, &txn,
			   qmi_wlanfw_host_cap_resp_msg_v01_ei, &resp);
	if (ret < 0)
		goto out;

	ret = qmi_send_request(&ab->qmi.handle, NULL, &txn,
			       QMI_WLANFW_HOST_CAP_REQ_V01,
			       QMI_WLANFW_HOST_CAP_REQ_MSG_V01_MAX_LEN,
			       qmi_wlanfw_host_cap_req_msg_v01_ei, &req);
	if (ret < 0) {
		ath11k_warn(ab, "Failed to send host capability request,err = %d\n", ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, msecs_to_jiffies(ATH11K_QMI_WLANFW_TIMEOUT_MS));
	if (ret < 0)
		goto out;

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ath11k_warn(ab, "Host capability request failed, result: %d, err: %d\n",
			    resp.resp.result, resp.resp.error);
		ret = -EINVAL;
		goto out;
	}

out:
	return ret;
}

static int ath11k_qmi_fw_ind_register_send(struct ath11k_base *ab)
{
	struct qmi_wlanfw_ind_register_req_msg_v01 *req;
	struct qmi_wlanfw_ind_register_resp_msg_v01 *resp;
	struct qmi_handle *handle = &ab->qmi.handle;
	struct qmi_txn txn;
	int ret;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);
	if (!resp) {
		ret = -ENOMEM;
		goto resp_out;
	}

	req->client_id_valid = 1;
	req->client_id = QMI_WLANFW_CLIENT_ID;
	req->fw_ready_enable_valid = 1;
	req->fw_ready_enable = 1;
	req->request_mem_enable_valid = 1;
	req->request_mem_enable = 1;
	req->fw_mem_ready_enable_valid = 1;
	req->fw_mem_ready_enable = 1;
	req->cal_done_enable_valid = 1;
	req->cal_done_enable = 1;
	req->fw_init_done_enable_valid = 1;
	req->fw_init_done_enable = 1;

	req->pin_connect_result_enable_valid = 0;
	req->pin_connect_result_enable = 0;

	ret = qmi_txn_init(handle, &txn,
			   qmi_wlanfw_ind_register_resp_msg_v01_ei, resp);
	if (ret < 0)
		goto out;

	ret = qmi_send_request(&ab->qmi.handle, NULL, &txn,
			       QMI_WLANFW_IND_REGISTER_REQ_V01,
			       QMI_WLANFW_IND_REGISTER_REQ_MSG_V01_MAX_LEN,
			       qmi_wlanfw_ind_register_req_msg_v01_ei, req);
	if (ret < 0) {
		ath11k_warn(ab, "Failed to send indication register request, err = %d\n",
			    ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, msecs_to_jiffies(ATH11K_QMI_WLANFW_TIMEOUT_MS));
	if (ret < 0) {
		ath11k_warn(ab, "failed to register fw indication %d\n", ret);
		goto out;
	}

	if (resp->resp.result != QMI_RESULT_SUCCESS_V01) {
		ath11k_warn(ab, "FW Ind register request failed, result: %d, err: %d\n",
			    resp->resp.result, resp->resp.error);
		ret = -EINVAL;
		goto out;
	}

out:
	kfree(resp);
resp_out:
	kfree(req);
	return ret;
}

static int ath11k_qmi_respond_fw_mem_request(struct ath11k_base *ab)
{
	struct qmi_wlanfw_respond_mem_req_msg_v01 *req;
	struct qmi_wlanfw_respond_mem_resp_msg_v01 resp;
	struct qmi_txn txn = {};
	int ret = 0, i;
	bool delayed;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	memset(&resp, 0, sizeof(resp));

	/* For QCA6390 by default FW requests a block of ~4M contiguous
	 * DMA memory, it's hard to allocate from OS. So host returns
	 * failure to FW and FW will then request mulitple blocks of small
	 * chunk size memory.
	 */
	if (!ab->bus_params.fixed_mem_region && ab->qmi.target_mem_delayed) {
		delayed = true;
		ath11k_dbg(ab, ATH11K_DBG_QMI, "qmi delays mem_request %d\n",
			   ab->qmi.mem_seg_count);
		memset(req, 0, sizeof(*req));
	} else {
		delayed = false;
		req->mem_seg_len = ab->qmi.mem_seg_count;

		for (i = 0; i < req->mem_seg_len ; i++) {
			req->mem_seg[i].addr = ab->qmi.target_mem[i].paddr;
			req->mem_seg[i].size = ab->qmi.target_mem[i].size;
			req->mem_seg[i].type = ab->qmi.target_mem[i].type;
		}
	}

	ret = qmi_txn_init(&ab->qmi.handle, &txn,
			   qmi_wlanfw_respond_mem_resp_msg_v01_ei, &resp);
	if (ret < 0)
		goto out;

	ret = qmi_send_request(&ab->qmi.handle, NULL, &txn,
			       QMI_WLANFW_RESPOND_MEM_REQ_V01,
			       QMI_WLANFW_RESPOND_MEM_REQ_MSG_V01_MAX_LEN,
			       qmi_wlanfw_respond_mem_req_msg_v01_ei, req);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to respond memory request, err = %d\n",
			    ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, msecs_to_jiffies(ATH11K_QMI_WLANFW_TIMEOUT_MS));
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed memory request, err = %d\n", ret);
		goto out;
	}

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		/* the error response is expected when
		 * target_mem_delayed is true.
		 */
		if (delayed && resp.resp.error == 0)
			goto out;

		ath11k_warn(ab, "Respond mem req failed, result: %d, err: %d\n",
			    resp.resp.result, resp.resp.error);
		ret = -EINVAL;
		goto out;
	}
out:
	kfree(req);
	return ret;
}

static void ath11k_qmi_free_target_mem_chunk(struct ath11k_base *ab)
{
	int i;

	if (ab->bus_params.fixed_mem_region)
		return;

	for (i = 0; i < ab->qmi.mem_seg_count; i++) {
		if (!ab->qmi.target_mem[i].vaddr)
			continue;

		dma_free_coherent(ab->dev,
				  ab->qmi.target_mem[i].size,
				  ab->qmi.target_mem[i].vaddr,
				  ab->qmi.target_mem[i].paddr);
		ab->qmi.target_mem[i].vaddr = NULL;
	}
}

static int ath11k_qmi_alloc_target_mem_chunk(struct ath11k_base *ab)
{
	int i;
	struct target_mem_chunk *chunk;

	ab->qmi.target_mem_delayed = false;

	for (i = 0; i < ab->qmi.mem_seg_count; i++) {
		chunk = &ab->qmi.target_mem[i];
		chunk->vaddr = dma_alloc_coherent(ab->dev,
						  chunk->size,
						  &chunk->paddr,
						  GFP_KERNEL);
		if (!chunk->vaddr) {
			if (ab->qmi.mem_seg_count <= 2) {
				ath11k_dbg(ab, ATH11K_DBG_QMI,
					   "qmi dma allocation failed (%d B type %u), will try later with small size\n",
					    chunk->size,
					    chunk->type);
				ath11k_qmi_free_target_mem_chunk(ab);
				ab->qmi.target_mem_delayed = true;
				return 0;
			}
			ath11k_err(ab, "failed to alloc memory, size: 0x%x, type: %u\n",
				   chunk->size,
				   chunk->type);
			return -EINVAL;
		}
	}

	return 0;
}

static int ath11k_qmi_assign_target_mem_chunk(struct ath11k_base *ab)
{
	int i, idx;

	for (i = 0, idx = 0; i < ab->qmi.mem_seg_count; i++) {
		switch (ab->qmi.target_mem[i].type) {
		case BDF_MEM_REGION_TYPE:
			ab->qmi.target_mem[idx].paddr = ab->hw_params.bdf_addr;
			ab->qmi.target_mem[idx].vaddr = NULL;
			ab->qmi.target_mem[idx].size = ab->qmi.target_mem[i].size;
			ab->qmi.target_mem[idx].type = ab->qmi.target_mem[i].type;
			idx++;
			break;
		case CALDB_MEM_REGION_TYPE:
			if (ab->qmi.target_mem[i].size > ATH11K_QMI_CALDB_SIZE) {
				ath11k_warn(ab, "qmi mem size is low to load caldata\n");
				return -EINVAL;
			}
			/* TODO ath11k does not support cold boot calibration */
			ab->qmi.target_mem[idx].paddr = 0;
			ab->qmi.target_mem[idx].vaddr = NULL;
			ab->qmi.target_mem[idx].size = ab->qmi.target_mem[i].size;
			ab->qmi.target_mem[idx].type = ab->qmi.target_mem[i].type;
			idx++;
			break;
		default:
			ath11k_warn(ab, "qmi ignore invalid mem req type %d\n",
				    ab->qmi.target_mem[i].type);
			break;
		}
	}
	ab->qmi.mem_seg_count = idx;

	return 0;
}

static int ath11k_qmi_request_target_cap(struct ath11k_base *ab)
{
	struct qmi_wlanfw_cap_req_msg_v01 req;
	struct qmi_wlanfw_cap_resp_msg_v01 resp;
	struct qmi_txn txn = {};
	int ret = 0;

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));

	ret = qmi_txn_init(&ab->qmi.handle, &txn,
			   qmi_wlanfw_cap_resp_msg_v01_ei, &resp);
	if (ret < 0)
		goto out;

	ret = qmi_send_request(&ab->qmi.handle, NULL, &txn,
			       QMI_WLANFW_CAP_REQ_V01,
			       QMI_WLANFW_CAP_REQ_MSG_V01_MAX_LEN,
			       qmi_wlanfw_cap_req_msg_v01_ei, &req);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to send target cap request, err = %d\n",
			    ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, msecs_to_jiffies(ATH11K_QMI_WLANFW_TIMEOUT_MS));
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed target cap request %d\n", ret);
		goto out;
	}

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ath11k_warn(ab, "qmi targetcap req failed, result: %d, err: %d\n",
			    resp.resp.result, resp.resp.error);
		ret = -EINVAL;
		goto out;
	}

	if (resp.chip_info_valid) {
		ab->qmi.target.chip_id = resp.chip_info.chip_id;
		ab->qmi.target.chip_family = resp.chip_info.chip_family;
	}

	if (resp.board_info_valid)
		ab->qmi.target.board_id = resp.board_info.board_id;
	else
		ab->qmi.target.board_id = 0xFF;

	if (resp.soc_info_valid)
		ab->qmi.target.soc_id = resp.soc_info.soc_id;

	if (resp.fw_version_info_valid) {
		ab->qmi.target.fw_version = resp.fw_version_info.fw_version;
		strlcpy(ab->qmi.target.fw_build_timestamp,
			resp.fw_version_info.fw_build_timestamp,
			sizeof(ab->qmi.target.fw_build_timestamp));
	}

	if (resp.fw_build_id_valid)
		strlcpy(ab->qmi.target.fw_build_id, resp.fw_build_id,
			sizeof(ab->qmi.target.fw_build_id));

	ath11k_info(ab, "chip_id 0x%x chip_family 0x%x board_id 0x%x soc_id 0x%x\n",
		    ab->qmi.target.chip_id, ab->qmi.target.chip_family,
		    ab->qmi.target.board_id, ab->qmi.target.soc_id);

	ath11k_info(ab, "fw_version 0x%x fw_build_timestamp %s fw_build_id %s",
		    ab->qmi.target.fw_version,
		    ab->qmi.target.fw_build_timestamp,
		    ab->qmi.target.fw_build_id);

out:
	return ret;
}

static int
ath11k_qmi_prepare_bdf_download(struct ath11k_base *ab, int type,
				struct qmi_wlanfw_bdf_download_req_msg_v01 *req,
				void __iomem *bdf_addr)
{
	const struct firmware *fw_entry;
	struct ath11k_board_data bd;
	u32 fw_size;
	int ret;

	switch (type) {
	case ATH11K_QMI_FILE_TYPE_BDF_GOLDEN:
		memset(&bd, 0, sizeof(bd));

		ret = ath11k_core_fetch_bdf(ab, &bd);
		if (ret) {
			ath11k_warn(ab, "qmi failed to load BDF\n");
			return ret;
		}

		fw_size = min_t(u32, ab->hw_params.fw.board_size, bd.len);
		memcpy_toio(bdf_addr, bd.data, fw_size);
		ath11k_core_free_bdf(ab, &bd);
		break;
	case ATH11K_QMI_FILE_TYPE_CALDATA:
		fw_entry = ath11k_core_firmware_request(ab, ATH11K_DEFAULT_CAL_FILE);
		if (IS_ERR(fw_entry)) {
			ret = PTR_ERR(fw_entry);
			ath11k_warn(ab, "failed to load %s: %d\n",
				    ATH11K_DEFAULT_CAL_FILE, ret);
			return ret;
		}

		fw_size = min_t(u32, ab->hw_params.fw.board_size,
				fw_entry->size);

		memcpy_toio(bdf_addr + ATH11K_QMI_CALDATA_OFFSET,
			    fw_entry->data, fw_size);

		release_firmware(fw_entry);
		break;
	default:
		return -EINVAL;
	}

	req->total_size = fw_size;
	return 0;
}

static int ath11k_qmi_load_bdf_fixed_addr(struct ath11k_base *ab)
{
	struct qmi_wlanfw_bdf_download_req_msg_v01 *req;
	struct qmi_wlanfw_bdf_download_resp_msg_v01 resp;
	struct qmi_txn txn = {};
	void __iomem *bdf_addr = NULL;
	int type, ret;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;
	memset(&resp, 0, sizeof(resp));

	bdf_addr = ioremap(ab->hw_params.bdf_addr, ATH11K_QMI_BDF_MAX_SIZE);
	if (!bdf_addr) {
		ath11k_warn(ab, "qmi ioremap error for BDF\n");
		ret = -EIO;
		goto out;
	}

	for (type = 0; type < ATH11K_QMI_MAX_FILE_TYPE; type++) {
		req->valid = 1;
		req->file_id_valid = 1;
		req->file_id = ab->qmi.target.board_id;
		req->total_size_valid = 1;
		req->seg_id_valid = 1;
		req->seg_id = type;
		req->data_valid = 0;
		req->data_len = ATH11K_QMI_MAX_BDF_FILE_NAME_SIZE;
		req->bdf_type = 0;
		req->bdf_type_valid = 0;
		req->end_valid = 1;
		req->end = 1;

		ret = ath11k_qmi_prepare_bdf_download(ab, type, req, bdf_addr);
		if (ret < 0)
			goto out_qmi_bdf;

		ret = qmi_txn_init(&ab->qmi.handle, &txn,
				   qmi_wlanfw_bdf_download_resp_msg_v01_ei,
				   &resp);
		if (ret < 0)
			goto out_qmi_bdf;

		ret = qmi_send_request(&ab->qmi.handle, NULL, &txn,
				       QMI_WLANFW_BDF_DOWNLOAD_REQ_V01,
				       QMI_WLANFW_BDF_DOWNLOAD_REQ_MSG_V01_MAX_LEN,
				       qmi_wlanfw_bdf_download_req_msg_v01_ei, req);
		if (ret < 0) {
			qmi_txn_cancel(&txn);
			goto out_qmi_bdf;
		}

		ret = qmi_txn_wait(&txn, msecs_to_jiffies(ATH11K_QMI_WLANFW_TIMEOUT_MS));
		if (ret < 0)
			goto out_qmi_bdf;

		if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
			ath11k_warn(ab, "qmi BDF download failed, result: %d, err: %d\n",
				    resp.resp.result, resp.resp.error);
			ret = -EINVAL;
			goto out_qmi_bdf;
		}
	}

out_qmi_bdf:
	iounmap(bdf_addr);
out:
	kfree(req);
	return ret;
}

static int ath11k_qmi_load_bdf_qmi(struct ath11k_base *ab)
{
	struct qmi_wlanfw_bdf_download_req_msg_v01 *req;
	struct qmi_wlanfw_bdf_download_resp_msg_v01 resp;
	struct ath11k_board_data bd;
	unsigned int remaining;
	struct qmi_txn txn = {};
	int ret;
	const u8 *temp;
	int bdf_type;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;
	memset(&resp, 0, sizeof(resp));

	memset(&bd, 0, sizeof(bd));
	ret = ath11k_core_fetch_bdf(ab, &bd);
	if (ret) {
		ath11k_warn(ab, "qmi failed to load bdf:\n");
		goto out;
	}

	temp = bd.data;
	remaining = bd.len;

	if (bd.len >= SELFMAG && memcmp(bd.data, ELFMAG, SELFMAG) == 0)
		bdf_type = ATH11K_QMI_BDF_TYPE_ELF;
	else
		bdf_type = ATH11K_QMI_BDF_TYPE_BIN;

	ath11k_dbg(ab, ATH11K_DBG_QMI, "qmi bdf_type %d\n", bdf_type);

	while (remaining) {
		req->valid = 1;
		req->file_id_valid = 1;
		req->file_id = ab->qmi.target.board_id;
		req->total_size_valid = 1;
		req->total_size = bd.len;
		req->seg_id_valid = 1;
		req->data_valid = 1;
		req->data_len = ATH11K_QMI_MAX_BDF_FILE_NAME_SIZE;
		req->bdf_type = bdf_type;
		req->bdf_type_valid = 1;
		req->end_valid = 1;
		req->end = 0;

		if (remaining > QMI_WLANFW_MAX_DATA_SIZE_V01) {
			req->data_len = QMI_WLANFW_MAX_DATA_SIZE_V01;
		} else {
			req->data_len = remaining;
			req->end = 1;
		}

		memcpy(req->data, temp, req->data_len);

		ret = qmi_txn_init(&ab->qmi.handle, &txn,
				   qmi_wlanfw_bdf_download_resp_msg_v01_ei,
				   &resp);
		if (ret < 0)
			goto out_qmi_bdf;

		ret = qmi_send_request(&ab->qmi.handle, NULL, &txn,
				       QMI_WLANFW_BDF_DOWNLOAD_REQ_V01,
				       QMI_WLANFW_BDF_DOWNLOAD_REQ_MSG_V01_MAX_LEN,
				       qmi_wlanfw_bdf_download_req_msg_v01_ei, req);
		if (ret < 0) {
			qmi_txn_cancel(&txn);
			goto out_qmi_bdf;
		}

		ret = qmi_txn_wait(&txn, msecs_to_jiffies(ATH11K_QMI_WLANFW_TIMEOUT_MS));
		if (ret < 0)
			goto out_qmi_bdf;

		if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
			ath11k_warn(ab, "qmi BDF download failed, result: %d, err: %d\n",
				    resp.resp.result, resp.resp.error);
			ret = resp.resp.result;
			goto out_qmi_bdf;
		}
		remaining -= req->data_len;
		temp += req->data_len;
		req->seg_id++;
	}

out_qmi_bdf:
	ath11k_core_free_bdf(ab, &bd);

out:
	kfree(req);
	return ret;
}

static int ath11k_qmi_m3_load(struct ath11k_base *ab)
{
	struct m3_mem_region *m3_mem = &ab->qmi.m3_mem;
	const struct firmware *fw;
	char path[100];
	int ret;

	if (m3_mem->vaddr || m3_mem->size)
		return 0;

	fw = ath11k_core_firmware_request(ab, ATH11K_M3_FILE);
	if (IS_ERR(fw)) {
		ret = PTR_ERR(fw);
		ath11k_core_create_firmware_path(ab, ATH11K_M3_FILE,
						 path, sizeof(path));
		ath11k_err(ab, "failed to load %s: %d\n", path, ret);
		return ret;
	}

	m3_mem->vaddr = dma_alloc_coherent(ab->dev,
					   fw->size, &m3_mem->paddr,
					   GFP_KERNEL);
	if (!m3_mem->vaddr) {
		ath11k_err(ab, "failed to allocate memory for M3 with size %zu\n",
			   fw->size);
		release_firmware(fw);
		return -ENOMEM;
	}

	memcpy(m3_mem->vaddr, fw->data, fw->size);
	m3_mem->size = fw->size;
	release_firmware(fw);

	return 0;
}

static void ath11k_qmi_m3_free(struct ath11k_base *ab)
{
	struct m3_mem_region *m3_mem = &ab->qmi.m3_mem;

	if (!ab->bus_params.m3_fw_support || !m3_mem->vaddr)
		return;

	dma_free_coherent(ab->dev, m3_mem->size,
			  m3_mem->vaddr, m3_mem->paddr);
	m3_mem->vaddr = NULL;
}

static int ath11k_qmi_wlanfw_m3_info_send(struct ath11k_base *ab)
{
	struct m3_mem_region *m3_mem = &ab->qmi.m3_mem;
	struct qmi_wlanfw_m3_info_req_msg_v01 req;
	struct qmi_wlanfw_m3_info_resp_msg_v01 resp;
	struct qmi_txn txn = {};
	int ret = 0;

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));

	if (ab->bus_params.m3_fw_support) {
		ret = ath11k_qmi_m3_load(ab);
		if (ret) {
			ath11k_err(ab, "failed to load m3 firmware: %d", ret);
			return ret;
		}

		req.addr = m3_mem->paddr;
		req.size = m3_mem->size;
	} else {
		req.addr = 0;
		req.size = 0;
	}

	ret = qmi_txn_init(&ab->qmi.handle, &txn,
			   qmi_wlanfw_m3_info_resp_msg_v01_ei, &resp);
	if (ret < 0)
		goto out;

	ret = qmi_send_request(&ab->qmi.handle, NULL, &txn,
			       QMI_WLANFW_M3_INFO_REQ_V01,
			       QMI_WLANFW_M3_INFO_REQ_MSG_V01_MAX_MSG_LEN,
			       qmi_wlanfw_m3_info_req_msg_v01_ei, &req);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to send M3 information request, err = %d\n",
			    ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, msecs_to_jiffies(ATH11K_QMI_WLANFW_TIMEOUT_MS));
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed M3 information request %d\n", ret);
		goto out;
	}

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ath11k_warn(ab, "qmi M3 info request failed, result: %d, err: %d\n",
			    resp.resp.result, resp.resp.error);
		ret = -EINVAL;
		goto out;
	}
out:
	return ret;
}

static int ath11k_qmi_wlanfw_mode_send(struct ath11k_base *ab,
				       u32 mode)
{
	struct qmi_wlanfw_wlan_mode_req_msg_v01 req;
	struct qmi_wlanfw_wlan_mode_resp_msg_v01 resp;
	struct qmi_txn txn = {};
	int ret = 0;

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));

	req.mode = mode;
	req.hw_debug_valid = 1;
	req.hw_debug = 0;

	ret = qmi_txn_init(&ab->qmi.handle, &txn,
			   qmi_wlanfw_wlan_mode_resp_msg_v01_ei, &resp);
	if (ret < 0)
		goto out;

	ret = qmi_send_request(&ab->qmi.handle, NULL, &txn,
			       QMI_WLANFW_WLAN_MODE_REQ_V01,
			       QMI_WLANFW_WLAN_MODE_REQ_MSG_V01_MAX_LEN,
			       qmi_wlanfw_wlan_mode_req_msg_v01_ei, &req);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to send mode request, mode: %d, err = %d\n",
			    mode, ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, msecs_to_jiffies(ATH11K_QMI_WLANFW_TIMEOUT_MS));
	if (ret < 0) {
		if (mode == ATH11K_FIRMWARE_MODE_OFF && ret == -ENETRESET) {
			ath11k_warn(ab, "WLFW service is dis-connected\n");
			return 0;
		}
		ath11k_warn(ab, "qmi failed set mode request, mode: %d, err = %d\n",
			    mode, ret);
		goto out;
	}

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ath11k_warn(ab, "Mode request failed, mode: %d, result: %d err: %d\n",
			    mode, resp.resp.result, resp.resp.error);
		ret = -EINVAL;
		goto out;
	}

out:
	return ret;
}

static int ath11k_qmi_wlanfw_wlan_cfg_send(struct ath11k_base *ab)
{
	struct qmi_wlanfw_wlan_cfg_req_msg_v01 *req;
	struct qmi_wlanfw_wlan_cfg_resp_msg_v01 resp;
	struct ce_pipe_config *ce_cfg;
	struct service_to_pipe *svc_cfg;
	struct qmi_txn txn = {};
	int ret = 0, pipe_num;

	ce_cfg	= (struct ce_pipe_config *)ab->qmi.ce_cfg.tgt_ce;
	svc_cfg	= (struct service_to_pipe *)ab->qmi.ce_cfg.svc_to_ce_map;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	memset(&resp, 0, sizeof(resp));

	req->host_version_valid = 1;
	strlcpy(req->host_version, ATH11K_HOST_VERSION_STRING,
		sizeof(req->host_version));

	req->tgt_cfg_valid = 1;
	/* This is number of CE configs */
	req->tgt_cfg_len = ab->qmi.ce_cfg.tgt_ce_len;
	for (pipe_num = 0; pipe_num < req->tgt_cfg_len ; pipe_num++) {
		req->tgt_cfg[pipe_num].pipe_num = ce_cfg[pipe_num].pipenum;
		req->tgt_cfg[pipe_num].pipe_dir = ce_cfg[pipe_num].pipedir;
		req->tgt_cfg[pipe_num].nentries = ce_cfg[pipe_num].nentries;
		req->tgt_cfg[pipe_num].nbytes_max = ce_cfg[pipe_num].nbytes_max;
		req->tgt_cfg[pipe_num].flags = ce_cfg[pipe_num].flags;
	}

	req->svc_cfg_valid = 1;
	/* This is number of Service/CE configs */
	req->svc_cfg_len = ab->qmi.ce_cfg.svc_to_ce_map_len;
	for (pipe_num = 0; pipe_num < req->svc_cfg_len; pipe_num++) {
		req->svc_cfg[pipe_num].service_id = svc_cfg[pipe_num].service_id;
		req->svc_cfg[pipe_num].pipe_dir = svc_cfg[pipe_num].pipedir;
		req->svc_cfg[pipe_num].pipe_num = svc_cfg[pipe_num].pipenum;
	}
	req->shadow_reg_valid = 0;

	/* set shadow v2 configuration */
	if (ab->hw_params.supports_shadow_regs) {
		req->shadow_reg_v2_valid = 1;
		req->shadow_reg_v2_len = min_t(u32,
					       ab->qmi.ce_cfg.shadow_reg_v2_len,
					       QMI_WLANFW_MAX_NUM_SHADOW_REG_V2_V01);
		memcpy(&req->shadow_reg_v2, ab->qmi.ce_cfg.shadow_reg_v2,
		       sizeof(u32) * req->shadow_reg_v2_len);
	} else {
		req->shadow_reg_v2_valid = 0;
	}

	ret = qmi_txn_init(&ab->qmi.handle, &txn,
			   qmi_wlanfw_wlan_cfg_resp_msg_v01_ei, &resp);
	if (ret < 0)
		goto out;

	ret = qmi_send_request(&ab->qmi.handle, NULL, &txn,
			       QMI_WLANFW_WLAN_CFG_REQ_V01,
			       QMI_WLANFW_WLAN_CFG_REQ_MSG_V01_MAX_LEN,
			       qmi_wlanfw_wlan_cfg_req_msg_v01_ei, req);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to send wlan config request, err = %d\n",
			    ret);
		goto out;
	}

	ret = qmi_txn_wait(&txn, msecs_to_jiffies(ATH11K_QMI_WLANFW_TIMEOUT_MS));
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed wlan config request, err = %d\n", ret);
		goto out;
	}

	if (resp.resp.result != QMI_RESULT_SUCCESS_V01) {
		ath11k_warn(ab, "qmi wlan config request failed, result: %d, err: %d\n",
			    resp.resp.result, resp.resp.error);
		ret = -EINVAL;
		goto out;
	}

out:
	kfree(req);
	return ret;
}

void ath11k_qmi_firmware_stop(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_qmi_wlanfw_mode_send(ab, ATH11K_FIRMWARE_MODE_OFF);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to send wlan mode off\n");
		return;
	}
}

int ath11k_qmi_firmware_start(struct ath11k_base *ab,
			      u32 mode)
{
	int ret;

	ret = ath11k_qmi_wlanfw_wlan_cfg_send(ab);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to send wlan cfg:%d\n", ret);
		return ret;
	}

	ret = ath11k_qmi_wlanfw_mode_send(ab, mode);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to send wlan fw mode:%d\n", ret);
		return ret;
	}

	return 0;
}

static int
ath11k_qmi_driver_event_post(struct ath11k_qmi *qmi,
			     enum ath11k_qmi_event_type type,
			     void *data)
{
	struct ath11k_qmi_driver_event *event;

	event = kzalloc(sizeof(*event), GFP_ATOMIC);
	if (!event)
		return -ENOMEM;

	event->type = type;
	event->data = data;

	spin_lock(&qmi->event_lock);
	list_add_tail(&event->list, &qmi->event_list);
	spin_unlock(&qmi->event_lock);

	queue_work(qmi->event_wq, &qmi->event_work);

	return 0;
}

static void ath11k_qmi_event_server_arrive(struct ath11k_qmi *qmi)
{
	struct ath11k_base *ab = qmi->ab;
	int ret;

	ret = ath11k_qmi_fw_ind_register_send(ab);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to send FW indication QMI:%d\n", ret);
		return;
	}

	ret = ath11k_qmi_host_cap_send(ab);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to send host cap QMI:%d\n", ret);
		return;
	}
}

static void ath11k_qmi_event_mem_request(struct ath11k_qmi *qmi)
{
	struct ath11k_base *ab = qmi->ab;
	int ret;

	ret = ath11k_qmi_respond_fw_mem_request(ab);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to respond fw mem req:%d\n", ret);
		return;
	}
}

static void ath11k_qmi_event_load_bdf(struct ath11k_qmi *qmi)
{
	struct ath11k_base *ab = qmi->ab;
	int ret;

	ret = ath11k_qmi_request_target_cap(ab);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to req target capabilities:%d\n", ret);
		return;
	}

	if (ab->bus_params.fixed_bdf_addr)
		ret = ath11k_qmi_load_bdf_fixed_addr(ab);
	else
		ret = ath11k_qmi_load_bdf_qmi(ab);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to load board data file:%d\n", ret);
		return;
	}

	ret = ath11k_qmi_wlanfw_m3_info_send(ab);
	if (ret < 0) {
		ath11k_warn(ab, "qmi failed to send m3 info req:%d\n", ret);
		return;
	}
}

static void ath11k_qmi_msg_mem_request_cb(struct qmi_handle *qmi_hdl,
					  struct sockaddr_qrtr *sq,
					  struct qmi_txn *txn,
					  const void *data)
{
	struct ath11k_qmi *qmi = container_of(qmi_hdl, struct ath11k_qmi, handle);
	struct ath11k_base *ab = qmi->ab;
	const struct qmi_wlanfw_request_mem_ind_msg_v01 *msg = data;
	int i, ret;

	ath11k_dbg(ab, ATH11K_DBG_QMI, "qmi firmware request memory request\n");

	if (msg->mem_seg_len == 0 ||
	    msg->mem_seg_len > ATH11K_QMI_WLANFW_MAX_NUM_MEM_SEG_V01)
		ath11k_warn(ab, "Invalid memory segment length: %u\n",
			    msg->mem_seg_len);

	ab->qmi.mem_seg_count = msg->mem_seg_len;

	for (i = 0; i < qmi->mem_seg_count ; i++) {
		ab->qmi.target_mem[i].type = msg->mem_seg[i].type;
		ab->qmi.target_mem[i].size = msg->mem_seg[i].size;
		ath11k_dbg(ab, ATH11K_DBG_QMI, "qmi mem seg type %d size %d\n",
			   msg->mem_seg[i].type, msg->mem_seg[i].size);
	}

	if (ab->bus_params.fixed_mem_region) {
		ret = ath11k_qmi_assign_target_mem_chunk(ab);
		if (ret) {
			ath11k_warn(ab, "qmi failed to assign target memory: %d\n",
				    ret);
			return;
		}
	} else {
		ret = ath11k_qmi_alloc_target_mem_chunk(ab);
		if (ret) {
			ath11k_warn(ab, "qmi failed to alloc target memory: %d\n",
				    ret);
			return;
		}
	}

	ath11k_qmi_driver_event_post(qmi, ATH11K_QMI_EVENT_REQUEST_MEM, NULL);
}

static void ath11k_qmi_msg_mem_ready_cb(struct qmi_handle *qmi_hdl,
					struct sockaddr_qrtr *sq,
					struct qmi_txn *txn,
					const void *decoded)
{
	struct ath11k_qmi *qmi = container_of(qmi_hdl, struct ath11k_qmi, handle);
	struct ath11k_base *ab = qmi->ab;

	ath11k_dbg(ab, ATH11K_DBG_QMI, "qmi firmware memory ready indication\n");
	ath11k_qmi_driver_event_post(qmi, ATH11K_QMI_EVENT_FW_MEM_READY, NULL);
}

static void ath11k_qmi_msg_fw_ready_cb(struct qmi_handle *qmi_hdl,
				       struct sockaddr_qrtr *sq,
				       struct qmi_txn *txn,
				       const void *decoded)
{
	struct ath11k_qmi *qmi = container_of(qmi_hdl, struct ath11k_qmi, handle);
	struct ath11k_base *ab = qmi->ab;

	ath11k_dbg(ab, ATH11K_DBG_QMI, "qmi firmware ready\n");
	ath11k_qmi_driver_event_post(qmi, ATH11K_QMI_EVENT_FW_READY, NULL);
}

static void ath11k_qmi_msg_cold_boot_cal_done_cb(struct qmi_handle *qmi,
						 struct sockaddr_qrtr *sq,
						 struct qmi_txn *txn,
						 const void *decoded)
{
}

static const struct qmi_msg_handler ath11k_qmi_msg_handlers[] = {
	{
		.type = QMI_INDICATION,
		.msg_id = QMI_WLFW_REQUEST_MEM_IND_V01,
		.ei = qmi_wlanfw_request_mem_ind_msg_v01_ei,
		.decoded_size = sizeof(struct qmi_wlanfw_request_mem_ind_msg_v01),
		.fn = ath11k_qmi_msg_mem_request_cb,
	},
	{
		.type = QMI_INDICATION,
		.msg_id = QMI_WLFW_FW_MEM_READY_IND_V01,
		.ei = qmi_wlanfw_mem_ready_ind_msg_v01_ei,
		.decoded_size = sizeof(struct qmi_wlanfw_fw_mem_ready_ind_msg_v01),
		.fn = ath11k_qmi_msg_mem_ready_cb,
	},
	{
		.type = QMI_INDICATION,
		.msg_id = QMI_WLFW_FW_READY_IND_V01,
		.ei = qmi_wlanfw_fw_ready_ind_msg_v01_ei,
		.decoded_size = sizeof(struct qmi_wlanfw_fw_ready_ind_msg_v01),
		.fn = ath11k_qmi_msg_fw_ready_cb,
	},
	{
		.type = QMI_INDICATION,
		.msg_id = QMI_WLFW_COLD_BOOT_CAL_DONE_IND_V01,
		.ei = qmi_wlanfw_cold_boot_cal_done_ind_msg_v01_ei,
		.decoded_size =
			sizeof(struct qmi_wlanfw_fw_cold_cal_done_ind_msg_v01),
		.fn = ath11k_qmi_msg_cold_boot_cal_done_cb,
	},
};

static int ath11k_qmi_ops_new_server(struct qmi_handle *qmi_hdl,
				     struct qmi_service *service)
{
	struct ath11k_qmi *qmi = container_of(qmi_hdl, struct ath11k_qmi, handle);
	struct ath11k_base *ab = qmi->ab;
	struct sockaddr_qrtr *sq = &qmi->sq;
	int ret;

	sq->sq_family = AF_QIPCRTR;
	sq->sq_node = service->node;
	sq->sq_port = service->port;

	ret = kernel_connect(qmi_hdl->sock, (struct sockaddr *)sq,
			     sizeof(*sq), 0);
	if (ret) {
		ath11k_warn(ab, "qmi failed to connect to remote service %d\n", ret);
		return ret;
	}

	ath11k_dbg(ab, ATH11K_DBG_QMI, "qmi wifi fw qmi service connected\n");
	ath11k_qmi_driver_event_post(qmi, ATH11K_QMI_EVENT_SERVER_ARRIVE, NULL);

	return 0;
}

static void ath11k_qmi_ops_del_server(struct qmi_handle *qmi_hdl,
				      struct qmi_service *service)
{
	struct ath11k_qmi *qmi = container_of(qmi_hdl, struct ath11k_qmi, handle);
	struct ath11k_base *ab = qmi->ab;

	ath11k_dbg(ab, ATH11K_DBG_QMI, "qmi wifi fw del server\n");
	ath11k_qmi_driver_event_post(qmi, ATH11K_QMI_EVENT_SERVER_EXIT, NULL);
}

static const struct qmi_ops ath11k_qmi_ops = {
	.new_server = ath11k_qmi_ops_new_server,
	.del_server = ath11k_qmi_ops_del_server,
};

static void ath11k_qmi_driver_event_work(struct work_struct *work)
{
	struct ath11k_qmi *qmi = container_of(work, struct ath11k_qmi,
					      event_work);
	struct ath11k_qmi_driver_event *event;
	struct ath11k_base *ab = qmi->ab;

	spin_lock(&qmi->event_lock);
	while (!list_empty(&qmi->event_list)) {
		event = list_first_entry(&qmi->event_list,
					 struct ath11k_qmi_driver_event, list);
		list_del(&event->list);
		spin_unlock(&qmi->event_lock);

		if (test_bit(ATH11K_FLAG_UNREGISTERING, &ab->dev_flags)) {
			kfree(event);
			return;
		}

		switch (event->type) {
		case ATH11K_QMI_EVENT_SERVER_ARRIVE:
			ath11k_qmi_event_server_arrive(qmi);
			break;
		case ATH11K_QMI_EVENT_SERVER_EXIT:
			set_bit(ATH11K_FLAG_CRASH_FLUSH, &ab->dev_flags);
			set_bit(ATH11K_FLAG_RECOVERY, &ab->dev_flags);
			break;
		case ATH11K_QMI_EVENT_REQUEST_MEM:
			ath11k_qmi_event_mem_request(qmi);
			break;
		case ATH11K_QMI_EVENT_FW_MEM_READY:
			ath11k_qmi_event_load_bdf(qmi);
			break;
		case ATH11K_QMI_EVENT_FW_READY:
			if (test_bit(ATH11K_FLAG_REGISTERED, &ab->dev_flags)) {
				ath11k_hal_dump_srng_stats(ab);
				queue_work(ab->workqueue, &ab->restart_work);
				break;
			}

			ath11k_core_qmi_firmware_ready(ab);
			ab->qmi.cal_done = 1;
			set_bit(ATH11K_FLAG_REGISTERED, &ab->dev_flags);

			break;
		case ATH11K_QMI_EVENT_COLD_BOOT_CAL_DONE:
			break;
		default:
			ath11k_warn(ab, "invalid event type: %d", event->type);
			break;
		}
		kfree(event);
		spin_lock(&qmi->event_lock);
	}
	spin_unlock(&qmi->event_lock);
}

int ath11k_qmi_init_service(struct ath11k_base *ab)
{
	int ret;

	memset(&ab->qmi.target, 0, sizeof(struct target_info));
	memset(&ab->qmi.target_mem, 0, sizeof(struct target_mem_chunk));
	ab->qmi.ab = ab;

	ab->qmi.target_mem_mode = ATH11K_QMI_TARGET_MEM_MODE_DEFAULT;
	ret = qmi_handle_init(&ab->qmi.handle, ATH11K_QMI_RESP_LEN_MAX,
			      &ath11k_qmi_ops, ath11k_qmi_msg_handlers);
	if (ret < 0) {
		ath11k_warn(ab, "failed to initialize qmi handle\n");
		return ret;
	}

	ab->qmi.event_wq = alloc_workqueue("ath11k_qmi_driver_event",
					   WQ_UNBOUND, 1);
	if (!ab->qmi.event_wq) {
		ath11k_err(ab, "failed to allocate workqueue\n");
		return -EFAULT;
	}

	INIT_LIST_HEAD(&ab->qmi.event_list);
	spin_lock_init(&ab->qmi.event_lock);
	INIT_WORK(&ab->qmi.event_work, ath11k_qmi_driver_event_work);

	ret = qmi_add_lookup(&ab->qmi.handle, ATH11K_QMI_WLFW_SERVICE_ID_V01,
			     ATH11K_QMI_WLFW_SERVICE_VERS_V01,
			     ab->qmi.service_ins_id);
	if (ret < 0) {
		ath11k_warn(ab, "failed to add qmi lookup\n");
		destroy_workqueue(ab->qmi.event_wq);
		return ret;
	}

	return ret;
}

void ath11k_qmi_deinit_service(struct ath11k_base *ab)
{
	qmi_handle_release(&ab->qmi.handle);
	cancel_work_sync(&ab->qmi.event_work);
	destroy_workqueue(ab->qmi.event_wq);
	ath11k_qmi_m3_free(ab);
	ath11k_qmi_free_target_mem_chunk(ab);
}

