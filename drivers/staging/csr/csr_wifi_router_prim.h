/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_ROUTER_PRIM_H__
#define CSR_WIFI_ROUTER_PRIM_H__

#include "csr_types.h"
#include "csr_prim_defs.h"
#include "csr_sched.h"
#include "csr_wifi_common.h"
#include "csr_result.h"
#include "csr_wifi_fsm_event.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CSR_WIFI_ROUTER_PRIM                                            (0x0400)

typedef CsrPrim CsrWifiRouterPrim;

typedef void (*CsrWifiRouterFrameFreeFunction)(void *frame);

/*******************************************************************************

  NAME
    CsrWifiRouterAppType

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_APP_TYPE_SME   -
    CSR_WIFI_ROUTER_APP_TYPE_PAL   -
    CSR_WIFI_ROUTER_APP_TYPE_NME   -
    CSR_WIFI_ROUTER_APP_TYPE_OTHER -

*******************************************************************************/
typedef u8 CsrWifiRouterAppType;
#define CSR_WIFI_ROUTER_APP_TYPE_SME     ((CsrWifiRouterAppType) 0x0)
#define CSR_WIFI_ROUTER_APP_TYPE_PAL     ((CsrWifiRouterAppType) 0x1)
#define CSR_WIFI_ROUTER_APP_TYPE_NME     ((CsrWifiRouterAppType) 0x2)
#define CSR_WIFI_ROUTER_APP_TYPE_OTHER   ((CsrWifiRouterAppType) 0x3)

/*******************************************************************************

  NAME
    CsrWifiRouterEncapsulation

  DESCRIPTION
    Indicates the type of encapsulation used for the subscription

 VALUES
    CSR_WIFI_ROUTER_ENCAPSULATION_ETHERNET
                   - Ethernet encapsulation
    CSR_WIFI_ROUTER_ENCAPSULATION_LLC_SNAP
                   - LLC/SNAP encapsulation

*******************************************************************************/
typedef u8 CsrWifiRouterEncapsulation;
#define CSR_WIFI_ROUTER_ENCAPSULATION_ETHERNET   ((CsrWifiRouterEncapsulation) 0x00)
#define CSR_WIFI_ROUTER_ENCAPSULATION_LLC_SNAP   ((CsrWifiRouterEncapsulation) 0x01)

/*******************************************************************************

  NAME
    CsrWifiRouterOui

  DESCRIPTION

 VALUES
    CSR_WIFI_ROUTER_OUI_RFC_1042 -
    CSR_WIFI_ROUTER_OUI_BT       -

*******************************************************************************/
typedef u32 CsrWifiRouterOui;
#define CSR_WIFI_ROUTER_OUI_RFC_1042   ((CsrWifiRouterOui) 0x000000)
#define CSR_WIFI_ROUTER_OUI_BT         ((CsrWifiRouterOui) 0x001958)

/*******************************************************************************

  NAME
    CsrWifiRouterPriority

  DESCRIPTION
    As defined in the IEEE 802.11 standards

 VALUES
    CSR_WIFI_ROUTER_PRIORITY_QOS_UP0
                   - See IEEE 802.11 Standard
    CSR_WIFI_ROUTER_PRIORITY_QOS_UP1
                   - See IEEE 802.11 Standard
    CSR_WIFI_ROUTER_PRIORITY_QOS_UP2
                   - See IEEE 802.11 Standard
    CSR_WIFI_ROUTER_PRIORITY_QOS_UP3
                   - See IEEE 802.11 Standard
    CSR_WIFI_ROUTER_PRIORITY_QOS_UP4
                   - See IEEE 802.11 Standard
    CSR_WIFI_ROUTER_PRIORITY_QOS_UP5
                   - See IEEE 802.11 Standard
    CSR_WIFI_ROUTER_PRIORITY_QOS_UP6
                   - See IEEE 802.11 Standard
    CSR_WIFI_ROUTER_PRIORITY_QOS_UP7
                   - See IEEE 802.11 Standard
    CSR_WIFI_ROUTER_PRIORITY_CONTENTION
                   - See IEEE 802.11 Standard
    CSR_WIFI_ROUTER_PRIORITY_MANAGEMENT
                   - See IEEE 802.11 Standard

*******************************************************************************/
typedef u16 CsrWifiRouterPriority;
#define CSR_WIFI_ROUTER_PRIORITY_QOS_UP0      ((CsrWifiRouterPriority) 0x0000)
#define CSR_WIFI_ROUTER_PRIORITY_QOS_UP1      ((CsrWifiRouterPriority) 0x0001)
#define CSR_WIFI_ROUTER_PRIORITY_QOS_UP2      ((CsrWifiRouterPriority) 0x0002)
#define CSR_WIFI_ROUTER_PRIORITY_QOS_UP3      ((CsrWifiRouterPriority) 0x0003)
#define CSR_WIFI_ROUTER_PRIORITY_QOS_UP4      ((CsrWifiRouterPriority) 0x0004)
#define CSR_WIFI_ROUTER_PRIORITY_QOS_UP5      ((CsrWifiRouterPriority) 0x0005)
#define CSR_WIFI_ROUTER_PRIORITY_QOS_UP6      ((CsrWifiRouterPriority) 0x0006)
#define CSR_WIFI_ROUTER_PRIORITY_QOS_UP7      ((CsrWifiRouterPriority) 0x0007)
#define CSR_WIFI_ROUTER_PRIORITY_CONTENTION   ((CsrWifiRouterPriority) 0x8000)
#define CSR_WIFI_ROUTER_PRIORITY_MANAGEMENT   ((CsrWifiRouterPriority) 0x8010)


/* Downstream */
#define CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_LOWEST            (0x0000)

#define CSR_WIFI_ROUTER_MA_PACKET_SUBSCRIBE_REQ           ((CsrWifiRouterPrim) (0x0000 + CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_MA_PACKET_UNSUBSCRIBE_REQ         ((CsrWifiRouterPrim) (0x0001 + CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_MA_PACKET_REQ                     ((CsrWifiRouterPrim) (0x0002 + CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_MA_PACKET_RES                     ((CsrWifiRouterPrim) (0x0003 + CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_MA_PACKET_CANCEL_REQ              ((CsrWifiRouterPrim) (0x0004 + CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_LOWEST))


#define CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_HIGHEST           (0x0004 + CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_LOWEST)

/* Upstream */
#define CSR_WIFI_ROUTER_PRIM_UPSTREAM_LOWEST              (0x0000 + CSR_PRIM_UPSTREAM)

#define CSR_WIFI_ROUTER_MA_PACKET_SUBSCRIBE_CFM           ((CsrWifiRouterPrim)(0x0000 + CSR_WIFI_ROUTER_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_MA_PACKET_UNSUBSCRIBE_CFM         ((CsrWifiRouterPrim)(0x0001 + CSR_WIFI_ROUTER_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_MA_PACKET_CFM                     ((CsrWifiRouterPrim)(0x0002 + CSR_WIFI_ROUTER_PRIM_UPSTREAM_LOWEST))
#define CSR_WIFI_ROUTER_MA_PACKET_IND                     ((CsrWifiRouterPrim)(0x0003 + CSR_WIFI_ROUTER_PRIM_UPSTREAM_LOWEST))

#define CSR_WIFI_ROUTER_PRIM_UPSTREAM_HIGHEST             (0x0003 + CSR_WIFI_ROUTER_PRIM_UPSTREAM_LOWEST)

#define CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_COUNT             (CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_HIGHEST + 1 - CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_LOWEST)
#define CSR_WIFI_ROUTER_PRIM_UPSTREAM_COUNT               (CSR_WIFI_ROUTER_PRIM_UPSTREAM_HIGHEST   + 1 - CSR_WIFI_ROUTER_PRIM_UPSTREAM_LOWEST)

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketSubscribeReq

  DESCRIPTION
    A task can use this primitive to subscribe for a particular OUI/protocol
    and transmit and receive frames matching the subscription.
    NOTE: Multiple subscriptions for a given protocol and OUI will result in
    the first subscription receiving the data and not the subsequent
    subscriptions.

  MEMBERS
    common        - Common header for use with the CsrWifiFsm Module
    interfaceTag  - Interface Identifier; unique identifier of an interface
    encapsulation - Specifies the encapsulation type, which will be used for the
                    subscription
    protocol      - Together with the OUI, specifies the protocol, which a task
                    wants to subscribe to
    oui           - Specifies the OUI for the protocol, which a task wants to
                    subscribe to

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent            common;
    u16                  interfaceTag;
    CsrWifiRouterEncapsulation encapsulation;
    u16                  protocol;
    u32                  oui;
} CsrWifiRouterMaPacketSubscribeReq;

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketUnsubscribeReq

  DESCRIPTION
    A task sends this primitive to unsubscribe a subscription

  MEMBERS
    common             - Common header for use with the CsrWifiFsm Module
    interfaceTag       - Interface Identifier; unique identifier of an interface
    subscriptionHandle - The handle of the subscription

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    u8        subscriptionHandle;
} CsrWifiRouterMaPacketUnsubscribeReq;

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketReq

  DESCRIPTION
    A task sends this primitive to transmit a frame.

  MEMBERS
    common             - Common header for use with the CsrWifiFsm Module
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
typedef struct
{
    CsrWifiFsmEvent                common;
    u16                      interfaceTag;
    u8                       subscriptionHandle;
    u16                      frameLength;
    u8                      *frame;
    CsrWifiRouterFrameFreeFunction freeFunction;
    CsrWifiRouterPriority          priority;
    u32                      hostTag;
    u8                        cfmRequested;
} CsrWifiRouterMaPacketReq;

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketRes

  DESCRIPTION
    A task send this primitive to confirm the reception of the received
    frame.

  MEMBERS
    common             - Common header for use with the CsrWifiFsm Module
    interfaceTag       - Interface Identifier; unique identifier of an interface
    subscriptionHandle - The handle of the subscription
    result             - Status of the operation

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    u8        subscriptionHandle;
    CsrResult       result;
} CsrWifiRouterMaPacketRes;

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketCancelReq

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

  MEMBERS
    common         - Common header for use with the CsrWifiFsm Module
    interfaceTag   - Interface Identifier; unique identifier of an interface
    hostTag        - The hostTag for the frame, which should be cancelled.
    priority       - Priority of the frame, which should be cancelled
    peerMacAddress - Destination MAC address of the frame, which should be
                     cancelled

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent       common;
    u16             interfaceTag;
    u32             hostTag;
    CsrWifiRouterPriority priority;
    CsrWifiMacAddress     peerMacAddress;
} CsrWifiRouterMaPacketCancelReq;

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketSubscribeCfm

  DESCRIPTION
    The router sends this primitive to confirm the result of the
    subscription.

  MEMBERS
    common             - Common header for use with the CsrWifiFsm Module
    interfaceTag       - Interface Identifier; unique identifier of an interface
    subscriptionHandle - Handle to the subscription
                         This handle must be used in all subsequent requests
    status             - Status of the operation
    allocOffset        - Size of the offset for the frames of the subscription

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    u8        subscriptionHandle;
    CsrResult       status;
    u16       allocOffset;
} CsrWifiRouterMaPacketSubscribeCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketUnsubscribeCfm

  DESCRIPTION
    The router sends this primitive to confirm the result of the
    unsubscription.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    status       - Status of the operation

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       status;
} CsrWifiRouterMaPacketUnsubscribeCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketCfm

  DESCRIPTION
    The router sends the primitive to confirm the result of the transmission
    of the packet of the corresponding CSR_WIFI_ROUTER MA_PACKET_REQ request.

  MEMBERS
    common       - Common header for use with the CsrWifiFsm Module
    interfaceTag - Interface Identifier; unique identifier of an interface
    result       - Status of the operation
    hostTag      - The hostTrag will match the hostTag sent in the request.
    rate         - Transmission/Reception rate

*******************************************************************************/
typedef struct
{
    CsrWifiFsmEvent common;
    u16       interfaceTag;
    CsrResult       result;
    u32       hostTag;
    u16       rate;
} CsrWifiRouterMaPacketCfm;

/*******************************************************************************

  NAME
    CsrWifiRouterMaPacketInd

  DESCRIPTION
    The router sends the primitive to a subscribed task when it receives a
    frame matching the subscription.

  MEMBERS
    common             - Common header for use with the CsrWifiFsm Module
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
typedef struct
{
    CsrWifiFsmEvent                common;
    u16                      interfaceTag;
    u8                       subscriptionHandle;
    CsrResult                      result;
    u16                      frameLength;
    u8                      *frame;
    CsrWifiRouterFrameFreeFunction freeFunction;
    s16                       rssi;
    s16                       snr;
    u16                      rate;
} CsrWifiRouterMaPacketInd;


#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_ROUTER_PRIM_H__ */

