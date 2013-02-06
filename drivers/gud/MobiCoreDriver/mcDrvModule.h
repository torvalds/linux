/**
 * Header file of MobiCore Driver Kernel Module.
 *
 * @addtogroup MobiCore_Driver_Kernel_Module
 * @{
 * Internal structures of the McDrvModule
 * @file
 *
 * Header file the MobiCore Driver Kernel Module,
 * its internal structures and defines.
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MC_DRV_KMOD_H_
#define _MC_DRV_KMOD_H_

#include "mcDrvModuleLinuxApi.h"
#include "public/mcDrvModuleApi.h"
/** Platform specific settings */
#include "platform.h"

/** ARM Specific masks and modes */
#define ARM_CPSR_MASK 0x1F
#define ARM_MONITOR_MODE 0b10110
#define ARM_SECURITY_EXTENSION_MASK 0x30

/** Number of page table entries in one L2 table. This is ARM specific, an
 *  L2 table covers 1 MiB by using 256 entry referring to 4KiB pages each.
 */
#define MC_ARM_L2_TABLE_ENTRIES		256

/** Number of address allocations for one driver instance. */
#define MC_DRV_KMOD_TUPLE_NR		16

/** Number of pages for L2 tables. There are 4 table in each page. */
#define MC_DRV_KMOD_L2_TABLE_PER_PAGES	4
#define MC_DRV_KMOD_L2_TABLE_PAGES	8

struct l2Table {
	pte_t	tableEntries[MC_ARM_L2_TABLE_ENTRIES];
};

#define INVALID_ADDRESS     ((void *)(-1))

/** ARM L2 PTE bits */
#define L2_FLAG_SMALL_XN    (1U <<  0)
#define L2_FLAG_SMALL       (1U <<  1)
#define L2_FLAG_B           (1U <<  2)
#define L2_FLAG_C           (1U <<  3)
#define L2_FLAG_AP0         (1U <<  4)
#define L2_FLAG_AP1         (1U <<  5)
#define L2_FLAG_SMALL_TEX0  (1U <<  6)
#define L2_FLAG_SMALL_TEX1  (1U <<  7)
#define L2_FLAG_SMALL_TEX2  (1U <<  8)
#define L2_FLAG_APX         (1U <<  9)
#define L2_FLAG_S           (1U << 10)
#define L2_FLAG_NG          (1U << 11)

/**
 * tuple list entry.
 * It describes the tuple, physical Kernel start address to the virtual Client
 * address. The virtual kernel address is added for a simpler search algorithm.
 */
struct mc_tuple {
	unsigned int	handle; /* unique handle */
	void		*virtUserAddr; /**< virtual User start address */
	void		*virtKernelAddr; /**< virtual Kernel start address */
	void		*physAddr; /**< physical start address */
	unsigned int	numPages; /**< number of pages */
};

/**
 * Driver instance data.
 */
struct mcInstance {
	/** unique handle */
	unsigned int	handle;
	/** process that opened this instance */
	pid_t		pidVnr;
	struct {
		/** number of pages */
		unsigned int	numPages;
		/** virtual start address kernel address space generated
			by mmap command */
		void		*virtAddr;
		/** physical start address kernel address space generated
			by mmap command */
		void		*physAddr;
	} map;
	/** tuple list for mmap generated address space and
		its virtual client address */
	struct mc_tuple	tuple[MC_DRV_KMOD_TUPLE_NR];
};



/** data structure of 4 L2 tables within one 4kb page*/
struct mcL2Page {
	struct l2Table table[MC_DRV_KMOD_L2_TABLE_PER_PAGES];
};


/** bookkeeping data structure that manages 4 L2 tables in one page */
struct mcL2TablesChunk {
	struct list_head	list;
	unsigned int		usageBitmap;	/**< usage bitmap */
	struct mcL2Page		*kernelVirt;	/**< kernel virtual address */
	struct mcL2Page		*phys;		/**< physical address */
	struct page		*pPage;		/**< pointer to page struct */
};

/** bookkeeping data structure that binds one L2 table
	to its chunk(page) and its user */
struct mcL2TablesDescr {
	struct list_head	list;
	unsigned int		handle;
	unsigned int		flags;
	struct mcInstance	*pInstance;
	struct mcL2TablesChunk	*pChunk;
	unsigned int		idx;
	unsigned int		nrOfPages;
};

#define MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_APP   (1U << 0)
#define MC_WSM_L2_CONTAINER_WSM_LOCKED_BY_MC    (1U << 1)


/** MobiCore S-SIQ interrupt context data. */
struct mcSSiqCtx {
	atomic_t		counter; /**< S-SIQ interrupt counter */
};


/** MobiCore Daemon  context data. */
struct mcDaemonCtx {
	struct semaphore		sem; /**< event semaphore */
	struct fasync_struct	*pAsyncQueue;
	unsigned int			ssiqCounter; /**< event counter */
};


/** MobiCore Driver Kernel Module context data. */
struct mcDrvKModCtx {
	atomic_t			uniqueCounter; /**< ever incrementing counter */
	struct mcSSiqCtx	ssiqCtx; /**< S-SIQ interrupt context */
	struct mcDaemonCtx	daemonCtx; /**< MobiCore Daemon context */
	struct mcInstance	*daemonInst; /**< pointer to instance of daemon */
	struct list_head	wsmL2Chunks; /**< Backing store for L2 tables */
	struct list_head	wsmL2Descriptors; /**< Bookkeeping for L2 tables */
	struct semaphore	wsmL2Sem; /**< semaphore to synchronize access to
									above lists */
};

/** MobiCore internal trace buffer structure. */
struct mcTraceBuf {
	uint32_t version; /**< version of trace buffer */
	uint32_t length; /**< length of allocated buffer(includes header) */
	uint32_t write_pos; /**< last write position */
	char  buff[1]; /**< start of the log buffer */
};

#define MCDRV_DBG_ERROR(txt, ...) \
	printk(KERN_ERR "mcDrvKMod [%d] %s() ### ERROR: " txt, \
		task_pid_vnr(current), \
		__func__, \
		##__VA_ARGS__)

/* dummy function helper macro. */
#define DUMMY_FUNCTION()    do {} while (0)

#if defined(DEBUG)

/* #define DEBUG_VERBOSE */
#if defined(DEBUG_VERBOSE)
#define MCDRV_DBG_VERBOSE          MCDRV_DBG
#else
#define MCDRV_DBG_VERBOSE(...)     DUMMY_FUNCTION()
#endif

#define MCDRV_DBG(txt, ...) \
	printk(KERN_INFO "mcDrvKMod [%d on CPU%d] %s(): " txt, \
		task_pid_vnr(current), \
		raw_smp_processor_id(), \
		__func__, \
		##__VA_ARGS__)

#define MCDRV_DBG_WARN(txt, ...) \
	printk(KERN_WARNING "mcDrvKMod [%d] %s() WARNING: " txt, \
		task_pid_vnr(current), \
		__func__, \
		##__VA_ARGS__)

#define MCDRV_ASSERT(cond) \
	do { \
		if (unlikely(!(cond))) { \
			panic("mcDrvKMod Assertion failed: %s:%d\n", \
				__FILE__, __LINE__); \
		} \
	} while (0)

#else

#define MCDRV_DBG_VERBOSE(...)	DUMMY_FUNCTION()
#define MCDRV_DBG(...)		DUMMY_FUNCTION()
#define MCDRV_DBG_WARN(...)	DUMMY_FUNCTION()

#define MCDRV_ASSERT(...)	DUMMY_FUNCTION()

#endif /* [not] defined(DEBUG) */


#endif /* _MC_DRV_KMOD_H_ */
/** @} */
