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
	u32 tx_status;
	/* Tx control */
	u32 tx_control;
	u32 tx_packet_location;
	/* Tx packet length */
	u16 tx_packet_length;
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
	u16 status;

	/* SNR */
	u8 snr;

	/* Tx control */
	u8 rx_control;

	/* Pkt length */
	u16 pkt_len;

	/* Noise Floor */
	u8 nf;

	/* Rx Packet Rate */
	u8 rx_rate;

	/* Pkt addr */
	u32 pkt_ptr;

	/* Next Rx RxPD addr */
	u32 next_rxpd_ptr;

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
	u32 len;
	u32 flags;  /* KEY_INFO_* from wlan_defs.h */
	u8 key[MRVL_MAX_KEY_WPA_KEY_LENGTH];
	u16 type; /* KEY_TYPE_* from wlan_defs.h */
};

struct IE_WPA {
	u8 elementid;
	u8 len;
	u8 oui[4];
	u16 version;
};

struct WLAN_802_11_SSID {
	/* SSID length */
	u32 ssidlength;

	/* SSID information field */
	u8 ssid[IW_ESSID_MAX_SIZE];
};

struct WPA_SUPPLICANT {
	u8 wpa_ie[256];
	u8 wpa_ie_len;
};

/* wlan_offset_value */
struct wlan_offset_value {
	u32 offset;
	u32 value;
};

struct WLAN_802_11_FIXED_IEs {
	u8 timestamp[8];
	u16 beaconinterval;
	u16 capabilities;
};

struct WLAN_802_11_VARIABLE_IEs {
	u8 elementid;
	u8 length;
	u8 data[1];
};

/* Define general data structure */
/* cmd_DS_GEN */
struct cmd_ds_gen {
	u16 command;
	u16 size;
	u16 seqnum;
	u16 result;
};

#define S_DS_GEN sizeof(struct cmd_ds_gen)
/*
 * Define data structure for cmd_get_hw_spec
 * This structure defines the response for the GET_HW_SPEC command
 */
struct cmd_ds_get_hw_spec {
	/* HW Interface version number */
	u16 hwifversion;
	/* HW version number */
	u16 version;
	/* Max number of TxPD FW can handle */
	u16 nr_txpd;
	/* Max no of Multicast address */
	u16 nr_mcast_adr;
	/* MAC address */
	u8 permanentaddr[6];

	/* region Code */
	u16 regioncode;

	/* Number of antenna used */
	u16 nr_antenna;

	/* FW release number, example 0x1234=1.2.3.4 */
	u32 fwreleasenumber;

	/* Base Address of TxPD queue */
	u32 wcb_base;
	/* Read Pointer of RxPd queue */
	u32 rxpd_rdptr;

	/* Write Pointer of RxPd queue */
	u32 rxpd_wrptr;

	/*FW/HW capability */
	u32 fwcapinfo;
} __attribute__ ((packed));

struct cmd_ds_802_11_reset {
	u16 action;
};

struct cmd_ds_802_11_subscribe_event {
	u16 action;
	u16 events;
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
	u16 bssdescriptsize;
	u8 nr_sets;
	u8 bssdesc_and_tlvbuffer[1];
};

struct cmd_ds_802_11_get_log {
	u32 mcasttxframe;
	u32 failed;
	u32 retry;
	u32 multiretry;
	u32 framedup;
	u32 rtssuccess;
	u32 rtsfailure;
	u32 ackfailure;
	u32 rxfrag;
	u32 mcastrxframe;
	u32 fcserror;
	u32 txframe;
	u32 wepundecryptable;
};

struct cmd_ds_mac_control {
	u16 action;
	u16 reserved;
};

struct cmd_ds_mac_multicast_adr {
	u16 action;
	u16 nr_of_adrs;
	u8 maclist[ETH_ALEN * MRVDRV_MAX_MULTICAST_LIST_SIZE];
};

struct cmd_ds_802_11_authenticate {
	u8 macaddr[ETH_ALEN];
	u8 authtype;
	u8 reserved[10];
};

struct cmd_ds_802_11_deauthenticate {
	u8 macaddr[6];
	u16 reasoncode;
};

struct cmd_ds_802_11_associate {
	u8 peerstaaddr[6];
	struct ieeetypes_capinfo capinfo;
	u16 listeninterval;
	u16 bcnperiod;
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
	u16 reasoncode;
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
	u16 action;

	/* key Index selected for Tx */
	u16 keyindex;

	/* 40, 128bit or TXWEP */
	u8 keytype[4];
	u8 keymaterial[4][16];
};

struct cmd_ds_802_3_get_stat {
	u32 xmitok;
	u32 rcvok;
	u32 xmiterror;
	u32 rcverror;
	u32 rcvnobuffer;
	u32 rcvcrcerror;
};

struct cmd_ds_802_11_get_stat {
	u32 txfragmentcnt;
	u32 mcasttxframecnt;
	u32 failedcnt;
	u32 retrycnt;
	u32 Multipleretrycnt;
	u32 rtssuccesscnt;
	u32 rtsfailurecnt;
	u32 ackfailurecnt;
	u32 frameduplicatecnt;
	u32 rxfragmentcnt;
	u32 mcastrxframecnt;
	u32 fcserrorcnt;
	u32 bcasttxframecnt;
	u32 bcastrxframecnt;
	u32 txbeacon;
	u32 rxbeacon;
	u32 wepundecryptable;
};

struct cmd_ds_802_11_snmp_mib {
	u16 querytype;
	u16 oid;
	u16 bufsize;
	u8 value[128];
};

struct cmd_ds_mac_reg_map {
	u16 buffersize;
	u8 regmap[128];
	u16 reserved;
};

struct cmd_ds_bbp_reg_map {
	u16 buffersize;
	u8 regmap[128];
	u16 reserved;
};

struct cmd_ds_rf_reg_map {
	u16 buffersize;
	u8 regmap[64];
	u16 reserved;
};

struct cmd_ds_mac_reg_access {
	u16 action;
	u16 offset;
	u32 value;
};

struct cmd_ds_bbp_reg_access {
	u16 action;
	u16 offset;
	u8 value;
	u8 reserved[3];
};

struct cmd_ds_rf_reg_access {
	u16 action;
	u16 offset;
	u8 value;
	u8 reserved[3];
};

struct cmd_ds_802_11_radio_control {
	u16 action;
	u16 control;
};

struct cmd_ds_802_11_sleep_params {
	/* ACT_GET/ACT_SET */
	u16 action;

	/* Sleep clock error in ppm */
	u16 error;

	/* Wakeup offset in usec */
	u16 offset;

	/* Clock stabilization time in usec */
	u16 stabletime;

	/* control periodic calibration */
	u8 calcontrol;

	/* control the use of external sleep clock */
	u8 externalsleepclk;

	/* reserved field, should be set to zero */
	u16 reserved;
};

struct cmd_ds_802_11_inactivity_timeout {
	/* ACT_GET/ACT_SET */
	u16 action;

	/* Inactivity timeout in msec */
	u16 timeout;
};

struct cmd_ds_802_11_rf_channel {
	u16 action;
	u16 currentchannel;
	u16 rftype;
	u16 reserved;
	u8 channellist[32];
};

struct cmd_ds_802_11_rssi {
	/* weighting factor */
	u16 N;

	u16 reserved_0;
	u16 reserved_1;
	u16 reserved_2;
};

struct cmd_ds_802_11_rssi_rsp {
	u16 SNR;
	u16 noisefloor;
	u16 avgSNR;
	u16 avgnoisefloor;
};

struct cmd_ds_802_11_mac_address {
	u16 action;
	u8 macadd[ETH_ALEN];
};

struct cmd_ds_802_11_rf_tx_power {
	u16 action;
	u16 currentlevel;
};

struct cmd_ds_802_11_rf_antenna {
	u16 action;

	/* Number of antennas or 0xffff(diversity) */
	u16 antennamode;

};

struct cmd_ds_802_11_ps_mode {
	u16 action;
	u16 nullpktinterval;
	u16 multipledtim;
	u16 reserved;
	u16 locallisteninterval;
};

struct PS_CMD_ConfirmSleep {
	u16 command;
	u16 size;
	u16 seqnum;
	u16 result;

	u16 action;
	u16 reserved1;
	u16 multipledtim;
	u16 reserved;
	u16 locallisteninterval;
};

struct cmd_ds_802_11_data_rate {
	u16 action;
	u16 reserverd;
	u8 datarate[G_SUPPORTED_RATES];
};

struct cmd_ds_802_11_rate_adapt_rateset {
	u16 action;
	u16 enablehwauto;
	u16 bitmap;
};

struct cmd_ds_802_11_ad_hoc_start {
	u8 SSID[IW_ESSID_MAX_SIZE];
	u8 bsstype;
	u16 beaconperiod;
	u8 dtimperiod;
	union IEEEtypes_ssparamset ssparamset;
	union ieeetypes_phyparamset phyparamset;
	u16 probedelay;
	struct ieeetypes_capinfo cap;
	u8 datarate[G_SUPPORTED_RATES];
	u8 tlv_memory_size_pad[100];
} __attribute__ ((packed));

struct adhoc_bssdesc {
	u8 BSSID[6];
	u8 SSID[32];
	u8 bsstype;
	u16 beaconperiod;
	u8 dtimperiod;
	u8 timestamp[8];
	u8 localtime[8];
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
	u16 failtimeout;
	u16 probedelay;

} __attribute__ ((packed));

struct cmd_ds_802_11_enable_rsn {
	u16 action;
	u16 enable;
};

struct MrvlIEtype_keyParamSet {
	/* type ID */
	u16 type;

	/* length of Payload */
	u16 length;

	/* type of key: WEP=0, TKIP=1, AES=2 */
	u16 keytypeid;

	/* key control Info specific to a keytypeid */
	u16 keyinfo;

	/* length of key */
	u16 keylen;

	/* key material of size keylen */
	u8 key[32];
};

struct cmd_ds_802_11_key_material {
	u16 action;
	struct MrvlIEtype_keyParamSet keyParamSet[2];
} __attribute__ ((packed));

struct cmd_ds_802_11_eeprom_access {
	u16 action;

	/* multiple 4 */
	u16 offset;
	u16 bytecount;
	u8 value;
} __attribute__ ((packed));

struct cmd_ds_802_11_tpc_cfg {
	u16 action;
	u8 enable;
	s8 P0;
	s8 P1;
	s8 P2;
	u8 usesnr;
} __attribute__ ((packed));

struct cmd_ds_802_11_led_ctrl {
	u16 action;
	u16 numled;
	u8 data[256];
} __attribute__ ((packed));

struct cmd_ds_802_11_pwr_cfg {
	u16 action;
	u8 enable;
	s8 PA_P0;
	s8 PA_P1;
	s8 PA_P2;
} __attribute__ ((packed));

struct cmd_ds_802_11_afc {
	u16 afc_auto;
	union {
		struct {
			u16 threshold;
			u16 period;
		};
		struct {
			s16 timing_offset;
			s16 carrier_offset;
		};
	};
} __attribute__ ((packed));

struct cmd_tx_rate_query {
	u16 txrate;
} __attribute__ ((packed));

struct cmd_ds_get_tsf {
	__le64 tsfvalue;
} __attribute__ ((packed));

struct cmd_ds_bt_access {
	u16 action;
	u32 id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
} __attribute__ ((packed));

struct cmd_ds_fwt_access {
	u16 action;
	u32 id;
	u8 da[ETH_ALEN];
	u8 dir;
	u8 ra[ETH_ALEN];
	u32 ssn;
	u32 dsn;
	u32 metric;
	u8 hopcount;
	u8 ttl;
	u32 expiration;
	u8 sleepmode;
	u32 snr;
	u32 references;
} __attribute__ ((packed));

#define MESH_STATS_NUM 7
struct cmd_ds_mesh_access {
	u16 action;
	u32 data[MESH_STATS_NUM + 1];	/* last position reserved */
} __attribute__ ((packed));

struct cmd_ds_command {
	/* command header */
	u16 command;
	u16 size;
	u16 seqnum;
	u16 result;

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
