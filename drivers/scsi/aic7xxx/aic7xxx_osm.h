/*
 * Adaptec AIC7xxx device driver for Linux.
 *
 * Copyright (c) 1994 John Aycock
 *   The University of Calgary Department of Computer Science.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * Copyright (c) 2000-2003 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/aic7xxx/linux/drivers/scsi/aic7xxx/aic7xxx_osm.h#151 $
 *
 */
#ifndef _AIC7XXX_LINUX_H_
#define _AIC7XXX_LINUX_H_

#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/smp_lock.h>
#include <linux/version.h>
#include <linux/module.h>
#include <asm/byteorder.h>
#include <asm/io.h>

#include <linux/interrupt.h> /* For tasklet support. */
#include <linux/config.h>
#include <linux/slab.h>

/* Core SCSI definitions */
#define AIC_LIB_PREFIX ahc
#include "scsi.h"
#include <scsi/scsi_host.h>

/* Name space conflict with BSD queue macros */
#ifdef LIST_HEAD
#undef LIST_HEAD
#endif

#include "cam.h"
#include "queue.h"
#include "scsi_message.h"
#include "aiclib.h"

/*********************************** Debugging ********************************/
#ifdef CONFIG_AIC7XXX_DEBUG_ENABLE
#ifdef CONFIG_AIC7XXX_DEBUG_MASK
#define AHC_DEBUG 1
#define AHC_DEBUG_OPTS CONFIG_AIC7XXX_DEBUG_MASK
#else
/*
 * Compile in debugging code, but do not enable any printfs.
 */
#define AHC_DEBUG 1
#endif
/* No debugging code. */
#endif

/************************* Forward Declarations *******************************/
struct ahc_softc;
typedef struct pci_dev *ahc_dev_softc_t;
typedef Scsi_Cmnd      *ahc_io_ctx_t;

/******************************* Byte Order ***********************************/
#define ahc_htobe16(x)	cpu_to_be16(x)
#define ahc_htobe32(x)	cpu_to_be32(x)
#define ahc_htobe64(x)	cpu_to_be64(x)
#define ahc_htole16(x)	cpu_to_le16(x)
#define ahc_htole32(x)	cpu_to_le32(x)
#define ahc_htole64(x)	cpu_to_le64(x)

#define ahc_be16toh(x)	be16_to_cpu(x)
#define ahc_be32toh(x)	be32_to_cpu(x)
#define ahc_be64toh(x)	be64_to_cpu(x)
#define ahc_le16toh(x)	le16_to_cpu(x)
#define ahc_le32toh(x)	le32_to_cpu(x)
#define ahc_le64toh(x)	le64_to_cpu(x)

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif

#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif

#ifndef BYTE_ORDER
#if defined(__BIG_ENDIAN)
#define BYTE_ORDER BIG_ENDIAN
#endif
#if defined(__LITTLE_ENDIAN)
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#endif /* BYTE_ORDER */

/************************* Configuration Data *********************************/
extern u_int aic7xxx_no_probe;
extern u_int aic7xxx_allow_memio;
extern int aic7xxx_detect_complete;
extern Scsi_Host_Template aic7xxx_driver_template;

/***************************** Bus Space/DMA **********************************/

typedef uint32_t bus_size_t;

typedef enum {
	BUS_SPACE_MEMIO,
	BUS_SPACE_PIO
} bus_space_tag_t;

typedef union {
	u_long		  ioport;
	volatile uint8_t __iomem *maddr;
} bus_space_handle_t;

typedef struct bus_dma_segment
{
	dma_addr_t	ds_addr;
	bus_size_t	ds_len;
} bus_dma_segment_t;

struct ahc_linux_dma_tag
{
	bus_size_t	alignment;
	bus_size_t	boundary;
	bus_size_t	maxsize;
};
typedef struct ahc_linux_dma_tag* bus_dma_tag_t;

struct ahc_linux_dmamap
{
	dma_addr_t	bus_addr;
};
typedef struct ahc_linux_dmamap* bus_dmamap_t;

typedef int bus_dma_filter_t(void*, dma_addr_t);
typedef void bus_dmamap_callback_t(void *, bus_dma_segment_t *, int, int);

#define BUS_DMA_WAITOK		0x0
#define BUS_DMA_NOWAIT		0x1
#define BUS_DMA_ALLOCNOW	0x2
#define BUS_DMA_LOAD_SEGS	0x4	/*
					 * Argument is an S/G list not
					 * a single buffer.
					 */

#define BUS_SPACE_MAXADDR	0xFFFFFFFF
#define BUS_SPACE_MAXADDR_32BIT	0xFFFFFFFF
#define BUS_SPACE_MAXSIZE_32BIT	0xFFFFFFFF

int	ahc_dma_tag_create(struct ahc_softc *, bus_dma_tag_t /*parent*/,
			   bus_size_t /*alignment*/, bus_size_t /*boundary*/,
			   dma_addr_t /*lowaddr*/, dma_addr_t /*highaddr*/,
			   bus_dma_filter_t*/*filter*/, void */*filterarg*/,
			   bus_size_t /*maxsize*/, int /*nsegments*/,
			   bus_size_t /*maxsegsz*/, int /*flags*/,
			   bus_dma_tag_t */*dma_tagp*/);

void	ahc_dma_tag_destroy(struct ahc_softc *, bus_dma_tag_t /*tag*/);

int	ahc_dmamem_alloc(struct ahc_softc *, bus_dma_tag_t /*dmat*/,
			 void** /*vaddr*/, int /*flags*/,
			 bus_dmamap_t* /*mapp*/);

void	ahc_dmamem_free(struct ahc_softc *, bus_dma_tag_t /*dmat*/,
			void* /*vaddr*/, bus_dmamap_t /*map*/);

void	ahc_dmamap_destroy(struct ahc_softc *, bus_dma_tag_t /*tag*/,
			   bus_dmamap_t /*map*/);

int	ahc_dmamap_load(struct ahc_softc *ahc, bus_dma_tag_t /*dmat*/,
			bus_dmamap_t /*map*/, void * /*buf*/,
			bus_size_t /*buflen*/, bus_dmamap_callback_t *,
			void */*callback_arg*/, int /*flags*/);

int	ahc_dmamap_unload(struct ahc_softc *, bus_dma_tag_t, bus_dmamap_t);

/*
 * Operations performed by ahc_dmamap_sync().
 */
#define BUS_DMASYNC_PREREAD	0x01	/* pre-read synchronization */
#define BUS_DMASYNC_POSTREAD	0x02	/* post-read synchronization */
#define BUS_DMASYNC_PREWRITE	0x04	/* pre-write synchronization */
#define BUS_DMASYNC_POSTWRITE	0x08	/* post-write synchronization */

/*
 * XXX
 * ahc_dmamap_sync is only used on buffers allocated with
 * the pci_alloc_consistent() API.  Although I'm not sure how
 * this works on architectures with a write buffer, Linux does
 * not have an API to sync "coherent" memory.  Perhaps we need
 * to do an mb()?
 */
#define ahc_dmamap_sync(ahc, dma_tag, dmamap, offset, len, op)

/************************** Timer DataStructures ******************************/
typedef struct timer_list ahc_timer_t;

/********************************** Includes **********************************/
#ifdef CONFIG_AIC7XXX_REG_PRETTY_PRINT
#define AIC_DEBUG_REGISTERS 1
#else
#define AIC_DEBUG_REGISTERS 0
#endif
#include "aic7xxx.h"

/***************************** Timer Facilities *******************************/
#define ahc_timer_init init_timer
#define ahc_timer_stop del_timer_sync
typedef void ahc_linux_callback_t (u_long);  
static __inline void ahc_timer_reset(ahc_timer_t *timer, int usec,
				     ahc_callback_t *func, void *arg);
static __inline void ahc_scb_timer_reset(struct scb *scb, u_int usec);

static __inline void
ahc_timer_reset(ahc_timer_t *timer, int usec, ahc_callback_t *func, void *arg)
{
	struct ahc_softc *ahc;

	ahc = (struct ahc_softc *)arg;
	del_timer(timer);
	timer->data = (u_long)arg;
	timer->expires = jiffies + (usec * HZ)/1000000;
	timer->function = (ahc_linux_callback_t*)func;
	add_timer(timer);
}

static __inline void
ahc_scb_timer_reset(struct scb *scb, u_int usec)
{
	mod_timer(&scb->io_ctx->eh_timeout, jiffies + (usec * HZ)/1000000);
}

/***************************** SMP support ************************************/
#include <linux/spinlock.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0) || defined(SCSI_HAS_HOST_LOCK))
#define AHC_SCSI_HAS_HOST_LOCK 1
#else
#define AHC_SCSI_HAS_HOST_LOCK 0
#endif

#define AIC7XXX_DRIVER_VERSION "6.2.36"

/**************************** Front End Queues ********************************/
/*
 * Data structure used to cast the Linux struct scsi_cmnd to something
 * that allows us to use the queue macros.  The linux structure has
 * plenty of space to hold the links fields as required by the queue
 * macros, but the queue macors require them to have the correct type.
 */
struct ahc_cmd_internal {
	/* Area owned by the Linux scsi layer. */
	uint8_t	private[offsetof(struct scsi_cmnd, SCp.Status)];
	union {
		STAILQ_ENTRY(ahc_cmd)	ste;
		LIST_ENTRY(ahc_cmd)	le;
		TAILQ_ENTRY(ahc_cmd)	tqe;
	} links;
	uint32_t			end;
};

struct ahc_cmd {
	union {
		struct ahc_cmd_internal	icmd;
		struct scsi_cmnd	scsi_cmd;
	} un;
};

#define acmd_icmd(cmd) ((cmd)->un.icmd)
#define acmd_scsi_cmd(cmd) ((cmd)->un.scsi_cmd)
#define acmd_links un.icmd.links

/*************************** Device Data Structures ***************************/
/*
 * A per probed device structure used to deal with some error recovery
 * scenarios that the Linux mid-layer code just doesn't know how to
 * handle.  The structure allocated for a device only becomes persistent
 * after a successfully completed inquiry command to the target when
 * that inquiry data indicates a lun is present.
 */
TAILQ_HEAD(ahc_busyq, ahc_cmd);
typedef enum {
	AHC_DEV_UNCONFIGURED	 = 0x01,
	AHC_DEV_FREEZE_TIL_EMPTY = 0x02, /* Freeze queue until active == 0 */
	AHC_DEV_TIMER_ACTIVE	 = 0x04, /* Our timer is active */
	AHC_DEV_ON_RUN_LIST	 = 0x08, /* Queued to be run later */
	AHC_DEV_Q_BASIC		 = 0x10, /* Allow basic device queuing */
	AHC_DEV_Q_TAGGED	 = 0x20, /* Allow full SCSI2 command queueing */
	AHC_DEV_PERIODIC_OTAG	 = 0x40, /* Send OTAG to prevent starvation */
	AHC_DEV_SLAVE_CONFIGURED = 0x80	 /* slave_configure() has been called */
} ahc_linux_dev_flags;

struct ahc_linux_target;
struct ahc_linux_device {
	TAILQ_ENTRY(ahc_linux_device) links;
	struct		ahc_busyq busyq;

	/*
	 * The number of transactions currently
	 * queued to the device.
	 */
	int			active;

	/*
	 * The currently allowed number of 
	 * transactions that can be queued to
	 * the device.  Must be signed for
	 * conversion from tagged to untagged
	 * mode where the device may have more
	 * than one outstanding active transaction.
	 */
	int			openings;

	/*
	 * A positive count indicates that this
	 * device's queue is halted.
	 */
	u_int			qfrozen;
	
	/*
	 * Cumulative command counter.
	 */
	u_long			commands_issued;

	/*
	 * The number of tagged transactions when
	 * running at our current opening level
	 * that have been successfully received by
	 * this device since the last QUEUE FULL.
	 */
	u_int			tag_success_count;
#define AHC_TAG_SUCCESS_INTERVAL 50

	ahc_linux_dev_flags	flags;

	/*
	 * Per device timer.
	 */
	struct timer_list	timer;

	/*
	 * The high limit for the tags variable.
	 */
	u_int			maxtags;

	/*
	 * The computed number of tags outstanding
	 * at the time of the last QUEUE FULL event.
	 */
	u_int			tags_on_last_queuefull;

	/*
	 * How many times we have seen a queue full
	 * with the same number of tags.  This is used
	 * to stop our adaptive queue depth algorithm
	 * on devices with a fixed number of tags.
	 */
	u_int			last_queuefull_same_count;
#define AHC_LOCK_TAGS_COUNT 50

	/*
	 * How many transactions have been queued
	 * without the device going idle.  We use
	 * this statistic to determine when to issue
	 * an ordered tag to prevent transaction
	 * starvation.  This statistic is only updated
	 * if the AHC_DEV_PERIODIC_OTAG flag is set
	 * on this device.
	 */
	u_int			commands_since_idle_or_otag;
#define AHC_OTAG_THRESH	500

	int			lun;
	Scsi_Device	       *scsi_device;
	struct			ahc_linux_target *target;
};

typedef enum {
	AHC_DV_REQUIRED		 = 0x01,
	AHC_INQ_VALID		 = 0x02,
	AHC_BASIC_DV		 = 0x04,
	AHC_ENHANCED_DV		 = 0x08
} ahc_linux_targ_flags;

/* DV States */
typedef enum {
	AHC_DV_STATE_EXIT = 0,
	AHC_DV_STATE_INQ_SHORT_ASYNC,
	AHC_DV_STATE_INQ_ASYNC,
	AHC_DV_STATE_INQ_ASYNC_VERIFY,
	AHC_DV_STATE_TUR,
	AHC_DV_STATE_REBD,
	AHC_DV_STATE_INQ_VERIFY,
	AHC_DV_STATE_WEB,
	AHC_DV_STATE_REB,
	AHC_DV_STATE_SU,
	AHC_DV_STATE_BUSY
} ahc_dv_state;

struct ahc_linux_target {
	struct ahc_linux_device	 *devices[AHC_NUM_LUNS];
	int			  channel;
	int			  target;
	int			  refcount;
	struct ahc_transinfo	  last_tinfo;
	struct ahc_softc	 *ahc;
	ahc_linux_targ_flags	  flags;
	struct scsi_inquiry_data *inq_data;
	/*
	 * The next "fallback" period to use for narrow/wide transfers.
	 */
	uint8_t			  dv_next_narrow_period;
	uint8_t			  dv_next_wide_period;
	uint8_t			  dv_max_width;
	uint8_t			  dv_max_ppr_options;
	uint8_t			  dv_last_ppr_options;
	u_int			  dv_echo_size;
	ahc_dv_state		  dv_state;
	u_int			  dv_state_retry;
	char			 *dv_buffer;
	char			 *dv_buffer1;
};

/********************* Definitions Required by the Core ***********************/
/*
 * Number of SG segments we require.  So long as the S/G segments for
 * a particular transaction are allocated in a physically contiguous
 * manner and are allocated below 4GB, the number of S/G segments is
 * unrestricted.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
/*
 * We dynamically adjust the number of segments in pre-2.5 kernels to
 * avoid fragmentation issues in the SCSI mid-layer's private memory
 * allocator.  See aic7xxx_osm.c ahc_linux_size_nseg() for details.
 */
extern u_int ahc_linux_nseg;
#define	AHC_NSEG ahc_linux_nseg
#define	AHC_LINUX_MIN_NSEG 64
#else
#define	AHC_NSEG 128
#endif

/*
 * Per-SCB OSM storage.
 */
typedef enum {
	AHC_UP_EH_SEMAPHORE = 0x1
} ahc_linux_scb_flags;

struct scb_platform_data {
	struct ahc_linux_device	*dev;
	dma_addr_t		 buf_busaddr;
	uint32_t		 xfer_len;
	uint32_t		 sense_resid;	/* Auto-Sense residual */
	ahc_linux_scb_flags	 flags;
};

/*
 * Define a structure used for each host adapter.  All members are
 * aligned on a boundary >= the size of the member to honor the
 * alignment restrictions of the various platforms supported by
 * this driver.
 */
typedef enum {
	AHC_DV_WAIT_SIMQ_EMPTY	 = 0x01,
	AHC_DV_WAIT_SIMQ_RELEASE = 0x02,
	AHC_DV_ACTIVE		 = 0x04,
	AHC_DV_SHUTDOWN		 = 0x08,
	AHC_RUN_CMPLT_Q_TIMER	 = 0x10
} ahc_linux_softc_flags;

TAILQ_HEAD(ahc_completeq, ahc_cmd);

struct ahc_platform_data {
	/*
	 * Fields accessed from interrupt context.
	 */
	struct ahc_linux_target *targets[AHC_NUM_TARGETS]; 
	TAILQ_HEAD(, ahc_linux_device) device_runq;
	struct ahc_completeq	 completeq;

	spinlock_t		 spin_lock;
	struct tasklet_struct	 runq_tasklet;
	u_int			 qfrozen;
	pid_t			 dv_pid;
	struct timer_list	 completeq_timer;
	struct timer_list	 reset_timer;
	struct semaphore	 eh_sem;
	struct semaphore	 dv_sem;
	struct semaphore	 dv_cmd_sem;	/* XXX This needs to be in
						 * the target struct
						 */
	struct scsi_device	*dv_scsi_dev;
	struct Scsi_Host        *host;		/* pointer to scsi host */
#define AHC_LINUX_NOIRQ	((uint32_t)~0)
	uint32_t		 irq;		/* IRQ for this adapter */
	uint32_t		 bios_address;
	uint32_t		 mem_busaddr;	/* Mem Base Addr */
	uint64_t		 hw_dma_mask;
	ahc_linux_softc_flags	 flags;
};

/************************** OS Utility Wrappers *******************************/
#define printf printk
#define M_NOWAIT GFP_ATOMIC
#define M_WAITOK 0
#define malloc(size, type, flags) kmalloc(size, flags)
#define free(ptr, type) kfree(ptr)

static __inline void ahc_delay(long);
static __inline void
ahc_delay(long usec)
{
	/*
	 * udelay on Linux can have problems for
	 * multi-millisecond waits.  Wait at most
	 * 1024us per call.
	 */
	while (usec > 0) {
		udelay(usec % 1024);
		usec -= 1024;
	}
}


/***************************** Low Level I/O **********************************/
static __inline uint8_t ahc_inb(struct ahc_softc * ahc, long port);
static __inline void ahc_outb(struct ahc_softc * ahc, long port, uint8_t val);
static __inline void ahc_outsb(struct ahc_softc * ahc, long port,
			       uint8_t *, int count);
static __inline void ahc_insb(struct ahc_softc * ahc, long port,
			       uint8_t *, int count);

static __inline uint8_t
ahc_inb(struct ahc_softc * ahc, long port)
{
	uint8_t x;

	if (ahc->tag == BUS_SPACE_MEMIO) {
		x = readb(ahc->bsh.maddr + port);
	} else {
		x = inb(ahc->bsh.ioport + port);
	}
	mb();
	return (x);
}

static __inline void
ahc_outb(struct ahc_softc * ahc, long port, uint8_t val)
{
	if (ahc->tag == BUS_SPACE_MEMIO) {
		writeb(val, ahc->bsh.maddr + port);
	} else {
		outb(val, ahc->bsh.ioport + port);
	}
	mb();
}

static __inline void
ahc_outsb(struct ahc_softc * ahc, long port, uint8_t *array, int count)
{
	int i;

	/*
	 * There is probably a more efficient way to do this on Linux
	 * but we don't use this for anything speed critical and this
	 * should work.
	 */
	for (i = 0; i < count; i++)
		ahc_outb(ahc, port, *array++);
}

static __inline void
ahc_insb(struct ahc_softc * ahc, long port, uint8_t *array, int count)
{
	int i;

	/*
	 * There is probably a more efficient way to do this on Linux
	 * but we don't use this for anything speed critical and this
	 * should work.
	 */
	for (i = 0; i < count; i++)
		*array++ = ahc_inb(ahc, port);
}

/**************************** Initialization **********************************/
int		ahc_linux_register_host(struct ahc_softc *,
					Scsi_Host_Template *);

uint64_t	ahc_linux_get_memsize(void);

/*************************** Pretty Printing **********************************/
struct info_str {
	char *buffer;
	int length;
	off_t offset;
	int pos;
};

void	ahc_format_transinfo(struct info_str *info,
			     struct ahc_transinfo *tinfo);

/******************************** Locking *************************************/
/* Lock protecting internal data structures */
static __inline void ahc_lockinit(struct ahc_softc *);
static __inline void ahc_lock(struct ahc_softc *, unsigned long *flags);
static __inline void ahc_unlock(struct ahc_softc *, unsigned long *flags);

/* Lock acquisition and release of the above lock in midlayer entry points. */
static __inline void ahc_midlayer_entrypoint_lock(struct ahc_softc *,
						  unsigned long *flags);
static __inline void ahc_midlayer_entrypoint_unlock(struct ahc_softc *,
						    unsigned long *flags);

/* Lock held during command compeletion to the upper layer */
static __inline void ahc_done_lockinit(struct ahc_softc *);
static __inline void ahc_done_lock(struct ahc_softc *, unsigned long *flags);
static __inline void ahc_done_unlock(struct ahc_softc *, unsigned long *flags);

/* Lock held during ahc_list manipulation and ahc softc frees */
extern spinlock_t ahc_list_spinlock;
static __inline void ahc_list_lockinit(void);
static __inline void ahc_list_lock(unsigned long *flags);
static __inline void ahc_list_unlock(unsigned long *flags);

static __inline void
ahc_lockinit(struct ahc_softc *ahc)
{
	spin_lock_init(&ahc->platform_data->spin_lock);
}

static __inline void
ahc_lock(struct ahc_softc *ahc, unsigned long *flags)
{
	spin_lock_irqsave(&ahc->platform_data->spin_lock, *flags);
}

static __inline void
ahc_unlock(struct ahc_softc *ahc, unsigned long *flags)
{
	spin_unlock_irqrestore(&ahc->platform_data->spin_lock, *flags);
}

static __inline void
ahc_midlayer_entrypoint_lock(struct ahc_softc *ahc, unsigned long *flags)
{
	/*
	 * In 2.5.X and some 2.4.X versions, the midlayer takes our
	 * lock just before calling us, so we avoid locking again.
	 * For other kernel versions, the io_request_lock is taken
	 * just before our entry point is called.  In this case, we
	 * trade the io_request_lock for our per-softc lock.
	 */
#if AHC_SCSI_HAS_HOST_LOCK == 0
	spin_unlock(&io_request_lock);
	spin_lock(&ahc->platform_data->spin_lock);
#endif
}

static __inline void
ahc_midlayer_entrypoint_unlock(struct ahc_softc *ahc, unsigned long *flags)
{
#if AHC_SCSI_HAS_HOST_LOCK == 0
	spin_unlock(&ahc->platform_data->spin_lock);
	spin_lock(&io_request_lock);
#endif
}

static __inline void
ahc_done_lockinit(struct ahc_softc *ahc)
{
	/*
	 * In 2.5.X, our own lock is held during completions.
	 * In previous versions, the io_request_lock is used.
	 * In either case, we can't initialize this lock again.
	 */
}

static __inline void
ahc_done_lock(struct ahc_softc *ahc, unsigned long *flags)
{
#if AHC_SCSI_HAS_HOST_LOCK == 0
	spin_lock_irqsave(&io_request_lock, *flags);
#endif
}

static __inline void
ahc_done_unlock(struct ahc_softc *ahc, unsigned long *flags)
{
#if AHC_SCSI_HAS_HOST_LOCK == 0
	spin_unlock_irqrestore(&io_request_lock, *flags);
#endif
}

static __inline void
ahc_list_lockinit(void)
{
	spin_lock_init(&ahc_list_spinlock);
}

static __inline void
ahc_list_lock(unsigned long *flags)
{
	spin_lock_irqsave(&ahc_list_spinlock, *flags);
}

static __inline void
ahc_list_unlock(unsigned long *flags)
{
	spin_unlock_irqrestore(&ahc_list_spinlock, *flags);
}

/******************************* PCI Definitions ******************************/
/*
 * PCIM_xxx: mask to locate subfield in register
 * PCIR_xxx: config register offset
 * PCIC_xxx: device class
 * PCIS_xxx: device subclass
 * PCIP_xxx: device programming interface
 * PCIV_xxx: PCI vendor ID (only required to fixup ancient devices)
 * PCID_xxx: device ID
 */
#define PCIR_DEVVENDOR		0x00
#define PCIR_VENDOR		0x00
#define PCIR_DEVICE		0x02
#define PCIR_COMMAND		0x04
#define PCIM_CMD_PORTEN		0x0001
#define PCIM_CMD_MEMEN		0x0002
#define PCIM_CMD_BUSMASTEREN	0x0004
#define PCIM_CMD_MWRICEN	0x0010
#define PCIM_CMD_PERRESPEN	0x0040
#define	PCIM_CMD_SERRESPEN	0x0100
#define PCIR_STATUS		0x06
#define PCIR_REVID		0x08
#define PCIR_PROGIF		0x09
#define PCIR_SUBCLASS		0x0a
#define PCIR_CLASS		0x0b
#define PCIR_CACHELNSZ		0x0c
#define PCIR_LATTIMER		0x0d
#define PCIR_HEADERTYPE		0x0e
#define PCIM_MFDEV		0x80
#define PCIR_BIST		0x0f
#define PCIR_CAP_PTR		0x34

/* config registers for header type 0 devices */
#define PCIR_MAPS	0x10
#define PCIR_SUBVEND_0	0x2c
#define PCIR_SUBDEV_0	0x2e

extern struct pci_driver aic7xxx_pci_driver;

typedef enum
{
	AHC_POWER_STATE_D0,
	AHC_POWER_STATE_D1,
	AHC_POWER_STATE_D2,
	AHC_POWER_STATE_D3
} ahc_power_state;

/**************************** VL/EISA Routines ********************************/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0) \
  && (defined(__i386__) || defined(__alpha__)) \
  && (!defined(CONFIG_EISA)))
#define CONFIG_EISA
#endif

#ifdef CONFIG_EISA
extern uint32_t aic7xxx_probe_eisa_vl;
int			 ahc_linux_eisa_init(void);
void			 ahc_linux_eisa_exit(void);
int			 aic7770_map_registers(struct ahc_softc *ahc,
					       u_int port);
int			 aic7770_map_int(struct ahc_softc *ahc, u_int irq);
#else
static inline int	ahc_linux_eisa_init(void) {
	return -ENODEV;
}
static inline void	ahc_linux_eisa_exit(void) {
}
#endif

/******************************* PCI Routines *********************************/
#ifdef CONFIG_PCI
int			 ahc_linux_pci_init(void);
void			 ahc_linux_pci_exit(void);
int			 ahc_pci_map_registers(struct ahc_softc *ahc);
int			 ahc_pci_map_int(struct ahc_softc *ahc);

static __inline uint32_t ahc_pci_read_config(ahc_dev_softc_t pci,
					     int reg, int width);

static __inline uint32_t
ahc_pci_read_config(ahc_dev_softc_t pci, int reg, int width)
{
	switch (width) {
	case 1:
	{
		uint8_t retval;

		pci_read_config_byte(pci, reg, &retval);
		return (retval);
	}
	case 2:
	{
		uint16_t retval;
		pci_read_config_word(pci, reg, &retval);
		return (retval);
	}
	case 4:
	{
		uint32_t retval;
		pci_read_config_dword(pci, reg, &retval);
		return (retval);
	}
	default:
		panic("ahc_pci_read_config: Read size too big");
		/* NOTREACHED */
		return (0);
	}
}

static __inline void ahc_pci_write_config(ahc_dev_softc_t pci,
					  int reg, uint32_t value,
					  int width);

static __inline void
ahc_pci_write_config(ahc_dev_softc_t pci, int reg, uint32_t value, int width)
{
	switch (width) {
	case 1:
		pci_write_config_byte(pci, reg, value);
		break;
	case 2:
		pci_write_config_word(pci, reg, value);
		break;
	case 4:
		pci_write_config_dword(pci, reg, value);
		break;
	default:
		panic("ahc_pci_write_config: Write size too big");
		/* NOTREACHED */
	}
}

static __inline int ahc_get_pci_function(ahc_dev_softc_t);
static __inline int
ahc_get_pci_function(ahc_dev_softc_t pci)
{
	return (PCI_FUNC(pci->devfn));
}

static __inline int ahc_get_pci_slot(ahc_dev_softc_t);
static __inline int
ahc_get_pci_slot(ahc_dev_softc_t pci)
{
	return (PCI_SLOT(pci->devfn));
}

static __inline int ahc_get_pci_bus(ahc_dev_softc_t);
static __inline int
ahc_get_pci_bus(ahc_dev_softc_t pci)
{
	return (pci->bus->number);
}
#else
static inline int ahc_linux_pci_init(void) {
	return 0;
}
static inline void ahc_linux_pci_exit(void) {
}
#endif

static __inline void ahc_flush_device_writes(struct ahc_softc *);
static __inline void
ahc_flush_device_writes(struct ahc_softc *ahc)
{
	/* XXX Is this sufficient for all architectures??? */
	ahc_inb(ahc, INTSTAT);
}

/**************************** Proc FS Support *********************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
int	ahc_linux_proc_info(char *, char **, off_t, int, int, int);
#else
int	ahc_linux_proc_info(struct Scsi_Host *, char *, char **,
			    off_t, int, int);
#endif

/*************************** Domain Validation ********************************/
#define AHC_DV_CMD(cmd) ((cmd)->scsi_done == ahc_linux_dv_complete)
#define AHC_DV_SIMQ_FROZEN(ahc)					\
	((((ahc)->platform_data->flags & AHC_DV_ACTIVE) != 0)	\
	 && (ahc)->platform_data->qfrozen == 1)

/*********************** Transaction Access Wrappers *************************/
static __inline void ahc_cmd_set_transaction_status(Scsi_Cmnd *, uint32_t);
static __inline void ahc_set_transaction_status(struct scb *, uint32_t);
static __inline void ahc_cmd_set_scsi_status(Scsi_Cmnd *, uint32_t);
static __inline void ahc_set_scsi_status(struct scb *, uint32_t);
static __inline uint32_t ahc_cmd_get_transaction_status(Scsi_Cmnd *cmd);
static __inline uint32_t ahc_get_transaction_status(struct scb *);
static __inline uint32_t ahc_cmd_get_scsi_status(Scsi_Cmnd *cmd);
static __inline uint32_t ahc_get_scsi_status(struct scb *);
static __inline void ahc_set_transaction_tag(struct scb *, int, u_int);
static __inline u_long ahc_get_transfer_length(struct scb *);
static __inline int ahc_get_transfer_dir(struct scb *);
static __inline void ahc_set_residual(struct scb *, u_long);
static __inline void ahc_set_sense_residual(struct scb *scb, u_long resid);
static __inline u_long ahc_get_residual(struct scb *);
static __inline u_long ahc_get_sense_residual(struct scb *);
static __inline int ahc_perform_autosense(struct scb *);
static __inline uint32_t ahc_get_sense_bufsize(struct ahc_softc *,
					       struct scb *);
static __inline void ahc_notify_xfer_settings_change(struct ahc_softc *,
						     struct ahc_devinfo *);
static __inline void ahc_platform_scb_free(struct ahc_softc *ahc,
					   struct scb *scb);
static __inline void ahc_freeze_scb(struct scb *scb);

static __inline
void ahc_cmd_set_transaction_status(Scsi_Cmnd *cmd, uint32_t status)
{
	cmd->result &= ~(CAM_STATUS_MASK << 16);
	cmd->result |= status << 16;
}

static __inline
void ahc_set_transaction_status(struct scb *scb, uint32_t status)
{
	ahc_cmd_set_transaction_status(scb->io_ctx,status);
}

static __inline
void ahc_cmd_set_scsi_status(Scsi_Cmnd *cmd, uint32_t status)
{
	cmd->result &= ~0xFFFF;
	cmd->result |= status;
}

static __inline
void ahc_set_scsi_status(struct scb *scb, uint32_t status)
{
	ahc_cmd_set_scsi_status(scb->io_ctx, status);
}

static __inline
uint32_t ahc_cmd_get_transaction_status(Scsi_Cmnd *cmd)
{
	return ((cmd->result >> 16) & CAM_STATUS_MASK);
}

static __inline
uint32_t ahc_get_transaction_status(struct scb *scb)
{
	return (ahc_cmd_get_transaction_status(scb->io_ctx));
}

static __inline
uint32_t ahc_cmd_get_scsi_status(Scsi_Cmnd *cmd)
{
	return (cmd->result & 0xFFFF);
}

static __inline
uint32_t ahc_get_scsi_status(struct scb *scb)
{
	return (ahc_cmd_get_scsi_status(scb->io_ctx));
}

static __inline
void ahc_set_transaction_tag(struct scb *scb, int enabled, u_int type)
{
	/*
	 * Nothing to do for linux as the incoming transaction
	 * has no concept of tag/non tagged, etc.
	 */
}

static __inline
u_long ahc_get_transfer_length(struct scb *scb)
{
	return (scb->platform_data->xfer_len);
}

static __inline
int ahc_get_transfer_dir(struct scb *scb)
{
	return (scb->io_ctx->sc_data_direction);
}

static __inline
void ahc_set_residual(struct scb *scb, u_long resid)
{
	scb->io_ctx->resid = resid;
}

static __inline
void ahc_set_sense_residual(struct scb *scb, u_long resid)
{
	scb->platform_data->sense_resid = resid;
}

static __inline
u_long ahc_get_residual(struct scb *scb)
{
	return (scb->io_ctx->resid);
}

static __inline
u_long ahc_get_sense_residual(struct scb *scb)
{
	return (scb->platform_data->sense_resid);
}

static __inline
int ahc_perform_autosense(struct scb *scb)
{
	/*
	 * We always perform autosense in Linux.
	 * On other platforms this is set on a
	 * per-transaction basis.
	 */
	return (1);
}

static __inline uint32_t
ahc_get_sense_bufsize(struct ahc_softc *ahc, struct scb *scb)
{
	return (sizeof(struct scsi_sense_data));
}

static __inline void
ahc_notify_xfer_settings_change(struct ahc_softc *ahc,
				struct ahc_devinfo *devinfo)
{
	/* Nothing to do here for linux */
}

static __inline void
ahc_platform_scb_free(struct ahc_softc *ahc, struct scb *scb)
{
	ahc->flags &= ~AHC_RESOURCE_SHORTAGE;
}

int	ahc_platform_alloc(struct ahc_softc *ahc, void *platform_arg);
void	ahc_platform_free(struct ahc_softc *ahc);
void	ahc_platform_freeze_devq(struct ahc_softc *ahc, struct scb *scb);

static __inline void
ahc_freeze_scb(struct scb *scb)
{
	if ((scb->io_ctx->result & (CAM_DEV_QFRZN << 16)) == 0) {
                scb->io_ctx->result |= CAM_DEV_QFRZN << 16;
                scb->platform_data->dev->qfrozen++;
        }
}

void	ahc_platform_set_tags(struct ahc_softc *ahc,
			      struct ahc_devinfo *devinfo, ahc_queue_alg);
int	ahc_platform_abort_scbs(struct ahc_softc *ahc, int target,
				char channel, int lun, u_int tag,
				role_t role, uint32_t status);
irqreturn_t
	ahc_linux_isr(int irq, void *dev_id, struct pt_regs * regs);
void	ahc_platform_flushwork(struct ahc_softc *ahc);
int	ahc_softc_comp(struct ahc_softc *, struct ahc_softc *);
void	ahc_done(struct ahc_softc*, struct scb*);
void	ahc_send_async(struct ahc_softc *, char channel,
		       u_int target, u_int lun, ac_code, void *);
void	ahc_print_path(struct ahc_softc *, struct scb *);
void	ahc_platform_dump_card_state(struct ahc_softc *ahc);

#ifdef CONFIG_PCI
#define AHC_PCI_CONFIG 1
#else
#define AHC_PCI_CONFIG 0
#endif
#define bootverbose aic7xxx_verbose
extern u_int aic7xxx_verbose;
#endif /* _AIC7XXX_LINUX_H_ */
