/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 *			Linux MegaRAID device driver
 *
 * Copyright (c) 2003-2004  LSI Logic Corporation.
 *
 * FILE		: mega_common.h
 *
 * Libaray of common routine used by all low-level megaraid drivers
 */

#ifndef _MEGA_COMMON_H_
#define _MEGA_COMMON_H_

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>


#define LSI_MAX_CHANNELS		16
#define LSI_MAX_LOGICAL_DRIVES_64LD	(64+1)

#define HBA_SIGNATURE_64_BIT		0x299
#define PCI_CONF_AMISIG64		0xa4

#define MEGA_SCSI_INQ_EVPD		1
#define MEGA_INVALID_FIELD_IN_CDB	0x24


/**
 * scb_t - scsi command control block
 * @ccb			: command control block for individual driver
 * @list		: list of control blocks
 * @gp			: general purpose field for LLDs
 * @sno			: all SCBs have a serial number
 * @scp			: associated scsi command
 * @state		: current state of scb
 * @dma_dir		: direction of data transfer
 * @dma_type		: transfer with sg list, buffer, or no data transfer
 * @dev_channel		: actual channel on the device
 * @dev_target		: actual target on the device
 * @status		: completion status
 *
 * This is our central data structure to issue commands the each driver.
 * Driver specific data structures are maintained in the ccb field.
 * scb provides a field 'gp', which can be used by LLD for its own purposes
 *
 * dev_channel and dev_target must be initialized with the actual channel and
 * target on the controller.
 */
typedef struct {
	caddr_t			ccb;
	struct list_head	list;
	unsigned long		gp;
	unsigned int		sno;
	struct scsi_cmnd	*scp;
	uint32_t		state;
	uint32_t		dma_direction;
	uint32_t		dma_type;
	uint16_t		dev_channel;
	uint16_t		dev_target;
	uint32_t		status;
} scb_t;

/*
 * SCB states as it transitions from one state to another
 */
#define SCB_FREE	0x0000	/* on the free list */
#define SCB_ACTIVE	0x0001	/* off the free list */
#define SCB_PENDQ	0x0002	/* on the pending queue */
#define SCB_ISSUED	0x0004	/* issued - owner f/w */
#define SCB_ABORT	0x0008	/* Got an abort for this one */
#define SCB_RESET	0x0010	/* Got a reset for this one */

/*
 * DMA types for scb
 */
#define MRAID_DMA_NONE	0x0000	/* no data transfer for this command */
#define MRAID_DMA_WSG	0x0001	/* data transfer using a sg list */
#define MRAID_DMA_WBUF	0x0002	/* data transfer using a contiguous buffer */


/**
 * struct adapter_t - driver's initialization structure
 * @aram dpc_h			: tasklet handle
 * @pdev			: pci configuration pointer for kernel
 * @host			: pointer to host structure of mid-layer
 * @lock			: synchronization lock for mid-layer and driver
 * @quiescent			: driver is quiescent for now.
 * @outstanding_cmds		: number of commands pending in the driver
 * @kscb_list			: pointer to the bulk of SCBs pointers for IO
 * @kscb_pool			: pool of free scbs for IO
 * @kscb_pool_lock		: lock for pool of free scbs
 * @pend_list			: pending commands list
 * @pend_list_lock		: exclusion lock for pending commands list
 * @completed_list		: list of completed commands
 * @completed_list_lock		: exclusion lock for list of completed commands
 * @sglen			: max sg elements supported
 * @device_ids			: to convert kernel device addr to our devices.
 * @raid_device			: raid adapter specific pointer
 * @max_channel			: maximum channel number supported - inclusive
 * @max_target			: max target supported - inclusive
 * @max_lun			: max lun supported - inclusive
 * @unique_id			: unique identifier for each adapter
 * @irq				: IRQ for this adapter
 * @ito				: internal timeout value, (-1) means no timeout
 * @ibuf			: buffer to issue internal commands
 * @ibuf_dma_h			: dma handle for the above buffer
 * @uscb_list			: SCB pointers for user cmds, common mgmt module
 * @uscb_pool			: pool of SCBs for user commands
 * @uscb_pool_lock		: exclusion lock for these SCBs
 * @max_cmds			: max outstanding commands
 * @fw_version			: firmware version
 * @bios_version		: bios version
 * @max_cdb_sz			: biggest CDB size supported.
 * @ha				: is high availability present - clustering
 * @init_id			: initiator ID, the default value should be 7
 * @max_sectors			: max sectors per request
 * @cmd_per_lun			: max outstanding commands per LUN
 * @being_detached		: set when unloading, no more mgmt calls
 *
 *
 * mraid_setup_device_map() can be called anytime after the device map is
 * available and MRAID_GET_DEVICE_MAP() can be called whenever the mapping is
 * required, usually from LLD's queue entry point. The formar API sets up the
 * MRAID_IS_LOGICAL(adapter_t *, struct scsi_cmnd *) to find out if the
 * device in question is a logical drive.
 *
 * quiescent flag should be set by the driver if it is not accepting more
 * commands
 *
 * NOTE: The fields of this structures are placed to minimize cache misses
 */

// amount of space required to store the bios and firmware version strings
#define VERSION_SIZE	16

typedef struct {
	struct tasklet_struct	dpc_h;
	struct pci_dev		*pdev;
	struct Scsi_Host	*host;
	spinlock_t		lock;
	uint8_t			quiescent;
	int			outstanding_cmds;
	scb_t			*kscb_list;
	struct list_head	kscb_pool;
	spinlock_t		kscb_pool_lock;
	struct list_head	pend_list;
	spinlock_t		pend_list_lock;
	struct list_head	completed_list;
	spinlock_t		completed_list_lock;
	uint16_t		sglen;
	int			device_ids[LSI_MAX_CHANNELS]
					[LSI_MAX_LOGICAL_DRIVES_64LD];
	caddr_t			raid_device;
	uint8_t			max_channel;
	uint16_t		max_target;
	uint8_t			max_lun;

	uint32_t		unique_id;
	int			irq;
	uint8_t			ito;
	caddr_t			ibuf;
	dma_addr_t		ibuf_dma_h;
	scb_t			*uscb_list;
	struct list_head	uscb_pool;
	spinlock_t		uscb_pool_lock;
	int			max_cmds;
	uint8_t			fw_version[VERSION_SIZE];
	uint8_t			bios_version[VERSION_SIZE];
	uint8_t			max_cdb_sz;
	uint8_t			ha;
	uint16_t		init_id;
	uint16_t		max_sectors;
	uint16_t		cmd_per_lun;
	atomic_t		being_detached;
} adapter_t;

#define SCSI_FREE_LIST_LOCK(adapter)	(&adapter->kscb_pool_lock)
#define USER_FREE_LIST_LOCK(adapter)	(&adapter->uscb_pool_lock)
#define PENDING_LIST_LOCK(adapter)	(&adapter->pend_list_lock)
#define COMPLETED_LIST_LOCK(adapter)	(&adapter->completed_list_lock)


// conversion from scsi command
#define SCP2HOST(scp)			(scp)->device->host	// to host
#define SCP2HOSTDATA(scp)		SCP2HOST(scp)->hostdata	// to soft state
#define SCP2CHANNEL(scp)		(scp)->device->channel	// to channel
#define SCP2TARGET(scp)			(scp)->device->id	// to target
#define SCP2LUN(scp)			(u32)(scp)->device->lun	// to LUN

// generic macro to convert scsi command and host to controller's soft state
#define SCSIHOST2ADAP(host)	(((caddr_t *)(host->hostdata))[0])
#define SCP2ADAPTER(scp)	(adapter_t *)SCSIHOST2ADAP(SCP2HOST(scp))


#define MRAID_IS_LOGICAL(adp, scp)	\
	(SCP2CHANNEL(scp) == (adp)->max_channel) ? 1 : 0

#define MRAID_IS_LOGICAL_SDEV(adp, sdev)	\
	(sdev->channel == (adp)->max_channel) ? 1 : 0

/**
 * MRAID_GET_DEVICE_MAP - device ids
 * @adp			: adapter's soft state
 * @scp			: mid-layer scsi command pointer
 * @p_chan		: physical channel on the controller
 * @target		: target id of the device or logical drive number
 * @islogical		: set if the command is for the logical drive
 *
 * Macro to retrieve information about device class, logical or physical and
 * the corresponding physical channel and target or logical drive number
 */
#define MRAID_GET_DEVICE_MAP(adp, scp, p_chan, target, islogical)	\
	/*								\
	 * Is the request coming for the virtual channel		\
	 */								\
	islogical = MRAID_IS_LOGICAL(adp, scp);				\
									\
	/*								\
	 * Get an index into our table of drive ids mapping		\
	 */								\
	if (islogical) {						\
		p_chan = 0xFF;						\
		target =						\
		(adp)->device_ids[(adp)->max_channel][SCP2TARGET(scp)];	\
	}								\
	else {								\
		p_chan = ((adp)->device_ids[SCP2CHANNEL(scp)]		\
					[SCP2TARGET(scp)] >> 8) & 0xFF;	\
		target = ((adp)->device_ids[SCP2CHANNEL(scp)]		\
					[SCP2TARGET(scp)] & 0xFF);	\
	}

/*
 * ### Helper routines ###
 */
#define LSI_DBGLVL mraid_debug_level	// each LLD must define a global
 					// mraid_debug_level

#ifdef DEBUG
#if defined (_ASSERT_PANIC)
#define ASSERT_ACTION	panic
#else
#define ASSERT_ACTION	printk
#endif

#define ASSERT(expression)						\
	if (!(expression)) {						\
	ASSERT_ACTION("assertion failed:(%s), file: %s, line: %d:%s\n",	\
			#expression, __FILE__, __LINE__, __func__);	\
	}
#else
#define ASSERT(expression)
#endif

/**
 * struct mraid_pci_blk - structure holds DMA memory block info
 * @vaddr		: virtual address to a memory block
 * @dma_addr		: DMA handle to a memory block
 *
 * This structure is filled up for the caller. It is the responsibilty of the
 * caller to allocate this array big enough to store addresses for all
 * requested elements
 */
struct mraid_pci_blk {
	caddr_t		vaddr;
	dma_addr_t	dma_addr;
};

#endif // _MEGA_COMMON_H_

// vim: set ts=8 sw=8 tw=78:
