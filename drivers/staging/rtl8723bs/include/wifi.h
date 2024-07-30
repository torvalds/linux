/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef _WIFI_H_
#define _WIFI_H_

#define WLAN_HDR_A3_LEN		24
#define WLAN_HDR_A3_QOS_LEN	26

#define WLAN_WMM_LEN		24

/*  This value is tested by WiFi 11n Test Plan 5.2.3. */
/*  This test verifies the WLAN NIC can update the NAV through sending the CTS with large duration. */
#define	WiFiNavUpperUs				30000	/*  30 ms */

enum {
	WIFI_MGT_TYPE  =	(0),
	WIFI_CTRL_TYPE =	(BIT(2)),
	WIFI_DATA_TYPE =	(BIT(3)),
	WIFI_QOS_DATA_TYPE	= (BIT(7)|BIT(3)),	/*  QoS Data */
};

enum {

    /*  below is for mgt frame */
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
    WIFI_ACTION_NOACK = (BIT(7) | BIT(6) | BIT(5) | WIFI_MGT_TYPE),

    /*  below is for control frame */
    WIFI_NDPA         = (BIT(6) | BIT(4) | WIFI_CTRL_TYPE),
    WIFI_PSPOLL         = (BIT(7) | BIT(5) | WIFI_CTRL_TYPE),
    WIFI_RTS            = (BIT(7) | BIT(5) | BIT(4) | WIFI_CTRL_TYPE),
    WIFI_CTS            = (BIT(7) | BIT(6) | WIFI_CTRL_TYPE),
    WIFI_ACK            = (BIT(7) | BIT(6) | BIT(4) | WIFI_CTRL_TYPE),
    WIFI_CFEND          = (BIT(7) | BIT(6) | BIT(5) | WIFI_CTRL_TYPE),
    WIFI_CFEND_CFACK    = (BIT(7) | BIT(6) | BIT(5) | BIT(4) | WIFI_CTRL_TYPE),

    /*  below is for data frame */
    WIFI_DATA           = (0 | WIFI_DATA_TYPE),
    WIFI_DATA_CFACK     = (BIT(4) | WIFI_DATA_TYPE),
    WIFI_DATA_CFPOLL    = (BIT(5) | WIFI_DATA_TYPE),
    WIFI_DATA_CFACKPOLL = (BIT(5) | BIT(4) | WIFI_DATA_TYPE),
    WIFI_DATA_NULL      = (BIT(6) | WIFI_DATA_TYPE),
    WIFI_CF_ACK         = (BIT(6) | BIT(4) | WIFI_DATA_TYPE),
    WIFI_CF_POLL        = (BIT(6) | BIT(5) | WIFI_DATA_TYPE),
    WIFI_CF_ACKPOLL     = (BIT(6) | BIT(5) | BIT(4) | WIFI_DATA_TYPE),
    WIFI_QOS_DATA_NULL	= (BIT(6) | WIFI_QOS_DATA_TYPE),
};

#define _TO_DS_		BIT(8)
#define _FROM_DS_	BIT(9)
#define _MORE_FRAG_	BIT(10)
#define _RETRY_		BIT(11)
#define _PWRMGT_	BIT(12)
#define _MORE_DATA_	BIT(13)
#define _PRIVACY_	BIT(14)
#define _ORDER_			BIT(15)

#define SetToDs(pbuf)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16(_TO_DS_))

#define GetToDs(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(_TO_DS_)) != 0)

#define SetFrDs(pbuf)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16(_FROM_DS_))

#define GetFrDs(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(_FROM_DS_)) != 0)

#define get_tofr_ds(pframe)	((GetToDs(pframe) << 1) | GetFrDs(pframe))

#define SetMFrag(pbuf)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16(_MORE_FRAG_))

#define GetMFrag(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(_MORE_FRAG_)) != 0)

#define ClearMFrag(pbuf)	\
	(*(__le16 *)(pbuf) &= (~cpu_to_le16(_MORE_FRAG_)))

#define GetRetry(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(_RETRY_)) != 0)

#define ClearRetry(pbuf)	\
	(*(__le16 *)(pbuf) &= (~cpu_to_le16(_RETRY_)))

#define SetPwrMgt(pbuf)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16(_PWRMGT_))

#define GetPwrMgt(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(_PWRMGT_)) != 0)

#define ClearPwrMgt(pbuf)	\
	(*(__le16 *)(pbuf) &= (~cpu_to_le16(_PWRMGT_)))

#define SetMData(pbuf)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16(_MORE_DATA_))

#define GetMData(pbuf)	(((*(__le16 *)(pbuf)) & cpu_to_le16(_MORE_DATA_)) != 0)

#define ClearMData(pbuf)	\
	(*(__le16 *)(pbuf) &= (~cpu_to_le16(_MORE_DATA_)))

#define SetPrivacy(pbuf)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16(_PRIVACY_))

#define GetPrivacy(pbuf)					\
	(((*(__le16 *)(pbuf)) & cpu_to_le16(_PRIVACY_)) != 0)

#define GetOrder(pbuf)					\
	(((*(__le16 *)(pbuf)) & cpu_to_le16(_ORDER_)) != 0)

#define GetFrameType(pbuf)				\
	(le16_to_cpu(*(__le16 *)(pbuf)) & (BIT(3) | BIT(2)))

#define SetFrameType(pbuf, type)	\
	do {	\
		*(unsigned short *)(pbuf) &= cpu_to_le16(~(BIT(3) | BIT(2))); \
		*(unsigned short *)(pbuf) |= cpu_to_le16(type); \
	} while (0)

#define GetFrameSubType(pbuf)	(le16_to_cpu(*(__le16 *)(pbuf)) & (BIT(7) |\
	 BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2)))

#define SetFrameSubType(pbuf, type) \
	do {    \
		*(__le16 *)(pbuf) &= cpu_to_le16(~(BIT(7) | BIT(6) |	\
		 BIT(5) | BIT(4) | BIT(3) | BIT(2))); \
		*(__le16 *)(pbuf) |= cpu_to_le16(type); \
	} while (0)

#define GetSequence(pbuf)			\
	(le16_to_cpu(*(__le16 *)((size_t)(pbuf) + 22)) >> 4)

#define GetFragNum(pbuf)			\
	(le16_to_cpu(*(__le16 *)((size_t)(pbuf) + 22)) & 0x0f)

#define SetFragNum(pbuf, num) \
	do {    \
		*(unsigned short *)((size_t)(pbuf) + 22) = \
			((*(unsigned short *)((size_t)(pbuf) + 22)) &	\
			le16_to_cpu(~(0x000f))) | \
			cpu_to_le16(0x0f & (num));     \
	} while (0)

#define SetSeqNum(pbuf, num) \
	do {    \
		*(__le16 *)((size_t)(pbuf) + 22) = \
			((*(__le16 *)((size_t)(pbuf) + 22)) & cpu_to_le16((unsigned short)0x000f)) | \
			cpu_to_le16((unsigned short)(0xfff0 & (num << 4))); \
	} while (0)

#define SetDuration(pbuf, dur) \
	(*(__le16 *)((size_t)(pbuf) + 2) = cpu_to_le16(0xffff & (dur)))


#define SetPriority(pbuf, tid)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16(tid & 0xf))

#define GetPriority(pbuf)	((le16_to_cpu(*(__le16 *)(pbuf))) & 0xf)

#define SetEOSP(pbuf, eosp)	\
		(*(__le16 *)(pbuf) |= cpu_to_le16((eosp & 1) << 4))

#define SetAckpolicy(pbuf, ack)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16((ack & 3) << 5))

#define GetAckpolicy(pbuf) (((le16_to_cpu(*(__le16 *)pbuf)) >> 5) & 0x3)

#define GetAMsdu(pbuf) (((le16_to_cpu(*(__le16 *)pbuf)) >> 7) & 0x1)

#define GetAid(pbuf)	(le16_to_cpu(*(__le16 *)((size_t)(pbuf) + 2)) & 0x3fff)

#define GetAddr1Ptr(pbuf)	((unsigned char *)((size_t)(pbuf) + 4))

#define GetAddr2Ptr(pbuf)	((unsigned char *)((size_t)(pbuf) + 10))

#define GetAddr3Ptr(pbuf)	((unsigned char *)((size_t)(pbuf) + 16))

#define GetAddr4Ptr(pbuf)	((unsigned char *)((size_t)(pbuf) + 24))

static inline unsigned char *rtl8723bs_get_ra(unsigned char *pframe)
{
	unsigned char *ra;
	ra = GetAddr1Ptr(pframe);
	return ra;
}
static inline unsigned char *get_ta(unsigned char *pframe)
{
	unsigned char *ta;
	ta = GetAddr2Ptr(pframe);
	return ta;
}

static inline unsigned char *get_da(unsigned char *pframe)
{
	unsigned char *da;
	unsigned int	to_fr_ds	= (GetToDs(pframe) << 1) | GetFrDs(pframe);

	switch (to_fr_ds) {
	case 0x00:	/*  ToDs = 0, FromDs = 0 */
		da = GetAddr1Ptr(pframe);
		break;
	case 0x01:	/*  ToDs = 0, FromDs = 1 */
		da = GetAddr1Ptr(pframe);
		break;
	case 0x02:	/*  ToDs = 1, FromDs = 0 */
		da = GetAddr3Ptr(pframe);
		break;
	default:	/*  ToDs = 1, FromDs = 1 */
		da = GetAddr3Ptr(pframe);
		break;
	}

	return da;
}


static inline unsigned char *get_sa(unsigned char *pframe)
{
	unsigned char *sa;
	unsigned int	to_fr_ds	= (GetToDs(pframe) << 1) | GetFrDs(pframe);

	switch (to_fr_ds) {
	case 0x00:	/*  ToDs = 0, FromDs = 0 */
		sa = GetAddr2Ptr(pframe);
		break;
	case 0x01:	/*  ToDs = 0, FromDs = 1 */
		sa = GetAddr3Ptr(pframe);
		break;
	case 0x02:	/*  ToDs = 1, FromDs = 0 */
		sa = GetAddr2Ptr(pframe);
		break;
	default:	/*  ToDs = 1, FromDs = 1 */
		sa = GetAddr4Ptr(pframe);
		break;
	}

	return sa;
}

static inline unsigned char *get_hdr_bssid(unsigned char *pframe)
{
	unsigned char *sa = NULL;
	unsigned int	to_fr_ds	= (GetToDs(pframe) << 1) | GetFrDs(pframe);

	switch (to_fr_ds) {
	case 0x00:	/*  ToDs = 0, FromDs = 0 */
		sa = GetAddr3Ptr(pframe);
		break;
	case 0x01:	/*  ToDs = 0, FromDs = 1 */
		sa = GetAddr2Ptr(pframe);
		break;
	case 0x02:	/*  ToDs = 1, FromDs = 0 */
		sa = GetAddr1Ptr(pframe);
		break;
	case 0x03:	/*  ToDs = 1, FromDs = 1 */
		sa = GetAddr1Ptr(pframe);
		break;
	}

	return sa;
}


static inline int IsFrameTypeCtrl(unsigned char *pframe)
{
	if (WIFI_CTRL_TYPE == GetFrameType(pframe))
		return true;
	else
		return false;
}
/*-----------------------------------------------------------------------------
			Below is for the security related definition
------------------------------------------------------------------------------*/

#define _ASOCREQ_IE_OFFSET_		4	/*  excluding wlan_hdr */
#define _REASOCREQ_IE_OFFSET_	10
#define _PROBEREQ_IE_OFFSET_	0
#define	_PROBERSP_IE_OFFSET_	12
#define _AUTH_IE_OFFSET_		6
#define _BEACON_IE_OFFSET_		12

#define _FIXED_IE_LENGTH_			_BEACON_IE_OFFSET_

/* ---------------------------------------------------------------------------
					Below is the fixed elements...
-----------------------------------------------------------------------------*/
#define _AUTH_ALGM_NUM_			2
#define _AUTH_SEQ_NUM_			2
#define _BEACON_ITERVAL_		2
#define _CAPABILITY_			2
#define _RSON_CODE_				2
#define _ASOC_ID_				2
#define _STATUS_CODE_			2
#define _TIMESTAMP_				8

/*-----------------------------------------------------------------------------
				Below is the definition for 802.11i / 802.1x
------------------------------------------------------------------------------*/
#define _IEEE8021X_MGT_			1		/*  WPA */
#define _IEEE8021X_PSK_			2		/*  WPA with pre-shared key */

#define _MME_IE_LENGTH_  18
/*-----------------------------------------------------------------------------
				Below is the definition for WMM
------------------------------------------------------------------------------*/
#define _WMM_IE_Length_				7  /*  for WMM STA */

/*-----------------------------------------------------------------------------
				Below is the definition for 802.11n
------------------------------------------------------------------------------*/
#define GetOrderBit(pbuf)	(((*(unsigned short *)(pbuf)) & cpu_to_le16(_ORDER_)) != 0)

#define ACT_CAT_VENDOR				0x7F/* 127 */

/**
 * struct rtw_ieee80211_ht_cap - HT additional information
 *
 * This structure refers to "HT information element" as
 * described in 802.11n draft section 7.3.2.53
 */
struct ieee80211_ht_addt_info {
	unsigned char control_chan;
	unsigned char 	ht_param;
	__le16	operation_mode;
	__le16	stbc_param;
	unsigned char 	basic_set[16];
} __attribute__ ((packed));


struct HT_caps_element {
	union {
		struct {
			__le16	HT_caps_info;
			unsigned char AMPDU_para;
			unsigned char MCS_rate[16];
			__le16	HT_ext_caps;
			__le16	Beamforming_caps;
			unsigned char ASEL_caps;
		} HT_cap_element;
		unsigned char HT_cap[26];
	} u;
} __attribute__ ((packed));

struct HT_info_element {
	unsigned char primary_channel;
	unsigned char infos[5];
	unsigned char MCS_rate[16];
}  __attribute__ ((packed));

struct AC_param {
	unsigned char 	ACI_AIFSN;
	unsigned char 	CW;
	__le16	TXOP_limit;
}  __attribute__ ((packed));

struct WMM_para_element {
	unsigned char 	QoS_info;
	unsigned char 	reserved;
	struct AC_param	ac_param[4];
}  __attribute__ ((packed));

struct ADDBA_request {
	unsigned char 	dialog_token;
	__le16	BA_para_set;
	__le16	BA_timeout_value;
	__le16	BA_starting_seqctrl;
}  __attribute__ ((packed));

/* 802.11n HT capabilities masks */
#define IEEE80211_HT_CAP_LDPC_CODING		0x0001
#define IEEE80211_HT_CAP_SUP_WIDTH		0x0002
#define IEEE80211_HT_CAP_SM_PS			0x000C
#define IEEE80211_HT_CAP_GRN_FLD		0x0010
#define IEEE80211_HT_CAP_SGI_20			0x0020
#define IEEE80211_HT_CAP_SGI_40			0x0040
#define IEEE80211_HT_CAP_TX_STBC			0x0080
#define IEEE80211_HT_CAP_RX_STBC_1R		0x0100
#define IEEE80211_HT_CAP_RX_STBC_3R		0x0300
#define IEEE80211_HT_CAP_MAX_AMSDU		0x0800
#define IEEE80211_HT_CAP_DSSSCCK40		0x1000
/* 802.11n HT capability AMPDU settings */
#define IEEE80211_HT_CAP_AMPDU_FACTOR		0x03
#define IEEE80211_HT_CAP_AMPDU_DENSITY		0x1C

/* endif */

/* 	===============WPS Section =============== */
/* 	WPS attribute ID */
#define WPS_ATTR_SELECTED_REGISTRAR	0x1041

/* 	=====================P2P Section ===================== */
enum p2p_role {
	P2P_ROLE_DISABLE = 0,
	P2P_ROLE_DEVICE = 1,
	P2P_ROLE_CLIENT = 2,
	P2P_ROLE_GO = 3
};

enum p2p_state {
	P2P_STATE_NONE = 0,							/* 	P2P disable */
	P2P_STATE_IDLE = 1,								/* 	P2P had enabled and do nothing */
	P2P_STATE_LISTEN = 2,							/* 	In pure listen state */
	P2P_STATE_SCAN = 3,							/* 	In scan phase */
	P2P_STATE_FIND_PHASE_LISTEN = 4,				/* 	In the listen state of find phase */
	P2P_STATE_FIND_PHASE_SEARCH = 5,				/* 	In the search state of find phase */
	P2P_STATE_TX_PROVISION_DIS_REQ = 6,			/* 	In P2P provisioning discovery */
	P2P_STATE_RX_PROVISION_DIS_RSP = 7,
	P2P_STATE_RX_PROVISION_DIS_REQ = 8,
	P2P_STATE_GONEGO_ING = 9,						/* 	Doing the group owner negotiation handshake */
	P2P_STATE_GONEGO_OK = 10,						/* 	finish the group negotiation handshake with success */
	P2P_STATE_GONEGO_FAIL = 11,					/* 	finish the group negotiation handshake with failure */
	P2P_STATE_RECV_INVITE_REQ_MATCH = 12,		/* 	receiving the P2P Invitation request and match with the profile. */
	P2P_STATE_PROVISIONING_ING = 13,				/* 	Doing the P2P WPS */
	P2P_STATE_PROVISIONING_DONE = 14,			/* 	Finish the P2P WPS */
	P2P_STATE_TX_INVITE_REQ = 15,					/* 	Transmit the P2P Invitation request */
	P2P_STATE_RX_INVITE_RESP_OK = 16,				/* 	Receiving the P2P Invitation response */
	P2P_STATE_RECV_INVITE_REQ_DISMATCH = 17,	/* 	receiving the P2P Invitation request and mismatch with the profile. */
	P2P_STATE_RECV_INVITE_REQ_GO = 18,			/* 	receiving the P2P Invitation request and this wifi is GO. */
	P2P_STATE_RECV_INVITE_REQ_JOIN = 19,			/* 	receiving the P2P Invitation request to join an existing P2P Group. */
	P2P_STATE_RX_INVITE_RESP_FAIL = 20,			/* 	recveing the P2P Invitation response with failure */
	P2P_STATE_RX_INFOR_NOREADY = 21,			/*  receiving p2p negotiation response with information is not available */
	P2P_STATE_TX_INFOR_NOREADY = 22,			/*  sending p2p negotiation response with information is not available */
};

enum p2p_wpsinfo {
	P2P_NO_WPSINFO						= 0,
	P2P_GOT_WPSINFO_PEER_DISPLAY_PIN	= 1,
	P2P_GOT_WPSINFO_SELF_DISPLAY_PIN	= 2,
	P2P_GOT_WPSINFO_PBC					= 3,
};

#define IP_MCAST_MAC(mac)		((mac[0] == 0x01) && (mac[1] == 0x00) && (mac[2] == 0x5e))
#define ICMPV6_MCAST_MAC(mac)	((mac[0] == 0x33) && (mac[1] == 0x33) && (mac[2] != 0xff))

/* Regulatroy Domain */
struct regd_pair_mapping {
	u16 reg_dmnenum;
	u16 reg_2ghz_ctl;
};

struct rtw_regulatory {
	char alpha2[2];
	u16 country_code;
	u16 max_power_level;
	u32 tp_scale;
	u16 current_rd;
	u16 current_rd_ext;
	int16_t power_limit;
	struct regd_pair_mapping *regpair;
};

#endif /*  _WIFI_H_ */
