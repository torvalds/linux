/*
 * Merged with mainline ieee80211.h in Aug 2004.  Original ieee802_11
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
 * <andrea.merello@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */
#ifndef IEEE80211_H
#define IEEE80211_H
#include <linux/if_ether.h> /* ETH_ALEN */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/interrupt.h>

#include <linux/delay.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>

#include "rtl819x_HT.h"
#include "rtl819x_BA.h"
#include "rtl819x_TS.h"


#ifndef IW_MODE_MONITOR
#define IW_MODE_MONITOR 6
#endif

#ifndef IWEVCUSTOM
#define IWEVCUSTOM 0x8c02
#endif

#define KEY_TYPE_NA		0x0
#define KEY_TYPE_WEP40		0x1
#define KEY_TYPE_TKIP		0x2
#define KEY_TYPE_CCMP		0x4
#define KEY_TYPE_WEP104		0x5

/* added for rtl819x tx procedure */
#define MAX_QUEUE_SIZE		0x10

//
// 8190 queue mapping
//
#define BK_QUEUE                               0
#define BE_QUEUE                               1
#define VI_QUEUE                               2
#define VO_QUEUE                               3
#define HCCA_QUEUE                             4
#define TXCMD_QUEUE                            5
#define MGNT_QUEUE                             6
#define HIGH_QUEUE                             7
#define BEACON_QUEUE                           8

#define LOW_QUEUE                              BE_QUEUE
#define NORMAL_QUEUE                           MGNT_QUEUE

//added by amy for ps
#define SWRF_TIMEOUT				50

//added by amy for LEAP related
#define IE_CISCO_FLAG_POSITION		0x08	// Flag byte: byte 8, numbered from 0.
#define SUPPORT_CKIP_MIC			0x08	// bit3
#define SUPPORT_CKIP_PK			0x10	// bit4
/* defined for skb cb field */
/* At most 28 byte */
typedef struct cb_desc {
	/* Tx Desc Related flags (8-9) */
	u8 bLastIniPkt:1;
	u8 bCmdOrInit:1;
	u8 bFirstSeg:1;
	u8 bLastSeg:1;
	u8 bEncrypt:1;
	u8 bTxDisableRateFallBack:1;
	u8 bTxUseDriverAssingedRate:1;
	u8 bHwSec:1; //indicate whether use Hw security. WB

	u8 reserved1;

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
	//u8 reserved2:2;
	u8 drv_agg_enable:1;
	u8 reserved2:1;

	/* Tx Desc related element(12-19) */
	u8 rata_index;
	u8 queue_index;
	//u8 reserved3;
	//u8 reserved4;
	u16 txbuf_size;
	//u8 reserved5;
	u8 RATRIndex;
	u8 reserved6;
	u8 reserved7;
	u8 reserved8;

	/* Tx firmware related element(20-27) */
	u8 data_rate;
	u8 rts_rate;
	u8 ampdu_factor;
	u8 ampdu_density;
	//u8 reserved9;
	//u8 reserved10;
	//u8 reserved11;
	u8 DrvAggrNum;
	u16 pkt_size;
	u8 reserved12;
}cb_desc, *pcb_desc;

/*--------------------------Define -------------------------------------------*/
#define MGN_1M                  0x02
#define MGN_2M                  0x04
#define MGN_5_5M                0x0b
#define MGN_11M                 0x16

#define MGN_6M                  0x0c
#define MGN_9M                  0x12
#define MGN_12M                 0x18
#define MGN_18M                 0x24
#define MGN_24M                 0x30
#define MGN_36M                 0x48
#define MGN_48M                 0x60
#define MGN_54M                 0x6c

#define MGN_MCS0                0x80
#define MGN_MCS1                0x81
#define MGN_MCS2                0x82
#define MGN_MCS3                0x83
#define MGN_MCS4                0x84
#define MGN_MCS5                0x85
#define MGN_MCS6                0x86
#define MGN_MCS7                0x87
#define MGN_MCS8                0x88
#define MGN_MCS9                0x89
#define MGN_MCS10               0x8a
#define MGN_MCS11               0x8b
#define MGN_MCS12               0x8c
#define MGN_MCS13               0x8d
#define MGN_MCS14               0x8e
#define MGN_MCS15               0x8f

#define aSifsTime ((priv->ieee80211->current_network.mode == IEEE_A || \
		    priv->ieee80211->current_network.mode == IEEE_N_24G || \
		    priv->ieee80211->current_network.mode == IEEE_N_5G) ? \
		   16 : 10)

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
//It should consistent with the driver_XXX.c
//   David, 2006.9.26
#define IEEE_PARAM_WPAX_SELECT			7
//Added for notify the encryption type selection
//   David, 2006.9.26
#define IEEE_PROTO_WPA				1
#define IEEE_PROTO_RSN				2
//Added for notify the encryption type selection
//   David, 2006.9.26
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

// added for kernel conflict
#define ieee80211_crypt_deinit_entries	ieee80211_crypt_deinit_entries_rsl
#define ieee80211_crypt_deinit_handler	ieee80211_crypt_deinit_handler_rsl
#define ieee80211_crypt_delayed_deinit	ieee80211_crypt_delayed_deinit_rsl
#define ieee80211_register_crypto_ops	ieee80211_register_crypto_ops_rsl
#define ieee80211_unregister_crypto_ops ieee80211_unregister_crypto_ops_rsl
#define ieee80211_get_crypto_ops	ieee80211_get_crypto_ops_rsl

#define ieee80211_ccmp_null		ieee80211_ccmp_null_rsl

#define ieee80211_tkip_null		ieee80211_tkip_null_rsl

#define ieee80211_wep_null		ieee80211_wep_null_rsl

#define free_ieee80211			free_ieee80211_rsl
#define alloc_ieee80211			alloc_ieee80211_rsl

#define ieee80211_rx			ieee80211_rx_rsl
#define ieee80211_rx_mgt		ieee80211_rx_mgt_rsl

#define ieee80211_get_beacon		ieee80211_get_beacon_rsl
#define ieee80211_wake_queue		ieee80211_wake_queue_rsl
#define ieee80211_stop_queue		ieee80211_stop_queue_rsl
#define ieee80211_reset_queue		ieee80211_reset_queue_rsl
#define ieee80211_softmac_stop_protocol	ieee80211_softmac_stop_protocol_rsl
#define ieee80211_softmac_start_protocol ieee80211_softmac_start_protocol_rsl
#define ieee80211_is_shortslot		ieee80211_is_shortslot_rsl
#define ieee80211_is_54g		ieee80211_is_54g_rsl
#define ieee80211_wpa_supplicant_ioctl	ieee80211_wpa_supplicant_ioctl_rsl
#define ieee80211_ps_tx_ack		ieee80211_ps_tx_ack_rsl
#define ieee80211_softmac_xmit		ieee80211_softmac_xmit_rsl
#define ieee80211_stop_send_beacons	ieee80211_stop_send_beacons_rsl
#define notify_wx_assoc_event		notify_wx_assoc_event_rsl
#define SendDisassociation		SendDisassociation_rsl
#define ieee80211_disassociate		ieee80211_disassociate_rsl
#define ieee80211_start_send_beacons	ieee80211_start_send_beacons_rsl
#define ieee80211_stop_scan		ieee80211_stop_scan_rsl
#define ieee80211_send_probe_requests	ieee80211_send_probe_requests_rsl
#define ieee80211_softmac_scan_syncro	ieee80211_softmac_scan_syncro_rsl
#define ieee80211_start_scan_syncro	ieee80211_start_scan_syncro_rsl

#define ieee80211_wx_get_essid		ieee80211_wx_get_essid_rsl
#define ieee80211_wx_set_essid		ieee80211_wx_set_essid_rsl
#define ieee80211_wx_set_rate		ieee80211_wx_set_rate_rsl
#define ieee80211_wx_get_rate		ieee80211_wx_get_rate_rsl
#define ieee80211_wx_set_wap		ieee80211_wx_set_wap_rsl
#define ieee80211_wx_get_wap		ieee80211_wx_get_wap_rsl
#define ieee80211_wx_set_mode		ieee80211_wx_set_mode_rsl
#define ieee80211_wx_get_mode		ieee80211_wx_get_mode_rsl
#define ieee80211_wx_set_scan		ieee80211_wx_set_scan_rsl
#define ieee80211_wx_get_freq		ieee80211_wx_get_freq_rsl
#define ieee80211_wx_set_freq		ieee80211_wx_set_freq_rsl
#define ieee80211_wx_set_rawtx		ieee80211_wx_set_rawtx_rsl
#define ieee80211_wx_get_name		ieee80211_wx_get_name_rsl
#define ieee80211_wx_set_power		ieee80211_wx_set_power_rsl
#define ieee80211_wx_get_power		ieee80211_wx_get_power_rsl
#define ieee80211_wlan_frequencies	ieee80211_wlan_frequencies_rsl
#define ieee80211_wx_set_rts		ieee80211_wx_set_rts_rsl
#define ieee80211_wx_get_rts		ieee80211_wx_get_rts_rsl

#define ieee80211_txb_free		ieee80211_txb_free_rsl

#define ieee80211_wx_set_gen_ie		ieee80211_wx_set_gen_ie_rsl
#define ieee80211_wx_get_scan		ieee80211_wx_get_scan_rsl
#define ieee80211_wx_set_encode		ieee80211_wx_set_encode_rsl
#define ieee80211_wx_get_encode		ieee80211_wx_get_encode_rsl
#define ieee80211_wx_set_mlme		ieee80211_wx_set_mlme_rsl
#define ieee80211_wx_set_auth		ieee80211_wx_set_auth_rsl
#define ieee80211_wx_set_encode_ext	ieee80211_wx_set_encode_ext_rsl
#define ieee80211_wx_get_encode_ext	ieee80211_wx_get_encode_ext_rsl


typedef struct ieee_param {
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
		struct{
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
}ieee_param;


// linux under 2.6.9 release may not support it, so modify it for common use
#define MSECS(t) msecs_to_jiffies(t)
#define msleep_interruptible_rsl  msleep_interruptible

#define IEEE80211_DATA_LEN		2304
/* Maximum size for the MA-UNITDATA primitive, 802.11 standard section
   6.2.1.1.2.

   The figure in section 7.1.2 suggests a body size of up to 2312
   bytes is allowed, which is a bit confusing, I suspect this
   represents the 2304 bytes of real data, plus a possible 8 bytes of
   WEP IV and ICV. (this interpretation suggested by Ramiro Barreiro) */
#define IEEE80211_1ADDR_LEN 10
#define IEEE80211_2ADDR_LEN 16
#define IEEE80211_3ADDR_LEN 24
#define IEEE80211_4ADDR_LEN 30
#define IEEE80211_FCS_LEN    4
#define IEEE80211_HLEN                  (IEEE80211_4ADDR_LEN)
#define IEEE80211_FRAME_LEN             (IEEE80211_DATA_LEN + IEEE80211_HLEN)
#define IEEE80211_MGMT_HDR_LEN 24
#define IEEE80211_DATA_HDR3_LEN 24
#define IEEE80211_DATA_HDR4_LEN 30

#define MIN_FRAG_THRESHOLD     256U
#define MAX_FRAG_THRESHOLD     2346U


/* Frame control field constants */
#define IEEE80211_FCTL_VERS		0x0003
#define IEEE80211_FCTL_FTYPE		0x000c
#define IEEE80211_FCTL_STYPE		0x00f0
#define IEEE80211_FCTL_FRAMETYPE	0x00fc
#define IEEE80211_FCTL_TODS		0x0100
#define IEEE80211_FCTL_FROMDS		0x0200
#define IEEE80211_FCTL_DSTODS		0x0300 //added by david
#define IEEE80211_FCTL_MOREFRAGS	0x0400
#define IEEE80211_FCTL_RETRY		0x0800
#define IEEE80211_FCTL_PM		0x1000
#define IEEE80211_FCTL_MOREDATA		0x2000
#define IEEE80211_FCTL_WEP		0x4000
#define IEEE80211_FCTL_ORDER		0x8000

#define IEEE80211_FTYPE_MGMT		0x0000
#define IEEE80211_FTYPE_CTL		0x0004
#define IEEE80211_FTYPE_DATA		0x0008

/* management */
#define IEEE80211_STYPE_ASSOC_REQ	0x0000
#define IEEE80211_STYPE_ASSOC_RESP	0x0010
#define IEEE80211_STYPE_REASSOC_REQ	0x0020
#define IEEE80211_STYPE_REASSOC_RESP	0x0030
#define IEEE80211_STYPE_PROBE_REQ	0x0040
#define IEEE80211_STYPE_PROBE_RESP	0x0050
#define IEEE80211_STYPE_BEACON		0x0080
#define IEEE80211_STYPE_ATIM		0x0090
#define IEEE80211_STYPE_DISASSOC	0x00A0
#define IEEE80211_STYPE_AUTH		0x00B0
#define IEEE80211_STYPE_DEAUTH		0x00C0
#define IEEE80211_STYPE_MANAGE_ACT	0x00D0

/* control */
#define IEEE80211_STYPE_PSPOLL		0x00A0
#define IEEE80211_STYPE_RTS		0x00B0
#define IEEE80211_STYPE_CTS		0x00C0
#define IEEE80211_STYPE_ACK		0x00D0
#define IEEE80211_STYPE_CFEND		0x00E0
#define IEEE80211_STYPE_CFENDACK	0x00F0
#define IEEE80211_STYPE_BLOCKACK   0x0094

/* data */
#define IEEE80211_STYPE_DATA		0x0000
#define IEEE80211_STYPE_DATA_CFACK	0x0010
#define IEEE80211_STYPE_DATA_CFPOLL	0x0020
#define IEEE80211_STYPE_DATA_CFACKPOLL	0x0030
#define IEEE80211_STYPE_NULLFUNC	0x0040
#define IEEE80211_STYPE_CFACK		0x0050
#define IEEE80211_STYPE_CFPOLL		0x0060
#define IEEE80211_STYPE_CFACKPOLL	0x0070
#define IEEE80211_STYPE_QOS_DATA	0x0080 //added for WMM 2006/8/2
#define IEEE80211_STYPE_QOS_NULL	0x00C0

#define IEEE80211_SCTL_FRAG		0x000F
#define IEEE80211_SCTL_SEQ		0xFFF0

/* QOS control */
#define IEEE80211_QCTL_TID              0x000F

#define	FC_QOS_BIT					BIT7
#define IsDataFrame(pdu)			( ((pdu[0] & 0x0C)==0x08) ? true : false )
#define	IsLegacyDataFrame(pdu)	(IsDataFrame(pdu) && (!(pdu[0]&FC_QOS_BIT)) )
//added by wb. Is this right?
#define IsQoSDataFrame(pframe)  ((*(u16 *)pframe&(IEEE80211_STYPE_QOS_DATA|IEEE80211_FTYPE_DATA)) == (IEEE80211_STYPE_QOS_DATA|IEEE80211_FTYPE_DATA))
#define Frame_Order(pframe)     (*(u16 *)pframe&IEEE80211_FCTL_ORDER)
#define SN_LESS(a, b)		(((a-b)&0x800)!=0)
#define SN_EQUAL(a, b)	(a == b)
#define MAX_DEV_ADDR_SIZE 8
typedef enum _ACT_CATEGORY{
	ACT_CAT_QOS = 1,
	ACT_CAT_DLS = 2,
	ACT_CAT_BA  = 3,
	ACT_CAT_HT  = 7,
	ACT_CAT_WMM = 17,
} ACT_CATEGORY, *PACT_CATEGORY;

typedef enum _TS_ACTION{
	ACT_ADDTSREQ = 0,
	ACT_ADDTSRSP = 1,
	ACT_DELTS    = 2,
	ACT_SCHEDULE = 3,
} TS_ACTION, *PTS_ACTION;

typedef enum _BA_ACTION{
	ACT_ADDBAREQ = 0,
	ACT_ADDBARSP = 1,
	ACT_DELBA    = 2,
} BA_ACTION, *PBA_ACTION;

typedef enum _InitialGainOpType{
	IG_Backup=0,
	IG_Restore,
	IG_Max
}InitialGainOpType;

/* debug macros */
#define CONFIG_IEEE80211_DEBUG
#ifdef CONFIG_IEEE80211_DEBUG
extern u32 ieee80211_debug_level;
#define IEEE80211_DEBUG(level, fmt, args...) \
do { if (ieee80211_debug_level & (level)) \
  printk(KERN_DEBUG "ieee80211: " fmt, ## args); } while (0)
//wb added to debug out data buf
//if you want print DATA buffer related BA, please set ieee80211_debug_level to DATA|BA
#define IEEE80211_DEBUG_DATA(level, data, datalen)	\
	do{ if ((ieee80211_debug_level & (level)) == (level))	\
		{	\
			int i;					\
			u8 *pdata = (u8 *) data;			\
			printk(KERN_DEBUG "ieee80211: %s()\n", __func__);	\
			for(i=0; i<(int)(datalen); i++)			\
			{						\
				printk("%2x ", pdata[i]);		\
				if ((i+1)%16 == 0) printk("\n");	\
			}				\
			printk("\n");			\
		}					\
	} while (0)
#else
#define IEEE80211_DEBUG(level, fmt, args...) do {} while (0)
#define IEEE80211_DEBUG_DATA(level, data, datalen) do {} while(0)
#endif	/* CONFIG_IEEE80211_DEBUG */

/* debug macros not dependent on CONFIG_IEEE80211_DEBUG */

/*
 * To use the debug system;
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of:
 *
 * #define IEEE80211_DL_xxxx VALUE
 *
 * shifting value to the left one bit from the previous entry.  xxxx should be
 * the name of the classification (for example, WEP)
 *
 * You then need to either add a IEEE80211_xxxx_DEBUG() macro definition for your
 * classification, or use IEEE80211_DEBUG(IEEE80211_DL_xxxx, ...) whenever you want
 * to send output to that classification.
 *
 * To add your debug level to the list of levels seen when you perform
 *
 * % cat /proc/net/ipw/debug_level
 *
 * you simply need to add your entry to the ipw_debug_levels array.
 *
 * If you do not see debug_level in /proc/net/ipw then you do not have
 * CONFIG_IEEE80211_DEBUG defined in your kernel configuration
 *
 */

#define IEEE80211_DL_INFO          (1<<0)
#define IEEE80211_DL_WX            (1<<1)
#define IEEE80211_DL_SCAN          (1<<2)
#define IEEE80211_DL_STATE         (1<<3)
#define IEEE80211_DL_MGMT          (1<<4)
#define IEEE80211_DL_FRAG          (1<<5)
#define IEEE80211_DL_EAP           (1<<6)
#define IEEE80211_DL_DROP          (1<<7)

#define IEEE80211_DL_TX            (1<<8)
#define IEEE80211_DL_RX            (1<<9)

#define IEEE80211_DL_HT		   (1<<10)  //HT
#define IEEE80211_DL_BA		   (1<<11)  //ba
#define IEEE80211_DL_TS		   (1<<12)  //TS
#define IEEE80211_DL_QOS           (1<<13)
#define IEEE80211_DL_REORDER	   (1<<14)
#define IEEE80211_DL_IOT	   (1<<15)
#define IEEE80211_DL_IPS	   (1<<16)
#define IEEE80211_DL_TRACE	   (1<<29)  //trace function, need to user net_ratelimit() together in order not to print too much to the screen
#define IEEE80211_DL_DATA	   (1<<30)   //use this flag to control whether print data buf out.
#define IEEE80211_DL_ERR	   (1<<31)   //always open
#define IEEE80211_ERROR(f, a...) printk(KERN_ERR "ieee80211: " f, ## a)
#define IEEE80211_WARNING(f, a...) printk(KERN_WARNING "ieee80211: " f, ## a)
#define IEEE80211_DEBUG_INFO(f, a...)   IEEE80211_DEBUG(IEEE80211_DL_INFO, f, ## a)

#define IEEE80211_DEBUG_WX(f, a...)     IEEE80211_DEBUG(IEEE80211_DL_WX, f, ## a)
#define IEEE80211_DEBUG_SCAN(f, a...)   IEEE80211_DEBUG(IEEE80211_DL_SCAN, f, ## a)
#define IEEE80211_DEBUG_STATE(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_STATE, f, ## a)
#define IEEE80211_DEBUG_MGMT(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_MGMT, f, ## a)
#define IEEE80211_DEBUG_FRAG(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_FRAG, f, ## a)
#define IEEE80211_DEBUG_EAP(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_EAP, f, ## a)
#define IEEE80211_DEBUG_DROP(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_DROP, f, ## a)
#define IEEE80211_DEBUG_TX(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_TX, f, ## a)
#define IEEE80211_DEBUG_RX(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_RX, f, ## a)
#define IEEE80211_DEBUG_QOS(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_QOS, f, ## a)

#include <linux/if_arp.h> /* ARPHRD_ETHER */

#ifndef WIRELESS_SPY
#define WIRELESS_SPY		// enable iwspy support
#endif
#include <net/iw_handler.h>	// new driver API

#ifndef ETH_P_PAE
#define ETH_P_PAE 0x888E /* Port Access Entity (IEEE 802.1X) */
#endif /* ETH_P_PAE */

#define ETH_P_PREAUTH 0x88C7 /* IEEE 802.11i pre-authentication */

#ifndef ETH_P_80211_RAW
#define ETH_P_80211_RAW (ETH_P_ECONET + 1)
#endif

/* IEEE 802.11 defines */

#define P80211_OUI_LEN 3

struct ieee80211_snap_hdr {

	u8    dsap;   /* always 0xAA */
	u8    ssap;   /* always 0xAA */
	u8    ctrl;   /* always 0x03 */
	u8    oui[P80211_OUI_LEN];    /* organizational universal id */

} __packed;

#define SNAP_SIZE sizeof(struct ieee80211_snap_hdr)

#define WLAN_FC_GET_VERS(fc) ((fc) & IEEE80211_FCTL_VERS)
#define WLAN_FC_GET_TYPE(fc) ((fc) & IEEE80211_FCTL_FTYPE)
#define WLAN_FC_GET_STYPE(fc) ((fc) & IEEE80211_FCTL_STYPE)

#define WLAN_FC_GET_FRAMETYPE(fc) ((fc) & IEEE80211_FCTL_FRAMETYPE)
#define WLAN_GET_SEQ_FRAG(seq) ((seq) & IEEE80211_SCTL_FRAG)
#define WLAN_GET_SEQ_SEQ(seq)  (((seq) & IEEE80211_SCTL_SEQ) >> 4)

#define WLAN_CAPABILITY_BSS (1<<0)
#define WLAN_CAPABILITY_IBSS (1<<1)
#define WLAN_CAPABILITY_CF_POLLABLE (1<<2)
#define WLAN_CAPABILITY_CF_POLL_REQUEST (1<<3)
#define WLAN_CAPABILITY_PRIVACY (1<<4)
#define WLAN_CAPABILITY_SHORT_PREAMBLE (1<<5)
#define WLAN_CAPABILITY_PBCC (1<<6)
#define WLAN_CAPABILITY_CHANNEL_AGILITY (1<<7)
#define WLAN_CAPABILITY_SPECTRUM_MGMT (1<<8)
#define WLAN_CAPABILITY_QOS (1<<9)
#define WLAN_CAPABILITY_SHORT_SLOT (1<<10)
#define WLAN_CAPABILITY_DSSS_OFDM (1<<13)

/* 802.11g ERP information element */
#define WLAN_ERP_NON_ERP_PRESENT (1<<0)
#define WLAN_ERP_USE_PROTECTION (1<<1)
#define WLAN_ERP_BARKER_PREAMBLE (1<<2)

#define IEEE80211_STATMASK_SIGNAL (1<<0)
#define IEEE80211_STATMASK_RSSI (1<<1)
#define IEEE80211_STATMASK_NOISE (1<<2)
#define IEEE80211_STATMASK_RATE (1<<3)
#define IEEE80211_STATMASK_WEMASK 0x7

#define IEEE80211_CCK_MODULATION    (1<<0)
#define IEEE80211_OFDM_MODULATION   (1<<1)

#define IEEE80211_24GHZ_BAND     (1<<0)
#define IEEE80211_52GHZ_BAND     (1<<1)

#define IEEE80211_CCK_RATE_LEN			4
#define IEEE80211_CCK_RATE_1MB			0x02
#define IEEE80211_CCK_RATE_2MB			0x04
#define IEEE80211_CCK_RATE_5MB			0x0B
#define IEEE80211_CCK_RATE_11MB			0x16
#define IEEE80211_OFDM_RATE_LEN			8
#define IEEE80211_OFDM_RATE_6MB			0x0C
#define IEEE80211_OFDM_RATE_9MB			0x12
#define IEEE80211_OFDM_RATE_12MB		0x18
#define IEEE80211_OFDM_RATE_18MB		0x24
#define IEEE80211_OFDM_RATE_24MB		0x30
#define IEEE80211_OFDM_RATE_36MB		0x48
#define IEEE80211_OFDM_RATE_48MB		0x60
#define IEEE80211_OFDM_RATE_54MB		0x6C
#define IEEE80211_BASIC_RATE_MASK		0x80

#define IEEE80211_CCK_RATE_1MB_MASK		(1<<0)
#define IEEE80211_CCK_RATE_2MB_MASK		(1<<1)
#define IEEE80211_CCK_RATE_5MB_MASK		(1<<2)
#define IEEE80211_CCK_RATE_11MB_MASK		(1<<3)
#define IEEE80211_OFDM_RATE_6MB_MASK		(1<<4)
#define IEEE80211_OFDM_RATE_9MB_MASK		(1<<5)
#define IEEE80211_OFDM_RATE_12MB_MASK		(1<<6)
#define IEEE80211_OFDM_RATE_18MB_MASK		(1<<7)
#define IEEE80211_OFDM_RATE_24MB_MASK		(1<<8)
#define IEEE80211_OFDM_RATE_36MB_MASK		(1<<9)
#define IEEE80211_OFDM_RATE_48MB_MASK		(1<<10)
#define IEEE80211_OFDM_RATE_54MB_MASK		(1<<11)

#define IEEE80211_CCK_RATES_MASK		0x0000000F
#define IEEE80211_CCK_BASIC_RATES_MASK	(IEEE80211_CCK_RATE_1MB_MASK | \
	IEEE80211_CCK_RATE_2MB_MASK)
#define IEEE80211_CCK_DEFAULT_RATES_MASK	(IEEE80211_CCK_BASIC_RATES_MASK | \
	IEEE80211_CCK_RATE_5MB_MASK | \
	IEEE80211_CCK_RATE_11MB_MASK)

#define IEEE80211_OFDM_RATES_MASK		0x00000FF0
#define IEEE80211_OFDM_BASIC_RATES_MASK	(IEEE80211_OFDM_RATE_6MB_MASK | \
	IEEE80211_OFDM_RATE_12MB_MASK | \
	IEEE80211_OFDM_RATE_24MB_MASK)
#define IEEE80211_OFDM_DEFAULT_RATES_MASK	(IEEE80211_OFDM_BASIC_RATES_MASK | \
	IEEE80211_OFDM_RATE_9MB_MASK  | \
	IEEE80211_OFDM_RATE_18MB_MASK | \
	IEEE80211_OFDM_RATE_36MB_MASK | \
	IEEE80211_OFDM_RATE_48MB_MASK | \
	IEEE80211_OFDM_RATE_54MB_MASK)
#define IEEE80211_DEFAULT_RATES_MASK (IEEE80211_OFDM_DEFAULT_RATES_MASK | \
				IEEE80211_CCK_DEFAULT_RATES_MASK)

#define IEEE80211_NUM_OFDM_RATES	    8
#define IEEE80211_NUM_CCK_RATES		    4
#define IEEE80211_OFDM_SHIFT_MASK_A         4


/* this is stolen and modified from the madwifi driver*/
#define IEEE80211_FC0_TYPE_MASK		0x0c
#define IEEE80211_FC0_TYPE_DATA		0x08
#define IEEE80211_FC0_SUBTYPE_MASK	0xB0
#define IEEE80211_FC0_SUBTYPE_QOS	0x80

#define IEEE80211_QOS_HAS_SEQ(fc) \
	(((fc) & (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) == \
	 (IEEE80211_FC0_TYPE_DATA | IEEE80211_FC0_SUBTYPE_QOS))

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
struct ieee80211_rx_stats {
	u32 mac_time[2];
	s8 rssi;
	u8 signal;
	u8 noise;
	u16 rate; /* in 100 kbps */
	u8 received_channel;
	u8 control;
	u8 mask;
	u8 freq;
	u16 len;
	u64 tsf;
	u32 beacon_time;
	u8 nic_type;
	u16       Length;
	//      u8        DataRate;      // In 0.5 Mbps
	u8        SignalQuality; // in 0-100 index.
	s32       RecvSignalPower; // Real power in dBm for this packet, no beautification and aggregation.
	s8        RxPower; // in dBm Translate from PWdB
	u8        SignalStrength; // in 0-100 index.
	u16       bHwError:1;
	u16       bCRC:1;
	u16       bICV:1;
	u16       bShortPreamble:1;
	u16       Antenna:1;      //for rtl8185
	u16       Decrypted:1;    //for rtl8185, rtl8187
	u16       Wakeup:1;       //for rtl8185
	u16       Reserved0:1;    //for rtl8185
	u8        AGC;
	u32       TimeStampLow;
	u32       TimeStampHigh;
	bool      bShift;
	bool      bIsQosData;             // Added by Annie, 2005-12-22.
	u8        UserPriority;

	//1!!!!!!!!!!!!!!!!!!!!!!!!!!!
	//1Attention Please!!!<11n or 8190 specific code should be put below this line>
	//1!!!!!!!!!!!!!!!!!!!!!!!!!!!

	u8        RxDrvInfoSize;
	u8        RxBufShift;
	bool      bIsAMPDU;
	bool      bFirstMPDU;
	bool      bContainHTC;
	bool      RxIs40MHzPacket;
	u32       RxPWDBAll;
	u8        RxMIMOSignalStrength[4];        // in 0~100 index
	s8        RxMIMOSignalQuality[2];
	bool      bPacketMatchBSSID;
	bool      bIsCCK;
	bool      bPacketToSelf;
	//added by amy
	u8        *virtual_address;
	u16          packetlength;              // Total packet length: Must equal to sum of all FragLength
	u16          fraglength;                        // FragLength should equal to PacketLength in non-fragment case
	u16          fragoffset;                        // Data offset for this fragment
	u16          ntotalfrag;
	bool		  bisrxaggrsubframe;
	bool		  bPacketBeacon;	//cosa add for rssi
	bool		  bToSelfBA;		//cosa add for rssi
	char	  cck_adc_pwdb[4];	//cosa add for rx path selection
	u16		  Seq_Num;

};

/* IEEE 802.11 requires that STA supports concurrent reception of at least
 * three fragmented frames. This define can be increased to support more
 * concurrent frames, but it should be noted that each entry can consume about
 * 2 kB of RAM and increasing cache size will slow down frame reassembly. */
#define IEEE80211_FRAG_CACHE_LEN 4

struct ieee80211_frag_entry {
	unsigned long first_frag_time;
	unsigned int seq;
	unsigned int last_frag;
	struct sk_buff *skb;
	u8 src_addr[ETH_ALEN];
	u8 dst_addr[ETH_ALEN];
};

struct ieee80211_stats {
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

struct ieee80211_device;

#include "ieee80211_crypt.h"

#define SEC_KEY_1         (1<<0)
#define SEC_KEY_2         (1<<1)
#define SEC_KEY_3         (1<<2)
#define SEC_KEY_4         (1<<3)
#define SEC_ACTIVE_KEY    (1<<4)
#define SEC_AUTH_MODE     (1<<5)
#define SEC_UNICAST_GROUP (1<<6)
#define SEC_LEVEL         (1<<7)
#define SEC_ENABLED       (1<<8)
#define SEC_ENCRYPT       (1<<9)

#define SEC_LEVEL_0      0 /* None */
#define SEC_LEVEL_1      1 /* WEP 40 and 104 bit */
#define SEC_LEVEL_2      2 /* Level 1 + TKIP */
#define SEC_LEVEL_2_CKIP 3 /* Level 1 + CKIP */
#define SEC_LEVEL_3      4 /* Level 2 + CCMP */

#define SEC_ALG_NONE            0
#define SEC_ALG_WEP             1
#define SEC_ALG_TKIP            2
#define SEC_ALG_CCMP            3

#define WEP_KEYS		4
#define WEP_KEY_LEN		13
#define SCM_KEY_LEN             32
#define SCM_TEMPORAL_KEY_LENGTH 16

struct ieee80211_security {
	u16 active_key:2,
	    enabled:1,
	    auth_algo:4,
	    unicast_uses_group:1,
	    encrypt:1;
	u8 auth_mode;
	u8 key_sizes[WEP_KEYS];
	u8 keys[WEP_KEYS][SCM_KEY_LEN];
	u8 level;
	u16 flags;
} __packed;


/*
 802.11 data frame from AP
      ,-------------------------------------------------------------------.
Bytes |  2   |  2   |    6    |    6    |    6    |  2   | 0..2312 |   4  |
      |------|------|---------|---------|---------|------|---------|------|
Desc. | ctrl | dura |  DA/RA  |   TA    |    SA   | Sequ |  frame  |  fcs |
      |      | tion | (BSSID) |         |         | ence |  data   |      |
      `-------------------------------------------------------------------'
Total: 28-2340 bytes
*/

/* Management Frame Information Element Types */
enum ieee80211_mfie {
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
	MFIE_TYPE_RSN = 48,
	MFIE_TYPE_RATES_EX = 50,
	MFIE_TYPE_HT_CAP= 45,
	 MFIE_TYPE_HT_INFO= 61,
	 MFIE_TYPE_AIRONET=133,
	MFIE_TYPE_GENERIC = 221,
	MFIE_TYPE_QOS_PARAMETER = 222,
};

/* Minimal header; can be used for passing 802.11 frames with sufficient
 * information to determine what type of underlying data type is actually
 * stored in the data. */
struct rtl_80211_hdr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 payload[0];
} __packed;

struct rtl_80211_hdr_1addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 payload[0];
} __packed;

struct rtl_80211_hdr_2addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 payload[0];
} __packed;

struct rtl_80211_hdr_3addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 payload[0];
} __packed;

struct rtl_80211_hdr_4addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 addr4[ETH_ALEN];
	u8 payload[0];
} __packed;

struct rtl_80211_hdr_3addrqos {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 payload[0];
	__le16 qos_ctl;
} __packed;

struct rtl_80211_hdr_4addrqos {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 addr4[ETH_ALEN];
	u8 payload[0];
	__le16 qos_ctl;
} __packed;

struct ieee80211_info_element {
	u8 id;
	u8 len;
	u8 data[0];
} __packed;

struct ieee80211_authentication {
	struct rtl_80211_hdr_3addr header;
	__le16 algorithm;
	__le16 transaction;
	__le16 status;
	/*challenge*/
	struct ieee80211_info_element info_element[0];
} __packed;

struct ieee80211_disassoc {
	struct rtl_80211_hdr_3addr header;
	__le16 reason;
} __packed;

struct ieee80211_probe_request {
	struct rtl_80211_hdr_3addr header;
	/* SSID, supported rates */
	struct ieee80211_info_element info_element[0];
} __packed;

struct ieee80211_probe_response {
	struct rtl_80211_hdr_3addr header;
	__le32 time_stamp[2];
	__le16 beacon_interval;
	__le16 capability;
	/* SSID, supported rates, FH params, DS params,
	 * CF params, IBSS params, TIM (if beacon), RSN */
	struct ieee80211_info_element info_element[0];
} __packed;

/* Alias beacon for probe_response */
#define ieee80211_beacon ieee80211_probe_response

struct ieee80211_assoc_request_frame {
	struct rtl_80211_hdr_3addr header;
	__le16 capability;
	__le16 listen_interval;
	/* SSID, supported rates, RSN */
	struct ieee80211_info_element info_element[0];
} __packed;

struct ieee80211_reassoc_request_frame {
	struct rtl_80211_hdr_3addr header;
	__le16 capability;
	__le16 listen_interval;
	u8 current_ap[ETH_ALEN];
	/* SSID, supported rates, RSN */
	struct ieee80211_info_element info_element[0];
} __packed;

struct ieee80211_assoc_response_frame {
	struct rtl_80211_hdr_3addr header;
	__le16 capability;
	__le16 status;
	__le16 aid;
	struct ieee80211_info_element info_element[0]; /* supported rates */
} __packed;

struct ieee80211_txb {
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
struct ieee80211_drv_agg_txb {
	u8 nr_drv_agg_frames;
	struct sk_buff *tx_agg_frames[MAX_TX_AGG_COUNT];
} __packed;

#define MAX_SUBFRAME_COUNT		  64
struct ieee80211_rxb {
	u8 nr_subframes;
	struct sk_buff *subframes[MAX_SUBFRAME_COUNT];
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
} __packed;

typedef union _frameqos {
	u16 shortdata;
	u8  chardata[2];
	struct {
		u16 tid:4;
		u16 eosp:1;
		u16 ack_policy:2;
		u16 reserved:1;
		u16 txop:8;
	}field;
} frameqos, *pframeqos;

/* SWEEP TABLE ENTRIES NUMBER*/
#define MAX_SWEEP_TAB_ENTRIES		  42
#define MAX_SWEEP_TAB_ENTRIES_PER_PACKET  7
/* MAX_RATES_LENGTH needs to be 12.  The spec says 8, and many APs
 * only use 8, and then use extended rates for the remaining supported
 * rates.  Other APs, however, stick all of their supported rates on the
 * main rates information element... */
#define MAX_RATES_LENGTH                  ((u8)12)
#define MAX_RATES_EX_LENGTH               ((u8)16)
#define MAX_NETWORK_COUNT                  128

#define MAX_CHANNEL_NUMBER                 161
#define IEEE80211_SOFTMAC_SCAN_TIME	   100
//(HZ / 2)
#define IEEE80211_SOFTMAC_ASSOC_RETRY_TIME (HZ * 2)

#define CRC_LENGTH                 4U

#define MAX_WPA_IE_LEN 64

#define NETWORK_EMPTY_ESSID (1<<0)
#define NETWORK_HAS_OFDM    (1<<1)
#define NETWORK_HAS_CCK     (1<<2)

/* QoS structure */
#define NETWORK_HAS_QOS_PARAMETERS      (1<<3)
#define NETWORK_HAS_QOS_INFORMATION     (1<<4)
#define NETWORK_HAS_QOS_MASK            (NETWORK_HAS_QOS_PARAMETERS | \
					 NETWORK_HAS_QOS_INFORMATION)
/* 802.11h */
#define NETWORK_HAS_POWER_CONSTRAINT    (1<<5)
#define NETWORK_HAS_CSA                 (1<<6)
#define NETWORK_HAS_QUIET               (1<<7)
#define NETWORK_HAS_IBSS_DFS            (1<<8)
#define NETWORK_HAS_TPC_REPORT          (1<<9)

#define NETWORK_HAS_ERP_VALUE           (1<<10)

#define QOS_QUEUE_NUM                   4
#define QOS_OUI_LEN                     3
#define QOS_OUI_TYPE                    2
#define QOS_ELEMENT_ID                  221
#define QOS_OUI_INFO_SUB_TYPE           0
#define QOS_OUI_PARAM_SUB_TYPE          1
#define QOS_VERSION_1                   1
#define QOS_AIFSN_MIN_VALUE             2
struct ieee80211_qos_information_element {
	u8 elementID;
	u8 length;
	u8 qui[QOS_OUI_LEN];
	u8 qui_type;
	u8 qui_subtype;
	u8 version;
	u8 ac_info;
} __packed;

struct ieee80211_qos_ac_parameter {
	u8 aci_aifsn;
	u8 ecw_min_max;
	__le16 tx_op_limit;
} __packed;

struct ieee80211_qos_parameter_info {
	struct ieee80211_qos_information_element info_element;
	u8 reserved;
	struct ieee80211_qos_ac_parameter ac_params_record[QOS_QUEUE_NUM];
} __packed;

struct ieee80211_qos_parameters {
	__le16 cw_min[QOS_QUEUE_NUM];
	__le16 cw_max[QOS_QUEUE_NUM];
	u8 aifs[QOS_QUEUE_NUM];
	u8 flag[QOS_QUEUE_NUM];
	__le16 tx_op_limit[QOS_QUEUE_NUM];
} __packed;

struct ieee80211_qos_data {
	struct ieee80211_qos_parameters parameters;
	int active;
	int supported;
	u8 param_count;
	u8 old_param_count;
};

struct ieee80211_tim_parameters {
	u8 tim_count;
	u8 tim_period;
} __packed;

//#else
struct ieee80211_wmm_ts_info {
	u8 ac_dir_tid;
	u8 ac_up_psb;
	u8 reserved;
} __packed;

struct ieee80211_wmm_tspec_elem {
	struct ieee80211_wmm_ts_info ts_info;
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
	return ((u32)type >= ARRAY_SIZE(eap_types)) ? "Unknown" : eap_types[type];
}
//added by amy for reorder
static inline u8 Frame_QoSTID(u8 *buf)
{
	struct rtl_80211_hdr_3addr *hdr;
	u16 fc;
	hdr = (struct rtl_80211_hdr_3addr *)buf;
	fc = le16_to_cpu(hdr->frame_ctl);
	return (u8)((frameqos *)(buf + (((fc & IEEE80211_FCTL_TODS)&&(fc & IEEE80211_FCTL_FROMDS))? 30 : 24)))->field.tid;
}

//added by amy for reorder

struct eapol {
	u8 snap[6];
	u16 ethertype;
	u8 version;
	u8 type;
	u16 length;
} __packed;

struct ieee80211_softmac_stats{
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

struct ieee80211_info_element_hdr {
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

#define IEEE80211_DEFAULT_TX_ESSID "Penguin"
#define IEEE80211_DEFAULT_BASIC_RATE 2 //1Mbps

enum {WMM_all_frame, WMM_two_frame, WMM_four_frame, WMM_six_frame};
#define MAX_SP_Len  (WMM_all_frame << 4)
#define IEEE80211_QOS_TID 0x0f
#define QOS_CTL_NOTCONTAIN_ACK (0x01 << 5)

#define IEEE80211_DTIM_MBCAST 4
#define IEEE80211_DTIM_UCAST 2
#define IEEE80211_DTIM_VALID 1
#define IEEE80211_DTIM_INVALID 0

#define IEEE80211_PS_DISABLED 0
#define IEEE80211_PS_UNICAST IEEE80211_DTIM_UCAST
#define IEEE80211_PS_MBCAST IEEE80211_DTIM_MBCAST

//added by David for QoS 2006/6/30
//#define WMM_Hang_8187
#ifdef WMM_Hang_8187
#undef WMM_Hang_8187
#endif

#define WME_AC_BK   0x00
#define WME_AC_BE   0x01
#define WME_AC_VI   0x02
#define WME_AC_VO   0x03
#define WME_ACI_MASK 0x03
#define WME_AIFSN_MASK 0x03
#define WME_AC_PRAM_LEN 16

#define MAX_RECEIVE_BUFFER_SIZE 9100

//UP Mapping to AC, using in MgntQuery_SequenceNumber() and maybe for DSCP
//#define UP2AC(up)	((up<3) ? ((up==0)?1:0) : (up>>1))
#define UP2AC(up) (		   \
	((up) < 1) ? WME_AC_BE : \
	((up) < 3) ? WME_AC_BK : \
	((up) < 4) ? WME_AC_BE : \
	((up) < 6) ? WME_AC_VI : \
	WME_AC_VO)
//AC Mapping to UP, using in Tx part for selecting the corresponding TX queue
#define AC2UP(_ac)	(       \
	((_ac) == WME_AC_VO) ? 6 : \
	((_ac) == WME_AC_VI) ? 5 : \
	((_ac) == WME_AC_BK) ? 1 : \
	0)

#define	ETHER_ADDR_LEN		6	/* length of an Ethernet address */
#define ETHERNET_HEADER_SIZE    14      /* length of two Ethernet address plus ether type*/

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

typedef enum _erp_t{
	ERP_NonERPpresent	= 0x01,
	ERP_UseProtection	= 0x02,
	ERP_BarkerPreambleMode = 0x04,
} erp_t;


struct ieee80211_network {
	/* These entries are used to identify a unique network */
	u8 bssid[ETH_ALEN];
	u8 channel;
	/* Ensure null-terminated for any debug msgs */
	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;
	struct ieee80211_qos_data qos_data;

	//added by amy for LEAP
	bool	bWithAironetIE;
	bool	bCkipSupported;
	bool	bCcxRmEnable;
	u16	CcxRmState[2];
	// CCXv4 S59, MBSSID.
	bool	bMBssidValid;
	u8	MBssidMask;
	u8	MBssid[6];
	// CCX 2 S38, WLAN Device Version Number element. Annie, 2006-08-20.
	bool	bWithCcxVerNum;
	u8	BssCcxVerNumber;
	/* These are network statistics */
	struct ieee80211_rx_stats stats;
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

	struct ieee80211_tim_parameters tim;
	u8  dtim_period;
	u8  dtim_data;
	u32 last_dtim_sta_time[2];

	//appeded for QoS
	u8 wmm_info;
	struct ieee80211_wmm_ac_param wmm_param[4];
	u8 QoS_Enable;
#ifdef THOMAS_TURBO
	u8 Turbo_Enable;//enable turbo mode, added by thomas
#endif
	u16 CountryIeLen;
	u8 CountryIeBuf[MAX_IE_LEN];
	// HT Related, by amy, 2008.04.29
	BSS_HT	bssht;
	// Add to handle broadcom AP management frame CCK rate.
	bool broadcom_cap_exist;
	bool ralink_cap_exist;
	bool atheros_cap_exist;
	bool cisco_cap_exist;
	bool unknown_cap_exist;
//	u8	berp_info;
	bool	berp_info_valid;
	bool buseprotection;
	//put at the end of the structure.
	struct list_head list;
};

enum ieee80211_state {

	/* the card is not linked at all */
	IEEE80211_NOLINK = 0,

	/* IEEE80211_ASSOCIATING* are for BSS client mode
	 * the driver shall not perform RX filtering unless
	 * the state is LINKED.
	 * The driver shall just check for the state LINKED and
	 * defaults to NOLINK for ALL the other states (including
	 * LINKED_SCANNING)
	 */

	/* the association procedure will start (wq scheduling)*/
	IEEE80211_ASSOCIATING,
	IEEE80211_ASSOCIATING_RETRY,

	/* the association procedure is sending AUTH request*/
	IEEE80211_ASSOCIATING_AUTHENTICATING,

	/* the association procedure has successfully authentcated
	 * and is sending association request
	 */
	IEEE80211_ASSOCIATING_AUTHENTICATED,

	/* the link is ok. the card associated to a BSS or linked
	 * to a ibss cell or acting as an AP and creating the bss
	 */
	IEEE80211_LINKED,

	/* same as LINKED, but the driver shall apply RX filter
	 * rules as we are in NO_LINK mode. As the card is still
	 * logically linked, but it is doing a syncro site survey
	 * then it will be back to LINKED state.
	 */
	IEEE80211_LINKED_SCANNING,

};

#define DEFAULT_MAX_SCAN_AGE (15 * HZ)
#define DEFAULT_FTS 2346

#define CFG_IEEE80211_RESERVE_FCS (1<<0)
#define CFG_IEEE80211_COMPUTE_FCS (1<<1)
#define CFG_IEEE80211_RTS (1<<2)

#define IEEE80211_24GHZ_MIN_CHANNEL 1
#define IEEE80211_24GHZ_MAX_CHANNEL 14
#define IEEE80211_24GHZ_CHANNELS (IEEE80211_24GHZ_MAX_CHANNEL - \
				  IEEE80211_24GHZ_MIN_CHANNEL + 1)

#define IEEE80211_52GHZ_MIN_CHANNEL 34
#define IEEE80211_52GHZ_MAX_CHANNEL 165
#define IEEE80211_52GHZ_CHANNELS (IEEE80211_52GHZ_MAX_CHANNEL - \
				  IEEE80211_52GHZ_MIN_CHANNEL + 1)



typedef struct tx_pending_t{
	int frag;
	struct ieee80211_txb *txb;
}tx_pending_t;

typedef struct _bandwidth_autoswitch {
	long threshold_20Mhzto40Mhz;
	long	threshold_40Mhzto20Mhz;
	bool bforced_tx20Mhz;
	bool bautoswitch_enable;
} bandwidth_autoswitch, *pbandwidth_autoswitch;


//added by amy for order

#define REORDER_WIN_SIZE	128
#define REORDER_ENTRY_NUM	128
typedef struct _RX_REORDER_ENTRY {
	struct list_head	List;
	u16			SeqNum;
	struct ieee80211_rxb *prxb;
} RX_REORDER_ENTRY, *PRX_REORDER_ENTRY;
//added by amy for order
typedef enum _Fsync_State{
	Default_Fsync,
	HW_Fsync,
	SW_Fsync
}Fsync_State;

// Power save mode configured.
typedef	enum _RT_PS_MODE
{
	eActive,	// Active/Continuous access.
	eMaxPs,		// Max power save mode.
	eFastPs		// Fast power save mode.
}RT_PS_MODE;

typedef enum _IPS_CALLBACK_FUNCION
{
	IPS_CALLBACK_NONE = 0,
	IPS_CALLBACK_MGNT_LINK_REQUEST = 1,
	IPS_CALLBACK_JOIN_REQUEST = 2,
}IPS_CALLBACK_FUNCION;

typedef enum _RT_JOIN_ACTION{
	RT_JOIN_INFRA   = 1,
	RT_JOIN_IBSS  = 2,
	RT_START_IBSS = 3,
	RT_NO_ACTION  = 4,
}RT_JOIN_ACTION;

typedef struct _IbssParms{
	u16   atimWin;
}IbssParms, *PIbssParms;
#define MAX_NUM_RATES	264 // Max num of support rates element: 8,  Max num of ext. support rate: 255. 061122, by rcnjko.

// RF state.
typedef	enum _RT_RF_POWER_STATE {
	eRfOn,
	eRfSleep,
	eRfOff
}RT_RF_POWER_STATE;

typedef struct _RT_POWER_SAVE_CONTROL {

	//
	// Inactive Power Save(IPS) : Disable RF when disconnected
	//
	bool				bInactivePs;
	bool				bIPSModeBackup;
	bool				bSwRfProcessing;
	RT_RF_POWER_STATE	eInactivePowerState;
	struct work_struct	InactivePsWorkItem;
	struct timer_list	InactivePsTimer;

	// Return point for join action
	IPS_CALLBACK_FUNCION	ReturnPoint;

	// Recored Parameters for rescheduled JoinRequest
	bool				bTmpBssDesc;
	RT_JOIN_ACTION		tmpJoinAction;
	struct ieee80211_network tmpBssDesc;

	// Recored Parameters for rescheduled MgntLinkRequest
	bool				bTmpScanOnly;
	bool				bTmpActiveScan;
	bool				bTmpFilterHiddenAP;
	bool				bTmpUpdateParms;
	u8					tmpSsidBuf[33];
	OCTET_STRING			tmpSsid2Scan;
	bool				bTmpSsid2Scan;
	u8					tmpNetworkType;
	u8					tmpChannelNumber;
	u16					tmpBcnPeriod;
	u8					tmpDtimPeriod;
	u16					tmpmCap;
	OCTET_STRING			tmpSuppRateSet;
	u8					tmpSuppRateBuf[MAX_NUM_RATES];
	bool				bTmpSuppRate;
	IbssParms				tmpIbpm;
	bool				bTmpIbpm;

	//
	// Leisre Poswer Save : Disable RF if connected but traffic is not busy
	//
	bool				bLeisurePs;

} RT_POWER_SAVE_CONTROL, *PRT_POWER_SAVE_CONTROL;

typedef u32 RT_RF_CHANGE_SOURCE;
#define RF_CHANGE_BY_SW BIT31
#define RF_CHANGE_BY_HW BIT30
#define RF_CHANGE_BY_PS BIT29
#define RF_CHANGE_BY_IPS BIT28
#define RF_CHANGE_BY_INIT	0	// Do not change the RFOff reason. Defined by Bruce, 2008-01-17.

typedef enum
{
	COUNTRY_CODE_FCC = 0,
	COUNTRY_CODE_IC = 1,
	COUNTRY_CODE_ETSI = 2,
	COUNTRY_CODE_SPAIN = 3,
	COUNTRY_CODE_FRANCE = 4,
	COUNTRY_CODE_MKK = 5,
	COUNTRY_CODE_MKK1 = 6,
	COUNTRY_CODE_ISRAEL = 7,
	COUNTRY_CODE_TELEC,
	COUNTRY_CODE_MIC,
	COUNTRY_CODE_GLOBAL_DOMAIN
}country_code_type_t;

#define RT_MAX_LD_SLOT_NUM	10
typedef struct _RT_LINK_DETECT_T{

	u32				NumRecvBcnInPeriod;
	u32				NumRecvDataInPeriod;

	u32				RxBcnNum[RT_MAX_LD_SLOT_NUM];	// number of Rx beacon / CheckForHang_period  to determine link status
	u32				RxDataNum[RT_MAX_LD_SLOT_NUM];	// number of Rx data / CheckForHang_period  to determine link status
	u16				SlotNum;	// number of CheckForHang period to determine link status
	u16				SlotIndex;

	u32				NumTxOkInPeriod;
	u32				NumRxOkInPeriod;
	bool				bBusyTraffic;
}RT_LINK_DETECT_T, *PRT_LINK_DETECT_T;


struct ieee80211_device {
	struct net_device *dev;
	struct ieee80211_security sec;

	//hw security related
//	u8 hwsec_support; //support?
	u8 hwsec_active;  //hw security active.
	bool is_silent_reset;
	bool ieee_up;
	//added by amy
	bool bSupportRemoteWakeUp;
	RT_PS_MODE	dot11PowerSaveMode; // Power save mode configured.
	bool actscanning;
	bool beinretry;
	RT_RF_POWER_STATE		eRFPowerState;
	RT_RF_CHANGE_SOURCE	RfOffReason;
	bool is_set_key;
	//11n spec related I wonder if These info structure need to be moved out of ieee80211_device

	//11n HT below
	PRT_HIGH_THROUGHPUT	pHTInfo;
	//struct timer_list		SwBwTimer;
//	spinlock_t chnlop_spinlock;
	spinlock_t bw_spinlock;

	spinlock_t reorder_spinlock;
	// for HT operation rate set.  we use this one for HT data rate to separate different descriptors
	//the way fill this is the same as in the IE
	u8	Regdot11HTOperationalRateSet[16];		//use RATR format
	u8	dot11HTOperationalRateSet[16];		//use RATR format
	u8	RegHTSuppRateSet[16];
	u8				HTCurrentOperaRate;
	u8				HTHighestOperaRate;
	//wb added for rate operation mode to firmware
	u8	bTxDisableRateFallBack;
	u8	bTxUseDriverAssingedRate;
	atomic_t	atm_chnlop;
	atomic_t	atm_swbw;
//	u8	HTHighestOperaRate;
//	u8	HTCurrentOperaRate;

	// 802.11e and WMM Traffic Stream Info (TX)
	struct list_head		Tx_TS_Admit_List;
	struct list_head		Tx_TS_Pending_List;
	struct list_head		Tx_TS_Unused_List;
	TX_TS_RECORD		TxTsRecord[TOTAL_TS_NUM];
	// 802.11e and WMM Traffic Stream Info (RX)
	struct list_head		Rx_TS_Admit_List;
	struct list_head		Rx_TS_Pending_List;
	struct list_head		Rx_TS_Unused_List;
	RX_TS_RECORD		RxTsRecord[TOTAL_TS_NUM];
//#ifdef TO_DO_LIST
	RX_REORDER_ENTRY	RxReorderEntry[128];
	struct list_head		RxReorder_Unused_List;
//#endif
	// Qos related. Added by Annie, 2005-11-01.
//	PSTA_QOS			pStaQos;
	u8				ForcedPriority;		// Force per-packet priority 1~7. (default: 0, not to force it.)


	/* Bookkeeping structures */
	struct net_device_stats stats;
	struct ieee80211_stats ieee_stats;
	struct ieee80211_softmac_stats softmac_stats;

	/* Probe / Beacon management */
	struct list_head network_free_list;
	struct list_head network_list;
	struct ieee80211_network *networks;
	int scans;
	int scan_age;

	int iw_mode; /* operating mode (IW_MODE_*) */
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
	bool bHalfWirelessN24GMode;
	int wpa_enabled;
	int drop_unencrypted;
	int tkip_countermeasures;
	int privacy_invoked;
	size_t wpa_ie_len;
	u8 *wpa_ie;
	u8 ap_mac_addr[6];
	u16 pairwise_key_type;
	u16 group_key_type;
	struct list_head crypt_deinit_list;
	struct ieee80211_crypt_data *crypt[WEP_KEYS];
	int tx_keyidx; /* default TX key index (crypt[tx_keyidx]) */
	struct timer_list crypt_deinit_timer;
	int crypt_quiesced;

	int bcrx_sta_key; /* use individual keys to override default keys even
			   * with RX of broad/multicast frames */

	/* Fragmentation structures */
	// each streaming contain a entry
	struct ieee80211_frag_entry frag_cache[17][IEEE80211_FRAG_CACHE_LEN];
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
	struct ieee80211_network current_network;

	enum ieee80211_state state;

	int short_slot;
	int reg_mode;
	int mode;       /* A, B, G */
	int modulation; /* CCK, OFDM */
	int freq_band;  /* 2.4Ghz, 5.2Ghz, Mixed */
	int abg_true;   /* ABG flag              */

	/* used for forcing the ibss workqueue to terminate
	 * without wait for the syncro scan to terminate
	 */
	short sync_scan_hurryup;

	int perfect_rssi;
	int worst_rssi;

	u16 prev_seq_ctl;       /* used to drop duplicate frames */

	/* map of allowed channels. 0 is dummy */
	// FIXME: remember to default to a basic channel plan depending of the PHY type
	void *pDot11dInfo;
	bool bGlobalDomain;
	int rate;       /* current rate */
	int basic_rate;
	//FIXME: pleace callback, see if redundant with softmac_features
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
	short ps;
	short sta_sleep;
	int ps_timeout;
	int ps_period;
	struct tasklet_struct ps_task;
	u32 ps_th;
	u32 ps_tl;

	short raw_tx;
	/* used if IEEE_SOFTMAC_TX_QUEUE is set */
	short queue_stop;
	short scanning;
	short proto_started;

	struct semaphore wx_sem;
	struct semaphore scan_sem;

	spinlock_t mgmt_tx_lock;
	spinlock_t beacon_lock;

	short beacon_txing;

	short wap_set;
	short ssid_set;

	u8  wpax_type_set;    //{added by David, 2006.9.28}
	u32 wpax_type_notify; //{added by David, 2006.9.26}

	/* QoS related flag */
	char init_wmmparam_flag;
	/* set on initialization */
	u8  qos_support;

	/* for discarding duplicated packets in IBSS */
	struct list_head ibss_mac_hash[IEEE_IBSS_MAC_HASH_SIZE];

	/* for discarding duplicated packets in BSS */
	u16 last_rxseq_num[17]; /* rx seq previous per-tid */
	u16 last_rxfrag_num[17];/* tx frag previous per-tid */
	unsigned long last_packet_time[17];

	/* for PS mode */
	unsigned long last_rx_ps_time;

	/* used if IEEE_SOFTMAC_SINGLE_QUEUE is set */
	struct sk_buff *mgmt_queue_ring[MGMT_QUEUE_NUM];
	int mgmt_queue_head;
	int mgmt_queue_tail;
//{ added for rtl819x
#define IEEE80211_QUEUE_LIMIT 128
	u8 AsocRetryCount;
	unsigned int hw_header;
	struct sk_buff_head skb_waitQ[MAX_QUEUE_SIZE];
	struct sk_buff_head  skb_aggQ[MAX_QUEUE_SIZE];
	struct sk_buff_head  skb_drv_aggQ[MAX_QUEUE_SIZE];
	u32	sta_edca_param[4];
	bool aggregation;
	// Enable/Disable Rx immediate BA capability.
	bool enable_rx_imm_BA;
	bool bibsscoordinator;

	//+by amy for DM ,080515
	//Dynamic Tx power for near/far range enable/Disable  , by amy , 2008-05-15
	bool	bdynamic_txpower_enable;

	bool bCTSToSelfEnable;
	u8	CTSToSelfTH;

	u32	fsync_time_interval;
	u32	fsync_rate_bitmap;
	u8	fsync_rssi_threshold;
	bool	bfsync_enable;

	u8	fsync_multiple_timeinterval;		// FsyncMultipleTimeInterval * FsyncTimeInterval
	u32	fsync_firstdiff_ratethreshold;		// low threshold
	u32	fsync_seconddiff_ratethreshold;	 // decrease threshold
	Fsync_State			fsync_state;
	bool		bis_any_nonbepkts;
	//20Mhz 40Mhz AutoSwitch Threshold
	bandwidth_autoswitch bandwidth_auto_switch;
	//for txpower tracking
	bool FwRWRF;

	//added by amy for AP roaming
	RT_LINK_DETECT_T	LinkDetectInfo;
	//added by amy for ps
	RT_POWER_SAVE_CONTROL	PowerSaveControl;
//}
	/* used if IEEE_SOFTMAC_TX_QUEUE is set */
	struct  tx_pending_t tx_pending;

	/* used if IEEE_SOFTMAC_ASSOCIATE is set */
	struct timer_list associate_timer;

	/* used if IEEE_SOFTMAC_BEACONS is set */
	struct timer_list beacon_timer;
	struct work_struct associate_complete_wq;
	struct work_struct associate_procedure_wq;
	struct delayed_work softmac_scan_wq;
	struct delayed_work associate_retry_wq;
	 struct delayed_work start_ibss_wq;
	struct work_struct wx_sync_scan_wq;
	struct workqueue_struct *wq;
	// Qos related. Added by Annie, 2005-11-01.
	//STA_QOS  StaQos;

	//u32 STA_EDCA_PARAM[4];
	//CHANNEL_ACCESS_SETTING ChannelAccessSetting;


	/* Callback functions */
	void (*set_security)(struct net_device *dev,
			     struct ieee80211_security *sec);

	/* Used to TX data frame by using txb structs.
	 * this is not used if in the softmac_features
	 * is set the flag IEEE_SOFTMAC_TX_QUEUE
	 */
	int (*hard_start_xmit)(struct ieee80211_txb *txb,
			       struct net_device *dev);

	int (*reset_port)(struct net_device *dev);
	int (*is_queue_full) (struct net_device *dev, int pri);

	int (*handle_management) (struct net_device *dev,
				  struct ieee80211_network *network, u16 type);
	int (*is_qos_active) (struct net_device *dev, struct sk_buff *skb);

	/* Softmac-generated frames (management) are TXed via this
	 * callback if the flag IEEE_SOFTMAC_SINGLE_QUEUE is
	 * not set. As some cards may have different HW queues that
	 * one might want to use for data and management frames
	 * the option to have two callbacks might be useful.
	 * This function can't sleep.
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
	 * the function stop_scan should stop both the syncro and
	 * background scanning and can sleep.
	 * The function start_scan should initiate the background
	 * scanning and can't sleep.
	 */
	void (*scan_syncro)(struct net_device *dev);
	void (*start_scan)(struct net_device *dev);
	void (*stop_scan)(struct net_device *dev);

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
	void (*start_send_beacons) (struct net_device *dev,u16 tx_rate);
	void (*stop_send_beacons) (struct net_device *dev);

	/* power save mode related */
	void (*sta_wake_up) (struct net_device *dev);
	void (*ps_request_tx_ack) (struct net_device *dev);
	void (*enter_sleep_state) (struct net_device *dev, u32 th, u32 tl);
	short (*ps_is_queue_empty) (struct net_device *dev);
	int (*handle_beacon) (struct net_device *dev, struct ieee80211_beacon *beacon, struct ieee80211_network *network);
	int (*handle_assoc_response) (struct net_device *dev, struct ieee80211_assoc_response_frame *resp, struct ieee80211_network *network);


	/* check whether Tx hw resource available */
	short (*check_nic_enough_desc)(struct net_device *dev, int queue_index);
	//added by wb for HT related
//	void (*SwChnlByTimerHandler)(struct net_device *dev, int channel);
	void (*SetBWModeHandler)(struct net_device *dev, HT_CHANNEL_WIDTH Bandwidth, HT_EXTCHNL_OFFSET Offset);
//	void (*UpdateHalRATRTableHandler)(struct net_device* dev, u8* pMcsRate);
	bool (*GetNmodeSupportBySecCfg)(struct net_device *dev);
	void (*SetWirelessMode)(struct net_device *dev, u8 wireless_mode);
	bool (*GetHalfNmodeSupportByAPsHandler)(struct net_device *dev);
	void (*InitialGainHandler)(struct net_device *dev, u8 Operation);

	/* This must be the last item so that it points to the data
	 * allocated beyond this structure by alloc_ieee80211 */
	u8 priv[0];
};

#define IEEE_A            (1<<0)
#define IEEE_B            (1<<1)
#define IEEE_G            (1<<2)
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

static inline void *ieee80211_priv(struct net_device *dev)
{
	return ((struct ieee80211_device *)netdev_priv(dev))->priv;
}

static inline int ieee80211_is_empty_essid(const char *essid, int essid_len)
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

static inline int ieee80211_is_valid_mode(struct ieee80211_device *ieee, int mode)
{
	/*
	 * It is possible for both access points and our device to support
	 * combinations of modes, so as long as there is one valid combination
	 * of ap/device supported modes, then return success
	 *
	 */
	if ((mode & IEEE_A) &&
	    (ieee->modulation & IEEE80211_OFDM_MODULATION) &&
	    (ieee->freq_band & IEEE80211_52GHZ_BAND))
		return 1;

	if ((mode & IEEE_G) &&
	    (ieee->modulation & IEEE80211_OFDM_MODULATION) &&
	    (ieee->freq_band & IEEE80211_24GHZ_BAND))
		return 1;

	if ((mode & IEEE_B) &&
	    (ieee->modulation & IEEE80211_CCK_MODULATION) &&
	    (ieee->freq_band & IEEE80211_24GHZ_BAND))
		return 1;

	return 0;
}

static inline int ieee80211_get_hdrlen(u16 fc)
{
	int hdrlen = IEEE80211_3ADDR_LEN;

	switch (WLAN_FC_GET_TYPE(fc)) {
	case IEEE80211_FTYPE_DATA:
		if ((fc & IEEE80211_FCTL_FROMDS) && (fc & IEEE80211_FCTL_TODS))
			hdrlen = IEEE80211_4ADDR_LEN; /* Addr4 */
		if(IEEE80211_QOS_HAS_SEQ(fc))
			hdrlen += 2; /* QOS ctrl*/
		break;
	case IEEE80211_FTYPE_CTL:
		switch (WLAN_FC_GET_STYPE(fc)) {
		case IEEE80211_STYPE_CTS:
		case IEEE80211_STYPE_ACK:
			hdrlen = IEEE80211_1ADDR_LEN;
			break;
		default:
			hdrlen = IEEE80211_2ADDR_LEN;
			break;
		}
		break;
	}

	return hdrlen;
}

static inline u8 *ieee80211_get_payload(struct rtl_80211_hdr *hdr)
{
	switch (ieee80211_get_hdrlen(le16_to_cpu(hdr->frame_ctl))) {
	case IEEE80211_1ADDR_LEN:
		return ((struct rtl_80211_hdr_1addr *)hdr)->payload;
	case IEEE80211_2ADDR_LEN:
		return ((struct rtl_80211_hdr_2addr *)hdr)->payload;
	case IEEE80211_3ADDR_LEN:
		return ((struct rtl_80211_hdr_3addr *)hdr)->payload;
	case IEEE80211_4ADDR_LEN:
		return ((struct rtl_80211_hdr_4addr *)hdr)->payload;
	}
	return NULL;
}

static inline int ieee80211_is_ofdm_rate(u8 rate)
{
	switch (rate & ~IEEE80211_BASIC_RATE_MASK) {
	case IEEE80211_OFDM_RATE_6MB:
	case IEEE80211_OFDM_RATE_9MB:
	case IEEE80211_OFDM_RATE_12MB:
	case IEEE80211_OFDM_RATE_18MB:
	case IEEE80211_OFDM_RATE_24MB:
	case IEEE80211_OFDM_RATE_36MB:
	case IEEE80211_OFDM_RATE_48MB:
	case IEEE80211_OFDM_RATE_54MB:
		return 1;
	}
	return 0;
}

static inline int ieee80211_is_cck_rate(u8 rate)
{
	switch (rate & ~IEEE80211_BASIC_RATE_MASK) {
	case IEEE80211_CCK_RATE_1MB:
	case IEEE80211_CCK_RATE_2MB:
	case IEEE80211_CCK_RATE_5MB:
	case IEEE80211_CCK_RATE_11MB:
		return 1;
	}
	return 0;
}


/* ieee80211.c */
void free_ieee80211(struct net_device *dev);
struct net_device *alloc_ieee80211(int sizeof_priv);

int ieee80211_set_encryption(struct ieee80211_device *ieee);

/* ieee80211_tx.c */

int ieee80211_encrypt_fragment(struct ieee80211_device *ieee,
			       struct sk_buff *frag, int hdr_len);

int ieee80211_xmit(struct sk_buff *skb, struct net_device *dev);
void ieee80211_txb_free(struct ieee80211_txb *);


/* ieee80211_rx.c */
int ieee80211_rx(struct ieee80211_device *ieee, struct sk_buff *skb,
		 struct ieee80211_rx_stats *rx_stats);
void ieee80211_rx_mgt(struct ieee80211_device *ieee,
		      struct rtl_80211_hdr_4addr *header,
		      struct ieee80211_rx_stats *stats);

/* ieee80211_wx.c */
int ieee80211_wx_get_scan(struct ieee80211_device *ieee,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *key);
int ieee80211_wx_set_encode(struct ieee80211_device *ieee,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *key);
int ieee80211_wx_get_encode(struct ieee80211_device *ieee,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *key);
int ieee80211_wx_get_encode_ext(struct ieee80211_device *ieee,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra);
int ieee80211_wx_set_encode_ext(struct ieee80211_device *ieee,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra);
int ieee80211_wx_set_auth(struct ieee80211_device *ieee,
			  struct iw_request_info *info,
			  struct iw_param *data, char *extra);
int ieee80211_wx_set_mlme(struct ieee80211_device *ieee,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);
int ieee80211_wx_set_gen_ie(struct ieee80211_device *ieee, u8 *ie, size_t len);

/* ieee80211_softmac.c */
short ieee80211_is_54g(const struct ieee80211_network *net);
short ieee80211_is_shortslot(const struct ieee80211_network *net);
int ieee80211_rx_frame_softmac(struct ieee80211_device *ieee,
			       struct sk_buff *skb,
			       struct ieee80211_rx_stats *rx_stats,
			       u16 type, u16 stype);
void ieee80211_softmac_new_net(struct ieee80211_device *ieee,
			       struct ieee80211_network *net);

void SendDisassociation(struct ieee80211_device *ieee, u8 *asSta, u8 asRsn);
void ieee80211_softmac_xmit(struct ieee80211_txb *txb,
			    struct ieee80211_device *ieee);

void ieee80211_stop_send_beacons(struct ieee80211_device *ieee);
void notify_wx_assoc_event(struct ieee80211_device *ieee);
void ieee80211_softmac_check_all_nets(struct ieee80211_device *ieee);
void ieee80211_start_bss(struct ieee80211_device *ieee);
void ieee80211_start_master_bss(struct ieee80211_device *ieee);
void ieee80211_start_ibss(struct ieee80211_device *ieee);
void ieee80211_softmac_init(struct ieee80211_device *ieee);
void ieee80211_softmac_free(struct ieee80211_device *ieee);
void ieee80211_associate_abort(struct ieee80211_device *ieee);
void ieee80211_disassociate(struct ieee80211_device *ieee);
void ieee80211_stop_scan(struct ieee80211_device *ieee);
void ieee80211_start_scan_syncro(struct ieee80211_device *ieee);
void ieee80211_check_all_nets(struct ieee80211_device *ieee);
void ieee80211_start_protocol(struct ieee80211_device *ieee);
void ieee80211_stop_protocol(struct ieee80211_device *ieee);
void ieee80211_softmac_start_protocol(struct ieee80211_device *ieee);
void ieee80211_softmac_stop_protocol(struct ieee80211_device *ieee);
void ieee80211_reset_queue(struct ieee80211_device *ieee);
void ieee80211_wake_queue(struct ieee80211_device *ieee);
void ieee80211_stop_queue(struct ieee80211_device *ieee);
struct sk_buff *ieee80211_get_beacon(struct ieee80211_device *ieee);
void ieee80211_start_send_beacons(struct ieee80211_device *ieee);
void ieee80211_stop_send_beacons(struct ieee80211_device *ieee);
int ieee80211_wpa_supplicant_ioctl(struct ieee80211_device *ieee,
				   struct iw_point *p);
void notify_wx_assoc_event(struct ieee80211_device *ieee);
void ieee80211_ps_tx_ack(struct ieee80211_device *ieee, short success);

void softmac_mgmt_xmit(struct sk_buff *skb, struct ieee80211_device *ieee);

/* ieee80211_crypt_ccmp&tkip&wep.c */
void ieee80211_tkip_null(void);
void ieee80211_wep_null(void);
void ieee80211_ccmp_null(void);

int ieee80211_crypto_init(void);
void ieee80211_crypto_deinit(void);
int ieee80211_crypto_tkip_init(void);
void ieee80211_crypto_tkip_exit(void);
int ieee80211_crypto_ccmp_init(void);
void ieee80211_crypto_ccmp_exit(void);
int ieee80211_crypto_wep_init(void);
void ieee80211_crypto_wep_exit(void);

/* ieee80211_softmac_wx.c */

int ieee80211_wx_get_wap(struct ieee80211_device *ieee,
			 struct iw_request_info *info,
			 union iwreq_data *wrqu, char *ext);

int ieee80211_wx_set_wap(struct ieee80211_device *ieee,
			 struct iw_request_info *info,
			 union iwreq_data *awrq,
			 char *extra);

int ieee80211_wx_get_essid(struct ieee80211_device *ieee,
			   struct iw_request_info *a,
			   union iwreq_data *wrqu, char *b);

int ieee80211_wx_set_rate(struct ieee80211_device *ieee,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);

int ieee80211_wx_get_rate(struct ieee80211_device *ieee,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);

int ieee80211_wx_set_mode(struct ieee80211_device *ieee,
			  struct iw_request_info *a,
			  union iwreq_data *wrqu, char *b);

int ieee80211_wx_set_scan(struct ieee80211_device *ieee,
			  struct iw_request_info *a,
			  union iwreq_data *wrqu, char *b);

int ieee80211_wx_set_essid(struct ieee80211_device *ieee,
			   struct iw_request_info *a,
			   union iwreq_data *wrqu, char *extra);

int ieee80211_wx_get_mode(struct ieee80211_device *ieee,
			  struct iw_request_info *a,
			  union iwreq_data *wrqu, char *b);

int ieee80211_wx_set_freq(struct ieee80211_device *ieee,
			  struct iw_request_info *a,
			  union iwreq_data *wrqu, char *b);

int ieee80211_wx_get_freq(struct ieee80211_device *ieee,
			  struct iw_request_info *a,
			  union iwreq_data *wrqu, char *b);

/* ieee80211_module.c */
int ieee80211_debug_init(void);
void ieee80211_debug_exit(void);

//extern void ieee80211_wx_sync_scan_wq(struct ieee80211_device *ieee);
void ieee80211_wx_sync_scan_wq(struct work_struct *work);


int ieee80211_wx_set_rawtx(struct ieee80211_device *ieee,
			   struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra);

int ieee80211_wx_get_name(struct ieee80211_device *ieee,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);

int ieee80211_wx_set_power(struct ieee80211_device *ieee,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra);

int ieee80211_wx_get_power(struct ieee80211_device *ieee,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra);

int ieee80211_wx_set_rts(struct ieee80211_device *ieee,
			 struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra);

int ieee80211_wx_get_rts(struct ieee80211_device *ieee,
			 struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra);
//HT
#define MAX_RECEIVE_BUFFER_SIZE 9100  //
void HTDebugHTCapability(u8 *CapIE, u8 *TitleString);
void HTDebugHTInfo(u8 *InfoIE, u8 *TitleString);

void HTSetConnectBwMode(struct ieee80211_device *ieee,
			HT_CHANNEL_WIDTH Bandwidth, HT_EXTCHNL_OFFSET Offset);
void HTUpdateDefaultSetting(struct ieee80211_device *ieee);
void HTConstructCapabilityElement(struct ieee80211_device *ieee, u8 *posHTCap,
				  u8 *len, u8 isEncrypt);
void HTConstructInfoElement(struct ieee80211_device *ieee, u8 *posHTInfo,
			    u8 *len, u8 isEncrypt);
void HTConstructRT2RTAggElement(struct ieee80211_device *ieee, u8 *posRT2RTAgg,
				u8 *len);
void HTOnAssocRsp(struct ieee80211_device *ieee);
void HTInitializeHTInfo(struct ieee80211_device *ieee);
void HTInitializeBssDesc(PBSS_HT pBssHT);
void HTResetSelfAndSavePeerSetting(struct ieee80211_device *ieee,
				   struct ieee80211_network *pNetwork);
void HTUpdateSelfAndPeerSetting(struct ieee80211_device *ieee,
				struct ieee80211_network *pNetwork);
u8 HTGetHighestMCSRate(struct ieee80211_device *ieee,
		       u8 *pMCSRateSet, u8 *pMCSFilter);
extern u8 MCS_FILTER_ALL[];
extern u16 MCS_DATA_RATE[2][2][77] ;
u8 HTCCheck(struct ieee80211_device *ieee, u8 *pFrame);
//extern void HTSetConnectBwModeCallback(unsigned long data);
void HTResetIOTSetting(PRT_HIGH_THROUGHPUT pHTInfo);
bool IsHTHalfNmodeAPs(struct ieee80211_device *ieee);
u16 HTHalfMcsToDataRate(struct ieee80211_device *ieee, u8 nMcsRate);
u16 HTMcsToDataRate(struct ieee80211_device *ieee, u8 nMcsRate);
u16 TxCountToDataRate(struct ieee80211_device *ieee, u8 nDataRate);
//function in BAPROC.c
int ieee80211_rx_ADDBAReq(struct ieee80211_device *ieee, struct sk_buff *skb);
int ieee80211_rx_ADDBARsp(struct ieee80211_device *ieee, struct sk_buff *skb);
int ieee80211_rx_DELBA(struct ieee80211_device *ieee, struct sk_buff *skb);
void TsInitAddBA(struct ieee80211_device *ieee, PTX_TS_RECORD pTS,
		 u8 Policy, u8 bOverwritePending);
void TsInitDelBA(struct ieee80211_device *ieee,
		 PTS_COMMON_INFO pTsCommonInfo, TR_SELECT TxRxSelect);
void BaSetupTimeOut(unsigned long data);
void TxBaInactTimeout(unsigned long data);
void RxBaInactTimeout(unsigned long data);
void ResetBaEntry(PBA_RECORD pBA);
//function in TS.c
bool GetTs(
	struct ieee80211_device		*ieee,
	PTS_COMMON_INFO                 *ppTS,
	u8                              *Addr,
	u8                              TID,
	TR_SELECT                       TxRxSelect,  //Rx:1, Tx:0
	bool                            bAddNewTs
	);
void TSInitialize(struct ieee80211_device *ieee);
void TsStartAddBaProcess(struct ieee80211_device *ieee, PTX_TS_RECORD   pTxTS);
void RemovePeerTS(struct ieee80211_device *ieee, u8 *Addr);
void RemoveAllTS(struct ieee80211_device *ieee);
void ieee80211_softmac_scan_syncro(struct ieee80211_device *ieee);

extern const long ieee80211_wlan_frequencies[];

static inline void ieee80211_increment_scans(struct ieee80211_device *ieee)
{
	ieee->scans++;
}

static inline int ieee80211_get_scans(struct ieee80211_device *ieee)
{
	return ieee->scans;
}

static inline const char *escape_essid(const char *essid, u8 essid_len) {
	static char escaped[IW_ESSID_MAX_SIZE * 2 + 1];

	if (ieee80211_is_empty_essid(essid, essid_len)) {
		memcpy(escaped, "<hidden>", sizeof("<hidden>"));
		return escaped;
	}

	snprintf(escaped, sizeof(escaped), "%*pEn", essid_len, essid);
	return escaped;
}

/* For the function is more related to hardware setting, it's better to use the
 * ieee handler to refer to it.
 */
short check_nic_enough_desc(struct net_device *dev, int queue_index);
int ieee80211_data_xmit(struct sk_buff *skb, struct net_device *dev);
int ieee80211_parse_info_param(struct ieee80211_device *ieee,
			       struct ieee80211_info_element *info_element,
			       u16 length,
			       struct ieee80211_network *network,
			       struct ieee80211_rx_stats *stats);

void ieee80211_indicate_packets(struct ieee80211_device *ieee,
				struct ieee80211_rxb **prxbIndicateArray,
				u8 index);
#define RT_ASOC_RETRY_LIMIT	5
#endif /* IEEE80211_H */
