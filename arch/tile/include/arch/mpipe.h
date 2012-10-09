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

#ifndef __ARCH_MPIPE_H__
#define __ARCH_MPIPE_H__

#include <arch/abi.h>
#include <arch/mpipe_def.h>

#ifndef __ASSEMBLER__

/*
 * MMIO Ingress DMA Release Region Address.
 * This is a description of the physical addresses used to manipulate ingress
 * credit counters.  Accesses to this address space should use an address of
 * this form and a value like that specified in IDMA_RELEASE_REGION_VAL.
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /* Reserved. */
    uint_reg_t __reserved_0  : 3;
    /* NotifRing to be released */
    uint_reg_t ring          : 8;
    /* Bucket to be released */
    uint_reg_t bucket        : 13;
    /* Enable NotifRing release */
    uint_reg_t ring_enable   : 1;
    /* Enable Bucket release */
    uint_reg_t bucket_enable : 1;
    /*
     * This field of the address selects the region (address space) to be
     * accessed.  For the iDMA release region, this field must be 4.
     */
    uint_reg_t region        : 3;
    /* Reserved. */
    uint_reg_t __reserved_1  : 6;
    /* This field of the address indexes the 32 entry service domain table. */
    uint_reg_t svc_dom       : 5;
    /* Reserved. */
    uint_reg_t __reserved_2  : 24;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved_2  : 24;
    uint_reg_t svc_dom       : 5;
    uint_reg_t __reserved_1  : 6;
    uint_reg_t region        : 3;
    uint_reg_t bucket_enable : 1;
    uint_reg_t ring_enable   : 1;
    uint_reg_t bucket        : 13;
    uint_reg_t ring          : 8;
    uint_reg_t __reserved_0  : 3;
#endif
  };

  uint_reg_t word;
} MPIPE_IDMA_RELEASE_REGION_ADDR_t;

/*
 * MMIO Ingress DMA Release Region Value - Release NotifRing and/or Bucket.
 * Provides release of the associated NotifRing.  The address of the MMIO
 * operation is described in IDMA_RELEASE_REGION_ADDR.
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /*
     * Number of packets being released.  The load balancer's count of
     * inflight packets will be decremented by this amount for the associated
     * Bucket and/or NotifRing
     */
    uint_reg_t count      : 16;
    /* Reserved. */
    uint_reg_t __reserved : 48;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved : 48;
    uint_reg_t count      : 16;
#endif
  };

  uint_reg_t word;
} MPIPE_IDMA_RELEASE_REGION_VAL_t;

/*
 * MMIO Buffer Stack Manager Region Address.
 * This MMIO region is used for posting or fetching buffers to/from the
 * buffer stack manager.  On an MMIO load, this pops a buffer descriptor from
 * the top of stack if one is available.  On an MMIO store, this pushes a
 * buffer to the stack.  The value read or written is described in
 * BSM_REGION_VAL.
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /* Reserved. */
    uint_reg_t __reserved_0 : 3;
    /* BufferStack being accessed. */
    uint_reg_t stack        : 5;
    /* Reserved. */
    uint_reg_t __reserved_1 : 18;
    /*
     * This field of the address selects the region (address space) to be
     * accessed.  For the buffer stack manager region, this field must be 6.
     */
    uint_reg_t region       : 3;
    /* Reserved. */
    uint_reg_t __reserved_2 : 6;
    /* This field of the address indexes the 32 entry service domain table. */
    uint_reg_t svc_dom      : 5;
    /* Reserved. */
    uint_reg_t __reserved_3 : 24;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved_3 : 24;
    uint_reg_t svc_dom      : 5;
    uint_reg_t __reserved_2 : 6;
    uint_reg_t region       : 3;
    uint_reg_t __reserved_1 : 18;
    uint_reg_t stack        : 5;
    uint_reg_t __reserved_0 : 3;
#endif
  };

  uint_reg_t word;
} MPIPE_BSM_REGION_ADDR_t;

/*
 * MMIO Buffer Stack Manager Region Value.
 * This MMIO region is used for posting or fetching buffers to/from the
 * buffer stack manager.  On an MMIO load, this pops a buffer descriptor from
 * the top of stack if one is available. On an MMIO store, this pushes a
 * buffer to the stack.  The address of the MMIO operation is described in
 * BSM_REGION_ADDR.
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /* Reserved. */
    uint_reg_t __reserved_0 : 7;
    /*
     * Base virtual address of the buffer.  Must be sign extended by consumer.
     */
    int_reg_t va           : 35;
    /* Reserved. */
    uint_reg_t __reserved_1 : 6;
    /*
     * Index of the buffer stack to which this buffer belongs.  Ignored on
     * writes since the offset bits specify the stack being accessed.
     */
    uint_reg_t stack_idx    : 5;
    /* Reserved. */
    uint_reg_t __reserved_2 : 5;
    /*
     * Reads as one to indicate that this is a hardware managed buffer.
     * Ignored on writes since all buffers on a given stack are the same size.
     */
    uint_reg_t hwb          : 1;
    /*
     * Encoded size of buffer (ignored on writes):
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
    /*
     * Valid indication for the buffer.  Ignored on writes.
     * 0 : Valid buffer descriptor popped from stack.
     * 3 : Could not pop a buffer from the stack.  Either the stack is empty,
     * or the hardware's prefetch buffer is empty for this stack.
     */
    uint_reg_t c            : 2;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t c            : 2;
    uint_reg_t size         : 3;
    uint_reg_t hwb          : 1;
    uint_reg_t __reserved_2 : 5;
    uint_reg_t stack_idx    : 5;
    uint_reg_t __reserved_1 : 6;
    int_reg_t va           : 35;
    uint_reg_t __reserved_0 : 7;
#endif
  };

  uint_reg_t word;
} MPIPE_BSM_REGION_VAL_t;

/*
 * MMIO Egress DMA Post Region Address.
 * Used to post descriptor locations to the eDMA descriptor engine.  The
 * value to be written is described in EDMA_POST_REGION_VAL
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /* Reserved. */
    uint_reg_t __reserved_0 : 3;
    /* eDMA ring being accessed */
    uint_reg_t ring         : 5;
    /* Reserved. */
    uint_reg_t __reserved_1 : 18;
    /*
     * This field of the address selects the region (address space) to be
     * accessed.  For the egress DMA post region, this field must be 5.
     */
    uint_reg_t region       : 3;
    /* Reserved. */
    uint_reg_t __reserved_2 : 6;
    /* This field of the address indexes the 32 entry service domain table. */
    uint_reg_t svc_dom      : 5;
    /* Reserved. */
    uint_reg_t __reserved_3 : 24;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved_3 : 24;
    uint_reg_t svc_dom      : 5;
    uint_reg_t __reserved_2 : 6;
    uint_reg_t region       : 3;
    uint_reg_t __reserved_1 : 18;
    uint_reg_t ring         : 5;
    uint_reg_t __reserved_0 : 3;
#endif
  };

  uint_reg_t word;
} MPIPE_EDMA_POST_REGION_ADDR_t;

/*
 * MMIO Egress DMA Post Region Value.
 * Used to post descriptor locations to the eDMA descriptor engine.  The
 * address is described in EDMA_POST_REGION_ADDR.
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /*
     * For writes, this specifies the current ring tail pointer prior to any
     * post.  For example, to post 1 or more descriptors starting at location
     * 23, this would contain 23 (not 24).  On writes, this index must be
     * masked based on the ring size.  The new tail pointer after this post
     * is COUNT+RING_IDX (masked by the ring size).
     *
     * For reads, this provides the hardware descriptor fetcher's head
     * pointer.  The descriptors prior to the head pointer, however, may not
     * yet have been processed so this indicator is only used to determine
     * how full the ring is and if software may post more descriptors.
     */
    uint_reg_t ring_idx   : 16;
    /*
     * For writes, this specifies number of contiguous descriptors that are
     * being posted.  Software may post up to RingSize descriptors with a
     * single MMIO store.  A zero in this field on a write will "wake up" an
     * eDMA ring and cause it fetch descriptors regardless of the hardware's
     * current view of the state of the tail pointer.
     *
     * For reads, this field provides a rolling count of the number of
     * descriptors that have been completely processed.  This may be used by
     * software to determine when buffers associated with a descriptor may be
     * returned or reused.  When the ring's flush bit is cleared by software
     * (after having been set by HW or SW), the COUNT will be cleared.
     */
    uint_reg_t count      : 16;
    /*
     * For writes, this specifies the generation number of the tail being
     * posted. Note that if tail+cnt wraps to the beginning of the ring, the
     * eDMA hardware assumes that the descriptors posted at the beginning of
     * the ring are also valid so it is okay to post around the wrap point.
     *
     * For reads, this is the current generation number.  Valid descriptors
     * will have the inverse of this generation number.
     */
    uint_reg_t gen        : 1;
    /* Reserved. */
    uint_reg_t __reserved : 31;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved : 31;
    uint_reg_t gen        : 1;
    uint_reg_t count      : 16;
    uint_reg_t ring_idx   : 16;
#endif
  };

  uint_reg_t word;
} MPIPE_EDMA_POST_REGION_VAL_t;

/*
 * Load Balancer Bucket Status Data.
 * Read/Write data for load balancer Bucket-Status Table. 4160 entries
 * indexed by LBL_INIT_CTL.IDX when LBL_INIT_CTL.STRUCT_SEL is BSTS_TBL
 */

__extension__
typedef union
{
  struct
  {
#ifndef __BIG_ENDIAN__
    /* NotifRing currently assigned to this bucket. */
    uint_reg_t notifring  : 8;
    /* Current reference count. */
    uint_reg_t count      : 16;
    /* Group associated with this bucket. */
    uint_reg_t group      : 5;
    /* Mode select for this bucket. */
    uint_reg_t mode       : 3;
    /* Reserved. */
    uint_reg_t __reserved : 32;
#else   /* __BIG_ENDIAN__ */
    uint_reg_t __reserved : 32;
    uint_reg_t mode       : 3;
    uint_reg_t group      : 5;
    uint_reg_t count      : 16;
    uint_reg_t notifring  : 8;
#endif
  };

  uint_reg_t word;
} MPIPE_LBL_INIT_DAT_BSTS_TBL_t;
#endif /* !defined(__ASSEMBLER__) */

#endif /* !defined(__ARCH_MPIPE_H__) */
