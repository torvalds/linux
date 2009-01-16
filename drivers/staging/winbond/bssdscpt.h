#ifndef __WINBOND_BSSDSCPT_H
#define __WINBOND_BSSDSCPT_H

#include <linux/types.h>

#include "mds_s.h"
#include "mlme_s.h"

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//	bssdscpt.c
//		BSS descriptor data base
//	history :
//
//	Description:
//		BSS descriptor data base will store the information of the stations at the
//		surrounding environment. The first entry( psBSS(0) ) will not be used and the
//		second one( psBSS(1) ) will be used for the broadcast address.
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//#define MAX_ACC_RSSI_COUNT		10
#define MAX_ACC_RSSI_COUNT		6

///////////////////////////////////////////////////////////////////////////
//
// BSS Description set Element , to store scan received Beacon information
//
// Our's differs slightly from the specs. The specify a PHY_Parameter_Set.
// Since we're only doing a DS design right now, we just have a DS structure.
//////////////////////////////////////////////////////////////////////////////
typedef struct BSSDescriptionElement
{
	u32		SlotValid;
	u32		PowerSaveMode;
	RXLAYER1	RxLayer1;

    u8		abPeerAddress[ MAC_ADDR_LENGTH + 2 ]; // peer MAC Address associated with this session. 6-OCTET value
    u32		dwBgScanStamp;		// BgScan Sequence Counter stamp, record psROAM->dwScanCounter.

	u16		Beacon_Period;
	u16		wATIM_Window;

    u8		abBssID[ MAC_ADDR_LENGTH + 2 ];				// 6B

    u8		bBssType;
    u8		DTIM_Period;        // 1 octet usually from TIM element, if present
	u8		boInTimerHandler;
	u8		boERP;			// analysis ERP or (extended) supported rate element

	u8		Timestamp[8];
	u8		BasicRate[32];
	u8		OperationalRate[32];
	u32		dwBasicRateBitmap;			//bit map, retrieve from SupportedRateSet
	u32		dwOperationalRateBitmap;	//bit map, retrieve from SupportedRateSet and
										// ExtendedSupportedRateSet
	// For RSSI calculating
	u32		HalRssi[MAX_ACC_RSSI_COUNT]; // Encode. It must use MACRO of HAL to get the LNA and AGC data
	u32		HalRssiIndex;

	////From beacon/probe response
    struct SSID_Element SSID;				// 34B
	u8	reserved_1[ 2 ];

    struct Capability_Information_Element   CapabilityInformation;  // 2B
	u8	reserved_2[ 2 ];

    struct CF_Parameter_Set_Element    CF_Parameter_Set;		// 8B
    struct IBSS_Parameter_Set_Element  IBSS_Parameter_Set;		// 4B
    struct TIM_Element                 TIM_Element_Set; 			// 256B

    struct DS_Parameter_Set_Element    DS_Parameter_Set;		// 3B
	u8	reserved_3;

	struct ERP_Information_Element		ERP_Information_Set;	// 3B
	u8	reserved_4;

    struct Supported_Rates_Element     SupportedRateSet;			// 10B
	u8	reserved_5[2];

	struct Extended_Supported_Rates_Element	ExtendedSupportedRateSet;	// 257B
	u8	reserved_6[3];

	u8	band;
	u8	reserved_7[3];

	// for MLME module
    u16		wState;			// the current state of the system
	u16		wIndex;			// THIS BSS element entry index

	void*	psadapter;		// pointer to THIS adapter
	struct timer_list timer;  // MLME timer

    // Authentication
    u16		wAuthAlgo;      // peer MAC MLME use Auth algorithm, default OPEN_AUTH
    u16		wAuthSeqNum;    // current local MAC sendout AuthReq sequence number

	u8		auth_challengeText[128];

	////For XP:
    u32		ies_len;		// information element length
    u8		ies[256];		// information element

	////For WPA
	u8	RsnIe_Type[2];		//added by ws for distinguish WPA and WPA2 05/14/04
	u8	RsnIe_len;
    u8	Rsn_Num;

    // to record the rsn cipher suites,addded by ws 09/05/04
	SUITE_SELECTOR			group_cipher; // 4B
	SUITE_SELECTOR			pairwise_key_cipher_suites[WLAN_MAX_PAIRWISE_CIPHER_SUITE_COUNT];
	SUITE_SELECTOR			auth_key_mgt_suites[WLAN_MAX_AUTH_KEY_MGT_SUITE_LIST_COUNT];

	u16					pairwise_key_cipher_suite_count;
	u16					auth_key_mgt_suite_count;

	u8					pairwise_key_cipher_suite_selected;
	u8					auth_key_mgt_suite_selected;
	u8					reserved_8[2];

	struct RSN_Capability_Element  rsn_capabilities; // 2B
	u8					reserved_9[2];

    //to record the rsn cipher suites for WPA2
    #ifdef _WPA2_
	u32					pre_auth;		//added by WS for distinguish for 05/04/04
    SUITE_SELECTOR			wpa2_group_cipher; // 4B
	SUITE_SELECTOR			wpa2_pairwise_key_cipher_suites[WLAN_MAX_PAIRWISE_CIPHER_SUITE_COUNT];
	SUITE_SELECTOR			wpa2_auth_key_mgt_suites[WLAN_MAX_AUTH_KEY_MGT_SUITE_LIST_COUNT];

	u16					wpa2_pairwise_key_cipher_suite_count;
	u16					wpa2_auth_key_mgt_suite_count;

	u8					wpa2_pairwise_key_cipher_suite_selected;
	u8					wpa2_auth_key_mgt_suite_selected;
	u8					reserved_10[2];

	struct RSN_Capability_Element  wpa2_rsn_capabilities; // 2B
	u8					reserved_11[2];
    #endif //endif _WPA2_

	//For Replay protection
//	u8		PairwiseTSC[6];
//	u8		GroupTSC[6];

	////For up-to-date
	u32		ScanTimeStamp;	//for the decision whether the station/AP(may exist at
							//different channels) has left. It must be detected by
							//scanning. Local device may connected or disconnected.
	u32		BssTimeStamp;	//Only for the decision whether the station/AP(exist in
							//the same channel, and no scanning) if local device has
							//connected successfully.

	// 20061108 Add for storing WPS_IE. [E id][Length][OUI][Data]
	u8		WPS_IE_Data[MAX_IE_APPEND_SIZE];
	u16		WPS_IE_length;
	u16		WPS_IE_length_tmp; // For verify there is an WPS_IE in Beacon or probe response

} WB_BSSDESCRIPTION, *PWB_BSSDESCRIPTION;

#define wBSSConnectedSTA(adapter)             \
    ((u16)(adapter)->sLocalPara.wConnectedSTAindex)

#define psBSS(i)			(&(adapter->asBSSDescriptElement[(i)]))

#endif
