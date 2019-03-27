/*
 * WPA Supplicant - Common definitions
 * Copyright (c) 2004-2018, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef DEFS_H
#define DEFS_H

#ifdef FALSE
#undef FALSE
#endif
#ifdef TRUE
#undef TRUE
#endif
typedef enum { FALSE = 0, TRUE = 1 } Boolean;


#define WPA_CIPHER_NONE BIT(0)
#define WPA_CIPHER_WEP40 BIT(1)
#define WPA_CIPHER_WEP104 BIT(2)
#define WPA_CIPHER_TKIP BIT(3)
#define WPA_CIPHER_CCMP BIT(4)
#define WPA_CIPHER_AES_128_CMAC BIT(5)
#define WPA_CIPHER_GCMP BIT(6)
#define WPA_CIPHER_SMS4 BIT(7)
#define WPA_CIPHER_GCMP_256 BIT(8)
#define WPA_CIPHER_CCMP_256 BIT(9)
#define WPA_CIPHER_BIP_GMAC_128 BIT(11)
#define WPA_CIPHER_BIP_GMAC_256 BIT(12)
#define WPA_CIPHER_BIP_CMAC_256 BIT(13)
#define WPA_CIPHER_GTK_NOT_USED BIT(14)

#define WPA_KEY_MGMT_IEEE8021X BIT(0)
#define WPA_KEY_MGMT_PSK BIT(1)
#define WPA_KEY_MGMT_NONE BIT(2)
#define WPA_KEY_MGMT_IEEE8021X_NO_WPA BIT(3)
#define WPA_KEY_MGMT_WPA_NONE BIT(4)
#define WPA_KEY_MGMT_FT_IEEE8021X BIT(5)
#define WPA_KEY_MGMT_FT_PSK BIT(6)
#define WPA_KEY_MGMT_IEEE8021X_SHA256 BIT(7)
#define WPA_KEY_MGMT_PSK_SHA256 BIT(8)
#define WPA_KEY_MGMT_WPS BIT(9)
#define WPA_KEY_MGMT_SAE BIT(10)
#define WPA_KEY_MGMT_FT_SAE BIT(11)
#define WPA_KEY_MGMT_WAPI_PSK BIT(12)
#define WPA_KEY_MGMT_WAPI_CERT BIT(13)
#define WPA_KEY_MGMT_CCKM BIT(14)
#define WPA_KEY_MGMT_OSEN BIT(15)
#define WPA_KEY_MGMT_IEEE8021X_SUITE_B BIT(16)
#define WPA_KEY_MGMT_IEEE8021X_SUITE_B_192 BIT(17)
#define WPA_KEY_MGMT_FILS_SHA256 BIT(18)
#define WPA_KEY_MGMT_FILS_SHA384 BIT(19)
#define WPA_KEY_MGMT_FT_FILS_SHA256 BIT(20)
#define WPA_KEY_MGMT_FT_FILS_SHA384 BIT(21)
#define WPA_KEY_MGMT_OWE BIT(22)
#define WPA_KEY_MGMT_DPP BIT(23)
#define WPA_KEY_MGMT_FT_IEEE8021X_SHA384 BIT(24)

static inline int wpa_key_mgmt_wpa_ieee8021x(int akm)
{
	return !!(akm & (WPA_KEY_MGMT_IEEE8021X |
			 WPA_KEY_MGMT_FT_IEEE8021X |
			 WPA_KEY_MGMT_FT_IEEE8021X_SHA384 |
			 WPA_KEY_MGMT_CCKM |
			 WPA_KEY_MGMT_OSEN |
			 WPA_KEY_MGMT_IEEE8021X_SHA256 |
			 WPA_KEY_MGMT_IEEE8021X_SUITE_B |
			 WPA_KEY_MGMT_IEEE8021X_SUITE_B_192 |
			 WPA_KEY_MGMT_FILS_SHA256 |
			 WPA_KEY_MGMT_FILS_SHA384 |
			 WPA_KEY_MGMT_FT_FILS_SHA256 |
			 WPA_KEY_MGMT_FT_FILS_SHA384));
}

static inline int wpa_key_mgmt_wpa_psk(int akm)
{
	return !!(akm & (WPA_KEY_MGMT_PSK |
			 WPA_KEY_MGMT_FT_PSK |
			 WPA_KEY_MGMT_PSK_SHA256 |
			 WPA_KEY_MGMT_SAE |
			 WPA_KEY_MGMT_FT_SAE));
}

static inline int wpa_key_mgmt_ft(int akm)
{
	return !!(akm & (WPA_KEY_MGMT_FT_PSK |
			 WPA_KEY_MGMT_FT_IEEE8021X |
			 WPA_KEY_MGMT_FT_IEEE8021X_SHA384 |
			 WPA_KEY_MGMT_FT_SAE |
			 WPA_KEY_MGMT_FT_FILS_SHA256 |
			 WPA_KEY_MGMT_FT_FILS_SHA384));
}

static inline int wpa_key_mgmt_ft_psk(int akm)
{
	return !!(akm & WPA_KEY_MGMT_FT_PSK);
}

static inline int wpa_key_mgmt_sae(int akm)
{
	return !!(akm & (WPA_KEY_MGMT_SAE |
			 WPA_KEY_MGMT_FT_SAE));
}

static inline int wpa_key_mgmt_fils(int akm)
{
	return !!(akm & (WPA_KEY_MGMT_FILS_SHA256 |
			 WPA_KEY_MGMT_FILS_SHA384 |
			 WPA_KEY_MGMT_FT_FILS_SHA256 |
			 WPA_KEY_MGMT_FT_FILS_SHA384));
}

static inline int wpa_key_mgmt_sha256(int akm)
{
	return !!(akm & (WPA_KEY_MGMT_PSK_SHA256 |
			 WPA_KEY_MGMT_IEEE8021X_SHA256 |
			 WPA_KEY_MGMT_SAE |
			 WPA_KEY_MGMT_FT_SAE |
			 WPA_KEY_MGMT_OSEN |
			 WPA_KEY_MGMT_IEEE8021X_SUITE_B |
			 WPA_KEY_MGMT_FILS_SHA256 |
			 WPA_KEY_MGMT_FT_FILS_SHA256));
}

static inline int wpa_key_mgmt_sha384(int akm)
{
	return !!(akm & (WPA_KEY_MGMT_IEEE8021X_SUITE_B_192 |
			 WPA_KEY_MGMT_FT_IEEE8021X_SHA384 |
			 WPA_KEY_MGMT_FILS_SHA384 |
			 WPA_KEY_MGMT_FT_FILS_SHA384));
}

static inline int wpa_key_mgmt_suite_b(int akm)
{
	return !!(akm & (WPA_KEY_MGMT_IEEE8021X_SUITE_B |
			 WPA_KEY_MGMT_IEEE8021X_SUITE_B_192));
}

static inline int wpa_key_mgmt_wpa(int akm)
{
	return wpa_key_mgmt_wpa_ieee8021x(akm) ||
		wpa_key_mgmt_wpa_psk(akm) ||
		wpa_key_mgmt_fils(akm) ||
		wpa_key_mgmt_sae(akm) ||
		akm == WPA_KEY_MGMT_OWE ||
		akm == WPA_KEY_MGMT_DPP;
}

static inline int wpa_key_mgmt_wpa_any(int akm)
{
	return wpa_key_mgmt_wpa(akm) || (akm & WPA_KEY_MGMT_WPA_NONE);
}

static inline int wpa_key_mgmt_cckm(int akm)
{
	return akm == WPA_KEY_MGMT_CCKM;
}


#define WPA_PROTO_WPA BIT(0)
#define WPA_PROTO_RSN BIT(1)
#define WPA_PROTO_WAPI BIT(2)
#define WPA_PROTO_OSEN BIT(3)

#define WPA_AUTH_ALG_OPEN BIT(0)
#define WPA_AUTH_ALG_SHARED BIT(1)
#define WPA_AUTH_ALG_LEAP BIT(2)
#define WPA_AUTH_ALG_FT BIT(3)
#define WPA_AUTH_ALG_SAE BIT(4)
#define WPA_AUTH_ALG_FILS BIT(5)
#define WPA_AUTH_ALG_FILS_SK_PFS BIT(6)

static inline int wpa_auth_alg_fils(int alg)
{
	return !!(alg & (WPA_AUTH_ALG_FILS | WPA_AUTH_ALG_FILS_SK_PFS));
}

enum wpa_alg {
	WPA_ALG_NONE,
	WPA_ALG_WEP,
	WPA_ALG_TKIP,
	WPA_ALG_CCMP,
	WPA_ALG_IGTK,
	WPA_ALG_PMK,
	WPA_ALG_GCMP,
	WPA_ALG_SMS4,
	WPA_ALG_KRK,
	WPA_ALG_GCMP_256,
	WPA_ALG_CCMP_256,
	WPA_ALG_BIP_GMAC_128,
	WPA_ALG_BIP_GMAC_256,
	WPA_ALG_BIP_CMAC_256
};

/**
 * enum wpa_states - wpa_supplicant state
 *
 * These enumeration values are used to indicate the current wpa_supplicant
 * state (wpa_s->wpa_state). The current state can be retrieved with
 * wpa_supplicant_get_state() function and the state can be changed by calling
 * wpa_supplicant_set_state(). In WPA state machine (wpa.c and preauth.c), the
 * wrapper functions wpa_sm_get_state() and wpa_sm_set_state() should be used
 * to access the state variable.
 */
enum wpa_states {
	/**
	 * WPA_DISCONNECTED - Disconnected state
	 *
	 * This state indicates that client is not associated, but is likely to
	 * start looking for an access point. This state is entered when a
	 * connection is lost.
	 */
	WPA_DISCONNECTED,

	/**
	 * WPA_INTERFACE_DISABLED - Interface disabled
	 *
	 * This state is entered if the network interface is disabled, e.g.,
	 * due to rfkill. wpa_supplicant refuses any new operations that would
	 * use the radio until the interface has been enabled.
	 */
	WPA_INTERFACE_DISABLED,

	/**
	 * WPA_INACTIVE - Inactive state (wpa_supplicant disabled)
	 *
	 * This state is entered if there are no enabled networks in the
	 * configuration. wpa_supplicant is not trying to associate with a new
	 * network and external interaction (e.g., ctrl_iface call to add or
	 * enable a network) is needed to start association.
	 */
	WPA_INACTIVE,

	/**
	 * WPA_SCANNING - Scanning for a network
	 *
	 * This state is entered when wpa_supplicant starts scanning for a
	 * network.
	 */
	WPA_SCANNING,

	/**
	 * WPA_AUTHENTICATING - Trying to authenticate with a BSS/SSID
	 *
	 * This state is entered when wpa_supplicant has found a suitable BSS
	 * to authenticate with and the driver is configured to try to
	 * authenticate with this BSS. This state is used only with drivers
	 * that use wpa_supplicant as the SME.
	 */
	WPA_AUTHENTICATING,

	/**
	 * WPA_ASSOCIATING - Trying to associate with a BSS/SSID
	 *
	 * This state is entered when wpa_supplicant has found a suitable BSS
	 * to associate with and the driver is configured to try to associate
	 * with this BSS in ap_scan=1 mode. When using ap_scan=2 mode, this
	 * state is entered when the driver is configured to try to associate
	 * with a network using the configured SSID and security policy.
	 */
	WPA_ASSOCIATING,

	/**
	 * WPA_ASSOCIATED - Association completed
	 *
	 * This state is entered when the driver reports that association has
	 * been successfully completed with an AP. If IEEE 802.1X is used
	 * (with or without WPA/WPA2), wpa_supplicant remains in this state
	 * until the IEEE 802.1X/EAPOL authentication has been completed.
	 */
	WPA_ASSOCIATED,

	/**
	 * WPA_4WAY_HANDSHAKE - WPA 4-Way Key Handshake in progress
	 *
	 * This state is entered when WPA/WPA2 4-Way Handshake is started. In
	 * case of WPA-PSK, this happens when receiving the first EAPOL-Key
	 * frame after association. In case of WPA-EAP, this state is entered
	 * when the IEEE 802.1X/EAPOL authentication has been completed.
	 */
	WPA_4WAY_HANDSHAKE,

	/**
	 * WPA_GROUP_HANDSHAKE - WPA Group Key Handshake in progress
	 *
	 * This state is entered when 4-Way Key Handshake has been completed
	 * (i.e., when the supplicant sends out message 4/4) and when Group
	 * Key rekeying is started by the AP (i.e., when supplicant receives
	 * message 1/2).
	 */
	WPA_GROUP_HANDSHAKE,

	/**
	 * WPA_COMPLETED - All authentication completed
	 *
	 * This state is entered when the full authentication process is
	 * completed. In case of WPA2, this happens when the 4-Way Handshake is
	 * successfully completed. With WPA, this state is entered after the
	 * Group Key Handshake; with IEEE 802.1X (non-WPA) connection is
	 * completed after dynamic keys are received (or if not used, after
	 * the EAP authentication has been completed). With static WEP keys and
	 * plaintext connections, this state is entered when an association
	 * has been completed.
	 *
	 * This state indicates that the supplicant has completed its
	 * processing for the association phase and that data connection is
	 * fully configured.
	 */
	WPA_COMPLETED
};

#define MLME_SETPROTECTION_PROTECT_TYPE_NONE 0
#define MLME_SETPROTECTION_PROTECT_TYPE_RX 1
#define MLME_SETPROTECTION_PROTECT_TYPE_TX 2
#define MLME_SETPROTECTION_PROTECT_TYPE_RX_TX 3

#define MLME_SETPROTECTION_KEY_TYPE_GROUP 0
#define MLME_SETPROTECTION_KEY_TYPE_PAIRWISE 1


/**
 * enum mfp_options - Management frame protection (IEEE 802.11w) options
 */
enum mfp_options {
	NO_MGMT_FRAME_PROTECTION = 0,
	MGMT_FRAME_PROTECTION_OPTIONAL = 1,
	MGMT_FRAME_PROTECTION_REQUIRED = 2,
};
#define MGMT_FRAME_PROTECTION_DEFAULT 3

/**
 * enum hostapd_hw_mode - Hardware mode
 */
enum hostapd_hw_mode {
	HOSTAPD_MODE_IEEE80211B,
	HOSTAPD_MODE_IEEE80211G,
	HOSTAPD_MODE_IEEE80211A,
	HOSTAPD_MODE_IEEE80211AD,
	HOSTAPD_MODE_IEEE80211ANY,
	NUM_HOSTAPD_MODES
};

/**
 * enum wpa_ctrl_req_type - Control interface request types
 */
enum wpa_ctrl_req_type {
	WPA_CTRL_REQ_UNKNOWN,
	WPA_CTRL_REQ_EAP_IDENTITY,
	WPA_CTRL_REQ_EAP_PASSWORD,
	WPA_CTRL_REQ_EAP_NEW_PASSWORD,
	WPA_CTRL_REQ_EAP_PIN,
	WPA_CTRL_REQ_EAP_OTP,
	WPA_CTRL_REQ_EAP_PASSPHRASE,
	WPA_CTRL_REQ_SIM,
	WPA_CTRL_REQ_PSK_PASSPHRASE,
	WPA_CTRL_REQ_EXT_CERT_CHECK,
	NUM_WPA_CTRL_REQS
};

/* Maximum number of EAP methods to store for EAP server user information */
#define EAP_MAX_METHODS 8

enum mesh_plink_state {
	PLINK_IDLE = 1,
	PLINK_OPN_SNT,
	PLINK_OPN_RCVD,
	PLINK_CNF_RCVD,
	PLINK_ESTAB,
	PLINK_HOLDING,
	PLINK_BLOCKED, /* not defined in the IEEE 802.11 standard */
};

enum set_band {
	WPA_SETBAND_AUTO,
	WPA_SETBAND_5G,
	WPA_SETBAND_2G
};

enum wpa_radio_work_band {
	BAND_2_4_GHZ = BIT(0),
	BAND_5_GHZ = BIT(1),
	BAND_60_GHZ = BIT(2),
};

enum beacon_rate_type {
	BEACON_RATE_LEGACY,
	BEACON_RATE_HT,
	BEACON_RATE_VHT
};

enum eap_proxy_sim_state {
	SIM_STATE_ERROR,
};

#define OCE_STA BIT(0)
#define OCE_STA_CFON BIT(1)
#define OCE_AP BIT(2)

#endif /* DEFS_H */
