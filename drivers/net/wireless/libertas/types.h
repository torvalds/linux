/**
  * This header file contains definition for global types
  */
#ifndef _LBS_TYPES_H_
#define _LBS_TYPES_H_

#include <linux/if_ether.h>
#include <linux/ieee80211.h>
#include <asm/byteorder.h>

struct ieee_ie_header {
	u8 id;
	u8 len;
} __attribute__ ((packed));

struct ieee_ie_cf_param_set {
	struct ieee_ie_header header;

	u8 cfpcnt;
	u8 cfpperiod;
	__le16 cfpmaxduration;
	__le16 cfpdurationremaining;
} __attribute__ ((packed));


struct ieee_ie_ibss_param_set {
	struct ieee_ie_header header;

	__le16 atimwindow;
} __attribute__ ((packed));

union ieee_ss_param_set {
	struct ieee_ie_cf_param_set cf;
	struct ieee_ie_ibss_param_set ibss;
} __attribute__ ((packed));

struct ieee_ie_fh_param_set {
	struct ieee_ie_header header;

	__le16 dwelltime;
	u8 hopset;
	u8 hoppattern;
	u8 hopindex;
} __attribute__ ((packed));

struct ieee_ie_ds_param_set {
	struct ieee_ie_header header;

	u8 channel;
} __attribute__ ((packed));

union ieee_phy_param_set {
	struct ieee_ie_fh_param_set fh;
	struct ieee_ie_ds_param_set ds;
} __attribute__ ((packed));

/** TLV  type ID definition */
#define PROPRIETARY_TLV_BASE_ID		0x0100

/* Terminating TLV type */
#define MRVL_TERMINATE_TLV_ID		0xffff

#define TLV_TYPE_SSID				0x0000
#define TLV_TYPE_RATES				0x0001
#define TLV_TYPE_PHY_FH				0x0002
#define TLV_TYPE_PHY_DS				0x0003
#define TLV_TYPE_CF				    0x0004
#define TLV_TYPE_IBSS				0x0006

#define TLV_TYPE_DOMAIN				0x0007

#define TLV_TYPE_POWER_CAPABILITY	0x0021

#define TLV_TYPE_KEY_MATERIAL       (PROPRIETARY_TLV_BASE_ID + 0)
#define TLV_TYPE_CHANLIST           (PROPRIETARY_TLV_BASE_ID + 1)
#define TLV_TYPE_NUMPROBES          (PROPRIETARY_TLV_BASE_ID + 2)
#define TLV_TYPE_RSSI_LOW           (PROPRIETARY_TLV_BASE_ID + 4)
#define TLV_TYPE_SNR_LOW            (PROPRIETARY_TLV_BASE_ID + 5)
#define TLV_TYPE_FAILCOUNT          (PROPRIETARY_TLV_BASE_ID + 6)
#define TLV_TYPE_BCNMISS            (PROPRIETARY_TLV_BASE_ID + 7)
#define TLV_TYPE_LED_GPIO           (PROPRIETARY_TLV_BASE_ID + 8)
#define TLV_TYPE_LEDBEHAVIOR        (PROPRIETARY_TLV_BASE_ID + 9)
#define TLV_TYPE_PASSTHROUGH        (PROPRIETARY_TLV_BASE_ID + 10)
#define TLV_TYPE_REASSOCAP          (PROPRIETARY_TLV_BASE_ID + 11)
#define TLV_TYPE_POWER_TBL_2_4GHZ   (PROPRIETARY_TLV_BASE_ID + 12)
#define TLV_TYPE_POWER_TBL_5GHZ     (PROPRIETARY_TLV_BASE_ID + 13)
#define TLV_TYPE_BCASTPROBE	    (PROPRIETARY_TLV_BASE_ID + 14)
#define TLV_TYPE_NUMSSID_PROBE	    (PROPRIETARY_TLV_BASE_ID + 15)
#define TLV_TYPE_WMMQSTATUS   	    (PROPRIETARY_TLV_BASE_ID + 16)
#define TLV_TYPE_CRYPTO_DATA	    (PROPRIETARY_TLV_BASE_ID + 17)
#define TLV_TYPE_WILDCARDSSID	    (PROPRIETARY_TLV_BASE_ID + 18)
#define TLV_TYPE_TSFTIMESTAMP	    (PROPRIETARY_TLV_BASE_ID + 19)
#define TLV_TYPE_RSSI_HIGH          (PROPRIETARY_TLV_BASE_ID + 22)
#define TLV_TYPE_SNR_HIGH           (PROPRIETARY_TLV_BASE_ID + 23)
#define TLV_TYPE_AUTH_TYPE          (PROPRIETARY_TLV_BASE_ID + 31)
#define TLV_TYPE_MESH_ID            (PROPRIETARY_TLV_BASE_ID + 37)
#define TLV_TYPE_OLD_MESH_ID        (PROPRIETARY_TLV_BASE_ID + 291)

/** TLV related data structures*/
struct mrvl_ie_header {
	__le16 type;
	__le16 len;
} __attribute__ ((packed));

struct mrvl_ie_data {
	struct mrvl_ie_header header;
	u8 Data[1];
} __attribute__ ((packed));

struct mrvl_ie_rates_param_set {
	struct mrvl_ie_header header;
	u8 rates[1];
} __attribute__ ((packed));

struct mrvl_ie_ssid_param_set {
	struct mrvl_ie_header header;
	u8 ssid[1];
} __attribute__ ((packed));

struct mrvl_ie_wildcard_ssid_param_set {
	struct mrvl_ie_header header;
	u8 MaxSsidlength;
	u8 ssid[1];
} __attribute__ ((packed));

struct chanscanmode {
#ifdef __BIG_ENDIAN_BITFIELD
	u8 reserved_2_7:6;
	u8 disablechanfilt:1;
	u8 passivescan:1;
#else
	u8 passivescan:1;
	u8 disablechanfilt:1;
	u8 reserved_2_7:6;
#endif
} __attribute__ ((packed));

struct chanscanparamset {
	u8 radiotype;
	u8 channumber;
	struct chanscanmode chanscanmode;
	__le16 minscantime;
	__le16 maxscantime;
} __attribute__ ((packed));

struct mrvl_ie_chanlist_param_set {
	struct mrvl_ie_header header;
	struct chanscanparamset chanscanparam[1];
} __attribute__ ((packed));

struct mrvl_ie_cf_param_set {
	struct mrvl_ie_header header;
	u8 cfpcnt;
	u8 cfpperiod;
	__le16 cfpmaxduration;
	__le16 cfpdurationremaining;
} __attribute__ ((packed));

struct mrvl_ie_ds_param_set {
	struct mrvl_ie_header header;
	u8 channel;
} __attribute__ ((packed));

struct mrvl_ie_rsn_param_set {
	struct mrvl_ie_header header;
	u8 rsnie[1];
} __attribute__ ((packed));

struct mrvl_ie_tsf_timestamp {
	struct mrvl_ie_header header;
	__le64 tsftable[1];
} __attribute__ ((packed));

/* v9 and later firmware only */
struct mrvl_ie_auth_type {
	struct mrvl_ie_header header;
	__le16 auth;
} __attribute__ ((packed));

/**  Local Power capability */
struct mrvl_ie_power_capability {
	struct mrvl_ie_header header;
	s8 minpower;
	s8 maxpower;
} __attribute__ ((packed));

/* used in CMD_802_11_SUBSCRIBE_EVENT for SNR, RSSI and Failure */
struct mrvl_ie_thresholds {
	struct mrvl_ie_header header;
	u8 value;
	u8 freq;
} __attribute__ ((packed));

struct mrvl_ie_beacons_missed {
	struct mrvl_ie_header header;
	u8 beaconmissed;
	u8 reserved;
} __attribute__ ((packed));

struct mrvl_ie_num_probes {
	struct mrvl_ie_header header;
	__le16 numprobes;
} __attribute__ ((packed));

struct mrvl_ie_bcast_probe {
	struct mrvl_ie_header header;
	__le16 bcastprobe;
} __attribute__ ((packed));

struct mrvl_ie_num_ssid_probe {
	struct mrvl_ie_header header;
	__le16 numssidprobe;
} __attribute__ ((packed));

struct led_pin {
	u8 led;
	u8 pin;
} __attribute__ ((packed));

struct mrvl_ie_ledgpio {
	struct mrvl_ie_header header;
	struct led_pin ledpin[1];
} __attribute__ ((packed));

struct led_bhv {
	uint8_t	firmwarestate;
	uint8_t	led;
	uint8_t	ledstate;
	uint8_t	ledarg;
} __attribute__ ((packed));


struct mrvl_ie_ledbhv {
	struct mrvl_ie_header header;
	struct led_bhv ledbhv[1];
} __attribute__ ((packed));

/* Meant to be packed as the value member of a struct ieee80211_info_element.
 * Note that the len member of the ieee80211_info_element varies depending on
 * the mesh_id_len */
struct mrvl_meshie_val {
	uint8_t oui[3];
	uint8_t type;
	uint8_t subtype;
	uint8_t version;
	uint8_t active_protocol_id;
	uint8_t active_metric_id;
	uint8_t mesh_capability;
	uint8_t mesh_id_len;
	uint8_t mesh_id[IEEE80211_MAX_SSID_LEN];
} __attribute__ ((packed));

struct mrvl_meshie {
	u8 id, len;
	struct mrvl_meshie_val val;
} __attribute__ ((packed));

struct mrvl_mesh_defaults {
	__le32 bootflag;
	uint8_t boottime;
	uint8_t reserved;
	__le16 channel;
	struct mrvl_meshie meshie;
} __attribute__ ((packed));

#endif
