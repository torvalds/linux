/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_SME_AP_PRIM_H__
#define CSR_WIFI_SME_AP_PRIM_H__

#include "csr_prim_defs.h"
#include "csr_sched.h"
#include "csr_wifi_common.h"
#include "csr_result.h"
#include "csr_wifi_fsm_event.h"
#include "csr_wifi_sme_prim.h"

#ifndef CSR_WIFI_AP_ENABLE
#error CSR_WIFI_AP_ENABLE MUST be defined inorder to use csr_wifi_sme_ap_prim.h
#endif

#define CSR_WIFI_SME_AP_PRIM                                            (0x0407)

typedef CsrPrim CsrWifiSmeApPrim;


/*******************************************************************************

  NAME
    CsrWifiSmeApAccessType

  DESCRIPTION
    Allow or deny STAs based on MAC address

 VALUES
    CSR_WIFI_AP_ACCESS_TYPE_NONE  - None
    CSR_WIFI_AP_ACCESS_TYPE_ALLOW - Allow only if MAC address is from the list
    CSR_WIFI_AP_ACCESS_TYPE_DENY  - Disallow if MAC address is from the list

*******************************************************************************/
typedef u8 CsrWifiSmeApAccessType;
#define CSR_WIFI_AP_ACCESS_TYPE_NONE    ((CsrWifiSmeApAccessType) 0x00)
#define CSR_WIFI_AP_ACCESS_TYPE_ALLOW   ((CsrWifiSmeApAccessType) 0x01)
#define CSR_WIFI_AP_ACCESS_TYPE_DENY    ((CsrWifiSmeApAccessType) 0x02)

/*******************************************************************************

  NAME
    CsrWifiSmeApAuthSupport

  DESCRIPTION
    Define bits for AP authentication support

 VALUES
    CSR_WIFI_SME_RSN_AUTH_WPAPSK  - RSN WPA-PSK Support
    CSR_WIFI_SME_RSN_AUTH_WPA2PSK - RSN WPA2-PSK Support
    CSR_WIFI_SME_AUTH_WAPIPSK     - WAPI-PSK Support

*******************************************************************************/
typedef u8 CsrWifiSmeApAuthSupport;
#define CSR_WIFI_SME_RSN_AUTH_WPAPSK    ((CsrWifiSmeApAuthSupport) 0x01)
#define CSR_WIFI_SME_RSN_AUTH_WPA2PSK   ((CsrWifiSmeApAuthSupport) 0x02)
#define CSR_WIFI_SME_AUTH_WAPIPSK       ((CsrWifiSmeApAuthSupport) 0x04)

/*******************************************************************************

  NAME
    CsrWifiSmeApAuthType

  DESCRIPTION
    Definition of the SME AP Authentication Options

 VALUES
    CSR_WIFI_SME_AP_AUTH_TYPE_OPEN_SYSTEM
                   - Open  authentication
    CSR_WIFI_SME_AP_AUTH_TYPE_PERSONAL
                   - Personal authentication using a passphrase or a pre-shared
                     key.
    CSR_WIFI_SME_AP_AUTH_TYPE_WEP
                   - WEP authentication. This can be either open or shared key

*******************************************************************************/
typedef u8 CsrWifiSmeApAuthType;
#define CSR_WIFI_SME_AP_AUTH_TYPE_OPEN_SYSTEM   ((CsrWifiSmeApAuthType) 0x00)
#define CSR_WIFI_SME_AP_AUTH_TYPE_PERSONAL      ((CsrWifiSmeApAuthType) 0x01)
#define CSR_WIFI_SME_AP_AUTH_TYPE_WEP           ((CsrWifiSmeApAuthType) 0x02)

/*******************************************************************************

  NAME
    CsrWifiSmeApDirection

  DESCRIPTION
    Definition of Direction

 VALUES
    CSR_WIFI_AP_DIRECTION_RECEIPIENT - Receipient
    CSR_WIFI_AP_DIRECTION_ORIGINATOR - Originator

*******************************************************************************/
typedef u8 CsrWifiSmeApDirection;
#define CSR_WIFI_AP_DIRECTION_RECEIPIENT   ((CsrWifiSmeApDirection) 0x00)
#define CSR_WIFI_AP_DIRECTION_ORIGINATOR   ((CsrWifiSmeApDirection) 0x01)

/*******************************************************************************

  NAME
    CsrWifiSmeApPhySupport

  DESCRIPTION
    Define bits for CsrWifiSmeApPhySupportMask

 VALUES
    CSR_WIFI_SME_AP_PHY_SUPPORT_A - 802.11a. It is not supported in the current
                                    release.
    CSR_WIFI_SME_AP_PHY_SUPPORT_B - 802.11b
    CSR_WIFI_SME_AP_PHY_SUPPORT_G - 802.11g
    CSR_WIFI_SME_AP_PHY_SUPPORT_N - 802.11n

*******************************************************************************/
typedef u8 CsrWifiSmeApPhySupport;
#define CSR_WIFI_SME_AP_PHY_SUPPORT_A   ((CsrWifiSmeApPhySupport) 0x01)
#define CSR_WIFI_SME_AP_PHY_SUPPORT_B   ((CsrWifiSmeApPhySupport) 0x02)
#define CSR_WIFI_SME_AP_PHY_SUPPORT_G   ((CsrWifiSmeApPhySupport) 0x04)
#define CSR_WIFI_SME_AP_PHY_SUPPORT_N   ((CsrWifiSmeApPhySupport) 0x08)

/*******************************************************************************

  NAME
    CsrWifiSmeApType

  DESCRIPTION
    Definition of AP types

 VALUES
    CSR_WIFI_AP_TYPE_LEGACY - Legacy AP
    CSR_WIFI_AP_TYPE_P2P    - P2P Group Owner(GO)

*******************************************************************************/
typedef u8 CsrWifiSmeApType;
#define CSR_WIFI_AP_TYPE_LEGACY   ((CsrWifiSmeApType) 0x00)
#define CSR_WIFI_AP_TYPE_P2P      ((CsrWifiSmeApType) 0x01)


/*******************************************************************************

  NAME
    CsrWifiSmeApAuthSupportMask

  DESCRIPTION
    See CsrWifiSmeApAuthSupport for bit definitions

*******************************************************************************/
typedef u8 CsrWifiSmeApAuthSupportMask;
/*******************************************************************************

  NAME
    CsrWifiSmeApPhySupportMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiSmeApPhySupport

*******************************************************************************/
typedef u8 CsrWifiSmeApPhySupportMask;
/*******************************************************************************

  NAME
    CsrWifiSmeApRsnCapabilities

  DESCRIPTION
    Set to 0 for the current release

*******************************************************************************/
typedef u16 CsrWifiSmeApRsnCapabilities;
/*******************************************************************************

  NAME
    CsrWifiSmeApRsnCapabilitiesMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiSmeApRsnCapabilities

*******************************************************************************/
typedef u16 CsrWifiSmeApRsnCapabilitiesMask;
/*******************************************************************************

  NAME
    CsrWifiSmeApWapiCapabilities

  DESCRIPTION
    Ignored by the stack as WAPI is not supported for AP operations in the
    current release

*******************************************************************************/
typedef u16 CsrWifiSmeApWapiCapabilities;
/*******************************************************************************

  NAME
    CsrWifiSmeApWapiCapabilitiesMask

  DESCRIPTION
    Mask type for use with the values defined by CsrWifiSmeApWapiCapabilities

*******************************************************************************/
typedef u16 CsrWifiSmeApWapiCapabilitiesMask;


/*******************************************************************************

  NAME
    CsrWifiSmeApHtParams

  DESCRIPTION
    Structure holding HT parameters

  MEMBERS
    greenfieldSupported - Indicates if the AP supports Htgreenfield operation
                          subject to the chip capability. If the chip does not
                          support Htgreenfield operation, this parameter will be
                          ignored.
                          NOTE: if shortGi20MHz is set to TRUE and the chip
                          supports short GI operation for 20MHz this field will
                          be be ignored and the AP will not support Htgreenfield
                          operation.
                          NOTE: This field is ignored by the Wi-Fi stack for the
                          current release. It implies that AP does not support
                          greenfield operation.
    shortGi20MHz        - Indicates if the AP support short GI operation for
                          20MHz subject to the chip capability.If the chip does
                          not support short GI for 20MHz, this parameter is
                          ignored
    rxStbc              - Support for STBC for receive. 0 => No support for STBC
                          , 1=> Use STBC for Rx
    rifsModeAllowed     - RIFS Mode is allowed to protect overlapping non-HT BSS
    htProtection        - Deprecated
    dualCtsProtection   - Dual CTS Protection enabled

*******************************************************************************/
typedef struct
{
    u8  greenfieldSupported;
    u8  shortGi20MHz;
    u8 rxStbc;
    u8  rifsModeAllowed;
    u8 htProtection;
    u8  dualCtsProtection;
} CsrWifiSmeApHtParams;

/*******************************************************************************

  NAME
    CsrWifiSmeApP2pOperatingChanEntry

  DESCRIPTION

  MEMBERS
    operatingClass        - Channel operating class
    operatingChannelCount - Number of channels in this entry
    operatingChannel      - List of channels

*******************************************************************************/
typedef struct
{
    u8  operatingClass;
    u8  operatingChannelCount;
    u8 *operatingChannel;
} CsrWifiSmeApP2pOperatingChanEntry;

/*******************************************************************************

  NAME
    CsrWifiSmeApP2pOperatingChanList

  DESCRIPTION
    This structure contains the lists of P2P operating channels

  MEMBERS
    country               - Country
    channelEntryListCount - Number of entries
    channelEntryList      - List of entries

*******************************************************************************/
typedef struct
{
    u8                           country[3];
    u8                           channelEntryListCount;
    CsrWifiSmeApP2pOperatingChanEntry *channelEntryList;
} CsrWifiSmeApP2pOperatingChanList;

/*******************************************************************************

  NAME
    CsrWifiSmeApAuthPers

  DESCRIPTION

  MEMBERS
    authSupport        -
    encryptionModeMask -
    rsnCapabilities    -
    wapiCapabilities   -

*******************************************************************************/
typedef struct
{
    CsrWifiSmeApAuthSupportMask      authSupport;
    CsrWifiSmeEncryptionMask         encryptionModeMask;
    CsrWifiSmeApRsnCapabilitiesMask  rsnCapabilities;
    CsrWifiSmeApWapiCapabilitiesMask wapiCapabilities;
} CsrWifiSmeApAuthPers;

/*******************************************************************************

  NAME
    CsrWifiSmeApBaSession

  DESCRIPTION

  MEMBERS
    peerMacAddress - Indicates MAC address of the peer station
    tid            - Specifies the TID of the MSDUs for which this Block Ack has
                     been set up. Range: 0-15
    direction      - Specifies if the AP is the originator or the recipient of
                     the data stream that uses the Block Ack.

*******************************************************************************/
typedef struct
{
    CsrWifiMacAddress     peerMacAddress;
    u8              tid;
    CsrWifiSmeApDirection direction;
} CsrWifiSmeApBaSession;

/*******************************************************************************

  NAME
    CsrWifiSmeApMacConfig

  DESCRIPTION
    Structure holding AP MAC configuration.

  MEMBERS
    phySupportedBitmap   - Indicates supported physical layers
    beaconInterval       - Beacon interval in terms of TUs
    dtimPeriod           - DTIM period in terms of number of beacon intervals
    maxListenInterval    - Maximum allowed listen interval as number of beacon
                           intervals
    supportedRatesCount  - Number of supported rates. Range : 0  to 20
    supportedRates       - List of supportedRates. A rate is specied in the
                           units of 500kbps. An entry for a basic rate shall
                           have the MSB set to 1.
    preamble             - Preamble to be advertised in beacons and probe
                           responses
    shortSlotTimeEnabled - TRUE indicates the AP shall use short slot time if
                           all the stations use short slot operation.
    ctsProtectionType    - CTS protection to be used
    wmmEnabled           - Indicate whether WMM is enabled or not. If set to
                           FALSE,the WMM parameters shall be ignored by the
                           receiver.
    wmmApParams          - WMM parameters to be used for local firmware queue
                           configuration. Array index corresponds to the ACI.
    wmmApBcParams        - WMM parameters to be advertised in beacon/probe
                           response. Array index corresponds to the ACI
    accessType           - Specifies whether the MAC addresses from the list
                           should be allowed or denied
    macAddressListCount  - Number of MAC addresses
    macAddressList       - List of MAC addresses
    apHtParams           - AP HT parameters. The stack shall use these
                           parameters only if phySupportedBitmap indicates
                           support for IEEE 802.11n

*******************************************************************************/
typedef struct
{
    CsrWifiSmeApPhySupportMask  phySupportedBitmap;
    u16                   beaconInterval;
    u8                    dtimPeriod;
    u16                   maxListenInterval;
    u8                    supportedRatesCount;
    u8                    supportedRates[20];
    CsrWifiSmePreambleType      preamble;
    u8                     shortSlotTimeEnabled;
    CsrWifiSmeCtsProtectionType ctsProtectionType;
    u8                     wmmEnabled;
    CsrWifiSmeWmmAcParams       wmmApParams[4];
    CsrWifiSmeWmmAcParams       wmmApBcParams[4];
    CsrWifiSmeApAccessType      accessType;
    u8                    macAddressListCount;
    CsrWifiMacAddress          *macAddressList;
    CsrWifiSmeApHtParams        apHtParams;
} CsrWifiSmeApMacConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeApP2pGoConfig

  DESCRIPTION

  MEMBERS
    groupCapability           - Indicates the P2P group capabilities
    operatingChanList         - List of operating channels in the order of
                                decreasing priority. It may contain channel
                                entry/entries not supported by the wifi stack.
                                These shall be filtered out by the wifi stack
    opPsEnabled               - Indicates whether opportunistic power save can
                                be used.
                                Note: This parameter is ignored by the WiFi
                                stack for the current release
    ctWindow                  - Define Client Traffic window to be used in terms
                                of number of TUs. Range: 0 to 127.
                                Note: This parameter is ignored by the WiFi
                                stack for the current release.
    noaConfigMethod           - Notice of Absence configuration method.
                                Note: This parameter is ignored by the WiFi
                                stack for the current release.
    allowNoaWithNonP2pDevices - Indicates if NOA should be allowed if non P2P
                                devices are connected. If allowed the non P2P
                                devices may suffer in throughput.
                                Note: This parameter is ignored by the WiFi
                                stack for the current release.

*******************************************************************************/
typedef struct
{
    CsrWifiSmeP2pGroupCapabilityMask groupCapability;
    CsrWifiSmeApP2pOperatingChanList operatingChanList;
    u8                          opPsEnabled;
    u8                         ctWindow;
    CsrWifiSmeP2pNoaConfigMethod     noaConfigMethod;
    u8                          allowNoaWithNonP2pDevices;
} CsrWifiSmeApP2pGoConfig;

/*******************************************************************************

  NAME
    CsrWifiSmeApCredentials

  DESCRIPTION

  MEMBERS
    authType                    -
    smeAuthType                 -
    smeAuthTypeopenSystemEmpty  -
    smeAuthTypeauthwep          -
    smeAuthTypeauthPers         -

*******************************************************************************/
typedef struct
{
    CsrWifiSmeApAuthType authType;
    union {
        CsrWifiSmeEmpty      openSystemEmpty;
        CsrWifiSmeWepAuth    authwep;
        CsrWifiSmeApAuthPers authPers;
    } smeAuthType;
} CsrWifiSmeApCredentials;

/*******************************************************************************

  NAME
    CsrWifiSmeApSecConfig

  DESCRIPTION

  MEMBERS
    apCredentials -
    wpsEnabled    -

*******************************************************************************/
typedef struct
{
    CsrWifiSmeApCredentials apCredentials;
    u8                 wpsEnabled;
} CsrWifiSmeApSecConfig;


/* Downstream */
#define CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_LOWEST            (0x0000)

#define CSR_WIFI_SME_AP_BEACONING_START_REQ               ((CsrWifiSmeApPrim) (0x0000 + CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_BEACONING_STOP_REQ                ((CsrWifiSmeApPrim) (0x0001 + CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_WPS_REGISTRATION_STARTED_REQ      ((CsrWifiSmeApPrim) (0x0002 + CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_WPS_REGISTRATION_FINISHED_REQ     ((CsrWifiSmeApPrim) (0x0003 + CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_WMM_PARAM_UPDATE_REQ              ((CsrWifiSmeApPrim) (0x0004 + CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_STA_DISCONNECT_REQ                ((CsrWifiSmeApPrim) (0x0005 + CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_WPS_CONFIGURATION_REQ             ((CsrWifiSmeApPrim) (0x0006 + CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_ACTIVE_BA_GET_REQ                 ((CsrWifiSmeApPrim) (0x0007 + CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_BA_DELETE_REQ                     ((CsrWifiSmeApPrim) (0x0008 + CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_LOWEST))


#define CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_HIGHEST           (0x0008 + CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_LOWEST)

/* Upstream */
#define CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST              (0x0000 + CSR_PRIM_UPSTREAM)

#define CSR_WIFI_SME_AP_BEACONING_START_CFM               ((CsrWifiSmeApPrim)(0x0000 + CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_BEACONING_STOP_CFM                ((CsrWifiSmeApPrim)(0x0001 + CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_STA_NOTIFY_IND                    ((CsrWifiSmeApPrim)(0x0002 + CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_STA_CONNECT_START_IND             ((CsrWifiSmeApPrim)(0x0003 + CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_WPS_REGISTRATION_STARTED_CFM      ((CsrWifiSmeApPrim)(0x0004 + CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_WPS_REGISTRATION_FINISHED_CFM     ((CsrWifiSmeApPrim)(0x0005 + CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_WMM_PARAM_UPDATE_CFM              ((CsrWifiSmeApPrim)(0x0006 + CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_STA_DISCONNECT_CFM                ((CsrWifiSmeApPrim)(0x0007 + CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_WPS_CONFIGURATION_CFM             ((CsrWifiSmeApPrim)(0x0008 + CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_ERROR_IND                         ((CsrWifiSmeApPrim)(0x0009 + CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_ACTIVE_BA_GET_CFM                 ((CsrWifiSmeApPrim)(0x000A + CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_SME_AP_BA_DELETE_CFM                     ((CsrWifiSmeApPrim)(0x000B + CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST))

#define CSR_WIFI_SME_AP_PRIM_UPSTREAM_HIGHEST             (0x000B + CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST)

#define CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_COUNT             (CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_HIGHEST + 1 - CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_LOWEST)
#define CSR_WIFI_SME_AP_PRIM_UPSTREAM_COUNT               (CSR_WIFI_SME_AP_PRIM_UPSTREAM_HIGHEST   + 1 - CSR_WIFI_SME_AP_PRIM_UPSTREAM_LOWEST)

/*******************************************************************************

  NAME
    CsrWifiSmeApBeaconingStartReq

  DESCRIPTION
    This primitive requests the SME to start AP or GO functionality

  MEMBERS
    common          - Common header for use with the CsrWifiFsm Module
    interfaceTag    -
    initialPresence - Set to 0, if Not in a group fomration phase, set to 1 ,
                      during group formation phase
    apType          - apType : Legacy AP or P2PGO
    cloakSsid       - cloakSsid flag.
    ssid            - ssid.
    ifIndex         - Radio Interface
    channel         - channel.
    maxConnections  - Maximum Stations + P2PClients allowed
    apCredentials   - AP security credeitals used to advertise in beacon /probe
                      response
    smeApConfig     - AP configuration
    p2pGoParam      - P2P specific GO parameters. Ignored if it is a leagacy AP

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent         common;
    u16               interfaceTag;
    u8                initialPresence;
    CsrWifiSmeApType        apType;
    u8                 cloakSsid;
    CsrWifiSsid             ssid;
    CsrWifiSmeRadioIF       ifIndex;
    u8                channel;
    u8                maxConnections;
    CsrWifiSmeApSecConfig   apCredentials;
    CsrWifiSmeApMacConfig   smeApConfig;
    CsrWifiSmeApP2pGoConfig p2pGoParam;
} CsrWifiSmeApBeaconingStartReq;

/*******************************************************************************

  NAME
    CsrWifiSmeApBeaconingStopReq

  DESCRIPTION
    This primitive requests the SME to STOP AP or P2PGO operation

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiSmeApBeaconingStopReq;

/*******************************************************************************

  NAME
    CsrWifiSmeApWpsRegistrationStartedReq

  DESCRIPTION
    This primitive tells SME that WPS registration procedure has started

  MEMBERS
    common                   - Common header for use with the CsrWifiFsm Module
    interfaceTag             -
    SelectedDevicePasswordId -
    SelectedconfigMethod     -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent         common;
    u16               interfaceTag;
    CsrWifiSmeWpsDpid       SelectedDevicePasswordId;
    CsrWifiSmeWpsConfigType SelectedconfigMethod;
} CsrWifiSmeApWpsRegistrationStartedReq;

/*******************************************************************************

  NAME
    CsrWifiSmeApWpsRegistrationFinishedReq

  DESCRIPTION
    This primitive tells SME that WPS registration procedure has finished

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiSmeApWpsRegistrationFinishedReq;

/*******************************************************************************

  NAME
    CsrWifiSmeApWmmParamUpdateReq

  DESCRIPTION
    Application uses this primitive to update the WMM parameters on the fly

  MEMBERS
    common        - Common header for use with the CsrWifiFsm Module
    interfaceTag  -
    wmmApParams   - WMM parameters to be used for local firmware queue
                    configuration
    wmmApBcParams - WMM parameters to be advertised in beacon/probe response

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    u16             interfaceTag;
    CsrWifiSmeWmmAcParams wmmApParams[4];
    CsrWifiSmeWmmAcParams wmmApBcParams[4];
} CsrWifiSmeApWmmParamUpdateReq;

/*******************************************************************************

  NAME
    CsrWifiSmeApStaDisconnectReq

  DESCRIPTION
    This primitive tells SME to deauth ot disassociate a particular station
    within BSS

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    interfaceTag   -
    deauthReason   -
    disassocReason -
    peerMacaddress -
    keepBlocking   - If TRUE, the station is blocked. If FALSE and the station
                     is connected, disconnect the station. If FALSE and the
                     station is not connected, no action is taken.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    u16                 interfaceTag;
    CsrWifiSmeIEEE80211Reason deauthReason;
    CsrWifiSmeIEEE80211Reason disassocReason;
    CsrWifiMacAddress         peerMacaddress;
    u8                   keepBlocking;
} CsrWifiSmeApStaDisconnectReq;

/*******************************************************************************

  NAME
    CsrWifiSmeApWpsConfigurationReq

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
} CsrWifiSmeApWpsConfigurationReq;

/*******************************************************************************

  NAME
    CsrWifiSmeApActiveBaGetReq

  DESCRIPTION
    This primitive used to retrieve information related to the active block
    ack sessions

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiSmeApActiveBaGetReq;

/*******************************************************************************

  NAME
    CsrWifiSmeApBaDeleteReq

  DESCRIPTION
    This primitive is used to delete an active block ack session

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    reason       -
    baSession    - BA session to be deleted

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    u16                 interfaceTag;
    CsrWifiSmeIEEE80211Reason reason;
    CsrWifiSmeApBaSession     baSession;
} CsrWifiSmeApBaDeleteReq;

/*******************************************************************************

  NAME
    CsrWifiSmeApBeaconingStartCfm

  DESCRIPTION
    This primitive confirms the completion of the request along with the
    status

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    status       -
    secIeLength  -
    secIe        -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
    u16       secIeLength;
    u8       *secIe;
} CsrWifiSmeApBeaconingStartCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeApBeaconingStopCfm

  DESCRIPTION
    This primitive confirms AP or P2PGO operation is terminated

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    status       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiSmeApBeaconingStopCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeApStaNotifyInd

  DESCRIPTION
    This primitive indicates that a station has joined or a previously joined
    station has left the BSS/group

  MEMBERS
    common            - Common header for use with the CsrWifiFsm Module
    interfaceTag      -
    mediaStatus       -
    peerMacAddress    -
    peerDeviceAddress -
    disassocReason    -
    deauthReason      -
    WpsRegistration   -
    secIeLength       -
    secIe             -
    groupKeyId        -
    seqNumber         -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent           common;
    u16                 interfaceTag;
    CsrWifiSmeMediaStatus     mediaStatus;
    CsrWifiMacAddress         peerMacAddress;
    CsrWifiMacAddress         peerDeviceAddress;
    CsrWifiSmeIEEE80211Reason disassocReason;
    CsrWifiSmeIEEE80211Reason deauthReason;
    CsrWifiSmeWpsRegistration WpsRegistration;
    u8                  secIeLength;
    u8                 *secIe;
    u8                  groupKeyId;
    u16                 seqNumber[8];
} CsrWifiSmeApStaNotifyInd;

/*******************************************************************************

  NAME
    CsrWifiSmeApStaConnectStartInd

  DESCRIPTION
    This primitive indicates that a stations request to join the group/BSS is
    accepted

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    interfaceTag   -
    peerMacAddress -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent   common;
    u16         interfaceTag;
    CsrWifiMacAddress peerMacAddress;
} CsrWifiSmeApStaConnectStartInd;

/*******************************************************************************

  NAME
    CsrWifiSmeApWpsRegistrationStartedCfm

  DESCRIPTION
    A confirm for UNIFI_MGT_AP_WPS_REGISTRATION_STARTED.request

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    status       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiSmeApWpsRegistrationStartedCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeApWpsRegistrationFinishedCfm

  DESCRIPTION
    A confirm for UNIFI_MGT_AP_WPS_REGISTRATION_FINISHED.request

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    status       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiSmeApWpsRegistrationFinishedCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeApWmmParamUpdateCfm

  DESCRIPTION
    A confirm for CSR_WIFI_SME_AP_WMM_PARAM_UPDATE.request

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    status       -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiSmeApWmmParamUpdateCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeApStaDisconnectCfm

  DESCRIPTION
    This primitive confirms the station is disconnected

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    interfaceTag   -
    status         -
    peerMacaddress -

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent   common;
    u16         interfaceTag;
    CsrResult         status;
    CsrWifiMacAddress peerMacaddress;
} CsrWifiSmeApStaDisconnectCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeApWpsConfigurationCfm

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
} CsrWifiSmeApWpsConfigurationCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeApErrorInd

  DESCRIPTION
    This primitve is sent by SME to indicate some error in AP operationi
    after AP operations were started successfully and continuing the AP
    operation may lead to undesired behaviour. It is the responsibility of
    the upper layers to stop AP operation if needed

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Range 0-1
    apType       -
    status       - Contains the error status

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent  common;
    u16        interfaceTag;
    CsrWifiSmeApType apType;
    CsrResult        status;
} CsrWifiSmeApErrorInd;

/*******************************************************************************

  NAME
    CsrWifiSmeApActiveBaGetCfm

  DESCRIPTION
    This primitive carries the information related to the active ba sessions

  MEMBERS
    common           - Common header for use with the CsrWifiFsm Module
    interfaceTag     -
    status           - Reports the result of the request
    activeBaCount    - Number of active block ack session
    activeBaSessions - Points to a buffer containing an array of
                       CsrWifiSmeApBaSession structures.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent        common;
    u16              interfaceTag;
    CsrResult              status;
    u16              activeBaCount;
    CsrWifiSmeApBaSession *activeBaSessions;
} CsrWifiSmeApActiveBaGetCfm;

/*******************************************************************************

  NAME
    CsrWifiSmeApBaDeleteCfm

  DESCRIPTION
    This primitive confirms the BA is deleted

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag -
    status       - Reports the result of the request
    baSession    - deleted BA session

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    u16             interfaceTag;
    CsrResult             status;
    CsrWifiSmeApBaSession baSession;
} CsrWifiSmeApBaDeleteCfm;


#endif /* CSR_WIFI_SME_AP_PRIM_H__ */

