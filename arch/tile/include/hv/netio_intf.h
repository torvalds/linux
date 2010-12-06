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
 * NetIO interface structures and macros.
 */

#ifndef __NETIO_INTF_H__
#define __NETIO_INTF_H__

#include <hv/netio_errors.h>

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#if !defined(__HV__) && !defined(__BOGUX__) && !defined(__KERNEL__)
#include <assert.h>
#define netio_assert assert  /**< Enable assertions from macros */
#else
#define netio_assert(...) ((void)(0))  /**< Disable assertions from macros */
#endif

/*
 * If none of these symbols are defined, we're building libnetio in an
 * environment where we have pthreads, so we'll enable locking.
 */
#if !defined(__HV__) && !defined(__BOGUX__) && !defined(__KERNEL__) && \
    !defined(__NEWLIB__)
#define _NETIO_PTHREAD       /**< Include a mutex in netio_queue_t below */

/*
 * If NETIO_UNLOCKED is defined, we don't do use per-cpu locks on
 * per-packet NetIO operations.  We still do pthread locking on things
 * like netio_input_register, though.  This is used for building
 * libnetio_unlocked.
 */
#ifndef NETIO_UNLOCKED

/* Avoid PLT overhead by using our own inlined per-cpu lock. */
#include <sched.h>
typedef int _netio_percpu_mutex_t;

static __inline int
_netio_percpu_mutex_init(_netio_percpu_mutex_t* lock)
{
  *lock = 0;
  return 0;
}

static __inline int
_netio_percpu_mutex_lock(_netio_percpu_mutex_t* lock)
{
  while (__builtin_expect(__insn_tns(lock), 0))
    sched_yield();
  return 0;
}

static __inline int
_netio_percpu_mutex_unlock(_netio_percpu_mutex_t* lock)
{
  *lock = 0;
  return 0;
}

#else /* NETIO_UNLOCKED */

/* Don't do any locking for per-packet NetIO operations. */
typedef int _netio_percpu_mutex_t;
#define _netio_percpu_mutex_init(L)
#define _netio_percpu_mutex_lock(L)
#define _netio_percpu_mutex_unlock(L)

#endif /* NETIO_UNLOCKED */
#endif /* !__HV__, !__BOGUX, !__KERNEL__, !__NEWLIB__ */

/** How many tiles can register for a given queue.
 *  @ingroup setup */
#define NETIO_MAX_TILES_PER_QUEUE  64


/** Largest permissible queue identifier.
 *  @ingroup setup  */
#define NETIO_MAX_QUEUE_ID        255


#ifndef __DOXYGEN__

/* Metadata packet checksum/ethertype flags. */

/** The L4 checksum has not been calculated. */
#define _NETIO_PKT_NO_L4_CSUM_SHIFT           0
#define _NETIO_PKT_NO_L4_CSUM_RMASK           1
#define _NETIO_PKT_NO_L4_CSUM_MASK \
         (_NETIO_PKT_NO_L4_CSUM_RMASK << _NETIO_PKT_NO_L4_CSUM_SHIFT)

/** The L3 checksum has not been calculated. */
#define _NETIO_PKT_NO_L3_CSUM_SHIFT           1
#define _NETIO_PKT_NO_L3_CSUM_RMASK           1
#define _NETIO_PKT_NO_L3_CSUM_MASK \
         (_NETIO_PKT_NO_L3_CSUM_RMASK << _NETIO_PKT_NO_L3_CSUM_SHIFT)

/** The L3 checksum is incorrect (or perhaps has not been calculated). */
#define _NETIO_PKT_BAD_L3_CSUM_SHIFT          2
#define _NETIO_PKT_BAD_L3_CSUM_RMASK          1
#define _NETIO_PKT_BAD_L3_CSUM_MASK \
         (_NETIO_PKT_BAD_L3_CSUM_RMASK << _NETIO_PKT_BAD_L3_CSUM_SHIFT)

/** The Ethernet packet type is unrecognized. */
#define _NETIO_PKT_TYPE_UNRECOGNIZED_SHIFT    3
#define _NETIO_PKT_TYPE_UNRECOGNIZED_RMASK    1
#define _NETIO_PKT_TYPE_UNRECOGNIZED_MASK \
         (_NETIO_PKT_TYPE_UNRECOGNIZED_RMASK << \
          _NETIO_PKT_TYPE_UNRECOGNIZED_SHIFT)

/* Metadata packet type flags. */

/** Where the packet type bits are; this field is the index into
 *  _netio_pkt_info. */
#define _NETIO_PKT_TYPE_SHIFT        4
#define _NETIO_PKT_TYPE_RMASK        0x3F

/** How many VLAN tags the packet has, and, if we have two, which one we
 *  actually grouped on.  A VLAN within a proprietary (Marvell or Broadcom)
 *  tag is counted here. */
#define _NETIO_PKT_VLAN_SHIFT        4
#define _NETIO_PKT_VLAN_RMASK        0x3
#define _NETIO_PKT_VLAN_MASK \
         (_NETIO_PKT_VLAN_RMASK << _NETIO_PKT_VLAN_SHIFT)
#define _NETIO_PKT_VLAN_NONE         0   /* No VLAN tag. */
#define _NETIO_PKT_VLAN_ONE          1   /* One VLAN tag. */
#define _NETIO_PKT_VLAN_TWO_OUTER    2   /* Two VLAN tags, outer one used. */
#define _NETIO_PKT_VLAN_TWO_INNER    3   /* Two VLAN tags, inner one used. */

/** Which proprietary tags the packet has. */
#define _NETIO_PKT_TAG_SHIFT         6
#define _NETIO_PKT_TAG_RMASK         0x3
#define _NETIO_PKT_TAG_MASK \
          (_NETIO_PKT_TAG_RMASK << _NETIO_PKT_TAG_SHIFT)
#define _NETIO_PKT_TAG_NONE          0   /* No proprietary tags. */
#define _NETIO_PKT_TAG_MRVL          1   /* Marvell HyperG.Stack tags. */
#define _NETIO_PKT_TAG_MRVL_EXT      2   /* HyperG.Stack extended tags. */
#define _NETIO_PKT_TAG_BRCM          3   /* Broadcom HiGig tags. */

/** Whether a packet has an LLC + SNAP header. */
#define _NETIO_PKT_SNAP_SHIFT        8
#define _NETIO_PKT_SNAP_RMASK        0x1
#define _NETIO_PKT_SNAP_MASK \
          (_NETIO_PKT_SNAP_RMASK << _NETIO_PKT_SNAP_SHIFT)

/* NOTE: Bits 9 and 10 are unused. */

/** Length of any custom data before the L2 header, in words. */
#define _NETIO_PKT_CUSTOM_LEN_SHIFT  11
#define _NETIO_PKT_CUSTOM_LEN_RMASK  0x1F
#define _NETIO_PKT_CUSTOM_LEN_MASK \
          (_NETIO_PKT_CUSTOM_LEN_RMASK << _NETIO_PKT_CUSTOM_LEN_SHIFT)

/** The L4 checksum is incorrect (or perhaps has not been calculated). */
#define _NETIO_PKT_BAD_L4_CSUM_SHIFT 16
#define _NETIO_PKT_BAD_L4_CSUM_RMASK 0x1
#define _NETIO_PKT_BAD_L4_CSUM_MASK \
          (_NETIO_PKT_BAD_L4_CSUM_RMASK << _NETIO_PKT_BAD_L4_CSUM_SHIFT)

/** Length of the L2 header, in words. */
#define _NETIO_PKT_L2_LEN_SHIFT  17
#define _NETIO_PKT_L2_LEN_RMASK  0x1F
#define _NETIO_PKT_L2_LEN_MASK \
          (_NETIO_PKT_L2_LEN_RMASK << _NETIO_PKT_L2_LEN_SHIFT)


/* Flags in minimal packet metadata. */

/** We need an eDMA checksum on this packet. */
#define _NETIO_PKT_NEED_EDMA_CSUM_SHIFT            0
#define _NETIO_PKT_NEED_EDMA_CSUM_RMASK            1
#define _NETIO_PKT_NEED_EDMA_CSUM_MASK \
         (_NETIO_PKT_NEED_EDMA_CSUM_RMASK << _NETIO_PKT_NEED_EDMA_CSUM_SHIFT)

/* Data within the packet information table. */

/* Note that, for efficiency, code which uses these fields assumes that none
 * of the shift values below are zero.  See uses below for an explanation. */

/** Offset within the L2 header of the innermost ethertype (in halfwords). */
#define _NETIO_PKT_INFO_ETYPE_SHIFT       6
#define _NETIO_PKT_INFO_ETYPE_RMASK    0x1F

/** Offset within the L2 header of the VLAN tag (in halfwords). */
#define _NETIO_PKT_INFO_VLAN_SHIFT       11
#define _NETIO_PKT_INFO_VLAN_RMASK     0x1F

#endif


/** The size of a memory buffer representing a small packet.
 *  @ingroup egress */
#define SMALL_PACKET_SIZE 256

/** The size of a memory buffer representing a large packet.
 *  @ingroup egress */
#define LARGE_PACKET_SIZE 2048

/** The size of a memory buffer representing a jumbo packet.
 *  @ingroup egress */
#define JUMBO_PACKET_SIZE (12 * 1024)


/* Common ethertypes.
 * @ingroup ingress */
/** @{ */
/** The ethertype of IPv4. */
#define ETHERTYPE_IPv4 (0x0800)
/** The ethertype of ARP. */
#define ETHERTYPE_ARP (0x0806)
/** The ethertype of VLANs. */
#define ETHERTYPE_VLAN (0x8100)
/** The ethertype of a Q-in-Q header. */
#define ETHERTYPE_Q_IN_Q (0x9100)
/** The ethertype of IPv6. */
#define ETHERTYPE_IPv6 (0x86DD)
/** The ethertype of MPLS. */
#define ETHERTYPE_MPLS (0x8847)
/** @} */


/** The possible return values of NETIO_PKT_STATUS.
 * @ingroup ingress
 */
typedef enum
{
  /** No problems were detected with this packet. */
  NETIO_PKT_STATUS_OK,
  /** The packet is undersized; this is expected behavior if the packet's
    * ethertype is unrecognized, but otherwise the packet is likely corrupt. */
  NETIO_PKT_STATUS_UNDERSIZE,
  /** The packet is oversized and some trailing bytes have been discarded.
      This is expected behavior for short packets, since it's impossible to
      precisely determine the amount of padding which may have been added to
      them to make them meet the minimum Ethernet packet size. */
  NETIO_PKT_STATUS_OVERSIZE,
  /** The packet was judged to be corrupt by hardware (for instance, it had
      a bad CRC, or part of it was discarded due to lack of buffer space in
      the I/O shim) and should be discarded. */
  NETIO_PKT_STATUS_BAD
} netio_pkt_status_t;


/** Log2 of how many buckets we have. */
#define NETIO_LOG2_NUM_BUCKETS (10)

/** How many buckets we have.
 * @ingroup ingress */
#define NETIO_NUM_BUCKETS (1 << NETIO_LOG2_NUM_BUCKETS)


/**
 * @brief A group-to-bucket identifier.
 *
 * @ingroup setup
 *
 * This tells us what to do with a given group.
 */
typedef union {
  /** The header broken down into bits. */
  struct {
    /** Whether we should balance on L4, if available */
    unsigned int __balance_on_l4:1;
    /** Whether we should balance on L3, if available */
    unsigned int __balance_on_l3:1;
    /** Whether we should balance on L2, if available */
    unsigned int __balance_on_l2:1;
    /** Reserved for future use */
    unsigned int __reserved:1;
    /** The base bucket to use to send traffic */
    unsigned int __bucket_base:NETIO_LOG2_NUM_BUCKETS;
    /** The mask to apply to the balancing value. This must be one less
     * than a power of two, e.g. 0x3 or 0xFF.
     */
    unsigned int __bucket_mask:NETIO_LOG2_NUM_BUCKETS;
    /** Pad to 32 bits */
    unsigned int __padding:(32 - 4 - 2 * NETIO_LOG2_NUM_BUCKETS);
  } bits;
  /** To send out the IDN. */
  unsigned int word;
}
netio_group_t;


/**
 * @brief A VLAN-to-bucket identifier.
 *
 * @ingroup setup
 *
 * This tells us what to do with a given VLAN.
 */
typedef netio_group_t netio_vlan_t;


/**
 * A bucket-to-queue mapping.
 * @ingroup setup
 */
typedef unsigned char netio_bucket_t;


/**
 * A packet size can always fit in a netio_size_t.
 * @ingroup setup
 */
typedef unsigned int netio_size_t;


/**
 * @brief Ethernet standard (ingress) packet metadata.
 *
 * @ingroup ingress
 *
 * This is additional data associated with each packet.
 * This structure is opaque and accessed through the @ref ingress.
 *
 * Also, the buffer population operation currently assumes that standard
 * metadata is at least as large as minimal metadata, and will need to be
 * modified if that is no longer the case.
 */
typedef struct
{
#ifdef __DOXYGEN__
  /** This structure is opaque. */
  unsigned char opaque[24];
#else
  /** The overall ordinal of the packet */
  unsigned int __packet_ordinal;
  /** The ordinal of the packet within the group */
  unsigned int __group_ordinal;
  /** The best flow hash IPP could compute. */
  unsigned int __flow_hash;
  /** Flags pertaining to checksum calculation, packet type, etc. */
  unsigned int __flags;
  /** The first word of "user data". */
  unsigned int __user_data_0;
  /** The second word of "user data". */
  unsigned int __user_data_1;
#endif
}
netio_pkt_metadata_t;


/** To ensure that the L3 header is aligned mod 4, the L2 header should be
 * aligned mod 4 plus 2, since every supported L2 header is 4n + 2 bytes
 * long.  The standard way to do this is to simply add 2 bytes of padding
 * before the L2 header.
 */
#define NETIO_PACKET_PADDING 2



/**
 * @brief Ethernet minimal (egress) packet metadata.
 *
 * @ingroup egress
 *
 * This structure represents information about packets which have
 * been processed by @ref netio_populate_buffer() or
 * @ref netio_populate_prepend_buffer().  This structure is opaque
 * and accessed through the @ref egress.
 *
 * @internal This structure is actually copied into the memory used by
 * standard metadata, which is assumed to be large enough.
 */
typedef struct
{
#ifdef __DOXYGEN__
  /** This structure is opaque. */
  unsigned char opaque[14];
#else
  /** The offset of the L2 header from the start of the packet data. */
  unsigned short l2_offset;
  /** The offset of the L3 header from the start of the packet data. */
  unsigned short l3_offset;
  /** Where to write the checksum. */
  unsigned char csum_location;
  /** Where to start checksumming from. */
  unsigned char csum_start;
  /** Flags pertaining to checksum calculation etc. */
  unsigned short flags;
  /** The L2 length of the packet. */
  unsigned short l2_length;
  /** The checksum with which to seed the checksum generator. */
  unsigned short csum_seed;
  /** How much to checksum. */
  unsigned short csum_length;
#endif
}
netio_pkt_minimal_metadata_t;


#ifndef __DOXYGEN__

/**
 * @brief An I/O notification header.
 *
 * This is the first word of data received from an I/O shim in a notification
 * packet. It contains framing and status information.
 */
typedef union
{
  unsigned int word; /**< The whole word. */
  /** The various fields. */
  struct
  {
    unsigned int __channel:7;    /**< Resource channel. */
    unsigned int __type:4;       /**< Type. */
    unsigned int __ack:1;        /**< Whether an acknowledgement is needed. */
    unsigned int __reserved:1;   /**< Reserved. */
    unsigned int __protocol:1;   /**< A protocol-specific word is added. */
    unsigned int __status:2;     /**< Status of the transfer. */
    unsigned int __framing:2;    /**< Framing of the transfer. */
    unsigned int __transfer_size:14; /**< Transfer size in bytes (total). */
  } bits;
}
__netio_pkt_notif_t;


/**
 * Returns the base address of the packet.
 */
#define _NETIO_PKT_HANDLE_BASE(p) \
  ((unsigned char*)((p).word & 0xFFFFFFC0))

/**
 * Returns the base address of the packet.
 */
#define _NETIO_PKT_BASE(p) \
  _NETIO_PKT_HANDLE_BASE(p->__packet)

/**
 * @brief An I/O notification packet (second word)
 *
 * This is the second word of data received from an I/O shim in a notification
 * packet.  This is the virtual address of the packet buffer, plus some flag
 * bits.  (The virtual address of the packet is always 256-byte aligned so we
 * have room for 8 bits' worth of flags in the low 8 bits.)
 *
 * @internal
 * NOTE: The low two bits must contain "__queue", so the "packet size"
 * (SIZE_SMALL, SIZE_LARGE, or SIZE_JUMBO) can be determined quickly.
 *
 * If __addr or __offset are moved, _NETIO_PKT_BASE
 * (defined right below this) must be changed.
 */
typedef union
{
  unsigned int word; /**< The whole word. */
  /** The various fields. */
  struct
  {
    /** Which queue the packet will be returned to once it is sent back to
        the IPP.  This is one of the SIZE_xxx values. */
    unsigned int __queue:2;

    /** The IPP handle of the sending IPP. */
    unsigned int __ipp_handle:2;

    /** Reserved for future use. */
    unsigned int __reserved:1;

    /** If 1, this packet has minimal (egress) metadata; otherwise, it
        has standard (ingress) metadata. */
    unsigned int __minimal:1;

    /** Offset of the metadata within the packet.  This value is multiplied
     *  by 64 and added to the base packet address to get the metadata
     *  address.  Note that this field is aligned within the word such that
     *  you can easily extract the metadata address with a 26-bit mask. */
    unsigned int __offset:2;

    /** The top 24 bits of the packet's virtual address. */
    unsigned int __addr:24;
  } bits;
}
__netio_pkt_handle_t;

#endif /* !__DOXYGEN__ */


/**
 * @brief A handle for an I/O packet's storage.
 * @ingroup ingress
 *
 * netio_pkt_handle_t encodes the concept of a ::netio_pkt_t with its
 * packet metadata removed.  It is a much smaller type that exists to
 * facilitate applications where the full ::netio_pkt_t type is too
 * large, such as those that cache enormous numbers of packets or wish
 * to transmit packet descriptors over the UDN.
 *
 * Because there is no metadata, most ::netio_pkt_t operations cannot be
 * performed on a netio_pkt_handle_t.  It supports only
 * netio_free_handle() (to free the buffer) and
 * NETIO_PKT_CUSTOM_DATA_H() (to access a pointer to its contents).
 * The application must acquire any additional metadata it wants from the
 * original ::netio_pkt_t and record it separately.
 *
 * A netio_pkt_handle_t can be extracted from a ::netio_pkt_t by calling
 * NETIO_PKT_HANDLE().  An invalid handle (analogous to NULL) can be
 * created by assigning the value ::NETIO_PKT_HANDLE_NONE. A handle can
 * be tested for validity with NETIO_PKT_HANDLE_IS_VALID().
 */
typedef struct
{
  unsigned int word; /**< Opaque bits. */
} netio_pkt_handle_t;

/**
 * @brief A packet descriptor.
 *
 * @ingroup ingress
 * @ingroup egress
 *
 * This data structure represents a packet.  The structure is manipulated
 * through the @ref ingress and the @ref egress.
 *
 * While the contents of a netio_pkt_t are opaque, the structure itself is
 * portable.  This means that it may be shared between all tiles which have
 * done a netio_input_register() call for the interface on which the pkt_t
 * was initially received (via netio_get_packet()) or retrieved (via
 * netio_get_buffer()).  The contents of a netio_pkt_t can be transmitted to
 * another tile via shared memory, or via a UDN message, or by other means.
 * The destination tile may then use the pkt_t as if it had originally been
 * received locally; it may read or write the packet's data, read its
 * metadata, free the packet, send the packet, transfer the netio_pkt_t to
 * yet another tile, and so forth.
 *
 * Once a netio_pkt_t has been transferred to a second tile, the first tile
 * should not reference the original copy; in particular, if more than one
 * tile frees or sends the same netio_pkt_t, the IPP's packet free lists will
 * become corrupted.  Note also that each tile which reads or modifies
 * packet data must obey the memory coherency rules outlined in @ref input.
 */
typedef struct
{
#ifdef __DOXYGEN__
  /** This structure is opaque. */
  unsigned char opaque[32];
#else
  /** For an ingress packet (one with standard metadata), this is the
   *  notification header we got from the I/O shim.  For an egress packet
   *  (one with minimal metadata), this word is zero if the packet has not
   *  been populated, and nonzero if it has. */
  __netio_pkt_notif_t __notif_header;

  /** Virtual address of the packet buffer, plus state flags. */
  __netio_pkt_handle_t __packet;

  /** Metadata associated with the packet. */
  netio_pkt_metadata_t __metadata;
#endif
}
netio_pkt_t;


#ifndef __DOXYGEN__

#define __NETIO_PKT_NOTIF_HEADER(pkt) ((pkt)->__notif_header)
#define __NETIO_PKT_IPP_HANDLE(pkt) ((pkt)->__packet.bits.__ipp_handle)
#define __NETIO_PKT_QUEUE(pkt) ((pkt)->__packet.bits.__queue)
#define __NETIO_PKT_NOTIF_HEADER_M(mda, pkt) ((pkt)->__notif_header)
#define __NETIO_PKT_IPP_HANDLE_M(mda, pkt) ((pkt)->__packet.bits.__ipp_handle)
#define __NETIO_PKT_MINIMAL(pkt) ((pkt)->__packet.bits.__minimal)
#define __NETIO_PKT_QUEUE_M(mda, pkt) ((pkt)->__packet.bits.__queue)
#define __NETIO_PKT_FLAGS_M(mda, pkt) ((mda)->__flags)

/* Packet information table, used by the attribute access functions below. */
extern const uint16_t _netio_pkt_info[];

#endif /* __DOXYGEN__ */


#ifndef __DOXYGEN__
/* These macros are deprecated and will disappear in a future MDE release. */
#define NETIO_PKT_GOOD_CHECKSUM(pkt) \
  NETIO_PKT_L4_CSUM_CORRECT(pkt)
#define NETIO_PKT_GOOD_CHECKSUM_M(mda, pkt) \
  NETIO_PKT_L4_CSUM_CORRECT_M(mda, pkt)
#endif /* __DOXYGEN__ */


/* Packet attribute access functions. */

/** Return a pointer to the metadata for a packet.
 * @ingroup ingress
 *
 * Calling this function once and passing the result to other retrieval
 * functions with a "_M" suffix usually improves performance.  This
 * function must be called on an 'ingress' packet (i.e. one retrieved
 * by @ref netio_get_packet(), on which @ref netio_populate_buffer() or
 * @ref netio_populate_prepend_buffer have not been called). Use of this
 * function on an 'egress' packet will cause an assertion failure.
 *
 * @param[in] pkt Packet on which to operate.
 * @return A pointer to the packet's standard metadata.
 */
static __inline netio_pkt_metadata_t*
NETIO_PKT_METADATA(netio_pkt_t* pkt)
{
  netio_assert(!pkt->__packet.bits.__minimal);
  return &pkt->__metadata;
}


/** Return a pointer to the minimal metadata for a packet.
 * @ingroup egress
 *
 * Calling this function once and passing the result to other retrieval
 * functions with a "_MM" suffix usually improves performance.  This
 * function must be called on an 'egress' packet (i.e. one on which
 * @ref netio_populate_buffer() or @ref netio_populate_prepend_buffer()
 * have been called, or one retrieved by @ref netio_get_buffer()). Use of
 * this function on an 'ingress' packet will cause an assertion failure.
 *
 * @param[in] pkt Packet on which to operate.
 * @return A pointer to the packet's standard metadata.
 */
static __inline netio_pkt_minimal_metadata_t*
NETIO_PKT_MINIMAL_METADATA(netio_pkt_t* pkt)
{
  netio_assert(pkt->__packet.bits.__minimal);
  return (netio_pkt_minimal_metadata_t*) &pkt->__metadata;
}


/** Determine whether a packet has 'minimal' metadata.
 * @ingroup pktfuncs
 *
 * This function will return nonzero if the packet is an 'egress'
 * packet (i.e. one on which @ref netio_populate_buffer() or
 * @ref netio_populate_prepend_buffer() have been called, or one
 * retrieved by @ref netio_get_buffer()), and zero if the packet
 * is an 'ingress' packet (i.e. one retrieved by @ref netio_get_packet(),
 * which has not been converted into an 'egress' packet).
 *
 * @param[in] pkt Packet on which to operate.
 * @return Nonzero if the packet has minimal metadata.
 */
static __inline unsigned int
NETIO_PKT_IS_MINIMAL(netio_pkt_t* pkt)
{
  return pkt->__packet.bits.__minimal;
}


/** Return a handle for a packet's storage.
 * @ingroup pktfuncs
 *
 * @param[in] pkt Packet on which to operate.
 * @return A handle for the packet's storage.
 */
static __inline netio_pkt_handle_t
NETIO_PKT_HANDLE(netio_pkt_t* pkt)
{
  netio_pkt_handle_t h;
  h.word = pkt->__packet.word;
  return h;
}


/** A special reserved value indicating the absence of a packet handle.
 *
 * @ingroup pktfuncs
 */
#define NETIO_PKT_HANDLE_NONE ((netio_pkt_handle_t) { 0 })


/** Test whether a packet handle is valid.
 *
 * Applications may wish to use the reserved value NETIO_PKT_HANDLE_NONE
 * to indicate no packet at all.  This function tests to see if a packet
 * handle is a real handle, not this special reserved value.
 *
 * @ingroup pktfuncs
 *
 * @param[in] handle Handle on which to operate.
 * @return One if the packet handle is valid, else zero.
 */
static __inline unsigned int
NETIO_PKT_HANDLE_IS_VALID(netio_pkt_handle_t handle)
{
  return handle.word != 0;
}



/** Return a pointer to the start of the packet's custom header.
 *  A custom header may or may not be present, depending upon the IPP; its
 *  contents and alignment are also IPP-dependent.  Currently, none of the
 *  standard IPPs supplied by Tilera produce a custom header.  If present,
 *  the custom header precedes the L2 header in the packet buffer.
 * @ingroup ingress
 *
 * @param[in] handle Handle on which to operate.
 * @return A pointer to start of the packet.
 */
static __inline unsigned char*
NETIO_PKT_CUSTOM_DATA_H(netio_pkt_handle_t handle)
{
  return _NETIO_PKT_HANDLE_BASE(handle) + NETIO_PACKET_PADDING;
}


/** Return the length of the packet's custom header.
 *  A custom header may or may not be present, depending upon the IPP; its
 *  contents and alignment are also IPP-dependent.  Currently, none of the
 *  standard IPPs supplied by Tilera produce a custom header.  If present,
 *  the custom header precedes the L2 header in the packet buffer.
 *
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The length of the packet's custom header, in bytes.
 */
static __inline netio_size_t
NETIO_PKT_CUSTOM_HEADER_LENGTH_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  /*
   * Note that we effectively need to extract a quantity from the flags word
   * which is measured in words, and then turn it into bytes by shifting
   * it left by 2.  We do this all at once by just shifting right two less
   * bits, and shifting the mask up two bits.
   */
  return ((mda->__flags >> (_NETIO_PKT_CUSTOM_LEN_SHIFT - 2)) &
          (_NETIO_PKT_CUSTOM_LEN_RMASK << 2));
}


/** Return the length of the packet, starting with the custom header.
 *  A custom header may or may not be present, depending upon the IPP; its
 *  contents and alignment are also IPP-dependent.  Currently, none of the
 *  standard IPPs supplied by Tilera produce a custom header.  If present,
 *  the custom header precedes the L2 header in the packet buffer.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The length of the packet, in bytes.
 */
static __inline netio_size_t
NETIO_PKT_CUSTOM_LENGTH_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return (__NETIO_PKT_NOTIF_HEADER(pkt).bits.__transfer_size -
          NETIO_PACKET_PADDING);
}


/** Return a pointer to the start of the packet's custom header.
 *  A custom header may or may not be present, depending upon the IPP; its
 *  contents and alignment are also IPP-dependent.  Currently, none of the
 *  standard IPPs supplied by Tilera produce a custom header.  If present,
 *  the custom header precedes the L2 header in the packet buffer.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return A pointer to start of the packet.
 */
static __inline unsigned char*
NETIO_PKT_CUSTOM_DATA_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return NETIO_PKT_CUSTOM_DATA_H(NETIO_PKT_HANDLE(pkt));
}


/** Return the length of the packet's L2 (Ethernet plus VLAN or SNAP) header.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The length of the packet's L2 header, in bytes.
 */
static __inline netio_size_t
NETIO_PKT_L2_HEADER_LENGTH_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  /*
   * Note that we effectively need to extract a quantity from the flags word
   * which is measured in words, and then turn it into bytes by shifting
   * it left by 2.  We do this all at once by just shifting right two less
   * bits, and shifting the mask up two bits.  We then add two bytes.
   */
  return ((mda->__flags >> (_NETIO_PKT_L2_LEN_SHIFT - 2)) &
          (_NETIO_PKT_L2_LEN_RMASK << 2)) + 2;
}


/** Return the length of the packet, starting with the L2 (Ethernet) header.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The length of the packet, in bytes.
 */
static __inline netio_size_t
NETIO_PKT_L2_LENGTH_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return (NETIO_PKT_CUSTOM_LENGTH_M(mda, pkt) -
          NETIO_PKT_CUSTOM_HEADER_LENGTH_M(mda,pkt));
}


/** Return a pointer to the start of the packet's L2 (Ethernet) header.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return A pointer to start of the packet.
 */
static __inline unsigned char*
NETIO_PKT_L2_DATA_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return (NETIO_PKT_CUSTOM_DATA_M(mda, pkt) +
          NETIO_PKT_CUSTOM_HEADER_LENGTH_M(mda, pkt));
}


/** Retrieve the length of the packet, starting with the L3 (generally,
 *  the IP) header.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return Length of the packet's L3 header and data, in bytes.
 */
static __inline netio_size_t
NETIO_PKT_L3_LENGTH_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return (NETIO_PKT_L2_LENGTH_M(mda, pkt) -
          NETIO_PKT_L2_HEADER_LENGTH_M(mda,pkt));
}


/** Return a pointer to the packet's L3 (generally, the IP) header.
 * @ingroup ingress
 *
 * Note that we guarantee word alignment of the L3 header.
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return A pointer to the packet's L3 header.
 */
static __inline unsigned char*
NETIO_PKT_L3_DATA_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return (NETIO_PKT_L2_DATA_M(mda, pkt) +
          NETIO_PKT_L2_HEADER_LENGTH_M(mda, pkt));
}


/** Return the ordinal of the packet.
 * @ingroup ingress
 *
 * Each packet is given an ordinal number when it is delivered by the IPP.
 * In the medium term, the ordinal is unique and monotonically increasing,
 * being incremented by 1 for each packet; the ordinal of the first packet
 * delivered after the IPP starts is zero.  (Since the ordinal is of finite
 * size, given enough input packets, it will eventually wrap around to zero;
 * in the long term, therefore, ordinals are not unique.)  The ordinals
 * handed out by different IPPs are not disjoint, so two packets from
 * different IPPs may have identical ordinals.  Packets dropped by the
 * IPP or by the I/O shim are not assigned ordinals.
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The packet's per-IPP packet ordinal.
 */
static __inline unsigned int
NETIO_PKT_ORDINAL_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return mda->__packet_ordinal;
}


/** Return the per-group ordinal of the packet.
 * @ingroup ingress
 *
 * Each packet is given a per-group ordinal number when it is
 * delivered by the IPP. By default, the group is the packet's VLAN,
 * although IPP can be recompiled to use different values.  In
 * the medium term, the ordinal is unique and monotonically
 * increasing, being incremented by 1 for each packet; the ordinal of
 * the first packet distributed to a particular group is zero.
 * (Since the ordinal is of finite size, given enough input packets,
 * it will eventually wrap around to zero; in the long term,
 * therefore, ordinals are not unique.)  The ordinals handed out by
 * different IPPs are not disjoint, so two packets from different IPPs
 * may have identical ordinals; similarly, packets distributed to
 * different groups may have identical ordinals.  Packets dropped by
 * the IPP or by the I/O shim are not assigned ordinals.
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The packet's per-IPP, per-group ordinal.
 */
static __inline unsigned int
NETIO_PKT_GROUP_ORDINAL_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return mda->__group_ordinal;
}


/** Return the VLAN ID assigned to the packet.
 * @ingroup ingress
 *
 * This value is usually contained within the packet header.
 *
 * This value will be zero if the packet does not have a VLAN tag, or if
 * this value was not extracted from the packet.
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The packet's VLAN ID.
 */
static __inline unsigned short
NETIO_PKT_VLAN_ID_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  int vl = (mda->__flags >> _NETIO_PKT_VLAN_SHIFT) & _NETIO_PKT_VLAN_RMASK;
  unsigned short* pkt_p;
  int index;
  unsigned short val;

  if (vl == _NETIO_PKT_VLAN_NONE)
    return 0;

  pkt_p = (unsigned short*) NETIO_PKT_L2_DATA_M(mda, pkt);
  index = (mda->__flags >> _NETIO_PKT_TYPE_SHIFT) & _NETIO_PKT_TYPE_RMASK;

  val = pkt_p[(_netio_pkt_info[index] >> _NETIO_PKT_INFO_VLAN_SHIFT) &
              _NETIO_PKT_INFO_VLAN_RMASK];

#ifdef __TILECC__
  return (__insn_bytex(val) >> 16) & 0xFFF;
#else
  return (__builtin_bswap32(val) >> 16) & 0xFFF;
#endif
}


/** Return the ethertype of the packet.
 * @ingroup ingress
 *
 * This value is usually contained within the packet header.
 *
 * This value is reliable if @ref NETIO_PKT_ETHERTYPE_RECOGNIZED_M()
 * returns true, and otherwise, may not be well defined.
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The packet's ethertype.
 */
static __inline unsigned short
NETIO_PKT_ETHERTYPE_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  unsigned short* pkt_p = (unsigned short*) NETIO_PKT_L2_DATA_M(mda, pkt);
  int index = (mda->__flags >> _NETIO_PKT_TYPE_SHIFT) & _NETIO_PKT_TYPE_RMASK;

  unsigned short val =
    pkt_p[(_netio_pkt_info[index] >> _NETIO_PKT_INFO_ETYPE_SHIFT) &
          _NETIO_PKT_INFO_ETYPE_RMASK];

  return __builtin_bswap32(val) >> 16;
}


/** Return the flow hash computed on the packet.
 * @ingroup ingress
 *
 * For TCP and UDP packets, this hash is calculated by hashing together
 * the "5-tuple" values, specifically the source IP address, destination
 * IP address, protocol type, source port and destination port.
 * The hash value is intended to be helpful for millions of distinct
 * flows.
 *
 * For IPv4 or IPv6 packets which are neither TCP nor UDP, the flow hash is
 * derived by hashing together the source and destination IP addresses.
 *
 * For MPLS-encapsulated packets, the flow hash is derived by hashing
 * the first MPLS label.
 *
 * For all other packets the flow hash is computed from the source
 * and destination Ethernet addresses.
 *
 * The hash is symmetric, meaning it produces the same value if the
 * source and destination are swapped. The only exceptions are
 * tunneling protocols 0x04 (IP in IP Encapsulation), 0x29 (Simple
 * Internet Protocol), 0x2F (General Routing Encapsulation) and 0x32
 * (Encap Security Payload), which use only the destination address
 * since the source address is not meaningful.
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The packet's 32-bit flow hash.
 */
static __inline unsigned int
NETIO_PKT_FLOW_HASH_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return mda->__flow_hash;
}


/** Return the first word of "user data" for the packet.
 *
 * The contents of the user data words depend on the IPP.
 *
 * When using the standard ipp1, ipp2, or ipp4 sub-drivers, the first
 * word of user data contains the least significant bits of the 64-bit
 * arrival cycle count (see @c get_cycle_count_low()).
 *
 * See the <em>System Programmer's Guide</em> for details.
 *
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The packet's first word of "user data".
 */
static __inline unsigned int
NETIO_PKT_USER_DATA_0_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return mda->__user_data_0;
}


/** Return the second word of "user data" for the packet.
 *
 * The contents of the user data words depend on the IPP.
 *
 * When using the standard ipp1, ipp2, or ipp4 sub-drivers, the second
 * word of user data contains the most significant bits of the 64-bit
 * arrival cycle count (see @c get_cycle_count_high()).
 *
 * See the <em>System Programmer's Guide</em> for details.
 *
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The packet's second word of "user data".
 */
static __inline unsigned int
NETIO_PKT_USER_DATA_1_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return mda->__user_data_1;
}


/** Determine whether the L4 (TCP/UDP) checksum was calculated.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return Nonzero if the L4 checksum was calculated.
 */
static __inline unsigned int
NETIO_PKT_L4_CSUM_CALCULATED_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return !(mda->__flags & _NETIO_PKT_NO_L4_CSUM_MASK);
}


/** Determine whether the L4 (TCP/UDP) checksum was calculated and found to
 *  be correct.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return Nonzero if the checksum was calculated and is correct.
 */
static __inline unsigned int
NETIO_PKT_L4_CSUM_CORRECT_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return !(mda->__flags &
           (_NETIO_PKT_BAD_L4_CSUM_MASK | _NETIO_PKT_NO_L4_CSUM_MASK));
}


/** Determine whether the L3 (IP) checksum was calculated.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return Nonzero if the L3 (IP) checksum was calculated.
*/
static __inline unsigned int
NETIO_PKT_L3_CSUM_CALCULATED_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return !(mda->__flags & _NETIO_PKT_NO_L3_CSUM_MASK);
}


/** Determine whether the L3 (IP) checksum was calculated and found to be
 *  correct.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return Nonzero if the checksum was calculated and is correct.
 */
static __inline unsigned int
NETIO_PKT_L3_CSUM_CORRECT_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return !(mda->__flags &
           (_NETIO_PKT_BAD_L3_CSUM_MASK | _NETIO_PKT_NO_L3_CSUM_MASK));
}


/** Determine whether the ethertype was recognized and L3 packet data was
 *  processed.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return Nonzero if the ethertype was recognized and L3 packet data was
 *   processed.
 */
static __inline unsigned int
NETIO_PKT_ETHERTYPE_RECOGNIZED_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return !(mda->__flags & _NETIO_PKT_TYPE_UNRECOGNIZED_MASK);
}


/** Retrieve the status of a packet and any errors that may have occurred
 * during ingress processing (length mismatches, CRC errors, etc.).
 * @ingroup ingress
 *
 * Note that packets for which @ref NETIO_PKT_ETHERTYPE_RECOGNIZED()
 * returns zero are always reported as underlength, as there is no a priori
 * means to determine their length.  Normally, applications should use
 * @ref NETIO_PKT_BAD_M() instead of explicitly checking status with this
 * function.
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The packet's status.
 */
static __inline netio_pkt_status_t
NETIO_PKT_STATUS_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return (netio_pkt_status_t) __NETIO_PKT_NOTIF_HEADER(pkt).bits.__status;
}


/** Report whether a packet is bad (i.e., was shorter than expected based on
 *  its headers, or had a bad CRC).
 * @ingroup ingress
 *
 * Note that this function does not verify L3 or L4 checksums.
 *
 * @param[in] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 * @return Nonzero if the packet is bad and should be discarded.
 */
static __inline unsigned int
NETIO_PKT_BAD_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return ((NETIO_PKT_STATUS_M(mda, pkt) & 1) &&
          (NETIO_PKT_ETHERTYPE_RECOGNIZED_M(mda, pkt) ||
           NETIO_PKT_STATUS_M(mda, pkt) == NETIO_PKT_STATUS_BAD));
}


/** Return the length of the packet, starting with the L2 (Ethernet) header.
 * @ingroup egress
 *
 * @param[in] mmd Pointer to packet's minimal metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The length of the packet, in bytes.
 */
static __inline netio_size_t
NETIO_PKT_L2_LENGTH_MM(netio_pkt_minimal_metadata_t* mmd, netio_pkt_t* pkt)
{
  return mmd->l2_length;
}


/** Return the length of the L2 (Ethernet) header.
 * @ingroup egress
 *
 * @param[in] mmd Pointer to packet's minimal metadata.
 * @param[in] pkt Packet on which to operate.
 * @return The length of the packet's L2 header, in bytes.
 */
static __inline netio_size_t
NETIO_PKT_L2_HEADER_LENGTH_MM(netio_pkt_minimal_metadata_t* mmd,
                              netio_pkt_t* pkt)
{
  return mmd->l3_offset - mmd->l2_offset;
}


/** Return the length of the packet, starting with the L3 (IP) header.
 * @ingroup egress
 *
 * @param[in] mmd Pointer to packet's minimal metadata.
 * @param[in] pkt Packet on which to operate.
 * @return Length of the packet's L3 header and data, in bytes.
 */
static __inline netio_size_t
NETIO_PKT_L3_LENGTH_MM(netio_pkt_minimal_metadata_t* mmd, netio_pkt_t* pkt)
{
  return (NETIO_PKT_L2_LENGTH_MM(mmd, pkt) -
          NETIO_PKT_L2_HEADER_LENGTH_MM(mmd, pkt));
}


/** Return a pointer to the packet's L3 (generally, the IP) header.
 * @ingroup egress
 *
 * Note that we guarantee word alignment of the L3 header.
 *
 * @param[in] mmd Pointer to packet's minimal metadata.
 * @param[in] pkt Packet on which to operate.
 * @return A pointer to the packet's L3 header.
 */
static __inline unsigned char*
NETIO_PKT_L3_DATA_MM(netio_pkt_minimal_metadata_t* mmd, netio_pkt_t* pkt)
{
  return _NETIO_PKT_BASE(pkt) + mmd->l3_offset;
}


/** Return a pointer to the packet's L2 (Ethernet) header.
 * @ingroup egress
 *
 * @param[in] mmd Pointer to packet's minimal metadata.
 * @param[in] pkt Packet on which to operate.
 * @return A pointer to start of the packet.
 */
static __inline unsigned char*
NETIO_PKT_L2_DATA_MM(netio_pkt_minimal_metadata_t* mmd, netio_pkt_t* pkt)
{
  return _NETIO_PKT_BASE(pkt) + mmd->l2_offset;
}


/** Retrieve the status of a packet and any errors that may have occurred
 * during ingress processing (length mismatches, CRC errors, etc.).
 * @ingroup ingress
 *
 * Note that packets for which @ref NETIO_PKT_ETHERTYPE_RECOGNIZED()
 * returns zero are always reported as underlength, as there is no a priori
 * means to determine their length.  Normally, applications should use
 * @ref NETIO_PKT_BAD() instead of explicitly checking status with this
 * function.
 *
 * @param[in] pkt Packet on which to operate.
 * @return The packet's status.
 */
static __inline netio_pkt_status_t
NETIO_PKT_STATUS(netio_pkt_t* pkt)
{
  netio_assert(!pkt->__packet.bits.__minimal);

  return (netio_pkt_status_t) __NETIO_PKT_NOTIF_HEADER(pkt).bits.__status;
}


/** Report whether a packet is bad (i.e., was shorter than expected based on
 *  its headers, or had a bad CRC).
 * @ingroup ingress
 *
 * Note that this function does not verify L3 or L4 checksums.
 *
 * @param[in] pkt Packet on which to operate.
 * @return Nonzero if the packet is bad and should be discarded.
 */
static __inline unsigned int
NETIO_PKT_BAD(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_BAD_M(mda, pkt);
}


/** Return the length of the packet's custom header.
 *  A custom header may or may not be present, depending upon the IPP; its
 *  contents and alignment are also IPP-dependent.  Currently, none of the
 *  standard IPPs supplied by Tilera produce a custom header.  If present,
 *  the custom header precedes the L2 header in the packet buffer.
 * @ingroup pktfuncs
 *
 * @param[in] pkt Packet on which to operate.
 * @return The length of the packet's custom header, in bytes.
 */
static __inline netio_size_t
NETIO_PKT_CUSTOM_HEADER_LENGTH(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_CUSTOM_HEADER_LENGTH_M(mda, pkt);
}


/** Return the length of the packet, starting with the custom header.
 *  A custom header may or may not be present, depending upon the IPP; its
 *  contents and alignment are also IPP-dependent.  Currently, none of the
 *  standard IPPs supplied by Tilera produce a custom header.  If present,
 *  the custom header precedes the L2 header in the packet buffer.
 * @ingroup pktfuncs
 *
 * @param[in] pkt Packet on which to operate.
 * @return  The length of the packet, in bytes.
 */
static __inline netio_size_t
NETIO_PKT_CUSTOM_LENGTH(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_CUSTOM_LENGTH_M(mda, pkt);
}


/** Return a pointer to the packet's custom header.
 *  A custom header may or may not be present, depending upon the IPP; its
 *  contents and alignment are also IPP-dependent.  Currently, none of the
 *  standard IPPs supplied by Tilera produce a custom header.  If present,
 *  the custom header precedes the L2 header in the packet buffer.
 * @ingroup pktfuncs
 *
 * @param[in] pkt Packet on which to operate.
 * @return A pointer to start of the packet.
 */
static __inline unsigned char*
NETIO_PKT_CUSTOM_DATA(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_CUSTOM_DATA_M(mda, pkt);
}


/** Return the length of the packet's L2 (Ethernet plus VLAN or SNAP) header.
 * @ingroup pktfuncs
 *
 * @param[in] pkt Packet on which to operate.
 * @return The length of the packet's L2 header, in bytes.
 */
static __inline netio_size_t
NETIO_PKT_L2_HEADER_LENGTH(netio_pkt_t* pkt)
{
  if (NETIO_PKT_IS_MINIMAL(pkt))
  {
    netio_pkt_minimal_metadata_t* mmd = NETIO_PKT_MINIMAL_METADATA(pkt);

    return NETIO_PKT_L2_HEADER_LENGTH_MM(mmd, pkt);
  }
  else
  {
    netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

    return NETIO_PKT_L2_HEADER_LENGTH_M(mda, pkt);
  }
}


/** Return the length of the packet, starting with the L2 (Ethernet) header.
 * @ingroup pktfuncs
 *
 * @param[in] pkt Packet on which to operate.
 * @return  The length of the packet, in bytes.
 */
static __inline netio_size_t
NETIO_PKT_L2_LENGTH(netio_pkt_t* pkt)
{
  if (NETIO_PKT_IS_MINIMAL(pkt))
  {
    netio_pkt_minimal_metadata_t* mmd = NETIO_PKT_MINIMAL_METADATA(pkt);

    return NETIO_PKT_L2_LENGTH_MM(mmd, pkt);
  }
  else
  {
    netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

    return NETIO_PKT_L2_LENGTH_M(mda, pkt);
  }
}


/** Return a pointer to the packet's L2 (Ethernet) header.
 * @ingroup pktfuncs
 *
 * @param[in] pkt Packet on which to operate.
 * @return A pointer to start of the packet.
 */
static __inline unsigned char*
NETIO_PKT_L2_DATA(netio_pkt_t* pkt)
{
  if (NETIO_PKT_IS_MINIMAL(pkt))
  {
    netio_pkt_minimal_metadata_t* mmd = NETIO_PKT_MINIMAL_METADATA(pkt);

    return NETIO_PKT_L2_DATA_MM(mmd, pkt);
  }
  else
  {
    netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

    return NETIO_PKT_L2_DATA_M(mda, pkt);
  }
}


/** Retrieve the length of the packet, starting with the L3 (generally, the IP)
 * header.
 * @ingroup pktfuncs
 *
 * @param[in] pkt Packet on which to operate.
 * @return Length of the packet's L3 header and data, in bytes.
 */
static __inline netio_size_t
NETIO_PKT_L3_LENGTH(netio_pkt_t* pkt)
{
  if (NETIO_PKT_IS_MINIMAL(pkt))
  {
    netio_pkt_minimal_metadata_t* mmd = NETIO_PKT_MINIMAL_METADATA(pkt);

    return NETIO_PKT_L3_LENGTH_MM(mmd, pkt);
  }
  else
  {
    netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

    return NETIO_PKT_L3_LENGTH_M(mda, pkt);
  }
}


/** Return a pointer to the packet's L3 (generally, the IP) header.
 * @ingroup pktfuncs
 *
 * Note that we guarantee word alignment of the L3 header.
 *
 * @param[in] pkt Packet on which to operate.
 * @return A pointer to the packet's L3 header.
 */
static __inline unsigned char*
NETIO_PKT_L3_DATA(netio_pkt_t* pkt)
{
  if (NETIO_PKT_IS_MINIMAL(pkt))
  {
    netio_pkt_minimal_metadata_t* mmd = NETIO_PKT_MINIMAL_METADATA(pkt);

    return NETIO_PKT_L3_DATA_MM(mmd, pkt);
  }
  else
  {
    netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

    return NETIO_PKT_L3_DATA_M(mda, pkt);
  }
}


/** Return the ordinal of the packet.
 * @ingroup ingress
 *
 * Each packet is given an ordinal number when it is delivered by the IPP.
 * In the medium term, the ordinal is unique and monotonically increasing,
 * being incremented by 1 for each packet; the ordinal of the first packet
 * delivered after the IPP starts is zero.  (Since the ordinal is of finite
 * size, given enough input packets, it will eventually wrap around to zero;
 * in the long term, therefore, ordinals are not unique.)  The ordinals
 * handed out by different IPPs are not disjoint, so two packets from
 * different IPPs may have identical ordinals.  Packets dropped by the
 * IPP or by the I/O shim are not assigned ordinals.
 *
 *
 * @param[in] pkt Packet on which to operate.
 * @return The packet's per-IPP packet ordinal.
 */
static __inline unsigned int
NETIO_PKT_ORDINAL(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_ORDINAL_M(mda, pkt);
}


/** Return the per-group ordinal of the packet.
 * @ingroup ingress
 *
 * Each packet is given a per-group ordinal number when it is
 * delivered by the IPP. By default, the group is the packet's VLAN,
 * although IPP can be recompiled to use different values.  In
 * the medium term, the ordinal is unique and monotonically
 * increasing, being incremented by 1 for each packet; the ordinal of
 * the first packet distributed to a particular group is zero.
 * (Since the ordinal is of finite size, given enough input packets,
 * it will eventually wrap around to zero; in the long term,
 * therefore, ordinals are not unique.)  The ordinals handed out by
 * different IPPs are not disjoint, so two packets from different IPPs
 * may have identical ordinals; similarly, packets distributed to
 * different groups may have identical ordinals.  Packets dropped by
 * the IPP or by the I/O shim are not assigned ordinals.
 *
 * @param[in] pkt Packet on which to operate.
 * @return The packet's per-IPP, per-group ordinal.
 */
static __inline unsigned int
NETIO_PKT_GROUP_ORDINAL(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_GROUP_ORDINAL_M(mda, pkt);
}


/** Return the VLAN ID assigned to the packet.
 * @ingroup ingress
 *
 * This is usually also contained within the packet header.  If the packet
 * does not have a VLAN tag, the VLAN ID returned by this function is zero.
 *
 * @param[in] pkt Packet on which to operate.
 * @return The packet's VLAN ID.
 */
static __inline unsigned short
NETIO_PKT_VLAN_ID(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_VLAN_ID_M(mda, pkt);
}


/** Return the ethertype of the packet.
 * @ingroup ingress
 *
 * This value is reliable if @ref NETIO_PKT_ETHERTYPE_RECOGNIZED()
 * returns true, and otherwise, may not be well defined.
 *
 * @param[in] pkt Packet on which to operate.
 * @return The packet's ethertype.
 */
static __inline unsigned short
NETIO_PKT_ETHERTYPE(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_ETHERTYPE_M(mda, pkt);
}


/** Return the flow hash computed on the packet.
 * @ingroup ingress
 *
 * For TCP and UDP packets, this hash is calculated by hashing together
 * the "5-tuple" values, specifically the source IP address, destination
 * IP address, protocol type, source port and destination port.
 * The hash value is intended to be helpful for millions of distinct
 * flows.
 *
 * For IPv4 or IPv6 packets which are neither TCP nor UDP, the flow hash is
 * derived by hashing together the source and destination IP addresses.
 *
 * For MPLS-encapsulated packets, the flow hash is derived by hashing
 * the first MPLS label.
 *
 * For all other packets the flow hash is computed from the source
 * and destination Ethernet addresses.
 *
 * The hash is symmetric, meaning it produces the same value if the
 * source and destination are swapped. The only exceptions are
 * tunneling protocols 0x04 (IP in IP Encapsulation), 0x29 (Simple
 * Internet Protocol), 0x2F (General Routing Encapsulation) and 0x32
 * (Encap Security Payload), which use only the destination address
 * since the source address is not meaningful.
 *
 * @param[in] pkt Packet on which to operate.
 * @return The packet's 32-bit flow hash.
 */
static __inline unsigned int
NETIO_PKT_FLOW_HASH(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_FLOW_HASH_M(mda, pkt);
}


/** Return the first word of "user data" for the packet.
 *
 * The contents of the user data words depend on the IPP.
 *
 * When using the standard ipp1, ipp2, or ipp4 sub-drivers, the first
 * word of user data contains the least significant bits of the 64-bit
 * arrival cycle count (see @c get_cycle_count_low()).
 *
 * See the <em>System Programmer's Guide</em> for details.
 *
 * @ingroup ingress
 *
 * @param[in] pkt Packet on which to operate.
 * @return The packet's first word of "user data".
 */
static __inline unsigned int
NETIO_PKT_USER_DATA_0(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_USER_DATA_0_M(mda, pkt);
}


/** Return the second word of "user data" for the packet.
 *
 * The contents of the user data words depend on the IPP.
 *
 * When using the standard ipp1, ipp2, or ipp4 sub-drivers, the second
 * word of user data contains the most significant bits of the 64-bit
 * arrival cycle count (see @c get_cycle_count_high()).
 *
 * See the <em>System Programmer's Guide</em> for details.
 *
 * @ingroup ingress
 *
 * @param[in] pkt Packet on which to operate.
 * @return The packet's second word of "user data".
 */
static __inline unsigned int
NETIO_PKT_USER_DATA_1(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_USER_DATA_1_M(mda, pkt);
}


/** Determine whether the L4 (TCP/UDP) checksum was calculated.
 * @ingroup ingress
 *
 * @param[in] pkt Packet on which to operate.
 * @return Nonzero if the L4 checksum was calculated.
 */
static __inline unsigned int
NETIO_PKT_L4_CSUM_CALCULATED(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_L4_CSUM_CALCULATED_M(mda, pkt);
}


/** Determine whether the L4 (TCP/UDP) checksum was calculated and found to
 *  be correct.
 * @ingroup ingress
 *
 * @param[in] pkt Packet on which to operate.
 * @return Nonzero if the checksum was calculated and is correct.
 */
static __inline unsigned int
NETIO_PKT_L4_CSUM_CORRECT(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_L4_CSUM_CORRECT_M(mda, pkt);
}


/** Determine whether the L3 (IP) checksum was calculated.
 * @ingroup ingress
 *
 * @param[in] pkt Packet on which to operate.
 * @return Nonzero if the L3 (IP) checksum was calculated.
*/
static __inline unsigned int
NETIO_PKT_L3_CSUM_CALCULATED(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_L3_CSUM_CALCULATED_M(mda, pkt);
}


/** Determine whether the L3 (IP) checksum was calculated and found to be
 *  correct.
 * @ingroup ingress
 *
 * @param[in] pkt Packet on which to operate.
 * @return Nonzero if the checksum was calculated and is correct.
 */
static __inline unsigned int
NETIO_PKT_L3_CSUM_CORRECT(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_L3_CSUM_CORRECT_M(mda, pkt);
}


/** Determine whether the Ethertype was recognized and L3 packet data was
 *  processed.
 * @ingroup ingress
 *
 * @param[in] pkt Packet on which to operate.
 * @return Nonzero if the Ethertype was recognized and L3 packet data was
 *   processed.
 */
static __inline unsigned int
NETIO_PKT_ETHERTYPE_RECOGNIZED(netio_pkt_t* pkt)
{
  netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

  return NETIO_PKT_ETHERTYPE_RECOGNIZED_M(mda, pkt);
}


/** Set an egress packet's L2 length, using a metadata pointer to speed the
 * computation.
 * @ingroup egress
 *
 * @param[in,out] mmd Pointer to packet's minimal metadata.
 * @param[in] pkt Packet on which to operate.
 * @param[in] len Packet L2 length, in bytes.
 */
static __inline void
NETIO_PKT_SET_L2_LENGTH_MM(netio_pkt_minimal_metadata_t* mmd, netio_pkt_t* pkt,
                           int len)
{
  mmd->l2_length = len;
}


/** Set an egress packet's L2 length.
 * @ingroup egress
 *
 * @param[in,out] pkt Packet on which to operate.
 * @param[in] len Packet L2 length, in bytes.
 */
static __inline void
NETIO_PKT_SET_L2_LENGTH(netio_pkt_t* pkt, int len)
{
  netio_pkt_minimal_metadata_t* mmd = NETIO_PKT_MINIMAL_METADATA(pkt);

  NETIO_PKT_SET_L2_LENGTH_MM(mmd, pkt, len);
}


/** Set an egress packet's L2 header length, using a metadata pointer to
 *  speed the computation.
 * @ingroup egress
 *
 * It is not normally necessary to call this routine; only the L2 length,
 * not the header length, is needed to transmit a packet.  It may be useful if
 * the egress packet will later be processed by code which expects to use
 * functions like @ref NETIO_PKT_L3_DATA() to get a pointer to the L3 payload.
 *
 * @param[in,out] mmd Pointer to packet's minimal metadata.
 * @param[in] pkt Packet on which to operate.
 * @param[in] len Packet L2 header length, in bytes.
 */
static __inline void
NETIO_PKT_SET_L2_HEADER_LENGTH_MM(netio_pkt_minimal_metadata_t* mmd,
                                  netio_pkt_t* pkt, int len)
{
  mmd->l3_offset = mmd->l2_offset + len;
}


/** Set an egress packet's L2 header length.
 * @ingroup egress
 *
 * It is not normally necessary to call this routine; only the L2 length,
 * not the header length, is needed to transmit a packet.  It may be useful if
 * the egress packet will later be processed by code which expects to use
 * functions like @ref NETIO_PKT_L3_DATA() to get a pointer to the L3 payload.
 *
 * @param[in,out] pkt Packet on which to operate.
 * @param[in] len Packet L2 header length, in bytes.
 */
static __inline void
NETIO_PKT_SET_L2_HEADER_LENGTH(netio_pkt_t* pkt, int len)
{
  netio_pkt_minimal_metadata_t* mmd = NETIO_PKT_MINIMAL_METADATA(pkt);

  NETIO_PKT_SET_L2_HEADER_LENGTH_MM(mmd, pkt, len);
}


/** Set up an egress packet for hardware checksum computation, using a
 *  metadata pointer to speed the operation.
 * @ingroup egress
 *
 *  NetIO provides the ability to automatically calculate a standard
 *  16-bit Internet checksum on transmitted packets.  The application
 *  may specify the point in the packet where the checksum starts, the
 *  number of bytes to be checksummed, and the two bytes in the packet
 *  which will be replaced with the completed checksum.  (If the range
 *  of bytes to be checksummed includes the bytes to be replaced, the
 *  initial values of those bytes will be included in the checksum.)
 *
 *  For some protocols, the packet checksum covers data which is not present
 *  in the packet, or is at least not contiguous to the main data payload.
 *  For instance, the TCP checksum includes a "pseudo-header" which includes
 *  the source and destination IP addresses of the packet.  To accommodate
 *  this, the checksum engine may be "seeded" with an initial value, which
 *  the application would need to compute based on the specific protocol's
 *  requirements.  Note that the seed is given in host byte order (little-
 *  endian), not network byte order (big-endian); code written to compute a
 *  pseudo-header checksum in network byte order will need to byte-swap it
 *  before use as the seed.
 *
 *  Note that the checksum is computed as part of the transmission process,
 *  so it will not be present in the packet upon completion of this routine.
 *
 * @param[in,out] mmd Pointer to packet's minimal metadata.
 * @param[in] pkt Packet on which to operate.
 * @param[in] start Offset within L2 packet of the first byte to include in
 *   the checksum.
 * @param[in] length Number of bytes to include in the checksum.
 *   the checksum.
 * @param[in] location Offset within L2 packet of the first of the two bytes
 *   to be replaced with the calculated checksum.
 * @param[in] seed Initial value of the running checksum before any of the
 *   packet data is added.
 */
static __inline void
NETIO_PKT_DO_EGRESS_CSUM_MM(netio_pkt_minimal_metadata_t* mmd,
                            netio_pkt_t* pkt, int start, int length,
                            int location, uint16_t seed)
{
  mmd->csum_start = start;
  mmd->csum_length = length;
  mmd->csum_location = location;
  mmd->csum_seed = seed;
  mmd->flags |= _NETIO_PKT_NEED_EDMA_CSUM_MASK;
}


/** Set up an egress packet for hardware checksum computation.
 * @ingroup egress
 *
 *  NetIO provides the ability to automatically calculate a standard
 *  16-bit Internet checksum on transmitted packets.  The application
 *  may specify the point in the packet where the checksum starts, the
 *  number of bytes to be checksummed, and the two bytes in the packet
 *  which will be replaced with the completed checksum.  (If the range
 *  of bytes to be checksummed includes the bytes to be replaced, the
 *  initial values of those bytes will be included in the checksum.)
 *
 *  For some protocols, the packet checksum covers data which is not present
 *  in the packet, or is at least not contiguous to the main data payload.
 *  For instance, the TCP checksum includes a "pseudo-header" which includes
 *  the source and destination IP addresses of the packet.  To accommodate
 *  this, the checksum engine may be "seeded" with an initial value, which
 *  the application would need to compute based on the specific protocol's
 *  requirements.  Note that the seed is given in host byte order (little-
 *  endian), not network byte order (big-endian); code written to compute a
 *  pseudo-header checksum in network byte order will need to byte-swap it
 *  before use as the seed.
 *
 *  Note that the checksum is computed as part of the transmission process,
 *  so it will not be present in the packet upon completion of this routine.
 *
 * @param[in,out] pkt Packet on which to operate.
 * @param[in] start Offset within L2 packet of the first byte to include in
 *   the checksum.
 * @param[in] length Number of bytes to include in the checksum.
 *   the checksum.
 * @param[in] location Offset within L2 packet of the first of the two bytes
 *   to be replaced with the calculated checksum.
 * @param[in] seed Initial value of the running checksum before any of the
 *   packet data is added.
 */
static __inline void
NETIO_PKT_DO_EGRESS_CSUM(netio_pkt_t* pkt, int start, int length,
                         int location, uint16_t seed)
{
  netio_pkt_minimal_metadata_t* mmd = NETIO_PKT_MINIMAL_METADATA(pkt);

  NETIO_PKT_DO_EGRESS_CSUM_MM(mmd, pkt, start, length, location, seed);
}


/** Return the number of bytes which could be prepended to a packet, using a
 *  metadata pointer to speed the operation.
 *  See @ref netio_populate_prepend_buffer() to get a full description of
 *  prepending.
 *
 * @param[in,out] mda Pointer to packet's standard metadata.
 * @param[in] pkt Packet on which to operate.
 */
static __inline int
NETIO_PKT_PREPEND_AVAIL_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
  return (pkt->__packet.bits.__offset << 6) +
         NETIO_PKT_CUSTOM_HEADER_LENGTH_M(mda, pkt);
}


/** Return the number of bytes which could be prepended to a packet, using a
 *  metadata pointer to speed the operation.
 *  See @ref netio_populate_prepend_buffer() to get a full description of
 *  prepending.
 * @ingroup egress
 *
 * @param[in,out] mmd Pointer to packet's minimal metadata.
 * @param[in] pkt Packet on which to operate.
 */
static __inline int
NETIO_PKT_PREPEND_AVAIL_MM(netio_pkt_minimal_metadata_t* mmd, netio_pkt_t* pkt)
{
  return (pkt->__packet.bits.__offset << 6) + mmd->l2_offset;
}


/** Return the number of bytes which could be prepended to a packet.
 *  See @ref netio_populate_prepend_buffer() to get a full description of
 *  prepending.
 * @ingroup egress
 *
 * @param[in] pkt Packet on which to operate.
 */
static __inline int
NETIO_PKT_PREPEND_AVAIL(netio_pkt_t* pkt)
{
  if (NETIO_PKT_IS_MINIMAL(pkt))
  {
    netio_pkt_minimal_metadata_t* mmd = NETIO_PKT_MINIMAL_METADATA(pkt);

    return NETIO_PKT_PREPEND_AVAIL_MM(mmd, pkt);
  }
  else
  {
    netio_pkt_metadata_t* mda = NETIO_PKT_METADATA(pkt);

    return NETIO_PKT_PREPEND_AVAIL_M(mda, pkt);
  }
}


/** Flush a packet's minimal metadata from the cache, using a metadata pointer
 *  to speed the operation.
 * @ingroup egress
 *
 * @param[in] mmd Pointer to packet's minimal metadata.
 * @param[in] pkt Packet on which to operate.
 */
static __inline void
NETIO_PKT_FLUSH_MINIMAL_METADATA_MM(netio_pkt_minimal_metadata_t* mmd,
                                    netio_pkt_t* pkt)
{
}


/** Invalidate a packet's minimal metadata from the cache, using a metadata
 *  pointer to speed the operation.
 * @ingroup egress
 *
 * @param[in] mmd Pointer to packet's minimal metadata.
 * @param[in] pkt Packet on which to operate.
 */
static __inline void
NETIO_PKT_INV_MINIMAL_METADATA_MM(netio_pkt_minimal_metadata_t* mmd,
                                  netio_pkt_t* pkt)
{
}


/** Flush and then invalidate a packet's minimal metadata from the cache,
 *  using a metadata pointer to speed the operation.
 * @ingroup egress
 *
 * @param[in] mmd Pointer to packet's minimal metadata.
 * @param[in] pkt Packet on which to operate.
 */
static __inline void
NETIO_PKT_FLUSH_INV_MINIMAL_METADATA_MM(netio_pkt_minimal_metadata_t* mmd,
                                        netio_pkt_t* pkt)
{
}


/** Flush a packet's metadata from the cache, using a metadata pointer
 *  to speed the operation.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's minimal metadata.
 * @param[in] pkt Packet on which to operate.
 */
static __inline void
NETIO_PKT_FLUSH_METADATA_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
}


/** Invalidate a packet's metadata from the cache, using a metadata
 *  pointer to speed the operation.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's metadata.
 * @param[in] pkt Packet on which to operate.
 */
static __inline void
NETIO_PKT_INV_METADATA_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
}


/** Flush and then invalidate a packet's metadata from the cache,
 *  using a metadata pointer to speed the operation.
 * @ingroup ingress
 *
 * @param[in] mda Pointer to packet's metadata.
 * @param[in] pkt Packet on which to operate.
 */
static __inline void
NETIO_PKT_FLUSH_INV_METADATA_M(netio_pkt_metadata_t* mda, netio_pkt_t* pkt)
{
}


/** Flush a packet's minimal metadata from the cache.
 * @ingroup egress
 *
 * @param[in] pkt Packet on which to operate.
 */
static __inline void
NETIO_PKT_FLUSH_MINIMAL_METADATA(netio_pkt_t* pkt)
{
}


/** Invalidate a packet's minimal metadata from the cache.
 * @ingroup egress
 *
 * @param[in] pkt Packet on which to operate.
 */
static __inline void
NETIO_PKT_INV_MINIMAL_METADATA(netio_pkt_t* pkt)
{
}


/** Flush and then invalidate a packet's minimal metadata from the cache.
 * @ingroup egress
 *
 * @param[in] pkt Packet on which to operate.
 */
static __inline void
NETIO_PKT_FLUSH_INV_MINIMAL_METADATA(netio_pkt_t* pkt)
{
}


/** Flush a packet's metadata from the cache.
 * @ingroup ingress
 *
 * @param[in] pkt Packet on which to operate.
 */
static __inline void
NETIO_PKT_FLUSH_METADATA(netio_pkt_t* pkt)
{
}


/** Invalidate a packet's metadata from the cache.
 * @ingroup ingress
 *
 * @param[in] pkt Packet on which to operate.
 */
static __inline void
NETIO_PKT_INV_METADATA(netio_pkt_t* pkt)
{
}


/** Flush and then invalidate a packet's metadata from the cache.
 * @ingroup ingress
 *
 * @param[in] pkt Packet on which to operate.
 */
static __inline void
NETIO_PKT_FLUSH_INV_METADATA(netio_pkt_t* pkt)
{
}

/** Number of NUMA nodes we can distribute buffers to.
 * @ingroup setup */
#define NETIO_NUM_NODE_WEIGHTS  16

/**
 * @brief An object for specifying the characteristics of NetIO communication
 * endpoint.
 *
 * @ingroup setup
 *
 * The @ref netio_input_register() function uses this structure to define
 * how an application tile will communicate with an IPP.
 *
 *
 * Future updates to NetIO may add new members to this structure,
 * which can affect the success of the registration operation.  Thus,
 * if dynamically initializing the structure, applications are urged to
 * zero it out first, for example:
 *
 * @code
 * netio_input_config_t config;
 * memset(&config, 0, sizeof (config));
 * config.flags = NETIO_RECV | NETIO_XMIT_CSUM | NETIO_TAG_NONE;
 * config.num_receive_packets = NETIO_MAX_RECEIVE_PKTS;
 * config.queue_id = 0;
 *     .
 *     .
 *     .
 * @endcode
 *
 * since that guarantees that any unused structure members, including
 * members which did not exist when the application was first developed,
 * will not have unexpected values.
 *
 * If statically initializing the structure, we strongly recommend use of
 * C99-style named initializers, for example:
 *
 * @code
 * netio_input_config_t config = {
 *    .flags = NETIO_RECV | NETIO_XMIT_CSUM | NETIO_TAG_NONE,
 *    .num_receive_packets = NETIO_MAX_RECEIVE_PKTS,
 *    .queue_id = 0,
 * },
 * @endcode
 *
 * instead of the old-style structure initialization:
 *
 * @code
 * // Bad example! Currently equivalent to the above, but don't do this.
 * netio_input_config_t config = {
 *    NETIO_RECV | NETIO_XMIT_CSUM | NETIO_TAG_NONE, NETIO_MAX_RECEIVE_PKTS, 0
 * },
 * @endcode
 *
 * since the C99 style requires no changes to the code if elements of the
 * config structure are rearranged.  (It also makes the initialization much
 * easier to understand.)
 *
 * Except for items which address a particular tile's transmit or receive
 * characteristics, such as the ::NETIO_RECV flag, applications are advised
 * to specify the same set of configuration data on all registrations.
 * This prevents differing results if multiple tiles happen to do their
 * registration operations in a different order on different invocations of
 * the application.  This is particularly important for things like link
 * management flags, and buffer size and homing specifications.
 *
 * Unless the ::NETIO_FIXED_BUFFER_VA flag is specified in flags, the NetIO
 * buffer pool is automatically created and mapped into the application's
 * virtual address space at an address chosen by the operating system,
 * using the common memory (cmem) facility in the Tilera Multicore
 * Components library.  The cmem facility allows multiple processes to gain
 * access to shared memory which is mapped into each process at an
 * identical virtual address.  In order for this to work, the processes
 * must have a common ancestor, which must create the common memory using
 * tmc_cmem_init().
 *
 * In programs using the iLib process creation API, or in programs which use
 * only one process (which include programs using the pthreads library),
 * tmc_cmem_init() is called automatically.  All other applications
 * must call it explicitly, before any child processes which might call
 * netio_input_register() are created.
 */
typedef struct
{
  /** Registration characteristics.

      This value determines several characteristics of the registration;
      flags for different types of behavior are ORed together to make the
      final flag value.  Generally applications should specify exactly
      one flag from each of the following categories:

      - Whether the application will be receiving packets on this queue
        (::NETIO_RECV or ::NETIO_NO_RECV).

      - Whether the application will be transmitting packets on this queue,
        and if so, whether it will request egress checksum calculation
        (::NETIO_XMIT, ::NETIO_XMIT_CSUM, or ::NETIO_NO_XMIT).  It is
        legal to call netio_get_buffer() without one of the XMIT flags,
        as long as ::NETIO_RECV is specified; in this case, the retrieved
        buffers must be passed to another tile for transmission.

      - Whether the application expects any vendor-specific tags in
        its packets' L2 headers (::NETIO_TAG_NONE, ::NETIO_TAG_BRCM,
        or ::NETIO_TAG_MRVL).  This must match the configuration of the
        target IPP.

      To accommodate applications written to previous versions of the NetIO
      interface, none of the flags above are currently required; if omitted,
      NetIO behaves more or less as if ::NETIO_RECV | ::NETIO_XMIT_CSUM |
      ::NETIO_TAG_NONE were used.  However, explicit specification of
      the relevant flags allows NetIO to do a better job of resource
      allocation, allows earlier detection of certain configuration errors,
      and may enable advanced features or higher performance in the future,
      so their use is strongly recommended.

      Note that specifying ::NETIO_NO_RECV along with ::NETIO_NO_XMIT
      is a special case, intended primarily for use by programs which
      retrieve network statistics or do link management operations.
      When these flags are both specified, the resulting queue may not
      be used with NetIO routines other than netio_get(), netio_set(),
      and netio_input_unregister().  See @ref link for more information
      on link management.

      Other flags are optional; their use is described below.
  */
  int flags;

  /** Interface name.  This is a string which identifies the specific
      Ethernet controller hardware to be used.  The format of the string
      is a device type and a device index, separated by a slash; so,
      the first 10 Gigabit Ethernet controller is named "xgbe/0", while
      the second 10/100/1000 Megabit Ethernet controller is named "gbe/1".
   */
  const char* interface;

  /** Receive packet queue size.  This specifies the maximum number
      of ingress packets that can be received on this queue without
      being retrieved by @ref netio_get_packet().  If the IPP's distribution
      algorithm calls for a packet to be sent to this queue, and this
      number of packets are already pending there, the new packet
      will either be discarded, or sent to another tile registered
      for the same queue_id (see @ref drops).  This value must
      be at least ::NETIO_MIN_RECEIVE_PKTS, can always be at least
      ::NETIO_MAX_RECEIVE_PKTS, and may be larger than that on certain
      interfaces.
   */
  int num_receive_packets;

  /** The queue ID being requested.  Legal values for this range from 0
      to ::NETIO_MAX_QUEUE_ID, inclusive.  ::NETIO_MAX_QUEUE_ID is always
      greater than or equal to the number of tiles; this allows one queue
      for each tile, plus at least one additional queue.  Some applications
      may wish to use the additional queue as a destination for unwanted
      packets, since packets delivered to queues for which no tiles have
      registered are discarded.
   */
  unsigned int queue_id;

  /** Maximum number of small send buffers to be held in the local empty
      buffer cache.  This specifies the size of the area which holds
      empty small egress buffers requested from the IPP but not yet
      retrieved via @ref netio_get_buffer().  This value must be greater
      than zero if the application will ever use @ref netio_get_buffer()
      to allocate empty small egress buffers; it may be no larger than
      ::NETIO_MAX_SEND_BUFFERS.  See @ref epp for more details on empty
      buffer caching.
   */
  int num_send_buffers_small_total;

  /** Number of small send buffers to be preallocated at registration.
      If this value is nonzero, the specified number of empty small egress
      buffers will be requested from the IPP during the netio_input_register
      operation; this may speed the execution of @ref netio_get_buffer().
      This may be no larger than @ref num_send_buffers_small_total.  See @ref
      epp for more details on empty buffer caching.
   */
  int num_send_buffers_small_prealloc;

  /** Maximum number of large send buffers to be held in the local empty
      buffer cache.  This specifies the size of the area which holds empty
      large egress buffers requested from the IPP but not yet retrieved via
      @ref netio_get_buffer().  This value must be greater than zero if the
      application will ever use @ref netio_get_buffer() to allocate empty
      large egress buffers; it may be no larger than ::NETIO_MAX_SEND_BUFFERS.
      See @ref epp for more details on empty buffer caching.
   */
  int num_send_buffers_large_total;

  /** Number of large send buffers to be preallocated at registration.
      If this value is nonzero, the specified number of empty large egress
      buffers will be requested from the IPP during the netio_input_register
      operation; this may speed the execution of @ref netio_get_buffer().
      This may be no larger than @ref num_send_buffers_large_total.  See @ref
      epp for more details on empty buffer caching.
   */
  int num_send_buffers_large_prealloc;

  /** Maximum number of jumbo send buffers to be held in the local empty
      buffer cache.  This specifies the size of the area which holds empty
      jumbo egress buffers requested from the IPP but not yet retrieved via
      @ref netio_get_buffer().  This value must be greater than zero if the
      application will ever use @ref netio_get_buffer() to allocate empty
      jumbo egress buffers; it may be no larger than ::NETIO_MAX_SEND_BUFFERS.
      See @ref epp for more details on empty buffer caching.
   */
  int num_send_buffers_jumbo_total;

  /** Number of jumbo send buffers to be preallocated at registration.
      If this value is nonzero, the specified number of empty jumbo egress
      buffers will be requested from the IPP during the netio_input_register
      operation; this may speed the execution of @ref netio_get_buffer().
      This may be no larger than @ref num_send_buffers_jumbo_total.  See @ref
      epp for more details on empty buffer caching.
   */
  int num_send_buffers_jumbo_prealloc;

  /** Total packet buffer size.  This determines the total size, in bytes,
      of the NetIO buffer pool.  Note that the maximum number of available
      buffers of each size is determined during hypervisor configuration
      (see the <em>System Programmer's Guide</em> for details); this just
      influences how much host memory is allocated for those buffers.

      The buffer pool is allocated from common memory, which will be
      automatically initialized if needed.  If your buffer pool is larger
      than 240 MB, you might need to explicitly call @c tmc_cmem_init(),
      as described in the Application Libraries Reference Manual (UG227).

      Packet buffers are currently allocated in chunks of 16 MB; this
      value will be rounded up to the next larger multiple of 16 MB.
      If this value is zero, a default of 32 MB will be used; this was
      the value used by previous versions of NetIO.  Note that taking this
      default also affects the placement of buffers on Linux NUMA nodes.
      See @ref buffer_node_weights for an explanation of buffer placement.

      In order to successfully allocate packet buffers, Linux must have
      available huge pages on the relevant Linux NUMA nodes.  See the
      <em>System Programmer's Guide</em> for information on configuring
      huge page support in Linux.
   */
  uint64_t total_buffer_size;

  /** Buffer placement weighting factors.

      This array specifies the relative amount of buffering to place
      on each of the available Linux NUMA nodes.  This array is
      indexed by the NUMA node, and the values in the array are
      proportional to the amount of buffer space to allocate on that
      node.

      If memory striping is enabled in the Hypervisor, then there is
      only one logical NUMA node (node 0). In that case, NetIO will by
      default ignore the suggested buffer node weights, and buffers
      will be striped across the physical memory controllers. See
      UG209 System Programmer's Guide for a description of the
      hypervisor option that controls memory striping.

      If memory striping is disabled, then there are up to four NUMA
      nodes, corresponding to the four DDRAM controllers in the TILE
      processor architecture.  See UG100 Tile Processor Architecture
      Overview for a diagram showing the location of each of the DDRAM
      controllers relative to the tile array.

      For instance, if memory striping is disabled, the following
      configuration strucure:

      @code
      netio_input_config_t config = {
            .
            .
            .
        .total_buffer_size = 4 * 16 * 1024 * 1024;
        .buffer_node_weights = { 1, 0, 1, 0 },
      },
      @endcode

      would result in 32 MB of buffers being placed on controller 0, and
      32 MB on controller 2.  (Since buffers are allocated in units of
      16 MB, some sets of weights will not be able to be matched exactly.)

      For the weights to be effective, @ref total_buffer_size must be
      nonzero.  If @ref total_buffer_size is zero, causing the default
      32 MB of buffer space to be used, then any specified weights will
      be ignored, and buffers will positioned as they were in previous
      versions of NetIO:

      - For xgbe/0 and gbe/0, 16 MB of buffers will be placed on controller 1,
        and the other 16 MB will be placed on controller 2.

      - For xgbe/1 and gbe/1, 16 MB of buffers will be placed on controller 2,
        and the other 16 MB will be placed on controller 3.

      If @ref total_buffer_size is nonzero, but all weights are zero,
      then all buffer space will be allocated on Linux NUMA node zero.

      By default, the specified buffer placement is treated as a hint;
      if sufficient free memory is not available on the specified
      controllers, the buffers will be allocated elsewhere.  However,
      if the ::NETIO_STRICT_HOMING flag is specified in @ref flags, then a
      failure to allocate buffer space exactly as requested will cause the
      registration operation to fail with an error of ::NETIO_CANNOT_HOME.

      Note that maximal network performance cannot be achieved with
      only one memory controller.
   */
  uint8_t buffer_node_weights[NETIO_NUM_NODE_WEIGHTS];

  /** Fixed virtual address for packet buffers.  Only valid when
      ::NETIO_FIXED_BUFFER_VA is specified in @ref flags; see the
      description of that flag for details.
   */
  void* fixed_buffer_va;

  /**
      Maximum number of outstanding send packet requests.  This value is
      only relevant when an EPP is in use; it determines the number of
      slots in the EPP's outgoing packet queue which this tile is allowed
      to consume, and thus the number of packets which may be sent before
      the sending tile must wait for an acknowledgment from the EPP.
      Modifying this value is generally only helpful when using @ref
      netio_send_packet_vector(), where it can help improve performance by
      allowing a single vector send operation to process more packets.
      Typically it is not specified, and the default, which divides the
      outgoing packet slots evenly between all tiles on the chip, is used.

      If a registration asks for more outgoing packet queue slots than are
      available, ::NETIO_TOOMANY_XMIT will be returned.  The total number
      of packet queue slots which are available for all tiles for each EPP
      is subject to change, but is currently ::NETIO_TOTAL_SENDS_OUTSTANDING.


      This value is ignored if ::NETIO_XMIT is not specified in flags.
      If you want to specify a large value here for a specific tile, you are
      advised to specify NETIO_NO_XMIT on other, non-transmitting tiles so
      that they do not consume a default number of packet slots.  Any tile
      transmitting is required to have at least ::NETIO_MIN_SENDS_OUTSTANDING
      slots allocated to it; values less than that will be silently
      increased by the NetIO library.
   */
  int num_sends_outstanding;
}
netio_input_config_t;


/** Registration flags; used in the @ref netio_input_config_t structure.
 * @addtogroup setup
 */
/** @{ */

/** Fail a registration request if we can't put packet buffers
    on the specified memory controllers. */
#define NETIO_STRICT_HOMING   0x00000002

/** This application expects no tags on its L2 headers. */
#define NETIO_TAG_NONE        0x00000004

/** This application expects Marvell extended tags on its L2 headers. */
#define NETIO_TAG_MRVL        0x00000008

/** This application expects Broadcom tags on its L2 headers. */
#define NETIO_TAG_BRCM        0x00000010

/** This registration may call routines which receive packets. */
#define NETIO_RECV            0x00000020

/** This registration may not call routines which receive packets. */
#define NETIO_NO_RECV         0x00000040

/** This registration may call routines which transmit packets. */
#define NETIO_XMIT            0x00000080

/** This registration may call routines which transmit packets with
    checksum acceleration. */
#define NETIO_XMIT_CSUM       0x00000100

/** This registration may not call routines which transmit packets. */
#define NETIO_NO_XMIT         0x00000200

/** This registration wants NetIO buffers mapped at an application-specified
    virtual address.

    NetIO buffers are by default created by the TMC common memory facility,
    which must be configured by a common ancestor of all processes sharing
    a network interface.  When this flag is specified, NetIO buffers are
    instead mapped at an address chosen by the application (and specified
    in @ref netio_input_config_t::fixed_buffer_va).  This allows multiple
    unrelated but cooperating processes to share a NetIO interface.
    All processes sharing the same interface must specify this flag,
    and all must specify the same fixed virtual address.

    @ref netio_input_config_t::fixed_buffer_va must be a
    multiple of 16 MB, and the packet buffers will occupy @ref
    netio_input_config_t::total_buffer_size bytes of virtual address
    space, beginning at that address.  If any of those virtual addresses
    are currently occupied by other memory objects, like application or
    shared library code or data, @ref netio_input_register() will return
    ::NETIO_FAULT.  While it is impossible to provide a fixed_buffer_va
    which will work for all applications, a good first guess might be to
    use 0xb0000000 minus @ref netio_input_config_t::total_buffer_size.
    If that fails, it might be helpful to consult the running application's
    virtual address description file (/proc/<em>pid</em>/maps) to see
    which regions of virtual address space are available.
 */
#define NETIO_FIXED_BUFFER_VA 0x00000400

/** This registration call will not complete unless the network link
    is up.  The process will wait several seconds for this to happen (the
    precise interval is link-dependent), but if the link does not come up,
    ::NETIO_LINK_DOWN will be returned.  This flag is the default if
    ::NETIO_NOREQUIRE_LINK_UP is not specified.  Note that this flag by
    itself does not request that the link be brought up; that can be done
    with the ::NETIO_AUTO_LINK_UPDN or ::NETIO_AUTO_LINK_UP flags (the
    latter is the default if no NETIO_AUTO_LINK_xxx flags are specified),
    or by explicitly setting the link's desired state via netio_set().
    If the link is not brought up by one of those methods, and this flag
    is specified, the registration operation will return ::NETIO_LINK_DOWN.
    This flag is ignored if it is specified along with ::NETIO_NO_XMIT and
    ::NETIO_NO_RECV.  See @ref link for more information on link
    management.
 */
#define NETIO_REQUIRE_LINK_UP    0x00000800

/** This registration call will complete even if the network link is not up.
    Whenever the link is not up, packets will not be sent or received:
    netio_get_packet() will return ::NETIO_NOPKT once all queued packets
    have been drained, and netio_send_packet() and similar routines will
    return NETIO_QUEUE_FULL once the outgoing packet queue in the EPP
    or the I/O shim is full.  See @ref link for more information on link
    management.
 */
#define NETIO_NOREQUIRE_LINK_UP  0x00001000

#ifndef __DOXYGEN__
/*
 * These are part of the implementation of the NETIO_AUTO_LINK_xxx flags,
 * but should not be used directly by applications, and are thus not
 * documented.
 */
#define _NETIO_AUTO_UP        0x00002000
#define _NETIO_AUTO_DN        0x00004000
#define _NETIO_AUTO_PRESENT   0x00008000
#endif

/** Set the desired state of the link to up, allowing any speeds which are
    supported by the link hardware, as part of this registration operation.
    Do not take down the link automatically.  This is the default if
    no other NETIO_AUTO_LINK_xxx flags are specified.  This flag is ignored
    if it is specified along with ::NETIO_NO_XMIT and ::NETIO_NO_RECV.
    See @ref link for more information on link management.
 */
#define NETIO_AUTO_LINK_UP     (_NETIO_AUTO_PRESENT | _NETIO_AUTO_UP)

/** Set the desired state of the link to up, allowing any speeds which are
    supported by the link hardware, as part of this registration operation.
    Set the desired state of the link to down the next time no tiles are
    registered for packet reception or transmission.  This flag is ignored
    if it is specified along with ::NETIO_NO_XMIT and ::NETIO_NO_RECV.
    See @ref link for more information on link management.
 */
#define NETIO_AUTO_LINK_UPDN   (_NETIO_AUTO_PRESENT | _NETIO_AUTO_UP | \
                                _NETIO_AUTO_DN)

/** Set the desired state of the link to down the next time no tiles are
    registered for packet reception or transmission.  This flag is ignored
    if it is specified along with ::NETIO_NO_XMIT and ::NETIO_NO_RECV.
    See @ref link for more information on link management.
 */
#define NETIO_AUTO_LINK_DN     (_NETIO_AUTO_PRESENT | _NETIO_AUTO_DN)

/** Do not bring up the link automatically as part of this registration
    operation.  Do not take down the link automatically.  This flag
    is ignored if it is specified along with ::NETIO_NO_XMIT and
    ::NETIO_NO_RECV.  See @ref link for more information on link management.
  */
#define NETIO_AUTO_LINK_NONE   _NETIO_AUTO_PRESENT


/** Minimum number of receive packets. */
#define NETIO_MIN_RECEIVE_PKTS            16

/** Lower bound on the maximum number of receive packets; may be higher
    than this on some interfaces. */
#define NETIO_MAX_RECEIVE_PKTS           128

/** Maximum number of send buffers, per packet size. */
#define NETIO_MAX_SEND_BUFFERS            16

/** Number of EPP queue slots, and thus outstanding sends, per EPP. */
#define NETIO_TOTAL_SENDS_OUTSTANDING   2015

/** Minimum number of EPP queue slots, and thus outstanding sends, per
 *  transmitting tile. */
#define NETIO_MIN_SENDS_OUTSTANDING       16


/**@}*/

#ifndef __DOXYGEN__

/**
 * An object for providing Ethernet packets to a process.
 */
struct __netio_queue_impl_t;

/**
 * An object for managing the user end of a NetIO queue.
 */
struct __netio_queue_user_impl_t;

#endif /* !__DOXYGEN__ */


/** A netio_queue_t describes a NetIO communications endpoint.
 * @ingroup setup
 */
typedef struct
{
#ifdef __DOXYGEN__
  uint8_t opaque[8];                 /**< This is an opaque structure. */
#else
  struct __netio_queue_impl_t* __system_part;    /**< The system part. */
  struct __netio_queue_user_impl_t* __user_part; /**< The user part. */
#ifdef _NETIO_PTHREAD
  _netio_percpu_mutex_t lock;                    /**< Queue lock. */
#endif
#endif
}
netio_queue_t;


/**
 * @brief Packet send context.
 *
 * @ingroup egress
 *
 * Packet send context for use with netio_send_packet_prepare and _commit.
 */
typedef struct
{
#ifdef __DOXYGEN__
  uint8_t opaque[44];   /**< This is an opaque structure. */
#else
  uint8_t flags;        /**< Defined below */
  uint8_t datalen;      /**< Number of valid words pointed to by data. */
  uint32_t request[9];  /**< Request to be sent to the EPP or shim.  Note
                             that this is smaller than the 11-word maximum
                             request size, since some constant values are
                             not saved in the context. */
  uint32_t *data;       /**< Data to be sent to the EPP or shim via IDN. */
#endif
}
netio_send_pkt_context_t;


#ifndef __DOXYGEN__
#define SEND_PKT_CTX_USE_EPP   1  /**< We're sending to an EPP. */
#define SEND_PKT_CTX_SEND_CSUM 2  /**< Request includes a checksum. */
#endif

/**
 * @brief Packet vector entry.
 *
 * @ingroup egress
 *
 * This data structure is used with netio_send_packet_vector() to send multiple
 * packets with one NetIO call.  The structure should be initialized by
 * calling netio_pkt_vector_set(), rather than by setting the fields
 * directly.
 *
 * This structure is guaranteed to be a power of two in size, no
 * bigger than one L2 cache line, and to be aligned modulo its size.
 */
typedef struct
#ifndef __DOXYGEN__
__attribute__((aligned(8)))
#endif
{
  /** Reserved for use by the user application.  When initialized with
   *  the netio_set_pkt_vector_entry() function, this field is guaranteed
   *  to be visible to readers only after all other fields are already
   *  visible.  This way it can be used as a valid flag or generation
   *  counter. */
  uint8_t user_data;

  /* Structure members below this point should not be accessed directly by
   * applications, as they may change in the future. */

  /** Low 8 bits of the packet address to send.  The high bits are
   *  acquired from the 'handle' field. */
  uint8_t buffer_address_low;

  /** Number of bytes to transmit. */
  uint16_t size;

  /** The raw handle from a netio_pkt_t.  If this is NETIO_PKT_HANDLE_NONE,
   *  this vector entry will be skipped and no packet will be transmitted. */
  netio_pkt_handle_t handle;
}
netio_pkt_vector_entry_t;


/**
 * @brief Initialize fields in a packet vector entry.
 *
 * @ingroup egress
 *
 * @param[out] v Pointer to the vector entry to be initialized.
 * @param[in] pkt Packet to be transmitted when the vector entry is passed to
 *        netio_send_packet_vector().  Note that the packet's attributes
 *        (e.g., its L2 offset and length) are captured at the time this
 *        routine is called; subsequent changes in those attributes will not
 *        be reflected in the packet which is actually transmitted.
 *        Changes in the packet's contents, however, will be so reflected.
 *        If this is NULL, no packet will be transmitted.
 * @param[in] user_data User data to be set in the vector entry.
 *        This function guarantees that the "user_data" field will become
 *        visible to a reader only after all other fields have become visible.
 *        This allows a structure in a ring buffer to be written and read
 *        by a polling reader without any locks or other synchronization.
 */
static __inline void
netio_pkt_vector_set(volatile netio_pkt_vector_entry_t* v, netio_pkt_t* pkt,
                     uint8_t user_data)
{
  if (pkt)
  {
    if (NETIO_PKT_IS_MINIMAL(pkt))
    {
      netio_pkt_minimal_metadata_t* mmd =
        (netio_pkt_minimal_metadata_t*) &pkt->__metadata;
      v->buffer_address_low = (uintptr_t) NETIO_PKT_L2_DATA_MM(mmd, pkt) & 0xFF;
      v->size = NETIO_PKT_L2_LENGTH_MM(mmd, pkt);
    }
    else
    {
      netio_pkt_metadata_t* mda = &pkt->__metadata;
      v->buffer_address_low = (uintptr_t) NETIO_PKT_L2_DATA_M(mda, pkt) & 0xFF;
      v->size = NETIO_PKT_L2_LENGTH_M(mda, pkt);
    }
    v->handle.word = pkt->__packet.word;
  }
  else
  {
    v->handle.word = 0;   /* Set handle to NETIO_PKT_HANDLE_NONE. */
  }

  __asm__("" : : : "memory");

  v->user_data = user_data;
}


/**
 * Flags and structures for @ref netio_get() and @ref netio_set().
 * @ingroup config
 */

/** @{ */
/** Parameter class; addr is a NETIO_PARAM_xxx value. */
#define NETIO_PARAM       0
/** Interface MAC address. This address is only valid with @ref netio_get().
 *  The value is a 6-byte MAC address.  Depending upon the overall system
 *  design, a MAC address may or may not be available for each interface. */
#define NETIO_PARAM_MAC        0

/** Determine whether to suspend output on the receipt of pause frames.
 *  If the value is nonzero, the I/O shim will suspend output when a pause
 *  frame is received.  If the value is zero, pause frames will be ignored. */
#define NETIO_PARAM_PAUSE_IN   1

/** Determine whether to send pause frames if the I/O shim packet FIFOs are
 *  nearly full.  If the value is zero, pause frames are not sent.  If
 *  the value is nonzero, it is the delay value which will be sent in any
 *  pause frames which are output, in units of 512 bit times. */
#define NETIO_PARAM_PAUSE_OUT  2

/** Jumbo frame support.  The value is a 4-byte integer.  If the value is
 *  nonzero, the MAC will accept frames of up to 10240 bytes.  If the value
 *  is zero, the MAC will only accept frames of up to 1544 bytes. */
#define NETIO_PARAM_JUMBO      3

/** I/O shim's overflow statistics register.  The value is two 16-bit integers.
 *  The first 16-bit value (or the low 16 bits, if the value is treated as a
 *  32-bit number) is the count of packets which were completely dropped and
 *  not delivered by the shim.  The second 16-bit value (or the high 16 bits,
 *  if the value is treated as a 32-bit number) is the count of packets
 *  which were truncated and thus only partially delivered by the shim.  This
 *  register is automatically reset to zero after it has been read.
 */
#define NETIO_PARAM_OVERFLOW   4

/** IPP statistics.  This address is only valid with @ref netio_get().  The
 *  value is a netio_stat_t structure.  Unlike the I/O shim statistics, the
 *  IPP statistics are not all reset to zero on read; see the description
 *  of the netio_stat_t for details. */
#define NETIO_PARAM_STAT 5

/** Possible link state.  The value is a combination of "NETIO_LINK_xxx"
 *  flags.  With @ref netio_get(), this will indicate which flags are
 *  actually supported by the hardware.
 *
 *  For historical reasons, specifying this value to netio_set() will have
 *  the same behavior as using ::NETIO_PARAM_LINK_CONFIG, but this usage is
 *  discouraged.
 */
#define NETIO_PARAM_LINK_POSSIBLE_STATE 6

/** Link configuration. The value is a combination of "NETIO_LINK_xxx" flags.
 *  With @ref netio_set(), this will attempt to immediately bring up the
 *  link using whichever of the requested flags are supported by the
 *  hardware, or take down the link if the flags are zero; if this is
 *  not possible, an error will be returned.  Many programs will want
 *  to use ::NETIO_PARAM_LINK_DESIRED_STATE instead.
 *
 *  For historical reasons, specifying this value to netio_get() will
 *  have the same behavior as using ::NETIO_PARAM_LINK_POSSIBLE_STATE,
 *  but this usage is discouraged.
 */
#define NETIO_PARAM_LINK_CONFIG NETIO_PARAM_LINK_POSSIBLE_STATE

/** Current link state. This address is only valid with @ref netio_get().
 *  The value is zero or more of the "NETIO_LINK_xxx" flags, ORed together.
 *  If the link is down, the value ANDed with NETIO_LINK_SPEED will be
 *  zero; if the link is up, the value ANDed with NETIO_LINK_SPEED will
 *  result in exactly one of the NETIO_LINK_xxx values, indicating the
 *  current speed. */
#define NETIO_PARAM_LINK_CURRENT_STATE 7

/** Variant symbol for current state, retained for compatibility with
 *  pre-MDE-2.1 programs. */
#define NETIO_PARAM_LINK_STATUS NETIO_PARAM_LINK_CURRENT_STATE

/** Packet Coherence protocol. This address is only valid with @ref netio_get().
 *  The value is nonzero if the interface is configured for cache-coherent DMA.
 */
#define NETIO_PARAM_COHERENT 8

/** Desired link state. The value is a conbination of "NETIO_LINK_xxx"
 *  flags, which specify the desired state for the link.  With @ref
 *  netio_set(), this will, in the background, attempt to bring up the link
 *  using whichever of the requested flags are reasonable, or take down the
 *  link if the flags are zero.  The actual link up or down operation may
 *  happen after this call completes.  If the link state changes in the
 *  future, the system will continue to try to get back to the desired link
 *  state; for instance, if the link is brought up successfully, and then
 *  the network cable is disconnected, the link will go down.  However, the
 *  desired state of the link is still up, so if the cable is reconnected,
 *  the link will be brought up again.
 *
 *  With @ref netio_get(), this will indicate the desired state for the
 *  link, as set with a previous netio_set() call, or implicitly by a
 *  netio_input_register() or netio_input_unregister() operation.  This may
 *  not reflect the current state of the link; to get that, use
 *  ::NETIO_PARAM_LINK_CURRENT_STATE. */
#define NETIO_PARAM_LINK_DESIRED_STATE 9

/** NetIO statistics structure.  Retrieved using the ::NETIO_PARAM_STAT
 *  address passed to @ref netio_get(). */
typedef struct
{
  /** Number of packets which have been received by the IPP and forwarded
   *  to a tile's receive queue for processing.  This value wraps at its
   *  maximum, and is not cleared upon read. */
  uint32_t packets_received;

  /** Number of packets which have been dropped by the IPP, because they could
   *  not be received, or could not be forwarded to a tile.  The former happens
   *  when the IPP does not have a free packet buffer of suitable size for an
   *  incoming frame.  The latter happens when all potential destination tiles
   *  for a packet, as defined by the group, bucket, and queue configuration,
   *  have full receive queues.   This value wraps at its maximum, and is not
   *  cleared upon read. */
  uint32_t packets_dropped;

  /*
   * Note: the #defines after each of the following four one-byte values
   * denote their location within the third word of the netio_stat_t.  They
   * are intended for use only by the IPP implementation and are thus omitted
   * from the Doxygen output.
   */

  /** Number of packets dropped because no worker was able to accept a new
   *  packet.  This value saturates at its maximum, and is cleared upon
   *  read. */
  uint8_t drops_no_worker;
#ifndef __DOXYGEN__
#define NETIO_STAT_DROPS_NO_WORKER   0
#endif

  /** Number of packets dropped because no small buffers were available.
   *  This value saturates at its maximum, and is cleared upon read. */
  uint8_t drops_no_smallbuf;
#ifndef __DOXYGEN__
#define NETIO_STAT_DROPS_NO_SMALLBUF 1
#endif

  /** Number of packets dropped because no large buffers were available.
   *  This value saturates at its maximum, and is cleared upon read. */
  uint8_t drops_no_largebuf;
#ifndef __DOXYGEN__
#define NETIO_STAT_DROPS_NO_LARGEBUF 2
#endif

  /** Number of packets dropped because no jumbo buffers were available.
   *  This value saturates at its maximum, and is cleared upon read. */
  uint8_t drops_no_jumbobuf;
#ifndef __DOXYGEN__
#define NETIO_STAT_DROPS_NO_JUMBOBUF 3
#endif
}
netio_stat_t;


/** Link can run, should run, or is running at 10 Mbps. */
#define NETIO_LINK_10M         0x01

/** Link can run, should run, or is running at 100 Mbps. */
#define NETIO_LINK_100M        0x02

/** Link can run, should run, or is running at 1 Gbps. */
#define NETIO_LINK_1G          0x04

/** Link can run, should run, or is running at 10 Gbps. */
#define NETIO_LINK_10G         0x08

/** Link should run at the highest speed supported by the link and by
 *  the device connected to the link.  Only usable as a value for
 *  the link's desired state; never returned as a value for the current
 *  or possible states. */
#define NETIO_LINK_ANYSPEED    0x10

/** All legal link speeds. */
#define NETIO_LINK_SPEED  (NETIO_LINK_10M  | \
                           NETIO_LINK_100M | \
                           NETIO_LINK_1G   | \
                           NETIO_LINK_10G  | \
                           NETIO_LINK_ANYSPEED)


/** MAC register class.  Addr is a register offset within the MAC.
 *  Registers within the XGbE and GbE MACs are documented in the Tile
 *  Processor I/O Device Guide (UG104). MAC registers start at address
 *  0x4000, and do not include the MAC_INTERFACE registers. */
#define NETIO_MAC             1

/** MDIO register class (IEEE 802.3 clause 22 format).  Addr is the "addr"
 *  member of a netio_mdio_addr_t structure. */
#define NETIO_MDIO            2

/** MDIO register class (IEEE 802.3 clause 45 format).  Addr is the "addr"
 *  member of a netio_mdio_addr_t structure. */
#define NETIO_MDIO_CLAUSE45   3

/** NetIO MDIO address type.  Retrieved or provided using the ::NETIO_MDIO
 *  address passed to @ref netio_get() or @ref netio_set(). */
typedef union
{
  struct
  {
    unsigned int reg:16;  /**< MDIO register offset.  For clause 22 access,
                               must be less than 32. */
    unsigned int phy:5;   /**< Which MDIO PHY to access. */
    unsigned int dev:5;   /**< Which MDIO device to access within that PHY.
                               Applicable for clause 45 access only; ignored
                               for clause 22 access. */
  }
  bits;                   /**< Container for bitfields. */
  uint64_t addr;          /**< Value to pass to @ref netio_get() or
                           *   @ref netio_set(). */
}
netio_mdio_addr_t;

/** @} */

#endif /* __NETIO_INTF_H__ */
