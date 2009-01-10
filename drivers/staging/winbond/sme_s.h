#ifndef __WINBOND_SME_S_H
#define __WINBOND_SME_S_H

#include <linux/types.h>

#include "mac_structures.h"
#include "localpara.h"

//
// SME_S.H -
// SME task global CONSTANTS, STRUCTURES, variables
//

//////////////////////////////////////////////////////////////////////////
//define the msg type of SME module
// 0x00~0x1F : MSG from GUI dispatch
// 0x20~0x3F : MSG from MLME
// 0x40~0x5F : MSG from SCAN
// 0x60~0x6F : MSG from TX/RX
// 0x70~0x7F : MSG from ROAMING
// 0x80~0x8F : MSG from ISR
// 0x90		 : MSG TimeOut

// from GUI
#define SMEMSG_SCAN_REQ					0x01
//#define SMEMSG_PASSIVE_SCAN_REQ			0x02
#define SMEMSG_JOIN_REQ					0x03
#define SMEMSG_START_IBSS				0x04
#define SMEMSG_DISCONNECT_REQ			0x05
#define SMEMSG_AUTHEN_REQ				0x06
#define SMEMSG_DEAUTHEN_REQ				0x07
#define SMEMSG_ASSOC_REQ				0x08
#define SMEMSG_REASSOC_REQ				0x09
#define SMEMSG_DISASSOC_REQ				0x0a
#define SMEMSG_POWERSAVE_REQ			0x0b


// from MLME
#define SMEMSG_AUTHEN_CFM				0x21
#define SMEMSG_AUTHEN_IND				0x22
#define SMEMSG_ASSOC_CFM				0x23
#define SMEMSG_DEAUTHEN_IND				0x24
#define SMEMSG_DISASSOC_IND				0x25
// from SCAN
#define SMEMSG_SCAN_CFM					0x41
#define SMEMSG_START_IBSS_CFM			0x42
// from MTO, function call to set SME parameters

// 0x60~0x6F : MSG from TX/RX
//#define SMEMSG_IBSS_JOIN_UPDATE_BSSID	0x61
#define SMEMSG_COUNTERMEASURE_MICFAIL_TIMEOUT		0x62
#define SMEMSG_COUNTERMEASURE_BLOCK_TIMEOUT	0x63
// from ROAMING
#define SMEMSG_HANDOVER_JOIN_REQ		0x71
#define SMEMSG_END_ROAMING				0x72
#define SMEMSG_SCAN_JOIN_REQ			0x73
// from ISR
#define SMEMSG_TSF_SYNC_IND				0x81
// from TimeOut
#define SMEMSG_TIMEOUT					0x91



#define MAX_PMKID_Accunt                16
//@added by ws 04/22/05
#define Cipher_Disabled                 0
#define Cipher_Wep                      1
#define Cipher_Tkip                     2
#define Cipher_Ccmp                     4


///////////////////////////////////////////////////////////////////////////
//Constants

///////////////////////////////////////////////////////////////////////////
//Global data structures

#define NUMOFWEPENTRIES     64

typedef enum _WEPKeyMode
{
    WEPKEY_DISABLED = 0,
    WEPKEY_64       = 1,
    WEPKEY_128      = 2

} WEPKEYMODE, *pWEPKEYMODE;

#ifdef _WPA2_

typedef struct _BSSInfo
{
  u8        PreAuthBssID[6];
  PMKID        pmkid_value;
}BSSID_Info;

typedef struct _PMKID_Table //added by ws 05/05/04
{
   u32  Length;
   u32  BSSIDInfoCount;
   BSSID_Info BSSIDInfo[16];

} PMKID_Table;

#endif //end def _WPA2_

#define MAX_BASIC_RATE_SET          15
#define MAX_OPT_RATE_SET            MAX_BASIC_RATE_SET


typedef struct _SME_PARAMETERS
{
    u16				wState;
	u8				boDUTmode;
	u8				bDesiredPowerSave;

	// SME timer and timeout value
	struct timer_list timer;

	u8				boInTimerHandler;
	u8 				boAuthRetryActive;
	u8				reserved_0[2];

	u32				AuthenRetryTimerVal;	// NOTE: Assoc don't fail timeout
	u32				JoinFailTimerVal;		// 10*Beacon-Interval

	//Rates
	u8				BSSBasicRateSet[(MAX_BASIC_RATE_SET + 3) & ~0x03 ];    // BSS basic rate set
	u8				OperationalRateSet[(MAX_OPT_RATE_SET + 3) & ~0x03 ]; // Operational rate set

	u8				NumOfBSSBasicRate;
	u8				NumOfOperationalRate;
	u8				reserved_1[2];

	u32				BasicRateBitmap;
	u32				OpRateBitmap;

	// System parameters Set by GUI
	//-------------------- start IBSS parameter ---------------------------//
	u32				boStartIBSS;			//Start IBSS toggle

	u16				wBeaconPeriod;
	u16				wATIM_Window;

	ChanInfo			IbssChan; // 2B	//channel setting when start IBSS
	u8				reserved_2[2];

    // Join related
	u16				wDesiredJoinBSS;		// BSS index which desire to join
	u8				boJoinReq;				//Join request toggle
	u8				bDesiredBSSType;		//for Join request

    u16				wCapabilityInfo;        // Used when invoking the MLME_Associate_request().
	u16				wNonERPcapabilityInfo;

    struct SSID_Element sDesiredSSID; // 34 B
	u8				reserved_3[2];

	u8    			abDesiredBSSID[MAC_ADDR_LENGTH + 2];

	u8				bJoinScanCount;			// the time of scan-join action need to do
	u8				bDesiredAuthMode;       // AUTH_OPEN_SYSTEM or AUTH_SHARED_KEY
	u8				reserved_4[2];

    // Encryption parameters
	u8     			_dot11PrivacyInvoked;
    u8             	_dot11PrivacyOptionImplemented;
	u8				reserved_5[2];

    //@ ws added
    u8				DesiredEncrypt;
	u8				encrypt_status;	//ENCRYPT_DISABLE, ENCRYPT_WEP, ENCRYPT_WEP_NOKEY, ENCRYPT_TKIP, ...
	u8				key_length;
	u8				pairwise_key_ok;

	u8				group_key_ok;
    u8				wpa_ok;             // indicate the control port of 802.1x is open or close
	u8				pairwise_key_type;
	u8				group_key_type;

    u32               _dot11WEPDefaultKeyID;

	u8              	tx_mic_key[8];      // TODO: 0627 kevin-TKIP
	u8              	rx_mic_key[8];      // TODO: 0627 kevin-TKIP
	u8				group_tx_mic_key[8];
	u8				group_rx_mic_key[8];

//	#ifdef _WPA_
	u8				AssocReqVarIE[200];
	u8				AssocRespVarIE[200];

	u16				AssocReqVarLen;
	u16				AssocRespVarLen;
	u8				boReassoc;				//use assoc. or reassoc.
	u8				reserved_6[3];
	u16				AssocRespCapability;
	u16				AssocRespStatus;
//	#endif

	#ifdef _WPA2_
    u8               PmkIdTable[256];
    u32               PmkidTableIndex;
	#endif //end def _WPA2_

} SME_PARAMETERS, *PSME_PARAMETERS;

#define psSME			(&(adapter->sSmePara))

#define wSMEGetCurrentSTAState(adapter)		((u16)(adapter)->sSmePara.wState)



//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//	SmeModule.h
//		Define the related definitions of SME module
//	history -- 01/14/03' created
//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//Define the state of SME module
#define DISABLED						0
#define INIT_SCAN						1
#define SCAN_READY						2
#define START_IBSS						3
#define JOIN_PENDING					4
#define JOIN_CFM						5
#define AUTHENTICATE_PENDING			6
#define AUTHENTICATED					7
#define CONNECTED						8
//#define EAP_STARTING					9
//#define EAPOL_AUTHEN_PENDING			10
//#define SECURE_CONNECTED				11


// Static function

#endif
