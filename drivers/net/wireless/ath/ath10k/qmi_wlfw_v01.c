// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/soc/qcom/qmi.h>
#include <linux/types.h>
#include "qmi_wlfw_v01.h"

static const struct qmi_elem_info wlfw_ce_tgt_pipe_cfg_s_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_ce_tgt_pipe_cfg_s_v01,
					   pipe_num),
	},
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum wlfw_pipedir_enum_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_ce_tgt_pipe_cfg_s_v01,
					   pipe_dir),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_ce_tgt_pipe_cfg_s_v01,
					   nentries),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_ce_tgt_pipe_cfg_s_v01,
					   nbytes_max),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_ce_tgt_pipe_cfg_s_v01,
					   flags),
	},
	{}
};

static const struct qmi_elem_info wlfw_ce_svc_pipe_cfg_s_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_ce_svc_pipe_cfg_s_v01,
					   service_id),
	},
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum wlfw_pipedir_enum_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_ce_svc_pipe_cfg_s_v01,
					   pipe_dir),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_ce_svc_pipe_cfg_s_v01,
					   pipe_num),
	},
	{}
};

static const struct qmi_elem_info wlfw_shadow_reg_cfg_s_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_2_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u16),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_shadow_reg_cfg_s_v01,
					   id),
	},
	{
		.data_type      = QMI_UNSIGNED_2_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u16),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_shadow_reg_cfg_s_v01,
					   offset),
	},
	{}
};

static const struct qmi_elem_info wlfw_shadow_reg_v2_cfg_s_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_shadow_reg_v2_cfg_s_v01,
					   addr),
	},
	{}
};

static const struct qmi_elem_info wlfw_memory_region_info_s_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u64),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_memory_region_info_s_v01,
					   region_addr),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_memory_region_info_s_v01,
					   size),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_memory_region_info_s_v01,
					   secure_flag),
	},
	{}
};

static const struct qmi_elem_info wlfw_mem_cfg_s_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u64),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_mem_cfg_s_v01,
					   offset),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_mem_cfg_s_v01,
					   size),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_mem_cfg_s_v01,
					   secure_flag),
	},
	{}
};

static const struct qmi_elem_info wlfw_mem_seg_s_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_mem_seg_s_v01,
					   size),
	},
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum wlfw_mem_type_enum_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_mem_seg_s_v01,
					   type),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_mem_seg_s_v01,
					   mem_cfg_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = QMI_WLFW_MAX_NUM_MEM_CFG_V01,
		.elem_size      = sizeof(struct wlfw_mem_cfg_s_v01),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_mem_seg_s_v01,
					   mem_cfg),
		.ei_array      = wlfw_mem_cfg_s_v01_ei,
	},
	{}
};

static const struct qmi_elem_info wlfw_mem_seg_resp_s_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u64),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_mem_seg_resp_s_v01,
					   addr),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_mem_seg_resp_s_v01,
					   size),
	},
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum wlfw_mem_type_enum_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_mem_seg_resp_s_v01,
					   type),
	},
	{}
};

static const struct qmi_elem_info wlfw_rf_chip_info_s_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_rf_chip_info_s_v01,
					   chip_id),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_rf_chip_info_s_v01,
					   chip_family),
	},
	{}
};

static const struct qmi_elem_info wlfw_rf_board_info_s_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_rf_board_info_s_v01,
					   board_id),
	},
	{}
};

static const struct qmi_elem_info wlfw_soc_info_s_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_soc_info_s_v01,
					   soc_id),
	},
	{}
};

static const struct qmi_elem_info wlfw_fw_version_info_s_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_fw_version_info_s_v01,
					   fw_version),
	},
	{
		.data_type      = QMI_STRING,
		.elem_len       = QMI_WLFW_MAX_TIMESTAMP_LEN_V01 + 1,
		.elem_size      = sizeof(char),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0,
		.offset         = offsetof(struct wlfw_fw_version_info_s_v01,
					   fw_build_timestamp),
	},
	{}
};

const struct qmi_elem_info wlfw_ind_register_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   fw_ready_enable_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   fw_ready_enable),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   initiate_cal_download_enable_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   initiate_cal_download_enable),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   initiate_cal_update_enable_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   initiate_cal_update_enable),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   msa_ready_enable_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   msa_ready_enable),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   pin_connect_result_enable_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   pin_connect_result_enable),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x15,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   client_id_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x15,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   client_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x16,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   request_mem_enable_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x16,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   request_mem_enable),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x17,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   mem_ready_enable_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x17,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   mem_ready_enable),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x18,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   fw_init_done_enable_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x18,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   fw_init_done_enable),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x19,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   rejuvenate_enable_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x19,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   rejuvenate_enable),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x1A,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   xo_cal_enable_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x1A,
		.offset         = offsetof(struct wlfw_ind_register_req_msg_v01,
					   xo_cal_enable),
	},
	{}
};

const struct qmi_elem_info wlfw_ind_register_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_ind_register_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_ind_register_resp_msg_v01,
					   fw_status_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u64),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_ind_register_resp_msg_v01,
					   fw_status),
	},
	{}
};

const struct qmi_elem_info wlfw_fw_ready_ind_msg_v01_ei[] = {
	{}
};

const struct qmi_elem_info wlfw_msa_ready_ind_msg_v01_ei[] = {
	{}
};

const struct qmi_elem_info wlfw_pin_connect_result_ind_msg_v01_ei[] = {
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_pin_connect_result_ind_msg_v01,
					   pwr_pin_result_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_pin_connect_result_ind_msg_v01,
					   pwr_pin_result),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_pin_connect_result_ind_msg_v01,
					   phy_io_pin_result_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_pin_connect_result_ind_msg_v01,
					   phy_io_pin_result),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_pin_connect_result_ind_msg_v01,
					   rf_pin_result_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_pin_connect_result_ind_msg_v01,
					   rf_pin_result),
	},
	{}
};

const struct qmi_elem_info wlfw_wlan_mode_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum wlfw_driver_mode_enum_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_wlan_mode_req_msg_v01,
					   mode),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_wlan_mode_req_msg_v01,
					   hw_debug_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_wlan_mode_req_msg_v01,
					   hw_debug),
	},
	{}
};

const struct qmi_elem_info wlfw_wlan_mode_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_wlan_mode_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_wlan_cfg_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   host_version_valid),
	},
	{
		.data_type      = QMI_STRING,
		.elem_len       = QMI_WLFW_MAX_STR_LEN_V01 + 1,
		.elem_size      = sizeof(char),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   host_version),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   tgt_cfg_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   tgt_cfg_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = QMI_WLFW_MAX_NUM_CE_V01,
		.elem_size      = sizeof(struct wlfw_ce_tgt_pipe_cfg_s_v01),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   tgt_cfg),
		.ei_array      = wlfw_ce_tgt_pipe_cfg_s_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   svc_cfg_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   svc_cfg_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = QMI_WLFW_MAX_NUM_SVC_V01,
		.elem_size      = sizeof(struct wlfw_ce_svc_pipe_cfg_s_v01),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   svc_cfg),
		.ei_array      = wlfw_ce_svc_pipe_cfg_s_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   shadow_reg_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   shadow_reg_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = QMI_WLFW_MAX_NUM_SHADOW_REG_V01,
		.elem_size      = sizeof(struct wlfw_shadow_reg_cfg_s_v01),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   shadow_reg),
		.ei_array      = wlfw_shadow_reg_cfg_s_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   shadow_reg_v2_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   shadow_reg_v2_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = QMI_WLFW_MAX_SHADOW_REG_V2,
		.elem_size      = sizeof(struct wlfw_shadow_reg_v2_cfg_s_v01),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_wlan_cfg_req_msg_v01,
					   shadow_reg_v2),
		.ei_array      = wlfw_shadow_reg_v2_cfg_s_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_wlan_cfg_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_wlan_cfg_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_cap_req_msg_v01_ei[] = {
	{}
};

const struct qmi_elem_info wlfw_cap_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_cap_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_cap_resp_msg_v01,
					   chip_info_valid),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct wlfw_rf_chip_info_s_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_cap_resp_msg_v01,
					   chip_info),
		.ei_array      = wlfw_rf_chip_info_s_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_cap_resp_msg_v01,
					   board_info_valid),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct wlfw_rf_board_info_s_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_cap_resp_msg_v01,
					   board_info),
		.ei_array      = wlfw_rf_board_info_s_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_cap_resp_msg_v01,
					   soc_info_valid),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct wlfw_soc_info_s_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_cap_resp_msg_v01,
					   soc_info),
		.ei_array      = wlfw_soc_info_s_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_cap_resp_msg_v01,
					   fw_version_info_valid),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct wlfw_fw_version_info_s_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_cap_resp_msg_v01,
					   fw_version_info),
		.ei_array      = wlfw_fw_version_info_s_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_cap_resp_msg_v01,
					   fw_build_id_valid),
	},
	{
		.data_type      = QMI_STRING,
		.elem_len       = QMI_WLFW_MAX_BUILD_ID_LEN_V01 + 1,
		.elem_size      = sizeof(char),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_cap_resp_msg_v01,
					   fw_build_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x15,
		.offset         = offsetof(struct wlfw_cap_resp_msg_v01,
					   num_macs_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x15,
		.offset         = offsetof(struct wlfw_cap_resp_msg_v01,
					   num_macs),
	},
	{}
};

const struct qmi_elem_info wlfw_bdf_download_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   valid),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   file_id_valid),
	},
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum wlfw_cal_temp_id_enum_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   file_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   total_size_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   total_size),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   seg_id_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   seg_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   data_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u16),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   data_len),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = QMI_WLFW_MAX_DATA_SIZE_V01,
		.elem_size      = sizeof(u8),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   data),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   end_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   end),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x15,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   bdf_type_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x15,
		.offset         = offsetof(struct wlfw_bdf_download_req_msg_v01,
					   bdf_type),
	},
	{}
};

const struct qmi_elem_info wlfw_bdf_download_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_bdf_download_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_cal_report_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_cal_report_req_msg_v01,
					   meta_data_len),
	},
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = QMI_WLFW_MAX_NUM_CAL_V01,
		.elem_size      = sizeof(enum wlfw_cal_temp_id_enum_v01),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_cal_report_req_msg_v01,
					   meta_data),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_cal_report_req_msg_v01,
					   xo_cal_data_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_cal_report_req_msg_v01,
					   xo_cal_data),
	},
	{}
};

const struct qmi_elem_info wlfw_cal_report_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_cal_report_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_initiate_cal_download_ind_msg_v01_ei[] = {
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum wlfw_cal_temp_id_enum_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_initiate_cal_download_ind_msg_v01,
					   cal_id),
	},
	{}
};

const struct qmi_elem_info wlfw_cal_download_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_cal_download_req_msg_v01,
					   valid),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_cal_download_req_msg_v01,
					   file_id_valid),
	},
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum wlfw_cal_temp_id_enum_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_cal_download_req_msg_v01,
					   file_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_cal_download_req_msg_v01,
					   total_size_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_cal_download_req_msg_v01,
					   total_size),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_cal_download_req_msg_v01,
					   seg_id_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_cal_download_req_msg_v01,
					   seg_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_cal_download_req_msg_v01,
					   data_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u16),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_cal_download_req_msg_v01,
					   data_len),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = QMI_WLFW_MAX_DATA_SIZE_V01,
		.elem_size      = sizeof(u8),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_cal_download_req_msg_v01,
					   data),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_cal_download_req_msg_v01,
					   end_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_cal_download_req_msg_v01,
					   end),
	},
	{}
};

const struct qmi_elem_info wlfw_cal_download_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_cal_download_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_initiate_cal_update_ind_msg_v01_ei[] = {
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum wlfw_cal_temp_id_enum_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_initiate_cal_update_ind_msg_v01,
					   cal_id),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_initiate_cal_update_ind_msg_v01,
					   total_size),
	},
	{}
};

const struct qmi_elem_info wlfw_cal_update_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum wlfw_cal_temp_id_enum_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_cal_update_req_msg_v01,
					   cal_id),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_cal_update_req_msg_v01,
					   seg_id),
	},
	{}
};

const struct qmi_elem_info wlfw_cal_update_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_cal_update_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_cal_update_resp_msg_v01,
					   file_id_valid),
	},
	{
		.data_type      = QMI_SIGNED_4_BYTE_ENUM,
		.elem_len       = 1,
		.elem_size      = sizeof(enum wlfw_cal_temp_id_enum_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_cal_update_resp_msg_v01,
					   file_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_cal_update_resp_msg_v01,
					   total_size_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_cal_update_resp_msg_v01,
					   total_size),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_cal_update_resp_msg_v01,
					   seg_id_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_cal_update_resp_msg_v01,
					   seg_id),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_cal_update_resp_msg_v01,
					   data_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u16),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_cal_update_resp_msg_v01,
					   data_len),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = QMI_WLFW_MAX_DATA_SIZE_V01,
		.elem_size      = sizeof(u8),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_cal_update_resp_msg_v01,
					   data),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_cal_update_resp_msg_v01,
					   end_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_cal_update_resp_msg_v01,
					   end),
	},
	{}
};

const struct qmi_elem_info wlfw_msa_info_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u64),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_msa_info_req_msg_v01,
					   msa_addr),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_msa_info_req_msg_v01,
					   size),
	},
	{}
};

const struct qmi_elem_info wlfw_msa_info_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_msa_info_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x03,
		.offset         = offsetof(struct wlfw_msa_info_resp_msg_v01,
					   mem_region_info_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = QMI_WLFW_MAX_MEM_REG_V01,
		.elem_size      = sizeof(struct wlfw_memory_region_info_s_v01),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x03,
		.offset         = offsetof(struct wlfw_msa_info_resp_msg_v01,
					   mem_region_info),
		.ei_array      = wlfw_memory_region_info_s_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_msa_ready_req_msg_v01_ei[] = {
	{}
};

const struct qmi_elem_info wlfw_msa_ready_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_msa_ready_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_ini_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_ini_req_msg_v01,
					   enablefwlog_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_ini_req_msg_v01,
					   enablefwlog),
	},
	{}
};

const struct qmi_elem_info wlfw_ini_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_ini_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_athdiag_read_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_athdiag_read_req_msg_v01,
					   offset),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_athdiag_read_req_msg_v01,
					   mem_type),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x03,
		.offset         = offsetof(struct wlfw_athdiag_read_req_msg_v01,
					   data_len),
	},
	{}
};

const struct qmi_elem_info wlfw_athdiag_read_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_athdiag_read_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_athdiag_read_resp_msg_v01,
					   data_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u16),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_athdiag_read_resp_msg_v01,
					   data_len),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = QMI_WLFW_MAX_ATHDIAG_DATA_SIZE_V01,
		.elem_size      = sizeof(u8),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_athdiag_read_resp_msg_v01,
					   data),
	},
	{}
};

const struct qmi_elem_info wlfw_athdiag_write_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_athdiag_write_req_msg_v01,
					   offset),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_athdiag_write_req_msg_v01,
					   mem_type),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u16),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x03,
		.offset         = offsetof(struct wlfw_athdiag_write_req_msg_v01,
					   data_len),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = QMI_WLFW_MAX_ATHDIAG_DATA_SIZE_V01,
		.elem_size      = sizeof(u8),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x03,
		.offset         = offsetof(struct wlfw_athdiag_write_req_msg_v01,
					   data),
	},
	{}
};

const struct qmi_elem_info wlfw_athdiag_write_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_athdiag_write_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_vbatt_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u64),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_vbatt_req_msg_v01,
					   voltage_uv),
	},
	{}
};

const struct qmi_elem_info wlfw_vbatt_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_vbatt_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_mac_addr_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_mac_addr_req_msg_v01,
					   mac_addr_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = QMI_WLFW_MAC_ADDR_SIZE_V01,
		.elem_size      = sizeof(u8),
		.array_type       = STATIC_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_mac_addr_req_msg_v01,
					   mac_addr),
	},
	{}
};

const struct qmi_elem_info wlfw_mac_addr_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_mac_addr_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_host_cap_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   daemon_support_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   daemon_support),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   wake_msi_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   wake_msi),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   gpios_valid),
	},
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   gpios_len),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = QMI_WLFW_MAX_NUM_GPIO_V01,
		.elem_size      = sizeof(u32),
		.array_type     = VAR_LEN_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   gpios),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   nm_modem_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   nm_modem),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   bdf_support_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x14,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   bdf_support),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x15,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   bdf_cache_support_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x15,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   bdf_cache_support),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x16,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   m3_support_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x16,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   m3_support),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x17,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   m3_cache_support_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x17,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   m3_cache_support),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x18,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   cal_filesys_support_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x18,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   cal_filesys_support),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x19,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   cal_cache_support_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x19,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   cal_cache_support),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x1A,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   cal_done_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x1A,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   cal_done),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x1B,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   mem_bucket_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x1B,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   mem_bucket),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x1C,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   mem_cfg_mode_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x1C,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   mem_cfg_mode),
	},
	{}
};

const struct qmi_elem_info wlfw_host_cap_8bit_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   daemon_support_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_host_cap_req_msg_v01,
					   daemon_support),
	},
	{}
};

const struct qmi_elem_info wlfw_host_cap_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_host_cap_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_request_mem_ind_msg_v01_ei[] = {
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_request_mem_ind_msg_v01,
					   mem_seg_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = QMI_WLFW_MAX_NUM_MEM_SEG_V01,
		.elem_size      = sizeof(struct wlfw_mem_seg_s_v01),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_request_mem_ind_msg_v01,
					   mem_seg),
		.ei_array      = wlfw_mem_seg_s_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_respond_mem_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_DATA_LEN,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_respond_mem_req_msg_v01,
					   mem_seg_len),
	},
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = QMI_WLFW_MAX_NUM_MEM_SEG_V01,
		.elem_size      = sizeof(struct wlfw_mem_seg_resp_s_v01),
		.array_type       = VAR_LEN_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_respond_mem_req_msg_v01,
					   mem_seg),
		.ei_array      = wlfw_mem_seg_resp_s_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_respond_mem_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_respond_mem_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_mem_ready_ind_msg_v01_ei[] = {
	{}
};

const struct qmi_elem_info wlfw_fw_init_done_ind_msg_v01_ei[] = {
	{}
};

const struct qmi_elem_info wlfw_rejuvenate_ind_msg_v01_ei[] = {
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_rejuvenate_ind_msg_v01,
					   cause_for_rejuvenation_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_rejuvenate_ind_msg_v01,
					   cause_for_rejuvenation),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_rejuvenate_ind_msg_v01,
					   requesting_sub_system_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_rejuvenate_ind_msg_v01,
					   requesting_sub_system),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_rejuvenate_ind_msg_v01,
					   line_number_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_2_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u16),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x12,
		.offset         = offsetof(struct wlfw_rejuvenate_ind_msg_v01,
					   line_number),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_rejuvenate_ind_msg_v01,
					   function_name_valid),
	},
	{
		.data_type      = QMI_STRING,
		.elem_len       = QMI_WLFW_FUNCTION_NAME_LEN_V01 + 1,
		.elem_size      = sizeof(char),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x13,
		.offset         = offsetof(struct wlfw_rejuvenate_ind_msg_v01,
					   function_name),
	},
	{}
};

const struct qmi_elem_info wlfw_rejuvenate_ack_req_msg_v01_ei[] = {
	{}
};

const struct qmi_elem_info wlfw_rejuvenate_ack_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_rejuvenate_ack_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_dynamic_feature_mask_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_dynamic_feature_mask_req_msg_v01,
					   mask_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u64),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_dynamic_feature_mask_req_msg_v01,
					   mask),
	},
	{}
};

const struct qmi_elem_info wlfw_dynamic_feature_mask_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_dynamic_feature_mask_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_dynamic_feature_mask_resp_msg_v01,
					   prev_mask_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u64),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x10,
		.offset         = offsetof(struct wlfw_dynamic_feature_mask_resp_msg_v01,
					   prev_mask),
	},
	{
		.data_type      = QMI_OPT_FLAG,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_dynamic_feature_mask_resp_msg_v01,
					   curr_mask_valid),
	},
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u64),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x11,
		.offset         = offsetof(struct wlfw_dynamic_feature_mask_resp_msg_v01,
					   curr_mask),
	},
	{}
};

const struct qmi_elem_info wlfw_m3_info_req_msg_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_8_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u64),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_m3_info_req_msg_v01,
					   addr),
	},
	{
		.data_type      = QMI_UNSIGNED_4_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u32),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_m3_info_req_msg_v01,
					   size),
	},
	{}
};

const struct qmi_elem_info wlfw_m3_info_resp_msg_v01_ei[] = {
	{
		.data_type      = QMI_STRUCT,
		.elem_len       = 1,
		.elem_size      = sizeof(struct qmi_response_type_v01),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x02,
		.offset         = offsetof(struct wlfw_m3_info_resp_msg_v01,
					   resp),
		.ei_array      = qmi_response_type_v01_ei,
	},
	{}
};

const struct qmi_elem_info wlfw_xo_cal_ind_msg_v01_ei[] = {
	{
		.data_type      = QMI_UNSIGNED_1_BYTE,
		.elem_len       = 1,
		.elem_size      = sizeof(u8),
		.array_type     = NO_ARRAY,
		.tlv_type       = 0x01,
		.offset         = offsetof(struct wlfw_xo_cal_ind_msg_v01,
					   xo_cal_data),
	},
	{}
};
