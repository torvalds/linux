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
 * @file drv_xgbe_intf.h
 * Interface to the hypervisor XGBE driver.
 */

#ifndef __DRV_XGBE_INTF_H__
#define __DRV_XGBE_INTF_H__

/**
 * An object for forwarding VAs and PAs to the hypervisor.
 * @ingroup types
 *
 * This allows the supervisor to specify a number of areas of memory to
 * store packet buffers.
 */
typedef struct
{
  /** The physical address of the memory. */
  HV_PhysAddr pa;
  /** Page table entry for the memory.  This is only used to derive the
   *  memory's caching mode; the PA bits are ignored. */
  HV_PTE pte;
  /** The virtual address of the memory. */
  HV_VirtAddr va;
  /** Size (in bytes) of the memory area. */
  int size;

}
netio_ipp_address_t;

/** The various pread/pwrite offsets into the hypervisor-level driver.
 * @ingroup types
 */
typedef enum
{
  /** Inform the Linux driver of the address of the NetIO arena memory.
   *  This offset is actually only used to convey information from netio
   *  to the Linux driver; it never makes it from there to the hypervisor.
   *  Write-only; takes a uint32_t specifying the VA address. */
  NETIO_FIXED_ADDR               = 0x5000000000000000ULL,

  /** Inform the Linux driver of the size of the NetIO arena memory.
   *  This offset is actually only used to convey information from netio
   *  to the Linux driver; it never makes it from there to the hypervisor.
   *  Write-only; takes a uint32_t specifying the VA size. */
  NETIO_FIXED_SIZE               = 0x5100000000000000ULL,

  /** Register current tile with IPP.  Write then read: write, takes a
   *  netio_input_config_t, read returns a pointer to a netio_queue_impl_t. */
  NETIO_IPP_INPUT_REGISTER_OFF   = 0x6000000000000000ULL,

  /** Unregister current tile from IPP.  Write-only, takes a dummy argument. */
  NETIO_IPP_INPUT_UNREGISTER_OFF = 0x6100000000000000ULL,

  /** Start packets flowing.  Write-only, takes a dummy argument. */
  NETIO_IPP_INPUT_INIT_OFF       = 0x6200000000000000ULL,

  /** Stop packets flowing.  Write-only, takes a dummy argument. */
  NETIO_IPP_INPUT_UNINIT_OFF     = 0x6300000000000000ULL,

  /** Configure group (typically we group on VLAN).  Write-only: takes an
   *  array of netio_group_t's, low 24 bits of the offset is the base group
   *  number times the size of a netio_group_t. */
  NETIO_IPP_INPUT_GROUP_CFG_OFF  = 0x6400000000000000ULL,

  /** Configure bucket.  Write-only: takes an array of netio_bucket_t's, low
   *  24 bits of the offset is the base bucket number times the size of a
   *  netio_bucket_t. */
  NETIO_IPP_INPUT_BUCKET_CFG_OFF = 0x6500000000000000ULL,

  /** Get/set a parameter.  Read or write: read or write data is the parameter
   *  value, low 32 bits of the offset is a __netio_getset_offset_t. */
  NETIO_IPP_PARAM_OFF            = 0x6600000000000000ULL,

  /** Get fast I/O index.  Read-only; returns a 4-byte base index value. */
  NETIO_IPP_GET_FASTIO_OFF       = 0x6700000000000000ULL,

  /** Configure hijack IP address.  Packets with this IPv4 dest address
   *  go to bucket NETIO_NUM_BUCKETS - 1.  Write-only: takes an IP address
   *  in some standard form.  FIXME: Define the form! */
  NETIO_IPP_INPUT_HIJACK_CFG_OFF  = 0x6800000000000000ULL,

  /**
   * Offsets beyond this point are reserved for the supervisor (although that
   * enforcement must be done by the supervisor driver itself).
   */
  NETIO_IPP_USER_MAX_OFF         = 0x6FFFFFFFFFFFFFFFULL,

  /** Register I/O memory.  Write-only, takes a netio_ipp_address_t. */
  NETIO_IPP_IOMEM_REGISTER_OFF   = 0x7000000000000000ULL,

  /** Unregister I/O memory.  Write-only, takes a netio_ipp_address_t. */
  NETIO_IPP_IOMEM_UNREGISTER_OFF = 0x7100000000000000ULL,

  /* Offsets greater than 0x7FFFFFFF can't be used directly from Linux
   * userspace code due to limitations in the pread/pwrite syscalls. */

  /** Drain LIPP buffers. */
  NETIO_IPP_DRAIN_OFF              = 0xFA00000000000000ULL,

  /** Supply a netio_ipp_address_t to be used as shared memory for the
   *  LEPP command queue. */
  NETIO_EPP_SHM_OFF              = 0xFB00000000000000ULL,

  /* 0xFC... is currently unused. */

  /** Stop IPP/EPP tiles.  Write-only, takes a dummy argument.  */
  NETIO_IPP_STOP_SHIM_OFF        = 0xFD00000000000000ULL,

  /** Start IPP/EPP tiles.  Write-only, takes a dummy argument.  */
  NETIO_IPP_START_SHIM_OFF       = 0xFE00000000000000ULL,

  /** Supply packet arena.  Write-only, takes an array of
    * netio_ipp_address_t values. */
  NETIO_IPP_ADDRESS_OFF          = 0xFF00000000000000ULL,
} netio_hv_offset_t;

/** Extract the base offset from an offset */
#define NETIO_BASE_OFFSET(off)    ((off) & 0xFF00000000000000ULL)
/** Extract the local offset from an offset */
#define NETIO_LOCAL_OFFSET(off)   ((off) & 0x00FFFFFFFFFFFFFFULL)


/**
 * Get/set offset.
 */
typedef union
{
  struct
  {
    uint64_t addr:48;        /**< Class-specific address */
    unsigned int class:8;    /**< Class (e.g., NETIO_PARAM) */
    unsigned int opcode:8;   /**< High 8 bits of NETIO_IPP_PARAM_OFF */
  }
  bits;                      /**< Bitfields */
  uint64_t word;             /**< Aggregated value to use as the offset */
}
__netio_getset_offset_t;

/**
 * Fast I/O index offsets (must be contiguous).
 */
typedef enum
{
  NETIO_FASTIO_ALLOCATE         = 0, /**< Get empty packet buffer */
  NETIO_FASTIO_FREE_BUFFER      = 1, /**< Give buffer back to IPP */
  NETIO_FASTIO_RETURN_CREDITS   = 2, /**< Give credits to IPP */
  NETIO_FASTIO_SEND_PKT_NOCK    = 3, /**< Send a packet, no checksum */
  NETIO_FASTIO_SEND_PKT_CK      = 4, /**< Send a packet, with checksum */
  NETIO_FASTIO_SEND_PKT_VEC     = 5, /**< Send a vector of packets */
  NETIO_FASTIO_SENDV_PKT        = 6, /**< Sendv one packet */
  NETIO_FASTIO_NUM_INDEX        = 7, /**< Total number of fast I/O indices */
} netio_fastio_index_t;

/** 3-word return type for Fast I/O call. */
typedef struct
{
  int err;            /**< Error code. */
  uint32_t val0;      /**< Value.  Meaning depends upon the specific call. */
  uint32_t val1;      /**< Value.  Meaning depends upon the specific call. */
} netio_fastio_rv3_t;

/** 0-argument fast I/O call */
int __netio_fastio0(uint32_t fastio_index);
/** 1-argument fast I/O call */
int __netio_fastio1(uint32_t fastio_index, uint32_t arg0);
/** 3-argument fast I/O call, 2-word return value */
netio_fastio_rv3_t __netio_fastio3_rv3(uint32_t fastio_index, uint32_t arg0,
                                       uint32_t arg1, uint32_t arg2);
/** 4-argument fast I/O call */
int __netio_fastio4(uint32_t fastio_index, uint32_t arg0, uint32_t arg1,
                    uint32_t arg2, uint32_t arg3);
/** 6-argument fast I/O call */
int __netio_fastio6(uint32_t fastio_index, uint32_t arg0, uint32_t arg1,
                    uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5);
/** 9-argument fast I/O call */
int __netio_fastio9(uint32_t fastio_index, uint32_t arg0, uint32_t arg1,
                    uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5,
                    uint32_t arg6, uint32_t arg7, uint32_t arg8);

/** Allocate an empty packet.
 * @param fastio_index Fast I/O index.
 * @param size Size of the packet to allocate.
 */
#define __netio_fastio_allocate(fastio_index, size) \
  __netio_fastio1((fastio_index) + NETIO_FASTIO_ALLOCATE, size)

/** Free a buffer.
 * @param fastio_index Fast I/O index.
 * @param handle Handle for the packet to free.
 */
#define __netio_fastio_free_buffer(fastio_index, handle) \
  __netio_fastio1((fastio_index) + NETIO_FASTIO_FREE_BUFFER, handle)

/** Increment our receive credits.
 * @param fastio_index Fast I/O index.
 * @param credits Number of credits to add.
 */
#define __netio_fastio_return_credits(fastio_index, credits) \
  __netio_fastio1((fastio_index) + NETIO_FASTIO_RETURN_CREDITS, credits)

/** Send packet, no checksum.
 * @param fastio_index Fast I/O index.
 * @param ackflag Nonzero if we want an ack.
 * @param size Size of the packet.
 * @param va Virtual address of start of packet.
 * @param handle Packet handle.
 */
#define __netio_fastio_send_pkt_nock(fastio_index, ackflag, size, va, handle) \
  __netio_fastio4((fastio_index) + NETIO_FASTIO_SEND_PKT_NOCK, ackflag, \
                  size, va, handle)

/** Send packet, calculate checksum.
 * @param fastio_index Fast I/O index.
 * @param ackflag Nonzero if we want an ack.
 * @param size Size of the packet.
 * @param va Virtual address of start of packet.
 * @param handle Packet handle.
 * @param csum0 Shim checksum header.
 * @param csum1 Checksum seed.
 */
#define __netio_fastio_send_pkt_ck(fastio_index, ackflag, size, va, handle, \
                                   csum0, csum1) \
  __netio_fastio6((fastio_index) + NETIO_FASTIO_SEND_PKT_CK, ackflag, \
                  size, va, handle, csum0, csum1)


/** Format for the "csum0" argument to the __netio_fastio_send routines
 * and LEPP.  Note that this is currently exactly identical to the
 * ShimProtocolOffloadHeader.
 */
typedef union
{
  struct
  {
    unsigned int start_byte:7;       /**< The first byte to be checksummed */
    unsigned int count:14;           /**< Number of bytes to be checksummed. */
    unsigned int destination_byte:7; /**< The byte to write the checksum to. */
    unsigned int reserved:4;         /**< Reserved. */
  } bits;                            /**< Decomposed method of access. */
  unsigned int word;                 /**< To send out the IDN. */
} __netio_checksum_header_t;


/** Sendv packet with 1 or 2 segments.
 * @param fastio_index Fast I/O index.
 * @param flags Ack/csum/notify flags in low 3 bits; number of segments minus
 *        1 in next 2 bits; expected checksum in high 16 bits.
 * @param confno Confirmation number to request, if notify flag set.
 * @param csum0 Checksum descriptor; if zero, no checksum.
 * @param va_F Virtual address of first segment.
 * @param va_L Virtual address of last segment, if 2 segments.
 * @param len_F_L Length of first segment in low 16 bits; length of last
 *        segment, if 2 segments, in high 16 bits.
 */
#define __netio_fastio_sendv_pkt_1_2(fastio_index, flags, confno, csum0, \
                                     va_F, va_L, len_F_L) \
  __netio_fastio6((fastio_index) + NETIO_FASTIO_SENDV_PKT, flags, confno, \
                  csum0, va_F, va_L, len_F_L)

/** Send packet on PCIe interface.
 * @param fastio_index Fast I/O index.
 * @param flags Ack/csum/notify flags in low 3 bits.
 * @param confno Confirmation number to request, if notify flag set.
 * @param csum0 Checksum descriptor; Hard wired 0, not needed for PCIe.
 * @param va_F Virtual address of the packet buffer.
 * @param va_L Virtual address of last segment, if 2 segments. Hard wired 0.
 * @param len_F_L Length of the packet buffer in low 16 bits.
 */
#define __netio_fastio_send_pcie_pkt(fastio_index, flags, confno, csum0, \
                                     va_F, va_L, len_F_L) \
  __netio_fastio6((fastio_index) + PCIE_FASTIO_SENDV_PKT, flags, confno, \
                  csum0, va_F, va_L, len_F_L)

/** Sendv packet with 3 or 4 segments.
 * @param fastio_index Fast I/O index.
 * @param flags Ack/csum/notify flags in low 3 bits; number of segments minus
 *        1 in next 2 bits; expected checksum in high 16 bits.
 * @param confno Confirmation number to request, if notify flag set.
 * @param csum0 Checksum descriptor; if zero, no checksum.
 * @param va_F Virtual address of first segment.
 * @param va_L Virtual address of last segment (third segment if 3 segments,
 *        fourth segment if 4 segments).
 * @param len_F_L Length of first segment in low 16 bits; length of last
 *        segment in high 16 bits.
 * @param va_M0 Virtual address of "middle 0" segment; this segment is sent
 *        second when there are three segments, and third if there are four.
 * @param va_M1 Virtual address of "middle 1" segment; this segment is sent
 *        second when there are four segments.
 * @param len_M0_M1 Length of middle 0 segment in low 16 bits; length of middle
 *        1 segment, if 4 segments, in high 16 bits.
 */
#define __netio_fastio_sendv_pkt_3_4(fastio_index, flags, confno, csum0, va_F, \
                                     va_L, len_F_L, va_M0, va_M1, len_M0_M1) \
  __netio_fastio9((fastio_index) + NETIO_FASTIO_SENDV_PKT, flags, confno, \
                  csum0, va_F, va_L, len_F_L, va_M0, va_M1, len_M0_M1)

/** Send vector of packets.
 * @param fastio_index Fast I/O index.
 * @param seqno Number of packets transmitted so far on this interface;
 *        used to decide which packets should be acknowledged.
 * @param nentries Number of entries in vector.
 * @param va Virtual address of start of vector entry array.
 * @return 3-word netio_fastio_rv3_t structure.  The structure's err member
 *         is an error code, or zero if no error.  The val0 member is the
 *         updated value of seqno; it has been incremented by 1 for each
 *         packet sent.  That increment may be less than nentries if an
 *         error occurred, or if some of the entries in the vector contain
 *         handles equal to NETIO_PKT_HANDLE_NONE.  The val1 member is the
 *         updated value of nentries; it has been decremented by 1 for each
 *         vector entry processed.  Again, that decrement may be less than
 *         nentries (leaving the returned value positive) if an error
 *         occurred.
 */
#define __netio_fastio_send_pkt_vec(fastio_index, seqno, nentries, va) \
  __netio_fastio3_rv3((fastio_index) + NETIO_FASTIO_SEND_PKT_VEC, seqno, \
                      nentries, va)


/** An egress DMA command for LEPP. */
typedef struct
{
  /** Is this a TSO transfer?
   *
   * NOTE: This field is always 0, to distinguish it from
   * lepp_tso_cmd_t.  It must come first!
   */
  uint8_t tso               : 1;

  /** Unused padding bits. */
  uint8_t _unused           : 3;

  /** Should this packet be sent directly from caches instead of DRAM,
   * using hash-for-home to locate the packet data?
   */
  uint8_t hash_for_home     : 1;

  /** Should we compute a checksum? */
  uint8_t compute_checksum  : 1;

  /** Is this the final buffer for this packet?
   *
   * A single packet can be split over several input buffers (a "gather"
   * operation).  This flag indicates that this is the last buffer
   * in a packet.
   */
  uint8_t end_of_packet     : 1;

  /** Should LEPP advance 'comp_busy' when this DMA is fully finished? */
  uint8_t send_completion   : 1;

  /** High bits of Client Physical Address of the start of the buffer
   *  to be egressed.
   *
   *  NOTE: Only 6 bits are actually needed here, as CPAs are
   *  currently 38 bits.  So two bits could be scavenged from this.
   */
  uint8_t cpa_hi;

  /** The number of bytes to be egressed. */
  uint16_t length;

  /** Low 32 bits of Client Physical Address of the start of the buffer
   *  to be egressed.
   */
  uint32_t cpa_lo;

  /** Checksum information (only used if 'compute_checksum'). */
  __netio_checksum_header_t checksum_data;

} lepp_cmd_t;


/** A chunk of physical memory for a TSO egress. */
typedef struct
{
  /** The low bits of the CPA. */
  uint32_t cpa_lo;
  /** The high bits of the CPA. */
  uint16_t cpa_hi		: 15;
  /** Should this packet be sent directly from caches instead of DRAM,
   *  using hash-for-home to locate the packet data?
   */
  uint16_t hash_for_home	: 1;
  /** The length in bytes. */
  uint16_t length;
} lepp_frag_t;


/** An LEPP command that handles TSO. */
typedef struct
{
  /** Is this a TSO transfer?
   *
   *  NOTE: This field is always 1, to distinguish it from
   *  lepp_cmd_t.  It must come first!
   */
  uint8_t tso             : 1;

  /** Unused padding bits. */
  uint8_t _unused         : 7;

  /** Size of the header[] array in bytes.  It must be in the range
   *  [40, 127], which are the smallest header for a TCP packet over
   *  Ethernet and the maximum possible prepend size supported by
   *  hardware, respectively.  Note that the array storage must be
   *  padded out to a multiple of four bytes so that the following
   *  LEPP command is aligned properly.
   */
  uint8_t header_size;

  /** Byte offset of the IP header in header[]. */
  uint8_t ip_offset;

  /** Byte offset of the TCP header in header[]. */
  uint8_t tcp_offset;

  /** The number of bytes to use for the payload of each packet,
   *  except of course the last one, which may not have enough bytes.
   *  This means that each Ethernet packet except the last will have a
   *  size of header_size + payload_size.
   */
  uint16_t payload_size;

  /** The length of the 'frags' array that follows this struct. */
  uint16_t num_frags;

  /** The actual frags. */
  lepp_frag_t frags[0 /* Variable-sized; num_frags entries. */];

  /*
   * The packet header template logically follows frags[],
   * but you can't declare that in C.
   *
   * uint32_t header[header_size_in_words_rounded_up];
   */

} lepp_tso_cmd_t;


/** An LEPP completion ring entry. */
typedef void* lepp_comp_t;


/** Maximum number of frags for one TSO command.  This is adapted from
 *  linux's "MAX_SKB_FRAGS", and presumably over-estimates by one, for
 *  our page size of exactly 65536.  We add one for a "body" fragment.
 */
#define LEPP_MAX_FRAGS (65536 / HV_PAGE_SIZE_SMALL + 2 + 1)

/** Total number of bytes needed for an lepp_tso_cmd_t. */
#define LEPP_TSO_CMD_SIZE(num_frags, header_size) \
  (sizeof(lepp_tso_cmd_t) + \
   (num_frags) * sizeof(lepp_frag_t) + \
   (((header_size) + 3) & -4))

/** The size of the lepp "cmd" queue. */
#define LEPP_CMD_QUEUE_BYTES \
 (((CHIP_L2_CACHE_SIZE() - 2 * CHIP_L2_LINE_SIZE()) / \
  (sizeof(lepp_cmd_t) + sizeof(lepp_comp_t))) * sizeof(lepp_cmd_t))

/** The largest possible command that can go in lepp_queue_t::cmds[]. */
#define LEPP_MAX_CMD_SIZE LEPP_TSO_CMD_SIZE(LEPP_MAX_FRAGS, 128)

/** The largest possible value of lepp_queue_t::cmd_{head, tail} (inclusive).
 */
#define LEPP_CMD_LIMIT \
  (LEPP_CMD_QUEUE_BYTES - LEPP_MAX_CMD_SIZE)

/** The maximum number of completions in an LEPP queue. */
#define LEPP_COMP_QUEUE_SIZE \
  ((LEPP_CMD_LIMIT + sizeof(lepp_cmd_t) - 1) / sizeof(lepp_cmd_t))

/** Increment an index modulo the queue size. */
#define LEPP_QINC(var) \
  (var = __insn_mnz(var - (LEPP_COMP_QUEUE_SIZE - 1), var + 1))

/** A queue used to convey egress commands from the client to LEPP. */
typedef struct
{
  /** Index of first completion not yet processed by user code.
   *  If this is equal to comp_busy, there are no such completions.
   *
   *  NOTE: This is only read/written by the user.
   */
  unsigned int comp_head;

  /** Index of first completion record not yet completed.
   *  If this is equal to comp_tail, there are no such completions.
   *  This index gets advanced (modulo LEPP_QUEUE_SIZE) whenever
   *  a command with the 'completion' bit set is finished.
   *
   *  NOTE: This is only written by LEPP, only read by the user.
   */
  volatile unsigned int comp_busy;

  /** Index of the first empty slot in the completion ring.
   *  Entries from this up to but not including comp_head (in ring order)
   *  can be filled in with completion data.
   *
   *  NOTE: This is only read/written by the user.
   */
  unsigned int comp_tail;

  /** Byte index of first command enqueued for LEPP but not yet processed.
   *
   *  This is always divisible by sizeof(void*) and always <= LEPP_CMD_LIMIT.
   *
   *  NOTE: LEPP advances this counter as soon as it no longer needs
   *  the cmds[] storage for this entry, but the transfer is not actually
   *  complete (i.e. the buffer pointed to by the command is no longer
   *  needed) until comp_busy advances.
   *
   *  If this is equal to cmd_tail, the ring is empty.
   *
   *  NOTE: This is only written by LEPP, only read by the user.
   */
  volatile unsigned int cmd_head;

  /** Byte index of first empty slot in the command ring.  This field can
   *  be incremented up to but not equal to cmd_head (because that would
   *  mean the ring is empty).
   *
   *  This is always divisible by sizeof(void*) and always <= LEPP_CMD_LIMIT.
   *
   *  NOTE: This is read/written by the user, only read by LEPP.
   */
  volatile unsigned int cmd_tail;

  /** A ring of variable-sized egress DMA commands.
   *
   *  NOTE: Only written by the user, only read by LEPP.
   */
  char cmds[LEPP_CMD_QUEUE_BYTES]
    __attribute__((aligned(CHIP_L2_LINE_SIZE())));

  /** A ring of user completion data.
   *  NOTE: Only read/written by the user.
   */
  lepp_comp_t comps[LEPP_COMP_QUEUE_SIZE]
    __attribute__((aligned(CHIP_L2_LINE_SIZE())));
} lepp_queue_t;


/** An internal helper function for determining the number of entries
 *  available in a ring buffer, given that there is one sentinel.
 */
static inline unsigned int
_lepp_num_free_slots(unsigned int head, unsigned int tail)
{
  /*
   * One entry is reserved for use as a sentinel, to distinguish
   * "empty" from "full".  So we compute
   * (head - tail - 1) % LEPP_QUEUE_SIZE, but without using a slow % operation.
   */
  return (head - tail - 1) + ((head <= tail) ? LEPP_COMP_QUEUE_SIZE : 0);
}


/** Returns how many new comp entries can be enqueued. */
static inline unsigned int
lepp_num_free_comp_slots(const lepp_queue_t* q)
{
  return _lepp_num_free_slots(q->comp_head, q->comp_tail);
}

static inline int
lepp_qsub(int v1, int v2)
{
  int delta = v1 - v2;
  return delta + ((delta >> 31) & LEPP_COMP_QUEUE_SIZE);
}


/** FIXME: Check this from linux, via a new "pwrite()" call. */
#define LIPP_VERSION 1


/** We use exactly two bytes of alignment padding. */
#define LIPP_PACKET_PADDING 2

/** The minimum size of a "small" buffer (including the padding). */
#define LIPP_SMALL_PACKET_SIZE 128

/*
 * NOTE: The following two values should total to less than around
 * 13582, to keep the total size used for "lipp_state_t" below 64K.
 */

/** The maximum number of "small" buffers.
 *  This is enough for 53 network cpus with 128 credits.  Note that
 *  if these are exhausted, we will fall back to using large buffers.
 */
#define LIPP_SMALL_BUFFERS 6785

/** The maximum number of "large" buffers.
 *  This is enough for 53 network cpus with 128 credits.
 */
#define LIPP_LARGE_BUFFERS 6785

#endif /* __DRV_XGBE_INTF_H__ */
