/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_NME_AP_PRIM_H__
#define CSR_WIFI_NME_AP_PRIM_H__

#include "csr_types.h"
#include "csr_prim_defs.h"
#include "csr_sched.h"
#include "csr_wifi_common.h"
#include "csr_result.h"
#include "csr_wifi_fsm_event.h"
#include "csr_wifi_sme_ap_prim.h"
#include "csr_wifi_nme_prim.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CSR_WIFI_NME_ENABLE
#error CSR_WIFI_NME_ENABLE MUST be defined inorder to use csr_wifi_nme_ap_prim.h
#endif
#ifndef CSR_WIFI_AP_ENABLE
#error CSR_WIFI_AP_ENABLE MUST be defined inorder to use csr_wifi_nme_ap_prim.h
#endif

#define CSR_WIFI_NME_AP_PRIM                                            (0x0426)

typedef CsrPrim CsrWifiNmeApPrim;


/*******************************************************************************

  NAME
    CsrWifiNmeApPersCredentialType

  DESCRIPTION
    NME Credential Types

 VALUES
    CSR_WIFI_NME_AP_CREDENTIAL_TYPE_PSK
                   - Use PSK as credential.
    CSR_WIFI_NME_AP_CREDENTIAL_TYPE_PASSPHRASE
                   - Use the specified passphrase as credential

*******************************************************************************/
typedef u8 CsrWifiNmeApPersCredentialType;
#define CSR_WIFI_NME_AP_CREDENTIAL_TYPE_PSK          ((CsrWifiNmeApPersCredentialType) 0x00)
#define CSR_WIFI_NME_AP_CREDENTIAL_TYPE_PASSPHRASE   ((CsrWifiNmeApPersCredentialType) 0x01)


/*******************************************************************************

  NAME
    CsrWifiNmeApConfig

  DESCRIPTION
    Structure holding AP config data.

  MEMBERS
    apGroupkeyTimeout - Access point group key timeout.
    apStrictGtkRekey  - Access point strict GTK rekey flag. If set TRUE, the AP
                        shall rekey GTK every time a connected STA leaves BSS.
    apGmkTimeout      - Access point GMK timeout
    apResponseTimeout - Response timeout
    apRetransLimit    - Max allowed retransmissions

*******************************************************************************/
typedef struct
{
    u16 apGroupkeyTimeout;
    u8   apStrictGtkRekey;
    u16 apGmkTimeout;
    u16 apResponseTimeout;
    u8  apRetransLimit;
} CsrWifiNmeApConfig;

/*******************************************************************************

  NAME
    CsrWifiNmeApAuthPers

  DESCRIPTION

  MEMBERS
    authSupport                     - Credential type value (as defined in the
                                      enumeration type).
    rsnCapabilities                 - RSN capabilities mask
    wapiCapabilities                - WAPI capabilities mask
    pskOrPassphrase                 - Credential type value (as defined in the
                                      enumeration type).
    authPers_credentials            - Union containing credentials which depends
                                      on credentialType parameter.
    authPers_credentialspsk         -
    authPers_credentialspassphrase  -

*******************************************************************************/
typedef struct
{
    CsrWifiSmeApAuthSupportMask      authSupport;
    CsrWifiSmeApRsnCapabilitiesMask  rsnCapabilities;
    CsrWifiSmeApWapiCapabilitiesMask wapiCapabilities;
    CsrWifiNmeApPersCredentialType   pskOrPassphrase;
    union {
        CsrWifiNmePsk        psk;
        CsrWifiNmePassphrase passphrase;
    } authPers_credentials;
} CsrWifiNmeApAuthPers;

/*******************************************************************************

  NAME
    CsrWifiNmeApCredentials

  DESCRIPTION
    Structure containing the Credentials data.

  MEMBERS
    authType                     - Authentication type
    nmeAuthType                  - Authentication parameters
    nmeAuthTypeopenSystemEmpty   -
    nmeAuthTypeauthwep           -
    nmeAuthTypeauthTypePersonal  -

*******************************************************************************/
typedef struct
{
    CsrWifiSmeApAuthType authType;
    union {
        CsrWifiSmeEmpty      openSystemEmpty;
        CsrWifiSmeWepAuth    authwep;
        CsrWifiNmeApAuthPers authTypePersonal;
    } nmeAuthType;
} CsrWifiNmeApCredentials;


/* Downstream */
#define CSR_WIFI_NME_AP_PRIM_DOWNSTREAM_LOWEST            (0x0000)

#define CSR_WIFI_NME_AP_CONFIG_SET_REQ                    ((CsrWifiNmeApPrim) (0x0000 + CSR_WIFI_NME_AP_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_AP_WPS_REGISTER_REQ                  ((CsrWifiNmeApPrim) (0x0001 + CSR_WIFI_NME_AP_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_AP_START_REQ                         ((CsrWifiNmeApPrim) (0x0002 + CSR_WIFI_NME_AP_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_AP_STOP_REQ                          ((CsrWifiNmeApPrim) (0x0003 + CSR_WIFI_NME_AP_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_AP_WMM_PARAM_UPDATE_REQ              ((CsrWifiNmeApPrim) (0x0004 + CSR_WIFI_NME_AP_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_NME_AP_STA_REMOVE_REQ                    ((CsrWifiNmeApPrim) (0x0005 + CSR_WIFI_NME_AP_PRIM_DOWNSTREAM_LOWEST))


#define CSR_WIFI_NME_AP_PRIM_DOWNSTREAM_HIGHEST           (0x0005 + CSR_WIFI_NME_AP_PRIM_DOWNSTREAM_LOWEST)

/* Upstream */
#define CSR_WIFI_NME_AP_PRIM_UPSTREAM_LOWEST              (0x0000 + CSR_PRIM_UPSTREAM)

#define CSR_WIFI_NME_AP_CONFIG_SET_CFM                    ((CsrWifiNmeApPrim)(0x0000 + CSR_WIFI_NME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_AP_WPS_REGISTER_CFM                  ((CsrWifiNmeApPrim)(0x0001 + CSR_WIFI_NME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_AP_START_CFM                         ((CsrWifiNmeApPrim)(0x0002 + CSR_WIFI_NME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_AP_STOP_CFM                          ((CsrWifiNmeApPrim)(0x0003 + CSR_WIFI_NME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_AP_STOP_IND                          ((CsrWifiNmeApPrim)(0x0004 + CSR_WIFI_NME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_AP_WMM_PARAM_UPDATE_CFM              ((CsrWifiNmeApPrim)(0x0005 + CSR_WIFI_NME_AP_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_NME_AP_STATION_IND                       ((CsrWifiNmeApPrim)(0x0006 + CSR_WIFI_NME_AP_PRIM_UPSTREAM_LOWEST))

#define CSR_WIFI_NME_AP_PRIM_UPSTREAM_HIGHEST             (0x0006 + CSR_WIFI_NME_AP_PRIM_UPSTREAM_LOWEST)

#define CSR_WIFI_NME_AP_PRIM_DOWNSTREAM_COUNT             (CSR_WIFI_NME_AP_PRIM_DOWNSTREAM_HIGHEST + 1 - CSR_WIFI_NME_AP_PRIM_DOWNSTREAM_LOWEST)
#define CSR_WIFI_NME_AP_PRIM_UPSTREAM_COUNT               (CSR_WIFI_NME_AP_PRIM_UPSTREAM_HIGHEST   + 1 - CSR_WIFI_NME_AP_PRIM_UPSTREAM_LOWEST)

/*******************************************************************************

  NAME
    CsrWifiNmeApConfigSetReq

  DESCRIPTION
    This primitive passes AP configuration info for NME. This can be sent at
    any time but will be acted upon when the AP is started again. This
    information is common to both P2P GO and AP

  MEMBERS
    common      - Common header for use with the CsrWifiFsm Module
    apConfig    - AP configuration for the NME.
    apMacConfig - MAC configuration to be acted on when
                  CSR_WIFI_NME_AP_START.request is sent.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    CsrWifiNmeApConfig    apConfig;
    CsrWifiSmeApMacConfig apMacConfig;
} CsrWifiNmeApConfigSetReq;

/*******************************************************************************

  NAME
    CsrWifiNmeApWpsRegisterReq

  DESCRIPTION
    This primitive allows the NME to accept the WPS registration from an
    enrollee. Such registration procedure can be cancelled by sending
    CSR_WIFI_NME_WPS_CANCEL.request.

  MEMBERS
    common                   - Common header for use with the CsrWifiFsm Module
    interfaceTag             - Interface Identifier; unique identifier of an
                               interface
    selectedDevicePasswordId - Selected password type
    selectedConfigMethod     - Selected WPS configuration method type
    pin                      - PIN value.
                               Relevant if selected device password ID is PIN.4
                               digit pin is passed by sending the pin digits in
                               pin[0]..pin[3] and rest of the contents filled
                               with '-'.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent         common;
    u16               interfaceTag;
    CsrWifiSmeWpsDpid       selectedDevicePasswordId;
    CsrWifiSmeWpsConfigType selectedConfigMethod;
    u8                pin[8];
} CsrWifiNmeApWpsRegisterReq;

/*******************************************************************************

  NAME
    CsrWifiNmeApStartReq

  DESCRIPTION
    This primitive requests NME to started the AP operation.

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    interfaceTag   - Interface identifier; unique identifier of an interface
    apType         - AP Type specifies the Legacy AP or P2P GO operation
    cloakSsid      - Indicates whether the SSID should be cloaked (hidden and
                     not broadcast in beacon) or not
    ssid           - Service Set Identifier
    ifIndex        - Radio interface
    channel        - Channel number of the channel to use
    apCredentials  - Security credential configuration.
    maxConnections - Maximum number of stations/P2P clients allowed
    p2pGoParam     - P2P specific GO parameters.
    wpsEnabled     - Indicates whether WPS should be enabled or not

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent         common;
    u16               interfaceTag;
    CsrWifiSmeApType        apType;
    u8                 cloakSsid;
    CsrWifiSsid             ssid;
    CsrWifiSmeRadioIF       ifIndex;
    u8                channel;
    CsrWifiNmeApCredentials apCredentials;
    u8                maxConnections;
    CsrWifiSmeApP2pGoConfig p2pGoParam;
    u8                 wpsEnabled;
} CsrWifiNmeApStartReq;

/*******************************************************************************

  NAME
    CsrWifiNmeApStopReq

  DESCRIPTION
    This primitive requests NME to stop the AP operation.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface identifier; unique identifier of an interface

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
} CsrWifiNmeApStopReq;

/*******************************************************************************

  NAME
    CsrWifiNmeApWmmParamUpdateReq

  DESCRIPTION
    Application uses this primitive to update the WMM parameters

  MEMBERS
    common        - Common header for use with the CsrWifiFsm Module
    wmmApParams   - WMM Access point parameters per access category. The array
                    index corresponds to the ACI
    wmmApBcParams - WMM station parameters per access category to be advertised
                    in the beacons and probe response The array index
                    corresponds to the ACI

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    CsrWifiSmeWmmAcParams wmmApParams[4];
    CsrWifiSmeWmmAcParams wmmApBcParams[4];
} CsrWifiNmeApWmmParamUpdateReq;

/*******************************************************************************

  NAME
    CsrWifiNmeApStaRemoveReq

  DESCRIPTION
    This primitive disconnects a connected station. If keepBlocking is set to
    TRUE, the station with the specified MAC address is not allowed to
    connect. If the requested station is not already connected,it may be
    blocked based on keepBlocking parameter.

  MEMBERS
    common        - Common header for use with the CsrWifiFsm Module
    interfaceTag  - Interface Identifier; unique identifier of an interface
    staMacAddress - Mac Address of the station to be disconnected or blocked
    keepBlocking  - If TRUE, the station is blocked. If FALSE and the station is
                    connected, disconnect the station. If FALSE and the station
                    is not connected, no action is taken.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent   common;
    u16         interfaceTag;
    CsrWifiMacAddress staMacAddress;
    u8           keepBlocking;
} CsrWifiNmeApStaRemoveReq;

/*******************************************************************************

  NAME
    CsrWifiNmeApConfigSetCfm

  DESCRIPTION
    This primitive reports the result of the request.

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Status of the request.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiNmeApConfigSetCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeApWpsRegisterCfm

  DESCRIPTION
    This primitive reports the result of WPS procedure.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface identifier; unique identifier of an interface
    status       - Status of the request.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiNmeApWpsRegisterCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeApStartCfm

  DESCRIPTION
    This primitive reports the result of CSR_WIFI_NME_AP_START.request.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface identifier; unique identifier of an interface
    status       - Status of the request.
    ssid         - Service Set Identifier

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
    CsrWifiSsid     ssid;
} CsrWifiNmeApStartCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeApStopCfm

  DESCRIPTION
    This primitive confirms that the AP operation is stopped. NME shall send
    this primitive in response to the request even if AP operation has
    already been stopped

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface identifier; unique identifier of an interface
    status       - Status of the request.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiNmeApStopCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeApStopInd

  DESCRIPTION
    Indicates that AP operation had stopped because of some unrecoverable
    error after AP operation was started successfully. NME sends this signal
    after failing to restart the AP operation internally following an error

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    apType       - Reports AP Type (P2PGO or AP)
    status       - Error Status

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent  common;
    u16        interfaceTag;
    CsrWifiSmeApType apType;
    CsrResult        status;
} CsrWifiNmeApStopInd;

/*******************************************************************************

  NAME
    CsrWifiNmeApWmmParamUpdateCfm

  DESCRIPTION
    A confirm for for the WMM parameters update

  MEMBERS
    common - Common header for use with the CsrWifiFsm Module
    status - Status of the request.

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    CsrResult       status;
} CsrWifiNmeApWmmParamUpdateCfm;

/*******************************************************************************

  NAME
    CsrWifiNmeApStationInd

  DESCRIPTION
    This primitive indicates that a station has joined or a previously joined
    station has left the BSS/group

  MEMBERS
    common            - Common header for use with the CsrWifiFsm Module
    interfaceTag      - Interface Identifier; unique identifier of an interface
    mediaStatus       - Indicates whether the station is connected or
                        disconnected
    peerMacAddress    - MAC address of the station
    peerDeviceAddress - P2P Device Address

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    u16             interfaceTag;
    CsrWifiSmeMediaStatus mediaStatus;
    CsrWifiMacAddress     peerMacAddress;
    CsrWifiMacAddress     peerDeviceAddress;
} CsrWifiNmeApStationInd;


#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_NME_AP_PRIM_H__ */

