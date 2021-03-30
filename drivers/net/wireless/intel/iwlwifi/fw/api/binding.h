/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014, 2020 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_binding_h__
#define __iwl_fw_api_binding_h__

#include <fw/file.h>
#include <fw/img.h>

#define MAX_MACS_IN_BINDING	(3)
#define MAX_BINDINGS		(4)

/**
 * struct iwl_binding_cmd_v1 - configuring bindings
 * ( BINDING_CONTEXT_CMD = 0x2b )
 * @id_and_color: ID and color of the relevant Binding,
 *	&enum iwl_ctxt_id_and_color
 * @action: action to perform, one of FW_CTXT_ACTION_*
 * @macs: array of MAC id and colors which belong to the binding,
 *	&enum iwl_ctxt_id_and_color
 * @phy: PHY id and color which belongs to the binding,
 *	&enum iwl_ctxt_id_and_color
 */
struct iwl_binding_cmd_v1 {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	__le32 id_and_color;
	__le32 action;
	/* BINDING_DATA_API_S_VER_1 */
	__le32 macs[MAX_MACS_IN_BINDING];
	__le32 phy;
} __packed; /* BINDING_CMD_API_S_VER_1 */

/**
 * struct iwl_binding_cmd - configuring bindings
 * ( BINDING_CONTEXT_CMD = 0x2b )
 * @id_and_color: ID and color of the relevant Binding,
 *	&enum iwl_ctxt_id_and_color
 * @action: action to perform, one of FW_CTXT_ACTION_*
 * @macs: array of MAC id and colors which belong to the binding
 *	&enum iwl_ctxt_id_and_color
 * @phy: PHY id and color which belongs to the binding
 *	&enum iwl_ctxt_id_and_color
 * @lmac_id: the lmac id the binding belongs to
 */
struct iwl_binding_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	__le32 id_and_color;
	__le32 action;
	/* BINDING_DATA_API_S_VER_1 */
	__le32 macs[MAX_MACS_IN_BINDING];
	__le32 phy;
	__le32 lmac_id;
} __packed; /* BINDING_CMD_API_S_VER_2 */

#define IWL_BINDING_CMD_SIZE_V1	sizeof(struct iwl_binding_cmd_v1)
#define IWL_LMAC_24G_INDEX		0
#define IWL_LMAC_5G_INDEX		1

static inline u32 iwl_mvm_get_lmac_id(const struct iwl_fw *fw,
				      enum nl80211_band band){
	if (!fw_has_capa(&fw->ucode_capa, IWL_UCODE_TLV_CAPA_CDB_SUPPORT) ||
	    band == NL80211_BAND_2GHZ)
		return IWL_LMAC_24G_INDEX;
	return IWL_LMAC_5G_INDEX;
}

/* The maximal number of fragments in the FW's schedule session */
#define IWL_MVM_MAX_QUOTA 128

/**
 * struct iwl_time_quota_data_v1 - configuration of time quota per binding
 * @id_and_color: ID and color of the relevant Binding,
 *	&enum iwl_ctxt_id_and_color
 * @quota: absolute time quota in TU. The scheduler will try to divide the
 *	remainig quota (after Time Events) according to this quota.
 * @max_duration: max uninterrupted context duration in TU
 */
struct iwl_time_quota_data_v1 {
	__le32 id_and_color;
	__le32 quota;
	__le32 max_duration;
} __packed; /* TIME_QUOTA_DATA_API_S_VER_1 */

/**
 * struct iwl_time_quota_cmd - configuration of time quota between bindings
 * ( TIME_QUOTA_CMD = 0x2c )
 * @quotas: allocations per binding
 * Note: on non-CDB the fourth one is the auxilary mac and is
 *	essentially zero.
 *	On CDB the fourth one is a regular binding.
 */
struct iwl_time_quota_cmd_v1 {
	struct iwl_time_quota_data_v1 quotas[MAX_BINDINGS];
} __packed; /* TIME_QUOTA_ALLOCATION_CMD_API_S_VER_1 */

enum iwl_quota_low_latency {
	IWL_QUOTA_LOW_LATENCY_NONE = 0,
	IWL_QUOTA_LOW_LATENCY_TX = BIT(0),
	IWL_QUOTA_LOW_LATENCY_RX = BIT(1),
	IWL_QUOTA_LOW_LATENCY_TX_RX =
		IWL_QUOTA_LOW_LATENCY_TX | IWL_QUOTA_LOW_LATENCY_RX,
};

/**
 * struct iwl_time_quota_data - configuration of time quota per binding
 * @id_and_color: ID and color of the relevant Binding.
 * @quota: absolute time quota in TU. The scheduler will try to divide the
 *	remainig quota (after Time Events) according to this quota.
 * @max_duration: max uninterrupted context duration in TU
 * @low_latency: low latency status, &enum iwl_quota_low_latency
 */
struct iwl_time_quota_data {
	__le32 id_and_color;
	__le32 quota;
	__le32 max_duration;
	__le32 low_latency;
} __packed; /* TIME_QUOTA_DATA_API_S_VER_2 */

/**
 * struct iwl_time_quota_cmd - configuration of time quota between bindings
 * ( TIME_QUOTA_CMD = 0x2c )
 * Note: on non-CDB the fourth one is the auxilary mac and is essentially zero.
 * On CDB the fourth one is a regular binding.
 *
 * @quotas: allocations per binding
 */
struct iwl_time_quota_cmd {
	struct iwl_time_quota_data quotas[MAX_BINDINGS];
} __packed; /* TIME_QUOTA_ALLOCATION_CMD_API_S_VER_2 */

#endif /* __iwl_fw_api_binding_h__ */
