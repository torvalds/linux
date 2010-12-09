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




#include "gc_hal_kernel_qnx.h"
#include "gc_hal_driver.h"
#include "gc_hal_user_context.h"

# include <winmgr/gpu.h>

static gckGALDEVICE galDevice;

/* GN TODO take these from the graphics.conf file. */
#ifdef MMP2
int irqLine = 8;
long registerMemBase = 0xd420d000;
#else
int irqLine = 48;
long registerMemBase = 0xf1840000;
#endif

/* Configurable Memory sizes. */
unsigned long contiguousSize =  ( 64 << 20);			/* Video memory pool. */
unsigned int internalPoolSize = ( 16 << 20);			/* Kernel local memory pool. */
unsigned int sharedPoolSize =   (  8 << 20);			/* Shared per-client memory pool initial size. */
unsigned long registerMemSize = (256 << 10);			/* GPU register memory size. */

/* ContiguousBase should be 0,
 * for video memory to be allocated from the memory pool. */
unsigned long contiguousBase = 0;
long bankSize = 0;
int fastClear = -1;
int compression = -1;
unsigned long baseAddress = 0;

/* Global video memory pool. */
typedef struct _gcsMEM_POOL
{
	gctINT32 freePage;
	gctSIZE_T pageCount;
	gctUINT32 pageSize;
	gctUINT32 poolSize;
	pthread_mutex_t mutex;
	gctUINT32 addr;
	gctUINT32 paddr;
	gckPAGE_USAGE pageUsage;
	gctCHAR fdName[64];
	gctINT fd;
} gcsMEM_POOL;

gcsMEM_POOL memPool;

/* Pointer to list of shared memory pools. */
gckSHM_POOL shmPoolList;
pthread_mutex_t shmPoolListMutex;

/* Resource Manager Globals. */
struct _gcsRESMGR_GLOBALS
{
	dispatch_t *dpp;
	dispatch_context_t *ctp;
	int id;
	thread_pool_attr_t pool_attr;
	thread_pool_t *tpp;
	pthread_t root;
} resmgr_globals;

win_gpu_2_cm_iface_t *g_qnx_gpu_2_cm_iface = 0;
static resmgr_connect_funcs_t connect_funcs;
static resmgr_io_funcs_t io_funcs;
static iofunc_attr_t attr;

gceSTATUS gckVIDMEM_FreeHandleMemory(IN gckVIDMEM Memory, IN gctHANDLE Handle);

gceSTATUS
drv_mempool_init()
{
	off64_t paddr;
	void* addr;
	size_t pcontig;

	/* Default 4KB page size. */
	memPool.pageSize = __PAGESIZE;

	/* Compute number of pages. */
	memPool.pageCount = (contiguousSize + internalPoolSize) / memPool.pageSize;
	gcmkASSERT(memPool.pageCount <= 65536);

	/* Align memPoolSize to page size. */
	memPool.poolSize = memPool.pageCount * memPool.pageSize;

    /* Allocate a single chunk of physical memory.
	 * Zero memory with MAP_ANON so we don't leak any sensitive information by chance. */
	snprintf(memPool.fdName, sizeof(memPool.fdName), "galcore:vidmem:%d", getpid());
    memPool.fd = shm_open(memPool.fdName, O_RDWR|O_CREAT, 0777);
    if (memPool.fd == -1) {
        fprintf(stderr, "galcore:%s[%d]: shm_open failed\n", __FUNCTION__, __LINE__);
        return gcvSTATUS_GENERIC_IO;
    }

    shm_unlink(memPool.fdName);

    if (shm_ctl_special(memPool.fd, SHMCTL_ANON|SHMCTL_PHYS, 0, memPool.poolSize, 0x9) == -1) {
        fprintf(stderr, "galcore:%s[%d]: shm_ctl_special failed\n", __FUNCTION__, __LINE__);
        close(memPool.fd);
        memPool.fd = -1;
        return gcvSTATUS_GENERIC_IO;
    }

    addr = mmap64(0, memPool.poolSize, PROT_READ|PROT_WRITE, MAP_SHARED, memPool.fd, 0);
    if (addr == MAP_FAILED) {
        fprintf(stderr, "galcore:%s[%d]: mmap64 failed\n", __FUNCTION__, __LINE__);
        close(memPool.fd);
        memPool.fd = -1;
        return gcvSTATUS_GENERIC_IO;
    }
	memPool.addr = (gctUINT32)addr;

	if (mem_offset64(addr, NOFD, memPool.poolSize, &paddr, &pcontig) == -1)
	{
	    fprintf(stderr, "galcore:%s[%d]: mem_offset64 failed\n", __FUNCTION__, __LINE__);
		munmap(addr, memPool.poolSize);
		close(memPool.fd);
		memPool.fd = -1;
		memPool.addr = NULL;
        return gcvSTATUS_GENERIC_IO;
	}

	/* TODO. Truncating 64bit value. */
	memPool.paddr = (gctUINT32)paddr;

    printf( "Mempool Map addr range[%x-%x]\n", memPool.addr, memPool.addr +  memPool.poolSize);
	printf( "Mempool Map paddr range[%x-%x]\n", memPool.paddr, memPool.paddr +  memPool.poolSize );

	/* Allocate the page usage array and Initialize all pages to free. */
	memPool.pageUsage = (gckPAGE_USAGE)calloc(
			memPool.pageCount,
			sizeof(struct _gckPAGE_USAGE));

	if (memPool.pageUsage == gcvNULL)
	{
        fprintf( stderr, "malloc failed: %s\n", strerror( errno ) );
		munmap(addr, memPool.poolSize);
		close(memPool.fd);
		memPool.fd = -1;
		memPool.addr = NULL;
		memPool.paddr = 0;
		return gcvSTATUS_GENERIC_IO;
	}

	/* The first page is free.*/
	memPool.freePage = 0;

	/* Initialize the semaphore. */
	if (pthread_mutex_init(&memPool.mutex, NULL) != EOK)
	{
		free(memPool.pageUsage);
		munmap(addr, memPool.poolSize);
		close(memPool.fd);
		memPool.fd = -1;
		memPool.addr = NULL;
		memPool.paddr = 0;
		return gcvSTATUS_GENERIC_IO;
	}

	return gcvSTATUS_OK;
}

void
drv_mempool_destroy()
{
	pthread_mutex_destroy(&memPool.mutex);
	free(memPool.pageUsage);
	memPool.pageUsage = NULL;
	munmap((void*)memPool.addr, memPool.poolSize);
	close(memPool.fd);
	memPool.fd = -1;
	memPool.addr = NULL;
	memPool.paddr = 0;
}

gctINT
drv_mempool_get_fileDescriptor()
{
    return memPool.fd;
}

gctUINT32
drv_mempool_get_basePAddress()
{
	return memPool.paddr;
}

gctUINT32
drv_mempool_get_baseAddress()
{
	return memPool.addr;
}

gctUINT32
drv_mempool_get_page_size()
{
	return memPool.pageSize;
}

gceSTATUS
drv_mempool_mem_offset(
	IN gctPOINTER Logical,
	OUT gctUINT32 * Address)
{
	gctUINT32 logical = (gctUINT32)Logical;

	if ( Address == gcvNULL )
		return gcvSTATUS_INVALID_ARGUMENT;

	if ( logical < memPool.addr
		||	logical > (memPool.addr + memPool.poolSize))
		return gcvSTATUS_INVALID_ARGUMENT;

	*Address = (logical - memPool.addr) + memPool.paddr;

	return gcvSTATUS_OK;
}

/* Allocate pages from mapped shared memory.
   Return Physical and Logical addresses.
*/
void
drv_mempool_alloc_contiguous(
	IN gctUINT32 Bytes,
	OUT gctPHYS_ADDR * Physical,
	OUT gctPOINTER * Logical
	)
{
	gctSIZE_T i, j;
	gctSIZE_T pageCount;

	pthread_mutex_lock(&memPool.mutex);

	/* Compute the number of required pages. */
	pageCount = gcmALIGN(Bytes, drv_mempool_get_page_size()) / drv_mempool_get_page_size();

	if ( (pageCount <= 0) || (memPool.freePage < 0) )
	{
		*Physical = gcvNULL;
		*Logical = gcvNULL;
		pthread_mutex_unlock(&memPool.mutex);
		/* No free pages left. */
		return;
	}

	/* Try finding enough contiguous free pages. */
	for (i = memPool.freePage; i < memPool.pageCount;)
	{
		/* All pages behind this free page should be free. */
		gctSIZE_T j;
		for (j = 1; j < pageCount; ++j)
		{
			if (memPool.pageUsage[i + j].pageCount != 0)
			{
				/* Bail out if page is allocated. */
				break;
			}
		}

		if (j == pageCount)
		{
			/* We found a spot that has enough free pages. */
			break;
		}

		/* Move to the page after the allocated page. */
		i += j + 1;

		/* Find the next free page. */
		while ((i < memPool.pageCount) && (memPool.pageUsage[i].pageCount != 0))
		{
			++i;
		}
	}

	if (i >= memPool.pageCount)
	{
		*Physical = gcvNULL;
		*Logical = gcvNULL;
		pthread_mutex_unlock(&memPool.mutex);
		/* Not enough contiguous pages. */
		return;
	}

	/* Check if we allocate from the first free page. */
	if (i == memPool.freePage)
	{
		/* Move first free page to beyond the contiguous request. */
		memPool.freePage = i + pageCount;

		/* Find first free page. */
		while ( (memPool.freePage < memPool.pageCount) &&
				(memPool.pageUsage[memPool.freePage].pageCount != 0) )
		{
			++memPool.freePage;
		}

		if (memPool.freePage >= memPool.pageCount)
		{
			/* No more free pages. */
			memPool.freePage = -1;
		}
	}

	/* Walk all pages. */
	for (j = 0; j < pageCount; ++j)
	{
		/* Store page count in each pageUsage to mark page is allocated. */
		memPool.pageUsage[i+j].pageCount = pageCount;
	}

	gcmkTRACE(gcvLEVEL_INFO, "Allocated %u contiguous pages from 0x%X\n",
		pageCount, i);

	/* Success. */
	*Physical = (gctPHYS_ADDR)(i * memPool.pageSize + (gctUINT32)memPool.paddr);
	*Logical = (gctPOINTER)(i * memPool.pageSize + (gctUINT32)memPool.addr);

	pthread_mutex_unlock(&memPool.mutex);
}

int drv_mempool_free(gctPOINTER Logical)
{
	gctUINT16 pageCount;
	gctSIZE_T i;
	gctINT32 pageIndex;

	gcmkTRACE(gcvLEVEL_INFO, "Freeing pages @ %x\n", Logical);

	pthread_mutex_lock(&memPool.mutex);

	pageIndex = ((gctUINT32)Logical - (gctUINT32)memPool.addr) / memPool.pageSize;

	/* Verify the memory is valid and unlocked. */
	if ( (pageIndex < 0) || (pageIndex >= memPool.pageCount) )
	{
		pthread_mutex_unlock(&memPool.mutex);
		gcmkTRACE(gcvLEVEL_ERROR, "Invalid page index @ %d\n", pageIndex);
		return -1;
	}

	pageCount = memPool.pageUsage[pageIndex].pageCount;

	/* Mark all used pages as free. */
	for (i = 0; i < pageCount; ++i)
	{
		gcmkASSERT(memPool.pageUsage[i + pageIndex].pageCount == pageCount);

		memPool.pageUsage[i + pageIndex].pageCount = 0;
	}

	/* Update first free page. */
	if ( (memPool.freePage < 0) || (pageIndex < memPool.freePage) )
	{
		memPool.freePage = pageIndex;
	}

	pthread_mutex_unlock(&memPool.mutex);

	gcmkTRACE(gcvLEVEL_INFO, "Free'd %u contiguos pages from 0x%X @ 0x%x\n",
		pageCount, pageIndex);

	return 1;
}

/*
 * Initialize shm pool list and mutex.
 */
gceSTATUS
drv_shm_init()
{
	shmPoolList = gcvNULL;
	pthread_mutex_init(&shmPoolListMutex, 0);
	return gcvSTATUS_OK;
/*	resmgr_globals.shmpool = gcvNULL;
	return pthread_mutex_init(&resmgr_globals.shmMutex, 0);*/
}

gceSTATUS
drv_shm_destroy()
{
	gckSHM_POOL shmPool;
	gctUINT32 count = 0;

	pthread_mutex_destroy(&shmPoolListMutex);

	shmPool = shmPoolList;
	while (shmPool != gcvNULL)
	{
		/* Remove this pool from the list. */
		drv_shmpool_destroy(shmPool);

		shmPool = shmPool->nextPool;
		count++;
	}

	return gcvSTATUS_OK;
/*	resmgr_globals.shmpool = gcvNULL;
	return pthread_mutex_destroy(&resmgr_globals.shmMutex);*/
}

/*
 * Get the shm pool associated with this Handle and PID and lock it.
 * Create one, if not present.
 */
gckSHM_POOL
drv_shm_acquire_pool(
		IN gctUINT32 Pid,
		IN gctHANDLE Handle
		)
{
	gckSHM_POOL shmPool, tail = gcvNULL;

	pthread_mutex_lock(&shmPoolListMutex);

	shmPool = shmPoolList;
	while (shmPool != gcvNULL)
	{
		if (shmPool->Handle == Handle && shmPool->pid == Pid)
		{
			pthread_mutex_unlock(&shmPoolListMutex);
			return shmPool;
		}

		tail = shmPool;
		shmPool = shmPool->nextPool;
	}

	/* TODO: Start with smaller shmMemory pool size and increase on demand. */
	/* Default 4KB page size. */
	shmPool = drv_shmpool_create(Pid, Handle, sharedPoolSize, __PAGESIZE);

	/* Add this pool to tail. */
	if ( shmPool != gcvNULL )
	{
		if (tail != gcvNULL )
		{
			tail->nextPool = shmPool;
		}
		else
		{
			/* Set this pool as head. */
			shmPoolList = shmPool;
		}

		shmPool->nextPool = gcvNULL;
	}
	else
	{
		/* Failed to create new shmPool. */
	}

	pthread_mutex_unlock(&shmPoolListMutex);
	return shmPool;
}

/*
 * Get the shm pool associated with this Logical pointer.
 */
gckSHM_POOL
drv_shm_acquire_pool2(
		IN gctPOINTER Logical
		)
{
	gckSHM_POOL shmPool, tail = gcvNULL;

	pthread_mutex_lock(&shmPoolListMutex);

	shmPool = shmPoolList;
	while (shmPool != gcvNULL)
	{
		/* Check if this address is in range of this shmPool. */
		if (shmPool->Logical <= (gctUINT32)Logical &&
			(shmPool->Logical + shmPool->pageCount * shmPool->pageSize) > (gctUINT32)Logical)
		{
			pthread_mutex_unlock(&shmPoolListMutex);
			return shmPool;
		}

		tail = shmPool;
		shmPool = shmPool->nextPool;
	}

	/* Failed to find associated shmPool. */
	pthread_mutex_unlock(&shmPoolListMutex);
	return shmPool;
}

/*
 * Remove the shm pool associated with this ocb.
 */
gceSTATUS
drv_shm_remove_pool(
		IN gctHANDLE Handle
		)
{
	gckSHM_POOL shmPool, prev = gcvNULL;

	pthread_mutex_lock(&shmPoolListMutex);

	shmPool = shmPoolList;

	while (shmPool != gcvNULL)
	{
		/* Remove this pool from the list. */
		if (shmPool->Handle == Handle)
		{
			if (prev == gcvNULL)
			{
				shmPoolList = shmPool->nextPool;
			}
			else
			{
				prev->nextPool = shmPool->nextPool;
			}

			drv_shmpool_destroy(shmPool);

			pthread_mutex_unlock(&shmPoolListMutex);
			return gcvSTATUS_OK;
		}

		prev = shmPool;
		shmPool = shmPool->nextPool;
	}

	pthread_mutex_unlock(&shmPoolListMutex);

	return gcvSTATUS_INVALID_ARGUMENT;
}

gceSTATUS
drv_shmpool_mem_offset(
	IN gctPOINTER Logical,
	OUT gctUINT32 * Address)
{
	gctUINT32 logical = (gctUINT32)Logical;
	gckSHM_POOL shmPool;

	if ( Address == gcvNULL )
		return gcvSTATUS_INVALID_ARGUMENT;

	pthread_mutex_lock(&shmPoolListMutex);

	shmPool = shmPoolList;
	while (shmPool != gcvNULL)
	{
		if ( (logical >= shmPool->Logical)
			&&	(logical < (shmPool->Logical + shmPool->pageCount * shmPool->pageSize)))
		{
			*Address = (logical - shmPool->Logical ) + shmPool->Physical;
			pthread_mutex_unlock(&shmPoolListMutex);
			return gcvSTATUS_OK;
		}

		shmPool = shmPool->nextPool;
	}

	pthread_mutex_unlock(&shmPoolListMutex);

	return gcvSTATUS_INVALID_ARGUMENT;
}

/* Initialize a shm pool with this ocb. */
gckSHM_POOL drv_shmpool_create(
		IN gctUINT32 Pid,
		IN gctHANDLE Handle,
		IN gctUINT32 PoolSize,
		IN gctUINT32 PageSize)
{
	int rc, poolSize;
	void* addr;
	char shm_file_name[20] = "shm_galcore";
	gctUINT32 i, pid;
	gckSHM_POOL shm = (gckSHM_POOL) calloc(1, sizeof(struct _gckSHM_POOL));

	/* Compute number of pages. */
	shm->pageSize = PageSize;
	shm->pageCount = PoolSize / shm->pageSize;
	gcmkASSERT(shm->pageCount <= 65536);

	/* Align poolSize to pageSize. */
	poolSize = shm->pageCount * shm->pageSize;

	shm->pid = Pid;
	shm->Handle = Handle;

	/* Initialize the semaphore. */
	if (pthread_mutex_init(&shm->mutex, NULL) != EOK)
	{
        fprintf( stderr, "pthread_mutex_init failed: %s\n", strerror( errno ) );
		free(shm);
		return gcvNULL;
	}

	/* Create a pseudo unique name, so as to not open
	 * the same file twice from different threads at the same time. */
	pid = Pid;
	i = strlen(shm_file_name);
	while(pid)
	{
		shm_file_name[i++] = (char)(pid % 10 + '0');
		pid /= 10;
	}
	shm_file_name[i] = '\0';

	shm->fd = shm_open(shm_file_name, O_RDWR | O_CREAT, 0777);
	if (shm->fd == -1) {
		free(shm);
		return gcvNULL;
	}

	shm_unlink(shm_file_name);

	/* Special flags for this shm, to make it write buffered. */
	rc = shm_ctl_special(shm->fd,
						 SHMCTL_ANON | SHMCTL_PHYS /*| SHMCTL_LAZYWRITE*/,
						 0,
						 poolSize,
						 0x9);
	if (rc == -1) {
		close(shm->fd);
		free(shm);
		return gcvNULL;
	}

	/* Map this memory inside user and galcore. */
	addr = mmap64_join(Pid,
					   0,
					   poolSize,
					   PROT_READ | PROT_WRITE,
					   MAP_SHARED,
					   shm->fd,
					   0);
	if (addr == MAP_FAILED)
	{
		free(shm);
		return gcvNULL;
	}

	/* TODO: Dont close fd if need to truncate shm later. */
	rc = close(shm->fd);
	if (rc == -1) {
		free(shm);
		return gcvNULL;
	}

	shm->Logical = (gctUINT32) addr;

	/* fd should be NOFD here, to get physical address. */
	rc = mem_offset( addr, NOFD, 1, (off_t *)&shm->Physical, NULL);
	if (rc == -1) {
		free(shm);
		return gcvNULL;
	}

    /* TODO: MLOCK may or may not be needed!. */
    mlock((void*)shm->Logical, poolSize);

	/* Allocate the page usage array and Initialize all pages to free. */
    shm->pageUsage = (gckPAGE_USAGE)calloc(shm->pageCount, sizeof(struct _gckPAGE_USAGE));
	if (shm->pageUsage == gcvNULL)
	{
		munmap((void*)shm->Logical, poolSize);
		munmap_peer(Pid, (void*)shm->Logical, poolSize);
		free(shm);
		return gcvNULL;
	}

	/* The first page is free. */
	shm->freePage = 0;

	return shm;
}

void
drv_shmpool_destroy(
		IN gckSHM_POOL ShmPool)
{
	if (ShmPool)
	{
		int poolSize = ShmPool->pageCount * ShmPool->pageSize;
		if (ShmPool->pageUsage)
			free(ShmPool->pageUsage);
		if (ShmPool->Logical)
		{
			munmap((void*)ShmPool->Logical, poolSize);
			munmap_peer(ShmPool->pid, (void*)ShmPool->Logical, poolSize);
		}
	}
}

gctUINT32
drv_shmpool_get_BaseAddress(
		IN gckSHM_POOL ShmPool
		)
{
	gcmkASSERT(ShmPool != 0);

	if (!ShmPool)
		return 0;

	return ShmPool->Logical;
}

gctUINT32
drv_shmpool_get_page_size(
		IN gckSHM_POOL ShmPool
		)
{
	gcmkASSERT(ShmPool != 0);

	if (!ShmPool)
		return 0;

	return ShmPool->pageSize;
}

/* Allocate pages from mapped shared memory.
   Return Logical user address.
*/
gctPOINTER
drv_shmpool_alloc_contiguous(
		IN gctUINT32 Pid,
		IN gctHANDLE Handle,
		IN gctUINT32 Bytes
		)
{
	gctSIZE_T i, j;
	int pageSize;
	gctSIZE_T pageCount;
	gckSHM_POOL shmPool = drv_shm_acquire_pool(Pid, Handle);

	if (shmPool == gcvNULL)
	{
		return gcvNULL;
	}
	/* Compute the number of required pages. */
	pageSize = drv_shmpool_get_page_size(shmPool);
	if ( pageSize == 0 )
	{
		/* Invalid pageSize. */
		return gcvNULL;
	}

	pageCount = gcmALIGN(Bytes, pageSize) / pageSize;

	if ( (pageCount <= 0) || (shmPool->freePage < 0) )
	{
		/* No free pages left. */
		return gcvNULL;
	}

	if ( (pageCount <= 0) || (shmPool->freePage < 0) )
	{
		/* No free pages left. */
		return gcvNULL;
	}

	/* Try finding enough contiguous free pages. */
	for (i = shmPool->freePage; i < shmPool->pageCount;)
	{
		/* All pages behind this free page should be free. */
		gctSIZE_T j;
		for (j = 1; j < pageCount; ++j)
		{
			if (shmPool->pageUsage[i + j].pageCount != 0)
			{
				/* Bail out if page is allocated. */
				break;
			}
		}

		if (j == pageCount)
		{
			/* We found a spot that has enough free pages. */
			break;
		}

		/* Move to the page after the allocated page. */
		i += j + 1;

		/* Find the next free page. */
		while ((i < shmPool->pageCount) && (shmPool->pageUsage[i].pageCount != 0))
		{
			++i;
		}
	}

	if (i >= shmPool->pageCount)
	{
		/* Not enough contiguous pages. */
		return gcvNULL;
	}

	/* Check if we allocate from the first free page. */
	if (i == shmPool->freePage)
	{
		/* Move first free page to beyond the contiguous request. */
		shmPool->freePage = i + pageCount;

		/* Find first free page. */
		while ( (shmPool->freePage < shmPool->pageCount) &&
				(shmPool->pageUsage[shmPool->freePage].pageCount != 0) )
		{
			++shmPool->freePage;
		}

		if (shmPool->freePage >= shmPool->pageCount)
		{
			/* No more free pages. */
			shmPool->freePage = -1;
		}
	}

	/* Walk all pages. */
	for (j = 0; j < pageCount; ++j)
	{
		/* Store page count in each pageUsage to mark page is allocated. */
		shmPool->pageUsage[i+j].pageCount = pageCount;
	}

	gcmkTRACE(gcvLEVEL_INFO, "Allocated %u contiguos pages from 0x%X\n",
		pageCount, i);

	/* Success. */
	return (gctPOINTER)(i * shmPool->pageSize + shmPool->Logical);
}

gctUINT32
drv_shmpool_free(
		IN gctPOINTER Logical
		)
{
	gctUINT16 pageCount;
	gctSIZE_T i;
	gctINT32 pageIndex;
	gckSHM_POOL shmPool = drv_shm_acquire_pool2(Logical);

	if (shmPool == gcvNULL)
	{
		gcmkTRACE(gcvLEVEL_ERROR, "Invalid Logical addr: %x.\n", Logical);
		return 0;
	}

	pageIndex = ((gctUINT32)Logical - shmPool->Logical)/shmPool->pageSize;

	gcmkTRACE(gcvLEVEL_INFO, "Freeing pages @ %d\n", pageIndex);

	/* Verify the memory is valid and unlocked. */
	if ( (pageIndex < 0) || (pageIndex >= shmPool->pageCount) )
	{
		gcmkTRACE(gcvLEVEL_ERROR, "Invalid page index @ %d\n", pageIndex);

		return 0;
	}

	pageCount = shmPool->pageUsage[pageIndex].pageCount;

	/* Mark all used pages as free. */
	for (i = 0; i < pageCount; ++i)
	{
		gcmkASSERT(shmPool->pageUsage[i + pageIndex].pageCount == pageCount);

		shmPool->pageUsage[i + pageIndex].pageCount = 0;
	}

	/* Update first free page. */
	if ( (shmPool->freePage < 0) || (pageIndex < shmPool->freePage) )
	{
		shmPool->freePage = pageIndex;
	}

	gcmkTRACE(gcvLEVEL_INFO, "Free'd %u contiguos pages from 0x%X @ 0x%x\n",
		pageCount, pageIndex);

	return 1;
}

int drv_msg(resmgr_context_t *ctp,
			io_msg_t *msg,
			RESMGR_OCB_T *ocb)
{
	gcsDRIVER_ARGS *drvArgs = (gcsDRIVER_ARGS *)msg;
    int rc;
    gceSTATUS status;
    gcsQUEUE_PTR queue;

#define UNLOCK_RESMGR
#ifdef UNLOCK_RESMGR
	iofunc_attr_unlock(&attr);
#endif

    if ((drvArgs->iomsg.i.type != _IO_MSG)
	|| (drvArgs->iomsg.i.mgrid != _IOMGR_VIVANTE)
	|| (drvArgs->iomsg.i.subtype != IOCTL_GCHAL_INTERFACE
	    && drvArgs->iomsg.i.subtype != IOCTL_GCHAL_KERNEL_INTERFACE
		&& drvArgs->iomsg.i.subtype != IOCTL_GCHAL_TERMINATE))
    {
        /* Unknown command. Fail the I/O. */
#ifdef UNLOCK_RESMGR
    	iofunc_attr_lock(&attr);
#endif
		rc = ENOSYS;
		if (ctp->info.scoid != -1)
			return _RESMGR_STATUS(ctp, rc);
		return _RESMGR_NOREPLY;
    }

	if (drvArgs->iomsg.i.subtype == IOCTL_GCHAL_TERMINATE)
	{
		/* terminate the resource manager */
		pthread_kill(resmgr_globals.root, SIGTERM);
#ifdef UNLOCK_RESMGR
		iofunc_attr_lock(&attr);
#endif
		return _RESMGR_NOREPLY;
	}

	/* Save channel handle and pid for later functions. */
	drvArgs->iface.handle = (gctHANDLE)ocb;
	drvArgs->iface.pid = (gctUINT32)ctp->info.pid;

	/* Store receive ID with signal event so that we can later respond via pulse. */
	switch (drvArgs->iface.command)
	{
	case gcvHAL_SIGNAL:
		printf("Setup rcvid as:%d\n", ctp->rcvid);
	    drvArgs->iface.u.Signal.rcvid = ctp->rcvid;
        break;

    case gcvHAL_EVENT_COMMIT:
        for (queue = drvArgs->iface.u.Event.queue; queue != gcvNULL; queue = queue->next)
        {
            if (queue->iface.command == gcvHAL_SIGNAL)
            {
                queue->iface.u.Signal.rcvid = ctp->rcvid;
            }
        }
        break;

    default:
        break;
	}

	status = gckKERNEL_Dispatch(galDevice->kernel,
								(drvArgs->iomsg.i.subtype == IOCTL_GCHAL_INTERFACE),
								&drvArgs->iface);

	if (gcmIS_ERROR(status))
	{
		gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DRIVER,
				  "[galcore] gckKERNEL_Dispatch returned %d.\n",
			  status);
	}
	else if (gcmIS_ERROR(drvArgs->iface.status))
	{
		gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DRIVER,
				  "[galcore] IOCTL %d returned %d.\n",
			  drvArgs->iface.command,
			  drvArgs->iface.status);
	}

	/* Reply data back to the user. */
	MsgReply(ctp->rcvid, EOK, (gctPOINTER) &drvArgs->iface, sizeof(gcsHAL_INTERFACE));

#ifdef UNLOCK_RESMGR
	iofunc_attr_lock(&attr);
#endif

	gcmkTRACE(gcvLEVEL_INFO, "Replied message with command %d, status %d\n",
		drvArgs->iface.command,
		drvArgs->iface.status);

    return (_RESMGR_NOREPLY);
}

static int drv_init(void)
{
	/* TODO: Enable clock by driver support? */
    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
    	    	  "Entering drv_init\n");

    /* Create the GAL device. */
    gcmkVERIFY_OK(gckGALDEVICE_Construct(irqLine,
    	    	    registerMemBase,
					registerMemSize,
					contiguousBase,
					contiguousSize,
					bankSize,
					fastClear,
					compression,
					baseAddress,
					&galDevice));

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
    	    	  "galcore device constructed.\n");

    /* Start the GAL device. */
    if (gcmIS_ERROR(gckGALDEVICE_Start(galDevice)))
    {
    	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    	    	      "[galcore] Can't start the gal device.\n");

    	/* Roll back. */
    	gckGALDEVICE_Stop(galDevice);
    	gckGALDEVICE_Destroy(galDevice);

    	return -1;
    }

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
    	    	  "[galcore] irqLine->%ld, contiguousSize->%lu, memBase->0x%lX\n",
		  irqLine,
		  contiguousSize,
		  registerMemBase);

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
    	    	  "[galcore] driver registered successfully.\n");

    return 0;
}

static void drv_exit(void)
{
    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
    	    	  "[galcore] Entering drv_exit\n");

    gckGALDEVICE_Stop(galDevice);
    gckGALDEVICE_Destroy(galDevice);

}

/* Invoked by OS, when a connection is closed or dies. */
int
drv_close_ocb(resmgr_context_t *ctp, void *reserved, RESMGR_OCB_T *ocb)
{
	gckVIDMEM videoMemory;
	gceSTATUS status;

	/* Free virtual memory owned by this client. */
	gckMMU_FreeHandleMemory(galDevice->kernel->mmu, (gctHANDLE)ocb);

	/* Free system memory owned by the client. */
	status = gckKERNEL_GetVideoMemoryPool(galDevice->kernel, gcvPOOL_SYSTEM, &videoMemory);

	if (status == gcvSTATUS_OK)
	{
		gckVIDMEM_FreeHandleMemory(videoMemory,
								   (gctHANDLE)ocb);
	}

	/* Free shared memory and its mapping. */
	drv_shm_remove_pool(ocb);

	return iofunc_close_ocb_default(ctp, reserved, ocb);
}

int gpu_init()
{
	/* Declare variables we'll be using. */
	resmgr_attr_t resmgr_attr;
	sigset_t  sigset;
	int rc;

	if (drv_mempool_init() != gcvSTATUS_OK)
	{
		fprintf(stderr, "drv_mempool_init failed.");
		goto fail_001;
	}

	if (drv_shm_init() != gcvSTATUS_OK)
	{
		fprintf(stderr, "drv_mempool_init failed.");
		goto fail_002;
	}

	if (drv_init() != 0)
	{
		fprintf(stderr, "drv_init failed.");
		goto fail_003;
	}

	/* initialize dispatch interface */
	if((resmgr_globals.dpp = dispatch_create()) == NULL)
	{
		fprintf(stderr,	"Unable to allocate dispatch handle.\n");
		goto fail_004;
	}

	/* initialize resource manager attributes */
	memset(&resmgr_attr, 0, sizeof resmgr_attr);
	resmgr_attr.nparts_max = 1;
	resmgr_attr.msg_max_size = 2048;

	/* initialize functions for handling messages */
	iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs,
			_RESMGR_IO_NFUNCS, &io_funcs);

	/* Register io handling functions. */
	io_funcs.msg = drv_msg;
	io_funcs.close_ocb = drv_close_ocb;


	/* initialize attribute structure used by the device */
	iofunc_attr_init(&attr, S_IFNAM | 0666, 0, 0);

	/* attach our device name */
	resmgr_globals.id = resmgr_attach(
			resmgr_globals.dpp,/* dispatch handle */
			&resmgr_attr,			/* resource manager attrs */
			GAL_DEV,				/* device name */
			_FTYPE_ANY,				/* open type */
			_RESMGR_FLAG_SELF,		/* flags */
			&connect_funcs,			/* connect routines */
			&io_funcs,				/* I/O routines */
			&attr);					/* handle */
	if (resmgr_globals.id == -1) {
		fprintf(stderr, "Unable to attach name.\n");
        goto fail_005;
	}

    /* Prevent signals from affecting resmgr threads. */
    sigfillset(&sigset);
    sigdelset(&sigset, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigset, NULL);

    /* initialize thread pool attributes */
    memset(&resmgr_globals.pool_attr, 0, sizeof(resmgr_globals.pool_attr));
    resmgr_globals.pool_attr.handle = resmgr_globals.dpp;
    resmgr_globals.pool_attr.context_alloc = resmgr_context_alloc;
    resmgr_globals.pool_attr.block_func = resmgr_block;
    resmgr_globals.pool_attr.unblock_func = resmgr_unblock;
    resmgr_globals.pool_attr.handler_func = resmgr_handler;
    resmgr_globals.pool_attr.context_free = resmgr_context_free;
    resmgr_globals.pool_attr.lo_water = 2;
    resmgr_globals.pool_attr.hi_water = 4;
    resmgr_globals.pool_attr.increment = 1;
    resmgr_globals.pool_attr.maximum = 50;
#if (defined(_NTO_VERSION) && (_NTO_VERSION >= 650))
	resmgr_globals.pool_attr.tid_name = "galcore-message-handler";
#endif

    /* allocate a thread pool handle */
    resmgr_globals.tpp = thread_pool_create(&resmgr_globals.pool_attr, POOL_FLAG_EXIT_SELF);
    if (resmgr_globals.tpp == NULL)
	{
        goto fail_006;
    }

    rc = pthread_create(NULL, resmgr_globals.pool_attr.attr, (void * (*)(void *))thread_pool_start, resmgr_globals.tpp);
    if (rc != 0)
	{
        goto fail_007;
    }

	/* TODO: gpu_suspend, gpu_resume */

	return EXIT_SUCCESS;

fail_007:
    thread_pool_destroy(resmgr_globals.tpp);
fail_006:
    resmgr_detach(resmgr_globals.dpp, resmgr_globals.id, 0);
fail_005:
    dispatch_destroy(resmgr_globals.dpp);
fail_004:
    drv_exit();
fail_003:
	drv_shm_destroy();
fail_002:
	drv_mempool_destroy();
fail_001:
    return EXIT_FAILURE;
}

int gpu_fini()
{
    thread_pool_destroy(resmgr_globals.tpp);
    resmgr_detach(resmgr_globals.dpp, resmgr_globals.id, 0);
    dispatch_destroy(resmgr_globals.dpp);
    drv_exit();
    drv_shm_destroy();
	drv_mempool_destroy();
	return EXIT_SUCCESS;
}

#ifndef QNX_USE_OLD_FRAMEWORK

int GPU_Startup(win_gpu_2_cm_iface_t *iface)
{
    g_qnx_gpu_2_cm_iface = iface;
	return gpu_init();
}

int GPU_Shutdown()
{
    g_qnx_gpu_2_cm_iface = NULL;
	return gpu_fini();
}

void win_gpu_module_getfuncs(win_cm_2_gpu_iface_t *iface)
{
	iface->init = GPU_Startup;
	iface->fini = GPU_Shutdown;
}

#else /* QNX_USE_OLD_FRAMEWORK */

int drv_resmgr_loop()
{
    sigset_t  sigset;
    siginfo_t info;

	resmgr_globals.root = pthread_self();

    /* Background ourselves */
    procmgr_daemon(EXIT_SUCCESS, PROCMGR_DAEMON_NODEVNULL |
								 PROCMGR_DAEMON_NOCHDIR |
								 PROCMGR_DAEMON_NOCLOSE);

    /*
     * This thread ignores all signals except SIGTERM. On receipt of
     * a SIGTERM, we shut everything down and exit.
     */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGTERM);

    while (1)
	{
        if (SignalWaitinfo(&sigset, &info) == -1)
            continue;
        if (info.si_signo == SIGTERM)
		{
            break;
		}
    }

    return EXIT_SUCCESS;
}

int	drv_start_cmd()
{
    int rc;

    printf("Starting up...\n");
	fflush(stdout);

    pthread_setname_np(pthread_self(), "vivante-monitor");

	if ((rc = gpu_init()) != EXIT_SUCCESS)
	{
		fprintf(stderr, "Initialization failed!, Exiting.");
        exit(EXIT_FAILURE);
	}

	printf("Running galcore...\n");
	fflush(stdout);
    rc = drv_resmgr_loop();
    printf("Shutting down galcore...\n");
	fflush(stdout);

	gpu_fini();

	return EXIT_SUCCESS;
}

int drv_stop_cmd()
{
    gcsDRIVER_ARGS args;
    int fd, rc;

	/* Open the gpu device. */
    fd = open(DRV_NAME, O_RDONLY);
    if (fd == -1)
	{
        fprintf(stderr, "Could not connect to " DRV_NAME);
        return EXIT_FAILURE;
    }

	/* Send the term message. */
    args.iomsg.i.type    = _IO_MSG;
    args.iomsg.i.subtype = IOCTL_GCHAL_TERMINATE;
    args.iomsg.i.mgrid   = _IOMGR_VIVANTE;
    args.iomsg.i.combine_len = sizeof(io_msg_t);

    do {
        rc = MsgSend_r(fd, &args, args.iomsg.i.combine_len, NULL, 0);
    } while ((rc * -1) == EINTR);

	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	enum { start, stop } cmd = start;
    int i;
	int rc = EXIT_FAILURE;

    /* Process command lines -start, -stop, -c (file), -d [file]. */
    for (i = 1; i < argc; i++)
	{
        if (stricmp(argv[i], "-start") == 0)
		{
            cmd = start;
        }
		else if (strcmp(argv[i], "-stop") == 0)
		{
            cmd = stop;
        }
		else if (strncmp(argv[i], "-poolsize=", strlen("-poolsize=")) == 0)
		{
			/* The syntax of the poolsize option is -poolsize=(number).
			 * All we need is to convert the number that starts after the '='.*/
			contiguousSize = atoi(argv[i] + strlen("-poolsize="));
			if (contiguousSize <= 0)
			{
				fprintf(stderr, "%s: poolsize needs to be a positive number\n", strerror(errno));
				return rc;
			}
		}
		else
		{
            fprintf(stderr, "%s: bad command line\n", argv[0]);
            return rc;
        }
    }

    switch (cmd)
	{
        case start:
			/* Elevate thread priority to do IO. */
			ThreadCtl(_NTO_TCTL_IO, 0);
			rc = drv_start_cmd();
            break;

        case stop:
            rc = drv_stop_cmd();
            break;
    }

    return rc;
}

#endif /* QNX_USE_OLD_FRAMEWORK */

