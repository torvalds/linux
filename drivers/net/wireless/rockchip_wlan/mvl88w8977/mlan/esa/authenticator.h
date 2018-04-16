/** @file authenticator.h
 *
 *  @brief This file contains the data structure for authenticator and supplicant.
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
#ifndef _AUTHENTICATOR_H
#define _AUTHENTICATOR_H

#include "wltypes.h"
#include "IEEE_types.h"
#include "wl_mib_rom.h"
#include "KeyApiStaDefs.h"
#include "keyApiStaTypes.h"
#include "keyCommonDef.h"
#include "keyMgmtApTypes.h"
#include "pmkCache_rom.h"

#include "hostsa_def.h"

extern const uint8 wpa_oui02[4];	/* WPA TKIP */
extern const uint8 wpa_oui04[4];	/* WPA AES */
extern const uint8 wpa_oui01[4];	/* WPA WEP-40 */
extern const uint8 wpa_oui05[4];	/* WPA WEP-104 */
extern const uint8 wpa_oui_none[4];	/* WPA NONE */

extern const uint8 wpa2_oui02[4];	/* WPA2 TKIP */
extern const uint8 wpa2_oui04[4];	/* WPA2 AES */
extern const uint8 wpa2_oui01[4];	/* WPA2 WEP-40 */
extern const uint8 wpa2_oui05[4];	/* WPA2 WEP-104 */

extern const uint8 wpa_oui[3];
extern const uint8 kde_oui[3];

typedef enum {
	NO_MIC_FAILURE,
	FIRST_MIC_FAIL_IN_60_SEC,
	SECOND_MIC_FAIL_IN_60_SEC
} MIC_Fail_State_e;

typedef struct {
	MIC_Fail_State_e status;
	BOOLEAN MICCounterMeasureEnabled;
	UINT32 disableStaAsso;
} MIC_Error_t;

typedef struct {
	UINT8 TKIPICVErrors;
	UINT8 TKIPLocalMICFailures;
	UINT8 TKIPCounterMeasuresInvoked;

} customMIB_RSNStats_t;

typedef struct {
	UINT8 kck[16];		/* PTK_KCK = L(PTK,   0, 128); */
	UINT8 kek[16];		/* PTK_KEK = L(PTK, 128, 128); */
	UINT8 tk[16];		/* PTK_TK  = L(PTK, 256, 128); */

} CcmPtk_t;

typedef struct {
	UINT8 kck[16];		/* PTK_KCK = L(PTK,   0, 128); */
	UINT8 kek[16];		/* PTK_KEK = L(PTK, 128, 128); */
	UINT8 tk[16];		/* PTK_TK  = L(PTK, 256, 128); */
	UINT8 rxMicKey[8];
	UINT8 txMicKey[8];

} TkipPtk_t;

typedef struct {
	MIC_Error_t apMicError;
	t_void *apMicTimer;

	UINT32 ageOutCnt;
	UINT32 stateInfo;
	//key mgmt data
	apKeyMgmtInfoSta_t keyMgmtInfo;

	t_u8 RSNEnabled;
	UINT16 deauthReason;

	UINT8 txPauseState;
	//RateChangeInfo[] is used by MAC HW to decide the start TX rate.
	//It should be placed in SQ. If staData_t is placed in ITCM/DTCM then put
	//staRateTable in SQ and use a pointer here
	//staRateTable RateChangeInfo;
	UINT16 stickyTimCount;
	BOOLEAN stickyTimEnabled;

#ifdef DOT11W
	/* Peer STA PMF capability */
	BOOLEAN peerPMFCapable;
#endif

} staData_t;
/**connectioninfo*/
typedef struct _cm_Connection {
    /**Hand shake timer*/
	t_void *HskTimer;
    /** Timer set flag */
	t_u8 timer_is_set;
    /** authenticator Private pointer */
	t_void *priv;
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH];
    /**sta data for authenticator*/
	staData_t staData;
    /**handshake data*/
	eapolHskData_t hskData;
} cm_Connection;

typedef struct {
	IEEEtypes_CapInfo_t CapInfo;
	UINT32 AssocStationsCnt;

	BOOLEAN updatePassPhrase;

	KeyData_t grpKeyData;
	UINT8 GNonce[32];

	/* Following two variables contain that multiple of BI which is just
	 ** greater than user configured ageout time in normal and PS mode. These
	 ** variables get updated at bss_start, and then are used whenever FW
	 ** resets STA age.
	 */
	UINT32 staAgeOutBcnCnt;
	UINT32 psStaAgeOutBcnCnt;

	// Store group rekey time as a multiple of beacon interval.
	UINT32 grpRekeyCntConfigured;
	UINT32 grpRekeyCntRemaining;

} BssData_t;

typedef struct {
	UINT16 keyExchange:1;
	UINT16 authenticate:1;
	UINT16 reserved:14;
} Operation_t;

typedef struct {
	Cipher_t mcstCipher;
	UINT8 mcstCipherCount;

	Cipher_t wpaUcstCipher;
	UINT8 wpaUcstCipherCount;

	Cipher_t wpa2UcstCipher;
	UINT8 wpa2UcstCipherCount;

	UINT16 AuthKey;
	UINT16 AuthKeyCount;
	Operation_t Akmp;
	UINT32 GrpReKeyTime;
	UINT8 PSKPassPhrase[PSK_PASS_PHRASE_LEN_MAX];
	UINT8 PSKPassPhraseLen;
	UINT8 PSKValue[PMK_LEN_MAX];
	UINT8 MaxPwsHskRetries;
	UINT8 MaxGrpHskRetries;
	UINT32 PwsHskTimeOut;
	UINT32 GrpHskTimeOut;
	UINT8 RSNReplayProtEn;	/* RSN Replay Attack Protection flag */
} apRsnConfig_t;

typedef struct {
	UINT8 ieSet;
	UINT8 version;
/*  UINT8      akmCnt     ;  */
	UINT8 akmTypes;
/*  UINT8      uCastCnt   ;  */
	UINT8 uCastTypes;
	UINT8 mCastTypes;
	UINT8 capInfo;
} wapi_ie_cfg_t;

typedef struct {
	/* The This section only used for initialization of the connPtr */
	IEEEtypes_SsId_t SsId;
	IEEEtypes_Len_t SsIdLen;
	// odd-sized ele clubbed together to keep even alignment
	IEEEtypes_DtimPeriod_t DtimPeriod;
	IEEEtypes_BcnInterval_t BcnPeriod;

	IEEEtypes_MacAddr_t BssId;
	UINT16 RtsThresh;
	UINT16 FragThresh;
	UINT8 ShortRetryLim;
	UINT8 LongRetryLim;

	// Used in MBSS mode for software beacon suppression
	UINT8 MbssBcnIntFac;
	UINT8 MbssCurBcnIntCnt;
	UINT16 Reserved;
} CommonMlmeData_t;

typedef struct {
	IEEEtypes_SsId_t SsId;
	IEEEtypes_Len_t SsIdLen;

	UINT8 wpa_ie[MAX_IE_SIZE];
	UINT16 wpa_ielen;
	UINT8 rsn_ie[MAX_IE_SIZE];
	UINT16 rsn_ielen;
	UINT32 StaAgeOutTime;
	UINT32 PsStaAgeOutTime;

	/* If the BssAddr field is not aligned on word boundary the hal
	   functions which update mac registers are unsafe for non-word
	   aligned pointers. Avoid direct use of the pointer to BssId
	   field in the hal functions */
	/*  this field is no longer used and we use mibOpdata_p->StaMacAddr
	   in its place now */
	IEEEtypes_MacAddr_t EepromMacAddr_defunct;
	IEEEtypes_DataRate_t OpRateSet[IEEEtypes_MAX_DATA_RATES_G];

	// odd-sized ele clubbed together to keep even alignment
	UINT8 AuthType;
	UINT8 TxPowerLevel;
	IEEEtypes_DataRate_t TxDataRate;
	IEEEtypes_DataRate_t TxMCBCDataRate;
	UINT8 MaxStaSupported;

	SecurityMode_t SecType;
	UINT8 Padding1[1];	//****** Use this for adding new members *******
	BOOLEAN apWmmEn;
	IEEEtypes_WMM_ParamElement_t apWmmParamSet;

	BOOLEAN ap11nEn;

	cipher_key_buf_t *pWepKeyBuf;
	cipher_key_buf_t *pGtkKeyBuf;
	UINT8 ScanChanCount;
	UINT8 AclStaCnt;

	UINT8 Padding3[1];	//****** Use this for adding new members *******
	apRsnConfig_t RsnConfig;
	BOOLEAN apWmmPsEn;
	channelInfo_t ScanChanList[IEEEtypes_MAX_CHANNELS];	/* Channels to scan */
	CommonMlmeData_t comData;
	IEEEtypes_OBSS_ScanParam_t ObssScanParam;

	cipher_key_buf_t *piGtkKeyBuf;
	UINT32 mgmtFrameSubtypeFwdEn;
	UINT8 Ht2040CoexEn;	// Enable/Disable 2040 Coex feature in uAP

	UINT8 Padding4[1];	//****** Use this for adding new members *******

	wapi_ie_cfg_t wapiCfg;
	IEEEtypes_ExtCapability_t ExtCap;
	UINT8 Padding6[1];	//****** Use this for adding new members *******
} BssConfig_t;

typedef struct {
	BssConfig_t bssConfig;
	BssData_t bssData;
} apInfo_t;
#ifdef DRV_EMBEDDED_SUPPLICANT
typedef struct {
	/* This structure is ROM'd */

	UINT8 RSNEnabled:1;	/* WPA, WPA2 */
	UINT8 pmkidValid:1;	/* PMKID valid */
	UINT8 rsnCapValid:1;
	UINT8 grpMgmtCipherValid:1;
	UINT8 rsvd:4;		/* rsvd */

	SecurityMode_t wpaType;
	Cipher_t mcstCipher;
	Cipher_t ucstCipher;
	AkmSuite_t AKM;
	UINT8 PMKID[16];

	IEEEtypes_RSNCapability_t rsnCap;

	Cipher_t grpMgmtCipher;

} RSNConfig_t;

typedef struct {
	UINT8 ANonce[NONCE_SIZE];
	UINT8 SNonce[NONCE_SIZE];
	UINT8 EAPOL_MIC_Key[EAPOL_MIC_KEY_SIZE];
	UINT8 EAPOL_Encr_Key[EAPOL_ENCR_KEY_SIZE];
	UINT32 apCounterLo;	/* last valid replay counter from authenticator */
	UINT32 apCounterHi;
	UINT32 apCounterZeroDone;	/* have we processed replay == 0? */
	UINT32 staCounterLo;	/* counter used in request EAPOL frames */
	UINT32 staCounterHi;

	BOOLEAN RSNDataTrafficEnabled;	/* Enabled after 4way handshake */
	BOOLEAN RSNSecured;	/* Enabled after group key is established */
	BOOLEAN pwkHandshakeComplete;
	cipher_key_t *pRxDecryptKey;

	KeyData_t PWKey;
	KeyData_t GRKey;

	KeyData_t newPWKey;

	MIC_Error_t sta_MIC_Error;
	t_void *rsnTimer;
	t_void *micTimer;
	t_void *deauthDelayTimer;	/* hacked in to delay the deauth */

	//phostsa_private psapriv;

	KeyData_t IGtk;

} keyMgmtInfoSta_t;

typedef struct supplicantData {
	BOOLEAN inUse;
	BOOLEAN suppInitialized;
	IEEEtypes_SsIdElement_t hashSsId;
	IEEEtypes_MacAddr_t localBssid;
	IEEEtypes_MacAddr_t localStaAddr;
	customMIB_RSNStats_t customMIB_RSNStats;
	RSNConfig_t customMIB_RSNConfig;
	keyMgmtInfoSta_t keyMgmtInfoSta;
	SecurityParams_t currParams;
	UINT8 wpa_rsn_ie[MAX_IE_SIZE];
} supplicantData_t;
#endif

/** supplicant/authenticator private structure */
typedef struct _hostsa_private {
    /** pmlan_private */
	t_void *pmlan_private;
    /** pmlan_adapter */
	t_void *pmlan_adapter;
    /** Utility functions table */
	hostsa_util_fns util_fns;
    /** MLAN APIs table */
	hostsa_mlan_fns mlan_fns;
   /**apinf_t*/
	apInfo_t apinfo;
   /**group rekey timer*/
	t_void *GrpRekeytimer;
   /**Group rekey timer set flag*/
	t_u8 GrpRekeyTimerIsSet;
   /**local mac address*/
	t_u8 curr_addr[MLAN_MAC_ADDR_LENGTH];
#ifdef DRV_EMBEDDED_SUPPLICANT
   /**supplicant data*/
	supplicantData_t *suppData;
#endif
	/* GTK installed status */
	t_u8 gtk_installed;
} hostsa_private, *phostsa_private;
#endif
