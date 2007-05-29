/*
 * This file contains the function prototypes, data structure
 * and defines for all the host/station commands
 */
#ifndef __HOSTCMD__H
#define __HOSTCMD__H

#include <linux/wireless.h>
#include "11d.h"
#include "types.h"

/* 802.11-related definitions */

/* TxPD descriptor */
struct txpd {
	/* Current Tx packet status */
	__le32 tx_status;
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
};

/* RxPD Descriptor */
struct rxpd {
	/* Current Rx packet status */
	__le16 status;

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
};

struct cmd_ctrl_node {
	/* CMD link list */
	struct list_head list;
	u32 status;
	/* CMD ID */
	u32 cmd_oid;
	/*CMD wait option: wait for finish or no wait */
	u16 wait_option;
	/* command parameter */
	void *pdata_buf;
	/*command data */
	u8 *bufvirtualaddr;
	u16 cmdflags;
	/* wait queue */
	u16 cmdwaitqwoken;
	wait_queue_head_t cmdwait_q;
};

/* WLAN_802_11_KEY
 *
 * Generic structure to hold all key types.  key type (WEP40, WEP104, TKIP, AES)
 * is determined from the keylength field.
 */
struct WLAN_802_11_KEY {
	__le32 len;
	__le32 flags;  /* KEY_INFO_* from wlan_defs.h */
	u8 key[MRVL_MAX_KEY_WPA_KEY_LENGTH];
	__le16 type; /* KEY_TYPE_* from wlan_defs.h */
};

struct IE_WPA {
	u8 elementid;
	u8 len;
	u8 oui[4];
	__le16 version;
};

/* wlan_offset_value */
struct wlan_offset_value {
	u32 offset;
	u32 value;
};

struct WLAN_802_11_FIXED_IEs {
	__le64 timestamp;
	__le16 beaconinterval;
	u16 capabilities; /* Actually struct ieeetypes_capinfo */
};

struct WLAN_802_11_VARIABLE_IEs {
	u8 elementid;
	u8 length;
	u8 data[1];
};

/* Define general data structure */
/* cmd_DS_GEN */
struct cmd_ds_gen {
	__le16 command;
	__le16 size;
	__le16 seqnum;
	__le16 result;
};

#define S_DS_GEN sizeof(struct cmd_ds_gen)
/*
 * Define data structure for cmd_get_hw_spec
 * This structure defines the response for the GET_HW_SPEC command
 */
struct cmd_ds_get_hw_spec {
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

	/* FW release number, example 1,2,3,4 = 3.2.1p4 */
	u8 fwreleasenumber[4];

	/* Base Address of TxPD queue */
	__le32 wcb_base;
	/* Read Pointer of RxPd queue */
	__le32 rxpd_rdptr;

	/* Write Pointer of RxPd queue */
	__le32 rxpd_wrptr;

	/*FW/HW capability */
	__le32 fwcapinfo;
} __attribute__ ((packed));

struct cmd_ds_802_11_reset {
	__le16 action;
};

struct cmd_ds_802_11_subscribe_event {
	__le16 action;
	__le16 events;
};

/*
 * This scan handle Country Information IE(802.11d compliant)
 * Define data structure for cmd_802_11_scan
 */
struct cmd_ds_802_11_scan {
	u8 bsstype;
	u8 BSSID[ETH_ALEN];
	u8 tlvbuffer[1];
#if 0
	mrvlietypes_ssidparamset_t ssidParamSet;
	mrvlietypes_chanlistparamset_t ChanListParamSet;
	mrvlietypes_ratesparamset_t OpRateSet;
#endif
};

struct cmd_ds_802_11_scan_rsp {
	__le16 bssdescriptsize;
	u8 nr_sets;
	u8 bssdesc_and_tlvbuffer[1];
};

struct cmd_ds_802_11_get_log {
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
};

struct cmd_ds_mac_control {
	__le16 action;
	__le16 reserved;
};

struct cmd_ds_mac_multicast_adr {
	__le16 action;
	__le16 nr_of_adrs;
	u8 maclist[ETH_ALEN * MRVDRV_MAX_MULTICAST_LIST_SIZE];
};

struct cmd_ds_802_11_authenticate {
	u8 macaddr[ETH_ALEN];
	u8 authtype;
	u8 reserved[10];
};

struct cmd_ds_802_11_deauthenticate {
	u8 macaddr[6];
	__le16 reasoncode;
};

struct cmd_ds_802_11_associate {
	u8 peerstaaddr[6];
	struct ieeetypes_capinfo capinfo;
	__le16 listeninterval;
	__le16 bcnperiod;
	u8 dtimperiod;

#if 0
	mrvlietypes_ssidparamset_t ssidParamSet;
	mrvlietypes_phyparamset_t phyparamset;
	mrvlietypes_ssparamset_t ssparamset;
	mrvlietypes_ratesparamset_t ratesParamSet;
#endif
} __attribute__ ((packed));

struct cmd_ds_802_11_disassociate {
	u8 destmacaddr[6];
	__le16 reasoncode;
};

struct cmd_ds_802_11_associate_rsp {
	struct ieeetypes_assocrsp assocRsp;
};

struct cmd_ds_802_11_ad_hoc_result {
	u8 PAD[3];
	u8 BSSID[ETH_ALEN];
};

struct cmd_ds_802_11_set_wep {
	/* ACT_ADD, ACT_REMOVE or ACT_ENABLE */
	__le16 action;

	/* key Index selected for Tx */
	__le16 keyindex;

	/* 40, 128bit or TXWEP */
	u8 keytype[4];
	u8 keymaterial[4][16];
};

struct cmd_ds_802_3_get_stat {
	__le32 xmitok;
	__le32 rcvok;
	__le32 xmiterror;
	__le32 rcverror;
	__le32 rcvnobuffer;
	__le32 rcvcrcerror;
};

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
};

struct cmd_ds_802_11_snmp_mib {
	__le16 querytype;
	__le16 oid;
	__le16 bufsize;
	u8 value[128];
};

struct cmd_ds_mac_reg_map {
	__le16 buffersize;
	u8 regmap[128];
	__le16 reserved;
};

struct cmd_ds_bbp_reg_map {
	__le16 buffersize;
	u8 regmap[128];
	__le16 reserved;
};

struct cmd_ds_rf_reg_map {
	__le16 buffersize;
	u8 regmap[64];
	__le16 reserved;
};

struct cmd_ds_mac_reg_access {
	__le16 action;
	__le16 offset;
	__le32 value;
};

struct cmd_ds_bbp_reg_access {
	__le16 action;
	__le16 offset;
	u8 value;
	u8 reserved[3];
};

struct cmd_ds_rf_reg_access {
	__le16 action;
	__le16 offset;
	u8 value;
	u8 reserved[3];
};

struct cmd_ds_802_11_radio_control {
	__le16 action;
	__le16 control;
};

struct cmd_ds_802_11_sleep_params {
	/* ACT_GET/ACT_SET */
	__le16 action;

	/* Sleep clock error in ppm */
	__le16 error;

	/* Wakeup offset in usec */
	__le16 offset;

	/* Clock stabilization time in usec */
	__le16 stabletime;

	/* control periodic calibration */
	u8 calcontrol;

	/* control the use of external sleep clock */
	u8 externalsleepclk;

	/* reserved field, should be set to zero */
	__le16 reserved;
};

struct cmd_ds_802_11_inactivity_timeout {
	/* ACT_GET/ACT_SET */
	__le16 action;

	/* Inactivity timeout in msec */
	__le16 timeout;
};

struct cmd_ds_802_11_rf_channel {
	__le16 action;
	__le16 currentchannel;
	__le16 rftype;
	__le16 reserved;
	u8 channellist[32];
};

struct cmd_ds_802_11_rssi {
	/* weighting factor */
	__le16 N;

	__le16 reserved_0;
	__le16 reserved_1;
	__le16 reserved_2;
};

struct cmd_ds_802_11_rssi_rsp {
	__le16 SNR;
	__le16 noisefloor;
	__le16 avgSNR;
	__le16 avgnoisefloor;
};

struct cmd_ds_802_11_mac_address {
	__le16 action;
	u8 macadd[ETH_ALEN];
};

struct cmd_ds_802_11_rf_tx_power {
	__le16 action;
	__le16 currentlevel;
};

struct cmd_ds_802_11_rf_antenna {
	__le16 action;

	/* Number of antennas or 0xffff(diversity) */
	__le16 antennamode;

};

struct cmd_ds_802_11_ps_mode {
	__le16 action;
	__le16 nullpktinterval;
	__le16 multipledtim;
	__le16 reserved;
	__le16 locallisteninterval;
};

struct PS_CMD_ConfirmSleep {
	__le16 command;
	__le16 size;
	__le16 seqnum;
	__le16 result;

	__le16 action;
	__le16 reserved1;
	__le16 multipledtim;
	__le16 reserved;
	__le16 locallisteninterval;
};

struct cmd_ds_802_11_data_rate {
	__le16 action;
	__le16 reserverd;
	u8 datarate[G_SUPPORTED_RATES];
};

struct cmd_ds_802_11_rate_adapt_rateset {
	__le16 action;
	__le16 enablehwauto;
	__le16 bitmap;
};

struct cmd_ds_802_11_ad_hoc_start {
	u8 SSID[IW_ESSID_MAX_SIZE];
	u8 bsstype;
	__le16 beaconperiod;
	u8 dtimperiod;
	union IEEEtypes_ssparamset ssparamset;
	union ieeetypes_phyparamset phyparamset;
	__le16 probedelay;
	struct ieeetypes_capinfo cap;
	u8 datarate[G_SUPPORTED_RATES];
	u8 tlv_memory_size_pad[100];
} __attribute__ ((packed));

struct adhoc_bssdesc {
	u8 BSSID[6];
	u8 SSID[32];
	u8 bsstype;
	__le16 beaconperiod;
	u8 dtimperiod;
	__le64 timestamp;
	__le64 localtime;
	union ieeetypes_phyparamset phyparamset;
	union IEEEtypes_ssparamset ssparamset;
	struct ieeetypes_capinfo cap;
	u8 datarates[G_SUPPORTED_RATES];

	/* DO NOT ADD ANY FIELDS TO THIS STRUCTURE. It is used below in the
	 * Adhoc join command and will cause a binary layout mismatch with
	 * the firmware
	 */
} __attribute__ ((packed));

struct cmd_ds_802_11_ad_hoc_join {
	struct adhoc_bssdesc bssdescriptor;
	__le16 failtimeout;
	__le16 probedelay;

} __attribute__ ((packed));

struct cmd_ds_802_11_enable_rsn {
	__le16 action;
	__le16 enable;
};

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
};

struct cmd_ds_802_11_key_material {
	__le16 action;
	struct MrvlIEtype_keyParamSet keyParamSet[2];
} __attribute__ ((packed));

struct cmd_ds_802_11_eeprom_access {
	__le16 action;

	/* multiple 4 */
	__le16 offset;
	__le16 bytecount;
	u8 value;
} __attribute__ ((packed));

struct cmd_ds_802_11_tpc_cfg {
	__le16 action;
	u8 enable;
	s8 P0;
	s8 P1;
	s8 P2;
	u8 usesnr;
} __attribute__ ((packed));

struct cmd_ds_802_11_led_ctrl {
	__le16 action;
	__le16 numled;
	u8 data[256];
} __attribute__ ((packed));

struct cmd_ds_802_11_pwr_cfg {
	__le16 action;
	u8 enable;
	s8 PA_P0;
	s8 PA_P1;
	s8 PA_P2;
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

struct cmd_ds_mesh_access {
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
		struct cmd_ds_get_hw_spec hwspec;
		struct cmd_ds_802_11_ps_mode psmode;
		struct cmd_ds_802_11_scan scan;
		struct cmd_ds_802_11_scan_rsp scanresp;
		struct cmd_ds_mac_control macctrl;
		struct cmd_ds_802_11_associate associate;
		struct cmd_ds_802_11_deauthenticate deauth;
		struct cmd_ds_802_11_set_wep wep;
		struct cmd_ds_802_11_ad_hoc_start ads;
		struct cmd_ds_802_11_reset reset;
		struct cmd_ds_802_11_ad_hoc_result result;
		struct cmd_ds_802_11_get_log glog;
		struct cmd_ds_802_11_authenticate auth;
		struct cmd_ds_802_11_get_stat gstat;
		struct cmd_ds_802_3_get_stat gstat_8023;
		struct cmd_ds_802_11_snmp_mib smib;
		struct cmd_ds_802_11_rf_tx_power txp;
		struct cmd_ds_802_11_rf_antenna rant;
		struct cmd_ds_802_11_data_rate drate;
		struct cmd_ds_802_11_rate_adapt_rateset rateset;
		struct cmd_ds_mac_multicast_adr madr;
		struct cmd_ds_802_11_ad_hoc_join adj;
		struct cmd_ds_802_11_radio_control radio;
		struct cmd_ds_802_11_rf_channel rfchannel;
		struct cmd_ds_802_11_rssi rssi;
		struct cmd_ds_802_11_rssi_rsp rssirsp;
		struct cmd_ds_802_11_disassociate dassociate;
		struct cmd_ds_802_11_mac_address macadd;
		struct cmd_ds_802_11_enable_rsn enbrsn;
		struct cmd_ds_802_11_key_material keymaterial;
		struct cmd_ds_mac_reg_access macreg;
		struct cmd_ds_bbp_reg_access bbpreg;
		struct cmd_ds_rf_reg_access rfreg;
		struct cmd_ds_802_11_eeprom_access rdeeprom;

		struct cmd_ds_802_11d_domain_info domaininfo;
		struct cmd_ds_802_11d_domain_info domaininforesp;

		struct cmd_ds_802_11_sleep_params sleep_params;
		struct cmd_ds_802_11_inactivity_timeout inactivity_timeout;
		struct cmd_ds_802_11_tpc_cfg tpccfg;
		struct cmd_ds_802_11_pwr_cfg pwrcfg;
		struct cmd_ds_802_11_afc afc;
		struct cmd_ds_802_11_led_ctrl ledgpio;

		struct cmd_tx_rate_query txrate;
		struct cmd_ds_bt_access bt;
		struct cmd_ds_fwt_access fwt;
		struct cmd_ds_mesh_access mesh;
		struct cmd_ds_get_tsf gettsf;
		struct cmd_ds_802_11_subscribe_event subscribe_event;
	} params;
} __attribute__ ((packed));

#endif
