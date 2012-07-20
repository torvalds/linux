/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_ROUTER_LIB_H__
#define CSR_WIFI_ROUTER_LIB_H__

#include "csr_pmem.h"
#include "csr_sched.h"
#include "csr_macro.h"
#include "csr_msg_transport.h"

#include "csr_wifi_lib.h"

#include "csr_wifi_router_prim.h"
#include "csr_wifi_router_task.h"


#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  CsrWifiRouterFreeUpstreamMessageContents
 *
 *  DESCRIPTION
 *      Free the allocated memory in a CSR_WIFI_ROUTER upstream message. Does not
 *      free the message itself, and can only be used for upstream messages.
 *
 *  PARAMETERS
 *      Deallocates the resources in a CSR_WIFI_ROUTER upstream message
 *----------------------------------------------------------------------------*/
void CsrWifiRouterFreeUpstreamMessageContents(u16 eventClass, void *message);

/*----------------------------------------------------------------------------*
 *  CsrWifiRouterFreeDownstreamMessageContents
 *
 *  DESCRIPTION
 *      Free the allocated memory in a CSR_WIFI_ROUTER downstream message. Does not
 *      free the message itself, and can only be used for downstream messages.
 *
 *  PARAMETERS
 *      Deallocates the resources in a CSR_WIFI_ROUTER downstream message
 *----------------------------------------------------------------------------*/
void CsrWifiRouterFreeDownstreamMessageContents(u16 eventClass, void *message);

/*----------------------------------------------------------------------------*
 * Enum to string functions
 *----------------------------------------------------------------------------*/
const char* CsrWifiRouterAppTypeToString(CsrWifiRouterAppType value);
const char* CsrWifiRouterEncapsulationToString(CsrWifiRouterEncapsulation value);
const char* CsrWifiRouterOuiToString(CsrWifiRouterOui value);
const char* CsrWifiRouterPriorityToString(CsrWifiRouterPriority value);


/*----------------------------------------------------------------------------*
 * CsrPrim Type toString function.
 * Converts a message type to the String name of the Message
 *----------------------------------------------------------------------------*/
const char* CsrWifiRouterPrimTypeToString(CsrPrim msgType);

/*----------------------------------------------------------------------------*
 * Lookup arrays for PrimType name Strings
 *----------------------------------------------------------------------------*/
extern const char *CsrWifiRouterUpstreamPrimNames[CSR_WIFI_ROUTER_PRIM_UPSTREAM_COUNT];
extern const char *CsrWifiRouterDownstreamPrimNames[CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_COUNT];

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketCancelReqSend

  DESCRIPTION
    This primitive is used to request cancellation of a previously send
    CsrWifiRouterMaPacketReq.
    The frame may already have been transmitted so there is no guarantees
    that the CsrWifiRouterMaPacketCancelReq actually cancels the transmission
    of the frame in question.
    If the cancellation fails, the Router will send, if required,
    CsrWifiRouterMaPacketCfm.
    If the cancellation succeeds, the Router will not send
    CsrWifiRouterMaPacketCfm.

  PARAMETERS
    queue          - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag   - Interface Identifier; unique identifier of an interface
    hostTag        - The hostTag for the frame, which should be cancelled.
    priority       - Priority of the frame, which should be cancelled
    peerMacAddress - Destination MAC address of the frame, which should be
                     cancelled

*******************************************************************************/
#define CsrWifiRouterMaPacketCancelReqCreate(msg__, dst__, src__, interfaceTag__, hostTag__, priority__, peerMacAddress__) \
    msg__ = kmalloc(sizeof(CsrWifiRouterMaPacketCancelReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_ROUTER_PRIM, CSR_WIFI_ROUTER_MA_PACKET_CANCEL_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->hostTag = (hostTag__); \
    msg__->priority = (priority__); \
    msg__->peerMacAddress = (peerMacAddress__);

#define CsrWifiRouterMaPacketCancelReqSendTo(dst__, src__, interfaceTag__, hostTag__, priority__, peerMacAddress__) \
    { \
        CsrWifiRouterMaPacketCancelReq *msg__; \
        CsrWifiRouterMaPacketCancelReqCreate(msg__, dst__, src__, interfaceTag__, hostTag__, priority__, peerMacAddress__); \
        CsrMsgTransport(dst__, CSR_WIFI_ROUTER_PRIM, msg__); \
    }

#define CsrWifiRouterMaPacketCancelReqSend(src__, interfaceTag__, hostTag__, priority__, peerMacAddress__) \
    CsrWifiRouterMaPacketCancelReqSendTo(CSR_WIFI_ROUTER_IFACEQUEUE, src__, interfaceTag__, hostTag__, priority__, peerMacAddress__)

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketReqSend

  DESCRIPTION
    A task sends this primitive to transmit a frame.

  PARAMETERS
    queue              - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag       - Interface Identifier; unique identifier of an interface
    subscriptionHandle - The handle of the subscription
    frameLength        - Length of the frame to be sent in bytes
    frame              - Pointer to the frame to be sent
    freeFunction       - Pointer to function to be used to free the frame
    priority           - Priority of the frame, which should be sent
    hostTag            - An application shall set the bits b31..b28 using one of
                         the CSR_WIFI_ROUTER_APP_TYPE_* masks. Bits b0..b27 can
                         be used by the requestor without any restrictions, but
                         the hostTag shall be unique so the hostTag for
                         CSR_WIFI_ROUTER_APP _TYPE_OTHER should be constructured
                         in the following way [ CSR_WIFI_ROUTER_APP_TYPE_OTHER
                         (4 bits) | SubscriptionHandle (8 bits) | Sequence no.
                         (20 bits) ]. If the hostTag is not unique, the
                         behaviour of the system is unpredicatable with respect
                         to data/management frame transfer.
    cfmRequested       - Indicates if the requestor needs a confirm for packet
                         requests sent under this subscription. If set to TRUE,
                         the router will send a confirm, else it will not send
                         any confirm

*******************************************************************************/
#define CsrWifiRouterMaPacketReqCreate(msg__, dst__, src__, interfaceTag__, subscriptionHandle__, frameLength__, frame__, freeFunction__, priority__, hostTag__, cfmRequested__) \
    msg__ = kmalloc(sizeof(CsrWifiRouterMaPacketReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_ROUTER_PRIM, CSR_WIFI_ROUTER_MA_PACKET_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->subscriptionHandle = (subscriptionHandle__); \
    msg__->frameLength = (frameLength__); \
    msg__->frame = (frame__); \
    msg__->freeFunction = (freeFunction__); \
    msg__->priority = (priority__); \
    msg__->hostTag = (hostTag__); \
    msg__->cfmRequested = (cfmRequested__);

#define CsrWifiRouterMaPacketReqSendTo(dst__, src__, interfaceTag__, subscriptionHandle__, frameLength__, frame__, freeFunction__, priority__, hostTag__, cfmRequested__) \
    { \
        CsrWifiRouterMaPacketReq *msg__; \
        CsrWifiRouterMaPacketReqCreate(msg__, dst__, src__, interfaceTag__, subscriptionHandle__, frameLength__, frame__, freeFunction__, priority__, hostTag__, cfmRequested__); \
        CsrMsgTransport(dst__, CSR_WIFI_ROUTER_PRIM, msg__); \
    }

#define CsrWifiRouterMaPacketReqSend(src__, interfaceTag__, subscriptionHandle__, frameLength__, frame__, freeFunction__, priority__, hostTag__, cfmRequested__) \
    CsrWifiRouterMaPacketReqSendTo(CSR_WIFI_ROUTER_IFACEQUEUE, src__, interfaceTag__, subscriptionHandle__, frameLength__, frame__, freeFunction__, priority__, hostTag__, cfmRequested__)

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketIndSend

  DESCRIPTION
    The router sends the primitive to a subscribed task when it receives a
    frame matching the subscription.

  PARAMETERS
    queue              - Destination Task Queue
    interfaceTag       - Interface Identifier; unique identifier of an interface
    subscriptionHandle - The handle of the subscription
    result             - Status of the operation
    frameLength        - Length of the received frame in bytes
    frame              - Pointer to the received frame
    freeFunction       - Pointer to function to be used to free the frame
    rssi               - Received signal strength indication in dBm
    snr                - Signal to Noise Ratio
    rate               - Transmission/Reception rate

*******************************************************************************/
#define CsrWifiRouterMaPacketIndCreate(msg__, dst__, src__, interfaceTag__, subscriptionHandle__, result__, frameLength__, frame__, freeFunction__, rssi__, snr__, rate__) \
    msg__ = kmalloc(sizeof(CsrWifiRouterMaPacketInd), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_ROUTER_PRIM, CSR_WIFI_ROUTER_MA_PACKET_IND, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->subscriptionHandle = (subscriptionHandle__); \
    msg__->result = (result__); \
    msg__->frameLength = (frameLength__); \
    msg__->frame = (frame__); \
    msg__->freeFunction = (freeFunction__); \
    msg__->rssi = (rssi__); \
    msg__->snr = (snr__); \
    msg__->rate = (rate__);

#define CsrWifiRouterMaPacketIndSendTo(dst__, src__, interfaceTag__, subscriptionHandle__, result__, frameLength__, frame__, freeFunction__, rssi__, snr__, rate__) \
    { \
        CsrWifiRouterMaPacketInd *msg__; \
        CsrWifiRouterMaPacketIndCreate(msg__, dst__, src__, interfaceTag__, subscriptionHandle__, result__, frameLength__, frame__, freeFunction__, rssi__, snr__, rate__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_ROUTER_PRIM, msg__); \
    }

#define CsrWifiRouterMaPacketIndSend(dst__, interfaceTag__, subscriptionHandle__, result__, frameLength__, frame__, freeFunction__, rssi__, snr__, rate__) \
    CsrWifiRouterMaPacketIndSendTo(dst__, CSR_WIFI_ROUTER_IFACEQUEUE, interfaceTag__, subscriptionHandle__, result__, frameLength__, frame__, freeFunction__, rssi__, snr__, rate__)

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketResSend

  DESCRIPTION
    A task send this primitive to confirm the reception of the received
    frame.

  PARAMETERS
    interfaceTag       - Interface Identifier; unique identifier of an interface
    subscriptionHandle - The handle of the subscription
    result             - Status of the operation

*******************************************************************************/
#define CsrWifiRouterMaPacketResCreate(msg__, dst__, src__, interfaceTag__, subscriptionHandle__, result__) \
    msg__ = kmalloc(sizeof(CsrWifiRouterMaPacketRes), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_ROUTER_PRIM, CSR_WIFI_ROUTER_MA_PACKET_RES, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->subscriptionHandle = (subscriptionHandle__); \
    msg__->result = (result__);

#define CsrWifiRouterMaPacketResSendTo(dst__, src__, interfaceTag__, subscriptionHandle__, result__) \
    { \
        CsrWifiRouterMaPacketRes *msg__; \
        CsrWifiRouterMaPacketResCreate(msg__, dst__, src__, interfaceTag__, subscriptionHandle__, result__); \
        CsrMsgTransport(dst__, CSR_WIFI_ROUTER_PRIM, msg__); \
    }

#define CsrWifiRouterMaPacketResSend(src__, interfaceTag__, subscriptionHandle__, result__) \
    CsrWifiRouterMaPacketResSendTo(CSR_WIFI_ROUTER_IFACEQUEUE, src__, interfaceTag__, subscriptionHandle__, result__)

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketCfmSend

  DESCRIPTION
    The router sends the primitive to confirm the result of the transmission
    of the packet of the corresponding CSR_WIFI_ROUTER MA_PACKET_REQ request.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    result       - Status of the operation
    hostTag      - The hostTrag will match the hostTag sent in the request.
    rate         - Transmission/Reception rate

*******************************************************************************/
#define CsrWifiRouterMaPacketCfmCreate(msg__, dst__, src__, interfaceTag__, result__, hostTag__, rate__) \
    msg__ = kmalloc(sizeof(CsrWifiRouterMaPacketCfm), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_ROUTER_PRIM, CSR_WIFI_ROUTER_MA_PACKET_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->result = (result__); \
    msg__->hostTag = (hostTag__); \
    msg__->rate = (rate__);

#define CsrWifiRouterMaPacketCfmSendTo(dst__, src__, interfaceTag__, result__, hostTag__, rate__) \
    { \
        CsrWifiRouterMaPacketCfm *msg__; \
        CsrWifiRouterMaPacketCfmCreate(msg__, dst__, src__, interfaceTag__, result__, hostTag__, rate__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_ROUTER_PRIM, msg__); \
    }

#define CsrWifiRouterMaPacketCfmSend(dst__, interfaceTag__, result__, hostTag__, rate__) \
    CsrWifiRouterMaPacketCfmSendTo(dst__, CSR_WIFI_ROUTER_IFACEQUEUE, interfaceTag__, result__, hostTag__, rate__)

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketSubscribeReqSend

  DESCRIPTION
    A task can use this primitive to subscribe for a particular OUI/protocol
    and transmit and receive frames matching the subscription.
    NOTE: Multiple subscriptions for a given protocol and OUI will result in
    the first subscription receiving the data and not the subsequent
    subscriptions.

  PARAMETERS
    queue         - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag  - Interface Identifier; unique identifier of an interface
    encapsulation - Specifies the encapsulation type, which will be used for the
                    subscription
    protocol      - Together with the OUI, specifies the protocol, which a task
                    wants to subscribe to
    oui           - Specifies the OUI for the protocol, which a task wants to
                    subscribe to

*******************************************************************************/
#define CsrWifiRouterMaPacketSubscribeReqCreate(msg__, dst__, src__, interfaceTag__, encapsulation__, protocol__, oui__) \
    msg__ = kmalloc(sizeof(CsrWifiRouterMaPacketSubscribeReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_ROUTER_PRIM, CSR_WIFI_ROUTER_MA_PACKET_SUBSCRIBE_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->encapsulation = (encapsulation__); \
    msg__->protocol = (protocol__); \
    msg__->oui = (oui__);

#define CsrWifiRouterMaPacketSubscribeReqSendTo(dst__, src__, interfaceTag__, encapsulation__, protocol__, oui__) \
    { \
        CsrWifiRouterMaPacketSubscribeReq *msg__; \
        CsrWifiRouterMaPacketSubscribeReqCreate(msg__, dst__, src__, interfaceTag__, encapsulation__, protocol__, oui__); \
        CsrMsgTransport(dst__, CSR_WIFI_ROUTER_PRIM, msg__); \
    }

#define CsrWifiRouterMaPacketSubscribeReqSend(src__, interfaceTag__, encapsulation__, protocol__, oui__) \
    CsrWifiRouterMaPacketSubscribeReqSendTo(CSR_WIFI_ROUTER_IFACEQUEUE, src__, interfaceTag__, encapsulation__, protocol__, oui__)

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketSubscribeCfmSend

  DESCRIPTION
    The router sends this primitive to confirm the result of the
    subscription.

  PARAMETERS
    queue              - Destination Task Queue
    interfaceTag       - Interface Identifier; unique identifier of an interface
    subscriptionHandle - Handle to the subscription
                         This handle must be used in all subsequent requests
    status             - Status of the operation
    allocOffset        - Size of the offset for the frames of the subscription

*******************************************************************************/
#define CsrWifiRouterMaPacketSubscribeCfmCreate(msg__, dst__, src__, interfaceTag__, subscriptionHandle__, status__, allocOffset__) \
    msg__ = kmalloc(sizeof(CsrWifiRouterMaPacketSubscribeCfm), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_ROUTER_PRIM, CSR_WIFI_ROUTER_MA_PACKET_SUBSCRIBE_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->subscriptionHandle = (subscriptionHandle__); \
    msg__->status = (status__); \
    msg__->allocOffset = (allocOffset__);

#define CsrWifiRouterMaPacketSubscribeCfmSendTo(dst__, src__, interfaceTag__, subscriptionHandle__, status__, allocOffset__) \
    { \
        CsrWifiRouterMaPacketSubscribeCfm *msg__; \
        CsrWifiRouterMaPacketSubscribeCfmCreate(msg__, dst__, src__, interfaceTag__, subscriptionHandle__, status__, allocOffset__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_ROUTER_PRIM, msg__); \
    }

#define CsrWifiRouterMaPacketSubscribeCfmSend(dst__, interfaceTag__, subscriptionHandle__, status__, allocOffset__) \
    CsrWifiRouterMaPacketSubscribeCfmSendTo(dst__, CSR_WIFI_ROUTER_IFACEQUEUE, interfaceTag__, subscriptionHandle__, status__, allocOffset__)

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketUnsubscribeReqSend

  DESCRIPTION
    A task sends this primitive to unsubscribe a subscription

  PARAMETERS
    queue              - Message Source Task Queue (Cfm's will be sent to this Queue)
    interfaceTag       - Interface Identifier; unique identifier of an interface
    subscriptionHandle - The handle of the subscription

*******************************************************************************/
#define CsrWifiRouterMaPacketUnsubscribeReqCreate(msg__, dst__, src__, interfaceTag__, subscriptionHandle__) \
    msg__ = kmalloc(sizeof(CsrWifiRouterMaPacketUnsubscribeReq), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_ROUTER_PRIM, CSR_WIFI_ROUTER_MA_PACKET_UNSUBSCRIBE_REQ, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->subscriptionHandle = (subscriptionHandle__);

#define CsrWifiRouterMaPacketUnsubscribeReqSendTo(dst__, src__, interfaceTag__, subscriptionHandle__) \
    { \
        CsrWifiRouterMaPacketUnsubscribeReq *msg__; \
        CsrWifiRouterMaPacketUnsubscribeReqCreate(msg__, dst__, src__, interfaceTag__, subscriptionHandle__); \
        CsrMsgTransport(dst__, CSR_WIFI_ROUTER_PRIM, msg__); \
    }

#define CsrWifiRouterMaPacketUnsubscribeReqSend(src__, interfaceTag__, subscriptionHandle__) \
    CsrWifiRouterMaPacketUnsubscribeReqSendTo(CSR_WIFI_ROUTER_IFACEQUEUE, src__, interfaceTag__, subscriptionHandle__)

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketUnsubscribeCfmSend

  DESCRIPTION
    The router sends this primitive to confirm the result of the
    unsubscription.

  PARAMETERS
    queue        - Destination Task Queue
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Status of the operation

*******************************************************************************/
#define CsrWifiRouterMaPacketUnsubscribeCfmCreate(msg__, dst__, src__, interfaceTag__, status__) \
    msg__ = kmalloc(sizeof(CsrWifiRouterMaPacketUnsubscribeCfm), GFP_KERNEL); \
    CsrWifiFsmEventInit(&msg__->common, CSR_WIFI_ROUTER_PRIM, CSR_WIFI_ROUTER_MA_PACKET_UNSUBSCRIBE_CFM, dst__, src__); \
    msg__->interfaceTag = (interfaceTag__); \
    msg__->status = (status__);

#define CsrWifiRouterMaPacketUnsubscribeCfmSendTo(dst__, src__, interfaceTag__, status__) \
    { \
        CsrWifiRouterMaPacketUnsubscribeCfm *msg__; \
        CsrWifiRouterMaPacketUnsubscribeCfmCreate(msg__, dst__, src__, interfaceTag__, status__); \
        CsrSchedMessagePut(dst__, CSR_WIFI_ROUTER_PRIM, msg__); \
    }

#define CsrWifiRouterMaPacketUnsubscribeCfmSend(dst__, interfaceTag__, status__) \
    CsrWifiRouterMaPacketUnsubscribeCfmSendTo(dst__, CSR_WIFI_ROUTER_IFACEQUEUE, interfaceTag__, status__)


#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_ROUTER_LIB_H__ */
