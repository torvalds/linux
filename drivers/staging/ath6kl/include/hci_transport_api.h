//------------------------------------------------------------------------------
// Copyright (c) 2009-2010 Atheros Corporation.  All rights reserved.
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
#ifndef _HCI_TRANSPORT_API_H_
#define _HCI_TRANSPORT_API_H_

    /* Bluetooth HCI packets are stored in HTC packet containers */
#include "htc_packet.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef void *HCI_TRANSPORT_HANDLE;

typedef HTC_ENDPOINT_ID HCI_TRANSPORT_PACKET_TYPE; 

    /* we map each HCI packet class to a static Endpoint ID */
#define HCI_COMMAND_TYPE   ENDPOINT_1
#define HCI_EVENT_TYPE     ENDPOINT_2
#define HCI_ACL_TYPE       ENDPOINT_3
#define HCI_PACKET_INVALID ENDPOINT_MAX

#define HCI_GET_PACKET_TYPE(pP)    (pP)->Endpoint
#define HCI_SET_PACKET_TYPE(pP,s)  (pP)->Endpoint = (s)

/* callback when an HCI packet was completely sent */
typedef void   (*HCI_TRANSPORT_SEND_PKT_COMPLETE)(void *, struct htc_packet *);
/* callback when an HCI packet is received */
typedef void   (*HCI_TRANSPORT_RECV_PKT)(void *, struct htc_packet *);
/* Optional receive buffer re-fill callback,
 * On some OSes (like Linux) packets are allocated from a global pool and indicated up
 * to the network stack.  The driver never gets the packets back from the OS.  For these OSes
 * a refill callback can be used to allocate and re-queue buffers into HTC.
 * A refill callback is used for the reception of ACL and EVENT packets.  The caller must
 * set the watermark trigger point to cause a refill.
 */
typedef void   (*HCI_TRANSPORT_RECV_REFILL)(void *, HCI_TRANSPORT_PACKET_TYPE Type, int BuffersAvailable);
/* Optional receive packet refill
 * On some systems packet buffers are an extremely limited resource.  Rather than
 * queue largest-possible-sized buffers to the HCI bridge, some systems would rather
 * allocate a specific size as the packet is received.  The trade off is
 * slightly more processing (callback invoked for each RX packet)
 * for the benefit of committing fewer buffer resources into the bridge.
 *
 * The callback is provided the length of the pending packet to fetch. This includes the
 * full transport header, HCI header, plus the length of payload.  The callback can return a pointer to
 * the allocated HTC packet for immediate use.
 *
 * NOTE*** This callback is mutually exclusive with the the refill callback above.
 *
 * */
typedef struct htc_packet *(*HCI_TRANSPORT_RECV_ALLOC)(void *, HCI_TRANSPORT_PACKET_TYPE Type, int Length);

typedef enum _HCI_SEND_FULL_ACTION {
    HCI_SEND_FULL_KEEP = 0,  /* packet that overflowed should be kept in the queue */
    HCI_SEND_FULL_DROP = 1,  /* packet that overflowed should be dropped */
} HCI_SEND_FULL_ACTION;

/* callback when an HCI send queue exceeds the caller's MaxSendQueueDepth threshold,
 * the callback must return the send full action to take (either DROP or KEEP) */
typedef HCI_SEND_FULL_ACTION  (*HCI_TRANSPORT_SEND_FULL)(void *, struct htc_packet *);

struct hci_transport_properties {
    int    HeadRoom;      /* number of bytes in front of HCI packet for header space */
    int    TailRoom;      /* number of bytes at the end of the HCI packet for tail space */
    int    IOBlockPad;    /* I/O block padding required (always a power of 2) */
};

struct hci_transport_config_info {
    int      ACLRecvBufferWaterMark;     /* low watermark to trigger recv refill */
    int      EventRecvBufferWaterMark;   /* low watermark to trigger recv refill */  
    int      MaxSendQueueDepth;          /* max number of packets in the single send queue */
    void     *pContext;                  /* context for all callbacks */
    void     (*TransportFailure)(void *pContext, int Status); /* transport failure callback */
    int (*TransportReady)(HCI_TRANSPORT_HANDLE, struct hci_transport_properties *,void *pContext); /* transport is ready */
    void     (*TransportRemoved)(void *pContext);                  /* transport was removed */
        /* packet processing callbacks */
    HCI_TRANSPORT_SEND_PKT_COMPLETE    pHCISendComplete;
    HCI_TRANSPORT_RECV_PKT             pHCIPktRecv;
    HCI_TRANSPORT_RECV_REFILL          pHCIPktRecvRefill;
    HCI_TRANSPORT_RECV_ALLOC           pHCIPktRecvAlloc;
    HCI_TRANSPORT_SEND_FULL            pHCISendFull;
};

/* ------ Function Prototypes ------ */
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Attach to the HCI transport module
  @function name: HCI_TransportAttach
  @input:  HTCHandle - HTC handle (see HTC apis)
           pInfo - initialization information
  @output:
  @return: HCI_TRANSPORT_HANDLE on success, NULL on failure
  @notes:    The HTC module provides HCI transport services.
  @example:
  @see also: HCI_TransportDetach
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
HCI_TRANSPORT_HANDLE HCI_TransportAttach(void *HTCHandle, struct hci_transport_config_info *pInfo);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Detach from the HCI transport module
  @function name: HCI_TransportDetach
  @input:  HciTrans - HCI transport handle
           pInfo - initialization information
  @output:
  @return: 
  @notes:  
  @example:
  @see also: 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void HCI_TransportDetach(HCI_TRANSPORT_HANDLE HciTrans);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Add receive packets to the HCI transport
  @function name: HCI_TransportAddReceivePkts
  @input:  HciTrans - HCI transport handle
           pQueue - a queue holding one or more packets
  @output:
  @return: 0 on success
  @notes:  user must supply HTC packets for capturing incomming HCI packets.  The caller
           must initialize each HTC packet using the SET_HTC_PACKET_INFO_RX_REFILL()
           macro. Each packet in the queue must be of the same type and length 
  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int    HCI_TransportAddReceivePkts(HCI_TRANSPORT_HANDLE HciTrans, struct htc_packet_queue *pQueue);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Send an HCI packet packet
  @function name: HCI_TransportSendPkt
  @input:  HciTrans - HCI transport handle
           pPacket - packet to send
           Synchronous - send the packet synchronously (blocking)
  @output:
  @return: 0
  @notes:  Caller must initialize packet using SET_HTC_PACKET_INFO_TX() and
           HCI_SET_PACKET_TYPE() macros to prepare the packet. 
           If Synchronous is set to false the call is fully asynchronous.  On error or completion,
           the registered send complete callback will be called.
           If Synchronous is set to true, the call will block until the packet is sent, if the
           interface cannot send the packet within a 2 second timeout, the function will return 
           the failure code : A_EBUSY.
           
           Synchronous Mode should only be used at start-up to initialize the HCI device using 
           custom HCI commands.  It should NOT be mixed with Asynchronous operations.  Mixed synchronous
           and asynchronous operation behavior is undefined.
           
  @example:
  @see also: 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int    HCI_TransportSendPkt(HCI_TRANSPORT_HANDLE HciTrans, struct htc_packet *pPacket, bool Synchronous);


/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Stop HCI transport
  @function name: HCI_TransportStop
  @input:  HciTrans - hci transport handle 
  @output:
  @return:
  @notes: HCI transport communication will be halted.  All receive and pending TX packets will
          be flushed.
  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void        HCI_TransportStop(HCI_TRANSPORT_HANDLE HciTrans);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Start the HCI transport
  @function name: HCI_TransportStart
  @input:  HciTrans - hci transport handle 
  @output:
  @return: 0 on success
  @notes: HCI transport communication will begin, the caller can expect the arrival
          of HCI recv packets as soon as this call returns.
  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int    HCI_TransportStart(HCI_TRANSPORT_HANDLE HciTrans);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Enable or Disable Asynchronous Recv
  @function name: HCI_TransportEnableDisableAsyncRecv
  @input:  HciTrans - hci transport handle 
           Enable - enable or disable asynchronous recv
  @output:
  @return: 0 on success
  @notes: This API must be called when HCI recv is handled synchronously
  @example:
  @see also:
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int    HCI_TransportEnableDisableAsyncRecv(HCI_TRANSPORT_HANDLE HciTrans, bool Enable);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Receive an event packet from the HCI transport synchronously using polling
  @function name: HCI_TransportRecvHCIEventSync
  @input:  HciTrans - hci transport handle 
           pPacket - HTC packet to hold the recv data
           MaxPollMS - maximum polling duration in Milliseconds;
  @output: 
  @return: 0 on success
  @notes: This API should be used only during HCI device initialization, the caller must call
          HCI_TransportEnableDisableAsyncRecv with Enable=false prior to using this API.
          This API will only capture HCI Event packets.
  @example:
  @see also: HCI_TransportEnableDisableAsyncRecv
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int    HCI_TransportRecvHCIEventSync(HCI_TRANSPORT_HANDLE HciTrans,
                                          struct htc_packet           *pPacket,
                                          int                  MaxPollMS);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Set the desired baud rate for the underlying transport layer
  @function name: HCI_TransportSetBaudRate
  @input:  HciTrans - hci transport handle 
           Baud - baud rate in bps
  @output: 
  @return: 0 on success
  @notes: This API should be used only after HCI device initialization
  @example:
  @see also: 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int    HCI_TransportSetBaudRate(HCI_TRANSPORT_HANDLE HciTrans, u32 Baud);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @desc: Enable/Disable HCI Transport Power Management
  @function name: HCI_TransportEnablePowerMgmt
  @input:  HciTrans - hci transport handle 
           Enable - 1 = Enable, 0 = Disable
  @output: 
  @return: 0 on success
  @notes: 
  @example:
  @see also: 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
int HCI_TransportEnablePowerMgmt(HCI_TRANSPORT_HANDLE HciTrans, bool Enable);

#ifdef __cplusplus
}
#endif

#endif /* _HCI_TRANSPORT_API_H_ */
