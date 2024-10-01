/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __CARD_BASE_H__
#define __CARD_BASE_H__

/**
 * IBM Accelerator Family 'GenWQE'
 *
 * (C) Copyright IBM Corp. 2013
 *
 * Author: Frank Haverkamp <haver@linux.vnet.ibm.com>
 * Author: Joerg-Stephan Vogt <jsvogt@de.ibm.com>
 * Author: Michael Jung <mijung@gmx.net>
 * Author: Michael Ruettger <michael@ibmra.de>
 */

/*
 * Interfaces within the GenWQE module. Defines genwqe_card and
 * ddcb_queue as well as ddcb_requ.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/stringify.h>
#include <linux/pci.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/slab.h>

#include <linux/genwqe/genwqe_card.h>
#include "genwqe_driver.h"

#define GENWQE_MSI_IRQS			4  /* Just one supported, no MSIx */

#define GENWQE_MAX_VFS			15 /* maximum 15 VFs are possible */
#define GENWQE_MAX_FUNCS		16 /* 1 PF and 15 VFs */
#define GENWQE_CARD_NO_MAX		(16 * GENWQE_MAX_FUNCS)

/* Compile parameters, some of them appear in debugfs for later adjustment */
#define GENWQE_DDCB_MAX			32 /* DDCBs on the work-queue */
#define GENWQE_POLLING_ENABLED		0  /* in case of irqs not working */
#define GENWQE_DDCB_SOFTWARE_TIMEOUT	10 /* timeout per DDCB in seconds */
#define GENWQE_KILL_TIMEOUT		8  /* time until process gets killed */
#define GENWQE_VF_JOBTIMEOUT_MSEC	250  /* 250 msec */
#define GENWQE_PF_JOBTIMEOUT_MSEC	8000 /* 8 sec should be ok */
#define GENWQE_HEALTH_CHECK_INTERVAL	4 /* <= 0: disabled */

/* Sysfs attribute groups used when we create the genwqe device */
extern const struct attribute_group *genwqe_attribute_groups[];

/*
 * Config space for Genwqe5 A7:
 * 00:[14 10 4b 04]40 00 10 00[00 00 00 12]00 00 00 00
 * 10: 0c 00 00 f0 07 3c 00 00 00 00 00 00 00 00 00 00
 * 20: 00 00 00 00 00 00 00 00 00 00 00 00[14 10 4b 04]
 * 30: 00 00 00 00 50 00 00 00 00 00 00 00 00 00 00 00
 */
#define PCI_DEVICE_GENWQE		0x044b /* Genwqe DeviceID */

#define PCI_SUBSYSTEM_ID_GENWQE5	0x035f /* Genwqe A5 Subsystem-ID */
#define PCI_SUBSYSTEM_ID_GENWQE5_NEW	0x044b /* Genwqe A5 Subsystem-ID */
#define PCI_CLASSCODE_GENWQE5		0x1200 /* UNKNOWN */

#define PCI_SUBVENDOR_ID_IBM_SRIOV	0x0000
#define PCI_SUBSYSTEM_ID_GENWQE5_SRIOV	0x0000 /* Genwqe A5 Subsystem-ID */
#define PCI_CLASSCODE_GENWQE5_SRIOV	0x1200 /* UNKNOWN */

#define	GENWQE_SLU_ARCH_REQ		2 /* Required SLU architecture level */

/**
 * struct genwqe_reg - Genwqe data dump functionality
 */
struct genwqe_reg {
	u32 addr;
	u32 idx;
	u64 val;
};

/*
 * enum genwqe_dbg_type - Specify chip unit to dump/debug
 */
enum genwqe_dbg_type {
	GENWQE_DBG_UNIT0 = 0,  /* captured before prev errs cleared */
	GENWQE_DBG_UNIT1 = 1,
	GENWQE_DBG_UNIT2 = 2,
	GENWQE_DBG_UNIT3 = 3,
	GENWQE_DBG_UNIT4 = 4,
	GENWQE_DBG_UNIT5 = 5,
	GENWQE_DBG_UNIT6 = 6,
	GENWQE_DBG_UNIT7 = 7,
	GENWQE_DBG_REGS  = 8,
	GENWQE_DBG_DMA   = 9,
	GENWQE_DBG_UNITS = 10, /* max number of possible debug units  */
};

/* Software error injection to simulate card failures */
#define GENWQE_INJECT_HARDWARE_FAILURE	0x00000001 /* injects -1 reg reads */
#define GENWQE_INJECT_BUS_RESET_FAILURE 0x00000002 /* pci_bus_reset fail */
#define GENWQE_INJECT_GFIR_FATAL	0x00000004 /* GFIR = 0x0000ffff */
#define GENWQE_INJECT_GFIR_INFO		0x00000008 /* GFIR = 0xffff0000 */

/*
 * Genwqe card description and management data.
 *
 * Error-handling in case of card malfunction
 * ------------------------------------------
 *
 * If the card is detected to be defective the outside environment
 * will cause the PCI layer to call deinit (the cleanup function for
 * probe). This is the same effect like doing a unbind/bind operation
 * on the card.
 *
 * The genwqe card driver implements a health checking thread which
 * verifies the card function. If this detects a problem the cards
 * device is being shutdown and restarted again, along with a reset of
 * the card and queue.
 *
 * All functions accessing the card device return either -EIO or -ENODEV
 * code to indicate the malfunction to the user. The user has to close
 * the file descriptor and open a new one, once the card becomes
 * available again.
 *
 * If the open file descriptor is setup to receive SIGIO, the signal is
 * genereated for the application which has to provide a handler to
 * react on it. If the application does not close the open
 * file descriptor a SIGKILL is send to enforce freeing the cards
 * resources.
 *
 * I did not find a different way to prevent kernel problems due to
 * reference counters for the cards character devices getting out of
 * sync. The character device deallocation does not block, even if
 * there is still an open file descriptor pending. If this pending
 * descriptor is closed, the data structures used by the character
 * device is reinstantiated, which will lead to the reference counter
 * dropping below the allowed values.
 *
 * Card recovery
 * -------------
 *
 * To test the internal driver recovery the following command can be used:
 *   sudo sh -c 'echo 0xfffff > /sys/class/genwqe/genwqe0_card/err_inject'
 */


/**
 * struct dma_mapping_type - Mapping type definition
 *
 * To avoid memcpying data arround we use user memory directly. To do
 * this we need to pin/swap-in the memory and request a DMA address
 * for it.
 */
enum dma_mapping_type {
	GENWQE_MAPPING_RAW = 0,		/* contignous memory buffer */
	GENWQE_MAPPING_SGL_TEMP,	/* sglist dynamically used */
	GENWQE_MAPPING_SGL_PINNED,	/* sglist used with pinning */
};

/**
 * struct dma_mapping - Information about memory mappings done by the driver
 */
struct dma_mapping {
	enum dma_mapping_type type;

	void *u_vaddr;			/* user-space vaddr/non-aligned */
	void *k_vaddr;			/* kernel-space vaddr/non-aligned */
	dma_addr_t dma_addr;		/* physical DMA address */

	struct page **page_list;	/* list of pages used by user buff */
	dma_addr_t *dma_list;		/* list of dma addresses per page */
	unsigned int nr_pages;		/* number of pages */
	unsigned int size;		/* size in bytes */

	struct list_head card_list;	/* list of usr_maps for card */
	struct list_head pin_list;	/* list of pinned memory for dev */
	int write;			/* writable map? useful in unmapping */
};

static inline void genwqe_mapping_init(struct dma_mapping *m,
				       enum dma_mapping_type type)
{
	memset(m, 0, sizeof(*m));
	m->type = type;
	m->write = 1; /* Assume the maps we create are R/W */
}

/**
 * struct ddcb_queue - DDCB queue data
 * @ddcb_max:          Number of DDCBs on the queue
 * @ddcb_next:         Next free DDCB
 * @ddcb_act:          Next DDCB supposed to finish
 * @ddcb_seq:          Sequence number of last DDCB
 * @ddcbs_in_flight:   Currently enqueued DDCBs
 * @ddcbs_completed:   Number of already completed DDCBs
 * @return_on_busy:    Number of -EBUSY returns on full queue
 * @wait_on_busy:      Number of waits on full queue
 * @ddcb_daddr:        DMA address of first DDCB in the queue
 * @ddcb_vaddr:        Kernel virtual address of first DDCB in the queue
 * @ddcb_req:          Associated requests (one per DDCB)
 * @ddcb_waitqs:       Associated wait queues (one per DDCB)
 * @ddcb_lock:         Lock to protect queuing operations
 * @ddcb_waitq:        Wait on next DDCB finishing
 */

struct ddcb_queue {
	int ddcb_max;			/* amount of DDCBs  */
	int ddcb_next;			/* next available DDCB num */
	int ddcb_act;			/* DDCB to be processed */
	u16 ddcb_seq;			/* slc seq num */
	unsigned int ddcbs_in_flight;	/* number of ddcbs in processing */
	unsigned int ddcbs_completed;
	unsigned int ddcbs_max_in_flight;
	unsigned int return_on_busy;    /* how many times -EBUSY? */
	unsigned int wait_on_busy;

	dma_addr_t ddcb_daddr;		/* DMA address */
	struct ddcb *ddcb_vaddr;	/* kernel virtual addr for DDCBs */
	struct ddcb_requ **ddcb_req;	/* ddcb processing parameter */
	wait_queue_head_t *ddcb_waitqs; /* waitqueue per ddcb */

	spinlock_t ddcb_lock;		/* exclusive access to queue */
	wait_queue_head_t busy_waitq;   /* wait for ddcb processing */

	/* registers or the respective queue to be used */
	u32 IO_QUEUE_CONFIG;
	u32 IO_QUEUE_STATUS;
	u32 IO_QUEUE_SEGMENT;
	u32 IO_QUEUE_INITSQN;
	u32 IO_QUEUE_WRAP;
	u32 IO_QUEUE_OFFSET;
	u32 IO_QUEUE_WTIME;
	u32 IO_QUEUE_ERRCNTS;
	u32 IO_QUEUE_LRW;
};

/*
 * GFIR, SLU_UNITCFG, APP_UNITCFG
 *   8 Units with FIR/FEC + 64 * 2ndary FIRS/FEC.
 */
#define GENWQE_FFDC_REGS	(3 + (8 * (2 + 2 * 64)))

struct genwqe_ffdc {
	unsigned int entries;
	struct genwqe_reg *regs;
};

/**
 * struct genwqe_dev - GenWQE device information
 * @card_state:       Card operation state, see above
 * @ffdc:             First Failure Data Capture buffers for each unit
 * @card_thread:      Working thread to operate the DDCB queue
 * @card_waitq:       Wait queue used in card_thread
 * @queue:            DDCB queue
 * @health_thread:    Card monitoring thread (only for PFs)
 * @health_waitq:     Wait queue used in health_thread
 * @pci_dev:          Associated PCI device (function)
 * @mmio:             Base address of 64-bit register space
 * @mmio_len:         Length of register area
 * @file_lock:        Lock to protect access to file_list
 * @file_list:        List of all processes with open GenWQE file descriptors
 *
 * This struct contains all information needed to communicate with a
 * GenWQE card. It is initialized when a GenWQE device is found and
 * destroyed when it goes away. It holds data to maintain the queue as
 * well as data needed to feed the user interfaces.
 */
struct genwqe_dev {
	enum genwqe_card_state card_state;
	spinlock_t print_lock;

	int card_idx;			/* card index 0..CARD_NO_MAX-1 */
	u64 flags;			/* general flags */

	/* FFDC data gathering */
	struct genwqe_ffdc ffdc[GENWQE_DBG_UNITS];

	/* DDCB workqueue */
	struct task_struct *card_thread;
	wait_queue_head_t queue_waitq;
	struct ddcb_queue queue;	/* genwqe DDCB queue */
	unsigned int irqs_processed;

	/* Card health checking thread */
	struct task_struct *health_thread;
	wait_queue_head_t health_waitq;

	int use_platform_recovery;	/* use platform recovery mechanisms */

	/* char device */
	dev_t  devnum_genwqe;		/* major/minor num card */
	struct class *class_genwqe;	/* reference to class object */
	struct device *dev;		/* for device creation */
	struct cdev cdev_genwqe;	/* char device for card */

	struct dentry *debugfs_root;	/* debugfs card root directory */
	struct dentry *debugfs_genwqe;	/* debugfs driver root directory */

	/* pci resources */
	struct pci_dev *pci_dev;	/* PCI device */
	void __iomem *mmio;		/* BAR-0 MMIO start */
	unsigned long mmio_len;
	int num_vfs;
	u32 vf_jobtimeout_msec[GENWQE_MAX_VFS];
	int is_privileged;		/* access to all regs possible */

	/* config regs which we need often */
	u64 slu_unitcfg;
	u64 app_unitcfg;
	u64 softreset;
	u64 err_inject;
	u64 last_gfir;
	char app_name[5];

	spinlock_t file_lock;		/* lock for open files */
	struct list_head file_list;	/* list of open files */

	/* debugfs parameters */
	int ddcb_software_timeout;	/* wait until DDCB times out */
	int skip_recovery;		/* circumvention if recovery fails */
	int kill_timeout;		/* wait after sending SIGKILL */
};

/**
 * enum genwqe_requ_state - State of a DDCB execution request
 */
enum genwqe_requ_state {
	GENWQE_REQU_NEW      = 0,
	GENWQE_REQU_ENQUEUED = 1,
	GENWQE_REQU_TAPPED   = 2,
	GENWQE_REQU_FINISHED = 3,
	GENWQE_REQU_STATE_MAX,
};

/**
 * struct genwqe_sgl - Scatter gather list describing user-space memory
 * @sgl:            scatter gather list needs to be 128 byte aligned
 * @sgl_dma_addr:   dma address of sgl
 * @sgl_size:       size of area used for sgl
 * @user_addr:      user-space address of memory area
 * @user_size:      size of user-space memory area
 * @page:           buffer for partial pages if needed
 * @page_dma_addr:  dma address partial pages
 * @write:          should we write it back to userspace?
 */
struct genwqe_sgl {
	dma_addr_t sgl_dma_addr;
	struct sg_entry *sgl;
	size_t sgl_size;	/* size of sgl */

	void __user *user_addr; /* user-space base-address */
	size_t user_size;       /* size of memory area */

	int write;

	unsigned long nr_pages;
	unsigned long fpage_offs;
	size_t fpage_size;
	size_t lpage_size;

	void *fpage;
	dma_addr_t fpage_dma_addr;

	void *lpage;
	dma_addr_t lpage_dma_addr;
};

int genwqe_alloc_sync_sgl(struct genwqe_dev *cd, struct genwqe_sgl *sgl,
			  void __user *user_addr, size_t user_size, int write);

int genwqe_setup_sgl(struct genwqe_dev *cd, struct genwqe_sgl *sgl,
		     dma_addr_t *dma_list);

int genwqe_free_sync_sgl(struct genwqe_dev *cd, struct genwqe_sgl *sgl);

/**
 * struct ddcb_requ - Kernel internal representation of the DDCB request
 * @cmd:          User space representation of the DDCB execution request
 */
struct ddcb_requ {
	/* kernel specific content */
	enum genwqe_requ_state req_state; /* request status */
	int num;			  /* ddcb_no for this request */
	struct ddcb_queue *queue;	  /* associated queue */

	struct dma_mapping  dma_mappings[DDCB_FIXUPS];
	struct genwqe_sgl sgls[DDCB_FIXUPS];

	/* kernel/user shared content */
	struct genwqe_ddcb_cmd cmd;	/* ddcb_no for this request */
	struct genwqe_debug_data debug_data;
};

/**
 * struct genwqe_file - Information for open GenWQE devices
 */
struct genwqe_file {
	struct genwqe_dev *cd;
	struct genwqe_driver *client;
	struct file *filp;

	struct fasync_struct *async_queue;
	struct pid *opener;
	struct list_head list;		/* entry in list of open files */

	spinlock_t map_lock;		/* lock for dma_mappings */
	struct list_head map_list;	/* list of dma_mappings */

	spinlock_t pin_lock;		/* lock for pinned memory */
	struct list_head pin_list;	/* list of pinned memory */
};

int  genwqe_setup_service_layer(struct genwqe_dev *cd); /* for PF only */
int  genwqe_finish_queue(struct genwqe_dev *cd);
int  genwqe_release_service_layer(struct genwqe_dev *cd);

/**
 * genwqe_get_slu_id() - Read Service Layer Unit Id
 * Return: 0x00: Development code
 *         0x01: SLC1 (old)
 *         0x02: SLC2 (sept2012)
 *         0x03: SLC2 (feb2013, generic driver)
 */
static inline int genwqe_get_slu_id(struct genwqe_dev *cd)
{
	return (int)((cd->slu_unitcfg >> 32) & 0xff);
}

int  genwqe_ddcbs_in_flight(struct genwqe_dev *cd);

u8   genwqe_card_type(struct genwqe_dev *cd);
int  genwqe_card_reset(struct genwqe_dev *cd);
int  genwqe_set_interrupt_capability(struct genwqe_dev *cd, int count);
void genwqe_reset_interrupt_capability(struct genwqe_dev *cd);

int  genwqe_device_create(struct genwqe_dev *cd);
int  genwqe_device_remove(struct genwqe_dev *cd);

/* debugfs */
void genwqe_init_debugfs(struct genwqe_dev *cd);
void genqwe_exit_debugfs(struct genwqe_dev *cd);

int  genwqe_read_softreset(struct genwqe_dev *cd);

/* Hardware Circumventions */
int  genwqe_recovery_on_fatal_gfir_required(struct genwqe_dev *cd);
int  genwqe_flash_readback_fails(struct genwqe_dev *cd);

/**
 * genwqe_write_vreg() - Write register in VF window
 * @cd:    genwqe device
 * @reg:   register address
 * @val:   value to write
 * @func:  0: PF, 1: VF0, ..., 15: VF14
 */
int genwqe_write_vreg(struct genwqe_dev *cd, u32 reg, u64 val, int func);

/**
 * genwqe_read_vreg() - Read register in VF window
 * @cd:    genwqe device
 * @reg:   register address
 * @func:  0: PF, 1: VF0, ..., 15: VF14
 *
 * Return: content of the register
 */
u64 genwqe_read_vreg(struct genwqe_dev *cd, u32 reg, int func);

/* FFDC Buffer Management */
int  genwqe_ffdc_buff_size(struct genwqe_dev *cd, int unit_id);
int  genwqe_ffdc_buff_read(struct genwqe_dev *cd, int unit_id,
			   struct genwqe_reg *regs, unsigned int max_regs);
int  genwqe_read_ffdc_regs(struct genwqe_dev *cd, struct genwqe_reg *regs,
			   unsigned int max_regs, int all);
int  genwqe_ffdc_dump_dma(struct genwqe_dev *cd,
			  struct genwqe_reg *regs, unsigned int max_regs);

int  genwqe_init_debug_data(struct genwqe_dev *cd,
			    struct genwqe_debug_data *d);

void genwqe_init_crc32(void);
int  genwqe_read_app_id(struct genwqe_dev *cd, char *app_name, int len);

/* Memory allocation/deallocation; dma address handling */
int  genwqe_user_vmap(struct genwqe_dev *cd, struct dma_mapping *m,
		      void *uaddr, unsigned long size);

int  genwqe_user_vunmap(struct genwqe_dev *cd, struct dma_mapping *m);

static inline bool dma_mapping_used(struct dma_mapping *m)
{
	if (!m)
		return false;
	return m->size != 0;
}

/**
 * __genwqe_execute_ddcb() - Execute DDCB request with addr translation
 *
 * This function will do the address translation changes to the DDCBs
 * according to the definitions required by the ATS field. It looks up
 * the memory allocation buffer or does vmap/vunmap for the respective
 * user-space buffers, inclusive page pinning and scatter gather list
 * buildup and teardown.
 */
int  __genwqe_execute_ddcb(struct genwqe_dev *cd,
			   struct genwqe_ddcb_cmd *cmd, unsigned int f_flags);

/**
 * __genwqe_execute_raw_ddcb() - Execute DDCB request without addr translation
 *
 * This version will not do address translation or any modification of
 * the DDCB data. It is used e.g. for the MoveFlash DDCB which is
 * entirely prepared by the driver itself. That means the appropriate
 * DMA addresses are already in the DDCB and do not need any
 * modification.
 */
int  __genwqe_execute_raw_ddcb(struct genwqe_dev *cd,
			       struct genwqe_ddcb_cmd *cmd,
			       unsigned int f_flags);
int  __genwqe_enqueue_ddcb(struct genwqe_dev *cd,
			   struct ddcb_requ *req,
			   unsigned int f_flags);

int  __genwqe_wait_ddcb(struct genwqe_dev *cd, struct ddcb_requ *req);
int  __genwqe_purge_ddcb(struct genwqe_dev *cd, struct ddcb_requ *req);

/* register access */
int __genwqe_writeq(struct genwqe_dev *cd, u64 byte_offs, u64 val);
u64 __genwqe_readq(struct genwqe_dev *cd, u64 byte_offs);
int __genwqe_writel(struct genwqe_dev *cd, u64 byte_offs, u32 val);
u32 __genwqe_readl(struct genwqe_dev *cd, u64 byte_offs);

void *__genwqe_alloc_consistent(struct genwqe_dev *cd, size_t size,
				 dma_addr_t *dma_handle);
void __genwqe_free_consistent(struct genwqe_dev *cd, size_t size,
			      void *vaddr, dma_addr_t dma_handle);

/* Base clock frequency in MHz */
int  genwqe_base_clock_frequency(struct genwqe_dev *cd);

/* Before FFDC is captured the traps should be stopped. */
void genwqe_stop_traps(struct genwqe_dev *cd);
void genwqe_start_traps(struct genwqe_dev *cd);

/* Hardware circumvention */
bool genwqe_need_err_masking(struct genwqe_dev *cd);

/**
 * genwqe_is_privileged() - Determine operation mode for PCI function
 *
 * On Intel with SRIOV support we see:
 *   PF: is_physfn = 1 is_virtfn = 0
 *   VF: is_physfn = 0 is_virtfn = 1
 *
 * On Systems with no SRIOV support _and_ virtualized systems we get:
 *       is_physfn = 0 is_virtfn = 0
 *
 * Other vendors have individual pci device ids to distinguish between
 * virtual function drivers and physical function drivers. GenWQE
 * unfortunately has just on pci device id for both, VFs and PF.
 *
 * The following code is used to distinguish if the card is running in
 * privileged mode, either as true PF or in a virtualized system with
 * full register access e.g. currently on PowerPC.
 *
 * if (pci_dev->is_virtfn)
 *          cd->is_privileged = 0;
 *  else
 *          cd->is_privileged = (__genwqe_readq(cd, IO_SLU_BITSTREAM)
 *				 != IO_ILLEGAL_VALUE);
 */
static inline int genwqe_is_privileged(struct genwqe_dev *cd)
{
	return cd->is_privileged;
}

#endif	/* __CARD_BASE_H__ */
