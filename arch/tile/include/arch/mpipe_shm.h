/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
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

/* Machine-generated file; do not edit. */


#ifndef __ARCH_MPIPE_SHM_H__
#define __ARCH_MPIPE_SHM_H__

#include <arch/abi.h>
#include <arch/mpipe_shm_def.h>

#ifndef __ASSEMBLER__
/**
 * MPIPE eDMA Descriptor.
 * The eDMA descriptor is written by software and consumed by hardware.  It
 * is used to specify the location of egress packet data to be sent out of
 * the chip via one of the packet interfaces.
 */

__extension__
typedef union
{
  struct
  {
    /* Word 0 */

#ifndef __BIG_ENDIAN__
    /**
     * Generation number.  Used to indicate a valid descriptor in ring.  When
     * a new descriptor is written into the ring, software must toggle this
     * bit.  The net effect is that the GEN bit being written into new
     * descriptors toggles each time the ring tail pointer wraps.
     */
    uint_reg_t gen        : 1;
    /**
     * For devices with EDMA reorder support, this field allows the
     * descriptor to select the egress FIFO.  The associated DMA ring must
     * have ALLOW_EFIFO_SEL enabled.
     */
    uint_reg_t efifo_sel  : 6;
    /** Reserved.  Must be zero. */
    uint_reg_t r0         : 1;
    /** Checksum generation enabled for this transfer. */
    uint_reg_t csum       : 1;
    /**
     * Nothing to be sent.  Used, for example, when software has dropped a
     * packet but still wishes to return all of the associated buffers.
     */
    uint_reg_t ns         : 1;
    /**
     * Notification interrupt will be delivered when packet has been egressed.
     */
    uint_reg_t notif      : 1;
    /**
     * Boundary indicator.  When 1, this transfer includes the EOP for this
     * command.  Must be clear on all but the last descriptor for an egress
     * packet.
     */
    uint_reg_t bound      : 1;
    /** Reserved.  Must be zero. */
    uint_reg_t r1         : 4;
    /**
     * Number of bytes to be sent for this descriptor.  When zero, no data
     * will be moved and the buffer descriptor will be ignored.  If the
     * buffer descriptor indicates that it is chained, the low 7 bits of the
     * VA indicate the offset within the first buffer (e.g. 127 bytes is the
     * maximum offset into the first buffer).  If the size exceeds a single
     * buffer, subsequent buffer descriptors will be fetched prior to
     * processing the next eDMA descriptor in the ring.
     */
    uint_reg_t xfer_size  : 14;
    /** Reserved.  Must be zero. */
    uint_reg_t r2         : 2;
    /**
     * Destination of checksum relative to CSUM_START relative to the first
     * byte moved by this descriptor.  Must be zero if CSUM=0 in this
     * descriptor.  Must be less than XFER_SIZE (e.g. the first byte of the
     * CSUM_DEST must be within the span of this descriptor).
     */
    uint_reg_t csum_dest  : 8;
    /**
     * Start byte of checksum relative to the first byte moved by this
     * descriptor.  If this is not the first descriptor for the egress
     * packet, CSUM_START is still relative to the first byte in this
     * descriptor.  Must be zero if CSUM=0 in this descriptor.
     */
    uint_reg_t csum_start : 8;
    /**
     * Initial value for 16-bit 1's compliment checksum if enabled via CSUM.
     * Specified in network order.  That is, bits[7:0] will be added to the
     * byte pointed to by CSUM_START and bits[15:8] will be added to the byte
     * pointed to by CSUM_START+1 (with appropriate 1's compliment carries).
     * Must be zero if CSUM=0 in this descriptor.
     */
    uint_reg_t csum_seed  : 16;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t csum_seed  : 16;
    uint_reg_t csum_start : 8;
    uint_reg_t csum_dest  : 8;
    uint_reg_t r2         : 2;
    uint_reg_t xfer_size  : 14;
    uint_reg_t r1         : 4;
    uint_reg_t bound      : 1;
    uint_reg_t notif      : 1;
    uint_reg_t ns         : 1;
    uint_reg_t csum       : 1;
    uint_reg_t r0         : 1;
    uint_reg_t efifo_sel  : 6;
    uint_reg_t gen        : 1;
#endif

    /* Word 1 */

#ifndef __BIG_ENDIAN__
    /** Virtual address.  Must be sign extended by consumer. */
    int_reg_t va           : 42;
    /** Reserved. */
    uint_reg_t __reserved_0 : 6;
    /** Index of the buffer stack to which this buffer belongs. */
    uint_reg_t stack_idx    : 5;
    /** Reserved. */
    uint_reg_t __reserved_1 : 3;
    /**
     * Instance ID.  For devices that support automatic buffer return between
     * mPIPE instances, this field indicates the buffer owner.  If the INST
     * field does not match the mPIPE's instance number when a packet is
     * egressed, buffers with HWB set will be returned to the other mPIPE
     * instance.  Note that not all devices support multi-mPIPE buffer
     * return.  The MPIPE_EDMA_INFO.REMOTE_BUFF_RTN_SUPPORT bit indicates
     * whether the INST field in the buffer descriptor is populated by iDMA
     * hardware.
     */
    uint_reg_t inst         : 2;
    /**
     * Always set to one by hardware in iDMA packet descriptors.  For eDMA,
     * indicates whether the buffer will be released to the buffer stack
     * manager.  When 0, software is responsible for releasing the buffer.
     */
    uint_reg_t hwb          : 1;
    /**
     * Encoded size of buffer.  Set by the ingress hardware for iDMA packet
     * descriptors.  For eDMA descriptors, indicates the buffer size if .c
     * indicates a chained packet.  If an eDMA descriptor is not chained and
     * the .hwb bit is not set, this field is ignored and the size is
     * specified by the .xfer_size field.
     * 0 = 128 bytes
     * 1 = 256 bytes
     * 2 = 512 bytes
     * 3 = 1024 bytes
     * 4 = 1664 bytes
     * 5 = 4096 bytes
     * 6 = 10368 bytes
     * 7 = 16384 bytes
     */
    uint_reg_t size         : 3;
    /**
     * Chaining configuration for the buffer.  Indicates that an ingress
     * packet or egress command is chained across multiple buffers, with each
     * buffer's size indicated by the .size field.
     */
    uint_reg_t c            : 2;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t c            : 2;
    uint_reg_t size         : 3;
    uint_reg_t hwb          : 1;
    uint_reg_t inst         : 2;
    uint_reg_t __reserved_1 : 3;
    uint_reg_t stack_idx    : 5;
    uint_reg_t __reserved_0 : 6;
    int_reg_t va           : 42;
#endif

  };

  /** Word access */
  uint_reg_t words[2];
} MPIPE_EDMA_DESC_t;

/**
 * MPIPE Packet Descriptor.
 * The packet descriptor is filled by the mPIPE's classification,
 * load-balancing, and buffer management services.  Some fields are consumed
 * by mPIPE hardware, and others are consumed by Tile software.
 */

__extension__
typedef union
{
  struct
  {
    /* Word 0 */

#ifndef __BIG_ENDIAN__
    /**
     * Notification ring into which this packet descriptor is written.
     * Typically written by load balancer, but can be overridden by
     * classification program if NR is asserted.
     */
    uint_reg_t notif_ring   : 8;
    /** Source channel for this packet.  Written by mPIPE DMA hardware. */
    uint_reg_t channel      : 5;
    /** Reserved. */
    uint_reg_t __reserved_0 : 1;
    /**
     * MAC Error.
     * Generated by the MAC interface.  Asserted if there was an overrun of
     * the MAC's receive FIFO.  This condition generally only occurs if the
     * mPIPE clock is running too slowly.
     */
    uint_reg_t me           : 1;
    /**
     * Truncation Error.
     * Written by the iDMA hardware.  Asserted if packet was truncated due to
     * insufficient space in iPkt buffer
     */
    uint_reg_t tr           : 1;
    /**
     * Written by the iDMA hardware.  Indicates the number of bytes written
     * to Tile memory.  In general, this is the actual size of the packet as
     * received from the MAC.  But if the packet is truncated due to running
     * out of buffers or due to the iPkt buffer filling up, then the L2_SIZE
     * will be reduced to reflect the actual number of valid bytes written to
     * Tile memory.
     */
    uint_reg_t l2_size      : 14;
    /**
     * CRC Error.
     * Generated by the MAC.  Asserted if MAC indicated an L2 CRC error or
     * other L2 error (bad length etc.) on the packet.
     */
    uint_reg_t ce           : 1;
    /**
     * Cut Through.
     * Written by the iDMA hardware.  Asserted if packet was not completely
     * received before being sent to classifier.  L2_Size will indicate
     * number of bytes received so far.
     */
    uint_reg_t ct           : 1;
    /**
     * Written by the classification program.  Used by the load balancer to
     * select the ring into which this packet descriptor is written.
     */
    uint_reg_t bucket_id    : 13;
    /** Reserved. */
    uint_reg_t __reserved_1 : 3;
    /**
     * Checksum.
     * Written by classification program.  When 1, the checksum engine will
     * perform checksum based on the CSUM_SEED, CSUM_START, and CSUM_BYTES
     * fields.  The result will be placed in CSUM_VAL.
     */
    uint_reg_t cs           : 1;
    /**
     * Notification Ring Select.
     * Written by the classification program.  When 1, the NotifRingIDX is
     * set by classification program rather than being set by load balancer.
     */
    uint_reg_t nr           : 1;
    /**
     * Written by classification program.  Indicates whether packet and
     * descriptor should both be dropped, both be delivered, or only the
     * descriptor should be delivered.
     */
    uint_reg_t dest         : 2;
    /**
     * General Purpose Sequence Number Enable.
     * Written by the classification program.  When 1, the GP_SQN_SEL field
     * contains the sequence number selector and the GP_SQN field will be
     * replaced with the associated sequence number.  When clear, the GP_SQN
     * field is left intact and be used as "Custom" bytes.
     */
    uint_reg_t sq           : 1;
    /**
     * TimeStamp Enable.
     * Enable TimeStamp insertion.  When clear, timestamp field may be filled
     * with custom data by classifier.  When set, hardware inserts the
     * timestamp when the start of packet is received from the MAC.
     */
    uint_reg_t ts           : 1;
    /**
     * Packet Sequence Number Enable.
     * Enable PacketSQN insertion.  When clear, PacketSQN field may be filled
     * with custom data by classifier.  When set, hardware inserts the packet
     * sequence number when the packet descriptor is written to a
     * notification ring.
     */
    uint_reg_t ps           : 1;
    /**
     * Buffer Error.
     * Written by the iDMA hardware.  Asserted if iDMA ran out of buffers
     * while writing the packet. Software must still return any buffer
     * descriptors whose C field indicates a valid descriptor was consumed.
     */
    uint_reg_t be           : 1;
    /**
     * Written by  the classification program.  The associated counter is
     * incremented when the packet is sent.
     */
    uint_reg_t ctr0         : 5;
    /** Reserved. */
    uint_reg_t __reserved_2 : 3;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved_2 : 3;
    uint_reg_t ctr0         : 5;
    uint_reg_t be           : 1;
    uint_reg_t ps           : 1;
    uint_reg_t ts           : 1;
    uint_reg_t sq           : 1;
    uint_reg_t dest         : 2;
    uint_reg_t nr           : 1;
    uint_reg_t cs           : 1;
    uint_reg_t __reserved_1 : 3;
    uint_reg_t bucket_id    : 13;
    uint_reg_t ct           : 1;
    uint_reg_t ce           : 1;
    uint_reg_t l2_size      : 14;
    uint_reg_t tr           : 1;
    uint_reg_t me           : 1;
    uint_reg_t __reserved_0 : 1;
    uint_reg_t channel      : 5;
    uint_reg_t notif_ring   : 8;
#endif

    /* Word 1 */

#ifndef __BIG_ENDIAN__
    /**
     * Written by  the classification program.  The associated counter is
     * incremented when the packet is sent.
     */
    uint_reg_t ctr1          : 5;
    /** Reserved. */
    uint_reg_t __reserved_3  : 3;
    /**
     * Written by classification program.  Indicates the start byte for
     * checksum.  Relative to 1st byte received from MAC.
     */
    uint_reg_t csum_start    : 8;
    /**
     * Checksum seed written by classification program.  Overwritten with
     * resultant checksum if CS bit is asserted.  The endianness of the CSUM
     * value bits when viewed by Tile software match the packet byte order.
     * That is, bits[7:0] of the resulting checksum value correspond to
     * earlier (more significant) bytes in the packet.  To avoid classifier
     * software from having to byte swap the CSUM_SEED, the iDMA checksum
     * engine byte swaps the classifier's result before seeding the checksum
     * calculation.  Thus, the CSUM_START byte of packet data is added to
     * bits[15:8] of the CSUM_SEED field generated by the classifier.  This
     * byte swap will be visible to Tile software if the CS bit is clear.
     */
    uint_reg_t csum_seed_val : 16;
    /**
     * Written by  the classification program.  Not interpreted by mPIPE
     * hardware.
     */
    uint_reg_t custom0       : 32;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t custom0       : 32;
    uint_reg_t csum_seed_val : 16;
    uint_reg_t csum_start    : 8;
    uint_reg_t __reserved_3  : 3;
    uint_reg_t ctr1          : 5;
#endif

    /* Word 2 */

#ifndef __BIG_ENDIAN__
    /**
     * Written by  the classification program.  Not interpreted by mPIPE
     * hardware.
     */
    uint_reg_t custom1 : 64;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t custom1 : 64;
#endif

    /* Word 3 */

#ifndef __BIG_ENDIAN__
    /**
     * Written by  the classification program.  Not interpreted by mPIPE
     * hardware.
     */
    uint_reg_t custom2 : 64;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t custom2 : 64;
#endif

    /* Word 4 */

#ifndef __BIG_ENDIAN__
    /**
     * Written by  the classification program.  Not interpreted by mPIPE
     * hardware.
     */
    uint_reg_t custom3 : 64;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t custom3 : 64;
#endif

    /* Word 5 */

#ifndef __BIG_ENDIAN__
    /**
     * Sequence number applied when packet is distributed.   Classifier
     * selects which sequence number is to be applied by writing the 13-bit
     * SQN-selector into this field.  For devices that support EXT_SQN (as
     * indicated in IDMA_INFO.EXT_SQN_SUPPORT), the GP_SQN can be extended to
     * 32-bits via the IDMA_CTL.EXT_SQN register.  In this case the
     * PACKET_SQN will be reduced to 32 bits.
     */
    uint_reg_t gp_sqn     : 16;
    /**
     * Written by notification hardware.  The packet sequence number is
     * incremented for each packet that wasn't dropped.
     */
    uint_reg_t packet_sqn : 48;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t packet_sqn : 48;
    uint_reg_t gp_sqn     : 16;
#endif

    /* Word 6 */

#ifndef __BIG_ENDIAN__
    /**
     * Written by hardware when the start-of-packet is received by the mPIPE
     * from the MAC.  This is the nanoseconds part of the packet timestamp.
     */
    uint_reg_t time_stamp_ns  : 32;
    /**
     * Written by hardware when the start-of-packet is received by the mPIPE
     * from the MAC.  This is the seconds part of the packet timestamp.
     */
    uint_reg_t time_stamp_sec : 32;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t time_stamp_sec : 32;
    uint_reg_t time_stamp_ns  : 32;
#endif

    /* Word 7 */

#ifndef __BIG_ENDIAN__
    /** Virtual address.  Must be sign extended by consumer. */
    int_reg_t va           : 42;
    /** Reserved. */
    uint_reg_t __reserved_4 : 6;
    /** Index of the buffer stack to which this buffer belongs. */
    uint_reg_t stack_idx    : 5;
    /** Reserved. */
    uint_reg_t __reserved_5 : 3;
    /**
     * Instance ID.  For devices that support automatic buffer return between
     * mPIPE instances, this field indicates the buffer owner.  If the INST
     * field does not match the mPIPE's instance number when a packet is
     * egressed, buffers with HWB set will be returned to the other mPIPE
     * instance.  Note that not all devices support multi-mPIPE buffer
     * return.  The MPIPE_EDMA_INFO.REMOTE_BUFF_RTN_SUPPORT bit indicates
     * whether the INST field in the buffer descriptor is populated by iDMA
     * hardware.
     */
    uint_reg_t inst         : 2;
    /**
     * Always set to one by hardware in iDMA packet descriptors.  For eDMA,
     * indicates whether the buffer will be released to the buffer stack
     * manager.  When 0, software is responsible for releasing the buffer.
     */
    uint_reg_t hwb          : 1;
    /**
     * Encoded size of buffer.  Set by the ingress hardware for iDMA packet
     * descriptors.  For eDMA descriptors, indicates the buffer size if .c
     * indicates a chained packet.  If an eDMA descriptor is not chained and
     * the .hwb bit is not set, this field is ignored and the size is
     * specified by the .xfer_size field.
     * 0 = 128 bytes
     * 1 = 256 bytes
     * 2 = 512 bytes
     * 3 = 1024 bytes
     * 4 = 1664 bytes
     * 5 = 4096 bytes
     * 6 = 10368 bytes
     * 7 = 16384 bytes
     */
    uint_reg_t size         : 3;
    /**
     * Chaining configuration for the buffer.  Indicates that an ingress
     * packet or egress command is chained across multiple buffers, with each
     * buffer's size indicated by the .size field.
     */
    uint_reg_t c            : 2;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t c            : 2;
    uint_reg_t size         : 3;
    uint_reg_t hwb          : 1;
    uint_reg_t inst         : 2;
    uint_reg_t __reserved_5 : 3;
    uint_reg_t stack_idx    : 5;
    uint_reg_t __reserved_4 : 6;
    int_reg_t va           : 42;
#endif

  };

  /** Word access */
  uint_reg_t words[8];
} MPIPE_PDESC_t;
#endif /* !defined(__ASSEMBLER__) */

#endif /* !defined(__ARCH_MPIPE_SHM_H__) */
