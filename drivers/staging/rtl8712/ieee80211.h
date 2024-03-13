/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __IEEE80211_H
#define __IEEE80211_H

#include <linux/ieee80211.h>

#define IEEE_CMD_SET_WPA_PARAM			1
#define IEEE_CMD_SET_WPA_IE			2
#define IEEE_CMD_SET_ENCRYPTION			3
#define IEEE_CMD_MLME				4

#define IEEE_PARAM_WPA_ENABLED			1
#define IEEE_PARAM_TKIP_COUNTERMEASURES		2
#define IEEE_PARAM_DROP_UNENCRYPTED		3
#define IEEE_PARAM_PRIVACY_INVOKED		4
#define IEEE_PARAM_AUTH_ALGS			5
#define IEEE_PARAM_IEEE_802_1X			6
#define IEEE_PARAM_WPAX_SELECT			7

#define AUTH_ALG_OPEN_SYSTEM			0x1
#define AUTH_ALG_SHARED_KEY			0x2
#define AUTH_ALG_LEAP				0x00000004

#define IEEE_MLME_STA_DEAUTH			1
#define IEEE_MLME_STA_DISASSOC			2

#define IEEE_CRYPT_ERR_UNKNOWN_ALG		2
#define IEEE_CRYPT_ERR_UNKNOWN_ADDR		3
#define IEEE_CRYPT_ERR_CRYPT_INIT_FAILED	4
#define IEEE_CRYPT_ERR_KEY_SET_FAILED		5
#define IEEE_CRYPT_ERR_TX_KEY_SET_FAILED	6
#define IEEE_CRYPT_ERR_CARD_CONF_FAILED		7

#define	IEEE_CRYPT_ALG_NAME_LEN			16

#define WPA_CIPHER_NONE				BIT(0)
#define WPA_CIPHER_WEP40			BIT(1)
#define WPA_CIPHER_WEP104			BIT(2)
#define WPA_CIPHER_TKIP				BIT(3)
#define WPA_CIPHER_CCMP				BIT(4)

#define WPA_SELECTOR_LEN			4
#define RSN_HEADER_LEN				4

#define RSN_SELECTOR_LEN 4

enum NETWORK_TYPE {
	WIRELESS_INVALID	= 0,
	WIRELESS_11B		= 1,
	WIRELESS_11G		= 2,
	WIRELESS_11BG		= (WIRELESS_11B | WIRELESS_11G),
	WIRELESS_11A		= 4,
	WIRELESS_11N		= 8,
	WIRELESS_11GN		= (WIRELESS_11G | WIRELESS_11N),
	WIRELESS_11BGN		= (WIRELESS_11B | WIRELESS_11G | WIRELESS_11N),
};

struct ieee_param {
	u32 cmd;
	u8 sta_addr[ETH_ALEN];
	union {
		struct {
			u8 name;
			u32 value;
		} wpa_param;
		struct {
			u32 len;
			u8 reserved[32];
			u8 data[];
		} wpa_ie;
		struct {
			int command;
			int reason_code;
		} mlme;
		struct {
			u8 alg[IEEE_CRYPT_ALG_NAME_LEN];
			u8 set_tx;
			u32 err;
			u8 idx;
			u8 seq[8]; /* sequence counter (set: RX, get: TX) */
			u16 key_len;
			u8 key[];
		} crypt;
	} u;
};

#define MIN_FRAG_THRESHOLD     256U
#define	MAX_FRAG_THRESHOLD     2346U

/* QoS,QOS */
#define NORMAL_ACK			0

/* IEEE 802.11 defines */

#define P80211_OUI_LEN 3

struct ieee80211_snap_hdr {
	u8    dsap;   /* always 0xAA */
	u8    ssap;   /* always 0xAA */
	u8    ctrl;   /* always 0x03 */
	u8    oui[P80211_OUI_LEN];    /* organizational universal id */
} __packed;

#define SNAP_SIZE sizeof(struct ieee80211_snap_hdr)

#define IEEE80211_CCK_RATE_LEN			4
#define IEEE80211_NUM_OFDM_RATESLEN	8

#define IEEE80211_CCK_RATE_1MB		        0x02
#define IEEE80211_CCK_RATE_2MB		        0x04
#define IEEE80211_CCK_RATE_5MB		        0x0B
#define IEEE80211_CCK_RATE_11MB		        0x16
#define IEEE80211_OFDM_RATE_6MB		        0x0C
#define IEEE80211_OFDM_RATE_9MB		        0x12
#define IEEE80211_OFDM_RATE_12MB		0x18
#define IEEE80211_OFDM_RATE_18MB		0x24
#define IEEE80211_OFDM_RATE_24MB		0x30
#define IEEE80211_OFDM_RATE_36MB		0x48
#define IEEE80211_OFDM_RATE_48MB		0x60
#define IEEE80211_OFDM_RATE_54MB		0x6C
#define IEEE80211_BASIC_RATE_MASK		0x80

#define WEP_KEYS 4

/* MAX_RATES_LENGTH needs to be 12.  The spec says 8, and many APs
 * only use 8, and then use extended rates for the remaining supported
 * rates.  Other APs, however, stick all of their supported rates on the
 * main rates information element...
 */
#define MAX_RATES_LENGTH                  ((u8)12)
#define MAX_WPA_IE_LEN 128

struct registry_priv;

u8 *r8712_set_ie(u8 *pbuf, sint index, uint len, u8 *source, uint *frlen);
u8 *r8712_get_ie(u8 *pbuf, sint index, uint *len, sint limit);
unsigned char *r8712_get_wpa_ie(unsigned char *pie, uint *rsn_ie_len,
				int limit);
unsigned char *r8712_get_wpa2_ie(unsigned char *pie, uint *rsn_ie_len,
				 int limit);
int r8712_parse_wpa_ie(u8 *wpa_ie, int wpa_ie_len, int *group_cipher,
		       int *pairwise_cipher);
int r8712_parse_wpa2_ie(u8 *wpa_ie, int wpa_ie_len, int *group_cipher,
			int *pairwise_cipher);
int r8712_get_sec_ie(u8 *in_ie, uint in_len, u8 *rsn_ie, u16 *rsn_len,
		     u8 *wpa_ie, u16 *wpa_len);
int r8712_get_wps_ie(u8 *in_ie, uint in_len, u8 *wps_ie, uint *wps_ielen);
int r8712_generate_ie(struct registry_priv *pregistrypriv);
uint r8712_is_cckrates_included(u8 *rate);
uint r8712_is_cckratesonly_included(u8 *rate);

#endif /* IEEE80211_H */

