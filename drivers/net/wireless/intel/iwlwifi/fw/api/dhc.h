/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2025 Intel Corporation
 */
#ifndef __iwl_fw_api_dhc_h__
#define __iwl_fw_api_dhc_h__

#define DHC_TABLE_MASK_POS (28)

/**
 * enum iwl_dhc_table_id - DHC table operations index
 */
enum iwl_dhc_table_id {
	/**
	 * @DHC_TABLE_INTEGRATION: select the integration table
	 */
	DHC_TABLE_INTEGRATION	= 2 << DHC_TABLE_MASK_POS,
	/**
	 * @DHC_TABLE_TOOLS: select the tools table
	 */
	DHC_TABLE_TOOLS		= 0,
};

/**
 * enum iwl_dhc_umac_tools_table - tools operations
 * @DHC_TOOLS_UMAC_GET_TAS_STATUS: Get TAS status.
 *	See @struct iwl_dhc_tas_status_resp
 */
enum iwl_dhc_umac_tools_table {
	DHC_TOOLS_UMAC_GET_TAS_STATUS = 0,
};

/**
 * enum iwl_dhc_umac_integration_table - integration operations
 */
enum iwl_dhc_umac_integration_table {
	/**
	 * @DHC_INT_UMAC_TWT_OPERATION: trigger a TWT operation
	 */
	DHC_INT_UMAC_TWT_OPERATION = 4,
	/**
	 * @DHC_INTEGRATION_TLC_DEBUG_CONFIG: TLC debug
	 */
	DHC_INTEGRATION_TLC_DEBUG_CONFIG = 1,
	/**
	 * @DHC_INTEGRATION_MAX: Maximum UMAC integration table entries
	 */
	DHC_INTEGRATION_MAX
};

#define DHC_TARGET_UMAC BIT(27)

/**
 * struct iwl_dhc_cmd - debug host command
 * @length: length in DWs of the data structure that is concatenated to the end
 *	of this struct
 * @index_and_mask: bit 31 is 1 for data set operation else it's 0
 *	bits 28-30 is the index of the table of the operation -
 *	&enum iwl_dhc_table_id *
 *	bit 27 is 0 if the cmd targeted to LMAC and 1 if targeted to UMAC,
 *	(LMAC is 0 for backward compatibility)
 *	bit 26 is 0 if the cmd targeted to LMAC0 and 1 if targeted to LMAC1,
 *	relevant only if bit 27 set to 0
 *	bits 0-25 is a specific entry index in the table specified in bits 28-30
 *
 * @data: the concatenated data.
 */
struct iwl_dhc_cmd {
	__le32 length;
	__le32 index_and_mask;
	__le32 data[];
} __packed; /* DHC_CMD_API_S */

/**
 * struct iwl_dhc_payload_hdr - DHC payload header
 * @version: a version of a payload
 * @reserved: reserved for alignment
 */
struct iwl_dhc_payload_hdr {
	u8 version;
	u8 reserved[3];
} __packed; /* DHC_PAYLOAD_HDR_API_S_VER_1 */

/**
 * struct iwl_dhc_tas_status_per_radio - TAS status per radio
 * @band: &PHY_BAND_5 for high band, PHY_BAND_24 for low band and
 *	&PHY_BAND_6 for ultra high band.
 * @static_status: TAS statically enabled or disabled
 * @static_disable_reason: TAS static disable reason, uses
 *	&enum iwl_tas_statically_disabled_reason
 * @near_disconnection: is TAS currently near disconnection per radio
 * @dynamic_status_ant_a: Antenna A current TAS status.
 *	uses &enum iwl_tas_dyna_status
 * @dynamic_status_ant_b: Antenna B current TAS status.
 *	uses &enum iwl_tas_dyna_status
 * @max_reg_pwr_limit_ant_a: Antenna A regulatory power limits in dBm
 * @max_reg_pwr_limit_ant_b: Antenna B regulatory power limits in dBm
 * @sar_limit_ant_a: Antenna A SAR limit per radio in dBm
 * @sar_limit_ant_b: Antenna B SAR limit per radio in dBm
 * @reserved: reserved for alignment
 */
struct iwl_dhc_tas_status_per_radio {
	u8 band;
	u8 static_status;
	u8 static_disable_reason;
	u8 near_disconnection;
	u8 dynamic_status_ant_a;
	u8 dynamic_status_ant_b;
	__le16 max_reg_pwr_limit_ant_a;
	__le16 max_reg_pwr_limit_ant_b;
	__le16 sar_limit_ant_a;
	__le16 sar_limit_ant_b;
	u8 reserved[2];
} __packed; /* DHC_TAS_STATUS_PER_RADIO_S_VER_1 */

/**
 * struct iwl_dhc_tas_status_resp - Response to DHC_TOOLS_UMAC_GET_TAS_STATUS
 * @header: DHC payload header, uses &struct iwl_dhc_payload_hdr
 * @tas_config_info: see @struct bios_value_u32
 * @mcc_block_list: block listed country codes
 * @tas_status_radio: TAS status, uses &struct iwl_dhc_tas_status_per_radio
 * @curr_mcc: current mcc
 * @valid_radio_mask: represent entry in tas_status_per_radio is valid.
 * @reserved: reserved for alignment
 */
struct iwl_dhc_tas_status_resp {
	struct iwl_dhc_payload_hdr header;
	struct bios_value_u32 tas_config_info;
	__le16 mcc_block_list[IWL_WTAS_BLACK_LIST_MAX];
	struct iwl_dhc_tas_status_per_radio tas_status_radio[2];
	__le16 curr_mcc;
	u8 valid_radio_mask;
	u8 reserved;
} __packed; /* DHC_TAS_STATUS_RSP_API_S_VER_1 */

/**
 * struct iwl_dhc_cmd_resp_v1 - debug host command response
 * @status: status of the command
 * @data: the response data
 */
struct iwl_dhc_cmd_resp_v1 {
	__le32 status;
	__le32 data[];
} __packed; /* DHC_RESP_API_S_VER_1 */

/**
 * struct iwl_dhc_cmd_resp - debug host command response
 * @status: status of the command
 * @descriptor: command descriptor (index_and_mask) returned
 * @data: the response data
 */
struct iwl_dhc_cmd_resp {
	__le32 status;
	__le32 descriptor;
	__le32 data[];
} __packed; /* DHC_RESP_API_S_VER_2 and DHC_RESP_API_S_VER_3 */

/**
 * enum iwl_dhc_twt_operation_type - describes the TWT operation type
 *
 * @DHC_TWT_REQUEST: Send a Request TWT command
 * @DHC_TWT_SUGGEST: Send a Suggest TWT command
 * @DHC_TWT_DEMAND: Send a Demand TWT command
 * @DHC_TWT_GROUPING: Send a Grouping TWT command
 * @DHC_TWT_ACCEPT: Send a Accept TWT command
 * @DHC_TWT_ALTERNATE: Send a Alternate TWT command
 * @DHC_TWT_DICTATE: Send a Dictate TWT command
 * @DHC_TWT_REJECT: Send a Reject TWT command
 * @DHC_TWT_TEARDOWN: Send a TearDown TWT command
 */
enum iwl_dhc_twt_operation_type {
	DHC_TWT_REQUEST,
	DHC_TWT_SUGGEST,
	DHC_TWT_DEMAND,
	DHC_TWT_GROUPING,
	DHC_TWT_ACCEPT,
	DHC_TWT_ALTERNATE,
	DHC_TWT_DICTATE,
	DHC_TWT_REJECT,
	DHC_TWT_TEARDOWN,
}; /* DHC_TWT_OPERATION_TYPE_E */

/**
 * struct iwl_dhc_twt_operation - trigger a TWT operation
 *
 * @mac_id: the mac Id on which to trigger TWT operation
 * @twt_operation: see &enum iwl_dhc_twt_operation_type
 * @target_wake_time: when should we be on channel
 * @interval_exp: the exponent for the interval
 * @interval_mantissa: the mantissa for the interval
 * @min_wake_duration: the minimum duration for the wake period
 * @trigger: is the TWT triggered or not
 * @flow_type: is the TWT announced or not
 * @flow_id: the TWT flow identifier from 0 to 7
 * @protection: is the TWT protected
 * @ndo_paging_indicator: is ndo_paging_indicator set
 * @responder_pm_mode: is responder_pm_mode set
 * @negotiation_type: if the responder wants to doze outside the TWT SP
 * @twt_request: 1 for TWT request, 0 otherwise
 * @implicit: is TWT implicit
 * @twt_group_assignment: the TWT group assignment
 * @twt_channel: the TWT channel
 * @reserved: reserved
 */
struct iwl_dhc_twt_operation {
	__le32 mac_id;
	__le32 twt_operation;
	__le64 target_wake_time;
	__le32 interval_exp;
	__le32 interval_mantissa;
	__le32 min_wake_duration;
	u8 trigger;
	u8 flow_type;
	u8 flow_id;
	u8 protection;
	u8 ndo_paging_indicator;
	u8 responder_pm_mode;
	u8 negotiation_type;
	u8 twt_request;
	u8 implicit;
	u8 twt_group_assignment;
	u8 twt_channel;
	u8 reserved;
}; /* DHC_TWT_OPERATION_API_S */

#endif /* __iwl_fw_api_dhc_h__ */
