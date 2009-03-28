/*
 *  Copyright (C) 2008, cozybit Inc.
 *  Copyright (C) 2007, Red Hat, Inc.
 *  Copyright (C) 2003-2006, Marvell International Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 */
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <net/mac80211.h>

#ifndef DRV_NAME
#define DRV_NAME "libertas_tf"
#endif

#define	MRVL_DEFAULT_RETRIES			9
#define MRVL_PER_PACKET_RATE			0x10
#define MRVL_MAX_BCN_SIZE			440
#define CMD_OPTION_WAITFORRSP			0x0002

/* Return command are almost always the same as the host command, but with
 * bit 15 set high.  There are a few exceptions, though...
 */
#define CMD_RET(cmd)			(0x8000 | cmd)

/* Command codes */
#define CMD_GET_HW_SPEC				0x0003
#define CMD_802_11_RESET			0x0005
#define CMD_MAC_MULTICAST_ADR			0x0010
#define CMD_802_11_RADIO_CONTROL		0x001c
#define CMD_802_11_RF_CHANNEL			0x001d
#define CMD_802_11_RF_TX_POWER			0x001e
#define CMD_MAC_CONTROL				0x0028
#define CMD_802_11_MAC_ADDRESS			0x004d
#define	CMD_SET_BOOT2_VER			0x00a5
#define CMD_802_11_BEACON_CTRL			0x00b0
#define CMD_802_11_BEACON_SET			0x00cb
#define CMD_802_11_SET_MODE			0x00cc
#define CMD_802_11_SET_BSSID			0x00cd

#define CMD_ACT_GET			0x0000
#define CMD_ACT_SET			0x0001

/* Define action or option for CMD_802_11_RESET */
#define CMD_ACT_HALT			0x0003

/* Define action or option for CMD_MAC_CONTROL */
#define CMD_ACT_MAC_RX_ON			0x0001
#define CMD_ACT_MAC_TX_ON			0x0002
#define CMD_ACT_MAC_MULTICAST_ENABLE		0x0020
#define CMD_ACT_MAC_BROADCAST_ENABLE		0x0040
#define CMD_ACT_MAC_PROMISCUOUS_ENABLE		0x0080
#define CMD_ACT_MAC_ALL_MULTICAST_ENABLE	0x0100

/* Define action or option for CMD_802_11_RADIO_CONTROL */
#define CMD_TYPE_AUTO_PREAMBLE		0x0001
#define CMD_TYPE_SHORT_PREAMBLE		0x0002
#define CMD_TYPE_LONG_PREAMBLE		0x0003

#define TURN_ON_RF			0x01
#define RADIO_ON			0x01
#define RADIO_OFF			0x00

#define SET_AUTO_PREAMBLE		0x05
#define SET_SHORT_PREAMBLE		0x03
#define SET_LONG_PREAMBLE		0x01

/* Define action or option for CMD_802_11_RF_CHANNEL */
#define CMD_OPT_802_11_RF_CHANNEL_GET	0x00
#define CMD_OPT_802_11_RF_CHANNEL_SET	0x01

/* Codes for CMD_802_11_SET_MODE */
enum lbtf_mode {
	LBTF_PASSIVE_MODE,
	LBTF_STA_MODE,
	LBTF_AP_MODE,
};

/** Card Event definition */
#define MACREG_INT_CODE_FIRMWARE_READY		48
/** Buffer Constants */

/*	The size of SQ memory PPA, DPA are 8 DWORDs, that keep the physical
*	addresses of TxPD buffers. Station has only 8 TxPD available, Whereas
*	driver has more local TxPDs. Each TxPD on the host memory is associated
*	with a Tx control node. The driver maintains 8 RxPD descriptors for
*	station firmware to store Rx packet information.
*
*	Current version of MAC has a 32x6 multicast address buffer.
*
*	802.11b can have up to  14 channels, the driver keeps the
*	BSSID(MAC address) of each APs or Ad hoc stations it has sensed.
*/

#define MRVDRV_MAX_MULTICAST_LIST_SIZE	32
#define LBS_NUM_CMD_BUFFERS             10
#define LBS_CMD_BUFFER_SIZE             (2 * 1024)
#define MRVDRV_MAX_CHANNEL_SIZE		14
#define MRVDRV_SNAP_HEADER_LEN          8

#define	LBS_UPLD_SIZE			2312
#define DEV_NAME_LEN			32

/** Misc constants */
/* This section defines 802.11 specific contants */

#define MRVDRV_MAX_REGION_CODE			6
/**
 * the table to keep region code
 */
#define LBTF_REGDOMAIN_US	0x10
#define LBTF_REGDOMAIN_CA	0x20
#define LBTF_REGDOMAIN_EU	0x30
#define LBTF_REGDOMAIN_SP	0x31
#define LBTF_REGDOMAIN_FR	0x32
#define LBTF_REGDOMAIN_JP	0x40

#define SBI_EVENT_CAUSE_SHIFT		3

/** RxPD status */

#define MRVDRV_RXPD_STATUS_OK                0x0001


/* This is for firmware specific length */
#define EXTRA_LEN	36

#define MRVDRV_ETH_TX_PACKET_BUFFER_SIZE \
	(ETH_FRAME_LEN + sizeof(struct txpd) + EXTRA_LEN)

#define MRVDRV_ETH_RX_PACKET_BUFFER_SIZE \
	(ETH_FRAME_LEN + sizeof(struct rxpd) \
	 + MRVDRV_SNAP_HEADER_LEN + EXTRA_LEN)

#define	CMD_F_HOSTCMD		(1 << 0)
#define FW_CAPINFO_WPA  	(1 << 0)

#define RF_ANTENNA_1		0x1
#define RF_ANTENNA_2		0x2
#define RF_ANTENNA_AUTO		0xFFFF

#define LBTF_EVENT_BCN_SENT	55

/** Global Variable Declaration */
/** mv_ms_type */
enum mv_ms_type {
	MVMS_DAT = 0,
	MVMS_CMD = 1,
	MVMS_TXDONE = 2,
	MVMS_EVENT
};

extern struct workqueue_struct *lbtf_wq;

struct lbtf_private;

struct lbtf_offset_value {
	u32 offset;
	u32 value;
};

struct channel_range {
	u8 regdomain;
	u8 start;
	u8 end; /* exclusive (channel must be less than end) */
};

struct if_usb_card;

/** Private structure for the MV device */
struct lbtf_private {
	void *card;
	struct ieee80211_hw *hw;

	/* Command response buffer */
	u8 cmd_resp_buff[LBS_UPLD_SIZE];
	/* Download sent:
	   bit0 1/0=data_sent/data_tx_done,
	   bit1 1/0=cmd_sent/cmd_tx_done,
	   all other bits reserved 0 */
	struct ieee80211_vif *vif;

	struct work_struct cmd_work;
	struct work_struct tx_work;
	/** Hardware access */
	int (*hw_host_to_card) (struct lbtf_private *priv, u8 type, u8 *payload, u16 nb);
	int (*hw_prog_firmware) (struct if_usb_card *cardp);
	int (*hw_reset_device) (struct if_usb_card *cardp);


	/** Wlan adapter data structure*/
	/** STATUS variables */
	u32 fwrelease;
	u32 fwcapinfo;
	/* protected with big lock */

	struct mutex lock;

	/** command-related variables */
	u16 seqnum;
	/* protected by big lock */

	struct cmd_ctrl_node *cmd_array;
	/** Current command */
	struct cmd_ctrl_node *cur_cmd;
	/** command Queues */
	/** Free command buffers */
	struct list_head cmdfreeq;
	/** Pending command buffers */
	struct list_head cmdpendingq;

	/** spin locks */
	spinlock_t driver_lock;

	/** Timers */
	struct timer_list command_timer;
	int nr_retries;
	int cmd_timed_out;

	u8 cmd_response_rxed;

	/** capability Info used in Association, start, join */
	u16 capability;

	/** MAC address information */
	u8 current_addr[ETH_ALEN];
	u8 multicastlist[MRVDRV_MAX_MULTICAST_LIST_SIZE][ETH_ALEN];
	u32 nr_of_multicastmacaddr;
	int cur_freq;

	struct sk_buff *skb_to_tx;
	struct sk_buff *tx_skb;

	/** NIC Operation characteristics */
	u16 mac_control;
	u16 regioncode;
	struct channel_range range;

	u8 radioon;
	u32 preamble;

	struct ieee80211_channel channels[14];
	struct ieee80211_rate rates[12];
	struct ieee80211_supported_band band;
	struct lbtf_offset_value offsetvalue;

	u8 fw_ready;
	u8 surpriseremoved;
	struct sk_buff_head bc_ps_buf;
};

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
	/* Time the packet has been queued in the driver (units = 2ms) */
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
	int (*callback)(struct lbtf_private *,
			unsigned long, struct cmd_header *);
	unsigned long callback_arg;
	/* command data */
	struct cmd_header *cmdbuf;
	/* wait queue */
	u16 cmdwaitqwoken;
	wait_queue_head_t cmdwait_q;
};

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

struct cmd_ds_mac_control {
	struct cmd_header hdr;
	__le16 action;
	u16 reserved;
};

struct cmd_ds_802_11_mac_address {
	struct cmd_header hdr;

	__le16 action;
	uint8_t macadd[ETH_ALEN];
};

struct cmd_ds_mac_multicast_addr {
	struct cmd_header hdr;

	__le16 action;
	__le16 nr_of_adrs;
	u8 maclist[ETH_ALEN * MRVDRV_MAX_MULTICAST_LIST_SIZE];
};

struct cmd_ds_set_mode {
	struct cmd_header hdr;

	__le16 mode;
};

struct cmd_ds_set_bssid {
	struct cmd_header hdr;

	u8 bssid[6];
	u8 activate;
};

struct cmd_ds_802_11_radio_control {
	struct cmd_header hdr;

	__le16 action;
	__le16 control;
};


struct cmd_ds_802_11_rf_channel {
	struct cmd_header hdr;

	__le16 action;
	__le16 channel;
	__le16 rftype;      /* unused */
	__le16 reserved;    /* unused */
	u8 channellist[32]; /* unused */
};

struct cmd_ds_set_boot2_ver {
	struct cmd_header hdr;

	__le16 action;
	__le16 version;
};

struct cmd_ds_802_11_reset {
	struct cmd_header hdr;

	__le16 action;
};

struct cmd_ds_802_11_beacon_control {
	struct cmd_header hdr;

	__le16 action;
	__le16 beacon_enable;
	__le16 beacon_period;
};

struct cmd_ds_802_11_beacon_set {
	struct cmd_header hdr;

	__le16 len;
	u8 beacon[MRVL_MAX_BCN_SIZE];
};

struct lbtf_private;
struct cmd_ctrl_node;

/** Function Prototype Declaration */
void lbtf_set_mac_control(struct lbtf_private *priv);

int lbtf_free_cmd_buffer(struct lbtf_private *priv);

int lbtf_allocate_cmd_buffer(struct lbtf_private *priv);
int lbtf_execute_next_command(struct lbtf_private *priv);
int lbtf_set_radio_control(struct lbtf_private *priv);
int lbtf_update_hw_spec(struct lbtf_private *priv);
int lbtf_cmd_set_mac_multicast_addr(struct lbtf_private *priv);
void lbtf_set_mode(struct lbtf_private *priv, enum lbtf_mode mode);
void lbtf_set_bssid(struct lbtf_private *priv, bool activate, const u8 *bssid);
int lbtf_set_mac_address(struct lbtf_private *priv, uint8_t *mac_addr);

int lbtf_set_channel(struct lbtf_private *priv, u8 channel);

int lbtf_beacon_set(struct lbtf_private *priv, struct sk_buff *beacon);
int lbtf_beacon_ctrl(struct lbtf_private *priv, bool beacon_enable,
		     int beacon_int);


int lbtf_process_rx_command(struct lbtf_private *priv);
void lbtf_complete_command(struct lbtf_private *priv, struct cmd_ctrl_node *cmd,
			  int result);
void lbtf_cmd_response_rx(struct lbtf_private *priv);

/* main.c */
struct chan_freq_power *lbtf_get_region_cfp_table(u8 region,
	int *cfp_no);
struct lbtf_private *lbtf_add_card(void *card, struct device *dmdev);
int lbtf_remove_card(struct lbtf_private *priv);
int lbtf_start_card(struct lbtf_private *priv);
int lbtf_rx(struct lbtf_private *priv, struct sk_buff *skb);
void lbtf_send_tx_feedback(struct lbtf_private *priv, u8 retrycnt, u8 fail);
void lbtf_bcn_sent(struct lbtf_private *priv);

/* support functions for cmd.c */
/* lbtf_cmd() infers the size of the buffer to copy data back into, from
   the size of the target of the pointer. Since the command to be sent
   may often be smaller, that size is set in cmd->size by the caller.*/
#define lbtf_cmd(priv, cmdnr, cmd, cb, cb_arg)	({		\
	uint16_t __sz = le16_to_cpu((cmd)->hdr.size);		\
	(cmd)->hdr.size = cpu_to_le16(sizeof(*(cmd)));		\
	__lbtf_cmd(priv, cmdnr, &(cmd)->hdr, __sz, cb, cb_arg);	\
})

#define lbtf_cmd_with_response(priv, cmdnr, cmd)	\
	lbtf_cmd(priv, cmdnr, cmd, lbtf_cmd_copyback, (unsigned long) (cmd))

void lbtf_cmd_async(struct lbtf_private *priv, uint16_t command,
	struct cmd_header *in_cmd, int in_cmd_size);

int __lbtf_cmd(struct lbtf_private *priv, uint16_t command,
	      struct cmd_header *in_cmd, int in_cmd_size,
	      int (*callback)(struct lbtf_private *, unsigned long,
			      struct cmd_header *),
	      unsigned long callback_arg);

int lbtf_cmd_copyback(struct lbtf_private *priv, unsigned long extra,
		     struct cmd_header *resp);
