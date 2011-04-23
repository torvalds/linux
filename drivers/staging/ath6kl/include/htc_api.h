//------------------------------------------------------------------------------
// <copyright file="htc_api.h" company="Atheros">
//    Copyright (c) 2007-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================
#ifndef _HTC_API_H_
#define _HTC_API_H_

#include "htc_packet.h"
#include <htc.h>
#include <htc_services.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* TODO.. for BMI */
#define ENDPOINT1 0
// TODO -remove me, but we have to fix BMI first
#define HTC_MAILBOX_NUM_MAX    4

/* this is the amount of header room required by users of HTC */
#define HTC_HEADER_LEN         HTC_HDR_LENGTH

typedef void *HTC_HANDLE;

typedef u16 HTC_SERVICE_ID;

struct htc_init_info {
    void   *pContext;           /* context for target failure notification */
    void   (*TargetFailure)(void *Instance, int Status);
};

/* per service connection send completion */
typedef void   (*HTC_EP_SEND_PKT_COMPLETE)(void *,struct htc_packet *);
/* per service connection callback when a plurality of packets have been sent
 * The struct htc_packet_queue is a temporary queue object (e.g. freed on return from the callback)
 * to hold a list of completed send packets.
 * If the handler cannot fully traverse the packet queue before returning, it should
 * transfer the items of the queue into the caller's private queue using:
 *   HTC_PACKET_ENQUEUE() */
typedef void   (*HTC_EP_SEND_PKT_COMP_MULTIPLE)(void *,struct htc_packet_queue *);
/* per service connection pkt received */
typedef void   (*HTC_EP_RECV_PKT)(void *,struct htc_packet *);
/* per service connection callback when a plurality of packets are received
 * The struct htc_packet_queue is a temporary queue object (e.g. freed on return from the callback)
 * to hold a list of recv packets.
 * If the handler cannot fully traverse the packet queue before returning, it should
 * transfer the items of the queue into the caller's private queue using:
 *   HTC_PACKET_ENQUEUE() */
typedef void   (*HTC_EP_RECV_PKT_MULTIPLE)(void *,struct htc_packet_queue *);

/* Optional per service connection receive buffer re-fill callback,
 * On some OSes (like Linux) packets are allocated from a global pool and indicated up
 * to the network stack.  The driver never gets the packets back from the OS.  For these OSes
 * a refill callback can be used to allocate and re-queue buffers into HTC.
 *
 * On other OSes, the network stack can call into the driver's OS-specifc "return_packet" handler and
 * the driver can re-queue these buffers into HTC. In this regard a refill callback is
 * unnecessary */
typedef void   (*HTC_EP_RECV_REFILL)(void *, HTC_ENDPOINT_ID Endpoint);

/* Optional per service connection receive buffer allocation callback.
 * On some systems packet buffers are an extremely limited resource.  Rather than
 * queue largest-possible-sized buffers to HTC, some systems would rather
 * allocate a specific size as the packet is received.  The trade off is
 * slightly more processing (callback invoked for each RX packet)
 * for the benefit of committing fewer buffer resources into HTC.
 *
 * The callback is provided the length of the pending packet to fetch. This includes the
 * HTC header length plus the length of payload.  The callback can return a pointer to
 * the allocated HTC packet for immediate use.
 *
 * Alternatively a variant of this handler can be used to allocate large receive packets as needed.  
 * For example an application can use the refill mechanism for normal packets and the recv-alloc mechanism to 
 * handle the case where a large packet buffer is required.  This can significantly reduce the
 * amount of "committed" memory used to receive packets.
 *  
 * */
typedef struct htc_packet *(*HTC_EP_RECV_ALLOC)(void *, HTC_ENDPOINT_ID Endpoint, int Length);

typedef enum _HTC_SEND_FULL_ACTION {
    HTC_SEND_FULL_KEEP = 0,  /* packet that overflowed should be kept in the queue */
    HTC_SEND_FULL_DROP = 1,  /* packet that overflowed should be dropped */
} HTC_SEND_FULL_ACTION;

/* Optional per service connection callback when a send queue is full. This can occur if the
 * host continues queueing up TX packets faster than credits can arrive
 * To prevent the host (on some Oses like Linux) from continuously queueing packets
 * and consuming resources, this callback is provided so that that the host
 * can disable TX in the subsystem (i.e. network stack).
 * This callback is invoked for each packet that "overflows" the HTC queue. The callback can
 * determine whether the new packet that overflowed the queue can be kept (HTC_SEND_FULL_KEEP) or
 * dropped (HTC_SEND_FULL_DROP).  If a packet is dropped, the EpTxComplete handler will be called
 * and the packet's status field will be set to A_NO_RESOURCE.
 * Other OSes require a "per-packet" indication for each completed TX packet, this
 * closed loop mechanism will prevent the network stack from overunning the NIC
 * The packet to keep or drop is passed for inspection to the registered handler the handler
 * must ONLY inspect the packet, it may not free or reclaim the packet. */
typedef HTC_SEND_FULL_ACTION (*HTC_EP_SEND_QUEUE_FULL)(void *, struct htc_packet *pPacket);

struct htc_ep_callbacks {
    void                     *pContext;     /* context for each callback */
    HTC_EP_SEND_PKT_COMPLETE EpTxComplete;  /* tx completion callback for connected endpoint */
    HTC_EP_RECV_PKT          EpRecv;        /* receive callback for connected endpoint */
    HTC_EP_RECV_REFILL       EpRecvRefill;  /* OPTIONAL receive re-fill callback for connected endpoint */
    HTC_EP_SEND_QUEUE_FULL   EpSendFull;    /* OPTIONAL send full callback */
    HTC_EP_RECV_ALLOC        EpRecvAlloc;   /* OPTIONAL recv allocation callback */
    HTC_EP_RECV_ALLOC        EpRecvAllocThresh;  /* OPTIONAL recv allocation callback based on a threshold */
    HTC_EP_SEND_PKT_COMP_MULTIPLE EpTxCompleteMultiple; /* OPTIONAL completion handler for multiple complete
                                                             indications (EpTxComplete must be NULL) */
    HTC_EP_RECV_PKT_MULTIPLE      EpRecvPktMultiple;      /* OPTIONAL completion handler for multiple
                                                             recv packet indications (EpRecv must be NULL) */           
    int                      RecvAllocThreshold;    /* if EpRecvAllocThresh is non-NULL, HTC will compare the 
                                                       threshold value to the current recv packet length and invoke
                                                       the EpRecvAllocThresh callback to acquire a packet buffer */
    int                      RecvRefillWaterMark;   /* if a EpRecvRefill handler is provided, this value
                                                       can be used to set a trigger refill callback 
                                                       when the recv queue drops below this value 
                                                       if set to 0, the refill is only called when packets 
                                                       are empty */
};

/* service connection information */
struct htc_service_connect_req {
    HTC_SERVICE_ID   ServiceID;                 /* service ID to connect to */
    u16 ConnectionFlags;           /* connection flags, see htc protocol definition */
    u8 *pMetaData;                 /* ptr to optional service-specific meta-data */
    u8 MetaDataLength;            /* optional meta data length */
    struct htc_ep_callbacks EpCallbacks;               /* endpoint callbacks */
    int              MaxSendQueueDepth;         /* maximum depth of any send queue */
    u32 LocalConnectionFlags;      /* HTC flags for the host-side (local) connection */
    unsigned int     MaxSendMsgSize;            /* override max message size in send direction */
};

#define HTC_LOCAL_CONN_FLAGS_ENABLE_SEND_BUNDLE_PADDING (1 << 0)  /* enable send bundle padding for this endpoint */

/* service connection response information */
struct htc_service_connect_resp {
    u8 *pMetaData;         /* caller supplied buffer to optional meta-data */
    u8 BufferLength;       /* length of caller supplied buffer */
    u8 ActualLength;       /* actual length of meta data */
    HTC_ENDPOINT_ID Endpoint;           /* endpoint to communicate over */
    unsigned int    MaxMsgLength;       /* max length of all messages over this endpoint */
    u8 ConnectRespCode;    /* connect response code from target */
};

/* endpoint distribution structure */
struct htc_endpoint_credit_dist {
    struct htc_endpoint_credit_dist *pNext;
    struct htc_endpoint_credit_dist *pPrev;
    HTC_SERVICE_ID      ServiceID;          /* Service ID (set by HTC) */
    HTC_ENDPOINT_ID     Endpoint;           /* endpoint for this distribution struct (set by HTC) */
    u32 DistFlags;          /* distribution flags, distribution function can
                                               set default activity using SET_EP_ACTIVE() macro */
    int                 TxCreditsNorm;      /* credits for normal operation, anything above this
                                               indicates the endpoint is over-subscribed, this field
                                               is only relevant to the credit distribution function */
    int                 TxCreditsMin;       /* floor for credit distribution, this field is
                                               only relevant to the credit distribution function */
    int                 TxCreditsAssigned;  /* number of credits assigned to this EP, this field
                                               is only relevant to the credit dist function */
    int                 TxCredits;          /* current credits available, this field is used by
                                               HTC to determine whether a message can be sent or
                                               must be queued */
    int                 TxCreditsToDist;    /* pending credits to distribute on this endpoint, this
                                               is set by HTC when credit reports arrive.
                                               The credit distribution functions sets this to zero
                                               when it distributes the credits */
    int                 TxCreditsSeek;      /* this is the number of credits that the current pending TX
                                               packet needs to transmit.  This is set by HTC when
                                               and endpoint needs credits in order to transmit */
    int                 TxCreditSize;       /* size in bytes of each credit (set by HTC) */
    int                 TxCreditsPerMaxMsg; /* credits required for a maximum sized messages (set by HTC) */
    void                *pHTCReserved;      /* reserved for HTC use */    
    int                 TxQueueDepth;       /* current depth of TX queue , i.e. messages waiting for credits
                                               This field is valid only when HTC_CREDIT_DIST_ACTIVITY_CHANGE
                                               or HTC_CREDIT_DIST_SEND_COMPLETE is indicated on an endpoint
                                               that has non-zero credits to recover
                                              */
};

#define HTC_EP_ACTIVE                            ((u32) (1u << 31))

/* macro to check if an endpoint has gone active, useful for credit
 * distributions */
#define IS_EP_ACTIVE(epDist)  ((epDist)->DistFlags & HTC_EP_ACTIVE)
#define SET_EP_ACTIVE(epDist) (epDist)->DistFlags |= HTC_EP_ACTIVE

    /* credit distibution code that is passed into the distrbution function,
     * there are mandatory and optional codes that must be handled */
typedef enum _HTC_CREDIT_DIST_REASON {
    HTC_CREDIT_DIST_SEND_COMPLETE = 0,     /* credits available as a result of completed
                                              send operations (MANDATORY) resulting in credit reports */
    HTC_CREDIT_DIST_ACTIVITY_CHANGE = 1,   /* a change in endpoint activity occurred (OPTIONAL) */
    HTC_CREDIT_DIST_SEEK_CREDITS,          /* an endpoint needs to "seek" credits (OPTIONAL) */
    HTC_DUMP_CREDIT_STATE                  /* for debugging, dump any state information that is kept by
                                              the distribution function */
} HTC_CREDIT_DIST_REASON;

typedef void (*HTC_CREDIT_DIST_CALLBACK)(void                     *Context,
                                         struct htc_endpoint_credit_dist *pEPList,
                                         HTC_CREDIT_DIST_REASON   Reason);

typedef void (*HTC_CREDIT_INIT_CALLBACK)(void *Context,
                                         struct htc_endpoint_credit_dist *pEPList,
                                         int                      TotalCredits);

    /* endpoint statistics action */
typedef enum _HTC_ENDPOINT_STAT_ACTION {
    HTC_EP_STAT_SAMPLE = 0,                /* only read statistics */
    HTC_EP_STAT_SAMPLE_AND_CLEAR = 1,      /* sample and immediately clear statistics */
    HTC_EP_STAT_CLEAR                      /* clear only */
} HTC_ENDPOINT_STAT_ACTION;

    /* endpoint statistics */
struct htc_endpoint_stats {
    u32 TxCreditLowIndications;  /* number of times the host set the credit-low flag in a send message on
                                        this endpoint */
    u32 TxIssued;               /* running count of total TX packets issued */
    u32 TxPacketsBundled;       /* running count of TX packets that were issued in bundles */
    u32 TxBundles;              /* running count of TX bundles that were issued */
    u32 TxDropped;              /* tx packets that were dropped */
    u32 TxCreditRpts;           /* running count of total credit reports received for this endpoint */
    u32 TxCreditRptsFromRx;     /* credit reports received from this endpoint's RX packets */
    u32 TxCreditRptsFromOther;  /* credit reports received from RX packets of other endpoints */
    u32 TxCreditRptsFromEp0;    /* credit reports received from endpoint 0 RX packets */
    u32 TxCreditsFromRx;        /* count of credits received via Rx packets on this endpoint */
    u32 TxCreditsFromOther;     /* count of credits received via another endpoint */
    u32 TxCreditsFromEp0;       /* count of credits received via another endpoint */
    u32 TxCreditsConsummed;     /* count of consummed credits */
    u32 TxCreditsReturned;      /* count of credits returned */
    u32 RxReceived;             /* count of RX packets received */
    u32 RxLookAheads;           /* count of lookahead records
                                         found in messages received on this endpoint */
    u32 RxPacketsBundled;       /* count of recv packets received in a bundle */
    u32 RxBundleLookAheads;     /* count of number of bundled lookaheads */
    u32 RxBundleIndFromHdr;     /* count of the number of bundle indications from the HTC header */
    u32 RxAllocThreshHit;       /* count of the number of times the recv allocation threshold was hit */
    u32 RxAllocThreshBytes;     /* total number of bytes */
};

/* ------ Function Prototypes ------ */
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Create an instance of HTC over the underlying HIF device
  @function name: HTCCreate
  @input:  HifDevice - hif device handle,
           pInfo - initialization information
  @output:
  @return: HTC_HANDLE on success, NULL on failure
  @notes: 
  @example:
  @see also: HTCDestroy
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
HTC_HANDLE HTCCreate(void *HifDevice, struct htc_init_info *pInfo);
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Get the underlying HIF device handle
  @function name: HTCGetHifDevice
  @input:  HTCHandle - handle passed into the AddInstance callback
  @output:
  @return: opaque HIF device handle usable in HIF API calls.
  @notes:
  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void       *HTCGetHifDevice(HTC_HANDLE HTCHandle);
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Set credit distribution parameters
  @function name: HTCSetCreditDistribution
  @input:  HTCHandle - HTC handle
           pCreditDistCont - caller supplied context to pass into distribution functions
           CreditDistFunc - Distribution function callback
           CreditDistInit - Credit Distribution initialization callback
           ServicePriorityOrder - Array containing list of service IDs, lowest index is highest
                                  priority
           ListLength - number of elements in ServicePriorityOrder
  @output:
  @return:
  @notes:  The user can set a custom credit distribution function to handle special requirements
           for each endpoint.  A default credit distribution routine can be used by setting
           CreditInitFunc to NULL.  The default credit distribution is only provided for simple
           "fair" credit distribution without regard to any prioritization.

  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void        HTCSetCreditDistribution(HTC_HANDLE               HTCHandle,
                                     void                     *pCreditDistContext,
                                     HTC_CREDIT_DIST_CALLBACK CreditDistFunc,
                                     HTC_CREDIT_INIT_CALLBACK CreditInitFunc,
                                     HTC_SERVICE_ID           ServicePriorityOrder[],
                                     int                      ListLength);
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Wait for the target to indicate the HTC layer is ready
  @function name: HTCWaitTarget
  @input:  HTCHandle - HTC handle
  @output:
  @return:
  @notes:  This API blocks until the target responds with an HTC ready message.
           The caller should not connect services until the target has indicated it is
           ready.
  @example:
  @see also: HTCConnectService
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int    HTCWaitTarget(HTC_HANDLE HTCHandle);
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Start target service communications
  @function name: HTCStart
  @input:  HTCHandle - HTC handle
  @output:
  @return:
  @notes: This API indicates to the target that the service connection phase is complete
          and the target can freely start all connected services.  This API should only be
          called AFTER all service connections have been made.  TCStart will issue a
          SETUP_COMPLETE message to the target to indicate that all service connections
          have been made and the target can start communicating over the endpoints.
  @example:
  @see also: HTCConnectService
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int    HTCStart(HTC_HANDLE HTCHandle);
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Add receive packet to HTC
  @function name: HTCAddReceivePkt
  @input:  HTCHandle - HTC handle
           pPacket - HTC receive packet to add
  @output:
  @return: 0 on success
  @notes:  user must supply HTC packets for capturing incomming HTC frames.  The caller
           must initialize each HTC packet using the SET_HTC_PACKET_INFO_RX_REFILL()
           macro.
  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int    HTCAddReceivePkt(HTC_HANDLE HTCHandle, struct htc_packet *pPacket);
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Connect to an HTC service
  @function name: HTCConnectService
  @input:  HTCHandle - HTC handle
           pReq - connection details
  @output: pResp - connection response
  @return:
  @notes:  Service connections must be performed before HTCStart.  User provides callback handlers
           for various endpoint events.
  @example:
  @see also: HTCStart
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int    HTCConnectService(HTC_HANDLE HTCHandle,
                              struct htc_service_connect_req  *pReq,
                              struct htc_service_connect_resp *pResp);
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Send an HTC packet
  @function name: HTCSendPkt
  @input:  HTCHandle - HTC handle
           pPacket - packet to send
  @output:
  @return: 0
  @notes:  Caller must initialize packet using SET_HTC_PACKET_INFO_TX() macro.
           This interface is fully asynchronous.  On error, HTC SendPkt will
           call the registered Endpoint callback to cleanup the packet.
  @example:
  @see also: HTCFlushEndpoint
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int    HTCSendPkt(HTC_HANDLE HTCHandle, struct htc_packet *pPacket);
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Stop HTC service communications
  @function name: HTCStop
  @input:  HTCHandle - HTC handle
  @output:
  @return:
  @notes: HTC communications is halted.  All receive and pending TX packets will
          be flushed.
  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void        HTCStop(HTC_HANDLE HTCHandle);
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Destroy HTC service
  @function name: HTCDestroy
  @input: HTCHandle 
  @output:
  @return:
  @notes:  This cleans up all resources allocated by HTCCreate().
  @example:
  @see also: HTCCreate
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void        HTCDestroy(HTC_HANDLE HTCHandle);
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Flush pending TX packets
  @function name: HTCFlushEndpoint
  @input:  HTCHandle - HTC handle
           Endpoint - Endpoint to flush
           Tag - flush tag
  @output:
  @return:
  @notes:  The Tag parameter is used to selectively flush packets with matching tags.
           The value of 0 forces all packets to be flush regardless of tag.
  @example:
  @see also: HTCSendPkt
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void        HTCFlushEndpoint(HTC_HANDLE HTCHandle, HTC_ENDPOINT_ID Endpoint, HTC_TX_TAG Tag);
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Dump credit distribution state
  @function name: HTCDumpCreditStates
  @input:  HTCHandle - HTC handle
  @output:
  @return:
  @notes:  This dumps all credit distribution information to the debugger
  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void        HTCDumpCreditStates(HTC_HANDLE HTCHandle);
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Indicate a traffic activity change on an endpoint
  @function name: HTCIndicateActivityChange
  @input:  HTCHandle - HTC handle
           Endpoint - endpoint in which activity has changed
           Active - true if active, false if it has become inactive
  @output:
  @return:
  @notes:  This triggers the registered credit distribution function to
           re-adjust credits for active/inactive endpoints.
  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void        HTCIndicateActivityChange(HTC_HANDLE      HTCHandle,
                                      HTC_ENDPOINT_ID Endpoint,
                                      bool          Active);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Get endpoint statistics
  @function name: HTCGetEndpointStatistics
  @input:  HTCHandle - HTC handle
           Endpoint - Endpoint identifier
           Action - action to take with statistics
  @output:
           pStats - statistics that were sampled (can be NULL if Action is HTC_EP_STAT_CLEAR)

  @return: true if statistics profiling is enabled, otherwise false.

  @notes:  Statistics is a compile-time option and this function may return false
           if HTC is not compiled with profiling.

           The caller can specify the statistic "action" to take when sampling
           the statistics.  This includes:

           HTC_EP_STAT_SAMPLE: The pStats structure is filled with the current values.
           HTC_EP_STAT_SAMPLE_AND_CLEAR: The structure is filled and the current statistics
                             are cleared.
           HTC_EP_STAT_CLEA : the statistics are cleared, the called can pass a NULL value for
                   pStats

  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
bool       HTCGetEndpointStatistics(HTC_HANDLE               HTCHandle,
                                      HTC_ENDPOINT_ID          Endpoint,
                                      HTC_ENDPOINT_STAT_ACTION Action,
                                      struct htc_endpoint_stats       *pStats);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Unblock HTC message reception
  @function name: HTCUnblockRecv
  @input:  HTCHandle - HTC handle
  @output:
  @return:
  @notes:
           HTC will block the receiver if the EpRecvAlloc callback fails to provide a packet.
           The caller can use this API to indicate to HTC when resources (buffers) are available
           such that the  receiver can be unblocked and HTC may re-attempt fetching the pending message.

           This API is not required if the user uses the EpRecvRefill callback or uses the HTCAddReceivePacket()
           API to recycle or provide receive packets to HTC.

  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void HTCUnblockRecv(HTC_HANDLE HTCHandle);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: send a series of HTC packets  
  @function name: HTCSendPktsMultiple
  @input:  HTCHandle - HTC handle
           pPktQueue - local queue holding packets to send
  @output:
  @return: 0
  @notes:  Caller must initialize each packet using SET_HTC_PACKET_INFO_TX() macro.
           The queue must only contain packets directed at the same endpoint.
           Caller supplies a pointer to an struct htc_packet_queue structure holding the TX packets in FIFO order.
           This API will remove the packets from the pkt queue and place them into the HTC Tx Queue
           and bundle messages where possible.
           The caller may allocate the pkt queue on the stack to hold the packets.           
           This interface is fully asynchronous.  On error, HTCSendPkts will
           call the registered Endpoint callback to cleanup the packet.
  @example:
  @see also: HTCFlushEndpoint
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int    HTCSendPktsMultiple(HTC_HANDLE HTCHandle, struct htc_packet_queue *pPktQueue);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Add multiple receive packets to HTC
  @function name: HTCAddReceivePktMultiple
  @input:  HTCHandle - HTC handle
           pPktQueue - HTC receive packet queue holding packets to add
  @output:
  @return: 0 on success
  @notes:  user must supply HTC packets for capturing incomming HTC frames.  The caller
           must initialize each HTC packet using the SET_HTC_PACKET_INFO_RX_REFILL()
           macro. The queue must only contain recv packets for the same endpoint.
           Caller supplies a pointer to an struct htc_packet_queue structure holding the recv packet.
           This API will remove the packets from the pkt queue and place them into internal
           recv packet list.
           The caller may allocate the pkt queue on the stack to hold the packets.           
  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int    HTCAddReceivePktMultiple(HTC_HANDLE HTCHandle, struct htc_packet_queue *pPktQueue);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Check if an endpoint is marked active
  @function name: HTCIsEndpointActive
  @input:  HTCHandle - HTC handle
           Endpoint - endpoint to check for active state
  @output:
  @return: returns true if Endpoint is Active
  @notes:  
  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
bool      HTCIsEndpointActive(HTC_HANDLE      HTCHandle,
                                HTC_ENDPOINT_ID Endpoint);


/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Get the number of recv buffers currently queued into an HTC endpoint
  @function name: HTCGetNumRecvBuffers
  @input:  HTCHandle - HTC handle
           Endpoint - endpoint to check
  @output:
  @return: returns number of buffers in queue
  @notes:  
  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int         HTCGetNumRecvBuffers(HTC_HANDLE      HTCHandle,
                                 HTC_ENDPOINT_ID Endpoint);
                                                                      
/* internally used functions for testing... */
void HTCEnableRecv(HTC_HANDLE HTCHandle);
void HTCDisableRecv(HTC_HANDLE HTCHandle);
int HTCWaitForPendingRecv(HTC_HANDLE   HTCHandle,
                               u32 TimeoutInMs,
                               bool      *pbIsRecvPending);

#ifdef __cplusplus
}
#endif

#endif /* _HTC_API_H_ */
