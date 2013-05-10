/*
 *
 *			Linux MegaRAID device driver
 *
 * Copyright (c) 2003-2004  LSI Logic Corporation.
 *
 *	   This program is free software; you can redistribute it and/or
 *	   modify it under the terms of the GNU General Public License
 *	   as published by the Free Software Foundation; either version
 *	   2 of the License, or (at your option) any later version.
 *
 * FILE		: megaraid_mbox.h
 */

#ifndef _MEGARAID_H_
#define _MEGARAID_H_


#include "mega_common.h"
#include "mbox_defs.h"
#include "megaraid_ioctl.h"


#define MEGARAID_VERSION	"2.20.5.1"
#define MEGARAID_EXT_VERSION	"(Release Date: Thu Nov 16 15:32:35 EST 2006)"


/*
 * Define some PCI values here until they are put in the kernel
 */
#define PCI_DEVICE_ID_PERC4_DI_DISCOVERY		0x000E
#define PCI_SUBSYS_ID_PERC4_DI_DISCOVERY		0x0123

#define PCI_DEVICE_ID_PERC4_SC				0x1960
#define PCI_SUBSYS_ID_PERC4_SC				0x0520

#define PCI_DEVICE_ID_PERC4_DC				0x1960
#define PCI_SUBSYS_ID_PERC4_DC				0x0518

#define PCI_DEVICE_ID_VERDE				0x0407

#define PCI_DEVICE_ID_PERC4_DI_EVERGLADES		0x000F
#define PCI_SUBSYS_ID_PERC4_DI_EVERGLADES		0x014A

#define PCI_DEVICE_ID_PERC4E_SI_BIGBEND			0x0013
#define PCI_SUBSYS_ID_PERC4E_SI_BIGBEND			0x016c

#define PCI_DEVICE_ID_PERC4E_DI_KOBUK			0x0013
#define PCI_SUBSYS_ID_PERC4E_DI_KOBUK			0x016d

#define PCI_DEVICE_ID_PERC4E_DI_CORVETTE		0x0013
#define PCI_SUBSYS_ID_PERC4E_DI_CORVETTE		0x016e

#define PCI_DEVICE_ID_PERC4E_DI_EXPEDITION		0x0013
#define PCI_SUBSYS_ID_PERC4E_DI_EXPEDITION		0x016f

#define PCI_DEVICE_ID_PERC4E_DI_GUADALUPE		0x0013
#define PCI_SUBSYS_ID_PERC4E_DI_GUADALUPE		0x0170

#define PCI_DEVICE_ID_DOBSON				0x0408

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_0		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_0		0xA520

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_1		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_1		0x0520

#define PCI_DEVICE_ID_MEGARAID_SCSI_320_2		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SCSI_320_2		0x0518

#define PCI_DEVICE_ID_MEGARAID_I4_133_RAID		0x1960
#define PCI_SUBSYS_ID_MEGARAID_I4_133_RAID		0x0522

#define PCI_DEVICE_ID_MEGARAID_SATA_150_4		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SATA_150_4		0x4523

#define PCI_DEVICE_ID_MEGARAID_SATA_150_6		0x1960
#define PCI_SUBSYS_ID_MEGARAID_SATA_150_6		0x0523

#define PCI_DEVICE_ID_LINDSAY				0x0409

#define PCI_DEVICE_ID_INTEL_RAID_SRCS16			0x1960
#define PCI_SUBSYS_ID_INTEL_RAID_SRCS16			0x0523

#define PCI_DEVICE_ID_INTEL_RAID_SRCU41L_LAKE_SHETEK	0x1960
#define PCI_SUBSYS_ID_INTEL_RAID_SRCU41L_LAKE_SHETEK	0x0520

#define PCI_SUBSYS_ID_PERC3_QC				0x0471
#define PCI_SUBSYS_ID_PERC3_DC				0x0493
#define PCI_SUBSYS_ID_PERC3_SC				0x0475
#define PCI_SUBSYS_ID_CERC_ATA100_4CH			0x0511


#define MBOX_MAX_SCSI_CMDS	128	// number of cmds reserved for kernel
#define MBOX_MAX_USER_CMDS	32	// number of cmds for applications
#define MBOX_DEF_CMD_PER_LUN	64	// default commands per lun
#define MBOX_DEFAULT_SG_SIZE	26	// default sg size supported by all fw
#define MBOX_MAX_SG_SIZE	32	// maximum scatter-gather list size
#define MBOX_MAX_SECTORS	128	// maximum sectors per IO
#define MBOX_TIMEOUT		30	// timeout value for internal cmds
#define MBOX_BUSY_WAIT		10	// max usec to wait for busy mailbox
#define MBOX_RESET_WAIT		180	// wait these many seconds in reset
#define MBOX_RESET_EXT_WAIT	120	// extended wait reset
#define MBOX_SYNC_WAIT_CNT	0xFFFF	// wait loop index for synchronous mode

#define MBOX_SYNC_DELAY_200	200	// 200 micro-seconds

/*
 * maximum transfer that can happen through the firmware commands issued
 * internnaly from the driver.
 */
#define MBOX_IBUF_SIZE		4096


/**
 * mbox_ccb_t - command control block specific to mailbox based controllers
 * @raw_mbox		: raw mailbox pointer
 * @mbox		: mailbox
 * @mbox64		: extended mailbox
 * @mbox_dma_h		: maibox dma address
 * @sgl64		: 64-bit scatter-gather list
 * @sgl32		: 32-bit scatter-gather list
 * @sgl_dma_h		: dma handle for the scatter-gather list
 * @pthru		: passthru structure
 * @pthru_dma_h		: dma handle for the passthru structure
 * @epthru		: extended passthru structure
 * @epthru_dma_h	: dma handle for extended passthru structure
 * @buf_dma_h		: dma handle for buffers w/o sg list
 *
 * command control block specific to the mailbox based controllers
 */
typedef struct {
	uint8_t			*raw_mbox;
	mbox_t			*mbox;
	mbox64_t		*mbox64;
	dma_addr_t		mbox_dma_h;
	mbox_sgl64		*sgl64;
	mbox_sgl32		*sgl32;
	dma_addr_t		sgl_dma_h;
	mraid_passthru_t	*pthru;
	dma_addr_t		pthru_dma_h;
	mraid_epassthru_t	*epthru;
	dma_addr_t		epthru_dma_h;
	dma_addr_t		buf_dma_h;
} mbox_ccb_t;


/**
 * mraid_device_t - adapter soft state structure for mailbox controllers
 * @una_mbox64			: 64-bit mbox - unaligned
 * @una_mbox64_dma		: mbox dma addr - unaligned
 * @mbox			: 32-bit mbox - aligned
 * @mbox64			: 64-bit mbox - aligned
 * @mbox_dma			: mbox dma addr - aligned
 * @mailbox_lock		: exclusion lock for the mailbox
 * @baseport			: base port of hba memory
 * @baseaddr			: mapped addr of hba memory
 * @mbox_pool			: pool of mailboxes
 * @mbox_pool_handle		: handle for the mailbox pool memory
 * @epthru_pool			: a pool for extended passthru commands
 * @epthru_pool_handle		: handle to the pool above
 * @sg_pool			: pool of scatter-gather lists for this driver
 * @sg_pool_handle		: handle to the pool above
 * @ccb_list			: list of our command control blocks
 * @uccb_list			: list of cmd control blocks for mgmt module
 * @umbox64			: array of mailbox for user commands (cmm)
 * @pdrv_state			: array for state of each physical drive.
 * @last_disp			: flag used to show device scanning
 * @hw_error			: set if FW not responding
 * @fast_load			: If set, skip physical device scanning
 * @channel_class		: channel class, RAID or SCSI
 * @sysfs_mtx			: mutex to serialize access to sysfs res.
 * @sysfs_uioc			: management packet to issue FW calls from sysfs
 * @sysfs_mbox64		: mailbox packet to issue FW calls from sysfs
 * @sysfs_buffer		: data buffer for FW commands issued from sysfs
 * @sysfs_buffer_dma		: DMA buffer for FW commands issued from sysfs
 * @sysfs_wait_q		: wait queue for sysfs operations
 * @random_del_supported	: set if the random deletion is supported
 * @curr_ldmap			: current LDID map
 *
 * Initialization structure for mailbox controllers: memory based and IO based
 * All the fields in this structure are LLD specific and may be discovered at
 * init() or start() time.
 *
 * NOTE: The fields of this structures are placed to minimize cache misses
 */
#define MAX_LD_EXTENDED64	64
typedef struct {
	mbox64_t			*una_mbox64;
	dma_addr_t			una_mbox64_dma;
	mbox_t				*mbox;
	mbox64_t			*mbox64;
	dma_addr_t			mbox_dma;
	spinlock_t			mailbox_lock;
	unsigned long			baseport;
	void __iomem *			baseaddr;
	struct mraid_pci_blk		mbox_pool[MBOX_MAX_SCSI_CMDS];
	struct dma_pool			*mbox_pool_handle;
	struct mraid_pci_blk		epthru_pool[MBOX_MAX_SCSI_CMDS];
	struct dma_pool			*epthru_pool_handle;
	struct mraid_pci_blk		sg_pool[MBOX_MAX_SCSI_CMDS];
	struct dma_pool			*sg_pool_handle;
	mbox_ccb_t			ccb_list[MBOX_MAX_SCSI_CMDS];
	mbox_ccb_t			uccb_list[MBOX_MAX_USER_CMDS];
	mbox64_t			umbox64[MBOX_MAX_USER_CMDS];

	uint8_t				pdrv_state[MBOX_MAX_PHYSICAL_DRIVES];
	uint32_t			last_disp;
	int				hw_error;
	int				fast_load;
	uint8_t				channel_class;
	struct mutex			sysfs_mtx;
	uioc_t				*sysfs_uioc;
	mbox64_t			*sysfs_mbox64;
	caddr_t				sysfs_buffer;
	dma_addr_t			sysfs_buffer_dma;
	wait_queue_head_t		sysfs_wait_q;
	int				random_del_supported;
	uint16_t			curr_ldmap[MAX_LD_EXTENDED64];
} mraid_device_t;

// route to raid device from adapter
#define ADAP2RAIDDEV(adp)	((mraid_device_t *)((adp)->raid_device))

#define MAILBOX_LOCK(rdev)	(&(rdev)->mailbox_lock)

// Find out if this channel is a RAID or SCSI
#define IS_RAID_CH(rdev, ch)	(((rdev)->channel_class >> (ch)) & 0x01)


#define RDINDOOR(rdev)		readl((rdev)->baseaddr + 0x20)
#define RDOUTDOOR(rdev)		readl((rdev)->baseaddr + 0x2C)
#define WRINDOOR(rdev, value)	writel(value, (rdev)->baseaddr + 0x20)
#define WROUTDOOR(rdev, value)	writel(value, (rdev)->baseaddr + 0x2C)

#endif // _MEGARAID_H_

// vim: set ts=8 sw=8 tw=78:
