/****************************************************************************
*
*    Copyright (c) 2005 - 2010 by Vivante Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Vivante Corporation.
*
*****************************************************************************
*
*    Auto-generated file on 12/8/2010. Do not edit!!!
*
*****************************************************************************/





/*
 * Os.h
 *
 *  Created on: Feb 2, 2010
 *      Author: Tarang Vaish
 */

#ifndef __gc_hal_kernel_os_h_
#define __gc_hal_kernel_os_h_

typedef struct
{
	io_msg_t iomsg;
	gcsHAL_INTERFACE iface;
} gcsDRIVER_ARGS;

struct _gckPAGE_USAGE
{
	gctUINT16 pageCount;
};

struct _gckSHM_POOL
{
	gctHANDLE Handle;
	gctINT32 fd;
	gctUINT32 pid;
	gctUINT32 freePage;
	gctUINT32 pageCount;
	gctUINT32 pageSize;
	pthread_mutex_t mutex;
	gctUINT32 Logical;
	gctUINT32 Physical;
	struct _gckPAGE_USAGE* pageUsage;
	struct _gckSHM_POOL* nextPool;
};

typedef struct _gckSHM_POOL* gckSHM_POOL;
typedef struct _gckPAGE_USAGE* gckPAGE_USAGE;

gceSTATUS
drv_mempool_init();

void
drv_mempool_destroy();

void
drv_mempool_alloc_contiguous(
	IN gctUINT32 Bytes,
	OUT gctPHYS_ADDR * Physical,
	OUT gctPOINTER * Logical
	);

int
drv_mempool_free(
		IN gctPOINTER Logical
		);

gctUINT32
drv_mempool_get_baseAddress();

gctUINT32
drv_mempool_get_basePAddress();

gctUINT32
drv_mempool_get_page_size();

gctINT
drv_mempool_get_fileDescriptor();

gceSTATUS
drv_mempool_mem_offset(
	IN gctPOINTER Logical,
	OUT gctUINT32 * Address);

/* Shared memory pool functions. */
gckSHM_POOL drv_shmpool_create(
		IN gctUINT32 Pid,
		IN gctHANDLE Handle,
		IN gctUINT32 PoolSize,
		IN gctUINT32 PageSize);
void
drv_shmpool_destroy(
		IN gckSHM_POOL ShmPool);

gckSHM_POOL
drv_shm_acquire_pool(
		IN gctUINT32 Pid,
		IN gctHANDLE Handle
		);

gckSHM_POOL
drv_shm_acquire_pool2(
		IN gctPOINTER Logical
		);

gceSTATUS
drv_shm_remove_pool(
		IN gctHANDLE Handle
		);

gctUINT32
drv_shmpool_get_BaseAddress(
		IN gckSHM_POOL ShmPool
		);

gctUINT32
drv_shmpool_get_page_size(
		IN gckSHM_POOL ShmPool
		);

gceSTATUS
drv_shmpool_mem_offset(
	IN gctPOINTER Logical,
	OUT gctUINT32 * Address);

gctPOINTER
drv_shmpool_alloc_contiguous(
		IN gctUINT32 Pid,
		IN gctHANDLE Handle,
		IN gctUINT32 Bytes
		);

gctUINT32
drv_shmpool_free(
		IN gctPOINTER Logical
		);

void *
mmap64_join(pid_t pid, void *addr, size_t len, int prot, int flags, int fd, off64_t off);

int
mem_offset64_peer(pid_t pid, const uintptr_t addr, size_t len,
				off64_t *offset, size_t *contig_len);

int
munmap_peer(pid_t pid, void *addr, size_t len);

void *
mmap64_peer(pid_t pid, void *addr, size_t len, int prot, int flags, int fd, off64_t off);

#endif /* __gc_hal_kernel_os_h_ */


