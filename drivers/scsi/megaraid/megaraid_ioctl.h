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
 * FILE		: megaraid_ioctl.h
 *
 * Definitions to interface with user level applications
 */

#ifndef _MEGARAID_IOCTL_H_
#define _MEGARAID_IOCTL_H_

#include <linux/types.h>
#include <asm/semaphore.h>

#include "mbox_defs.h"

/**
 * con_log() - console log routine
 * @param level		: indicates the severity of the message.
 * @fparam mt		: format string
 *
 * con_log displays the error messages on the console based on the current
 * debug level. Also it attaches the appropriate kernel severity level with
 * the message.
 *
 *
 * consolge messages debug levels
 */
#define	CL_ANN		0	/* print unconditionally, announcements */
#define CL_DLEVEL1	1	/* debug level 1, informative */
#define CL_DLEVEL2	2	/* debug level 2, verbose */
#define CL_DLEVEL3	3	/* debug level 3, very verbose */

#define	con_log(level, fmt) if (LSI_DBGLVL >= level) printk fmt;

/*
 * Definitions & Declarations needed to use common management module
 */

#define MEGAIOC_MAGIC		'm'
#define MEGAIOCCMD		_IOWR(MEGAIOC_MAGIC, 0, mimd_t)

#define MEGAIOC_QNADAP		'm'	/* Query # of adapters		*/
#define MEGAIOC_QDRVRVER	'e'	/* Query driver version		*/
#define MEGAIOC_QADAPINFO   	'g'	/* Query adapter information	*/

#define USCSICMD		0x80
#define UIOC_RD			0x00001
#define UIOC_WR			0x00002

#define MBOX_CMD		0x00000
#define GET_DRIVER_VER		0x10000
#define GET_N_ADAP		0x20000
#define GET_ADAP_INFO		0x30000
#define GET_CAP			0x40000
#define GET_STATS		0x50000
#define GET_IOCTL_VERSION	0x01

#define EXT_IOCTL_SIGN_SZ	16
#define EXT_IOCTL_SIGN		"$$_EXTD_IOCTL_$$"

#define	MBOX_LEGACY		0x00		/* ioctl has legacy mbox*/
#define MBOX_HPE		0x01		/* ioctl has hpe mbox	*/

#define	APPTYPE_MIMD		0x00		/* old existing apps	*/
#define APPTYPE_UIOC		0x01		/* new apps using uioc	*/

#define IOCTL_ISSUE		0x00000001	/* Issue ioctl		*/
#define IOCTL_ABORT		0x00000002	/* Abort previous ioctl	*/

#define DRVRTYPE_MBOX		0x00000001	/* regular mbox driver	*/
#define DRVRTYPE_HPE		0x00000002	/* new hpe driver	*/

#define MKADAP(adapno)	(MEGAIOC_MAGIC << 8 | (adapno) )
#define GETADAP(mkadap)	((mkadap) ^ MEGAIOC_MAGIC << 8)

#define MAX_DMA_POOLS		5		/* 4k, 8k, 16k, 32k, 64k*/


/**
 * struct uioc_t - the common ioctl packet structure
 *
 * @signature	: Must be "$$_EXTD_IOCTL_$$"
 * @mb_type	: Type of the mail box (MB_LEGACY or MB_HPE)
 * @app_type	: Type of the issuing application (existing or new)
 * @opcode	: Opcode of the command
 * @adapno	: Adapter number
 * @cmdbuf	: Pointer to buffer - can point to mbox or plain data buffer
 * @xferlen	: xferlen for DCMD and non mailbox commands
 * @data_dir	: Direction of the data transfer
 * @status	: Status from the driver
 * @reserved	: reserved bytes for future expansion
 *
 * @user_data	: user data transfer address is saved in this
 * @user_data_len: length of the data buffer sent by user app
 * @user_pthru	: user passthru address is saves in this (null if DCMD)
 * @pthru32	: kernel address passthru (allocated per kioc)
 * @pthru32_h	: physicall address of @pthru32
 * @list	: for kioc free pool list maintenance
 * @done	: call back routine for llds to call when kioc is completed
 * @buf_vaddr	: dma pool buffer attached to kioc for data transfer
 * @buf_paddr	: physical address of the dma pool buffer
 * @pool_index	: index of the dma pool that @buf_vaddr is taken from
 * @free_buf	: indicates if buffer needs to be freed after kioc completes
 *
 * Note		: All LSI drivers understand only this packet. Any other
 *		: format sent by applications would be converted to this.
 */
typedef struct uioc {

/* User Apps: */

	uint8_t			signature[EXT_IOCTL_SIGN_SZ];
	uint16_t		mb_type;
	uint16_t		app_type;
	uint32_t		opcode;
	uint32_t		adapno;
	uint64_t		cmdbuf;
	uint32_t		xferlen;
	uint32_t		data_dir;
	int32_t			status;
	uint8_t			reserved[128];

/* Driver Data: */
	void __user *		user_data;
	uint32_t		user_data_len;
	mraid_passthru_t	__user *user_pthru;

	mraid_passthru_t	*pthru32;
	dma_addr_t		pthru32_h;

	struct list_head	list;
	void			(*done)(struct uioc*);

	caddr_t			buf_vaddr;
	dma_addr_t		buf_paddr;
	int8_t			pool_index;
	uint8_t			free_buf;

	uint8_t			timedout;

} __attribute__ ((aligned(1024),packed)) uioc_t;


/**
 * struct mraid_hba_info - information about the controller
 *
 * @param pci_vendor_id		: PCI vendor id
 * @param pci_device_id		: PCI device id
 * @param subsystem_vendor_id	: PCI subsystem vendor id
 * @param subsystem_device_id	: PCI subsystem device id
 * @param baseport		: base port of hba memory
 * @param pci_bus		: PCI bus
 * @param pci_dev_fn		: PCI device/function values
 * @param irq			: interrupt vector for the device
 *
 * Extended information of 256 bytes about the controller. Align on the single
 * byte boundary so that 32-bit applications can be run on 64-bit platform
 * drivers withoug re-compilation.
 * NOTE: reduce the number of reserved bytes whenever new field are added, so
 * that total size of the structure remains 256 bytes.
 */
typedef struct mraid_hba_info {

	uint16_t	pci_vendor_id;
	uint16_t	pci_device_id;
	uint16_t	subsys_vendor_id;
	uint16_t	subsys_device_id;

	uint64_t	baseport;
	uint8_t		pci_bus;
	uint8_t		pci_dev_fn;
	uint8_t		pci_slot;
	uint8_t		irq;

	uint32_t	unique_id;
	uint32_t	host_no;

	uint8_t		num_ldrv;
} __attribute__ ((aligned(256), packed)) mraid_hba_info_t;


/**
 * mcontroller	: adapter info structure for old mimd_t apps
 *
 * @base	: base address
 * @irq		: irq number
 * @numldrv	: number of logical drives
 * @pcibus	: pci bus
 * @pcidev	: pci device
 * @pcifun	: pci function
 * @pciid	: pci id
 * @pcivendor	: vendor id
 * @pcislot	: slot number
 * @uid		: unique id
 */
typedef struct mcontroller {

	uint64_t	base;
	uint8_t		irq;
	uint8_t		numldrv;
	uint8_t		pcibus;
	uint16_t	pcidev;
	uint8_t		pcifun;
	uint16_t	pciid;
	uint16_t	pcivendor;
	uint8_t		pcislot;
	uint32_t	uid;

} __attribute__ ((packed)) mcontroller_t;


/**
 * mm_dmapool_t	: Represents one dma pool with just one buffer
 *
 * @vaddr	: Virtual address
 * @paddr	: DMA physicall address
 * @bufsize	: In KB - 4 = 4k, 8 = 8k etc.
 * @handle	: Handle to the dma pool
 * @lock	: lock to synchronize access to the pool
 * @in_use	: If pool already in use, attach new block
 */
typedef struct mm_dmapool {
	caddr_t		vaddr;
	dma_addr_t	paddr;
	uint32_t	buf_size;
	struct dma_pool	*handle;
	spinlock_t	lock;
	uint8_t		in_use;
} mm_dmapool_t;


/**
 * mraid_mmadp_t: Structure that drivers pass during (un)registration
 *
 * @unique_id		: Any unique id (usually PCI bus+dev+fn)
 * @drvr_type		: megaraid or hpe (DRVRTYPE_MBOX or DRVRTYPE_HPE)
 * @drv_data		: Driver specific; not touched by the common module
 * @timeout		: timeout for issued kiocs
 * @max_kioc		: Maximum ioctl packets acceptable by the lld
 * @pdev		: pci dev; used for allocating dma'ble memory
 * @issue_uioc		: Driver supplied routine to issue uioc_t commands
 *			: issue_uioc(drvr_data, kioc, ISSUE/ABORT, uioc_done)
 * @quiescent		: flag to indicate if ioctl can be issued to this adp
 * @list		: attach with the global list of adapters
 * @kioc_list		: block of mem for @max_kioc number of kiocs
 * @kioc_pool		: pool of free kiocs
 * @kioc_pool_lock	: protection for free pool
 * @kioc_semaphore	: so as not to exceed @max_kioc parallel ioctls
 * @mbox_list		: block of mem for @max_kioc number of mboxes
 * @pthru_dma_pool	: DMA pool to allocate passthru packets
 * @dma_pool_list	: array of dma pools
 */

typedef struct mraid_mmadp {

/* Filled by driver */

	uint32_t		unique_id;
	uint32_t		drvr_type;
	unsigned long		drvr_data;
	uint16_t		timeout;
	uint8_t			max_kioc;

	struct pci_dev		*pdev;

	int(*issue_uioc)(unsigned long, uioc_t *, uint32_t);

/* Maintained by common module */
	uint32_t		quiescent;

	struct list_head	list;
	uioc_t			*kioc_list;
	struct list_head	kioc_pool;
	spinlock_t		kioc_pool_lock;
	struct semaphore	kioc_semaphore;

	mbox64_t		*mbox_list;
	struct dma_pool		*pthru_dma_pool;
	mm_dmapool_t		dma_pool_list[MAX_DMA_POOLS];

} mraid_mmadp_t;

int mraid_mm_register_adp(mraid_mmadp_t *);
int mraid_mm_unregister_adp(uint32_t);
uint32_t mraid_mm_adapter_app_handle(uint32_t);

#endif /* _MEGARAID_IOCTL_H_ */
