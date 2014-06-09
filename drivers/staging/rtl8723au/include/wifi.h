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
				Below is the definition for 802.11n
------------------------------------------------------------------------------*/

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


/*	===============WPS Section=============== */
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
