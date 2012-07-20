/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_SME_PRIM_H__
#define CSR_WIFI_SME_PRIM_H__

#include "csr_types.h"
#include "csr_prim_defs.h"
#include "csr_sched.h"
#include "csr_wifi_common.h"
#include "csr_result.h"
#include "csr_wifi_fsm_event.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CSR_WIFI_SME_PRIM                                               (0x0404)

typedef CsrPrim CsrWifiSmePrim;


/*******************************************************************************

  NAME
    CsrWifiSme80211NetworkType

  DESCRIPTION
    Indicates the physical layer of the network

 VALUES
    CSR_WIFI_SME_80211_NETWORK_TYPE_DS
                   - Direct-sequence spread spectrum
    CSR_WIFI_SME_80211_NETWORK_TYPE_OFDM24
                   - Orthogonal Frequency Division Multiplexing at 2.4 GHz
    CSR_WIFI_SME_80211_NETWORK_TYPE_OFDM5
                   - Orthogonal Frequency Division Multiplexing at 5 GHz
    CSR_WIFI_SME_80211_NETWORK_TYPE_AUTO
                   - Automatic

*******************************************************************************/
typedef u8 CsrWifiSme80211NetworkType;
#define CSR_WIFI_SME_80211_NETWORK_TYPE_DS       ((CsrWifiSme80211NetworkType) 0x00)
#define CSR_WIFI_SME_80211_NETWORK_TYPE_OFDM24   ((CsrWifiSme80211NetworkType) 0x01)
#define CSR_WIFI_SME_80211_NETWORK_TYPE_OFDM5    ((CsrWifiSme80211NetworkType) 0x02)
#define CSR_WIFI_SME_80211_NETWORK_TYPE_AUTO     ((CsrWifiSme80211NetworkType) 0x03)

/*******************************************************************************

  NAME
    CsrWifiSme80211PrivacyMode

  DESCRIPTION
    Bits to enable or disable the privacy mode

 VALUES
    CSR_WIFI_SME_80211_PRIVACY_MODE_DISABLED
                   - Privacy mode is enabled: use of WEP for confidentiality is
                     required.
    CSR_WIFI_SME_80211_PRIVACY_MODE_ENABLED
                   - Privacy mode is disabled

*******************************************************************************/
typedef u8 CsrWifiSme80211PrivacyMode;
#define CSR_WIFI_SME_80211_PRIVACY_MODE_DISABLED   ((CsrWifiSme80211PrivacyMode) 0x00)
#define CSR_WIFI_SME_80211_PRIVACY_MODE_ENABLED    ((CsrWifiSme80211PrivacyMode) 0x01)

/*******************************************************************************

  NAME
    CsrWifiSme80211dTrustLevel

  DESCRIPTION
    Level of trust for the information coming from the network

 VALUES
    CSR_WIFI_SME_80211D_TRUST_LEVEL_STRICT
                   - Start with passive scanning and only accept country IE for
                     updating channel lists
    CSR_WIFI_SME_80211D_TRUST_LEVEL_ADJUNCT
                   - As above plus accept adjunct technology location
                     information
    CSR_WIFI_SME_80211D_TRUST_LEVEL_BSS
                   - As above accept plus receiving channel from infrastructure
                     networks
    CSR_WIFI_SME_80211D_TRUST_LEVEL_IBSS
                   - As above accept plus receiving channel from the ad hoc
                     networks
    CSR_WIFI_SME_80211D_TRUST_LEVEL_MIB
                   - Start with active scanning with list of active channels
                     from the MIB and accept as above
    CSR_WIFI_SME_80211D_TRUST_LEVEL_DISABLED
                   - Start with active scanning with list of active channels
                     from the MIB and ignore any channel information from the
                     network

*******************************************************************************/
typedef u8 CsrWifiSme80211dTrustLevel;
#define CSR_WIFI_SME_80211D_TRUST_LEVEL_STRICT     ((CsrWifiSme80211dTrustLevel) 0x01)
#define CSR_WIFI_SME_80211D_TRUST_LEVEL_ADJUNCT    ((CsrWifiSme80211dTrustLevel) 0x02)
#define CSR_WIFI_SME_80211D_TRUST_LEVEL_BSS        ((CsrWifiSme80211dTrustLevel) 0x03)
#define CSR_WIFI_SME_80211D_TRUST_LEVEL_IBSS       ((CsrWifiSme80211dTrustLevel) 0x04)
#define CSR_WIFI_SME_80211D_TRUST_LEVEL_MIB        ((CsrWifiSme80211dTrustLevel) 0x05)
#define CSR_WIFI_SME_80211D_TRUST_LEVEL_DISABLED   ((CsrWifiSme80211dTrustLevel) 0x06)

/*******************************************************************************

  NAME
    CsrWifiSmeAmpStatus

  DESCRIPTION
    AMP Current Status

 VALUES
    CSR_WIFI_SME_AMP_ACTIVE   - AMP ACTIVE.
    CSR_WIFI_SME_AMP_INACTIVE - AMP INACTIVE

*******************************************************************************/
typedef u8 CsrWifiSmeAmpStatus;
#define CSR_WIFI_SME_AMP_ACTIVE     ((CsrWifiSmeAmpStatus) 0x00)
#define CSR_WIFI_SME_AMP_INACTIVE   ((CsrWifiSmeAmpStatus) 0x01)

/*******************************************************************************

  NAME
    CsrWifiSmeAuthMode

  DESCRIPTION
    Define bits for CsrWifiSmeAuthMode

 VALUES
    CSR_WIFI_SME_AUTH_MODE_80211_OPEN
                   - Connects to an open system network (i.e. no authentication,
                     no encryption) or to a WEP enabled network.
    CSR_WIFI_SME_AUTH_MODE_80211_SHARED
                   - Connect to a WEP enabled network.
    CSR_WIFI_SME_AUTH_MODE_8021X_WPA
                   - Connects to a WPA Enterprise enabled network.
    CSR_WIFI_SME_AUTH_MODE_8021X_WPAPSK
                   - Connects to a WPA with Pre-Shared Key enabled network.
    CSR_WIFI_SME_AUTH_MODE_8021X_WPA2
                   - Connects to a WPA2 Enterprise enabled network.
    CSR_WIFI_SME_AUTH_MODE_8021X_WPA2PSK
                   - Connects to a WPA2 with Pre-Shared Key enabled network.
    CSR_WIFI_SME_AUTH_MODE_8021X_CCKM
                   - Connects to a CCKM enabled network.
    CSR_WIFI_SME_AUTH_MODE_WAPI_WAI
                   - Connects to a WAPI Enterprise enabled network.
    CSR_WIFI_SME_AUTH_MODE_WAPI_WAIPSK
                   - Connects to a WAPI with Pre-Shared Key enabled network.
    CSR_WIFI_SME_AUTH_MODE_8021X_OTHER1X
                   - For future use.

*******************************************************************************/
typedef u16 CsrWifiSmeAuthMode;
#define CSR_WIFI_SME_AUTH_MODE_80211_OPEN      ((CsrWifiSmeAuthMode) 0x0001)
#define CSR_WIFI_SME_AUTH_MODE_80211_SHARED    ((CsrWifiSmeAuthMode) 0x0002)
#define CSR_WIFI_SME_AUTH_MODE_8021X_WPA       ((CsrWifiSmeAuthMode) 0x0004)
#define CSR_WIFI_SME_AUTH_MODE_8021X_WPAPSK    ((CsrWifiSmeAuthMode) 0x0008)
#define CSR_WIFI_SME_AUTH_MODE_8021X_WPA2      ((CsrWifiSmeAuthMode) 0x0010)
#define CSR_WIFI_SME_AUTH_MODE_8021X_WPA2PSK   ((CsrWifiSmeAuthMode) 0x0020)
#define CSR_WIFI_SME_AUTH_MODE_8021X_CCKM      ((CsrWifiSmeAuthMode) 0x0040)
#define CSR_WIFI_SME_AUTH_MODE_WAPI_WAI        ((CsrWifiSmeAuthMode) 0x0080)
#define CSR_WIFI_SME_AUTH_MODE_WAPI_WAIPSK     ((CsrWifiSmeAuthMode) 0x0100)
#define CSR_WIFI_SME_AUTH_MODE_8021X_OTHER1X   ((CsrWifiSmeAuthMode) 0x0200)

/*******************************************************************************

  NAME
    CsrWifiSmeBasicUsability

  DESCRIPTION
    Indicates the usability level of a channel

 VALUES
    CSR_WIFI_SME_BASIC_USABILITY_UNUSABLE
                   - Not usable; connection not recommended
    CSR_WIFI_SME_BASIC_USABILITY_POOR
                   - Poor quality; connect only if nothing better is available
    CSR_WIFI_SME_BASIC_USABILITY_SATISFACTORY
                   - Quality is satisfactory
    CSR_WIFI_SME_BASIC_USABILITY_NOT_CONNECTED
                   - Not connected

*******************************************************************************/
typedef u8 CsrWifiSmeBasicUsability;
#define CSR_WIFI_SME_BASIC_USABILITY_UNUSABLE        ((CsrWifiSmeBasicUsability) 0x00)
#define CSR_WIFI_SME_BASIC_USABILITY_POOR            ((CsrWifiSmeBasicUsability) 0x01)
#define CSR_WIFI_SME_BASIC_USABILITY_SATISFACTORY    ((CsrWifiSmeBasicUsability) 0x02)
#define CSR_WIFI_SME_BASIC_USABILITY_NOT_CONNECTED   ((CsrWifiSmeBasicUsability) 0x03)

/*******************************************************************************

  NAME
    CsrWifiSmeBssType

  DESCRIPTION
    Indicates the BSS type

 VALUES
    CSR_WIFI_SME_BSS_TYPE_INFRASTRUCTURE
                   - Infrastructure BSS.
    CSR_WIFI_SME_BSS_TYPE_ADHOC
                   - Ad hoc or Independent BSS.
    CSR_WIFI_SME_BSS_TYPE_ANY_BSS
                   - Specifies any type of BSS
    CSR_WIFI_SME_BSS_TYPE_P2P
                   - Specifies P2P

*******************************************************************************/
typedef u8 CsrWifiSmeBssType;
#define CSR_WIFI_SME_BSS_TYPE_INFRASTRUCTURE   ((CsrWifiSmeBssType) 0x00)
#define CSR_WIFI_SME_BSS_TYPE_ADHOC            ((CsrWifiSmeBssType) 0x01)
#define CSR_WIFI_SME_BSS_TYPE_ANY_BSS          ((CsrWifiSmeBssType) 0x02)
#define CSR_WIFI_SME_BSS_TYPE_P2P              ((CsrWifiSmeBssType) 0x03)

/*******************************************************************************

  NAME
    CsrWifiSmeCoexScheme

  DESCRIPTION
    Options for the coexistence signalling
    Same as MibValues

 VALUES
    CSR_WIFI_SME_COEX_SCHEME_DISABLED
                   - The coexistence signalling is disabled
    CSR_WIFI_SME_COEX_SCHEME_CSR
                   - Basic CSR coexistence signalling
    CSR_WIFI_SME_COEX_SCHEME_CSR_CHANNEL
                   - Full CSR coexistence signalling
    CSR_WIFI_SME_COEX_SCHEME_PTA
                   - Packet Traffic Arbitrator coexistence signalling

*******************************************************************************/
typedef u8 CsrWifiSmeCoexScheme;
#define CSR_WIFI_SME_COEX_SCHEME_DISABLED      ((CsrWifiSmeCoexScheme) 0x00)
#define CSR_WIFI_SME_COEX_SCHEME_CSR           ((CsrWifiSmeCoexScheme) 0x01)
#define CSR_WIFI_SME_COEX_SCHEME_CSR_CHANNEL   ((CsrWifiSmeCoexScheme) 0x02)
#define CSR_WIFI_SME_COEX_SCHEME_PTA           ((CsrWifiSmeCoexScheme) 0x03)

/*******************************************************************************

  NAME
    CsrWifiSmeControlIndication

  DESCRIPTION
    Indicates the reason why the Wi-Fi has been switched off.
    The values of this type are used across the NME/SME/Router API's and they
    must be kept consistent with the corresponding types in the .xml of the
    ottherinterfaces

 VALUES
    CSR_WIFI_SME_CONTROL_INDICATION_ERROR
                   - An unrecoverable error (for example, an unrecoverable SDIO
                     error) has occurred.
                     The wireless manager application should reinitialise the
                     chip by calling CSR_WIFI_SME_WIFI_ON_REQ.
    CSR_WIFI_SME_CONTROL_INDICATION_EXIT
                   - The chip became unavailable due to an external action, for
                     example, when a plug-in card is ejected or the driver is
                     unloaded.
    CSR_WIFI_SME_CONTROL_INDICATION_USER_REQUESTED
                   - The Wi-Fi has been switched off as the wireless manager
                     application has sent CSR_WIFI_SME_WIFI_OFF_REQ

*******************************************************************************/
typedef u8 CsrWifiSmeControlIndication;
#define CSR_WIFI_SME_CONTROL_INDICATION_ERROR            ((CsrWifiSmeControlIndication) 0x01)
#define CSR_WIFI_SME_CONTROL_INDICATION_EXIT             ((CsrWifiSmeControlIndication) 0x02)
#define CSR_WIFI_SME_CONTROL_INDICATION_USER_REQUESTED   ((CsrWifiSmeControlIndication) 0x03)

/*******************************************************************************

  NAME
    CsrWifiSmeCtsProtectionType

  DESCRIPTION
    SME CTS Protection Types

 VALUES
    CSR_WIFI_SME_CTS_PROTECTION_AUTOMATIC
                   - AP CTS Protection automatic based on non-ERP station in own
                     BSS or neighbouring BSS on the same channel based on OLBC.
                     This requires monitoring of beacons from other APs.
    CSR_WIFI_SME_CTS_PROTECTION_FORCE_ENABLED
                   - AP CTS Protection Force enabled
    CSR_WIFI_SME_CTS_PROTECTION_FORCE_DISABLED
                   - AP CTS Protection Force disabled.
    CSR_WIFI_SME_CTS_PROTECTION_AUTOMATIC_NO_OLBC
                   - AP CTS Protection automatic without considering OLBC but
                     considering non-ERP station in the own BSS Valid only if AP
                     is configured to work in 802.11bg or 802.11g mode otherwise
                     this option specifies the same behaviour as AUTOMATIC

*******************************************************************************/
typedef u8 CsrWifiSmeCtsProtectionType;
#define CSR_WIFI_SME_CTS_PROTECTION_AUTOMATIC           ((CsrWifiSmeCtsProtectionType) 0x00)
#define CSR_WIFI_SME_CTS_PROTECTION_FORCE_ENABLED       ((CsrWifiSmeCtsProtectionType) 0x01)
#define CSR_WIFI_SME_CTS_PROTECTION_FORCE_DISABLED      ((CsrWifiSmeCtsProtectionType) 0x02)
#define CSR_WIFI_SME_CTS_PROTECTION_AUTOMATIC_NO_OLBC   ((CsrWifiSmeCtsProtectionType) 0x03)

/*******************************************************************************

  NAME
    CsrWifiSmeD3AutoScanMode

  DESCRIPTION
    Autonomous scan status while in D3 suspended period

 VALUES
    CSR_WIFI_SME_D3AUTO_SCAN_MODE_PSON
                   - Autonomous scan stays on
    CSR_WIFI_SME_D3AUTO_SCAN_MODE_PSOFF
                   - Autonomous scan is switched off
    CSR_WIFI_SME_D3AUTO_SCAN_MODE_PSAUTO
                   - Automatically select autoscanning behaviour.
                     CURRENTLY NOT SUPPORTED

*******************************************************************************/
typedef u8 CsrWifiSmeD3AutoScanMode;
#define CSR_WIFI_SME_D3AUTO_SCAN_MODE_PSON     ((CsrWifiSmeD3AutoScanMode) 0x00)
#define CSR_WIFI_SME_D3AUTO_SCAN_MODE_PSOFF    ((CsrWifiSmeD3AutoScanMode) 0x01)
#define CSR_WIFI_SME_D3AUTO_SCAN_MODE_PSAUTO   ((CsrWifiSmeD3AutoScanMode) 0x02)

/*******************************************************************************

  NAME
    CsrWifiSmeEncryption

  DESCRIPTION
    Defines bits for CsrWifiSmeEncryption
    For a WEP enabled network, the caller must specify the correct
    combination of flags in the encryptionModeMask.

 VALUES
    CSR_WIFI_SME_ENCRYPTION_CIPHER_NONE
                   - No encryption set
    CSR_WIFI_SME_ENCRYPTION_CIPHER_PAIRWISE_WEP40
                   - Selects 40 byte key WEP for unicast communication
    CSR_WIFI_SME_ENCRYPTION_CIPHER_PAIRWISE_WEP104
                   - Selects 104 byte key WEP for unicast communication
    CSR_WIFI_SME_ENCRYPTION_CIPHER_PAIRWISE_TKIP
                   - Selects TKIP for unicast communication
    CSR_WIFI_SME_ENCRYPTION_CIPHER_PAIRWISE_CCMP
                   - Selects CCMP for unicast communication
    CSR_WIFI_SME_ENCRYPTION_CIPHER_PAIRWISE_SMS4
                   - Selects SMS4 for unicast communication
    CSR_WIFI_SME_ENCRYPTION_CIPHER_GROUP_WEP40
                   - Selects 40 byte key WEP for broadcast messages
    CSR_WIFI_SME_ENCRYPTION_CIPHER_GROUP_WEP104
                   - Selects 104 byte key WEP for broadcast messages
    CSR_WIFI_SME_ENCRYPTION_CIPHER_GROUP_TKIP
                   - Selects a TKIP for broadcast messages
    CSR_WIFI_SME_ENCRYPTION_CIPHER_GROUP_CCMP
                   - Selects CCMP for broadcast messages
    CSR_WIFI_SME_ENCRYPTION_CIPHER_GROUP_SMS4
                   - Selects SMS4 for broadcast messages

*******************************************************************************/
typedef u16 CsrWifiSmeEncryption;
#define CSR_WIFI_SME_ENCRYPTION_CIPHER_NONE              ((CsrWifiSmeEncryption) 0x0000)
#define CSR_WIFI_SME_ENCRYPTION_CIPHER_PAIRWISE_WEP40    ((CsrWifiSmeEncryption) 0x0001)
#define CSR_WIFI_SME_ENCRYPTION_CIPHER_PAIRWISE_WEP104   ((CsrWifiSmeEncryption) 0x0002)
#define CSR_WIFI_SME_ENCRYPTION_CIPHER_PAIRWISE_TKIP     ((CsrWifiSmeEncryption) 0x0004)
#define CSR_WIFI_SME_ENCRYPTION_CIPHER_PAIRWISE_CCMP     ((CsrWifiSmeEncryption) 0x0008)
#define CSR_WIFI_SME_ENCRYPTION_CIPHER_PAIRWISE_SMS4     ((CsrWifiSmeEncryption) 0x0010)
#define CSR_WIFI_SME_ENCRYPTION_CIPHER_GROUP_WEP40       ((CsrWifiSmeEncryption) 0x0020)
#define CSR_WIFI_SME_ENCRYPTION_CIPHER_GROUP_WEP104      ((CsrWifiSmeEncryption) 0x0040)
#define CSR_WIFI_SME_ENCRYPTION_CIPHER_GROUP_TKIP        ((CsrWifiSmeEncryption) 0x0080)
#define CSR_WIFI_SME_ENCRYPTION_CIPHER_GROUP_CCMP        ((CsrWifiSmeEncryption) 0x0100)
#define CSR_WIFI_SME_ENCRYPTION_CIPHER_GROUP_SMS4        ((CsrWifiSmeEncryption) 0x0200)

/*******************************************************************************

  NAME
    CsrWifiSmeFirmwareDriverInterface

  DESCRIPTION
    Type of communication between Host and Firmware

 VALUES
    CSR_WIFI_SME_FIRMWARE_DRIVER_INTERFACE_UNIT_DATA_INTERFACE
                   - No preformated header. NOT SUPPORTED in the current release
    CSR_WIFI_SME_FIRMWARE_DRIVER_INTERFACE_PACKET_INTERFACE
                   - Preformated IEEE 802.11 header for user plane

*******************************************************************************/
typedef u8 CsrWifiSmeFirmwareDriverInterface;
#define CSR_WIFI_SME_FIRMWARE_DRIVER_INTERFACE_UNIT_DATA_INTERFACE   ((CsrWifiSmeFirmwareDriverInterface) 0x00)
#define CSR_WIFI_SME_FIRMWARE_DRIVER_INTERFACE_PACKET_INTERFACE      ((CsrWifiSmeFirmwareDriverInterface) 0x01)

/*******************************************************************************

  NAME
    CsrWifiSmeHostPowerMode

  DESCRIPTION
    Defines the power mode

 VALUES
    CSR_WIFI_SME_HOST_POWER_MODE_ACTIVE
                   - Host device is running on external power.
    CSR_WIFI_SME_HOST_POWER_MODE_POWER_SAVE
                   - Host device is running on (internal) battery power.
    CSR_WIFI_SME_HOST_POWER_MODE_FULL_POWER_SAVE
                   - For future use.

*******************************************************************************/
typedef u8 CsrWifiSmeHostPowerMode;
#define CSR_WIFI_SME_HOST_POWER_MODE_ACTIVE            ((CsrWifiSmeHostPowerMode) 0x00)
#define CSR_WIFI_SME_HOST_POWER_MODE_POWER_SAVE        ((CsrWifiSmeHostPowerMode) 0x01)
#define CSR_WIFI_SME_HOST_POWER_MODE_FULL_POWER_SAVE   ((CsrWifiSmeHostPowerMode) 0x02)

/*******************************************************************************

  NAME
    CsrWifiSmeIEEE80211Reason

  DESCRIPTION
    As definined in the IEEE 802.11 standards

 VALUES
    CSR_WIFI_SME_IEEE80211_REASON_SUCCESS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_UNSPECIFIED_REASON
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_AUTHENTICATION_NOT_VALID
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_DEAUTHENTICATED_LEAVE_BSS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_DISASSOCIATED_INACTIVITY
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_AP_OVERLOAD
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_CLASS_2FRAME_ERROR
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_CLASS_3FRAME_ERROR
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_DISASSOCIATED_LEAVE_BSS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_ASSOCIATION_NOT_AUTHENTICATED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_DISASSOCIATED_POWER_CAPABILITY
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_DISASSOCIATED_SUPPORTED_CHANNELS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_INVALID_INFORMATION_ELEMENT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_MICHAEL_MIC_FAILURE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_FOURWAY_HANDSHAKE_TIMEOUT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_GROUP_KEY_UPDATE_TIMEOUT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_HANDSHAKE_ELEMENT_DIFFERENT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_INVALID_GROUP_CIPHER
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_INVALID_PAIRWISE_CIPHER
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_INVALID_AKMP
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_UNSUPPORTED_RSN_IEVERSION
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_INVALID_RSN_IECAPABILITIES
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_DOT1X_AUTH_FAILED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_CIPHER_REJECTED_BY_POLICY
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_SERVICE_CHANGE_PRECLUDES_TS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_QOS_UNSPECIFIED_REASON
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_QOS_INSUFFICIENT_BANDWIDTH
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_QOS_EXCESSIVE_NOT_ACK
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_QOS_TXOPLIMIT_EXCEEDED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_QSTA_LEAVING
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_END_TS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_END_DLS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_END_BA
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_UNKNOWN_TS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_UNKNOWN_BA
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_UNKNOWN_DLS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_TIMEOUT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_STAKEY_MISMATCH
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_UNICAST_KEY_NEGOTIATION_TIMEOUT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_MULTICAST_KEY_ANNOUNCEMENT_TIMEOUT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_INCOMPATIBLE_UNICAST_KEY_NEGOTIATION_IE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_INVALID_MULTICAST_CIPHER
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_INVALID_UNICAST_CIPHER
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_UNSUPPORTED_WAPI_IE_VERSION
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_INVALID_WAPI_CAPABILITY_IE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_REASON_WAI_CERTIFICATE_AUTHENTICATION_FAILED
                   - See IEEE 802.11 Standard

*******************************************************************************/
typedef u16 CsrWifiSmeIEEE80211Reason;
#define CSR_WIFI_SME_IEEE80211_REASON_SUCCESS                                   ((CsrWifiSmeIEEE80211Reason) 0x0000)
#define CSR_WIFI_SME_IEEE80211_REASON_UNSPECIFIED_REASON                        ((CsrWifiSmeIEEE80211Reason) 0x0001)
#define CSR_WIFI_SME_IEEE80211_REASON_AUTHENTICATION_NOT_VALID                  ((CsrWifiSmeIEEE80211Reason) 0x0002)
#define CSR_WIFI_SME_IEEE80211_REASON_DEAUTHENTICATED_LEAVE_BSS                 ((CsrWifiSmeIEEE80211Reason) 0x0003)
#define CSR_WIFI_SME_IEEE80211_REASON_DISASSOCIATED_INACTIVITY                  ((CsrWifiSmeIEEE80211Reason) 0x0004)
#define CSR_WIFI_SME_IEEE80211_REASON_AP_OVERLOAD                               ((CsrWifiSmeIEEE80211Reason) 0x0005)
#define CSR_WIFI_SME_IEEE80211_REASON_CLASS_2FRAME_ERROR                        ((CsrWifiSmeIEEE80211Reason) 0x0006)
#define CSR_WIFI_SME_IEEE80211_REASON_CLASS_3FRAME_ERROR                        ((CsrWifiSmeIEEE80211Reason) 0x0007)
#define CSR_WIFI_SME_IEEE80211_REASON_DISASSOCIATED_LEAVE_BSS                   ((CsrWifiSmeIEEE80211Reason) 0x0008)
#define CSR_WIFI_SME_IEEE80211_REASON_ASSOCIATION_NOT_AUTHENTICATED             ((CsrWifiSmeIEEE80211Reason) 0x0009)
#define CSR_WIFI_SME_IEEE80211_REASON_DISASSOCIATED_POWER_CAPABILITY            ((CsrWifiSmeIEEE80211Reason) 0x000a)
#define CSR_WIFI_SME_IEEE80211_REASON_DISASSOCIATED_SUPPORTED_CHANNELS          ((CsrWifiSmeIEEE80211Reason) 0x000b)
#define CSR_WIFI_SME_IEEE80211_REASON_INVALID_INFORMATION_ELEMENT               ((CsrWifiSmeIEEE80211Reason) 0x000d)
#define CSR_WIFI_SME_IEEE80211_REASON_MICHAEL_MIC_FAILURE                       ((CsrWifiSmeIEEE80211Reason) 0x000e)
#define CSR_WIFI_SME_IEEE80211_REASON_FOURWAY_HANDSHAKE_TIMEOUT                 ((CsrWifiSmeIEEE80211Reason) 0x000f)
#define CSR_WIFI_SME_IEEE80211_REASON_GROUP_KEY_UPDATE_TIMEOUT                  ((CsrWifiSmeIEEE80211Reason) 0x0010)
#define CSR_WIFI_SME_IEEE80211_REASON_HANDSHAKE_ELEMENT_DIFFERENT               ((CsrWifiSmeIEEE80211Reason) 0x0011)
#define CSR_WIFI_SME_IEEE80211_REASON_INVALID_GROUP_CIPHER                      ((CsrWifiSmeIEEE80211Reason) 0x0012)
#define CSR_WIFI_SME_IEEE80211_REASON_INVALID_PAIRWISE_CIPHER                   ((CsrWifiSmeIEEE80211Reason) 0x0013)
#define CSR_WIFI_SME_IEEE80211_REASON_INVALID_AKMP                              ((CsrWifiSmeIEEE80211Reason) 0x0014)
#define CSR_WIFI_SME_IEEE80211_REASON_UNSUPPORTED_RSN_IEVERSION                 ((CsrWifiSmeIEEE80211Reason) 0x0015)
#define CSR_WIFI_SME_IEEE80211_REASON_INVALID_RSN_IECAPABILITIES                ((CsrWifiSmeIEEE80211Reason) 0x0016)
#define CSR_WIFI_SME_IEEE80211_REASON_DOT1X_AUTH_FAILED                         ((CsrWifiSmeIEEE80211Reason) 0x0017)
#define CSR_WIFI_SME_IEEE80211_REASON_CIPHER_REJECTED_BY_POLICY                 ((CsrWifiSmeIEEE80211Reason) 0x0018)
#define CSR_WIFI_SME_IEEE80211_REASON_SERVICE_CHANGE_PRECLUDES_TS               ((CsrWifiSmeIEEE80211Reason) 0x001F)
#define CSR_WIFI_SME_IEEE80211_REASON_QOS_UNSPECIFIED_REASON                    ((CsrWifiSmeIEEE80211Reason) 0x0020)
#define CSR_WIFI_SME_IEEE80211_REASON_QOS_INSUFFICIENT_BANDWIDTH                ((CsrWifiSmeIEEE80211Reason) 0x0021)
#define CSR_WIFI_SME_IEEE80211_REASON_QOS_EXCESSIVE_NOT_ACK                     ((CsrWifiSmeIEEE80211Reason) 0x0022)
#define CSR_WIFI_SME_IEEE80211_REASON_QOS_TXOPLIMIT_EXCEEDED                    ((CsrWifiSmeIEEE80211Reason) 0x0023)
#define CSR_WIFI_SME_IEEE80211_REASON_QSTA_LEAVING                              ((CsrWifiSmeIEEE80211Reason) 0x0024)
#define CSR_WIFI_SME_IEEE80211_REASON_END_TS                                    ((CsrWifiSmeIEEE80211Reason) 0x0025)
#define CSR_WIFI_SME_IEEE80211_REASON_END_DLS                                   ((CsrWifiSmeIEEE80211Reason) 0x0025)
#define CSR_WIFI_SME_IEEE80211_REASON_END_BA                                    ((CsrWifiSmeIEEE80211Reason) 0x0025)
#define CSR_WIFI_SME_IEEE80211_REASON_UNKNOWN_TS                                ((CsrWifiSmeIEEE80211Reason) 0x0026)
#define CSR_WIFI_SME_IEEE80211_REASON_UNKNOWN_BA                                ((CsrWifiSmeIEEE80211Reason) 0x0026)
#define CSR_WIFI_SME_IEEE80211_REASON_UNKNOWN_DLS                               ((CsrWifiSmeIEEE80211Reason) 0x0026)
#define CSR_WIFI_SME_IEEE80211_REASON_TIMEOUT                                   ((CsrWifiSmeIEEE80211Reason) 0x0027)
#define CSR_WIFI_SME_IEEE80211_REASON_STAKEY_MISMATCH                           ((CsrWifiSmeIEEE80211Reason) 0x002d)
#define CSR_WIFI_SME_IEEE80211_REASON_UNICAST_KEY_NEGOTIATION_TIMEOUT           ((CsrWifiSmeIEEE80211Reason) 0xf019)
#define CSR_WIFI_SME_IEEE80211_REASON_MULTICAST_KEY_ANNOUNCEMENT_TIMEOUT        ((CsrWifiSmeIEEE80211Reason) 0xf01a)
#define CSR_WIFI_SME_IEEE80211_REASON_INCOMPATIBLE_UNICAST_KEY_NEGOTIATION_IE   ((CsrWifiSmeIEEE80211Reason) 0xf01b)
#define CSR_WIFI_SME_IEEE80211_REASON_INVALID_MULTICAST_CIPHER                  ((CsrWifiSmeIEEE80211Reason) 0xf01c)
#define CSR_WIFI_SME_IEEE80211_REASON_INVALID_UNICAST_CIPHER                    ((CsrWifiSmeIEEE80211Reason) 0xf01d)
#define CSR_WIFI_SME_IEEE80211_REASON_UNSUPPORTED_WAPI_IE_VERSION               ((CsrWifiSmeIEEE80211Reason) 0xf01e)
#define CSR_WIFI_SME_IEEE80211_REASON_INVALID_WAPI_CAPABILITY_IE                ((CsrWifiSmeIEEE80211Reason) 0xf01f)
#define CSR_WIFI_SME_IEEE80211_REASON_WAI_CERTIFICATE_AUTHENTICATION_FAILED     ((CsrWifiSmeIEEE80211Reason) 0xf020)

/*******************************************************************************

  NAME
    CsrWifiSmeIEEE80211Result

  DESCRIPTION
    As definined in the IEEE 802.11 standards

 VALUES
    CSR_WIFI_SME_IEEE80211_RESULT_SUCCESS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_UNSPECIFIED_FAILURE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_CAPABILITIES_MISMATCH
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REASSOCIATION_DENIED_NO_ASSOCIATION
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_EXTERNAL_REASON
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_AUTHENTICATION_MISMATCH
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_INVALID_AUTHENTICATION_SEQUENCE_NUMBER
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_CHALLENGE_FAILURE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_AUTHENTICATION_TIMEOUT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_AP_OUT_OF_MEMORY
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_BASIC_RATES_MISMATCH
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_SHORT_PREAMBLE_REQUIRED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_PBCC_MODULATION_REQUIRED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_CHANNEL_AGILITY_REQUIRED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_SPECTRUM_MANAGEMENT_REQUIRED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_POWER_CAPABILITY_UNACCEPTABLE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_SUPPORTED_CHANNELS_UNACCEPTABLE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_SHORT_SLOT_REQUIRED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_DSSS_OFDMREQUIRED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_NO_HT_SUPPORT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_R0KH_UNREACHABLE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_PCO_TRANSITION_SUPPORT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_ASSOCIATION_REQUEST_REJECTED_TEMPORARILY
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_ROBUST_MANAGEMENT_FRAME_POLICY_VIOLATION
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_FAILURE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_AP_BANDWIDTH_INSUFFICIENT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_POOR_OPERATING_CHANNEL
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_QOS_REQUIRED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_REASON_UNSPECIFIED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_INVALID_PARAMETERS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_WITH_SUGGESTED_TSPEC_CHANGES
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_INVALID_IE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_INVALID_GROUP_CIPHER
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_INVALID_PAIRWISE_CIPHER
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_INVALID_AKMP
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_UNSUPPORTED_RSN_VERSION
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_INVALID_RSN_CAPABILITY
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_SECURITY_POLICY
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_FOR_DELAY_PERIOD
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_NOT_ALLOWED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_NOT_PRESENT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_NOT_QSTA
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_LISTEN_INTERVAL_TOO_LARGE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_INVALID_FT_ACTION_FRAME_COUNT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_INVALID_PMKID
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_INVALID_MDIE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_INVALID_FTIE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_UNSPECIFIED_QOS_FAILURE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_WRONG_POLICY
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_INSUFFICIENT_BANDWIDTH
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_INVALID_TSPEC_PARAMETERS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_TIMEOUT
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_TOO_MANY_SIMULTANEOUS_REQUESTS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_BSS_ALREADY_STARTED_OR_JOINED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_NOT_SUPPORTED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_TRANSMISSION_FAILURE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_NOT_AUTHENTICATED
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_RESET_REQUIRED_BEFORE_START
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_LM_INFO_UNAVAILABLE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_INVALID_UNICAST_CIPHER
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_INVALID_MULTICAST_CIPHER
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_UNSUPPORTED_WAPI_IE_VERSION
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_IEEE80211_RESULT_INVALID_WAPI_CAPABILITY_IE
                   - See IEEE 802.11 Standard

*******************************************************************************/
typedef u16 CsrWifiSmeIEEE80211Result;
#define CSR_WIFI_SME_IEEE80211_RESULT_SUCCESS                                          ((CsrWifiSmeIEEE80211Result) 0x0000)
#define CSR_WIFI_SME_IEEE80211_RESULT_UNSPECIFIED_FAILURE                              ((CsrWifiSmeIEEE80211Result) 0x0001)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_CAPABILITIES_MISMATCH                    ((CsrWifiSmeIEEE80211Result) 0x000a)
#define CSR_WIFI_SME_IEEE80211_RESULT_REASSOCIATION_DENIED_NO_ASSOCIATION              ((CsrWifiSmeIEEE80211Result) 0x000b)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_EXTERNAL_REASON                          ((CsrWifiSmeIEEE80211Result) 0x000c)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_AUTHENTICATION_MISMATCH                  ((CsrWifiSmeIEEE80211Result) 0x000d)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_INVALID_AUTHENTICATION_SEQUENCE_NUMBER   ((CsrWifiSmeIEEE80211Result) 0x000e)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_CHALLENGE_FAILURE                        ((CsrWifiSmeIEEE80211Result) 0x000f)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_AUTHENTICATION_TIMEOUT                   ((CsrWifiSmeIEEE80211Result) 0x0010)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_AP_OUT_OF_MEMORY                         ((CsrWifiSmeIEEE80211Result) 0x0011)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_BASIC_RATES_MISMATCH                     ((CsrWifiSmeIEEE80211Result) 0x0012)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_SHORT_PREAMBLE_REQUIRED                  ((CsrWifiSmeIEEE80211Result) 0x0013)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_PBCC_MODULATION_REQUIRED                 ((CsrWifiSmeIEEE80211Result) 0x0014)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_CHANNEL_AGILITY_REQUIRED                 ((CsrWifiSmeIEEE80211Result) 0x0015)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_SPECTRUM_MANAGEMENT_REQUIRED             ((CsrWifiSmeIEEE80211Result) 0x0016)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_POWER_CAPABILITY_UNACCEPTABLE            ((CsrWifiSmeIEEE80211Result) 0x0017)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_SUPPORTED_CHANNELS_UNACCEPTABLE          ((CsrWifiSmeIEEE80211Result) 0x0018)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_SHORT_SLOT_REQUIRED                      ((CsrWifiSmeIEEE80211Result) 0x0019)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_DSSS_OFDMREQUIRED                        ((CsrWifiSmeIEEE80211Result) 0x001a)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_NO_HT_SUPPORT                            ((CsrWifiSmeIEEE80211Result) 0x001b)
#define CSR_WIFI_SME_IEEE80211_RESULT_R0KH_UNREACHABLE                                 ((CsrWifiSmeIEEE80211Result) 0x001c)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_PCO_TRANSITION_SUPPORT                   ((CsrWifiSmeIEEE80211Result) 0x001d)
#define CSR_WIFI_SME_IEEE80211_RESULT_ASSOCIATION_REQUEST_REJECTED_TEMPORARILY         ((CsrWifiSmeIEEE80211Result) 0x001e)
#define CSR_WIFI_SME_IEEE80211_RESULT_ROBUST_MANAGEMENT_FRAME_POLICY_VIOLATION         ((CsrWifiSmeIEEE80211Result) 0x001f)
#define CSR_WIFI_SME_IEEE80211_RESULT_FAILURE                                          ((CsrWifiSmeIEEE80211Result) 0x0020)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_AP_BANDWIDTH_INSUFFICIENT                ((CsrWifiSmeIEEE80211Result) 0x0021)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_POOR_OPERATING_CHANNEL                   ((CsrWifiSmeIEEE80211Result) 0x0022)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_QOS_REQUIRED                             ((CsrWifiSmeIEEE80211Result) 0x0023)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_REASON_UNSPECIFIED                       ((CsrWifiSmeIEEE80211Result) 0x0025)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED                                          ((CsrWifiSmeIEEE80211Result) 0x0025)
#define CSR_WIFI_SME_IEEE80211_RESULT_INVALID_PARAMETERS                               ((CsrWifiSmeIEEE80211Result) 0x0026)
#define CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_WITH_SUGGESTED_TSPEC_CHANGES            ((CsrWifiSmeIEEE80211Result) 0x0027)
#define CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_INVALID_IE                              ((CsrWifiSmeIEEE80211Result) 0x0028)
#define CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_INVALID_GROUP_CIPHER                    ((CsrWifiSmeIEEE80211Result) 0x0029)
#define CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_INVALID_PAIRWISE_CIPHER                 ((CsrWifiSmeIEEE80211Result) 0x002a)
#define CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_INVALID_AKMP                            ((CsrWifiSmeIEEE80211Result) 0x002b)
#define CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_UNSUPPORTED_RSN_VERSION                 ((CsrWifiSmeIEEE80211Result) 0x002c)
#define CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_INVALID_RSN_CAPABILITY                  ((CsrWifiSmeIEEE80211Result) 0x002d)
#define CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_SECURITY_POLICY                         ((CsrWifiSmeIEEE80211Result) 0x002e)
#define CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_FOR_DELAY_PERIOD                        ((CsrWifiSmeIEEE80211Result) 0x002f)
#define CSR_WIFI_SME_IEEE80211_RESULT_NOT_ALLOWED                                      ((CsrWifiSmeIEEE80211Result) 0x0030)
#define CSR_WIFI_SME_IEEE80211_RESULT_NOT_PRESENT                                      ((CsrWifiSmeIEEE80211Result) 0x0031)
#define CSR_WIFI_SME_IEEE80211_RESULT_NOT_QSTA                                         ((CsrWifiSmeIEEE80211Result) 0x0032)
#define CSR_WIFI_SME_IEEE80211_RESULT_REJECTED_LISTEN_INTERVAL_TOO_LARGE               ((CsrWifiSmeIEEE80211Result) 0x0033)
#define CSR_WIFI_SME_IEEE80211_RESULT_INVALID_FT_ACTION_FRAME_COUNT                    ((CsrWifiSmeIEEE80211Result) 0x0034)
#define CSR_WIFI_SME_IEEE80211_RESULT_INVALID_PMKID                                    ((CsrWifiSmeIEEE80211Result) 0x0035)
#define CSR_WIFI_SME_IEEE80211_RESULT_INVALID_MDIE                                     ((CsrWifiSmeIEEE80211Result) 0x0036)
#define CSR_WIFI_SME_IEEE80211_RESULT_INVALID_FTIE                                     ((CsrWifiSmeIEEE80211Result) 0x0037)
#define CSR_WIFI_SME_IEEE80211_RESULT_UNSPECIFIED_QOS_FAILURE                          ((CsrWifiSmeIEEE80211Result) 0x00c8)
#define CSR_WIFI_SME_IEEE80211_RESULT_WRONG_POLICY                                     ((CsrWifiSmeIEEE80211Result) 0x00c9)
#define CSR_WIFI_SME_IEEE80211_RESULT_INSUFFICIENT_BANDWIDTH                           ((CsrWifiSmeIEEE80211Result) 0x00ca)
#define CSR_WIFI_SME_IEEE80211_RESULT_INVALID_TSPEC_PARAMETERS                         ((CsrWifiSmeIEEE80211Result) 0x00cb)
#define CSR_WIFI_SME_IEEE80211_RESULT_TIMEOUT                                          ((CsrWifiSmeIEEE80211Result) 0x8000)
#define CSR_WIFI_SME_IEEE80211_RESULT_TOO_MANY_SIMULTANEOUS_REQUESTS                   ((CsrWifiSmeIEEE80211Result) 0x8001)
#define CSR_WIFI_SME_IEEE80211_RESULT_BSS_ALREADY_STARTED_OR_JOINED                    ((CsrWifiSmeIEEE80211Result) 0x8002)
#define CSR_WIFI_SME_IEEE80211_RESULT_NOT_SUPPORTED                                    ((CsrWifiSmeIEEE80211Result) 0x8003)
#define CSR_WIFI_SME_IEEE80211_RESULT_TRANSMISSION_FAILURE                             ((CsrWifiSmeIEEE80211Result) 0x8004)
#define CSR_WIFI_SME_IEEE80211_RESULT_REFUSED_NOT_AUTHENTICATED                        ((CsrWifiSmeIEEE80211Result) 0x8005)
#define CSR_WIFI_SME_IEEE80211_RESULT_RESET_REQUIRED_BEFORE_START                      ((CsrWifiSmeIEEE80211Result) 0x8006)
#define CSR_WIFI_SME_IEEE80211_RESULT_LM_INFO_UNAVAILABLE                              ((CsrWifiSmeIEEE80211Result) 0x8007)
#define CSR_WIFI_SME_IEEE80211_RESULT_INVALID_UNICAST_CIPHER                           ((CsrWifiSmeIEEE80211Result) 0xf02f)
#define CSR_WIFI_SME_IEEE80211_RESULT_INVALID_MULTICAST_CIPHER                         ((CsrWifiSmeIEEE80211Result) 0xf030)
#define CSR_WIFI_SME_IEEE80211_RESULT_UNSUPPORTED_WAPI_IE_VERSION                      ((CsrWifiSmeIEEE80211Result) 0xf031)
#define CSR_WIFI_SME_IEEE80211_RESULT_INVALID_WAPI_CAPABILITY_IE                       ((CsrWifiSmeIEEE80211Result) 0xf032)

/*******************************************************************************

  NAME
    CsrWifiSmeIndications

  DESCRIPTION
    Defines bits for CsrWifiSmeIndicationsMask

 VALUES
    CSR_WIFI_SME_INDICATIONS_NONE
                   - Used to cancel the registrations for receiving indications
    CSR_WIFI_SME_INDICATIONS_WIFIOFF
                   - Used to register for CSR_WIFI_SME_WIFI_OFF_IND events
    CSR_WIFI_SME_INDICATIONS_SCANRESULT
                   - Used to register for CSR_WIFI_SME_SCAN_RESULT_IND events
    CSR_WIFI_SME_INDICATIONS_CONNECTIONQUALITY
                   - Used to register for CSR_WIFI_SME_CONNECTION_QUALITY_IND
                     events
    CSR_WIFI_SME_INDICATIONS_MEDIASTATUS
                   - Used to register for CSR_WIFI_SME_MEDIA_STATUS_IND events
    CSR_WIFI_SME_INDICATIONS_MICFAILURE
                   - Used to register for CSR_WIFI_SME_MICFAILURE_IND events
    CSR_WIFI_SME_INDICATIONS_PMKIDCANDIDATELIST
                   - Used to register for CSR_WIFI_SME_PMKIDCANDIDATE_LIST_IND
                     events
    CSR_WIFI_SME_INDICATIONS_TSPEC
                   - Used to register for CSR_WIFI_SME_TSPEC_IND events
    CSR_WIFI_SME_INDICATIONS_ROAMSTART
                   - Used to register for CSR_WIFI_SME_ROAM_START_IND events
    CSR_WIFI_SME_INDICATIONS_ROAMCOMPLETE
                   - Used to register for CSR_WIFI_SME_ROAM_COMPLETE_IND events
    CSR_WIFI_SME_INDICATIONS_ASSOCIATIONSTART
                   - Used to register for CSR_WIFI_SME_ASSOCIATION_START_IND
                     events
    CSR_WIFI_SME_INDICATIONS_ASSOCIATIONCOMPLETE
                   - Used to register for CSR_WIFI_SME_ASSOCIATION_COMPLETE_IND
                     events
    CSR_WIFI_SME_INDICATIONS_IBSSSTATION
                   - Used to register for CSR_WIFI_SME_IBSS_STATION_IND events
    CSR_WIFI_SME_INDICATIONS_WIFION
                   - Used to register for CSR_WIFI_SME_WIFI_ON_IND events
    CSR_WIFI_SME_INDICATIONS_ERROR
                   - Used to register for CSR_WIFI_SME_ERROR_IND events
    CSR_WIFI_SME_INDICATIONS_INFO
                   - Used to register for CSR_WIFI_SME_INFO_IND events
    CSR_WIFI_SME_INDICATIONS_COREDUMP
                   - Used to register for CSR_WIFI_SME_CORE_DUMP_IND events
    CSR_WIFI_SME_INDICATIONS_ALL
                   - Used to register for all available indications

*******************************************************************************/
typedef CsrUint32 CsrWifiSmeIndications;
#define CSR_WIFI_SME_INDICATIONS_NONE                  ((CsrWifiSmeIndications) 0x00000000)
#define CSR_WIFI_SME_INDICATIONS_WIFIOFF               ((CsrWifiSmeIndications) 0x00000001)
#define CSR_WIFI_SME_INDICATIONS_SCANRESULT            ((CsrWifiSmeIndications) 0x00000002)
#define CSR_WIFI_SME_INDICATIONS_CONNECTIONQUALITY     ((CsrWifiSmeIndications) 0x00000004)
#define CSR_WIFI_SME_INDICATIONS_MEDIASTATUS           ((CsrWifiSmeIndications) 0x00000008)
#define CSR_WIFI_SME_INDICATIONS_MICFAILURE            ((CsrWifiSmeIndications) 0x00000010)
#define CSR_WIFI_SME_INDICATIONS_PMKIDCANDIDATELIST    ((CsrWifiSmeIndications) 0x00000020)
#define CSR_WIFI_SME_INDICATIONS_TSPEC                 ((CsrWifiSmeIndications) 0x00000040)
#define CSR_WIFI_SME_INDICATIONS_ROAMSTART             ((CsrWifiSmeIndications) 0x00000080)
#define CSR_WIFI_SME_INDICATIONS_ROAMCOMPLETE          ((CsrWifiSmeIndications) 0x00000100)
#define CSR_WIFI_SME_INDICATIONS_ASSOCIATIONSTART      ((CsrWifiSmeIndications) 0x00000200)
#define CSR_WIFI_SME_INDICATIONS_ASSOCIATIONCOMPLETE   ((CsrWifiSmeIndications) 0x00000400)
#define CSR_WIFI_SME_INDICATIONS_IBSSSTATION           ((CsrWifiSmeIndications) 0x00000800)
#define CSR_WIFI_SME_INDICATIONS_WIFION                ((CsrWifiSmeIndications) 0x00001000)
#define CSR_WIFI_SME_INDICATIONS_ERROR                 ((CsrWifiSmeIndications) 0x00002000)
#define CSR_WIFI_SME_INDICATIONS_INFO                  ((CsrWifiSmeIndications) 0x00004000)
#define CSR_WIFI_SME_INDICATIONS_COREDUMP              ((CsrWifiSmeIndications) 0x00008000)
#define CSR_WIFI_SME_INDICATIONS_ALL                   ((CsrWifiSmeIndications) 0xFFFFFFFF)

/*******************************************************************************

  NAME
    CsrWifiSmeKeyType

  DESCRIPTION
    Indicates the type of the key

 VALUES
    CSR_WIFI_SME_KEY_TYPE_GROUP    - Key for broadcast communication
    CSR_WIFI_SME_KEY_TYPE_PAIRWISE - Key for unicast communication
    CSR_WIFI_SME_KEY_TYPE_STAKEY   - Key for direct link communication to
                                     another station in infrastructure networks
    CSR_WIFI_SME_KEY_TYPE_IGTK     - Integrity Group Temporal Key
    CSR_WIFI_SME_KEY_TYPE_CCKM     - Key for Cisco Centralized Key Management

*******************************************************************************/
typedef u8 CsrWifiSmeKeyType;
#define CSR_WIFI_SME_KEY_TYPE_GROUP      ((CsrWifiSmeKeyType) 0x00)
#define CSR_WIFI_SME_KEY_TYPE_PAIRWISE   ((CsrWifiSmeKeyType) 0x01)
#define CSR_WIFI_SME_KEY_TYPE_STAKEY     ((CsrWifiSmeKeyType) 0x02)
#define CSR_WIFI_SME_KEY_TYPE_IGTK       ((CsrWifiSmeKeyType) 0x03)
#define CSR_WIFI_SME_KEY_TYPE_CCKM       ((CsrWifiSmeKeyType) 0x04)

/*******************************************************************************

  NAME
    CsrWifiSmeListAction

  DESCRIPTION
    Identifies the type of action to be performed on a list of items
    The values of this type are used across the NME/SME/Router API's and they
    must be kept consistent with the corresponding types in the .xml of the
    ottherinterfaces

 VALUES
    CSR_WIFI_SME_LIST_ACTION_GET    - Retrieve the current list of items
    CSR_WIFI_SME_LIST_ACTION_ADD    - Add one or more items
    CSR_WIFI_SME_LIST_ACTION_REMOVE - Remove one or more items
    CSR_WIFI_SME_LIST_ACTION_FLUSH  - Remove all items

*******************************************************************************/
typedef u8 CsrWifiSmeListAction;
#define CSR_WIFI_SME_LIST_ACTION_GET      ((CsrWifiSmeListAction) 0x00)
#define CSR_WIFI_SME_LIST_ACTION_ADD      ((CsrWifiSmeListAction) 0x01)
#define CSR_WIFI_SME_LIST_ACTION_REMOVE   ((CsrWifiSmeListAction) 0x02)
#define CSR_WIFI_SME_LIST_ACTION_FLUSH    ((CsrWifiSmeListAction) 0x03)

/*******************************************************************************

  NAME
    CsrWifiSmeMediaStatus

  DESCRIPTION
    Indicates the connection status
    The values of this type are used across the NME/SME/Router API's and they
    must be kept consistent with the corresponding types in the .xml of the
    ottherinterfaces

 VALUES
    CSR_WIFI_SME_MEDIA_STATUS_CONNECTED
                   - Value CSR_WIFI_SME_MEDIA_STATUS_CONNECTED can happen in two
                     situations:
                       * A network connection is established. Specifically, this is
                         when the MLME_ASSOCIATION completes or the first peer
                         relationship is established in an IBSS. In a WPA/WPA2
                         network, this indicates that the stack is ready to perform
                         the 4-way handshake or 802.1x authentication if CSR NME
                         security library is not used. If CSR NME security library
                         is used this indicates, completion of 4way handshake or
                         802.1x authentication
                       * The SME roams to another AP on the same ESS
                     During the AP operation, it indicates that the peer station
                     is connected to the AP and is ready for data transfer.
    CSR_WIFI_SME_MEDIA_STATUS_DISCONNECTED
                   - Value CSR_WIFI_SME_MEDIA_STATUS_DISCONNECTED can happen in
                     two situations:
                       * when the connection to a network is lost and there is no
                         alternative on the same ESS to roam to
                       * when a CSR_WIFI_SME_DISCONNECT_REQ request is issued
                     During AP or P2PGO operation, it indicates that the peer
                     station has disconnected from the AP

*******************************************************************************/
typedef u8 CsrWifiSmeMediaStatus;
#define CSR_WIFI_SME_MEDIA_STATUS_CONNECTED      ((CsrWifiSmeMediaStatus) 0x00)
#define CSR_WIFI_SME_MEDIA_STATUS_DISCONNECTED   ((CsrWifiSmeMediaStatus) 0x01)

/*******************************************************************************

  NAME
    CsrWifiSmeP2pCapability

  DESCRIPTION
    Defines P2P Device Capabilities

 VALUES
    CSR_WIFI_SME_P2P_SERVICE_DISCOVERY_CAPABILITY
                   - This field is set to 1 if the P2P Device supports Service
                     Discovery, and to 0 otherwise
    CSR_WIFI_SME_P2P_CLIENT_DISCOVERABILITY_CAPABILITY
                   - This field is set to 1 when the P2P Device supports P2P
                     Client Discoverability, and to 0 otherwise.
    CSR_WIFI_SME_P2P_CONCURRENT_OPERATION_CAPABILITY
                   - This field is set to 1 when the P2P Device supports
                     Concurrent Operation with WLAN, and to 0 otherwise.
    CSR_WIFI_SME_P2P_MANAGED_DEVICE_CAPABILITY
                   - This field is set to 1 when the P2P interface of the P2P
                     Device is capable of being managed by the WLAN
                     (infrastructure network) based on P2P coexistence
                     parameters, and to 0 otherwise
    CSR_WIFI_SME_P2P_INVITAION_CAPABILITY
                   - This field is set to 1 if the P2P Device is capable of
                     processing P2P Invitation Procedure signaling, and to 0
                     otherwise.

*******************************************************************************/
typedef u8 CsrWifiSmeP2pCapability;
#define CSR_WIFI_SME_P2P_SERVICE_DISCOVERY_CAPABILITY        ((CsrWifiSmeP2pCapability) 0x01)
#define CSR_WIFI_SME_P2P_CLIENT_DISCOVERABILITY_CAPABILITY   ((CsrWifiSmeP2pCapability) 0x02)
#define CSR_WIFI_SME_P2P_CONCURRENT_OPERATION_CAPABILITY     ((CsrWifiSmeP2pCapability) 0x04)
#define CSR_WIFI_SME_P2P_MANAGED_DEVICE_CAPABILITY           ((CsrWifiSmeP2pCapability) 0x08)
#define CSR_WIFI_SME_P2P_INVITAION_CAPABILITY                ((CsrWifiSmeP2pCapability) 0x20)

/*******************************************************************************

  NAME
    CsrWifiSmeP2pGroupCapability

  DESCRIPTION
    Define bits for P2P Group Capability

 VALUES
    CSR_WIFI_P2P_GRP_CAP_GO
                   - Indicates if the local device has become a GO after GO
                     negotiation
    CSR_WIFI_P2P_GRP_CAP_PERSISTENT
                   - Persistent group
    CSR_WIFI_P2P_GRP_CAP_INTRABSS_DIST
                   - Intra-BSS data distribution support
    CSR_WIFI_P2P_GRP_CAP_CROSS_CONN
                   - Support of cross connection
    CSR_WIFI_P2P_GRP_CAP_PERSISTENT_RECONNECT
                   - Support of persistent reconnect

*******************************************************************************/
typedef u8 CsrWifiSmeP2pGroupCapability;
#define CSR_WIFI_P2P_GRP_CAP_GO                     ((CsrWifiSmeP2pGroupCapability) 0x01)
#define CSR_WIFI_P2P_GRP_CAP_PERSISTENT             ((CsrWifiSmeP2pGroupCapability) 0x02)
#define CSR_WIFI_P2P_GRP_CAP_INTRABSS_DIST          ((CsrWifiSmeP2pGroupCapability) 0x08)
#define CSR_WIFI_P2P_GRP_CAP_CROSS_CONN             ((CsrWifiSmeP2pGroupCapability) 0x10)
#define CSR_WIFI_P2P_GRP_CAP_PERSISTENT_RECONNECT   ((CsrWifiSmeP2pGroupCapability) 0x20)

/*******************************************************************************

  NAME
    CsrWifiSmeP2pNoaConfigMethod

  DESCRIPTION
    Notice of Absece Configuration

 VALUES
    CSR_WIFI_P2P_NOA_NONE         - Do not use NOA
    CSR_WIFI_P2P_NOA_AUTONOMOUS   - NOA based on the traffic analysis
    CSR_WIFI_P2P_NOA_USER_DEFINED - NOA as specified by the user

*******************************************************************************/
typedef u8 CsrWifiSmeP2pNoaConfigMethod;
#define CSR_WIFI_P2P_NOA_NONE           ((CsrWifiSmeP2pNoaConfigMethod) 0x00)
#define CSR_WIFI_P2P_NOA_AUTONOMOUS     ((CsrWifiSmeP2pNoaConfigMethod) 0x01)
#define CSR_WIFI_P2P_NOA_USER_DEFINED   ((CsrWifiSmeP2pNoaConfigMethod) 0x02)

/*******************************************************************************

  NAME
    CsrWifiSmeP2pRole

  DESCRIPTION
    Definition of roles for a P2P Device

 VALUES
    CSR_WIFI_SME_P2P_ROLE_NONE       - A non-P2PDevice device
    CSR_WIFI_SME_P2P_ROLE_STANDALONE - A Standalone P2P device
    CSR_WIFI_SME_P2P_ROLE_GO         - Role Assumed is that of a Group Owner
                                       within a P2P Group
    CSR_WIFI_SME_P2P_ROLE_CLI        - Role Assumed is that of a P2P Client
                                       within a P2P Group

*******************************************************************************/
typedef u8 CsrWifiSmeP2pRole;
#define CSR_WIFI_SME_P2P_ROLE_NONE         ((CsrWifiSmeP2pRole) 0x00)
#define CSR_WIFI_SME_P2P_ROLE_STANDALONE   ((CsrWifiSmeP2pRole) 0x01)
#define CSR_WIFI_SME_P2P_ROLE_GO           ((CsrWifiSmeP2pRole) 0x02)
#define CSR_WIFI_SME_P2P_ROLE_CLI          ((CsrWifiSmeP2pRole) 0x03)

/*******************************************************************************

  NAME
    CsrWifiSmeP2pStatus

  DESCRIPTION
    This data type enumerates the outcome of P2P procedure

 VALUES
    CSR_WIFI_SME_P2P_STATUS_SUCCESS
                   - Success
    CSR_WIFI_SME_P2P_STATUS_FAIL_INFO_UNAVAILABLE
                   - Fail; information is currently unavailable
    CSR_WIFI_SME_P2P_STATUS_FAIL_INCOMPATIBLE_PARAM
                   - Fail; incompatible parameters
    CSR_WIFI_SME_P2P_STATUS_FAIL_LIMIT_REACHED
                   - Fail; limit reached
    CSR_WIFI_SME_P2P_STATUS_FAIL_INVALID_PARAM
                   - Fail; invalid parameters
    CSR_WIFI_SME_P2P_STATUS_FAIL_ACCOMODATE
                   - Fail; unable to accommodate request
    CSR_WIFI_SME_P2P_STATUS_FAIL_PREV_ERROR
                   - Fail; previous protocol error, or disruptive behavior
    CSR_WIFI_SME_P2P_STATUS_FAIL_COMMON_CHANNELS
                   - Fail; no common channels
    CSR_WIFI_SME_P2P_STATUS_FAIL_UNKNOWN_GROUP
                   - Fail; unknown P2P Group
    CSR_WIFI_SME_P2P_STATUS_FAIL_GO_INTENT
                   - Fail: both P2P Devices indicated an Intent of 15 in Group
                     Owner Negotiation
    CSR_WIFI_SME_P2P_STATUS_FAIL_PROVISION_METHOD_INCOMPATIBLE
                   - Fail; incompatible provisioning method
    CSR_WIFI_SME_P2P_STATUS_FAIL_REJECT
                   - Fail: rejected by user
    CSR_WIFI_SME_P2P_STATUS_FAIL_RESERVED
                   - Fail; Status Reserved

*******************************************************************************/
typedef u8 CsrWifiSmeP2pStatus;
#define CSR_WIFI_SME_P2P_STATUS_SUCCESS                              ((CsrWifiSmeP2pStatus) 0x00)
#define CSR_WIFI_SME_P2P_STATUS_FAIL_INFO_UNAVAILABLE                ((CsrWifiSmeP2pStatus) 0x01)
#define CSR_WIFI_SME_P2P_STATUS_FAIL_INCOMPATIBLE_PARAM              ((CsrWifiSmeP2pStatus) 0x02)
#define CSR_WIFI_SME_P2P_STATUS_FAIL_LIMIT_REACHED                   ((CsrWifiSmeP2pStatus) 0x03)
#define CSR_WIFI_SME_P2P_STATUS_FAIL_INVALID_PARAM                   ((CsrWifiSmeP2pStatus) 0x04)
#define CSR_WIFI_SME_P2P_STATUS_FAIL_ACCOMODATE                      ((CsrWifiSmeP2pStatus) 0x05)
#define CSR_WIFI_SME_P2P_STATUS_FAIL_PREV_ERROR                      ((CsrWifiSmeP2pStatus) 0x06)
#define CSR_WIFI_SME_P2P_STATUS_FAIL_COMMON_CHANNELS                 ((CsrWifiSmeP2pStatus) 0x07)
#define CSR_WIFI_SME_P2P_STATUS_FAIL_UNKNOWN_GROUP                   ((CsrWifiSmeP2pStatus) 0x08)
#define CSR_WIFI_SME_P2P_STATUS_FAIL_GO_INTENT                       ((CsrWifiSmeP2pStatus) 0x09)
#define CSR_WIFI_SME_P2P_STATUS_FAIL_PROVISION_METHOD_INCOMPATIBLE   ((CsrWifiSmeP2pStatus) 0x0A)
#define CSR_WIFI_SME_P2P_STATUS_FAIL_REJECT                          ((CsrWifiSmeP2pStatus) 0x0B)
#define CSR_WIFI_SME_P2P_STATUS_FAIL_RESERVED                        ((CsrWifiSmeP2pStatus) 0xFF)

/*******************************************************************************

  NAME
    CsrWifiSmePacketFilterMode

  DESCRIPTION
    Options for the filter mode parameter.
    The Values here match the HIP interface

 VALUES
    CSR_WIFI_SME_PACKET_FILTER_MODE_OPT_OUT
                   - Broadcast packets are always reported to the host unless
                     they match at least one of the specified TCLAS IEs.
    CSR_WIFI_SME_PACKET_FILTER_MODE_OPT_IN
                   - Broadcast packets are reported to the host only if they
                     match at least one of the specified TCLAS IEs.

*******************************************************************************/
typedef u8 CsrWifiSmePacketFilterMode;
#define CSR_WIFI_SME_PACKET_FILTER_MODE_OPT_OUT   ((CsrWifiSmePacketFilterMode) 0x00)
#define CSR_WIFI_SME_PACKET_FILTER_MODE_OPT_IN    ((CsrWifiSmePacketFilterMode) 0x03)

/*******************************************************************************

  NAME
    CsrWifiSmePowerSaveLevel

  DESCRIPTION
    Power Save Level options as defined in the IEEE 802.11 standards
    First 3 values are set to match the mlme PowerManagementMode

 VALUES
    CSR_WIFI_SME_POWER_SAVE_LEVEL_LOW  - No power save: the driver will remain
                                         active at all times
    CSR_WIFI_SME_POWER_SAVE_LEVEL_HIGH - Enter power save after all packets in
                                         the queues are transmitted and received
    CSR_WIFI_SME_POWER_SAVE_LEVEL_MED  - Enter power save after all packets in
                                         the queues are transmitted and received
                                         and a further configurable delay
                                         (default 1s) has elapsed
    CSR_WIFI_SME_POWER_SAVE_LEVEL_AUTO - The SME will decide when to enter power
                                         save mode according to the traffic
                                         analysis

*******************************************************************************/
typedef u8 CsrWifiSmePowerSaveLevel;
#define CSR_WIFI_SME_POWER_SAVE_LEVEL_LOW    ((CsrWifiSmePowerSaveLevel) 0x00)
#define CSR_WIFI_SME_POWER_SAVE_LEVEL_HIGH   ((CsrWifiSmePowerSaveLevel) 0x01)
#define CSR_WIFI_SME_POWER_SAVE_LEVEL_MED    ((CsrWifiSmePowerSaveLevel) 0x02)
#define CSR_WIFI_SME_POWER_SAVE_LEVEL_AUTO   ((CsrWifiSmePowerSaveLevel) 0x03)

/*******************************************************************************

  NAME
    CsrWifiSmePreambleType

  DESCRIPTION
    SME Preamble Types

 VALUES
    CSR_WIFI_SME_USE_LONG_PREAMBLE  - Use legacy (long) preamble
    CSR_WIFI_SME_USE_SHORT_PREAMBLE - Use short PPDU format

*******************************************************************************/
typedef u8 CsrWifiSmePreambleType;
#define CSR_WIFI_SME_USE_LONG_PREAMBLE    ((CsrWifiSmePreambleType) 0x00)
#define CSR_WIFI_SME_USE_SHORT_PREAMBLE   ((CsrWifiSmePreambleType) 0x01)

/*******************************************************************************

  NAME
    CsrWifiSmeRadioIF

  DESCRIPTION
    Indicates the frequency

 VALUES
    CSR_WIFI_SME_RADIO_IF_GHZ_2_4 - Indicates the 2.4 GHZ frequency
    CSR_WIFI_SME_RADIO_IF_GHZ_5_0 - Future use: currently not supported
    CSR_WIFI_SME_RADIO_IF_BOTH    - Future use: currently not supported

*******************************************************************************/
typedef u8 CsrWifiSmeRadioIF;
#define CSR_WIFI_SME_RADIO_IF_GHZ_2_4   ((CsrWifiSmeRadioIF) 0x01)
#define CSR_WIFI_SME_RADIO_IF_GHZ_5_0   ((CsrWifiSmeRadioIF) 0x02)
#define CSR_WIFI_SME_RADIO_IF_BOTH      ((CsrWifiSmeRadioIF) 0x03)

/*******************************************************************************

  NAME
    CsrWifiSmeRegulatoryDomain

  DESCRIPTION
    Indicates the regulatory domain as defined in IEEE 802.11 standards

 VALUES
    CSR_WIFI_SME_REGULATORY_DOMAIN_OTHER
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_REGULATORY_DOMAIN_FCC
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_REGULATORY_DOMAIN_IC
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_REGULATORY_DOMAIN_ETSI
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_REGULATORY_DOMAIN_SPAIN
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_REGULATORY_DOMAIN_FRANCE
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_REGULATORY_DOMAIN_JAPAN
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_REGULATORY_DOMAIN_JAPANBIS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_REGULATORY_DOMAIN_CHINA
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_REGULATORY_DOMAIN_CHINABIS
                   - See IEEE 802.11 Standard
    CSR_WIFI_SME_REGULATORY_DOMAIN_NONE
                   - See IEEE 802.11 Standard

*******************************************************************************/
typedef u8 CsrWifiSmeRegulatoryDomain;
#define CSR_WIFI_SME_REGULATORY_DOMAIN_OTHER      ((CsrWifiSmeRegulatoryDomain) 0x00)
#define CSR_WIFI_SME_REGULATORY_DOMAIN_FCC        ((CsrWifiSmeRegulatoryDomain) 0x10)
#define CSR_WIFI_SME_REGULATORY_DOMAIN_IC         ((CsrWifiSmeRegulatoryDomain) 0x20)
#define CSR_WIFI_SME_REGULATORY_DOMAIN_ETSI       ((CsrWifiSmeRegulatoryDomain) 0x30)
#define CSR_WIFI_SME_REGULATORY_DOMAIN_SPAIN      ((CsrWifiSmeRegulatoryDomain) 0x31)
#define CSR_WIFI_SME_REGULATORY_DOMAIN_FRANCE     ((CsrWifiSmeRegulatoryDomain) 0x32)
#define CSR_WIFI_SME_REGULATORY_DOMAIN_JAPAN      ((CsrWifiSmeRegulatoryDomain) 0x40)
#define CSR_WIFI_SME_REGULATORY_DOMAIN_JAPANBIS   ((CsrWifiSmeRegulatoryDomain) 0x41)
#define CSR_WIFI_SME_REGULATORY_DOMAIN_CHINA      ((CsrWifiSmeRegulatoryDomain) 0x50)
#define CSR_WIFI_SME_REGULATORY_DOMAIN_CHINABIS   ((CsrWifiSmeRegulatoryDomain) 0x51)
#define CSR_WIFI_SME_REGULATORY_DOMAIN_NONE       ((CsrWifiSmeRegulatoryDomain) 0xFF)

/*******************************************************************************

  NAME
    CsrWifiSmeRoamReason

  DESCRIPTION
    Indicates the reason for roaming

 VALUES
    CSR_WIFI_SME_ROAM_REASON_BEACONLOST
                   - The station cannot receive the beacon signal any more
    CSR_WIFI_SME_ROAM_REASON_DISASSOCIATED
                   - The station has been disassociated
    CSR_WIFI_SME_ROAM_REASON_DEAUTHENTICATED
                   - The station has been deauthenticated
    CSR_WIFI_SME_ROAM_REASON_BETTERAPFOUND
                   - A better AP has been found

*******************************************************************************/
typedef u8 CsrWifiSmeRoamReason;
#define CSR_WIFI_SME_ROAM_REASON_BEACONLOST        ((CsrWifiSmeRoamReason) 0x00)
#define CSR_WIFI_SME_ROAM_REASON_DISASSOCIATED     ((CsrWifiSmeRoamReason) 0x01)
#define CSR_WIFI_SME_ROAM_REASON_DEAUTHENTICATED   ((CsrWifiSmeRoamReason) 0x02)
#define CSR_WIFI_SME_ROAM_REASON_BETTERAPFOUND     ((CsrWifiSmeRoamReason) 0x03)

/*******************************************************************************

  NAME
    CsrWifiSmeScanType

  DESCRIPTION
    Identifies the type of scan to be performed

 VALUES
    CSR_WIFI_SME_SCAN_TYPE_ALL     - Scan actively or passively according to the
                                     regulatory domain restrictions
    CSR_WIFI_SME_SCAN_TYPE_ACTIVE  - Scan actively only: send probes and listen
                                     for answers
    CSR_WIFI_SME_SCAN_TYPE_PASSIVE - Scan passively only: listen for beacon
                                     messages

*******************************************************************************/
typedef u8 CsrWifiSmeScanType;
#define CSR_WIFI_SME_SCAN_TYPE_ALL       ((CsrWifiSmeScanType) 0x00)
#define CSR_WIFI_SME_SCAN_TYPE_ACTIVE    ((CsrWifiSmeScanType) 0x01)
#define CSR_WIFI_SME_SCAN_TYPE_PASSIVE   ((CsrWifiSmeScanType) 0x02)

/*******************************************************************************

  NAME
    CsrWifiSmeTrafficType

  DESCRIPTION
    Identifies the type of traffic going on on the connection.
    The values of this type are used across the NME/SME/Router API's and they
    must be kept consistent with the corresponding types in the .xml of the
    ottherinterfaces

 VALUES
    CSR_WIFI_SME_TRAFFIC_TYPE_OCCASIONAL
                   - During the last 30 seconds there were fewer than 20 packets
                     per seconds in each second in both directions
    CSR_WIFI_SME_TRAFFIC_TYPE_BURSTY
                   - During the last 30 seconds there was at least one second
                     during which more than 20 packets were received in either
                     direction
    CSR_WIFI_SME_TRAFFIC_TYPE_PERIODIC
                   - During the last 5 seconds there were at least 10 packets
                     received each second and a defined period for the traffic
                     can be recognized
    CSR_WIFI_SME_TRAFFIC_TYPE_CONTINUOUS
                   - During the last 5 seconds there were at least 20 packets
                     received each second in either direction

*******************************************************************************/
typedef u8 CsrWifiSmeTrafficType;
#define CSR_WIFI_SME_TRAFFIC_TYPE_OCCASIONAL   ((CsrWifiSmeTrafficType) 0x00)
#define CSR_WIFI_SME_TRAFFIC_TYPE_BURSTY       ((CsrWifiSmeTrafficType) 0x01)
#define CSR_WIFI_SME_TRAFFIC_TYPE_PERIODIC     ((CsrWifiSmeTrafficType) 0x02)
#define CSR_WIFI_SME_TRAFFIC_TYPE_CONTINUOUS   ((CsrWifiSmeTrafficType) 0x03)

/*******************************************************************************

  NAME
    CsrWifiSmeTspecCtrl

  DESCRIPTION
    Defines bits for CsrWifiSmeTspecCtrlMask for additional CCX configuration.
    CURRENTLY NOT SUPPORTED.

 VALUES
    CSR_WIFI_SME_TSPEC_CTRL_STRICT
                   - No automatic negotiation
    CSR_WIFI_SME_TSPEC_CTRL_CCX_SIGNALLING
                   - Signalling TSPEC
    CSR_WIFI_SME_TSPEC_CTRL_CCX_VOICE
                   - Voice traffic TSPEC

*******************************************************************************/
typedef u8 CsrWifiSmeTspecCtrl;
#define CSR_WIFI_SME_TSPEC_CTRL_STRICT           ((CsrWifiSmeTspecCtrl) 0x01)
#define CSR_WIFI_SME_TSPEC_CTRL_CCX_SIGNALLING   ((CsrWifiSmeTspecCtrl) 0x02)
#define CSR_WIFI_SME_TSPEC_CTRL_CCX_VOICE        ((CsrWifiSmeTspecCtrl) 0x04)

/*******************************************************************************

  NAME
    CsrWifiSmeTspecResultCode

  DESCRIPTION
    Defines the result of the TSPEC exchanges with the AP

 VALUES
    CSR_WIFI_SME_TSPEC_RESULT_SUCCESS
                   - TSPEC command has been processed correctly
    CSR_WIFI_SME_TSPEC_RESULT_UNSPECIFIED_QOS_FAILURE
                   - The Access Point reported a failure
    CSR_WIFI_SME_TSPEC_RESULT_FAILURE
                   - Internal failure in the SME
    CSR_WIFI_SME_TSPEC_RESULT_INVALID_TSPEC_PARAMETERS
                   - The TSPEC parameters are invalid
    CSR_WIFI_SME_TSPEC_RESULT_INVALID_TCLAS_PARAMETERS
                   - The TCLASS parameters are invalid
    CSR_WIFI_SME_TSPEC_RESULT_INSUFFICIENT_BANDWIDTH
                   - As specified by the WMM Spec
    CSR_WIFI_SME_TSPEC_RESULT_WRONG_POLICY
                   - As specified by the WMM Spec
    CSR_WIFI_SME_TSPEC_RESULT_REJECTED_WITH_SUGGESTED_CHANGES
                   - As specified by the WMM Spec
    CSR_WIFI_SME_TSPEC_RESULT_TIMEOUT
                   - The TSPEC negotiation timed out
    CSR_WIFI_SME_TSPEC_RESULT_NOT_SUPPORTED
                   - The Access Point does not support the TSPEC
    CSR_WIFI_SME_TSPEC_RESULT_IE_LENGTH_INCORRECT
                   - The length of the TSPEC is not correct
    CSR_WIFI_SME_TSPEC_RESULT_INVALID_TRANSACTION_ID
                   - The TSPEC transaction id is not in the list
    CSR_WIFI_SME_TSPEC_RESULT_INSTALLED
                   - The TSPEC has been installed and it is now active.
    CSR_WIFI_SME_TSPEC_RESULT_TID_ALREADY_INSTALLED
                   - The Traffic ID has already been installed
    CSR_WIFI_SME_TSPEC_RESULT_TSPEC_REMOTELY_DELETED
                   - The AP has deleted the TSPEC

*******************************************************************************/
typedef u8 CsrWifiSmeTspecResultCode;
#define CSR_WIFI_SME_TSPEC_RESULT_SUCCESS                           ((CsrWifiSmeTspecResultCode) 0x00)
#define CSR_WIFI_SME_TSPEC_RESULT_UNSPECIFIED_QOS_FAILURE           ((CsrWifiSmeTspecResultCode) 0x01)
#define CSR_WIFI_SME_TSPEC_RESULT_FAILURE                           ((CsrWifiSmeTspecResultCode) 0x02)
#define CSR_WIFI_SME_TSPEC_RESULT_INVALID_TSPEC_PARAMETERS          ((CsrWifiSmeTspecResultCode) 0x05)
#define CSR_WIFI_SME_TSPEC_RESULT_INVALID_TCLAS_PARAMETERS          ((CsrWifiSmeTspecResultCode) 0x06)
#define CSR_WIFI_SME_TSPEC_RESULT_INSUFFICIENT_BANDWIDTH            ((CsrWifiSmeTspecResultCode) 0x07)
#define CSR_WIFI_SME_TSPEC_RESULT_WRONG_POLICY                      ((CsrWifiSmeTspecResultCode) 0x08)
#define CSR_WIFI_SME_TSPEC_RESULT_REJECTED_WITH_SUGGESTED_CHANGES   ((CsrWifiSmeTspecResultCode) 0x09)
#define CSR_WIFI_SME_TSPEC_RESULT_TIMEOUT                           ((CsrWifiSmeTspecResultCode) 0x0D)
#define CSR_WIFI_SME_TSPEC_RESULT_NOT_SUPPORTED                     ((CsrWifiSmeTspecResultCode) 0x0E)
#define CSR_WIFI_SME_TSPEC_RESULT_IE_LENGTH_INCORRECT               ((CsrWifiSmeTspecResultCode) 0x10)
#define CSR_WIFI_SME_TSPEC_RESULT_INVALID_TRANSACTION_ID            ((CsrWifiSmeTspecResultCode) 0x11)
#define CSR_WIFI_SME_TSPEC_RESULT_INSTALLED                         ((CsrWifiSmeTspecResultCode) 0x12)
#define CSR_WIFI_SME_TSPEC_RESULT_TID_ALREADY_INSTALLED             ((CsrWifiSmeTspecResultCode) 0x13)
#define CSR_WIFI_SME_TSPEC_RESULT_TSPEC_REMOTELY_DELETED            ((CsrWifiSmeTspecResultCode) 0x14)

/*******************************************************************************

  NAME
    CsrWifiSmeWepAuthMode

  DESCRIPTION
    Define bits for CsrWifiSmeWepAuthMode

 VALUES
    CSR_WIFI_SME_WEP_AUTH_MODE_OPEN   - Open-WEP enabled network
    CSR_WIFI_SME_WEP_AUTH_MODE_SHARED - Shared-key WEP enabled network.

*******************************************************************************/
typedef u8 CsrWifiSmeWepAuthMode;
#define CSR_WIFI_SME_WEP_AUTH_MODE_OPEN     ((CsrWifiSmeWepAuthMode) 0x00)
#define CSR_WIFI_SME_WEP_AUTH_MODE_SHARED   ((CsrWifiSmeWepAuthMode) 0x01)

/*******************************************************************************

  NAME
    CsrWifiSmeWepCredentialType

  DESCRIPTION
    Definition of types of WEP Credentials

 VALUES
    CSR_WIFI_SME_CREDENTIAL_TYPE_WEP64
                   - WEP 64 credential
    CSR_WIFI_SME_CREDENTIAL_TYPE_WEP128
                   - WEP 128 credential

*******************************************************************************/
typedef u8 CsrWifiSmeWepCredentialType;
#define CSR_WIFI_SME_CREDENTIAL_TYPE_WEP64    ((CsrWifiSmeWepCredentialType) 0x00)
#define CSR_WIFI_SME_CREDENTIAL_TYPE_WEP128   ((CsrWifiSmeWepCredentialType) 0x01)

/*******************************************************************************

  NAME
    CsrWifiSmeWmmMode

  DESCRIPTION
    Defines bits for CsrWifiSmeWmmModeMask: enable/disable WMM features.

 VALUES
    CSR_WIFI_SME_WMM_MODE_DISABLED   - Disables the WMM features.
    CSR_WIFI_SME_WMM_MODE_AC_ENABLED - Enables support for WMM-AC.
    CSR_WIFI_SME_WMM_MODE_PS_ENABLED - Enables support for WMM Power Save.
    CSR_WIFI_SME_WMM_MODE_SA_ENABLED - Currently not supported
    CSR_WIFI_SME_WMM_MODE_ENABLED    - Enables support for all currently
                                       available WMM features.

*******************************************************************************/
typedef u8 CsrWifiSmeWmmMode;
#define CSR_WIFI_SME_WMM_MODE_DISABLED     ((CsrWifiSmeWmmMode) 0x00)
#define CSR_WIFI_SME_WMM_MODE_AC_ENABLED   ((CsrWifiSmeWmmMode) 0x01)
#define CSR_WIFI_SME_WMM_MODE_PS_ENABLED   ((CsrWifiSmeWmmMode) 0x02)
#define CSR_WIFI_SME_WMM_MODE_SA_ENABLED   ((CsrWifiSmeWmmMode) 0x04)
#define CSR_WIFI_SME_WMM_MODE_ENABLED      ((CsrWifiSmeWmmMode) 0xFF)

/*******************************************************************************

  NAME
    CsrWifiSmeWmmQosInfo

  DESCRIPTION
    Defines bits for the QoS Info Octect as defined in the WMM specification.
    The first four values define one bit each that can be set or cleared.
    Each of the last four values define all the remaining 4 bits and only one
    of them at the time shall be used.
    For more information, see 'WMM (including WMM Power Save) Specification -
    Version 1.1' and 'UniFi Configuring WMM and WMM-PS, Application Note'.

 VALUES
    CSR_WIFI_SME_WMM_QOS_INFO_AC_MAX_SP_ALL
                   - WMM AP may deliver all buffered frames
    CSR_WIFI_SME_WMM_QOS_INFO_AC_VO
                   - Enable UAPSD(both trigger and delivery) for Voice Access
                     Category
    CSR_WIFI_SME_WMM_QOS_INFO_AC_VI
                   - Enable UAPSD(both trigger and delivery) for  Video Access
                     Category
    CSR_WIFI_SME_WMM_QOS_INFO_AC_BK
                   - Enable UAPSD(both trigger and delivery) for  Background
                     Access Category
    CSR_WIFI_SME_WMM_QOS_INFO_AC_BE
                   - Enable UAPSD(both trigger and delivery) for  Best Effort
                     Access Category
    CSR_WIFI_SME_WMM_QOS_INFO_AC_MAX_SP_TWO
                   - WMM AP may deliver a maximum of 2 buffered frames (MSDUs
                     and MMPDUs) per USP
    CSR_WIFI_SME_WMM_QOS_INFO_AC_MAX_SP_FOUR
                   - WMM AP may deliver a maximum of 4 buffered frames (MSDUs
                     and MMPDUs) per USP
    CSR_WIFI_SME_WMM_QOS_INFO_AC_MAX_SP_SIX
                   - WMM AP may deliver a maximum of 6 buffered frames (MSDUs
                     and MMPDUs) per USP

*******************************************************************************/
typedef u8 CsrWifiSmeWmmQosInfo;
#define CSR_WIFI_SME_WMM_QOS_INFO_AC_MAX_SP_ALL    ((CsrWifiSmeWmmQosInfo) 0x00)
#define CSR_WIFI_SME_WMM_QOS_INFO_AC_VO            ((CsrWifiSmeWmmQosInfo) 0x01)
#define CSR_WIFI_SME_WMM_QOS_INFO_AC_VI            ((CsrWifiSmeWmmQosInfo) 0x02)
#define CSR_WIFI_SME_WMM_QOS_INFO_AC_BK            ((CsrWifiSmeWmmQosInfo) 0x04)
#define CSR_WIFI_SME_WMM_QOS_INFO_AC_BE            ((CsrWifiSmeWmmQosInfo) 0x08)
#define CSR_WIFI_SME_WMM_QOS_INFO_AC_MAX_SP_TWO    ((CsrWifiSmeWmmQosInfo) 0x20)
#define CSR_WIFI_SME_WMM_QOS_INFO_AC_MAX_SP_FOUR   ((CsrWifiSmeWmmQosInfo) 0x40)
#define CSR_WIFI_SME_WMM_QOS_INFO_AC_MAX_SP_SIX    ((CsrWifiSmeWmmQosInfo) 0x60)

/*******************************************************************************

  NAME
    CsrWifiSmeWpsConfigType

  DESCRIPTION
    WPS config methods supported/used by a device

 VALUES
    CSR_WIFI_WPS_CONFIG_LABEL
                   - Label
    CSR_WIFI_WPS_CONFIG_DISPLAY
                   - Display
    CSR_WIFI_WPS_CONFIG_EXT_NFC
                   - External NFC : Not supported in this release
    CSR_WIFI_WPS_CONFIG_INT_NFC
                   - Internal NFC : Not supported in this release
    CSR_WIFI_WPS_CONFIG_NFC_IFACE
                   - NFC interface : Not supported in this release
    CSR_WIFI_WPS_CONFIG_PBC
                   - PBC
    CSR_WIFI_WPS_CONFIG_KEYPAD
                   - KeyPad
    CSR_WIFI_WPS_CONFIG_VIRTUAL_PBC
                   - PBC through software user interface
    CSR_WIFI_WPS_CONFIG_PHYSICAL_PBC
                   - Physical PBC
    CSR_WIFI_WPS_CONFIG_VIRTUAL_DISPLAY
                   - Virtual Display : via html config page etc
    CSR_WIFI_WPS_CONFIG_PHYSICAL_DISPLAY
                   - Physical Display : Attached to the device

*******************************************************************************/
typedef u16 CsrWifiSmeWpsConfigType;
#define CSR_WIFI_WPS_CONFIG_LABEL              ((CsrWifiSmeWpsConfigType) 0x0004)
#define CSR_WIFI_WPS_CONFIG_DISPLAY            ((CsrWifiSmeWpsConfigType) 0x0008)
#define CSR_WIFI_WPS_CONFIG_EXT_NFC            ((CsrWifiSmeWpsConfigType) 0x0010)
#define CSR_WIFI_WPS_CONFIG_INT_NFC            ((CsrWifiSmeWpsConfigType) 0x0020)
#define CSR_WIFI_WPS_CONFIG_NFC_IFACE          ((CsrWifiSmeWpsConfigType) 0x0040)
#define CSR_WIFI_WPS_CONFIG_PBC                ((CsrWifiSmeWpsConfigType) 0x0080)
#define CSR_WIFI_WPS_CONFIG_KEYPAD             ((CsrWifiSmeWpsConfigType) 0x0100)
#define CSR_WIFI_WPS_CONFIG_VIRTUAL_PBC        ((CsrWifiSmeWpsConfigType) 0x0280)
#define CSR_WIFI_WPS_CONFIG_PHYSICAL_PBC       ((CsrWifiSmeWpsConfigType) 0x0480)
#define CSR_WIFI_WPS_CONFIG_VIRTUAL_DISPLAY    ((CsrWifiSmeWpsConfigType) 0x2008)
#define CSR_WIFI_WPS_CONFIG_PHYSICAL_DISPLAY   ((CsrWifiSmeWpsConfigType) 0x4008)

/*******************************************************************************

  NAME
    CsrWifiSmeWpsDeviceCategory

  DESCRIPTION
    Wps Primary Device Types

 VALUES
    CSR_WIFI_SME_WPS_CATEGORY_UNSPECIFIED
                   - Unspecified.
    CSR_WIFI_SME_WPS_CATEGORY_COMPUTER
                   - Computer.
    CSR_WIFI_SME_WPS_CATEGORY_INPUT_DEV
                   - Input device
    CSR_WIFI_SME_WPS_CATEGORY_PRT_SCAN_FX_CP
                   - Printer Scanner Fax Copier etc
    CSR_WIFI_SME_WPS_CATEGORY_CAMERA
                   - Camera
    CSR_WIFI_SME_WPS_CATEGORY_STORAGE
                   - Storage
    CSR_WIFI_SME_WPS_CATEGORY_NET_INFRA
                   - Net Infra
    CSR_WIFI_SME_WPS_CATEGORY_DISPLAY
                   - Display
    CSR_WIFI_SME_WPS_CATEGORY_MULTIMEDIA
                   - Multimedia
    CSR_WIFI_SME_WPS_CATEGORY_GAMING
                   - Gaming.
    CSR_WIFI_SME_WPS_CATEGORY_TELEPHONE
                   - Telephone.
    CSR_WIFI_SME_WPS_CATEGORY_AUDIO
                   - Audio
    CSR_WIFI_SME_WPS_CATEOARY_OTHERS
                   - Others.

*******************************************************************************/
typedef u8 CsrWifiSmeWpsDeviceCategory;
#define CSR_WIFI_SME_WPS_CATEGORY_UNSPECIFIED      ((CsrWifiSmeWpsDeviceCategory) 0x00)
#define CSR_WIFI_SME_WPS_CATEGORY_COMPUTER         ((CsrWifiSmeWpsDeviceCategory) 0x01)
#define CSR_WIFI_SME_WPS_CATEGORY_INPUT_DEV        ((CsrWifiSmeWpsDeviceCategory) 0x02)
#define CSR_WIFI_SME_WPS_CATEGORY_PRT_SCAN_FX_CP   ((CsrWifiSmeWpsDeviceCategory) 0x03)
#define CSR_WIFI_SME_WPS_CATEGORY_CAMERA           ((CsrWifiSmeWpsDeviceCategory) 0x04)
#define CSR_WIFI_SME_WPS_CATEGORY_STORAGE          ((CsrWifiSmeWpsDeviceCategory) 0x05)
#define CSR_WIFI_SME_WPS_CATEGORY_NET_INFRA        ((CsrWifiSmeWpsDeviceCategory) 0x06)
#define CSR_WIFI_SME_WPS_CATEGORY_DISPLAY          ((CsrWifiSmeWpsDeviceCategory) 0x07)
#define CSR_WIFI_SME_WPS_CATEGORY_MULTIMEDIA       ((CsrWifiSmeWpsDeviceCategory) 0x08)
#define CSR_WIFI_SME_WPS_CATEGORY_GAMING           ((CsrWifiSmeWpsDeviceCategory) 0x09)
#define CSR_WIFI_SME_WPS_CATEGORY_TELEPHONE        ((CsrWifiSmeWpsDeviceCategory) 0x0A)
#define CSR_WIFI_SME_WPS_CATEGORY_AUDIO            ((CsrWifiSmeWpsDeviceCategory) 0x0B)
#define CSR_WIFI_SME_WPS_CATEOARY_OTHERS           ((CsrWifiSmeWpsDeviceCategory) 0xFF)

/*******************************************************************************

  NAME
    CsrWifiSmeWpsDeviceSubCategory

  DESCRIPTION
    Wps Secondary Device Types

 VALUES
    CSR_WIFI_SME_WPS_SUB_CATEGORY_UNSPECIFIED
                   - Unspecied
    CSR_WIFI_SME_WPS_STORAGE_SUB_CATEGORY_NAS
                   - Network Associated Storage
    CSR_WIFI_SME_WPS_PSFC_SUB_CATEGORY_PRNTR
                   - Printer or print server
    CSR_WIFI_SME_WPS_TELEPHONE_SUB_CATEGORY_WM
                   - Windows mobile
    CSR_WIFI_SME_WPS_AUDIO_SUB_CATEGORY_TUNER
                   - Audio tuner/receiver
    CSR_WIFI_SME_WPS_CAMERA_SUB_CATEGORY_DIG_STL
                   - Digital still camera
    CSR_WIFI_SME_WPS_NET_INFRA_SUB_CATEGORY_AP
                   - Access Point
    CSR_WIFI_SME_WPS_DISPLAY_SUB_CATEGORY_TV
                   - TV.
    CSR_WIFI_SME_WPS_MM_SUB_CATEGORY_DAR
                   - DAR.
    CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_KEYBD
                   - Keyboard.
    CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_PC
                   - PC.
    CSR_WIFI_SME_WPS_GAMING_SUB_CATEGORY_XBOX
                   - Xbox.
    CSR_WIFI_SME_WPS_PSFC_SUB_CATEGORY_SCNR
                   - Scanner
    CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_SERVER
                   - Server
    CSR_WIFI_SME_WPS_NET_INFRA_SUB_CATEGORY_ROUTER
                   - Router
    CSR_WIFI_SME_WPS_MM_SUB_CATEGORY_PVR
                   - PVR
    CSR_WIFI_SME_WPS_AUDIO_SUB_CATEGORY_SPEAKER
                   - Speaker
    CSR_WIFI_SME_WPS_TELEPHONE_SUB_CATEGORY_FP_SM
                   - Feature phone - Single mode
    CSR_WIFI_SME_WPS_CAMERA_SUB_CATEGORY_VIDEO
                   - Video camera
    CSR_WIFI_SME_WPS_DISPLAY_SUB_CATEGORY_PIC_FRM
                   - Picture frame
    CSR_WIFI_SME_WPS_GAMING_SUB_CATEGORY_XBOX_360
                   - Xbox360
    CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_MOUSE
                   - Mouse
    CSR_WIFI_SME_WPS_NET_INFRA_SUB_CATEGORY_SWITCH
                   - Switch
    CSR_WIFI_SME_WPS_AUDIO_SUB_CATEGORY_PMP
                   - Portable music player
    CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_JOYSTK
                   - Joy stick
    CSR_WIFI_SME_WPS_GAMING_SUB_CATEGORY_PLAY_STN
                   - Play-station
    CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_MED_CENT
                   - Media Center
    CSR_WIFI_SME_WPS_MM_SUB_CATEGORY_MCX
                   - MCX
    CSR_WIFI_SME_WPS_TELEPHONE_SUB_CATEGORY_FP_DM
                   - Feature phone - Dual mode
    CSR_WIFI_SME_WPS_CAMERA_SUB_CATEGORY_WEB
                   - Web camera
    CSR_WIFI_SME_WPS_PSFC_SUB_CATEGORY_FAX
                   - Fax
    CSR_WIFI_SME_WPS_DISPLAY_SUB_CATEGORY_PROJECTOR
                   - Projector
    CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_TRKBL
                   - Track Ball
    CSR_WIFI_SME_WPS_MM_SUB_CATEGORY_ST_BOX
                   - Set-Top-Box
    CSR_WIFI_SME_WPS_NET_INFRA_SUB_CATEGORY_GATEWAY
                   - GateWay.
    CSR_WIFI_SME_WPS_CAMERA_SUB_CATEGORY_SECURITY
                   - Security camera
    CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_ULTRA_MOB_PC
                   - Ultra mobile PC.
    CSR_WIFI_SME_WPS_GAMING_SUB_CATEGORY_CONSOLE
                   - Game console/Game console adapter
    CSR_WIFI_SME_WPS_PSFC_SUB_CATEGORY_CPR
                   - Copier
    CSR_WIFI_SME_WPS_AUDIO_SUB_CATEGORY_HEADSET
                   - Headset(headphones + microphone)
    CSR_WIFI_SME_WPS_TELEPHONE_SUB_CATEGORY_SP_SM
                   - Smart phone - Single mode
    CSR_WIFI_SME_WPS_DISPLAY_SUB_CATEGORY_MONITOR
                   - Monitor.
    CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_GAME_CTRL
                   - Game control.
    CSR_WIFI_SME_WPS_PSFC_SUB_CATEGORY_ALL
                   - All-in-One
    CSR_WIFI_SME_WPS_MM_SUB_CATEGORY_MEDIA
                   - Media Server/Media Adapter/Media Extender
    CSR_WIFI_SME_WPS_TELEPHONE_SUB_CATEGORY_SP_DM
                   - Smart phone - Dual mode
    CSR_WIFI_SME_WPS_GAMING_SUB_CATEGORY_PORT_DEV
                   - Portable gaming device
    CSR_WIFI_SME_WPS_AUDIO_SUB_CATEGORY_HEADPHONE
                   - Headphone.
    CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_NOTEBOOK
                   - Notebook.
    CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_REMOTE
                   - Remote control
    CSR_WIFI_SME_WPS_AUDIO_SUB_CATEGORY_MIC
                   - Microphone
    CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_DESKTOP
                   - Desktop.
    CSR_WIFI_SME_WPS_MM_SUB_CATEGORY_VP
                   - Portable video player
    CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_MID
                   - Mobile internet device
    CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_TOUCH_SCRN
                   - Touch screen
    CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_BIOMET_RDR
                   - Biometric reader
    CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_NETBOOK
                   - Netbook
    CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_BRCD_RDR
                   - Bar code reader.

*******************************************************************************/
typedef u8 CsrWifiSmeWpsDeviceSubCategory;
#define CSR_WIFI_SME_WPS_SUB_CATEGORY_UNSPECIFIED             ((CsrWifiSmeWpsDeviceSubCategory) 0x00)
#define CSR_WIFI_SME_WPS_STORAGE_SUB_CATEGORY_NAS             ((CsrWifiSmeWpsDeviceSubCategory) 0x01)
#define CSR_WIFI_SME_WPS_PSFC_SUB_CATEGORY_PRNTR              ((CsrWifiSmeWpsDeviceSubCategory) 0x01)
#define CSR_WIFI_SME_WPS_TELEPHONE_SUB_CATEGORY_WM            ((CsrWifiSmeWpsDeviceSubCategory) 0x01)
#define CSR_WIFI_SME_WPS_AUDIO_SUB_CATEGORY_TUNER             ((CsrWifiSmeWpsDeviceSubCategory) 0x01)
#define CSR_WIFI_SME_WPS_CAMERA_SUB_CATEGORY_DIG_STL          ((CsrWifiSmeWpsDeviceSubCategory) 0x01)
#define CSR_WIFI_SME_WPS_NET_INFRA_SUB_CATEGORY_AP            ((CsrWifiSmeWpsDeviceSubCategory) 0x01)
#define CSR_WIFI_SME_WPS_DISPLAY_SUB_CATEGORY_TV              ((CsrWifiSmeWpsDeviceSubCategory) 0x01)
#define CSR_WIFI_SME_WPS_MM_SUB_CATEGORY_DAR                  ((CsrWifiSmeWpsDeviceSubCategory) 0x01)
#define CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_KEYBD         ((CsrWifiSmeWpsDeviceSubCategory) 0x01)
#define CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_PC             ((CsrWifiSmeWpsDeviceSubCategory) 0x01)
#define CSR_WIFI_SME_WPS_GAMING_SUB_CATEGORY_XBOX             ((CsrWifiSmeWpsDeviceSubCategory) 0x01)
#define CSR_WIFI_SME_WPS_PSFC_SUB_CATEGORY_SCNR               ((CsrWifiSmeWpsDeviceSubCategory) 0x02)
#define CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_SERVER         ((CsrWifiSmeWpsDeviceSubCategory) 0x02)
#define CSR_WIFI_SME_WPS_NET_INFRA_SUB_CATEGORY_ROUTER        ((CsrWifiSmeWpsDeviceSubCategory) 0x02)
#define CSR_WIFI_SME_WPS_MM_SUB_CATEGORY_PVR                  ((CsrWifiSmeWpsDeviceSubCategory) 0x02)
#define CSR_WIFI_SME_WPS_AUDIO_SUB_CATEGORY_SPEAKER           ((CsrWifiSmeWpsDeviceSubCategory) 0x02)
#define CSR_WIFI_SME_WPS_TELEPHONE_SUB_CATEGORY_FP_SM         ((CsrWifiSmeWpsDeviceSubCategory) 0x02)
#define CSR_WIFI_SME_WPS_CAMERA_SUB_CATEGORY_VIDEO            ((CsrWifiSmeWpsDeviceSubCategory) 0x02)
#define CSR_WIFI_SME_WPS_DISPLAY_SUB_CATEGORY_PIC_FRM         ((CsrWifiSmeWpsDeviceSubCategory) 0x02)
#define CSR_WIFI_SME_WPS_GAMING_SUB_CATEGORY_XBOX_360         ((CsrWifiSmeWpsDeviceSubCategory) 0x02)
#define CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_MOUSE         ((CsrWifiSmeWpsDeviceSubCategory) 0x02)
#define CSR_WIFI_SME_WPS_NET_INFRA_SUB_CATEGORY_SWITCH        ((CsrWifiSmeWpsDeviceSubCategory) 0x03)
#define CSR_WIFI_SME_WPS_AUDIO_SUB_CATEGORY_PMP               ((CsrWifiSmeWpsDeviceSubCategory) 0x03)
#define CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_JOYSTK        ((CsrWifiSmeWpsDeviceSubCategory) 0x03)
#define CSR_WIFI_SME_WPS_GAMING_SUB_CATEGORY_PLAY_STN         ((CsrWifiSmeWpsDeviceSubCategory) 0x03)
#define CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_MED_CENT       ((CsrWifiSmeWpsDeviceSubCategory) 0x03)
#define CSR_WIFI_SME_WPS_MM_SUB_CATEGORY_MCX                  ((CsrWifiSmeWpsDeviceSubCategory) 0x03)
#define CSR_WIFI_SME_WPS_TELEPHONE_SUB_CATEGORY_FP_DM         ((CsrWifiSmeWpsDeviceSubCategory) 0x03)
#define CSR_WIFI_SME_WPS_CAMERA_SUB_CATEGORY_WEB              ((CsrWifiSmeWpsDeviceSubCategory) 0x03)
#define CSR_WIFI_SME_WPS_PSFC_SUB_CATEGORY_FAX                ((CsrWifiSmeWpsDeviceSubCategory) 0x03)
#define CSR_WIFI_SME_WPS_DISPLAY_SUB_CATEGORY_PROJECTOR       ((CsrWifiSmeWpsDeviceSubCategory) 0x03)
#define CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_TRKBL         ((CsrWifiSmeWpsDeviceSubCategory) 0x04)
#define CSR_WIFI_SME_WPS_MM_SUB_CATEGORY_ST_BOX               ((CsrWifiSmeWpsDeviceSubCategory) 0x04)
#define CSR_WIFI_SME_WPS_NET_INFRA_SUB_CATEGORY_GATEWAY       ((CsrWifiSmeWpsDeviceSubCategory) 0x04)
#define CSR_WIFI_SME_WPS_CAMERA_SUB_CATEGORY_SECURITY         ((CsrWifiSmeWpsDeviceSubCategory) 0x04)
#define CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_ULTRA_MOB_PC   ((CsrWifiSmeWpsDeviceSubCategory) 0x04)
#define CSR_WIFI_SME_WPS_GAMING_SUB_CATEGORY_CONSOLE          ((CsrWifiSmeWpsDeviceSubCategory) 0x04)
#define CSR_WIFI_SME_WPS_PSFC_SUB_CATEGORY_CPR                ((CsrWifiSmeWpsDeviceSubCategory) 0x04)
#define CSR_WIFI_SME_WPS_AUDIO_SUB_CATEGORY_HEADSET           ((CsrWifiSmeWpsDeviceSubCategory) 0x04)
#define CSR_WIFI_SME_WPS_TELEPHONE_SUB_CATEGORY_SP_SM         ((CsrWifiSmeWpsDeviceSubCategory) 0x04)
#define CSR_WIFI_SME_WPS_DISPLAY_SUB_CATEGORY_MONITOR         ((CsrWifiSmeWpsDeviceSubCategory) 0x04)
#define CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_GAME_CTRL     ((CsrWifiSmeWpsDeviceSubCategory) 0x05)
#define CSR_WIFI_SME_WPS_PSFC_SUB_CATEGORY_ALL                ((CsrWifiSmeWpsDeviceSubCategory) 0x05)
#define CSR_WIFI_SME_WPS_MM_SUB_CATEGORY_MEDIA                ((CsrWifiSmeWpsDeviceSubCategory) 0x05)
#define CSR_WIFI_SME_WPS_TELEPHONE_SUB_CATEGORY_SP_DM         ((CsrWifiSmeWpsDeviceSubCategory) 0x05)
#define CSR_WIFI_SME_WPS_GAMING_SUB_CATEGORY_PORT_DEV         ((CsrWifiSmeWpsDeviceSubCategory) 0x05)
#define CSR_WIFI_SME_WPS_AUDIO_SUB_CATEGORY_HEADPHONE         ((CsrWifiSmeWpsDeviceSubCategory) 0x05)
#define CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_NOTEBOOK       ((CsrWifiSmeWpsDeviceSubCategory) 0x05)
#define CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_REMOTE        ((CsrWifiSmeWpsDeviceSubCategory) 0x06)
#define CSR_WIFI_SME_WPS_AUDIO_SUB_CATEGORY_MIC               ((CsrWifiSmeWpsDeviceSubCategory) 0x06)
#define CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_DESKTOP        ((CsrWifiSmeWpsDeviceSubCategory) 0x06)
#define CSR_WIFI_SME_WPS_MM_SUB_CATEGORY_VP                   ((CsrWifiSmeWpsDeviceSubCategory) 0x06)
#define CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_MID            ((CsrWifiSmeWpsDeviceSubCategory) 0x07)
#define CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_TOUCH_SCRN    ((CsrWifiSmeWpsDeviceSubCategory) 0x07)
#define CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_BIOMET_RDR    ((CsrWifiSmeWpsDeviceSubCategory) 0x08)
#define CSR_WIFI_SME_WPS_COMPUTER_SUB_CATEGORY_NETBOOK        ((CsrWifiSmeWpsDeviceSubCategory) 0x08)
#define CSR_WIFI_SME_WPS_INPUT_DEV_SUB_CATEGORY_BRCD_RDR      ((CsrWifiSmeWpsDeviceSubCategory) 0x09)

/*******************************************************************************

  NAME
    CsrWifiSmeWpsDpid

  DESCRIPTION
    Device Password ID for the chosen config method

 VALUES
    CSR_WIFI_SME_WPS_DPID_PIN       - PIN
    CSR_WIFI_SME_WPS_DPID_USER      - User specified : Used only during P2P GO
                                      negotiation procedure
    CSR_WIFI_SME_WPS_DPID_MACHINE   - Machine specified i: Not used in this
                                      release
    CSR_WIFI_SME_WPS_DPID_REKEY     - Rekey : Not used in this release
    CSR_WIFI_SME_WPS_DPID_PBC       - PBC
    CSR_WIFI_SME_WPS_DPID_REGISTRAR - Registrar specified : Used only in P2P Go
                                      negotiation procedure

*******************************************************************************/
typedef u16 CsrWifiSmeWpsDpid;
#define CSR_WIFI_SME_WPS_DPID_PIN         ((CsrWifiSmeWpsDpid) 0x0000)
#define CSR_WIFI_SME_WPS_DPID_USER        ((CsrWifiSmeWpsDpid) 0x0001)
#define CSR_WIFI_SME_WPS_DPID_MACHINE     ((CsrWifiSmeWpsDpid) 0x0002)
#define CSR_WIFI_SME_WPS_DPID_REKEY       ((CsrWifiSmeWpsDpid) 0x0003)
#define CSR_WIFI_SME_WPS_DPID_PBC         ((CsrWifiSmeWpsDpid) 0x0004)
#define CSR_WIFI_SME_WPS_DPID_REGISTRAR   ((CsrWifiSmeWpsDpid) 0x0005)

/*******************************************************************************

  NAME
    CsrWifiSmeWpsRegistration

  DESCRIPTION

 VALUES
    CSR_WIFI_SME_WPS_REG_NOT_REQUIRED - No encryption set
    CSR_WIFI_SME_WPS_REG_REQUIRED     - No encryption set
    CSR_WIFI_SME_WPS_REG_UNKNOWN      - No encryption set

*******************************************************************************/
typedef u8 CsrWifiSmeWpsRegistration;
#define CSR_WIFI_SME_WPS_REG_NOT_REQUIRED   ((CsrWifiSmeWpsRegistration) 0x00)
#define CSR_WIFI_SME_WPS_REG_REQUIRED       ((CsrWifiSmeWpsRegistration) 0x01)
#define CSR_WIFI_SME_WPS_REG_UNKNOWN        ((CsrWifiSmeWpsRegistration) 0x02)


/*******************************************************************************

  NAME
    CsrWifiSmeAuthModeMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiSmeAuthMode

*******************************************************************************/
typedef u16 CsrWifiSmeAuthModeMask;
/*******************************************************************************

  NAME
    CsrWifiSmeEncryptionMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiSmeEncryption

*******************************************************************************/
typedef u16 CsrWifiSmeEncryptionMask;
/*******************************************************************************

  NAME
    CsrWifiSmeIndicationsMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiSmeIndications

*******************************************************************************/
typedef CsrUint32 CsrWifiSmeIndicationsMask;
/*******************************************************************************

  NAME
    CsrWifiSmeP2pCapabilityMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiSmeP2pCapability

*******************************************************************************/
typedef u8 CsrWifiSmeP2pCapabilityMask;
/*******************************************************************************

  NAME
    CsrWifiSmeP2pGroupCapabilityMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiSmeP2pGroupCapability

*******************************************************************************/
typedef u8 CsrWifiSmeP2pGroupCapabilityMask;
/*******************************************************************************

  NAME
    CsrWifiSmeTspecCtrlMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiSmeTspecCtrl

*******************************************************************************/
typedef u8 CsrWifiSmeTspecCtrlMask;
/*******************************************************************************

  NAME
    CsrWifiSmeWmmModeMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiSmeWmmMode

*******************************************************************************/
typedef u8 CsrWifiSmeWmmModeMask;
/*******************************************************************************

  NAME
    CsrWifiSmeWmmQosInfoMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiSmeWmmQosInfo

*******************************************************************************/
typedef u8 CsrWifiSmeWmmQosInfoMask;
/*******************************************************************************

  NAME
    CsrWifiSmeWpsConfigTypeMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiSmeWpsConfigType

*******************************************************************************/
typedef u16 CsrWifiSmeWpsConfigTypeMask;


/*******************************************************************************

  NAME
    CsrWifiSmeAdHocConfig

  DESCRIPTION
    Defines values to use when starting an Ad-hoc (IBSS) network.

  MEMBERS
    atimWindowTu          - ATIM window specified for IBSS
    beaconPeriodTu        - Interval between beacon packets
    joinOnlyAttempts      - Maximum number of attempts to join an ad-hoc network.
                            The default value is 1.
                            Set to 0 for infinite attempts.
    joinAttemptIntervalMs - Time between scans for joining the requested IBSS.

*******************************************************************************/
typedef struct
{
    u16 atimWindowTu;
    u16 beaconPeriodTu;
    u16 joinOnlyAttempts;
    u16 joinAttemptIntervalMs;
} CsrWifiSmeAdHocConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeAvailabilityConfig

  DESCRIPTION

  MEMBERS
    listenChannel        -
    availabilityDuration -
    avalabilityPeriod    -

*******************************************************************************/
typedef struct
{
    u8  listenChannel;
    u16 availabilityDuration;
    u16 avalabilityPeriod;
} CsrWifiSmeAvailabilityConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeCcxConfig

  DESCRIPTION
    This type is reserved for future use and should not be used.

  MEMBERS
    keepAliveTimeMs    - NOT USED
    apRoamingEnabled   - NOT USED
    measurementsMask   - NOT USED
    ccxRadioMgtEnabled - NOT USED

*******************************************************************************/
typedef struct
{
    u8 keepAliveTimeMs;
    CsrBool  apRoamingEnabled;
    u8 measurementsMask;
    CsrBool  ccxRadioMgtEnabled;
} CsrWifiSmeCcxConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeCoexConfig

  DESCRIPTION
    Parameters for the coexistence behaviour.

  MEMBERS
    coexEnableSchemeManagement     - Enables the Coexistence Management Scheme
    coexPeriodicWakeHost           - If TRUE the firmware wakes up the host
                                     periodically according to the traffic
                                     period and latency parameters; the host
                                     will then send the data to transmit only
                                     when woken up.
                                     If FALSE, the firmware does not wake up the
                                     host and the host will send the data to
                                     transmit to the firmware whenever there is
                                     data to transmit
    coexTrafficBurstyLatencyMs     - Period of awakening for the firmware used
                                     when bursty traffic is detected
    coexTrafficContinuousLatencyMs - Period of awakening for the firmware used
                                     when continuous traffic is detected
    coexObexBlackoutDurationMs     - Blackout Duration when a Obex Connection is
                                     used
    coexObexBlackoutPeriodMs       - Blackout Period when a Obex Connection is
                                     used
    coexA2dpBrBlackoutDurationMs   - Blackout Duration when a Basic Rate A2DP
                                     Connection streaming data
    coexA2dpBrBlackoutPeriodMs     - Blackout Period when a Basic Rate A2DP
                                     Connection streaming data
    coexA2dpEdrBlackoutDurationMs  - Blackout Duration when an Enhanced Data
                                     Rate A2DP Connection streaming data
    coexA2dpEdrBlackoutPeriodMs    - Blackout Period when an Enhanced Data Rate
                                     A2DP Connection streaming data
    coexPagingBlackoutDurationMs   - Blackout Duration when a BT page is active
    coexPagingBlackoutPeriodMs     - Blackout Period when a BT page is active
    coexInquiryBlackoutDurationMs  - Blackout Duration when a BT inquiry is
                                     active
    coexInquiryBlackoutPeriodMs    - Blackout Period when a BT inquiry is active

*******************************************************************************/
typedef struct
{
    CsrBool   coexEnableSchemeManagement;
    CsrBool   coexPeriodicWakeHost;
    u16 coexTrafficBurstyLatencyMs;
    u16 coexTrafficContinuousLatencyMs;
    u16 coexObexBlackoutDurationMs;
    u16 coexObexBlackoutPeriodMs;
    u16 coexA2dpBrBlackoutDurationMs;
    u16 coexA2dpBrBlackoutPeriodMs;
    u16 coexA2dpEdrBlackoutDurationMs;
    u16 coexA2dpEdrBlackoutPeriodMs;
    u16 coexPagingBlackoutDurationMs;
    u16 coexPagingBlackoutPeriodMs;
    u16 coexInquiryBlackoutDurationMs;
    u16 coexInquiryBlackoutPeriodMs;
} CsrWifiSmeCoexConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionStats

  DESCRIPTION
    Indicates the statistics of the connection.
    The dot11 fields are defined in the Annex D of the IEEE 802.11 standard.

  MEMBERS
    unifiTxDataRate
                   - The bit rate currently in use for transmissions of unicast
                     data frames; a data rate in units of 500kbit/s.
                     On an infrastructure BSS, this is the data rate used in
                     communicating with the associated access point; if there is
                     none, an error is returned.
                     On an IBSS, this is the data rate used for the last
                     transmission of a unicast data frame to any station in the
                     IBSS. If no such transmission has been made, an error is
                     returned.
    unifiRxDataRate
                   - As above for receiving data
    dot11RetryCount
                   - See IEEE 802.11 Standard
    dot11MultipleRetryCount
                   - See IEEE 802.11 Standard
    dot11AckFailureCount
                   - See IEEE 802.11 Standard
    dot11FrameDuplicateCount
                   - See IEEE 802.11 Standard
    dot11FcsErrorCount
                   - See IEEE 802.11 Standard
    dot11RtsSuccessCount
                   - See IEEE 802.11 Standard
    dot11RtsFailureCount
                   - See IEEE 802.11 Standard
    dot11FailedCount
                   - See IEEE 802.11 Standard
    dot11TransmittedFragmentCount
                   - See IEEE 802.11 Standard
    dot11TransmittedFrameCount
                   - See IEEE 802.11 Standard
    dot11WepExcludedCount
                   - See IEEE 802.11 Standard
    dot11WepIcvErrorCount
                   - See IEEE 802.11 Standard
    dot11WepUndecryptableCount
                   - See IEEE 802.11 Standard
    dot11MulticastReceivedFrameCount
                   - See IEEE 802.11 Standard
    dot11MulticastTransmittedFrameCount
                   - See IEEE 802.11 Standard
    dot11ReceivedFragmentCount
                   - See IEEE 802.11 Standard
    dot11Rsna4WayHandshakeFailures
                   - See IEEE 802.11 Standard
    dot11RsnaTkipCounterMeasuresInvoked
                   - See IEEE 802.11 Standard
    dot11RsnaStatsTkipLocalMicFailures
                   - See IEEE 802.11 Standard
    dot11RsnaStatsTkipReplays
                   - See IEEE 802.11 Standard
    dot11RsnaStatsTkipIcvErrors
                   - See IEEE 802.11 Standard
    dot11RsnaStatsCcmpReplays
                   - See IEEE 802.11 Standard
    dot11RsnaStatsCcmpDecryptErrors
                   - See IEEE 802.11 Standard

*******************************************************************************/
typedef struct
{
    u8  unifiTxDataRate;
    u8  unifiRxDataRate;
    CsrUint32 dot11RetryCount;
    CsrUint32 dot11MultipleRetryCount;
    CsrUint32 dot11AckFailureCount;
    CsrUint32 dot11FrameDuplicateCount;
    CsrUint32 dot11FcsErrorCount;
    CsrUint32 dot11RtsSuccessCount;
    CsrUint32 dot11RtsFailureCount;
    CsrUint32 dot11FailedCount;
    CsrUint32 dot11TransmittedFragmentCount;
    CsrUint32 dot11TransmittedFrameCount;
    CsrUint32 dot11WepExcludedCount;
    CsrUint32 dot11WepIcvErrorCount;
    CsrUint32 dot11WepUndecryptableCount;
    CsrUint32 dot11MulticastReceivedFrameCount;
    CsrUint32 dot11MulticastTransmittedFrameCount;
    CsrUint32 dot11ReceivedFragmentCount;
    CsrUint32 dot11Rsna4WayHandshakeFailures;
    CsrUint32 dot11RsnaTkipCounterMeasuresInvoked;
    CsrUint32 dot11RsnaStatsTkipLocalMicFailures;
    CsrUint32 dot11RsnaStatsTkipReplays;
    CsrUint32 dot11RsnaStatsTkipIcvErrors;
    CsrUint32 dot11RsnaStatsCcmpReplays;
    CsrUint32 dot11RsnaStatsCcmpDecryptErrors;
} CsrWifiSmeConnectionStats;

/*******************************************************************************

  NAME
    CsrWifiSmeDataBlock

  DESCRIPTION
    Holds a generic data block to be passed through the interface

  MEMBERS
    length - Length of the data block
    data   - Points to the first byte of the data block

*******************************************************************************/
typedef struct
{
    u16 length;
    u8 *data;
} CsrWifiSmeDataBlock;

/*******************************************************************************

  NAME
    CsrWifiSmeEmpty

  DESCRIPTION
    Empty Structure to indicate that no parameters are available.

  MEMBERS
    empty  - Only element of the empty structure (always set to 0).

*******************************************************************************/
typedef struct
{
    u8 empty;
} CsrWifiSmeEmpty;

/*******************************************************************************

  NAME
    CsrWifiSmeLinkQuality

  DESCRIPTION
    Indicates the quality of the link

  MEMBERS
    unifiRssi - Indicates the received signal strength indication of the link in
                dBm
    unifiSnr  - Indicates the signal to noise ratio of the link in dB

*******************************************************************************/
typedef struct
{
    CsrInt16 unifiRssi;
    CsrInt16 unifiSnr;
} CsrWifiSmeLinkQuality;

/*******************************************************************************

  NAME
    CsrWifiSmeMibConfig

  DESCRIPTION
    Allows low level configuration in the chip.

  MEMBERS
    unifiFixMaxTxDataRate       - This attribute is used in combination with
                                  unifiFixTxDataRate. If it is FALSE, then
                                  unifiFixTxDataRate specifies a specific data
                                  rate to use. If it is TRUE, unifiFixTxDataRate
                                  instead specifies a maximum data rate.
    unifiFixTxDataRate          - Transmit rate for unicast data.
                                  See MIB description for more details
    dot11RtsThreshold           - See IEEE 802.11 Standard
    dot11FragmentationThreshold - See IEEE 802.11 Standard
    dot11CurrentTxPowerLevel    - See IEEE 802.11 Standard

*******************************************************************************/
typedef struct
{
    CsrBool   unifiFixMaxTxDataRate;
    u8  unifiFixTxDataRate;
    u16 dot11RtsThreshold;
    u16 dot11FragmentationThreshold;
    u16 dot11CurrentTxPowerLevel;
} CsrWifiSmeMibConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeP2pProfileIdentity

  DESCRIPTION
    Details to be filled in

  MEMBERS
    listenChannel        -
    availabilityDuration -
    avalabilityPeriod    -

*******************************************************************************/
typedef struct
{
    u8  listenChannel;
    u16 availabilityDuration;
    u16 avalabilityPeriod;
} CsrWifiSmeP2pProfileIdentity;

/*******************************************************************************

  NAME
    CsrWifiSmePmkid

  DESCRIPTION
    Defines a PMKID association with BSS

  MEMBERS
    bssid  - BSS identifier
    pmkid  - PMKID

*******************************************************************************/
typedef struct
{
    CsrWifiMacAddress bssid;
    u8          pmkid[16];
} CsrWifiSmePmkid;

/*******************************************************************************

  NAME
    CsrWifiSmePmkidCandidate

  DESCRIPTION
    Information for a PMKID candidate

  MEMBERS
    bssid          - BSS identifier
    preAuthAllowed - Indicates whether preauthentication is allowed

*******************************************************************************/
typedef struct
{
    CsrWifiMacAddress bssid;
    CsrBool           preAuthAllowed;
} CsrWifiSmePmkidCandidate;

/*******************************************************************************

  NAME
    CsrWifiSmePmkidList

  DESCRIPTION
    NOT USED
    Used in the Sync access API

  MEMBERS
    pmkidsCount - Number of PMKIDs in the list
    pmkids      - Points to the first PMKID in the list

*******************************************************************************/
typedef struct
{
    u8         pmkidsCount;
    CsrWifiSmePmkid *pmkids;
} CsrWifiSmePmkidList;

/*******************************************************************************

  NAME
    CsrWifiSmeRegulatoryDomainInfo

  DESCRIPTION
    Regulatory domain options.

  MEMBERS
    dot11MultiDomainCapabilityImplemented
                   - TRUE is the multi domain capability is implemented
    dot11MultiDomainCapabilityEnabled
                   - TRUE is the multi domain capability is enabled
    currentRegulatoryDomain
                   - Current regulatory domain
    currentCountryCode
                   - Current country code as defined by the IEEE 802.11
                     standards

*******************************************************************************/
typedef struct
{
    CsrBool                    dot11MultiDomainCapabilityImplemented;
    CsrBool                    dot11MultiDomainCapabilityEnabled;
    CsrWifiSmeRegulatoryDomain currentRegulatoryDomain;
    u8                   currentCountryCode[2];
} CsrWifiSmeRegulatoryDomainInfo;

/*******************************************************************************

  NAME
    CsrWifiSmeRoamingBandData

  DESCRIPTION
    Thresholds to define one usability level category for the received signal

  MEMBERS
    rssiHighThreshold - Received Signal Strength Indication upper bound in dBm
                        for the usability level
    rssiLowThreshold  - Received Signal Strength Indication lower bound in dBm
                        for the usability level
    snrHighThreshold  - Signal to Noise Ratio upper bound in dB for the
                        usability level
    snrLowThreshold   - Signal to Noise Ratio lower bound in dB for the
                        usability level

*******************************************************************************/
typedef struct
{
    CsrInt16 rssiHighThreshold;
    CsrInt16 rssiLowThreshold;
    CsrInt16 snrHighThreshold;
    CsrInt16 snrLowThreshold;
} CsrWifiSmeRoamingBandData;

/*******************************************************************************

  NAME
    CsrWifiSmeScanConfigData

  DESCRIPTION
    Configures the scanning behaviour of the driver and firmware

  MEMBERS
    intervalSeconds         - All the channels will be scanned once in this time
                              interval.
                              If connected, the channel scans are spread across
                              the interval.
                              If disconnected, all the channels will be scanned
                              together
    validitySeconds         - How long the scan result are cached
    minActiveChannelTimeTu  - Minimum time of listening on a channel being
                              actively scanned before leaving if no probe
                              responses or beacon frames have been received
    maxActiveChannelTimeTu  - Maximum time of listening on a channel being
                              actively scanned
    minPassiveChannelTimeTu - Minimum time of listening on a channel being
                              passive scanned before leaving if no beacon frames
                              have been received
    maxPassiveChannelTimeTu - Maximum time of listening on a channel being
                              passively scanned

*******************************************************************************/
typedef struct
{
    u16 intervalSeconds;
    u16 validitySeconds;
    u16 minActiveChannelTimeTu;
    u16 maxActiveChannelTimeTu;
    u16 minPassiveChannelTimeTu;
    u16 maxPassiveChannelTimeTu;
} CsrWifiSmeScanConfigData;

/*******************************************************************************

  NAME
    CsrWifiSmeTsfTime

  DESCRIPTION
    Time stamp representation

  MEMBERS
    data   - TSF Bytes

*******************************************************************************/
typedef struct
{
    u8 data[8];
} CsrWifiSmeTsfTime;

/*******************************************************************************

  NAME
    CsrWifiSmeVersions

  DESCRIPTION
    Reports version information for the chip, the firmware and the driver and
    the SME.

  MEMBERS
    chipId        - Chip ID
    chipVersion   - Chip version ID
    firmwareBuild - Firmware Rom build number
    firmwarePatch - Firmware Patch build number (if applicable)
    firmwareHip   - Firmware HIP protocol version number
    routerBuild   - Router build number
    routerHip     - Router HIP protocol version number
    smeBuild      - SME build number
    smeHip        - SME HIP protocol version number

*******************************************************************************/
typedef struct
{
    CsrUint32      chipId;
    CsrUint32      chipVersion;
    CsrUint32      firmwareBuild;
    CsrUint32      firmwarePatch;
    CsrUint32      firmwareHip;
    CsrCharString *routerBuild;
    CsrUint32      routerHip;
    CsrCharString *smeBuild;
    CsrUint32      smeHip;
} CsrWifiSmeVersions;

/*******************************************************************************

  NAME
    CsrWifiSmeWmmAcParams

  DESCRIPTION
    Structure holding WMM AC params data.

  MEMBERS
    cwMin                     - Exponent for the calculation of CWmin. Range: 0
                                to 15
    cwMax                     - Exponent for the calculation of CWmax. Range: 0
                                to15
    aifs                      - Arbitration Inter Frame Spacing in terms of
                                number of timeslots. Range 2 to 15
    txopLimit                 - TXOP Limit in the units of 32 microseconds
    admissionControlMandatory - Indicates whether the admission control is
                                mandatory or not. Current release does not
                                support admission control , hence shall be set
                                to FALSE.

*******************************************************************************/
typedef struct
{
    u8  cwMin;
    u8  cwMax;
    u8  aifs;
    u16 txopLimit;
    CsrBool   admissionControlMandatory;
} CsrWifiSmeWmmAcParams;

/*******************************************************************************

  NAME
    CsrWifiSmeWpsDeviceType

  DESCRIPTION
    Structure holding AP WPS device type data.

  MEMBERS
    deviceDetails - category , sub category etc

*******************************************************************************/
typedef struct
{
    u8 deviceDetails[8];
} CsrWifiSmeWpsDeviceType;

/*******************************************************************************

  NAME
    CsrWifiSmeWpsDeviceTypeCommon

  DESCRIPTION

  MEMBERS
    spportWps  -
    deviceType -

*******************************************************************************/
typedef struct
{
    CsrBool  spportWps;
    u8 deviceType;
} CsrWifiSmeWpsDeviceTypeCommon;

/*******************************************************************************

  NAME
    CsrWifiSmeWpsInfo

  DESCRIPTION

  MEMBERS
    version         -
    configMethods   -
    devicePassworId -

*******************************************************************************/
typedef struct
{
    u16 version;
    u16 configMethods;
    u16 devicePassworId;
} CsrWifiSmeWpsInfo;

/*******************************************************************************

  NAME
    CsrWifiSmeCloakedSsidConfig

  DESCRIPTION
    List of cloaked SSIDs .

  MEMBERS
    cloakedSsidsCount - Number of cloaked SSID
    cloakedSsids      - Points to the first byte of the first SSID provided

*******************************************************************************/
typedef struct
{
    u8     cloakedSsidsCount;
    CsrWifiSsid *cloakedSsids;
} CsrWifiSmeCloakedSsidConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeCoexInfo

  DESCRIPTION
    Information and state related to coexistence.

  MEMBERS
    hasTrafficData            - TRUE if any Wi-Fi traffic is detected
    currentTrafficType        - Current type of traffic
    currentPeriodMs           - Period of the traffic as detected by the traffic
                                analysis.
                                If the traffic is not periodic, it is set to 0.
    currentPowerSave          - Current power save level
    currentCoexPeriodMs       - Period of awakening for the firmware used when
                                periodic traffic is detected.
                                If the traffic is not periodic, it is set to 0.
    currentCoexLatencyMs      - Period of awakening for the firmware used when
                                non-periodic traffic is detected
    hasBtDevice               - TRUE if there is a Bluetooth device connected
    currentBlackoutDurationUs - Current blackout duration for protecting
                                Bluetooth
    currentBlackoutPeriodUs   - Current blackout period
    currentCoexScheme         - Defines the scheme for the coexistence
                                signalling

*******************************************************************************/
typedef struct
{
    CsrBool                  hasTrafficData;
    CsrWifiSmeTrafficType    currentTrafficType;
    u16                currentPeriodMs;
    CsrWifiSmePowerSaveLevel currentPowerSave;
    u16                currentCoexPeriodMs;
    u16                currentCoexLatencyMs;
    CsrBool                  hasBtDevice;
    CsrUint32                currentBlackoutDurationUs;
    CsrUint32                currentBlackoutPeriodUs;
    CsrWifiSmeCoexScheme     currentCoexScheme;
} CsrWifiSmeCoexInfo;

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionConfig

  DESCRIPTION
    Specifies the parameters that the SME should use in selecting a network.

  MEMBERS
    ssid
                   - Service Set identifier
    bssid
                   - BSS identifier
    bssType
                   - Indicates the type of BSS
    ifIndex
                   - Indicates the radio interface
    privacyMode
                   - Specifies whether the privacy mode is enabled or disabled.
    authModeMask
                   - Sets the authentication options that the SME can use while
                     associating to the AP
                     Set mask with values from CsrWifiSmeAuthMode
    encryptionModeMask
                   - Sets the encryption options that the SME can use while
                     associating to the AP
                     Set mask with values from CsrWifiSmeEncryption
    mlmeAssociateReqInformationElementsLength
                   - Length in bytes of information elements to be sent in the
                     Association Request.
    mlmeAssociateReqInformationElements
                   - Points to the first byte of the information elements, if
                     any.
    wmmQosInfo
                   - This parameter allows the driver's WMM behaviour to be
                     configured.
                     To enable support for WMM, use
                     CSR_WIFI_SME_SME_CONFIG_SET_REQ with the
                     CSR_WIFI_SME_WMM_MODE_AC_ENABLED bit set in wmmModeMask
                     field in smeConfig parameter.
                     Set mask with values from CsrWifiSmeWmmQosInfo
    adhocJoinOnly
                   - This parameter is relevant only if bssType is NOT set to
                     CSR_WIFI_SME_BSS_TYPE_INFRASTRUCTURE:
                     if TRUE the SME will only try to join an ad-hoc network if
                     there is one already established;
                     if FALSE the SME will try to join an ad-hoc network if
                     there is one already established or it will try to
                     establish a new one
    adhocChannel
                   - This parameter is relevant only if bssType is NOT set to
                     CSR_WIFI_SME_BSS_TYPE_INFRASTRUCTURE:
                     it indicates the channel to use joining an ad hoc network.
                     Setting this to 0 causes the SME to select a channel from
                     those permitted in the regulatory domain.

*******************************************************************************/
typedef struct
{
    CsrWifiSsid                ssid;
    CsrWifiMacAddress          bssid;
    CsrWifiSmeBssType          bssType;
    CsrWifiSmeRadioIF          ifIndex;
    CsrWifiSme80211PrivacyMode privacyMode;
    CsrWifiSmeAuthModeMask     authModeMask;
    CsrWifiSmeEncryptionMask   encryptionModeMask;
    u16                  mlmeAssociateReqInformationElementsLength;
    u8                  *mlmeAssociateReqInformationElements;
    CsrWifiSmeWmmQosInfoMask   wmmQosInfo;
    CsrBool                    adhocJoinOnly;
    u8                   adhocChannel;
} CsrWifiSmeConnectionConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionInfo

  DESCRIPTION
    Parameters that the SME should use in selecting a network

  MEMBERS
    ssid                        - Service set identifier
    bssid                       - BSS identifier
    networkType80211            - Physical layer used for the connection
    channelNumber               - Channel number
    channelFrequency            - Channel frequency
    authMode                    - Authentication mode used for the connection
    pairwiseCipher              - Encryption type for peer to peer communication
    groupCipher                 - Encryption type for broadcast and multicast
                                  communication
    ifIndex                     - Indicates the radio interface
    atimWindowTu                - ATIM window specified for IBSS
    beaconPeriodTu              - Interval between beacon packets
    reassociation               - Indicates whether a reassociation occurred
    beaconFrameLength           - Indicates the number of bytes of the beacon
                                  frame
    beaconFrame                 - Points at the first byte of the beacon frame
    associationReqFrameLength   - Indicates the number of bytes of the
                                  association request frame
    associationReqFrame         - Points at the first byte of the association
                                  request frame
    associationRspFrameLength   - Indicates the number of bytes of the
                                  association response frame
    associationRspFrame         - Points at the first byte of the association
                                  response frame
    assocScanInfoElementsLength - Indicates the number of bytes in the buffer
                                  pointed by assocScanInfoElements
    assocScanInfoElements       - Pointer to the buffer containing the
                                  information elements of the probe response
                                  received after the probe requests sent before
                                  attempting to authenticate to the network
    assocReqCapabilities        - Reports the content of the Capability
                                  information element as specified in the
                                  association request.
    assocReqListenIntervalTu    - Listen Interval specified in the association
                                  request
    assocReqApAddress           - AP address to which the association requests
                                  has been sent
    assocReqInfoElementsLength  - Indicates the number of bytes of the
                                  association request information elements
    assocReqInfoElements        - Points at the first byte of the association
                                  request information elements
    assocRspResult              - Result reported in the association response
    assocRspCapabilityInfo      - Reports the content of the Capability
                                  information element as received in the
                                  association response.
    assocRspAssociationId       - Reports the association ID received in the
                                  association response.
    assocRspInfoElementsLength  - Indicates the number of bytes of the
                                  association response information elements
    assocRspInfoElements        - Points at the first byte of the association
                                  response information elements

*******************************************************************************/
typedef struct
{
    CsrWifiSsid                ssid;
    CsrWifiMacAddress          bssid;
    CsrWifiSme80211NetworkType networkType80211;
    u8                   channelNumber;
    u16                  channelFrequency;
    CsrWifiSmeAuthMode         authMode;
    CsrWifiSmeEncryption       pairwiseCipher;
    CsrWifiSmeEncryption       groupCipher;
    CsrWifiSmeRadioIF          ifIndex;
    u16                  atimWindowTu;
    u16                  beaconPeriodTu;
    CsrBool                    reassociation;
    u16                  beaconFrameLength;
    u8                  *beaconFrame;
    u16                  associationReqFrameLength;
    u8                  *associationReqFrame;
    u16                  associationRspFrameLength;
    u8                  *associationRspFrame;
    u16                  assocScanInfoElementsLength;
    u8                  *assocScanInfoElements;
    u16                  assocReqCapabilities;
    u16                  assocReqListenIntervalTu;
    CsrWifiMacAddress          assocReqApAddress;
    u16                  assocReqInfoElementsLength;
    u8                  *assocReqInfoElements;
    CsrWifiSmeIEEE80211Result  assocRspResult;
    u16                  assocRspCapabilityInfo;
    u16                  assocRspAssociationId;
    u16                  assocRspInfoElementsLength;
    u8                  *assocRspInfoElements;
} CsrWifiSmeConnectionInfo;

/*******************************************************************************

  NAME
    CsrWifiSmeDeviceConfig

  DESCRIPTION
    General configuration options in the SME

  MEMBERS
    trustLevel              - Level of trust of the information coming from the
                              network
    countryCode             - Country code as specified by IEEE 802.11 standard
    firmwareDriverInterface - Specifies the type of communication between Host
                              and Firmware
    enableStrictDraftN      - If TRUE TKIP is disallowed when connecting to
                              802.11n enabled access points

*******************************************************************************/
typedef struct
{
    CsrWifiSme80211dTrustLevel        trustLevel;
    u8                          countryCode[2];
    CsrWifiSmeFirmwareDriverInterface firmwareDriverInterface;
    CsrBool                           enableStrictDraftN;
} CsrWifiSmeDeviceConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeDeviceInfo

  DESCRIPTION
    P2P Information for a P2P Device

  MEMBERS
    deviceAddress            - Device Address of the P2P device
    configMethods            - Supported WPS configuration methods.
    p2PDeviceCap             - P2P device capabilities
    primDeviceType           - Primary WPS device type
    secondaryDeviceTypeCount - Number of secondary device types
    secDeviceType            - list of secondary WPS device types
    deviceName               - Device name without up to 32 characters'\0'.
    deviceNameLength         - Number of characters of the device name

*******************************************************************************/
typedef struct
{
    CsrWifiMacAddress           deviceAddress;
    CsrWifiSmeWpsConfigTypeMask configMethods;
    CsrWifiSmeP2pCapabilityMask p2PDeviceCap;
    CsrWifiSmeWpsDeviceType     primDeviceType;
    u8                    secondaryDeviceTypeCount;
    CsrWifiSmeWpsDeviceType    *secDeviceType;
    u8                    deviceName[32];
    u8                    deviceNameLength;
} CsrWifiSmeDeviceInfo;

/*******************************************************************************

  NAME
    CsrWifiSmeDeviceInfoCommon

  DESCRIPTION
    Structure holding device information.

  MEMBERS
    p2pDeviceAddress          -
    primaryDeviceType         -
    secondaryDeviceTypesCount -
    secondaryDeviceTypes      -
    deviceNameLength          -
    deviceName                -

*******************************************************************************/
typedef struct
{
    CsrWifiMacAddress             p2pDeviceAddress;
    CsrWifiSmeWpsDeviceTypeCommon primaryDeviceType;
    u8                      secondaryDeviceTypesCount;
    u8                      secondaryDeviceTypes[10];
    u8                      deviceNameLength;
    u8                      deviceName[32];
} CsrWifiSmeDeviceInfoCommon;

/*******************************************************************************

  NAME
    CsrWifiSmeHostConfig

  DESCRIPTION
    Defines the host power state (for example, on mains power, on battery
    power etc) and the periodicity of the traffic data.

  MEMBERS
    powerMode               - The wireless manager application should use the
                              powerMode parameter to inform the SME of the host
                              power state.
    applicationDataPeriodMs - The applicationDataPeriodMs parameter allows a
                              wireless manager application to inform the SME
                              that an application is running that generates
                              periodic network traffic and the period of the
                              traffic.
                              An example of such an application is a VoIP client.
                              The wireless manager application should set
                              applicationDataPeriodMs to the period in
                              milliseconds between data packets or zero if no
                              periodic application is running.
                              Voip etc 0 = No Periodic Data

*******************************************************************************/
typedef struct
{
    CsrWifiSmeHostPowerMode powerMode;
    u16               applicationDataPeriodMs;
} CsrWifiSmeHostConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeKey

  DESCRIPTION
    Information for a key to be used for encryption

  MEMBERS
    keyType       - Specifies whether the key is a pairwise or group key; it
                    should be set to CSR_WIFI_SME_GROUP_KEY or
                    CSR_WIFI_SME_PAIRWISE_KEY, as required.
    keyIndex      - Specifies which WEP key (0-3) to set; it should be set to 0
                    for a WPA/WPA2 pairwise key and non-zero for a WPA/WPA2
                    group key.
    wepTxKey      - If wepTxKey is TRUE, and the key is a WEP key, the key will
                    be selected for encrypting transmitted packets.
                    To select a previously defined key as the transmit
                    encryption key, set keyIndex to the required key, wepTxKey
                    to TRUE and the keyLength to 0.
    keyRsc        - Key Receive Sequence Counter
    authenticator - If TRUE the WMA will act as authenticator.
                    CURRENTLY NOT SUPPORTED
    address       - BSS identifier of the AP
    keyLength     - Length of the key in bytes
    key           - Points to the first byte of the key

*******************************************************************************/
typedef struct
{
    CsrWifiSmeKeyType keyType;
    u8          keyIndex;
    CsrBool           wepTxKey;
    u16         keyRsc[8];
    CsrBool           authenticator;
    CsrWifiMacAddress address;
    u8          keyLength;
    u8          key[32];
} CsrWifiSmeKey;

/*******************************************************************************

  NAME
    CsrWifiSmeP2pClientInfoType

  DESCRIPTION
    P2P Information for a P2P Client

  MEMBERS
    p2PClientInterfaceAddress - MAC address of the P2P Client
    clientDeviceInfo          - Device Information

*******************************************************************************/
typedef struct
{
    CsrWifiMacAddress    p2PClientInterfaceAddress;
    CsrWifiSmeDeviceInfo clientDeviceInfo;
} CsrWifiSmeP2pClientInfoType;

/*******************************************************************************

  NAME
    CsrWifiSmeP2pGroupInfo

  DESCRIPTION
    P2P Information for a P2P Group

  MEMBERS
    groupCapability    - P2P group capabilities
    p2pDeviceAddress   - Device Address of the GO
    p2pClientInfoCount - Number of P2P Clients that belong to the group.
    p2PClientInfo      - Pointer to the list containing client information for
                         each client in the group

*******************************************************************************/
typedef struct
{
    CsrWifiSmeP2pGroupCapabilityMask groupCapability;
    CsrWifiMacAddress                p2pDeviceAddress;
    u8                         p2pClientInfoCount;
    CsrWifiSmeP2pClientInfoType     *p2PClientInfo;
} CsrWifiSmeP2pGroupInfo;

/*******************************************************************************

  NAME
    CsrWifiSmePowerConfig

  DESCRIPTION
    Configures the power-save behaviour of the driver and firmware.

  MEMBERS
    powerSaveLevel         - Power Save Level option
    listenIntervalTu       - Interval for waking to receive beacon frames
    rxDtims                - If TRUE, wake for DTIM every beacon period, to
                             allow the reception broadcast packets
    d3AutoScanMode         - Defines whether the autonomous scanning will be
                             turned off or will stay on during a D3 suspended
                             period
    clientTrafficWindow    - Deprecated
    opportunisticPowerSave - Deprecated
    noticeOfAbsence        - Deprecated

*******************************************************************************/
typedef struct
{
    CsrWifiSmePowerSaveLevel powerSaveLevel;
    u16                listenIntervalTu;
    CsrBool                  rxDtims;
    CsrWifiSmeD3AutoScanMode d3AutoScanMode;
    u8                 clientTrafficWindow;
    CsrBool                  opportunisticPowerSave;
    CsrBool                  noticeOfAbsence;
} CsrWifiSmePowerConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeRoamingConfig

  DESCRIPTION
    Configures the roaming behaviour of the driver and firmware

  MEMBERS
    roamingBands             - Defines the thresholds to determine the usability
                               level of the current connection.
                               roamingBands is indexed by the first 3 entries of
                               the CsrWifiSmeBasicUsability enum
    disableSmoothRoaming     - Disable the RSSI/SNR triggers from the Firmware
                               that the SME uses to detect the quality of the
                               connection.
                               This implicitly disables disableRoamScans
    disableRoamScans         - Disables the scanning for the roaming operation
    reconnectLimit           - Maximum number of times SME may reconnect in the
                               given interval
    reconnectLimitIntervalMs - Interval for maximum number of times SME may
                               reconnect to the same Access Point
    roamScanCfg              - Scanning behaviour for the specifically aimed at
                               improving roaming performance.
                               roamScanCfg is indexed by the first 3 entries of
                               the CsrWifiSmeBasicUsability enum

*******************************************************************************/
typedef struct
{
    CsrWifiSmeRoamingBandData roamingBands[3];
    CsrBool                   disableSmoothRoaming;
    CsrBool                   disableRoamScans;
    u8                  reconnectLimit;
    u16                 reconnectLimitIntervalMs;
    CsrWifiSmeScanConfigData  roamScanCfg[3];
} CsrWifiSmeRoamingConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeScanConfig

  DESCRIPTION
    Parameters for the autonomous scanning behaviour of the system

  MEMBERS
    scanCfg                 - Scan configuration data.
                              Indexed by the CsrWifiSmeBasicUsability enum
    disableAutonomousScans  - Enables or disables the autonomous scan
    maxResults              - Maximum number of results to be cached in the SME
    highRssiThreshold       - High received signal strength indication threshold
                              in dBm for an AP above which the system will
                              report scan indications
    lowRssiThreshold        - Low received signal strength indication threshold
                              in dBm for an AP below which the system will
                              report scan indications
    deltaRssiThreshold      - Minimum difference for received signal strength
                              indication in dBm for an AP which trigger a scan
                              indication to be sent.
    highSnrThreshold        - High Signal to Noise Ratio threshold in dB for an
                              AP above which the system will report scan
                              indications
    lowSnrThreshold         - Low Signal to Noise Ratio threshold in dB for an
                              AP below which the system will report scan
                              indications
    deltaSnrThreshold       - Minimum difference for Signal to Noise Ratio in dB
                              for an AP which trigger a scan indication to be
                              sent.
    passiveChannelListCount - Number of channels to be scanned passively.
    passiveChannelList      - Points to the first channel to be scanned
                              passively , if any.

*******************************************************************************/
typedef struct
{
    CsrWifiSmeScanConfigData scanCfg[4];
    CsrBool                  disableAutonomousScans;
    u16                maxResults;
    s8                  highRssiThreshold;
    s8                  lowRssiThreshold;
    s8                  deltaRssiThreshold;
    s8                  highSnrThreshold;
    s8                  lowSnrThreshold;
    s8                  deltaSnrThreshold;
    u16                passiveChannelListCount;
    u8                *passiveChannelList;
} CsrWifiSmeScanConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeScanResult

  DESCRIPTION
    This structure defines the scan result for each BSS found

  MEMBERS
    ssid                         - Service set identifier
    bssid                        - BSS identifier
    rssi                         - Received signal strength indication in dBm
    snr                          - Signal to noise ratio in dB
    ifIndex                      - Indicates the radio interface
    beaconPeriodTu               - Interval between beacon frames
    timeStamp                    - Timestamp in the BSS
    localTime                    - Timestamp in the Access Point
    channelFrequency             - Channel frequency
    capabilityInformation        - Capabilities of the BSS.
    channelNumber                - Channel number
    usability                    - Indicates the usability level.
    bssType                      - Type of BSS.
    informationElementsLength    - Number of bytes of the information elements
                                   received as part of the beacon or probe
                                   response.
    informationElements          - Points to the first byte of the IEs received
                                   as part of the beacon or probe response.
                                   The format of the IEs is as specified in the
                                   IEEE 802.11 specification.
    p2pDeviceRole                - Role of the P2P device.
                                   Relevant only if bssType is
                                   CSR_WIFI_SME_BSS_TYPE_P2P
    deviceInfo                   - Union containing P2P device info which
                                   depends on p2pDeviceRole parameter.
    deviceInforeservedCli        -
    deviceInfogroupInfo          -
    deviceInforeservedNone       -
    deviceInfostandalonedevInfo  -

*******************************************************************************/
typedef struct
{
    CsrWifiSsid              ssid;
    CsrWifiMacAddress        bssid;
    CsrInt16                 rssi;
    CsrInt16                 snr;
    CsrWifiSmeRadioIF        ifIndex;
    u16                beaconPeriodTu;
    CsrWifiSmeTsfTime        timeStamp;
    CsrWifiSmeTsfTime        localTime;
    u16                channelFrequency;
    u16                capabilityInformation;
    u8                 channelNumber;
    CsrWifiSmeBasicUsability usability;
    CsrWifiSmeBssType        bssType;
    u16                informationElementsLength;
    u8                *informationElements;
    CsrWifiSmeP2pRole        p2pDeviceRole;
    union {
        CsrWifiSmeEmpty        reservedCli;
        CsrWifiSmeP2pGroupInfo groupInfo;
        CsrWifiSmeEmpty        reservedNone;
        CsrWifiSmeDeviceInfo   standalonedevInfo;
    } deviceInfo;
} CsrWifiSmeScanResult;

/*******************************************************************************

  NAME
    CsrWifiSmeStaConfig

  DESCRIPTION
    Station configuration options in the SME

  MEMBERS
    connectionQualityRssiChangeTrigger - Sets the difference of RSSI
                                         measurements which triggers reports
                                         from the Firmware
    connectionQualitySnrChangeTrigger  - Sets the difference of SNR measurements
                                         which triggers reports from the
                                         Firmware
    wmmModeMask                        - Mask containing one or more values from
                                         CsrWifiSmeWmmMode
    ifIndex                            - Indicates the band of frequencies used
    allowUnicastUseGroupCipher         - If TRUE, it allows to use groupwise
                                         keys if no pairwise key is specified
    enableOpportunisticKeyCaching      - If TRUE, enables the Opportunistic Key
                                         Caching feature

*******************************************************************************/
typedef struct
{
    u8              connectionQualityRssiChangeTrigger;
    u8              connectionQualitySnrChangeTrigger;
    CsrWifiSmeWmmModeMask wmmModeMask;
    CsrWifiSmeRadioIF     ifIndex;
    CsrBool               allowUnicastUseGroupCipher;
    CsrBool               enableOpportunisticKeyCaching;
} CsrWifiSmeStaConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeWep128Keys

  DESCRIPTION
    Structure holding WEP Authentication Type and WEP keys that can be used
    when using WEP128.

  MEMBERS
    wepAuthType    - Mask to select the WEP authentication type (Open or Shared)
    selectedWepKey - Index to one of the four keys below indicating the
                     currently used WEP key. Mapping From SME/User -> firmware.
                     Key 1 -> Index 0. Key 2 -> Index 1. key 3 -> Index 2. Key
                     4-> Index 3.
    key1           - Value for key number 1.
    key2           - Value for key number 2.
    key3           - Value for key number 3.
    key4           - Value for key number 4.

*******************************************************************************/
typedef struct
{
    CsrWifiSmeWepAuthMode wepAuthType;
    u8              selectedWepKey;
    u8              key1[13];
    u8              key2[13];
    u8              key3[13];
    u8              key4[13];
} CsrWifiSmeWep128Keys;

/*******************************************************************************

  NAME
    CsrWifiSmeWep64Keys

  DESCRIPTION
    Structure holding WEP Authentication Type and WEP keys that can be used
    when using WEP64.

  MEMBERS
    wepAuthType    - Mask to select the WEP authentication type (Open or Shared)
    selectedWepKey - Index to one of the four keys below indicating the
                     currently used WEP key. Mapping From SME/User -> firmware.
                     Key 1 -> Index 0. Key 2 -> Index 1. key 3 -> Index 2. Key
                     4-> Index 3.
    key1           - Value for key number 1.
    key2           - Value for key number 2.
    key3           - Value for key number 3.
    key4           - Value for key number 4.

*******************************************************************************/
typedef struct
{
    CsrWifiSmeWepAuthMode wepAuthType;
    u8              selectedWepKey;
    u8              key1[5];
    u8              key2[5];
    u8              key3[5];
    u8              key4[5];
} CsrWifiSmeWep64Keys;

/*******************************************************************************

  NAME
    CsrWifiSmeWepAuth

  DESCRIPTION
    WEP authentication parameter structure

  MEMBERS
    wepKeyType               - WEP key try (128 bit or 64 bit)
    wepCredentials           - Union containing credentials which depends on
                               wepKeyType parameter.
    wepCredentialswep128Key  -
    wepCredentialswep64Key   -

*******************************************************************************/
typedef struct
{
    CsrWifiSmeWepCredentialType wepKeyType;
    union {
        CsrWifiSmeWep128Keys wep128Key;
        CsrWifiSmeWep64Keys  wep64Key;
    } wepCredentials;
} CsrWifiSmeWepAuth;

/*******************************************************************************

  NAME
    CsrWifiSmeWpsConfig

  DESCRIPTION
    Structure holding AP WPS Config data.

  MEMBERS
    wpsVersion               - wpsVersion should be 0x10 for WPS1.0h or 0x20 for
                               WSC2.0
    uuid                     - uuid.
    deviceName               - Device name upto 32 characters without '\0'.
    deviceNameLength         - deviceNameLen.
    manufacturer             - manufacturer: CSR
    manufacturerLength       - manufacturerLen.
    modelName                - modelName Unifi
    modelNameLength          - modelNameLen.
    modelNumber              - modelNumber
    modelNumberLength        - modelNumberLen.
    serialNumber             - serialNumber
    primDeviceType           - Primary WPS device type
    secondaryDeviceTypeCount - Number of secondary device types
    secondaryDeviceType      - list of secondary WPS device types
    configMethods            - Supported WPS config methods
    rfBands                  - RfBands.
    osVersion                - Os version on which the device is running

*******************************************************************************/
typedef struct
{
    u8                    wpsVersion;
    u8                    uuid[16];
    u8                    deviceName[32];
    u8                    deviceNameLength;
    u8                    manufacturer[64];
    u8                    manufacturerLength;
    u8                    modelName[32];
    u8                    modelNameLength;
    u8                    modelNumber[32];
    u8                    modelNumberLength;
    u8                    serialNumber[32];
    CsrWifiSmeWpsDeviceType     primDeviceType;
    u8                    secondaryDeviceTypeCount;
    CsrWifiSmeWpsDeviceType    *secondaryDeviceType;
    CsrWifiSmeWpsConfigTypeMask configMethods;
    u8                    rfBands;
    u8                    osVersion[4];
} CsrWifiSmeWpsConfig;


/* Downstream */
#define CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST            (0x0000)

#define CSR_WIFI_SME_ACTIVATE_REQ                         ((CsrWifiSmePrim) (0x0000 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_ADHOC_CONFIG_GET_REQ                 ((CsrWifiSmePrim) (0x0001 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_ADHOC_CONFIG_SET_REQ                 ((CsrWifiSmePrim) (0x0002 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_BLACKLIST_REQ                        ((CsrWifiSmePrim) (0x0003 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_CALIBRATION_DATA_GET_REQ             ((CsrWifiSmePrim) (0x0004 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_CALIBRATION_DATA_SET_REQ             ((CsrWifiSmePrim) (0x0005 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_CCX_CONFIG_GET_REQ                   ((CsrWifiSmePrim) (0x0006 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_CCX_CONFIG_SET_REQ                   ((CsrWifiSmePrim) (0x0007 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_COEX_CONFIG_GET_REQ                  ((CsrWifiSmePrim) (0x0008 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_COEX_CONFIG_SET_REQ                  ((CsrWifiSmePrim) (0x0009 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_COEX_INFO_GET_REQ                    ((CsrWifiSmePrim) (0x000A + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_CONNECT_REQ                          ((CsrWifiSmePrim) (0x000B + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_CONNECTION_CONFIG_GET_REQ            ((CsrWifiSmePrim) (0x000C + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_CONNECTION_INFO_GET_REQ              ((CsrWifiSmePrim) (0x000D + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_CONNECTION_STATS_GET_REQ             ((CsrWifiSmePrim) (0x000E + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_DEACTIVATE_REQ                       ((CsrWifiSmePrim) (0x000F + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_DISCONNECT_REQ                       ((CsrWifiSmePrim) (0x0010 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_EVENT_MASK_SET_REQ                   ((CsrWifiSmePrim) (0x0011 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_HOST_CONFIG_GET_REQ                  ((CsrWifiSmePrim) (0x0012 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_HOST_CONFIG_SET_REQ                  ((CsrWifiSmePrim) (0x0013 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_KEY_REQ                              ((CsrWifiSmePrim) (0x0014 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_LINK_QUALITY_GET_REQ                 ((CsrWifiSmePrim) (0x0015 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_MIB_CONFIG_GET_REQ                   ((CsrWifiSmePrim) (0x0016 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_MIB_CONFIG_SET_REQ                   ((CsrWifiSmePrim) (0x0017 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_MIB_GET_NEXT_REQ                     ((CsrWifiSmePrim) (0x0018 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_MIB_GET_REQ                          ((CsrWifiSmePrim) (0x0019 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_MIB_SET_REQ                          ((CsrWifiSmePrim) (0x001A + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_MULTICAST_ADDRESS_REQ                ((CsrWifiSmePrim) (0x001B + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_PACKET_FILTER_SET_REQ                ((CsrWifiSmePrim) (0x001C + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_PERMANENT_MAC_ADDRESS_GET_REQ        ((CsrWifiSmePrim) (0x001D + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_PMKID_REQ                            ((CsrWifiSmePrim) (0x001E + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_POWER_CONFIG_GET_REQ                 ((CsrWifiSmePrim) (0x001F + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_POWER_CONFIG_SET_REQ                 ((CsrWifiSmePrim) (0x0020 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_REGULATORY_DOMAIN_INFO_GET_REQ       ((CsrWifiSmePrim) (0x0021 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_ROAMING_CONFIG_GET_REQ               ((CsrWifiSmePrim) (0x0022 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_ROAMING_CONFIG_SET_REQ               ((CsrWifiSmePrim) (0x0023 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_SCAN_CONFIG_GET_REQ                  ((CsrWifiSmePrim) (0x0024 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_SCAN_CONFIG_SET_REQ                  ((CsrWifiSmePrim) (0x0025 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_SCAN_FULL_REQ                        ((CsrWifiSmePrim) (0x0026 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_SCAN_RESULTS_FLUSH_REQ               ((CsrWifiSmePrim) (0x0027 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_SCAN_RESULTS_GET_REQ                 ((CsrWifiSmePrim) (0x0028 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_SME_STA_CONFIG_GET_REQ               ((CsrWifiSmePrim) (0x0029 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_SME_STA_CONFIG_SET_REQ               ((CsrWifiSmePrim) (0x002A + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_STATION_MAC_ADDRESS_GET_REQ          ((CsrWifiSmePrim) (0x002B + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_TSPEC_REQ                            ((CsrWifiSmePrim) (0x002C + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_VERSIONS_GET_REQ                     ((CsrWifiSmePrim) (0x002D + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_WIFI_FLIGHTMODE_REQ                  ((CsrWifiSmePrim) (0x002E + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_WIFI_OFF_REQ                         ((CsrWifiSmePrim) (0x002F + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_WIFI_ON_REQ                          ((CsrWifiSmePrim) (0x0030 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_CLOAKED_SSIDS_SET_REQ                ((CsrWifiSmePrim) (0x0031 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_CLOAKED_SSIDS_GET_REQ                ((CsrWifiSmePrim) (0x0032 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_SME_COMMON_CONFIG_GET_REQ            ((CsrWifiSmePrim) (0x0033 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_SME_COMMON_CONFIG_SET_REQ            ((CsrWifiSmePrim) (0x0034 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_INTERFACE_CAPABILITY_GET_REQ         ((CsrWifiSmePrim) (0x0035 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_WPS_CONFIGURATION_REQ                ((CsrWifiSmePrim) (0x0036 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_SET_REQ                              ((CsrWifiSmePrim) (0x0037 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST))


#define CSR_WIFI_SME_PRIM_DOWNSTREAM_HIGHEST           (0x0037 + CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST)

/* Upstream */
#define CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST              (0x0000 + CSR_PRIM_UPSTREAM)

#define CSR_WIFI_SME_ACTIVATE_CFM                         ((CsrWifiSmePrim)(0x0000 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_ADHOC_CONFIG_GET_CFM                 ((CsrWifiSmePrim)(0x0001 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_ADHOC_CONFIG_SET_CFM                 ((CsrWifiSmePrim)(0x0002 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_ASSOCIATION_COMPLETE_IND             ((CsrWifiSmePrim)(0x0003 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_ASSOCIATION_START_IND                ((CsrWifiSmePrim)(0x0004 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_BLACKLIST_CFM                        ((CsrWifiSmePrim)(0x0005 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_CALIBRATION_DATA_GET_CFM             ((CsrWifiSmePrim)(0x0006 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_CALIBRATION_DATA_SET_CFM             ((CsrWifiSmePrim)(0x0007 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_CCX_CONFIG_GET_CFM                   ((CsrWifiSmePrim)(0x0008 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_CCX_CONFIG_SET_CFM                   ((CsrWifiSmePrim)(0x0009 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_COEX_CONFIG_GET_CFM                  ((CsrWifiSmePrim)(0x000A + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_COEX_CONFIG_SET_CFM                  ((CsrWifiSmePrim)(0x000B + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_COEX_INFO_GET_CFM                    ((CsrWifiSmePrim)(0x000C + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_CONNECT_CFM                          ((CsrWifiSmePrim)(0x000D + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_CONNECTION_CONFIG_GET_CFM            ((CsrWifiSmePrim)(0x000E + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_CONNECTION_INFO_GET_CFM              ((CsrWifiSmePrim)(0x000F + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_CONNECTION_QUALITY_IND               ((CsrWifiSmePrim)(0x0010 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_CONNECTION_STATS_GET_CFM             ((CsrWifiSmePrim)(0x0011 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_DEACTIVATE_CFM                       ((CsrWifiSmePrim)(0x0012 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_DISCONNECT_CFM                       ((CsrWifiSmePrim)(0x0013 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_EVENT_MASK_SET_CFM                   ((CsrWifiSmePrim)(0x0014 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_HOST_CONFIG_GET_CFM                  ((CsrWifiSmePrim)(0x0015 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_HOST_CONFIG_SET_CFM                  ((CsrWifiSmePrim)(0x0016 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_IBSS_STATION_IND                     ((CsrWifiSmePrim)(0x0017 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_KEY_CFM                              ((CsrWifiSmePrim)(0x0018 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_LINK_QUALITY_GET_CFM                 ((CsrWifiSmePrim)(0x0019 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_MEDIA_STATUS_IND                     ((CsrWifiSmePrim)(0x001A + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_MIB_CONFIG_GET_CFM                   ((CsrWifiSmePrim)(0x001B + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_MIB_CONFIG_SET_CFM                   ((CsrWifiSmePrim)(0x001C + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_MIB_GET_CFM                          ((CsrWifiSmePrim)(0x001D + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_MIB_GET_NEXT_CFM                     ((CsrWifiSmePrim)(0x001E + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_MIB_SET_CFM                          ((CsrWifiSmePrim)(0x001F + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_MIC_FAILURE_IND                      ((CsrWifiSmePrim)(0x0020 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_MULTICAST_ADDRESS_CFM                ((CsrWifiSmePrim)(0x0021 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_PACKET_FILTER_SET_CFM                ((CsrWifiSmePrim)(0x0022 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_PERMANENT_MAC_ADDRESS_GET_CFM        ((CsrWifiSmePrim)(0x0023 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_PMKID_CANDIDATE_LIST_IND             ((CsrWifiSmePrim)(0x0024 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_PMKID_CFM                            ((CsrWifiSmePrim)(0x0025 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_POWER_CONFIG_GET_CFM                 ((CsrWifiSmePrim)(0x0026 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_POWER_CONFIG_SET_CFM                 ((CsrWifiSmePrim)(0x0027 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_REGULATORY_DOMAIN_INFO_GET_CFM       ((CsrWifiSmePrim)(0x0028 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_ROAM_COMPLETE_IND                    ((CsrWifiSmePrim)(0x0029 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_ROAM_START_IND                       ((CsrWifiSmePrim)(0x002A + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_ROAMING_CONFIG_GET_CFM               ((CsrWifiSmePrim)(0x002B + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_ROAMING_CONFIG_SET_CFM               ((CsrWifiSmePrim)(0x002C + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_SCAN_CONFIG_GET_CFM                  ((CsrWifiSmePrim)(0x002D + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_SCAN_CONFIG_SET_CFM                  ((CsrWifiSmePrim)(0x002E + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_SCAN_FULL_CFM                        ((CsrWifiSmePrim)(0x002F + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_SCAN_RESULT_IND                      ((CsrWifiSmePrim)(0x0030 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_SCAN_RESULTS_FLUSH_CFM               ((CsrWifiSmePrim)(0x0031 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_SCAN_RESULTS_GET_CFM                 ((CsrWifiSmePrim)(0x0032 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_SME_STA_CONFIG_GET_CFM               ((CsrWifiSmePrim)(0x0033 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_SME_STA_CONFIG_SET_CFM               ((CsrWifiSmePrim)(0x0034 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_STATION_MAC_ADDRESS_GET_CFM          ((CsrWifiSmePrim)(0x0035 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_TSPEC_IND                            ((CsrWifiSmePrim)(0x0036 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_TSPEC_CFM                            ((CsrWifiSmePrim)(0x0037 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_VERSIONS_GET_CFM                     ((CsrWifiSmePrim)(0x0038 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_WIFI_FLIGHTMODE_CFM                  ((CsrWifiSmePrim)(0x0039 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_WIFI_OFF_IND                         ((CsrWifiSmePrim)(0x003A + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_WIFI_OFF_CFM                         ((CsrWifiSmePrim)(0x003B + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_WIFI_ON_CFM                          ((CsrWifiSmePrim)(0x003C + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_CLOAKED_SSIDS_SET_CFM                ((CsrWifiSmePrim)(0x003D + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_CLOAKED_SSIDS_GET_CFM                ((CsrWifiSmePrim)(0x003E + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_WIFI_ON_IND                          ((CsrWifiSmePrim)(0x003F + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_SME_COMMON_CONFIG_GET_CFM            ((CsrWifiSmePrim)(0x0040 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_SME_COMMON_CONFIG_SET_CFM            ((CsrWifiSmePrim)(0x0041 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_INTERFACE_CAPABILITY_GET_CFM         ((CsrWifiSmePrim)(0x0042 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_ERROR_IND                            ((CsrWifiSmePrim)(0x0043 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_INFO_IND                             ((CsrWifiSmePrim)(0x0044 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_CORE_DUMP_IND                        ((CsrWifiSmePrim)(0x0045 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_AMP_STATUS_CHANGE_IND                ((CsrWifiSmePrim)(0x0046 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_WPS_CONFIGURATION_CFM                ((CsrWifiSmePrim)(0x0047 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST))

#define CSR_WIFI_SME_PRIM_UPSTREAM_HIGHEST             (0x0047 + CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST)

#define CSR_WIFI_SME_PRIM_DOWNSTREAM_COUNT             (CSR_WIFI_SME_PRIM_DOWNSTREAM_HIGHEST + 1 - CSR_WIFI_SME_PRIM_DOWNSTREAM_LOWEST)
#define CSR_WIFI_SME_PRIM_UPSTREAM_COUNT               (CSR_WIFI_SME_PRIM_UPSTREAM_HIGHEST   + 1 - CSR_WIFI_SME_PRIM_UPSTREAM_LOWEST)

/*******************************************************************************

  NAME
    CsrWifiSmeActivateReq

  DESCRIPTION
    The WMA sends this primitive to activate the SME.
    The WMA must activate the SME before it can send any other primitive.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeActivateReq;

/*******************************************************************************

  NAME
    CsrWifiSmeAdhocConfigGetReq

  DESCRIPTION
    This primitive gets the value of the adHocConfig parameter.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeAdhocConfigGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeAdhocConfigSetReq

  DESCRIPTION
    This primitive sets the value of the adHocConfig parameter.

  MEMBERS
    common      - Common header for use with the CsrWifiFsm Module
    adHocConfig - Sets the values to use when starting an ad hoc network.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    CsrWifiSmeAdHocConfig adHocConfig;
} CsrWifiSmeAdhocConfigSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeBlacklistReq

  DESCRIPTION
    The wireless manager application should call this primitive to notify the
    driver of any networks that should not be connected to. The interface
    allows the wireless manager application to query, add, remove, and flush
    the BSSIDs that the driver may not connect or roam to.
    When this primitive adds to the black list the BSSID to which the SME is
    currently connected, the SME will try to roam, if applicable, to another
    BSSID in the same ESS; if the roaming procedure fails, the SME will
    disconnect.

  MEMBERS
    common          - Common header for use with the CsrWifiFsm Module
    interfaceTag    - Interface Identifier; unique identifier of an interface
    action          - The value of the CsrWifiSmeListAction parameter instructs
                      the driver to modify or provide the list of blacklisted
                      networks.
    setAddressCount - Number of BSSIDs sent with this primitive
    setAddresses    - Pointer to the list of BBSIDs sent with the primitive, set
                      to NULL if none is sent.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    u16            interfaceTag;
    CsrWifiSmeListAction action;
    u8             setAddressCount;
    CsrWifiMacAddress   *setAddresses;
} CsrWifiSmeBlacklistReq;

/*******************************************************************************

  NAME
    CsrWifiSmeCalibrationDataGetReq

  DESCRIPTION
    This primitive retrieves the Wi-Fi radio calibration data.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeCalibrationDataGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeCalibrationDataSetReq

  DESCRIPTION
    This primitive sets the Wi-Fi radio calibration data.
    The usage of the primitive with proper calibration data will avoid
    time-consuming configuration after power-up.

  MEMBERS
    common                - Common header for use with the CsrWifiFsm Module
    calibrationDataLength - Number of bytes in the buffer pointed by
                            calibrationData
    calibrationData       - Pointer to a buffer of length calibrationDataLength
                            containing the calibration data

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       calibrationDataLength;
    u8       *calibrationData;
} CsrWifiSmeCalibrationDataSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeCcxConfigGetReq

  DESCRIPTION
    This primitive gets the value of the CcxConfig parameter.
    CURRENTLY NOT SUPPORTED.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiSmeCcxConfigGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeCcxConfigSetReq

  DESCRIPTION
    This primitive sets the value of the CcxConfig parameter.
    CURRENTLY NOT SUPPORTED.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    ccxConfig    - Currently not supported

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent     common;
    u16           interfaceTag;
    CsrWifiSmeCcxConfig ccxConfig;
} CsrWifiSmeCcxConfigSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeCoexConfigGetReq

  DESCRIPTION
    This primitive gets the value of the CoexConfig parameter.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeCoexConfigGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeCoexConfigSetReq

  DESCRIPTION
    This primitive sets the value of the CoexConfig parameter.

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    coexConfig - Configures the coexistence behaviour

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    CsrWifiSmeCoexConfig coexConfig;
} CsrWifiSmeCoexConfigSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeCoexInfoGetReq

  DESCRIPTION
    This primitive gets the value of the CoexInfo parameter.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeCoexInfoGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeConnectReq

  DESCRIPTION
    The wireless manager application calls this primitive to start the
    process of joining an 802.11 wireless network or to start an ad hoc
    network.
    The structure pointed by connectionConfig contains parameters describing
    the network to join or, in case of an ad hoc network, to host or join.
    The SME will select a network, perform the IEEE 802.11 Join, Authenticate
    and Associate exchanges.
    The SME selects the networks from the current scan list that match both
    the SSID and BSSID, however either or both of these may be the wildcard
    value. Using this rule, the following operations are possible:
      * To connect to a network by name, specify the SSID and set the BSSID to
        0xFF 0xFF 0xFF 0xFF 0xFF 0xFF. If there are two or more networks visible,
        the SME will select the one with the strongest signal.
      * To connect to a specific network, specify the BSSID. The SSID is
        optional, but if given it must match the SSID of the network. An empty
        SSID may be specified by setting the SSID length to zero. Please note
        that if the BSSID is specified (i.e. not equal to 0xFF 0xFF 0xFF 0xFF
        0xFF 0xFF), the SME will not attempt to roam if signal conditions become
        poor, even if there is an alternative AP with an SSID that matches the
        current network SSID.
      * To connect to any network matching the other parameters (i.e. security,
        etc), set the SSID length to zero and set the BSSID to 0xFF 0xFF 0xFF
        0xFF 0xFF 0xFF. In this case, the SME will order all available networks
        by their signal strengths and will iterate through this list until it
        successfully connects.
    NOTE: Specifying the BSSID will restrict the selection to one specific
    network. If SSID and BSSID are given, they must both match the network
    for it to be selected. To select a network based on the SSID only, the
    wireless manager application must set the BSSID to 0xFF 0xFF 0xFF 0xFF
    0xFF 0xFF.
    The SME will try to connect to each network that matches the provided
    parameters, one by one, until it succeeds or has tried unsuccessfully
    with all the matching networks.
    If there is no network that matches the parameters and the request allows
    to host an ad hoc network, the SME will advertise a new ad hoc network
    instead.
    If the SME cannot connect, it will notify the failure in the confirm.

  MEMBERS
    common           - Common header for use with the CsrWifiFsm Module
    interfaceTag     - Interface Identifier; unique identifier of an interface
    connectionConfig - Describes the candidate network to join or to host.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent            common;
    u16                  interfaceTag;
    CsrWifiSmeConnectionConfig connectionConfig;
} CsrWifiSmeConnectReq;

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionConfigGetReq

  DESCRIPTION
    This primitive gets the value of the ConnectionConfig parameter.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiSmeConnectionConfigGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionInfoGetReq

  DESCRIPTION
    This primitive gets the value of the ConnectionInfo parameter.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiSmeConnectionInfoGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionStatsGetReq

  DESCRIPTION
    This primitive gets the value of the ConnectionStats parameter.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiSmeConnectionStatsGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeDeactivateReq

  DESCRIPTION
    The WMA sends this primitive to deactivate the SME.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeDeactivateReq;

/*******************************************************************************

  NAME
    CsrWifiSmeDisconnectReq

  DESCRIPTION
    The wireless manager application may disconnect from the current network
    by calling this primitive

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiSmeDisconnectReq;

/*******************************************************************************

  NAME
    CsrWifiSmeEventMaskSetReq

  DESCRIPTION
    The wireless manager application may register with the SME to receive
    notification of interesting events. Indications will be sent only if the
    wireless manager explicitly registers to be notified of that event.
    indMask is a bit mask of values defined in CsrWifiSmeIndicationsMask.

  MEMBERS
    common  - Common header for use with the CsrWifiFsm Module
    indMask - Set mask with values from CsrWifiSmeIndications

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    CsrWifiSmeIndicationsMask indMask;
} CsrWifiSmeEventMaskSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeHostConfigGetReq

  DESCRIPTION
    This primitive gets the value of the hostConfig parameter.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiSmeHostConfigGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeHostConfigSetReq

  DESCRIPTION
    This primitive sets the value of the hostConfig parameter.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    hostConfig   - Communicates a change of host power state (for example, on
                   mains power, on battery power etc) and of the periodicity of
                   traffic data

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    u16            interfaceTag;
    CsrWifiSmeHostConfig hostConfig;
} CsrWifiSmeHostConfigSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeKeyReq

  DESCRIPTION
    The wireless manager application calls this primitive to add or remove
    keys that the chip should use for encryption of data.
    The interface allows the wireless manager application to add and remove
    keys according to the specified action.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    action       - The value of the CsrWifiSmeListAction parameter instructs the
                   driver to modify or provide the list of keys.
                   CSR_WIFI_SME_LIST_ACTION_GET is not supported here.
    key          - Key to be added or removed

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    u16            interfaceTag;
    CsrWifiSmeListAction action;
    CsrWifiSmeKey        key;
} CsrWifiSmeKeyReq;

/*******************************************************************************

  NAME
    CsrWifiSmeLinkQualityGetReq

  DESCRIPTION
    This primitive gets the value of the LinkQuality parameter.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiSmeLinkQualityGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeMibConfigGetReq

  DESCRIPTION
    This primitive gets the value of the MibConfig parameter.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeMibConfigGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeMibConfigSetReq

  DESCRIPTION
    This primitive sets the value of the MibConfig parameter.

  MEMBERS
    common    - Common header for use with the CsrWifiFsm Module
    mibConfig - Conveys the desired value of various IEEE 802.11 attributes as
                currently configured

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent     common;
    CsrWifiSmeMibConfig mibConfig;
} CsrWifiSmeMibConfigSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeMibGetNextReq

  DESCRIPTION
    To read a sequence of MIB parameters, for example a table, call this
    primitive to find the name of the next MIB variable

  MEMBERS
    common             - Common header for use with the CsrWifiFsm Module
    mibAttributeLength - Length of mibAttribute
    mibAttribute       - Points to a VarBind or VarBindList containing the
                         name(s) of the MIB variable(s) to search from.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       mibAttributeLength;
    u8       *mibAttribute;
} CsrWifiSmeMibGetNextReq;

/*******************************************************************************

  NAME
    CsrWifiSmeMibGetReq

  DESCRIPTION
    The wireless manager application calls this primitive to retrieve one or
    more MIB variables.

  MEMBERS
    common             - Common header for use with the CsrWifiFsm Module
    mibAttributeLength - Length of mibAttribute
    mibAttribute       - Points to the VarBind or VarBindList containing the
                         names of the MIB variables to be retrieved

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       mibAttributeLength;
    u8       *mibAttribute;
} CsrWifiSmeMibGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeMibSetReq

  DESCRIPTION
    The SME provides raw access to the MIB on the chip, which may be used by
    some configuration or diagnostic utilities, but is not normally needed by
    the wireless manager application.
    The MIB access functions use BER encoded names (OID) of the MIB
    parameters and BER encoded values, as described in the chip Host
    Interface Protocol Specification.
    The MIB parameters are described in 'Wi-Fi 5.0.0 Management Information
    Base Reference Guide'.
    The wireless manager application calls this primitive to set one or more
    MIB variables

  MEMBERS
    common             - Common header for use with the CsrWifiFsm Module
    mibAttributeLength - Length of mibAttribute
    mibAttribute       - Points to the VarBind or VarBindList containing the
                         names and values of the MIB variables to set

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       mibAttributeLength;
    u8       *mibAttribute;
} CsrWifiSmeMibSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeMulticastAddressReq

  DESCRIPTION
    The wireless manager application calls this primitive to specify the
    multicast addresses which the chip should recognise. The interface allows
    the wireless manager application to query, add, remove and flush the
    multicast addresses for the network interface according to the specified
    action.

  MEMBERS
    common            - Common header for use with the CsrWifiFsm Module
    interfaceTag      - Interface Identifier; unique identifier of an interface
    action            - The value of the CsrWifiSmeListAction parameter
                        instructs the driver to modify or provide the list of
                        MAC addresses.
    setAddressesCount - Number of MAC addresses sent with the primitive
    setAddresses      - Pointer to the list of MAC Addresses sent with the
                        primitive, set to NULL if none is sent.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    u16            interfaceTag;
    CsrWifiSmeListAction action;
    u8             setAddressesCount;
    CsrWifiMacAddress   *setAddresses;
} CsrWifiSmeMulticastAddressReq;

/*******************************************************************************

  NAME
    CsrWifiSmePacketFilterSetReq

  DESCRIPTION
    The wireless manager application should call this primitive to enable or
    disable filtering of broadcast packets: uninteresting broadcast packets
    will be dropped by the Wi-Fi chip, instead of passing them up to the
    host.
    This has the advantage of saving power in the host application processor
    as it removes the need to process unwanted packets.
    All broadcast packets are filtered according to the filter and the filter
    mode provided, except ARP packets, which are filtered using
    arpFilterAddress.
    Filters are not cumulative: only the parameters specified in the most
    recent successful request are significant.
    For more information, see 'UniFi Firmware API Specification'.

  MEMBERS
    common           - Common header for use with the CsrWifiFsm Module
    interfaceTag     - Interface Identifier; unique identifier of an interface
    filterLength     - Length of the filter in bytes.
                       filterLength=0 disables the filter previously set
    filter           - Points to the first byte of the filter provided, if any.
                       This shall include zero or more instance of the
                       information elements of one of these types
                         * Traffic Classification (TCLAS) elements
                         * WMM-SA TCLAS elements
    mode             - Specifies whether the filter selects or excludes packets
                       matching the filter
    arpFilterAddress - IPv4 address to be used for filtering the ARP packets.
                         * If the specified address is the IPv4 broadcast address
                           (255.255.255.255), all ARP packets are reported to the
                           host,
                         * If the specified address is NOT the IPv4 broadcast
                           address, only ARP packets with the specified address in
                           the Source or Target Protocol Address fields are reported
                           to the host

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent            common;
    u16                  interfaceTag;
    u16                  filterLength;
    u8                  *filter;
    CsrWifiSmePacketFilterMode mode;
    CsrWifiIp4Address          arpFilterAddress;
} CsrWifiSmePacketFilterSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmePermanentMacAddressGetReq

  DESCRIPTION
    This primitive retrieves the MAC address stored in EEPROM

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmePermanentMacAddressGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmePmkidReq

  DESCRIPTION
    The wireless manager application calls this primitive to request an
    operation on the SME PMKID list.
    The action argument specifies the operation to perform.
    When the connection is complete, the wireless manager application may
    then send and receive EAPOL packets to complete WPA or WPA2
    authentication if appropriate.
    The wireless manager application can then pass the resulting encryption
    keys using this primitive.

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    interfaceTag   - Interface Identifier; unique identifier of an interface
    action         - The value of the CsrWifiSmeListAction parameter instructs
                     the driver to modify or provide the list of PMKIDs.
    setPmkidsCount - Number of PMKIDs sent with the primitive
    setPmkids      - Pointer to the list of PMKIDs sent with the primitive, set
                     to NULL if none is sent.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    u16            interfaceTag;
    CsrWifiSmeListAction action;
    u8             setPmkidsCount;
    CsrWifiSmePmkid     *setPmkids;
} CsrWifiSmePmkidReq;

/*******************************************************************************

  NAME
    CsrWifiSmePowerConfigGetReq

  DESCRIPTION
    This primitive gets the value of the PowerConfig parameter.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmePowerConfigGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmePowerConfigSetReq

  DESCRIPTION
    This primitive sets the value of the PowerConfig parameter.

  MEMBERS
    common      - Common header for use with the CsrWifiFsm Module
    powerConfig - Power saving configuration

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    CsrWifiSmePowerConfig powerConfig;
} CsrWifiSmePowerConfigSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeRegulatoryDomainInfoGetReq

  DESCRIPTION
    This primitive gets the value of the RegulatoryDomainInfo parameter.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeRegulatoryDomainInfoGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeRoamingConfigGetReq

  DESCRIPTION
    This primitive gets the value of the RoamingConfig parameter.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiSmeRoamingConfigGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeRoamingConfigSetReq

  DESCRIPTION
    This primitive sets the value of the RoamingConfig parameter.

  MEMBERS
    common        - Common header for use with the CsrWifiFsm Module
    interfaceTag  - Interface Identifier; unique identifier of an interface
    roamingConfig - Desired roaming behaviour values

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent         common;
    u16               interfaceTag;
    CsrWifiSmeRoamingConfig roamingConfig;
} CsrWifiSmeRoamingConfigSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeScanConfigGetReq

  DESCRIPTION
    This primitive gets the value of the ScanConfig parameter.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeScanConfigGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeScanConfigSetReq

  DESCRIPTION
    This primitive sets the value of the ScanConfig parameter.
    The SME normally configures the firmware to perform autonomous scanning
    without involving the host.
    The firmware passes beacon / probe response or indicates loss of beacon
    on certain changes of state, for example:
      * A new AP is seen for the first time
      * An AP is no longer visible
      * The signal strength of an AP changes by more than a certain amount, as
        configured by the thresholds in the scanConfig parameter
    In addition to the autonomous scan, the wireless manager application may
    request a scan at any time using CSR_WIFI_SME_SCAN_FULL_REQ.

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    scanConfig - Reports the configuration for the autonomous scanning behaviour
                 of the firmware

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    CsrWifiSmeScanConfig scanConfig;
} CsrWifiSmeScanConfigSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeScanFullReq

  DESCRIPTION
    The wireless manager application should call this primitive to request a
    full scan.
    Channels are scanned actively or passively according to the requirement
    set by regulatory domain.
    If the SME receives this primitive while a full scan is going on, the new
    request is buffered and it will be served after the current full scan is
    completed.

  MEMBERS
    common           - Common header for use with the CsrWifiFsm Module
    ssidCount        - Number of SSIDs provided.
                       If it is 0, the SME will attempt to detect any network
    ssid             - Points to the first SSID provided, if any.
    bssid            - BSS identifier.
                       If it is equal to FF-FF-FF-FF-FF, the SME will listen for
                       messages from any BSS.
                       If it is different from FF-FF-FF-FF-FF and any SSID is
                       provided, one SSID must match the network of the BSS.
    forceScan        - Forces the scan even if the SME is in a state which would
                       normally prevent it (e.g. autonomous scan is running).
    bssType          - Type of BSS to scan for
    scanType         - Type of scan to perform
    channelListCount - Number of channels provided.
                       If it is 0, the SME will initiate a scan of all the
                       supported channels that are permitted by the current
                       regulatory domain.
    channelList      - Points to the first channel , or NULL if channelListCount
                       is zero.
    probeIeLength    - Length of the information element in bytes to be sent
                       with the probe message.
    probeIe          - Points to the first byte of the information element to be
                       sent with the probe message.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent    common;
    u8           ssidCount;
    CsrWifiSsid       *ssid;
    CsrWifiMacAddress  bssid;
    CsrBool            forceScan;
    CsrWifiSmeBssType  bssType;
    CsrWifiSmeScanType scanType;
    u16          channelListCount;
    u8          *channelList;
    u16          probeIeLength;
    u8          *probeIe;
} CsrWifiSmeScanFullReq;

/*******************************************************************************

  NAME
    CsrWifiSmeScanResultsFlushReq

  DESCRIPTION
    The Wireless Manager calls this primitive to ask the SME to delete all
    scan results from its cache, except for the scan result of any currently
    connected network.
    As scan results are received by the SME from the firmware, they are
    cached in the SME memory.
    Any time the Wireless Manager requests scan results, they are returned
    from the SME internal cache.
    For some applications it may be desirable to clear this cache prior to
    requesting that a scan be performed; this will ensure that the cache then
    only contains the networks detected in the most recent scan.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeScanResultsFlushReq;

/*******************************************************************************

  NAME
    CsrWifiSmeScanResultsGetReq

  DESCRIPTION
    The wireless manager application calls this primitive to retrieve the
    current set of scan results, either after receiving a successful
    CSR_WIFI_SME_SCAN_FULL_CFM, or to get autonomous scan results.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeScanResultsGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeSmeStaConfigGetReq

  DESCRIPTION
    This primitive gets the value of the SmeStaConfig parameter.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiSmeSmeStaConfigGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeSmeStaConfigSetReq

  DESCRIPTION
    This primitive sets the value of the SmeConfig parameter.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    smeConfig    - SME Station Parameters to be set

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent     common;
    u16           interfaceTag;
    CsrWifiSmeStaConfig smeConfig;
} CsrWifiSmeSmeStaConfigSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeStationMacAddressGetReq

  DESCRIPTION
    This primitives is used to retrieve the current MAC address used by the
    station.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeStationMacAddressGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeTspecReq

  DESCRIPTION
    The wireless manager application should call this primitive to use the
    TSPEC feature.
    The chip supports the use of TSPECs and TCLAS for the use of IEEE
    802.11/WMM Quality of Service features.
    The API allows the wireless manager application to supply a correctly
    formatted TSPEC and TCLAS pair to the driver.
    After performing basic validation, the driver negotiates the installation
    of the TSPEC with the AP as defined by the 802.11 specification.
    The driver retains all TSPEC and TCLAS pairs until they are specifically
    removed.
    It is not compulsory for a TSPEC to have a TCLAS (NULL is used to
    indicate that no TCLAS is supplied), while a TCLASS always require a
    TSPEC.
    The format of the TSPEC element is specified in 'WMM (including WMM Power
    Save) Specification - Version 1.1' and 'ANSI/IEEE Std 802.11-REVmb/D3.0'.
    For more information, see 'UniFi Configuring WMM and WMM-PS'.

  MEMBERS
    common        - Common header for use with the CsrWifiFsm Module
    interfaceTag  - Interface Identifier; unique identifier of an interface
    action        - Specifies the action to be carried out on the list of TSPECs.
                    CSR_WIFI_SME_LIST_ACTION_FLUSH is not applicable here.
    transactionId - Unique Transaction ID for the TSPEC, as assigned by the
                    driver
    strict        - If it set to false, allows the SME to perform automatic
                    TSPEC negotiation
    ctrlMask      - Additional TSPEC configuration for CCX.
                    Set mask with values from CsrWifiSmeTspecCtrl.
                    CURRENTLY NOT SUPPORTED
    tspecLength   - Length of the TSPEC.
    tspec         - Points to the first byte of the TSPEC
    tclasLength   - Length of the TCLAS.
                    If it is equal to 0, no TCLASS is provided for the TSPEC
    tclas         - Points to the first byte of the TCLAS, if any.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent         common;
    u16               interfaceTag;
    CsrWifiSmeListAction    action;
    CsrUint32               transactionId;
    CsrBool                 strict;
    CsrWifiSmeTspecCtrlMask ctrlMask;
    u16               tspecLength;
    u8               *tspec;
    u16               tclasLength;
    u8               *tclas;
} CsrWifiSmeTspecReq;

/*******************************************************************************

  NAME
    CsrWifiSmeVersionsGetReq

  DESCRIPTION
    This primitive gets the value of the Versions parameter.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeVersionsGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeWifiFlightmodeReq

  DESCRIPTION
    The wireless manager application may call this primitive on boot-up of
    the platform to ensure that the chip is placed in a mode that prevents
    any emission of RF energy.
    This primitive is an alternative to CSR_WIFI_SME_WIFI_ON_REQ.
    As in CSR_WIFI_SME_WIFI_ON_REQ, it causes the download of the patch file
    (if any) and the programming of the initial MIB settings (if supplied by
    the WMA), but it also ensures that the chip is left in its lowest
    possible power-mode with the radio subsystems disabled.
    This feature is useful on platforms where power cannot be removed from
    the chip (leaving the chip not initialised will cause it to consume more
    power so calling this function ensures that the chip is initialised into
    a low power mode but without entering a state where it could emit any RF
    energy).
    NOTE: this primitive does not cause the Wi-Fi to change state: Wi-Fi
    stays conceptually off. Configuration primitives can be sent after
    CSR_WIFI_SME_WIFI_FLIGHTMODE_REQ and the configuration will be maintained.
    Requests that require the state of the Wi-Fi to be ON will return
    CSR_WIFI_SME_STATUS_WIFI_OFF in their confirms.

  MEMBERS
    common        - Common header for use with the CsrWifiFsm Module
    address       - Optionally specifies a station MAC address.
                    In normal use, the manager should set the address to 0xFF
                    0xFF 0xFF 0xFF 0xFF 0xFF, which will cause the chip to use
                    the MAC address in the MIB.
    mibFilesCount - Number of provided data blocks with initial MIB values
    mibFiles      - Points to the first data block with initial MIB values.
                    These data blocks are typically the contents of the provided
                    files ufmib.dat and localmib.dat, available from the host
                    file system, if they exist.
                    These files typically contain radio tuning and calibration
                    values.
                    More values can be created using the Host Tools.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    CsrWifiMacAddress    address;
    u16            mibFilesCount;
    CsrWifiSmeDataBlock *mibFiles;
} CsrWifiSmeWifiFlightmodeReq;

/*******************************************************************************

  NAME
    CsrWifiSmeWifiOffReq

  DESCRIPTION
    The wireless manager application calls this primitive to turn off the
    chip, thus saving power when Wi-Fi is not in use.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeWifiOffReq;

/*******************************************************************************

  NAME
    CsrWifiSmeWifiOnReq

  DESCRIPTION
    The wireless manager application calls this primitive to turn on the
    Wi-Fi chip.
    If the Wi-Fi chip is currently off, the SME turns the Wi-Fi chip on,
    downloads the patch file (if any), and programs the initial MIB settings
    (if supplied by the WMA).
    The patch file is not provided with the SME API; its downloading is
    automatic and handled internally by the system.
    The MIB settings, when provided, override the default values that the
    firmware loads from EEPROM.
    If the Wi-Fi chip is already on, the SME takes no action and returns a
    successful status in the confirm.

  MEMBERS
    common        - Common header for use with the CsrWifiFsm Module
    address       - Optionally specifies a station MAC address.
                    In normal use, the manager should set the address to 0xFF
                    0xFF 0xFF 0xFF 0xFF 0xFF, which will cause the chip to use
                    the MAC address in the MIB
    mibFilesCount - Number of provided data blocks with initial MIB values
    mibFiles      - Points to the first data block with initial MIB values.
                    These data blocks are typically the contents of the provided
                    files ufmib.dat and localmib.dat, available from the host
                    file system, if they exist.
                    These files typically contain radio tuning and calibration
                    values.
                    More values can be created using the Host Tools.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    CsrWifiMacAddress    address;
    u16            mibFilesCount;
    CsrWifiSmeDataBlock *mibFiles;
} CsrWifiSmeWifiOnReq;

/*******************************************************************************

  NAME
    CsrWifiSmeCloakedSsidsSetReq

  DESCRIPTION
    This primitive sets the list of cloaked SSIDs for which the WMA possesses
    profiles.
    When the driver detects a cloaked AP, the SME will explicitly scan for it
    using the list of cloaked SSIDs provided it, and, if the scan succeeds,
    it will report the AP to the WMA either via CSR_WIFI_SME_SCAN_RESULT_IND
    (if registered) or via CSR_WIFI_SCAN_RESULT_GET_CFM.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    cloakedSsids - Sets the list of cloaked SSIDs

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent             common;
    CsrWifiSmeCloakedSsidConfig cloakedSsids;
} CsrWifiSmeCloakedSsidsSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeCloakedSsidsGetReq

  DESCRIPTION
    This primitive gets the value of the CloakedSsids parameter.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeCloakedSsidsGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeSmeCommonConfigGetReq

  DESCRIPTION
    This primitive gets the value of the Sme common parameter.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeSmeCommonConfigGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeSmeCommonConfigSetReq

  DESCRIPTION
    This primitive sets the value of the Sme common.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    deviceConfig - Configuration options in the SME

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent        common;
    CsrWifiSmeDeviceConfig deviceConfig;
} CsrWifiSmeSmeCommonConfigSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeInterfaceCapabilityGetReq

  DESCRIPTION
    The Wireless Manager calls this primitive to ask the SME for the
    capabilities of the supported interfaces

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
} CsrWifiSmeInterfaceCapabilityGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeWpsConfigurationReq

  DESCRIPTION
    This primitive passes the WPS information for the device to SME. This may
    be accepted only if no interface is active.

  MEMBERS
    common    - Common header for use with the CsrWifiFsm Module
    wpsConfig - WPS config.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent     common;
    CsrWifiSmeWpsConfig wpsConfig;
} CsrWifiSmeWpsConfigurationReq;

/*******************************************************************************

  NAME
    CsrWifiSmeSetReq

  DESCRIPTION
    Used to pass custom data to the SME. Format is the same as 802.11 Info
    Elements => | Id | Length | Data
    1) Cmanr Test Mode "Id:0 Length:1 Data:0x00 = OFF 0x01 = ON" "0x00 0x01
    (0x00|0x01)"

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    dataLength - Number of bytes in the buffer pointed to by 'data'
    data       - Pointer to the buffer containing 'dataLength' bytes

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrUint32       dataLength;
    u8       *data;
} CsrWifiSmeSetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeActivateCfm

  DESCRIPTION
    The SME sends this primitive when the activation is complete.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeActivateCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeAdhocConfigGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common      - Common header for use with the CsrWifiFsm Module
    status      - Reports the result of the request
    adHocConfig - Contains the values used when starting an Ad-hoc (IBSS)
                  connection.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    CsrResult             status;
    CsrWifiSmeAdHocConfig adHocConfig;
} CsrWifiSmeAdhocConfigGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeAdhocConfigSetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeAdhocConfigSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeAssociationCompleteInd

  DESCRIPTION
    The SME will send this primitive to all the tasks that have registered to
    receive it whenever it completes an attempt to associate with an AP. If
    the association was successful, status will be set to
    CSR_WIFI_SME_STATUS_SUCCESS, otherwise status and deauthReason shall be
    set to appropriate error codes.

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    interfaceTag   - Interface Identifier; unique identifier of an interface
    status         - Reports the result of the association procedure
    connectionInfo - This parameter is relevant only if result is
                     CSR_WIFI_SME_STATUS_SUCCESS:
                     it points to the connection information for the new network
    deauthReason   - This parameter is relevant only if result is not
                     CSR_WIFI_SME_STATUS_SUCCESS:
                     if the AP deauthorised the station, it gives the reason of
                     the deauthorization

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    u16                 interfaceTag;
    CsrResult                 status;
    CsrWifiSmeConnectionInfo  connectionInfo;
    CsrWifiSmeIEEE80211Reason deauthReason;
} CsrWifiSmeAssociationCompleteInd;

/*******************************************************************************

  NAME
    CsrWifiSmeAssociationStartInd

  DESCRIPTION
    The SME will send this primitive to all the tasks that have registered to
    receive it whenever it begins an attempt to associate with an AP.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    address      - BSSID of the associating network
    ssid         - Service Set identifier of the associating network

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent   common;
    u16         interfaceTag;
    CsrWifiMacAddress address;
    CsrWifiSsid       ssid;
} CsrWifiSmeAssociationStartInd;

/*******************************************************************************

  NAME
    CsrWifiSmeBlacklistCfm

  DESCRIPTION
    The SME will call this primitive when the action on the blacklist has
    completed. For a GET action, this primitive also reports the list of
    BBSIDs in the blacklist.

  MEMBERS
    common          - Common header for use with the CsrWifiFsm Module
    interfaceTag    - Interface Identifier; unique identifier of an interface
    status          - Reports the result of the request
    action          - Action in the request
    getAddressCount - This parameter is only relevant if action is
                      CSR_WIFI_SME_LIST_ACTION_GET:
                      number of BSSIDs sent with this primitive
    getAddresses    - Pointer to the list of BBSIDs sent with the primitive, set
                      to NULL if none is sent.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    u16            interfaceTag;
    CsrResult            status;
    CsrWifiSmeListAction action;
    u8             getAddressCount;
    CsrWifiMacAddress   *getAddresses;
} CsrWifiSmeBlacklistCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeCalibrationDataGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common                - Common header for use with the CsrWifiFsm Module
    status                - Reports the result of the request
    calibrationDataLength - Number of bytes in the buffer pointed by
                            calibrationData
    calibrationData       - Pointer to a buffer of length calibrationDataLength
                            containing the calibration data

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
    u16       calibrationDataLength;
    u8       *calibrationData;
} CsrWifiSmeCalibrationDataGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeCalibrationDataSetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeCalibrationDataSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeCcxConfigGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request
    ccxConfig    - Currently not supported

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent     common;
    u16           interfaceTag;
    CsrResult           status;
    CsrWifiSmeCcxConfig ccxConfig;
} CsrWifiSmeCcxConfigGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeCcxConfigSetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiSmeCcxConfigSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeCoexConfigGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    status     - Reports the result of the request
    coexConfig - Reports the parameters used to configure the coexistence
                 behaviour

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    CsrResult            status;
    CsrWifiSmeCoexConfig coexConfig;
} CsrWifiSmeCoexConfigGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeCoexConfigSetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeCoexConfigSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeCoexInfoGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common   - Common header for use with the CsrWifiFsm Module
    status   - Reports the result of the request
    coexInfo - Reports information and state related to coexistence.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent    common;
    CsrResult          status;
    CsrWifiSmeCoexInfo coexInfo;
} CsrWifiSmeCoexInfoGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeConnectCfm

  DESCRIPTION
    The SME calls this primitive when the connection exchange is complete or
    all connection attempts fail.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request.
                   CSR_WIFI_SME_STATUS_NOT_FOUND: all attempts by the SME to
                   locate the requested AP failed

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiSmeConnectCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionConfigGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common           - Common header for use with the CsrWifiFsm Module
    interfaceTag     - Interface Identifier; unique identifier of an interface
    status           - Reports the result of the request
    connectionConfig - Parameters used by the SME for selecting a network

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent            common;
    u16                  interfaceTag;
    CsrResult                  status;
    CsrWifiSmeConnectionConfig connectionConfig;
} CsrWifiSmeConnectionConfigGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionInfoGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    interfaceTag   - Interface Identifier; unique identifier of an interface
    status         - Reports the result of the request
    connectionInfo - Information about the current connection

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent          common;
    u16                interfaceTag;
    CsrResult                status;
    CsrWifiSmeConnectionInfo connectionInfo;
} CsrWifiSmeConnectionInfoGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionQualityInd

  DESCRIPTION
    The SME sends this primitive to all the tasks that have registered to
    receive it whenever the value of the current connection quality
    parameters change by more than a certain configurable amount.
    The wireless manager application may configure the trigger thresholds for
    this indication using the field in smeConfig parameter of
    CSR_WIFI_SME_SME_CONFIG_SET_REQ.
    Connection quality messages can be suppressed by setting both thresholds
    to zero.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    linkQuality  - Indicates the quality of the link

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    u16             interfaceTag;
    CsrWifiSmeLinkQuality linkQuality;
} CsrWifiSmeConnectionQualityInd;

/*******************************************************************************

  NAME
    CsrWifiSmeConnectionStatsGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common          - Common header for use with the CsrWifiFsm Module
    interfaceTag    - Interface Identifier; unique identifier of an interface
    status          - Reports the result of the request
    connectionStats - Statistics for current connection.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    u16                 interfaceTag;
    CsrResult                 status;
    CsrWifiSmeConnectionStats connectionStats;
} CsrWifiSmeConnectionStatsGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeDeactivateCfm

  DESCRIPTION
    The SME sends this primitive when the deactivation is complete.
    The WMA cannot send any more primitives until it actives the SME again
    sending another CSR_WIFI_SME_ACTIVATE_REQ.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeDeactivateCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeDisconnectCfm

  DESCRIPTION
    On reception of CSR_WIFI_SME_DISCONNECT_REQ the SME will perform a
    disconnect operation, sending a CsrWifiSmeMediaStatusInd with
    CSR_WIFI_SME_MEDIA_STATUS_DISCONNECTED and then call this primitive when
    disconnection is complete.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiSmeDisconnectCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeEventMaskSetCfm

  DESCRIPTION
    The SME calls the primitive to report the result of the request
    primitive.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeEventMaskSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeHostConfigGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request
    hostConfig   - Current host power state.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    u16            interfaceTag;
    CsrResult            status;
    CsrWifiSmeHostConfig hostConfig;
} CsrWifiSmeHostConfigGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeHostConfigSetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiSmeHostConfigSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeIbssStationInd

  DESCRIPTION
    The SME will send this primitive to indicate that a station has joined or
    left the ad-hoc network.

  MEMBERS
    common      - Common header for use with the CsrWifiFsm Module
    address     - MAC address of the station that has joined or left
    isconnected - TRUE if the station joined, FALSE if the station left

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent   common;
    CsrWifiMacAddress address;
    CsrBool           isconnected;
} CsrWifiSmeIbssStationInd;

/*******************************************************************************

  NAME
    CsrWifiSmeKeyCfm

  DESCRIPTION
    The SME calls the primitive to report the result of the request
    primitive.

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    interfaceTag   - Interface Identifier; unique identifier of an interface
    status         - Reports the result of the request
    action         - Action in the request
    keyType        - Type of the key added/deleted
    peerMacAddress - Peer MAC Address of the key added/deleted

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    u16            interfaceTag;
    CsrResult            status;
    CsrWifiSmeListAction action;
    CsrWifiSmeKeyType    keyType;
    CsrWifiMacAddress    peerMacAddress;
} CsrWifiSmeKeyCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeLinkQualityGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request
    linkQuality  - Indicates the quality of the link

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    u16             interfaceTag;
    CsrResult             status;
    CsrWifiSmeLinkQuality linkQuality;
} CsrWifiSmeLinkQualityGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeMediaStatusInd

  DESCRIPTION
    The SME sends this primitive to all the tasks that have registered to
    receive it when a network connection is established, lost or has moved to
    another AP.

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    interfaceTag   - Interface Identifier; unique identifier of an interface
    mediaStatus    - Indicates the media status
    connectionInfo - This parameter is relevant only if the mediaStatus is
                     CSR_WIFI_SME_MEDIA_STATUS_CONNECTED:
                     it points to the connection information for the new network
    disassocReason - This parameter is relevant only if the mediaStatus is
                     CSR_WIFI_SME_MEDIA_STATUS_DISCONNECTED:
                     if a disassociation has occurred it gives the reason of the
                     disassociation
    deauthReason   - This parameter is relevant only if the mediaStatus is
                     CSR_WIFI_SME_MEDIA_STATUS_DISCONNECTED:
                     if a deauthentication has occurred it gives the reason of
                     the deauthentication

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    u16                 interfaceTag;
    CsrWifiSmeMediaStatus     mediaStatus;
    CsrWifiSmeConnectionInfo  connectionInfo;
    CsrWifiSmeIEEE80211Reason disassocReason;
    CsrWifiSmeIEEE80211Reason deauthReason;
} CsrWifiSmeMediaStatusInd;

/*******************************************************************************

  NAME
    CsrWifiSmeMibConfigGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common    - Common header for use with the CsrWifiFsm Module
    status    - Reports the result of the request
    mibConfig - Reports various IEEE 802.11 attributes as currently configured

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent     common;
    CsrResult           status;
    CsrWifiSmeMibConfig mibConfig;
} CsrWifiSmeMibConfigGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeMibConfigSetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeMibConfigSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeMibGetCfm

  DESCRIPTION
    The SME calls this primitive to return the requested MIB variable values.

  MEMBERS
    common             - Common header for use with the CsrWifiFsm Module
    status             - Reports the result of the request
    mibAttributeLength - Length of mibAttribute
    mibAttribute       - Points to the VarBind or VarBindList containing the
                         names and values of the MIB variables requested

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
    u16       mibAttributeLength;
    u8       *mibAttribute;
} CsrWifiSmeMibGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeMibGetNextCfm

  DESCRIPTION
    The SME calls this primitive to return the requested MIB name(s).
    The wireless manager application can then read the value of the MIB
    variable using CSR_WIFI_SME_MIB_GET_REQ, using the names provided.

  MEMBERS
    common             - Common header for use with the CsrWifiFsm Module
    status             - Reports the result of the request
    mibAttributeLength - Length of mibAttribute
    mibAttribute       - Points to a VarBind or VarBindList containing the
                         name(s) of the MIB variable(s) lexicographically
                         following the name(s) given in the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
    u16       mibAttributeLength;
    u8       *mibAttribute;
} CsrWifiSmeMibGetNextCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeMibSetCfm

  DESCRIPTION
    The SME calls the primitive to report the result of the set primitive.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeMibSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeMicFailureInd

  DESCRIPTION
    The SME sends this primitive to all the tasks that have registered to
    receive it whenever the chip firmware reports a MIC failure.

  MEMBERS
    common        - Common header for use with the CsrWifiFsm Module
    interfaceTag  - Interface Identifier; unique identifier of an interface
    secondFailure - TRUE if this indication is for a second failure in 60
                    seconds
    count         - The number of MIC failure events since the connection was
                    established
    address       - MAC address of the transmitter that caused the MIC failure
    keyType       - Type of key for which the failure occurred

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent   common;
    u16         interfaceTag;
    CsrBool           secondFailure;
    u16         count;
    CsrWifiMacAddress address;
    CsrWifiSmeKeyType keyType;
} CsrWifiSmeMicFailureInd;

/*******************************************************************************

  NAME
    CsrWifiSmeMulticastAddressCfm

  DESCRIPTION
    The SME will call this primitive when the operation is complete. For a
    GET action, this primitive reports the current list of MAC addresses.

  MEMBERS
    common            - Common header for use with the CsrWifiFsm Module
    interfaceTag      - Interface Identifier; unique identifier of an interface
    status            - Reports the result of the request
    action            - Action in the request
    getAddressesCount - This parameter is only relevant if action is
                        CSR_WIFI_SME_LIST_ACTION_GET:
                        number of MAC addresses sent with the primitive
    getAddresses      - Pointer to the list of MAC Addresses sent with the
                        primitive, set to NULL if none is sent.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    u16            interfaceTag;
    CsrResult            status;
    CsrWifiSmeListAction action;
    u8             getAddressesCount;
    CsrWifiMacAddress   *getAddresses;
} CsrWifiSmeMulticastAddressCfm;

/*******************************************************************************

  NAME
    CsrWifiSmePacketFilterSetCfm

  DESCRIPTION
    The SME calls the primitive to report the result of the set primitive.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiSmePacketFilterSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmePermanentMacAddressGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common              - Common header for use with the CsrWifiFsm Module
    status              - Reports the result of the request
    permanentMacAddress - MAC address stored in the EEPROM

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent   common;
    CsrResult         status;
    CsrWifiMacAddress permanentMacAddress;
} CsrWifiSmePermanentMacAddressGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmePmkidCandidateListInd

  DESCRIPTION
    The SME will send this primitive to all the tasks that have registered to
    receive it when a new network supporting preauthentication and/or PMK
    caching is seen.

  MEMBERS
    common               - Common header for use with the CsrWifiFsm Module
    interfaceTag         - Interface Identifier; unique identifier of an
                           interface
    pmkidCandidatesCount - Number of PMKID candidates provided
    pmkidCandidates      - Points to the first PMKID candidate

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    u16                 interfaceTag;
    u8                  pmkidCandidatesCount;
    CsrWifiSmePmkidCandidate *pmkidCandidates;
} CsrWifiSmePmkidCandidateListInd;

/*******************************************************************************

  NAME
    CsrWifiSmePmkidCfm

  DESCRIPTION
    The SME will call this primitive when the operation is complete. For a
    GET action, this primitive reports the current list of PMKIDs

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    interfaceTag   - Interface Identifier; unique identifier of an interface
    status         - Reports the result of the request
    action         - Action in the request
    getPmkidsCount - This parameter is only relevant if action is
                     CSR_WIFI_SME_LIST_ACTION_GET:
                     number of PMKIDs sent with the primitive
    getPmkids      - Pointer to the list of PMKIDs sent with the primitive, set
                     to NULL if none is sent.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    u16            interfaceTag;
    CsrResult            status;
    CsrWifiSmeListAction action;
    u8             getPmkidsCount;
    CsrWifiSmePmkid     *getPmkids;
} CsrWifiSmePmkidCfm;

/*******************************************************************************

  NAME
    CsrWifiSmePowerConfigGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common      - Common header for use with the CsrWifiFsm Module
    status      - Reports the result of the request
    powerConfig - Returns the current parameters for the power configuration of
                  the firmware

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    CsrResult             status;
    CsrWifiSmePowerConfig powerConfig;
} CsrWifiSmePowerConfigGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmePowerConfigSetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmePowerConfigSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeRegulatoryDomainInfoGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    status     - Reports the result of the request
    regDomInfo - Reports information and state related to regulatory domain
                 operation.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent                common;
    CsrResult                      status;
    CsrWifiSmeRegulatoryDomainInfo regDomInfo;
} CsrWifiSmeRegulatoryDomainInfoGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeRoamCompleteInd

  DESCRIPTION
    The SME will send this primitive to all the tasks that have registered to
    receive it whenever it completes an attempt to roam to an AP. If the roam
    attempt was successful, status will be set to CSR_WIFI_SME_SUCCESS,
    otherwise it shall be set to the appropriate error code.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the roaming procedure

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiSmeRoamCompleteInd;

/*******************************************************************************

  NAME
    CsrWifiSmeRoamStartInd

  DESCRIPTION
    The SME will send this primitive to all the tasks that have registered to
    receive it whenever it begins an attempt to roam to an AP.
    If the wireless manager application connect request specified the SSID
    and the BSSID was set to the broadcast address (0xFF 0xFF 0xFF 0xFF 0xFF
    0xFF), the SME monitors the signal quality and maintains a list of
    candidates to roam to. When the signal quality of the current connection
    falls below a threshold, and there is a candidate with better quality,
    the SME will attempt to the candidate AP.
    If the roaming procedure succeeds, the SME will also issue a Media
    Connect indication to inform the wireless manager application of the
    change.
    NOTE: to prevent the SME from initiating roaming the WMA must specify the
    BSSID in the connection request; this forces the SME to connect only to
    that AP.
    The wireless manager application can obtain statistics for roaming
    purposes using CSR_WIFI_SME_CONNECTION_QUALITY_IND and
    CSR_WIFI_SME_CONNECTION_STATS_GET_REQ.
    When the wireless manager application wishes to roam to another AP, it
    must issue a connection request specifying the BSSID of the desired AP.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    roamReason   - Indicates the reason for starting the roaming procedure
    reason80211  - Indicates the reason for deauthentication or disassociation

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    u16                 interfaceTag;
    CsrWifiSmeRoamReason      roamReason;
    CsrWifiSmeIEEE80211Reason reason80211;
} CsrWifiSmeRoamStartInd;

/*******************************************************************************

  NAME
    CsrWifiSmeRoamingConfigGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common        - Common header for use with the CsrWifiFsm Module
    interfaceTag  - Interface Identifier; unique identifier of an interface
    status        - Reports the result of the request
    roamingConfig - Reports the roaming behaviour of the driver and firmware

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent         common;
    u16               interfaceTag;
    CsrResult               status;
    CsrWifiSmeRoamingConfig roamingConfig;
} CsrWifiSmeRoamingConfigGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeRoamingConfigSetCfm

  DESCRIPTION
    This primitive sets the value of the RoamingConfig parameter.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiSmeRoamingConfigSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeScanConfigGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    status     - Reports the result of the request
    scanConfig - Returns the current parameters for the autonomous scanning
                 behaviour of the firmware

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    CsrResult            status;
    CsrWifiSmeScanConfig scanConfig;
} CsrWifiSmeScanConfigGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeScanConfigSetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeScanConfigSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeScanFullCfm

  DESCRIPTION
    The SME calls this primitive when the results from the scan are
    available.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeScanFullCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeScanResultInd

  DESCRIPTION
    The SME sends this primitive to all the tasks that have registered to
    receive it whenever a scan indication is received from the firmware.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    result - Points to a buffer containing a scan result.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent      common;
    CsrWifiSmeScanResult result;
} CsrWifiSmeScanResultInd;

/*******************************************************************************

  NAME
    CsrWifiSmeScanResultsFlushCfm

  DESCRIPTION
    The SME will call this primitive when the cache has been cleared.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeScanResultsFlushCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeScanResultsGetCfm

  DESCRIPTION
    The SME sends this primitive to provide the current set of scan results.

  MEMBERS
    common           - Common header for use with the CsrWifiFsm Module
    status           - Reports the result of the request
    scanResultsCount - Number of scan results
    scanResults      - Points to a buffer containing an array of
                       CsrWifiSmeScanResult structures.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    CsrResult             status;
    u16             scanResultsCount;
    CsrWifiSmeScanResult *scanResults;
} CsrWifiSmeScanResultsGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeSmeStaConfigGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request
    smeConfig    - Current SME Station Parameters

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent     common;
    u16           interfaceTag;
    CsrResult           status;
    CsrWifiSmeStaConfig smeConfig;
} CsrWifiSmeSmeStaConfigGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeSmeStaConfigSetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiSmeSmeStaConfigSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeStationMacAddressGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common            - Common header for use with the CsrWifiFsm Module
    status            - Reports the result of the request
    stationMacAddress - Current MAC address of the station.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent   common;
    CsrResult         status;
    CsrWifiMacAddress stationMacAddress[2];
} CsrWifiSmeStationMacAddressGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeTspecInd

  DESCRIPTION
    The SME will send this primitive to all the task that have registered to
    receive it when a status change in the TSPEC occurs.

  MEMBERS
    common          - Common header for use with the CsrWifiFsm Module
    interfaceTag    - Interface Identifier; unique identifier of an interface
    transactionId   - Unique Transaction ID for the TSPEC, as assigned by the
                      driver
    tspecResultCode - Specifies the TSPEC operation requested by the peer
                      station
    tspecLength     - Length of the TSPEC.
    tspec           - Points to the first byte of the TSPEC

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    u16                 interfaceTag;
    CsrUint32                 transactionId;
    CsrWifiSmeTspecResultCode tspecResultCode;
    u16                 tspecLength;
    u8                 *tspec;
} CsrWifiSmeTspecInd;

/*******************************************************************************

  NAME
    CsrWifiSmeTspecCfm

  DESCRIPTION
    The SME calls the primitive to report the result of the TSpec primitive
    request.

  MEMBERS
    common          - Common header for use with the CsrWifiFsm Module
    interfaceTag    - Interface Identifier; unique identifier of an interface
    status          - Reports the result of the request
    transactionId   - Unique Transaction ID for the TSPEC, as assigned by the
                      driver
    tspecResultCode - Specifies the result of the negotiated TSPEC operation
    tspecLength     - Length of the TSPEC.
    tspec           - Points to the first byte of the TSPEC

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    u16                 interfaceTag;
    CsrResult                 status;
    CsrUint32                 transactionId;
    CsrWifiSmeTspecResultCode tspecResultCode;
    u16                 tspecLength;
    u8                 *tspec;
} CsrWifiSmeTspecCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeVersionsGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common   - Common header for use with the CsrWifiFsm Module
    status   - Reports the result of the request
    versions - Version IDs of the product

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent    common;
    CsrResult          status;
    CsrWifiSmeVersions versions;
} CsrWifiSmeVersionsGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeWifiFlightmodeCfm

  DESCRIPTION
    The SME calls this primitive when the chip is initialised for low power
    mode and with the radio subsystem disabled. To leave flight mode, and
    enable Wi-Fi, the wireless manager application should call
    CSR_WIFI_SME_WIFI_ON_REQ.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeWifiFlightmodeCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeWifiOffInd

  DESCRIPTION
    The SME sends this primitive to all the tasks that have registered to
    receive it to report that the chip has been turned off.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    reason - Indicates the reason why the Wi-Fi has been switched off.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent             common;
    CsrWifiSmeControlIndication reason;
} CsrWifiSmeWifiOffInd;

/*******************************************************************************

  NAME
    CsrWifiSmeWifiOffCfm

  DESCRIPTION
    After receiving CSR_WIFI_SME_WIFI_OFF_REQ, if the chip is connected to a
    network, the SME will perform a disconnect operation, will send a
    CSR_WIFI_SME_MEDIA_STATUS_IND with
    CSR_WIFI_SME_MEDIA_STATUS_DISCONNECTED, and then will call
    CSR_WIFI_SME_WIFI_OFF_CFM when the chip is off.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeWifiOffCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeWifiOnCfm

  DESCRIPTION
    The SME sends this primitive to the task that has sent the request once
    the chip has been initialised and is available for use.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeWifiOnCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeCloakedSsidsSetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeCloakedSsidsSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeCloakedSsidsGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    status       - Reports the result of the request
    cloakedSsids - Reports list of cloaked SSIDs that are explicitly scanned for
                   by the driver

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent             common;
    CsrResult                   status;
    CsrWifiSmeCloakedSsidConfig cloakedSsids;
} CsrWifiSmeCloakedSsidsGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeWifiOnInd

  DESCRIPTION
    The SME sends this primitive to all tasks that have registered to receive
    it once the chip becomes available and ready to use.

  MEMBERS
    common  - Common header for use with the CsrWifiFsm Module
    address - Current MAC address

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent   common;
    CsrWifiMacAddress address;
} CsrWifiSmeWifiOnInd;

/*******************************************************************************

  NAME
    CsrWifiSmeSmeCommonConfigGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    status       - Reports the result of the request
    deviceConfig - Configuration options in the SME

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent        common;
    CsrResult              status;
    CsrWifiSmeDeviceConfig deviceConfig;
} CsrWifiSmeSmeCommonConfigGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeSmeCommonConfigSetCfm

  DESCRIPTION
    Reports the result of the request

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Reports the result of the request

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiSmeSmeCommonConfigSetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeInterfaceCapabilityGetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common        - Common header for use with the CsrWifiFsm Module
    status        - Result of the request
    numInterfaces - Number of the interfaces supported
    capBitmap     - Points to the list of capabilities bitmaps provided for each
                    interface.
                    The bits represent the following capabilities:
                    -bits 7 to 4-Reserved
                    -bit 3-AMP
                    -bit 2-P2P
                    -bit 1-AP
                    -bit 0-STA

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
    u16       numInterfaces;
    u8        capBitmap[2];
} CsrWifiSmeInterfaceCapabilityGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeErrorInd

  DESCRIPTION
    Important error message indicating a error of some importance

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    errorMessage - Contains the error message.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrCharString  *errorMessage;
} CsrWifiSmeErrorInd;

/*******************************************************************************

  NAME
    CsrWifiSmeInfoInd

  DESCRIPTION
    Message indicating a some info about current activity. Mostly of interest
    in testing but may be useful in the field.

  MEMBERS
    common      - Common header for use with the CsrWifiFsm Module
    infoMessage - Contains the message.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrCharString  *infoMessage;
} CsrWifiSmeInfoInd;

/*******************************************************************************

  NAME
    CsrWifiSmeCoreDumpInd

  DESCRIPTION
    The SME will send this primitive to all the tasks that have registered to
    receive Wi-Fi Chip core dump data.
    The core dump data may be fragmented and sent using more than one
    indication.
    To indicate that all the data has been sent, the last indication contains
    a 'length' of 0 and 'data' of NULL.

  MEMBERS
    common     - Common header for use with the CsrWifiFsm Module
    dataLength - Number of bytes in the buffer pointed to by 'data'
    data       - Pointer to the buffer containing 'dataLength' bytes of core
                 dump data

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrUint32       dataLength;
    u8       *data;
} CsrWifiSmeCoreDumpInd;

/*******************************************************************************

  NAME
    CsrWifiSmeAmpStatusChangeInd

  DESCRIPTION
    Indication of change to AMP activity.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface on which the AMP activity changed.
    ampStatus    - The new status of AMP activity.Range: {AMP_ACTIVE,
                   AMP_INACTIVE}.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent     common;
    u16           interfaceTag;
    CsrWifiSmeAmpStatus ampStatus;
} CsrWifiSmeAmpStatusChangeInd;

/*******************************************************************************

  NAME
    CsrWifiSmeWpsConfigurationCfm

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
} CsrWifiSmeWpsConfigurationCfm;


#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_SME_PRIM_H__ */

