/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef _WIFI_H_
#define _WIFI_H_

#define WLAN_ETHHDR_LEN		14
#define WLAN_ETHADDR_LEN	6
#define WLAN_IEEE_OUI_LEN	3
#define WLAN_ADDR_LEN		6
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
#define WLAN_ETHHDR_LEN		14
#define WLAN_WMM_LEN		24

#define P80211CAPTURE_VERSION	0x80211001

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

#define GetTupleCache(pbuf)			\
	(cpu_to_le16(*(unsigned short *)((size_t)(pbuf) + 22)))

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
#define _RESERVED_FRAME_TYPE_	0
#define _SKB_FRAME_TYPE_		2
#define _PRE_ALLOCMEM_			1
#define _PRE_ALLOCHDR_			3
#define _PRE_ALLOCLLCHDR_		4
#define _PRE_ALLOCICVHDR_		5
#define _PRE_ALLOCMICHDR_		6

#define _ACKCTSLNG_				14	/* 14 bytes long, including crclng */
#define _CRCLNG_				4

#define _ASOCREQ_IE_OFFSET_		4	/*  excluding wlan_hdr */
#define	_ASOCRSP_IE_OFFSET_		6
#define _REASOCREQ_IE_OFFSET_	10
#define _REASOCRSP_IE_OFFSET_	6
#define _PROBEREQ_IE_OFFSET_	0
#define	_PROBERSP_IE_OFFSET_	12
#define _AUTH_IE_OFFSET_		6
#define _DEAUTH_IE_OFFSET_		0
#define _BEACON_IE_OFFSET_		12
#define _PUBLIC_ACTION_IE_OFFSET_	8

#define _FIXED_IE_LENGTH_			_BEACON_IE_OFFSET_

/* ---------------------------------------------------------------------------
					Below is the fixed elements...
-----------------------------------------------------------------------------*/
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
#define _WMM_Para_Element_Length_		24


/*-----------------------------------------------------------------------------
				Below is the definition for 802.11n
------------------------------------------------------------------------------*/

#define SetOrderBit(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) |= cpu_to_le16(_ORDER_); \
	} while (0)

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
#define IEEE80211_HT_CAP_RX_STBC_2R		0x0200
#define IEEE80211_HT_CAP_RX_STBC_3R		0x0300
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
/* 802.11n HT capability TXBF capability */
#define IEEE80211_HT_CAP_TXBF_RX_NDP		0x00000008
#define IEEE80211_HT_CAP_TXBF_TX_NDP		0x00000010
#define IEEE80211_HT_CAP_TXBF_EXPLICIT_COMP_STEERING_CAP	0x00000400

/* endif */

/* 	===============WPS Section =============== */
/* 	For WPSv1.0 */
#define WPSOUI							0x0050f204
/* 	WPS attribute ID */
#define WPS_ATTR_VER1					0x104A
#define WPS_ATTR_SIMPLE_CONF_STATE	0x1044
#define WPS_ATTR_RESP_TYPE			0x103B
#define WPS_ATTR_UUID_E				0x1047
#define WPS_ATTR_MANUFACTURER		0x1021
#define WPS_ATTR_MODEL_NAME			0x1023
#define WPS_ATTR_MODEL_NUMBER		0x1024
#define WPS_ATTR_SERIAL_NUMBER		0x1042
#define WPS_ATTR_PRIMARY_DEV_TYPE	0x1054
#define WPS_ATTR_SEC_DEV_TYPE_LIST	0x1055
#define WPS_ATTR_DEVICE_NAME			0x1011
#define WPS_ATTR_CONF_METHOD			0x1008
#define WPS_ATTR_RF_BANDS				0x103C
#define WPS_ATTR_DEVICE_PWID			0x1012
#define WPS_ATTR_REQUEST_TYPE			0x103A
#define WPS_ATTR_ASSOCIATION_STATE	0x1002
#define WPS_ATTR_CONFIG_ERROR			0x1009
#define WPS_ATTR_VENDOR_EXT			0x1049
#define WPS_ATTR_SELECTED_REGISTRAR	0x1041

/* 	Value of WPS attribute "WPS_ATTR_DEVICE_NAME */
#define WPS_MAX_DEVICE_NAME_LEN		32

/* 	Value of WPS Request Type Attribute */
#define WPS_REQ_TYPE_ENROLLEE_INFO_ONLY			0x00
#define WPS_REQ_TYPE_ENROLLEE_OPEN_8021X		0x01
#define WPS_REQ_TYPE_REGISTRAR					0x02
#define WPS_REQ_TYPE_WLAN_MANAGER_REGISTRAR	0x03

/* 	Value of WPS Response Type Attribute */
#define WPS_RESPONSE_TYPE_INFO_ONLY	0x00
#define WPS_RESPONSE_TYPE_8021X		0x01
#define WPS_RESPONSE_TYPE_REGISTRAR	0x02
#define WPS_RESPONSE_TYPE_AP			0x03

/* 	Value of WPS WiFi Simple Configuration State Attribute */
#define WPS_WSC_STATE_NOT_CONFIG	0x01
#define WPS_WSC_STATE_CONFIG			0x02

/* 	Value of WPS Version Attribute */
#define WPS_VERSION_1					0x10

/* 	Value of WPS Configuration Method Attribute */
#define WPS_CONFIG_METHOD_FLASH		0x0001
#define WPS_CONFIG_METHOD_ETHERNET	0x0002
#define WPS_CONFIG_METHOD_LABEL		0x0004
#define WPS_CONFIG_METHOD_DISPLAY	0x0008
#define WPS_CONFIG_METHOD_E_NFC		0x0010
#define WPS_CONFIG_METHOD_I_NFC		0x0020
#define WPS_CONFIG_METHOD_NFC		0x0040
#define WPS_CONFIG_METHOD_PBC		0x0080
#define WPS_CONFIG_METHOD_KEYPAD	0x0100
#define WPS_CONFIG_METHOD_VPBC		0x0280
#define WPS_CONFIG_METHOD_PPBC		0x0480
#define WPS_CONFIG_METHOD_VDISPLAY	0x2008
#define WPS_CONFIG_METHOD_PDISPLAY	0x4008

/* 	Value of Category ID of WPS Primary Device Type Attribute */
#define WPS_PDT_CID_DISPLAYS			0x0007
#define WPS_PDT_CID_MULIT_MEDIA		0x0008
#define WPS_PDT_CID_RTK_WIDI			WPS_PDT_CID_MULIT_MEDIA

/* 	Value of Sub Category ID of WPS Primary Device Type Attribute */
#define WPS_PDT_SCID_MEDIA_SERVER	0x0005
#define WPS_PDT_SCID_RTK_DMP			WPS_PDT_SCID_MEDIA_SERVER

/* 	Value of Device Password ID */
#define WPS_DPID_PIN					0x0000
#define WPS_DPID_USER_SPEC			0x0001
#define WPS_DPID_MACHINE_SPEC			0x0002
#define WPS_DPID_REKEY					0x0003
#define WPS_DPID_PBC					0x0004
#define WPS_DPID_REGISTRAR_SPEC		0x0005

/* 	Value of WPS RF Bands Attribute */
#define WPS_RF_BANDS_2_4_GHZ		0x01
#define WPS_RF_BANDS_5_GHZ		0x02

/* 	Value of WPS Association State Attribute */
#define WPS_ASSOC_STATE_NOT_ASSOCIATED			0x00
#define WPS_ASSOC_STATE_CONNECTION_SUCCESS		0x01
#define WPS_ASSOC_STATE_CONFIGURATION_FAILURE	0x02
#define WPS_ASSOC_STATE_ASSOCIATION_FAILURE		0x03
#define WPS_ASSOC_STATE_IP_FAILURE				0x04

/* 	=====================P2P Section ===================== */
/* 	For P2P */
#define	P2POUI							0x506F9A09

/* 	P2P Attribute ID */
#define	P2P_ATTR_STATUS					0x00
#define	P2P_ATTR_MINOR_REASON_CODE		0x01
#define	P2P_ATTR_CAPABILITY				0x02
#define	P2P_ATTR_DEVICE_ID				0x03
#define	P2P_ATTR_GO_INTENT				0x04
#define	P2P_ATTR_CONF_TIMEOUT			0x05
#define	P2P_ATTR_LISTEN_CH				0x06
#define	P2P_ATTR_GROUP_BSSID				0x07
#define	P2P_ATTR_EX_LISTEN_TIMING		0x08
#define	P2P_ATTR_INTENTED_IF_ADDR		0x09
#define	P2P_ATTR_MANAGEABILITY			0x0A
#define	P2P_ATTR_CH_LIST					0x0B
#define	P2P_ATTR_NOA						0x0C
#define	P2P_ATTR_DEVICE_INFO				0x0D
#define	P2P_ATTR_GROUP_INFO				0x0E
#define	P2P_ATTR_GROUP_ID					0x0F
#define	P2P_ATTR_INTERFACE				0x10
#define	P2P_ATTR_OPERATING_CH			0x11
#define	P2P_ATTR_INVITATION_FLAGS		0x12

/* 	Value of Status Attribute */
#define	P2P_STATUS_SUCCESS						0x00
#define	P2P_STATUS_FAIL_INFO_UNAVAILABLE		0x01
#define	P2P_STATUS_FAIL_INCOMPATIBLE_PARAM		0x02
#define	P2P_STATUS_FAIL_LIMIT_REACHED			0x03
#define	P2P_STATUS_FAIL_INVALID_PARAM			0x04
#define	P2P_STATUS_FAIL_REQUEST_UNABLE			0x05
#define	P2P_STATUS_FAIL_PREVOUS_PROTO_ERR		0x06
#define	P2P_STATUS_FAIL_NO_COMMON_CH			0x07
#define	P2P_STATUS_FAIL_UNKNOWN_P2PGROUP		0x08
#define	P2P_STATUS_FAIL_BOTH_GOINTENT_15		0x09
#define	P2P_STATUS_FAIL_INCOMPATIBLE_PROVSION	0x0A
#define	P2P_STATUS_FAIL_USER_REJECT				0x0B

/* 	Value of Invitation Flags Attribute */
#define	P2P_INVITATION_FLAGS_PERSISTENT			BIT(0)

#define	DMP_P2P_DEVCAP_SUPPORT	(P2P_DEVCAP_SERVICE_DISCOVERY | \
									P2P_DEVCAP_CLIENT_DISCOVERABILITY | \
									P2P_DEVCAP_CONCURRENT_OPERATION | \
									P2P_DEVCAP_INVITATION_PROC)

#define	DMP_P2P_GRPCAP_SUPPORT	(P2P_GRPCAP_INTRABSS)

/* 	Value of Device Capability Bitmap */
#define	P2P_DEVCAP_SERVICE_DISCOVERY		BIT(0)
#define	P2P_DEVCAP_CLIENT_DISCOVERABILITY	BIT(1)
#define	P2P_DEVCAP_CONCURRENT_OPERATION	BIT(2)
#define	P2P_DEVCAP_INFRA_MANAGED			BIT(3)
#define	P2P_DEVCAP_DEVICE_LIMIT				BIT(4)
#define	P2P_DEVCAP_INVITATION_PROC			BIT(5)

/* 	Value of Group Capability Bitmap */
#define	P2P_GRPCAP_GO							BIT(0)
#define	P2P_GRPCAP_PERSISTENT_GROUP			BIT(1)
#define	P2P_GRPCAP_GROUP_LIMIT				BIT(2)
#define	P2P_GRPCAP_INTRABSS					BIT(3)
#define	P2P_GRPCAP_CROSS_CONN				BIT(4)
#define	P2P_GRPCAP_PERSISTENT_RECONN		BIT(5)
#define	P2P_GRPCAP_GROUP_FORMATION			BIT(6)

/* 	P2P Public Action Frame (Management Frame) */
#define	P2P_PUB_ACTION_ACTION				0x09

/* 	P2P Public Action Frame Type */
#define	P2P_GO_NEGO_REQ						0
#define	P2P_GO_NEGO_RESP						1
#define	P2P_GO_NEGO_CONF						2
#define	P2P_INVIT_REQ							3
#define	P2P_INVIT_RESP							4
#define	P2P_DEVDISC_REQ						5
#define	P2P_DEVDISC_RESP						6
#define	P2P_PROVISION_DISC_REQ				7
#define	P2P_PROVISION_DISC_RESP				8

/* 	P2P Action Frame Type */
#define	P2P_NOTICE_OF_ABSENCE	0
#define	P2P_PRESENCE_REQUEST		1
#define	P2P_PRESENCE_RESPONSE	2
#define	P2P_GO_DISC_REQUEST		3


#define	P2P_MAX_PERSISTENT_GROUP_NUM		10

#define	P2P_PROVISIONING_SCAN_CNT			3

#define	P2P_WILDCARD_SSID_LEN				7

#define	P2P_FINDPHASE_EX_NONE				0	/*  default value, used when: (1)p2p disabled or (2)p2p enabled but only do 1 scan phase */
#define	P2P_FINDPHASE_EX_FULL				1	/*  used when p2p enabled and want to do 1 scan phase and P2P_FINDPHASE_EX_MAX-1 find phase */
#define	P2P_FINDPHASE_EX_SOCIAL_FIRST		(P2P_FINDPHASE_EX_FULL+1)
#define	P2P_FINDPHASE_EX_MAX					4
#define	P2P_FINDPHASE_EX_SOCIAL_LAST		P2P_FINDPHASE_EX_MAX

#define	P2P_PROVISION_TIMEOUT				5000	/* 	5 seconds timeout for sending the provision discovery request */
#define	P2P_CONCURRENT_PROVISION_TIMEOUT	3000	/* 	3 seconds timeout for sending the provision discovery request under concurrent mode */
#define	P2P_GO_NEGO_TIMEOUT					5000	/* 	5 seconds timeout for receiving the group negotiation response */
#define	P2P_CONCURRENT_GO_NEGO_TIMEOUT		3000	/* 	3 seconds timeout for sending the negotiation request under concurrent mode */
#define	P2P_TX_PRESCAN_TIMEOUT				100		/* 	100ms */
#define	P2P_INVITE_TIMEOUT					5000	/* 	5 seconds timeout for sending the invitation request */
#define	P2P_CONCURRENT_INVITE_TIMEOUT		3000	/* 	3 seconds timeout for sending the invitation request under concurrent mode */
#define	P2P_RESET_SCAN_CH						25000	/* 	25 seconds timeout to reset the scan channel (based on channel plan) */
#define	P2P_MAX_INTENT						15

#define	P2P_MAX_NOA_NUM						2

/* 	WPS Configuration Method */
#define	WPS_CM_NONE							0x0000
#define	WPS_CM_LABEL							0x0004
#define	WPS_CM_DISPLYA						0x0008
#define	WPS_CM_EXTERNAL_NFC_TOKEN			0x0010
#define	WPS_CM_INTEGRATED_NFC_TOKEN		0x0020
#define	WPS_CM_NFC_INTERFACE					0x0040
#define	WPS_CM_PUSH_BUTTON					0x0080
#define	WPS_CM_KEYPAD						0x0100
#define	WPS_CM_SW_PUHS_BUTTON				0x0280
#define	WPS_CM_HW_PUHS_BUTTON				0x0480
#define	WPS_CM_SW_DISPLAY_PIN				0x2008
#define	WPS_CM_LCD_DISPLAY_PIN				0x4008

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

#define	P2P_PRIVATE_IOCTL_SET_LEN		64

/* 	=====================WFD Section ===================== */
/* 	For Wi-Fi Display */
#define	WFD_ATTR_DEVICE_INFO			0x00
#define	WFD_ATTR_ASSOC_BSSID			0x01
#define	WFD_ATTR_COUPLED_SINK_INFO	0x06
#define	WFD_ATTR_LOCAL_IP_ADDR		0x08
#define	WFD_ATTR_SESSION_INFO		0x09
#define	WFD_ATTR_ALTER_MAC			0x0a

/* 	For WFD Device Information Attribute */
#define	WFD_DEVINFO_SOURCE					0x0000
#define	WFD_DEVINFO_PSINK					0x0001
#define	WFD_DEVINFO_SSINK					0x0002
#define	WFD_DEVINFO_DUAL					0x0003

#define	WFD_DEVINFO_SESSION_AVAIL			0x0010
#define	WFD_DEVINFO_WSD						0x0040
#define	WFD_DEVINFO_PC_TDLS					0x0080
#define	WFD_DEVINFO_HDCP_SUPPORT			0x0100

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
