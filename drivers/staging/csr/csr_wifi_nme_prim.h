/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_NME_PRIM_H__
#define CSR_WIFI_NME_PRIM_H__

#include "csr_types.h"
#include "csr_prim_defs.h"
#include "csr_sched.h"
#include "csr_wifi_common.h"
#include "csr_result.h"
#include "csr_wifi_fsm_event.h"
#include "csr_wifi_sme_prim.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CSR_WIFI_NME_ENABLE
#error CSR_WIFI_NME_ENABLE MUST be defined inorder to use csr_wifi_nme_prim.h
#endif

#define CSR_WIFI_NME_PRIM                                               (0x0424)

typedef CsrPrim CsrWifiNmePrim;

typedef void (*CsrWifiNmeFrameFreeFunction)(void *frame);

/*******************************************************************************

  NAME
    CsrWifiNmeAuthMode

  DESCRIPTION
    WiFi Authentication Mode

 VALUES
    CSR_WIFI_NME_AUTH_MODE_80211_OPEN
                   - Connects to an open system network (i.e. no authentication,
                     no encryption) or to a WEP enabled network.
    CSR_WIFI_NME_AUTH_MODE_80211_SHARED
                   - Connect to a WEP enabled network.
    CSR_WIFI_NME_AUTH_MODE_8021X_WPA
                   - Connects to a WPA Enterprise enabled network.
    CSR_WIFI_NME_AUTH_MODE_8021X_WPAPSK
                   - Connects to a WPA with Pre-Shared Key enabled network.
    CSR_WIFI_NME_AUTH_MODE_8021X_WPA2
                   - Connects to a WPA2 Enterprise enabled network.
    CSR_WIFI_NME_AUTH_MODE_8021X_WPA2PSK
                   - Connects to a WPA2 with Pre-Shared Key enabled network.
    CSR_WIFI_NME_AUTH_MODE_8021X_CCKM
                   - Connects to a CCKM enabled network.
    CSR_WIFI_NME_AUTH_MODE_WAPI_WAI
                   - Connects to a WAPI Enterprise enabled network.
    CSR_WIFI_NME_AUTH_MODE_WAPI_WAIPSK
                   - Connects to a WAPI with Pre-Shared Key enabled network.
    CSR_WIFI_NME_AUTH_MODE_8021X_OTHER1X
                   - For future use.

*******************************************************************************/
typedef u16 CsrWifiNmeAuthMode;
#define CSR_WIFI_NME_AUTH_MODE_80211_OPEN      ((CsrWifiNmeAuthMode) 0x0001)
#define CSR_WIFI_NME_AUTH_MODE_80211_SHARED    ((CsrWifiNmeAuthMode) 0x0002)
#define CSR_WIFI_NME_AUTH_MODE_8021X_WPA       ((CsrWifiNmeAuthMode) 0x0004)
#define CSR_WIFI_NME_AUTH_MODE_8021X_WPAPSK    ((CsrWifiNmeAuthMode) 0x0008)
#define CSR_WIFI_NME_AUTH_MODE_8021X_WPA2      ((CsrWifiNmeAuthMode) 0x0010)
#define CSR_WIFI_NME_AUTH_MODE_8021X_WPA2PSK   ((CsrWifiNmeAuthMode) 0x0020)
#define CSR_WIFI_NME_AUTH_MODE_8021X_CCKM      ((CsrWifiNmeAuthMode) 0x0040)
#define CSR_WIFI_NME_AUTH_MODE_WAPI_WAI        ((CsrWifiNmeAuthMode) 0x0080)
#define CSR_WIFI_NME_AUTH_MODE_WAPI_WAIPSK     ((CsrWifiNmeAuthMode) 0x0100)
#define CSR_WIFI_NME_AUTH_MODE_8021X_OTHER1X   ((CsrWifiNmeAuthMode) 0x0200)

/*******************************************************************************

  NAME
    CsrWifiNmeBssType

  DESCRIPTION
    Type of BSS

 VALUES
    CSR_WIFI_NME_BSS_TYPE_INFRASTRUCTURE
                   - Infrastructure BSS type where access to the network is via
                     one or several Access Points.
    CSR_WIFI_NME_BSS_TYPE_ADHOC
                   - Adhoc or Independent BSS Type where one Station acts as a
                     host and future stations can join the adhoc network without
                     needing an access point.
    CSR_WIFI_NME_BSS_TYPE_RESERVED
                   - To be in sync with SME.This is not used.
    CSR_WIFI_NME_BSS_TYPE_P2P
                   - P2P mode of operation.

*******************************************************************************/
typedef u8 CsrWifiNmeBssType;
#define CSR_WIFI_NME_BSS_TYPE_INFRASTRUCTURE   ((CsrWifiNmeBssType) 0x00)
#define CSR_WIFI_NME_BSS_TYPE_ADHOC            ((CsrWifiNmeBssType) 0x01)
#define CSR_WIFI_NME_BSS_TYPE_RESERVED         ((CsrWifiNmeBssType) 0x02)
#define CSR_WIFI_NME_BSS_TYPE_P2P              ((CsrWifiNmeBssType) 0x03)

/*******************************************************************************

  NAME
    CsrWifiNmeCcxOptionsMask

  DESCRIPTION
    Enumeration type defining possible mask values for setting CCX options.

 VALUES
    CSR_WIFI_NME_CCX_OPTION_NONE - No CCX option is set.
    CSR_WIFI_NME_CCX_OPTION_CCKM - CCX option cckm is set.

*******************************************************************************/
typedef u8 CsrWifiNmeCcxOptionsMask;
#define CSR_WIFI_NME_CCX_OPTION_NONE   ((CsrWifiNmeCcxOptionsMask) 0x00)
#define CSR_WIFI_NME_CCX_OPTION_CCKM   ((CsrWifiNmeCcxOptionsMask) 0x01)

/*******************************************************************************

  NAME
    CsrWifiNmeConfigAction

  DESCRIPTION

 VALUES
    CSR_WIFI_PIN_ENTRY_PUSH_BUTTON -
    CSR_WIFI_PIN_ENTRY_DISPLAY_PIN -
    CSR_WIFI_PIN_ENTRY_ENTER_PIN   -

*******************************************************************************/
typedef u8 CsrWifiNmeConfigAction;
#define CSR_WIFI_PIN_ENTRY_PUSH_BUTTON   ((CsrWifiNmeConfigAction) 0x00)
#define CSR_WIFI_PIN_ENTRY_DISPLAY_PIN   ((CsrWifiNmeConfigAction) 0x01)
#define CSR_WIFI_PIN_ENTRY_ENTER_PIN     ((CsrWifiNmeConfigAction) 0x02)

/*******************************************************************************

  NAME
    CsrWifiNmeConnectionStatus

  DESCRIPTION
    Indicate the NME Connection Status when connecting or when disconnecting

 VALUES
    CSR_WIFI_NME_CONNECTION_STATUS_CONNECTION_STATUS_DISCONNECTED
                   - NME is disconnected.
    CSR_WIFI_NME_CONNECTION_STATUS_CONNECTION_STATUS_CONNECTING
                   - NME is in the process of connecting.
    CSR_WIFI_NME_CONNECTION_STATUS_CONNECTION_STATUS_AUTHENTICATING
                   - NME is in the authentication stage of a connection attempt.
    CSR_WIFI_NME_CONNECTION_STATUS_CONNECTION_STATUS_CONNECTED
                   - NME is connected.
    CSR_WIFI_NME_CONNECTION_STATUS_CONNECTION_STATUS_DISCONNECTING
                   - NME is in the process of disconnecting.

*******************************************************************************/
typedef u8 CsrWifiNmeConnectionStatus;
#define CSR_WIFI_NME_CONNECTION_STATUS_CONNECTION_STATUS_DISCONNECTED     ((CsrWifiNmeConnectionStatus) 0x00)
#define CSR_WIFI_NME_CONNECTION_STATUS_CONNECTION_STATUS_CONNECTING       ((CsrWifiNmeConnectionStatus) 0x01)
#define CSR_WIFI_NME_CONNECTION_STATUS_CONNECTION_STATUS_AUTHENTICATING   ((CsrWifiNmeConnectionStatus) 0x02)
#define CSR_WIFI_NME_CONNECTION_STATUS_CONNECTION_STATUS_CONNECTED        ((CsrWifiNmeConnectionStatus) 0x03)
#define CSR_WIFI_NME_CONNECTION_STATUS_CONNECTION_STATUS_DISCONNECTING    ((CsrWifiNmeConnectionStatus) 0x04)

/*******************************************************************************

  NAME
    CsrWifiNmeCredentialType

  DESCRIPTION
    NME Credential Types

 VALUES
    CSR_WIFI_NME_CREDENTIAL_TYPE_OPEN_SYSTEM
                   - Credential Type Open System.
    CSR_WIFI_NME_CREDENTIAL_TYPE_WEP64
                   - Credential Type WEP-64
    CSR_WIFI_NME_CREDENTIAL_TYPE_WEP128
                   - Credential Type WEP-128
    CSR_WIFI_NME_CREDENTIAL_TYPE_WPA_PSK
                   - Credential Type WPA Pre-Shared Key
    CSR_WIFI_NME_CREDENTIAL_TYPE_WPA_PASSPHRASE
                   - Credential Type WPA pass phrase
    CSR_WIFI_NME_CREDENTIAL_TYPE_WPA2_PSK
                   - Credential Type WPA2 Pre-Shared Key.
    CSR_WIFI_NME_CREDENTIAL_TYPE_WPA2_PASSPHRASE
                   - Credential Type WPA2 pass phrase
    CSR_WIFI_NME_CREDENTIAL_TYPE_WAPI_PSK
                   - Credential Type WAPI Pre-Shared Key.
    CSR_WIFI_NME_CREDENTIAL_TYPE_WAPI_PASSPHRASE
                   - Credential Type WAPI pass phrase
    CSR_WIFI_NME_CREDENTIAL_TYPE_WAPI
                   - Credential Type WAPI certificates
    CSR_WIFI_NME_CREDENTIAL_TYPE_8021X
                   - Credential Type 802.1X: the associated type supports
                     FAST/LEAP/TLS/TTLS/PEAP/etc.

*******************************************************************************/
typedef u16 CsrWifiNmeCredentialType;
#define CSR_WIFI_NME_CREDENTIAL_TYPE_OPEN_SYSTEM       ((CsrWifiNmeCredentialType) 0x0000)
#define CSR_WIFI_NME_CREDENTIAL_TYPE_WEP64             ((CsrWifiNmeCredentialType) 0x0001)
#define CSR_WIFI_NME_CREDENTIAL_TYPE_WEP128            ((CsrWifiNmeCredentialType) 0x0002)
#define CSR_WIFI_NME_CREDENTIAL_TYPE_WPA_PSK           ((CsrWifiNmeCredentialType) 0x0003)
#define CSR_WIFI_NME_CREDENTIAL_TYPE_WPA_PASSPHRASE    ((CsrWifiNmeCredentialType) 0x0004)
#define CSR_WIFI_NME_CREDENTIAL_TYPE_WPA2_PSK          ((CsrWifiNmeCredentialType) 0x0005)
#define CSR_WIFI_NME_CREDENTIAL_TYPE_WPA2_PASSPHRASE   ((CsrWifiNmeCredentialType) 0x0006)
#define CSR_WIFI_NME_CREDENTIAL_TYPE_WAPI_PSK          ((CsrWifiNmeCredentialType) 0x0007)
#define CSR_WIFI_NME_CREDENTIAL_TYPE_WAPI_PASSPHRASE   ((CsrWifiNmeCredentialType) 0x0008)
#define CSR_WIFI_NME_CREDENTIAL_TYPE_WAPI              ((CsrWifiNmeCredentialType) 0x0009)
#define CSR_WIFI_NME_CREDENTIAL_TYPE_8021X             ((CsrWifiNmeCredentialType) 0x000A)

/*******************************************************************************

  NAME
    CsrWifiNmeEapMethod

  DESCRIPTION
    Outer EAP method with possibly inner method.

 VALUES
    CSR_WIFI_NME_EAP_METHOD_TLS
                   - EAP-TLS Method.
    CSR_WIFI_NME_EAP_METHOD_TTLS_MSCHAPV2
                   - EAP-TTLS Method with MSCHAPV2.
    CSR_WIFI_NME_EAP_METHOD_PEAP_GTC
                   - EAP-PEAP Method with GTC.
    CSR_WIFI_NME_EAP_METHOD_PEAP_MSCHAPV2
                   - EAP-PEAP Method with MSCHAPV2.
    CSR_WIFI_NME_EAP_METHOD_SIM
                   - EAP-SIM Method.
    CSR_WIFI_NME_EAP_METHOD_AKA
                   - EAP-AKA Method.
    CSR_WIFI_NME_EAP_METHOD_FAST_GTC
                   - EAP-FAST Method with GTC.
    CSR_WIFI_NME_EAP_METHOD_FAST_MSCHAPV2
                   - EAP-FAST Method with MSCHAPV2.
    CSR_WIFI_NME_EAP_METHOD_LEAP
                   - EAP-LEAP Method.

*******************************************************************************/
typedef u16 CsrWifiNmeEapMethod;
#define CSR_WIFI_NME_EAP_METHOD_TLS             ((CsrWifiNmeEapMethod) 0x0001)
#define CSR_WIFI_NME_EAP_METHOD_TTLS_MSCHAPV2   ((CsrWifiNmeEapMethod) 0x0002)
#define CSR_WIFI_NME_EAP_METHOD_PEAP_GTC        ((CsrWifiNmeEapMethod) 0x0004)
#define CSR_WIFI_NME_EAP_METHOD_PEAP_MSCHAPV2   ((CsrWifiNmeEapMethod) 0x0008)
#define CSR_WIFI_NME_EAP_METHOD_SIM             ((CsrWifiNmeEapMethod) 0x0010)
#define CSR_WIFI_NME_EAP_METHOD_AKA             ((CsrWifiNmeEapMethod) 0x0020)
#define CSR_WIFI_NME_EAP_METHOD_FAST_GTC        ((CsrWifiNmeEapMethod) 0x0040)
#define CSR_WIFI_NME_EAP_METHOD_FAST_MSCHAPV2   ((CsrWifiNmeEapMethod) 0x0080)
#define CSR_WIFI_NME_EAP_METHOD_LEAP            ((CsrWifiNmeEapMethod) 0x0100)

/*******************************************************************************

  NAME
    CsrWifiNmeEncryption

  DESCRIPTION
    WiFi Encryption method

 VALUES
    CSR_WIFI_NME_ENCRYPTION_CIPHER_NONE
                   - No encryprion set.
    CSR_WIFI_NME_ENCRYPTION_CIPHER_PAIRWISE_WEP40
                   - 40 bytes WEP key for peer to peer communication.
    CSR_WIFI_NME_ENCRYPTION_CIPHER_PAIRWISE_WEP104
                   - 104 bytes WEP key for peer to peer communication.
    CSR_WIFI_NME_ENCRYPTION_CIPHER_PAIRWISE_TKIP
                   - TKIP key for peer to peer communication.
    CSR_WIFI_NME_ENCRYPTION_CIPHER_PAIRWISE_CCMP
                   - CCMP key for peer to peer communication.
    CSR_WIFI_NME_ENCRYPTION_CIPHER_PAIRWISE_SMS4
                   - SMS4 key for peer to peer communication.
    CSR_WIFI_NME_ENCRYPTION_CIPHER_GROUP_WEP40
                   - 40 bytes WEP key for broadcast messages.
    CSR_WIFI_NME_ENCRYPTION_CIPHER_GROUP_WEP104
                   - 104 bytes WEP key for broadcast messages.
    CSR_WIFI_NME_ENCRYPTION_CIPHER_GROUP_TKIP
                   - TKIP key for broadcast messages.
    CSR_WIFI_NME_ENCRYPTION_CIPHER_GROUP_CCMP
                   - CCMP key for broadcast messages
    CSR_WIFI_NME_ENCRYPTION_CIPHER_GROUP_SMS4
                   - SMS4 key for broadcast messages.

*******************************************************************************/
typedef u16 CsrWifiNmeEncryption;
#define CSR_WIFI_NME_ENCRYPTION_CIPHER_NONE              ((CsrWifiNmeEncryption) 0x0000)
#define CSR_WIFI_NME_ENCRYPTION_CIPHER_PAIRWISE_WEP40    ((CsrWifiNmeEncryption) 0x0001)
#define CSR_WIFI_NME_ENCRYPTION_CIPHER_PAIRWISE_WEP104   ((CsrWifiNmeEncryption) 0x0002)
#define CSR_WIFI_NME_ENCRYPTION_CIPHER_PAIRWISE_TKIP     ((CsrWifiNmeEncryption) 0x0004)
#define CSR_WIFI_NME_ENCRYPTION_CIPHER_PAIRWISE_CCMP     ((CsrWifiNmeEncryption) 0x0008)
#define CSR_WIFI_NME_ENCRYPTION_CIPHER_PAIRWISE_SMS4     ((CsrWifiNmeEncryption) 0x0010)
#define CSR_WIFI_NME_ENCRYPTION_CIPHER_GROUP_WEP40       ((CsrWifiNmeEncryption) 0x0020)
#define CSR_WIFI_NME_ENCRYPTION_CIPHER_GROUP_WEP104      ((CsrWifiNmeEncryption) 0x0040)
#define CSR_WIFI_NME_ENCRYPTION_CIPHER_GROUP_TKIP        ((CsrWifiNmeEncryption) 0x0080)
#define CSR_WIFI_NME_ENCRYPTION_CIPHER_GROUP_CCMP        ((CsrWifiNmeEncryption) 0x0100)
#define CSR_WIFI_NME_ENCRYPTION_CIPHER_GROUP_SMS4        ((CsrWifiNmeEncryption) 0x0200)

/*******************************************************************************

  NAME
    CsrWifiNmeIndications

  DESCRIPTION
    NME indications

 VALUES
    CSR_WIFI_NME_INDICATIONS_IND_AP_STATION
                   - NME AP Station Indication.
    CSR_WIFI_NME_INDICATIONS_IND_AP_STOP
                   - NME AP Stop Indication.
    CSR_WIFI_NME_INDICATIONS_IND_SIM_UMTS_AUTH
                   - NME UMTS Authentication Indication.
    CSR_WIFI_NME_INDICATIONS_IND_P2P_GROUP_START
                   - NME P2P Group Start Indication.
    CSR_WIFI_NME_INDICATIONS_IND_P2P_GROUP_STATUS
                   - NME P2P Group Status Indication.
    CSR_WIFI_NME_INDICATIONS_IND_P2P_GROUP_ROLE
                   - NME P2P Group Role Indication.
    CSR_WIFI_NME_INDICATIONS_IND_PROFILE_DISCONNECT
                   - NME Profile Disconnect Indication.
    CSR_WIFI_NME_INDICATIONS_IND_PROFILE_UPDATE
                   - NME Profile Update Indication.
    CSR_WIFI_NME_INDICATIONS_IND_SIM_IMSI_GET
                   - NME GET IMSI Indication.
    CSR_WIFI_NME_INDICATIONS_IND_SIM_GSM_AUTH
                   - NME GSM Authentication Indication.
    CSR_WIFI_NME_INDICATIONS_ALL
                   - Used to register for all available indications

*******************************************************************************/
typedef CsrUint32 CsrWifiNmeIndications;
#define CSR_WIFI_NME_INDICATIONS_IND_AP_STATION           ((CsrWifiNmeIndications) 0x00100000)
#define CSR_WIFI_NME_INDICATIONS_IND_AP_STOP              ((CsrWifiNmeIndications) 0x00200000)
#define CSR_WIFI_NME_INDICATIONS_IND_SIM_UMTS_AUTH        ((CsrWifiNmeIndications) 0x01000000)
#define CSR_WIFI_NME_INDICATIONS_IND_P2P_GROUP_START      ((CsrWifiNmeIndications) 0x02000000)
#define CSR_WIFI_NME_INDICATIONS_IND_P2P_GROUP_STATUS     ((CsrWifiNmeIndications) 0x04000000)
#define CSR_WIFI_NME_INDICATIONS_IND_P2P_GROUP_ROLE       ((CsrWifiNmeIndications) 0x08000000)
#define CSR_WIFI_NME_INDICATIONS_IND_PROFILE_DISCONNECT   ((CsrWifiNmeIndications) 0x10000000)
#define CSR_WIFI_NME_INDICATIONS_IND_PROFILE_UPDATE       ((CsrWifiNmeIndications) 0x20000000)
#define CSR_WIFI_NME_INDICATIONS_IND_SIM_IMSI_GET         ((CsrWifiNmeIndications) 0x40000000)
#define CSR_WIFI_NME_INDICATIONS_IND_SIM_GSM_AUTH         ((CsrWifiNmeIndications) 0x80000000)
#define CSR_WIFI_NME_INDICATIONS_ALL                      ((CsrWifiNmeIndications) 0xFFFFFFFF)

/*******************************************************************************

  NAME
    CsrWifiNmeSecError

  DESCRIPTION
    NME Security Errors
    place holder for the security library abort reason

 VALUES
    CSR_WIFI_NME_SEC_ERROR_SEC_ERROR_UNKNOWN
                   - Unknown Security Error.

*******************************************************************************/
typedef u8 CsrWifiNmeSecError;
#define CSR_WIFI_NME_SEC_ERROR_SEC_ERROR_UNKNOWN   ((CsrWifiNmeSecError) 0x00)

/*******************************************************************************

  NAME
    CsrWifiNmeSimCardType

  DESCRIPTION
    (U)SIM Card (or UICC) types

 VALUES
    CSR_WIFI_NME_SIM_CARD_TYPE_2G   - 2G SIM card, capable of performing GSM
                                      authentication only.
    CSR_WIFI_NME_SIM_CARD_TYPE_3G   - UICC supporting USIM application, capable
                                      of performing UMTS authentication only.
    CSR_WIFI_NME_SIM_CARD_TYPE_2G3G - UICC supporting both USIM and SIM
                                      applications, capable of performing both
                                      UMTS and GSM authentications.

*******************************************************************************/
typedef u8 CsrWifiNmeSimCardType;
#define CSR_WIFI_NME_SIM_CARD_TYPE_2G     ((CsrWifiNmeSimCardType) 0x01)
#define CSR_WIFI_NME_SIM_CARD_TYPE_3G     ((CsrWifiNmeSimCardType) 0x02)
#define CSR_WIFI_NME_SIM_CARD_TYPE_2G3G   ((CsrWifiNmeSimCardType) 0x03)

/*******************************************************************************

  NAME
    CsrWifiNmeUmtsAuthResult

  DESCRIPTION
    Only relevant for UMTS Authentication. It indicates if the UICC has
    successfully authenticated the network or otherwise.

 VALUES
    CSR_WIFI_NME_UMTS_AUTH_RESULT_SUCCESS
                   - Successful outcome from USIM indicating that the card has
                     successfully authenticated the network.
    CSR_WIFI_NME_UMTS_AUTH_RESULT_SYNC_FAIL
                   - Unsuccessful outcome from USIM indicating that the card is
                     requesting the network to synchronise and re-try again. If
                     no further request is received an NME timer will expire and
                     the authentication is aborted.
    CSR_WIFI_NME_UMTS_AUTH_RESULT_REJECT
                   - Unsuccessful outcome from USIM indicating that the card has
                     rejected the network and that the authentication is
                     aborted.

*******************************************************************************/
typedef u8 CsrWifiNmeUmtsAuthResult;
#define CSR_WIFI_NME_UMTS_AUTH_RESULT_SUCCESS     ((CsrWifiNmeUmtsAuthResult) 0x00)
#define CSR_WIFI_NME_UMTS_AUTH_RESULT_SYNC_FAIL   ((CsrWifiNmeUmtsAuthResult) 0x01)
#define CSR_WIFI_NME_UMTS_AUTH_RESULT_REJECT      ((CsrWifiNmeUmtsAuthResult) 0x02)

/*******************************************************************************

  NAME
    CsrWifiNmeWmmQosInfo

  DESCRIPTION
    Defines bits for the QoS Info octect as defined in the WMM specification.
    The values of this type are used across the NME/SME/Router API's and they
    must be kept consistent with the corresponding types in the .xml of the
    other interfaces

 VALUES
    CSR_WIFI_NME_WMM_QOS_INFO_AC_MAX_SP_ALL
                   - WMM AP may deliver all buffered frames.
    CSR_WIFI_NME_WMM_QOS_INFO_AC_VO
                   - To enable the triggering and delivery of QoS Voice.
    CSR_WIFI_NME_WMM_QOS_INFO_AC_VI
                   - To enable the triggering and delivery of QoS Video.
    CSR_WIFI_NME_WMM_QOS_INFO_AC_BK
                   - To enable the triggering and delivery of QoS Background.
    CSR_WIFI_NME_WMM_QOS_INFO_AC_BE
                   - To enable the triggering and delivery of QoS Best Effort.
    CSR_WIFI_NME_WMM_QOS_INFO_AC_MAX_SP_TWO
                   - WMM AP may deliver a maximum of 2 buffered frames per
                     Unscheduled Service Period (USP).
    CSR_WIFI_NME_WMM_QOS_INFO_AC_MAX_SP_FOUR
                   - WMM AP may deliver a maximum of 4 buffered frames per USP.
    CSR_WIFI_NME_WMM_QOS_INFO_AC_MAX_SP_SIX
                   - WMM AP may deliver a maximum of 6 buffered frames per USP.

*******************************************************************************/
typedef u8 CsrWifiNmeWmmQosInfo;
#define CSR_WIFI_NME_WMM_QOS_INFO_AC_MAX_SP_ALL    ((CsrWifiNmeWmmQosInfo) 0x00)
#define CSR_WIFI_NME_WMM_QOS_INFO_AC_VO            ((CsrWifiNmeWmmQosInfo) 0x01)
#define CSR_WIFI_NME_WMM_QOS_INFO_AC_VI            ((CsrWifiNmeWmmQosInfo) 0x02)
#define CSR_WIFI_NME_WMM_QOS_INFO_AC_BK            ((CsrWifiNmeWmmQosInfo) 0x04)
#define CSR_WIFI_NME_WMM_QOS_INFO_AC_BE            ((CsrWifiNmeWmmQosInfo) 0x08)
#define CSR_WIFI_NME_WMM_QOS_INFO_AC_MAX_SP_TWO    ((CsrWifiNmeWmmQosInfo) 0x20)
#define CSR_WIFI_NME_WMM_QOS_INFO_AC_MAX_SP_FOUR   ((CsrWifiNmeWmmQosInfo) 0x40)
#define CSR_WIFI_NME_WMM_QOS_INFO_AC_MAX_SP_SIX    ((CsrWifiNmeWmmQosInfo) 0x60)


/*******************************************************************************

  NAME
    CsrWifiNmeEapMethodMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiNmeEapMethod.

*******************************************************************************/
typedef u16 CsrWifiNmeEapMethodMask;
/*******************************************************************************

  NAME
    CsrWifiNmeEncryptionMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiNmeEncryption

*******************************************************************************/
typedef u16 CsrWifiNmeEncryptionMask;
/*******************************************************************************

  NAME
    CsrWifiNmeIndicationsMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiNmeIndications

*******************************************************************************/
typedef CsrUint32 CsrWifiNmeIndicationsMask;
/*******************************************************************************

  NAME
    CsrWifiNmeNmeIndicationsMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiNmeNmeIndications.
    Used to overlap the unused portion of the unifi_IndicationsMask For NME
    specific indications

*******************************************************************************/
typedef CsrUint32 CsrWifiNmeNmeIndicationsMask;
/*******************************************************************************

  NAME
    CsrWifiNmeWmmQosInfoMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiNmeWmmQosInfo

*******************************************************************************/
typedef u8 CsrWifiNmeWmmQosInfoMask;


/*******************************************************************************

  NAME
    CsrWifiNmeEmpty

  DESCRIPTION
    Empty Structure to indicate that no credentials are available.

  MEMBERS
    empty  - Only element of the empty structure (always set to 0).

*******************************************************************************/
typedef struct
{
    u8 empty;
} CsrWifiNmeEmpty;

/*******************************************************************************

  NAME
    CsrWifiNmePassphrase

  DESCRIPTION
    Structure holding the ASCII Pass Phrase data.

  MEMBERS
    encryptionMode - Encryption type as defined in CsrWifiSmeEncryption.
    passphrase     - Pass phrase ASCII value.

*******************************************************************************/
typedef struct
{
    u16      encryptionMode;
    CsrCharString *passphrase;
} CsrWifiNmePassphrase;

/*******************************************************************************

  NAME
    CsrWifiNmePsk

  DESCRIPTION
    Structure holding the Pre-Shared Key data.

  MEMBERS
    encryptionMode - Encryption type as defined in CsrWifiSmeEncryption.
    psk            - Pre-Shared Key value.

*******************************************************************************/
typedef struct
{
    u16 encryptionMode;
    u8  psk[32];
} CsrWifiNmePsk;

/*******************************************************************************

  NAME
    CsrWifiNmeWapiCredentials

  DESCRIPTION
    Structure holding WAPI credentials data.

  MEMBERS
    certificateLength   - Length in bytes of the following client certificate.
    certificate         - The actual client certificate data (if present).
                          DER/PEM format supported.
    privateKeyLength    - Length in bytes of the following private key.
    privateKey          - The actual private key. DER/PEM format.
    caCertificateLength - Length in bytes of the following certificate authority
                          certificate.
    caCertificate       - The actual certificate authority certificate data. If
                          not supplied the received certificate authority
                          certificate is assumed to be validate, if present the
                          received certificate is validated against it. DER/PEM
                          format supported.

*******************************************************************************/
typedef struct
{
    CsrUint32 certificateLength;
    u8 *certificate;
    u16 privateKeyLength;
    u8 *privateKey;
    CsrUint32 caCertificateLength;
    u8 *caCertificate;
} CsrWifiNmeWapiCredentials;

/*******************************************************************************

  NAME
    CsrWifiNmeConnectAttempt

  DESCRIPTION
    Structure holding Connection attempt data.

  MEMBERS
    bssid         - Id of Basic Service Set connections attempt have been made
                    to.
    status        - Status returned to indicate the success or otherwise of the
                    connection attempt.
    securityError - Security error status indicating the nature of the failure
                    to connect.

*******************************************************************************/
typedef struct
{
    CsrWifiMacAddress  bssid;
    CsrResult          status;
    CsrWifiNmeSecError securityError;
} CsrWifiNmeConnectAttempt;

/*******************************************************************************

  NAME
    CsrWifiNmeEapCredentials

  DESCRIPTION
    Supports the use of multiple EAP methods via a single structure. The
    methods required are indicated by the value set in the eapMethodMask

  MEMBERS
    eapMethodMask
                   - Bit mask of supported EAP methods
                     Currently only supports the setting of one bit.
                     Required for all the EAP methods.
    authMode
                   - Bit mask representing the authentication types that may be
                     supported by a suitable AP. An AP must support at least one
                     of the authentication types specified to be considered for
                     connection. Required for all EAP methods.
    encryptionMode
                   - Bit mask representing the encryption types that may be
                     supported by a suitable AP. An AP must support a suitable
                     mix of the pairwise and group encryption types requested to
                     be considered for connection. Required for all EAP methods.
    userName
                   - User name. Required for all EAP methods except: SIM or AKA.
    userPassword
                   - User Password. Required for all EAP methods except: TLS,
                     SIM or AKA.
    authServerUserIdentity
                   - Authentication server user Identity. Required for all EAP
                     methods except: TLS, SIM, AKA or FAST.
    clientCertificateLength
                   - Length in bytes of the following client certificate (if
                     present). Only required for TLS.
    clientCertificate
                   - The actual client certificate data (if present). Only
                     required for TLS. DER/PEM format supported.
    certificateAuthorityCertificateLength
                   - Length in bytes of the following certificate authority
                     certificate (if present). Optional for TLS, TTLS, PEAP.
    certificateAuthorityCertificate
                   - The actual certificate authority certificate data (if
                     present). If not supplied the received certificate
                     authority certificate is assumed to be valid, if present
                     the received certificate is validated against it. Optional
                     for TLS, TTLS, PEAP. DER/PEM format supported.
    privateKeyLength
                   - Length in bytes of the following private key (if present).
                     Only required for TLS.
    privateKey
                   - The actual private key (if present). Only required for TLS.
                     DER/PEM format, maybe password protected.
    privateKeyPassword
                   - Optional password to protect the private key.
    sessionLength
                   - Length in bytes of the following session field Supported
                     for all EAP methods except: SIM or AKA.
    session
                   - Session information to support faster re-authentication.
                     Supported for all EAP methods except: SIM or AKA.
    allowPacProvisioning
                   - If TRUE: PAC provisioning is allowed 'over-the_air';
                     If FALSE: a PAC must be supplied.
                     Only required for FAST.
    pacLength
                   - Length the following PAC field. If allowPacProvisioning is
                     FALSE then the PAC MUST be supplied (i.e. non-zero). Only
                     required for FAST.
    pac
                   - The actual PAC data. If allowPacProvisioning is FALSE then
                     the PAC MUST be supplied. Only required for FAST.
    pacPassword
                   - Optional password to protect the PAC. Only required for
                     FAST.

*******************************************************************************/
typedef struct
{
    CsrWifiNmeEapMethodMask  eapMethodMask;
    CsrWifiSmeAuthModeMask   authMode;
    CsrWifiNmeEncryptionMask encryptionMode;
    CsrCharString           *userName;
    CsrCharString           *userPassword;
    CsrCharString           *authServerUserIdentity;
    CsrUint32                clientCertificateLength;
    u8                *clientCertificate;
    CsrUint32                certificateAuthorityCertificateLength;
    u8                *certificateAuthorityCertificate;
    u16                privateKeyLength;
    u8                *privateKey;
    CsrCharString           *privateKeyPassword;
    CsrUint32                sessionLength;
    u8                *session;
    CsrBool                  allowPacProvisioning;
    CsrUint32                pacLength;
    u8                *pac;
    CsrCharString           *pacPassword;
} CsrWifiNmeEapCredentials;

/*******************************************************************************

  NAME
    CsrWifiNmePeerConfig

  DESCRIPTION
    Structure holding Peer Config data.

  MEMBERS
    p2pDeviceId         -
    groupCapabilityMask -
    groupOwnerIntent    -

*******************************************************************************/
typedef struct
{
    CsrWifiMacAddress                p2pDeviceId;
    CsrWifiSmeP2pGroupCapabilityMask groupCapabilityMask;
    u8                         groupOwnerIntent;
} CsrWifiNmePeerConfig;

/*******************************************************************************

  NAME
    CsrWifiNmeProfileIdentity

  DESCRIPTION
    The identity of a profile is defined as the unique combination the BSSID
    and SSID.

  MEMBERS
    bssid  - ID of Basic Service Set for or the P2pDevice address of the GO for
             which a connection attempt was made.
    ssid   - Service Set Id.

*******************************************************************************/
typedef struct
{
    CsrWifiMacAddress bssid;
    CsrWifiSsid       ssid;
} CsrWifiNmeProfileIdentity;

/*******************************************************************************

  NAME
    CsrWifiNmeWep128Keys

  DESCRIPTION
    Structure holding WEP Authentication Type and WEP keys that can be used
    when using WEP128.

  MEMBERS
    wepAuthType    - Mask to select the WEP authentication type (Open or Shared)
    selectedWepKey - Index to one of the four keys below indicating the
                     currently used WEP key.
    key1           - Value for key number 1.
    key2           - Value for key number 2.
    key3           - Value for key number 3.
    key4           - Value for key number 4.

*******************************************************************************/
typedef struct
{
    CsrWifiSmeAuthModeMask wepAuthType;
    u8               selectedWepKey;
    u8               key1[13];
    u8               key2[13];
    u8               key3[13];
    u8               key4[13];
} CsrWifiNmeWep128Keys;

/*******************************************************************************

  NAME
    CsrWifiNmeWep64Keys

  DESCRIPTION
    Structure for holding WEP Authentication Type and WEP keys that can be
    used when using WEP64.

  MEMBERS
    wepAuthType    - Mask to select the WEP authentication type (Open or Shared)
    selectedWepKey - Index to one of the four keys below indicating the
                     currently used WEP key.
    key1           - Value for key number 1.
    key2           - Value for key number 2.
    key3           - Value for key number 3.
    key4           - Value for key number 4.

*******************************************************************************/
typedef struct
{
    CsrWifiSmeAuthModeMask wepAuthType;
    u8               selectedWepKey;
    u8               key1[5];
    u8               key2[5];
    u8               key3[5];
    u8               key4[5];
} CsrWifiNmeWep64Keys;

/*******************************************************************************

  NAME
    CsrWifiNmeCredentials

  DESCRIPTION
    Structure containing the Credentials data.

  MEMBERS
    credentialType            - Credential type value (as defined in the
                                enumeration type).
    credential                - Union containing credentials which depends on
                                credentialType parameter.
    credentialeap             -
    credentialwapiPassphrase  -
    credentialwpa2Passphrase  -
    credentialwpa2Psk         -
    credentialwapiPsk         -
    credentialwpaPassphrase   -
    credentialwapi            -
    credentialwep128Key       -
    credentialwpaPsk          -
    credentialopenSystem      -
    credentialwep64Key        -

*******************************************************************************/
typedef struct
{
    CsrWifiNmeCredentialType credentialType;
    union {
        CsrWifiNmeEapCredentials  eap;
        CsrWifiNmePassphrase      wapiPassphrase;
        CsrWifiNmePassphrase      wpa2Passphrase;
        CsrWifiNmePsk             wpa2Psk;
        CsrWifiNmePsk             wapiPsk;
        CsrWifiNmePassphrase      wpaPassphrase;
        CsrWifiNmeWapiCredentials wapi;
        CsrWifiNmeWep128Keys      wep128Key;
        CsrWifiNmePsk             wpaPsk;
        CsrWifiNmeEmpty           openSystem;
        CsrWifiNmeWep64Keys       wep64Key;
    } credential;
} CsrWifiNmeCredentials;

/*******************************************************************************

  NAME
    CsrWifiNmeProfile

  DESCRIPTION
    Structure containing the Profile data.

  MEMBERS
    profileIdentity - Profile Identity.
    wmmQosInfoMask  - Mask for WMM QoS information.
    bssType         - Type of BSS (Infrastructure or Adhoc).
    channelNo       - Channel Number.
    ccxOptionsMask  - Options mask for Cisco Compatible Extentions.
    cloakedSsid     - Flag to decide whether the SSID is cloaked (not
                      transmitted) or not.
    credentials     - Credentials data.

*******************************************************************************/
typedef struct
{
    CsrWifiNmeProfileIdentity profileIdentity;
    CsrWifiNmeWmmQosInfoMask  wmmQosInfoMask;
    CsrWifiNmeBssType         bssType;
    u8                  channelNo;
    u8                  ccxOptionsMask;
    CsrBool                   cloakedSsid;
    CsrWifiNmeCredentials     credentials;
} CsrWifiNmeProfile;


/* Downstream */
#define CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST            (0x0000)

#define CSR_WIFI_NME_PROFILE_SET_REQ                      ((CsrWifiNmePrim) (0x0000 + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_PROFILE_DELETE_REQ                   ((CsrWifiNmePrim) (0x0001 + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_PROFILE_DELETE_ALL_REQ               ((CsrWifiNmePrim) (0x0002 + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_PROFILE_ORDER_SET_REQ                ((CsrWifiNmePrim) (0x0003 + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_PROFILE_CONNECT_REQ                  ((CsrWifiNmePrim) (0x0004 + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_WPS_REQ                              ((CsrWifiNmePrim) (0x0005 + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_WPS_CANCEL_REQ                       ((CsrWifiNmePrim) (0x0006 + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_CONNECTION_STATUS_GET_REQ            ((CsrWifiNmePrim) (0x0007 + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_SIM_IMSI_GET_RES                     ((CsrWifiNmePrim) (0x0008 + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_SIM_GSM_AUTH_RES                     ((CsrWifiNmePrim) (0x0009 + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_SIM_UMTS_AUTH_RES                    ((CsrWifiNmePrim) (0x000A + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_WPS_CONFIG_SET_REQ                   ((CsrWifiNmePrim) (0x000B + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_EVENT_MASK_SET_REQ                   ((CsrWifiNmePrim) (0x000C + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST))


#define CSR_WIFI_NME_PRIM_DOWNSTREAM_HIGHEST           (0x000C + CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST)

/* Upstream */
#define CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST              (0x0000 + CSR_PRIM_UPSTREAM)

#define CSR_WIFI_NME_PROFILE_SET_CFM                      ((CsrWifiNmePrim)(0x0000 + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_PROFILE_DELETE_CFM                   ((CsrWifiNmePrim)(0x0001 + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_PROFILE_DELETE_ALL_CFM               ((CsrWifiNmePrim)(0x0002 + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_PROFILE_ORDER_SET_CFM                ((CsrWifiNmePrim)(0x0003 + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_PROFILE_CONNECT_CFM                  ((CsrWifiNmePrim)(0x0004 + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_WPS_CFM                              ((CsrWifiNmePrim)(0x0005 + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_WPS_CANCEL_CFM                       ((CsrWifiNmePrim)(0x0006 + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_CONNECTION_STATUS_GET_CFM            ((CsrWifiNmePrim)(0x0007 + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_PROFILE_UPDATE_IND                   ((CsrWifiNmePrim)(0x0008 + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_PROFILE_DISCONNECT_IND               ((CsrWifiNmePrim)(0x0009 + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_SIM_IMSI_GET_IND                     ((CsrWifiNmePrim)(0x000A + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_SIM_GSM_AUTH_IND                     ((CsrWifiNmePrim)(0x000B + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_SIM_UMTS_AUTH_IND                    ((CsrWifiNmePrim)(0x000C + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_WPS_CONFIG_SET_CFM                   ((CsrWifiNmePrim)(0x000D + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_EVENT_MASK_SET_CFM                   ((CsrWifiNmePrim)(0x000E + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST))

#define CSR_WIFI_NME_PRIM_UPSTREAM_HIGHEST             (0x000E + CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST)

#define CSR_WIFI_NME_PRIM_DOWNSTREAM_COUNT             (CSR_WIFI_NME_PRIM_DOWNSTREAM_HIGHEST + 1 - CSR_WIFI_NME_PRIM_DOWNSTREAM_LOWEST)
#define CSR_WIFI_NME_PRIM_UPSTREAM_COUNT               (CSR_WIFI_NME_PRIM_UPSTREAM_HIGHEST   + 1 - CSR_WIFI_NME_PRIM_UPSTREAM_LOWEST)

/*******************************************************************************

  NAME
    CsrWifiNmeProfileSetReq

  DESCRIPTION
    Creates or updates an existing profile in the NME that matches the unique
    identity of the profile. Each profile is identified by the combination of
    BSSID and SSID. The profile contains all the required credentials for
    attempting to connect to the network. Creating or updating a profile via
    the NME PROFILE SET REQ does NOT add the profile to the preferred profile
    list within the NME used for the NME auto-connect behaviour.

  MEMBERS
    common  - Common header for use with the CsrWifiFsm Module
    profile - Specifies the identity and credentials of the network.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent   common;
    CsrWifiNmeProfile profile;
} CsrWifiNmeProfileSetReq;

/*******************************************************************************

  NAME
    CsrWifiNmeProfileDeleteReq

  DESCRIPTION
    Will delete the profile with a matching identity, but does NOT modify the
    preferred profile list.

  MEMBERS
    common          - Common header for use with the CsrWifiFsm Module
    profileIdentity - Identity (BSSID, SSID) of profile to be deleted.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    CsrWifiNmeProfileIdentity profileIdentity;
} CsrWifiNmeProfileDeleteReq;

/*******************************************************************************

  NAME
    CsrWifiNmeProfileDeleteAllReq

  DESCRIPTION
    Deletes all profiles present in the NME, but does NOT modify the
    preferred profile list.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiNmeProfileDeleteAllReq;

/*******************************************************************************

  NAME
    CsrWifiNmeProfileOrderSetReq

  DESCRIPTION
    Defines the preferred order that profiles present in the NME should be
    used during the NME auto-connect behaviour.
    If profileIdentitysCount == 0, it removes any existing preferred profile
    list already present in the NME, effectively disabling the auto-connect
    behaviour.
    NOTE: Profile identities that do not match any profile stored in the NME
    are ignored during the auto-connect procedure.
    NOTE: during auto-connect the NME will only attempt to join an existing
    adhoc network and it will never attempt to host an adhoc network; for
    hosting and adhoc network, use CSR_WIFI_NME_PROFILE_CONNECT_REQ

  MEMBERS
    common                - Common header for use with the CsrWifiFsm Module
    interfaceTag          - Interface Identifier; unique identifier of an
                            interface
    profileIdentitysCount - The number of profiles identities in the list.
    profileIdentitys      - Points to the list of profile identities.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent            common;
    u16                  interfaceTag;
    u8                   profileIdentitysCount;
    CsrWifiNmeProfileIdentity *profileIdentitys;
} CsrWifiNmeProfileOrderSetReq;

/*******************************************************************************

  NAME
    CsrWifiNmeProfileConnectReq

  DESCRIPTION
    Requests the NME to attempt to connect to the specified profile.
    Overrides any current connection attempt.

  MEMBERS
    common          - Common header for use with the CsrWifiFsm Module
    interfaceTag    - Interface Identifier; unique identifier of an interface
    profileIdentity - Identity (BSSID, SSID) of profile to be connected to.
                      It must match an existing profile in the NME.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    u16                 interfaceTag;
    CsrWifiNmeProfileIdentity profileIdentity;
} CsrWifiNmeProfileConnectReq;

/*******************************************************************************

  NAME
    CsrWifiNmeWpsReq

  DESCRIPTION
    Requests the NME to look for WPS enabled APs and attempt to perform WPS
    to determine the appropriate security credentials to connect to the AP.
    If the PIN == '00000000' then 'push button mode' is indicated, otherwise
    the PIN has to match that of the AP. 4 digit pin is passed by sending the
    pin digits in pin[0]..pin[3] and rest of the contents filled with '-'.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    pin          - PIN value.
    ssid         - Service Set identifier
    bssid        - ID of Basic Service Set for which a WPS connection attempt is
                   being made.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent   common;
    u16         interfaceTag;
    u8          pin[8];
    CsrWifiSsid       ssid;
    CsrWifiMacAddress bssid;
} CsrWifiNmeWpsReq;

/*******************************************************************************

  NAME
    CsrWifiNmeWpsCancelReq

  DESCRIPTION
    Requests the NME to cancel any WPS procedure that it is currently
    performing. This includes WPS registrar activities started because of
    CSR_WIFI_NME_AP_REGISTER.request

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiNmeWpsCancelReq;

/*******************************************************************************

  NAME
    CsrWifiNmeConnectionStatusGetReq

  DESCRIPTION
    Requests the current connection status of the NME.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiNmeConnectionStatusGetReq;

/*******************************************************************************

  NAME
    CsrWifiNmeSimImsiGetRes

  DESCRIPTION
    Response from the application that received the NME SIM IMSI GET IND.

  MEMBERS
    common   - Common header for use with the CsrWifiFsm Module
    status   - Indicates the outcome of the requested operation: STATUS_SUCCESS
               or STATUS_ERROR.
    imsi     - The value of the IMSI obtained from the UICC.
    cardType - The UICC type (GSM only (SIM), UMTS only (USIM), Both).

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    CsrResult             status;
    CsrCharString        *imsi;
    CsrWifiNmeSimCardType cardType;
} CsrWifiNmeSimImsiGetRes;

/*******************************************************************************

  NAME
    CsrWifiNmeSimGsmAuthRes

  DESCRIPTION
    Response from the application that received the NME SIM GSM AUTH IND. For
    each GSM authentication round a GSM Ciphering key (Kc) and a signed
    response (SRES) are produced. Since 2 or 3 GSM authentication rounds are
    used the 2 or 3 Kc's obtained respectively are combined into one buffer
    and similarly the 2 or 3 SRES's obtained are combined into another
    buffer. The order of Kc values (SRES values respectively) in their buffer
    is the same as that of their corresponding RAND values in the incoming
    indication.

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    status     - Indicates the outcome of the requested operation:
                 STATUS_SUCCESS or STATUS_ERROR
    kcsLength  - Length in Bytes of Kc buffer. Legal values are: 16 or 24.
    kcs        - Kc buffer holding 2 or 3 Kc values.
    sresLength - Length in Bytes of SRES buffer. Legal values are: 8 or 12.
    sres       - SRES buffer holding 2 or 3 SRES values.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
    u8        kcsLength;
    u8       *kcs;
    u8        sresLength;
    u8       *sres;
} CsrWifiNmeSimGsmAuthRes;

/*******************************************************************************

  NAME
    CsrWifiNmeSimUmtsAuthRes

  DESCRIPTION
    Response from the application that received the NME SIM UMTS AUTH IND.
    The values of umtsCipherKey, umtsIntegrityKey, resParameterLength and
    resParameter are only meanigful when result = UMTS_AUTH_RESULT_SUCCESS.
    The value of auts is only meaningful when
    result=UMTS_AUTH_RESULT_SYNC_FAIL.

  MEMBERS
    common             - Common header for use with the CsrWifiFsm Module
    status             - Indicates the outcome of the requested operation:
                         STATUS_SUCCESS or STATUS_ERROR.
    result             - The result of UMTS authentication as performed by the
                         UICC which could be: Success, Authentication Reject or
                         Synchronisation Failure. For all these 3 outcomes the
                         value of status is success.
    umtsCipherKey      - The UMTS Cipher Key as calculated and returned by the
                         UICC.
    umtsIntegrityKey   - The UMTS Integrity Key as calculated and returned by
                         the UICC.
    resParameterLength - The length (in bytes) of the RES parameter (min=4; max
                         = 16).
    resParameter       - The RES parameter as calculated and returned by the
                         UICC.
    auts               - The AUTS parameter as calculated and returned by the
                         UICC.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent          common;
    CsrResult                status;
    CsrWifiNmeUmtsAuthResult result;
    u8                 umtsCipherKey[16];
    u8                 umtsIntegrityKey[16];
    u8                 resParameterLength;
    u8                *resParameter;
    u8                 auts[14];
} CsrWifiNmeSimUmtsAuthRes;

/*******************************************************************************

  NAME
    CsrWifiNmeWpsConfigSetReq

  DESCRIPTION
    This primitive passes the WPS information for the device to NME. This may
    be accepted only if no interface is active.

  MEMBERS
    common    - Common header for use with the CsrWifiFsm Module
    wpsConfig - WPS config.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent     common;
    CsrWifiSmeWpsConfig wpsConfig;
} CsrWifiNmeWpsConfigSetReq;

/*******************************************************************************

  NAME
    CsrWifiNmeEventMaskSetReq

  DESCRIPTION
    The wireless manager application may register with the NME to receive
    notification of interesting events. Indications will be sent only if the
    wireless manager explicitly registers to be notified of that event.
    indMask is a bit mask of values defined in CsrWifiNmeIndicationsMask.

  MEMBERS
    common  - Common header for use with the CsrWifiFsm Module
    indMask - Set mask with values from CsrWifiNmeIndications

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    CsrWifiNmeIndicationsMask indMask;
} CsrWifiNmeEventMaskSetReq;

/*******************************************************************************

  NAME
    CsrWifiNmeProfileSetCfm

  DESCRIPTION
    Reports the status of the NME PROFILE SET REQ; the request will only fail
    if the details specified in the profile contains an invalid combination
    of parameters for example specifying the profile as cloaked but not
    specifying the SSID. The NME doesn't limit the number of profiles that
    may be created. The NME assumes that the entity configuring it is aware
    of the appropriate limits.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Indicates the success or otherwise of the requested operation.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiNmeProfileSetCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeProfileDeleteCfm

  DESCRIPTION
    Reports the status of the CSR_WIFI_NME_PROFILE_DELETE_REQ.
    Returns CSR_WIFI_NME_STATUS_NOT_FOUND if there is no matching profile.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Indicates the success or otherwise of the requested operation.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiNmeProfileDeleteCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeProfileDeleteAllCfm

  DESCRIPTION
    Reports the status of the CSR_WIFI_NME_PROFILE_DELETE_ALL_REQ.
    Returns always CSR_WIFI_NME_STATUS_SUCCESS.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Indicates the success or otherwise of the requested operation, but
             in this case it always set to success.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiNmeProfileDeleteAllCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeProfileOrderSetCfm

  DESCRIPTION
    Confirmation to UNIFI_NME_PROFILE_ORDER_SET.request.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Indicates the success or otherwise of the requested
                   operation.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiNmeProfileOrderSetCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeProfileConnectCfm

  DESCRIPTION
    Reports the status of the NME PROFILE CONNECT REQ. If unsuccessful the
    connectAttempt parameters contain details of the APs that the NME
    attempted to connect to before reporting the failure of the request.

  MEMBERS
    common               - Common header for use with the CsrWifiFsm Module
    interfaceTag         - Interface Identifier; unique identifier of an
                           interface
    status               - Indicates the success or otherwise of the requested
                           operation.
    connectAttemptsCount - This parameter is relevant only if
                           status!=CSR_WIFI_NME_STATUS_SUCCESS.
                           Number of connection attempt elements provided with
                           this primitive
    connectAttempts      - This parameter is relevant only if
                           status!=CSR_WIFI_NME_STATUS_SUCCESS.
                           Points to the list of connection attempt elements
                           provided with this primitive
                           Each element of the list provides information about
                           an AP on which the connection attempt was made and
                           the error that occurred during the attempt.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    u16                 interfaceTag;
    CsrResult                 status;
    u8                  connectAttemptsCount;
    CsrWifiNmeConnectAttempt *connectAttempts;
} CsrWifiNmeProfileConnectCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeWpsCfm

  DESCRIPTION
    Reports the status of the NME WPS REQ.
    If CSR_WIFI_NME_STATUS_SUCCESS, the profile parameter contains the
    identity and credentials of the AP.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Indicates the success or otherwise of the requested
                   operation.
    profile      - This parameter is relevant only if
                   status==CSR_WIFI_NME_STATUS_SUCCESS.
                   The identity and credentials of the network.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent   common;
    u16         interfaceTag;
    CsrResult         status;
    CsrWifiNmeProfile profile;
} CsrWifiNmeWpsCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeWpsCancelCfm

  DESCRIPTION
    Reports the status of the NME WPS REQ, the request is always SUCCESSFUL.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Only returns CSR_WIFI_NME_STATUS_SUCCESS

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiNmeWpsCancelCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeConnectionStatusGetCfm

  DESCRIPTION
    Reports the connection status of the NME.

  MEMBERS
    common           - Common header for use with the CsrWifiFsm Module
    interfaceTag     - Interface Identifier; unique identifier of an interface
    status           - Indicates the success or otherwise of the requested
                       operation.
    connectionStatus - NME current connection status

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent            common;
    u16                  interfaceTag;
    CsrResult                  status;
    CsrWifiNmeConnectionStatus connectionStatus;
} CsrWifiNmeConnectionStatusGetCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeProfileUpdateInd

  DESCRIPTION
    Indication generated from the NME (if an application subscribes to
    receive it) that informs that application that the contained profile has
    changed.
    For example, either the credentials EAP-FAST PAC file or the session data
    within the profile has changed.
    It is up to the application whether it stores this updated profile or
    not.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    profile      - The identity and credentials of the network.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent   common;
    u16         interfaceTag;
    CsrWifiNmeProfile profile;
} CsrWifiNmeProfileUpdateInd;

/*******************************************************************************

  NAME
    CsrWifiNmeProfileDisconnectInd

  DESCRIPTION
    Indication generated from the NME (if an application subscribes to
    receive it) that informs that application that the current profile
    connection has disconnected. The indication will contain information
    about APs that it attempted to maintain the connection via i.e. in the
    case of failed roaming.

  MEMBERS
    common               - Common header for use with the CsrWifiFsm Module
    interfaceTag         - Interface Identifier; unique identifier of an
                           interface
    connectAttemptsCount - Number of connection attempt elements provided with
                           this primitive
    connectAttempts      - Points to the list of connection attempt elements
                           provided with this primitive
                           Each element of the list provides information about
                           an AP on which the connection attempt was made and
                           the error occurred during the attempt.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    u16                 interfaceTag;
    u8                  connectAttemptsCount;
    CsrWifiNmeConnectAttempt *connectAttempts;
} CsrWifiNmeProfileDisconnectInd;

/*******************************************************************************

  NAME
    CsrWifiNmeSimImsiGetInd

  DESCRIPTION
    Indication generated from the NME (if an application subscribes to
    receive it) that requests the IMSI and UICC type from the UICC Manager.
    This indication is generated when the NME is attempting to connect to a
    profile configured for EAP-SIM/AKA. An application MUST register to
    receive this indication for the NME to support the EAP-SIM/AKA credential
    types. Otherwise the NME has no route to obtain the information from the
    UICC.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiNmeSimImsiGetInd;

/*******************************************************************************

  NAME
    CsrWifiNmeSimGsmAuthInd

  DESCRIPTION
    Indication generated from the NME (if an application subscribes to
    receive it) that requests the UICC Manager to perform a GSM
    authentication on behalf of the NME. This indication is generated when
    the NME is attempting to connect to a profile configured for EAP-SIM. An
    application MUST register to receive this indication for the NME to
    support the EAP-SIM credential types. Otherwise the NME has no route to
    obtain the information from the UICC. EAP-SIM authentication requires 2
    or 3 GSM authentication rounds and therefore 2 or 3 RANDS (GSM Random
    Challenges) are included.

  MEMBERS
    common      - Common header for use with the CsrWifiFsm Module
    randsLength - GSM RAND is 16 bytes long hence valid values are 32 (2 RANDS)
                  or 48 (3 RANDs).
    rands       - 2 or 3 RANDs values.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u8        randsLength;
    u8       *rands;
} CsrWifiNmeSimGsmAuthInd;

/*******************************************************************************

  NAME
    CsrWifiNmeSimUmtsAuthInd

  DESCRIPTION
    Indication generated from the NME (if an application subscribes to
    receive it) that requests the UICC Manager to perform a UMTS
    authentication on behalf of the NME. This indication is generated when
    the NME is attempting to connect to a profile configured for EAP-AKA. An
    application MUST register to receive this indication for the NME to
    support the EAP-AKA credential types. Otherwise the NME has no route to
    obtain the information from the USIM. EAP-AKA requires one UMTS
    authentication round and therefore only one RAND and one AUTN values are
    included.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    rand   - UMTS RAND value.
    autn   - UMTS AUTN value.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u8        rand[16];
    u8        autn[16];
} CsrWifiNmeSimUmtsAuthInd;

/*******************************************************************************

  NAME
    CsrWifiNmeWpsConfigSetCfm

  DESCRIPTION
    Confirm.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Status of the request.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiNmeWpsConfigSetCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeEventMaskSetCfm

  DESCRIPTION
    The NME calls the primitive to report the result of the request
    primitive.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiNmeEventMaskSetCfm;


#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_NME_PRIM_H__ */

