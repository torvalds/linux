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
