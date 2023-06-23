// SPDX-License-Identifier: GPL-2.0-only

#include "wcn36xx.h"
#include "firmware.h"

#define DEFINE(s)[s] = #s

static const char * const wcn36xx_firmware_caps_names[] = {
	DEFINE(MCC),
	DEFINE(P2P),
	DEFINE(DOT11AC),
	DEFINE(SLM_SESSIONIZATION),
	DEFINE(DOT11AC_OPMODE),
	DEFINE(SAP32STA),
	DEFINE(TDLS),
	DEFINE(P2P_GO_NOA_DECOUPLE_INIT_SCAN),
	DEFINE(WLANACTIVE_OFFLOAD),
	DEFINE(BEACON_OFFLOAD),
	DEFINE(SCAN_OFFLOAD),
	DEFINE(ROAM_OFFLOAD),
	DEFINE(BCN_MISS_OFFLOAD),
	DEFINE(STA_POWERSAVE),
	DEFINE(STA_ADVANCED_PWRSAVE),
	DEFINE(AP_UAPSD),
	DEFINE(AP_DFS),
	DEFINE(BLOCKACK),
	DEFINE(PHY_ERR),
	DEFINE(BCN_FILTER),
	DEFINE(RTT),
	DEFINE(RATECTRL),
	DEFINE(WOW),
	DEFINE(WLAN_ROAM_SCAN_OFFLOAD),
	DEFINE(SPECULATIVE_PS_POLL),
	DEFINE(SCAN_SCH),
	DEFINE(IBSS_HEARTBEAT_OFFLOAD),
	DEFINE(WLAN_SCAN_OFFLOAD),
	DEFINE(WLAN_PERIODIC_TX_PTRN),
	DEFINE(ADVANCE_TDLS),
	DEFINE(BATCH_SCAN),
	DEFINE(FW_IN_TX_PATH),
	DEFINE(EXTENDED_NSOFFLOAD_SLOT),
	DEFINE(CH_SWITCH_V1),
	DEFINE(HT40_OBSS_SCAN),
	DEFINE(UPDATE_CHANNEL_LIST),
	DEFINE(WLAN_MCADDR_FLT),
	DEFINE(WLAN_CH144),
	DEFINE(NAN),
	DEFINE(TDLS_SCAN_COEXISTENCE),
	DEFINE(LINK_LAYER_STATS_MEAS),
	DEFINE(MU_MIMO),
	DEFINE(EXTENDED_SCAN),
	DEFINE(DYNAMIC_WMM_PS),
	DEFINE(MAC_SPOOFED_SCAN),
	DEFINE(BMU_ERROR_GENERIC_RECOVERY),
	DEFINE(DISA),
	DEFINE(FW_STATS),
	DEFINE(WPS_PRBRSP_TMPL),
	DEFINE(BCN_IE_FLT_DELTA),
	DEFINE(TDLS_OFF_CHANNEL),
	DEFINE(RTT3),
	DEFINE(MGMT_FRAME_LOGGING),
	DEFINE(ENHANCED_TXBD_COMPLETION),
	DEFINE(LOGGING_ENHANCEMENT),
	DEFINE(EXT_SCAN_ENHANCED),
	DEFINE(MEMORY_DUMP_SUPPORTED),
	DEFINE(PER_PKT_STATS_SUPPORTED),
	DEFINE(EXT_LL_STAT),
	DEFINE(WIFI_CONFIG),
	DEFINE(ANTENNA_DIVERSITY_SELECTION),
};

#undef DEFINE

const char *wcn36xx_firmware_get_cap_name(enum wcn36xx_firmware_feat_caps x)
{
	if (x >= ARRAY_SIZE(wcn36xx_firmware_caps_names))
		return "UNKNOWN";
	return wcn36xx_firmware_caps_names[x];
}

void wcn36xx_firmware_set_feat_caps(u32 *bitmap,
				    enum wcn36xx_firmware_feat_caps cap)
{
	int arr_idx, bit_idx;

	if (cap < 0 || cap > 127) {
		wcn36xx_warn("error cap idx %d\n", cap);
		return;
	}

	arr_idx = cap / 32;
	bit_idx = cap % 32;
	bitmap[arr_idx] |= (1 << bit_idx);
}

int wcn36xx_firmware_get_feat_caps(u32 *bitmap,
				   enum wcn36xx_firmware_feat_caps cap)
{
	int arr_idx, bit_idx;

	if (cap < 0 || cap > 127) {
		wcn36xx_warn("error cap idx %d\n", cap);
		return -EINVAL;
	}

	arr_idx = cap / 32;
	bit_idx = cap % 32;

	return (bitmap[arr_idx] & (1 << bit_idx)) ? 1 : 0;
}

void wcn36xx_firmware_clear_feat_caps(u32 *bitmap,
				      enum wcn36xx_firmware_feat_caps cap)
{
	int arr_idx, bit_idx;

	if (cap < 0 || cap > 127) {
		wcn36xx_warn("error cap idx %d\n", cap);
		return;
	}

	arr_idx = cap / 32;
	bit_idx = cap % 32;
	bitmap[arr_idx] &= ~(1 << bit_idx);
}
