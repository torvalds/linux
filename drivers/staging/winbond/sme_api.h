/*
 * sme_api.h
 *
 * Copyright(C) 2002 Winbond Electronics Corp.
 */

#ifndef __SME_API_H__
#define __SME_API_H__

#include <linux/types.h>

#include "localpara.h"

/****************** CONSTANT AND MACRO SECTION ******************************/

#define MEDIA_STATE_DISCONNECTED	0
#define MEDIA_STATE_CONNECTED		1

/* ARRAY CHECK */
#define MAX_POWER_TO_DB			32

/****************** TYPE DEFINITION SECTION *********************************/

/****************** EXPORTED FUNCTION DECLARATION SECTION *******************/

/* OID_802_11_BSSID */
s8 sme_get_bssid(void *pcore_data, u8 *pbssid);
s8 sme_get_desired_bssid(void *pcore_data, u8 *pbssid); /* Unused */
s8 sme_set_desired_bssid(void *pcore_data, u8 *pbssid);

/* OID_802_11_SSID */
s8 sme_get_ssid(void *pcore_data, u8 *pssid, u8 *pssid_len);
s8 sme_get_desired_ssid(void *pcore_data, u8 *pssid, u8 *pssid_len);/* Unused */
s8 sme_set_desired_ssid(void *pcore_data, u8 *pssid, u8 ssid_len);

/* OID_802_11_INFRASTRUCTURE_MODE */
s8 sme_get_bss_type(void *pcore_data, u8 *pbss_type);
s8 sme_get_desired_bss_type(void *pcore_data, u8 *pbss_type); /* Unused */
s8 sme_set_desired_bss_type(void *pcore_data, u8 bss_type);

/* OID_802_11_FRAGMENTATION_THRESHOLD */
s8 sme_get_fragment_threshold(void *pcore_data, u32 *pthreshold);
s8 sme_set_fragment_threshold(void *pcore_data, u32 threshold);

/* OID_802_11_RTS_THRESHOLD */
s8 sme_get_rts_threshold(void *pcore_data, u32 *pthreshold);
s8 sme_set_rts_threshold(void *pcore_data, u32 threshold);

/* OID_802_11_CONFIGURATION */
s8 sme_get_beacon_period(void *pcore_data, u16 *pbeacon_period);
s8 sme_set_beacon_period(void *pcore_data, u16 beacon_period);

s8 sme_get_atim_window(void *pcore_data, u16 *patim_window);
s8 sme_set_atim_window(void *pcore_data, u16 atim_window);

s8 sme_get_current_channel(void *pcore_data, u8 *pcurrent_channel);
s8 sme_get_current_band(void *pcore_data, u8 *pcurrent_band);
s8 sme_set_current_channel(void *pcore_data, u8 current_channel);

/* OID_802_11_BSSID_LIST */
s8 sme_get_scan_bss_count(void *pcore_data, u8 *pcount);
s8 sme_get_scan_bss(void *pcore_data, u8 index, void **ppbss);

s8 sme_get_connected_bss(void *pcore_data, void **ppbss_now);

/* OID_802_11_AUTHENTICATION_MODE */
s8 sme_get_auth_mode(void *pcore_data, u8 *pauth_mode);
s8 sme_set_auth_mode(void *pcore_data, u8 auth_mode);

/* OID_802_11_WEP_STATUS / OID_802_11_ENCRYPTION_STATUS */
s8 sme_get_wep_mode(void *pcore_data, u8 *pwep_mode);
s8 sme_set_wep_mode(void *pcore_data, u8 wep_mode);

/* OID_GEN_VENDOR_ID */
/* OID_802_3_PERMANENT_ADDRESS */
s8 sme_get_permanent_mac_addr(void *pcore_data, u8 *pmac_addr);

/* OID_802_3_CURRENT_ADDRESS */
s8 sme_get_current_mac_addr(void *pcore_data, u8 *pmac_addr);

/* OID_802_11_NETWORK_TYPE_IN_USE */
s8 sme_get_network_type_in_use(void *pcore_data, u8 *ptype);
s8 sme_set_network_type_in_use(void *pcore_data, u8 type);

/* OID_802_11_SUPPORTED_RATES */
s8 sme_get_supported_rate(void *pcore_data, u8 *prates);

/* OID_802_11_ADD_WEP */
s8 sme_set_add_wep(void *pcore_data, u32 key_index, u32 key_len,
					 u8 *Address, u8 *key);

/* OID_802_11_REMOVE_WEP */
s8 sme_set_remove_wep(void *pcre_data, u32 key_index);

/* OID_802_11_DISASSOCIATE */
s8 sme_set_disassociate(void *pcore_data);

/* OID_802_11_POWER_MODE */
s8 sme_get_power_mode(void *pcore_data, u8 *pmode);
s8 sme_set_power_mode(void *pcore_data, u8 mode);

/* OID_802_11_BSSID_LIST_SCAN */
s8 sme_set_bssid_list_scan(void *pcore_data, void *pscan_para);

/* OID_802_11_RELOAD_DEFAULTS */
s8 sme_set_reload_defaults(void *pcore_data, u8 reload_type);


/*------------------------- non-standard ----------------------------------*/
s8 sme_get_connect_status(void *pcore_data, u8 *pstatus);
/*--------------------------------------------------------------------------*/

void sme_get_encryption_status(void *pcore_data, u8 *EncryptStatus);
void sme_set_encryption_status(void *pcore_data, u8 EncryptStatus);
s8 sme_add_key(void	*pcore_data,
		u32	key_index,
		u8	key_len,
		u8	key_type,
		u8	*key_bssid,
		u8	*ptx_tsc,
		u8	*prx_tsc,
		u8	*key_material);
void sme_remove_default_key(void *pcore_data, int index);
void sme_remove_mapping_key(void *pcore_data, u8 *pmac_addr);
void sme_clear_all_mapping_key(void *pcore_data);
void sme_clear_all_default_key(void *pcore_data);



s8 sme_set_preamble_mode(void *pcore_data, u8 mode);
s8 sme_get_preamble_mode(void *pcore_data, u8 *mode);
s8 sme_get_preamble_type(void *pcore_data, u8 *type);
s8 sme_set_slottime_mode(void *pcore_data, u8 mode);
s8 sme_get_slottime_mode(void *pcore_data, u8 *mode);
s8 sme_get_slottime_type(void *pcore_data, u8 *type);
s8 sme_set_txrate_policy(void *pcore_data, u8 policy);
s8 sme_get_txrate_policy(void *pcore_data, u8 *policy);
s8 sme_get_cwmin_value(void *pcore_data, u8 *cwmin);
s8 sme_get_cwmax_value(void *pcore_data, u16 *cwmax);
s8 sme_get_ms_radio_mode(void *pcore_data, u8 *pMsRadioOff);
s8 sme_set_ms_radio_mode(void *pcore_data, u8 boMsRadioOff);

void sme_get_tx_power_level(void *pcore_data, u32 *TxPower);
u8 sme_set_tx_power_level(void *pcore_data, u32 TxPower);
void sme_get_antenna_count(void *pcore_data, u32 *AntennaCount);
void sme_get_rx_antenna(void *pcore_data, u32 *RxAntenna);
u8 sme_set_rx_antenna(void *pcore_data, u32 RxAntenna);
void sme_get_tx_antenna(void *pcore_data, u32 *TxAntenna);
s8 sme_set_tx_antenna(void *pcore_data, u32 TxAntenna);
s8 sme_set_IBSS_chan(void *pcore_data, struct chan_info chan);
s8 sme_set_IE_append(void *pcore_data, u8 *buffer, u16 buf_len);

/* ================== Local functions ====================== */
static const u32 PowerDbToMw[] = {
	56,	/* mW, MAX - 0,	17.5 dbm */
	40,	/* mW, MAX - 1,	16.0 dbm */
	30,	/* mW, MAX - 2,	14.8 dbm */
	20,	/* mW, MAX - 3,	13.0 dbm */
	15,	/* mW, MAX - 4,	11.8 dbm */
	12,	/* mW, MAX - 5,	10.6 dbm */
	9,	/* mW, MAX - 6,	 9.4 dbm */
	7,	/* mW, MAX - 7,	 8.3 dbm */
	5,	/* mW, MAX - 8,	 6.4 dbm */
	4,	/* mW, MAX - 9,	 5.3 dbm */
	3,	/* mW, MAX - 10,  4.0 dbm */
	2,	/* mW, MAX - 11,  ? dbm */
	2,	/* mW, MAX - 12,  ? dbm */
	2,	/* mW, MAX - 13,  ? dbm */
	2,	/* mW, MAX - 14,  ? dbm */
	2,	/* mW, MAX - 15,  ? dbm */
	2,	/* mW, MAX - 16,  ? dbm */
	2,	/* mW, MAX - 17,  ? dbm */
	2,	/* mW, MAX - 18,  ? dbm */
	1,	/* mW, MAX - 19,  ? dbm */
	1,	/* mW, MAX - 20,  ? dbm */
	1,	/* mW, MAX - 21,  ? dbm */
	1,	/* mW, MAX - 22,  ? dbm */
	1,	/* mW, MAX - 23,  ? dbm */
	1,	/* mW, MAX - 24,  ? dbm */
	1,	/* mW, MAX - 25,  ? dbm */
	1,	/* mW, MAX - 26,  ? dbm */
	1,	/* mW, MAX - 27,  ? dbm */
	1,	/* mW, MAX - 28,  ? dbm */
	1,	/* mW, MAX - 29,  ? dbm */
	1,	/* mW, MAX - 30,  ? dbm */
	1	/* mW, MAX - 31,  ? dbm */
};

#endif /* __SME_API_H__ */


