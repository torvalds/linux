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

#define WLAN_IEEE_OUI_LEN	3
#define WLAN_CRC_LEN		4
#define WLAN_BSSID_LEN		6
#define WLAN_BSS_TS_LEN		8
#define WLAN_HDR_A3_LEN		24
#define WLAN_HDR_A4_LEN		30
#define WLAN_HDR_A3_QOS_LEN	26
#define WLAN_HDR_A4_QOS_LEN	32
#define WLAN_SSID_MAXLEN	32
#define WLAN_DATA_MAXLEN	2312

#define WLAN_A3_PN_OFFSET	24
#define WLAN_A4_PN_OFFSET	30

#define WLAN_MIN_ETHFRM_LEN	60
#define WLAN_MAX_ETHFRM_LEN	1514

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

enum WIFI_REASON_CODE	{
	_RSON_RESERVED_			= 0,
	_RSON_UNSPECIFIED_		= 1,
	_RSON_AUTH_NO_LONGER_VALID_	= 2,
	_RSON_DEAUTH_STA_LEAVING_	= 3,
	_RSON_INACTIVITY_		= 4,
	_RSON_UNABLE_HANDLE_		= 5,
	_RSON_CLS2_			= 6,
	_RSON_CLS3_			= 7,
	_RSON_DISAOC_STA_LEAVING_	= 8,
	_RSON_ASOC_NOT_AUTH_		= 9,
	/* WPA reason */
	_RSON_INVALID_IE_		= 13,
	_RSON_MIC_FAILURE_		= 14,
	_RSON_4WAY_HNDSHK_TIMEOUT_	= 15,
	_RSON_GROUP_KEY_UPDATE_TIMEOUT_	= 16,
	_RSON_DIFF_IE_			= 17,
	_RSON_MLTCST_CIPHER_NOT_VALID_	= 18,
	_RSON_UNICST_CIPHER_NOT_VALID_	= 19,
	_RSON_AKMP_NOT_VALID_		= 20,
	_RSON_UNSUPPORT_RSNE_VER_	= 21,
	_RSON_INVALID_RSNE_CAP_		= 22,
	_RSON_IEEE_802DOT1X_AUTH_FAIL_	= 23,
	/* below are Realtek definitions */
	_RSON_PMK_NOT_AVAILABLE_	= 24,
};

enum WIFI_STATUS_CODE {
	_STATS_SUCCESSFUL_		= 0,
	_STATS_FAILURE_			= 1,
	_STATS_CAP_FAIL_		= 10,
	_STATS_NO_ASOC_			= 11,
	_STATS_OTHER_			= 12,
	_STATS_NO_SUPP_ALG_		= 13,
	_STATS_OUT_OF_AUTH_SEQ_		= 14,
	_STATS_CHALLENGE_FAIL_		= 15,
	_STATS_AUTH_TIMEOUT_		= 16,
	_STATS_UNABLE_HANDLE_STA_	= 17,
	_STATS_RATE_FAIL_		= 18,
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

#define _TO_DS_		BIT(8)
#define _FROM_DS_	BIT(9)
#define _MORE_FRAG_	BIT(10)
#define _RETRY_		BIT(11)
#define _PWRMGT_	BIT(12)
#define _MORE_DATA_	BIT(13)
#define _PRIVACY_	BIT(14)
#define _ORDER_		BIT(15)

#define SetToDs(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(_TO_DS_); \
})

#define GetToDs(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(_TO_DS_)) != 0)

#define ClearToDs(pbuf)	({ \
	*(__le16 *)(pbuf) &= (~cpu_to_le16(_TO_DS_)); \
})

#define SetFrDs(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(_FROM_DS_); \
})

#define GetFrDs(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(_FROM_DS_)) != 0)

#define ClearFrDs(pbuf)	({ \
	*(__le16 *)(pbuf) &= (~cpu_to_le16(_FROM_DS_)); \
})

static inline unsigned char get_tofr_ds(unsigned char *pframe)
{
	return ((GetToDs(pframe) << 1) | GetFrDs(pframe));
}

#define SetMFrag(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(_MORE_FRAG_); \
})

#define GetMFrag(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(_MORE_FRAG_)) != 0)

#define ClearMFrag(pbuf) ({ \
	*(__le16 *)(pbuf) &= (~cpu_to_le16(_MORE_FRAG_)); \
})

#define SetRetry(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(_RETRY_); \
})

#define GetRetry(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(_RETRY_)) != 0)

#define ClearRetry(pbuf) ({ \
	*(__le16 *)(pbuf) &= (~cpu_to_le16(_RETRY_)); \
})

#define SetPwrMgt(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(_PWRMGT_); \
})

#define GetPwrMgt(pbuf)	(((*(__le16 *)(pbuf)) & \
			cpu_to_le16(_PWRMGT_)) != 0)

#define ClearPwrMgt(pbuf) ({ \
	*(__le16 *)(pbuf) &= (~cpu_to_le16(_PWRMGT_)); \
})

#define SetMData(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(_MORE_DATA_); \
})

#define GetMData(pbuf)	(((*(__le16 *)(pbuf)) & \
			cpu_to_le16(_MORE_DATA_)) != 0)

#define ClearMData(pbuf) ({ \
	*(__le16 *)(pbuf) &= (~cpu_to_le16(_MORE_DATA_)); \
})

#define SetPrivacy(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(_PRIVACY_); \
})

#define GetPrivacy(pbuf)	(((*(__le16 *)(pbuf)) & \
				cpu_to_le16(_PRIVACY_)) != 0)

#define GetOrder(pbuf)	(((*(__le16 *)(pbuf)) & \
			cpu_to_le16(_ORDER_)) != 0)

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

#define SetDuration(pbuf, dur) ({ \
	*(__le16 *)((addr_t)(pbuf) + 2) |= \
	cpu_to_le16(0xffff & (dur)); \
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

#define GetAid(pbuf)	(cpu_to_le16(*(__le16 *)((addr_t)(pbuf) + 2)) \
			& 0x3fff)

#define GetAddr1Ptr(pbuf)	((unsigned char *)((addr_t)(pbuf) + 4))

#define GetAddr2Ptr(pbuf)	((unsigned char *)((addr_t)(pbuf) + 10))

#define GetAddr3Ptr(pbuf)	((unsigned char *)((addr_t)(pbuf) + 16))

#define GetAddr4Ptr(pbuf)	((unsigned char *)((addr_t)(pbuf) + 24))

static inline unsigned char *get_da(unsigned char *pframe)
{
	unsigned char	*da;
	unsigned int	to_fr_ds = (GetToDs(pframe) << 1) | GetFrDs(pframe);

	switch (to_fr_ds) {
	case 0x00:	/* ToDs=0, FromDs=0 */
		da = GetAddr1Ptr(pframe);
		break;
	case 0x01:	/* ToDs=0, FromDs=1 */
		da = GetAddr1Ptr(pframe);
		break;
	case 0x02:	/* ToDs=1, FromDs=0 */
		da = GetAddr3Ptr(pframe);
		break;
	default:	/* ToDs=1, FromDs=1 */
		da = GetAddr3Ptr(pframe);
		break;
	}
	return da;
}


static inline unsigned char *get_sa(unsigned char *pframe)
{
	unsigned char	*sa;
	unsigned int	to_fr_ds = (GetToDs(pframe) << 1) | GetFrDs(pframe);

	switch (to_fr_ds) {
	case 0x00:	/* ToDs=0, FromDs=0 */
		sa = GetAddr2Ptr(pframe);
		break;
	case 0x01:	/* ToDs=0, FromDs=1 */
		sa = GetAddr3Ptr(pframe);
		break;
	case 0x02:	/* ToDs=1, FromDs=0 */
		sa = GetAddr2Ptr(pframe);
		break;
	default:	/* ToDs=1, FromDs=1 */
		sa = GetAddr4Ptr(pframe);
		break;
	}

	return sa;
}

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
#define _RESERVED_FRAME_TYPE_	0
#define _SKB_FRAME_TYPE_	2
#define _PRE_ALLOCMEM_		1
#define _PRE_ALLOCHDR_		3
#define _PRE_ALLOCLLCHDR_	4
#define _PRE_ALLOCICVHDR_	5
#define _PRE_ALLOCMICHDR_	6

#define _SIFSTIME_		((priv->pmib->BssType.net_work_type & \
				WIRELESS_11A) ? 16 : 10)
#define _ACKCTSLNG_		14	/*14 bytes long, including crclng */
#define _CRCLNG_		4

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

#define WLAN_ETHCONV_ENCAP		1
#define WLAN_ETHCONV_RFC1042	2
#define WLAN_ETHCONV_8021h		3

#define cap_ESS BIT(0)
#define cap_IBSS BIT(1)
#define cap_CFPollable BIT(2)
#define cap_CFRequest BIT(3)
#define cap_Privacy BIT(4)
#define cap_ShortPremble BIT(5)

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
#define _WMM_Para_Element_Length_		24


/*-----------------------------------------------------------------------------
 *			Below is the definition for 802.11n
 *------------------------------------------------------------------------------
 */

/* block-ack parameters */
#define IEEE80211_ADDBA_PARAM_POLICY_MASK 0x0002
#define IEEE80211_ADDBA_PARAM_TID_MASK 0x003C
#define IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK 0xFFA0
#define IEEE80211_DELBA_PARAM_TID_MASK 0xF000
#define IEEE80211_DELBA_PARAM_INITIATOR_MASK 0x0800

#define SetOrderBit(pbuf) ({ \
	*(__le16 *)(pbuf) |= cpu_to_le16(_ORDER_); \
})

#define GetOrderBit(pbuf)	(((*(__le16 *)(pbuf)) & \
				le16_to_cpu(_ORDER_)) != 0)


/**
 * struct ieee80211_bar - HT Block Ack Request
 *
 * This structure refers to "HT BlockAckReq" as
 * described in 802.11n draft section 7.2.1.7.1
 */
struct ieee80211_bar {
	__le16 frame_control;
	__le16 duration;
	unsigned char ra[6];
	unsigned char ta[6];
	__le16 control;
	__le16 start_seq_num;
} __packed;

/* 802.11 BAR control masks */
#define IEEE80211_BAR_CTRL_ACK_POLICY_NORMAL     0x0000
#define IEEE80211_BAR_CTRL_CBMTID_COMPRESSED_BA  0x0004


/*
 * struct ieee80211_ht_cap - HT capabilities
 *
 * This structure refers to "HT capabilities element" as
 * described in 802.11n draft section 7.3.2.52
 */

struct ieee80211_ht_cap {
	__le16	cap_info;
	unsigned char	ampdu_params_info;
	unsigned char	supp_mcs_set[16];
	__le16	extended_ht_cap_info;
	__le32	tx_BF_cap_info;
	unsigned char	       antenna_selection_info;
} __packed;

/**
 * struct ieee80211_ht_cap - HT additional information
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

/* 802.11n HT capabilities masks */
#define IEEE80211_HT_CAP_SUP_WIDTH		0x0002
#define IEEE80211_HT_CAP_SM_PS			0x000C
#define IEEE80211_HT_CAP_GRN_FLD		0x0010
#define IEEE80211_HT_CAP_SGI_20			0x0020
#define IEEE80211_HT_CAP_SGI_40			0x0040
#define IEEE80211_HT_CAP_TX_STBC			0x0080
#define IEEE80211_HT_CAP_DELAY_BA		0x0400
#define IEEE80211_HT_CAP_MAX_AMSDU		0x0800
#define IEEE80211_HT_CAP_DSSSCCK40		0x1000
/* 802.11n HT capability AMPDU settings */
#define IEEE80211_HT_CAP_AMPDU_FACTOR		0x03
#define IEEE80211_HT_CAP_AMPDU_DENSITY		0x1C
/* 802.11n HT capability MSC set */
#define IEEE80211_SUPP_MCS_SET_UEQM		4
#define IEEE80211_HT_CAP_MAX_STREAMS		4
#define IEEE80211_SUPP_MCS_SET_LEN		10
/* maximum streams the spec allows */
#define IEEE80211_HT_CAP_MCS_TX_DEFINED		0x01
#define IEEE80211_HT_CAP_MCS_TX_RX_DIFF		0x02
#define IEEE80211_HT_CAP_MCS_TX_STREAMS		0x0C
#define IEEE80211_HT_CAP_MCS_TX_UEQM		0x10
/* 802.11n HT IE masks */
#define IEEE80211_HT_IE_CHA_SEC_OFFSET		0x03
#define IEEE80211_HT_IE_CHA_SEC_NONE		0x00
#define IEEE80211_HT_IE_CHA_SEC_ABOVE		0x01
#define IEEE80211_HT_IE_CHA_SEC_BELOW		0x03
#define IEEE80211_HT_IE_CHA_WIDTH		0x04
#define IEEE80211_HT_IE_HT_PROTECTION		0x0003
#define IEEE80211_HT_IE_NON_GF_STA_PRSNT	0x0004
#define IEEE80211_HT_IE_NON_HT_STA_PRSNT	0x0010

/* block-ack parameters */
#define IEEE80211_ADDBA_PARAM_POLICY_MASK 0x0002
#define IEEE80211_ADDBA_PARAM_TID_MASK 0x003C
#define IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK 0xFFA0
#define IEEE80211_DELBA_PARAM_TID_MASK 0xF000
#define IEEE80211_DELBA_PARAM_INITIATOR_MASK 0x0800

/*
 * A-PMDU buffer sizes
 * According to IEEE802.11n spec size varies from 8K to 64K (in powers of 2)
 */
#define IEEE80211_MIN_AMPDU_BUF 0x8


/* Spatial Multiplexing Power Save Modes */
#define WLAN_HT_CAP_SM_PS_STATIC		0
#define WLAN_HT_CAP_SM_PS_DYNAMIC	1
#define WLAN_HT_CAP_SM_PS_INVALID	2
#define WLAN_HT_CAP_SM_PS_DISABLED	3

#endif /* _WIFI_H_ */

