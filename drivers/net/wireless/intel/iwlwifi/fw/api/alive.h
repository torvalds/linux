/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014, 2018, 2020-2021, 2024 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_alive_h__
#define __iwl_fw_api_alive_h__

/* alive response is_valid values */
#define ALIVE_RESP_UCODE_OK	BIT(0)
#define ALIVE_RESP_RFKILL	BIT(1)

/* alive response ver_type values */
enum {
	FW_TYPE_HW = 0,
	FW_TYPE_PROT = 1,
	FW_TYPE_AP = 2,
	FW_TYPE_WOWLAN = 3,
	FW_TYPE_TIMING = 4,
	FW_TYPE_WIPAN = 5
};

/* alive response ver_subtype values */
enum {
	FW_SUBTYPE_FULL_FEATURE = 0,
	FW_SUBTYPE_BOOTSRAP = 1, /* Not valid */
	FW_SUBTYPE_REDUCED = 2,
	FW_SUBTYPE_ALIVE_ONLY = 3,
	FW_SUBTYPE_WOWLAN = 4,
	FW_SUBTYPE_AP_SUBTYPE = 5,
	FW_SUBTYPE_WIPAN = 6,
	FW_SUBTYPE_INITIALIZE = 9
};

#define IWL_ALIVE_STATUS_ERR 0xDEAD
#define IWL_ALIVE_STATUS_OK 0xCAFE

#define IWL_ALIVE_FLG_RFKILL	BIT(0)

struct iwl_lmac_debug_addrs {
	__le32 error_event_table_ptr;	/* SRAM address for error log */
	__le32 log_event_table_ptr;	/* SRAM address for LMAC event log */
	__le32 cpu_register_ptr;
	__le32 dbgm_config_ptr;
	__le32 alive_counter_ptr;
	__le32 scd_base_ptr;		/* SRAM address for SCD */
	__le32 st_fwrd_addr;		/* pointer to Store and forward */
	__le32 st_fwrd_size;
} __packed; /* UCODE_DEBUG_ADDRS_API_S_VER_2 */

struct iwl_lmac_alive {
	__le32 ucode_major;
	__le32 ucode_minor;
	u8 ver_subtype;
	u8 ver_type;
	u8 mac;
	u8 opt;
	__le32 timestamp;
	struct iwl_lmac_debug_addrs dbg_ptrs;
} __packed; /* UCODE_ALIVE_NTFY_API_S_VER_3 */

struct iwl_umac_debug_addrs {
	__le32 error_info_addr;		/* SRAM address for UMAC error log */
	__le32 dbg_print_buff_addr;
} __packed; /* UMAC_DEBUG_ADDRS_API_S_VER_1 */

struct iwl_umac_alive {
	__le32 umac_major;		/* UMAC version: major */
	__le32 umac_minor;		/* UMAC version: minor */
	struct iwl_umac_debug_addrs dbg_ptrs;
} __packed; /* UMAC_ALIVE_DATA_API_S_VER_2 */

struct iwl_sku_id {
	__le32 data[3];
} __packed; /* SKU_ID_API_S_VER_1 */

struct iwl_alive_ntf_v3 {
	__le16 status;
	__le16 flags;
	struct iwl_lmac_alive lmac_data;
	struct iwl_umac_alive umac_data;
} __packed; /* UCODE_ALIVE_NTFY_API_S_VER_3 */

struct iwl_alive_ntf_v4 {
	__le16 status;
	__le16 flags;
	struct iwl_lmac_alive lmac_data[2];
	struct iwl_umac_alive umac_data;
} __packed; /* UCODE_ALIVE_NTFY_API_S_VER_4 */

struct iwl_alive_ntf_v5 {
	__le16 status;
	__le16 flags;
	struct iwl_lmac_alive lmac_data[2];
	struct iwl_umac_alive umac_data;
	struct iwl_sku_id sku_id;
} __packed; /* UCODE_ALIVE_NTFY_API_S_VER_5 */

struct iwl_imr_alive_info {
	__le64 base_addr;
	__le32 size;
	__le32 enabled;
} __packed; /* IMR_ALIVE_INFO_API_S_VER_1 */

struct iwl_alive_ntf_v6 {
	__le16 status;
	__le16 flags;
	struct iwl_lmac_alive lmac_data[2];
	struct iwl_umac_alive umac_data;
	struct iwl_sku_id sku_id;
	struct iwl_imr_alive_info imr;
} __packed; /* UCODE_ALIVE_NTFY_API_S_VER_6 */

/**
 * enum iwl_extended_cfg_flags - commands driver may send before
 *	finishing init flow
 * @IWL_INIT_DEBUG_CFG: driver is going to send debug config command
 * @IWL_INIT_NVM: driver is going to send NVM_ACCESS commands
 * @IWL_INIT_PHY: driver is going to send PHY_DB commands
 */
enum iwl_extended_cfg_flags {
	IWL_INIT_DEBUG_CFG,
	IWL_INIT_NVM,
	IWL_INIT_PHY,
};

/**
 * struct iwl_init_extended_cfg_cmd - mark what commands ucode should wait for
 * before finishing init flows
 * @init_flags: values from iwl_extended_cfg_flags
 */
struct iwl_init_extended_cfg_cmd {
	__le32 init_flags;
} __packed; /* INIT_EXTENDED_CFG_CMD_API_S_VER_1 */

/**
 * struct iwl_radio_version_notif - information on the radio version
 * ( RADIO_VERSION_NOTIFICATION = 0x68 )
 * @radio_flavor: radio flavor
 * @radio_step: radio version step
 * @radio_dash: radio version dash
 */
struct iwl_radio_version_notif {
	__le32 radio_flavor;
	__le32 radio_step;
	__le32 radio_dash;
} __packed; /* RADIO_VERSION_NOTOFICATION_S_VER_1 */

enum iwl_card_state_flags {
	CARD_ENABLED		= 0x00,
	HW_CARD_DISABLED	= 0x01,
	SW_CARD_DISABLED	= 0x02,
	CT_KILL_CARD_DISABLED	= 0x04,
	HALT_CARD_DISABLED	= 0x08,
	CARD_DISABLED_MSK	= 0x0f,
	CARD_IS_RX_ON		= 0x10,
};

/**
 * enum iwl_error_recovery_flags - flags for error recovery cmd
 * @ERROR_RECOVERY_UPDATE_DB: update db from blob sent
 * @ERROR_RECOVERY_END_OF_RECOVERY: end of recovery
 */
enum iwl_error_recovery_flags {
	ERROR_RECOVERY_UPDATE_DB = BIT(0),
	ERROR_RECOVERY_END_OF_RECOVERY = BIT(1),
};

/**
 * struct iwl_fw_error_recovery_cmd - recovery cmd sent upon assert
 * @flags: &enum iwl_error_recovery_flags
 * @buf_size: db buffer size in bytes
 */
struct iwl_fw_error_recovery_cmd {
	__le32 flags;
	__le32 buf_size;
} __packed; /* ERROR_RECOVERY_CMD_HDR_API_S_VER_1 */

#endif /* __iwl_fw_api_alive_h__ */
