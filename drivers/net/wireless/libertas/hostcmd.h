/*
 * This file contains the function prototypes, data structure
 * and defines for all the host/station commands
 */
#ifndef _LBS_HOSTCMD_H
#define _LBS_HOSTCMD_H

#include <linux/wireless.h>
#include "11d.h"
#include "types.h"

/* 802.11-related definitions */

/* TxPD descriptor */
struct txpd {
	/* union to cope up with later FW revisions */
	union {
		/* Current Tx packet status */
		__le32 tx_status;
		struct {
			/* BSS type: client, AP, etc. */
			u8 bss_type;
			/* BSS number */
			u8 bss_num;
			/* Reserved */
			__le16 reserved;
		} bss;
	} u;
	/* Tx control */
	__le32 tx_control;
	__le32 tx_packet_location;
	/* Tx packet length */
	__le16 tx_packet_length;
	/* First 2 byte of destination MAC address */
	u8 tx_dest_addr_high[2];
	/* Last 4 byte of destination MAC address */
	u8 tx_dest_addr_low[4];
	/* Pkt Priority */
	u8 priority;
	/* Pkt Trasnit Power control */
	u8 powermgmt;
	/* Amount of time the packet has been queued in the driver (units = 2ms) */
	u8 pktdelay_2ms;
	/* reserved */
	u8 reserved1;
} __attribute__ ((packed));

/* RxPD Descriptor */
struct rxpd {
	/* union to cope up with later FW revisions */
	union {
		/* Current Rx packet status */
		__le16 status;
		struct {
			/* BSS type: client, AP, etc. */
			u8 bss_type;
			/* BSS number */
			u8 bss_num;
		} bss;
	} u;

	/* SNR */
	u8 snr;

	/* Tx control */
	u8 rx_control;

	/* Pkt length */
	__le16 pkt_len;

	/* Noise Floor */
	u8 nf;

	/* Rx Packet Rate */
	u8 rx_rate;

	/* Pkt addr */
	__le32 pkt_ptr;

	/* Next Rx RxPD addr */
	__le32 next_rxpd_ptr;

	/* Pkt Priority */
	u8 priority;
	u8 reserved[3];
} __attribute__ ((packed));

struct cmd_header {
	__le16 command;
	__le16 size;
	__le16 seqnum;
	__le16 result;
} __attribute__ ((packed));

struct cmd_ctrl_node {
	struct list_head list;
	int result;
	/* command response */
	int (*callback)(struct lbs_private *, unsigned long, struct cmd_header *);
	unsigned long callback_arg;
	/* command data */
	struct cmd_header *cmdbuf;
	/* wait queue */
	u16 cmdwaitqwoken;
	wait_queue_head_t cmdwait_q;
};

/* Generic structure to hold all key types. */
struct enc_key {
	u16 len;
	u16 flags;  /* KEY_INFO_* from defs.h */
	u16 type; /* KEY_TYPE_* from defs.h */
	u8 key[32];
};

/* lbs_offset_value */
struct lbs_offset_value {
	u32 offset;
	u32 value;
} __attribute__ ((packed));

/* Define general data structure */
/* cmd_DS_GEN */
struct cmd_ds_gen {
	__le16 command;
	__le16 size;
	__le16 seqnum;
	__le16 result;
	void *cmdresp[0];
} __attribute__ ((packed));

#define S_DS_GEN sizeof(struct cmd_ds_gen)


/*
 * Define data structure for CMD_GET_HW_SPEC
 * This structure defines the response for the GET_HW_SPEC command
 */
struct cmd_ds_get_hw_spec {
	struct cmd_header hdr;

	/* HW Interface version number */
	__le16 hwifversion;
	/* HW version number */
	__le16 version;
	/* Max number of TxPD FW can handle */
	__le16 nr_txpd;
	/* Max no of Multicast address */
	__le16 nr_mcast_adr;
	/* MAC address */
	u8 permanentaddr[6];

	/* region Code */
	__le16 regioncode;

	/* Number of antenna used */
	__le16 nr_antenna;

	/* FW release number, example 0x01030304 = 2.3.4p1 */
	__le32 fwrelease;

	/* Base Address of TxPD queue */
	__le32 wcb_base;
	/* Read Pointer of RxPd queue */
	__le32 rxpd_rdptr;

	/* Write Pointer of RxPd queue */
	__le32 rxpd_wrptr;

	/*FW/HW capability */
	__le32 fwcapinfo;
} __attribute__ ((packed));

struct cmd_ds_802_11_subscribe_event {
	struct cmd_header hdr;

	__le16 action;
	__le16 events;

	/* A TLV to the CMD_802_11_SUBSCRIBE_EVENT command can contain a
	 * number of TLVs. From the v5.1 manual, those TLVs would add up to
	 * 40 bytes. However, future firmware might add additional TLVs, so I
	 * bump this up a bit.
	 */
	uint8_t tlv[128];
} __attribute__ ((packed));

/*
 * This scan handle Country Information IE(802.11d compliant)
 * Define data structure for CMD_802_11_SCAN
 */
struct cmd_ds_802_11_scan {
	struct cmd_header hdr;

	uint8_t bsstype;
	uint8_t bssid[ETH_ALEN];
	uint8_t tlvbuffer[0];
#if 0
	mrvlietypes_ssidparamset_t ssidParamSet;
	mrvlietypes_chanlistparamset_t ChanListParamSet;
	mrvlietypes_ratesparamset_t OpRateSet;
#endif
} __attribute__ ((packed));

struct cmd_ds_802_11_scan_rsp {
	struct cmd_header hdr;

	__le16 bssdescriptsize;
	uint8_t nr_sets;
	uint8_t bssdesc_and_tlvbuffer[0];
} __attribute__ ((packed));

struct cmd_ds_802_11_get_log {
	struct cmd_header hdr;

	__le32 mcasttxframe;
	__le32 failed;
	__le32 retry;
	__le32 multiretry;
	__le32 framedup;
	__le32 rtssuccess;
	__le32 rtsfailure;
	__le32 ackfailure;
	__le32 rxfrag;
	__le32 mcastrxframe;
	__le32 fcserror;
	__le32 txframe;
	__le32 wepundecryptable;
} __attribute__ ((packed));

struct cmd_ds_mac_control {
	struct cmd_header hdr;
	__le16 action;
	u16 reserved;
} __attribute__ ((packed));

struct cmd_ds_mac_multicast_adr {
	struct cmd_header hdr;
	__le16 action;
	__le16 nr_of_adrs;
	u8 maclist[ETH_ALEN * MRVDRV_MAX_MULTICAST_LIST_SIZE];
} __attribute__ ((packed));

struct cmd_ds_gspi_bus_config {
	struct cmd_header hdr;
	__le16 action;
	__le16 bus_delay_mode;
	__le16 host_time_delay_to_read_port;
	__le16 host_time_delay_to_read_register;
} __attribute__ ((packed));

struct cmd_ds_802_11_authenticate {
	struct cmd_header hdr;

	u8 bssid[ETH_ALEN];
	u8 authtype;
	u8 reserved[10];
} __attribute__ ((packed));

struct cmd_ds_802_11_deauthenticate {
	struct cmd_header hdr;

	u8 macaddr[ETH_ALEN];
	__le16 reasoncode;
} __attribute__ ((packed));

struct cmd_ds_802_11_associate {
	struct cmd_header hdr;

	u8 bssid[6];
	__le16 capability;
	__le16 listeninterval;
	__le16 bcnperiod;
	u8 dtimperiod;
	u8 iebuf[512];    /* Enough for required and most optional IEs */
} __attribute__ ((packed));

struct cmd_ds_802_11_associate_response {
	struct cmd_header hdr;

	__le16 capability;
	__le16 statuscode;
	__le16 aid;
	u8 iebuf[512];
} __attribute__ ((packed));

struct cmd_ds_802_11_set_wep {
	struct cmd_header hdr;

	/* ACT_ADD, ACT_REMOVE or ACT_ENABLE */
	__le16 action;

	/* key Index selected for Tx */
	__le16 keyindex;

	/* 40, 128bit or TXWEP */
	uint8_t keytype[4];
	uint8_t keymaterial[4][16];
} __attribute__ ((packed));

struct cmd_ds_802_3_get_stat {
	__le32 xmitok;
	__le32 rcvok;
	__le32 xmiterror;
	__le32 rcverror;
	__le32 rcvnobuffer;
	__le32 rcvcrcerror;
} __attribute__ ((packed));

struct cmd_ds_802_11_get_stat {
	__le32 txfragmentcnt;
	__le32 mcasttxframecnt;
	__le32 failedcnt;
	__le32 retrycnt;
	__le32 Multipleretrycnt;
	__le32 rtssuccesscnt;
	__le32 rtsfailurecnt;
	__le32 ackfailurecnt;
	__le32 frameduplicatecnt;
	__le32 rxfragmentcnt;
	__le32 mcastrxframecnt;
	__le32 fcserrorcnt;
	__le32 bcasttxframecnt;
	__le32 bcastrxframecnt;
	__le32 txbeacon;
	__le32 rxbeacon;
	__le32 wepundecryptable;
} __attribute__ ((packed));

struct cmd_ds_802_11_snmp_mib {
	struct cmd_header hdr;

	__le16 action;
	__le16 oid;
	__le16 bufsize;
	u8 value[128];
} __attribute__ ((packed));

struct cmd_ds_mac_reg_map {
	__le16 buffersize;
	u8 regmap[128];
	__le16 reserved;
} __attribute__ ((packed));

struct cmd_ds_bbp_reg_map {
	__le16 buffersize;
	u8 regmap[128];
	__le16 reserved;
} __attribute__ ((packed));

struct cmd_ds_rf_reg_map {
	__le16 buffersize;
	u8 regmap[64];
	__le16 reserved;
} __attribute__ ((packed));

struct cmd_ds_mac_reg_access {
	__le16 action;
	__le16 offset;
	__le32 value;
} __attribute__ ((packed));

struct cmd_ds_bbp_reg_access {
	__le16 action;
	__le16 offset;
	u8 value;
	u8 reserved[3];
} __attribute__ ((packed));

struct cmd_ds_rf_reg_access {
	__le16 action;
	__le16 offset;
	u8 value;
	u8 reserved[3];
} __attribute__ ((packed));

struct cmd_ds_802_11_radio_control {
	struct cmd_header hdr;

	__le16 action;
	__le16 control;
} __attribute__ ((packed));

struct cmd_ds_802_11_beacon_control {
	__le16 action;
	__le16 beacon_enable;
	__le16 beacon_period;
} __attribute__ ((packed));

struct cmd_ds_802_11_sleep_params {
	struct cmd_header hdr;

	/* ACT_GET/ACT_SET */
	__le16 action;

	/* Sleep clock error in ppm */
	__le16 error;

	/* Wakeup offset in usec */
	__le16 offset;

	/* Clock stabilization time in usec */
	__le16 stabletime;

	/* control periodic calibration */
	uint8_t calcontrol;

	/* control the use of external sleep clock */
	uint8_t externalsleepclk;

	/* reserved field, should be set to zero */
	__le16 reserved;
} __attribute__ ((packed));

struct cmd_ds_802_11_inactivity_timeout {
	struct cmd_header hdr;

	/* ACT_GET/ACT_SET */
	__le16 action;

	/* Inactivity timeout in msec */
	__le16 timeout;
} __attribute__ ((packed));

struct cmd_ds_802_11_rf_channel {
	struct cmd_header hdr;

	__le16 action;
	__le16 channel;
	__le16 rftype;      /* unused */
	__le16 reserved;    /* unused */
	u8 channellist[32]; /* unused */
} __attribute__ ((packed));

struct cmd_ds_802_11_rssi {
	/* weighting factor */
	__le16 N;

	__le16 reserved_0;
	__le16 reserved_1;
	__le16 reserved_2;
} __attribute__ ((packed));

struct cmd_ds_802_11_rssi_rsp {
	__le16 SNR;
	__le16 noisefloor;
	__le16 avgSNR;
	__le16 avgnoisefloor;
} __attribute__ ((packed));

struct cmd_ds_802_11_mac_address {
	struct cmd_header hdr;

	__le16 action;
	u8 macadd[ETH_ALEN];
} __attribute__ ((packed));

struct cmd_ds_802_11_rf_tx_power {
	struct cmd_header hdr;

	__le16 action;
	__le16 curlevel;
	s8 maxlevel;
	s8 minlevel;
} __attribute__ ((packed));

struct cmd_ds_802_11_rf_antenna {
	__le16 action;

	/* Number of antennas or 0xffff(diversity) */
	__le16 antennamode;

} __attribute__ ((packed));

struct cmd_ds_802_11_monitor_mode {
	__le16 action;
	__le16 mode;
} __attribute__ ((packed));

struct cmd_ds_set_boot2_ver {
	struct cmd_header hdr;

	__le16 action;
	__le16 version;
} __attribute__ ((packed));

struct cmd_ds_802_11_fw_wake_method {
	struct cmd_header hdr;

	__le16 action;
	__le16 method;
} __attribute__ ((packed));

struct cmd_ds_802_11_sleep_period {
	struct cmd_header hdr;

	__le16 action;
	__le16 period;
} __attribute__ ((packed));

struct cmd_ds_802_11_ps_mode {
	__le16 action;
	__le16 nullpktinterval;
	__le16 multipledtim;
	__le16 reserved;
	__le16 locallisteninterval;
} __attribute__ ((packed));

struct cmd_confirm_sleep {
	struct cmd_header hdr;

	__le16 action;
	__le16 nullpktinterval;
	__le16 multipledtim;
	__le16 reserved;
	__le16 locallisteninterval;
} __attribute__ ((packed));

struct cmd_ds_802_11_data_rate {
	struct cmd_header hdr;

	__le16 action;
	__le16 reserved;
	u8 rates[MAX_RATES];
} __attribute__ ((packed));

struct cmd_ds_802_11_rate_adapt_rateset {
	struct cmd_header hdr;
	__le16 action;
	__le16 enablehwauto;
	__le16 bitmap;
} __attribute__ ((packed));

struct cmd_ds_802_11_ad_hoc_start {
	struct cmd_header hdr;

	u8 ssid[IW_ESSID_MAX_SIZE];
	u8 bsstype;
	__le16 beaconperiod;
	u8 dtimperiod;   /* Reserved on v9 and later */
	struct ieee_ie_ibss_param_set ibss;
	u8 reserved1[4];
	struct ieee_ie_ds_param_set ds;
	u8 reserved2[4];
	__le16 probedelay;  /* Reserved on v9 and later */
	__le16 capability;
	u8 rates[MAX_RATES];
	u8 tlv_memory_size_pad[100];
} __attribute__ ((packed));

struct cmd_ds_802_11_ad_hoc_result {
	struct cmd_header hdr;

	u8 pad[3];
	u8 bssid[ETH_ALEN];
} __attribute__ ((packed));

struct adhoc_bssdesc {
	u8 bssid[ETH_ALEN];
	u8 ssid[IW_ESSID_MAX_SIZE];
	u8 type;
	__le16 beaconperiod;
	u8 dtimperiod;
	__le64 timestamp;
	__le64 localtime;
	struct ieee_ie_ds_param_set ds;
	u8 reserved1[4];
	struct ieee_ie_ibss_param_set ibss;
	u8 reserved2[4];
	__le16 capability;
	u8 rates[MAX_RATES];

	/* DO NOT ADD ANY FIELDS TO THIS STRUCTURE. It is used below in the
	 * Adhoc join command and will cause a binary layout mismatch with
	 * the firmware
	 */
} __attribute__ ((packed));

struct cmd_ds_802_11_ad_hoc_join {
	struct cmd_header hdr;

	struct adhoc_bssdesc bss;
	__le16 failtimeout;   /* Reserved on v9 and later */
	__le16 probedelay;    /* Reserved on v9 and later */
} __attribute__ ((packed));

struct cmd_ds_802_11_ad_hoc_stop {
	struct cmd_header hdr;
} __attribute__ ((packed));

struct cmd_ds_802_11_enable_rsn {
	struct cmd_header hdr;

	__le16 action;
	__le16 enable;
} __attribute__ ((packed));

struct MrvlIEtype_keyParamSet {
	/* type ID */
	__le16 type;

	/* length of Payload */
	__le16 length;

	/* type of key: WEP=0, TKIP=1, AES=2 */
	__le16 keytypeid;

	/* key control Info specific to a keytypeid */
	__le16 keyinfo;

	/* length of key */
	__le16 keylen;

	/* key material of size keylen */
	u8 key[32];
} __attribute__ ((packed));

#define MAX_WOL_RULES 		16

struct host_wol_rule {
	uint8_t rule_no;
	uint8_t rule_ops;
	__le16 sig_offset;
	__le16 sig_length;
	__le16 reserve;
	__be32 sig_mask;
	__be32 signature;
} __attribute__ ((packed));

struct wol_config {
	uint8_t action;
	uint8_t pattern;
	uint8_t no_rules_in_cmd;
	uint8_t result;
	struct host_wol_rule rule[MAX_WOL_RULES];
} __attribute__ ((packed));

struct cmd_ds_host_sleep {
	struct cmd_header hdr;
	__le32 criteria;
	uint8_t gpio;
	uint16_t gap;
	struct wol_config wol_conf;
} __attribute__ ((packed));



struct cmd_ds_802_11_key_material {
	struct cmd_header hdr;

	__le16 action;
	struct MrvlIEtype_keyParamSet keyParamSet[2];
} __attribute__ ((packed));

struct cmd_ds_802_11_eeprom_access {
	struct cmd_header hdr;
	__le16 action;
	__le16 offset;
	__le16 len;
	/* firmware says it returns a maximum of 20 bytes */
#define LBS_EEPROM_READ_LEN 20
	u8 value[LBS_EEPROM_READ_LEN];
} __attribute__ ((packed));

struct cmd_ds_802_11_tpc_cfg {
	struct cmd_header hdr;

	__le16 action;
	uint8_t enable;
	int8_t P0;
	int8_t P1;
	int8_t P2;
	uint8_t usesnr;
} __attribute__ ((packed));


struct cmd_ds_802_11_pa_cfg {
	struct cmd_header hdr;

	__le16 action;
	uint8_t enable;
	int8_t P0;
	int8_t P1;
	int8_t P2;
} __attribute__ ((packed));


struct cmd_ds_802_11_led_ctrl {
	__le16 action;
	__le16 numled;
	u8 data[256];
} __attribute__ ((packed));

struct cmd_ds_802_11_afc {
	__le16 afc_auto;
	union {
		struct {
			__le16 threshold;
			__le16 period;
		};
		struct {
			__le16 timing_offset; /* signed */
			__le16 carrier_offset; /* signed */
		};
	};
} __attribute__ ((packed));

struct cmd_tx_rate_query {
	__le16 txrate;
} __attribute__ ((packed));

struct cmd_ds_get_tsf {
	__le64 tsfvalue;
} __attribute__ ((packed));

struct cmd_ds_bt_access {
	__le16 action;
	__le32 id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
} __attribute__ ((packed));

struct cmd_ds_fwt_access {
	__le16 action;
	__le32 id;
	u8 valid;
	u8 da[ETH_ALEN];
	u8 dir;
	u8 ra[ETH_ALEN];
	__le32 ssn;
	__le32 dsn;
	__le32 metric;
	u8 rate;
	u8 hopcount;
	u8 ttl;
	__le32 expiration;
	u8 sleepmode;
	__le32 snr;
	__le32 references;
	u8 prec[ETH_ALEN];
} __attribute__ ((packed));


struct cmd_ds_mesh_config {
	struct cmd_header hdr;

        __le16 action;
        __le16 channel;
        __le16 type;
        __le16 length;
        u8 data[128];   /* last position reserved */
} __attribute__ ((packed));


struct cmd_ds_mesh_access {
	struct cmd_header hdr;

	__le16 action;
	__le32 data[32];	/* last position reserved */
} __attribute__ ((packed));

/* Number of stats counters returned by the firmware */
#define MESH_STATS_NUM 8

struct cmd_ds_command {
	/* command header */
	__le16 command;
	__le16 size;
	__le16 seqnum;
	__le16 result;

	/* command Body */
	union {
		struct cmd_ds_802_11_ps_mode psmode;
		struct cmd_ds_802_11_get_stat gstat;
		struct cmd_ds_802_3_get_stat gstat_8023;
		struct cmd_ds_802_11_rf_antenna rant;
		struct cmd_ds_802_11_monitor_mode monitor;
		struct cmd_ds_802_11_rssi rssi;
		struct cmd_ds_802_11_rssi_rsp rssirsp;
		struct cmd_ds_mac_reg_access macreg;
		struct cmd_ds_bbp_reg_access bbpreg;
		struct cmd_ds_rf_reg_access rfreg;

		struct cmd_ds_802_11d_domain_info domaininfo;
		struct cmd_ds_802_11d_domain_info domaininforesp;

		struct cmd_ds_802_11_tpc_cfg tpccfg;
		struct cmd_ds_802_11_afc afc;
		struct cmd_ds_802_11_led_ctrl ledgpio;

		struct cmd_tx_rate_query txrate;
		struct cmd_ds_bt_access bt;
		struct cmd_ds_fwt_access fwt;
		struct cmd_ds_get_tsf gettsf;
		struct cmd_ds_802_11_beacon_control bcn_ctrl;
	} params;
} __attribute__ ((packed));

#endif
