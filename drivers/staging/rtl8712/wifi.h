/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef _WIFI_H_
#define _WIFI_H_

#include <linux/compiler.h>
#include <linux/ieee80211.h>

#define WLAN_HDR_A3_LEN		24
#define WLAN_HDR_A3_QOS_LEN	26

#define P80211CAPTURE_VERSION	0x80211001

enum WIFI_FRAME_TYPE {
	WIFI_MGT_TYPE  =	(0),
	WIFI_CTRL_TYPE =	(BIT(2)),
	WIFI_DATA_TYPE =	(BIT(3)),
	WIFI_QOS_DATA_TYPE	= (BIT(7)|BIT(3)),	/*!< QoS Data */
};

enum WIFI_FRAME_SUBTYPE {
	/* below is for mgt frame */
	WIFI_ASSOCREQ       = (0 | WIFI_MGT_TYPE),
	WIFI_ASSOCRSP       = (BIT(4) | WIFI_MGT_TYPE),
	WIFI_REASSOCREQ     = (BIT(5) | WIFI_MGT_TYPE),
	WIFI_REASSOCRSP     = (BIT(5) | BIT(4) | WIFI_MGT_TYPE),
	WIFI_PROBEREQ       = (BIT(6) | WIFI_MGT_TYPE),
	WIFI_PROBERSP       = (BIT(6) | BIT(4) | WIFI_MGT_TYPE),
	WIFI_BEACON         = (BIT(7) | WIFI_MGT_TYPE),
	WIFI_ATIM           = (BIT(7) | BIT(4) | WIFI_MGT_TYPE),
	WIFI_DISASSOC       = (BIT(7) | BIT(5) | WIFI_MGT_TYPE),
	WIFI_AUTH           = (BIT(7) | BIT(5) | BIT(4) | WIFI_MGT_TYPE),
	WIFI_DEAUTH         = (BIT(7) | BIT(6) | WIFI_MGT_TYPE),
	WIFI_ACTION         = (BIT(7) | BIT(6) | BIT(4) | WIFI_MGT_TYPE),
	/* below is for control frame */
	WIFI_PSPOLL         = (BIT(7) | BIT(5) | WIFI_CTRL_TYPE),
	WIFI_RTS            = (BIT(7) | BIT(5) | BIT(4) | WIFI_CTRL_TYPE),
	WIFI_CTS            = (BIT(7) | BIT(6) | WIFI_CTRL_TYPE),
	WIFI_ACK            = (BIT(7) | BIT(6) | BIT(4) | WIFI_CTRL_TYPE),
	WIFI_CFEND          = (BIT(7) | BIT(6) | BIT(5) | WIFI_CTRL_TYPE),
	WIFI_CFEND_CFACK = (BIT(7) | BIT(6) | BIT(5) | BIT(4) | WIFI_CTRL_TYPE),
	/* below is for data frame */
	WIFI_DATA           = (0 | WIFI_DATA_TYPE),
	WIFI_DATA_CFACK     = (BIT(4) | WIFI_DATA_TYPE),
	WIFI_DATA_CFPOLL    = (BIT(5) | WIFI_DATA_TYPE),
	WIFI_DATA_CFACKPOLL = (BIT(5) | BIT(4) | WIFI_DATA_TYPE),
	WIFI_DATA_NULL      = (BIT(6) | WIFI_DATA_TYPE),
	WIFI_CF_ACK         = (BIT(6) | BIT(4) | WIFI_DATA_TYPE),
	WIFI_CF_POLL        = (BIT(6) | BIT(5) | WIFI_DATA_TYPE),
	WIFI_CF_ACKPOLL     = (BIT(6) | BIT(5) | BIT(4) | WIFI_DATA_TYPE),
};

enum WIFI_REG_DOMAIN {
	DOMAIN_FCC	= 1,
	DOMAIN_IC	= 2,
	DOMAIN_ETSI	= 3,
	DOMAIN_SPAIN	= 4,
	DOMAIN_FRANCE	= 5,
	DOMAIN_MKK	= 6,
	DOMAIN_ISRAEL	= 7,
	DOMAIN_MKK1	= 8,
	DOMAIN_MKK2	= 9,
	DOMAIN_MKK3	= 10,
	DOMAIN_MAX
};

#define SetToDs(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(IEEE80211_FCTL_TODS); \
})

#define GetToDs(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(IEEE80211_FCTL_TODS)) != 0)

#define ClearToDs(pbuf)	({ \
	*(__le16 *)(pbuf) &= (~cpu_to_le16(IEEE80211_FCTL_TODS)); \
})

#define SetFrDs(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(IEEE80211_FCTL_FROMDS); \
})

#define GetFrDs(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(IEEE80211_FCTL_FROMDS)) != 0)

#define ClearFrDs(pbuf)	({ \
	*(__le16 *)(pbuf) &= (~cpu_to_le16(IEEE80211_FCTL_FROMDS)); \
})

static inline unsigned char get_tofr_ds(unsigned char *pframe)
{
	return ((GetToDs(pframe) << 1) | GetFrDs(pframe));
}

#define SetMFrag(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(IEEE80211_FCTL_MOREFRAGS); \
})

#define GetMFrag(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(IEEE80211_FCTL_MOREFRAGS)) != 0)

#define ClearMFrag(pbuf) ({ \
	*(__le16 *)(pbuf) &= (~cpu_to_le16(IEEE80211_FCTL_MOREFRAGS)); \
})

#define SetRetry(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(IEEE80211_FCTL_RETRY); \
})

#define GetRetry(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(IEEE80211_FCTL_RETRY)) != 0)

#define ClearRetry(pbuf) ({ \
	*(__le16 *)(pbuf) &= (~cpu_to_le16(IEEE80211_FCTL_RETRY)); \
})

#define SetPwrMgt(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(IEEE80211_FCTL_PM); \
})

#define GetPwrMgt(pbuf)	(((*(__le16 *)(pbuf)) & \
			cpu_to_le16(IEEE80211_FCTL_PM)) != 0)

#define ClearPwrMgt(pbuf) ({ \
	*(__le16 *)(pbuf) &= (~cpu_to_le16(IEEE80211_FCTL_PM)); \
})

#define SetMData(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(IEEE80211_FCTL_MOREDATA); \
})

#define GetMData(pbuf)	(((*(__le16 *)(pbuf)) & \
			cpu_to_le16(IEEE80211_FCTL_MOREDATA)) != 0)

#define ClearMData(pbuf) ({ \
	*(__le16 *)(pbuf) &= (~cpu_to_le16(IEEE80211_FCTL_MOREDATA)); \
})

#define SetPrivacy(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(IEEE80211_FCTL_PROTECTED); \
})

#define GetPrivacy(pbuf)	(((*(__le16 *)(pbuf)) & \
				cpu_to_le16(IEEE80211_FCTL_PROTECTED)) != 0)

#define GetOrder(pbuf)	(((*(__le16 *)(pbuf)) & \
			cpu_to_le16(IEEE80211_FCTL_ORDER)) != 0)

#define GetFrameType(pbuf)	(le16_to_cpu(*(__le16 *)(pbuf)) & \
				(BIT(3) | BIT(2)))

#define SetFrameType(pbuf, type)	\
	do {	\
		*(__le16 *)(pbuf) &= cpu_to_le16(~(BIT(3) | \
		BIT(2))); \
		*(__le16 *)(pbuf) |= cpu_to_le16(type); \
	} while (0)

#define GetFrameSubType(pbuf)	(le16_to_cpu(*(__le16 *)(pbuf)) & \
				(BIT(7) | BIT(6) | BIT(5) | BIT(4) | BIT(3) | \
				BIT(2)))

#define SetFrameSubType(pbuf, type) \
	do {    \
		*(__le16 *)(pbuf) &= cpu_to_le16(~(BIT(7) | BIT(6) | \
		BIT(5) | BIT(4) | BIT(3) | BIT(2))); \
		*(__le16 *)(pbuf) |= cpu_to_le16(type); \
	} while (0)

#define GetSequence(pbuf)	(le16_to_cpu(*(__le16 *)\
				((addr_t)(pbuf) + 22)) >> 4)

#define GetFragNum(pbuf)	(le16_to_cpu(*(__le16 *)((addr_t)\
				(pbuf) + 22)) & 0x0f)

#define SetSeqNum(pbuf, num) ({ \
	*(__le16 *)((addr_t)(pbuf) + 22) = \
	cpu_to_le16((le16_to_cpu(*(__le16 *)((addr_t)(pbuf) + 22)) & \
	0x000f) | (0xfff0 & (num << 4))); \
})

#define SetPriority(pbuf, tid) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(tid & 0xf); \
})

#define GetPriority(pbuf)	((le16_to_cpu(*(__le16 *)(pbuf))) & 0xf)

#define SetAckpolicy(pbuf, ack) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16((ack & 3) << 5); \
})

#define GetAckpolicy(pbuf) (((le16_to_cpu(*(__le16 *)pbuf)) >> 5) & 0x3)

#define GetAMsdu(pbuf) (((le16_to_cpu(*(__le16 *)pbuf)) >> 7) & 0x1)

#define GetAddr1Ptr(pbuf)	((unsigned char *)((addr_t)(pbuf) + 4))

#define GetAddr2Ptr(pbuf)	((unsigned char *)((addr_t)(pbuf) + 10))

#define GetAddr3Ptr(pbuf)	((unsigned char *)((addr_t)(pbuf) + 16))

#define GetAddr4Ptr(pbuf)	((unsigned char *)((addr_t)(pbuf) + 24))

static inline unsigned char *get_hdr_bssid(unsigned char *pframe)
{
	unsigned char	*sa;
	unsigned int	to_fr_ds = (GetToDs(pframe) << 1) | GetFrDs(pframe);

	switch (to_fr_ds) {
	case 0x00:	/* ToDs=0, FromDs=0 */
		sa = GetAddr3Ptr(pframe);
		break;
	case 0x01:	/* ToDs=0, FromDs=1 */
		sa = GetAddr2Ptr(pframe);
		break;
	case 0x02:	/* ToDs=1, FromDs=0 */
		sa = GetAddr1Ptr(pframe);
		break;
	default:	/* ToDs=1, FromDs=1 */
		sa = NULL;
		break;
	}
	return sa;
}

/*-----------------------------------------------------------------------------
 *		Below is for the security related definition
 *-----------------------------------------------------------------------------
 */
#define _ASOCREQ_IE_OFFSET_	4	/* excluding wlan_hdr */
#define	_ASOCRSP_IE_OFFSET_	6
#define _REASOCREQ_IE_OFFSET_	10
#define _REASOCRSP_IE_OFFSET_	6
#define _PROBEREQ_IE_OFFSET_	0
#define	_PROBERSP_IE_OFFSET_	12
#define _AUTH_IE_OFFSET_	6
#define _DEAUTH_IE_OFFSET_	0
#define _BEACON_IE_OFFSET_	12

#define _FIXED_IE_LENGTH_	_BEACON_IE_OFFSET_

#define _SSID_IE_		0
#define _SUPPORTEDRATES_IE_	1
#define _DSSET_IE_		3
#define _IBSS_PARA_IE_		6
#define _ERPINFO_IE_		42
#define _EXT_SUPPORTEDRATES_IE_	50

#define _HT_CAPABILITY_IE_	45
#define _HT_EXTRA_INFO_IE_	61
#define _HT_ADD_INFO_IE_	61 /* _HT_EXTRA_INFO_IE_ */

#define _VENDOR_SPECIFIC_IE_	221

#define	_RESERVED47_		47

/* ---------------------------------------------------------------------------
 *			Below is the fixed elements...
 * ---------------------------------------------------------------------------
 */
#define _AUTH_ALGM_NUM_			2
#define _AUTH_SEQ_NUM_			2
#define _BEACON_ITERVAL_		2
#define _CAPABILITY_			2
#define _CURRENT_APADDR_		6
#define _LISTEN_INTERVAL_		2
#define _RSON_CODE_				2
#define _ASOC_ID_				2
#define _STATUS_CODE_			2
#define _TIMESTAMP_				8

#define AUTH_ODD_TO				0
#define AUTH_EVEN_TO			1

/*-----------------------------------------------------------------------------
 *			Below is the definition for 802.11i / 802.1x
 *------------------------------------------------------------------------------
 */
#define _IEEE8021X_MGT_			1	/*WPA */
#define _IEEE8021X_PSK_			2	/* WPA with pre-shared key */

/*-----------------------------------------------------------------------------
 *			Below is the definition for WMM
 *------------------------------------------------------------------------------
 */
#define _WMM_IE_Length_				7  /* for WMM STA */

/*-----------------------------------------------------------------------------
 *			Below is the definition for 802.11n
 *------------------------------------------------------------------------------
 */

/*
 * struct rtl_ieee80211_ht_cap - HT capabilities
 *
 * This structure refers to "HT capabilities element" as
 * described in 802.11n draft section 7.3.2.52
 */

struct rtl_ieee80211_ht_cap {
	__le16	cap_info;
	unsigned char	ampdu_params_info;
	unsigned char	supp_mcs_set[16];
	__le16	extended_ht_cap_info;
	__le32	tx_BF_cap_info;
	unsigned char	       antenna_selection_info;
} __packed;

/**
 * struct ieee80211_ht_addt_info - HT additional information
 *
 * This structure refers to "HT information element" as
 * described in 802.11n draft section 7.3.2.53
 */
struct ieee80211_ht_addt_info {
	unsigned char	control_chan;
	unsigned char		ht_param;
	__le16	operation_mode;
	__le16	stbc_param;
	unsigned char		basic_set[16];
} __packed;

#endif /* _WIFI_H_ */

