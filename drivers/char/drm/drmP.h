/**
 * \file drmP.h
 * Private header for Direct Rendering Manager
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DRM_P_H_
#define _DRM_P_H_

/* If you want the memory alloc debug functionality, change define below */
/* #define DEBUG_MEMORY */

#ifdef __KERNEL__
#ifdef __alpha__
/* add include of current.h so that "current" is defined
 * before static inline funcs in wait.h. Doing this so we
 * can build the DRM (part of PI DRI). 4/21/2000 S + B */
#include <asm/current.h>
#endif				/* __alpha__ */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/pci.h>
#include <linux/jiffies.h>
#include <linux/smp_lock.h>	/* For (un)lock_kernel */
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#if defined(__alpha__) || defined(__powerpc__)
#include <asm/pgtable.h>	/* For pte_wrprotect */
#endif
#include <asm/io.h>
#include <asm/mman.h>
#include <asm/uaccess.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif
#if defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
#include <linux/types.h>
#include <linux/agp_backend.h>
#endif
#include <linux/workqueue.h>
#include <linux/poll.h>
#include <asm/pgalloc.h>
#include "drm.h"

#define __OS_HAS_AGP (defined(CONFIG_AGP) || (defined(CONFIG_AGP_MODULE) && defined(MODULE)))
#define __OS_HAS_MTRR (defined(CONFIG_MTRR))

#include "drm_os_linux.h"

/***********************************************************************/
/** \name DRM template customization defaults */
/*@{*/

/* driver capabilities and requirements mask */
#define DRIVER_USE_AGP     0x1
#define DRIVER_REQUIRE_AGP 0x2
#define DRIVER_USE_MTRR    0x4
#define DRIVER_PCI_DMA     0x8
#define DRIVER_SG          0x10
#define DRIVER_HAVE_DMA    0x20
#define DRIVER_HAVE_IRQ    0x40
#define DRIVER_IRQ_SHARED  0x80
#define DRIVER_IRQ_VBL     0x100
#define DRIVER_DMA_QUEUE   0x200
#define DRIVER_FB_DMA      0x400

/***********************************************************************/
/** \name Begin the DRM... */
/*@{*/

#define DRM_DEBUG_CODE 2	  /**< Include debugging code if > 1, then
				     also include looping detection. */

#define DRM_HASH_SIZE	      16 /**< Size of key hash table. Must be power of 2. */
#define DRM_KERNEL_CONTEXT    0	 /**< Change drm_resctx if changed */
#define DRM_RESERVED_CONTEXTS 1	 /**< Change drm_resctx if changed */
#define DRM_LOOPING_LIMIT     5000000
#define DRM_TIME_SLICE	      (HZ/20)  /**< Time slice for GLXContexts */
#define DRM_LOCK_SLICE	      1	/**< Time slice for lock, in jiffies */

#define DRM_FLAG_DEBUG	  0x01

#define DRM_MEM_DMA	   0
#define DRM_MEM_SAREA	   1
#define DRM_MEM_DRIVER	   2
#define DRM_MEM_MAGIC	   3
#define DRM_MEM_IOCTLS	   4
#define DRM_MEM_MAPS	   5
#define DRM_MEM_VMAS	   6
#define DRM_MEM_BUFS	   7
#define DRM_MEM_SEGS	   8
#define DRM_MEM_PAGES	   9
#define DRM_MEM_FILES	  10
#define DRM_MEM_QUEUES	  11
#define DRM_MEM_CMDS	  12
#define DRM_MEM_MAPPINGS  13
#define DRM_MEM_BUFLISTS  14
#define DRM_MEM_AGPLISTS  15
#define DRM_MEM_TOTALAGP  16
#define DRM_MEM_BOUNDAGP  17
#define DRM_MEM_CTXBITMAP 18
#define DRM_MEM_STUB      19
#define DRM_MEM_SGLISTS   20
#define DRM_MEM_CTXLIST  21

#define DRM_MAX_CTXBITMAP (PAGE_SIZE * 8)

/*@}*/

/***********************************************************************/
/** \name Backward compatibility section */
/*@{*/

#define DRM_RPR_ARG(vma) vma,

#define VM_OFFSET(vma) ((vma)->vm_pgoff << PAGE_SHIFT)

/*@}*/

/***********************************************************************/
/** \name Macros to make printk easier */
/*@{*/

/**
 * Error output.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_ERROR(fmt, arg...) \
	printk(KERN_ERR "[" DRM_NAME ":%s] *ERROR* " fmt , __FUNCTION__ , ##arg)

/**
 * Memory error output.
 *
 * \param area memory area where the error occurred.
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_MEM_ERROR(area, fmt, arg...) \
	printk(KERN_ERR "[" DRM_NAME ":%s:%s] *ERROR* " fmt , __FUNCTION__, \
	       drm_mem_stats[area].name , ##arg)

#define DRM_INFO(fmt, arg...)  printk(KERN_INFO "[" DRM_NAME "] " fmt , ##arg)

/**
 * Debug output.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#if DRM_DEBUG_CODE
#define DRM_DEBUG(fmt, arg...)						\
	do {								\
		if ( drm_debug )			\
			printk(KERN_DEBUG				\
			       "[" DRM_NAME ":%s] " fmt ,	\
			       __FUNCTION__ , ##arg);			\
	} while (0)
#else
#define DRM_DEBUG(fmt, arg...)		 do { } while (0)
#endif

#define DRM_PROC_LIMIT (PAGE_SIZE-80)

#define DRM_PROC_PRINT(fmt, arg...)					\
   len += sprintf(&buf[len], fmt , ##arg);				\
   if (len > DRM_PROC_LIMIT) { *eof = 1; return len - offset; }

#define DRM_PROC_PRINT_RET(ret, fmt, arg...)				\
   len += sprintf(&buf[len], fmt , ##arg);				\
   if (len > DRM_PROC_LIMIT) { ret; *eof = 1; return len - offset; }

/*@}*/

/***********************************************************************/
/** \name Internal types and structures */
/*@{*/

#define DRM_ARRAY_SIZE(x) ARRAY_SIZE(x)
#define DRM_MIN(a,b) min(a,b)
#define DRM_MAX(a,b) max(a,b)

#define DRM_LEFTCOUNT(x) (((x)->rp + (x)->count - (x)->wp) % ((x)->count + 1))
#define DRM_BUFCOUNT(x) ((x)->count - DRM_LEFTCOUNT(x))
#define DRM_WAITCOUNT(dev,idx) DRM_BUFCOUNT(&dev->queuelist[idx]->waitlist)

#define DRM_IF_VERSION(maj, min) (maj << 16 | min)
/**
 * Get the private SAREA mapping.
 *
 * \param _dev DRM device.
 * \param _ctx context number.
 * \param _map output mapping.
 */
#define DRM_GET_PRIV_SAREA(_dev, _ctx, _map) do {	\
	(_map) = (_dev)->context_sareas[_ctx];		\
} while(0)

/**
 * Test that the hardware lock is held by the caller, returning otherwise.
 *
 * \param dev DRM device.
 * \param filp file pointer of the caller.
 */
#define LOCK_TEST_WITH_RETURN( dev, filp )				\
do {									\
	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||		\
	     dev->lock.filp != filp ) {				\
		DRM_ERROR( "%s called without lock held\n",		\
			   __FUNCTION__ );				\
		return -EINVAL;						\
	}								\
} while (0)

/**
 * Copy and IOCTL return string to user space
 */
#define DRM_COPY( name, value )						\
	len = strlen( value );						\
	if ( len > name##_len ) len = name##_len;			\
	name##_len = strlen( value );					\
	if ( len && name ) {						\
		if ( copy_to_user( name, value, len ) )			\
			return -EFAULT;					\
	}

/**
 * Ioctl function type.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg argument.
 */
typedef int drm_ioctl_t(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);

typedef int drm_ioctl_compat_t(struct file *filp, unsigned int cmd,
			       unsigned long arg);

#define DRM_AUTH	0x1
#define	DRM_MASTER	0x2
#define DRM_ROOT_ONLY	0x4

typedef struct drm_ioctl_desc {
	drm_ioctl_t *func;
	int flags;
} drm_ioctl_desc_t;

typedef struct drm_devstate {
	pid_t owner;			/**< X server pid holding x_lock */
} drm_devstate_t;

typedef struct drm_magic_entry {
	drm_magic_t magic;
	struct drm_file *priv;
	struct drm_magic_entry *next;
} drm_magic_entry_t;

typedef struct drm_magic_head {
	struct drm_magic_entry *head;
	struct drm_magic_entry *tail;
} drm_magic_head_t;

typedef struct drm_vma_entry {
	struct vm_area_struct *vma;
	struct drm_vma_entry *next;
	pid_t pid;
} drm_vma_entry_t;

/**
 * DMA buffer.
 */
typedef struct drm_buf {
	int idx;		       /**< Index into master buflist */
	int total;		       /**< Buffer size */
	int order;		       /**< log-base-2(total) */
	int used;		       /**< Amount of buffer in use (for DMA) */
	unsigned long offset;	       /**< Byte offset (used internally) */
	void *address;		       /**< Address of buffer */
	unsigned long bus_address;     /**< Bus address of buffer */
	struct drm_buf *next;	       /**< Kernel-only: used for free list */
	__volatile__ int waiting;      /**< On kernel DMA queue */
	__volatile__ int pending;      /**< On hardware DMA queue */
	wait_queue_head_t dma_wait;    /**< Processes waiting */
	struct file *filp;	       /**< Pointer to holding file descr */
	int context;		       /**< Kernel queue for this buffer */
	int while_locked;	       /**< Dispatch this buffer while locked */
	enum {
		DRM_LIST_NONE = 0,
		DRM_LIST_FREE = 1,
		DRM_LIST_WAIT = 2,
		DRM_LIST_PEND = 3,
		DRM_LIST_PRIO = 4,
		DRM_LIST_RECLAIM = 5
	} list;			       /**< Which list we're on */

	int dev_priv_size;		 /**< Size of buffer private storage */
	void *dev_private;		 /**< Per-buffer private storage */
} drm_buf_t;

/** bufs is one longer than it has to be */
typedef struct drm_waitlist {
	int count;			/**< Number of possible buffers */
	drm_buf_t **bufs;		/**< List of pointers to buffers */
	drm_buf_t **rp;			/**< Read pointer */
	drm_buf_t **wp;			/**< Write pointer */
	drm_buf_t **end;		/**< End pointer */
	spinlock_t read_lock;
	spinlock_t write_lock;
} drm_waitlist_t;

typedef struct drm_freelist {
	int initialized;	       /**< Freelist in use */
	atomic_t count;		       /**< Number of free buffers */
	drm_buf_t *next;	       /**< End pointer */

	wait_queue_head_t waiting;     /**< Processes waiting on free bufs */
	int low_mark;		       /**< Low water mark */
	int high_mark;		       /**< High water mark */
	atomic_t wfh;		       /**< If waiting for high mark */
	spinlock_t lock;
} drm_freelist_t;

/**
 * Buffer entry.  There is one of this for each buffer size order.
 */
typedef struct drm_buf_entry {
	int buf_size;			/**< size */
	int buf_count;			/**< number of buffers */
	drm_buf_t *buflist;		/**< buffer list */
	int seg_count;
	int page_order;
	unsigned long *seglist;

	drm_freelist_t freelist;
} drm_buf_entry_t;

/** File private data */
typedef struct drm_file {
	int authenticated;
	int master;
	int minor;
	pid_t pid;
	uid_t uid;
	drm_magic_t magic;
	unsigned long ioctl_count;
	struct drm_file *next;
	struct drm_file *prev;
	struct drm_head *head;
	int remove_auth_on_close;
	unsigned long lock_count;
	void *driver_priv;
} drm_file_t;

/** Wait queue */
typedef struct drm_queue {
	atomic_t use_count;		/**< Outstanding uses (+1) */
	atomic_t finalization;		/**< Finalization in progress */
	atomic_t block_count;		/**< Count of processes waiting */
	atomic_t block_read;		/**< Queue blocked for reads */
	wait_queue_head_t read_queue;	/**< Processes waiting on block_read */
	atomic_t block_write;		/**< Queue blocked for writes */
	wait_queue_head_t write_queue;	/**< Processes waiting on block_write */
#if 1
	atomic_t total_queued;		/**< Total queued statistic */
	atomic_t total_flushed;		/**< Total flushes statistic */
	atomic_t total_locks;		/**< Total locks statistics */
#endif
	drm_ctx_flags_t flags;		/**< Context preserving and 2D-only */
	drm_waitlist_t waitlist;	/**< Pending buffers */
	wait_queue_head_t flush_queue;	/**< Processes waiting until flush */
} drm_queue_t;

/**
 * Lock data.
 */
typedef struct drm_lock_data {
	drm_hw_lock_t *hw_lock;		/**< Hardware lock */
	struct file *filp;		/**< File descr of lock holder (0=kernel) */
	wait_queue_head_t lock_queue;	/**< Queue of blocked processes */
	unsigned long lock_time;	/**< Time of last lock in jiffies */
} drm_lock_data_t;

/**
 * DMA data.
 */
typedef struct drm_device_dma {

	drm_buf_entry_t bufs[DRM_MAX_ORDER + 1];	/**< buffers, grouped by their size order */
	int buf_count;			/**< total number of buffers */
	drm_buf_t **buflist;		/**< Vector of pointers into drm_device_dma::bufs */
	int seg_count;
	int page_count;			/**< number of pages */
	unsigned long *pagelist;	/**< page list */
	unsigned long byte_count;
	enum {
		_DRM_DMA_USE_AGP = 0x01,
		_DRM_DMA_USE_SG = 0x02,
		_DRM_DMA_USE_FB = 0x04
	} flags;

} drm_device_dma_t;

/**
 * AGP memory entry.  Stored as a doubly linked list.
 */
typedef struct drm_agp_mem {
	unsigned long handle;		/**< handle */
	DRM_AGP_MEM *memory;
	unsigned long bound;		/**< address */
	int pages;
	struct drm_agp_mem *prev;	/**< previous entry */
	struct drm_agp_mem *next;	/**< next entry */
} drm_agp_mem_t;

/**
 * AGP data.
 *
 * \sa drm_agp_init() and drm_device::agp.
 */
typedef struct drm_agp_head {
	DRM_AGP_KERN agp_info;		/**< AGP device information */
	drm_agp_mem_t *memory;		/**< memory entries */
	unsigned long mode;		/**< AGP mode */
	struct agp_bridge_data *bridge;
	int enabled;			/**< whether the AGP bus as been enabled */
	int acquired;			/**< whether the AGP device has been acquired */
	unsigned long base;
	int agp_mtrr;
	int cant_use_aperture;
	unsigned long page_mask;
} drm_agp_head_t;

/**
 * Scatter-gather memory.
 */
typedef struct drm_sg_mem {
	unsigned long handle;
	void *virtual;
	int pages;
	struct page **pagelist;
	dma_addr_t *busaddr;
} drm_sg_mem_t;

typedef struct drm_sigdata {
	int context;
	drm_hw_lock_t *lock;
} drm_sigdata_t;

typedef struct drm_dma_handle {
	dma_addr_t busaddr;
	void *vaddr;
	size_t size;
} drm_dma_handle_t;

/**
 * Mappings list
 */
typedef struct drm_map_list {
	struct list_head head;		/**< list head */
	drm_map_t *map;			/**< mapping */
	unsigned int user_token;
} drm_map_list_t;

typedef drm_map_t drm_local_map_t;

/**
 * Context handle list
 */
typedef struct drm_ctx_list {
	struct list_head head;		/**< list head */
	drm_context_t handle;		/**< context handle */
	drm_file_t *tag;		/**< associated fd private data */
} drm_ctx_list_t;

typedef struct drm_vbl_sig {
	struct list_head head;
	unsigned int sequence;
	struct siginfo info;
	struct task_struct *task;
} drm_vbl_sig_t;

/* location of GART table */
#define DRM_ATI_GART_MAIN 1
#define DRM_ATI_GART_FB   2

typedef struct ati_pcigart_info {
	int gart_table_location;
	int is_pcie;
	void *addr;
	dma_addr_t bus_addr;
	drm_local_map_t mapping;
} drm_ati_pcigart_info;

/**
 * DRM driver structure. This structure represent the common code for
 * a family of cards. There will one drm_device for each card present
 * in this family
 */
struct drm_device;

struct drm_driver {
	int (*load) (struct drm_device *, unsigned long flags);
	int (*firstopen) (struct drm_device *);
	int (*open) (struct drm_device *, drm_file_t *);
	void (*preclose) (struct drm_device *, struct file * filp);
	void (*postclose) (struct drm_device *, drm_file_t *);
	void (*lastclose) (struct drm_device *);
	int (*unload) (struct drm_device *);
	int (*dma_ioctl) (DRM_IOCTL_ARGS);
	void (*dma_ready) (struct drm_device *);
	int (*dma_quiescent) (struct drm_device *);
	int (*context_ctor) (struct drm_device * dev, int context);
	int (*context_dtor) (struct drm_device * dev, int context);
	int (*kernel_context_switch) (struct drm_device * dev, int old,
				      int new);
	void (*kernel_context_switch_unlock) (struct drm_device * dev,
					      drm_lock_t *lock);
	int (*vblank_wait) (struct drm_device * dev, unsigned int *sequence);
	int (*dri_library_name) (struct drm_device *dev, char *buf);

	/**
	 * Called by \c drm_device_is_agp.  Typically used to determine if a
	 * card is really attached to AGP or not.
	 *
	 * \param dev  DRM device handle
	 *
	 * \returns
	 * One of three values is returned depending on whether or not the
	 * card is absolutely \b not AGP (return of 0), absolutely \b is AGP
	 * (return of 1), or may or may not be AGP (return of 2).
	 */
	int (*device_is_agp) (struct drm_device * dev);

	/* these have to be filled in */

	irqreturn_t(*irq_handler) (DRM_IRQ_ARGS);
	void (*irq_preinstall) (struct drm_device * dev);
	void (*irq_postinstall) (struct drm_device * dev);
	void (*irq_uninstall) (struct drm_device * dev);
	void (*reclaim_buffers) (struct drm_device * dev, struct file * filp);
	void (*reclaim_buffers_locked) (struct drm_device *dev,
					struct file *filp);
	unsigned long (*get_map_ofs) (drm_map_t * map);
	unsigned long (*get_reg_ofs) (struct drm_device * dev);
	void (*set_version) (struct drm_device * dev, drm_set_version_t * sv);

	int major;
	int minor;
	int patchlevel;
	char *name;
	char *desc;
	char *date;

	u32 driver_features;
	int dev_priv_size;
	drm_ioctl_desc_t *ioctls;
	int num_ioctls;
	struct file_operations fops;
	struct pci_driver pci_driver;
};

/**
 * DRM head structure. This structure represent a video head on a card
 * that may contain multiple heads. Embed one per head of these in the
 * private drm_device structure.
 */
typedef struct drm_head {
	int minor;			/**< Minor device number */
	struct drm_device *dev;
	struct proc_dir_entry *dev_root;  /**< proc directory entry */
	dev_t device;			/**< Device number for mknod */
	struct class_device *dev_class;
} drm_head_t;

/**
 * DRM device structure. This structure represent a complete card that
 * may contain multiple heads.
 */
typedef struct drm_device {
	char *unique;			/**< Unique identifier: e.g., busid */
	int unique_len;			/**< Length of unique field */
	char *devname;			/**< For /proc/interrupts */
	int if_version;			/**< Highest interface version set */

	int blocked;			/**< Blocked due to VC switch? */

	/** \name Locks */
	/*@{ */
	spinlock_t count_lock;		/**< For inuse, drm_device::open_count, drm_device::buf_use */
	struct mutex struct_mutex;	/**< For others */
	/*@} */

	/** \name Usage Counters */
	/*@{ */
	int open_count;			/**< Outstanding files open */
	atomic_t ioctl_count;		/**< Outstanding IOCTLs pending */
	atomic_t vma_count;		/**< Outstanding vma areas open */
	int buf_use;			/**< Buffers in use -- cannot alloc */
	atomic_t buf_alloc;		/**< Buffer allocation in progress */
	/*@} */

	/** \name Performance counters */
	/*@{ */
	unsigned long counters;
	drm_stat_type_t types[15];
	atomic_t counts[15];
	/*@} */

	/** \name Authentication */
	/*@{ */
	drm_file_t *file_first;		/**< file list head */
	drm_file_t *file_last;		/**< file list tail */
	drm_magic_head_t magiclist[DRM_HASH_SIZE];	/**< magic hash table */
	/*@} */

	/** \name Memory management */
	/*@{ */
	drm_map_list_t *maplist;	/**< Linked list of regions */
	int map_count;			/**< Number of mappable regions */

	/** \name Context handle management */
	/*@{ */
	drm_ctx_list_t *ctxlist;	/**< Linked list of context handles */
	int ctx_count;			/**< Number of context handles */
	struct mutex ctxlist_mutex;	/**< For ctxlist */

	drm_map_t **context_sareas;	    /**< per-context SAREA's */
	int max_context;

	drm_vma_entry_t *vmalist;	/**< List of vmas (for debugging) */
	drm_lock_data_t lock;		/**< Information on hardware lock */
	/*@} */

	/** \name DMA queues (contexts) */
	/*@{ */
	int queue_count;		/**< Number of active DMA queues */
	int queue_reserved;		  /**< Number of reserved DMA queues */
	int queue_slots;		/**< Actual length of queuelist */
	drm_queue_t **queuelist;	/**< Vector of pointers to DMA queues */
	drm_device_dma_t *dma;		/**< Optional pointer for DMA support */
	/*@} */

	/** \name Context support */
	/*@{ */
	int irq;			/**< Interrupt used by board */
	int irq_enabled;		/**< True if irq handler is enabled */
	__volatile__ long context_flag;	/**< Context swapping flag */
	__volatile__ long interrupt_flag; /**< Interruption handler flag */
	__volatile__ long dma_flag;	/**< DMA dispatch flag */
	struct timer_list timer;	/**< Timer for delaying ctx switch */
	wait_queue_head_t context_wait;	/**< Processes waiting on ctx switch */
	int last_checked;		/**< Last context checked for DMA */
	int last_context;		/**< Last current context */
	unsigned long last_switch;	/**< jiffies at last context switch */
	/*@} */

	struct work_struct work;
	/** \name VBLANK IRQ support */
	/*@{ */

	wait_queue_head_t vbl_queue;	/**< VBLANK wait queue */
	atomic_t vbl_received;
	spinlock_t vbl_lock;
	drm_vbl_sig_t vbl_sigs;		/**< signal list to send on VBLANK */
	unsigned int vbl_pending;

	/*@} */
	cycles_t ctx_start;
	cycles_t lck_start;

	struct fasync_struct *buf_async;/**< Processes waiting for SIGIO */
	wait_queue_head_t buf_readers;	/**< Processes waiting to read */
	wait_queue_head_t buf_writers;	/**< Processes waiting to ctx switch */

	drm_agp_head_t *agp;	/**< AGP data */

	struct pci_dev *pdev;		/**< PCI device structure */
	int pci_domain;			/**< PCI bus domain number */
	int pci_bus;			/**< PCI bus number */
	int pci_slot;			/**< PCI slot number */
	int pci_func;			/**< PCI function number */
#ifdef __alpha__
	struct pci_controller *hose;
#endif
	drm_sg_mem_t *sg;	/**< Scatter gather memory */
	unsigned long *ctx_bitmap;	/**< context bitmap */
	void *dev_private;		/**< device private data */
	drm_sigdata_t sigdata;	   /**< For block_all_signals */
	sigset_t sigmask;

	struct drm_driver *driver;
	drm_local_map_t *agp_buffer_map;
	unsigned int agp_buffer_token;
	drm_head_t primary;		/**< primary screen head */
} drm_device_t;

static __inline__ int drm_core_check_feature(struct drm_device *dev,
					     int feature)
{
	return ((dev->driver->driver_features & feature) ? 1 : 0);
}

#if __OS_HAS_AGP
static inline int drm_core_has_AGP(struct drm_device *dev)
{
	return drm_core_check_feature(dev, DRIVER_USE_AGP);
}
#else
#define drm_core_has_AGP(dev) (0)
#endif

#if __OS_HAS_MTRR
static inline int drm_core_has_MTRR(struct drm_device *dev)
{
	return drm_core_check_feature(dev, DRIVER_USE_MTRR);
}

#define DRM_MTRR_WC		MTRR_TYPE_WRCOMB

static inline int drm_mtrr_add(unsigned long offset, unsigned long size,
			       unsigned int flags)
{
	return mtrr_add(offset, size, flags, 1);
}

static inline int drm_mtrr_del(int handle, unsigned long offset,
			       unsigned long size, unsigned int flags)
{
	return mtrr_del(handle, offset, size);
}

#else
#define drm_core_has_MTRR(dev) (0)

#define DRM_MTRR_WC		0

static inline int drm_mtrr_add(unsigned long offset, unsigned long size,
			       unsigned int flags)
{
	return 0;
}

static inline int drm_mtrr_del(int handle, unsigned long offset,
			       unsigned long size, unsigned int flags)
{
	return 0;
}
#endif

/******************************************************************/
/** \name Internal function definitions */
/*@{*/

				/* Driver support (drm_drv.h) */
extern int drm_init(struct drm_driver *driver);
extern void drm_exit(struct drm_driver *driver);
extern int drm_ioctl(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg);
extern long drm_compat_ioctl(struct file *filp,
			     unsigned int cmd, unsigned long arg);
extern int drm_lastclose(drm_device_t *dev);

				/* Device support (drm_fops.h) */
extern int drm_open(struct inode *inode, struct file *filp);
extern int drm_stub_open(struct inode *inode, struct file *filp);
extern int drm_fasync(int fd, struct file *filp, int on);
extern int drm_release(struct inode *inode, struct file *filp);

				/* Mapping support (drm_vm.h) */
extern int drm_mmap(struct file *filp, struct vm_area_struct *vma);
extern unsigned int drm_poll(struct file *filp, struct poll_table_struct *wait);

				/* Memory management support (drm_memory.h) */
#include "drm_memory.h"
extern void drm_mem_init(void);
extern int drm_mem_info(char *buf, char **start, off_t offset,
			int request, int *eof, void *data);
extern void *drm_realloc(void *oldpt, size_t oldsize, size_t size, int area);
extern unsigned long drm_alloc_pages(int order, int area);
extern void drm_free_pages(unsigned long address, int order, int area);
extern void *drm_ioremap(unsigned long offset, unsigned long size,
			 drm_device_t * dev);
extern void *drm_ioremap_nocache(unsigned long offset, unsigned long size,
				 drm_device_t * dev);
extern void drm_ioremapfree(void *pt, unsigned long size, drm_device_t * dev);

extern DRM_AGP_MEM *drm_alloc_agp(drm_device_t * dev, int pages, u32 type);
extern int drm_free_agp(DRM_AGP_MEM * handle, int pages);
extern int drm_bind_agp(DRM_AGP_MEM * handle, unsigned int start);
extern int drm_unbind_agp(DRM_AGP_MEM * handle);

				/* Misc. IOCTL support (drm_ioctl.h) */
extern int drm_irq_by_busid(struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg);
extern int drm_getunique(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);
extern int drm_setunique(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);
extern int drm_getmap(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg);
extern int drm_getclient(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);
extern int drm_getstats(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int drm_setversion(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int drm_noop(struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg);

				/* Context IOCTL support (drm_context.h) */
extern int drm_resctx(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg);
extern int drm_addctx(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg);
extern int drm_modctx(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg);
extern int drm_getctx(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg);
extern int drm_switchctx(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);
extern int drm_newctx(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg);
extern int drm_rmctx(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg);

extern int drm_ctxbitmap_init(drm_device_t * dev);
extern void drm_ctxbitmap_cleanup(drm_device_t * dev);
extern void drm_ctxbitmap_free(drm_device_t * dev, int ctx_handle);

extern int drm_setsareactx(struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg);
extern int drm_getsareactx(struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg);

				/* Drawable IOCTL support (drm_drawable.h) */
extern int drm_adddraw(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg);
extern int drm_rmdraw(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg);

				/* Authentication IOCTL support (drm_auth.h) */
extern int drm_getmagic(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int drm_authmagic(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);

				/* Locking IOCTL support (drm_lock.h) */
extern int drm_lock(struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg);
extern int drm_unlock(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg);
extern int drm_lock_take(__volatile__ unsigned int *lock, unsigned int context);
extern int drm_lock_free(drm_device_t * dev,
			 __volatile__ unsigned int *lock, unsigned int context);

				/* Buffer management support (drm_bufs.h) */
extern int drm_addbufs_agp(drm_device_t * dev, drm_buf_desc_t * request);
extern int drm_addbufs_pci(drm_device_t * dev, drm_buf_desc_t * request);
extern int drm_addbufs_fb(drm_device_t *dev, drm_buf_desc_t *request);
extern int drm_addmap(drm_device_t * dev, unsigned int offset,
		      unsigned int size, drm_map_type_t type,
		      drm_map_flags_t flags, drm_local_map_t ** map_ptr);
extern int drm_addmap_ioctl(struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg);
extern int drm_rmmap(drm_device_t * dev, drm_local_map_t * map);
extern int drm_rmmap_locked(drm_device_t * dev, drm_local_map_t * map);
extern int drm_rmmap_ioctl(struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg);

extern int drm_order(unsigned long size);
extern int drm_addbufs(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg);
extern int drm_infobufs(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int drm_markbufs(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int drm_freebufs(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int drm_mapbufs(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg);
extern unsigned long drm_get_resource_start(drm_device_t * dev,
					    unsigned int resource);
extern unsigned long drm_get_resource_len(drm_device_t * dev,
					  unsigned int resource);

				/* DMA support (drm_dma.h) */
extern int drm_dma_setup(drm_device_t * dev);
extern void drm_dma_takedown(drm_device_t * dev);
extern void drm_free_buffer(drm_device_t * dev, drm_buf_t * buf);
extern void drm_core_reclaim_buffers(drm_device_t * dev, struct file *filp);

				/* IRQ support (drm_irq.h) */
extern int drm_control(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg);
extern irqreturn_t drm_irq_handler(DRM_IRQ_ARGS);
extern int drm_irq_uninstall(drm_device_t * dev);
extern void drm_driver_irq_preinstall(drm_device_t * dev);
extern void drm_driver_irq_postinstall(drm_device_t * dev);
extern void drm_driver_irq_uninstall(drm_device_t * dev);

extern int drm_wait_vblank(struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg);
extern int drm_vblank_wait(drm_device_t * dev, unsigned int *vbl_seq);
extern void drm_vbl_send_signals(drm_device_t * dev);

				/* AGP/GART support (drm_agpsupport.h) */
extern drm_agp_head_t *drm_agp_init(drm_device_t * dev);
extern int drm_agp_acquire(drm_device_t * dev);
extern int drm_agp_acquire_ioctl(struct inode *inode, struct file *filp,
				 unsigned int cmd, unsigned long arg);
extern int drm_agp_release(drm_device_t * dev);
extern int drm_agp_release_ioctl(struct inode *inode, struct file *filp,
				 unsigned int cmd, unsigned long arg);
extern int drm_agp_enable(drm_device_t * dev, drm_agp_mode_t mode);
extern int drm_agp_enable_ioctl(struct inode *inode, struct file *filp,
				unsigned int cmd, unsigned long arg);
extern int drm_agp_info(drm_device_t * dev, drm_agp_info_t * info);
extern int drm_agp_info_ioctl(struct inode *inode, struct file *filp,
			      unsigned int cmd, unsigned long arg);
extern int drm_agp_alloc(drm_device_t *dev, drm_agp_buffer_t *request);
extern int drm_agp_alloc_ioctl(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);
extern int drm_agp_free(drm_device_t *dev, drm_agp_buffer_t *request);
extern int drm_agp_free_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int drm_agp_unbind(drm_device_t *dev, drm_agp_binding_t *request);
extern int drm_agp_unbind_ioctl(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int drm_agp_bind(drm_device_t *dev, drm_agp_binding_t *request);
extern int drm_agp_bind_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern DRM_AGP_MEM *drm_agp_allocate_memory(struct agp_bridge_data *bridge,
					    size_t pages, u32 type);
extern int drm_agp_free_memory(DRM_AGP_MEM * handle);
extern int drm_agp_bind_memory(DRM_AGP_MEM * handle, off_t start);
extern int drm_agp_unbind_memory(DRM_AGP_MEM * handle);

				/* Stub support (drm_stub.h) */
extern int drm_get_dev(struct pci_dev *pdev, const struct pci_device_id *ent,
		       struct drm_driver *driver);
extern int drm_put_dev(drm_device_t * dev);
extern int drm_put_head(drm_head_t * head);
extern unsigned int drm_debug;
extern unsigned int drm_cards_limit;
extern drm_head_t **drm_heads;
extern struct class *drm_class;
extern struct proc_dir_entry *drm_proc_root;

				/* Proc support (drm_proc.h) */
extern int drm_proc_init(drm_device_t * dev,
			 int minor,
			 struct proc_dir_entry *root,
			 struct proc_dir_entry **dev_root);
extern int drm_proc_cleanup(int minor,
			    struct proc_dir_entry *root,
			    struct proc_dir_entry *dev_root);

				/* Scatter Gather Support (drm_scatter.h) */
extern void drm_sg_cleanup(drm_sg_mem_t * entry);
extern int drm_sg_alloc(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int drm_sg_free(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg);

			       /* ATI PCIGART support (ati_pcigart.h) */
extern int drm_ati_pcigart_init(drm_device_t * dev,
				drm_ati_pcigart_info * gart_info);
extern int drm_ati_pcigart_cleanup(drm_device_t * dev,
				   drm_ati_pcigart_info * gart_info);

extern drm_dma_handle_t *drm_pci_alloc(drm_device_t * dev, size_t size,
				       size_t align, dma_addr_t maxaddr);
extern void __drm_pci_free(drm_device_t * dev, drm_dma_handle_t * dmah);
extern void drm_pci_free(drm_device_t * dev, drm_dma_handle_t * dmah);

			       /* sysfs support (drm_sysfs.c) */
extern struct class *drm_sysfs_create(struct module *owner, char *name);
extern void drm_sysfs_destroy(struct class *cs);
extern struct class_device *drm_sysfs_device_add(struct class *cs,
						 drm_head_t *head);
extern void drm_sysfs_device_remove(struct class_device *class_dev);

/* Inline replacements for DRM_IOREMAP macros */
static __inline__ void drm_core_ioremap(struct drm_map *map,
					struct drm_device *dev)
{
	map->handle = drm_ioremap(map->offset, map->size, dev);
}

static __inline__ void drm_core_ioremap_nocache(struct drm_map *map,
						struct drm_device *dev)
{
	map->handle = drm_ioremap_nocache(map->offset, map->size, dev);
}

static __inline__ void drm_core_ioremapfree(struct drm_map *map,
					    struct drm_device *dev)
{
	if (map->handle && map->size)
		drm_ioremapfree(map->handle, map->size, dev);
}

static __inline__ struct drm_map *drm_core_findmap(struct drm_device *dev,
						   unsigned int token)
{
	drm_map_list_t *_entry;
	list_for_each_entry(_entry, &dev->maplist->head, head)
	    if (_entry->user_token == token)
		return _entry->map;
	return NULL;
}

static __inline__ int drm_device_is_agp(drm_device_t * dev)
{
	if (dev->driver->device_is_agp != NULL) {
		int err = (*dev->driver->device_is_agp) (dev);

		if (err != 2) {
			return err;
		}
	}

	return pci_find_capability(dev->pdev, PCI_CAP_ID_AGP);
}

static __inline__ int drm_device_is_pcie(drm_device_t * dev)
{
	return pci_find_capability(dev->pdev, PCI_CAP_ID_EXP);
}

static __inline__ void drm_core_dropmap(struct drm_map *map)
{
}

#ifndef DEBUG_MEMORY
/** Wrapper around kmalloc() */
static __inline__ void *drm_alloc(size_t size, int area)
{
	return kmalloc(size, GFP_KERNEL);
}

/** Wrapper around kfree() */
static __inline__ void drm_free(void *pt, size_t size, int area)
{
	kfree(pt);
}

/** Wrapper around kcalloc() */
static __inline__ void *drm_calloc(size_t nmemb, size_t size, int area)
{
	return kcalloc(nmemb, size, GFP_KERNEL);
}
#else
extern void *drm_alloc(size_t size, int area);
extern void drm_free(void *pt, size_t size, int area);
extern void *drm_calloc(size_t nmemb, size_t size, int area);
#endif

/*@}*/

extern unsigned long drm_core_get_map_ofs(drm_map_t * map);
extern unsigned long drm_core_get_reg_ofs(struct drm_device *dev);

#ifndef pci_pretty_name
#define pci_pretty_name(dev) ""
#endif

#endif				/* __KERNEL__ */
#endif
