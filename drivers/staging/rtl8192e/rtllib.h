/*
 * Merged with mainline rtllib.h in Aug 2004.  Original ieee802_11
 * remains copyright by the original authors
 *
 * Portions of the merged code are based on Host AP (software wireless
 * LAN access point) driver for Intersil Prism2/2.5/3.
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <jkmaline@cc.hut.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 *
 * Adaption to a generic IEEE 802.11 stack by James Ketrenos
 * <jketreno@linux.intel.com>
 * Copyright (c) 2004, Intel Corporation
 *
 * Modified for Realtek's wi-fi cards by Andrea Merello
 * <andreamrl@tiscali.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */
#ifndef RTLLIB_H
#define RTLLIB_H
#include <linux/if_ether.h> /* ETH_ALEN */
#include <linux/kernel.h>   /* ARRAY_SIZE */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/semaphore.h>

#include <linux/delay.h>
#include <linux/wireless.h>

#include "rtllib_debug.h"
#include "rtl819x_HT.h"
#include "rtl819x_BA.h"
#include "rtl819x_TS.h"

#include <linux/netdevice.h>
#include <linux/if_arp.h> /* ARPHRD_ETHER */
#include <net/lib80211.h>

#define MAX_PRECMD_CNT 16
#define MAX_RFDEPENDCMD_CNT 16
#define MAX_POSTCMD_CNT 16

#ifndef WIRELESS_SPY
#define WIRELESS_SPY
#endif
#include <net/iw_handler.h>

#ifndef IW_MODE_MONITOR
#define IW_MODE_MONITOR 6
#endif

#ifndef IWEVCUSTOM
#define IWEVCUSTOM 0x8c02
#endif

#ifndef IW_CUSTOM_MAX
/* Max number of char in custom event - use multiple of them if needed */
#define IW_CUSTOM_MAX	256	/* In bytes */
#endif

#ifndef container_of
/**
 * container_of - cast a member of a structure out to the containing structure
 *
 * @ptr:	the pointer to the member.
 * @type:       the type of the container struct this is embedded in.
 * @member:     the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({		      \
	const typeof(((type *)0)->member)*__mptr = (ptr);    \
	(type *)((char *)__mptr - offsetof(type, member)); })
#endif

#define skb_tail_pointer_rsl(skb) skb_tail_pointer(skb)

#define EXPORT_SYMBOL_RSL(x) EXPORT_SYMBOL(x)


#define queue_delayed_work_rsl(x, y, z) queue_delayed_work(x, y, z)
#define INIT_DELAYED_WORK_RSL(x, y, z) INIT_DELAYED_WORK(x, y)

#define queue_work_rsl(x, y) queue_work(x, y)
#define INIT_WORK_RSL(x, y, z) INIT_WORK(x, y)

#define container_of_work_rsl(x, y, z) container_of(x, y, z)
#define container_of_dwork_rsl(x, y, z)				\
	container_of(container_of(x, struct delayed_work, work), y, z)

#define iwe_stream_add_event_rsl(info, start, stop, iwe, len)	\
	iwe_stream_add_event(info, start, stop, iwe, len)

#define iwe_stream_add_point_rsl(info, start, stop, iwe, p)	\
	iwe_stream_add_point(info, start, stop, iwe, p)

#define usb_alloc_urb_rsl(x, y) usb_alloc_urb(x, y)
#define usb_submit_urb_rsl(x, y) usb_submit_urb(x, y)

static inline void *netdev_priv_rsl(struct net_device *dev)
{
	return netdev_priv(dev);
}

#define KEY_TYPE_NA		0x0
#define KEY_TYPE_WEP40		0x1
#define KEY_TYPE_TKIP		0x2
#define KEY_TYPE_CCMP		0x4
#define KEY_TYPE_WEP104		0x5
/* added for rtl819x tx procedure */
#define MAX_QUEUE_SIZE		0x10

#define BK_QUEUE			       0
#define BE_QUEUE			       1
#define VI_QUEUE			       2
#define VO_QUEUE			       3
#define HCCA_QUEUE			     4
#define TXCMD_QUEUE			    5
#define MGNT_QUEUE			     6
#define HIGH_QUEUE			     7
#define BEACON_QUEUE			   8

#define LOW_QUEUE			      BE_QUEUE
#define NORMAL_QUEUE			   MGNT_QUEUE

#ifndef IW_MODE_MESH
#define IW_MODE_MESH			7
#endif
#define AMSDU_SUBHEADER_LEN 14
#define SWRF_TIMEOUT				50

#define IE_CISCO_FLAG_POSITION		0x08
#define SUPPORT_CKIP_MIC			0x08
#define SUPPORT_CKIP_PK			0x10
#define	RT_RF_OFF_LEVL_ASPM			BIT0
#define	RT_RF_OFF_LEVL_CLK_REQ		BIT1
#define	RT_RF_OFF_LEVL_PCI_D3			BIT2
#define	RT_RF_OFF_LEVL_HALT_NIC		BIT3
#define	RT_RF_OFF_LEVL_FREE_FW		BIT4
#define	RT_RF_OFF_LEVL_FW_32K		BIT5
#define	RT_RF_PS_LEVEL_ALWAYS_ASPM	BIT6
#define	RT_RF_LPS_DISALBE_2R			BIT30
#define	RT_RF_LPS_LEVEL_ASPM			BIT31
#define	RT_IN_PS_LEVEL(pPSC, _PS_FLAG)		\
	((pPSC->CurPsLevel & _PS_FLAG) ? true : false)
#define	RT_CLEAR_PS_LEVEL(pPSC, _PS_FLAG)	\
	(pPSC->CurPsLevel &= (~(_PS_FLAG)))
#define	RT_SET_PS_LEVEL(pPSC, _PS_FLAG)	(pPSC->CurPsLevel |= _PS_FLAG)

/* defined for skb cb field */
/* At most 28 byte */
struct cb_desc {
	/* Tx Desc Related flags (8-9) */
	u8 bLastIniPkt:1;
	u8 bCmdOrInit:1;
	u8 bFirstSeg:1;
	u8 bLastSeg:1;
	u8 bEncrypt:1;
	u8 bTxDisableRateFallBack:1;
	u8 bTxUseDriverAssingedRate:1;
	u8 bHwSec:1;

	u8 nStuckCount;

	/* Tx Firmware Relaged flags (10-11)*/
	u8 bCTSEnable:1;
	u8 bRTSEnable:1;
	u8 bUseShortGI:1;
	u8 bUseShortPreamble:1;
	u8 bTxEnableFwCalcDur:1;
	u8 bAMPDUEnable:1;
	u8 bRTSSTBC:1;
	u8 RTSSC:1;

	u8 bRTSBW:1;
	u8 bPacketBW:1;
	u8 bRTSUseShortPreamble:1;
	u8 bRTSUseShortGI:1;
	u8 bMulticast:1;
	u8 bBroadcast:1;
	u8 drv_agg_enable:1;
	u8 reserved2:1;

	/* Tx Desc related element(12-19) */
	u8 rata_index;
	u8 queue_index;
	u16 txbuf_size;
	u8 RATRIndex;
	u8 bAMSDU:1;
	u8 bFromAggrQ:1;
	u8 reserved6:6;
	u8 macId;
	u8 priority;

	/* Tx firmware related element(20-27) */
	u8 data_rate;
	u8 rts_rate;
	u8 ampdu_factor;
	u8 ampdu_density;
	u8 DrvAggrNum;
	u8 bdhcp;
	u16 pkt_size;
	u8 bIsSpecialDataFrame;

	u8 bBTTxPacket;
	u8 bIsBTProbRsp;
};

enum sw_chnl_cmd_id {
	CmdID_End,
	CmdID_SetTxPowerLevel,
	CmdID_BBRegWrite10,
	CmdID_WritePortUlong,
	CmdID_WritePortUshort,
	CmdID_WritePortUchar,
	CmdID_RF_WriteReg,
};

struct sw_chnl_cmd {
	enum sw_chnl_cmd_id CmdID;
	u32			Para1;
	u32			Para2;
	u32			msDelay;
} __packed;

/*--------------------------Define -------------------------------------------*/
#define MGN_1M		  0x02
#define MGN_2M		  0x04
#define MGN_5_5M		0x0b
#define MGN_11M		 0x16

#define MGN_6M		  0x0c
#define MGN_9M		  0x12
#define MGN_12M		 0x18
#define MGN_18M		 0x24
#define MGN_24M		 0x30
#define MGN_36M		 0x48
#define MGN_48M		 0x60
#define MGN_54M		 0x6c

#define MGN_MCS0		0x80
#define MGN_MCS1		0x81
#define MGN_MCS2		0x82
#define MGN_MCS3		0x83
#define MGN_MCS4		0x84
#define MGN_MCS5		0x85
#define MGN_MCS6		0x86
#define MGN_MCS7		0x87
#define MGN_MCS8		0x88
#define MGN_MCS9		0x89
#define MGN_MCS10	       0x8a
#define MGN_MCS11	       0x8b
#define MGN_MCS12	       0x8c
#define MGN_MCS13	       0x8d
#define MGN_MCS14	       0x8e
#define MGN_MCS15	       0x8f
#define	MGN_MCS0_SG			0x90
#define	MGN_MCS1_SG			0x91
#define	MGN_MCS2_SG			0x92
#define	MGN_MCS3_SG			0x93
#define	MGN_MCS4_SG			0x94
#define	MGN_MCS5_SG			0x95
#define	MGN_MCS6_SG			0x96
#define	MGN_MCS7_SG			0x97
#define	MGN_MCS8_SG			0x98
#define	MGN_MCS9_SG			0x99
#define	MGN_MCS10_SG		0x9a
#define	MGN_MCS11_SG		0x9b
#define	MGN_MCS12_SG		0x9c
#define	MGN_MCS13_SG		0x9d
#define	MGN_MCS14_SG		0x9e
#define	MGN_MCS15_SG		0x9f


enum	_ReasonCode {
	unspec_reason	= 0x1,
	auth_not_valid	= 0x2,
	deauth_lv_ss	= 0x3,
	inactivity		= 0x4,
	ap_overload	= 0x5,
	class2_err		= 0x6,
	class3_err		= 0x7,
	disas_lv_ss	= 0x8,
	asoc_not_auth	= 0x9,

	mic_failure	= 0xe,

	invalid_IE		= 0x0d,
	four_way_tmout	= 0x0f,
	two_way_tmout	= 0x10,
	IE_dismatch	= 0x11,
	invalid_Gcipher = 0x12,
	invalid_Pcipher = 0x13,
	invalid_AKMP	= 0x14,
	unsup_RSNIEver = 0x15,
	invalid_RSNIE	= 0x16,
	auth_802_1x_fail = 0x17,
	ciper_reject		= 0x18,

	QoS_unspec		= 0x20,
	QAP_bandwidth	= 0x21,
	poor_condition	= 0x22,
	no_facility	= 0x23,
	req_declined	= 0x25,
	invalid_param	= 0x26,
	req_not_honored = 0x27,
	TS_not_created	= 0x2F,
	DL_not_allowed	= 0x30,
	dest_not_exist	= 0x31,
	dest_not_QSTA	= 0x32,
};

enum hal_def_variable {
	HAL_DEF_TPC_ENABLE,
	HAL_DEF_INIT_GAIN,
	HAL_DEF_PROT_IMP_MODE,
	HAL_DEF_HIGH_POWER_MECHANISM,
	HAL_DEF_RATE_ADAPTIVE_MECHANISM,
	HAL_DEF_ANTENNA_DIVERSITY_MECHANISM,
	HAL_DEF_LED,
	HAL_DEF_CW_MAX_MIN,

	HAL_DEF_WOWLAN,
	HAL_DEF_ENDPOINTS,
	HAL_DEF_MIN_TX_POWER_DBM,
	HAL_DEF_MAX_TX_POWER_DBM,
	HW_DEF_EFUSE_REPG_SECTION1_FLAG,
	HW_DEF_EFUSE_REPG_DATA,
	HW_DEF_GPIO,
	HAL_DEF_PCI_SUPPORT_ASPM,
	HAL_DEF_THERMAL_VALUE,
	HAL_DEF_USB_IN_TOKEN_REV,
};

enum hw_variables {
	HW_VAR_ETHER_ADDR,
	HW_VAR_MULTICAST_REG,
	HW_VAR_BASIC_RATE,
	HW_VAR_BSSID,
	HW_VAR_MEDIA_STATUS,
	HW_VAR_SECURITY_CONF,
	HW_VAR_BEACON_INTERVAL,
	HW_VAR_ATIM_WINDOW,
	HW_VAR_LISTEN_INTERVAL,
	HW_VAR_CS_COUNTER,
	HW_VAR_DEFAULTKEY0,
	HW_VAR_DEFAULTKEY1,
	HW_VAR_DEFAULTKEY2,
	HW_VAR_DEFAULTKEY3,
	HW_VAR_SIFS,
	HW_VAR_DIFS,
	HW_VAR_EIFS,
	HW_VAR_SLOT_TIME,
	HW_VAR_ACK_PREAMBLE,
	HW_VAR_CW_CONFIG,
	HW_VAR_CW_VALUES,
	HW_VAR_RATE_FALLBACK_CONTROL,
	HW_VAR_CONTENTION_WINDOW,
	HW_VAR_RETRY_COUNT,
	HW_VAR_TR_SWITCH,
	HW_VAR_COMMAND,
	HW_VAR_WPA_CONFIG,
	HW_VAR_AMPDU_MIN_SPACE,
	HW_VAR_SHORTGI_DENSITY,
	HW_VAR_AMPDU_FACTOR,
	HW_VAR_MCS_RATE_AVAILABLE,
	HW_VAR_AC_PARAM,
	HW_VAR_ACM_CTRL,
	HW_VAR_DIS_Req_Qsize,
	HW_VAR_CCX_CHNL_LOAD,
	HW_VAR_CCX_NOISE_HISTOGRAM,
	HW_VAR_CCX_CLM_NHM,
	HW_VAR_TxOPLimit,
	HW_VAR_TURBO_MODE,
	HW_VAR_RF_STATE,
	HW_VAR_RF_OFF_BY_HW,
	HW_VAR_BUS_SPEED,
	HW_VAR_SET_DEV_POWER,

	HW_VAR_RCR,
	HW_VAR_RATR_0,
	HW_VAR_RRSR,
	HW_VAR_CPU_RST,
	HW_VAR_CECHK_BSSID,
	HW_VAR_LBK_MODE,
	HW_VAR_AES_11N_FIX,
	HW_VAR_USB_RX_AGGR,
	HW_VAR_USER_CONTROL_TURBO_MODE,
	HW_VAR_RETRY_LIMIT,
	HW_VAR_INIT_TX_RATE,
	HW_VAR_TX_RATE_REG,
	HW_VAR_EFUSE_USAGE,
	HW_VAR_EFUSE_BYTES,
	HW_VAR_AUTOLOAD_STATUS,
	HW_VAR_RF_2R_DISABLE,
	HW_VAR_SET_RPWM,
	HW_VAR_H2C_FW_PWRMODE,
	HW_VAR_H2C_FW_JOINBSSRPT,
	HW_VAR_1X1_RECV_COMBINE,
	HW_VAR_STOP_SEND_BEACON,
	HW_VAR_TSF_TIMER,
	HW_VAR_IO_CMD,

	HW_VAR_RF_RECOVERY,
	HW_VAR_H2C_FW_UPDATE_GTK,
	HW_VAR_WF_MASK,
	HW_VAR_WF_CRC,
	HW_VAR_WF_IS_MAC_ADDR,
	HW_VAR_H2C_FW_OFFLOAD,
	HW_VAR_RESET_WFCRC,

	HW_VAR_HANDLE_FW_C2H,
	HW_VAR_DL_FW_RSVD_PAGE,
	HW_VAR_AID,
	HW_VAR_HW_SEQ_ENABLE,
	HW_VAR_CORRECT_TSF,
	HW_VAR_BCN_VALID,
	HW_VAR_FWLPS_RF_ON,
	HW_VAR_DUAL_TSF_RST,
	HW_VAR_SWITCH_EPHY_WoWLAN,
	HW_VAR_INT_MIGRATION,
	HW_VAR_INT_AC,
	HW_VAR_RF_TIMING,
};

enum rt_op_mode {
	RT_OP_MODE_AP,
	RT_OP_MODE_INFRASTRUCTURE,
	RT_OP_MODE_IBSS,
	RT_OP_MODE_NO_LINK,
};


#define aSifsTime						\
	 (((priv->rtllib->current_network.mode == IEEE_A)	\
	|| (priv->rtllib->current_network.mode == IEEE_N_24G)	\
	|| (priv->rtllib->current_network.mode == IEEE_N_5G)) ? 16 : 10)

#define MGMT_QUEUE_NUM 5

#define IEEE_CMD_SET_WPA_PARAM			1
#define	IEEE_CMD_SET_WPA_IE			2
#define IEEE_CMD_SET_ENCRYPTION			3
#define IEEE_CMD_MLME				4

#define IEEE_PARAM_WPA_ENABLED			1
#define IEEE_PARAM_TKIP_COUNTERMEASURES		2
#define IEEE_PARAM_DROP_UNENCRYPTED		3
#define IEEE_PARAM_PRIVACY_INVOKED		4
#define IEEE_PARAM_AUTH_ALGS			5
#define IEEE_PARAM_IEEE_802_1X			6
#define IEEE_PARAM_WPAX_SELECT			7
#define IEEE_PROTO_WPA				1
#define IEEE_PROTO_RSN				2
#define IEEE_WPAX_USEGROUP			0
#define IEEE_WPAX_WEP40				1
#define IEEE_WPAX_TKIP				2
#define IEEE_WPAX_WRAP				3
#define IEEE_WPAX_CCMP				4
#define IEEE_WPAX_WEP104			5

#define IEEE_KEY_MGMT_IEEE8021X			1
#define IEEE_KEY_MGMT_PSK			2

#define IEEE_MLME_STA_DEAUTH			1
#define IEEE_MLME_STA_DISASSOC			2


#define IEEE_CRYPT_ERR_UNKNOWN_ALG		2
#define IEEE_CRYPT_ERR_UNKNOWN_ADDR		3
#define IEEE_CRYPT_ERR_CRYPT_INIT_FAILED	4
#define IEEE_CRYPT_ERR_KEY_SET_FAILED		5
#define IEEE_CRYPT_ERR_TX_KEY_SET_FAILED	6
#define IEEE_CRYPT_ERR_CARD_CONF_FAILED		7
#define	IEEE_CRYPT_ALG_NAME_LEN			16

#define MAX_IE_LEN  0xff
#define RT_ASSERT_RET(_Exp) do {} while (0)
#define RT_ASSERT_RET_VALUE(_Exp, Ret)		\
	do {} while (0)

struct ieee_param {
	u32 cmd;
	u8 sta_addr[ETH_ALEN];
	union {
		struct {
			u8 name;
			u32 value;
		} wpa_param;
		struct {
			u32 len;
			u8 reserved[32];
			u8 data[0];
		} wpa_ie;
		struct {
			int command;
			int reason_code;
		} mlme;
		struct {
			u8 alg[IEEE_CRYPT_ALG_NAME_LEN];
			u8 set_tx;
			u32 err;
			u8 idx;
			u8 seq[8]; /* sequence counter (set: RX, get: TX) */
			u16 key_len;
			u8 key[0];
		} crypt;
	} u;
};


#if WIRELESS_EXT < 17
#define IW_QUAL_QUAL_INVALID   0x10
#define IW_QUAL_LEVEL_INVALID  0x20
#define IW_QUAL_NOISE_INVALID  0x40
#define IW_QUAL_QUAL_UPDATED   0x1
#define IW_QUAL_LEVEL_UPDATED  0x2
#define IW_QUAL_NOISE_UPDATED  0x4
#endif

#define MSECS(t) msecs_to_jiffies(t)
#define msleep_interruptible_rsl  msleep_interruptible

#define RTLLIB_DATA_LEN		2304
/* Maximum size for the MA-UNITDATA primitive, 802.11 standard section
   6.2.1.1.2.

   The figure in section 7.1.2 suggests a body size of up to 2312
   bytes is allowed, which is a bit confusing, I suspect this
   represents the 2304 bytes of real data, plus a possible 8 bytes of
   WEP IV and ICV. (this interpretation suggested by Ramiro Barreiro) */
#define RTLLIB_1ADDR_LEN 10
#define RTLLIB_2ADDR_LEN 16
#define RTLLIB_3ADDR_LEN 24
#define RTLLIB_4ADDR_LEN 30
#define RTLLIB_FCS_LEN    4
#define RTLLIB_HLEN		  (RTLLIB_4ADDR_LEN)
#define RTLLIB_FRAME_LEN	     (RTLLIB_DATA_LEN + RTLLIB_HLEN)
#define RTLLIB_MGMT_HDR_LEN 24
#define RTLLIB_DATA_HDR3_LEN 24
#define RTLLIB_DATA_HDR4_LEN 30

#define RTLLIB_SKBBUFFER_SIZE 2500

#define MIN_FRAG_THRESHOLD     256U
#define MAX_FRAG_THRESHOLD     2346U
#define MAX_HT_DATA_FRAG_THRESHOLD 0x2000

#define HT_AMSDU_SIZE_4K 3839
#define HT_AMSDU_SIZE_8K 7935

/* Frame control field constants */
#define RTLLIB_FCTL_VERS		0x0003
#define RTLLIB_FCTL_FTYPE		0x000c
#define RTLLIB_FCTL_STYPE		0x00f0
#define RTLLIB_FCTL_FRAMETYPE	0x00fc
#define RTLLIB_FCTL_TODS		0x0100
#define RTLLIB_FCTL_FROMDS		0x0200
#define RTLLIB_FCTL_DSTODS		0x0300
#define RTLLIB_FCTL_MOREFRAGS	0x0400
#define RTLLIB_FCTL_RETRY		0x0800
#define RTLLIB_FCTL_PM		0x1000
#define RTLLIB_FCTL_MOREDATA		0x2000
#define RTLLIB_FCTL_WEP		0x4000
#define RTLLIB_FCTL_ORDER		0x8000

#define RTLLIB_FTYPE_MGMT		0x0000
#define RTLLIB_FTYPE_CTL		0x0004
#define RTLLIB_FTYPE_DATA		0x0008

/* management */
#define RTLLIB_STYPE_ASSOC_REQ	0x0000
#define RTLLIB_STYPE_ASSOC_RESP		0x0010
#define RTLLIB_STYPE_REASSOC_REQ	0x0020
#define RTLLIB_STYPE_REASSOC_RESP	0x0030
#define RTLLIB_STYPE_PROBE_REQ	0x0040
#define RTLLIB_STYPE_PROBE_RESP	0x0050
#define RTLLIB_STYPE_BEACON		0x0080
#define RTLLIB_STYPE_ATIM		0x0090
#define RTLLIB_STYPE_DISASSOC	0x00A0
#define RTLLIB_STYPE_AUTH		0x00B0
#define RTLLIB_STYPE_DEAUTH		0x00C0
#define RTLLIB_STYPE_MANAGE_ACT	0x00D0

/* control */
#define RTLLIB_STYPE_PSPOLL		0x00A0
#define RTLLIB_STYPE_RTS		0x00B0
#define RTLLIB_STYPE_CTS		0x00C0
#define RTLLIB_STYPE_ACK		0x00D0
#define RTLLIB_STYPE_CFEND		0x00E0
#define RTLLIB_STYPE_CFENDACK	0x00F0
#define RTLLIB_STYPE_BLOCKACK   0x0094

/* data */
#define RTLLIB_STYPE_DATA		0x0000
#define RTLLIB_STYPE_DATA_CFACK	0x0010
#define RTLLIB_STYPE_DATA_CFPOLL	0x0020
#define RTLLIB_STYPE_DATA_CFACKPOLL	0x0030
#define RTLLIB_STYPE_NULLFUNC	0x0040
#define RTLLIB_STYPE_CFACK		0x0050
#define RTLLIB_STYPE_CFPOLL		0x0060
#define RTLLIB_STYPE_CFACKPOLL	0x0070
#define RTLLIB_STYPE_QOS_DATA	0x0080
#define RTLLIB_STYPE_QOS_NULL	0x00C0

#define RTLLIB_SCTL_FRAG		0x000F
#define RTLLIB_SCTL_SEQ		0xFFF0

/* QOS control */
#define RTLLIB_QCTL_TID	      0x000F

#define	FC_QOS_BIT					BIT7
#define IsDataFrame(pdu)	(((pdu[0] & 0x0C) == 0x08) ? true : false)
#define	IsLegacyDataFrame(pdu)	(IsDataFrame(pdu) && (!(pdu[0]&FC_QOS_BIT)))
#define IsQoSDataFrame(pframe)			\
	((*(u16 *)pframe&(RTLLIB_STYPE_QOS_DATA|RTLLIB_FTYPE_DATA)) ==	\
	(RTLLIB_STYPE_QOS_DATA|RTLLIB_FTYPE_DATA))
#define Frame_Order(pframe)     (*(u16 *)pframe&RTLLIB_FCTL_ORDER)
#define SN_LESS(a, b)		(((a-b)&0x800) != 0)
#define SN_EQUAL(a, b)	(a == b)
#define MAX_DEV_ADDR_SIZE 8

enum act_category {
	ACT_CAT_QOS = 1,
	ACT_CAT_DLS = 2,
	ACT_CAT_BA  = 3,
	ACT_CAT_HT  = 7,
	ACT_CAT_WMM = 17,
};

enum ts_action {
	ACT_ADDTSREQ = 0,
	ACT_ADDTSRSP = 1,
	ACT_DELTS    = 2,
	ACT_SCHEDULE = 3,
};

enum ba_action {
	ACT_ADDBAREQ = 0,
	ACT_ADDBARSP = 1,
	ACT_DELBA    = 2,
};

enum init_gain_op_type {
	IG_Backup = 0,
	IG_Restore,
	IG_Max
};

enum led_ctl_mode {
	LED_CTL_POWER_ON = 1,
	LED_CTL_LINK = 2,
	LED_CTL_NO_LINK = 3,
	LED_CTL_TX = 4,
	LED_CTL_RX = 5,
	LED_CTL_SITE_SURVEY = 6,
	LED_CTL_POWER_OFF = 7,
	LED_CTL_START_TO_LINK = 8,
	LED_CTL_START_WPS = 9,
	LED_CTL_STOP_WPS = 10,
	LED_CTL_START_WPS_BOTTON = 11,
	LED_CTL_STOP_WPS_FAIL = 12,
	 LED_CTL_STOP_WPS_FAIL_OVERLAP = 13,
};

enum rt_rf_type_def {
	RF_1T2R = 0,
	RF_2T4R,
	RF_2T2R,
	RF_1T1R,
	RF_2T2R_GREEN,
	RF_819X_MAX_TYPE
};

enum wireless_mode {
	WIRELESS_MODE_UNKNOWN = 0x00,
	WIRELESS_MODE_A = 0x01,
	WIRELESS_MODE_B = 0x02,
	WIRELESS_MODE_G = 0x04,
	WIRELESS_MODE_AUTO = 0x08,
	WIRELESS_MODE_N_24G = 0x10,
	WIRELESS_MODE_N_5G = 0x20
};

enum wireless_network_type {
	WIRELESS_11B = 1,
	WIRELESS_11G = 2,
	WIRELESS_11A = 4,
	WIRELESS_11N = 8
};

#define OUI_SUBTYPE_WMM_INFO		0
#define OUI_SUBTYPE_WMM_PARAM	1
#define OUI_SUBTYPE_QOS_CAPABI	5

/* debug macros */
extern u32 rtllib_debug_level;
#define RTLLIB_DEBUG(level, fmt, args...) \
do {								\
	if (rtllib_debug_level & (level))			\
		printk(KERN_DEBUG "rtllib: " fmt, ## args);	\
} while (0)

#define RTLLIB_DEBUG_DATA(level, data, datalen)	\
	do {							\
		if ((rtllib_debug_level & (level)) == (level)) {	\
			int i;					\
			u8 *pdata = (u8 *)data;			\
			printk(KERN_DEBUG "rtllib: %s()\n", __func__);	\
			for (i = 0; i < (int)(datalen); i++)	{	\
				printk("%2.2x ", pdata[i]);		\
				if ((i+1)%16 == 0)			\
					printk("\n");	\
			}				\
			printk("\n");			\
		}					\
	} while (0)

/*
 * To use the debug system;
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of:
 *
 * #define RTLLIB_DL_xxxx VALUE
 *
 * shifting value to the left one bit from the previous entry.  xxxx should be
 * the name of the classification (for example, WEP)
 *
 * You then need to either add a RTLLIB_xxxx_DEBUG() macro definition for your
 * classification, or use RTLLIB_DEBUG(RTLLIB_DL_xxxx, ...) whenever you want
 * to send output to that classification.
 *
 * To add your debug level to the list of levels seen when you perform
 *
 * % cat /proc/net/ipw/debug_level
 *
 * you simply need to add your entry to the ipw_debug_levels array.
 *
 *
 */

#define RTLLIB_DL_INFO	  (1<<0)
#define RTLLIB_DL_WX	    (1<<1)
#define RTLLIB_DL_SCAN	  (1<<2)
#define RTLLIB_DL_STATE	 (1<<3)
#define RTLLIB_DL_MGMT	  (1<<4)
#define RTLLIB_DL_FRAG	  (1<<5)
#define RTLLIB_DL_EAP	   (1<<6)
#define RTLLIB_DL_DROP	  (1<<7)

#define RTLLIB_DL_TX	    (1<<8)
#define RTLLIB_DL_RX	    (1<<9)

#define RTLLIB_DL_HT		   (1<<10)
#define RTLLIB_DL_BA		   (1<<11)
#define RTLLIB_DL_TS		   (1<<12)
#define RTLLIB_DL_QOS	   (1<<13)
#define RTLLIB_DL_REORDER	   (1<<14)
#define RTLLIB_DL_IOT	   (1<<15)
#define RTLLIB_DL_IPS	   (1<<16)
#define RTLLIB_DL_TRACE	   (1<<29)
#define RTLLIB_DL_DATA	   (1<<30)
#define RTLLIB_DL_ERR	   (1<<31)
#define RTLLIB_ERROR(f, a...) printk(KERN_ERR "rtllib: " f, ## a)
#define RTLLIB_WARNING(f, a...) printk(KERN_WARNING "rtllib: " f, ## a)
#define RTLLIB_DEBUG_INFO(f, a...)   RTLLIB_DEBUG(RTLLIB_DL_INFO, f, ## a)

#define RTLLIB_DEBUG_WX(f, a...)     RTLLIB_DEBUG(RTLLIB_DL_WX, f, ## a)
#define RTLLIB_DEBUG_SCAN(f, a...)   RTLLIB_DEBUG(RTLLIB_DL_SCAN, f, ## a)
#define RTLLIB_DEBUG_STATE(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_STATE, f, ## a)
#define RTLLIB_DEBUG_MGMT(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_MGMT, f, ## a)
#define RTLLIB_DEBUG_FRAG(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_FRAG, f, ## a)
#define RTLLIB_DEBUG_EAP(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_EAP, f, ## a)
#define RTLLIB_DEBUG_DROP(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_DROP, f, ## a)
#define RTLLIB_DEBUG_TX(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_TX, f, ## a)
#define RTLLIB_DEBUG_RX(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_RX, f, ## a)
#define RTLLIB_DEBUG_QOS(f, a...)  RTLLIB_DEBUG(RTLLIB_DL_QOS, f, ## a)

/* Added by Annie, 2005-11-22. */
#define MAX_STR_LEN     64
/* I want to see ASCII 33 to 126 only. Otherwise, I print '?'. */
#define PRINTABLE(_ch)  (_ch > '!' && _ch < '~')
#define RTLLIB_PRINT_STR(_Comp, _TitleString, _Ptr, _Len)		\
	if ((_Comp) & level) {					       \
		int	     __i;				    \
		u8  struct buffer[MAX_STR_LEN];				\
		int length = (_Len < MAX_STR_LEN) ? _Len : (MAX_STR_LEN-1) ;\
		memset(struct buffer, 0, MAX_STR_LEN);		\
		memcpy(struct buffer, (u8 *)_Ptr, length);		\
		for (__i = 0; __i < MAX_STR_LEN; __i++) {		\
			if (!PRINTABLE(struct buffer[__i]))		\
				struct buffer[__i] = '?';		\
		}							\
		struct buffer[length] = '\0';				\
		printk(KERN_INFO "Rtl819x: ");				\
		printk(_TitleString);					\
		printk(": %d, <%s>\n", _Len, struct buffer);		\
	}
#ifndef ETH_P_PAE
#define ETH_P_PAE 0x888E /* Port Access Entity (IEEE 802.1X) */
#define ETH_P_IP	0x0800		/* Internet Protocol packet	*/
#define ETH_P_ARP	0x0806		/* Address Resolution packet	*/
#endif /* ETH_P_PAE */

#define ETH_P_PREAUTH 0x88C7 /* IEEE 802.11i pre-authentication */

#ifndef ETH_P_80211_RAW
#define ETH_P_80211_RAW (ETH_P_ECONET + 1)
#endif

/* IEEE 802.11 defines */

#define P80211_OUI_LEN 3

struct rtllib_snap_hdr {

	u8    dsap;   /* always 0xAA */
	u8    ssap;   /* always 0xAA */
	u8    ctrl;   /* always 0x03 */
	u8    oui[P80211_OUI_LEN];    /* organizational universal id */

} __packed;

enum _REG_PREAMBLE_MODE {
	PREAMBLE_LONG = 1,
	PREAMBLE_AUTO = 2,
	PREAMBLE_SHORT = 3,
};

#define SNAP_SIZE sizeof(struct rtllib_snap_hdr)

#define WLAN_FC_GET_VERS(fc) ((fc) & RTLLIB_FCTL_VERS)
#define WLAN_FC_GET_TYPE(fc) ((fc) & RTLLIB_FCTL_FTYPE)
#define WLAN_FC_GET_STYPE(fc) ((fc) & RTLLIB_FCTL_STYPE)
#define WLAN_FC_MORE_DATA(fc) ((fc) & RTLLIB_FCTL_MOREDATA)

#define WLAN_FC_GET_FRAMETYPE(fc) ((fc) & RTLLIB_FCTL_FRAMETYPE)
#define WLAN_GET_SEQ_FRAG(seq) ((seq) & RTLLIB_SCTL_FRAG)
#define WLAN_GET_SEQ_SEQ(seq)  (((seq) & RTLLIB_SCTL_SEQ) >> 4)

/* Authentication algorithms */
#define WLAN_AUTH_OPEN 0
#define WLAN_AUTH_SHARED_KEY 1
#define WLAN_AUTH_LEAP 128

#define WLAN_AUTH_CHALLENGE_LEN 128

#define WLAN_CAPABILITY_ESS (1<<0)
#define WLAN_CAPABILITY_IBSS (1<<1)
#define WLAN_CAPABILITY_CF_POLLABLE (1<<2)
#define WLAN_CAPABILITY_CF_POLL_REQUEST (1<<3)
#define WLAN_CAPABILITY_PRIVACY (1<<4)
#define WLAN_CAPABILITY_SHORT_PREAMBLE (1<<5)
#define WLAN_CAPABILITY_PBCC (1<<6)
#define WLAN_CAPABILITY_CHANNEL_AGILITY (1<<7)
#define WLAN_CAPABILITY_SPECTRUM_MGMT (1<<8)
#define WLAN_CAPABILITY_QOS (1<<9)
#define WLAN_CAPABILITY_SHORT_SLOT_TIME (1<<10)
#define WLAN_CAPABILITY_DSSS_OFDM (1<<13)

/* 802.11g ERP information element */
#define WLAN_ERP_NON_ERP_PRESENT (1<<0)
#define WLAN_ERP_USE_PROTECTION (1<<1)
#define WLAN_ERP_BARKER_PREAMBLE (1<<2)

#define RTLLIB_STATMASK_SIGNAL (1<<0)
#define RTLLIB_STATMASK_RSSI (1<<1)
#define RTLLIB_STATMASK_NOISE (1<<2)
#define RTLLIB_STATMASK_RATE (1<<3)
#define RTLLIB_STATMASK_WEMASK 0x7

#define RTLLIB_CCK_MODULATION    (1<<0)
#define RTLLIB_OFDM_MODULATION   (1<<1)

#define RTLLIB_24GHZ_BAND     (1<<0)
#define RTLLIB_52GHZ_BAND     (1<<1)

#define RTLLIB_CCK_RATE_LEN		4
#define RTLLIB_CCK_RATE_1MB			0x02
#define RTLLIB_CCK_RATE_2MB			0x04
#define RTLLIB_CCK_RATE_5MB			0x0B
#define RTLLIB_CCK_RATE_11MB			0x16
#define RTLLIB_OFDM_RATE_LEN		8
#define RTLLIB_OFDM_RATE_6MB			0x0C
#define RTLLIB_OFDM_RATE_9MB			0x12
#define RTLLIB_OFDM_RATE_12MB		0x18
#define RTLLIB_OFDM_RATE_18MB		0x24
#define RTLLIB_OFDM_RATE_24MB		0x30
#define RTLLIB_OFDM_RATE_36MB		0x48
#define RTLLIB_OFDM_RATE_48MB		0x60
#define RTLLIB_OFDM_RATE_54MB		0x6C
#define RTLLIB_BASIC_RATE_MASK		0x80

#define RTLLIB_CCK_RATE_1MB_MASK		(1<<0)
#define RTLLIB_CCK_RATE_2MB_MASK		(1<<1)
#define RTLLIB_CCK_RATE_5MB_MASK		(1<<2)
#define RTLLIB_CCK_RATE_11MB_MASK		(1<<3)
#define RTLLIB_OFDM_RATE_6MB_MASK		(1<<4)
#define RTLLIB_OFDM_RATE_9MB_MASK		(1<<5)
#define RTLLIB_OFDM_RATE_12MB_MASK		(1<<6)
#define RTLLIB_OFDM_RATE_18MB_MASK		(1<<7)
#define RTLLIB_OFDM_RATE_24MB_MASK		(1<<8)
#define RTLLIB_OFDM_RATE_36MB_MASK		(1<<9)
#define RTLLIB_OFDM_RATE_48MB_MASK		(1<<10)
#define RTLLIB_OFDM_RATE_54MB_MASK		(1<<11)

#define RTLLIB_CCK_RATES_MASK		0x0000000F
#define RTLLIB_CCK_BASIC_RATES_MASK	(RTLLIB_CCK_RATE_1MB_MASK | \
	RTLLIB_CCK_RATE_2MB_MASK)
#define RTLLIB_CCK_DEFAULT_RATES_MASK	(RTLLIB_CCK_BASIC_RATES_MASK | \
	RTLLIB_CCK_RATE_5MB_MASK | \
	RTLLIB_CCK_RATE_11MB_MASK)

#define RTLLIB_OFDM_RATES_MASK		0x00000FF0
#define RTLLIB_OFDM_BASIC_RATES_MASK	(RTLLIB_OFDM_RATE_6MB_MASK | \
	RTLLIB_OFDM_RATE_12MB_MASK | \
	RTLLIB_OFDM_RATE_24MB_MASK)
#define RTLLIB_OFDM_DEFAULT_RATES_MASK	(RTLLIB_OFDM_BASIC_RATES_MASK | \
	RTLLIB_OFDM_RATE_9MB_MASK  | \
	RTLLIB_OFDM_RATE_18MB_MASK | \
	RTLLIB_OFDM_RATE_36MB_MASK | \
	RTLLIB_OFDM_RATE_48MB_MASK | \
	RTLLIB_OFDM_RATE_54MB_MASK)
#define RTLLIB_DEFAULT_RATES_MASK (RTLLIB_OFDM_DEFAULT_RATES_MASK | \
				RTLLIB_CCK_DEFAULT_RATES_MASK)

#define RTLLIB_NUM_OFDM_RATES	    8
#define RTLLIB_NUM_CCK_RATES		    4
#define RTLLIB_OFDM_SHIFT_MASK_A	 4


/* this is stolen and modified from the madwifi driver*/
#define RTLLIB_FC0_TYPE_MASK		0x0c
#define RTLLIB_FC0_TYPE_DATA		0x08
#define RTLLIB_FC0_SUBTYPE_MASK	0xB0
#define RTLLIB_FC0_SUBTYPE_QOS	0x80

#define RTLLIB_QOS_HAS_SEQ(fc) \
	(((fc) & (RTLLIB_FC0_TYPE_MASK | RTLLIB_FC0_SUBTYPE_MASK)) == \
	 (RTLLIB_FC0_TYPE_DATA | RTLLIB_FC0_SUBTYPE_QOS))

/* this is stolen from ipw2200 driver */
#define IEEE_IBSS_MAC_HASH_SIZE 31
struct ieee_ibss_seq {
	u8 mac[ETH_ALEN];
	u16 seq_num[17];
	u16 frag_num[17];
	unsigned long packet_time[17];
	struct list_head list;
};

/* NOTE: This data is for statistical purposes; not all hardware provides this
 *       information for frames received.  Not setting these will not cause
 *       any adverse affects. */
struct rtllib_rx_stats {
	u64 mac_time;
	s8  rssi;
	u8  signal;
	u8  noise;
	u16 rate; /* in 100 kbps */
	u8  received_channel;
	u8  control;
	u8  mask;
	u8  freq;
	u16 len;
	u64 tsf;
	u32 beacon_time;
	u8  nic_type;
	u16 Length;
	u8  SignalQuality;
	s32 RecvSignalPower;
	s8  RxPower;
	u8  SignalStrength;
	u16 bHwError:1;
	u16 bCRC:1;
	u16 bICV:1;
	u16 bShortPreamble:1;
	u16 Antenna:1;
	u16 Decrypted:1;
	u16 Wakeup:1;
	u16 Reserved0:1;
	u8  AGC;
	u32 TimeStampLow;
	u32 TimeStampHigh;
	bool bShift;
	bool bIsQosData;
	u8   UserPriority;

	u8    RxDrvInfoSize;
	u8    RxBufShift;
	bool  bIsAMPDU;
	bool  bFirstMPDU;
	bool  bContainHTC;
	bool  RxIs40MHzPacket;
	u32   RxPWDBAll;
	u8    RxMIMOSignalStrength[4];
	s8    RxMIMOSignalQuality[2];
	bool  bPacketMatchBSSID;
	bool  bIsCCK;
	bool  bPacketToSelf;
	u8 *virtual_address;
	u16    packetlength;
	u16    fraglength;
	u16    fragoffset;
	u16    ntotalfrag;
	bool   bisrxaggrsubframe;
	bool   bPacketBeacon;
	bool   bToSelfBA;
	char   cck_adc_pwdb[4];
	u16    Seq_Num;
	u8     nTotalAggPkt;
};

/* IEEE 802.11 requires that STA supports concurrent reception of at least
 * three fragmented frames. This define can be increased to support more
 * concurrent frames, but it should be noted that each entry can consume about
 * 2 kB of RAM and increasing cache size will slow down frame reassembly. */
#define RTLLIB_FRAG_CACHE_LEN 4

struct rtllib_frag_entry {
	unsigned long first_frag_time;
	unsigned int seq;
	unsigned int last_frag;
	struct sk_buff *skb;
	u8 src_addr[ETH_ALEN];
	u8 dst_addr[ETH_ALEN];
};

struct rtllib_stats {
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
	unsigned int rx_discards_undecryptable;
	unsigned int rx_message_in_msg_fragments;
	unsigned int rx_message_in_bad_msg_fragments;
};

struct rtllib_device;

#define SEC_KEY_1	 (1<<0)
#define SEC_KEY_2	 (1<<1)
#define SEC_KEY_3	 (1<<2)
#define SEC_KEY_4	 (1<<3)
#define SEC_ACTIVE_KEY    (1<<4)
#define SEC_AUTH_MODE     (1<<5)
#define SEC_UNICAST_GROUP (1<<6)
#define SEC_LEVEL	 (1<<7)
#define SEC_ENABLED       (1<<8)
#define SEC_ENCRYPT       (1<<9)

#define SEC_LEVEL_0      0 /* None */
#define SEC_LEVEL_1      1 /* WEP 40 and 104 bit */
#define SEC_LEVEL_2      2 /* Level 1 + TKIP */
#define SEC_LEVEL_2_CKIP 3 /* Level 1 + CKIP */
#define SEC_LEVEL_3      4 /* Level 2 + CCMP */

#define SEC_ALG_NONE		0
#define SEC_ALG_WEP		1
#define SEC_ALG_TKIP		2
#define SEC_ALG_CCMP		4

#define WEP_KEY_LEN		13
#define SCM_KEY_LEN		32
#define SCM_TEMPORAL_KEY_LENGTH 16

struct rtllib_security {
	u16 active_key:2,
	    enabled:1,
	    auth_mode:2,
	    auth_algo:4,
	    unicast_uses_group:1,
	    encrypt:1;
	u8 key_sizes[NUM_WEP_KEYS];
	u8 keys[NUM_WEP_KEYS][SCM_KEY_LEN];
	u8 level;
	u16 flags;
} __packed;


/*
 802.11 data frame from AP
      ,-------------------------------------------------------------------.
Bytes |  2   |  2   |    6    |    6    |    6    |  2   | 0..2312 |   4  |
      |------|------|---------|---------|---------|------|---------|------|
Desc. | ctrl | dura |  DA/RA  |   TA    |    SA   | Sequ |  frame  |  fcs |
      |      | tion | (BSSID) |	 |	 | ence |  data   |      |
      `-------------------------------------------------------------------'
Total: 28-2340 bytes
*/

/* Management Frame Information Element Types */
enum rtllib_mfie {
	MFIE_TYPE_SSID = 0,
	MFIE_TYPE_RATES = 1,
	MFIE_TYPE_FH_SET = 2,
	MFIE_TYPE_DS_SET = 3,
	MFIE_TYPE_CF_SET = 4,
	MFIE_TYPE_TIM = 5,
	MFIE_TYPE_IBSS_SET = 6,
	MFIE_TYPE_COUNTRY = 7,
	MFIE_TYPE_HOP_PARAMS = 8,
	MFIE_TYPE_HOP_TABLE = 9,
	MFIE_TYPE_REQUEST = 10,
	MFIE_TYPE_CHALLENGE = 16,
	MFIE_TYPE_POWER_CONSTRAINT = 32,
	MFIE_TYPE_POWER_CAPABILITY = 33,
	MFIE_TYPE_TPC_REQUEST = 34,
	MFIE_TYPE_TPC_REPORT = 35,
	MFIE_TYPE_SUPP_CHANNELS = 36,
	MFIE_TYPE_CSA = 37,
	MFIE_TYPE_MEASURE_REQUEST = 38,
	MFIE_TYPE_MEASURE_REPORT = 39,
	MFIE_TYPE_QUIET = 40,
	MFIE_TYPE_IBSS_DFS = 41,
	MFIE_TYPE_ERP = 42,
	MFIE_TYPE_HT_CAP = 45,
	MFIE_TYPE_RSN = 48,
	MFIE_TYPE_RATES_EX = 50,
	MFIE_TYPE_HT_INFO = 61,
	MFIE_TYPE_AIRONET = 133,
	MFIE_TYPE_GENERIC = 221,
	MFIE_TYPE_QOS_PARAMETER = 222,
};

/* Minimal header; can be used for passing 802.11 frames with sufficient
 * information to determine what type of underlying data type is actually
 * stored in the data. */
struct rtllib_pspoll_hdr {
	__le16 frame_ctl;
	__le16 aid;
	u8 bssid[ETH_ALEN];
	u8 ta[ETH_ALEN];
} __packed;

struct rtllib_hdr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 payload[0];
} __packed;

struct rtllib_hdr_1addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 payload[0];
} __packed;

struct rtllib_hdr_2addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 payload[0];
} __packed;

struct rtllib_hdr_3addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 payload[0];
} __packed;

struct rtllib_hdr_4addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 addr4[ETH_ALEN];
	u8 payload[0];
} __packed;

struct rtllib_hdr_3addrqos {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	__le16 qos_ctl;
	u8 payload[0];
} __packed;

struct rtllib_hdr_4addrqos {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 addr4[ETH_ALEN];
	__le16 qos_ctl;
	u8 payload[0];
} __packed;

struct rtllib_info_element {
	u8 id;
	u8 len;
	u8 data[0];
} __packed;

struct rtllib_authentication {
	struct rtllib_hdr_3addr header;
	__le16 algorithm;
	__le16 transaction;
	__le16 status;
	/*challenge*/
	struct rtllib_info_element info_element[0];
} __packed;

struct rtllib_disauth {
	struct rtllib_hdr_3addr header;
	__le16 reason;
} __packed;

struct rtllib_disassoc {
	struct rtllib_hdr_3addr header;
	__le16 reason;
} __packed;

struct rtllib_probe_request {
	struct rtllib_hdr_3addr header;
	/* SSID, supported rates */
	struct rtllib_info_element info_element[0];
} __packed;

struct rtllib_probe_response {
	struct rtllib_hdr_3addr header;
	u32 time_stamp[2];
	__le16 beacon_interval;
	__le16 capability;
	/* SSID, supported rates, FH params, DS params,
	 * CF params, IBSS params, TIM (if beacon), RSN */
	struct rtllib_info_element info_element[0];
} __packed;

/* Alias beacon for probe_response */
#define rtllib_beacon rtllib_probe_response

struct rtllib_assoc_request_frame {
	struct rtllib_hdr_3addr header;
	__le16 capability;
	__le16 listen_interval;
	/* SSID, supported rates, RSN */
	struct rtllib_info_element info_element[0];
} __packed;

struct rtllib_reassoc_request_frame {
	struct rtllib_hdr_3addr header;
	__le16 capability;
	__le16 listen_interval;
	u8 current_ap[ETH_ALEN];
	/* SSID, supported rates, RSN */
	struct rtllib_info_element info_element[0];
} __packed;

struct rtllib_assoc_response_frame {
	struct rtllib_hdr_3addr header;
	__le16 capability;
	__le16 status;
	__le16 aid;
	struct rtllib_info_element info_element[0]; /* supported rates */
} __packed;

struct rtllib_txb {
	u8 nr_frags;
	u8 encrypted;
	u8 queue_index;
	u8 rts_included;
	u16 reserved;
	__le16 frag_size;
	__le16 payload_size;
	struct sk_buff *fragments[0];
};

#define MAX_TX_AGG_COUNT		  16
struct rtllib_drv_agg_txb {
	u8 nr_drv_agg_frames;
	struct sk_buff *tx_agg_frames[MAX_TX_AGG_COUNT];
} __packed;

#define MAX_SUBFRAME_COUNT		  64
struct rtllib_rxb {
	u8 nr_subframes;
	struct sk_buff *subframes[MAX_SUBFRAME_COUNT];
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
} __packed;

union frameqos {
	u16 shortdata;
	u8  chardata[2];
	struct {
		u16 tid:4;
		u16 eosp:1;
		u16 ack_policy:2;
		u16 reserved:1;
		u16 txop:8;
	} field;
};

/* SWEEP TABLE ENTRIES NUMBER*/
#define MAX_SWEEP_TAB_ENTRIES		  42
#define MAX_SWEEP_TAB_ENTRIES_PER_PACKET  7
/* MAX_RATES_LENGTH needs to be 12.  The spec says 8, and many APs
 * only use 8, and then use extended rates for the remaining supported
 * rates.  Other APs, however, stick all of their supported rates on the
 * main rates information element... */
#define MAX_RATES_LENGTH		  ((u8)12)
#define MAX_RATES_EX_LENGTH	       ((u8)16)
#define MAX_NETWORK_COUNT		  96

#define MAX_CHANNEL_NUMBER		 161
#define RTLLIB_SOFTMAC_SCAN_TIME	   100
#define RTLLIB_SOFTMAC_ASSOC_RETRY_TIME (HZ * 2)

#define CRC_LENGTH		 4U

#define MAX_WPA_IE_LEN 64
#define MAX_WZC_IE_LEN 256

#define NETWORK_EMPTY_ESSID (1<<0)
#define NETWORK_HAS_OFDM    (1<<1)
#define NETWORK_HAS_CCK     (1<<2)

/* QoS structure */
#define NETWORK_HAS_QOS_PARAMETERS      (1<<3)
#define NETWORK_HAS_QOS_INFORMATION     (1<<4)
#define NETWORK_HAS_QOS_MASK	    (NETWORK_HAS_QOS_PARAMETERS | \
					 NETWORK_HAS_QOS_INFORMATION)
/* 802.11h */
#define NETWORK_HAS_POWER_CONSTRAINT    (1<<5)
#define NETWORK_HAS_CSA		 (1<<6)
#define NETWORK_HAS_QUIET	       (1<<7)
#define NETWORK_HAS_IBSS_DFS	    (1<<8)
#define NETWORK_HAS_TPC_REPORT	  (1<<9)

#define NETWORK_HAS_ERP_VALUE	   (1<<10)

#define QOS_QUEUE_NUM		   4
#define QOS_OUI_LEN		     3
#define QOS_OUI_TYPE		    2
#define QOS_ELEMENT_ID		  221
#define QOS_OUI_INFO_SUB_TYPE	   0
#define QOS_OUI_PARAM_SUB_TYPE	  1
#define QOS_VERSION_1		   1
#define QOS_AIFSN_MIN_VALUE	     2

struct rtllib_qos_information_element {
	u8 elementID;
	u8 length;
	u8 qui[QOS_OUI_LEN];
	u8 qui_type;
	u8 qui_subtype;
	u8 version;
	u8 ac_info;
} __packed;

struct rtllib_qos_ac_parameter {
	u8 aci_aifsn;
	u8 ecw_min_max;
	__le16 tx_op_limit;
} __packed;

struct rtllib_qos_parameter_info {
	struct rtllib_qos_information_element info_element;
	u8 reserved;
	struct rtllib_qos_ac_parameter ac_params_record[QOS_QUEUE_NUM];
} __packed;

struct rtllib_qos_parameters {
	__le16 cw_min[QOS_QUEUE_NUM];
	__le16 cw_max[QOS_QUEUE_NUM];
	u8 aifs[QOS_QUEUE_NUM];
	u8 flag[QOS_QUEUE_NUM];
	__le16 tx_op_limit[QOS_QUEUE_NUM];
} __packed;

struct rtllib_qos_data {
	struct rtllib_qos_parameters parameters;
	unsigned int wmm_acm;
	int active;
	int supported;
	u8 param_count;
	u8 old_param_count;
};

struct rtllib_tim_parameters {
	u8 tim_count;
	u8 tim_period;
} __packed;

struct rtllib_wmm_ac_param {
	u8 ac_aci_acm_aifsn;
	u8 ac_ecwmin_ecwmax;
	u16 ac_txop_limit;
};

struct rtllib_wmm_ts_info {
	u8 ac_dir_tid;
	u8 ac_up_psb;
	u8 reserved;
} __packed;

struct rtllib_wmm_tspec_elem {
	struct rtllib_wmm_ts_info ts_info;
	u16 norm_msdu_size;
	u16 max_msdu_size;
	u32 min_serv_inter;
	u32 max_serv_inter;
	u32 inact_inter;
	u32 suspen_inter;
	u32 serv_start_time;
	u32 min_data_rate;
	u32 mean_data_rate;
	u32 peak_data_rate;
	u32 max_burst_size;
	u32 delay_bound;
	u32 min_phy_rate;
	u16 surp_band_allow;
	u16 medium_time;
} __packed;

enum eap_type {
	EAP_PACKET = 0,
	EAPOL_START,
	EAPOL_LOGOFF,
	EAPOL_KEY,
	EAPOL_ENCAP_ASF_ALERT
};

static const char *eap_types[] = {
	[EAP_PACKET]		= "EAP-Packet",
	[EAPOL_START]		= "EAPOL-Start",
	[EAPOL_LOGOFF]		= "EAPOL-Logoff",
	[EAPOL_KEY]		= "EAPOL-Key",
	[EAPOL_ENCAP_ASF_ALERT]	= "EAPOL-Encap-ASF-Alert"
};

static inline const char *eap_get_type(int type)
{
	return ((u32)type >= ARRAY_SIZE(eap_types)) ? "Unknown" :
		 eap_types[type];
}
static inline u8 Frame_QoSTID(u8 *buf)
{
	struct rtllib_hdr_3addr *hdr;
	u16 fc;
	hdr = (struct rtllib_hdr_3addr *)buf;
	fc = le16_to_cpu(hdr->frame_ctl);
	return (u8)((union frameqos *)(buf + (((fc & RTLLIB_FCTL_TODS) &&
		    (fc & RTLLIB_FCTL_FROMDS)) ? 30 : 24)))->field.tid;
}


struct eapol {
	u8 snap[6];
	u16 ethertype;
	u8 version;
	u8 type;
	u16 length;
} __packed;

struct rtllib_softmac_stats {
	unsigned int rx_ass_ok;
	unsigned int rx_ass_err;
	unsigned int rx_probe_rq;
	unsigned int tx_probe_rs;
	unsigned int tx_beacons;
	unsigned int rx_auth_rq;
	unsigned int rx_auth_rs_ok;
	unsigned int rx_auth_rs_err;
	unsigned int tx_auth_rq;
	unsigned int no_auth_rs;
	unsigned int no_ass_rs;
	unsigned int tx_ass_rq;
	unsigned int rx_ass_rq;
	unsigned int tx_probe_rq;
	unsigned int reassoc;
	unsigned int swtxstop;
	unsigned int swtxawake;
	unsigned char CurrentShowTxate;
	unsigned char last_packet_rate;
	unsigned int txretrycount;
};

#define BEACON_PROBE_SSID_ID_POSITION 12

struct rtllib_info_element_hdr {
	u8 id;
	u8 len;
} __packed;

/*
 * These are the data types that can make up management packets
 *
	u16 auth_algorithm;
	u16 auth_sequence;
	u16 beacon_interval;
	u16 capability;
	u8 current_ap[ETH_ALEN];
	u16 listen_interval;
	struct {
		u16 association_id:14, reserved:2;
	} __packed;
	u32 time_stamp[2];
	u16 reason;
	u16 status;
*/

#define RTLLIB_DEFAULT_TX_ESSID "Penguin"
#define RTLLIB_DEFAULT_BASIC_RATE 2

enum {WMM_all_frame, WMM_two_frame, WMM_four_frame, WMM_six_frame};
#define MAX_SP_Len  (WMM_all_frame << 4)
#define RTLLIB_QOS_TID 0x0f
#define QOS_CTL_NOTCONTAIN_ACK (0x01 << 5)

#define RTLLIB_DTIM_MBCAST 4
#define RTLLIB_DTIM_UCAST 2
#define RTLLIB_DTIM_VALID 1
#define RTLLIB_DTIM_INVALID 0

#define RTLLIB_PS_DISABLED 0
#define RTLLIB_PS_UNICAST RTLLIB_DTIM_UCAST
#define RTLLIB_PS_MBCAST RTLLIB_DTIM_MBCAST

#define WME_AC_BK   0x00
#define WME_AC_BE   0x01
#define WME_AC_VI   0x02
#define WME_AC_VO   0x03
#define WME_ACI_MASK 0x03
#define WME_AIFSN_MASK 0x03
#define WME_AC_PRAM_LEN 16

#define MAX_RECEIVE_BUFFER_SIZE 9100

#define UP2AC(up) (		   \
	((up) < 1) ? WME_AC_BE : \
	((up) < 3) ? WME_AC_BK : \
	((up) < 4) ? WME_AC_BE : \
	((up) < 6) ? WME_AC_VI : \
	WME_AC_VO)

#define AC2UP(_ac)	(       \
	((_ac) == WME_AC_VO) ? 6 : \
	((_ac) == WME_AC_VI) ? 5 : \
	((_ac) == WME_AC_BK) ? 1 : \
	0)

#define	ETHER_ADDR_LEN		6	/* length of an Ethernet address */
#define ETHERNET_HEADER_SIZE    14      /* length of two Ethernet address
					 * plus ether type*/

struct	ether_header {
	u8 ether_dhost[ETHER_ADDR_LEN];
	u8 ether_shost[ETHER_ADDR_LEN];
	u16 ether_type;
} __packed;

#ifndef ETHERTYPE_PAE
#define	ETHERTYPE_PAE	0x888e		/* EAPOL PAE/802.1x */
#endif
#ifndef ETHERTYPE_IP
#define	ETHERTYPE_IP	0x0800		/* IP protocol */
#endif


enum erp_t {
	ERP_NonERPpresent	= 0x01,
	ERP_UseProtection	= 0x02,
	ERP_BarkerPreambleMode = 0x04,
};

struct rtllib_network {
	/* These entries are used to identify a unique network */
	u8 bssid[ETH_ALEN];
	u8 channel;
	/* Ensure null-terminated for any debug msgs */
	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;
	u8 hidden_ssid[IW_ESSID_MAX_SIZE + 1];
	u8 hidden_ssid_len;
	struct rtllib_qos_data qos_data;

	bool	bWithAironetIE;
	bool	bCkipSupported;
	bool	bCcxRmEnable;
	u16	CcxRmState[2];
	bool	bMBssidValid;
	u8	MBssidMask;
	u8	MBssid[6];
	bool	bWithCcxVerNum;
	u8	BssCcxVerNumber;
	/* These are network statistics */
	struct rtllib_rx_stats stats;
	u16 capability;
	u8  rates[MAX_RATES_LENGTH];
	u8  rates_len;
	u8  rates_ex[MAX_RATES_EX_LENGTH];
	u8  rates_ex_len;
	unsigned long last_scanned;
	u8  mode;
	u32 flags;
	u32 last_associate;
	u32 time_stamp[2];
	u16 beacon_interval;
	u16 listen_interval;
	u16 atim_window;
	u8  erp_value;
	u8  wpa_ie[MAX_WPA_IE_LEN];
	size_t wpa_ie_len;
	u8  rsn_ie[MAX_WPA_IE_LEN];
	size_t rsn_ie_len;
	u8  wzc_ie[MAX_WZC_IE_LEN];
	size_t wzc_ie_len;

	struct rtllib_tim_parameters tim;
	u8  dtim_period;
	u8  dtim_data;
	u64 last_dtim_sta_time;

	u8 wmm_info;
	struct rtllib_wmm_ac_param wmm_param[4];
	u8 Turbo_Enable;
	u16 CountryIeLen;
	u8 CountryIeBuf[MAX_IE_LEN];
	struct bss_ht bssht;
	bool broadcom_cap_exist;
	bool realtek_cap_exit;
	bool marvell_cap_exist;
	bool ralink_cap_exist;
	bool atheros_cap_exist;
	bool cisco_cap_exist;
	bool airgo_cap_exist;
	bool unknown_cap_exist;
	bool	berp_info_valid;
	bool buseprotection;
	bool bIsNetgear854T;
	u8 SignalStrength;
	u8 RSSI;
	struct list_head list;
};

#if 1
enum rtllib_state {

	/* the card is not linked at all */
	RTLLIB_NOLINK = 0,

	/* RTLLIB_ASSOCIATING* are for BSS client mode
	 * the driver shall not perform RX filtering unless
	 * the state is LINKED.
	 * The driver shall just check for the state LINKED and
	 * defaults to NOLINK for ALL the other states (including
	 * LINKED_SCANNING)
	 */

	/* the association procedure will start (wq scheduling)*/
	RTLLIB_ASSOCIATING,
	RTLLIB_ASSOCIATING_RETRY,

	/* the association procedure is sending AUTH request*/
	RTLLIB_ASSOCIATING_AUTHENTICATING,

	/* the association procedure has successfully authentcated
	 * and is sending association request
	 */
	RTLLIB_ASSOCIATING_AUTHENTICATED,

	/* the link is ok. the card associated to a BSS or linked
	 * to a ibss cell or acting as an AP and creating the bss
	 */
	RTLLIB_LINKED,

	/* same as LINKED, but the driver shall apply RX filter
	 * rules as we are in NO_LINK mode. As the card is still
	 * logically linked, but it is doing a syncro site survey
	 * then it will be back to LINKED state.
	 */
	RTLLIB_LINKED_SCANNING,
};
#else
enum rtllib_state {
	RTLLIB_UNINITIALIZED = 0,
	RTLLIB_INITIALIZED,
	RTLLIB_ASSOCIATING,
	RTLLIB_ASSOCIATED,
	RTLLIB_AUTHENTICATING,
	RTLLIB_AUTHENTICATED,
	RTLLIB_SHUTDOWN
};
#endif

#define DEFAULT_MAX_SCAN_AGE (15 * HZ)
#define DEFAULT_FTS 2346

#define CFG_RTLLIB_RESERVE_FCS (1<<0)
#define CFG_RTLLIB_COMPUTE_FCS (1<<1)
#define CFG_RTLLIB_RTS (1<<2)

#define RTLLIB_24GHZ_MIN_CHANNEL 1
#define RTLLIB_24GHZ_MAX_CHANNEL 14
#define RTLLIB_24GHZ_CHANNELS (RTLLIB_24GHZ_MAX_CHANNEL - \
				  RTLLIB_24GHZ_MIN_CHANNEL + 1)

#define RTLLIB_52GHZ_MIN_CHANNEL 34
#define RTLLIB_52GHZ_MAX_CHANNEL 165
#define RTLLIB_52GHZ_CHANNELS (RTLLIB_52GHZ_MAX_CHANNEL - \
				  RTLLIB_52GHZ_MIN_CHANNEL + 1)
#ifndef eqMacAddr
#define eqMacAddr(a, b)					\
	(((a)[0] == (b)[0] && (a)[1] == (b)[1] && (a)[2] == (b)[2] &&	\
	(a)[3] == (b)[3] && (a)[4] == (b)[4] && (a)[5] == (b)[5]) ? 1 : 0)
#endif
struct tx_pending {
	int frag;
	struct rtllib_txb *txb;
};

struct bandwidth_autoswitch {
	long threshold_20Mhzto40Mhz;
	long	threshold_40Mhzto20Mhz;
	bool bforced_tx20Mhz;
	bool bautoswitch_enable;
};



#define REORDER_WIN_SIZE	128
#define REORDER_ENTRY_NUM	128
struct rx_reorder_entry {
	struct list_head	List;
	u16			SeqNum;
	struct rtllib_rxb *prxb;
};
enum fsync_state {
	Default_Fsync,
	HW_Fsync,
	SW_Fsync
};

enum rt_ps_mode {
	eActive,
	eMaxPs,
	eFastPs,
	eAutoPs,
};

enum ips_callback_function {
	IPS_CALLBACK_NONE = 0,
	IPS_CALLBACK_MGNT_LINK_REQUEST = 1,
	IPS_CALLBACK_JOIN_REQUEST = 2,
};

enum rt_join_action {
	RT_JOIN_INFRA   = 1,
	RT_JOIN_IBSS  = 2,
	RT_START_IBSS = 3,
	RT_NO_ACTION  = 4,
};

struct ibss_parms {
	u16   atimWin;
};
#define MAX_NUM_RATES	264

enum rt_rf_power_state {
	eRfOn,
	eRfSleep,
	eRfOff
};

#define	MAX_SUPPORT_WOL_PATTERN_NUM		8

#define	MAX_WOL_BIT_MASK_SIZE		16
#define	MAX_WOL_PATTERN_SIZE		128

enum wol_pattern_type {
	eNetBIOS = 0,
	eIPv4IPv6ARP,
	eIPv4IPv6TCPSYN,
	eMACIDOnly,
	eNoDefined,
};

struct rt_pm_wol_info {
	u32	PatternId;
	u32	Mask[4];
	u16	CrcRemainder;
	u8	WFMIndex;
	enum wol_pattern_type PatternType;
};

struct rt_pwr_save_ctrl {

	bool				bInactivePs;
	bool				bIPSModeBackup;
	bool				bHaltAdapterClkRQ;
	bool				bSwRfProcessing;
	enum rt_rf_power_state eInactivePowerState;
	struct work_struct		InactivePsWorkItem;
	struct timer_list	InactivePsTimer;

	enum ips_callback_function ReturnPoint;

	bool				bTmpBssDesc;
	enum rt_join_action tmpJoinAction;
	struct rtllib_network tmpBssDesc;

	bool				bTmpScanOnly;
	bool				bTmpActiveScan;
	bool				bTmpFilterHiddenAP;
	bool				bTmpUpdateParms;
	u8				tmpSsidBuf[33];
	struct octet_string tmpSsid2Scan;
	bool				bTmpSsid2Scan;
	u8				tmpNetworkType;
	u8				tmpChannelNumber;
	u16				tmpBcnPeriod;
	u8				tmpDtimPeriod;
	u16				tmpmCap;
	struct octet_string tmpSuppRateSet;
	u8				tmpSuppRateBuf[MAX_NUM_RATES];
	bool				bTmpSuppRate;
	struct ibss_parms tmpIbpm;
	bool				bTmpIbpm;

	bool				bLeisurePs;
	u32				PowerProfile;
	u8				LpsIdleCount;
	u8				RegMaxLPSAwakeIntvl;
	u8				LPSAwakeIntvl;

	u32				CurPsLevel;
	u32				RegRfPsLevel;

	bool				bFwCtrlLPS;
	u8				FWCtrlPSMode;

	bool				LinkReqInIPSRFOffPgs;
	bool				BufConnectinfoBefore;


	bool				bGpioRfSw;

	u8				RegAMDPciASPM;

	u8				oWLANMode;
	struct rt_pm_wol_info PmWoLPatternInfo[MAX_SUPPORT_WOL_PATTERN_NUM];

};

#define RT_RF_CHANGE_SOURCE u32

#define RF_CHANGE_BY_SW BIT31
#define RF_CHANGE_BY_HW BIT30
#define RF_CHANGE_BY_PS BIT29
#define RF_CHANGE_BY_IPS BIT28
#define RF_CHANGE_BY_INIT	0

enum country_code_type {
	COUNTRY_CODE_FCC = 0,
	COUNTRY_CODE_IC = 1,
	COUNTRY_CODE_ETSI = 2,
	COUNTRY_CODE_SPAIN = 3,
	COUNTRY_CODE_FRANCE = 4,
	COUNTRY_CODE_MKK = 5,
	COUNTRY_CODE_MKK1 = 6,
	COUNTRY_CODE_ISRAEL = 7,
	COUNTRY_CODE_TELEC = 8,
	COUNTRY_CODE_MIC = 9,
	COUNTRY_CODE_GLOBAL_DOMAIN = 10,
	COUNTRY_CODE_WORLD_WIDE_13 = 11,
	COUNTRY_CODE_TELEC_NETGEAR = 12,
	COUNTRY_CODE_MAX
};

enum scan_op_backup_opt {
	SCAN_OPT_BACKUP = 0,
	SCAN_OPT_RESTORE,
	SCAN_OPT_MAX
};

enum fw_cmd_io_type {
	FW_CMD_DIG_ENABLE = 0,
	FW_CMD_DIG_DISABLE = 1,
	FW_CMD_DIG_HALT = 2,
	FW_CMD_DIG_RESUME = 3,
	FW_CMD_HIGH_PWR_ENABLE = 4,
	FW_CMD_HIGH_PWR_DISABLE = 5,
	FW_CMD_RA_RESET = 6,
	FW_CMD_RA_ACTIVE = 7,
	FW_CMD_RA_REFRESH_N = 8,
	FW_CMD_RA_REFRESH_BG = 9,
	FW_CMD_RA_INIT = 10,
	FW_CMD_IQK_ENABLE = 11,
	FW_CMD_TXPWR_TRACK_ENABLE = 12,
	FW_CMD_TXPWR_TRACK_DISABLE = 13,
	FW_CMD_TXPWR_TRACK_THERMAL = 14,
	FW_CMD_PAUSE_DM_BY_SCAN = 15,
	FW_CMD_RESUME_DM_BY_SCAN = 16,
	FW_CMD_RA_REFRESH_N_COMB = 17,
	FW_CMD_RA_REFRESH_BG_COMB = 18,
	FW_CMD_ANTENNA_SW_ENABLE = 19,
	FW_CMD_ANTENNA_SW_DISABLE = 20,
	FW_CMD_TX_FEEDBACK_CCX_ENABLE = 21,
	FW_CMD_LPS_ENTER = 22,
	FW_CMD_LPS_LEAVE = 23,
	FW_CMD_DIG_MODE_SS = 24,
	FW_CMD_DIG_MODE_FA = 25,
	FW_CMD_ADD_A2_ENTRY = 26,
	FW_CMD_CTRL_DM_BY_DRIVER = 27,
	FW_CMD_CTRL_DM_BY_DRIVER_NEW = 28,
	FW_CMD_PAPE_CONTROL = 29,
	FW_CMD_CHAN_SET = 30,
};

#define RT_MAX_LD_SLOT_NUM	10
struct rt_link_detect {

	u32				NumRecvBcnInPeriod;
	u32				NumRecvDataInPeriod;

	u32				RxBcnNum[RT_MAX_LD_SLOT_NUM];
	u32				RxDataNum[RT_MAX_LD_SLOT_NUM];
	u16				SlotNum;
	u16				SlotIndex;

	u32				NumTxOkInPeriod;
	u32				NumRxOkInPeriod;
	u32				NumRxUnicastOkInPeriod;
	bool				bBusyTraffic;
	bool				bHigherBusyTraffic;
	bool				bHigherBusyRxTraffic;
	u8				IdleCount;
	u32				NumTxUnicastOkInPeriod;
	u32				LastNumTxUnicast;
	u32				LastNumRxUnicast;
};

struct sw_cam_table {

	u8				macaddr[6];
	bool				bused;
	u8				key_buf[16];
	u16				key_type;
	u8				useDK;
	u8				key_index;

};
#define   TOTAL_CAM_ENTRY				32
struct rate_adaptive {
	u8				rate_adaptive_disabled;
	u8				ratr_state;
	u16				reserve;

	u32				high_rssi_thresh_for_ra;
	u32				high2low_rssi_thresh_for_ra;
	u8				low2high_rssi_thresh_for_ra40M;
	u32				low_rssi_thresh_for_ra40M;
	u8				low2high_rssi_thresh_for_ra20M;
	u32				low_rssi_thresh_for_ra20M;
	u32				upper_rssi_threshold_ratr;
	u32				middle_rssi_threshold_ratr;
	u32				low_rssi_threshold_ratr;
	u32				low_rssi_threshold_ratr_40M;
	u32				low_rssi_threshold_ratr_20M;
	u8				ping_rssi_enable;
	u32				ping_rssi_ratr;
	u32				ping_rssi_thresh_for_ra;
	u32				last_ratr;
	u8				PreRATRState;

};
enum ratr_table_mode_8192s {
	RATR_INX_WIRELESS_NGB = 0,
	RATR_INX_WIRELESS_NG = 1,
	RATR_INX_WIRELESS_NB = 2,
	RATR_INX_WIRELESS_N = 3,
	RATR_INX_WIRELESS_GB = 4,
	RATR_INX_WIRELESS_G = 5,
	RATR_INX_WIRELESS_B = 6,
	RATR_INX_WIRELESS_MC = 7,
	RATR_INX_WIRELESS_A = 8,
};

#define	NUM_PMKID_CACHE		16
struct rt_pmkid_list {
	u8 bUsed;
	u8 Bssid[6];
	u8 PMKID[16];
	u8 SsidBuf[33];
	u8 *ssid_octet;
	u16 ssid_length;
};

struct rt_intel_promisc_mode {
	bool bPromiscuousOn;
	bool bFilterSourceStationFrame;
};


/*************** DRIVER STATUS   *****/
#define STATUS_SCANNING			0
#define STATUS_SCAN_HW			1
#define STATUS_SCAN_ABORTING	2
#define STATUS_SETTING_CHAN		3
/*************** DRIVER STATUS   *****/

enum {
	NO_USE		= 0,
	USED		= 1,
	HW_SEC		= 2,
	SW_SEC		= 3,
};

enum {
	LPS_IS_WAKE = 0,
	LPS_IS_SLEEP = 1,
	LPS_WAIT_NULL_DATA_SEND = 2,
};

struct rtllib_device {
	struct pci_dev *pdev;
	struct net_device *dev;
	struct rtllib_security sec;

	bool disable_mgnt_queue;

	unsigned long status;
	short hwscan_ch_bk;
	enum ht_extchnl_offset chan_offset_bk;
	enum ht_channel_width bandwidth_bk;
	u8 hwscan_sem_up;
	u8	CntAfterLink;

	enum rt_op_mode OpMode;

	u8 VersionID;
	/* The last AssocReq/Resp IEs */
	u8 *assocreq_ies, *assocresp_ies;
	size_t assocreq_ies_len, assocresp_ies_len;

	bool b_customer_lenovo_id;
	bool	bForcedShowRxRate;
	bool	bForcedShowRateStill;
	u8	SystemQueryDataRateCount;
	bool	bForcedBgMode;
	bool bUseRAMask;
	bool b1x1RecvCombine;
	u8 RF_Type;
	bool b1SSSupport;

	u8 hwsec_active;
	bool is_silent_reset;
	bool force_mic_error;
	bool is_roaming;
	bool ieee_up;
	bool cannot_notify;
	bool bSupportRemoteWakeUp;
	enum rt_ps_mode dot11PowerSaveMode;
	bool actscanning;
	bool FirstIe_InScan;
	bool be_scan_inprogress;
	bool beinretry;
	enum rt_rf_power_state eRFPowerState;
	RT_RF_CHANGE_SOURCE	RfOffReason;
	bool is_set_key;
	bool wx_set_enc;
	struct rt_hi_throughput *pHTInfo;
	spinlock_t bw_spinlock;

	spinlock_t reorder_spinlock;
	u8	Regdot11HTOperationalRateSet[16];
	u8	Regdot11TxHTOperationalRateSet[16];
	u8	dot11HTOperationalRateSet[16];
	u8	RegHTSuppRateSet[16];
	u8	HTCurrentOperaRate;
	u8	HTHighestOperaRate;
	u8	MinSpaceCfg;
	u8	MaxMssDensity;
	u8	bTxDisableRateFallBack;
	u8	bTxUseDriverAssingedRate;
	u8	bTxEnableFwCalcDur;
	atomic_t	atm_chnlop;
	atomic_t	atm_swbw;

	struct list_head		Tx_TS_Admit_List;
	struct list_head		Tx_TS_Pending_List;
	struct list_head		Tx_TS_Unused_List;
	struct tx_ts_record TxTsRecord[TOTAL_TS_NUM];
	struct list_head		Rx_TS_Admit_List;
	struct list_head		Rx_TS_Pending_List;
	struct list_head		Rx_TS_Unused_List;
	struct rx_ts_record RxTsRecord[TOTAL_TS_NUM];
	struct rx_reorder_entry RxReorderEntry[128];
	struct list_head		RxReorder_Unused_List;
	u8				ForcedPriority;


	/* Bookkeeping structures */
	struct net_device_stats stats;
	struct rtllib_stats ieee_stats;
	struct rtllib_softmac_stats softmac_stats;

	/* Probe / Beacon management */
	struct list_head network_free_list;
	struct list_head network_list;
	struct rtllib_network *networks;
	int scans;
	int scan_age;

	int iw_mode; /* operating mode (IW_MODE_*) */
	bool bNetPromiscuousMode;
	struct rt_intel_promisc_mode IntelPromiscuousModeInfo;

	struct iw_spy_data spy_data;

	spinlock_t lock;
	spinlock_t wpax_suitlist_lock;

	int tx_headroom; /* Set to size of any additional room needed at front
			  * of allocated Tx SKBs */
	u32 config;

	/* WEP and other encryption related settings at the device level */
	int open_wep; /* Set to 1 to allow unencrypted frames */
	int auth_mode;
	int reset_on_keychange; /* Set to 1 if the HW needs to be reset on
				 * WEP key changes */

	/* If the host performs {en,de}cryption, then set to 1 */
	int host_encrypt;
	int host_encrypt_msdu;
	int host_decrypt;
	/* host performs multicast decryption */
	int host_mc_decrypt;

	/* host should strip IV and ICV from protected frames */
	/* meaningful only when hardware decryption is being used */
	int host_strip_iv_icv;

	int host_open_frag;
	int host_build_iv;
	int ieee802_1x; /* is IEEE 802.1X used */

	/* WPA data */
	bool bHalfNMode;
	bool bHalfWirelessN24GMode;
	int wpa_enabled;
	int drop_unencrypted;
	int tkip_countermeasures;
	int privacy_invoked;
	size_t wpa_ie_len;
	u8 *wpa_ie;
	size_t wps_ie_len;
	u8 *wps_ie;
	u8 ap_mac_addr[6];
	u16 pairwise_key_type;
	u16 group_key_type;

	struct lib80211_crypt_info crypt_info;

	struct sw_cam_table swcamtable[TOTAL_CAM_ENTRY];
	int bcrx_sta_key; /* use individual keys to override default keys even
			   * with RX of broad/multicast frames */

	struct rt_pmkid_list PMKIDList[NUM_PMKID_CACHE];

	/* Fragmentation structures */
	struct rtllib_frag_entry frag_cache[17][RTLLIB_FRAG_CACHE_LEN];
	unsigned int frag_next_idx[17];
	u16 fts; /* Fragmentation Threshold */
#define DEFAULT_RTS_THRESHOLD 2346U
#define MIN_RTS_THRESHOLD 1
#define MAX_RTS_THRESHOLD 2346U
	u16 rts; /* RTS threshold */

	/* Association info */
	u8 bssid[ETH_ALEN];

	/* This stores infos for the current network.
	 * Either the network we are associated in INFRASTRUCTURE
	 * or the network that we are creating in MASTER mode.
	 * ad-hoc is a mixture ;-).
	 * Note that in infrastructure mode, even when not associated,
	 * fields bssid and essid may be valid (if wpa_set and essid_set
	 * are true) as thy carry the value set by the user via iwconfig
	 */
	struct rtllib_network current_network;

	enum rtllib_state state;

	int short_slot;
	int reg_mode;
	int mode;       /* A, B, G */
	int modulation; /* CCK, OFDM */
	int freq_band;  /* 2.4Ghz, 5.2Ghz, Mixed */
	int abg_true;   /* ABG flag	      */

	/* used for forcing the ibss workqueue to terminate
	 * without wait for the syncro scan to terminate
	 */
	short sync_scan_hurryup;
	u16 scan_watch_dog;
	int perfect_rssi;
	int worst_rssi;

	u16 prev_seq_ctl;       /* used to drop duplicate frames */

	/* map of allowed channels. 0 is dummy */
	void *pDot11dInfo;
	bool bGlobalDomain;
	u8 active_channel_map[MAX_CHANNEL_NUMBER+1];

	u8   IbssStartChnl;
	u8   ibss_maxjoin_chal;

	int rate;       /* current rate */
	int basic_rate;
	u32	currentRate;

	short active_scan;

	/* this contains flags for selectively enable softmac support */
	u16 softmac_features;

	/* if the sequence control field is not filled by HW */
	u16 seq_ctrl[5];

	/* association procedure transaction sequence number */
	u16 associate_seq;

	/* AID for RTXed association responses */
	u16 assoc_id;

	/* power save mode related*/
	u8 ack_tx_to_ieee;
	short ps;
	short sta_sleep;
	int ps_timeout;
	int ps_period;
	struct tasklet_struct ps_task;
	u64 ps_time;
	bool polling;

	short raw_tx;
	/* used if IEEE_SOFTMAC_TX_QUEUE is set */
	short queue_stop;
	short scanning_continue ;
	short proto_started;
	short proto_stoppping;

	struct semaphore wx_sem;
	struct semaphore scan_sem;
	struct semaphore ips_sem;

	spinlock_t mgmt_tx_lock;
	spinlock_t beacon_lock;

	short beacon_txing;

	short wap_set;
	short ssid_set;

	/* set on initialization */
	u8  qos_support;
	unsigned int wmm_acm;

	/* for discarding duplicated packets in IBSS */
	struct list_head ibss_mac_hash[IEEE_IBSS_MAC_HASH_SIZE];

	/* for discarding duplicated packets in BSS */
	u16 last_rxseq_num[17]; /* rx seq previous per-tid */
	u16 last_rxfrag_num[17];/* tx frag previous per-tid */
	unsigned long last_packet_time[17];

	/* for PS mode */
	unsigned long last_rx_ps_time;
	bool			bAwakePktSent;
	u8			LPSDelayCnt;

	/* used if IEEE_SOFTMAC_SINGLE_QUEUE is set */
	struct sk_buff *mgmt_queue_ring[MGMT_QUEUE_NUM];
	int mgmt_queue_head;
	int mgmt_queue_tail;
#define RTLLIB_QUEUE_LIMIT 128
	u8 AsocRetryCount;
	unsigned int hw_header;
	struct sk_buff_head skb_waitQ[MAX_QUEUE_SIZE];
	struct sk_buff_head  skb_aggQ[MAX_QUEUE_SIZE];
	struct sk_buff_head  skb_drv_aggQ[MAX_QUEUE_SIZE];
	u32	sta_edca_param[4];
	bool aggregation;
	bool enable_rx_imm_BA;
	bool bibsscoordinator;

	bool	bdynamic_txpower_enable;

	bool bCTSToSelfEnable;
	u8	CTSToSelfTH;

	u32	fsync_time_interval;
	u32	fsync_rate_bitmap;
	u8	fsync_rssi_threshold;
	bool	bfsync_enable;

	u8	fsync_multiple_timeinterval;
	u32	fsync_firstdiff_ratethreshold;
	u32	fsync_seconddiff_ratethreshold;
	enum fsync_state fsync_state;
	bool		bis_any_nonbepkts;
	struct bandwidth_autoswitch bandwidth_auto_switch;
	bool FwRWRF;

	struct rt_link_detect LinkDetectInfo;
	bool bIsAggregateFrame;
	struct rt_pwr_save_ctrl PowerSaveControl;
	u8 amsdu_in_process;

	/* used if IEEE_SOFTMAC_TX_QUEUE is set */
	struct tx_pending tx_pending;

	/* used if IEEE_SOFTMAC_ASSOCIATE is set */
	struct timer_list associate_timer;

	/* used if IEEE_SOFTMAC_BEACONS is set */
	struct timer_list beacon_timer;
	u8 need_sw_enc;
	struct work_struct associate_complete_wq;
	struct work_struct ips_leave_wq;
	struct delayed_work associate_procedure_wq;
	struct delayed_work softmac_scan_wq;
	struct delayed_work softmac_hint11d_wq;
	struct delayed_work associate_retry_wq;
	struct delayed_work start_ibss_wq;
	struct delayed_work hw_wakeup_wq;
	struct delayed_work hw_sleep_wq;
	struct delayed_work link_change_wq;
	struct work_struct wx_sync_scan_wq;

	struct workqueue_struct *wq;
	union {
		struct rtllib_rxb *RfdArray[REORDER_WIN_SIZE];
		struct rtllib_rxb *stats_IndicateArray[REORDER_WIN_SIZE];
		struct rtllib_rxb *prxbIndicateArray[REORDER_WIN_SIZE];
		struct {
			struct sw_chnl_cmd PreCommonCmd[MAX_PRECMD_CNT];
			struct sw_chnl_cmd PostCommonCmd[MAX_POSTCMD_CNT];
			struct sw_chnl_cmd RfDependCmd[MAX_RFDEPENDCMD_CNT];
		};
	};

	/* Callback functions */
	void (*set_security)(struct net_device *dev,
			     struct rtllib_security *sec);

	/* Used to TX data frame by using txb structs.
	 * this is not used if in the softmac_features
	 * is set the flag IEEE_SOFTMAC_TX_QUEUE
	 */
	int (*hard_start_xmit)(struct rtllib_txb *txb,
			       struct net_device *dev);

	int (*reset_port)(struct net_device *dev);
	int (*is_queue_full)(struct net_device *dev, int pri);

	int (*handle_management)(struct net_device *dev,
				 struct rtllib_network *network, u16 type);
	int (*is_qos_active)(struct net_device *dev, struct sk_buff *skb);

	/* Softmac-generated frames (mamagement) are TXed via this
	 * callback if the flag IEEE_SOFTMAC_SINGLE_QUEUE is
	 * not set. As some cards may have different HW queues that
	 * one might want to use for data and management frames
	 * the option to have two callbacks might be useful.
	 * This fucntion can't sleep.
	 */
	int (*softmac_hard_start_xmit)(struct sk_buff *skb,
			       struct net_device *dev);

	/* used instead of hard_start_xmit (not softmac_hard_start_xmit)
	 * if the IEEE_SOFTMAC_TX_QUEUE feature is used to TX data
	 * frames. I the option IEEE_SOFTMAC_SINGLE_QUEUE is also set
	 * then also management frames are sent via this callback.
	 * This function can't sleep.
	 */
	void (*softmac_data_hard_start_xmit)(struct sk_buff *skb,
			       struct net_device *dev, int rate);

	/* stops the HW queue for DATA frames. Useful to avoid
	 * waste time to TX data frame when we are reassociating
	 * This function can sleep.
	 */
	void (*data_hard_stop)(struct net_device *dev);

	/* OK this is complementar to data_poll_hard_stop */
	void (*data_hard_resume)(struct net_device *dev);

	/* ask to the driver to retune the radio .
	 * This function can sleep. the driver should ensure
	 * the radio has been swithced before return.
	 */
	void (*set_chan)(struct net_device *dev, short ch);

	/* These are not used if the ieee stack takes care of
	 * scanning (IEEE_SOFTMAC_SCAN feature set).
	 * In this case only the set_chan is used.
	 *
	 * The syncro version is similar to the start_scan but
	 * does not return until all channels has been scanned.
	 * this is called in user context and should sleep,
	 * it is called in a work_queue when swithcing to ad-hoc mode
	 * or in behalf of iwlist scan when the card is associated
	 * and root user ask for a scan.
	 * the fucntion stop_scan should stop both the syncro and
	 * background scanning and can sleep.
	 * The fucntion start_scan should initiate the background
	 * scanning and can't sleep.
	 */
	void (*scan_syncro)(struct net_device *dev);
	void (*start_scan)(struct net_device *dev);
	void (*stop_scan)(struct net_device *dev);

	void (*rtllib_start_hw_scan)(struct net_device *dev);
	void (*rtllib_stop_hw_scan)(struct net_device *dev);

	/* indicate the driver that the link state is changed
	 * for example it may indicate the card is associated now.
	 * Driver might be interested in this to apply RX filter
	 * rules or simply light the LINK led
	 */
	void (*link_change)(struct net_device *dev);

	/* these two function indicates to the HW when to start
	 * and stop to send beacons. This is used when the
	 * IEEE_SOFTMAC_BEACONS is not set. For now the
	 * stop_send_bacons is NOT guaranteed to be called only
	 * after start_send_beacons.
	 */
	void (*start_send_beacons)(struct net_device *dev);
	void (*stop_send_beacons)(struct net_device *dev);

	/* power save mode related */
	void (*sta_wake_up)(struct net_device *dev);
	void (*enter_sleep_state)(struct net_device *dev, u64 time);
	short (*ps_is_queue_empty)(struct net_device *dev);
	int (*handle_beacon)(struct net_device *dev,
			     struct rtllib_beacon *beacon,
			     struct rtllib_network *network);
	int (*handle_assoc_response)(struct net_device *dev,
				     struct rtllib_assoc_response_frame *resp,
				     struct rtllib_network *network);


	/* check whether Tx hw resouce available */
	short (*check_nic_enough_desc)(struct net_device *dev, int queue_index);
	short (*get_nic_desc_num)(struct net_device *dev, int queue_index);
	void (*SetBWModeHandler)(struct net_device *dev,
				 enum ht_channel_width Bandwidth,
				 enum ht_extchnl_offset Offset);
	bool (*GetNmodeSupportBySecCfg)(struct net_device *dev);
	void (*SetWirelessMode)(struct net_device *dev, u8 wireless_mode);
	bool (*GetHalfNmodeSupportByAPsHandler)(struct net_device *dev);
	u8   (*rtllib_ap_sec_type)(struct rtllib_device *ieee);
	void (*HalUsbRxAggrHandler)(struct net_device *dev, bool Value);
	void (*InitialGainHandler)(struct net_device *dev, u8 Operation);
	bool (*SetFwCmdHandler)(struct net_device *dev,
				enum fw_cmd_io_type FwCmdIO);
	void (*UpdateHalRAMaskHandler)(struct net_device *dev, bool bMulticast,
				       u8 macId, u8 MimoPs, u8 WirelessMode,
				       u8 bCurTxBW40MHz, u8 rssi_level);
	void (*UpdateBeaconInterruptHandler)(struct net_device *dev,
					     bool start);
	void (*UpdateInterruptMaskHandler)(struct net_device *dev, u32 AddMSR,
					   u32 RemoveMSR);
	u16  (*rtl_11n_user_show_rates)(struct net_device *dev);
	void (*ScanOperationBackupHandler)(struct net_device *dev,
					   u8 Operation);
	void (*LedControlHandler)(struct net_device *dev,
				  enum led_ctl_mode LedAction);
	void (*SetHwRegHandler)(struct net_device *dev, u8 variable, u8 *val);
	void (*GetHwRegHandler)(struct net_device *dev, u8 variable, u8 *val);

	void (*AllowAllDestAddrHandler)(struct net_device *dev,
					bool bAllowAllDA, bool WriteIntoReg);

	void (*rtllib_ips_leave_wq) (struct net_device *dev);
	void (*rtllib_ips_leave)(struct net_device *dev);
	void (*LeisurePSLeave)(struct net_device *dev);
	void (*rtllib_rfkill_poll)(struct net_device *dev);

	/* This must be the last item so that it points to the data
	 * allocated beyond this structure by alloc_rtllib */
	u8 priv[0];
};

#define IEEE_A	    (1<<0)
#define IEEE_B	    (1<<1)
#define IEEE_G	    (1<<2)
#define IEEE_N_24G		  (1<<4)
#define	IEEE_N_5G		  (1<<5)
#define IEEE_MODE_MASK    (IEEE_A|IEEE_B|IEEE_G)

/* Generate a 802.11 header */

/* Uses the channel change callback directly
 * instead of [start/stop] scan callbacks
 */
#define IEEE_SOFTMAC_SCAN (1<<2)

/* Perform authentication and association handshake */
#define IEEE_SOFTMAC_ASSOCIATE (1<<3)

/* Generate probe requests */
#define IEEE_SOFTMAC_PROBERQ (1<<4)

/* Generate respones to probe requests */
#define IEEE_SOFTMAC_PROBERS (1<<5)

/* The ieee802.11 stack will manages the netif queue
 * wake/stop for the driver, taking care of 802.11
 * fragmentation. See softmac.c for details. */
#define IEEE_SOFTMAC_TX_QUEUE (1<<7)

/* Uses only the softmac_data_hard_start_xmit
 * even for TX management frames.
 */
#define IEEE_SOFTMAC_SINGLE_QUEUE (1<<8)

/* Generate beacons.  The stack will enqueue beacons
 * to the card
 */
#define IEEE_SOFTMAC_BEACONS (1<<6)


static inline void *rtllib_priv(struct net_device *dev)
{
	return ((struct rtllib_device *)netdev_priv(dev))->priv;
}

extern inline int rtllib_is_empty_essid(const char *essid, int essid_len)
{
	/* Single white space is for Linksys APs */
	if (essid_len == 1 && essid[0] == ' ')
		return 1;

	/* Otherwise, if the entire essid is 0, we assume it is hidden */
	while (essid_len) {
		essid_len--;
		if (essid[essid_len] != '\0')
			return 0;
	}

	return 1;
}

extern inline int rtllib_is_valid_mode(struct rtllib_device *ieee, int mode)
{
	/*
	 * It is possible for both access points and our device to support
	 * combinations of modes, so as long as there is one valid combination
	 * of ap/device supported modes, then return success
	 *
	 */
	if ((mode & IEEE_A) &&
	    (ieee->modulation & RTLLIB_OFDM_MODULATION) &&
	    (ieee->freq_band & RTLLIB_52GHZ_BAND))
		return 1;

	if ((mode & IEEE_G) &&
	    (ieee->modulation & RTLLIB_OFDM_MODULATION) &&
	    (ieee->freq_band & RTLLIB_24GHZ_BAND))
		return 1;

	if ((mode & IEEE_B) &&
	    (ieee->modulation & RTLLIB_CCK_MODULATION) &&
	    (ieee->freq_band & RTLLIB_24GHZ_BAND))
		return 1;

	return 0;
}

extern inline int rtllib_get_hdrlen(u16 fc)
{
	int hdrlen = RTLLIB_3ADDR_LEN;

	switch (WLAN_FC_GET_TYPE(fc)) {
	case RTLLIB_FTYPE_DATA:
		if ((fc & RTLLIB_FCTL_FROMDS) && (fc & RTLLIB_FCTL_TODS))
			hdrlen = RTLLIB_4ADDR_LEN; /* Addr4 */
		if (RTLLIB_QOS_HAS_SEQ(fc))
			hdrlen += 2; /* QOS ctrl*/
		break;
	case RTLLIB_FTYPE_CTL:
		switch (WLAN_FC_GET_STYPE(fc)) {
		case RTLLIB_STYPE_CTS:
		case RTLLIB_STYPE_ACK:
			hdrlen = RTLLIB_1ADDR_LEN;
			break;
		default:
			hdrlen = RTLLIB_2ADDR_LEN;
			break;
		}
		break;
	}

	return hdrlen;
}

static inline u8 *rtllib_get_payload(struct rtllib_hdr *hdr)
{
	switch (rtllib_get_hdrlen(le16_to_cpu(hdr->frame_ctl))) {
	case RTLLIB_1ADDR_LEN:
		return ((struct rtllib_hdr_1addr *)hdr)->payload;
	case RTLLIB_2ADDR_LEN:
		return ((struct rtllib_hdr_2addr *)hdr)->payload;
	case RTLLIB_3ADDR_LEN:
		return ((struct rtllib_hdr_3addr *)hdr)->payload;
	case RTLLIB_4ADDR_LEN:
		return ((struct rtllib_hdr_4addr *)hdr)->payload;
	}
	return NULL;
}

static inline int rtllib_is_ofdm_rate(u8 rate)
{
	switch (rate & ~RTLLIB_BASIC_RATE_MASK) {
	case RTLLIB_OFDM_RATE_6MB:
	case RTLLIB_OFDM_RATE_9MB:
	case RTLLIB_OFDM_RATE_12MB:
	case RTLLIB_OFDM_RATE_18MB:
	case RTLLIB_OFDM_RATE_24MB:
	case RTLLIB_OFDM_RATE_36MB:
	case RTLLIB_OFDM_RATE_48MB:
	case RTLLIB_OFDM_RATE_54MB:
		return 1;
	}
	return 0;
}

static inline int rtllib_is_cck_rate(u8 rate)
{
	switch (rate & ~RTLLIB_BASIC_RATE_MASK) {
	case RTLLIB_CCK_RATE_1MB:
	case RTLLIB_CCK_RATE_2MB:
	case RTLLIB_CCK_RATE_5MB:
	case RTLLIB_CCK_RATE_11MB:
		return 1;
	}
	return 0;
}


/* rtllib.c */
extern void free_rtllib(struct net_device *dev);
extern struct net_device *alloc_rtllib(int sizeof_priv);

extern int rtllib_set_encryption(struct rtllib_device *ieee);

/* rtllib_tx.c */

extern int rtllib_encrypt_fragment(
	struct rtllib_device *ieee,
	struct sk_buff *frag,
	int hdr_len);

extern int rtllib_xmit(struct sk_buff *skb,  struct net_device *dev);
extern int rtllib_xmit_inter(struct sk_buff *skb, struct net_device *dev);
extern void rtllib_txb_free(struct rtllib_txb *);

/* rtllib_rx.c */
extern int rtllib_rx(struct rtllib_device *ieee, struct sk_buff *skb,
			struct rtllib_rx_stats *rx_stats);
extern void rtllib_rx_mgt(struct rtllib_device *ieee,
			     struct sk_buff *skb,
			     struct rtllib_rx_stats *stats);
extern void rtllib_rx_probe_rq(struct rtllib_device *ieee,
			   struct sk_buff *skb);
extern int rtllib_legal_channel(struct rtllib_device *rtllib, u8 channel);

/* rtllib_wx.c */
extern int rtllib_wx_get_scan(struct rtllib_device *ieee,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *key);
extern int rtllib_wx_set_encode(struct rtllib_device *ieee,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *key);
extern int rtllib_wx_get_encode(struct rtllib_device *ieee,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *key);
#if WIRELESS_EXT >= 18
extern int rtllib_wx_get_encode_ext(struct rtllib_device *ieee,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra);
extern int rtllib_wx_set_encode_ext(struct rtllib_device *ieee,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra);
#endif
extern int rtllib_wx_set_auth(struct rtllib_device *ieee,
			       struct iw_request_info *info,
			       struct iw_param *data, char *extra);
extern int rtllib_wx_set_mlme(struct rtllib_device *ieee,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra);
extern int rtllib_wx_set_gen_ie(struct rtllib_device *ieee, u8 *ie, size_t len);

/* rtllib_softmac.c */
extern short rtllib_is_54g(struct rtllib_network *net);
extern short rtllib_is_shortslot(const struct rtllib_network *net);
extern int rtllib_rx_frame_softmac(struct rtllib_device *ieee,
				   struct sk_buff *skb,
				   struct rtllib_rx_stats *rx_stats, u16 type,
				   u16 stype);
extern void rtllib_softmac_new_net(struct rtllib_device *ieee,
				   struct rtllib_network *net);

void SendDisassociation(struct rtllib_device *ieee, bool deauth, u16 asRsn);
extern void rtllib_softmac_xmit(struct rtllib_txb *txb,
				struct rtllib_device *ieee);

extern void rtllib_stop_send_beacons(struct rtllib_device *ieee);
extern void notify_wx_assoc_event(struct rtllib_device *ieee);
extern void rtllib_softmac_check_all_nets(struct rtllib_device *ieee);
extern void rtllib_start_bss(struct rtllib_device *ieee);
extern void rtllib_start_master_bss(struct rtllib_device *ieee);
extern void rtllib_start_ibss(struct rtllib_device *ieee);
extern void rtllib_softmac_init(struct rtllib_device *ieee);
extern void rtllib_softmac_free(struct rtllib_device *ieee);
extern void rtllib_associate_abort(struct rtllib_device *ieee);
extern void rtllib_disassociate(struct rtllib_device *ieee);
extern void rtllib_stop_scan(struct rtllib_device *ieee);
extern bool rtllib_act_scanning(struct rtllib_device *ieee, bool sync_scan);
extern void rtllib_stop_scan_syncro(struct rtllib_device *ieee);
extern void rtllib_start_scan_syncro(struct rtllib_device *ieee, u8 is_mesh);
extern inline struct sk_buff *rtllib_probe_req(struct rtllib_device *ieee);
extern u8 MgntQuery_MgntFrameTxRate(struct rtllib_device *ieee);
extern void rtllib_sta_ps_send_null_frame(struct rtllib_device *ieee,
					  short pwr);
extern void rtllib_sta_wakeup(struct rtllib_device *ieee, short nl);
extern void rtllib_sta_ps_send_pspoll_frame(struct rtllib_device *ieee);
extern void rtllib_check_all_nets(struct rtllib_device *ieee);
extern void rtllib_start_protocol(struct rtllib_device *ieee);
extern void rtllib_stop_protocol(struct rtllib_device *ieee, u8 shutdown);

extern void rtllib_EnableNetMonitorMode(struct net_device *dev,
					bool bInitState);
extern void rtllib_DisableNetMonitorMode(struct net_device *dev,
					 bool bInitState);
extern void rtllib_EnableIntelPromiscuousMode(struct net_device *dev,
					      bool bInitState);
extern void rtllib_DisableIntelPromiscuousMode(struct net_device *dev,
					       bool bInitState);
extern void rtllib_send_probe_requests(struct rtllib_device *ieee, u8 is_mesh);

extern void rtllib_softmac_stop_protocol(struct rtllib_device *ieee,
					 u8 mesh_flag, u8 shutdown);
extern void rtllib_softmac_start_protocol(struct rtllib_device *ieee,
					  u8 mesh_flag);

extern void rtllib_reset_queue(struct rtllib_device *ieee);
extern void rtllib_wake_queue(struct rtllib_device *ieee);
extern void rtllib_stop_queue(struct rtllib_device *ieee);
extern void rtllib_wake_all_queues(struct rtllib_device *ieee);
extern void rtllib_stop_all_queues(struct rtllib_device *ieee);
extern struct sk_buff *rtllib_get_beacon(struct rtllib_device *ieee);
extern void rtllib_start_send_beacons(struct rtllib_device *ieee);
extern void rtllib_stop_send_beacons(struct rtllib_device *ieee);
extern int rtllib_wpa_supplicant_ioctl(struct rtllib_device *ieee,
				       struct iw_point *p, u8 is_mesh);

extern void notify_wx_assoc_event(struct rtllib_device *ieee);
extern void rtllib_ps_tx_ack(struct rtllib_device *ieee, short success);

extern void softmac_mgmt_xmit(struct sk_buff *skb,
			      struct rtllib_device *ieee);
extern u16 rtllib_query_seqnum(struct rtllib_device *ieee,
			       struct sk_buff *skb, u8 *dst);
extern u8 rtllib_ap_sec_type(struct rtllib_device *ieee);

/* rtllib_crypt_ccmp&tkip&wep.c */
extern void rtllib_tkip_null(void);
extern void rtllib_wep_null(void);
extern void rtllib_ccmp_null(void);

/* rtllib_softmac_wx.c */

extern int rtllib_wx_get_wap(struct rtllib_device *ieee,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *ext);

extern int rtllib_wx_set_wap(struct rtllib_device *ieee,
			     struct iw_request_info *info,
			     union iwreq_data *awrq,
			     char *extra);

extern int rtllib_wx_get_essid(struct rtllib_device *ieee,
			       struct iw_request_info *a,
			       union iwreq_data *wrqu, char *b);

extern int rtllib_wx_set_rate(struct rtllib_device *ieee,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra);

extern int rtllib_wx_get_rate(struct rtllib_device *ieee,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra);

extern int rtllib_wx_set_mode(struct rtllib_device *ieee,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *b);

extern int rtllib_wx_set_scan(struct rtllib_device *ieee,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *b);

extern int rtllib_wx_set_essid(struct rtllib_device *ieee,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra);

extern int rtllib_wx_get_mode(struct rtllib_device *ieee,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *b);

extern int rtllib_wx_set_freq(struct rtllib_device *ieee,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *b);

extern int rtllib_wx_get_freq(struct rtllib_device *ieee,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *b);
extern void rtllib_wx_sync_scan_wq(void *data);

extern int rtllib_wx_set_rawtx(struct rtllib_device *ieee,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra);

extern int rtllib_wx_get_name(struct rtllib_device *ieee,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra);

extern int rtllib_wx_set_power(struct rtllib_device *ieee,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *extra);

extern int rtllib_wx_get_power(struct rtllib_device *ieee,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *extra);

extern int rtllib_wx_set_rts(struct rtllib_device *ieee,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra);

extern int rtllib_wx_get_rts(struct rtllib_device *ieee,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra);
#define MAX_RECEIVE_BUFFER_SIZE 9100
extern void HTDebugHTCapability(u8 *CapIE, u8 *TitleString);
extern void HTDebugHTInfo(u8 *InfoIE, u8 *TitleString);

void HTSetConnectBwMode(struct rtllib_device *ieee,
			enum ht_channel_width Bandwidth,
			enum ht_extchnl_offset Offset);
extern void HTUpdateDefaultSetting(struct rtllib_device *ieee);
extern void HTConstructCapabilityElement(struct rtllib_device *ieee,
					 u8 *posHTCap, u8 *len,
					 u8 isEncrypt, bool bAssoc);
extern void HTConstructInfoElement(struct rtllib_device *ieee,
				   u8 *posHTInfo, u8 *len, u8 isEncrypt);
extern void HTConstructRT2RTAggElement(struct rtllib_device *ieee,
				       u8 *posRT2RTAgg, u8* len);
extern void HTOnAssocRsp(struct rtllib_device *ieee);
extern void HTInitializeHTInfo(struct rtllib_device *ieee);
extern void HTInitializeBssDesc(struct bss_ht *pBssHT);
extern void HTResetSelfAndSavePeerSetting(struct rtllib_device *ieee,
					  struct rtllib_network *pNetwork);
extern void HT_update_self_and_peer_setting(struct rtllib_device *ieee,
					    struct rtllib_network *pNetwork);
extern u8 HTGetHighestMCSRate(struct rtllib_device *ieee, u8 *pMCSRateSet,
			      u8 *pMCSFilter);
extern u8 MCS_FILTER_ALL[];
extern u16 MCS_DATA_RATE[2][2][77] ;
extern u8 HTCCheck(struct rtllib_device *ieee, u8 *pFrame);
extern void HTResetIOTSetting(struct rt_hi_throughput *pHTInfo);
extern bool IsHTHalfNmodeAPs(struct rtllib_device *ieee);
extern u16 HTHalfMcsToDataRate(struct rtllib_device *ieee, u8 nMcsRate);
extern u16 HTMcsToDataRate(struct rtllib_device *ieee, u8 nMcsRate);
extern u16  TxCountToDataRate(struct rtllib_device *ieee, u8 nDataRate);
extern int rtllib_rx_ADDBAReq(struct rtllib_device *ieee, struct sk_buff *skb);
extern int rtllib_rx_ADDBARsp(struct rtllib_device *ieee, struct sk_buff *skb);
extern int rtllib_rx_DELBA(struct rtllib_device *ieee, struct sk_buff *skb);
extern void TsInitAddBA(struct rtllib_device *ieee, struct tx_ts_record *pTS,
			u8 Policy, u8 bOverwritePending);
extern void TsInitDelBA(struct rtllib_device *ieee,
			struct ts_common_info *pTsCommonInfo,
			enum tr_select TxRxSelect);
extern void BaSetupTimeOut(unsigned long data);
extern void TxBaInactTimeout(unsigned long data);
extern void RxBaInactTimeout(unsigned long data);
extern void ResetBaEntry(struct ba_record *pBA);
extern bool GetTs(
	struct rtllib_device *ieee,
	struct ts_common_info **ppTS,
	u8 *Addr,
	u8 TID,
	enum tr_select TxRxSelect,
	bool bAddNewTs
);
extern void TSInitialize(struct rtllib_device *ieee);
extern  void TsStartAddBaProcess(struct rtllib_device *ieee,
				  struct tx_ts_record *pTxTS);
extern void RemovePeerTS(struct rtllib_device *ieee, u8 *Addr);
extern void RemoveAllTS(struct rtllib_device *ieee);
void rtllib_softmac_scan_syncro(struct rtllib_device *ieee, u8 is_mesh);

extern const long rtllib_wlan_frequencies[];

extern inline void rtllib_increment_scans(struct rtllib_device *ieee)
{
	ieee->scans++;
}

extern inline int rtllib_get_scans(struct rtllib_device *ieee)
{
	return ieee->scans;
}

static inline const char *escape_essid(const char *essid, u8 essid_len)
{
	static char escaped[IW_ESSID_MAX_SIZE * 2 + 1];
	const char *s = essid;
	char *d = escaped;

	if (rtllib_is_empty_essid(essid, essid_len)) {
		memcpy(escaped, "<hidden>", sizeof("<hidden>"));
		return escaped;
	}

	essid_len = min(essid_len, (u8)IW_ESSID_MAX_SIZE);
	while (essid_len--) {
		if (*s == '\0') {
			*d++ = '\\';
			*d++ = '0';
			s++;
		} else {
			*d++ = *s++;
		}
	}
	*d = '\0';
	return escaped;
}

#define CONVERT_RATE(_ieee, _MGN_RATE)			\
	((_MGN_RATE < MGN_MCS0) ? (_MGN_RATE) :		\
	(HTMcsToDataRate(_ieee, (u8)_MGN_RATE)))

/* fun with the built-in rtllib stack... */
bool rtllib_MgntDisconnect(struct rtllib_device *rtllib, u8 asRsn);


/* For the function is more related to hardware setting, it's better to use the
 * ieee handler to refer to it.
 */
extern void rtllib_update_active_chan_map(struct rtllib_device *ieee);
extern void rtllib_FlushRxTsPendingPkts(struct rtllib_device *ieee,
					struct rx_ts_record *pTS);
extern int rtllib_data_xmit(struct sk_buff *skb, struct net_device *dev);
extern int rtllib_parse_info_param(struct rtllib_device *ieee,
		struct rtllib_info_element *info_element,
		u16 length,
		struct rtllib_network *network,
		struct rtllib_rx_stats *stats);

void rtllib_indicate_packets(struct rtllib_device *ieee,
			     struct rtllib_rxb **prxbIndicateArray, u8  index);
extern u8 HTFilterMCSRate(struct rtllib_device *ieee, u8 *pSupportMCS,
			  u8 *pOperateMCS);
extern void HTUseDefaultSetting(struct rtllib_device *ieee);
#define RT_ASOC_RETRY_LIMIT	5
u8 MgntQuery_TxRateExcludeCCKRates(struct rtllib_device *ieee);
extern void rtllib_TURBO_Info(struct rtllib_device *ieee, u8 **tag_p);
#ifndef ENABLE_LOCK_DEBUG
#define SPIN_LOCK_IEEE(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_IEEE(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_IEEE_REORDER(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_IEEE_REORDER(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_IEEE_WPAX(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_IEEE_WPAX(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_IEEE_MGNTTX(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_IEEE_MGNTTX(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_IEEE_BCN(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_IEEE_BCN(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_MSH_STAINFO(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_MSH_STAINFO(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_MSH_PREQ(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_MSH_PREQ(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_MSH_QUEUE(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_MSH_QUEUE(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_PRIV_RFPS(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_PRIV_RFPS(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_PRIV_IRQTH(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_PRIV_IRQTH(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_PRIV_TX(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_PRIV_TX(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_PRIV_D3(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_PRIV_D3(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_PRIV_RF(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_PRIV_RF(plock) spin_unlock_irqrestore((plock), flags)
#define SPIN_LOCK_PRIV_PS(plock) spin_lock_irqsave((plock), flags)
#define SPIN_UNLOCK_PRIV_PS(plock) spin_unlock_irqrestore((plock), flags)
#define SEM_DOWN_IEEE_WX(psem) down(psem)
#define SEM_UP_IEEE_WX(psem) up(psem)
#define SEM_DOWN_IEEE_SCAN(psem) down(psem)
#define SEM_UP_IEEE_SCAN(psem) up(psem)
#define SEM_DOWN_IEEE_IPS(psem) down(psem)
#define SEM_UP_IEEE_IPS(psem) up(psem)
#define SEM_DOWN_PRIV_WX(psem) down(psem)
#define SEM_UP_PRIV_WX(psem) up(psem)
#define SEM_DOWN_PRIV_RF(psem) down(psem)
#define SEM_UP_PRIV_RF(psem) up(psem)
#define MUTEX_LOCK_PRIV(pmutex) mutex_lock(pmutex)
#define MUTEX_UNLOCK_PRIV(pmutex) mutex_unlock(pmutex)
#endif

#endif /* RTLLIB_H */
