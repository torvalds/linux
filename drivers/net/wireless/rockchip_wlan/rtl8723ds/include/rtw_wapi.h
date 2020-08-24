/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __INC_WAPI_H
#define __INC_WAPI_H


#define CONFIG_WAPI_SW_SMS4
#define WAPI_DEBUG

#define SMS4_MIC_LEN                16
#define WAPI_EXT_LEN                18
#define MAX_WAPI_IE_LEN		    256
#define sMacHdrLng				24		/* octets in data header, no WEP */

#ifdef WAPI_DEBUG

/* WAPI trace debug */
extern u32 wapi_debug_component;

static inline void dump_buf(u8 *buf, u32 len)
{
	u32 i;
	printk("-----------------Len %d----------------\n", len);
	for (i = 0; i < len; i++)
		printk("%2.2x-", *(buf + i));
	printk("\n");
}

#define WAPI_TRACE(component, x, args...) \
	do { if (wapi_debug_component & (component)) \
			printk(KERN_DEBUG "WAPI" ":" x "" , \
			       ##args);\
	} while (0);

#define WAPI_DATA(component, x, buf, len) \
	do { if (wapi_debug_component & (component)) { \
			printk("%s:\n", x);\
			dump_buf((buf), (len)); } \
	} while (0);

#define RT_ASSERT_RET(_Exp)								\
	if (!(_Exp)) {									\
		printk("RTWLAN: ");					\
		printk("Assertion failed! %s,%s, line=%d\n", \
		       #_Exp, __FUNCTION__, __LINE__);          \
		return;						\
	}
#define RT_ASSERT_RET_VALUE(_Exp, Ret)								\
	if (!(_Exp)) {									\
		printk("RTWLAN: ");					\
		printk("Assertion failed! %s,%s, line=%d\n", \
		       #_Exp, __FUNCTION__, __LINE__);          \
		return Ret;						\
	}

#else
#define RT_ASSERT_RET(_Exp) do {} while (0)
#define RT_ASSERT_RET_VALUE(_Exp, Ret) do {} while (0)
#define WAPI_TRACE(component, x, args...) do {} while (0)
#define WAPI_DATA(component, x, buf, len) do {} while (0)
#endif


enum WAPI_DEBUG {
	WAPI_INIT				= 1,
	WAPI_API				= 1 << 1,
	WAPI_TX				= 1 << 2,
	WAPI_RX				= 1 << 3,
	WAPI_MLME				= 1 << 4,
	WAPI_IOCTL				= 1 << 5,
	WAPI_ERR			= 1 << 31
};

#define			WAPI_MAX_BKID_NUM				4
#define			WAPI_MAX_STAINFO_NUM			4
#define			WAPI_CAM_ENTRY_NUM			14	/* 28/2 = 14 */

typedef struct  _RT_WAPI_BKID {
	struct list_head	list;
	u8				bkid[16];
} RT_WAPI_BKID, *PRT_WAPI_BKID;

typedef struct  _RT_WAPI_KEY {
	u8			dataKey[16];
	u8			micKey[16];
	u8			keyId;
	bool			bSet;
	bool             bTxEnable;
} RT_WAPI_KEY, *PRT_WAPI_KEY;

typedef enum _RT_WAPI_PACKET_TYPE {
	WAPI_NONE = 0,
	WAPI_PREAUTHENTICATE = 1,
	WAPI_STAKEY_REQUEST = 2,
	WAPI_AUTHENTICATE_ACTIVE = 3,
	WAPI_ACCESS_AUTHENTICATE_REQUEST = 4,
	WAPI_ACCESS_AUTHENTICATE_RESPONSE = 5,
	WAPI_CERTIFICATE_AUTHENTICATE_REQUEST = 6,
	WAPI_CERTIFICATE_AUTHENTICATE_RESPONSE = 7,
	WAPI_USK_REQUEST = 8,
	WAPI_USK_RESPONSE = 9,
	WAPI_USK_CONFIRM = 10,
	WAPI_MSK_NOTIFICATION = 11,
	WAPI_MSK_RESPONSE = 12
} RT_WAPI_PACKET_TYPE;

typedef struct	_RT_WAPI_STA_INFO {
	struct list_head		list;
	u8					PeerMacAddr[6];
	RT_WAPI_KEY		      wapiUsk;
	RT_WAPI_KEY		      wapiUskUpdate;
	RT_WAPI_KEY		      wapiMsk;
	RT_WAPI_KEY		      wapiMskUpdate;
	u8					lastRxUnicastPN[16];
	u8					lastTxUnicastPN[16];
	u8					lastRxMulticastPN[16];
	u8					lastRxUnicastPNBEQueue[16];
	u8					lastRxUnicastPNBKQueue[16];
	u8					lastRxUnicastPNVIQueue[16];
	u8					lastRxUnicastPNVOQueue[16];
	bool					bSetkeyOk;
	bool					bAuthenticateInProgress;
	bool					bAuthenticatorInUpdata;
} RT_WAPI_STA_INFO, *PRT_WAPI_STA_INFO;

/* Added for HW wapi en/decryption */
typedef struct _RT_WAPI_CAM_ENTRY {
	/* RT_LIST_ENTRY		list; */
	u8			IsUsed;
	u8			entry_idx;/* for cam entry */
	u8			keyidx;	/* 0 or 1,new or old key */
	u8			PeerMacAddr[6];
	u8			type;	/* should be 110,wapi */
} RT_WAPI_CAM_ENTRY, *PRT_WAPI_CAM_ENTRY;

typedef struct _RT_WAPI_T {
	/* BKID */
	RT_WAPI_BKID		wapiBKID[WAPI_MAX_BKID_NUM];
	struct list_head		wapiBKIDIdleList;
	struct list_head		wapiBKIDStoreList;
	/* Key for Tx Multicast/Broadcast */
	RT_WAPI_KEY		      wapiTxMsk;

	/* sec related */
	u8				lastTxMulticastPN[16];
	/* STA list */
	RT_WAPI_STA_INFO	wapiSta[WAPI_MAX_STAINFO_NUM];
	struct list_head		wapiSTAIdleList;
	struct list_head		wapiSTAUsedList;
	/*  */
	bool				bWapiEnable;

	/* store WAPI IE */
	u8				wapiIE[256];
	u8				wapiIELength;
	bool				bWapiPSK;
	/* last sequece number for wai packet */
	u16				wapiSeqnumAndFragNum;
	int extra_prefix_len;
	int extra_postfix_len;

	RT_WAPI_CAM_ENTRY	wapiCamEntry[WAPI_CAM_ENTRY_NUM];
} RT_WAPI_T, *PRT_WAPI_T;

typedef struct _WLAN_HEADER_WAPI_EXTENSION {
	u8      KeyIdx;
	u8      Reserved;
	u8      PN[16];
} WLAN_HEADER_WAPI_EXTENSION, *PWLAN_HEADER_WAPI_EXTENSION;

u32 WapiComparePN(u8 *PN1, u8 *PN2);


void rtw_wapi_init(_adapter *padapter);

void rtw_wapi_free(_adapter *padapter);

void rtw_wapi_disable_tx(_adapter *padapter);

u8 rtw_wapi_is_wai_packet(_adapter *padapter, u8 *pkt_data);

void rtw_wapi_update_info(_adapter *padapter, union recv_frame *precv_frame);

u8 rtw_wapi_check_for_drop(_adapter *padapter, union recv_frame *precv_frame, u8 *ehdr_ops);

void rtw_build_probe_resp_wapi_ie(_adapter *padapter, unsigned char *pframe, struct pkt_attrib *pattrib);

void rtw_build_beacon_wapi_ie(_adapter *padapter, unsigned char *pframe, struct pkt_attrib *pattrib);

void rtw_build_assoc_req_wapi_ie(_adapter *padapter, unsigned char *pframe, struct pkt_attrib *pattrib);

void rtw_wapi_on_assoc_ok(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE);

void rtw_wapi_return_one_sta_info(_adapter *padapter, u8 *MacAddr);

void rtw_wapi_return_all_sta_info(_adapter *padapter);

void rtw_wapi_clear_cam_entry(_adapter *padapter, u8 *pMacAddr);

void rtw_wapi_clear_all_cam_entry(_adapter *padapter);

void rtw_wapi_set_key(_adapter *padapter, RT_WAPI_KEY *pWapiKey, RT_WAPI_STA_INFO *pWapiSta, u8 bGroupKey, u8 bUseDefaultKey);

int rtw_wapi_create_event_send(_adapter *padapter, u8 EventId, u8 *MacAddr, u8 *Buff, u16 BufLen);

u32	rtw_sms4_encrypt(_adapter *padapter, u8 *pxmitframe);

u32	rtw_sms4_decrypt(_adapter *padapter, u8 *precvframe);

void rtw_wapi_get_iv(_adapter *padapter, u8 *pRA, u8 *IV);

u8 WapiIncreasePN(u8 *PN, u8 AddCount);

bool rtw_wapi_drop_for_key_absent(_adapter *padapter, u8 *pRA);

void rtw_wapi_set_set_encryption(_adapter *padapter, struct ieee_param *param);

#endif
