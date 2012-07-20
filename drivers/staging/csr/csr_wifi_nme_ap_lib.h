/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_NME_AP_LIB_H__
#define CSR_WIFI_NME_AP_LIB_H__

#include "csr_types.h"
#include "csr_pmem.h"
#include "csr_sched.h"
#include "csr_util.h"
#include "csr_msg_transport.h"

#include "csr_wifi_lib.h"

#include "csr_wifi_nme_ap_prim.h"
#include "csr_wifi_nme_task.h"


#ifdef __cplusplus
extern "C" {
#endif

#ifndef CSR_WIFI_NME_ENABLE
#error CSR_WIFI_NME_ENABLE MUST be defined inorder to use csr_wifi_nme_ap_lib.h
#endif
#ifndef CSR_WIFI_AP_ENABLE
#error CSR_WIFI_AP_ENABLE MUST be defined inorder to use csr_wifi_nme_ap_lib.h
#endif

/*----------------------------------------------------------------------------*
 *  CsrWifiNmeApFreeUpstreamMessageContents
 *
 *  DESCRIPTION
 *      Free the allocated memory in a CSR_WIFI_NME_AP upstream message. Does not
 *      free the message itself, and can only be used for upstream messages.
 *
 *  PARAMETERS
 *      Deallocates the resources in a CSR_WIFI_NME_AP upstream message
 *----------------------------------------------------------------------------*/
void CsrWifiNmeApFreeUpstreamMessageContents(CsrUint16 eventClass, void *message);

/*----------------------------------------------------------------------------*
 *  CsrWifiNmeApFreeDownstreamMessageContents
 *
 *  DESCRIPTION
 *      Free the allocated memory in a CSR_WIFI_NME_AP downstream message. Does not
 *      free the message itself, and can only be used for downstream messages.
 *
 *  PARAMETERS
 *      Deallocates the resources in a CSR_WIFI_NME_AP downstream message
 *----------------------------------------------------------------------------*/
void CsrWifiNmeApFreeDownstreamMessageContents(CsrUint16 eventClass, void *message);

/*----------------------------------------------------------------------------*
 * Enum to string functions
 *----------------------------------------------------------------------------*/
const CsrCharString* CsrWifiNmeApPersCredentialTypeToString(CsrWifiNmeApPersCredentialType value);


/*----------------------------------------------------------------------------*
 * CsrPrim Type toString function.
 * Converts a message type to the String name of the Message
 *----------------------------------------------------------------------------*/
const CsrCharString* CsrWifiNmeApPrimTypeToString(CsrPrim msgType);

/*----------------------------------------------------------------------------*
 * Lookup arrays for PrimType name Strings
 *----------------------------------------------------------------------------*/
extern const CsrCharString *CsrWifiNmeApUpstreamPrimNames[CSR_WIFI_NME_AP_PRIM_UPSTREAM_COUNT];
extern const CsrCharString *CsrWifiNmeApDownstreamPrimNames[CSR_WIFI_NME_AP_PRIM_DOWNSTREAM_COUNT];

/*******************************************************************************

  NAME
    CsrWifiNmeApConfigSetReqSend

  DESCRIPTION
    This primitive passes AP configuration info for NME. This can be sent at
    any time but will be acted upon when the AP is started again. This
    information is common to both P2P GO and AP

  PARAMETERS
    queue       - Message Source Task Queue (Cfm's will be sent to this Queue)
    apConfig    - AP configuration for the NME.
    apMacConfig - MAC configuration to be acted on when
                  CSR_WIFI_NME_AP_START.request is sent.

*******************************************************************************/
#define CsrWifiNmeApConfigSetReqCreate(msg__, dst__, src__, apConfig__, apMacConfig__) \
    msg__ = (CsrWifiNmeApConfigSetReq *) CsrPmemAlloc(sizeof(CsrWifiNmeApConfigSetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_AP_PRIM, CSR_WIFI_NME_AP_CONFIG_SET_REQ, dst__, src__); \
    msg__->apConfig = (apConfig__); \
    msg__->apMacConfig = (apMacConfig__);

#define CsrWifiNmeApConfigSetReqSendTo(dst__, src__, apConfig__, apMacConfig__) \
    { \
        CsrWifiNmeApConfigSetReq *msg__; \
        CsrWifiNmeApConfigSetReqCreate(msg__, dst__, src__, apConfig__, apMacConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_AP_PRIM, msg__); \
    }

#define CsrWifiNmeApConfigSetReqSend(src__, apConfig__, apMacConfig__) \
    CsrWifiNmeApConfigSetReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, apConfig__, apMacConfig__)

/*******************************************************************************

  NAME
    CsrWifiNmeApConfigSetCfmSend

  DESCRIPTION
    This primitive reports the result of the request.

  PARAMETERS
    queue  - Destination Task Queue
    status - Status of the request.

*******************************************************************************/
#define CsrWifiNmeApConfigSetCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiNmeApConfigSetCfm *) CsrPmemAlloc(sizeof(CsrWifiNmeApConfigSetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_AP_PRIM, CSR_WIFI_NME_AP_CONFIG_SET_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiNmeApConfigSetCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiNmeApConfigSetCfm *msg__; \
        CsrWifiNmeApConfigSetCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_AP_PRIM, msg__); \
    }

#define CsrWifiNmeApConfigSetCfmSend(dst__, status__) \
    CsrWifiNmeApConfigSetCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiNmeApStaRemoveReqSend

  DESCRIPTION
    This primitive disconnects a connected station. If keepBlocking is set to
    TRUE, the station with the specified MAC address is not allowed to
    connect. If the requested station is not already connected,it may be
    blocked based on keepBlocking parameter.

  PARAMETERS
    queue         - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag  - Interface Identifier; unique identifier of an interface
    staMacAddress - Mac Address of the station to be disconnected or blocked
    keepBlocking  - If TRUE, the station is blocked. If FALSE and the station is
                    connected, disconnect the station. If FALSE and the station
                    is not connected, no action is taken.

*******************************************************************************/
#define CsrWifiNmeApStaRemoveReqCreate(msg__, dst__, src__, interfaceTag__, staMacAddress__, keepBlocking__) \
    msg__ = (CsrWifiNmeApStaRemoveReq *) CsrPmemAlloc(sizeof(CsrWifiNmeApStaRemoveReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_AP_PRIM, CSR_WIFI_NME_AP_STA_REMOVE_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->staMacAddress = (staMacAddress__); \
    msg__->keepBlocking = (keepBlocking__);

#define CsrWifiNmeApStaRemoveReqSendTo(dst__, src__, interfaceTag__, staMacAddress__, keepBlocking__) \
    { \
        CsrWifiNmeApStaRemoveReq *msg__; \
        CsrWifiNmeApStaRemoveReqCreate(msg__, dst__, src__, interfaceTag__, staMacAddress__, keepBlocking__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_AP_PRIM, msg__); \
    }

#define CsrWifiNmeApStaRemoveReqSend(src__, interfaceTag__, staMacAddress__, keepBlocking__) \
    CsrWifiNmeApStaRemoveReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, interfaceTag__, staMacAddress__, keepBlocking__)

/*******************************************************************************

  NAME
    CsrWifiNmeApStartReqSend

  DESCRIPTION
    This primitive requests NME to started the AP operation.

  PARAMETERS
    queue          - Message Source Task Queue (Cfm's will be sent to this Queue)
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
#define CsrWifiNmeApStartReqCreate(msg__, dst__, src__, interfaceTag__, apType__, cloakSsid__, ssid__, ifIndex__, channel__, apCredentials__, maxConnections__, p2pGoParam__, wpsEnabled__) \
    msg__ = (CsrWifiNmeApStartReq *) CsrPmemAlloc(sizeof(CsrWifiNmeApStartReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_AP_PRIM, CSR_WIFI_NME_AP_START_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->apType = (apType__); \
    msg__->cloakSsid = (cloakSsid__); \
    msg__->ssid = (ssid__); \
    msg__->ifIndex = (ifIndex__); \
    msg__->channel = (channel__); \
    msg__->apCredentials = (apCredentials__); \
    msg__->maxConnections = (maxConnections__); \
    msg__->p2pGoParam = (p2pGoParam__); \
    msg__->wpsEnabled = (wpsEnabled__);

#define CsrWifiNmeApStartReqSendTo(dst__, src__, interfaceTag__, apType__, cloakSsid__, ssid__, ifIndex__, channel__, apCredentials__, maxConnections__, p2pGoParam__, wpsEnabled__) \
    { \
        CsrWifiNmeApStartReq *msg__; \
        CsrWifiNmeApStartReqCreate(msg__, dst__, src__, interfaceTag__, apType__, cloakSsid__, ssid__, ifIndex__, channel__, apCredentials__, maxConnections__, p2pGoParam__, wpsEnabled__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_AP_PRIM, msg__); \
    }

#define CsrWifiNmeApStartReqSend(src__, interfaceTag__, apType__, cloakSsid__, ssid__, ifIndex__, channel__, apCredentials__, maxConnections__, p2pGoParam__, wpsEnabled__) \
    CsrWifiNmeApStartReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, interfaceTag__, apType__, cloakSsid__, ssid__, ifIndex__, channel__, apCredentials__, maxConnections__, p2pGoParam__, wpsEnabled__)

/*******************************************************************************

  NAME
    CsrWifiNmeApStartCfmSend

  DESCRIPTION
    This primitive reports the result of CSR_WIFI_NME_AP_START.request.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface identifier; unique identifier of an interface
    status       - Status of the request.
    ssid         - Service Set Identifier

*******************************************************************************/
#define CsrWifiNmeApStartCfmCreate(msg__, dst__, src__, interfaceTag__, status__, ssid__) \
    msg__ = (CsrWifiNmeApStartCfm *) CsrPmemAlloc(sizeof(CsrWifiNmeApStartCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_AP_PRIM, CSR_WIFI_NME_AP_START_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->ssid = (ssid__);

#define CsrWifiNmeApStartCfmSendTo(dst__, src__, interfaceTag__, status__, ssid__) \
    { \
        CsrWifiNmeApStartCfm *msg__; \
        CsrWifiNmeApStartCfmCreate(msg__, dst__, src__, interfaceTag__, status__, ssid__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_AP_PRIM, msg__); \
    }

#define CsrWifiNmeApStartCfmSend(dst__, interfaceTag__, status__, ssid__) \
    CsrWifiNmeApStartCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, interfaceTag__, status__, ssid__)

/*******************************************************************************

  NAME
    CsrWifiNmeApStationIndSend

  DESCRIPTION
    This primitive indicates that a station has joined or a previously joined
    station has left the BSS/group

  PARAMETERS
    queue             - Destination Task Queue
    interfaceTag      - Interface Identifier; unique identifier of an interface
    mediaStatus       - Indicates whether the station is connected or
                        disconnected
    peerMacAddress    - MAC address of the station
    peerDeviceAddress - P2P Device Address

*******************************************************************************/
#define CsrWifiNmeApStationIndCreate(msg__, dst__, src__, interfaceTag__, mediaStatus__, peerMacAddress__, peerDeviceAddress__) \
    msg__ = (CsrWifiNmeApStationInd *) CsrPmemAlloc(sizeof(CsrWifiNmeApStationInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_AP_PRIM, CSR_WIFI_NME_AP_STATION_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->mediaStatus = (mediaStatus__); \
    msg__->peerMacAddress = (peerMacAddress__); \
    msg__->peerDeviceAddress = (peerDeviceAddress__);

#define CsrWifiNmeApStationIndSendTo(dst__, src__, interfaceTag__, mediaStatus__, peerMacAddress__, peerDeviceAddress__) \
    { \
        CsrWifiNmeApStationInd *msg__; \
        CsrWifiNmeApStationIndCreate(msg__, dst__, src__, interfaceTag__, mediaStatus__, peerMacAddress__, peerDeviceAddress__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_AP_PRIM, msg__); \
    }

#define CsrWifiNmeApStationIndSend(dst__, interfaceTag__, mediaStatus__, peerMacAddress__, peerDeviceAddress__) \
    CsrWifiNmeApStationIndSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, interfaceTag__, mediaStatus__, peerMacAddress__, peerDeviceAddress__)

/*******************************************************************************

  NAME
    CsrWifiNmeApStopReqSend

  DESCRIPTION
    This primitive requests NME to stop the AP operation.

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag - Interface identifier; unique identifier of an interface

*******************************************************************************/
#define CsrWifiNmeApStopReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = (CsrWifiNmeApStopReq *) CsrPmemAlloc(sizeof(CsrWifiNmeApStopReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_AP_PRIM, CSR_WIFI_NME_AP_STOP_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiNmeApStopReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiNmeApStopReq *msg__; \
        CsrWifiNmeApStopReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_AP_PRIM, msg__); \
    }

#define CsrWifiNmeApStopReqSend(src__, interfaceTag__) \
    CsrWifiNmeApStopReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiNmeApStopIndSend

  DESCRIPTION
    Indicates that AP operation had stopped because of some unrecoverable
    error after AP operation was started successfully. NME sends this signal
    after failing to restart the AP operation internally following an error

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    apType       - Reports AP Type (P2PGO or AP)
    status       - Error Status

*******************************************************************************/
#define CsrWifiNmeApStopIndCreate(msg__, dst__, src__, interfaceTag__, apType__, status__) \
    msg__ = (CsrWifiNmeApStopInd *) CsrPmemAlloc(sizeof(CsrWifiNmeApStopInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_AP_PRIM, CSR_WIFI_NME_AP_STOP_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->apType = (apType__); \
    msg__->status = (status__);

#define CsrWifiNmeApStopIndSendTo(dst__, src__, interfaceTag__, apType__, status__) \
    { \
        CsrWifiNmeApStopInd *msg__; \
        CsrWifiNmeApStopIndCreate(msg__, dst__, src__, interfaceTag__, apType__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_AP_PRIM, msg__); \
    }

#define CsrWifiNmeApStopIndSend(dst__, interfaceTag__, apType__, status__) \
    CsrWifiNmeApStopIndSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, interfaceTag__, apType__, status__)

/*******************************************************************************

  NAME
    CsrWifiNmeApStopCfmSend

  DESCRIPTION
    This primitive confirms that the AP operation is stopped. NME shall send
    this primitive in response to the request even if AP operation has
    already been stopped

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface identifier; unique identifier of an interface
    status       - Status of the request.

*******************************************************************************/
#define CsrWifiNmeApStopCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiNmeApStopCfm *) CsrPmemAlloc(sizeof(CsrWifiNmeApStopCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_AP_PRIM, CSR_WIFI_NME_AP_STOP_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiNmeApStopCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiNmeApStopCfm *msg__; \
        CsrWifiNmeApStopCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_AP_PRIM, msg__); \
    }

#define CsrWifiNmeApStopCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiNmeApStopCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiNmeApWmmParamUpdateReqSend

  DESCRIPTION
    Application uses this primitive to update the WMM parameters

  PARAMETERS
    queue         - Message Source Task Queue (Cfm's will be sent to this Queue)
    wmmApParams   - WMM Access point parameters per access category. The array
                    index corresponds to the ACI
    wmmApBcParams - WMM station parameters per access category to be advertised
                    in the beacons and probe response The array index
                    corresponds to the ACI

*******************************************************************************/
#define CsrWifiNmeApWmmParamUpdateReqCreate(msg__, dst__, src__, wmmApParams__, wmmApBcParams__) \
    msg__ = (CsrWifiNmeApWmmParamUpdateReq *) CsrPmemAlloc(sizeof(CsrWifiNmeApWmmParamUpdateReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_AP_PRIM, CSR_WIFI_NME_AP_WMM_PARAM_UPDATE_REQ, dst__, src__); \
    CsrMemCpy(msg__->wmmApParams, (wmmApParams__), sizeof(CsrWifiSmeWmmAcParams) * 4); \
    CsrMemCpy(msg__->wmmApBcParams, (wmmApBcParams__), sizeof(CsrWifiSmeWmmAcParams) * 4);

#define CsrWifiNmeApWmmParamUpdateReqSendTo(dst__, src__, wmmApParams__, wmmApBcParams__) \
    { \
        CsrWifiNmeApWmmParamUpdateReq *msg__; \
        CsrWifiNmeApWmmParamUpdateReqCreate(msg__, dst__, src__, wmmApParams__, wmmApBcParams__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_AP_PRIM, msg__); \
    }

#define CsrWifiNmeApWmmParamUpdateReqSend(src__, wmmApParams__, wmmApBcParams__) \
    CsrWifiNmeApWmmParamUpdateReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, wmmApParams__, wmmApBcParams__)

/*******************************************************************************

  NAME
    CsrWifiNmeApWmmParamUpdateCfmSend

  DESCRIPTION
    A confirm for for the WMM parameters update

  PARAMETERS
    queue  - Destination Task Queue
    status - Status of the request.

*******************************************************************************/
#define CsrWifiNmeApWmmParamUpdateCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiNmeApWmmParamUpdateCfm *) CsrPmemAlloc(sizeof(CsrWifiNmeApWmmParamUpdateCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_AP_PRIM, CSR_WIFI_NME_AP_WMM_PARAM_UPDATE_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiNmeApWmmParamUpdateCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiNmeApWmmParamUpdateCfm *msg__; \
        CsrWifiNmeApWmmParamUpdateCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_AP_PRIM, msg__); \
    }

#define CsrWifiNmeApWmmParamUpdateCfmSend(dst__, status__) \
    CsrWifiNmeApWmmParamUpdateCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiNmeApWpsRegisterReqSend

  DESCRIPTION
    This primitive allows the NME to accept the WPS registration from an
    enrollee. Such registration procedure can be cancelled by sending
    CSR_WIFI_NME_WPS_CANCEL.request.

  PARAMETERS
    queue                    - Message Source Task Queue (Cfm's will be sent to this Queue)
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
#define CsrWifiNmeApWpsRegisterReqCreate(msg__, dst__, src__, interfaceTag__, selectedDevicePasswordId__, selectedConfigMethod__, pin__) \
    msg__ = (CsrWifiNmeApWpsRegisterReq *) CsrPmemAlloc(sizeof(CsrWifiNmeApWpsRegisterReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_AP_PRIM, CSR_WIFI_NME_AP_WPS_REGISTER_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->selectedDevicePasswordId = (selectedDevicePasswordId__); \
    msg__->selectedConfigMethod = (selectedConfigMethod__); \
    CsrMemCpy(msg__->pin, (pin__), sizeof(u8) * 8);

#define CsrWifiNmeApWpsRegisterReqSendTo(dst__, src__, interfaceTag__, selectedDevicePasswordId__, selectedConfigMethod__, pin__) \
    { \
        CsrWifiNmeApWpsRegisterReq *msg__; \
        CsrWifiNmeApWpsRegisterReqCreate(msg__, dst__, src__, interfaceTag__, selectedDevicePasswordId__, selectedConfigMethod__, pin__); \
        CsrMsgTransport(dst__, CSR_WIFI_NME_AP_PRIM, msg__); \
    }

#define CsrWifiNmeApWpsRegisterReqSend(src__, interfaceTag__, selectedDevicePasswordId__, selectedConfigMethod__, pin__) \
    CsrWifiNmeApWpsRegisterReqSendTo(CSR_WIFI_NME_IFACEQUEUE, src__, interfaceTag__, selectedDevicePasswordId__, selectedConfigMethod__, pin__)

/*******************************************************************************

  NAME
    CsrWifiNmeApWpsRegisterCfmSend

  DESCRIPTION
    This primitive reports the result of WPS procedure.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface identifier; unique identifier of an interface
    status       - Status of the request.

*******************************************************************************/
#define CsrWifiNmeApWpsRegisterCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiNmeApWpsRegisterCfm *) CsrPmemAlloc(sizeof(CsrWifiNmeApWpsRegisterCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_NME_AP_PRIM, CSR_WIFI_NME_AP_WPS_REGISTER_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiNmeApWpsRegisterCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiNmeApWpsRegisterCfm *msg__; \
        CsrWifiNmeApWpsRegisterCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_NME_AP_PRIM, msg__); \
    }

#define CsrWifiNmeApWpsRegisterCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiNmeApWpsRegisterCfmSendTo(dst__, CSR_WIFI_NME_IFACEQUEUE, interfaceTag__, status__)


#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_NME_AP_LIB_H__ */
