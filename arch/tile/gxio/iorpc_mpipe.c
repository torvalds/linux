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
#include "gxio/iorpc_mpipe.h"

struct alloc_buffer_stacks_param {
	unsigned int count;
	unsigned int first;
	unsigned int flags;
};

int gxio_mpipe_alloc_buffer_stacks(gxio_mpipe_context_t * context,
				   unsigned int count, unsigned int first,
				   unsigned int flags)
{
	struct alloc_buffer_stacks_param temp;
	struct alloc_buffer_stacks_param *params = &temp;

	params->count = count;
	params->first = first;
	params->flags = flags;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params),
			     GXIO_MPIPE_OP_ALLOC_BUFFER_STACKS);
}

EXPORT_SYMBOL(gxio_mpipe_alloc_buffer_stacks);

struct init_buffer_stack_aux_param {
	union iorpc_mem_buffer buffer;
	unsigned int stack;
	unsigned int buffer_size_enum;
};

int gxio_mpipe_init_buffer_stack_aux(gxio_mpipe_context_t * context,
				     void *mem_va, size_t mem_size,
				     unsigned int mem_flags, unsigned int stack,
				     unsigned int buffer_size_enum)
{
	int __result;
	unsigned long long __cpa;
	pte_t __pte;
	struct init_buffer_stack_aux_param temp;
	struct init_buffer_stack_aux_param *params = &temp;

	__result = va_to_cpa_and_pte(mem_va, &__cpa, &__pte);
	if (__result != 0)
		return __result;
	params->buffer.kernel.cpa = __cpa;
	params->buffer.kernel.size = mem_size;
	params->buffer.kernel.pte = __pte;
	params->buffer.kernel.flags = mem_flags;
	params->stack = stack;
	params->buffer_size_enum = buffer_size_enum;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params),
			     GXIO_MPIPE_OP_INIT_BUFFER_STACK_AUX);
}

EXPORT_SYMBOL(gxio_mpipe_init_buffer_stack_aux);


struct alloc_notif_rings_param {
	unsigned int count;
	unsigned int first;
	unsigned int flags;
};

int gxio_mpipe_alloc_notif_rings(gxio_mpipe_context_t * context,
				 unsigned int count, unsigned int first,
				 unsigned int flags)
{
	struct alloc_notif_rings_param temp;
	struct alloc_notif_rings_param *params = &temp;

	params->count = count;
	params->first = first;
	params->flags = flags;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_MPIPE_OP_ALLOC_NOTIF_RINGS);
}

EXPORT_SYMBOL(gxio_mpipe_alloc_notif_rings);

struct init_notif_ring_aux_param {
	union iorpc_mem_buffer buffer;
	unsigned int ring;
};

int gxio_mpipe_init_notif_ring_aux(gxio_mpipe_context_t * context, void *mem_va,
				   size_t mem_size, unsigned int mem_flags,
				   unsigned int ring)
{
	int __result;
	unsigned long long __cpa;
	pte_t __pte;
	struct init_notif_ring_aux_param temp;
	struct init_notif_ring_aux_param *params = &temp;

	__result = va_to_cpa_and_pte(mem_va, &__cpa, &__pte);
	if (__result != 0)
		return __result;
	params->buffer.kernel.cpa = __cpa;
	params->buffer.kernel.size = mem_size;
	params->buffer.kernel.pte = __pte;
	params->buffer.kernel.flags = mem_flags;
	params->ring = ring;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params),
			     GXIO_MPIPE_OP_INIT_NOTIF_RING_AUX);
}

EXPORT_SYMBOL(gxio_mpipe_init_notif_ring_aux);

struct request_notif_ring_interrupt_param {
	union iorpc_interrupt interrupt;
	unsigned int ring;
};

int gxio_mpipe_request_notif_ring_interrupt(gxio_mpipe_context_t * context,
					    int inter_x, int inter_y,
					    int inter_ipi, int inter_event,
					    unsigned int ring)
{
	struct request_notif_ring_interrupt_param temp;
	struct request_notif_ring_interrupt_param *params = &temp;

	params->interrupt.kernel.x = inter_x;
	params->interrupt.kernel.y = inter_y;
	params->interrupt.kernel.ipi = inter_ipi;
	params->interrupt.kernel.event = inter_event;
	params->ring = ring;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params),
			     GXIO_MPIPE_OP_REQUEST_NOTIF_RING_INTERRUPT);
}

EXPORT_SYMBOL(gxio_mpipe_request_notif_ring_interrupt);

struct enable_notif_ring_interrupt_param {
	unsigned int ring;
};

int gxio_mpipe_enable_notif_ring_interrupt(gxio_mpipe_context_t * context,
					   unsigned int ring)
{
	struct enable_notif_ring_interrupt_param temp;
	struct enable_notif_ring_interrupt_param *params = &temp;

	params->ring = ring;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params),
			     GXIO_MPIPE_OP_ENABLE_NOTIF_RING_INTERRUPT);
}

EXPORT_SYMBOL(gxio_mpipe_enable_notif_ring_interrupt);

struct alloc_notif_groups_param {
	unsigned int count;
	unsigned int first;
	unsigned int flags;
};

int gxio_mpipe_alloc_notif_groups(gxio_mpipe_context_t * context,
				  unsigned int count, unsigned int first,
				  unsigned int flags)
{
	struct alloc_notif_groups_param temp;
	struct alloc_notif_groups_param *params = &temp;

	params->count = count;
	params->first = first;
	params->flags = flags;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_MPIPE_OP_ALLOC_NOTIF_GROUPS);
}

EXPORT_SYMBOL(gxio_mpipe_alloc_notif_groups);

struct init_notif_group_param {
	unsigned int group;
	gxio_mpipe_notif_group_bits_t bits;
};

int gxio_mpipe_init_notif_group(gxio_mpipe_context_t * context,
				unsigned int group,
				gxio_mpipe_notif_group_bits_t bits)
{
	struct init_notif_group_param temp;
	struct init_notif_group_param *params = &temp;

	params->group = group;
	params->bits = bits;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_MPIPE_OP_INIT_NOTIF_GROUP);
}

EXPORT_SYMBOL(gxio_mpipe_init_notif_group);

struct alloc_buckets_param {
	unsigned int count;
	unsigned int first;
	unsigned int flags;
};

int gxio_mpipe_alloc_buckets(gxio_mpipe_context_t * context, unsigned int count,
			     unsigned int first, unsigned int flags)
{
	struct alloc_buckets_param temp;
	struct alloc_buckets_param *params = &temp;

	params->count = count;
	params->first = first;
	params->flags = flags;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_MPIPE_OP_ALLOC_BUCKETS);
}

EXPORT_SYMBOL(gxio_mpipe_alloc_buckets);

struct init_bucket_param {
	unsigned int bucket;
	MPIPE_LBL_INIT_DAT_BSTS_TBL_t bucket_info;
};

int gxio_mpipe_init_bucket(gxio_mpipe_context_t * context, unsigned int bucket,
			   MPIPE_LBL_INIT_DAT_BSTS_TBL_t bucket_info)
{
	struct init_bucket_param temp;
	struct init_bucket_param *params = &temp;

	params->bucket = bucket;
	params->bucket_info = bucket_info;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_MPIPE_OP_INIT_BUCKET);
}

EXPORT_SYMBOL(gxio_mpipe_init_bucket);

struct alloc_edma_rings_param {
	unsigned int count;
	unsigned int first;
	unsigned int flags;
};

int gxio_mpipe_alloc_edma_rings(gxio_mpipe_context_t * context,
				unsigned int count, unsigned int first,
				unsigned int flags)
{
	struct alloc_edma_rings_param temp;
	struct alloc_edma_rings_param *params = &temp;

	params->count = count;
	params->first = first;
	params->flags = flags;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_MPIPE_OP_ALLOC_EDMA_RINGS);
}

EXPORT_SYMBOL(gxio_mpipe_alloc_edma_rings);

struct init_edma_ring_aux_param {
	union iorpc_mem_buffer buffer;
	unsigned int ring;
	unsigned int channel;
};

int gxio_mpipe_init_edma_ring_aux(gxio_mpipe_context_t * context, void *mem_va,
				  size_t mem_size, unsigned int mem_flags,
				  unsigned int ring, unsigned int channel)
{
	int __result;
	unsigned long long __cpa;
	pte_t __pte;
	struct init_edma_ring_aux_param temp;
	struct init_edma_ring_aux_param *params = &temp;

	__result = va_to_cpa_and_pte(mem_va, &__cpa, &__pte);
	if (__result != 0)
		return __result;
	params->buffer.kernel.cpa = __cpa;
	params->buffer.kernel.size = mem_size;
	params->buffer.kernel.pte = __pte;
	params->buffer.kernel.flags = mem_flags;
	params->ring = ring;
	params->channel = channel;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_MPIPE_OP_INIT_EDMA_RING_AUX);
}

EXPORT_SYMBOL(gxio_mpipe_init_edma_ring_aux);


int gxio_mpipe_commit_rules(gxio_mpipe_context_t * context, const void *blob,
			    size_t blob_size)
{
	const void *params = blob;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params, blob_size,
			     GXIO_MPIPE_OP_COMMIT_RULES);
}

EXPORT_SYMBOL(gxio_mpipe_commit_rules);

struct register_client_memory_param {
	unsigned int iotlb;
	HV_PTE pte;
	unsigned int flags;
};

int gxio_mpipe_register_client_memory(gxio_mpipe_context_t * context,
				      unsigned int iotlb, HV_PTE pte,
				      unsigned int flags)
{
	struct register_client_memory_param temp;
	struct register_client_memory_param *params = &temp;

	params->iotlb = iotlb;
	params->pte = pte;
	params->flags = flags;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params),
			     GXIO_MPIPE_OP_REGISTER_CLIENT_MEMORY);
}

EXPORT_SYMBOL(gxio_mpipe_register_client_memory);

struct link_open_aux_param {
	_gxio_mpipe_link_name_t name;
	unsigned int flags;
};

int gxio_mpipe_link_open_aux(gxio_mpipe_context_t * context,
			     _gxio_mpipe_link_name_t name, unsigned int flags)
{
	struct link_open_aux_param temp;
	struct link_open_aux_param *params = &temp;

	params->name = name;
	params->flags = flags;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_MPIPE_OP_LINK_OPEN_AUX);
}

EXPORT_SYMBOL(gxio_mpipe_link_open_aux);

struct link_close_aux_param {
	int mac;
};

int gxio_mpipe_link_close_aux(gxio_mpipe_context_t * context, int mac)
{
	struct link_close_aux_param temp;
	struct link_close_aux_param *params = &temp;

	params->mac = mac;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_MPIPE_OP_LINK_CLOSE_AUX);
}

EXPORT_SYMBOL(gxio_mpipe_link_close_aux);


struct get_timestamp_aux_param {
	uint64_t sec;
	uint64_t nsec;
	uint64_t cycles;
};

int gxio_mpipe_get_timestamp_aux(gxio_mpipe_context_t * context, uint64_t * sec,
				 uint64_t * nsec, uint64_t * cycles)
{
	int __result;
	struct get_timestamp_aux_param temp;
	struct get_timestamp_aux_param *params = &temp;

	__result =
	    hv_dev_pread(context->fd, 0, (HV_VirtAddr) params, sizeof(*params),
			 GXIO_MPIPE_OP_GET_TIMESTAMP_AUX);
	*sec = params->sec;
	*nsec = params->nsec;
	*cycles = params->cycles;

	return __result;
}

EXPORT_SYMBOL(gxio_mpipe_get_timestamp_aux);

struct set_timestamp_aux_param {
	uint64_t sec;
	uint64_t nsec;
	uint64_t cycles;
};

int gxio_mpipe_set_timestamp_aux(gxio_mpipe_context_t * context, uint64_t sec,
				 uint64_t nsec, uint64_t cycles)
{
	struct set_timestamp_aux_param temp;
	struct set_timestamp_aux_param *params = &temp;

	params->sec = sec;
	params->nsec = nsec;
	params->cycles = cycles;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_MPIPE_OP_SET_TIMESTAMP_AUX);
}

EXPORT_SYMBOL(gxio_mpipe_set_timestamp_aux);

struct adjust_timestamp_aux_param {
	int64_t nsec;
};

int gxio_mpipe_adjust_timestamp_aux(gxio_mpipe_context_t * context,
				    int64_t nsec)
{
	struct adjust_timestamp_aux_param temp;
	struct adjust_timestamp_aux_param *params = &temp;

	params->nsec = nsec;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params),
			     GXIO_MPIPE_OP_ADJUST_TIMESTAMP_AUX);
}

EXPORT_SYMBOL(gxio_mpipe_adjust_timestamp_aux);

struct arm_pollfd_param {
	union iorpc_pollfd pollfd;
};

int gxio_mpipe_arm_pollfd(gxio_mpipe_context_t * context, int pollfd_cookie)
{
	struct arm_pollfd_param temp;
	struct arm_pollfd_param *params = &temp;

	params->pollfd.kernel.cookie = pollfd_cookie;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_MPIPE_OP_ARM_POLLFD);
}

EXPORT_SYMBOL(gxio_mpipe_arm_pollfd);

struct close_pollfd_param {
	union iorpc_pollfd pollfd;
};

int gxio_mpipe_close_pollfd(gxio_mpipe_context_t * context, int pollfd_cookie)
{
	struct close_pollfd_param temp;
	struct close_pollfd_param *params = &temp;

	params->pollfd.kernel.cookie = pollfd_cookie;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_MPIPE_OP_CLOSE_POLLFD);
}

EXPORT_SYMBOL(gxio_mpipe_close_pollfd);

struct get_mmio_base_param {
	HV_PTE base;
};

int gxio_mpipe_get_mmio_base(gxio_mpipe_context_t * context, HV_PTE *base)
{
	int __result;
	struct get_mmio_base_param temp;
	struct get_mmio_base_param *params = &temp;

	__result =
	    hv_dev_pread(context->fd, 0, (HV_VirtAddr) params, sizeof(*params),
			 GXIO_MPIPE_OP_GET_MMIO_BASE);
	*base = params->base;

	return __result;
}

EXPORT_SYMBOL(gxio_mpipe_get_mmio_base);

struct check_mmio_offset_param {
	unsigned long offset;
	unsigned long size;
};

int gxio_mpipe_check_mmio_offset(gxio_mpipe_context_t * context,
				 unsigned long offset, unsigned long size)
{
	struct check_mmio_offset_param temp;
	struct check_mmio_offset_param *params = &temp;

	params->offset = offset;
	params->size = size;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_MPIPE_OP_CHECK_MMIO_OFFSET);
}

EXPORT_SYMBOL(gxio_mpipe_check_mmio_offset);
