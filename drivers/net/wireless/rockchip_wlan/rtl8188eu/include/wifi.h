/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef _WIFI_H_
#define _WIFI_H_


#ifdef BIT
/* #error	"BIT define occurred earlier elsewhere!\n" */
#undef BIT
#endif
#define BIT(x)	(1 << (x))


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

#ifdef CONFIG_APPEND_VENDOR_IE_ENABLE
#define WLAN_MAX_VENDOR_IE_LEN 255
#define WLAN_MAX_VENDOR_IE_NUM 5
#define WIFI_BEACON_VENDOR_IE_BIT BIT(0)
#define WIFI_PROBEREQ_VENDOR_IE_BIT BIT(1)
#define WIFI_PROBERESP_VENDOR_IE_BIT BIT(2)
#define WIFI_ASSOCREQ_VENDOR_IE_BIT BIT(3)
#define WIFI_ASSOCRESP_VENDOR_IE_BIT BIT(4)
#endif

#define P80211CAPTURE_VERSION	0x80211001

/* This value is tested by WiFi 11n Test Plan 5.2.3.
 * This test verifies the WLAN NIC can update the NAV through sending the CTS with large duration. */
#define	WiFiNavUpperUs				30000	/* 30 ms */

#ifdef GREEN_HILL
#pragma pack(1)
#endif

enum WIFI_FRAME_TYPE {
	WIFI_MGT_TYPE  =	(0),
	WIFI_CTRL_TYPE =	(BIT(2)),
	WIFI_DATA_TYPE =	(BIT(3)),
	WIFI_QOS_DATA_TYPE	= (BIT(7) | BIT(3)),	/* !< QoS Data	 */
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
	WIFI_ACTION_NOACK = (BIT(7) | BIT(6) | BIT(5) | WIFI_MGT_TYPE),

	/* below is for control frame */
	WIFI_BF_REPORT_POLL = (BIT(6) | WIFI_CTRL_TYPE),
	WIFI_NDPA         = (BIT(6) | BIT(4) | WIFI_CTRL_TYPE),
	WIFI_PSPOLL         = (BIT(7) | BIT(5) | WIFI_CTRL_TYPE),
	WIFI_RTS            = (BIT(7) | BIT(5) | BIT(4) | WIFI_CTRL_TYPE),
	WIFI_CTS            = (BIT(7) | BIT(6) | WIFI_CTRL_TYPE),
	WIFI_ACK            = (BIT(7) | BIT(6) | BIT(4) | WIFI_CTRL_TYPE),
	WIFI_CFEND          = (BIT(7) | BIT(6) | BIT(5) | WIFI_CTRL_TYPE),
	WIFI_CFEND_CFACK    = (BIT(7) | BIT(6) | BIT(5) | BIT(4) | WIFI_CTRL_TYPE),

	/* below is for data frame */
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

enum WIFI_REASON_CODE	{
	_RSON_RESERVED_					= 0,
	_RSON_UNSPECIFIED_				= 1,
	_RSON_AUTH_NO_LONGER_VALID_		= 2,
	_RSON_DEAUTH_STA_LEAVING_		= 3,
	_RSON_INACTIVITY_				= 4,
	_RSON_UNABLE_HANDLE_			= 5,
	_RSON_CLS2_						= 6,
	_RSON_CLS3_						= 7,
	_RSON_DISAOC_STA_LEAVING_		= 8,
	_RSON_ASOC_NOT_AUTH_			= 9,

	/* WPA reason */
	_RSON_INVALID_IE_				= 13,
	_RSON_MIC_FAILURE_				= 14,
	_RSON_4WAY_HNDSHK_TIMEOUT_		= 15,
	_RSON_GROUP_KEY_UPDATE_TIMEOUT_	= 16,
	_RSON_DIFF_IE_					= 17,
	_RSON_MLTCST_CIPHER_NOT_VALID_	= 18,
	_RSON_UNICST_CIPHER_NOT_VALID_	= 19,
	_RSON_AKMP_NOT_VALID_			= 20,
	_RSON_UNSUPPORT_RSNE_VER_		= 21,
	_RSON_INVALID_RSNE_CAP_			= 22,
	_RSON_IEEE_802DOT1X_AUTH_FAIL_	= 23,

	/* belowing are Realtek definition */
	_RSON_PMK_NOT_AVAILABLE_		= 24,
	_RSON_TDLS_TEAR_TOOFAR_			= 25,
	_RSON_TDLS_TEAR_UN_RSN_			= 26,
};

/* Reason codes (IEEE 802.11-2007, 7.3.1.7, Table 7-22) */
#if 0
#define WLAN_REASON_UNSPECIFIED 1
#define WLAN_REASON_PREV_AUTH_NOT_VALID 2
#define WLAN_REASON_DEAUTH_LEAVING 3
#define WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY 4
#define WLAN_REASON_DISASSOC_AP_BUSY 5
#define WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA 6
#define WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA 7
#define WLAN_REASON_DISASSOC_STA_HAS_LEFT 8
#define WLAN_REASON_STA_REQ_ASSOC_WITHOUT_AUTH 9
#endif
/* IEEE 802.11h */
#define WLAN_REASON_PWR_CAPABILITY_NOT_VALID 10
#define WLAN_REASON_SUPPORTED_CHANNEL_NOT_VALID 11
#if 0
/* IEEE 802.11i */
#define WLAN_REASON_INVALID_IE 13
#define WLAN_REASON_MICHAEL_MIC_FAILURE 14
#define WLAN_REASON_4WAY_HANDSHAKE_TIMEOUT 15
#define WLAN_REASON_GROUP_KEY_UPDATE_TIMEOUT 16
#define WLAN_REASON_IE_IN_4WAY_DIFFERS 17
#define WLAN_REASON_GROUP_CIPHER_NOT_VALID 18
#define WLAN_REASON_PAIRWISE_CIPHER_NOT_VALID 19
#define WLAN_REASON_AKMP_NOT_VALID 20
#define WLAN_REASON_UNSUPPORTED_RSN_IE_VERSION 21
#define WLAN_REASON_INVALID_RSN_IE_CAPAB 22
#define WLAN_REASON_IEEE_802_1X_AUTH_FAILED 23
#define WLAN_REASON_CIPHER_SUITE_REJECTED 24
#endif

enum WIFI_STATUS_CODE {
	_STATS_SUCCESSFUL_			= 0,
	_STATS_FAILURE_				= 1,
	_STATS_SEC_DISABLED_			= 5,
	_STATS_NOT_IN_SAME_BSS_		= 7,
	_STATS_CAP_FAIL_			= 10,
	_STATS_NO_ASOC_				= 11,
	_STATS_OTHER_				= 12,
	_STATS_NO_SUPP_ALG_			= 13,
	_STATS_OUT_OF_AUTH_SEQ_		= 14,
	_STATS_CHALLENGE_FAIL_		= 15,
	_STATS_AUTH_TIMEOUT_		= 16,
	_STATS_UNABLE_HANDLE_STA_	= 17,
	_STATS_RATE_FAIL_			= 18,
	_STATS_REFUSED_TEMPORARILY_ = 30,
	_STATS_DECLINE_REQ_			= 37,
	_STATS_INVALID_PARAMETERS_	= 38,
	_STATS_INVALID_RSNIE_			= 72,
};

/* Status codes (IEEE 802.11-2007, 7.3.1.9, Table 7-23) */
#if 0
#define WLAN_STATUS_SUCCESS 0
#define WLAN_STATUS_UNSPECIFIED_FAILURE 1
#define WLAN_STATUS_CAPS_UNSUPPORTED 10
#define WLAN_STATUS_REASSOC_NO_ASSOC 11
#define WLAN_STATUS_ASSOC_DENIED_UNSPEC 12
#define WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG 13
#define WLAN_STATUS_UNKNOWN_AUTH_TRANSACTION 14
#define WLAN_STATUS_CHALLENGE_FAIL 15
#define WLAN_STATUS_AUTH_TIMEOUT 16
#define WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA 17
#define WLAN_STATUS_ASSOC_DENIED_RATES 18
#endif
/* entended */
/* IEEE 802.11b */
#define WLAN_STATUS_ASSOC_DENIED_NOSHORT 19
#define WLAN_STATUS_ASSOC_DENIED_NOPBCC 20
#define WLAN_STATUS_ASSOC_DENIED_NOAGILITY 21
/* IEEE 802.11h */
#define WLAN_STATUS_SPEC_MGMT_REQUIRED 22
#define WLAN_STATUS_PWR_CAPABILITY_NOT_VALID 23
#define WLAN_STATUS_SUPPORTED_CHANNEL_NOT_VALID 24
/* IEEE 802.11g */
#define WLAN_STATUS_ASSOC_DENIED_NO_SHORT_SLOT_TIME 25
#define WLAN_STATUS_ASSOC_DENIED_NO_ER_PBCC 26
#define WLAN_STATUS_ASSOC_DENIED_NO_DSSS_OFDM 27
/* IEEE 802.11w */
#define WLAN_STATUS_ASSOC_REJECTED_TEMPORARILY 30
#define WLAN_STATUS_ROBUST_MGMT_FRAME_POLICY_VIOLATION 31
/* IEEE 802.11i */
#define WLAN_STATUS_INVALID_IE 40
#define WLAN_STATUS_GROUP_CIPHER_NOT_VALID 41
#define WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID 42
#define WLAN_STATUS_AKMP_NOT_VALID 43
#define WLAN_STATUS_UNSUPPORTED_RSN_IE_VERSION 44
#define WLAN_STATUS_INVALID_RSN_IE_CAPAB 45
#define WLAN_STATUS_CIPHER_REJECTED_PER_POLICY 46
#define WLAN_STATUS_TS_NOT_CREATED 47
#define WLAN_STATUS_DIRECT_LINK_NOT_ALLOWED 48
#define WLAN_STATUS_DEST_STA_NOT_PRESENT 49
#define WLAN_STATUS_DEST_STA_NOT_QOS_STA 50
#define WLAN_STATUS_ASSOC_DENIED_LISTEN_INT_TOO_LARGE 51
/* IEEE 802.11r */
#define WLAN_STATUS_INVALID_FT_ACTION_FRAME_COUNT 52
#define WLAN_STATUS_INVALID_PMKID 53
#define WLAN_STATUS_INVALID_MDIE 54
#define WLAN_STATUS_INVALID_FTIE 55


enum WIFI_REG_DOMAIN {
	DOMAIN_FCC		= 1,
	DOMAIN_IC		= 2,
	DOMAIN_ETSI		= 3,
	DOMAIN_SPAIN	= 4,
	DOMAIN_FRANCE	= 5,
	DOMAIN_MKK		= 6,
	DOMAIN_ISRAEL	= 7,
	DOMAIN_MKK1		= 8,
	DOMAIN_MKK2		= 9,
	DOMAIN_MKK3		= 10,
	DOMAIN_MAX
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
	do	{	\
		*(unsigned short *)(pbuf) |= cpu_to_le16(_TO_DS_); \
	} while (0)

#define GetToDs(pbuf)	(((*(unsigned short *)(pbuf)) & le16_to_cpu(_TO_DS_)) != 0)

#define ClearToDs(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) &= (~cpu_to_le16(_TO_DS_)); \
	} while (0)

#define SetFrDs(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) |= cpu_to_le16(_FROM_DS_); \
	} while (0)

#define GetFrDs(pbuf)	(((*(unsigned short *)(pbuf)) & le16_to_cpu(_FROM_DS_)) != 0)

#define ClearFrDs(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) &= (~cpu_to_le16(_FROM_DS_)); \
	} while (0)

#define get_tofr_ds(pframe)	((GetToDs(pframe) << 1) | GetFrDs(pframe))


#define SetMFrag(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) |= cpu_to_le16(_MORE_FRAG_); \
	} while (0)

#define GetMFrag(pbuf)	(((*(unsigned short *)(pbuf)) & le16_to_cpu(_MORE_FRAG_)) != 0)

#define ClearMFrag(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) &= (~cpu_to_le16(_MORE_FRAG_)); \
	} while (0)

#define SetRetry(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) |= cpu_to_le16(_RETRY_); \
	} while (0)

#define GetRetry(pbuf)	(((*(unsigned short *)(pbuf)) & le16_to_cpu(_RETRY_)) != 0)

#define ClearRetry(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) &= (~cpu_to_le16(_RETRY_)); \
	} while (0)

#define SetPwrMgt(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) |= cpu_to_le16(_PWRMGT_); \
	} while (0)

#define GetPwrMgt(pbuf)	(((*(unsigned short *)(pbuf)) & le16_to_cpu(_PWRMGT_)) != 0)

#define ClearPwrMgt(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) &= (~cpu_to_le16(_PWRMGT_)); \
	} while (0)

#define SetMData(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) |= cpu_to_le16(_MORE_DATA_); \
	} while (0)

#define GetMData(pbuf)	(((*(unsigned short *)(pbuf)) & le16_to_cpu(_MORE_DATA_)) != 0)

#define ClearMData(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) &= (~cpu_to_le16(_MORE_DATA_)); \
	} while (0)

#define SetPrivacy(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) |= cpu_to_le16(_PRIVACY_); \
	} while (0)

#define GetPrivacy(pbuf)	(((*(unsigned short *)(pbuf)) & le16_to_cpu(_PRIVACY_)) != 0)

#define ClearPrivacy(pbuf)	\
	do	{	\
		*(unsigned short *)(pbuf) &= (~cpu_to_le16(_PRIVACY_)); \
	} while (0)


#define GetOrder(pbuf)	(((*(unsigned short *)(pbuf)) & le16_to_cpu(_ORDER_)) != 0)

#define GetFrameType(pbuf)	(le16_to_cpu(*(unsigned short *)(pbuf)) & (BIT(3) | BIT(2)))

#define SetFrameType(pbuf, type)	\
	do {	\
		*(unsigned short *)(pbuf) &= __constant_cpu_to_le16(~(BIT(3) | BIT(2))); \
		*(unsigned short *)(pbuf) |= __constant_cpu_to_le16(type); \
	} while (0)

#define get_frame_sub_type(pbuf)	(cpu_to_le16(*(unsigned short *)(pbuf)) & (BIT(7) | BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2)))


#define set_frame_sub_type(pbuf, type) \
	do {    \
		*(unsigned short *)(pbuf) &= cpu_to_le16(~(BIT(7) | BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2))); \
		*(unsigned short *)(pbuf) |= cpu_to_le16(type); \
	} while (0)


#define GetSequence(pbuf)	(cpu_to_le16(*(unsigned short *)((SIZE_PTR)(pbuf) + 22)) >> 4)

#define GetFragNum(pbuf)	(cpu_to_le16(*(unsigned short *)((SIZE_PTR)(pbuf) + 22)) & 0x0f)

#define GetTupleCache(pbuf)	(cpu_to_le16(*(unsigned short *)((SIZE_PTR)(pbuf) + 22)))

#define SetFragNum(pbuf, num) \
	do {    \
		*(unsigned short *)((SIZE_PTR)(pbuf) + 22) = \
			((*(unsigned short *)((SIZE_PTR)(pbuf) + 22)) & le16_to_cpu(~(0x000f))) | \
				cpu_to_le16(0x0f & (num));     \
	} while (0)

#define SetSeqNum(pbuf, num) \
	do {    \
		*(unsigned short *)((SIZE_PTR)(pbuf) + 22) = \
			((*(unsigned short *)((SIZE_PTR)(pbuf) + 22)) & le16_to_cpu((unsigned short)~0xfff0)) | \
			le16_to_cpu((unsigned short)(0xfff0 & (num << 4))); \
	} while (0)

#define set_duration(pbuf, dur) \
	do {    \
		*(unsigned short *)((SIZE_PTR)(pbuf) + 2) = cpu_to_le16(0xffff & (dur)); \
	} while (0)


#define SetPriority(pbuf, tid)	\
	do	{	\
		*(unsigned short *)(pbuf) |= cpu_to_le16(tid & 0xf); \
	} while (0)

#define GetPriority(pbuf)	((le16_to_cpu(*(unsigned short *)(pbuf))) & 0xf)

#define SetEOSP(pbuf, eosp)	\
	do	{	\
		*(unsigned short *)(pbuf) |= cpu_to_le16((eosp & 1) << 4); \
	} while (0)

#define SetAckpolicy(pbuf, ack)	\
	do	{	\
		*(unsigned short *)(pbuf) |= cpu_to_le16((ack & 3) << 5); \
	} while (0)

#define GetAckpolicy(pbuf) (((le16_to_cpu(*(unsigned short *)pbuf)) >> 5) & 0x3)

#define GetAMsdu(pbuf) (((le16_to_cpu(*(unsigned short *)pbuf)) >> 7) & 0x1)

#define SetAMsdu(pbuf, amsdu)	\
	do	{	\
		*(unsigned short *)(pbuf) |= cpu_to_le16((amsdu & 1) << 7); \
	} while (0)

#define GetAid(pbuf)	(cpu_to_le16(*(unsigned short *)((SIZE_PTR)(pbuf) + 2)) & 0x3fff)

#define GetTid(pbuf)	(cpu_to_le16(*(unsigned short *)((SIZE_PTR)(pbuf) + (((GetToDs(pbuf)<<1) | GetFrDs(pbuf)) == 3 ? 30 : 24))) & 0x000f)

#define GetAddr1Ptr(pbuf)	((unsigned char *)((SIZE_PTR)(pbuf) + 4))

#define get_addr2_ptr(pbuf)	((unsigned char *)((SIZE_PTR)(pbuf) + 10))

#define GetAddr3Ptr(pbuf)	((unsigned char *)((SIZE_PTR)(pbuf) + 16))

#define GetAddr4Ptr(pbuf)	((unsigned char *)((SIZE_PTR)(pbuf) + 24))


#define MacAddr_isBcst(addr) \
	(\
	 ((addr[0] == 0xff) && (addr[1] == 0xff) && \
	  (addr[2] == 0xff) && (addr[3] == 0xff) && \
	  (addr[4] == 0xff) && (addr[5] == 0xff)) ? _TRUE : _FALSE \
	)

__inline static int IS_MCAST(unsigned char *da)
{
	if ((*da) & 0x01)
		return _TRUE;
	else
		return _FALSE;
}

__inline static unsigned char *get_ra(unsigned char *pframe)
{
	unsigned char	*ra;
	ra = GetAddr1Ptr(pframe);
	return ra;
}
__inline static unsigned char *get_ta(unsigned char *pframe)
{
	unsigned char	*ta;
	ta = get_addr2_ptr(pframe);
	return ta;
}

__inline static unsigned char *get_da(unsigned char *pframe)
{
	unsigned char	*da;
	unsigned int	to_fr_ds	= (GetToDs(pframe) << 1) | GetFrDs(pframe);

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


__inline static unsigned char *get_sa(unsigned char *pframe)
{
	unsigned char	*sa;
	unsigned int	to_fr_ds	= (GetToDs(pframe) << 1) | GetFrDs(pframe);

	switch (to_fr_ds) {
	case 0x00:	/* ToDs=0, FromDs=0 */
		sa = get_addr2_ptr(pframe);
		break;
	case 0x01:	/* ToDs=0, FromDs=1 */
		sa = GetAddr3Ptr(pframe);
		break;
	case 0x02:	/* ToDs=1, FromDs=0 */
		sa = get_addr2_ptr(pframe);
		break;
	default:	/* ToDs=1, FromDs=1 */
		sa = GetAddr4Ptr(pframe);
		break;
	}

	return sa;
}

__inline static unsigned char *get_hdr_bssid(unsigned char *pframe)
{
	unsigned char	*sa = NULL;
	unsigned int	to_fr_ds	= (GetToDs(pframe) << 1) | GetFrDs(pframe);

	switch (to_fr_ds) {
	case 0x00:	/* ToDs=0, FromDs=0 */
		sa = GetAddr3Ptr(pframe);
		break;
	case 0x01:	/* ToDs=0, FromDs=1 */
		sa = get_addr2_ptr(pframe);
		break;
	case 0x02:	/* ToDs=1, FromDs=0 */
		sa = GetAddr1Ptr(pframe);
		break;
	case 0x03:	/* ToDs=1, FromDs=1 */
		sa = GetAddr1Ptr(pframe);
		break;
	}

	return sa;
}


__inline static int IsFrameTypeCtrl(unsigned char *pframe)
{
	if (WIFI_CTRL_TYPE == GetFrameType(pframe))
		return _TRUE;
	else
		return _FALSE;
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

#define _SIFSTIME_				((priv->pmib->dot11BssType.net_work_type&WIRELESS_11A) ? 16 : 10)
#define _ACKCTSLNG_				14	/* 14 bytes long, including crclng */
#define _CRCLNG_				4

#define _ASOCREQ_IE_OFFSET_		4	/* excluding wlan_hdr */
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

#define _SSID_IE_				0
#define _SUPPORTEDRATES_IE_	1
#define _DSSET_IE_				3
#define _TIM_IE_					5
#define _IBSS_PARA_IE_			6
#define _COUNTRY_IE_			7
#define _CHLGETXT_IE_			16
#define _SUPPORTED_CH_IE_		36
#define _CH_SWTICH_ANNOUNCE_	37	/* Secondary Channel Offset */
#define _RSN_IE_2_				48
#define _SSN_IE_1_					221
#define _ERPINFO_IE_			42
#define _EXT_SUPPORTEDRATES_IE_	50

#define _HT_CAPABILITY_IE_			45
#define _MDIE_						54
#define _FTIE_						55
#define _TIMEOUT_ITVL_IE_			56
#define _SRC_IE_				59
#define _HT_EXTRA_INFO_IE_			61
#define _HT_ADD_INFO_IE_			61 /* _HT_EXTRA_INFO_IE_ */
#define _WAPI_IE_					68


/* #define EID_BSSCoexistence			72 */ /* 20/40 BSS Coexistence
 * #define EID_BSSIntolerantChlReport	73 */
#define _RIC_Descriptor_IE_			75
#ifdef CONFIG_IEEE80211W
#define _MME_IE_					76 /* 802.11w Management MIC element */
#endif /* CONFIG_IEEE80211W */
#define _LINK_ID_IE_					101
#define _CH_SWITCH_TIMING_		104
#define _PTI_BUFFER_STATUS_		106
#define _EXT_CAP_IE_				127
#define _VENDOR_SPECIFIC_IE_		221

#define	_RESERVED47_				47

typedef	enum _ELEMENT_ID {
	EID_SsId					= 0, /* service set identifier (0:32) */
	EID_SupRates				= 1, /* supported rates (1:8) */
	EID_FHParms				= 2, /* FH parameter set (5) */
	EID_DSParms				= 3, /* DS parameter set (1) */
	EID_CFParms				= 4, /* CF parameter set (6) */
	EID_Tim						= 5, /* Traffic Information Map (4:254) */
	EID_IbssParms				= 6, /* IBSS parameter set (2) */
	EID_Country					= 7, /* */

	/* Form 7.3.2: Information elements in 802.11E/D13.0, page 46. */
	EID_QBSSLoad				= 11,
	EID_EDCAParms				= 12,
	EID_TSpec					= 13,
	EID_TClass					= 14,
	EID_Schedule				= 15,
	/*  */

	EID_Ctext					= 16, /* challenge text*/
	EID_POWER_CONSTRAINT		= 32, /* Power Constraint*/

	/* vivi for WIFITest, 802.11h AP, 20100427 */
	/* 2010/12/26 MH The definition we can declare always!! */
	EID_PowerCap				= 33,
	EID_SupportedChannels		= 36,
	EID_ChlSwitchAnnounce		= 37,

	EID_MeasureRequest			= 38, /* Measurement Request */
	EID_MeasureReport			= 39, /* Measurement Report */

	EID_ERPInfo				= 42,

	/* Form 7.3.2: Information elements in 802.11E/D13.0, page 46. */
	EID_TSDelay				= 43,
	EID_TCLASProc				= 44,
	EID_HTCapability			= 45,
	EID_QoSCap					= 46,
	/*  */

	EID_WPA2					= 48,
	EID_ExtSupRates			= 50,

	EID_FTIE					= 55, /* Defined in 802.11r */
	EID_Timeout				= 56, /* Defined in 802.11r */

	EID_SupRegulatory			= 59, /* Supported Requlatory Classes 802.11y */
	EID_HTInfo					= 61,
	EID_SecondaryChnlOffset		= 62,

	EID_BSSCoexistence			= 72, /* 20/40 BSS Coexistence */
	EID_BSSIntolerantChlReport	= 73,
	EID_OBSS					= 74, /* Overlapping BSS Scan Parameters */

	EID_LinkIdentifier			= 101, /* Defined in 802.11z */
	EID_WakeupSchedule		= 102, /* Defined in 802.11z */
	EID_ChnlSwitchTimeing		= 104, /* Defined in 802.11z */
	EID_PTIControl				= 105, /* Defined in 802.11z */
	EID_PUBufferStatus			= 106, /* Defined in 802.11z */

	EID_EXTCapability			= 127, /* Extended Capabilities */
	/* From S19:Aironet IE and S21:AP IP address IE in CCX v1.13, p16 and p18. */
	EID_Aironet					= 133, /* 0x85: Aironet Element for Cisco CCX */
	EID_CiscoIP					= 149, /* 0x95: IP Address IE for Cisco CCX */

	EID_CellPwr					= 150, /* 0x96: Cell Power Limit IE. Ref. 0x96. */

	EID_CCKM					= 156,

	EID_Vendor					= 221, /* 0xDD: Vendor Specific */

	EID_WAPI					= 68,
	EID_VHTCapability 			= 191, /* Based on 802.11ac D2.0 */
	EID_VHTOperation 			= 192, /* Based on 802.11ac D2.0 */
	EID_AID						= 197, /* Based on 802.11ac D4.0 */
	EID_OpModeNotification		= 199, /* Based on 802.11ac D3.0 */
} ELEMENT_ID, *PELEMENT_ID;

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

#define cap_ESS BIT(0)
#define cap_IBSS BIT(1)
#define cap_CFPollable BIT(2)
#define cap_CFRequest BIT(3)
#define cap_Privacy BIT(4)
#define cap_ShortPremble BIT(5)
#define cap_PBCC	BIT(6)
#define cap_ChAgility	BIT(7)
#define cap_SpecMgmt	BIT(8)
#define cap_QoS	BIT(9)
#define cap_ShortSlot	BIT(10)

/*-----------------------------------------------------------------------------
				Below is the definition for 802.11i / 802.1x
------------------------------------------------------------------------------*/
#define _IEEE8021X_MGT_			1		/* WPA */
#define _IEEE8021X_PSK_			2		/* WPA with pre-shared key */

#if 0
#define _NO_PRIVACY_			0
#define _WEP_40_PRIVACY_		1
#define _TKIP_PRIVACY_			2
#define _WRAP_PRIVACY_			3
#define _CCMP_PRIVACY_			4
#define _WEP_104_PRIVACY_		5
#define _WEP_WPA_MIXED_PRIVACY_ 6	/*  WEP + WPA */
#endif

#ifdef CONFIG_IEEE80211W
#define _MME_IE_LENGTH_  18
#endif /* CONFIG_IEEE80211W */
/*-----------------------------------------------------------------------------
				Below is the definition for WMM
------------------------------------------------------------------------------*/
#define _WMM_IE_Length_				7  /* for WMM STA */
#define _WMM_Para_Element_Length_		24


/*-----------------------------------------------------------------------------
				Below is the definition for 802.11n
------------------------------------------------------------------------------*/

/* #ifdef CONFIG_80211N_HT */

#define set_order_bit(pbuf)	\
		do	{	\
			*(unsigned short *)(pbuf) |= cpu_to_le16(_ORDER_); \
		} while (0)



#define GetOrderBit(pbuf)	(((*(unsigned short *)(pbuf)) & le16_to_cpu(_ORDER_)) != 0)

#define ACT_CAT_VENDOR				0x7F/* 127 */

/**
 * struct rtw_ieee80211_bar - HT Block Ack Request
 *
 * This structure refers to "HT BlockAckReq" as
 * described in 802.11n draft section 7.2.1.7.1
 */
#if defined(PLATFORM_LINUX) || defined(CONFIG_RTL8712FW)
struct rtw_ieee80211_bar {
	unsigned short frame_control;
	unsigned short duration;
	unsigned char ra[6];
	unsigned char ta[6];
	unsigned short control;
	unsigned short start_seq_num;
} __attribute__((packed));
#endif

/* 802.11 BAR control masks */
#define IEEE80211_BAR_CTRL_ACK_POLICY_NORMAL     0x0000
#define IEEE80211_BAR_CTRL_CBMTID_COMPRESSED_BA  0x0004


#if defined(PLATFORM_LINUX) || defined(CONFIG_RTL8712FW) || defined(PLATFORM_FREEBSD)



/**
* struct rtw_ieee80211_ht_cap - HT capabilities
*
* This structure refers to "HT capabilities element" as
* described in 802.11n draft section 7.3.2.52
*/

struct rtw_ieee80211_ht_cap {
	unsigned short	cap_info;
	unsigned char	ampdu_params_info;
	unsigned char	supp_mcs_set[16];
	unsigned short	extended_ht_cap_info;
	unsigned int		tx_BF_cap_info;
	unsigned char	       antenna_selection_info;
} __attribute__((packed));

/**
 * struct rtw_ieee80211_ht_cap - HT additional information
 *
 * This structure refers to "HT information element" as
 * described in 802.11n draft section 7.3.2.53
 */
struct ieee80211_ht_addt_info {
	unsigned char	control_chan;
	unsigned char		ht_param;
	unsigned short	operation_mode;
	unsigned short	stbc_param;
	unsigned char		basic_set[16];
} __attribute__((packed));


struct HT_caps_element {
	union {
		struct {
			unsigned short	HT_caps_info;
			unsigned char	AMPDU_para;
			unsigned char	MCS_rate[16];
			unsigned short	HT_ext_caps;
			unsigned int	Beamforming_caps;
			unsigned char	ASEL_caps;
		} HT_cap_element;
		unsigned char HT_cap[26];
	} u;
} __attribute__((packed));

struct HT_info_element {
	unsigned char	primary_channel;
	unsigned char	infos[5];
	unsigned char	MCS_rate[16];
}  __attribute__((packed));

struct AC_param {
	unsigned char		ACI_AIFSN;
	unsigned char		CW;
	unsigned short	TXOP_limit;
}  __attribute__((packed));

struct WMM_para_element {
	unsigned char		QoS_info;
	unsigned char		reserved;
	struct AC_param	ac_param[4];
}  __attribute__((packed));

struct ADDBA_request {
	unsigned char		dialog_token;
	unsigned short	BA_para_set;
	unsigned short	BA_timeout_value;
	unsigned short	BA_starting_seqctrl;
}  __attribute__((packed));



#endif


#ifdef PLATFORM_WINDOWS

#pragma pack(1)

struct rtw_ieee80211_ht_cap {
	unsigned short	cap_info;
	unsigned char	ampdu_params_info;
	unsigned char	supp_mcs_set[16];
	unsigned short	extended_ht_cap_info;
	unsigned int		tx_BF_cap_info;
	unsigned char	       antenna_selection_info;
};


struct ieee80211_ht_addt_info {
	unsigned char	control_chan;
	unsigned char		ht_param;
	unsigned short	operation_mode;
	unsigned short	stbc_param;
	unsigned char		basic_set[16];
};

struct HT_caps_element {
	union {
		struct {
			unsigned short	HT_caps_info;
			unsigned char	AMPDU_para;
			unsigned char	MCS_rate[16];
			unsigned short	HT_ext_caps;
			unsigned int	Beamforming_caps;
			unsigned char	ASEL_caps;
		} HT_cap_element;
		unsigned char HT_cap[26];
	};
};

struct HT_info_element {
	unsigned char	primary_channel;
	unsigned char	infos[5];
	unsigned char	MCS_rate[16];
};

struct AC_param {
	unsigned char		ACI_AIFSN;
	unsigned char		CW;
	unsigned short	TXOP_limit;
};

struct WMM_para_element {
	unsigned char		QoS_info;
	unsigned char		reserved;
	struct AC_param	ac_param[4];
};

struct ADDBA_request {
	unsigned char		dialog_token;
	unsigned short	BA_para_set;
	unsigned short	BA_timeout_value;
	unsigned short	BA_starting_seqctrl;
};


#pragma pack()

#endif

typedef enum _HT_CAP_AMPDU_FACTOR {
	MAX_AMPDU_FACTOR_8K		= 0,
	MAX_AMPDU_FACTOR_16K	= 1,
	MAX_AMPDU_FACTOR_32K	= 2,
	MAX_AMPDU_FACTOR_64K	= 3,
} HT_CAP_AMPDU_FACTOR;


typedef enum _HT_CAP_AMPDU_DENSITY {
	AMPDU_DENSITY_VALUE_0 = 0 , /* For no restriction */
	AMPDU_DENSITY_VALUE_1 = 1 , /* For 1/4 us */
	AMPDU_DENSITY_VALUE_2 = 2 , /* For 1/2 us */
	AMPDU_DENSITY_VALUE_3 = 3 , /* For 1 us */
	AMPDU_DENSITY_VALUE_4 = 4 , /* For 2 us */
	AMPDU_DENSITY_VALUE_5 = 5 , /* For 4 us */
	AMPDU_DENSITY_VALUE_6 = 6 , /* For 8 us */
	AMPDU_DENSITY_VALUE_7 = 7 , /* For 16 us */
} HT_CAP_AMPDU_DENSITY;

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
#define RTW_IEEE80211_HT_CAP_40MHZ_INTOLERANT	((u16) BIT(14))
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
#define RTW_IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK 0xFFC0
#define IEEE80211_DELBA_PARAM_TID_MASK 0xF000
#define IEEE80211_DELBA_PARAM_INITIATOR_MASK 0x0800

/*
 * A-PMDU buffer sizes
 * According to IEEE802.11n spec size varies from 8K to 64K (in powers of 2)
 */
#define IEEE80211_MIN_AMPDU_BUF 0x8
#define IEEE80211_MAX_AMPDU_BUF 0x40


/* Spatial Multiplexing Power Save Modes */
#define WLAN_HT_CAP_SM_PS_STATIC		0
#define WLAN_HT_CAP_SM_PS_DYNAMIC	1
#define WLAN_HT_CAP_SM_PS_INVALID	2
#define WLAN_HT_CAP_SM_PS_DISABLED	3


#define OP_MODE_PURE                    0
#define OP_MODE_MAY_BE_LEGACY_STAS      1
#define OP_MODE_20MHZ_HT_STA_ASSOCED    2
#define OP_MODE_MIXED                   3

#define HT_INFO_HT_PARAM_SECONDARY_CHNL_OFF_MASK	((u8) BIT(0) | BIT(1))
#define HT_INFO_HT_PARAM_SECONDARY_CHNL_ABOVE		((u8) BIT(0))
#define HT_INFO_HT_PARAM_SECONDARY_CHNL_BELOW		((u8) BIT(0) | BIT(1))
#define HT_INFO_HT_PARAM_REC_TRANS_CHNL_WIDTH		((u8) BIT(2))
#define HT_INFO_HT_PARAM_RIFS_MODE			((u8) BIT(3))
#define HT_INFO_HT_PARAM_CTRL_ACCESS_ONLY		((u8) BIT(4))
#define HT_INFO_HT_PARAM_SRV_INTERVAL_GRANULARITY	((u8) BIT(5))

#define HT_INFO_OPERATION_MODE_OP_MODE_MASK	\
	((u16) (0x0001 | 0x0002))
#define HT_INFO_OPERATION_MODE_OP_MODE_OFFSET		0
#define HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT	((u8) BIT(2))
#define HT_INFO_OPERATION_MODE_TRANSMIT_BURST_LIMIT	((u8) BIT(3))
#define HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT	((u8) BIT(4))

#define HT_INFO_STBC_PARAM_DUAL_BEACON			((u16) BIT(6))
#define HT_INFO_STBC_PARAM_DUAL_STBC_PROTECT		((u16) BIT(7))
#define HT_INFO_STBC_PARAM_SECONDARY_BCN		((u16) BIT(8))
#define HT_INFO_STBC_PARAM_LSIG_TXOP_PROTECT_ALLOWED	((u16) BIT(9))
#define HT_INFO_STBC_PARAM_PCO_ACTIVE			((u16) BIT(10))
#define HT_INFO_STBC_PARAM_PCO_PHASE			((u16) BIT(11))



/* #endif */

/*	===============WPS Section=============== */
/*	For WPSv1.0 */
#define WPSOUI							0x0050f204
/*	WPS attribute ID */
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

/*	Value of WPS attribute "WPS_ATTR_DEVICE_NAME */
#define WPS_MAX_DEVICE_NAME_LEN		32

/*	Value of WPS Request Type Attribute */
#define WPS_REQ_TYPE_ENROLLEE_INFO_ONLY			0x00
#define WPS_REQ_TYPE_ENROLLEE_OPEN_8021X		0x01
#define WPS_REQ_TYPE_REGISTRAR					0x02
#define WPS_REQ_TYPE_WLAN_MANAGER_REGISTRAR	0x03

/*	Value of WPS Response Type Attribute */
#define WPS_RESPONSE_TYPE_INFO_ONLY	0x00
#define WPS_RESPONSE_TYPE_8021X		0x01
#define WPS_RESPONSE_TYPE_REGISTRAR	0x02
#define WPS_RESPONSE_TYPE_AP			0x03

/*	Value of WPS WiFi Simple Configuration State Attribute */
#define WPS_WSC_STATE_NOT_CONFIG	0x01
#define WPS_WSC_STATE_CONFIG			0x02

/*	Value of WPS Version Attribute */
#define WPS_VERSION_1					0x10

/*	Value of WPS Configuration Method Attribute */
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

/*	Value of Category ID of WPS Primary Device Type Attribute */
#define WPS_PDT_CID_DISPLAYS			0x0007
#define WPS_PDT_CID_MULIT_MEDIA		0x0008
#define WPS_PDT_CID_RTK_WIDI			WPS_PDT_CID_MULIT_MEDIA

/*	Value of Sub Category ID of WPS Primary Device Type Attribute */
#define WPS_PDT_SCID_MEDIA_SERVER	0x0005
#define WPS_PDT_SCID_RTK_DMP			WPS_PDT_SCID_MEDIA_SERVER

/*	Value of Device Password ID */
#define WPS_DPID_PIN					0x0000
#define WPS_DPID_USER_SPEC			0x0001
#define WPS_DPID_MACHINE_SPEC			0x0002
#define WPS_DPID_REKEY					0x0003
#define WPS_DPID_PBC					0x0004
#define WPS_DPID_REGISTRAR_SPEC		0x0005

/*	Value of WPS RF Bands Attribute */
#define WPS_RF_BANDS_2_4_GHZ		0x01
#define WPS_RF_BANDS_5_GHZ		0x02

/*	Value of WPS Association State Attribute */
#define WPS_ASSOC_STATE_NOT_ASSOCIATED			0x00
#define WPS_ASSOC_STATE_CONNECTION_SUCCESS		0x01
#define WPS_ASSOC_STATE_CONFIGURATION_FAILURE	0x02
#define WPS_ASSOC_STATE_ASSOCIATION_FAILURE		0x03
#define WPS_ASSOC_STATE_IP_FAILURE				0x04

/*	=====================P2P Section===================== */
/*	For P2P */
#define	P2POUI							0x506F9A09

/*	P2P Attribute ID */
#define	P2P_ATTR_STATUS					0x00
#define	P2P_ATTR_MINOR_REASON_CODE		0x01
#define	P2P_ATTR_CAPABILITY				0x02
#define	P2P_ATTR_DEVICE_ID				0x03
#define	P2P_ATTR_GO_INTENT				0x04
#define	P2P_ATTR_CONF_TIMEOUT			0x05
#define	P2P_ATTR_LISTEN_CH				0x06
#define	P2P_ATTR_GROUP_BSSID				0x07
#define	P2P_ATTR_EX_LISTEN_TIMING		0x08
#define	P2P_ATTR_INTENDED_IF_ADDR		0x09
#define	P2P_ATTR_MANAGEABILITY			0x0A
#define	P2P_ATTR_CH_LIST					0x0B
#define	P2P_ATTR_NOA						0x0C
#define	P2P_ATTR_DEVICE_INFO				0x0D
#define	P2P_ATTR_GROUP_INFO				0x0E
#define	P2P_ATTR_GROUP_ID					0x0F
#define	P2P_ATTR_INTERFACE				0x10
#define	P2P_ATTR_OPERATING_CH			0x11
#define	P2P_ATTR_INVITATION_FLAGS		0x12

/*	Value of Status Attribute */
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

/*	Value of Inviation Flags Attribute */
#define	P2P_INVITATION_FLAGS_PERSISTENT			BIT(0)

#define	DMP_P2P_DEVCAP_SUPPORT	(P2P_DEVCAP_SERVICE_DISCOVERY | \
				 P2P_DEVCAP_CLIENT_DISCOVERABILITY | \
				 P2P_DEVCAP_CONCURRENT_OPERATION | \
				 P2P_DEVCAP_INVITATION_PROC)

#define	DMP_P2P_GRPCAP_SUPPORT	(P2P_GRPCAP_INTRABSS)

/*	Value of Device Capability Bitmap */
#define	P2P_DEVCAP_SERVICE_DISCOVERY		BIT(0)
#define	P2P_DEVCAP_CLIENT_DISCOVERABILITY	BIT(1)
#define	P2P_DEVCAP_CONCURRENT_OPERATION	BIT(2)
#define	P2P_DEVCAP_INFRA_MANAGED			BIT(3)
#define	P2P_DEVCAP_DEVICE_LIMIT				BIT(4)
#define	P2P_DEVCAP_INVITATION_PROC			BIT(5)

/*	Value of Group Capability Bitmap */
#define	P2P_GRPCAP_GO							BIT(0)
#define	P2P_GRPCAP_PERSISTENT_GROUP			BIT(1)
#define	P2P_GRPCAP_GROUP_LIMIT				BIT(2)
#define	P2P_GRPCAP_INTRABSS					BIT(3)
#define	P2P_GRPCAP_CROSS_CONN				BIT(4)
#define	P2P_GRPCAP_PERSISTENT_RECONN		BIT(5)
#define	P2P_GRPCAP_GROUP_FORMATION			BIT(6)

/*	P2P Public Action Frame ( Management Frame ) */
#define	P2P_PUB_ACTION_ACTION				0x09

/*	P2P Public Action Frame Type */
#define	P2P_GO_NEGO_REQ						0
#define	P2P_GO_NEGO_RESP						1
#define	P2P_GO_NEGO_CONF						2
#define	P2P_INVIT_REQ							3
#define	P2P_INVIT_RESP							4
#define	P2P_DEVDISC_REQ						5
#define	P2P_DEVDISC_RESP						6
#define	P2P_PROVISION_DISC_REQ				7
#define	P2P_PROVISION_DISC_RESP				8

/*	P2P Action Frame Type */
#define	P2P_NOTICE_OF_ABSENCE	0
#define	P2P_PRESENCE_REQUEST		1
#define	P2P_PRESENCE_RESPONSE	2
#define	P2P_GO_DISC_REQUEST		3


#define	P2P_MAX_PERSISTENT_GROUP_NUM		10

#define	P2P_PROVISIONING_SCAN_CNT			3

#define	P2P_WILDCARD_SSID_LEN				7

#define	P2P_FINDPHASE_EX_NONE				0	/* default value, used when: (1)p2p disabed or (2)p2p enabled but only do 1 scan phase */
#define	P2P_FINDPHASE_EX_FULL				1	/* used when p2p enabled and want to do 1 scan phase and P2P_FINDPHASE_EX_MAX-1 find phase */
#define	P2P_FINDPHASE_EX_SOCIAL_FIRST		(P2P_FINDPHASE_EX_FULL+1)
#define	P2P_FINDPHASE_EX_MAX					4
#define	P2P_FINDPHASE_EX_SOCIAL_LAST		P2P_FINDPHASE_EX_MAX

#define	P2P_PROVISION_TIMEOUT				5000	/*	5 seconds timeout for sending the provision discovery request */
#define	P2P_CONCURRENT_PROVISION_TIMEOUT	3000	/*	3 seconds timeout for sending the provision discovery request under concurrent mode */
#define	P2P_GO_NEGO_TIMEOUT					5000	/*	5 seconds timeout for receiving the group negotation response */
#define	P2P_CONCURRENT_GO_NEGO_TIMEOUT		3000	/*	3 seconds timeout for sending the negotiation request under concurrent mode */
#define	P2P_TX_PRESCAN_TIMEOUT				100		/*	100ms */
#define	P2P_INVITE_TIMEOUT					5000	/*	5 seconds timeout for sending the invitation request */
#define	P2P_CONCURRENT_INVITE_TIMEOUT		3000	/*	3 seconds timeout for sending the invitation request under concurrent mode */
#define	P2P_RESET_SCAN_CH						25000	/*	25 seconds timeout to reset the scan channel (based on channel plan) */
#define	P2P_MAX_INTENT						15

#define	P2P_MAX_NOA_NUM						2

/*	WPS Configuration Method */
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

enum P2P_ROLE {
	P2P_ROLE_DISABLE = 0,
	P2P_ROLE_DEVICE = 1,
	P2P_ROLE_CLIENT = 2,
	P2P_ROLE_GO = 3
};

enum P2P_STATE {
	P2P_STATE_NONE = 0,							/*	P2P disable */
	P2P_STATE_IDLE = 1,								/*	P2P had enabled and do nothing ,  buddy adapters is linked */
	P2P_STATE_LISTEN = 2,							/*	In pure listen state */
	P2P_STATE_SCAN = 3,							/*	In scan phase */
	P2P_STATE_FIND_PHASE_LISTEN = 4,				/*	In the listen state of find phase */
	P2P_STATE_FIND_PHASE_SEARCH = 5,				/*	In the search state of find phase */
	P2P_STATE_TX_PROVISION_DIS_REQ = 6,			/*	In P2P provisioning discovery */
	P2P_STATE_RX_PROVISION_DIS_RSP = 7,
	P2P_STATE_RX_PROVISION_DIS_REQ = 8,
	P2P_STATE_GONEGO_ING = 9,						/*	Doing the group owner negoitation handshake */
	P2P_STATE_GONEGO_OK = 10,						/*	finish the group negoitation handshake with success */
	P2P_STATE_GONEGO_FAIL = 11,					/*	finish the group negoitation handshake with failure */
	P2P_STATE_RECV_INVITE_REQ_MATCH = 12,		/*	receiving the P2P Inviation request and match with the profile. */
	P2P_STATE_PROVISIONING_ING = 13,				/*	Doing the P2P WPS */
	P2P_STATE_PROVISIONING_DONE = 14,			/*	Finish the P2P WPS */
	P2P_STATE_TX_INVITE_REQ = 15,					/*	Transmit the P2P Invitation request */
	P2P_STATE_RX_INVITE_RESP_OK = 16,				/*	Receiving the P2P Invitation response */
	P2P_STATE_RECV_INVITE_REQ_DISMATCH = 17,	/*	receiving the P2P Inviation request and dismatch with the profile. */
	P2P_STATE_RECV_INVITE_REQ_GO = 18,			/*	receiving the P2P Inviation request and this wifi is GO. */
	P2P_STATE_RECV_INVITE_REQ_JOIN = 19,			/*	receiving the P2P Inviation request to join an existing P2P Group. */
	P2P_STATE_RX_INVITE_RESP_FAIL = 20,			/*	recveing the P2P Inviation response with failure */
	P2P_STATE_RX_INFOR_NOREADY = 21,			/* receiving p2p negoitation response with information is not available */
	P2P_STATE_TX_INFOR_NOREADY = 22,			/* sending p2p negoitation response with information is not available */
};

enum P2P_WPSINFO {
	P2P_NO_WPSINFO						= 0,
	P2P_GOT_WPSINFO_PEER_DISPLAY_PIN	= 1,
	P2P_GOT_WPSINFO_SELF_DISPLAY_PIN	= 2,
	P2P_GOT_WPSINFO_PBC					= 3,
};

#define	P2P_PRIVATE_IOCTL_SET_LEN		64

enum P2P_PROTO_WK_ID {
	P2P_FIND_PHASE_WK = 0,
	P2P_RESTORE_STATE_WK = 1,
	P2P_PRE_TX_PROVDISC_PROCESS_WK = 2,
	P2P_PRE_TX_NEGOREQ_PROCESS_WK = 3,
	P2P_PRE_TX_INVITEREQ_PROCESS_WK = 4,
	P2P_AP_P2P_CH_SWITCH_PROCESS_WK = 5,
	P2P_RO_CH_WK = 6,
	P2P_CANCEL_RO_CH_WK = 7,
};

#ifdef CONFIG_P2P_PS
enum P2P_PS_STATE {
	P2P_PS_DISABLE = 0,
	P2P_PS_ENABLE = 1,
	P2P_PS_SCAN = 2,
	P2P_PS_SCAN_DONE = 3,
	P2P_PS_ALLSTASLEEP = 4, /* for P2P GO */
};

enum P2P_PS_MODE {
	P2P_PS_NONE = 0,
	P2P_PS_CTWINDOW = 1,
	P2P_PS_NOA	 = 2,
	P2P_PS_MIX = 3, /* CTWindow and NoA */
};
#endif /* CONFIG_P2P_PS */

/*	=====================WFD Section=====================
 *	For Wi-Fi Display */
#define	WFD_ATTR_DEVICE_INFO			0x00
#define	WFD_ATTR_ASSOC_BSSID			0x01
#define	WFD_ATTR_COUPLED_SINK_INFO	0x06
#define	WFD_ATTR_LOCAL_IP_ADDR		0x08
#define	WFD_ATTR_SESSION_INFO		0x09
#define	WFD_ATTR_ALTER_MAC			0x0a

/*	For WFD Device Information Attribute */
#define	WFD_DEVINFO_SOURCE					0x0000
#define	WFD_DEVINFO_PSINK					0x0001
#define	WFD_DEVINFO_SSINK					0x0002
#define	WFD_DEVINFO_DUAL					0x0003

#define	WFD_DEVINFO_SESSION_AVAIL			0x0010
#define	WFD_DEVINFO_WSD						0x0040
#define	WFD_DEVINFO_PC_TDLS					0x0080
#define	WFD_DEVINFO_HDCP_SUPPORT			0x0100

#ifdef CONFIG_TX_MCAST2UNI
#define IP_MCAST_MAC(mac)		((mac[0] == 0x01) && (mac[1] == 0x00) && (mac[2] == 0x5e))
#define ICMPV6_MCAST_MAC(mac)	((mac[0] == 0x33) && (mac[1] == 0x33) && (mac[2] != 0xff))
#endif /* CONFIG_TX_MCAST2UNI */

#ifdef CONFIG_IOCTL_CFG80211
/* Regulatroy Domain */
struct regd_pair_mapping {
	u16 reg_dmnenum;
	u16 reg_5ghz_ctl;
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
#endif

#ifdef CONFIG_WAPI_SUPPORT
#ifndef IW_AUTH_WAPI_VERSION_1
#define IW_AUTH_WAPI_VERSION_1		0x00000008
#endif
#ifndef IW_AUTH_KEY_MGMT_WAPI_PSK
#define IW_AUTH_KEY_MGMT_WAPI_PSK	0x04
#endif
#ifndef IW_AUTH_WAPI_ENABLED
#define IW_AUTH_WAPI_ENABLED		0x20
#endif
#ifndef IW_ENCODE_ALG_SM4
#define IW_ENCODE_ALG_SM4			0x20
#endif
#endif

#endif /* _WIFI_H_ */
