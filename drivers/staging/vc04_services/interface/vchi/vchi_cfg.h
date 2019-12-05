/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHI_CFG_H_
#define VCHI_CFG_H_

/*******************************************************************************
 * Defines in this first section are part of the VCHI API and may be examined by
 * VCHI services.
 ******************************************************************************/

/*
 * Required alignment of base addresses for bulk transfer, if unaligned
 * transfers are not enabled
 * Really determined by the message driver, and should be available from
 * a run-time call.
 */
#ifndef VCHI_BULK_ALIGN
#   if __VCCOREVER__ >= 0x04000000
#       define VCHI_BULK_ALIGN 32 // Allows for the need to do cache cleans
#   else
#       define VCHI_BULK_ALIGN 16
#   endif
#endif

/*
 * Required length multiple for bulk transfers, if unaligned transfers are
 * not enabled
 * May be less than or greater than VCHI_BULK_ALIGN
 * Really determined by the message driver, and should be available from
 * a run-time call.
 */
#ifndef VCHI_BULK_GRANULARITY
#   if __VCCOREVER__ >= 0x04000000
#       define VCHI_BULK_GRANULARITY 32 // Allows for the need to do cache cleans
#   else
#       define VCHI_BULK_GRANULARITY 16
#   endif
#endif

/* The largest possible message to be queued with vchi_msg_queue. */
#ifndef VCHI_MAX_MSG_SIZE
#   if defined VCHI_LOCAL_HOST_PORT
#       define VCHI_MAX_MSG_SIZE     16384         // makes file transfers fast, but should they be using bulk?
#   else
#       define VCHI_MAX_MSG_SIZE      4096 // NOTE: THIS MUST BE LARGER THAN OR EQUAL TO THE SIZE OF THE KHRONOS MERGE BUFFER!!
#   endif
#endif

/******************************************************************************
 * Defines below are system configuration options, and should not be used by
 * VCHI services.
 ******************************************************************************/

/*
 * How many connections can we support? A localhost implementation uses
 * 2 connections, 1 for host-app, 1 for VMCS, and these are hooked together
 * by a loopback MPHI VCFW  driver.
 */
#ifndef VCHI_MAX_NUM_CONNECTIONS
#   define VCHI_MAX_NUM_CONNECTIONS 3
#endif

/*
 * How many services can we open per connection? Extending this doesn't cost
 * processing time, just a small amount of static memory.
 */
#ifndef VCHI_MAX_SERVICES_PER_CONNECTION
#  define VCHI_MAX_SERVICES_PER_CONNECTION 36
#endif

/* Adjust if using a message driver that supports more logical TX channels */
#ifndef VCHI_MAX_BULK_TX_CHANNELS_PER_CONNECTION
#   define VCHI_MAX_BULK_TX_CHANNELS_PER_CONNECTION 9 // 1 MPHI + 8 CCP2 logical channels
#endif

/* Adjust if using a message driver that supports more logical RX channels */
#ifndef VCHI_MAX_BULK_RX_CHANNELS_PER_CONNECTION
#   define VCHI_MAX_BULK_RX_CHANNELS_PER_CONNECTION 1 // 1 MPHI
#endif

/*
 * How many receive slots do we use. This times VCHI_MAX_MSG_SIZE gives the
 * effective receive queue space, less message headers.
 */
#ifndef VCHI_NUM_READ_SLOTS
#  if defined(VCHI_LOCAL_HOST_PORT)
#     define VCHI_NUM_READ_SLOTS 4
#  else
#     define VCHI_NUM_READ_SLOTS 48
#  endif
#endif

/*
 * Do we utilise overrun facility for receive message slots? Can aid peer
 * transmit performance. Only define on VideoCore end, talking to host.
 */
//#define VCHI_MSG_RX_OVERRUN

/*
 * How many transmit slots do we use. Generally don't need many,
 * as the hardware driver underneath VCHI will usually have its own buffering.
 */
#ifndef VCHI_NUM_WRITE_SLOTS
#  define VCHI_NUM_WRITE_SLOTS 4
#endif

/*
 * If a service has held or queued received messages in VCHI_XOFF_THRESHOLD or
 * more slots, then it's taking up too much buffer space,
 * and the peer service will be told to stop transmitting with an XOFF message.
 * For this to be effective, the VCHI_NUM_READ_SLOTS needs to be considerably
 * bigger than VCHI_NUM_WRITE_SLOTS, or the transmit latency is too high.
 */
#ifndef VCHI_XOFF_THRESHOLD
#  define VCHI_XOFF_THRESHOLD (VCHI_NUM_READ_SLOTS / 2)
#endif

/*
 * After we've sent an XOFF, the peer will be told to resume transmission
 * once the local service has dequeued/released enough messages that it's now
 * occupying VCHI_XON_THRESHOLD slots or fewer.
 */
#ifndef VCHI_XON_THRESHOLD
#  define VCHI_XON_THRESHOLD (VCHI_NUM_READ_SLOTS / 4)
#endif

/*
 * A size below which a bulk transfer omits the handshake completely and always
 * goes via the message channel, if bulk auxiliary is being sent on that
 * service. (The user can guarantee this by enabling unaligned transmits).
 * Not API.
 */
#ifndef VCHI_MIN_BULK_SIZE
#  define VCHI_MIN_BULK_SIZE    (VCHI_MAX_MSG_SIZE / 2 < 4096 ? VCHI_MAX_MSG_SIZE / 2 : 4096)
#endif

/*
 * Maximum size of bulk transmission chunks, for each interface type.
 * A trade-off between speed and latency; the smaller the chunk size the better
 * change of messages and other bulk transmissions getting in when big bulk
 * transfers are happening. Set to 0 to not break transmissions into chunks.
 */
#ifndef VCHI_MAX_BULK_CHUNK_SIZE_MPHI
#  define VCHI_MAX_BULK_CHUNK_SIZE_MPHI (16 * 1024)
#endif

/*
 * NB Chunked CCP2 transmissions violate the letter of the CCP2 spec
 * by using "JPEG8" mode with multiple-line frames. Only use if the receiver
 * can cope.
 */
#ifndef VCHI_MAX_BULK_CHUNK_SIZE_CCP2
#  define VCHI_MAX_BULK_CHUNK_SIZE_CCP2 0
#endif

/*
 * How many TX messages can we have pending in our transmit slots.
 * Once exhausted, vchi_msg_queue will be blocked.
 */
#ifndef VCHI_TX_MSG_QUEUE_SIZE
#  define VCHI_TX_MSG_QUEUE_SIZE           256
#endif

/*
 * How many RX messages can we have parsed in the receive slots. Once exhausted,
 * parsing will be suspended until older messages are dequeued/released.
 */
#ifndef VCHI_RX_MSG_QUEUE_SIZE
#  define VCHI_RX_MSG_QUEUE_SIZE           256
#endif

/*
 * Really should be able to cope if we run out of received message descriptors,
 * by suspending parsing as the comment above says, but we don't.
 * This sweeps the issue under the carpet.
 */
#if VCHI_RX_MSG_QUEUE_SIZE < (VCHI_MAX_MSG_SIZE / 16 + 1) * VCHI_NUM_READ_SLOTS
#  undef VCHI_RX_MSG_QUEUE_SIZE
#  define VCHI_RX_MSG_QUEUE_SIZE ((VCHI_MAX_MSG_SIZE / 16 + 1) * VCHI_NUM_READ_SLOTS)
#endif

/*
 * How many bulk transmits can we have pending. Once exhausted,
 * vchi_bulk_queue_transmit will be blocked.
 */
#ifndef VCHI_TX_BULK_QUEUE_SIZE
#  define VCHI_TX_BULK_QUEUE_SIZE           64
#endif

/*
 * How many bulk receives can we have pending. Once exhausted,
 *vchi_bulk_queue_receive will be blocked.
 */
#ifndef VCHI_RX_BULK_QUEUE_SIZE
#  define VCHI_RX_BULK_QUEUE_SIZE           64
#endif

/*
 * A limit on how many outstanding bulk requests we expect the peer to give us.
 * If the peer asks for more than this, VCHI will fail and assert.
 * The number is determined by the peer's hardware
 * - it's the number of outstanding requests that can be queued
 * on all bulk channels. VC3's MPHI peripheral allows 16.
 */
#ifndef VCHI_MAX_PEER_BULK_REQUESTS
#  define VCHI_MAX_PEER_BULK_REQUESTS       32
#endif

/*
 * Define VCHI_CCP2TX_MANUAL_POWER if the host tells us when to turn the CCP2
 * transmitter on and off.
 */
/*#define VCHI_CCP2TX_MANUAL_POWER*/

#ifndef VCHI_CCP2TX_MANUAL_POWER

/*
 * Timeout (in milliseconds) for putting the CCP2TX interface into IDLE state.
 * Set negative for no IDLE.
 */
#  ifndef VCHI_CCP2TX_IDLE_TIMEOUT
#    define VCHI_CCP2TX_IDLE_TIMEOUT        5
#  endif

/*
 * Timeout (in milliseconds) for putting the CCP2TX interface into OFF state.
 * Set negative for no OFF.
 */
#  ifndef VCHI_CCP2TX_OFF_TIMEOUT
#    define VCHI_CCP2TX_OFF_TIMEOUT         1000
#  endif

#endif /* VCHI_CCP2TX_MANUAL_POWER */

#endif /* VCHI_CFG_H_ */

/****************************** End of file **********************************/
