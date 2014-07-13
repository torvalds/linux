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

#define DBG_PRT(l, p, args...) { if (l <= msglevel) printk(p, ##args); }

typedef enum __device_msg_level {
	MSG_LEVEL_ERR = 0,            /* Errors causing abnormal operation */
	MSG_LEVEL_NOTICE = 1,         /* Errors needing user notification */
	MSG_LEVEL_INFO = 2,           /* Normal message. */
	MSG_LEVEL_VERBOSE = 3,        /* Will report all trival errors. */
	MSG_LEVEL_DEBUG = 4           /* Only for debug purpose. */
} DEVICE_MSG_LEVEL, *PDEVICE_MSG_LEVEL;

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
	void *Next;
	signed long Ref;
	void *pDevice;
	struct urb *pUrb;
	struct sk_buff *skb;
	int bBoolInUse;
};

/* used to track bulk out irps */
struct vnt_usb_send_context {
	void *priv;
	struct sk_buff *skb;
	struct urb *urb;
	struct ieee80211_hdr *hdr;
	unsigned int buf_len;
	u16 tx_hdr_size;
	u8 type;
	u8 pkt_no;
	u8 fb_option;
	bool in_use;
	unsigned char data[MAX_TOTAL_SIZE_WITH_ALL_HEADERS];
};

/* tx packet info for rxtx */
struct vnt_tx_pkt_info {
	u16 fifo_ctl;
	u8 dest_addr[ETH_ALEN];
};

/*
 * Structure to keep track of USB interrupt packets
 */
struct vnt_interrupt_buffer {
	u8 *data_buf;
	bool in_use;
};

/*++ NDIS related */

typedef enum __DEVICE_NDIS_STATUS {
    STATUS_SUCCESS = 0,
    STATUS_FAILURE,
    STATUS_RESOURCES,
    STATUS_PENDING,
} DEVICE_NDIS_STATUS, *PDEVICE_NDIS_STATUS;


/* flags for options */
#define     DEVICE_FLAGS_UNPLUG          0x00000001UL

/* flags for driver status */
#define     DEVICE_FLAGS_OPENED          0x00010000UL

struct vnt_private {
	/* mac80211 */
	struct ieee80211_hw *hw;
	struct ieee80211_vif *vif;
	u8 mac_hw;
	/* netdev */
	struct usb_device *usb;
	struct net_device_stats stats;

	u64 tsf_time;
	u8 rx_rate;

	u32 rx_buf_sz;
	int mc_list_count;

	spinlock_t lock;
	struct mutex usb_lock;

	u32 flags;
	unsigned long Flags;

	/* USB */
	struct urb *pInterruptURB;
	u32 int_interval;

	/* Variables to track resources for the BULK In Pipe */
	struct vnt_rcb *apRCB[CB_MAX_RX_DESC];
	u32 cbRD;

	/* Variables to track resources for the BULK Out Pipe */
	struct vnt_usb_send_context *apTD[CB_MAX_TX_DESC];
	u32 cbTD;
	struct vnt_tx_pkt_info pkt_info[16];

	/* Variables to track resources for the Interrupt In Pipe */
	struct vnt_interrupt_buffer int_buf;

	/* Version control */
	u16 wFirmwareVersion;
	u8 byLocalID;
	u8 byRFType;
	u8 byBBRxConf;

	u8 byZoneType;

	struct vnt_cmd_card_init init_command;
	struct vnt_rsp_card_init init_response;
	u8 abyCurrentNetAddr[ETH_ALEN];
	u8 abyPermanentNetAddr[ETH_ALEN];

	int bExistSWNetAddr;

	u64 qwCurrTSF;

	/* 802.11 MAC specific */
	u32 uCurrRSSI;

	/* Antenna Diversity */
	int bTxRxAntInv;
	u32 dwRxAntennaSel;
	u32 dwTxAntennaSel;
	u8 byAntennaCount;
	u8 byRxAntennaMode;
	u8 byTxAntennaMode;
	u8 byRadioCtl;
	u8 bHWRadioOff;

	/* IFS & Cw */
	u32 uSIFS;  /* Current SIFS */
	u32 uDIFS;  /* Current DIFS */
	u32 uEIFS;  /* Current EIFS */
	u32 uSlot;  /* Current SlotTime */
	u32 uCwMin; /* Current CwMin */
	u32 uCwMax; /* CwMax is fixed on 1023 */

	/* Rate */
	u8 byBBType; /* 0: 11A, 1:11B, 2:11G */
	u8 byPacketType; /* 0:11a 1:11b 2:11gb 3:11ga */
	u32 wBasicRate;
	u8 byTopOFDMBasicRate;
	u8 byTopCCKBasicRate;

	u8 abyEEPROM[EEP_MAX_CONTEXT_SIZE];  /*u32 alignment */

	u8 byPreambleType;

	/* For RF Power table */
	u8 byCCKPwr;
	u8 byOFDMPwrG;
	u8 byOFDMPwrA;
	u8 byCurPwr;
	u8 abyCCKPwrTbl[14];
	u8 abyOFDMPwrTbl[14];
	u8 abyOFDMAPwrTbl[42];

	u16 wCurrentRate;
	u16 tx_rate_fb0;
	u16 tx_rate_fb1;

	u8 byShortRetryLimit;
	u8 byLongRetryLimit;

	enum nl80211_iftype op_mode;

	int bShortSlotTime;
	int bBarkerPreambleMd;

	int bRadioControlOff;
	int bRadioOff;

	/* Power save */
	u16 current_aid;

	/* Beacon releated */
	u16 wSeqCounter;

	enum vnt_cmd_state command_state;

	enum vnt_cmd command;

	int bStopDataPkt;

	/* 802.11 counter */

	CMD_ITEM eCmdQueue[CMD_Q_SIZE];
	u32 uCmdDequeueIdx;
	u32 uCmdEnqueueIdx;
	u32 cbFreeCmdQueue;
	int bCmdRunning;
	int bCmdClear;
	int bNeedRadioOFF;

	unsigned long key_entry_inuse;

	u8 byAutoFBCtrl;

	/* For Update BaseBand VGA Gain Offset */
	u32 uBBVGADiffCount;
	u8 byBBVGANew;
	u8 byBBVGACurrent;
	u8 abyBBVGA[BB_VGA_LEVEL];
	signed long ldBmThreshold[BB_VGA_LEVEL];

	u8 byBBPreEDRSSI;
	u8 byBBPreEDIndex;

	int bRadioCmd;

	/* command timer */
	struct delayed_work run_command_work;

	u8 tx_data_time_out;

	int bChannelSwitch;
	u8 byNewChannel;
	u8 byChannelSwitchCount;

	struct iw_statistics wstats; /* wireless stats */
	struct ieee80211_low_level_stats low_stats;
};

#define ADD_ONE_WITH_WRAP_AROUND(uVar, uModulo) {   \
    if ((uVar) >= ((uModulo) - 1))                  \
        (uVar) = 0;                                 \
    else                                            \
        (uVar)++;                                   \
}

#define fMP_DISCONNECTED                    0x00000002
#define fMP_POST_READS                      0x00000100
#define fMP_POST_WRITES                     0x00000200

#define MP_SET_FLAG(_M, _F)             ((_M)->Flags |= (_F))
#define MP_CLEAR_FLAG(_M, _F)            ((_M)->Flags &= ~(_F))
#define MP_TEST_FLAGS(_M, _F)            (((_M)->Flags & (_F)) == (_F))

#define MP_IS_READY(_M)        (((_M)->Flags & fMP_DISCONNECTED) == 0)

int vnt_init(struct vnt_private *priv);

#endif
