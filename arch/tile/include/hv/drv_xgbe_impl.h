/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/**
 * @file drivers/xgbe/impl.h
 * Implementation details for the NetIO library.
 */

#ifndef __DRV_XGBE_IMPL_H__
#define __DRV_XGBE_IMPL_H__

#include <hv/netio_errors.h>
#include <hv/netio_intf.h>
#include <hv/drv_xgbe_intf.h>


/** How many groups we have (log2). */
#define LOG2_NUM_GROUPS (12)
/** How many groups we have. */
#define NUM_GROUPS (1 << LOG2_NUM_GROUPS)

/** Number of output requests we'll buffer per tile. */
#define EPP_REQS_PER_TILE (32)

/** Words used in an eDMA command without checksum acceleration. */
#define EDMA_WDS_NO_CSUM      8
/** Words used in an eDMA command with checksum acceleration. */
#define EDMA_WDS_CSUM        10
/** Total available words in the eDMA command FIFO. */
#define EDMA_WDS_TOTAL      128


/*
 * FIXME: These definitions are internal and should have underscores!
 * NOTE: The actual numeric values here are intentional and allow us to
 * optimize the concept "if small ... else if large ... else ...", by
 * checking for the low bit being set, and then for non-zero.
 * These are used as array indices, so they must have the values (0, 1, 2)
 * in some order.
 */
#define SIZE_SMALL (1)       /**< Small packet queue. */
#define SIZE_LARGE (2)       /**< Large packet queue. */
#define SIZE_JUMBO (0)       /**< Jumbo packet queue. */

/** The number of "SIZE_xxx" values. */
#define NETIO_NUM_SIZES 3


/*
 * Default numbers of packets for IPP drivers.  These values are chosen
 * such that CIPP1 will not overflow its L2 cache.
 */

/** The default number of small packets. */
#define NETIO_DEFAULT_SMALL_PACKETS 2750
/** The default number of large packets. */
#define NETIO_DEFAULT_LARGE_PACKETS 2500
/** The default number of jumbo packets. */
#define NETIO_DEFAULT_JUMBO_PACKETS 250


/** Log2 of the size of a memory arena. */
#define NETIO_ARENA_SHIFT      24      /* 16 MB */
/** Size of a memory arena. */
#define NETIO_ARENA_SIZE       (1 << NETIO_ARENA_SHIFT)


/** A queue of packets.
 *
 * This structure partially defines a queue of packets waiting to be
 * processed.  The queue as a whole is written to by an interrupt handler and
 * read by non-interrupt code; this data structure is what's touched by the
 * interrupt handler.  The other part of the queue state, the read offset, is
 * kept in user space, not in hypervisor space, so it is in a separate data
 * structure.
 *
 * The read offset (__packet_receive_read in the user part of the queue
 * structure) points to the next packet to be read. When the read offset is
 * equal to the write offset, the queue is empty; therefore the queue must
 * contain one more slot than the required maximum queue size.
 *
 * Here's an example of all 3 state variables and what they mean.  All
 * pointers move left to right.
 *
 * @code
 *   I   I   V   V   V   V   I   I   I   I
 *   0   1   2   3   4   5   6   7   8   9  10
 *           ^       ^       ^               ^
 *           |               |               |
 *           |               |               __last_packet_plus_one
 *           |               __buffer_write
 *           __packet_receive_read
 * @endcode
 *
 * This queue has 10 slots, and thus can hold 9 packets (_last_packet_plus_one
 * = 10).  The read pointer is at 2, and the write pointer is at 6; thus,
 * there are valid, unread packets in slots 2, 3, 4, and 5.  The remaining
 * slots are invalid (do not contain a packet).
 */
typedef struct {
  /** Byte offset of the next notify packet to be written: zero for the first
   *  packet on the queue, sizeof (netio_pkt_t) for the second packet on the
   *  queue, etc. */
  volatile uint32_t __packet_write;

  /** Offset of the packet after the last valid packet (i.e., when any
   *  pointer is incremented to this value, it wraps back to zero). */
  uint32_t __last_packet_plus_one;
}
__netio_packet_queue_t;


/** A queue of buffers.
 *
 * This structure partially defines a queue of empty buffers which have been
 * obtained via requests to the IPP.  (The elements of the queue are packet
 * handles, which are transformed into a full netio_pkt_t when the buffer is
 * retrieved.)  The queue as a whole is written to by an interrupt handler and
 * read by non-interrupt code; this data structure is what's touched by the
 * interrupt handler.  The other parts of the queue state, the read offset and
 * requested write offset, are kept in user space, not in hypervisor space, so
 * they are in a separate data structure.
 *
 * The read offset (__buffer_read in the user part of the queue structure)
 * points to the next buffer to be read. When the read offset is equal to the
 * write offset, the queue is empty; therefore the queue must contain one more
 * slot than the required maximum queue size.
 *
 * The requested write offset (__buffer_requested_write in the user part of
 * the queue structure) points to the slot which will hold the next buffer we
 * request from the IPP, once we get around to sending such a request.  When
 * the requested write offset is equal to the write offset, no requests for
 * new buffers are outstanding; when the requested write offset is one greater
 * than the read offset, no more requests may be sent.
 *
 * Note that, unlike the packet_queue, the buffer_queue places incoming
 * buffers at decreasing addresses.  This makes the check for "is it time to
 * wrap the buffer pointer" cheaper in the assembly code which receives new
 * buffers, and means that the value which defines the queue size,
 * __last_buffer, is different than in the packet queue.  Also, the offset
 * used in the packet_queue is already scaled by the size of a packet; here we
 * use unscaled slot indices for the offsets.  (These differences are
 * historical, and in the future it's possible that the packet_queue will look
 * more like this queue.)
 *
 * @code
 * Here's an example of all 4 state variables and what they mean.  Remember:
 * all pointers move right to left.
 *
 *   V   V   V   I   I   R   R   V   V   V
 *   0   1   2   3   4   5   6   7   8   9
 *           ^       ^       ^           ^
 *           |       |       |           |
 *           |       |       |           __last_buffer
 *           |       |       __buffer_write
 *           |       __buffer_requested_write
 *           __buffer_read
 * @endcode
 *
 * This queue has 10 slots, and thus can hold 9 buffers (_last_buffer = 9).
 * The read pointer is at 2, and the write pointer is at 6; thus, there are
 * valid, unread buffers in slots 2, 1, 0, 9, 8, and 7.  The requested write
 * pointer is at 4; thus, requests have been made to the IPP for buffers which
 * will be placed in slots 6 and 5 when they arrive.  Finally, the remaining
 * slots are invalid (do not contain a buffer).
 */
typedef struct
{
  /** Ordinal number of the next buffer to be written: 0 for the first slot in
   *  the queue, 1 for the second slot in the queue, etc. */
  volatile uint32_t __buffer_write;

  /** Ordinal number of the last buffer (i.e., when any pointer is decremented
   *  below zero, it is reloaded with this value). */
  uint32_t __last_buffer;
}
__netio_buffer_queue_t;


/**
 * An object for providing Ethernet packets to a process.
 */
typedef struct __netio_queue_impl_t
{
  /** The queue of packets waiting to be received. */
  __netio_packet_queue_t __packet_receive_queue;
  /** The intr bit mask that IDs this device. */
  unsigned int __intr_id;
  /** Offset to queues of empty buffers, one per size. */
  uint32_t __buffer_queue[NETIO_NUM_SIZES];
  /** The address of the first EPP tile, or -1 if no EPP. */
  /* ISSUE: Actually this is always "0" or "~0". */
  uint32_t __epp_location;
  /** The queue ID that this queue represents. */
  unsigned int __queue_id;
  /** Number of acknowledgements received. */
  volatile uint32_t __acks_received;
  /** Last completion number received for packet_sendv. */
  volatile uint32_t __last_completion_rcv;
  /** Number of packets allowed to be outstanding. */
  uint32_t __max_outstanding;
  /** First VA available for packets. */
  void* __va_0;
  /** First VA in second range available for packets. */
  void* __va_1;
  /** Padding to align the "__packets" field to the size of a netio_pkt_t. */
  uint32_t __padding[3];
  /** The packets themselves. */
  netio_pkt_t __packets[0];
}
netio_queue_impl_t;


/**
 * An object for managing the user end of a NetIO queue.
 */
typedef struct __netio_queue_user_impl_t
{
  /** The next incoming packet to be read. */
  uint32_t __packet_receive_read;
  /** The next empty buffers to be read, one index per size. */
  uint8_t __buffer_read[NETIO_NUM_SIZES];
  /** Where the empty buffer we next request from the IPP will go, one index
   * per size. */
  uint8_t __buffer_requested_write[NETIO_NUM_SIZES];
  /** PCIe interface flag. */
  uint8_t __pcie;
  /** Number of packets left to be received before we send a credit update. */
  uint32_t __receive_credit_remaining;
  /** Value placed in __receive_credit_remaining when it reaches zero. */
  uint32_t __receive_credit_interval;
  /** First fast I/O routine index. */
  uint32_t __fastio_index;
  /** Number of acknowledgements expected. */
  uint32_t __acks_outstanding;
  /** Last completion number requested. */
  uint32_t __last_completion_req;
  /** File descriptor for driver. */
  int __fd;
}
netio_queue_user_impl_t;


#define NETIO_GROUP_CHUNK_SIZE   64   /**< Max # groups in one IPP request */
#define NETIO_BUCKET_CHUNK_SIZE  64   /**< Max # buckets in one IPP request */


/** Internal structure used to convey packet send information to the
 * hypervisor.  FIXME: Actually, it's not used for that anymore, but
 * netio_packet_send() still uses it internally.
 */
typedef struct
{
  uint16_t flags;              /**< Packet flags (__NETIO_SEND_FLG_xxx) */
  uint16_t transfer_size;      /**< Size of packet */
  uint32_t va;                 /**< VA of start of packet */
  __netio_pkt_handle_t handle; /**< Packet handle */
  uint32_t csum0;              /**< First checksum word */
  uint32_t csum1;              /**< Second checksum word */
}
__netio_send_cmd_t;


/** Flags used in two contexts:
 *  - As the "flags" member in the __netio_send_cmd_t, above; used only
 *    for netio_pkt_send_{prepare,commit}.
 *  - As part of the flags passed to the various send packet fast I/O calls.
 */

/** Need acknowledgement on this packet.  Note that some code in the
 *  normal send_pkt fast I/O handler assumes that this is equal to 1. */
#define __NETIO_SEND_FLG_ACK    0x1

/** Do checksum on this packet.  (Only used with the __netio_send_cmd_t;
 *  normal packet sends use a special fast I/O index to denote checksumming,
 *  and multi-segment sends test the checksum descriptor.) */
#define __NETIO_SEND_FLG_CSUM   0x2

/** Get a completion on this packet.  Only used with multi-segment sends.  */
#define __NETIO_SEND_FLG_COMPLETION 0x4

/** Position of the number-of-extra-segments value in the flags word.
    Only used with multi-segment sends. */
#define __NETIO_SEND_FLG_XSEG_SHIFT 3

/** Width of the number-of-extra-segments value in the flags word. */
#define __NETIO_SEND_FLG_XSEG_WIDTH 2

#endif /* __DRV_XGBE_IMPL_H__ */
