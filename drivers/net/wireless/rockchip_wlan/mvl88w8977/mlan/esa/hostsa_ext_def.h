/** @file hostsa_ext_def.h
 *
 *  @brief This file declares the generic data structures and APIs.
 *
 *  Copyright (C) 2014-2017, Marvell International Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: initial version
******************************************************/

#ifndef _HOSTSA_EXT_DEF_H_
#define _HOSTSA_EXT_DEF_H_

#include "common/IEEE_types.h"
/* ####################
     From mlan_decl.h
   #################### */

/** Re-define generic data types for MLAN/MOAL */
/** Signed char (1-byte) */
typedef signed char t_s8;
/** Unsigned char (1-byte) */
typedef unsigned char t_u8;
/** Signed short (2-bytes) */
typedef short t_s16;
/** Unsigned short (2-bytes) */
typedef unsigned short t_u16;
/** Signed long (4-bytes) */
typedef int t_s32;
/** Unsigned long (4-bytes) */
typedef unsigned int t_u32;
/** Signed long long 8-bytes) */
typedef long long t_s64;
/** Unsigned long long 8-bytes) */
typedef unsigned long long t_u64;
/** Void pointer (4-bytes) */
typedef void t_void;
/** Size type */
typedef t_u32 t_size;
/** Boolean type */
typedef t_u8 t_bool;

#ifdef MLAN_64BIT
/** Pointer type (64-bit) */
typedef t_u64 t_ptr;
/** Signed value (64-bit) */
typedef t_s64 t_sval;
#else
/** Pointer type (32-bit) */
typedef t_u32 t_ptr;
/** Signed value (32-bit) */
typedef t_s32 t_sval;
#endif

/** MLAN MNULL pointer */
#define MNULL                    (0)

/** MLAN TRUE */
#define MTRUE                    (1)
/** MLAN FALSE */
#define MFALSE                   (0)

/** MLAN MAC Address Length */
#define MLAN_MAC_ADDR_LENGTH     (6)

/** MLAN_MEM_DEF */
#define MLAN_MEM_DEF             (0)

/** MLAN BSS type */
typedef enum _mlan_bss_type {
	MLAN_BSS_TYPE_STA = 0,
	MLAN_BSS_TYPE_UAP = 1,
#ifdef WIFI_DIRECT_SUPPORT
	MLAN_BSS_TYPE_WIFIDIRECT = 2,
#endif
	MLAN_BSS_TYPE_NAN = 4,
	MLAN_BSS_TYPE_ANY = 0xff,
} mlan_bss_type;

/** MLAN BSS role */
typedef enum _mlan_bss_role {
	MLAN_BSS_ROLE_STA = 0,
	MLAN_BSS_ROLE_UAP = 1,
	MLAN_BSS_ROLE_ANY = 0xff,
} mlan_bss_role;

/** mlan_status */
typedef enum _mlan_status {
	MLAN_STATUS_FAILURE = 0xffffffff,
	MLAN_STATUS_SUCCESS = 0,
	MLAN_STATUS_PENDING,
	MLAN_STATUS_RESOURCE,
	MLAN_STATUS_COMPLETE,
} mlan_status;

/** mlan_buf_type */
typedef enum _mlan_buf_type {
	MLAN_BUF_TYPE_CMD = 1,
	MLAN_BUF_TYPE_DATA,
	MLAN_BUF_TYPE_EVENT,
	MLAN_BUF_TYPE_RAW_DATA,
	MLAN_BUF_TYPE_SPA_DATA,
} mlan_buf_type;

typedef MLAN_PACK_START struct {
#ifdef BIG_ENDIAN_SUPPORT
       /** Host tx power ctrl:
            0x0: use fw setting for TX power
            0x1: value specified in bit[6] and bit[5:0] are valid */
	t_u8 hostctl:1;
       /** Sign of the power specified in bit[5:0] */
	t_u8 sign:1;
       /** Power to be used for transmission(in dBm) */
	t_u8 abs_val:6;
#else
       /** Power to be used for transmission(in dBm) */
	t_u8 abs_val:6;
       /** Sign of the power specified in bit[5:0] */
	t_u8 sign:1;
       /** Host tx power ctrl:
            0x0: use fw setting for TX power
            0x1: value specified in bit[6] and bit[5:0] are valid */
	t_u8 hostctl:1;
#endif
} MLAN_PACK_END tx_power_t;

/* pkt_txctrl */
typedef MLAN_PACK_START struct _pkt_txctrl {
    /**Data rate in unit of 0.5Mbps */
	t_u16 data_rate;
	/*Channel number to transmit the frame */
	t_u8 channel;
    /** Bandwidth to transmit the frame*/
	t_u8 bw;
    /** Power to be used for transmission*/
	union {
		tx_power_t tp;
		t_u8 val;
	} tx_power;
    /** Retry time of tx transmission*/
	t_u8 retry_limit;
} MLAN_PACK_END pkt_txctrl, *ppkt_txctrl;

/** pkt_rxinfo */
typedef MLAN_PACK_START struct _pkt_rxinfo {
    /** Data rate of received paccket*/
	t_u16 data_rate;
    /** Channel on which packet was received*/
	t_u8 channel;
    /** Rx antenna*/
	t_u8 antenna;
    /** Rx Rssi*/
	t_u8 rssi;
} MLAN_PACK_END pkt_rxinfo, *ppkt_rxinfo;

/** mlan_buffer data structure */
typedef struct _mlan_buffer {
    /** Pointer to previous mlan_buffer */
	struct _mlan_buffer *pprev;
    /** Pointer to next mlan_buffer */
	struct _mlan_buffer *pnext;
    /** Status code from firmware/driver */
	t_u32 status_code;
    /** Flags for this buffer */
	t_u32 flags;
    /** BSS index number for multiple BSS support */
	t_u32 bss_index;
    /** Buffer descriptor, e.g. skb in Linux */
	t_void *pdesc;
    /** Pointer to buffer */
	t_u8 *pbuf;
    /** Offset to data */
	t_u32 data_offset;
    /** Data length */
	t_u32 data_len;
    /** Buffer type: data, cmd, event etc. */
	mlan_buf_type buf_type;

    /** Fields below are valid for data packet only */
    /** QoS priority */
	t_u32 priority;
    /** Time stamp when packet is received (seconds) */
	t_u32 in_ts_sec;
    /** Time stamp when packet is received (micro seconds) */
	t_u32 in_ts_usec;
    /** Time stamp when packet is processed (seconds) */
	t_u32 out_ts_sec;
    /** Time stamp when packet is processed (micro seconds) */
	t_u32 out_ts_usec;
    /** tx_seq_num */
	t_u32 tx_seq_num;

    /** Fields below are valid for MLAN module only */
    /** Pointer to parent mlan_buffer */
	struct _mlan_buffer *pparent;
    /** Use count for this buffer */
	t_u32 use_count;
	union {
		pkt_txctrl tx_info;
		pkt_rxinfo rx_info;
	} u;
} mlan_buffer, *pmlan_buffer;

/** Maximum data rates */
#define MAX_DATA_RATES          14
/** Maximum key length */
#define MLAN_MAX_KEY_LENGTH             32
/** Maximum data rates */
#define MAX_DATA_RATES          14
/** Maximum number of AC QOS queues available in the driver/firmware */
#define MAX_AC_QUEUES 4

/** MLAN Maximum SSID Length */
#define MLAN_MAX_SSID_LENGTH     (32)

/** Max Ie length */
#define MAX_IE_SIZE             256

/** Max channel */
#define MLAN_MAX_CHANNEL    165

#ifdef UAP_SUPPORT
/** Maximum packet forward control value */
#define MAX_PKT_FWD_CTRL 15
/** Maximum BEACON period */
#define MAX_BEACON_PERIOD 4000
/** Minimum BEACON period */
#define MIN_BEACON_PERIOD 50
/** Maximum DTIM period */
#define MAX_DTIM_PERIOD 100
/** Minimum DTIM period */
#define MIN_DTIM_PERIOD 1
/** Maximum TX Power Limit */
#define MAX_TX_POWER    20
/** Minimum TX Power Limit */
#define MIN_TX_POWER    0
/** MAX station count */
#define MAX_STA_COUNT   10
/** Maximum RTS threshold */
#define MAX_RTS_THRESHOLD   2347
/** Maximum fragmentation threshold */
#define MAX_FRAG_THRESHOLD 2346
/** Minimum fragmentation threshold */
#define MIN_FRAG_THRESHOLD 256
/** data rate 54 M */
#define DATA_RATE_54M   108
/** antenna A */
#define ANTENNA_MODE_A      0
/** antenna B */
#define ANTENNA_MODE_B      1
/** transmit antenna */
#define TX_ANTENNA          1
/** receive antenna */
#define RX_ANTENNA          0
/** Maximum stage out time */
#define MAX_STAGE_OUT_TIME  864000
/** Minimum stage out time */
#define MIN_STAGE_OUT_TIME  300
/** Maximum Retry Limit */
#define MAX_RETRY_LIMIT         14

/** Maximum group key timer in seconds */
#define MAX_GRP_TIMER           86400
/**Default ssid for micro AP*/
#define AP_DEFAULT_SSID                 "Marvell Micro AP"
/**Default pairwise key handshake retry times*/
#define PWS_HSK_RETRIES                 3
/**Default group key handshake retry times*/
#define GRP_HSK_RETRIES                 3
/**Default pairwise key handshake timeout*/
#define PWS_HSK_TIMEOUT                 100	//100 ms
/**Default group key handshake timeout*/
#define GRP_HSK_TIMEOUT                 100	//100 ms
/**Default Group key rekey time*/
#define GRP_REKEY_TIME                  86400	//86400 sec

/** Maximum value of 4 byte configuration */
#define MAX_VALID_DWORD         0x7FFFFFFF	/*  (1 << 31) - 1 */

/** Band config ACS mode */
#define BAND_CONFIG_ACS_MODE    0x40
/** Band config manual */
#define BAND_CONFIG_MANUAL      0x00

/** Maximum data rates */
#define MAX_DATA_RATES          14

/** auto data rate */
#define DATA_RATE_AUTO       0

/**filter mode: disable */
#define MAC_FILTER_MODE_DISABLE         0
/**filter mode: block mac address */
#define MAC_FILTER_MODE_ALLOW_MAC       1
/**filter mode: block mac address */
#define MAC_FILTER_MODE_BLOCK_MAC       2
/** Maximum mac filter num */
#define MAX_MAC_FILTER_NUM           16

/* Bitmap for protocol to use */
/** No security */
#define PROTOCOL_NO_SECURITY        0x01
/** Static WEP */
#define PROTOCOL_STATIC_WEP         0x02
/** WPA */
#define PROTOCOL_WPA                0x08
/** WPA2 */
#define PROTOCOL_WPA2               0x20
/** WP2 Mixed */
#define PROTOCOL_WPA2_MIXED         0x28
/** EAP */
#define PROTOCOL_EAP                0x40
/** WAPI */
#define PROTOCOL_WAPI               0x80

/** Key_mgmt_psk */
#define KEY_MGMT_NONE   0x04
/** Key_mgmt_none */
#define KEY_MGMT_PSK    0x02
/** Key_mgmt_eap  */
#define KEY_MGMT_EAP    0x01
/** Key_mgmt_psk_sha256 */
#define KEY_MGMT_PSK_SHA256     0x100

/** TKIP */
#define CIPHER_TKIP                 0x04
/** AES CCMP */
#define CIPHER_AES_CCMP             0x08

/** Valid cipher bitmap */
#define VALID_CIPHER_BITMAP         0x0c
/** 60 seconds */
#define MRVDRV_TIMER_60S                60000
/** 10 seconds */
#define MRVDRV_TIMER_10S                10000
/** 5 seconds */
#define MRVDRV_TIMER_5S                 5000
/** 1 second */
#define MRVDRV_TIMER_1S                 1000
/** DMA alignment */
#define DMA_ALIGNMENT            32
/** max size of TxPD */
#define MAX_TXPD_SIZE            32

typedef t_u8 mlan_802_11_mac_addr[MLAN_MAC_ADDR_LENGTH];

/** mlan_802_11_ssid data structure */
typedef struct _mlan_802_11_ssid {
    /** SSID Length */
	t_u32 ssid_len;
    /** SSID information field */
	t_u8 ssid[MLAN_MAX_SSID_LENGTH];
} mlan_802_11_ssid, *pmlan_802_11_ssid;

/** mac_filter data structure */
typedef struct _mac_filter {
    /** mac filter mode */
	t_u16 filter_mode;
    /** mac adress count */
	t_u16 mac_count;
    /** mac address list */
	mlan_802_11_mac_addr mac_list[MAX_MAC_FILTER_NUM];
} mac_filter;

/** wpa parameter */
typedef struct _wpa_param {
    /** Pairwise cipher WPA */
	t_u8 pairwise_cipher_wpa;
    /** Pairwise cipher WPA2 */
	t_u8 pairwise_cipher_wpa2;
    /** group cipher */
	t_u8 group_cipher;
    /** RSN replay protection */
	t_u8 rsn_protection;
    /** passphrase length */
	t_u32 length;
    /** passphrase */
	t_u8 passphrase[64];
    /**group key rekey time in seconds */
	t_u32 gk_rekey_time;
} wpa_param;

/** wep key */
typedef struct _wep_key {
    /** key index 0-3 */
	t_u8 key_index;
    /** is default */
	t_u8 is_default;
    /** length */
	t_u16 length;
    /** key data */
	t_u8 key[26];
} wep_key;

/** wep param */
typedef struct _wep_param {
    /** key 0 */
	wep_key key0;
    /** key 1 */
	wep_key key1;
    /** key 2 */
	wep_key key2;
    /** key 3 */
	wep_key key3;
} wep_param;

/** Data structure of WMM QoS information */
typedef struct _wmm_qos_info_t {
#ifdef BIG_ENDIAN_SUPPORT
    /** QoS UAPSD */
	t_u8 qos_uapsd:1;
    /** Reserved */
	t_u8 reserved:3;
    /** Parameter set count */
	t_u8 para_set_count:4;
#else
    /** Parameter set count */
	t_u8 para_set_count:4;
    /** Reserved */
	t_u8 reserved:3;
    /** QoS UAPSD */
	t_u8 qos_uapsd:1;
#endif				/* BIG_ENDIAN_SUPPORT */
} wmm_qos_info_t, *pwmm_qos_info_t;

/** Data structure of WMM ECW */
typedef struct _wmm_ecw_t {
#ifdef BIG_ENDIAN_SUPPORT
    /** Maximum Ecw */
	t_u8 ecw_max:4;
    /** Minimum Ecw */
	t_u8 ecw_min:4;
#else
    /** Minimum Ecw */
	t_u8 ecw_min:4;
    /** Maximum Ecw */
	t_u8 ecw_max:4;
#endif				/* BIG_ENDIAN_SUPPORT */
} wmm_ecw_t, *pwmm_ecw_t;

/** 5G band */
#define BAND_CONFIG_5G        0x01
/** 2.4 G band */
#define BAND_CONFIG_2G		  0x00
/** MAX BG channel */
#define MAX_BG_CHANNEL 14
/** mlan_bss_param
 * Note: For each entry you must enter an invalid value
 * in the MOAL function woal_set_sys_config_invalid_data().
 * Otherwise for a valid data an unwanted TLV will be
 * added to that command.
 */

/**ethernet II header len*/
#define ETHII_HEADER_LEN  (14)

/** The bit to indicate the key is for unicast */
#define MLAN_KEY_INDEX_UNICAST        0x40000000
/** The key index to indicate default key */
#define MLAN_KEY_INDEX_DEFAULT        0x000000ff
/** Maximum key length */
/* #define MLAN_MAX_KEY_LENGTH        32 */
/** Minimum passphrase length */
#define MLAN_MIN_PASSPHRASE_LENGTH    8
/** Maximum passphrase length */
#define MLAN_MAX_PASSPHRASE_LENGTH    63
/** PMK length */
#define MLAN_PMK_HEXSTR_LENGTH        64
/* A few details needed for WEP (Wireless Equivalent Privacy) */
/** 104 bits */
#define MAX_WEP_KEY_SIZE	13
/** 40 bits RC4 - WEP */
#define MIN_WEP_KEY_SIZE	5
/** packet number size */
#define PN_SIZE			16
/** max seq size of wpa/wpa2 key */
#define SEQ_MAX_SIZE        8

/** key flag for tx_seq */
#define KEY_FLAG_TX_SEQ_VALID	0x00000001
/** key flag for rx_seq */
#define KEY_FLAG_RX_SEQ_VALID	0x00000002
/** key flag for group key */
#define KEY_FLAG_GROUP_KEY      0x00000004
/** key flag for tx */
#define KEY_FLAG_SET_TX_KEY     0x00000008
/** key flag for mcast IGTK */
#define KEY_FLAG_AES_MCAST_IGTK 0x00000010
/** key flag for remove key */
#define KEY_FLAG_REMOVE_KEY     0x80000000
/** Type definition of mlan_ds_encrypt_key for MLAN_OID_SEC_CFG_ENCRYPT_KEY */
typedef struct _mlan_ds_encrypt_key {
    /** Key disabled, all other fields will be
     *  ignore when this flag set to MTRUE
     */
	t_u32 key_disable;
    /** key removed flag, when this flag is set
     *  to MTRUE, only key_index will be check
     */
	t_u32 key_remove;
    /** Key index, used as current tx key index
     *  when is_current_wep_key is set to MTRUE
     */
	t_u32 key_index;
    /** Current Tx key flag */
	t_u32 is_current_wep_key;
    /** Key length */
	t_u32 key_len;
    /** Key */
	t_u8 key_material[MLAN_MAX_KEY_LENGTH];
    /** mac address */
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH];
    /** wapi key flag */
	t_u32 is_wapi_key;
    /** Initial packet number */
	t_u8 pn[PN_SIZE];
    /** key flags */
	t_u32 key_flags;
} mlan_ds_encrypt_key, *pmlan_ds_encrypt_key;

/** mlan_deauth_param */
typedef struct _mlan_deauth_param {
    /** STA mac addr */
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH];
    /** deauth reason */
	t_u16 reason_code;
} mlan_deauth_param;

/** Enumeration for PSK */
enum _mlan_psk_type {
	MLAN_PSK_PASSPHRASE = 1,
	MLAN_PSK_PMK,
	MLAN_PSK_CLEAR,
	MLAN_PSK_QUERY,
};
/** Type definition of mlan_passphrase_t */
typedef struct _mlan_passphrase_t {
    /** Length of passphrase */
	t_u32 passphrase_len;
    /** Passphrase */
	t_u8 passphrase[MLAN_MAX_PASSPHRASE_LENGTH];
} mlan_passphrase_t;

/** Maximum PMK R0 NAME key length */
#define MLAN_MAX_PMKR0_NAME_LENGTH      16

/** Type defnition of mlan_pmk_t */
typedef struct _mlan_pmk_t {
    /** PMK */
	t_u8 pmk[MLAN_MAX_KEY_LENGTH];
	t_u8 pmk_r0[MLAN_MAX_KEY_LENGTH];
	t_u8 pmk_r0_name[MLAN_MAX_PMKR0_NAME_LENGTH];
} mlan_pmk_t;

/** Type definition of mlan_ds_passphrase for MLAN_OID_SEC_CFG_PASSPHRASE */
typedef struct _mlan_ds_passphrase {
    /** SSID may be used */
	mlan_802_11_ssid ssid;
    /** BSSID may be used */
	mlan_802_11_mac_addr bssid;
    /** Flag for passphrase or pmk used */
	t_u16 psk_type;
    /** Passphrase or PMK */
	union {
	/** Passphrase */
		mlan_passphrase_t passphrase;
	/** PMK */
		mlan_pmk_t pmk;
	} psk;
} mlan_ds_passphrase, *pmlan_ds_passphrase;

/** mlan_ssid_bssid  data structure for
 *  MLAN_OID_BSS_START and MLAN_OID_BSS_FIND_BSS
 */
typedef struct _mlan_ssid_bssid {
    /** SSID */
	mlan_802_11_ssid ssid;
    /** BSSID */
	mlan_802_11_mac_addr bssid;
    /** index in BSSID list, start from 1 */
	t_u32 idx;
    /** Receive signal strength in dBm */
	t_s32 rssi;
    /**channel*/
	t_u16 channel;
    /**mobility domain value*/
	t_u16 ft_md;
    /**ft capability*/
	t_u8 ft_cap;
    /**band*/
	t_u16 bss_band;
	t_u32 channel_flags;
} mlan_ssid_bssid;

/** Channel List Entry */
typedef struct _channel_list {
    /** Channel Number */
	t_u8 chan_number;
    /** Band Config */
	BandConfig_t bandcfg;
} scan_chan_list;

/** Data structure of WMM Aci/Aifsn */
typedef struct _wmm_aci_aifsn_t {
#ifdef BIG_ENDIAN_SUPPORT
    /** Reserved */
	t_u8 reserved:1;
    /** Aci */
	t_u8 aci:2;
    /** Acm */
	t_u8 acm:1;
    /** Aifsn */
	t_u8 aifsn:4;
#else
    /** Aifsn */
	t_u8 aifsn:4;
    /** Acm */
	t_u8 acm:1;
    /** Aci */
	t_u8 aci:2;
    /** Reserved */
	t_u8 reserved:1;
#endif				/* BIG_ENDIAN_SUPPORT */
} wmm_aci_aifsn_t, *pwmm_aci_aifsn_t;

/** Data structure of WMM AC parameters  */
typedef struct _wmm_ac_parameters_t {
	wmm_aci_aifsn_t aci_aifsn;   /**< AciAifSn */
	wmm_ecw_t ecw;		   /**< Ecw */
	t_u16 tx_op_limit;		      /**< Tx op limit */
} wmm_ac_parameters_t, *pwmm_ac_parameters_t;

/** Data structure of WMM parameter IE  */
typedef struct _wmm_parameter_t {
    /** OuiType:  00:50:f2:02 */
	t_u8 ouitype[4];
    /** Oui subtype: 01 */
	t_u8 ouisubtype;
    /** version: 01 */
	t_u8 version;
    /** QoS information */
	t_u8 qos_info;
    /** Reserved */
	t_u8 reserved;
    /** AC Parameters Record WMM_AC_BE, WMM_AC_BK, WMM_AC_VI, WMM_AC_VO */
	wmm_ac_parameters_t ac_params[MAX_AC_QUEUES];
} wmm_parameter_t, *pwmm_parameter_t;

typedef struct _mlan_uap_bss_param {
    /** AP mac addr */
	mlan_802_11_mac_addr mac_addr;
    /** SSID */
	mlan_802_11_ssid ssid;
    /** Broadcast ssid control */
	t_u8 bcast_ssid_ctl;
    /** Radio control: on/off */
	t_u8 radio_ctl;
    /** dtim period */
	t_u8 dtim_period;
    /** beacon period */
	t_u16 beacon_period;
    /** rates */
	t_u8 rates[MAX_DATA_RATES];
    /** Tx data rate */
	t_u16 tx_data_rate;
    /** Tx beacon rate */
	t_u16 tx_beacon_rate;
    /** multicast/broadcast data rate */
	t_u16 mcbc_data_rate;
    /** Tx power level in dBm */
	t_u8 tx_power_level;
    /** Tx antenna */
	t_u8 tx_antenna;
    /** Rx antenna */
	t_u8 rx_antenna;
    /** packet forward control */
	t_u8 pkt_forward_ctl;
    /** max station count */
	t_u16 max_sta_count;
    /** mac filter */
	mac_filter filter;
    /** station ageout timer in unit of 100ms  */
	t_u32 sta_ageout_timer;
    /** PS station ageout timer in unit of 100ms  */
	t_u32 ps_sta_ageout_timer;
    /** RTS threshold */
	t_u16 rts_threshold;
    /** fragmentation threshold */
	t_u16 frag_threshold;
    /**  retry_limit */
	t_u16 retry_limit;
    /**  pairwise update timeout in milliseconds */
	t_u32 pairwise_update_timeout;
    /** pairwise handshake retries */
	t_u32 pwk_retries;
    /**  groupwise update timeout in milliseconds */
	t_u32 groupwise_update_timeout;
    /** groupwise handshake retries */
	t_u32 gwk_retries;
    /** preamble type */
	t_u8 preamble_type;
    /** band cfg */
	BandConfig_t band_cfg;
    /** channel */
	t_u8 channel;
    /** auth mode */
	t_u16 auth_mode;
    /** encryption protocol */
	t_u16 protocol;
    /** key managment type */
	t_u16 key_mgmt;
    /** wep param */
	wep_param wep_cfg;
    /** wpa param */
	wpa_param wpa_cfg;
    /** Mgmt IE passthru mask */
	t_u32 mgmt_ie_passthru_mask;
	/*
	 * 11n HT Cap  HTCap_t  ht_cap
	 */
    /** HT Capabilities Info field */
	t_u16 ht_cap_info;
    /** A-MPDU Parameters field */
	t_u8 ampdu_param;
    /** Supported MCS Set field */
	t_u8 supported_mcs_set[16];
    /** HT Extended Capabilities field */
	t_u16 ht_ext_cap;
    /** Transmit Beamforming Capabilities field */
	t_u32 tx_bf_cap;
    /** Antenna Selection Capability field */
	t_u8 asel;
    /** Enable 2040 Coex */
	t_u8 enable_2040coex;
    /** key management operation */
	t_u16 key_mgmt_operation;
    /** BSS status */
	t_u16 bss_status;
#ifdef WIFI_DIRECT_SUPPORT
	/* pre shared key */
	t_u8 psk[MLAN_MAX_KEY_LENGTH];
#endif				/* WIFI_DIRECT_SUPPORT */
    /** Number of channels in scan_channel_list */
	t_u32 num_of_chan;
    /** scan channel list in ACS mode */
	scan_chan_list chan_list[MLAN_MAX_CHANNEL];
    /** Wmm parameters */
	wmm_parameter_t wmm_para;

} mlan_uap_bss_param;
#endif

/** Enumeration for authentication mode */
enum _mlan_auth_mode {
	MLAN_AUTH_MODE_OPEN = 0x00,
	MLAN_AUTH_MODE_SHARED = 0x01,
	MLAN_AUTH_MODE_FT = 0x02,
	MLAN_AUTH_MODE_NETWORKEAP = 0x80,
	MLAN_AUTH_MODE_AUTO = 0xFF,
};

#ifdef UAP_SUPPORT
/** TxPD descriptor */
typedef MLAN_PACK_START struct _UapTxPD {
	/** BSS type */
	t_u8 bss_type;
	/** BSS number */
	t_u8 bss_num;
	/** Tx packet length */
	t_u16 tx_pkt_length;
	/** Tx packet offset */
	t_u16 tx_pkt_offset;
	/** Tx packet type */
	t_u16 tx_pkt_type;
	/** Tx Control */
	t_u32 tx_control;
	/** Pkt Priority */
	t_u8 priority;
	/** Transmit Pkt Flags*/
	t_u8 flags;
    /** Amount of time the packet has been queued
      * in the driver (units = 2ms)*/
	t_u8 pkt_delay_2ms;
    /** reserved */
	t_u8 reserved;
    /** Tx Control */
	t_u32 tx_control_1;
} MLAN_PACK_END UapTxPD, *PUapTxPD;

/** RxPD Descriptor */
typedef MLAN_PACK_START struct _UapRxPD {
    /** BSS Type */
	t_u8 bss_type;
    /** BSS number*/
	t_u8 bss_num;
    /** Rx packet length */
	t_u16 rx_pkt_length;
    /** Rx packet offset */
	t_u16 rx_pkt_offset;
    /** Rx packet type */
	t_u16 rx_pkt_type;
    /** Sequence nunmber */
	t_u16 seq_num;
    /** Packet Priority */
	t_u8 priority;
    /** Rx Packet Rate */
	t_u8 rx_rate;
    /** SNR */
	t_s8 snr;
    /** Noise Floor */
	t_s8 nf;
    /** [Bit 1] [Bit 0] RxRate format: legacy rate = 00 HT = 01 VHT = 10
     *  [Bit 3] [Bit 2] HT/VHT Bandwidth BW20 = 00 BW40 = 01 BW80 = 10 BW160 = 11
     *  [Bit 4] HT/VHT Guard interval LGI = 0 SGI = 1
     *  [Bit 5] STBC support Enabled = 1
     *  [Bit 6] LDPC support Enabled = 1
     *  [Bit 7] Reserved */
	t_u8 rate_info;
    /** Reserved */
	t_u8 reserved1[3];
    /** TDLS flags, bit 0: 0=InfraLink, 1=DirectLink */
	t_u8 flags;
    /** For SD8887 ntenna info: 0 = 2.4G antenna a; 1 = 2.4G antenna b; 3 = 5G antenna; 0xff = invalid value */
	t_u8 antenna;
	/* [31:0] ToA of the rx packet, [63:32] ToD of the ack for the rx packet Both ToA and ToD are in nanoseconds */
	t_u64 toa_tod_tstamps;
    /** rx info */
	t_u32 rx_info;
} MLAN_PACK_END UapRxPD, *PUapRxPD;
#endif /* UAP_SUPPORT */

/* ####################
     From mlan_main.h
   #################### */

/** 16 bits byte swap */
#define swap_byte_16(x) \
((t_u16)((((t_u16)(x) & 0x00ffU) << 8) | \
		 (((t_u16)(x) & 0xff00U) >> 8)))

/** 32 bits byte swap */
#define swap_byte_32(x) \
((t_u32)((((t_u32)(x) & 0x000000ffUL) << 24) | \
	 (((t_u32)(x) & 0x0000ff00UL) <<  8) | \
	 (((t_u32)(x) & 0x00ff0000UL) >>  8) | \
	 (((t_u32)(x) & 0xff000000UL) >> 24)))

/** 64 bits byte swap */
#define swap_byte_64(x) \
((t_u64)((t_u64)(((t_u64)(x) & 0x00000000000000ffULL) << 56) | \
	     (t_u64)(((t_u64)(x) & 0x000000000000ff00ULL) << 40) | \
	     (t_u64)(((t_u64)(x) & 0x0000000000ff0000ULL) << 24) | \
	     (t_u64)(((t_u64)(x) & 0x00000000ff000000ULL) <<  8) | \
	     (t_u64)(((t_u64)(x) & 0x000000ff00000000ULL) >>  8) | \
	     (t_u64)(((t_u64)(x) & 0x0000ff0000000000ULL) >> 24) | \
	     (t_u64)(((t_u64)(x) & 0x00ff000000000000ULL) >> 40) | \
	     (t_u64)(((t_u64)(x) & 0xff00000000000000ULL) >> 56)))

#ifdef BIG_ENDIAN_SUPPORT
/** Convert ulong n/w to host */
#define mlan_ntohl(x) x
/** Convert host ulong to n/w */
#define mlan_htonl(x) x
/** Convert n/w to host */
#define mlan_ntohs(x)  x
/** Convert host to n/w */
#define mlan_htons(x)  x
/** Convert from 16 bit little endian format to CPU format */
#define wlan_le16_to_cpu(x) swap_byte_16(x)
/** Convert from 32 bit little endian format to CPU format */
#define wlan_le32_to_cpu(x) swap_byte_32(x)
/** Convert from 64 bit little endian format to CPU format */
#define wlan_le64_to_cpu(x) swap_byte_64(x)
/** Convert to 16 bit little endian format from CPU format */
#define wlan_cpu_to_le16(x) swap_byte_16(x)
/** Convert to 32 bit little endian format from CPU format */
#define wlan_cpu_to_le32(x) swap_byte_32(x)
/** Convert to 64 bit little endian format from CPU format */
#define wlan_cpu_to_le64(x) swap_byte_64(x)

/** Convert TxPD to little endian format from CPU format */
#define endian_convert_TxPD(x)                                          \
	{                                                                   \
	    (x)->tx_pkt_length = wlan_cpu_to_le16((x)->tx_pkt_length);      \
	    (x)->tx_pkt_offset = wlan_cpu_to_le16((x)->tx_pkt_offset);      \
	    (x)->tx_pkt_type   = wlan_cpu_to_le16((x)->tx_pkt_type);        \
	    (x)->tx_control    = wlan_cpu_to_le32((x)->tx_control);         \
        (x)->tx_control_1  = wlan_cpu_to_le32((x)->tx_control_1);         \
	}
/** Convert RxPD from little endian format to CPU format */
#define endian_convert_RxPD(x)                                          \
	{                                                                   \
	    (x)->rx_pkt_length = wlan_le16_to_cpu((x)->rx_pkt_length);      \
	    (x)->rx_pkt_offset = wlan_le16_to_cpu((x)->rx_pkt_offset);      \
	    (x)->rx_pkt_type   = wlan_le16_to_cpu((x)->rx_pkt_type);        \
	    (x)->seq_num       = wlan_le16_to_cpu((x)->seq_num);            \
        (x)->rx_info       = wlan_le32_to_cpu((x)->rx_info);            \
	}
/** Convert RxPD extra header from little endian format to CPU format */
#define endian_convert_RxPD_extra_header(x)                                          \
	{                                                                   \
	    (x)->channel_flags = wlan_le16_to_cpu((x)->channel_flags);      \
	}
#else
/** Convert ulong n/w to host */
#define mlan_ntohl(x) swap_byte_32(x)
/** Convert host ulong to n/w */
#define mlan_htonl(x) swap_byte_32(x)
/** Convert n/w to host */
#define mlan_ntohs(x) swap_byte_16(x)
/** Convert host to n/w */
#define mlan_htons(x) swap_byte_16(x)
/** Do nothing */
#define wlan_le16_to_cpu(x) x
/** Do nothing */
#define wlan_le32_to_cpu(x) x
/** Do nothing */
#define wlan_le64_to_cpu(x) x
/** Do nothing */
#define wlan_cpu_to_le16(x) x
/** Do nothing */
#define wlan_cpu_to_le32(x) x
/** Do nothing */
#define wlan_cpu_to_le64(x) x

/** Convert TxPD to little endian format from CPU format */
#define endian_convert_TxPD(x)  do {} while (0)
/** Convert RxPD from little endian format to CPU format */
#define endian_convert_RxPD(x)  do {} while (0)
/** Convert RxPD extra header from little endian format to CPU format */
#define endian_convert_RxPD_extra_header(x)  do {} while (0)
#endif /* BIG_ENDIAN_SUPPORT */

/** Find minimum */
#ifndef MIN
#define MIN(a, b)    ((a) < (b) ? (a) : (b))
#endif

/** Find maximum */
#ifndef MAX
#define MAX(a, b)    ((a) > (b) ? (a) : (b))
#endif

#ifdef memset
#undef memset
#endif
/** Memset routine */
#define memset(mpl_utils, s, c, len) \
	(mpl_utils->moal_memset(mpl_utils->pmoal_handle, s, c, len))

#ifdef memmove
#undef memmove
#endif
/** Memmove routine */
#define memmove(mpl_utils, dest, src, len) \
	(mpl_utils->moal_memmove(mpl_utils->pmoal_handle, dest, src, len))

#ifdef memcpy
#undef memcpy
#endif
/** Memcpy routine */
#define memcpy(mpl_utils, to, from, len) \
	(mpl_utils->moal_memcpy(mpl_utils->pmoal_handle, to, from, len))

#ifdef memcmp
#undef memcmp
#endif
/** Memcmp routine */
#define memcmp(mpl_utils, s1, s2, len) \
	(mpl_utils->moal_memcmp(mpl_utils->pmoal_handle, s1, s2, len))

/** Find number of elements */
#ifndef NELEMENTS
#define NELEMENTS(x)    (sizeof(x)/sizeof(x[0]))
#endif

#define MOAL_ALLOC_MLAN_BUFFER  (0)
#define MOAL_MALLOC_BUFFER      (1)

/* ##################
     From mlan_fw.h
   ################## */
/** TxPD descriptor */
typedef MLAN_PACK_START struct _TxPD {
    /** BSS type */
	t_u8 bss_type;
    /** BSS number */
	t_u8 bss_num;
    /** Tx packet length */
	t_u16 tx_pkt_length;
    /** Tx packet offset */
	t_u16 tx_pkt_offset;
    /** Tx packet type */
	t_u16 tx_pkt_type;
    /** Tx Control */
	t_u32 tx_control;
    /** Pkt Priority */
	t_u8 priority;
    /** Transmit Pkt Flags*/
	t_u8 flags;
    /** Amount of time the packet has been queued
      * in the driver (units = 2ms)*/
	t_u8 pkt_delay_2ms;
    /** reserved */
	t_u8 reserved;
    /** Tx Control */
	t_u32 tx_control_1;
} MLAN_PACK_END TxPD, *PTxPD;

/** 2K buf size */
#define MLAN_TX_DATA_BUF_SIZE_2K        2048

/* ####################
     From mlan_decl.h
   #################### */

/** IN parameter */
#define IN
/** OUT parameter */
#define OUT

/** BIT value */
#define MBIT(x)    (((t_u32)1) << (x))

#ifdef DEBUG_LEVEL1
/** Debug level bit definition */
#define	MMSG        MBIT(0)
#define MFATAL      MBIT(1)
#define MERROR      MBIT(2)
#define MDATA       MBIT(3)
#define MCMND       MBIT(4)
#define MEVENT      MBIT(5)
#define MINTR       MBIT(6)
#define MIOCTL      MBIT(7)

#define MMPA_D      MBIT(15)
#define MDAT_D      MBIT(16)
#define MCMD_D      MBIT(17)
#define MEVT_D      MBIT(18)
#define MFW_D       MBIT(19)
#define MIF_D       MBIT(20)

#define MENTRY      MBIT(28)
#define MWARN       MBIT(29)
#define MINFO       MBIT(30)
#define MHEX_DUMP   MBIT(31)
#endif /* DEBUG_LEVEL1 */

/** Wait until a condition becomes true */
#define MASSERT(cond)                   \
do {                                    \
	if (!(cond)) {                      \
		PRINTM(MFATAL, "ASSERT: %s: %i\n", __FUNCTION__, __LINE__); \
	}                                   \
} while (0)

/** Log entry point for debugging */
#define ENTER()      PRINTM(MENTRY, "Enter: %s\n", __FUNCTION__)
/** Log exit point for debugging */
#define LEAVE()      PRINTM(MENTRY, "Leave: %s\n", __FUNCTION__)

/* ####################
     From mlan_main.h
   #################### */

#ifdef DEBUG_LEVEL1
extern t_void (*print_callback) (IN t_void *pmoal_handle,
				 IN t_u32 level, IN char *pformat, IN ...
	);

extern mlan_status (*get_sys_time_callback) (IN t_void *pmoal_handle,
					     OUT t_u32 *psec, OUT t_u32 *pusec);

extern t_u32 mlan_drvdbg;

#ifdef DEBUG_LEVEL2
#define PRINTM_MINFO(msg...)  do {if ((mlan_drvdbg & MINFO) && (print_callback)) \
									print_callback(MNULL, MINFO, msg); } while (0)
#define PRINTM_MWARN(msg...)  do {if ((mlan_drvdbg & MWARN) && (print_callback)) \
									print_callback(MNULL, MWARN, msg); } while (0)
#define PRINTM_MENTRY(msg...) do {if ((mlan_drvdbg & MENTRY) && (print_callback)) \
									print_callback(MNULL, MENTRY, msg); } while (0)
#define PRINTM_GET_SYS_TIME(level, psec, pusec)             \
do {                                                        \
	if ((level & mlan_drvdbg) && (get_sys_time_callback))        \
		get_sys_time_callback(MNULL, psec, pusec);          \
} while (0)

/** Hexdump for level-2 debugging */
#define HEXDUMP(x, y, z)   \
do {                \
	if ((mlan_drvdbg & (MHEX_DUMP | MINFO)) && (print_callback))  \
		print_callback(MNULL, MHEX_DUMP | MINFO, x, y, z); \
} while (0)

#else

#define PRINTM_MINFO(msg...)  do {} while (0)
#define PRINTM_MWARN(msg...)  do {} while (0)
#define PRINTM_MENTRY(msg...) do {} while (0)

#define PRINTM_GET_SYS_TIME(level, psec, pusec)         \
do {                                                    \
	if ((level & mlan_drvdbg) && (get_sys_time_callback)     \
			&& (level != MINFO) && (level != MWARN))    \
		get_sys_time_callback(MNULL, psec, pusec);      \
} while (0)

/** Hexdump for debugging */
#define HEXDUMP(x, y, z) do {} while (0)

#endif /* DEBUG_LEVEL2 */

#define PRINTM_MFW_D(msg...)  do {if ((mlan_drvdbg & MFW_D) && (print_callback)) \
									print_callback(MNULL, MFW_D, msg); } while (0)
#define PRINTM_MCMD_D(msg...) do {if ((mlan_drvdbg & MCMD_D) && (print_callback)) \
									print_callback(MNULL, MCMD_D, msg); } while (0)
#define PRINTM_MDAT_D(msg...) do {if ((mlan_drvdbg & MDAT_D) && (print_callback)) \
									print_callback(MNULL, MDAT_D, msg); } while (0)
#define PRINTM_MIF_D(msg...) do {if ((mlan_drvdbg & MIF_D) && (print_callback)) \
									print_callback(MNULL, MIF_D, msg); } while (0)

#define PRINTM_MIOCTL(msg...) do {if ((mlan_drvdbg & MIOCTL) && (print_callback)) \
									print_callback(MNULL, MIOCTL, msg); } while (0)
#define PRINTM_MINTR(msg...)  do {if ((mlan_drvdbg & MINTR) && (print_callback)) \
									print_callback(MNULL, MINTR, msg); } while (0)
#define PRINTM_MEVENT(msg...) do {if ((mlan_drvdbg & MEVENT) && (print_callback)) \
									print_callback(MNULL, MEVENT, msg); } while (0)
#define PRINTM_MCMND(msg...)  do {if ((mlan_drvdbg & MCMND) && (print_callback)) \
									print_callback(MNULL, MCMND, msg); } while (0)
#define PRINTM_MDATA(msg...)  do {if ((mlan_drvdbg & MDATA) && (print_callback)) \
									print_callback(MNULL, MDATA, msg); } while (0)
#define PRINTM_MERROR(msg...) do {if ((mlan_drvdbg & MERROR) && (print_callback)) \
									print_callback(MNULL, MERROR, msg); } while (0)
#define PRINTM_MFATAL(msg...) do {if ((mlan_drvdbg & MFATAL) && (print_callback)) \
									print_callback(MNULL, MFATAL, msg); } while (0)
#define PRINTM_MMSG(msg...)   do {if ((mlan_drvdbg & MMSG) && (print_callback)) \
									print_callback(MNULL, MMSG, msg); } while (0)

#define PRINTM(level, msg...) PRINTM_##level((char *)msg)

/** Log debug message */
#ifdef __GNUC__
#define PRINTM_NETINTF(level, pmu, pml)   \
do {                                    \
	if ((mlan_drvdbg & level) && pmu && pml      \
			&& pmu->moal_print_netintf) \
		pmu->moal_print_netintf( \
			pmu->pmoal_handle, \
			pml->bss_index, level); \
} while (0)
#endif /* __GNUC__ */

/** Max hex dump data length */
#define MAX_DATA_DUMP_LEN     64

/** Debug hexdump for level-1 debugging */
#define DBG_HEXDUMP(level, x, y, z)   \
do {                \
	if ((mlan_drvdbg & level) && print_callback)  \
		print_callback(MNULL, MHEX_DUMP | level, x, y, z); \
} while (0)

#else /* DEBUG_LEVEL1 */

#define PRINTM(level, msg...) do {} while (0)

#define PRINTM_NETINTF(level, pmpriv) do {} while (0)

/** Debug hexdump for level-1 debugging */
#define DBG_HEXDUMP(level, x, y, z) do {} while (0)

/** Hexdump for debugging */
#define HEXDUMP(x, y, z) do {} while (0)

#define PRINTM_GET_SYS_TIME(level, psec, pusec) do { } while (0)

#endif /* DEBUG_LEVEL1 */

/* #######################################################
     embedded authenticator and supplicant specific
   ################ ########################################*/

/** Get_system_time routine */
#define get_system_time(mpl_utils, psec, pusec) \
	(mpl_utils->moal_get_system_time(mpl_utils->pmoal_handle, psec, pusec))

/** malloc routine */
#ifdef malloc
#undef malloc
#endif
#define malloc(mpl_utils, len, pptr) \
	(mpl_utils->moal_malloc(mpl_utils->pmoal_handle, len, MLAN_MEM_DEF, pptr))

/** free routine */
#ifdef free
#undef free
#endif
#define free(mpl_utils, ptr) \
	(mpl_utils->moal_mfree(mpl_utils->pmoal_handle, ptr))

#endif /* _HOSTSA_EXT_DEF_H_ */
