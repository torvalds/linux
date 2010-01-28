/* Copyright (C) 2006, Red Hat, Inc. */

#ifndef _LBS_ASSOC_H_
#define _LBS_ASSOC_H_


#include "defs.h"
#include "host.h"


struct lbs_private;

/*
 * In theory, the IE is limited to the IE length, 255,
 * but in practice 64 bytes are enough.
 */
#define MAX_WPA_IE_LEN 64



struct lbs_802_11_security {
	u8 WPAenabled;
	u8 WPA2enabled;
	u8 wep_enabled;
	u8 auth_mode;
	u32 key_mgmt;
};

/** Current Basic Service Set State Structure */
struct current_bss_params {
	/** bssid */
	u8 bssid[ETH_ALEN];
	/** ssid */
	u8 ssid[IEEE80211_MAX_SSID_LEN + 1];
	u8 ssid_len;

	/** band */
	u8 band;
	/** channel is directly in priv->channel */
	/** zero-terminated array of supported data rates */
	u8 rates[MAX_RATES + 1];
};

/**
 *  @brief Structure used to store information for each beacon/probe response
 */
struct bss_descriptor {
	u8 bssid[ETH_ALEN];

	u8 ssid[IEEE80211_MAX_SSID_LEN + 1];
	u8 ssid_len;

	u16 capability;
	u32 rssi;
	u32 channel;
	u16 beaconperiod;
	__le16 atimwindow;

	/* IW_MODE_AUTO, IW_MODE_ADHOC, IW_MODE_INFRA */
	u8 mode;

	/* zero-terminated array of supported data rates */
	u8 rates[MAX_RATES + 1];

	unsigned long last_scanned;

	union ieee_phy_param_set phy;
	union ieee_ss_param_set ss;

	u8 wpa_ie[MAX_WPA_IE_LEN];
	size_t wpa_ie_len;
	u8 rsn_ie[MAX_WPA_IE_LEN];
	size_t rsn_ie_len;

	u8 mesh;

	struct list_head list;
};

/** Association request
 *
 * Encapsulates all the options that describe a specific assocation request
 * or configuration of the wireless card's radio, mode, and security settings.
 */
struct assoc_request {
#define ASSOC_FLAG_SSID			1
#define ASSOC_FLAG_CHANNEL		2
#define ASSOC_FLAG_BAND			3
#define ASSOC_FLAG_MODE			4
#define ASSOC_FLAG_BSSID		5
#define ASSOC_FLAG_WEP_KEYS		6
#define ASSOC_FLAG_WEP_TX_KEYIDX	7
#define ASSOC_FLAG_WPA_MCAST_KEY	8
#define ASSOC_FLAG_WPA_UCAST_KEY	9
#define ASSOC_FLAG_SECINFO		10
#define ASSOC_FLAG_WPA_IE		11
	unsigned long flags;

	u8 ssid[IEEE80211_MAX_SSID_LEN + 1];
	u8 ssid_len;
	u8 channel;
	u8 band;
	u8 mode;
	u8 bssid[ETH_ALEN] __attribute__ ((aligned (2)));

	/** WEP keys */
	struct enc_key wep_keys[4];
	u16 wep_tx_keyidx;

	/** WPA keys */
	struct enc_key wpa_mcast_key;
	struct enc_key wpa_unicast_key;

	struct lbs_802_11_security secinfo;

	/** WPA Information Elements*/
	u8 wpa_ie[MAX_WPA_IE_LEN];
	u8 wpa_ie_len;

	/* BSS to associate with for infrastructure of Ad-Hoc join */
	struct bss_descriptor bss;
};


extern u8 lbs_bg_rates[MAX_RATES];

void lbs_association_worker(struct work_struct *work);
struct assoc_request *lbs_get_association_request(struct lbs_private *priv);

int lbs_adhoc_stop(struct lbs_private *priv);

int lbs_cmd_80211_deauthenticate(struct lbs_private *priv,
				 u8 bssid[ETH_ALEN], u16 reason);

int lbs_cmd_802_11_rssi(struct lbs_private *priv,
				struct cmd_ds_command *cmd);
int lbs_ret_802_11_rssi(struct lbs_private *priv,
				struct cmd_ds_command *resp);

int lbs_cmd_bcn_ctrl(struct lbs_private *priv,
				struct cmd_ds_command *cmd,
				u16 cmd_action);
int lbs_ret_802_11_bcn_ctrl(struct lbs_private *priv,
					struct cmd_ds_command *resp);

int lbs_cmd_802_11_set_wep(struct lbs_private *priv, uint16_t cmd_action,
			   struct assoc_request *assoc);

int lbs_cmd_802_11_enable_rsn(struct lbs_private *priv, uint16_t cmd_action,
			      uint16_t *enable);

int lbs_cmd_802_11_key_material(struct lbs_private *priv, uint16_t cmd_action,
				struct assoc_request *assoc);

#endif /* _LBS_ASSOC_H */
