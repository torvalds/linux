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

/* This file is machine-generated; DO NOT EDIT! */
#ifndef __GXIO_MPIPE_LINUX_RPC_H__
#define __GXIO_MPIPE_LINUX_RPC_H__

#include <hv/iorpc.h>

#include <hv/drv_mpipe_intf.h>
#include <asm/page.h>
#include <gxio/kiorpc.h>
#include <gxio/mpipe.h>
#include <linux/string.h>
#include <linux/module.h>
#include <asm/pgtable.h>

#define GXIO_MPIPE_OP_ALLOC_BUFFER_STACKS IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1200)
#define GXIO_MPIPE_OP_INIT_BUFFER_STACK_AUX IORPC_OPCODE(IORPC_FORMAT_KERNEL_MEM, 0x1201)

#define GXIO_MPIPE_OP_ALLOC_NOTIF_RINGS IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1203)
#define GXIO_MPIPE_OP_INIT_NOTIF_RING_AUX IORPC_OPCODE(IORPC_FORMAT_KERNEL_MEM, 0x1204)
#define GXIO_MPIPE_OP_REQUEST_NOTIF_RING_INTERRUPT IORPC_OPCODE(IORPC_FORMAT_KERNEL_INTERRUPT, 0x1205)
#define GXIO_MPIPE_OP_ENABLE_NOTIF_RING_INTERRUPT IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1206)
#define GXIO_MPIPE_OP_ALLOC_NOTIF_GROUPS IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1207)
#define GXIO_MPIPE_OP_INIT_NOTIF_GROUP IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1208)
#define GXIO_MPIPE_OP_ALLOC_BUCKETS    IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1209)
#define GXIO_MPIPE_OP_INIT_BUCKET      IORPC_OPCODE(IORPC_FORMAT_NONE, 0x120a)
#define GXIO_MPIPE_OP_ALLOC_EDMA_RINGS IORPC_OPCODE(IORPC_FORMAT_NONE, 0x120b)
#define GXIO_MPIPE_OP_INIT_EDMA_RING_AUX IORPC_OPCODE(IORPC_FORMAT_KERNEL_MEM, 0x120c)

#define GXIO_MPIPE_OP_COMMIT_RULES     IORPC_OPCODE(IORPC_FORMAT_NONE, 0x120f)
#define GXIO_MPIPE_OP_REGISTER_CLIENT_MEMORY IORPC_OPCODE(IORPC_FORMAT_NONE_NOUSER, 0x1210)
#define GXIO_MPIPE_OP_LINK_OPEN_AUX    IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1211)
#define GXIO_MPIPE_OP_LINK_CLOSE_AUX   IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1212)
#define GXIO_MPIPE_OP_LINK_SET_ATTR_AUX IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1213)

#define GXIO_MPIPE_OP_GET_TIMESTAMP_AUX IORPC_OPCODE(IORPC_FORMAT_NONE, 0x121e)
#define GXIO_MPIPE_OP_SET_TIMESTAMP_AUX IORPC_OPCODE(IORPC_FORMAT_NONE, 0x121f)
#define GXIO_MPIPE_OP_ADJUST_TIMESTAMP_AUX IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1220)
#define GXIO_MPIPE_OP_CONFIG_EDMA_RING_BLKS IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1221)
#define GXIO_MPIPE_OP_ADJUST_TIMESTAMP_FREQ IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1222)
#define GXIO_MPIPE_OP_ARM_POLLFD       IORPC_OPCODE(IORPC_FORMAT_KERNEL_POLLFD, 0x9000)
#define GXIO_MPIPE_OP_CLOSE_POLLFD     IORPC_OPCODE(IORPC_FORMAT_KERNEL_POLLFD, 0x9001)
#define GXIO_MPIPE_OP_GET_MMIO_BASE    IORPC_OPCODE(IORPC_FORMAT_NONE_NOUSER, 0x8000)
#define GXIO_MPIPE_OP_CHECK_MMIO_OFFSET IORPC_OPCODE(IORPC_FORMAT_NONE_NOUSER, 0x8001)

int gxio_mpipe_alloc_buffer_stacks(gxio_mpipe_context_t * context,
				   unsigned int count, unsigned int first,
				   unsigned int flags);

int gxio_mpipe_init_buffer_stack_aux(gxio_mpipe_context_t * context,
				     void *mem_va, size_t mem_size,
				     unsigned int mem_flags, unsigned int stack,
				     unsigned int buffer_size_enum);


int gxio_mpipe_alloc_notif_rings(gxio_mpipe_context_t * context,
				 unsigned int count, unsigned int first,
				 unsigned int flags);

int gxio_mpipe_init_notif_ring_aux(gxio_mpipe_context_t * context, void *mem_va,
				   size_t mem_size, unsigned int mem_flags,
				   unsigned int ring);

int gxio_mpipe_request_notif_ring_interrupt(gxio_mpipe_context_t * context,
					    int inter_x, int inter_y,
					    int inter_ipi, int inter_event,
					    unsigned int ring);

int gxio_mpipe_enable_notif_ring_interrupt(gxio_mpipe_context_t * context,
					   unsigned int ring);

int gxio_mpipe_alloc_notif_groups(gxio_mpipe_context_t * context,
				  unsigned int count, unsigned int first,
				  unsigned int flags);

int gxio_mpipe_init_notif_group(gxio_mpipe_context_t * context,
				unsigned int group,
				gxio_mpipe_notif_group_bits_t bits);

int gxio_mpipe_alloc_buckets(gxio_mpipe_context_t * context, unsigned int count,
			     unsigned int first, unsigned int flags);

int gxio_mpipe_init_bucket(gxio_mpipe_context_t * context, unsigned int bucket,
			   MPIPE_LBL_INIT_DAT_BSTS_TBL_t bucket_info);

int gxio_mpipe_alloc_edma_rings(gxio_mpipe_context_t * context,
				unsigned int count, unsigned int first,
				unsigned int flags);

int gxio_mpipe_init_edma_ring_aux(gxio_mpipe_context_t * context, void *mem_va,
				  size_t mem_size, unsigned int mem_flags,
				  unsigned int ring, unsigned int channel);


int gxio_mpipe_commit_rules(gxio_mpipe_context_t * context, const void *blob,
			    size_t blob_size);

int gxio_mpipe_register_client_memory(gxio_mpipe_context_t * context,
				      unsigned int iotlb, HV_PTE pte,
				      unsigned int flags);

int gxio_mpipe_link_open_aux(gxio_mpipe_context_t * context,
			     _gxio_mpipe_link_name_t name, unsigned int flags);

int gxio_mpipe_link_close_aux(gxio_mpipe_context_t * context, int mac);

int gxio_mpipe_link_set_attr_aux(gxio_mpipe_context_t * context, int mac,
				 uint32_t attr, int64_t val);

int gxio_mpipe_get_timestamp_aux(gxio_mpipe_context_t * context, uint64_t * sec,
				 uint64_t * nsec, uint64_t * cycles);

int gxio_mpipe_set_timestamp_aux(gxio_mpipe_context_t * context, uint64_t sec,
				 uint64_t nsec, uint64_t cycles);

int gxio_mpipe_adjust_timestamp_aux(gxio_mpipe_context_t * context,
				    int64_t nsec);

int gxio_mpipe_adjust_timestamp_freq(gxio_mpipe_context_t * context,
				     int32_t ppb);

int gxio_mpipe_arm_pollfd(gxio_mpipe_context_t * context, int pollfd_cookie);

int gxio_mpipe_close_pollfd(gxio_mpipe_context_t * context, int pollfd_cookie);

int gxio_mpipe_get_mmio_base(gxio_mpipe_context_t * context, HV_PTE *base);

int gxio_mpipe_check_mmio_offset(gxio_mpipe_context_t * context,
				 unsigned long offset, unsigned long size);

#endif /* !__GXIO_MPIPE_LINUX_RPC_H__ */
