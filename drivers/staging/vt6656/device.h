/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: device.h
 *
 * Purpose: MAC Data structure
 *
 * Author: Tevin Chen
 *
 * Date: Mar 17, 1997
 *
 */

#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/suspend.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/timer.h>
#include <linux/usb.h>
#include <linux/crc32.h>
#include <net/mac80211.h>

#ifdef SIOCETHTOOL
#define DEVICE_ETHTOOL_IOCTL_SUPPORT
#include <linux/ethtool.h>
#else
#undef DEVICE_ETHTOOL_IOCTL_SUPPORT
#endif

#define RATE_1M		0
#define RATE_2M		1
#define RATE_5M		2
#define RATE_11M	3
#define RATE_6M		4
#define RATE_9M		5
#define RATE_12M	6
#define RATE_18M	7
#define RATE_24M	8
#define RATE_36M	9
#define RATE_48M	10
#define RATE_54M	11
#define RATE_AUTO	12

#define MAX_RATE			12

/*
 * device specific
 */

#include "wcmd.h"
#include "desc.h"
#include "key.h"
#include "card.h"

#define VNT_USB_VENDOR_ID                     0x160a
#define VNT_USB_PRODUCT_ID                    0x3184

#define DEVICE_NAME			"vt6656"
#define DEVICE_FULL_DRV_NAM		"VIA Networking Wireless LAN USB Driver"

#define DEVICE_VERSION			"1.19_12"

#define CONFIG_PATH			"/etc/vntconfiguration.dat"

#define MAX_UINTS			8
#define OPTION_DEFAULT			{ [0 ... MAX_UINTS-1] = -1}

#define DUPLICATE_RX_CACHE_LENGTH       5

#define AUTO_FB_NONE            0
#define AUTO_FB_0               1
#define AUTO_FB_1               2

#define FB_RATE0                0
#define FB_RATE1                1

/* Antenna Mode */
#define ANT_A                   0
#define ANT_B                   1
#define ANT_DIVERSITY           2
#define ANT_RXD_TXA             3
#define ANT_RXD_TXB             4
#define ANT_UNKNOWN             0xFF
#define ANT_TXA                 0
#define ANT_TXB                 1
#define ANT_RXA                 2
#define ANT_RXB                 3

#define BB_VGA_LEVEL            4
#define BB_VGA_CHANGE_THRESHOLD 3

#define EEP_MAX_CONTEXT_SIZE    256

/* Contents in the EEPROM */
#define EEP_OFS_PAR		0x0
#define EEP_OFS_ANTENNA		0x17
#define EEP_OFS_RADIOCTL	0x18
#define EEP_OFS_RFTYPE		0x1b
#define EEP_OFS_MINCHANNEL	0x1c
#define EEP_OFS_MAXCHANNEL	0x1d
#define EEP_OFS_SIGNATURE	0x1e
#define EEP_OFS_ZONETYPE	0x1f
#define EEP_OFS_RFTABLE		0x20
#define EEP_OFS_PWR_CCK		0x20
#define EEP_OFS_SETPT_CCK	0x21
#define EEP_OFS_PWR_OFDMG	0x23

#define EEP_OFS_CALIB_TX_IQ	0x24
#define EEP_OFS_CALIB_TX_DC	0x25
#define EEP_OFS_CALIB_RX_IQ	0x26

#define EEP_OFS_MAJOR_VER	0x2e
#define EEP_OFS_MINOR_VER	0x2f

#define EEP_OFS_CCK_PWR_TBL	0x30
#define EEP_OFS_OFDM_PWR_TBL	0x40
#define EEP_OFS_OFDMA_PWR_TBL	0x50

/* Bits in EEP_OFS_ANTENNA */
#define EEP_ANTENNA_MAIN	0x1
#define EEP_ANTENNA_AUX		0x2
#define EEP_ANTINV		0x4

/* Bits in EEP_OFS_RADIOCTL */
#define EEP_RADIOCTL_ENABLE	0x80

/* control commands */
#define MESSAGE_TYPE_READ		0x1
#define MESSAGE_TYPE_WRITE		0x0
#define MESSAGE_TYPE_LOCK_OR		0x2
#define MESSAGE_TYPE_LOCK_AND		0x3
#define MESSAGE_TYPE_WRITE_MASK		0x4
#define MESSAGE_TYPE_CARDINIT		0x5
#define MESSAGE_TYPE_INIT_RSP		0x6
#define MESSAGE_TYPE_MACSHUTDOWN	0x7
#define MESSAGE_TYPE_SETKEY		0x8
#define MESSAGE_TYPE_CLRKEYENTRY	0x9
#define MESSAGE_TYPE_WRITE_MISCFF	0xa
#define MESSAGE_TYPE_SET_ANTMD		0xb
#define MESSAGE_TYPE_SELECT_CHANNLE	0xc
#define MESSAGE_TYPE_SET_TSFTBTT	0xd
#define MESSAGE_TYPE_SET_SSTIFS		0xe
#define MESSAGE_TYPE_CHANGE_BBTYPE	0xf
#define MESSAGE_TYPE_DISABLE_PS		0x10
#define MESSAGE_TYPE_WRITE_IFRF		0x11

/* command read/write(index) */
#define MESSAGE_REQUEST_MEM		0x1
#define MESSAGE_REQUEST_BBREG		0x2
#define MESSAGE_REQUEST_MACREG		0x3
#define MESSAGE_REQUEST_EEPROM		0x4
#define MESSAGE_REQUEST_TSF		0x5
#define MESSAGE_REQUEST_TBTT		0x6
#define MESSAGE_REQUEST_BBAGC		0x7
#define MESSAGE_REQUEST_VERSION		0x8
#define MESSAGE_REQUEST_RF_INIT		0x9
#define MESSAGE_REQUEST_RF_INIT2	0xa
#define MESSAGE_REQUEST_RF_CH0		0xb
#define MESSAGE_REQUEST_RF_CH1		0xc
#define MESSAGE_REQUEST_RF_CH2		0xd

/* USB registers */
#define USB_REG4			0x604

#define DEVICE_INIT_COLD	0x0 /* cold init */
#define DEVICE_INIT_RESET	0x1 /* reset init or Dx to D0 power remain */
#define DEVICE_INIT_DXPL	0x2 /* Dx to D0 power lost init */

/* Device init */
struct vnt_cmd_card_init {
	u8 init_class;
	u8 exist_sw_net_addr;
	u8 sw_net_addr[6];
	u8 short_retry_limit;
	u8 long_retry_limit;
};

struct vnt_rsp_card_init {
	u8 status;
	u8 net_addr[6];
	u8 rf_type;
	u8 min_channel;
	u8 max_channel;
};

/* USB */

/*
 * Enum of context types for SendPacket
 */
enum {
	CONTEXT_DATA_PACKET = 1,
	CONTEXT_MGMT_PACKET,
	CONTEXT_BEACON_PACKET
};

/* RCB (Receive Control Block) */
struct vnt_rcb {
	void *priv;
	struct urb *urb;
	struct sk_buff *skb;
	int in_use;
};

/* used to track bulk out irps */
struct vnt_usb_send_context {
	void *priv;
	struct sk_buff *skb;
	struct urb *urb;
	struct ieee80211_hdr *hdr;
	unsigned int buf_len;
	u32 frame_len;
	u16 tx_hdr_size;
	u16 tx_rate;
	u8 type;
	u8 pkt_no;
	u8 pkt_type;
	u8 need_ack;
	u8 fb_option;
	bool in_use;
	unsigned char data[MAX_TOTAL_SIZE_WITH_ALL_HEADERS];
};

/*
 * Structure to keep track of USB interrupt packets
 */
struct vnt_interrupt_buffer {
	u8 *data_buf;
	bool in_use;
};

/*++ NDIS related */

enum {
	STATUS_SUCCESS = 0,
	STATUS_FAILURE,
	STATUS_RESOURCES,
	STATUS_PENDING,
};

/* flags for options */
#define DEVICE_FLAGS_UNPLUG		BIT(0)
#define DEVICE_FLAGS_DISCONNECTED	BIT(1)

struct vnt_private {
	/* mac80211 */
	struct ieee80211_hw *hw;
	struct ieee80211_vif *vif;
	u8 mac_hw;
	/* netdev */
	struct usb_device *usb;

	u64 tsf_time;
	u8 rx_rate;

	u32 rx_buf_sz;
	int mc_list_count;

	spinlock_t lock;
	struct mutex usb_lock;

	unsigned long flags;

	/* USB */
	struct urb *interrupt_urb;
	u32 int_interval;

	/* Variables to track resources for the BULK In Pipe */
	struct vnt_rcb *rcb[CB_MAX_RX_DESC];
	u32 num_rcb;

	/* Variables to track resources for the BULK Out Pipe */
	struct vnt_usb_send_context *tx_context[CB_MAX_TX_DESC];
	u32 num_tx_context;

	/* Variables to track resources for the Interrupt In Pipe */
	struct vnt_interrupt_buffer int_buf;

	/* Version control */
	u16 firmware_version;
	u8 local_id;
	u8 rf_type;
	u8 bb_rx_conf;

	struct vnt_cmd_card_init init_command;
	struct vnt_rsp_card_init init_response;
	u8 current_net_addr[ETH_ALEN];
	u8 permanent_net_addr[ETH_ALEN];

	u8 exist_sw_net_addr;

	u64 current_tsf;

	/* 802.11 MAC specific */
	u32 current_rssi;

	/* Antenna Diversity */
	int tx_rx_ant_inv;
	u32 rx_antenna_sel;
	u8 rx_antenna_mode;
	u8 tx_antenna_mode;
	u8 radio_ctl;

	/* IFS & Cw */
	u32 sifs;  /* Current SIFS */
	u32 difs;  /* Current DIFS */
	u32 eifs;  /* Current EIFS */
	u32 slot;  /* Current SlotTime */

	/* Rate */
	u8 bb_type; /* 0: 11A, 1:11B, 2:11G */
	u8 packet_type; /* 0:11a 1:11b 2:11gb 3:11ga */
	u32 basic_rates;
	u8 top_ofdm_basic_rate;
	u8 top_cck_basic_rate;

	u8 eeprom[EEP_MAX_CONTEXT_SIZE];  /*u32 alignment */

	u8 preamble_type;

	/* For RF Power table */
	u8 cck_pwr;
	u8 ofdm_pwr_g;
	u8 ofdm_pwr_a;
	u8 power;
	u8 cck_pwr_tbl[14];
	u8 ofdm_pwr_tbl[14];
	u8 ofdm_a_pwr_tbl[42];

	u16 current_rate;
	u16 tx_rate_fb0;
	u16 tx_rate_fb1;

	u8 short_retry_limit;
	u8 long_retry_limit;

	enum nl80211_iftype op_mode;

	int short_slot_time;

	/* Power save */
	u16 current_aid;

	/* Beacon releated */
	u16 seq_counter;

	enum vnt_cmd_state command_state;

	enum vnt_cmd command;

	/* 802.11 counter */

	enum vnt_cmd cmd_queue[CMD_Q_SIZE];
	u32 cmd_dequeue_idx;
	u32 cmd_enqueue_idx;
	u32 free_cmd_queue;
	int cmd_running;

	unsigned long key_entry_inuse;

	u8 auto_fb_ctrl;

	/* For Update BaseBand VGA Gain Offset */
	u8 bb_vga[BB_VGA_LEVEL];

	u8 bb_pre_ed_rssi;
	u8 bb_pre_ed_index;

	/* command timer */
	struct delayed_work run_command_work;

	struct ieee80211_low_level_stats low_stats;
};

#define ADD_ONE_WITH_WRAP_AROUND(uVar, uModulo) {	\
	if ((uVar) >= ((uModulo) - 1))			\
		(uVar) = 0;				\
	else						\
		(uVar)++;				\
}

#define MP_TEST_FLAGS(_M, _F)            (((_M)->flags & (_F)) == (_F))

int vnt_init(struct vnt_private *priv);

#endif
