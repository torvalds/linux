/** MobiCore driver module.(interface to the secure world SWD)
 * @addtogroup MobiCore_Driver_Kernel_Module
 * @{
 * @file
 * MobiCore Driver Kernel Module.
 * This module is written as a Linux device driver.
 * This driver represents the command proxy on the lowest layer, from the
 * secure world to the non secure world, and vice versa.
 * This driver is located in the non secure world (Linux).
 * This driver offers IOCTL commands, for access to the secure world, and has
 * the interface from the secure world to the normal world.
 * The access to the driver is possible with a file descriptor,
 * which has to be created by the fd = open(/dev/mobicore) command.
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "mcDrvModule.h"
#include "mcDrvModuleLinuxApi.h"
#include "mcDrvModuleAndroid.h"
#include "mcDrvModuleFc.h"
#include "public/mcKernelApi.h"
#include "Mci/mcimcp.h"
#include "buildTag.h"

/** MobiCore interrupt context data */
static struct mcDrvKModCtx	mcDrvKModCtx;

/** MobiCore MCI information */
static uint32_t             mciBase = 0;
static mcpBuffer_ptr        mcpBuffer = NULL;
/*
#############################################################################
##
## Convenience functions for Linux API functions
##
#############################################################################*/
static int gotoCpu0(void);
static int gotoAllCpu(void) __attribute__ ((unused));

/*----------------------------------------------------------------------------*/
static void initAndAddToList(
	struct list_head *pItem,
	struct list_head *pListHead
)
{
	INIT_LIST_HEAD(pItem);

	list_add(pItem, pListHead);
}

/*----------------------------------------------------------------------------*/
/** check if CPU supports the ARM TrustZone Security Extensions
 *    @return int TRUE or FALSE */
static int hasSecurityExtensions(
	void
)
{
	u32 fea = 0;
	asm volatile("\
		mrc p15, 0, %[fea], cr0, cr1, 0" :
		[fea]"=r" (fea));

	MCDRV_DBG_VERBOSE("CPU Features: 0x%X", fea);

	/* If the CPU features ID has 0 for security features then the CPU
	 * doesn't support TrustZone at all!
	 */
	if ((fea & ARM_SECURITY_EXTENSION_MASK) == 0)
		return 0;

	return 1;
}

/*----------------------------------------------------------------------------*/
/** check if running in secure mode
 *    @return int TRUE or FALSE */
static int isSecureMode(
	void
)
{
	u32 cpsr = 0, nsacr = 0;
	asm volatile("\
		mrc	p15, 0, %[nsacr], cr1, cr1, 2\n\
		mrs %[cpsr], cpsr\n" :
		[nsacr]"=r" (nsacr),
		[cpsr]"=r"(cpsr));

	MCDRV_DBG_VERBOSE("CPRS.M = set to 0x%X\n", cpsr & ARM_CPSR_MASK);
	MCDRV_DBG_VERBOSE("SCR.NS = set to 0x%X\n", nsacr);

	/* If the NSACR contains the reset value(=0) then most likely we are
	 * running in Secure MODE.
	 * If the cpsr mode is set to monitor mode then we cannot load!
	 */
	if (nsacr == 0 || ((cpsr & ARM_CPSR_MASK) == ARM_MONITOR_MODE))
		return 1;

	return 0;
}

/*----------------------------------------------------------------------------*/
/** check if userland caller is privileged (aka has "root" access rights).
    @return int TRUE or FALSE */
static int isUserlandCallerPrivileged(
	void
) {
	MCDRV_DBG_VERBOSE("enter %u\n", current_euid());
	/* For some platforms we cannot run the Daemon as root - for Android
	 * compliance tests it is not allowed, thus we assume the daemon is ran
	 * as the system user.
	 * In Android the system user for daemons has no particular capabilities
	 * other than a fixed UID: AID_SYSTEM        1000
	 * The actual number is guaranteed to be the same in all Android systems
	 * so we will take it for granted: see android_filesystem_config.h in
	 * the Android source tree for all UIDs and their meaning:
	 * http://android-dls.com/wiki/index.php?title=Android_UIDs_and_GIDs
	 */
#ifdef MC_ANDROID_UID_CHECK
	return (current_euid() == AID_SYSTEM);
#else
	/* capable should cover all possibilities, root or sudo, uid checking
	 * was not very reliable */
	return capable(CAP_SYS_ADMIN);
#endif
}



/*----------------------------------------------------------------------------*/
static void unlockPagefromWsmL2Table(
	struct page *pPage
){
	/* REV axh: check if we should do this. */
	SetPageDirty(pPage);

	/* release page, old api was page_cache_release() */
	ClearPageReserved(pPage);
	put_page(pPage);
}

/*----------------------------------------------------------------------------*/
/* convert L2 PTE to page pointer */
static struct page *l2PteToPage(
	pte_t pte
) {
	void         *physPageAddr = (void *)((unsigned int)pte & PAGE_MASK);
	unsigned int pfn          = addrToPfn(physPageAddr);
	struct page  *pPage        = pfn_to_page(pfn);
	return pPage;
}

/*----------------------------------------------------------------------------*/
/* convert page pointer to L2 PTE */
static pte_t pageToL2Pte(
	struct page *pPage
)
{
	unsigned int pfn      = page_to_pfn(pPage);
	void         *physAddr = pfnToAddr(pfn);
	pte_t        pte      = (pte_t)((unsigned int)physAddr & PAGE_MASK);
	return pte;
}


/*----------------------------------------------------------------------------*/
static inline int lockUserPages(
	struct task_struct	*pTask,
	void			*virtStartPageAddr,
	int			nrOfPages,
	struct page		**pages
)
{
	int		ret = 0;
	int		lockedPages = 0;
	unsigned int	i;

	do {

		/* lock user pages, must hold the mmap_sem to do this. */
		down_read(&(pTask->mm->mmap_sem));
		lockedPages = get_user_pages_nocma(
				      pTask,
				      pTask->mm,
				      (unsigned long)virtStartPageAddr,
				      nrOfPages,
				      1, /* write access */
				      0, /* they say drivers should always
						pass 0 here..... */
				      pages,
				      NULL); /* we don't need the VMAs */
		up_read(&(pTask->mm->mmap_sem));

		/* could as lock all pages? */
		if (lockedPages != nrOfPages) {
			MCDRV_DBG_ERROR(
				"get_user_pages() failed, "
				"lockedPages=%d\n",
				lockedPages);
			ret = -ENOMEM;
			/* check if an error has been returned. */
			if (lockedPages < 0) {
				ret = lockedPages;
				lockedPages = 0;
			}
			break;
		}

		/* do cache maintenance on locked pages. */
		for (i = 0; i < nrOfPages; i++)
			flush_dcache_page(pages[i]);

	} while (FALSE);


	if (0 != ret) {
		/* release all locked pages. */
		MCDRV_ASSERT(0 <= lockedPages);
		for (i = 0; i < lockedPages; i++)
			put_page(pages[i]);
	}

	return ret;

}

/*
#############################################################################
##
## Driver implementation functions
##
#############################################################################*/

#ifdef MC_MEM_TRACES
/** MobiCore log previous char */
static uint32_t mcLogPos = 0;
/** MobiCore log buffer structure */
static struct mcTraceBuf *mcLogBuf = NULL;
static DEFINE_MUTEX(log_mutex);
/*----------------------------------------------------------------------------*/
static void mobicore_read_log(
	void
) {
	uint32_t write_pos;
	char *buff, *last_char;

	if (mcLogBuf == NULL)
		return;

	write_pos = mcLogBuf->write_pos;
	buff = mcLogBuf->buff + mcLogPos;
	last_char = mcLogBuf->buff + write_pos;

	/* Nothing to do! */
	if(write_pos == mcLogPos)
		return;
	mutex_lock(&log_mutex);
	while( buff != last_char) {
		printk("%c", *(buff++));
		/* Wrap around */
		if(buff >= (char*)mcLogBuf + PAGE_SIZE)
			buff = mcLogBuf->buff;
	}
	mcLogPos = write_pos;
	mutex_unlock(&log_mutex);
}

/*----------------------------------------------------------------------------*/
/**
 * Setup mobicore kernel log
 */
static void mcKernelModule_setupLog(
	void
) {
	void *logPage;
	unsigned long physLogPage;
	union fcGeneric fcLog;
	int ret;

	/* We need to go to CPU0 because we are going to do some SMC calls and
	 * they will otherwise fail because the Mobicore Monitor resides on
	 * CPU0(for Cortex A9 and lower) */
	ret = gotoCpu0();
	if (0 != ret) {
		MCDRV_DBG("changing core failed!\n");
		return;
	}

	mcLogPos = 0;
	do {
		if (!(logPage = (void *)get_zeroed_page(GFP_KERNEL))) {
			MCDRV_DBG_ERROR("Failed to get page for logger!");
			break;
		}
		physLogPage = virt_to_phys(logPage);
		mcLogBuf = logPage;

		memset(&fcLog, 0, sizeof(fcLog));
		fcLog.asIn.cmd      = MC_FC_NWD_TRACE;
		fcLog.asIn.param[0] = physLogPage;
		fcLog.asIn.param[1] = PAGE_SIZE;

		MCDRV_DBG("fcLog virtPage=%p phyLogPage=%p ", logPage, (void*)physLogPage);
		mcFastCall(&fcLog);
		MCDRV_DBG("fcInfo out ret=0x%08x", fcLog.asOut.ret);

		if (fcLog.asOut.ret) {
			MCDRV_DBG_ERROR("Mobicore shared traces setup failed!");
			free_page((unsigned long)logPage);
			mcLogBuf = NULL;
			break;
		}
	} while(FALSE);

	/* Reset the mask of the current process to All cpus */
	gotoAllCpu();

	MCDRV_DBG_VERBOSE("fcLog Logger version %u\n", mcLogBuf->version);
}
#endif //#ifdef MC_MEM_TRACES


/*----------------------------------------------------------------------------*/
/* check if caller is MobiCore Daemon */
static unsigned int isCallerMcDaemon(
	struct mcInstance *pInstance
)
{
	return ((NULL != pInstance)
		&& (mcDrvKModCtx.daemonInst == pInstance));
}


/*----------------------------------------------------------------------------*/
static struct mcInstance *getInstance(
	struct file *pFile
) {
	MCDRV_ASSERT(NULL != pFile);

	return (struct mcInstance *)(pFile->private_data);
}


/*----------------------------------------------------------------------------*/
/* get a unique ID */
static unsigned int getMcKModUniqueId(
	void
)
{
	return (unsigned int)atomic_inc_return(
		       &(mcDrvKModCtx.uniqueCounter));
}


/*----------------------------------------------------------------------------*/
/* get a unique ID */
static struct l2Table *getL2TableKernelVirt(
	struct mcL2TablesDescr	*pDescr
)
{
	MCDRV_ASSERT(NULL != pDescr);
	MCDRV_ASSERT(NULL != pDescr->pChunk);
	MCDRV_ASSERT(NULL != pDescr->pChunk->kernelVirt);
	return &(pDescr->pChunk->kernelVirt->table[pDescr->idx]);
}

/*----------------------------------------------------------------------------*/
/* get a unique ID */
static struct l2Table *getL2TablePhys(
	struct mcL2TablesDescr  *pDescr
)
{
	MCDRV_ASSERT(NULL != pDescr);
	MCDRV_ASSERT(NULL != pDescr->pChunk);
	MCDRV_ASSERT(NULL != pDescr->pChunk->phys);
	return &(pDescr->pChunk->phys->table[pDescr->idx]);
}

/*----------------------------------------------------------------------------*/
/* get a unique ID */
static unsigned int isWsmL2InUse(
	struct mcL2TablesDescr	*pWsmL2Descr
)
{
	return (0 != (pWsmL2Descr->flags &
		      (MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_APP
		       | MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_MC)));
}



/*----------------------------------------------------------------------------*/
static struct mcL2TablesDescr *findWsmL2ByHandle(
	unsigned int	handle
) {
	struct mcL2TablesDescr  *pTmpDescr;
	struct mcL2TablesDescr  *pWsmL2TableDescr = NULL;

	list_for_each_entry(
		pTmpDescr,
		&(mcDrvKModCtx.wsmL2Descriptors),
		list
	) {
		if (handle == pTmpDescr->handle) {
			pWsmL2TableDescr = pTmpDescr;
			break;
		}
	}

	return pWsmL2TableDescr;
}

/*
#############################################################################
##
## L2 Table Pool
##
#############################################################################*/

/*----------------------------------------------------------------------------*/
static struct mcL2TablesDescr *allocateWsmL2TableContainer(
	struct mcInstance	*pInstance
) {
	int			ret = 0;
	struct mcL2TablesChunk	*pWsmL2TablesChunk = NULL;
	struct mcL2Page		*pWsmL2Page = NULL;
	struct mcL2TablesDescr  *pWsmL2TableDescr = NULL;
	struct page		*pPage;
	unsigned int		i = 0;

	do {
		/* allocate a WSM L2 descriptor */
		pWsmL2TableDescr  = kmalloc(sizeof(*pWsmL2TableDescr),
					    GFP_KERNEL);
		if (NULL == pWsmL2TableDescr) {
			ret = -ENOMEM;
			MCDRV_DBG_ERROR("out of memory\n");
			break;
		}
		/* clean */
		memset(pWsmL2TableDescr, 0, sizeof(*pWsmL2TableDescr));
		pWsmL2TableDescr->handle    = getMcKModUniqueId();
		pWsmL2TableDescr->pInstance = pInstance;

		/* add to global list. */
		initAndAddToList(
			&(pWsmL2TableDescr->list),
			&(mcDrvKModCtx.wsmL2Descriptors));

		/* walk though list to find free chunk. */
		list_for_each_entry(
			pWsmL2TablesChunk,
			&(mcDrvKModCtx.wsmL2Chunks),
			list
		) {
			for (i = 0; i < MC_DRV_KMOD_L2_TABLE_PER_PAGES; i++) {
				if (0 ==
					(pWsmL2TablesChunk->usageBitmap
					& (1U << i))
				) {
					/* found a chunk, pL2TablesChunk and i
						are set. */
				      pWsmL2Page = pWsmL2TablesChunk->kernelVirt;
				      break;
				}
			}
			if (NULL != pWsmL2Page)
				break;
		} /* end while */

		if (NULL == pWsmL2Page) {
			pWsmL2Page = (struct mcL2Page *)get_zeroed_page(GFP_KERNEL);
			if (NULL == pWsmL2Page) {
				ret = -ENOMEM;
				break;
			}

			/* Actually, locking is not necessary, because kernel
				memory is not supposed to get swapped out. But
				we play safe.... */
			pPage = virt_to_page(pWsmL2Page);
			SetPageReserved(pPage);

			/* allocate a descriptor */
			pWsmL2TablesChunk = kmalloc(sizeof(struct mcL2TablesChunk),
						    GFP_KERNEL);
			if (NULL == pWsmL2TablesChunk) {
				kfree(pWsmL2Page);
				ret = -ENOMEM;
				break;
			}
			/* initialize */
			memset(pWsmL2TablesChunk, 0,
			       sizeof(*pWsmL2TablesChunk));

			pWsmL2TablesChunk->kernelVirt	= pWsmL2Page;
			pWsmL2TablesChunk->pPage = pPage;
			pWsmL2TablesChunk->phys = (void *)virt_to_phys(pWsmL2Page);

			/* init add to list. */
			initAndAddToList(
				&(pWsmL2TablesChunk->list),
				&(mcDrvKModCtx.wsmL2Chunks));

			/* use first table */
			i = 0;
		}

		/* set chunk usage */
		pWsmL2TablesChunk->usageBitmap |= (1U << i);


		/* set chunk reference */
		pWsmL2TableDescr->pChunk    = pWsmL2TablesChunk;
		pWsmL2TableDescr->idx       = i;

		MCDRV_DBG_VERBOSE("allocateWsmL2TableContainer():chunkPhys=%p,idx=%d\n",
			pWsmL2TablesChunk->phys, i);

	} while (FALSE);

	if (0 != ret) {
		if (NULL != pWsmL2TableDescr) {
			/* remove from list */
			if (pWsmL2TablesChunk != NULL)
				list_del(&(pWsmL2TablesChunk->list));
			/* free memory */
			kfree(pWsmL2TableDescr);
			pWsmL2TableDescr = NULL;
		}
	}

	return pWsmL2TableDescr;
}

/*----------------------------------------------------------------------------*/
static void freeWsmL2TableContainer(
	struct mcL2TablesDescr *pL2TableDescr
)
{
	struct mcL2TablesChunk	*pWsmL2TablesChunk;
	unsigned int		idx;

	MCDRV_ASSERT(NULL != pL2TableDescr);

	pWsmL2TablesChunk = pL2TableDescr->pChunk;
	MCDRV_ASSERT(NULL != pWsmL2TablesChunk);

	/* clean usage flag */
	idx = pL2TableDescr->idx;
	MCDRV_ASSERT(MC_DRV_KMOD_L2_TABLE_PER_PAGES > idx);
	pWsmL2TablesChunk->usageBitmap &= ~(1U << idx);

	/* if nobody uses this chunk, we can release it. */
	if (0 == pWsmL2TablesChunk->usageBitmap) {
		MCDRV_ASSERT(NULL != pWsmL2TablesChunk->pPage);
		ClearPageReserved(pWsmL2TablesChunk->pPage);

		MCDRV_ASSERT(NULL != pWsmL2TablesChunk->kernelVirt);
		free_page((unsigned long)pWsmL2TablesChunk->kernelVirt);

		/* remove from list */
		list_del(&(pWsmL2TablesChunk->list));

		/* free memory */
		kfree(pWsmL2TablesChunk);
	}

	return;
}



/*----------------------------------------------------------------------------*/
/**
 * Create a L2 table in a WSM container that has been allocates previously.
 *
 * @param pTask          pointer to task owning WSM
 * @param wsmBuffer      user space WSM start
 * @param wsmLen         WSM length
 * @param pL2TableDescr  Pointer to L2 table details
 */
static int createWsmL2Table(
	struct task_struct	*pTask,
	void			*wsmBuffer,
	unsigned int		wsmLen,
	struct mcL2TablesDescr	*pL2TableDescr
)
{
	int		ret = 0;
	unsigned int	i, nrOfPages;
	void		*virtAddrPage;
	struct page	*pPage;
	struct l2Table	*pL2Table;
	struct page	**pL2TableAsArrayOfPointersToPage;

	/* pTask can be null when called from kernel space
	MCDRV_ASSERT(NULL != pTask); */
	MCDRV_ASSERT(NULL != wsmBuffer);
	MCDRV_ASSERT(0 != wsmLen);
	MCDRV_ASSERT(NULL != pL2TableDescr);

	MCDRV_DBG_VERBOSE("WSM addr=0x%p, len=0x%08x\n", wsmBuffer, wsmLen);

	/* Check if called from kernel space wsmBuffer is actually
	 * vmalloced or not */
	if (pTask == NULL && !is_vmalloc_addr(wsmBuffer)) {
		MCDRV_DBG_ERROR("WSM addr is not a vmalloc address");
		return -EINVAL;
	}

	pL2Table = getL2TableKernelVirt(pL2TableDescr);
	/* We use the memory for the L2 table to hold the pointer
	and convert them later. This works, as everything comes
	down to a 32 bit value. */
	pL2TableAsArrayOfPointersToPage = (struct page **)pL2Table;

	do {

		/* no size > 1Mib supported */
		if (wsmLen > SZ_1M) {
			MCDRV_DBG_ERROR("size > 1 MiB\n");
			ret = -EINVAL;
			break;
		}

		/* calculate page usage */
		virtAddrPage       = getPageStart(wsmBuffer);
		nrOfPages          = getNrOfPagesForBuffer(wsmBuffer, wsmLen);


		MCDRV_DBG_VERBOSE("virtAddr pageStart=0x%p,pages=%d\n",
				  virtAddrPage,
				  nrOfPages);

		/* L2 table can hold max 1MiB in 256 pages. */
		if (SZ_1M < (nrOfPages*PAGE_SIZE)) {
			MCDRV_DBG_ERROR("WSM paged exceed 1 MiB\n");
			ret = -EINVAL;
			break;
		}

		/* Request comes from user space */
		if (pTask != NULL) {
			/* lock user page in memory, so they do not get swapped
			* out.
			* REV axh:
			* Kernel 2.6.27 added a new get_user_pages_fast()
			* function, maybe it is called fast_gup() in some
			* versions.
			* handle user process doing a fork().
			* Child should not get things.
			* http://osdir.com/ml/linux-media/2009-07/msg00813.html
			* http://lwn.net/Articles/275808/ */

			ret = lockUserPages(
				      pTask,
				      virtAddrPage,
				      nrOfPages,
				      pL2TableAsArrayOfPointersToPage);
			if (0 != ret) {
				MCDRV_DBG_ERROR("lockUserPages() failed\n");
				break;
			}
		}
		/* Request comes from kernel space(vmalloc buffer) */
		else {
			void *uaddr = wsmBuffer;
			for (i = 0; i < nrOfPages; i++) {
				pPage = vmalloc_to_page(uaddr);
				if (!pPage) {
					MCDRV_DBG_ERROR(
						"vmalloc_to_Page()"
						" failed to map address\n");
					ret = -EINVAL;
					break;
				}
				get_page(pPage);
				/* Lock the page in memory, it can't be swapped
				 * out */
				SetPageReserved(pPage);
				pL2TableAsArrayOfPointersToPage[i] = pPage;
				uaddr += PAGE_SIZE;
			}
		}

		pL2TableDescr->nrOfPages = nrOfPages;
		pL2TableDescr->flags |= MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_APP;

		/* create L2 Table entries. "pL2Page->table" contains a list of
		page pointers here. For a proper cleanup we have to ensure that
		the following code either works and "pL2table" contains a valid
		L2 table - or fails and "pL2Page->table" contains the list of
		page pointers. Any mixed contents will make cleanup difficult.*/

		for (i = 0; i < nrOfPages; i++) {
			pte_t pte;
			pPage = pL2TableAsArrayOfPointersToPage[i];

			/* create L2 table entry, see ARM MMU docu for details
			about flags stored in the lowest 12 bits. As a side
			reference, the Article "ARM's multiply-mapped memory
			mess" found in the collection at at
			http://lwn.net/Articles/409032/ is also worth reading.*/
			pte = pageToL2Pte(pPage)
					| L2_FLAG_AP1 | L2_FLAG_AP0
					| L2_FLAG_C | L2_FLAG_B
					| L2_FLAG_SMALL | L2_FLAG_SMALL_XN
			/* Linux uses different mappings for SMP systems(the sharing flag
			 * is set for the pte. In order not to confuse things too much in
			 * Mobicore make sure the shared buffers have the same flags.
			 * This should also be done in SWD side
			 */
#ifdef CONFIG_SMP
					| L2_FLAG_S | L2_FLAG_SMALL_TEX0
#endif
				  ;

			pL2Table->tableEntries[i] = pte;
			MCDRV_DBG_VERBOSE("L2 entry %d:  0x%08x\n", i,
					  (unsigned int)(pte));
		}

		/* ensure rest of table is empty */
		while (i < 255)
			pL2Table->tableEntries[i++] = (pte_t)0;

	} while (FALSE);

	return ret;
}


/*----------------------------------------------------------------------------*/
/**
 * Remove a L2 table in a WSM container. Afterwards the container may be
 * released.
 *
 * @param pL2TableDescr    Pointer to L2 table details
 */

static void destroyWsmL2Table(
	struct mcL2TablesDescr	*pL2TableDescr
)
{
	unsigned int	i;
	struct l2Table	*pL2Table;

	MCDRV_ASSERT(NULL != pL2TableDescr);
	/* this should not happen, as we have no empty tables. */
	MCDRV_ASSERT(!isWsmL2InUse(pL2TableDescr));

	/* found the table, now release the resources. */
	MCDRV_DBG_VERBOSE("clear L2 table, physBase=%p, nrOfPages=%d\n",
			  getL2TablePhys(pL2TableDescr),
			  pL2TableDescr->nrOfPages);

	pL2Table = getL2TableKernelVirt(pL2TableDescr);

	/* release all locked user space pages */
	for (i = 0; i < pL2TableDescr->nrOfPages; i++) {
		/* convert physical entries from L2 table to page pointers */
		pte_t pte =
			getL2TableKernelVirt(pL2TableDescr)->tableEntries[i];
		struct page *pPage = l2PteToPage(pte);
		unlockPagefromWsmL2Table(pPage);
	}

	/* remember that all pages have been freed */
	pL2TableDescr->nrOfPages = 0;

	return;
}


/*
#############################################################################
##
## Helper functions
##
#############################################################################*/
/*----------------------------------------------------------------------------*/
#define FREE_FROM_SWD	TRUE
#define FREE_FROM_NWD	FALSE
static void freeWsmL2Table(
	struct mcL2TablesDescr	*pWsmL2TableDescr,
	unsigned int		isSwd
)
{
	if (isSwd) {
		pWsmL2TableDescr->flags &=
			~MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_MC;
	} else {
		pWsmL2TableDescr->flags &=
			~MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_APP;
		pWsmL2TableDescr->pInstance = NULL;
	}

	/* release if Nwd and Swd/MC do no longer use it. */
	if (isWsmL2InUse(pWsmL2TableDescr)) {
		MCDRV_DBG_WARN(
			"WSM L2 table still in use: physBase=%p, "
			"nrOfPages=%d\n",
			getL2TablePhys(pWsmL2TableDescr),
			pWsmL2TableDescr->nrOfPages);
	} else {
		destroyWsmL2Table(pWsmL2TableDescr);
		freeWsmL2TableContainer(pWsmL2TableDescr);

		list_del(&(pWsmL2TableDescr->list));

		kfree(pWsmL2TableDescr);
	}
	return;
}

/*----------------------------------------------------------------------------*/
/** Allocate L2 table and map buffer into it. That is, create respective table
	entries must hold Semaphore mcDrvKModCtx.wsmL2Sem */
static struct mcL2TablesDescr *newWsmL2Table(
	struct mcInstance	*pInstance,
	struct task_struct	*pTask,
	void			*wsmBuffer,
	unsigned int		wsmLen
) {
	int			ret = 0;
	struct mcL2TablesDescr	*pL2TableDescr;

	do {
		pL2TableDescr = allocateWsmL2TableContainer(pInstance);
		if (NULL == pL2TableDescr) {
			MCDRV_DBG_ERROR(
				"allocateWsmL2TableContainer() failed\n");
			break;
		}

		/* create the L2 page for the WSM */
		ret = createWsmL2Table(
			      pTask,
			      wsmBuffer,
			      wsmLen,
			      pL2TableDescr);
		if (0 != ret) {
			MCDRV_DBG_ERROR("createWsmL2Table() failed\n");
			freeWsmL2Table(pL2TableDescr, FREE_FROM_NWD);
			pL2TableDescr = NULL;
			break;
		}

	} while (FALSE);


	return pL2TableDescr;
}

/*
#############################################################################
##
## IoCtl handler
##
#############################################################################*/

/**
 * Map a virtual memory buffer structure to Mobicore
 * @param pInstance
 * @param addr		address of the buffer(NB it must be kernel virtual!)
 * @param len		buffer length
 * @param pHandle	pointer to handle
 * @param physWsmL2Table	pointer to physical L2 table(?)
 *
 * @return 0 if no error
 *
 */
/*----------------------------------------------------------------------------*/
int mobicore_map_vmem(
	struct mcInstance	*pInstance,
	void			*addr,
	uint32_t		len,
	uint32_t		*pHandle,
	void			**physWsmL2Table
)
{
	int ret = 0;
	struct mcL2TablesDescr *pWsmL2TableDescr = NULL;
	MCDRV_ASSERT(NULL != pInstance);

	MCDRV_DBG_VERBOSE("enter\n");

	do {
		if (0 == len) {
			MCDRV_DBG_ERROR("len=0 is not supported!\n");
			ret = -EINVAL;
			break;
		}

		/* try to get the semaphore */
		ret = down_interruptible(&(mcDrvKModCtx.wsmL2Sem));
		if (0 != ret) {
			MCDRV_DBG_ERROR("down_interruptible() failed with %d\n", ret);
			ret = -ERESTARTSYS;
			break;
		}

		do {
			pWsmL2TableDescr = newWsmL2Table(
						   pInstance,
						   NULL,
						   addr,
						   len);

			if (NULL == pWsmL2TableDescr) {
				MCDRV_DBG_ERROR("newWsmL2Table() failed\n");
				ret = -EINVAL;
				break;
			}

			/* set response */
			*pHandle = pWsmL2TableDescr->handle;
			*physWsmL2Table =
				(void *)getL2TablePhys(pWsmL2TableDescr);
			MCDRV_DBG_VERBOSE("handle: %d, phys=%p\n",
					  *pHandle,
					  (void *)(*physWsmL2Table));

		} while (FALSE);

		/* release semaphore */
		up(&(mcDrvKModCtx.wsmL2Sem));

	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return ret;
}
EXPORT_SYMBOL(mobicore_map_vmem);
/*----------------------------------------------------------------------------*/
/**
 *
 * @param pInstance
 * @param arg
 *
 * @return 0 if no error
 *
 */
static int handleIoCtlAppRegisterWsmL2(
	struct mcInstance		*pInstance,
	union mcIoCtlAppRegWsmL2Params	*pUserParams
)
{
	int				ret = 0;
	union mcIoCtlAppRegWsmL2Params	params;
	struct mcL2TablesDescr		*pWsmL2TableDescr = NULL;
	struct pid			*pPidStruct = NULL;
	struct task_struct		*pTask = current;
	MCDRV_ASSERT(NULL != pInstance);

	MCDRV_DBG_VERBOSE("enter\n");

	do {
		/* get use parameters */
		ret = copy_from_user(
			      &(params.in),
			      &(pUserParams->in),
			      sizeof(params.in));
		if (0 != ret) {
			MCDRV_DBG_ERROR("copy_from_user() failed\n");
			break;
		}

		/* daemon can do this for another task. */
		if (0 != params.in.pid) {
			MCDRV_DBG_ERROR("pid != 0 unsupported\n");
			ret = -EINVAL;
			break;
			/*
			MCDRV_DBG("PID=%d\n", params.in.pid);
			if (isCallerMcDaemon(pInstance))
			{
			    MCDRV_DBG_ERROR(
				"pid != 0 only allowed fore daemon\n");
			    ret = -EFAULT;
			    break;
			}

			Don't use find_vpid(), as this requires holding some
			locks. Better user find_get_pid(), which is take care of
			the locks internally.
			pPidStruct = find_get_pid(params.in.pid);
			if (NULL != pPidStruct)
			{
			    MCDRV_DBG_ERROR("find_get_pid() failed\n");
			    ret = -EFAULT;
			    break;
			}
			now we have a unique reference to another task. We must
			release this reference using put_pid() when we are done
			pTask = pid_task(pPidStruct, PIDTYPE_PID);
			if (NULL != pTask)
			{
			    MCDRV_DBG_ERROR("pid_task() failed\n");
			    ret = -EFAULT;
			    break;
			}
			*/
		}
		if (0 == params.in.len) {
			MCDRV_DBG_ERROR("len=0 is not supported!\n");
			ret = -EINVAL;
			break;
		}

		/* try to get the semaphore */
		ret = down_interruptible(&(mcDrvKModCtx.wsmL2Sem));
		if (0 != ret) {
			MCDRV_DBG_ERROR("down_interruptible() failed with %d\n", ret);
			ret = -ERESTARTSYS;
			break;
		}

		do {
			pWsmL2TableDescr = newWsmL2Table(
						   pInstance,
						   pTask,
						   (void *)(params.in.buffer),
						   params.in.len);

			if (NULL == pWsmL2TableDescr) {
				MCDRV_DBG_ERROR("newWsmL2Table() failed\n");
				ret = -EINVAL;
				break;
			}

			/* if the daemon does this, we set the MC lock */
			if (isCallerMcDaemon(pInstance))
				pWsmL2TableDescr->flags |=
					MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_MC;

			/* set response */
			memset(&params.out, 0, sizeof(params.out));
			params.out.handle = pWsmL2TableDescr->handle;
			/* TODO: return the physical address for daemon only,
				otherwise set NULL */
			/* if (isCallerMcDaemon(pInstance))... */
			params.out.physWsmL2Table =
				(uint32_t)getL2TablePhys(pWsmL2TableDescr);

			MCDRV_DBG_VERBOSE("handle: %d, phys=%p\n",
					  params.out.handle,
					  (void *)(params.out.physWsmL2Table));


			/* copy L2Table to user space */
			ret = copy_to_user(
				      &(pUserParams->out),
				      &(params.out),
				      sizeof(params.out));
			if (0 != ret) {
				MCDRV_DBG_ERROR("copy_to_user() failed\n");

				/* free the table again, as app does not know
					about anything. */
				if (isCallerMcDaemon(pInstance)) {
					pWsmL2TableDescr->flags &=
					~MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_MC;
				}
				freeWsmL2Table(pWsmL2TableDescr, FREE_FROM_NWD);
				pWsmL2TableDescr = NULL;
				break;
			}

		} while (FALSE);

		/* release semaphore */
		up(&(mcDrvKModCtx.wsmL2Sem));

	} while (FALSE);



	/* release PID struct reference */
	if (NULL != pPidStruct)
		put_pid(pPidStruct);


	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return ret;
}


/*----------------------------------------------------------------------------*/
/**
 * Unmap a virtual memory buffer from mobicore
 * @param pInstance
 * @param handle
 *
 * @return 0 if no error
 *
 */
int mobicore_unmap_vmem(
	struct mcInstance	*pInstance,
	uint32_t		handle
)
{
	int ret = 0;
	struct mcL2TablesDescr *pWsmL2TableDescr = NULL;

	MCDRV_ASSERT(NULL != pInstance);
	MCDRV_DBG_VERBOSE("enter\n");

	do {
		/* try to get the semaphore */
		ret = down_interruptible(&(mcDrvKModCtx.wsmL2Sem));
		if (0 != ret) {
			MCDRV_DBG_ERROR("processOpenSession() failed with %d\n", ret);
			ret = -ERESTARTSYS;
			break;
		}

		do {
			pWsmL2TableDescr = findWsmL2ByHandle(handle);
			if (NULL == pWsmL2TableDescr) {
				ret = -EINVAL;
				MCDRV_DBG_ERROR("entry not found\n");
				break;
			}

			if (pInstance != pWsmL2TableDescr->pInstance) {
				ret = -EINVAL;
				MCDRV_DBG_ERROR("instance does no own it\n");
				break;
			}

			/* free table (if no further locks exist) */
			freeWsmL2Table(pWsmL2TableDescr, FREE_FROM_NWD);
			pWsmL2TableDescr = NULL;
			/* there are no out parameters */
		} while (FALSE);
		/* release semaphore */
		up(&(mcDrvKModCtx.wsmL2Sem));

	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return ret;
}
EXPORT_SYMBOL(mobicore_unmap_vmem);
/*----------------------------------------------------------------------------*/
/**
 *
 * @param pInstance
 * @param arg
 *
 * @return 0 if no error
 *
 */
static int handleIoCtlAppUnregisterWsmL2(
	struct mcInstance			*pInstance,
	struct mcIoCtlAppUnregWsmL2Params	*pUserParams
)
{
	int					ret = 0;
	struct mcIoCtlAppUnregWsmL2Params	params;
	struct mcL2TablesDescr			*pWsmL2TableDescr = NULL;

	MCDRV_ASSERT(NULL != pInstance);
	MCDRV_DBG_VERBOSE("enter\n");

	do {
		ret = copy_from_user(
			      &(params.in),
			      &(pUserParams->in),
			      sizeof(params.in));

		if (0 != ret) {
			MCDRV_DBG_ERROR("copy_from_user\n");
			break;
		}

		/* try to get the semaphore */
		ret = down_interruptible(&(mcDrvKModCtx.wsmL2Sem));
		if (0 != ret) {
			MCDRV_DBG_ERROR("down_interruptible() failed with %d\n", ret);
			ret = -ERESTARTSYS;
			break;
		}

		do {
			/* daemon can do this for another task. */
			if (0 != params.in.pid) {
				MCDRV_DBG_ERROR("pid != 0 unsupported\n");
				ret = -EINVAL;
				break;
			}

			pWsmL2TableDescr = findWsmL2ByHandle(params.in.handle);
			if (NULL == pWsmL2TableDescr) {
				ret = -EINVAL;
				MCDRV_DBG_ERROR("entry not found\n");
				break;
			}

			if (isCallerMcDaemon(pInstance)) {
				/* if daemon does this, we have to release the
					MobiCore lock. */
				pWsmL2TableDescr->flags &=
					~MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_MC;
			} else if (pInstance != pWsmL2TableDescr->pInstance) {
				ret = -EINVAL;
				MCDRV_DBG_ERROR("instance does no own it\n");
				break;
			}

			/* free table (if no further locks exist) */
			freeWsmL2Table(pWsmL2TableDescr, FREE_FROM_NWD);
			pWsmL2TableDescr = NULL;

			/* there are no out parameters */

		} while (FALSE);

		/* release semaphore */
		up(&(mcDrvKModCtx.wsmL2Sem));

	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return ret;
}


/*----------------------------------------------------------------------------*/
static int handleIoCtlDaemonLockWsmL2(
	struct mcInstance			*pInstance,
	struct mcIoCtlDaemonLockWsmL2Params	*pUserParams
)
{
	int					ret = 0;
	struct mcIoCtlDaemonLockWsmL2Params	params;
	struct mcL2TablesDescr			*pWsmL2TableDescr = NULL;

	MCDRV_ASSERT(NULL != pInstance);
	MCDRV_DBG_VERBOSE("enter\n");

	do {
		if (!isCallerMcDaemon(pInstance)) {
			MCDRV_DBG_ERROR("caller not MobiCore Daemon\n");
			ret = -EFAULT;
			break;
		}

		ret = copy_from_user(
			      &(params.in),
			      &(pUserParams->in),
			      sizeof(params.in));

		if (0 != ret) {
			MCDRV_DBG_ERROR("copy_from_user\n");
			break;
		}
		/* try to get the semaphore */
		ret = down_interruptible(&(mcDrvKModCtx.wsmL2Sem));
		if (0 != ret) {
			MCDRV_DBG_ERROR("down_interruptible() failed with %d\n", ret);
			ret = -ERESTARTSYS;
			break;
		}

		do {
			pWsmL2TableDescr = findWsmL2ByHandle(params.in.handle);
			if (NULL == pWsmL2TableDescr) {
				ret = -EINVAL;
				MCDRV_DBG_ERROR("entry not found\n");
				break;
			}
			if (pInstance != pWsmL2TableDescr->pInstance) {
				ret = -EINVAL;
				MCDRV_DBG_ERROR("instance does no own it\n");
				break;
			}

			/* lock entry */
			if (0 != (pWsmL2TableDescr->flags &
				  MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_MC)) {
				MCDRV_DBG_WARN("entry already locked\n");
			}
			pWsmL2TableDescr->flags |=
				MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_MC;

			/* prepare response */
			memset(&(params.out), 0, sizeof(params.out));
			params.out.physWsmL2Table =
				(uint32_t)getL2TablePhys(pWsmL2TableDescr);

			/* copy to user space */
			ret = copy_to_user(
				      &(pUserParams->out),
				      &(params.out),
				      sizeof(params.out));
			if (0 != ret) {
				MCDRV_DBG_ERROR("copy_to_user() failed\n");

				/* undo, as userspace did not get it. */
				pWsmL2TableDescr->flags |=
					MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_MC;
				break;
			}

		} while (FALSE);

		/* release semaphore */
		up(&(mcDrvKModCtx.wsmL2Sem));

	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return ret;
}


/*----------------------------------------------------------------------------*/
static int handleIoCtlDaemonUnlockWsmL2(
	struct mcInstance			*pInstance,
	struct mcIoCtlDaemonUnlockWsmL2Params	*pUserParams
)
{
	int					ret = 0;
	struct mcIoCtlDaemonUnlockWsmL2Params	params;
	struct mcL2TablesDescr			*pWsmL2TableDescr = NULL;

	MCDRV_ASSERT(NULL != pInstance);
	MCDRV_DBG_VERBOSE("enter\n");

	do {
		if (!isCallerMcDaemon(pInstance)) {
			MCDRV_DBG_ERROR("caller not MobiCore Daemon\n");
			ret = -EFAULT;
			break;
		}

		ret = copy_from_user(
			      &(params.in),
			      &(pUserParams->in),
			      sizeof(params.in));

		if (0 != ret) {
			MCDRV_DBG_ERROR("copy_from_user\n");
			break;
		}
		/* try to get the semaphore */
		ret = down_interruptible(&(mcDrvKModCtx.wsmL2Sem));
		if (0 != ret) {
			MCDRV_DBG_ERROR("down_interruptible() failed with %d\n", ret);
			ret = -ERESTARTSYS;
			break;
		}

		do {
			pWsmL2TableDescr = findWsmL2ByHandle(params.in.handle);
			if (NULL == pWsmL2TableDescr) {
				ret = -EINVAL;
				MCDRV_DBG_ERROR("entry not found\n");
				break;
			}
			if (pInstance != pWsmL2TableDescr->pInstance) {
				ret = -EINVAL;
				MCDRV_DBG_ERROR("instance does no own it\n");
				break;
			}

			/* lock entry */
			if (0 == (pWsmL2TableDescr->flags &
				  MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_MC)) {
				MCDRV_DBG_WARN("entry is not locked locked\n");
			}

			/* free table (if no further locks exist) */
			freeWsmL2Table(pWsmL2TableDescr, FREE_FROM_SWD);
			pWsmL2TableDescr = NULL;

			/* there are no out parameters */

		} while (FALSE);

	} while (FALSE);


	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return ret;
}

/*----------------------------------------------------------------------------*/
static inline void freeContinguousPages(
	void		*addr,
	unsigned int	size
)
{
	/* clears the reserved bit of each page and frees this page */
	struct page *pPage = virt_to_page(addr);
	int i;
	for (i = 0; i < size; i++) {
		MCDRV_DBG_VERBOSE("free page at 0x%p\n", pPage);
		ClearPageReserved(pPage);
		pPage++;
	}
	/* REV luh: see man kmalloc */
	free_pages((unsigned long)addr, sizeToOrder(size));
}

/*----------------------------------------------------------------------------*/
/**
 * Free a WSM buffer allocated with mobicore_allocate_wsm
 * @param pInstance
 * @param handle		handle of the buffer
 *
 * @return 0 if no error
 *
 */
int mobicore_free(
	struct mcInstance	*pInstance,
	uint32_t		handle
)
{
	int			ret = 0;
	struct mc_tuple		*pTuple;
	unsigned int		i;

	do {
		/* search for the given address in the tuple list */
		for (i = 0; i < MC_DRV_KMOD_TUPLE_NR; i++) {
			pTuple = &(pInstance->tuple[i]);
			if (pTuple->handle == handle)
				break;
		}
		if (MC_DRV_KMOD_TUPLE_NR == i) {
			MCDRV_DBG_ERROR("tuple not found\n");
			ret = -EFAULT;
			break;
		}

		MCDRV_DBG_VERBOSE("physAddr=0x%p, virtAddr=0x%p\n",
				  pTuple->physAddr, pTuple->virtKernelAddr);

		freeContinguousPages(pTuple->virtKernelAddr, pTuple->numPages);

		memset(pTuple, 0, sizeof(*pTuple));

		/* there are no out parameters */

	} while (FALSE);


	return ret;
}
EXPORT_SYMBOL(mobicore_free);
/*----------------------------------------------------------------------------*/

/**
 *
 * @param pInstance
 * @param arg
 *
 * @return 0 if no error
 *
 */
static int handleIoCtlFree(
	struct mcInstance		*pInstance,
	union mcIoCtltoFreeParams	*pUserParams
)
{
	int				ret = 0;
	union mcIoCtltoFreeParams	params;


	MCDRV_ASSERT(NULL != pInstance);
	MCDRV_DBG_VERBOSE("enter\n");

	do {
		ret = copy_from_user(
			      &(params.in),
			      &(pUserParams->in),
			      sizeof(params.in));

		if (0 != ret) {
			MCDRV_DBG_ERROR("copy_from_user\n");
			break;
		}

		/* daemon can do this for another task. */
		if (0 != params.in.pid) {
			MCDRV_DBG_ERROR("pid != 0 unsupported\n");
			ret = -EINVAL;
			break;
		}

		ret = mobicore_free(pInstance, params.in.handle);

		/* there are no out parameters */

	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return ret;

}


/*----------------------------------------------------------------------------*/
/**
 *
 * @param pInstance
 * @param arg
 *
 * @return 0 if no error
 *
 */
static int handleIoCtlInfo(
	struct mcInstance	*pInstance,
	union mcIoCtlInfoParams	*pUserParams
)
{
	int			ret = 0;
	union mcIoCtlInfoParams	params;
	union mcFcInfo		fcInfo;


	MCDRV_ASSERT(NULL != pInstance);
	MCDRV_DBG_VERBOSE("enter\n");

	do {
		/* only the MobiCore Daemon is allowed to call this function */
		if (!isCallerMcDaemon(pInstance)) {
			MCDRV_DBG_ERROR("caller not MobiCore Daemon\n");
			ret = -EFAULT;
			break;
		}

		ret = copy_from_user(
			      &(params.in),
			      &(pUserParams->in),
			      sizeof(params.in));

		if (0 != ret) {
			MCDRV_DBG_ERROR("copy_from_user\n");
			break;
		}


		memset(&fcInfo, 0, sizeof(fcInfo));
		fcInfo.asIn.cmd       = MC_FC_INFO;
		fcInfo.asIn.extInfoId = params.in.extInfoId;

		MCDRV_DBG(
			"fcInfo in cmd=0x%08x, extInfoid=0x%08x "
			"rfu=(0x%08x, 0x%08x)\n",
			fcInfo.asIn.cmd,
			fcInfo.asIn.extInfoId,
			fcInfo.asIn.rfu[0],
			fcInfo.asIn.rfu[1]);

		mcFastCall(&(fcInfo.asGeneric));

		MCDRV_DBG(
			"fcInfo out resp=0x%08x, ret=0x%08x "
			"state=0x%08x, extInfo=0x%08x\n",
			fcInfo.asOut.resp,
			fcInfo.asOut.ret,
			fcInfo.asOut.state,
			fcInfo.asOut.extInfo);

		ret = convertFcRet(fcInfo.asOut.ret);
		if (0 != ret)
			break;

		memset(&(params.out), 0, sizeof(params.out));
		params.out.state  = fcInfo.asOut.state;
		params.out.extInfo = fcInfo.asOut.extInfo;

		ret = copy_to_user(
			      &(pUserParams->out),
			      &(params.out),
			      sizeof(params.out));

		if (0 != ret) {
			MCDRV_DBG_ERROR("copy_to_user\n");
			break;
		}
	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return ret;
}

/*----------------------------------------------------------------------------*/
/**
 *
 * @param pInstance
 * @param arg
 *
 * @return 0 if no error
 *
 */
static int handleIoCtlYield(
	struct mcInstance	*pInstance
)
{
	int			ret = 0;
	union mcFcSYield	fcSYield;

	MCDRV_ASSERT(NULL != pInstance);

	/* Can't allow Yields while preparing to sleep */
    if(mcpBuffer->mcFlags.sleepMode.SleepReq == MC_FLAG_REQ_TO_SLEEP)
        return ret;

	/* avoid putting debug output here, as we do this very often */
	/* MCDRV_DBG("enter\n"); */

	do {
		/* only the MobiCore Daemon is allowed to call this function */
		if (!isCallerMcDaemon(pInstance)) {
			MCDRV_DBG_ERROR("caller not MobiCore Daemon\n");
			ret = -EFAULT;
			break;
		}

		memset(&fcSYield, 0, sizeof(fcSYield));
		fcSYield.asIn.cmd = MC_SMC_N_YIELD;
		mcFastCall(&(fcSYield.asGeneric));
		ret = convertFcRet(fcSYield.asOut.ret);
		if (0 != ret)
			break;

	} while (FALSE);

	/* MCDRV_DBG("exit with %d/0x%08X\n", ret, ret); */

	return ret;
}

/*----------------------------------------------------------------------------*/
/**
 * handle ioctl and call common notify
 *
 * @param pInstance
 * @param arg
 *
 * @return 0 if no error
 *
 */
static int handleIoCtlNSIQ(
	struct mcInstance	*pInstance,
	unsigned long		arg
)
{
	int		ret = 0;

	MCDRV_ASSERT(NULL != pInstance);

	/* Can't allow Yields while preparing to sleep */
    if(mcpBuffer->mcFlags.sleepMode.SleepReq == MC_FLAG_REQ_TO_SLEEP)
        return ret;

	/* avoid putting debug output here, as we do this very often */
	 //MCDRV_DBG("enter\n");
	/* only the MobiCore Daemon is allowed to call this function */
	if (!isCallerMcDaemon(pInstance)) {
		MCDRV_DBG_ERROR("caller not MobiCore Daemon\n");
		return -EFAULT;
	}

	do {
		union mcFcNSIQ fcNSIQ;
		memset(&fcNSIQ, 0, sizeof(fcNSIQ));
		fcNSIQ.asIn.cmd = MC_SMC_N_SIQ;
		mcFastCall(&(fcNSIQ.asGeneric));
		ret = convertFcRet(fcNSIQ.asOut.ret);
		if (0 != ret)
			break;
	} while (FALSE);

	/* MCDRV_DBG("exit with %d/0x%08X\n", ret, ret); */

	return ret;
}

/*----------------------------------------------------------------------------*/
/**
 *
 * @param pInstance
 * @param arg
 *
 * @return 0 if no error
 *
 */
static int handleIoCtlDumpStatus(
	struct mcInstance	*pInstance,
	unsigned long		arg
)
{
	int		ret = 0;
	int		i = 0;
	union mcFcInfo	fcInfo;

	MCDRV_ASSERT(NULL != pInstance);
	MCDRV_DBG_VERBOSE("enter\n");

	do {
		/* anybody with root access can do this. */
		if (!isUserlandCallerPrivileged()) {
			MCDRV_DBG_ERROR("caller must have root privileges\n");
			ret = -EFAULT;
			break;
		}

		/* loop extInfo */
		while (TRUE) {
			memset(&fcInfo, 0, sizeof(fcInfo));
			fcInfo.asIn.cmd       = MC_FC_INFO;
			fcInfo.asIn.extInfoId = i;

			MCDRV_DBG(
				"fcInfo in cmd=0x%08x, extInfoid=0x%08x "
				"rfu=(0x%08x, 0x%08x)\n",
				fcInfo.asIn.cmd,
				fcInfo.asIn.extInfoId,
				fcInfo.asIn.rfu[0],
				fcInfo.asIn.rfu[1]);

			mcFastCall(&(fcInfo.asGeneric));

			MCDRV_DBG(
				"fcInfo out resp=0x%08x, ret=0x%08x "
				"state=0x%08x, extInfo=0x%08x\n",
				fcInfo.asOut.resp,
				fcInfo.asOut.ret,
				fcInfo.asOut.state,
				fcInfo.asOut.extInfo);

			ret = convertFcRet(fcInfo.asOut.ret);
			if (0 != ret)
				break;

			MCDRV_DBG("state=%08X, idx=%02d: extInfo=%08X\n",
				  fcInfo.asOut.state, i, fcInfo.asOut.extInfo);
			i++;
		};

		if (0 != ret)
			break;


	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return ret;
}

/*----------------------------------------------------------------------------*/
/**
 *
 * @param pInstance
 * @param arg
 *
 * @return 0 if no error
 *
 */
static int handleIoCtlInit(
	struct mcInstance	*pInstance,
	union mcIoCtlInitParams	*pUserParams
)
{
	int			ret = 0;
	union mcIoCtlInitParams	params;
	union mcFcInit		fcInit;

	MCDRV_ASSERT(NULL != pInstance);
	MCDRV_DBG_VERBOSE("enter\n");

	do {
		/* only the MobiCore Daemon is allowed to call this function */
		if (!isCallerMcDaemon(pInstance)) {
			MCDRV_DBG_ERROR("caller not MobiCore Daemon\n");
			ret = -EFAULT;
			break;
		}

		ret = copy_from_user(
			      &(params.in),
			      &(pUserParams->in),
			      sizeof(params.in));
		if (0 != ret) {
			MCDRV_DBG_ERROR("copy_from_user failed\n");
			break;
		}

		memset(&fcInit, 0, sizeof(fcInit));

		fcInit.asIn.cmd    = MC_FC_INIT;
		/* base address of mci buffer 4KB aligned */
		fcInit.asIn.base   = (uint32_t)params.in.base;
		/* notification buffer start/length [16:16] [start, length] */
		fcInit.asIn.nqInfo  = (params.in.nqOffset << 16)
				      | (params.in.nqLength & 0xFFFF);
		/* mcp buffer start/length [16:16] [start, length] */
		fcInit.asIn.mcpInfo = (params.in.mcpOffset << 16)
				      | (params.in.mcpLength & 0xFFFF);

		/* Set KMOD notification queue to start of MCI
			mciInfo was already set up in mmap */
		if (!mciBase) {
			MCDRV_DBG_ERROR("No MCI set yet.\n");
			return -EFAULT;
		} else {
			/* Save the information for later usage in the module */
			mcpBuffer = (void*)mciBase + params.in.mcpOffset;
		}
		MCDRV_DBG("in cmd=0x%08x, base=0x%08x, "
			  "nqInfo=0x%08x, mcpInfo=0x%08x\n",
			  fcInit.asIn.cmd,
			  fcInit.asIn.base,
			  fcInit.asIn.nqInfo,
			  fcInit.asIn.mcpInfo);

		mcFastCall(&(fcInit.asGeneric));

		MCDRV_DBG("out cmd=0x%08x, ret=0x%08x rfu=(0x%08x, 0x%08x)\n",
			  fcInit.asOut.resp,
			  fcInit.asOut.ret,
			  fcInit.asOut.rfu[0],
			  fcInit.asOut.rfu[1]);

		MCDRV_DBG("MCP addr=%p IDLE=%d\n", mcpBuffer, mcpBuffer->mcFlags.schedule);

		ret = convertFcRet(fcInit.asOut.ret);
		if (0 != ret)
			break;

		/* no ioctl response parameters */

	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return ret;
}

/*----------------------------------------------------------------------------*/
/**
 *
 * @param pInstance
 * @param arg
 *
 * @return 0 if no error
 *
 */
static int handleIoCtlFcExecute(
	struct mcInstance		*pInstance,
	union mcIoCtlFcExecuteParams	*pUserParams
)
{
	int				ret = 0;
	union mcIoCtlFcExecuteParams	params;
	union fcGeneric			fcParams;

	MCDRV_ASSERT(NULL != pInstance);
	MCDRV_DBG_VERBOSE("enter\n");

	do {
		/* only the MobiCore Daemon is allowed to call this function */
		if (!isCallerMcDaemon(pInstance)) {
			MCDRV_DBG_ERROR("caller not MobiCore Daemon\n");
			ret = -EFAULT;
			break;
		}

		ret = copy_from_user(
			      &(params.in),
			      &(pUserParams->in),
			      sizeof(params.in));
		if (0 != ret) {
			MCDRV_DBG_ERROR("copy_from_user failed\n");
			break;
		}

		fcParams.asIn.cmd = -4;/*FC_EXECUTE */
		fcParams.asIn.param[0] = params.in.physStartAddr;
		fcParams.asIn.param[1] = params.in.length;
		fcParams.asIn.param[2] = 0;

		MCDRV_DBG("in cmd=0x%08x, startAddr=0x%08x, length=0x%08x\n",
			  fcParams.asIn.cmd,
			  fcParams.asIn.param[0],
			  fcParams.asIn.param[1]);

		mcFastCall(&fcParams);

		MCDRV_DBG("out cmd=0x%08x, ret=0x%08x rfu=(0x%08x, 0x%08x)\n",
			  fcParams.asOut.resp,
			  fcParams.asOut.ret,
			  fcParams.asOut.param[0],
			  fcParams.asOut.param[1]);

		ret = convertFcRet(fcParams.asOut.ret);
		if (0 != ret)
			break;

		/* no ioctl response parameters */

	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return ret;
}

/*----------------------------------------------------------------------------*/
/**
 *
 * @param pInstance
 * @param arg
 *
 * @return 0 if no error
 *
 */
#define MC_MAKE_VERSION(major, minor) \
		(((major & 0x0000ffff) << 16) | (minor & 0x0000ffff))

static int handleIoCtlGetVersion(
    struct mcInstance       *pInstance,
    struct mcIoCtlGetVersionParams    *pUserParams
)
{
	int ret = 0;
	struct mcIoCtlGetVersionParams  params = {
		{
			MC_MAKE_VERSION(MCDRVMODULEAPI_VERSION_MAJOR,
							MCDRVMODULEAPI_VERSION_MINOR)
		}
	};

	MCDRV_ASSERT(NULL != pInstance);
	MCDRV_DBG_VERBOSE("enter\n");

	do {
		MCDRV_DBG("mcDrvModuleApi version is %i.%i\n",
				MCDRVMODULEAPI_VERSION_MAJOR,
				MCDRVMODULEAPI_VERSION_MINOR);

		/* no ioctl response parameters */
		ret = copy_to_user(
					&(pUserParams->out),
					&(params.out),
					sizeof(params.out));
		if (ret != 0)
			MCDRV_DBG_ERROR("copy_to_user() failed\n");

	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return ret;
}

/*----------------------------------------------------------------------------*/
/**
 * This function will be called from user space as ioctl(...).
 * @param pInode   pointer to inode
 * @param pFile    pointer to file
 * @param cmd      command
 * @param arg
 *
 * @return int 0 for OK and an errno in case of error
 */
static long mcKernelModule_ioctl(
	struct file	*pFile,
	unsigned int	cmd,
	unsigned long	arg
)
{
	int			ret;
	struct mcInstance	*pInstance = getInstance(pFile);

	MCDRV_ASSERT(NULL != pInstance);

	switch (cmd) {
	/*--------------------------------------------------------------------*/
	case MC_DRV_KMOD_IOCTL_DUMP_STATUS:
		ret = handleIoCtlDumpStatus(
			      pInstance,
			      arg);
		break;

	/*--------------------------------------------------------------------*/
	case MC_DRV_KMOD_IOCTL_FC_INIT:
		ret = handleIoCtlInit(
			      pInstance,
			      (union mcIoCtlInitParams *)arg);
		break;
	/*--------------------------------------------------------------------*/
	case MC_DRV_KMOD_IOCTL_FC_INFO:
		ret = handleIoCtlInfo(
			      pInstance,
			      (union mcIoCtlInfoParams *)arg);
		break;

	/*--------------------------------------------------------------------*/
	case MC_DRV_KMOD_IOCTL_FC_YIELD:
		ret = handleIoCtlYield(
			      pInstance);
		break;

	/*--------------------------------------------------------------------*/
	case MC_DRV_KMOD_IOCTL_FC_NSIQ:
		ret = handleIoCtlNSIQ(
			      pInstance,
			      arg);
		break;

	/*--------------------------------------------------------------------*/
	case MC_DRV_KMOD_IOCTL_DAEMON_LOCK_WSM_L2:
		ret = handleIoCtlDaemonLockWsmL2(
			      pInstance,
			      (struct mcIoCtlDaemonLockWsmL2Params *)arg);
		break;

	/*--------------------------------------------------------------------*/
	case MC_DRV_KMOD_IOCTL_DAEMON_UNLOCK_WSM_L2:
		ret = handleIoCtlDaemonUnlockWsmL2(
			      pInstance,
			      (struct mcIoCtlDaemonUnlockWsmL2Params *)arg);
		break;

	/*--------------------------------------------------------------------*/
	case MC_DRV_KMOD_IOCTL_FREE:
		/* called by ClientLib */
		ret = handleIoCtlFree(
			      pInstance,
			      (union mcIoCtltoFreeParams *)arg);
		break;

	/*--------------------------------------------------------------------*/
	case MC_DRV_KMOD_IOCTL_APP_REGISTER_WSM_L2:
		/* called by ClientLib */
		ret = handleIoCtlAppRegisterWsmL2(
				pInstance,
				(union mcIoCtlAppRegWsmL2Params *)arg);
		break;

	/*--------------------------------------------------------------------*/
	case MC_DRV_KMOD_IOCTL_APP_UNREGISTER_WSM_L2:
		/* called by ClientLib */
		ret = handleIoCtlAppUnregisterWsmL2(
				pInstance,
				(struct mcIoCtlAppUnregWsmL2Params *)arg);
		break;

	/*--------------------------------------------------------------------*/
	case MC_DRV_KMOD_IOCTL_FC_EXECUTE:
		ret = handleIoCtlFcExecute(
				pInstance,
				(union mcIoCtlFcExecuteParams *)arg);
		break;

	/*--------------------------------------------------------------------*/
	case MC_DRV_KMOD_IOCTL_GET_VERSION:
		ret = handleIoCtlGetVersion(
					pInstance,
					(struct mcIoCtlGetVersionParams *)arg);
		break;

	/*--------------------------------------------------------------------*/
	default:
		MCDRV_DBG_ERROR("unsupported cmd=%d\n", cmd);
		ret = -EFAULT;
		break;

	} /* end switch(cmd) */

#ifdef MC_MEM_TRACES
	mobicore_read_log();
#endif

	return (int)ret;
}


/*----------------------------------------------------------------------------*/
/**
 * This function will be called from user space as read(...).
 * The read function is blocking until a interrupt occurs. In that case the
 * event counter is copied into user space and the function is finished.
 * @param *pFile
 * @param *pBuffer  buffer where to copy to(userspace)
 * @param count     number of requested data
 * @param *pPos     not used
 * @return ssize_t  ok case: number of copied data
 *                  error case: return errno
 */
static ssize_t mcKernelModule_read(
	struct file	*pFile,
	char		*pBuffer,
	size_t		bufferLen,
	loff_t		*pPos
)
{
	int ret = 0, ssiqCounter;
	size_t retLen = 0;
	struct mcInstance *pInstance = getInstance(pFile);

	MCDRV_ASSERT(NULL != pInstance);

	/* avoid debug output on non-error, because this is call quite often */
	MCDRV_DBG_VERBOSE("enter\n");

	do {
		/* only the MobiCore Daemon is allowed to call this function */
		if (!isCallerMcDaemon(pInstance)) {
			MCDRV_DBG_ERROR("caller not MobiCore Daemon\n");
			ret = -EFAULT;
			break;
		}

		if (bufferLen < sizeof(unsigned int)) {
			MCDRV_DBG_ERROR("invalid length\n");
			ret = (ssize_t)(-EINVAL);
			break;
		}

		for (;;) {
			if (down_interruptible(&mcDrvKModCtx.daemonCtx.sem)) {
				MCDRV_DBG_VERBOSE("read interrupted\n");
				ret = (ssize_t)-ERESTARTSYS;
				break;
			}

			ssiqCounter = atomic_read(&(mcDrvKModCtx.ssiqCtx.counter));
			MCDRV_DBG_VERBOSE("ssiqCounter=%i, ctx.counter=%i\n",
						ssiqCounter,
						mcDrvKModCtx.daemonCtx.ssiqCounter);

			if (ssiqCounter != mcDrvKModCtx.daemonCtx.ssiqCounter) {
				/* read data and exit loop without
					error */
				mcDrvKModCtx.daemonCtx.ssiqCounter =
					ssiqCounter;
				ret = 0;
				break;
			}

			/* end loop if non-blocking */
			if (0 != (pFile->f_flags & O_NONBLOCK)) {
				MCDRV_DBG_ERROR("non-blocking read\n");
				ret = (ssize_t)(-EAGAIN);
				break;
			}

			if (0 != signal_pending(current)) {
				MCDRV_DBG_VERBOSE("received signal.\n");
				ret = (ssize_t)(-ERESTARTSYS);
				break;
			}

		}

		/* we are here if an event occurred or we had an
			error.*/
		if (0 != ret)
			break;

		/* read data and exit loop */
		ret = copy_to_user(
				  pBuffer,
				  &(mcDrvKModCtx.daemonCtx.ssiqCounter),
				  sizeof(unsigned int));


		if (0 != ret) {
			MCDRV_DBG_ERROR("copy_to_user failed\n");
			ret = (ssize_t)(-EFAULT);
			break;
		}

		retLen = sizeof(s32);

	} while (FALSE);

	/* avoid debug on non-error. */
	if (0 == ret)
		ret = (size_t)retLen;
	else
		MCDRV_DBG("exit with %d/0x%08X\n", ret, ret);

	return (ssize_t)ret;
}

/*----------------------------------------------------------------------------*/
/**
 * Allocate WSM for given instance
 *
 * @param pInstance		instance
 * @param requestedSize		size of the WSM
 * @param pHandle		pointer where the handle will be saved
 * @param pKernelVirtAddr	pointer for the kernel virtual address
 * @param pPhysAddr		pointer for the physical address
 *
 * @return error code or 0 for success
 */
int mobicore_allocate_wsm(
	struct mcInstance	*pInstance,
	unsigned long		requestedSize,
	uint32_t		*pHandle,
	void			**pKernelVirtAddr,
	void			**pPhysAddr
)
{
	unsigned int	i;
	unsigned int	order;
	unsigned long	allocatedSize;
	int		ret = 0;
	struct mc_tuple	*pTuple = 0;
	void		*kernelVirtAddr;
	void		*physAddr;

	MCDRV_ASSERT(NULL != pInstance);
	MCDRV_DBG("%s (size=%ld)\n", __func__, requestedSize);

	order = sizeToOrder(requestedSize);
	if (INVALID_ORDER == order) {
		MCDRV_DBG_ERROR(
			"size to order converting failed for size %ld\n",
			requestedSize);
		return INVALID_ORDER;
	}

	allocatedSize = (1<<order)*PAGE_SIZE;

	MCDRV_DBG("size %ld -> order %d --> %ld (2^n pages)\n",
		  requestedSize, order, allocatedSize);

	do {
		/* Usual Wsm request, allocate tuple. */
		/* search for a free entry in the tuple list
		 * REV axh: serialize this over multiple instances. */
		for (i = 0; i < MC_DRV_KMOD_TUPLE_NR; i++) {
			pTuple = &(pInstance->tuple[i]);
			if (0 == pTuple->handle) {
				pTuple->handle = getMcKModUniqueId();
				break;
			}
		}
		if (MC_DRV_KMOD_TUPLE_NR == i) {
			MCDRV_DBG_ERROR("no free tuple\n");
			ret = -EFAULT;
			break;
		}

		/* Common code for all allocation paths */
		kernelVirtAddr = (void *)__get_free_pages(GFP_USER | __GFP_COMP, order);
		if (NULL == kernelVirtAddr) {
			MCDRV_DBG_ERROR("get_free_pages failed\n");
			ret = -ENOMEM;
			break;
		}

		/* Get physical address to instance data */
		physAddr = (void *)virt_to_phys(kernelVirtAddr);
		/* TODO: check for INVALID_ADDRESS? */

		MCDRV_DBG(
			"allocated phys=0x%p - 0x%p, "
			"size=%ld, kernelVirt=0x%p, handle=%d\n",
			physAddr,
			(void *)((unsigned int)physAddr+allocatedSize),
				allocatedSize, kernelVirtAddr, pTuple->handle);

		/* Usual Wsm request, allocate tuple.
		 *		Also, we never free a persistent Tci */
		pTuple->physAddr       = physAddr;
		pTuple->virtKernelAddr = kernelVirtAddr;
		pTuple->virtUserAddr   = kernelVirtAddr;
		pTuple->numPages       = (1U << order);
		*pHandle = pTuple->handle;
		*pKernelVirtAddr = kernelVirtAddr;
		*pPhysAddr = physAddr;

	} while (FALSE);

	MCDRV_DBG_VERBOSE("%s: exit with 0x%08X\n", __func__, ret);

	return ret;
}
EXPORT_SYMBOL(mobicore_allocate_wsm);


/*----------------------------------------------------------------------------*/
/**
 * This function will be called from user space as address = mmap(...).
 *
 * @param pFile
 * @param pVmArea
 * pVmArea.pg_offset != 0 is mapping of MCI is requested
 *
 * @return 0 if OK or -ENOMEM in case of error.
 */
static int mcKernelModule_mmap(
	struct file		*pFile,
	struct vm_area_struct	*pVmArea
)
{
	unsigned int		i;
	unsigned int		order;
	void			*kernelVirtAddr = 0;
	void			*physAddr = 0;
	unsigned long		requestedSize =
					pVmArea->vm_end - pVmArea->vm_start;
	unsigned long		allocatedSize;
	int			ret = 0;
	struct mc_tuple		*pTuple = 0;
	unsigned int		handle = 0;
	struct mcInstance	*pInstance = getInstance(pFile);
	unsigned int		request = pVmArea->vm_pgoff * 4096;
#if defined(DEBUG)
    bool release = false;
#else
    bool release = true;
#endif

	MCDRV_ASSERT(NULL != pInstance);
	MCDRV_DBG("enter (vmaStart=0x%p, size=%ld, request=0x%x, mci=0x%x)\n",
		  (void *)pVmArea->vm_start,
		  requestedSize,
		  request,
		  mciBase);

	order = sizeToOrder(requestedSize);
	if (INVALID_ORDER == order) {
		MCDRV_DBG_ERROR(
			"size to order converting failed for size %ld\n",
			requestedSize);
		return -ENOMEM;
	}

	allocatedSize = (1<<order)*PAGE_SIZE;

	MCDRV_DBG("size %ld -> order %d --> %ld (2^n pages)\n",
		  requestedSize, order, allocatedSize);

	do {
		/* Daemon tries to get an existing MCI */
		if ((MC_DRV_KMOD_MMAP_MCI == request) && (mciBase != 0)) {
			MCDRV_DBG("Request MCI, it is at (%x)\n",
				  mciBase);

			if (!isCallerMcDaemon(pInstance)) {
				ret = -EPERM;
				break;
			}
			kernelVirtAddr = (void *)mciBase;
			physAddr = (void *)virt_to_phys(kernelVirtAddr);
		} else {
			/* Usual Wsm request, allocate tuple. */
			if (MC_DRV_KMOD_MMAP_WSM == request) {
				/* search for a free entry in the tuple list
				REV axh: serialize this over multiple instances. */
				for (i = 0; i < MC_DRV_KMOD_TUPLE_NR; i++) {
					pTuple = &(pInstance->tuple[i]);
					if (0 == pTuple->handle) {
						pTuple->handle = getMcKModUniqueId();
						break;
					}
				}
				if (MC_DRV_KMOD_TUPLE_NR == i) {
					MCDRV_DBG_ERROR("no free tuple\n");
					ret = -EFAULT;
					break;
				}
			} else {
				if (request <= MC_DRV_KMOD_MMAP_PERSISTENTWSM || release) {
					/* Special Wsm request
						--> only Daemon is allowed */
					if (!isCallerMcDaemon(pInstance)) {
						ret = -EPERM;
						break;
					}
				}
			}
			if (request <= MC_DRV_KMOD_MMAP_PERSISTENTWSM) {
				/* Common code for all allocation paths
					*  get physical address, */
				kernelVirtAddr = (void *)__get_free_pages(
							GFP_USER | __GFP_COMP, order);
				if (NULL == kernelVirtAddr) {
					MCDRV_DBG_ERROR("get_free_pages failed\n");
					ret = -ENOMEM;
					break;
				}
				if (MC_DRV_KMOD_MMAP_WSM == request)
					handle = pTuple->handle;
				/* Get physical address to instance data */
				/* TODO: check for INVALID_ADDRESS? */
				physAddr = (void *)virt_to_phys(kernelVirtAddr);
			} else {
#if defined(DEBUG)
				/*kernelVirtAddr = phys_to_virt(request);
				kernelVirtAddr = ioremap(request,requestedSize);*/
				physAddr = (void *)request;
				kernelVirtAddr = phys_to_virt(request);
				/* NOTE: request != virt_to_phys(phys_to_virt(request))*/
#endif
			}
		}
		/* Common code for all mmap calls:
		 * map page to user
		 * store data in page */

		MCDRV_DBG("allocated phys=0x%p - 0x%p, "
			"size=%ld, kernelVirt=0x%p, handle=%d\n",
			physAddr,
			(void *)((unsigned int)physAddr+allocatedSize),
			allocatedSize, kernelVirtAddr, handle);

		pVmArea->vm_flags |= VM_RESERVED;
		/* convert Kernel address to User Address. Kernel address begins
			at PAGE_OFFSET, user Address range is below PAGE_OFFSET.
			Remapping the area is always done, so multiple mappings
			of one region are possible. Now remap kernel address
			space into user space */
		ret = (int)remap_pfn_range(
				pVmArea,
				(pVmArea->vm_start),
				addrToPfn(physAddr),
				requestedSize,
				pVmArea->vm_page_prot);
		if (0 != ret) {
			MCDRV_DBG_ERROR("remapPfnRange failed\n");

			/* free allocated pages when mmap fails, however, do not
				do it, when daemon tried to get an MCI that
				existed */
			if (!((MC_DRV_KMOD_MMAP_MCI == request) &&
			      (mciBase != 0)))
				freeContinguousPages(
					kernelVirtAddr,
					(1U << order));
			break;
		}

		/* Usual Wsm request, allocate tuple.
			When requesting Mci, we do not associate the page with
			the process.
			Note: we also never free the Mci
			Also, we never free a persistent Tci */
		if (MC_DRV_KMOD_MMAP_WSM == request) {
			pTuple->physAddr       = physAddr;
			pTuple->virtKernelAddr = kernelVirtAddr;
			pTuple->virtUserAddr   = (void *)(pVmArea->vm_start);
			pTuple->numPages       = (1U << order);
		}

		/* set response in allocated buffer */
		{
			struct mcMmapResp *mmapResp =
				(struct mcMmapResp *)kernelVirtAddr;
			/* TODO: do this for daemon only, otherwise set NULL */
			mmapResp->physAddr = (uint32_t)physAddr;
			mmapResp->handle = handle;
			if ((MC_DRV_KMOD_MMAP_MCI == request) &&
			    (0 != mciBase)) {
				mmapResp->isReused = 1;
			} else
				mmapResp->isReused = 0;
		}

		/* store MCI pointer */
		if ((MC_DRV_KMOD_MMAP_MCI == request) && (0 == mciBase)) {
			mciBase = (uint32_t)kernelVirtAddr;
			MCDRV_DBG("MCI base set to 0x%x\n", mciBase);
		}
	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return (int)ret;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 28)
/*----------------------------------------------------------------------------*/
/**
 * Force migration of current task to CPU0(where the monitor resides)
 *
 * @return Error code or 0 for success
 */
static int gotoCpu0(
	void
)
{
	int		ret = 0;
	struct cpumask	mask =  CPU_MASK_CPU0;

	MCDRV_DBG_VERBOSE("System has %d CPU's, we are on CPU #%d\n"
		  "\tBinding this process to CPU #0.\n"
		  "\tactive mask is %lx, setting it to mask=%lx\n",
		  nr_cpu_ids,
		  raw_smp_processor_id(),
		  cpu_active_mask->bits[0],
		  mask.bits[0]);
	ret = set_cpus_allowed_ptr(current, &mask);
	if (0 != ret)
		MCDRV_DBG_ERROR("set_cpus_allowed_ptr=%d.\n", ret);
	MCDRV_DBG_VERBOSE("And now we are on CPU #%d\n", raw_smp_processor_id());

	return ret;
}

/*----------------------------------------------------------------------------*/
/**
 * Restore CPU mask for current to ALL Cpus(reverse of gotoCpu0)
 *
 * @return Error code or 0 for success
 */
static int gotoAllCpu(
	void
)
{
	int		ret = 0;

	struct cpumask	mask =  CPU_MASK_ALL;

	MCDRV_DBG_VERBOSE("System has %d CPU's, we are on CPU #%d\n"
		  "\tBinding this process to CPU #0.\n"
		  "\tactive mask is %lx, setting it to mask=%lx\n",
		  nr_cpu_ids,
		  raw_smp_processor_id(),
		  cpu_active_mask->bits[0],
		  mask.bits[0]);
	ret = set_cpus_allowed_ptr(current, &mask);
	if (0 != ret)
		MCDRV_DBG_ERROR("set_cpus_allowed_ptr=%d.\n", ret);
	MCDRV_DBG_VERBOSE("And now we are on CPU #%d\n", raw_smp_processor_id());

	return ret;
}

#else
static int gotoCpu0(void)
{
	return 0;
}

static int gotoAllCpu(void)
{
	return 0;
}
#endif

/*----------------------------------------------------------------------------*/
/**
 * Initialize a new mobicore API instance object
 *
 * @return Instance or NULL if no allocation was possible.
 */
struct mcInstance *mobicore_open(
	void
) {
	struct mcInstance	*pInstance;
	pid_t			pidVnr;

	pInstance = kzalloc(sizeof(*pInstance), GFP_KERNEL);
	if (NULL == pInstance)
		return NULL;

	/* get a unique ID for this instance (PIDs are not unique) */
	pInstance->handle = getMcKModUniqueId();

	/* get the PID of the calling process. We avoid using
	 *	current->pid directly, as 2.6.24 introduced PID
	 *	namespaces. See also http://lwn.net/Articles/259217 */
	pidVnr = task_pid_vnr(current);
	pInstance->pidVnr = pidVnr;

	return pInstance;
}
EXPORT_SYMBOL(mobicore_open);

/*----------------------------------------------------------------------------*/
/**
 * This function will be called from user space as fd = open(...).
 * A set of internal instance data are created and initialized.
 *
 * @param pInode
 * @param pFile
 * @return 0 if OK or -ENOMEM if no allocation was possible.
 */
static int mcKernelModule_open(
	struct inode	*pInode,
	struct file	*pFile
)
{
	struct mcInstance	*pInstance;
	int			ret = 0;

	MCDRV_DBG_VERBOSE("enter\n");

	do {
		pInstance = mobicore_open();
		if (pInstance == NULL)
			return -ENOMEM;

		/* check if Daemon. We simply assume that the first to open us
			with root privileges must be the daemon. */
		if ((isUserlandCallerPrivileged())
		    && (NULL == mcDrvKModCtx.daemonInst)) {
			MCDRV_DBG("accept this as MobiCore Daemon\n");

			/* Set the caller's CPU mask to CPU0*/
			ret = gotoCpu0();
			if (0 != ret) {
				MCDRV_DBG("changing core failed!\n");
				break;
			}

			mcDrvKModCtx.daemonInst = pInstance;
			sema_init(&mcDrvKModCtx.daemonCtx.sem, 0);
			/* init ssiq event counter */
			mcDrvKModCtx.daemonCtx.ssiqCounter =
				atomic_read(&(mcDrvKModCtx.ssiqCtx.counter));
		}

		/* store instance data reference */
		pFile->private_data = pInstance;

		/* TODO axh: link all instances to allow clean up? */

	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return (int)ret;

}

/*----------------------------------------------------------------------------*/
/**
 * Release a mobicore instance object and all objects related to it
 * @param pInstance instance
 * @return 0 if Ok or -E ERROR
 */
int mobicore_release(
	struct mcInstance	*pInstance
)
{
	int			ret = 0;
	unsigned int		idxTuple;
	struct mcL2TablesDescr	*pWsmL2Descr, *pTemp;

	do {
		/* try to get the semaphore */
		ret = down_interruptible(&(mcDrvKModCtx.wsmL2Sem));
		if (0 != ret) {
			MCDRV_DBG_ERROR("down_interruptible() failed with %d\n", ret);
			/* TODO: can be block here? */
			ret = -ERESTARTSYS;
		} else {
			/* Check if some WSM is still in use. */
			list_for_each_entry_safe(
				pWsmL2Descr,
				pTemp,
				&(mcDrvKModCtx.wsmL2Descriptors),
				list
			) {
				if (pWsmL2Descr->pInstance == pInstance) {
					MCDRV_DBG_WARN(
						"trying to release WSM L2: "
						"physBase=%p ,nrOfPages=%d\n",
						getL2TablePhys(pWsmL2Descr),
						pWsmL2Descr->nrOfPages);

					/* unlock app usage and free if MobiCore
					does not use it */
					freeWsmL2Table(pWsmL2Descr, FREE_FROM_NWD);
				}
			} /* end while */

			/* release semaphore */
			up(&(mcDrvKModCtx.wsmL2Sem));
		}



		/* release all mapped data */
		for (idxTuple = 0;
		     idxTuple < MC_DRV_KMOD_TUPLE_NR;
		     idxTuple++) {
			struct mc_tuple *pTuple = &(pInstance->tuple[idxTuple]);

			if (0 != pTuple->virtUserAddr) {
				freeContinguousPages(
					pTuple->virtKernelAddr,
					pTuple->numPages);
			}
		}

		/* release instance context */
		kfree(pInstance);
	} while (FALSE);

	return ret;
}
EXPORT_SYMBOL(mobicore_release);

/*----------------------------------------------------------------------------*/
/**
 * This function will be called from user space as close(...).
 * The instance data are freed and the associated memory pages are unreserved.
 *
 * @param pInode
 * @param pFile
 *
 * @return 0
 */
static int mcKernelModule_release(
	struct inode	*pInode,
	struct file	*pFile
)
{
	int			ret = 0;
	struct mcInstance	*pInstance = getInstance(pFile);

	MCDRV_DBG_VERBOSE("enter\n");


	do {
		/* check if daemon closes us. */
		if (isCallerMcDaemon(pInstance)) {
			/* TODO: cleanup?
				* mcDrvKModCtx.wsmL2Descriptors remains */
			MCDRV_DBG_WARN("WARNING: MobiCore Daemon died\n");
			mcDrvKModCtx.daemonInst = NULL;
		}

		ret = mobicore_release(pInstance);

	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return (int)ret;
}


/*----------------------------------------------------------------------------*/
/**
 * This function represents the interrupt function of the mcDrvModule.
 * It signals by incrementing of an event counter and the start of the read
 * waiting queue, the read function a interrupt has occurred.
 *
 * @param   intr
 * @param   *pContext  pointer to registered device data
 *
 * @return  IRQ_HANDLED
 */
static irqreturn_t mcKernelModule_intrSSIQ(
	int	intr,
	void	*pContext
)
{
	irqreturn_t	ret = IRQ_NONE;

	/* we know the context. */
	MCDRV_ASSERT(&mcDrvKModCtx == pContext);

	do {
		if (MC_INTR_SSIQ != intr) {
			/* this should not happen, as we did no register for any
				other interrupt. Fir debugging, we print a
				message, but continue */
			MCDRV_DBG_WARN(
				"unknown interrupt %d, expecting only %d\n",
				intr, MC_INTR_SSIQ);
		}
		MCDRV_DBG_VERBOSE("received interrupt %d\n",
				  intr);

		/* increment interrupt event counter */
		atomic_inc(&(mcDrvKModCtx.ssiqCtx.counter));

		/* signal the daemon */
		up(&mcDrvKModCtx.daemonCtx.sem);


		ret = IRQ_HANDLED;

	} while (FALSE);

	return ret;
}

/*----------------------------------------------------------------------------*/
/** function table structure of this device driver. */
static const struct file_operations mcKernelModule_fileOperations = {
	.owner		= THIS_MODULE, /**< driver owner */
	.open		= mcKernelModule_open, /**< driver open function */
	.release	= mcKernelModule_release, /**< driver release function*/
	.unlocked_ioctl	= mcKernelModule_ioctl, /**< driver ioctl function */
	.mmap		= mcKernelModule_mmap, /**< driver mmap function */
	.read		= mcKernelModule_read, /**< driver read function */
};

/*----------------------------------------------------------------------------*/
/** registration structure as miscdevice. */
static struct miscdevice mcKernelModule_device = {
	.name	= MC_DRV_MOD_DEVNODE, /**< device name */
	.minor	= MISC_DYNAMIC_MINOR, /**< device minor number */
	/** device interface function structure */
	.fops	= &mcKernelModule_fileOperations,
};

#ifdef CONFIG_PM_RUNTIME

static struct timer_list resume_timer;

static void mobicore_resume_handler(unsigned long data)
{
	if(!mciBase || !mcpBuffer)
		return;

	mcpBuffer->mcFlags.sleepMode.SleepReq = MC_FLAG_NO_SLEEP_REQ;
}

static void mobicore_suspend_handler(struct work_struct *work)
{
	union fcGeneric fcSleep;
#ifdef MC_MEM_TRACES
    mobicore_read_log();
#endif
	memset(&fcSleep, 0, sizeof(fcSleep));
	fcSleep.asIn.cmd = MC_SMC_N_SIQ;

	mcpBuffer->mcFlags.sleepMode.SleepReq = MC_FLAG_REQ_TO_SLEEP;
	mcFastCall(&(fcSleep));
}

DECLARE_WORK(mobicore_suspend_work, mobicore_suspend_handler);

static int mobicore_suspend_notifier(struct notifier_block *nb,
									 unsigned long event, void* dummy)
{
#ifdef MC_MEM_TRACES
    mobicore_read_log();
#endif
	/* We have noting to say if MobiCore is not initialized*/
	if(!mciBase || !mcpBuffer)
		return 0;

	switch (event)
	{
		case PM_SUSPEND_PREPARE:
			/* We can't go to sleep if MobiCore is not IDLE or not Ready to sleep */
			if (!(mcpBuffer->mcFlags.sleepMode.ReadyToSleep & MC_STATE_READY_TO_SLEEP))
			{
				MCDRV_DBG("Suspend IDLE=%d!\n", mcpBuffer->mcFlags.schedule);
				MCDRV_DBG("Suspend REQ=%d!\n", mcpBuffer->mcFlags.sleepMode.SleepReq);
				MCDRV_DBG("Suspend Ready=%d!\n", mcpBuffer->mcFlags.sleepMode.ReadyToSleep);
				schedule_work_on(0, &mobicore_suspend_work);
				MCDRV_DBG("Don't allow SLEEP!");
				return NOTIFY_BAD;
			}
			MCDRV_DBG("Suspend IDLE=%d!\n", mcpBuffer->mcFlags.schedule);
            MCDRV_DBG("Suspend REQ=%d!\n", mcpBuffer->mcFlags.sleepMode.SleepReq);
            MCDRV_DBG("Suspend Ready=%d!\n", mcpBuffer->mcFlags.sleepMode.ReadyToSleep);
			cancel_work_sync(&mobicore_suspend_work);
			mod_timer(&resume_timer, 0);
			break;
        case PM_POST_SUSPEND :
			MCDRV_DBG("POST-Sleep request %d in!\n", mcpBuffer->mcFlags.sleepMode.SleepReq);
			mod_timer(&resume_timer, jiffies + msecs_to_jiffies(1000) );
            break;
		default:
			break;
	}
	return 0;
}

static struct notifier_block mobicore_notif_block = {
    .notifier_call = mobicore_suspend_notifier,
};
#endif /* CONFIG_PM_RUNTIME */

/*----------------------------------------------------------------------------*/
/**
 * This function is called the kernel during startup or by a insmod command.
 * This device is installed and registered as miscdevice, then interrupt and
 * queue handling is set up
 *
 * @return 0 for no error or -EIO if registration fails
 */
static int __init mcKernelModule_init(
	void
)
{
	int ret = 0;

	MCDRV_DBG("enter (Build " __TIMESTAMP__ ")\n");
	MCDRV_DBG("mcDrvModuleApi version is %i.%i\n",
			MCDRVMODULEAPI_VERSION_MAJOR,
			MCDRVMODULEAPI_VERSION_MINOR);
    MCDRV_DBG("%s\n",MOBICORE_COMPONENT_BUILD_TAG);

	do {
		/* Hardware does not support ARM TrustZone -> Cannot continue! */
		if (!hasSecurityExtensions()) {
			MCDRV_DBG_ERROR("Hardware does't support ARM TrustZone!\n");
			ret = -ENODEV;
			break;
		}

		/* Running in secure mode -> Cannot load the driver! */
		if (isSecureMode()) {
			MCDRV_DBG_ERROR("Running in secure MODE!\n");
			ret = -ENODEV;
			break;
		}

#ifdef MC_MEM_TRACES
		/* setupLog won't fail, it eats up any errors */
		mcKernelModule_setupLog();
#endif
		mcDrvKModCtx.daemonInst = NULL;
		sema_init(&mcDrvKModCtx.daemonCtx.sem, 0);
		/* set up S-SIQ interrupt handler */
		ret = request_irq(
			      MC_INTR_SSIQ,
			      mcKernelModule_intrSSIQ,
			      IRQF_TRIGGER_RISING,
			      MC_DRV_MOD_DEVNODE,
			      &mcDrvKModCtx);
		if (0 != ret) {
			MCDRV_DBG_ERROR("interrupt request failed\n");
			break;
		}

		ret = misc_register(&mcKernelModule_device);
		if (0 != ret) {
			MCDRV_DBG_ERROR("device register failed\n");
			break;
		}
#ifdef CONFIG_PM_RUNTIME
		setup_timer( &resume_timer, mobicore_resume_handler, 0 );
		if ((ret = register_pm_notifier(&mobicore_notif_block))) {
			MCDRV_DBG_ERROR("device pm register failed\n");
			break;
		}
#endif //CONFIG_PM_RUNTIME
		/* initialize event counter for signaling of an IRQ to zero */
		atomic_set(&(mcDrvKModCtx.ssiqCtx.counter), 0);

		/* init list for WSM L2 chunks. */
		INIT_LIST_HEAD(&(mcDrvKModCtx.wsmL2Chunks));

		/* L2 table descriptor list. */
		INIT_LIST_HEAD(&(mcDrvKModCtx.wsmL2Descriptors));

		sema_init(&(mcDrvKModCtx.wsmL2Sem), 1);

		/* initialize unique number counter which we can use for
			handles. It is limited to 2^32, but this should be
			enough to be roll-over safe for us. We start with 1
			instead of 0. */
		atomic_set(&(mcDrvKModCtx.uniqueCounter), 1);

		MCDRV_DBG("initialized\n");

		ret = 0;

	} while (FALSE);

	MCDRV_DBG_VERBOSE("exit with %d/0x%08X\n", ret, ret);

	return (int)ret;
}



/*----------------------------------------------------------------------------*/
/**
 * This function removes this device driver from the Linux device manager .
 */
static void __exit mcKernelModule_exit(
	void
)
{
	struct mcL2TablesDescr	*pWsmL2Descr;

	MCDRV_DBG_VERBOSE("enter\n");

	/* Check if some WSM is still in use. */
	list_for_each_entry(
		pWsmL2Descr,
		&(mcDrvKModCtx.wsmL2Descriptors),
		list
	) {
		MCDRV_DBG_WARN(
			"WSM L2 still in use: physBase=%p ,nrOfPages=%d\n",
			getL2TablePhys(pWsmL2Descr),
			pWsmL2Descr->nrOfPages);
	} /* end while */

#ifdef CONFIG_PM_RUNTIME
	if (unregister_pm_notifier(&mobicore_notif_block)) {
		MCDRV_DBG_ERROR("device pm unregister failed\n");
	}
	del_timer(&resume_timer);
#endif

	free_irq(MC_INTR_SSIQ, &mcDrvKModCtx);

	misc_deregister(&mcKernelModule_device);
#ifdef MC_MEM_TRACES
	if (mcLogBuf) {
		free_page((unsigned long)mcLogBuf);
	}
#endif
	MCDRV_DBG_VERBOSE("exit");
}


/*----------------------------------------------------------------------------*/
/* Linux Driver Module Macros */
module_init(mcKernelModule_init);
module_exit(mcKernelModule_exit);
MODULE_AUTHOR("Giesecke & Devrient GmbH");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MobiCore driver");

/** @} */
