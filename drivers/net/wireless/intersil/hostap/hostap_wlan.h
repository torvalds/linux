/* SPDX-License-Identifier: GPL-2.0 */
#ifndef HOSTAP_WLAN_H
#define HOSTAP_WLAN_H

#include <linux/interrupt.h>
#include <linux/wireless.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <net/iw_handler.h>
#include <net/ieee80211_radiotap.h>
#include <net/lib80211.h>

#include "hostap_config.h"
#include "hostap_common.h"

#define MAX_PARM_DEVICES 8
#define PARM_MIN_MAX "1-" __MODULE_STRING(MAX_PARM_DEVICES)
#define DEF_INTS -1, -1, -1, -1, -1, -1, -1
#define GET_INT_PARM(var,idx) var[var[idx] < 0 ? 0 : idx]


/* Specific skb->protocol value that indicates that the packet already contains
 * txdesc header.
 * FIX: This might need own value that would be allocated especially for Prism2
 * txdesc; ETH_P_CONTROL is commented as "Card specific control frames".
 * However, these skb's should have only minimal path in the kernel side since
 * prism2_send_mgmt() sends these with dev_queue_xmit() to prism2_tx(). */
#define ETH_P_HOSTAP ETH_P_CONTROL

/* ARPHRD_IEEE80211_PRISM uses a bloated version of Prism2 RX frame header
 * (from linux-wlan-ng) */
struct linux_wlan_ng_val {
	u32 did;
	u16 status, len;
	u32 data;
} __packed;

struct linux_wlan_ng_prism_hdr {
	u32 msgcode, msglen;
	char devname[16];
	struct linux_wlan_ng_val hosttime, mactime, channel, rssi, sq, signal,
		noise, rate, istx, frmlen;
} __packed;

struct linux_wlan_ng_cap_hdr {
	__be32 version;
	__be32 length;
	__be64 mactime;
	__be64 hosttime;
	__be32 phytype;
	__be32 channel;
	__be32 datarate;
	__be32 antenna;
	__be32 priority;
	__be32 ssi_type;
	__be32 ssi_signal;
	__be32 ssi_noise;
	__be32 preamble;
	__be32 encoding;
} __packed;

struct hostap_radiotap_rx {
	struct ieee80211_radiotap_header hdr;
	__le64 tsft;
	u8 rate;
	u8 padding;
	__le16 chan_freq;
	__le16 chan_flags;
	s8 dbm_antsignal;
	s8 dbm_antnoise;
} __packed;

#define LWNG_CAP_DID_BASE   (4 | (1 << 6)) /* section 4, group 1 */
#define LWNG_CAPHDR_VERSION 0x80211001

struct hfa384x_rx_frame {
	/* HFA384X RX frame descriptor */
	__le16 status; /* HFA384X_RX_STATUS_ flags */
	__le32 time; /* timestamp, 1 microsecond resolution */
	u8 silence; /* 27 .. 154; seems to be 0 */
	u8 signal; /* 27 .. 154 */
	u8 rate; /* 10, 20, 55, or 110 */
	u8 rxflow;
	__le32 reserved;

	/* 802.11 */
	__le16 frame_control;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctrl;
	u8 addr4[ETH_ALEN];
	__le16 data_len;

	/* 802.3 */
	u8 dst_addr[ETH_ALEN];
	u8 src_addr[ETH_ALEN];
	__be16 len;

	/* followed by frame data; max 2304 bytes */
} __packed;


struct hfa384x_tx_frame {
	/* HFA384X TX frame descriptor */
	__le16 status; /* HFA384X_TX_STATUS_ flags */
	__le16 reserved1;
	__le16 reserved2;
	__le32 sw_support;
	u8 retry_count; /* not yet implemented */
	u8 tx_rate; /* Host AP only; 0 = firmware, or 10, 20, 55, 110 */
	__le16 tx_control; /* HFA384X_TX_CTRL_ flags */

	/* 802.11 */
	struct_group(header,
		__le16 frame_control; /* parts not used */
		__le16 duration_id;
		u8 addr1[ETH_ALEN];
		u8 addr2[ETH_ALEN]; /* filled by firmware */
		u8 addr3[ETH_ALEN];
		__le16 seq_ctrl; /* filled by firmware */
	);
	u8 addr4[ETH_ALEN];
	__le16 data_len;

	/* 802.3 */
	u8 dst_addr[ETH_ALEN];
	u8 src_addr[ETH_ALEN];
	__be16 len;

	/* followed by frame data; max 2304 bytes */
} __packed;


struct hfa384x_rid_hdr
{
	__le16 len;
	__le16 rid;
} __packed;


/* Macro for converting signal levels (range 27 .. 154) to wireless ext
 * dBm value with some accuracy */
#define HFA384X_LEVEL_TO_dBm(v) 0x100 + (v) * 100 / 255 - 100

#define HFA384X_LEVEL_TO_dBm_sign(v) (v) * 100 / 255 - 100

struct hfa384x_scan_request {
	__le16 channel_list;
	__le16 txrate; /* HFA384X_RATES_* */
} __packed;

struct hfa384x_hostscan_request {
	__le16 channel_list;
	__le16 txrate;
	__le16 target_ssid_len;
	u8 target_ssid[32];
} __packed;

struct hfa384x_join_request {
	u8 bssid[ETH_ALEN];
	__le16 channel;
} __packed;

struct hfa384x_info_frame {
	__le16 len;
	__le16 type;
} __packed;

struct hfa384x_comm_tallies {
	__le16 tx_unicast_frames;
	__le16 tx_multicast_frames;
	__le16 tx_fragments;
	__le16 tx_unicast_octets;
	__le16 tx_multicast_octets;
	__le16 tx_deferred_transmissions;
	__le16 tx_single_retry_frames;
	__le16 tx_multiple_retry_frames;
	__le16 tx_retry_limit_exceeded;
	__le16 tx_discards;
	__le16 rx_unicast_frames;
	__le16 rx_multicast_frames;
	__le16 rx_fragments;
	__le16 rx_unicast_octets;
	__le16 rx_multicast_octets;
	__le16 rx_fcs_errors;
	__le16 rx_discards_no_buffer;
	__le16 tx_discards_wrong_sa;
	__le16 rx_discards_wep_undecryptable;
	__le16 rx_message_in_msg_fragments;
	__le16 rx_message_in_bad_msg_fragments;
} __packed;

struct hfa384x_comm_tallies32 {
	__le32 tx_unicast_frames;
	__le32 tx_multicast_frames;
	__le32 tx_fragments;
	__le32 tx_unicast_octets;
	__le32 tx_multicast_octets;
	__le32 tx_deferred_transmissions;
	__le32 tx_single_retry_frames;
	__le32 tx_multiple_retry_frames;
	__le32 tx_retry_limit_exceeded;
	__le32 tx_discards;
	__le32 rx_unicast_frames;
	__le32 rx_multicast_frames;
	__le32 rx_fragments;
	__le32 rx_unicast_octets;
	__le32 rx_multicast_octets;
	__le32 rx_fcs_errors;
	__le32 rx_discards_no_buffer;
	__le32 tx_discards_wrong_sa;
	__le32 rx_discards_wep_undecryptable;
	__le32 rx_message_in_msg_fragments;
	__le32 rx_message_in_bad_msg_fragments;
} __packed;

struct hfa384x_scan_result_hdr {
	__le16 reserved;
	__le16 scan_reason;
#define HFA384X_SCAN_IN_PROGRESS 0 /* no results available yet */
#define HFA384X_SCAN_HOST_INITIATED 1
#define HFA384X_SCAN_FIRMWARE_INITIATED 2
#define HFA384X_SCAN_INQUIRY_FROM_HOST 3
} __packed;

#define HFA384X_SCAN_MAX_RESULTS 32

struct hfa384x_scan_result {
	__le16 chid;
	__le16 anl;
	__le16 sl;
	u8 bssid[ETH_ALEN];
	__le16 beacon_interval;
	__le16 capability;
	__le16 ssid_len;
	u8 ssid[32];
	u8 sup_rates[10];
	__le16 rate;
} __packed;

struct hfa384x_hostscan_result {
	__le16 chid;
	__le16 anl;
	__le16 sl;
	u8 bssid[ETH_ALEN];
	__le16 beacon_interval;
	__le16 capability;
	__le16 ssid_len;
	u8 ssid[32];
	u8 sup_rates[10];
	__le16 rate;
	__le16 atim;
} __packed;

struct comm_tallies_sums {
	unsigned int tx_unicast_frames;
	unsigned int tx_multicast_frames;
	unsigned int tx_fragments;
	unsigned int tx_unicast_octets;
	unsigned int tx_multicast_octets;
	unsigned int tx_deferred_transmissions;
	unsigned int tx_single_retry_frames;
	unsigned int tx_multiple_retry_frames;
	unsigned int tx_retry_limit_exceeded;
	unsigned int tx_discards;
	unsigned int rx_unicast_frames;
	unsigned int rx_multicast_frames;
	unsigned int rx_fragments;
	unsigned int rx_unicast_octets;
	unsigned int rx_multicast_octets;
	unsigned int rx_fcs_errors;
	unsigned int rx_discards_no_buffer;
	unsigned int tx_discards_wrong_sa;
	unsigned int rx_discards_wep_undecryptable;
	unsigned int rx_message_in_msg_fragments;
	unsigned int rx_message_in_bad_msg_fragments;
};


struct hfa384x_regs {
	u16 cmd;
	u16 evstat;
	u16 offset0;
	u16 offset1;
	u16 swsupport0;
};


#if defined(PRISM2_PCCARD) || defined(PRISM2_PLX)
/* I/O ports for HFA384X Controller access */
#define HFA384X_CMD_OFF 0x00
#define HFA384X_PARAM0_OFF 0x02
#define HFA384X_PARAM1_OFF 0x04
#define HFA384X_PARAM2_OFF 0x06
#define HFA384X_STATUS_OFF 0x08
#define HFA384X_RESP0_OFF 0x0A
#define HFA384X_RESP1_OFF 0x0C
#define HFA384X_RESP2_OFF 0x0E
#define HFA384X_INFOFID_OFF 0x10
#define HFA384X_CONTROL_OFF 0x14
#define HFA384X_SELECT0_OFF 0x18
#define HFA384X_SELECT1_OFF 0x1A
#define HFA384X_OFFSET0_OFF 0x1C
#define HFA384X_OFFSET1_OFF 0x1E
#define HFA384X_RXFID_OFF 0x20
#define HFA384X_ALLOCFID_OFF 0x22
#define HFA384X_TXCOMPLFID_OFF 0x24
#define HFA384X_SWSUPPORT0_OFF 0x28
#define HFA384X_SWSUPPORT1_OFF 0x2A
#define HFA384X_SWSUPPORT2_OFF 0x2C
#define HFA384X_EVSTAT_OFF 0x30
#define HFA384X_INTEN_OFF 0x32
#define HFA384X_EVACK_OFF 0x34
#define HFA384X_DATA0_OFF 0x36
#define HFA384X_DATA1_OFF 0x38
#define HFA384X_AUXPAGE_OFF 0x3A
#define HFA384X_AUXOFFSET_OFF 0x3C
#define HFA384X_AUXDATA_OFF 0x3E
#endif /* PRISM2_PCCARD || PRISM2_PLX */

#ifdef PRISM2_PCI
/* Memory addresses for ISL3874 controller access */
#define HFA384X_CMD_OFF 0x00
#define HFA384X_PARAM0_OFF 0x04
#define HFA384X_PARAM1_OFF 0x08
#define HFA384X_PARAM2_OFF 0x0C
#define HFA384X_STATUS_OFF 0x10
#define HFA384X_RESP0_OFF 0x14
#define HFA384X_RESP1_OFF 0x18
#define HFA384X_RESP2_OFF 0x1C
#define HFA384X_INFOFID_OFF 0x20
#define HFA384X_CONTROL_OFF 0x28
#define HFA384X_SELECT0_OFF 0x30
#define HFA384X_SELECT1_OFF 0x34
#define HFA384X_OFFSET0_OFF 0x38
#define HFA384X_OFFSET1_OFF 0x3C
#define HFA384X_RXFID_OFF 0x40
#define HFA384X_ALLOCFID_OFF 0x44
#define HFA384X_TXCOMPLFID_OFF 0x48
#define HFA384X_PCICOR_OFF 0x4C
#define HFA384X_SWSUPPORT0_OFF 0x50
#define HFA384X_SWSUPPORT1_OFF 0x54
#define HFA384X_SWSUPPORT2_OFF 0x58
#define HFA384X_PCIHCR_OFF 0x5C
#define HFA384X_EVSTAT_OFF 0x60
#define HFA384X_INTEN_OFF 0x64
#define HFA384X_EVACK_OFF 0x68
#define HFA384X_DATA0_OFF 0x6C
#define HFA384X_DATA1_OFF 0x70
#define HFA384X_AUXPAGE_OFF 0x74
#define HFA384X_AUXOFFSET_OFF 0x78
#define HFA384X_AUXDATA_OFF 0x7C
#define HFA384X_PCI_M0_ADDRH_OFF 0x80
#define HFA384X_PCI_M0_ADDRL_OFF 0x84
#define HFA384X_PCI_M0_LEN_OFF 0x88
#define HFA384X_PCI_M0_CTL_OFF 0x8C
#define HFA384X_PCI_STATUS_OFF 0x98
#define HFA384X_PCI_M1_ADDRH_OFF 0xA0
#define HFA384X_PCI_M1_ADDRL_OFF 0xA4
#define HFA384X_PCI_M1_LEN_OFF 0xA8
#define HFA384X_PCI_M1_CTL_OFF 0xAC

/* PCI bus master control bits (these are undocumented; based on guessing and
 * experimenting..) */
#define HFA384X_PCI_CTL_FROM_BAP (BIT(5) | BIT(1) | BIT(0))
#define HFA384X_PCI_CTL_TO_BAP (BIT(5) | BIT(0))

#endif /* PRISM2_PCI */


/* Command codes for CMD reg. */
#define HFA384X_CMDCODE_INIT 0x00
#define HFA384X_CMDCODE_ENABLE 0x01
#define HFA384X_CMDCODE_DISABLE 0x02
#define HFA384X_CMDCODE_ALLOC 0x0A
#define HFA384X_CMDCODE_TRANSMIT 0x0B
#define HFA384X_CMDCODE_INQUIRE 0x11
#define HFA384X_CMDCODE_ACCESS 0x21
#define HFA384X_CMDCODE_ACCESS_WRITE (0x21 | BIT(8))
#define HFA384X_CMDCODE_DOWNLOAD 0x22
#define HFA384X_CMDCODE_READMIF 0x30
#define HFA384X_CMDCODE_WRITEMIF 0x31
#define HFA384X_CMDCODE_TEST 0x38

#define HFA384X_CMDCODE_MASK 0x3F

/* Test mode operations */
#define HFA384X_TEST_CHANGE_CHANNEL 0x08
#define HFA384X_TEST_MONITOR 0x0B
#define HFA384X_TEST_STOP 0x0F
#define HFA384X_TEST_CFG_BITS 0x15
#define HFA384X_TEST_CFG_BIT_ALC BIT(3)

#define HFA384X_CMD_BUSY BIT(15)

#define HFA384X_CMD_TX_RECLAIM BIT(8)

#define HFA384X_OFFSET_ERR BIT(14)
#define HFA384X_OFFSET_BUSY BIT(15)


/* ProgMode for download command */
#define HFA384X_PROGMODE_DISABLE 0
#define HFA384X_PROGMODE_ENABLE_VOLATILE 1
#define HFA384X_PROGMODE_ENABLE_NON_VOLATILE 2
#define HFA384X_PROGMODE_PROGRAM_NON_VOLATILE 3

#define HFA384X_AUX_MAGIC0 0xfe01
#define HFA384X_AUX_MAGIC1 0xdc23
#define HFA384X_AUX_MAGIC2 0xba45

#define HFA384X_AUX_PORT_DISABLED 0
#define HFA384X_AUX_PORT_DISABLE BIT(14)
#define HFA384X_AUX_PORT_ENABLE BIT(15)
#define HFA384X_AUX_PORT_ENABLED (BIT(14) | BIT(15))
#define HFA384X_AUX_PORT_MASK (BIT(14) | BIT(15))

#define PRISM2_PDA_SIZE 1024


/* Events; EvStat, Interrupt mask (IntEn), and acknowledge bits (EvAck) */
#define HFA384X_EV_TICK BIT(15)
#define HFA384X_EV_WTERR BIT(14)
#define HFA384X_EV_INFDROP BIT(13)
#ifdef PRISM2_PCI
#define HFA384X_EV_PCI_M1 BIT(9)
#define HFA384X_EV_PCI_M0 BIT(8)
#endif /* PRISM2_PCI */
#define HFA384X_EV_INFO BIT(7)
#define HFA384X_EV_DTIM BIT(5)
#define HFA384X_EV_CMD BIT(4)
#define HFA384X_EV_ALLOC BIT(3)
#define HFA384X_EV_TXEXC BIT(2)
#define HFA384X_EV_TX BIT(1)
#define HFA384X_EV_RX BIT(0)


/* HFA384X Information frames */
#define HFA384X_INFO_HANDOVERADDR 0xF000 /* AP f/w ? */
#define HFA384X_INFO_HANDOVERDEAUTHADDR 0xF001 /* AP f/w 1.3.7 */
#define HFA384X_INFO_COMMTALLIES 0xF100
#define HFA384X_INFO_SCANRESULTS 0xF101
#define HFA384X_INFO_CHANNELINFORESULTS 0xF102 /* AP f/w only */
#define HFA384X_INFO_HOSTSCANRESULTS 0xF103
#define HFA384X_INFO_LINKSTATUS 0xF200
#define HFA384X_INFO_ASSOCSTATUS 0xF201 /* ? */
#define HFA384X_INFO_AUTHREQ 0xF202 /* ? */
#define HFA384X_INFO_PSUSERCNT 0xF203 /* ? */
#define HFA384X_INFO_KEYIDCHANGED 0xF204 /* ? */

enum { HFA384X_LINKSTATUS_CONNECTED = 1,
       HFA384X_LINKSTATUS_DISCONNECTED = 2,
       HFA384X_LINKSTATUS_AP_CHANGE = 3,
       HFA384X_LINKSTATUS_AP_OUT_OF_RANGE = 4,
       HFA384X_LINKSTATUS_AP_IN_RANGE = 5,
       HFA384X_LINKSTATUS_ASSOC_FAILED = 6 };

enum { HFA384X_PORTTYPE_BSS = 1, HFA384X_PORTTYPE_WDS = 2,
       HFA384X_PORTTYPE_PSEUDO_IBSS = 3, HFA384X_PORTTYPE_IBSS = 0,
       HFA384X_PORTTYPE_HOSTAP = 6 };

#define HFA384X_RATES_1MBPS BIT(0)
#define HFA384X_RATES_2MBPS BIT(1)
#define HFA384X_RATES_5MBPS BIT(2)
#define HFA384X_RATES_11MBPS BIT(3)

#define HFA384X_ROAMING_FIRMWARE 1
#define HFA384X_ROAMING_HOST 2
#define HFA384X_ROAMING_DISABLED 3

#define HFA384X_WEPFLAGS_PRIVACYINVOKED BIT(0)
#define HFA384X_WEPFLAGS_EXCLUDEUNENCRYPTED BIT(1)
#define HFA384X_WEPFLAGS_HOSTENCRYPT BIT(4)
#define HFA384X_WEPFLAGS_HOSTDECRYPT BIT(7)

#define HFA384X_RX_STATUS_MSGTYPE (BIT(15) | BIT(14) | BIT(13))
#define HFA384X_RX_STATUS_PCF BIT(12)
#define HFA384X_RX_STATUS_MACPORT (BIT(10) | BIT(9) | BIT(8))
#define HFA384X_RX_STATUS_UNDECR BIT(1)
#define HFA384X_RX_STATUS_FCSERR BIT(0)

#define HFA384X_RX_STATUS_GET_MSGTYPE(s) \
(((s) & HFA384X_RX_STATUS_MSGTYPE) >> 13)
#define HFA384X_RX_STATUS_GET_MACPORT(s) \
(((s) & HFA384X_RX_STATUS_MACPORT) >> 8)

enum { HFA384X_RX_MSGTYPE_NORMAL = 0, HFA384X_RX_MSGTYPE_RFC1042 = 1,
       HFA384X_RX_MSGTYPE_BRIDGETUNNEL = 2, HFA384X_RX_MSGTYPE_MGMT = 4 };


#define HFA384X_TX_CTRL_ALT_RTRY BIT(5)
#define HFA384X_TX_CTRL_802_11 BIT(3)
#define HFA384X_TX_CTRL_802_3 0
#define HFA384X_TX_CTRL_TX_EX BIT(2)
#define HFA384X_TX_CTRL_TX_OK BIT(1)

#define HFA384X_TX_STATUS_RETRYERR BIT(0)
#define HFA384X_TX_STATUS_AGEDERR BIT(1)
#define HFA384X_TX_STATUS_DISCON BIT(2)
#define HFA384X_TX_STATUS_FORMERR BIT(3)

/* HFA3861/3863 (BBP) Control Registers */
#define HFA386X_CR_TX_CONFIGURE 0x12 /* CR9 */
#define HFA386X_CR_RX_CONFIGURE 0x14 /* CR10 */
#define HFA386X_CR_A_D_TEST_MODES2 0x1A /* CR13 */
#define HFA386X_CR_MANUAL_TX_POWER 0x3E /* CR31 */
#define HFA386X_CR_MEASURED_TX_POWER 0x74 /* CR58 */


#ifdef __KERNEL__

#define PRISM2_TXFID_COUNT 8
#define PRISM2_DATA_MAXLEN 2304
#define PRISM2_TXFID_LEN (PRISM2_DATA_MAXLEN + sizeof(struct hfa384x_tx_frame))
#define PRISM2_TXFID_EMPTY 0xffff
#define PRISM2_TXFID_RESERVED 0xfffe
#define PRISM2_DUMMY_FID 0xffff
#define MAX_SSID_LEN 32
#define MAX_NAME_LEN 32 /* this is assumed to be equal to MAX_SSID_LEN */

#define PRISM2_DUMP_RX_HDR BIT(0)
#define PRISM2_DUMP_TX_HDR BIT(1)
#define PRISM2_DUMP_TXEXC_HDR BIT(2)

struct hostap_tx_callback_info {
	u16 idx;
	void (*func)(struct sk_buff *, int ok, void *);
	void *data;
	struct hostap_tx_callback_info *next;
};


/* IEEE 802.11 requires that STA supports concurrent reception of at least
 * three fragmented frames. This define can be increased to support more
 * concurrent frames, but it should be noted that each entry can consume about
 * 2 kB of RAM and increasing cache size will slow down frame reassembly. */
#define PRISM2_FRAG_CACHE_LEN 4

struct prism2_frag_entry {
	unsigned long first_frag_time;
	unsigned int seq;
	unsigned int last_frag;
	struct sk_buff *skb;
	u8 src_addr[ETH_ALEN];
	u8 dst_addr[ETH_ALEN];
};


struct hostap_cmd_queue {
	struct list_head list;
	wait_queue_head_t compl;
	volatile enum { CMD_SLEEP, CMD_CALLBACK, CMD_COMPLETED } type;
	void (*callback)(struct net_device *dev, long context, u16 resp0,
			 u16 res);
	long context;
	u16 cmd, param0, param1;
	u16 resp0, res;
	volatile int issued, issuing;

	refcount_t usecnt;
	int del_req;
};

/* options for hw_shutdown */
#define HOSTAP_HW_NO_DISABLE BIT(0)
#define HOSTAP_HW_ENABLE_CMDCOMPL BIT(1)

typedef struct local_info local_info_t;

struct prism2_helper_functions {
	/* these functions are defined in hardware model specific files
	 * (hostap_{cs,plx,pci}.c */
	int (*card_present)(local_info_t *local);
	void (*cor_sreset)(local_info_t *local);
	void (*genesis_reset)(local_info_t *local, int hcr);

	/* the following functions are from hostap_hw.c, but they may have some
	 * hardware model specific code */

	/* FIX: low-level commands like cmd might disappear at some point to
	 * make it easier to change them if needed (e.g., cmd would be replaced
	 * with write_mif/read_mif/testcmd/inquire); at least get_rid and
	 * set_rid might move to hostap_{cs,plx,pci}.c */
	int (*cmd)(struct net_device *dev, u16 cmd, u16 param0, u16 *param1,
		   u16 *resp0);
	void (*read_regs)(struct net_device *dev, struct hfa384x_regs *regs);
	int (*get_rid)(struct net_device *dev, u16 rid, void *buf, int len,
		       int exact_len);
	int (*set_rid)(struct net_device *dev, u16 rid, void *buf, int len);
	int (*hw_enable)(struct net_device *dev, int initial);
	int (*hw_config)(struct net_device *dev, int initial);
	void (*hw_reset)(struct net_device *dev);
	void (*hw_shutdown)(struct net_device *dev, int no_disable);
	int (*reset_port)(struct net_device *dev);
	void (*schedule_reset)(local_info_t *local);
	int (*download)(local_info_t *local,
			struct prism2_download_param *param);
	int (*tx)(struct sk_buff *skb, struct net_device *dev);
	int (*set_tim)(struct net_device *dev, int aid, int set);
	const struct proc_ops *read_aux_proc_ops;

	int need_tx_headroom; /* number of bytes of headroom needed before
			       * IEEE 802.11 header */
	enum { HOSTAP_HW_PCCARD, HOSTAP_HW_PLX, HOSTAP_HW_PCI } hw_type;
};


struct prism2_download_data {
	u32 dl_cmd;
	u32 start_addr;
	u32 num_areas;
	struct prism2_download_data_area {
		u32 addr; /* wlan card address */
		u32 len;
		u8 *data; /* allocated data */
	} data[] __counted_by(num_areas);
};


#define HOSTAP_MAX_BSS_COUNT 64
#define MAX_WPA_IE_LEN 64

struct hostap_bss_info {
	struct list_head list;
	unsigned long last_update;
	unsigned int count;
	u8 bssid[ETH_ALEN];
	u16 capab_info;
	u8 ssid[32];
	size_t ssid_len;
	u8 wpa_ie[MAX_WPA_IE_LEN];
	size_t wpa_ie_len;
	u8 rsn_ie[MAX_WPA_IE_LEN];
	size_t rsn_ie_len;
	int chan;
	int included;
};


/* Per radio private Host AP data - shared by all net devices interfaces used
 * by each radio (wlan#, wlan#ap, wlan#sta, WDS).
 * ((struct hostap_interface *) netdev_priv(dev))->local points to this
 * structure. */
struct local_info {
	struct module *hw_module;
	int card_idx;
	int dev_enabled;
	int master_dev_auto_open; /* was master device opened automatically */
	int num_dev_open; /* number of open devices */
	struct net_device *dev; /* master radio device */
	struct net_device *ddev; /* main data device */
	struct list_head hostap_interfaces; /* Host AP interface list (contains
					     * struct hostap_interface entries)
					     */
	rwlock_t iface_lock; /* hostap_interfaces read lock; use write lock
			      * when removing entries from the list.
			      * TX and RX paths can use read lock. */
	spinlock_t cmdlock, baplock, lock, irq_init_lock;
	struct mutex rid_bap_mtx;
	u16 infofid; /* MAC buffer id for info frame */
	/* txfid, intransmitfid, next_txtid, and next_alloc are protected by
	 * txfidlock */
	spinlock_t txfidlock;
	int txfid_len; /* length of allocated TX buffers */
	u16 txfid[PRISM2_TXFID_COUNT]; /* buffer IDs for TX frames */
	/* buffer IDs for intransmit frames or PRISM2_TXFID_EMPTY if
	 * corresponding txfid is free for next TX frame */
	u16 intransmitfid[PRISM2_TXFID_COUNT];
	int next_txfid; /* index to the next txfid to be checked for
			 * availability */
	int next_alloc; /* index to the next intransmitfid to be checked for
			 * allocation events */

	/* bitfield for atomic bitops */
#define HOSTAP_BITS_TRANSMIT 0
#define HOSTAP_BITS_BAP_TASKLET 1
#define HOSTAP_BITS_BAP_TASKLET2 2
	unsigned long bits;

	struct ap_data *ap;

	char essid[MAX_SSID_LEN + 1];
	char name[MAX_NAME_LEN + 1];
	int name_set;
	u16 channel_mask; /* mask of allowed channels */
	u16 scan_channel_mask; /* mask of channels to be scanned */
	struct comm_tallies_sums comm_tallies;
	struct proc_dir_entry *proc;
	int iw_mode; /* operating mode (IW_MODE_*) */
	int pseudo_adhoc; /* 0: IW_MODE_ADHOC is real 802.11 compliant IBSS
			   * 1: IW_MODE_ADHOC is "pseudo IBSS" */
	char bssid[ETH_ALEN];
	int channel;
	int beacon_int;
	int dtim_period;
	int mtu;
	int frame_dump; /* dump RX/TX frame headers, PRISM2_DUMP_ flags */
	int fw_tx_rate_control;
	u16 tx_rate_control;
	u16 basic_rates;
	int hw_resetting;
	int hw_ready;
	int hw_reset_tries; /* how many times reset has been tried */
	int hw_downloading;
	int shutdown;
	int pri_only;
	int no_pri; /* no PRI f/w present */
	int sram_type; /* 8 = x8 SRAM, 16 = x16 SRAM, -1 = unknown */

	enum {
		PRISM2_TXPOWER_AUTO = 0, PRISM2_TXPOWER_OFF,
		PRISM2_TXPOWER_FIXED, PRISM2_TXPOWER_UNKNOWN
	} txpower_type;
	int txpower; /* if txpower_type == PRISM2_TXPOWER_FIXED */

	/* command queue for hfa384x_cmd(); protected with cmdlock */
	struct list_head cmd_queue;
	/* max_len for cmd_queue; in addition, cmd_callback can use two
	 * additional entries to prevent sleeping commands from stopping
	 * transmits */
#define HOSTAP_CMD_QUEUE_MAX_LEN 16
	int cmd_queue_len; /* number of entries in cmd_queue */

	/* if card timeout is detected in interrupt context, reset_queue is
	 * used to schedule card reseting to be done in user context */
	struct work_struct reset_queue;

	/* For scheduling a change of the promiscuous mode RID */
	int is_promisc;
	struct work_struct set_multicast_list_queue;

	struct work_struct set_tim_queue;
	struct list_head set_tim_list;
	spinlock_t set_tim_lock;

	int wds_max_connections;
	int wds_connections;
#define HOSTAP_WDS_BROADCAST_RA BIT(0)
#define HOSTAP_WDS_AP_CLIENT BIT(1)
#define HOSTAP_WDS_STANDARD_FRAME BIT(2)
	u32 wds_type;
	u16 tx_control; /* flags to be used in TX description */
	int manual_retry_count; /* -1 = use f/w default; otherwise retry count
				 * to be used with all frames */

	struct iw_statistics wstats;
	unsigned long scan_timestamp; /* Time started to scan */
	enum {
		PRISM2_MONITOR_80211 = 0, PRISM2_MONITOR_PRISM = 1,
		PRISM2_MONITOR_CAPHDR = 2, PRISM2_MONITOR_RADIOTAP = 3
	} monitor_type;
	int monitor_allow_fcserr;

	int hostapd; /* whether user space daemon, hostapd, is used for AP
		      * management */
	int hostapd_sta; /* whether hostapd is used with an extra STA interface
			  */
	struct net_device *apdev;
	struct net_device_stats apdevstats;

	char assoc_ap_addr[ETH_ALEN];
	struct net_device *stadev;
	struct net_device_stats stadevstats;

#define WEP_KEYS 4
#define WEP_KEY_LEN 13
	struct lib80211_crypt_info crypt_info;

	int open_wep; /* allow unencrypted frames */
	int host_encrypt;
	int host_decrypt;
	int privacy_invoked; /* force privacy invoked flag even if no keys are
			      * configured */
	int fw_encrypt_ok; /* whether firmware-based WEP encrypt is working
			    * in Host AP mode (STA f/w 1.4.9 or newer) */
	int bcrx_sta_key; /* use individual keys to override default keys even
			   * with RX of broad/multicast frames */

	struct prism2_frag_entry frag_cache[PRISM2_FRAG_CACHE_LEN];
	unsigned int frag_next_idx;

	int ieee_802_1x; /* is IEEE 802.1X used */

	int antsel_tx, antsel_rx;
	int rts_threshold; /* dot11RTSThreshold */
	int fragm_threshold; /* dot11FragmentationThreshold */
	int auth_algs; /* PRISM2_AUTH_ flags */

	int enh_sec; /* cnfEnhSecurity options (broadcast SSID hide/ignore) */
	int tallies32; /* 32-bit tallies in use */

	struct prism2_helper_functions *func;

	u8 *pda;
	int fw_ap;
#define PRISM2_FW_VER(major, minor, variant) \
(((major) << 16) | ((minor) << 8) | variant)
	u32 sta_fw_ver;

	/* Tasklets for handling hardware IRQ related operations outside hw IRQ
	 * handler */
	struct tasklet_struct bap_tasklet;

	struct tasklet_struct info_tasklet;
	struct sk_buff_head info_list; /* info frames as skb's for
					* info_tasklet */

	struct hostap_tx_callback_info *tx_callback; /* registered TX callbacks
						      */

	struct tasklet_struct rx_tasklet;
	struct sk_buff_head rx_list;

	struct tasklet_struct sta_tx_exc_tasklet;
	struct sk_buff_head sta_tx_exc_list;

	int host_roaming;
	unsigned long last_join_time; /* time of last JoinRequest */
	struct hfa384x_hostscan_result *last_scan_results;
	int last_scan_results_count;
	enum { PRISM2_SCAN, PRISM2_HOSTSCAN } last_scan_type;
	struct work_struct info_queue;
	unsigned long pending_info; /* bit field of pending info_queue items */
#define PRISM2_INFO_PENDING_LINKSTATUS 0
#define PRISM2_INFO_PENDING_SCANRESULTS 1
	int prev_link_status; /* previous received LinkStatus info */
	int prev_linkstatus_connected;
	u8 preferred_ap[ETH_ALEN]; /* use this AP if possible */

#ifdef PRISM2_CALLBACK
	void *callback_data; /* Can be used in callbacks; e.g., allocate
			      * on enable event and free on disable event.
			      * Host AP driver code does not touch this. */
#endif /* PRISM2_CALLBACK */

	wait_queue_head_t hostscan_wq;

	/* Passive scan in Host AP mode */
	struct timer_list passive_scan_timer;
	int passive_scan_interval; /* in seconds, 0 = disabled */
	int passive_scan_channel;
	enum { PASSIVE_SCAN_WAIT, PASSIVE_SCAN_LISTEN } passive_scan_state;

	struct timer_list tick_timer;
	unsigned long last_tick_timer;
	unsigned int sw_tick_stuck;

	/* commsQuality / dBmCommsQuality data from periodic polling; only
	 * valid for Managed and Ad-hoc modes */
	unsigned long last_comms_qual_update;
	int comms_qual; /* in some odd unit.. */
	int avg_signal; /* in dB (note: negative) */
	int avg_noise; /* in dB (note: negative) */
	struct work_struct comms_qual_update;

	/* RSSI to dBm adjustment (for RX descriptor fields) */
	int rssi_to_dBm; /* subtract from RSSI to get approximate dBm value */

	/* BSS list / protected by local->lock */
	struct list_head bss_list;
	int num_bss_info;
	int wpa; /* WPA support enabled */
	int tkip_countermeasures;
	int drop_unencrypted;
	/* Generic IEEE 802.11 info element to be added to
	 * ProbeResp/Beacon/(Re)AssocReq */
	u8 *generic_elem;
	size_t generic_elem_len;

#ifdef PRISM2_DOWNLOAD_SUPPORT
	/* Persistent volatile download data */
	struct prism2_download_data *dl_pri;
	struct prism2_download_data *dl_sec;
#endif /* PRISM2_DOWNLOAD_SUPPORT */

#ifdef PRISM2_IO_DEBUG
#define PRISM2_IO_DEBUG_SIZE 10000
	u32 io_debug[PRISM2_IO_DEBUG_SIZE];
	int io_debug_head;
	int io_debug_enabled;
#endif /* PRISM2_IO_DEBUG */

	/* Pointer to hardware model specific (cs,pci,plx) private data. */
	void *hw_priv;
};


/* Per interface private Host AP data
 * Allocated for each net device that Host AP uses (wlan#, wlan#ap, wlan#sta,
 * WDS) and netdev_priv(dev) points to this structure. */
struct hostap_interface {
	struct list_head list; /* list entry in Host AP interface list */
	struct net_device *dev; /* pointer to this device */
	struct local_info *local; /* pointer to shared private data */
	struct net_device_stats stats;
	struct iw_spy_data spy_data; /* iwspy support */
	struct iw_public_data wireless_data;

	enum {
		HOSTAP_INTERFACE_MASTER,
		HOSTAP_INTERFACE_MAIN,
		HOSTAP_INTERFACE_AP,
		HOSTAP_INTERFACE_STA,
		HOSTAP_INTERFACE_WDS,
	} type;

	union {
		struct hostap_interface_wds {
			u8 remote_addr[ETH_ALEN];
		} wds;
	} u;
};


#define HOSTAP_SKB_TX_DATA_MAGIC 0xf08a36a2

/*
 * TX meta data - stored in skb->cb buffer, so this must not be increased over
 * the 48-byte limit.
 * THE PADDING THIS STARTS WITH IS A HORRIBLE HACK THAT SHOULD NOT LIVE
 * TO SEE THE DAY.
 */
struct hostap_skb_tx_data {
	unsigned int __padding_for_default_qdiscs;
	u32 magic; /* HOSTAP_SKB_TX_DATA_MAGIC */
	u8 rate; /* transmit rate */
#define HOSTAP_TX_FLAGS_WDS BIT(0)
#define HOSTAP_TX_FLAGS_BUFFERED_FRAME BIT(1)
#define HOSTAP_TX_FLAGS_ADD_MOREDATA BIT(2)
	u8 flags; /* HOSTAP_TX_FLAGS_* */
	u16 tx_cb_idx;
	struct hostap_interface *iface;
	unsigned long jiffies; /* queueing timestamp */
	unsigned short ethertype;
};


#ifndef PRISM2_NO_DEBUG

#define DEBUG_FID BIT(0)
#define DEBUG_PS BIT(1)
#define DEBUG_FLOW BIT(2)
#define DEBUG_AP BIT(3)
#define DEBUG_HW BIT(4)
#define DEBUG_EXTRA BIT(5)
#define DEBUG_EXTRA2 BIT(6)
#define DEBUG_PS2 BIT(7)
#define DEBUG_MASK (DEBUG_PS | DEBUG_AP | DEBUG_HW | DEBUG_EXTRA)
#define PDEBUG(n, args...) \
do { if ((n) & DEBUG_MASK) printk(KERN_DEBUG args); } while (0)
#define PDEBUG2(n, args...) \
do { if ((n) & DEBUG_MASK) printk(args); } while (0)

#else /* PRISM2_NO_DEBUG */

#define PDEBUG(n, args...)
#define PDEBUG2(n, args...)

#endif /* PRISM2_NO_DEBUG */

enum { BAP0 = 0, BAP1 = 1 };

#define PRISM2_IO_DEBUG_CMD_INB 0
#define PRISM2_IO_DEBUG_CMD_INW 1
#define PRISM2_IO_DEBUG_CMD_INSW 2
#define PRISM2_IO_DEBUG_CMD_OUTB 3
#define PRISM2_IO_DEBUG_CMD_OUTW 4
#define PRISM2_IO_DEBUG_CMD_OUTSW 5
#define PRISM2_IO_DEBUG_CMD_ERROR 6
#define PRISM2_IO_DEBUG_CMD_INTERRUPT 7

#ifdef PRISM2_IO_DEBUG

#define PRISM2_IO_DEBUG_ENTRY(cmd, reg, value) \
(((cmd) << 24) | ((reg) << 16) | value)

static inline void prism2_io_debug_add(struct net_device *dev, int cmd,
				       int reg, int value)
{
	struct hostap_interface *iface = netdev_priv(dev);
	local_info_t *local = iface->local;

	if (!local->io_debug_enabled)
		return;

	local->io_debug[local->io_debug_head] =	jiffies & 0xffffffff;
	if (++local->io_debug_head >= PRISM2_IO_DEBUG_SIZE)
		local->io_debug_head = 0;
	local->io_debug[local->io_debug_head] =
		PRISM2_IO_DEBUG_ENTRY(cmd, reg, value);
	if (++local->io_debug_head >= PRISM2_IO_DEBUG_SIZE)
		local->io_debug_head = 0;
}


static inline void prism2_io_debug_error(struct net_device *dev, int err)
{
	struct hostap_interface *iface = netdev_priv(dev);
	local_info_t *local = iface->local;
	unsigned long flags;

	if (!local->io_debug_enabled)
		return;

	spin_lock_irqsave(&local->lock, flags);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_ERROR, 0, err);
	if (local->io_debug_enabled == 1) {
		local->io_debug_enabled = 0;
		printk(KERN_DEBUG "%s: I/O debug stopped\n", dev->name);
	}
	spin_unlock_irqrestore(&local->lock, flags);
}

#else /* PRISM2_IO_DEBUG */

static inline void prism2_io_debug_add(struct net_device *dev, int cmd,
				       int reg, int value)
{
}

static inline void prism2_io_debug_error(struct net_device *dev, int err)
{
}

#endif /* PRISM2_IO_DEBUG */


#ifdef PRISM2_CALLBACK
enum {
	/* Called when card is enabled */
	PRISM2_CALLBACK_ENABLE,

	/* Called when card is disabled */
	PRISM2_CALLBACK_DISABLE,

	/* Called when RX/TX starts/ends */
	PRISM2_CALLBACK_RX_START, PRISM2_CALLBACK_RX_END,
	PRISM2_CALLBACK_TX_START, PRISM2_CALLBACK_TX_END
};
void prism2_callback(local_info_t *local, int event);
#else /* PRISM2_CALLBACK */
#define prism2_callback(d, e) do { } while (0)
#endif /* PRISM2_CALLBACK */

#endif /* __KERNEL__ */

#endif /* HOSTAP_WLAN_H */
