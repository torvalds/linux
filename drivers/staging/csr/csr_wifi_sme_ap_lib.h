/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_SME_AP_LIB_H__
#define CSR_WIFI_SME_AP_LIB_H__

#include "csr_pmem.h"
#include "csr_sched.h"
#include "csr_macro.h"
#include "csr_msg_transport.h"

#include "csr_wifi_lib.h"

#include "csr_wifi_sme_ap_prim.h"
#include "csr_wifi_sme_task.h"


#ifdef __cplusplus
extern "C" {
#endif

#ifndef CSR_WIFI_AP_ENABLE
#error CSR_WIFI_AP_ENABLE MUST be defined inorder to use csr_wifi_sme_ap_lib.h
#endif

/*----------------------------------------------------------------------------*
 *  CsrWifiSmeApFreeUpstreamMessageContents
 *
 *  DESCRIPTION
 *      Free the allocated memory in a CSR_WIFI_SME_AP upstream message. Does not
 *      free the message itself, and can only be used for upstream messages.
 *
 *  PARAMETERS
 *      Deallocates the resources in a CSR_WIFI_SME_AP upstream message
 *----------------------------------------------------------------------------*/
void CsrWifiSmeApFreeUpstreamMessageContents(u16 eventClass, void *message);

/*----------------------------------------------------------------------------*
 *  CsrWifiSmeApFreeDownstreamMessageContents
 *
 *  DESCRIPTION
 *      Free the allocated memory in a CSR_WIFI_SME_AP downstream message. Does not
 *      free the message itself, and can only be used for downstream messages.
 *
 *  PARAMETERS
 *      Deallocates the resources in a CSR_WIFI_SME_AP downstream message
 *----------------------------------------------------------------------------*/
void CsrWifiSmeApFreeDownstreamMessageContents(u16 eventClass, void *message);

/*----------------------------------------------------------------------------*
 * Enum to string functions
 *----------------------------------------------------------------------------*/
const char* CsrWifiSmeApAccessTypeToString(CsrWifiSmeApAccessType value);
const char* CsrWifiSmeApAuthSupportToString(CsrWifiSmeApAuthSupport value);
const char* CsrWifiSmeApAuthTypeToString(CsrWifiSmeApAuthType value);
const char* CsrWifiSmeApDirectionToString(CsrWifiSmeApDirection value);
const char* CsrWifiSmeApPhySupportToString(CsrWifiSmeApPhySupport value);
const char* CsrWifiSmeApTypeToString(CsrWifiSmeApType value);


/*----------------------------------------------------------------------------*
 * CsrPrim Type toString function.
 * Converts a message type to the String name of the Message
 *----------------------------------------------------------------------------*/
const char* CsrWifiSmeApPrimTypeToString(CsrPrim msgType);

/*----------------------------------------------------------------------------*
 * Lookup arrays for PrimType name Strings
 *----------------------------------------------------------------------------*/
extern const char *CsrWifiSmeApUpstreamPrimNames[CSR_WIFI_SME_AP_PRIM_UPSTREAM_COUNT];
extern const char *CsrWifiSmeApDownstreamPrimNames[CSR_WIFI_SME_AP_PRIM_DOWNSTREAM_COUNT];

/*******************************************************************************

  NAME
    CsrWifiSmeApActiveBaGetReqSend

  DESCRIPTION
    This primitive used to retrieve information related to the active block
    ack sessions

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag -

*******************************************************************************/
#define CsrWifiSmeApActiveBaGetReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = (CsrWifiSmeApActiveBaGetReq *) CsrPmemAlloc(sizeof(CsrWifiSmeApActiveBaGetReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_ACTIVE_BA_GET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiSmeApActiveBaGetReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiSmeApActiveBaGetReq *msg__; \
        CsrWifiSmeApActiveBaGetReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApActiveBaGetReqSend(src__, interfaceTag__) \
    CsrWifiSmeApActiveBaGetReqSendTo(CSR_WIFI_SME_IFACEQUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiSmeApActiveBaGetCfmSend

  DESCRIPTION
    This primitive carries the information related to the active ba sessions

  PARAMETERS
    queue            - Destination Task Queue
    interfaceTag     -
    status           - Reports the result of the request
    activeBaCount    - Number of active block ack session
    activeBaSessions - Points to a buffer containing an array of
                       CsrWifiSmeApBaSession structures.

*******************************************************************************/
#define CsrWifiSmeApActiveBaGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, activeBaCount__, activeBaSessions__) \
    msg__ = (CsrWifiSmeApActiveBaGetCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeApActiveBaGetCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_ACTIVE_BA_GET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->activeBaCount = (activeBaCount__); \
    msg__->activeBaSessions = (activeBaSessions__);

#define CsrWifiSmeApActiveBaGetCfmSendTo(dst__, src__, interfaceTag__, status__, activeBaCount__, activeBaSessions__) \
    { \
        CsrWifiSmeApActiveBaGetCfm *msg__; \
        CsrWifiSmeApActiveBaGetCfmCreate(msg__, dst__, src__, interfaceTag__, status__, activeBaCount__, activeBaSessions__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApActiveBaGetCfmSend(dst__, interfaceTag__, status__, activeBaCount__, activeBaSessions__) \
    CsrWifiSmeApActiveBaGetCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, activeBaCount__, activeBaSessions__)

/*******************************************************************************

  NAME
    CsrWifiSmeApBaDeleteReqSend

  DESCRIPTION
    This primitive is used to delete an active block ack session

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag -
    reason       -
    baSession    - BA session to be deleted

*******************************************************************************/
#define CsrWifiSmeApBaDeleteReqCreate(msg__, dst__, src__, interfaceTag__, reason__, baSession__) \
    msg__ = (CsrWifiSmeApBaDeleteReq *) CsrPmemAlloc(sizeof(CsrWifiSmeApBaDeleteReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_BA_DELETE_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->reason = (reason__); \
    msg__->baSession = (baSession__);

#define CsrWifiSmeApBaDeleteReqSendTo(dst__, src__, interfaceTag__, reason__, baSession__) \
    { \
        CsrWifiSmeApBaDeleteReq *msg__; \
        CsrWifiSmeApBaDeleteReqCreate(msg__, dst__, src__, interfaceTag__, reason__, baSession__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApBaDeleteReqSend(src__, interfaceTag__, reason__, baSession__) \
    CsrWifiSmeApBaDeleteReqSendTo(CSR_WIFI_SME_IFACEQUEUE, src__, interfaceTag__, reason__, baSession__)

/*******************************************************************************

  NAME
    CsrWifiSmeApBaDeleteCfmSend

  DESCRIPTION
    This primitive confirms the BA is deleted

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag -
    status       - Reports the result of the request
    baSession    - deleted BA session

*******************************************************************************/
#define CsrWifiSmeApBaDeleteCfmCreate(msg__, dst__, src__, interfaceTag__, status__, baSession__) \
    msg__ = (CsrWifiSmeApBaDeleteCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeApBaDeleteCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_BA_DELETE_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->baSession = (baSession__);

#define CsrWifiSmeApBaDeleteCfmSendTo(dst__, src__, interfaceTag__, status__, baSession__) \
    { \
        CsrWifiSmeApBaDeleteCfm *msg__; \
        CsrWifiSmeApBaDeleteCfmCreate(msg__, dst__, src__, interfaceTag__, status__, baSession__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApBaDeleteCfmSend(dst__, interfaceTag__, status__, baSession__) \
    CsrWifiSmeApBaDeleteCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, baSession__)

/*******************************************************************************

  NAME
    CsrWifiSmeApBeaconingStartReqSend

  DESCRIPTION
    This primitive requests the SME to start AP or GO functionality

  PARAMETERS
    queue           - Message Source Task Queue (Cfm's will be sent to this Queue)
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
#define CsrWifiSmeApBeaconingStartReqCreate(msg__, dst__, src__, interfaceTag__, initialPresence__, apType__, cloakSsid__, ssid__, ifIndex__, channel__, maxConnections__, apCredentials__, smeApConfig__, p2pGoParam__) \
    msg__ = (CsrWifiSmeApBeaconingStartReq *) CsrPmemAlloc(sizeof(CsrWifiSmeApBeaconingStartReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_BEACONING_START_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->initialPresence = (initialPresence__); \
    msg__->apType = (apType__); \
    msg__->cloakSsid = (cloakSsid__); \
    msg__->ssid = (ssid__); \
    msg__->ifIndex = (ifIndex__); \
    msg__->channel = (channel__); \
    msg__->maxConnections = (maxConnections__); \
    msg__->apCredentials = (apCredentials__); \
    msg__->smeApConfig = (smeApConfig__); \
    msg__->p2pGoParam = (p2pGoParam__);

#define CsrWifiSmeApBeaconingStartReqSendTo(dst__, src__, interfaceTag__, initialPresence__, apType__, cloakSsid__, ssid__, ifIndex__, channel__, maxConnections__, apCredentials__, smeApConfig__, p2pGoParam__) \
    { \
        CsrWifiSmeApBeaconingStartReq *msg__; \
        CsrWifiSmeApBeaconingStartReqCreate(msg__, dst__, src__, interfaceTag__, initialPresence__, apType__, cloakSsid__, ssid__, ifIndex__, channel__, maxConnections__, apCredentials__, smeApConfig__, p2pGoParam__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApBeaconingStartReqSend(src__, interfaceTag__, initialPresence__, apType__, cloakSsid__, ssid__, ifIndex__, channel__, maxConnections__, apCredentials__, smeApConfig__, p2pGoParam__) \
    CsrWifiSmeApBeaconingStartReqSendTo(CSR_WIFI_SME_IFACEQUEUE, src__, interfaceTag__, initialPresence__, apType__, cloakSsid__, ssid__, ifIndex__, channel__, maxConnections__, apCredentials__, smeApConfig__, p2pGoParam__)

/*******************************************************************************

  NAME
    CsrWifiSmeApBeaconingStartCfmSend

  DESCRIPTION
    This primitive confirms the completion of the request along with the
    status

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag -
    status       -
    secIeLength  -
    secIe        -

*******************************************************************************/
#define CsrWifiSmeApBeaconingStartCfmCreate(msg__, dst__, src__, interfaceTag__, status__, secIeLength__, secIe__) \
    msg__ = (CsrWifiSmeApBeaconingStartCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeApBeaconingStartCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_BEACONING_START_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->secIeLength = (secIeLength__); \
    msg__->secIe = (secIe__);

#define CsrWifiSmeApBeaconingStartCfmSendTo(dst__, src__, interfaceTag__, status__, secIeLength__, secIe__) \
    { \
        CsrWifiSmeApBeaconingStartCfm *msg__; \
        CsrWifiSmeApBeaconingStartCfmCreate(msg__, dst__, src__, interfaceTag__, status__, secIeLength__, secIe__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApBeaconingStartCfmSend(dst__, interfaceTag__, status__, secIeLength__, secIe__) \
    CsrWifiSmeApBeaconingStartCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, secIeLength__, secIe__)

/*******************************************************************************

  NAME
    CsrWifiSmeApBeaconingStopReqSend

  DESCRIPTION
    This primitive requests the SME to STOP AP or P2PGO operation

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag -

*******************************************************************************/
#define CsrWifiSmeApBeaconingStopReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = (CsrWifiSmeApBeaconingStopReq *) CsrPmemAlloc(sizeof(CsrWifiSmeApBeaconingStopReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_BEACONING_STOP_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiSmeApBeaconingStopReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiSmeApBeaconingStopReq *msg__; \
        CsrWifiSmeApBeaconingStopReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApBeaconingStopReqSend(src__, interfaceTag__) \
    CsrWifiSmeApBeaconingStopReqSendTo(CSR_WIFI_SME_IFACEQUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiSmeApBeaconingStopCfmSend

  DESCRIPTION
    This primitive confirms AP or P2PGO operation is terminated

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag -
    status       -

*******************************************************************************/
#define CsrWifiSmeApBeaconingStopCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiSmeApBeaconingStopCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeApBeaconingStopCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_BEACONING_STOP_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiSmeApBeaconingStopCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiSmeApBeaconingStopCfm *msg__; \
        CsrWifiSmeApBeaconingStopCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApBeaconingStopCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiSmeApBeaconingStopCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeApErrorIndSend

  DESCRIPTION
    This primitve is sent by SME to indicate some error in AP operationi
    after AP operations were started successfully and continuing the AP
    operation may lead to undesired behaviour. It is the responsibility of
    the upper layers to stop AP operation if needed

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Range 0-1
    apType       -
    status       - Contains the error status

*******************************************************************************/
#define CsrWifiSmeApErrorIndCreate(msg__, dst__, src__, interfaceTag__, apType__, status__) \
    msg__ = (CsrWifiSmeApErrorInd *) CsrPmemAlloc(sizeof(CsrWifiSmeApErrorInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_ERROR_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->apType = (apType__); \
    msg__->status = (status__);

#define CsrWifiSmeApErrorIndSendTo(dst__, src__, interfaceTag__, apType__, status__) \
    { \
        CsrWifiSmeApErrorInd *msg__; \
        CsrWifiSmeApErrorIndCreate(msg__, dst__, src__, interfaceTag__, apType__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApErrorIndSend(dst__, interfaceTag__, apType__, status__) \
    CsrWifiSmeApErrorIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, apType__, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeApStaConnectStartIndSend

  DESCRIPTION
    This primitive indicates that a stations request to join the group/BSS is
    accepted

  PARAMETERS
    queue          - Destination Task Queue
    interfaceTag   -
    peerMacAddress -

*******************************************************************************/
#define CsrWifiSmeApStaConnectStartIndCreate(msg__, dst__, src__, interfaceTag__, peerMacAddress__) \
    msg__ = (CsrWifiSmeApStaConnectStartInd *) CsrPmemAlloc(sizeof(CsrWifiSmeApStaConnectStartInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_STA_CONNECT_START_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->peerMacAddress = (peerMacAddress__);

#define CsrWifiSmeApStaConnectStartIndSendTo(dst__, src__, interfaceTag__, peerMacAddress__) \
    { \
        CsrWifiSmeApStaConnectStartInd *msg__; \
        CsrWifiSmeApStaConnectStartIndCreate(msg__, dst__, src__, interfaceTag__, peerMacAddress__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApStaConnectStartIndSend(dst__, interfaceTag__, peerMacAddress__) \
    CsrWifiSmeApStaConnectStartIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, peerMacAddress__)

/*******************************************************************************

  NAME
    CsrWifiSmeApStaDisconnectReqSend

  DESCRIPTION
    This primitive tells SME to deauth ot disassociate a particular station
    within BSS

  PARAMETERS
    queue          - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag   -
    deauthReason   -
    disassocReason -
    peerMacaddress -
    keepBlocking   - If TRUE, the station is blocked. If FALSE and the station
                     is connected, disconnect the station. If FALSE and the
                     station is not connected, no action is taken.

*******************************************************************************/
#define CsrWifiSmeApStaDisconnectReqCreate(msg__, dst__, src__, interfaceTag__, deauthReason__, disassocReason__, peerMacaddress__, keepBlocking__) \
    msg__ = (CsrWifiSmeApStaDisconnectReq *) CsrPmemAlloc(sizeof(CsrWifiSmeApStaDisconnectReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_STA_DISCONNECT_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->deauthReason = (deauthReason__); \
    msg__->disassocReason = (disassocReason__); \
    msg__->peerMacaddress = (peerMacaddress__); \
    msg__->keepBlocking = (keepBlocking__);

#define CsrWifiSmeApStaDisconnectReqSendTo(dst__, src__, interfaceTag__, deauthReason__, disassocReason__, peerMacaddress__, keepBlocking__) \
    { \
        CsrWifiSmeApStaDisconnectReq *msg__; \
        CsrWifiSmeApStaDisconnectReqCreate(msg__, dst__, src__, interfaceTag__, deauthReason__, disassocReason__, peerMacaddress__, keepBlocking__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApStaDisconnectReqSend(src__, interfaceTag__, deauthReason__, disassocReason__, peerMacaddress__, keepBlocking__) \
    CsrWifiSmeApStaDisconnectReqSendTo(CSR_WIFI_SME_IFACEQUEUE, src__, interfaceTag__, deauthReason__, disassocReason__, peerMacaddress__, keepBlocking__)

/*******************************************************************************

  NAME
    CsrWifiSmeApStaDisconnectCfmSend

  DESCRIPTION
    This primitive confirms the station is disconnected

  PARAMETERS
    queue          - Destination Task Queue
    interfaceTag   -
    status         -
    peerMacaddress -

*******************************************************************************/
#define CsrWifiSmeApStaDisconnectCfmCreate(msg__, dst__, src__, interfaceTag__, status__, peerMacaddress__) \
    msg__ = (CsrWifiSmeApStaDisconnectCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeApStaDisconnectCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_STA_DISCONNECT_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__); \
    msg__->peerMacaddress = (peerMacaddress__);

#define CsrWifiSmeApStaDisconnectCfmSendTo(dst__, src__, interfaceTag__, status__, peerMacaddress__) \
    { \
        CsrWifiSmeApStaDisconnectCfm *msg__; \
        CsrWifiSmeApStaDisconnectCfmCreate(msg__, dst__, src__, interfaceTag__, status__, peerMacaddress__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApStaDisconnectCfmSend(dst__, interfaceTag__, status__, peerMacaddress__) \
    CsrWifiSmeApStaDisconnectCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__, peerMacaddress__)

/*******************************************************************************

  NAME
    CsrWifiSmeApStaNotifyIndSend

  DESCRIPTION
    This primitive indicates that a station has joined or a previously joined
    station has left the BSS/group

  PARAMETERS
    queue             - Destination Task Queue
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
#define CsrWifiSmeApStaNotifyIndCreate(msg__, dst__, src__, interfaceTag__, mediaStatus__, peerMacAddress__, peerDeviceAddress__, disassocReason__, deauthReason__, WpsRegistration__, secIeLength__, secIe__, groupKeyId__, seqNumber__) \
    msg__ = (CsrWifiSmeApStaNotifyInd *) CsrPmemAlloc(sizeof(CsrWifiSmeApStaNotifyInd)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_STA_NOTIFY_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->mediaStatus = (mediaStatus__); \
    msg__->peerMacAddress = (peerMacAddress__); \
    msg__->peerDeviceAddress = (peerDeviceAddress__); \
    msg__->disassocReason = (disassocReason__); \
    msg__->deauthReason = (deauthReason__); \
    msg__->WpsRegistration = (WpsRegistration__); \
    msg__->secIeLength = (secIeLength__); \
    msg__->secIe = (secIe__); \
    msg__->groupKeyId = (groupKeyId__); \
    memcpy(msg__->seqNumber, (seqNumber__), sizeof(u16) * 8);

#define CsrWifiSmeApStaNotifyIndSendTo(dst__, src__, interfaceTag__, mediaStatus__, peerMacAddress__, peerDeviceAddress__, disassocReason__, deauthReason__, WpsRegistration__, secIeLength__, secIe__, groupKeyId__, seqNumber__) \
    { \
        CsrWifiSmeApStaNotifyInd *msg__; \
        CsrWifiSmeApStaNotifyIndCreate(msg__, dst__, src__, interfaceTag__, mediaStatus__, peerMacAddress__, peerDeviceAddress__, disassocReason__, deauthReason__, WpsRegistration__, secIeLength__, secIe__, groupKeyId__, seqNumber__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApStaNotifyIndSend(dst__, interfaceTag__, mediaStatus__, peerMacAddress__, peerDeviceAddress__, disassocReason__, deauthReason__, WpsRegistration__, secIeLength__, secIe__, groupKeyId__, seqNumber__) \
    CsrWifiSmeApStaNotifyIndSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, mediaStatus__, peerMacAddress__, peerDeviceAddress__, disassocReason__, deauthReason__, WpsRegistration__, secIeLength__, secIe__, groupKeyId__, seqNumber__)

/*******************************************************************************

  NAME
    CsrWifiSmeApWmmParamUpdateReqSend

  DESCRIPTION
    Application uses this primitive to update the WMM parameters on the fly

  PARAMETERS
    queue         - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag  -
    wmmApParams   - WMM parameters to be used for local firmware queue
                    configuration
    wmmApBcParams - WMM parameters to be advertised in beacon/probe response

*******************************************************************************/
#define CsrWifiSmeApWmmParamUpdateReqCreate(msg__, dst__, src__, interfaceTag__, wmmApParams__, wmmApBcParams__) \
    msg__ = (CsrWifiSmeApWmmParamUpdateReq *) CsrPmemAlloc(sizeof(CsrWifiSmeApWmmParamUpdateReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_WMM_PARAM_UPDATE_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    memcpy(msg__->wmmApParams, (wmmApParams__), sizeof(CsrWifiSmeWmmAcParams) * 4); \
    memcpy(msg__->wmmApBcParams, (wmmApBcParams__), sizeof(CsrWifiSmeWmmAcParams) * 4);

#define CsrWifiSmeApWmmParamUpdateReqSendTo(dst__, src__, interfaceTag__, wmmApParams__, wmmApBcParams__) \
    { \
        CsrWifiSmeApWmmParamUpdateReq *msg__; \
        CsrWifiSmeApWmmParamUpdateReqCreate(msg__, dst__, src__, interfaceTag__, wmmApParams__, wmmApBcParams__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApWmmParamUpdateReqSend(src__, interfaceTag__, wmmApParams__, wmmApBcParams__) \
    CsrWifiSmeApWmmParamUpdateReqSendTo(CSR_WIFI_SME_IFACEQUEUE, src__, interfaceTag__, wmmApParams__, wmmApBcParams__)

/*******************************************************************************

  NAME
    CsrWifiSmeApWmmParamUpdateCfmSend

  DESCRIPTION
    A confirm for CSR_WIFI_SME_AP_WMM_PARAM_UPDATE.request

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag -
    status       -

*******************************************************************************/
#define CsrWifiSmeApWmmParamUpdateCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiSmeApWmmParamUpdateCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeApWmmParamUpdateCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_WMM_PARAM_UPDATE_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiSmeApWmmParamUpdateCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiSmeApWmmParamUpdateCfm *msg__; \
        CsrWifiSmeApWmmParamUpdateCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApWmmParamUpdateCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiSmeApWmmParamUpdateCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeApWpsConfigurationReqSend

  DESCRIPTION
    This primitive passes the WPS information for the device to SME. This may
    be accepted only if no interface is active.

  PARAMETERS
    queue     - Message Source Task Queue (Cfm's will be sent to this Queue)
    wpsConfig - WPS config.

*******************************************************************************/
#define CsrWifiSmeApWpsConfigurationReqCreate(msg__, dst__, src__, wpsConfig__) \
    msg__ = (CsrWifiSmeApWpsConfigurationReq *) CsrPmemAlloc(sizeof(CsrWifiSmeApWpsConfigurationReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_WPS_CONFIGURATION_REQ, dst__, src__); \
    msg__->wpsConfig = (wpsConfig__);

#define CsrWifiSmeApWpsConfigurationReqSendTo(dst__, src__, wpsConfig__) \
    { \
        CsrWifiSmeApWpsConfigurationReq *msg__; \
        CsrWifiSmeApWpsConfigurationReqCreate(msg__, dst__, src__, wpsConfig__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApWpsConfigurationReqSend(src__, wpsConfig__) \
    CsrWifiSmeApWpsConfigurationReqSendTo(CSR_WIFI_SME_IFACEQUEUE, src__, wpsConfig__)

/*******************************************************************************

  NAME
    CsrWifiSmeApWpsConfigurationCfmSend

  DESCRIPTION
    Confirm.

  PARAMETERS
    queue  - Destination Task Queue
    status - Status of the request.

*******************************************************************************/
#define CsrWifiSmeApWpsConfigurationCfmCreate(msg__, dst__, src__, status__) \
    msg__ = (CsrWifiSmeApWpsConfigurationCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeApWpsConfigurationCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_WPS_CONFIGURATION_CFM, dst__, src__); \
    msg__->status = (status__);

#define CsrWifiSmeApWpsConfigurationCfmSendTo(dst__, src__, status__) \
    { \
        CsrWifiSmeApWpsConfigurationCfm *msg__; \
        CsrWifiSmeApWpsConfigurationCfmCreate(msg__, dst__, src__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApWpsConfigurationCfmSend(dst__, status__) \
    CsrWifiSmeApWpsConfigurationCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeApWpsRegistrationFinishedReqSend

  DESCRIPTION
    This primitive tells SME that WPS registration procedure has finished

  PARAMETERS
    queue        - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag -

*******************************************************************************/
#define CsrWifiSmeApWpsRegistrationFinishedReqCreate(msg__, dst__, src__, interfaceTag__) \
    msg__ = (CsrWifiSmeApWpsRegistrationFinishedReq *) CsrPmemAlloc(sizeof(CsrWifiSmeApWpsRegistrationFinishedReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_WPS_REGISTRATION_FINISHED_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__);

#define CsrWifiSmeApWpsRegistrationFinishedReqSendTo(dst__, src__, interfaceTag__) \
    { \
        CsrWifiSmeApWpsRegistrationFinishedReq *msg__; \
        CsrWifiSmeApWpsRegistrationFinishedReqCreate(msg__, dst__, src__, interfaceTag__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApWpsRegistrationFinishedReqSend(src__, interfaceTag__) \
    CsrWifiSmeApWpsRegistrationFinishedReqSendTo(CSR_WIFI_SME_IFACEQUEUE, src__, interfaceTag__)

/*******************************************************************************

  NAME
    CsrWifiSmeApWpsRegistrationFinishedCfmSend

  DESCRIPTION
    A confirm for UNIFI_MGT_AP_WPS_REGISTRATION_FINISHED.request

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag -
    status       -

*******************************************************************************/
#define CsrWifiSmeApWpsRegistrationFinishedCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiSmeApWpsRegistrationFinishedCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeApWpsRegistrationFinishedCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_WPS_REGISTRATION_FINISHED_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiSmeApWpsRegistrationFinishedCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiSmeApWpsRegistrationFinishedCfm *msg__; \
        CsrWifiSmeApWpsRegistrationFinishedCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApWpsRegistrationFinishedCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiSmeApWpsRegistrationFinishedCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__)

/*******************************************************************************

  NAME
    CsrWifiSmeApWpsRegistrationStartedReqSend

  DESCRIPTION
    This primitive tells SME that WPS registration procedure has started

  PARAMETERS
    queue                    - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag             -
    SelectedDevicePasswordId -
    SelectedconfigMethod     -

*******************************************************************************/
#define CsrWifiSmeApWpsRegistrationStartedReqCreate(msg__, dst__, src__, interfaceTag__, SelectedDevicePasswordId__, SelectedconfigMethod__) \
    msg__ = (CsrWifiSmeApWpsRegistrationStartedReq *) CsrPmemAlloc(sizeof(CsrWifiSmeApWpsRegistrationStartedReq)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_WPS_REGISTRATION_STARTED_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->SelectedDevicePasswordId = (SelectedDevicePasswordId__); \
    msg__->SelectedconfigMethod = (SelectedconfigMethod__);

#define CsrWifiSmeApWpsRegistrationStartedReqSendTo(dst__, src__, interfaceTag__, SelectedDevicePasswordId__, SelectedconfigMethod__) \
    { \
        CsrWifiSmeApWpsRegistrationStartedReq *msg__; \
        CsrWifiSmeApWpsRegistrationStartedReqCreate(msg__, dst__, src__, interfaceTag__, SelectedDevicePasswordId__, SelectedconfigMethod__); \
        CsrMsgTransport(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApWpsRegistrationStartedReqSend(src__, interfaceTag__, SelectedDevicePasswordId__, SelectedconfigMethod__) \
    CsrWifiSmeApWpsRegistrationStartedReqSendTo(CSR_WIFI_SME_IFACEQUEUE, src__, interfaceTag__, SelectedDevicePasswordId__, SelectedconfigMethod__)

/*******************************************************************************

  NAME
    CsrWifiSmeApWpsRegistrationStartedCfmSend

  DESCRIPTION
    A confirm for UNIFI_MGT_AP_WPS_REGISTRATION_STARTED.request

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag -
    status       -

*******************************************************************************/
#define CsrWifiSmeApWpsRegistrationStartedCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = (CsrWifiSmeApWpsRegistrationStartedCfm *) CsrPmemAlloc(sizeof(CsrWifiSmeApWpsRegistrationStartedCfm)); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_SME_AP_PRIM, CSR_WIFI_SME_AP_WPS_REGISTRATION_STARTED_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiSmeApWpsRegistrationStartedCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiSmeApWpsRegistrationStartedCfm *msg__; \
        CsrWifiSmeApWpsRegistrationStartedCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_SME_AP_PRIM, msg__); \
    }

#define CsrWifiSmeApWpsRegistrationStartedCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiSmeApWpsRegistrationStartedCfmSendTo(dst__, CSR_WIFI_SME_IFACEQUEUE, interfaceTag__, status__)


#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_SME_AP_LIB_H__ */
