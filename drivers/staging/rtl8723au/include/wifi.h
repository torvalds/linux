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
 ******************************************************************************/
#ifndef _WIFI_H_
#define _WIFI_H_

#define P80211CAPTURE_VERSION	0x80211001

/*  This value is tested by WiFi 11n Test Plan 5.2.3.
 *  This test verifies the WLAN NIC can update the NAV through sending
 *  the CTS with large duration.
 */
#define	WiFiNavUpperUs		30000	/*  30 ms */

enum WIFI_FRAME_TYPE {
	WIFI_MGT_TYPE  =	(0),
	WIFI_CTRL_TYPE =	(BIT(2)),
	WIFI_DATA_TYPE =	(BIT(3)),
	WIFI_QOS_DATA_TYPE	= (BIT(7)|BIT(3)),	/*  QoS Data */
};

enum WIFI_FRAME_SUBTYPE {
	/*  below is for mgt frame */
	WIFI_ASSOCREQ = (0 | WIFI_MGT_TYPE),
	WIFI_ASSOCRSP = (BIT(4) | WIFI_MGT_TYPE),
	WIFI_REASSOCREQ = (BIT(5) | WIFI_MGT_TYPE),
	WIFI_REASSOCRSP = (BIT(5) | BIT(4) | WIFI_MGT_TYPE),
	WIFI_PROBEREQ = (BIT(6) | WIFI_MGT_TYPE),
	WIFI_PROBERSP = (BIT(6) | BIT(4) | WIFI_MGT_TYPE),
	WIFI_BEACON = (BIT(7) | WIFI_MGT_TYPE),
	WIFI_ATIM = (BIT(7) | BIT(4) | WIFI_MGT_TYPE),
	WIFI_DISASSOC = (BIT(7) | BIT(5) | WIFI_MGT_TYPE),
	WIFI_AUTH = (BIT(7) | BIT(5) | BIT(4) | WIFI_MGT_TYPE),
	WIFI_DEAUTH = (BIT(7) | BIT(6) | WIFI_MGT_TYPE),
	WIFI_ACTION = (BIT(7) | BIT(6) | BIT(4) | WIFI_MGT_TYPE),

	/*  below is for control frame */
	WIFI_PSPOLL = (BIT(7) | BIT(5) | WIFI_CTRL_TYPE),
	WIFI_RTS = (BIT(7) | BIT(5) | BIT(4) | WIFI_CTRL_TYPE),
	WIFI_CTS = (BIT(7) | BIT(6) | WIFI_CTRL_TYPE),
	WIFI_ACK = (BIT(7) | BIT(6) | BIT(4) | WIFI_CTRL_TYPE),
	WIFI_CFEND = (BIT(7) | BIT(6) | BIT(5) | WIFI_CTRL_TYPE),
	WIFI_CFEND_CFACK = (BIT(7) | BIT(6) | BIT(5) | BIT(4) | WIFI_CTRL_TYPE),

	/*  below is for data frame */
	WIFI_DATA = (0 | WIFI_DATA_TYPE),
	WIFI_DATA_CFACK = (BIT(4) | WIFI_DATA_TYPE),
	WIFI_DATA_CFPOLL = (BIT(5) | WIFI_DATA_TYPE),
	WIFI_DATA_CFACKPOLL = (BIT(5) | BIT(4) | WIFI_DATA_TYPE),
	WIFI_DATA_NULL = (BIT(6) | WIFI_DATA_TYPE),
	WIFI_CF_ACK = (BIT(6) | BIT(4) | WIFI_DATA_TYPE),
	WIFI_CF_POLL = (BIT(6) | BIT(5) | WIFI_DATA_TYPE),
	WIFI_CF_ACKPOLL = (BIT(6) | BIT(5) | BIT(4) | WIFI_DATA_TYPE),
	WIFI_QOS_DATA_NULL = (BIT(6) | WIFI_QOS_DATA_TYPE),
};


enum WIFI_REG_DOMAIN {
	DOMAIN_FCC		= 1,
	DOMAIN_IC		= 2,
	DOMAIN_ETSI		= 3,
	DOMAIN_SPAIN		= 4,
	DOMAIN_FRANCE		= 5,
	DOMAIN_MKK		= 6,
	DOMAIN_ISRAEL		= 7,
	DOMAIN_MKK1		= 8,
	DOMAIN_MKK2		= 9,
	DOMAIN_MKK3		= 10,
	DOMAIN_MAX
};


#define SetToDs(pbuf)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16(IEEE80211_FCTL_TODS))

#define SetFrDs(pbuf)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16(IEEE80211_FCTL_FROMDS))

#define SetPwrMgt(pbuf)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16(IEEE80211_FCTL_PM))

#define SetMData(pbuf)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16(IEEE80211_FCTL_MOREDATA))

#define SetPrivacy(pbuf)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16(IEEE80211_FCTL_PROTECTED))

#define SetFrameType(pbuf, type)	\
	do {	\
		*(__le16 *)(pbuf) &= __constant_cpu_to_le16(~(BIT(3) | BIT(2))); \
		*(__le16 *)(pbuf) |= __constant_cpu_to_le16(type); \
	} while (0)

#define SetFrameSubType(pbuf, type) \
	do {    \
		*(__le16 *)(pbuf) &= cpu_to_le16(~(BIT(7) | BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2))); \
		*(__le16 *)(pbuf) |= cpu_to_le16(type); \
	} while (0)

#define SetFragNum(pbuf, num) \
	do {    \
		*(unsigned short *)((unsigned long)(pbuf) + 22) = \
			((*(unsigned short *)((unsigned long)(pbuf) + 22)) & le16_to_cpu(~(0x000f))) | \
			cpu_to_le16(0x0f & (num));     \
	} while (0)

#define SetSeqNum(pbuf, num) \
	do {    \
		*(__le16 *)((size_t)(pbuf) + 22) = \
			((*(__le16 *)((size_t)(pbuf) + 22)) & cpu_to_le16((unsigned short)0x000f)) | \
			cpu_to_le16((unsigned short)(0xfff0 & (num << 4))); \
	} while (0)

#define SetDuration(pbuf, dur) \
	(*(__le16 *)((unsigned long)(pbuf) + 2) =		\
	 cpu_to_le16(0xffff & (dur)))

#define SetPriority(pbuf, tid)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16(tid & 0xf))

#define SetEOSP(pbuf, eosp)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16((eosp & 1) << 4))

#define SetAckpolicy(pbuf, ack)	\
	(*(__le16 *)(pbuf) |= cpu_to_le16((ack & 3) << 5))

#define _ASOCREQ_IE_OFFSET_		4	/*  excluding wlan_hdr */
#define	_ASOCRSP_IE_OFFSET_		6
#define _REASOCREQ_IE_OFFSET_		10
#define _REASOCRSP_IE_OFFSET_		6
#define _PROBEREQ_IE_OFFSET_		0
#define	_PROBERSP_IE_OFFSET_		12
#define _AUTH_IE_OFFSET_		6
#define _DEAUTH_IE_OFFSET_		0
#define _BEACON_IE_OFFSET_		12
#define _PUBLIC_ACTION_IE_OFFSET_	8

#define _FIXED_IE_LENGTH_		_BEACON_IE_OFFSET_


#define	EID_BSSIntolerantChlReport	73

/* ---------------------------------------------------------------------------
					Below is the fixed elements...
-----------------------------------------------------------------------------*/
#define _AUTH_ALGM_NUM_		2
#define _AUTH_SEQ_NUM_		2
#define _BEACON_ITERVAL_	2
#define _CAPABILITY_		2
#define _CURRENT_APADDR_	6
#define _LISTEN_INTERVAL_	2
#define _ASOC_ID_		2
#define _STATUS_CODE_		2
#define _TIMESTAMP_		8

/*-----------------------------------------------------------------------------
				Below is the definition for WMM
------------------------------------------------------------------------------*/
#define _WMM_IE_Length_				7  /*  for WMM STA */
#define _WMM_Para_Element_Length_		24


/*-----------------------------------------------------------------------------
				Below is the definition for 802.11n
------------------------------------------------------------------------------*/

/* struct rtw_ieee80211_ht_cap - HT additional information
 *
 * This structure refers to "HT information element" as
 * described in 802.11n draft section 7.3.2.53
 */
struct ieee80211_ht_addt_info {
	unsigned char	control_chan;
	unsigned char	ht_param;
	unsigned short	operation_mode;
	unsigned short	stbc_param;
	unsigned char	basic_set[16];
} __packed;

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
} __packed;

struct HT_info_element {
	unsigned char	primary_channel;
	unsigned char	infos[5];
	unsigned char	MCS_rate[16];
}  __packed;

struct AC_param {
	unsigned char		ACI_AIFSN;
	unsigned char		CW;
	unsigned short	TXOP_limit;
}  __packed;

struct WMM_para_element {
	unsigned char		QoS_info;
	unsigned char		reserved;
	struct AC_param	ac_param[4];
}  __packed;

struct ADDBA_request {
	unsigned char		dialog_token;
	unsigned short	BA_para_set;
	unsigned short	BA_timeout_value;
	unsigned short	BA_starting_seqctrl;
}  __packed;


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

/*	===============WPS Section=============== */
/*	For WPSv1.0 */
#define WPSOUI					0x0050f204
/*	WPS attribute ID */
#define WPS_ATTR_VER1				0x104A
#define WPS_ATTR_SIMPLE_CONF_STATE		0x1044
#define WPS_ATTR_RESP_TYPE			0x103B
#define WPS_ATTR_UUID_E				0x1047
#define WPS_ATTR_MANUFACTURER			0x1021
#define WPS_ATTR_MODEL_NAME			0x1023
#define WPS_ATTR_MODEL_NUMBER			0x1024
#define WPS_ATTR_SERIAL_NUMBER			0x1042
#define WPS_ATTR_PRIMARY_DEV_TYPE		0x1054
#define WPS_ATTR_SEC_DEV_TYPE_LIST		0x1055
#define WPS_ATTR_DEVICE_NAME			0x1011
#define WPS_ATTR_CONF_METHOD			0x1008
#define WPS_ATTR_RF_BANDS			0x103C
#define WPS_ATTR_DEVICE_PWID			0x1012
#define WPS_ATTR_REQUEST_TYPE			0x103A
#define WPS_ATTR_ASSOCIATION_STATE		0x1002
#define WPS_ATTR_CONFIG_ERROR			0x1009
#define WPS_ATTR_VENDOR_EXT			0x1049
#define WPS_ATTR_SELECTED_REGISTRAR		0x1041

/*	Value of WPS attribute "WPS_ATTR_DEVICE_NAME */
#define WPS_MAX_DEVICE_NAME_LEN			32

/*	Value of WPS Request Type Attribute */
#define WPS_REQ_TYPE_ENROLLEE_INFO_ONLY		0x00
#define WPS_REQ_TYPE_ENROLLEE_OPEN_8021X	0x01
#define WPS_REQ_TYPE_REGISTRAR			0x02
#define WPS_REQ_TYPE_WLAN_MANAGER_REGISTRAR	0x03

/*	Value of WPS Response Type Attribute */
#define WPS_RESPONSE_TYPE_INFO_ONLY		0x00
#define WPS_RESPONSE_TYPE_8021X			0x01
#define WPS_RESPONSE_TYPE_REGISTRAR		0x02
#define WPS_RESPONSE_TYPE_AP			0x03

/*	Value of WPS WiFi Simple Configuration State Attribute */
#define WPS_WSC_STATE_NOT_CONFIG		0x01
#define WPS_WSC_STATE_CONFIG			0x02

/*	Value of WPS Version Attribute */
#define WPS_VERSION_1				0x10

/*	Value of WPS Configuration Method Attribute */
#define WPS_CONFIG_METHOD_FLASH			0x0001
#define WPS_CONFIG_METHOD_ETHERNET		0x0002
#define WPS_CONFIG_METHOD_LABEL			0x0004
#define WPS_CONFIG_METHOD_DISPLAY		0x0008
#define WPS_CONFIG_METHOD_E_NFC			0x0010
#define WPS_CONFIG_METHOD_I_NFC			0x0020
#define WPS_CONFIG_METHOD_NFC			0x0040
#define WPS_CONFIG_METHOD_PBC			0x0080
#define WPS_CONFIG_METHOD_KEYPAD		0x0100
#define WPS_CONFIG_METHOD_VPBC			0x0280
#define WPS_CONFIG_METHOD_PPBC			0x0480
#define WPS_CONFIG_METHOD_VDISPLAY		0x2008
#define WPS_CONFIG_METHOD_PDISPLAY		0x4008

/*	Value of Category ID of WPS Primary Device Type Attribute */
#define WPS_PDT_CID_DISPLAYS			0x0007
#define WPS_PDT_CID_MULIT_MEDIA			0x0008
#define WPS_PDT_CID_RTK_WIDI			WPS_PDT_CID_MULIT_MEDIA

/*	Value of Sub Category ID of WPS Primary Device Type Attribute */
#define WPS_PDT_SCID_MEDIA_SERVER		0x0005
#define WPS_PDT_SCID_RTK_DMP			WPS_PDT_SCID_MEDIA_SERVER

/*	Value of Device Password ID */
#define WPS_DPID_PIN				0x0000
#define WPS_DPID_USER_SPEC			0x0001
#define WPS_DPID_MACHINE_SPEC			0x0002
#define WPS_DPID_REKEY				0x0003
#define WPS_DPID_PBC				0x0004
#define WPS_DPID_REGISTRAR_SPEC			0x0005

/*	Value of WPS RF Bands Attribute */
#define WPS_RF_BANDS_2_4_GHZ			0x01
#define WPS_RF_BANDS_5_GHZ			0x02

/*	Value of WPS Association State Attribute */
#define WPS_ASSOC_STATE_NOT_ASSOCIATED		0x00
#define WPS_ASSOC_STATE_CONNECTION_SUCCESS	0x01
#define WPS_ASSOC_STATE_CONFIGURATION_FAILURE	0x02
#define WPS_ASSOC_STATE_ASSOCIATION_FAILURE	0x03
#define WPS_ASSOC_STATE_IP_FAILURE		0x04

/*	P2P Public Action Frame ( Management Frame ) */
#define	P2P_PUB_ACTION_ACTION			0x09

/*	P2P Public Action Frame Type */
#define	P2P_GO_NEGO_REQ				0
#define	P2P_GO_NEGO_RESP			1
#define	P2P_GO_NEGO_CONF			2
#define	P2P_INVIT_REQ				3
#define	P2P_INVIT_RESP				4
#define	P2P_DEVDISC_REQ				5
#define	P2P_DEVDISC_RESP			6
#define	P2P_PROVISION_DISC_REQ			7
#define	P2P_PROVISION_DISC_RESP			8

/*	WPS Configuration Method */
#define	WPS_CM_NONE					0x0000
#define	WPS_CM_LABEL					0x0004
#define	WPS_CM_DISPLYA					0x0008
#define	WPS_CM_EXTERNAL_NFC_TOKEN			0x0010
#define	WPS_CM_INTEGRATED_NFC_TOKEN			0x0020
#define	WPS_CM_NFC_INTERFACE				0x0040
#define	WPS_CM_PUSH_BUTTON				0x0080
#define	WPS_CM_KEYPAD					0x0100
#define	WPS_CM_SW_PUHS_BUTTON				0x0280
#define	WPS_CM_HW_PUHS_BUTTON				0x0480
#define	WPS_CM_SW_DISPLAY_PIN				0x2008
#define	WPS_CM_LCD_DISPLAY_PIN				0x4008

#endif /*  _WIFI_H_ */
