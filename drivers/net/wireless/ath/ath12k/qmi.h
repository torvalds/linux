/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_QMI_H
#define ATH12K_QMI_H

#include <linux/mutex.h>
#include <linux/soc/qcom/qmi.h>

#define ATH12K_HOST_VERSION_STRING		"WIN"
#define ATH12K_QMI_WLANFW_TIMEOUT_MS		10000
#define ATH12K_QMI_MAX_BDF_FILE_NAME_SIZE	64
#define ATH12K_QMI_CALDB_ADDRESS		0x4BA00000
#define ATH12K_QMI_WLANFW_MAX_BUILD_ID_LEN_V01	128
#define ATH12K_QMI_WLFW_SERVICE_ID_V01		0x45
#define ATH12K_QMI_WLFW_SERVICE_VERS_V01	0x01
#define ATH12K_QMI_WLFW_SERVICE_INS_ID_V01	0x02
#define ATH12K_QMI_WLFW_SERVICE_INS_ID_V01_WCN7850 0x1

#define ATH12K_QMI_WLFW_SERVICE_INS_ID_V01_QCN9274	0x07
#define ATH12K_QMI_WLANFW_MAX_TIMESTAMP_LEN_V01	32
#define ATH12K_QMI_RESP_LEN_MAX			8192
#define ATH12K_QMI_WLANFW_MAX_NUM_MEM_SEG_V01	52
#define ATH12K_QMI_CALDB_SIZE			0x480000
#define ATH12K_QMI_BDF_EXT_STR_LENGTH		0x20
#define ATH12K_QMI_FW_MEM_REQ_SEGMENT_CNT	3
#define ATH12K_QMI_WLFW_MAX_DEV_MEM_NUM_V01 4
#define ATH12K_QMI_DEVMEM_CMEM_INDEX	0

#define QMI_WLFW_REQUEST_MEM_IND_V01		0x0035
#define QMI_WLFW_FW_MEM_READY_IND_V01		0x0037
#define QMI_WLFW_FW_READY_IND_V01		0x0038

#define QMI_WLANFW_MAX_DATA_SIZE_V01		6144
#define ATH12K_FIRMWARE_MODE_OFF		4
#define ATH12K_QMI_TARGET_MEM_MODE_DEFAULT	0

#define ATH12K_BOARD_ID_DEFAULT	0xFF

struct ath12k_base;

enum ath12k_qmi_file_type {
	ATH12K_QMI_FILE_TYPE_BDF_GOLDEN	= 0,
	ATH12K_QMI_FILE_TYPE_CALDATA	= 2,
	ATH12K_QMI_FILE_TYPE_EEPROM	= 3,
	ATH12K_QMI_MAX_FILE_TYPE	= 4,
};

enum ath12k_qmi_bdf_type {
	ATH12K_QMI_BDF_TYPE_BIN			= 0,
	ATH12K_QMI_BDF_TYPE_ELF			= 1,
	ATH12K_QMI_BDF_TYPE_REGDB		= 4,
	ATH12K_QMI_BDF_TYPE_CALIBRATION		= 5,
};

enum ath12k_qmi_event_type {
	ATH12K_QMI_EVENT_SERVER_ARRIVE,
	ATH12K_QMI_EVENT_SERVER_EXIT,
	ATH12K_QMI_EVENT_REQUEST_MEM,
	ATH12K_QMI_EVENT_FW_MEM_READY,
	ATH12K_QMI_EVENT_FW_READY,
	ATH12K_QMI_EVENT_REGISTER_DRIVER,
	ATH12K_QMI_EVENT_UNREGISTER_DRIVER,
	ATH12K_QMI_EVENT_RECOVERY,
	ATH12K_QMI_EVENT_FORCE_FW_ASSERT,
	ATH12K_QMI_EVENT_POWER_UP,
	ATH12K_QMI_EVENT_POWER_DOWN,
	ATH12K_QMI_EVENT_MAX,
};

struct ath12k_qmi_driver_event {
	struct list_head list;
	enum ath12k_qmi_event_type type;
	void *data;
};

struct ath12k_qmi_ce_cfg {
	const struct ce_pipe_config *tgt_ce;
	int tgt_ce_len;
	const struct service_to_pipe *svc_to_ce_map;
	int svc_to_ce_map_len;
	const u8 *shadow_reg;
	int shadow_reg_len;
	u32 *shadow_reg_v3;
	int shadow_reg_v3_len;
};

struct ath12k_qmi_event_msg {
	struct list_head list;
	enum ath12k_qmi_event_type type;
};

struct target_mem_chunk {
	u32 size;
	u32 type;
	u32 prev_size;
	u32 prev_type;
	dma_addr_t paddr;
	union {
		void __iomem *ioaddr;
		void *addr;
	} v;
};

struct target_info {
	u32 chip_id;
	u32 chip_family;
	u32 board_id;
	u32 soc_id;
	u32 fw_version;
	u32 eeprom_caldata;
	char fw_build_timestamp[ATH12K_QMI_WLANFW_MAX_TIMESTAMP_LEN_V01 + 1];
	char fw_build_id[ATH12K_QMI_WLANFW_MAX_BUILD_ID_LEN_V01 + 1];
	char bdf_ext[ATH12K_QMI_BDF_EXT_STR_LENGTH];
};

struct m3_mem_region {
	u32 size;
	dma_addr_t paddr;
	void *vaddr;
};

struct dev_mem_info {
	u64 start;
	u64 size;
};

struct ath12k_qmi {
	struct ath12k_base *ab;
	struct qmi_handle handle;
	struct sockaddr_qrtr sq;
	struct work_struct event_work;
	struct workqueue_struct *event_wq;
	struct list_head event_list;
	spinlock_t event_lock; /* spinlock for qmi event list */
	struct ath12k_qmi_ce_cfg ce_cfg;
	struct target_mem_chunk target_mem[ATH12K_QMI_WLANFW_MAX_NUM_MEM_SEG_V01];
	u32 mem_seg_count;
	u32 target_mem_mode;
	bool target_mem_delayed;
	u8 cal_done;
	u8 num_radios;
	struct target_info target;
	struct m3_mem_region m3_mem;
	unsigned int service_ins_id;
	struct dev_mem_info dev_mem[ATH12K_QMI_WLFW_MAX_DEV_MEM_NUM_V01];
};

#define QMI_WLANFW_HOST_CAP_REQ_MSG_V01_MAX_LEN		261
#define QMI_WLANFW_HOST_CAP_REQ_V01			0x0034
#define QMI_WLANFW_HOST_CAP_RESP_MSG_V01_MAX_LEN	7
#define QMI_WLFW_HOST_CAP_RESP_V01			0x0034
#define QMI_WLFW_MAX_NUM_GPIO_V01			32
#define QMI_WLANFW_MAX_PLATFORM_NAME_LEN_V01		64
#define QMI_WLANFW_MAX_HOST_DDR_RANGE_SIZE_V01		3

struct qmi_wlanfw_host_ddr_range {
	u64 start;
	u64 size;
};

enum ath12k_qmi_target_mem {
	HOST_DDR_REGION_TYPE = 0x1,
	BDF_MEM_REGION_TYPE = 0x2,
	M3_DUMP_REGION_TYPE = 0x3,
	CALDB_MEM_REGION_TYPE = 0x4,
	PAGEABLE_MEM_REGION_TYPE = 0x9,
};

enum qmi_wlanfw_host_build_type {
	WLANFW_HOST_BUILD_TYPE_ENUM_MIN_VAL_V01 = INT_MIN,
	QMI_WLANFW_HOST_BUILD_TYPE_UNSPECIFIED_V01 = 0,
	QMI_WLANFW_HOST_BUILD_TYPE_PRIMARY_V01 = 1,
	QMI_WLANFW_HOST_BUILD_TYPE_SECONDARY_V01 = 2,
	WLANFW_HOST_BUILD_TYPE_ENUM_MAX_VAL_V01 = INT_MAX,
};

#define QMI_WLFW_MAX_NUM_MLO_CHIPS_V01 3
#define QMI_WLFW_MAX_NUM_MLO_LINKS_PER_CHIP_V01 2

struct wlfw_host_mlo_chip_info_s_v01 {
	u8 chip_id;
	u8 num_local_links;
	u8 hw_link_id[QMI_WLFW_MAX_NUM_MLO_LINKS_PER_CHIP_V01];
	u8 valid_mlo_link_id[QMI_WLFW_MAX_NUM_MLO_LINKS_PER_CHIP_V01];
};

enum ath12k_qmi_cnss_feature {
	CNSS_FEATURE_MIN_ENUM_VAL_V01 = INT_MIN,
	CNSS_QDSS_CFG_MISS_V01 = 3,
	CNSS_PCIE_PERST_NO_PULL_V01 = 4,
	CNSS_MAX_FEATURE_V01 = 64,
	CNSS_FEATURE_MAX_ENUM_VAL_V01 = INT_MAX,
};

struct qmi_wlanfw_host_cap_req_msg_v01 {
	u8 num_clients_valid;
	u32 num_clients;
	u8 wake_msi_valid;
	u32 wake_msi;
	u8 gpios_valid;
	u32 gpios_len;
	u32 gpios[QMI_WLFW_MAX_NUM_GPIO_V01];
	u8 nm_modem_valid;
	u8 nm_modem;
	u8 bdf_support_valid;
	u8 bdf_support;
	u8 bdf_cache_support_valid;
	u8 bdf_cache_support;
	u8 m3_support_valid;
	u8 m3_support;
	u8 m3_cache_support_valid;
	u8 m3_cache_support;
	u8 cal_filesys_support_valid;
	u8 cal_filesys_support;
	u8 cal_cache_support_valid;
	u8 cal_cache_support;
	u8 cal_done_valid;
	u8 cal_done;
	u8 mem_bucket_valid;
	u32 mem_bucket;
	u8 mem_cfg_mode_valid;
	u8 mem_cfg_mode;
	u8 cal_duration_valid;
	u16 cal_duraiton;
	u8 platform_name_valid;
	char platform_name[QMI_WLANFW_MAX_PLATFORM_NAME_LEN_V01 + 1];
	u8 ddr_range_valid;
	struct qmi_wlanfw_host_ddr_range ddr_range[QMI_WLANFW_MAX_HOST_DDR_RANGE_SIZE_V01];
	u8 host_build_type_valid;
	enum qmi_wlanfw_host_build_type host_build_type;
	u8 mlo_capable_valid;
	u8 mlo_capable;
	u8 mlo_chip_id_valid;
	u16 mlo_chip_id;
	u8 mlo_group_id_valid;
	u8 mlo_group_id;
	u8 max_mlo_peer_valid;
	u16 max_mlo_peer;
	u8 mlo_num_chips_valid;
	u8 mlo_num_chips;
	u8 mlo_chip_info_valid;
	struct wlfw_host_mlo_chip_info_s_v01 mlo_chip_info[QMI_WLFW_MAX_NUM_MLO_CHIPS_V01];
	u8 feature_list_valid;
	u64 feature_list;

};

struct qmi_wlanfw_host_cap_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

#define QMI_WLANFW_PHY_CAP_REQ_MSG_V01_MAX_LEN		0
#define QMI_WLANFW_PHY_CAP_REQ_V01			0x0057
#define QMI_WLANFW_PHY_CAP_RESP_MSG_V01_MAX_LEN		18
#define QMI_WLANFW_PHY_CAP_RESP_V01			0x0057

struct qmi_wlanfw_phy_cap_req_msg_v01 {
};

struct qmi_wlanfw_phy_cap_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
	u8 num_phy_valid;
	u8 num_phy;
	u8 board_id_valid;
	u32 board_id;
};

#define QMI_WLANFW_IND_REGISTER_REQ_MSG_V01_MAX_LEN		54
#define QMI_WLANFW_IND_REGISTER_REQ_V01				0x0020
#define QMI_WLANFW_IND_REGISTER_RESP_MSG_V01_MAX_LEN		18
#define QMI_WLANFW_IND_REGISTER_RESP_V01			0x0020
#define QMI_WLANFW_CLIENT_ID					0x4b4e454c

struct qmi_wlanfw_ind_register_req_msg_v01 {
	u8 fw_ready_enable_valid;
	u8 fw_ready_enable;
	u8 initiate_cal_download_enable_valid;
	u8 initiate_cal_download_enable;
	u8 initiate_cal_update_enable_valid;
	u8 initiate_cal_update_enable;
	u8 msa_ready_enable_valid;
	u8 msa_ready_enable;
	u8 pin_connect_result_enable_valid;
	u8 pin_connect_result_enable;
	u8 client_id_valid;
	u32 client_id;
	u8 request_mem_enable_valid;
	u8 request_mem_enable;
	u8 fw_mem_ready_enable_valid;
	u8 fw_mem_ready_enable;
	u8 fw_init_done_enable_valid;
	u8 fw_init_done_enable;
	u8 rejuvenate_enable_valid;
	u32 rejuvenate_enable;
	u8 xo_cal_enable_valid;
	u8 xo_cal_enable;
	u8 cal_done_enable_valid;
	u8 cal_done_enable;
};

struct qmi_wlanfw_ind_register_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
	u8 fw_status_valid;
	u64 fw_status;
};

#define QMI_WLANFW_REQUEST_MEM_IND_MSG_V01_MAX_LEN	1824
#define QMI_WLANFW_RESPOND_MEM_REQ_MSG_V01_MAX_LEN	888
#define QMI_WLANFW_RESPOND_MEM_RESP_MSG_V01_MAX_LEN	7
#define QMI_WLANFW_REQUEST_MEM_IND_V01			0x0035
#define QMI_WLANFW_RESPOND_MEM_REQ_V01			0x0036
#define QMI_WLANFW_RESPOND_MEM_RESP_V01			0x0036
#define QMI_WLANFW_MAX_NUM_MEM_CFG_V01			2
#define QMI_WLANFW_MAX_STR_LEN_V01                      16

struct qmi_wlanfw_mem_cfg_s_v01 {
	u64 offset;
	u32 size;
	u8 secure_flag;
};

enum qmi_wlanfw_mem_type_enum_v01 {
	WLANFW_MEM_TYPE_ENUM_MIN_VAL_V01 = INT_MIN,
	QMI_WLANFW_MEM_TYPE_MSA_V01 = 0,
	QMI_WLANFW_MEM_TYPE_DDR_V01 = 1,
	QMI_WLANFW_MEM_BDF_V01 = 2,
	QMI_WLANFW_MEM_M3_V01 = 3,
	QMI_WLANFW_MEM_CAL_V01 = 4,
	QMI_WLANFW_MEM_DPD_V01 = 5,
	WLANFW_MEM_TYPE_ENUM_MAX_VAL_V01 = INT_MAX,
};

struct qmi_wlanfw_mem_seg_s_v01 {
	u32 size;
	enum qmi_wlanfw_mem_type_enum_v01 type;
	u32 mem_cfg_len;
	struct qmi_wlanfw_mem_cfg_s_v01 mem_cfg[QMI_WLANFW_MAX_NUM_MEM_CFG_V01];
};

struct qmi_wlanfw_request_mem_ind_msg_v01 {
	u32 mem_seg_len;
	struct qmi_wlanfw_mem_seg_s_v01 mem_seg[ATH12K_QMI_WLANFW_MAX_NUM_MEM_SEG_V01];
};

struct qmi_wlanfw_mem_seg_resp_s_v01 {
	u64 addr;
	u32 size;
	enum qmi_wlanfw_mem_type_enum_v01 type;
	u8 restore;
};

struct qmi_wlanfw_respond_mem_req_msg_v01 {
	u32 mem_seg_len;
	struct qmi_wlanfw_mem_seg_resp_s_v01 mem_seg[ATH12K_QMI_WLANFW_MAX_NUM_MEM_SEG_V01];
};

struct qmi_wlanfw_respond_mem_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

struct qmi_wlanfw_fw_mem_ready_ind_msg_v01 {
	char placeholder;
};

struct qmi_wlanfw_fw_ready_ind_msg_v01 {
	char placeholder;
};

#define QMI_WLANFW_CAP_REQ_MSG_V01_MAX_LEN	0
#define QMI_WLANFW_CAP_RESP_MSG_V01_MAX_LEN	207
#define QMI_WLANFW_CAP_REQ_V01			0x0024
#define QMI_WLANFW_CAP_RESP_V01			0x0024

enum qmi_wlanfw_pipedir_enum_v01 {
	QMI_WLFW_PIPEDIR_NONE_V01 = 0,
	QMI_WLFW_PIPEDIR_IN_V01 = 1,
	QMI_WLFW_PIPEDIR_OUT_V01 = 2,
	QMI_WLFW_PIPEDIR_INOUT_V01 = 3,
};

struct qmi_wlanfw_ce_tgt_pipe_cfg_s_v01 {
	__le32 pipe_num;
	__le32 pipe_dir;
	__le32 nentries;
	__le32 nbytes_max;
	__le32 flags;
};

struct qmi_wlanfw_ce_svc_pipe_cfg_s_v01 {
	__le32 service_id;
	__le32 pipe_dir;
	__le32 pipe_num;
};

struct qmi_wlanfw_shadow_reg_cfg_s_v01 {
	u16 id;
	u16 offset;
};

struct qmi_wlanfw_shadow_reg_v3_cfg_s_v01 {
	u32 addr;
};

struct qmi_wlanfw_memory_region_info_s_v01 {
	u64 region_addr;
	u32 size;
	u8 secure_flag;
};

struct qmi_wlanfw_rf_chip_info_s_v01 {
	u32 chip_id;
	u32 chip_family;
};

struct qmi_wlanfw_rf_board_info_s_v01 {
	u32 board_id;
};

struct qmi_wlanfw_soc_info_s_v01 {
	u32 soc_id;
};

struct qmi_wlanfw_fw_version_info_s_v01 {
	u32 fw_version;
	char fw_build_timestamp[ATH12K_QMI_WLANFW_MAX_TIMESTAMP_LEN_V01 + 1];
};

struct qmi_wlanfw_dev_mem_info_s_v01 {
	u64 start;
	u64 size;
};

enum qmi_wlanfw_cal_temp_id_enum_v01 {
	QMI_WLANFW_CAL_TEMP_IDX_0_V01 = 0,
	QMI_WLANFW_CAL_TEMP_IDX_1_V01 = 1,
	QMI_WLANFW_CAL_TEMP_IDX_2_V01 = 2,
	QMI_WLANFW_CAL_TEMP_IDX_3_V01 = 3,
	QMI_WLANFW_CAL_TEMP_IDX_4_V01 = 4,
	QMI_WLANFW_CAL_TEMP_ID_MAX_V01 = 0xFF,
};

enum qmi_wlanfw_rd_card_chain_cap_v01 {
	WLFW_RD_CARD_CHAIN_CAP_MIN_VAL_V01 = INT_MIN,
	WLFW_RD_CARD_CHAIN_CAP_UNSPECIFIED_V01 = 0,
	WLFW_RD_CARD_CHAIN_CAP_1x1_V01 = 1,
	WLFW_RD_CARD_CHAIN_CAP_2x2_V01 = 2,
	WLFW_RD_CARD_CHAIN_CAP_MAX_VAL_V01 = INT_MAX,
};

struct qmi_wlanfw_cap_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
	u8 chip_info_valid;
	struct qmi_wlanfw_rf_chip_info_s_v01 chip_info;
	u8 board_info_valid;
	struct qmi_wlanfw_rf_board_info_s_v01 board_info;
	u8 soc_info_valid;
	struct qmi_wlanfw_soc_info_s_v01 soc_info;
	u8 fw_version_info_valid;
	struct qmi_wlanfw_fw_version_info_s_v01 fw_version_info;
	u8 fw_build_id_valid;
	char fw_build_id[ATH12K_QMI_WLANFW_MAX_BUILD_ID_LEN_V01 + 1];
	u8 num_macs_valid;
	u8 num_macs;
	u8 voltage_mv_valid;
	u32 voltage_mv;
	u8 time_freq_hz_valid;
	u32 time_freq_hz;
	u8 otp_version_valid;
	u32 otp_version;
	u8 eeprom_caldata_read_timeout_valid;
	u32 eeprom_caldata_read_timeout;
	u8 fw_caps_valid;
	u64 fw_caps;
	u8 rd_card_chain_cap_valid;
	enum qmi_wlanfw_rd_card_chain_cap_v01 rd_card_chain_cap;
	u8 dev_mem_info_valid;
	struct qmi_wlanfw_dev_mem_info_s_v01 dev_mem[ATH12K_QMI_WLFW_MAX_DEV_MEM_NUM_V01];
};

struct qmi_wlanfw_cap_req_msg_v01 {
	char placeholder;
};

#define QMI_WLANFW_BDF_DOWNLOAD_REQ_MSG_V01_MAX_LEN	6182
#define QMI_WLANFW_BDF_DOWNLOAD_RESP_MSG_V01_MAX_LEN	7
#define QMI_WLANFW_BDF_DOWNLOAD_RESP_V01		0x0025
#define QMI_WLANFW_BDF_DOWNLOAD_REQ_V01			0x0025
/* TODO: Need to check with MCL and FW team that data can be pointer and
 * can be last element in structure
 */
struct qmi_wlanfw_bdf_download_req_msg_v01 {
	u8 valid;
	u8 file_id_valid;
	enum qmi_wlanfw_cal_temp_id_enum_v01 file_id;
	u8 total_size_valid;
	u32 total_size;
	u8 seg_id_valid;
	u32 seg_id;
	u8 data_valid;
	u32 data_len;
	u8 data[QMI_WLANFW_MAX_DATA_SIZE_V01];
	u8 end_valid;
	u8 end;
	u8 bdf_type_valid;
	u8 bdf_type;

};

struct qmi_wlanfw_bdf_download_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

#define QMI_WLANFW_M3_INFO_REQ_MSG_V01_MAX_MSG_LEN	18
#define QMI_WLANFW_M3_INFO_RESP_MSG_V01_MAX_MSG_LEN	7
#define QMI_WLANFW_M3_INFO_RESP_V01		0x003C
#define QMI_WLANFW_M3_INFO_REQ_V01		0x003C

struct qmi_wlanfw_m3_info_req_msg_v01 {
	u64 addr;
	u32 size;
};

struct qmi_wlanfw_m3_info_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

#define QMI_WLANFW_WLAN_MODE_REQ_MSG_V01_MAX_LEN	11
#define QMI_WLANFW_WLAN_MODE_RESP_MSG_V01_MAX_LEN	7
#define QMI_WLANFW_WLAN_CFG_REQ_MSG_V01_MAX_LEN		803
#define QMI_WLANFW_WLAN_CFG_RESP_MSG_V01_MAX_LEN	7
#define QMI_WLANFW_WLAN_MODE_REQ_V01			0x0022
#define QMI_WLANFW_WLAN_MODE_RESP_V01			0x0022
#define QMI_WLANFW_WLAN_CFG_REQ_V01			0x0023
#define QMI_WLANFW_WLAN_CFG_RESP_V01			0x0023
#define QMI_WLANFW_MAX_STR_LEN_V01			16
#define QMI_WLANFW_MAX_NUM_CE_V01			12
#define QMI_WLANFW_MAX_NUM_SVC_V01			24
#define QMI_WLANFW_MAX_NUM_SHADOW_REG_V01		24
#define QMI_WLANFW_MAX_NUM_SHADOW_REG_V3_V01		60

struct qmi_wlanfw_wlan_mode_req_msg_v01 {
	u32 mode;
	u8 hw_debug_valid;
	u8 hw_debug;
};

struct qmi_wlanfw_wlan_mode_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

struct qmi_wlanfw_wlan_cfg_req_msg_v01 {
	u8 host_version_valid;
	char host_version[QMI_WLANFW_MAX_STR_LEN_V01 + 1];
	u8  tgt_cfg_valid;
	u32  tgt_cfg_len;
	struct qmi_wlanfw_ce_tgt_pipe_cfg_s_v01
			tgt_cfg[QMI_WLANFW_MAX_NUM_CE_V01];
	u8  svc_cfg_valid;
	u32 svc_cfg_len;
	struct qmi_wlanfw_ce_svc_pipe_cfg_s_v01
			svc_cfg[QMI_WLANFW_MAX_NUM_SVC_V01];
	u8 shadow_reg_valid;
	u32 shadow_reg_len;
	struct qmi_wlanfw_shadow_reg_cfg_s_v01
		shadow_reg[QMI_WLANFW_MAX_NUM_SHADOW_REG_V01];
	u8 shadow_reg_v3_valid;
	u32 shadow_reg_v3_len;
	struct qmi_wlanfw_shadow_reg_v3_cfg_s_v01
		shadow_reg_v3[QMI_WLANFW_MAX_NUM_SHADOW_REG_V3_V01];
};

struct qmi_wlanfw_wlan_cfg_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

#define ATH12K_QMI_WLANFW_WLAN_INI_REQ_V01	0x002F
#define ATH12K_QMI_WLANFW_WLAN_INI_RESP_V01	0x002F
#define QMI_WLANFW_WLAN_INI_REQ_MSG_V01_MAX_LEN		7
#define QMI_WLANFW_WLAN_INI_RESP_MSG_V01_MAX_LEN	7

struct qmi_wlanfw_wlan_ini_req_msg_v01 {
	/* Must be set to true if enable_fwlog is being passed */
	u8 enable_fwlog_valid;
	u8 enable_fwlog;
};

struct qmi_wlanfw_wlan_ini_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

int ath12k_qmi_firmware_start(struct ath12k_base *ab,
			      u32 mode);
void ath12k_qmi_firmware_stop(struct ath12k_base *ab);
void ath12k_qmi_deinit_service(struct ath12k_base *ab);
int ath12k_qmi_init_service(struct ath12k_base *ab);
void ath12k_qmi_free_resource(struct ath12k_base *ab);

#endif
