/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *	NOTE TO LINUX KERNEL HACKERS:  DO NOT REFORMAT THIS CODE!
 *
 *	This is shared code between Digi's CVS archive and the
 *	Linux Kernel sources.
 *	Changing the source just for reformatting needlessly breaks
 *	our CVS diff history.
 *
 *	Send any bug fixes/changes to:  Eng.Linux at digi dot com.
 *	Thank you.
 *
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include "dgnc_driver.h"
#include "dgnc_pci.h"
#include "dpacompat.h"
#include "dgnc_mgmt.h"
#include "dgnc_tty.h"
#include "dgnc_cls.h"
#include "dgnc_neo.h"
#include "dgnc_sysfs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Digi International, http://www.digi.com");
MODULE_DESCRIPTION("Driver for the Digi International Neo and Classic PCI based product line");
MODULE_SUPPORTED_DEVICE("dgnc");

/**************************************************************************
 *
 * protos for this file
 *
 */
static int		dgnc_start(void);
static int		dgnc_finalize_board_init(struct dgnc_board *brd);
static void		dgnc_init_globals(void);
static int		dgnc_found_board(struct pci_dev *pdev, int id);
static void		dgnc_cleanup_board(struct dgnc_board *brd);
static void		dgnc_poll_handler(ulong dummy);
static int		dgnc_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);
static void		dgnc_do_remap(struct dgnc_board *brd);

/*
 * File operations permitted on Control/Management major.
 */
static const struct file_operations dgnc_BoardFops = {
	.owner		=	THIS_MODULE,
	.unlocked_ioctl =	dgnc_mgmt_ioctl,
	.open		=	dgnc_mgmt_open,
	.release	=	dgnc_mgmt_close
};


/*
 * Globals
 */
uint			dgnc_NumBoards;
struct dgnc_board		*dgnc_Board[MAXBOARDS];
DEFINE_SPINLOCK(dgnc_global_lock);
uint			dgnc_Major;
int			dgnc_poll_tick = 20;	/* Poll interval - 20 ms */

/*
 * Static vars.
 */
static struct class *dgnc_class;

/*
 * Poller stuff
 */
static DEFINE_SPINLOCK(dgnc_poll_lock); /* Poll scheduling lock */
static ulong		dgnc_poll_time;				/* Time of next poll */
static uint		dgnc_poll_stop;				/* Used to tell poller to stop */
static struct timer_list dgnc_poll_timer;


static struct pci_device_id dgnc_pci_tbl[] = {
	{	DIGI_VID, PCI_DEVICE_CLASSIC_4_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	0 },
	{	DIGI_VID, PCI_DEVICE_CLASSIC_4_422_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	1 },
	{	DIGI_VID, PCI_DEVICE_CLASSIC_8_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	2 },
	{	DIGI_VID, PCI_DEVICE_CLASSIC_8_422_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	3 },
	{0,}						/* 0 terminated list. */
};
MODULE_DEVICE_TABLE(pci, dgnc_pci_tbl);

struct board_id {
	unsigned char *name;
	uint maxports;
	unsigned int is_pci_express;
};

static struct board_id dgnc_Ids[] = {
	{	PCI_DEVICE_CLASSIC_4_PCI_NAME,		4,	0	},
	{	PCI_DEVICE_CLASSIC_4_422_PCI_NAME,	4,	0	},
	{	PCI_DEVICE_CLASSIC_8_PCI_NAME,		8,	0	},
	{	PCI_DEVICE_CLASSIC_8_422_PCI_NAME,	8,	0	},
	{	PCI_DEVICE_NEO_4_PCI_NAME,		4,	0	},
	{	PCI_DEVICE_NEO_8_PCI_NAME,		8,	0	},
	{	PCI_DEVICE_NEO_2DB9_PCI_NAME,		2,	0	},
	{	PCI_DEVICE_NEO_2DB9PRI_PCI_NAME,	2,	0	},
	{	PCI_DEVICE_NEO_2RJ45_PCI_NAME,		2,	0	},
	{	PCI_DEVICE_NEO_2RJ45PRI_PCI_NAME,	2,	0	},
	{	PCI_DEVICE_NEO_1_422_PCI_NAME,		1,	0	},
	{	PCI_DEVICE_NEO_1_422_485_PCI_NAME,	1,	0	},
	{	PCI_DEVICE_NEO_2_422_485_PCI_NAME,	2,	0	},
	{	PCI_DEVICE_NEO_EXPRESS_8_PCI_NAME,	8,	1	},
	{	PCI_DEVICE_NEO_EXPRESS_4_PCI_NAME,	4,	1	},
	{	PCI_DEVICE_NEO_EXPRESS_4RJ45_PCI_NAME,	4,	1	},
	{	PCI_DEVICE_NEO_EXPRESS_8RJ45_PCI_NAME,	8,	1	},
	{	NULL,					0,	0	}
};

static struct pci_driver dgnc_driver = {
	.name		= "dgnc",
	.probe		= dgnc_init_one,
	.id_table       = dgnc_pci_tbl,
};


char *dgnc_state_text[] = {
	"Board Failed",
	"Board Found",
	"Board READY",
};


/************************************************************************
 *
 * Driver load/unload functions
 *
 ************************************************************************/

/*
 * dgnc_cleanup_module()
 *
 * Module unload.  This is where it all ends.
 */
static void dgnc_cleanup_module(void)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&dgnc_poll_lock, flags);
	dgnc_poll_stop = 1;
	spin_unlock_irqrestore(&dgnc_poll_lock, flags);

	/* Turn off poller right away. */
	del_timer_sync(&dgnc_poll_timer);

	dgnc_remove_driver_sysfiles(&dgnc_driver);

	device_destroy(dgnc_class, MKDEV(dgnc_Major, 0));
	class_destroy(dgnc_class);
	unregister_chrdev(dgnc_Major, "dgnc");

	for (i = 0; i < dgnc_NumBoards; ++i) {
		dgnc_remove_ports_sysfiles(dgnc_Board[i]);
		dgnc_tty_uninit(dgnc_Board[i]);
		dgnc_cleanup_board(dgnc_Board[i]);
	}

	dgnc_tty_post_uninit();

	if (dgnc_NumBoards)
		pci_unregister_driver(&dgnc_driver);
}

/*
 * init_module()
 *
 * Module load.  This is where it all starts.
 */
static int __init dgnc_init_module(void)
{
	int rc = 0;

	/*
	 * Initialize global stuff
	 */
	rc = dgnc_start();

	if (rc < 0)
		return rc;

	/*
	 * Find and configure all the cards
	 */
	rc = pci_register_driver(&dgnc_driver);

	/*
	 * If something went wrong in the scan, bail out of driver.
	 */
	if (rc < 0) {
		/* Only unregister the pci driver if it was actually registered. */
		if (dgnc_NumBoards)
			pci_unregister_driver(&dgnc_driver);
		else
			pr_warn("WARNING: dgnc driver load failed.  No Digi Neo or Classic boards found.\n");

		dgnc_cleanup_module();
	} else {
		dgnc_create_driver_sysfiles(&dgnc_driver);
	}

	return rc;
}

module_init(dgnc_init_module);
module_exit(dgnc_cleanup_module);

/*
 * Start of driver.
 */
static int dgnc_start(void)
{
	int rc = 0;
	unsigned long flags;

	/* make sure that the globals are init'd before we do anything else */
	dgnc_init_globals();

	/*
	 * Register our base character device into the kernel.
	 * This allows the download daemon to connect to the downld device
	 * before any of the boards are init'ed.
	 *
	 * Register management/dpa devices
	 */
	rc = register_chrdev(0, "dgnc", &dgnc_BoardFops);
	if (rc <= 0) {
		pr_err(DRVSTR ": Can't register dgnc driver device (%d)\n", rc);
		return -ENXIO;
	}
	dgnc_Major = rc;

	dgnc_class = class_create(THIS_MODULE, "dgnc_mgmt");
	device_create(dgnc_class, NULL,
		MKDEV(dgnc_Major, 0),
		NULL, "dgnc_mgmt");

	/*
	 * Init any global tty stuff.
	 */
	rc = dgnc_tty_preinit();

	if (rc < 0) {
		pr_err(DRVSTR ": tty preinit - not enough memory (%d)\n", rc);
		return rc;
	}

	/* Start the poller */
	spin_lock_irqsave(&dgnc_poll_lock, flags);
	init_timer(&dgnc_poll_timer);
	dgnc_poll_timer.function = dgnc_poll_handler;
	dgnc_poll_timer.data = 0;
	dgnc_poll_time = jiffies + dgnc_jiffies_from_ms(dgnc_poll_tick);
	dgnc_poll_timer.expires = dgnc_poll_time;
	spin_unlock_irqrestore(&dgnc_poll_lock, flags);

	add_timer(&dgnc_poll_timer);

	return rc;
}

/* returns count (>= 0), or negative on error */
static int dgnc_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int rc;

	/* wake up and enable device */
	rc = pci_enable_device(pdev);

	if (rc < 0) {
		rc = -EIO;
	} else {
		rc = dgnc_found_board(pdev, ent->driver_data);
		if (rc == 0)
			dgnc_NumBoards++;
	}
	return rc;
}

/*
 * dgnc_cleanup_board()
 *
 * Free all the memory associated with a board
 */
static void dgnc_cleanup_board(struct dgnc_board *brd)
{
	int i = 0;

	if (!brd || brd->magic != DGNC_BOARD_MAGIC)
		return;

	switch (brd->device) {
	case PCI_DEVICE_CLASSIC_4_DID:
	case PCI_DEVICE_CLASSIC_8_DID:
	case PCI_DEVICE_CLASSIC_4_422_DID:
	case PCI_DEVICE_CLASSIC_8_422_DID:

		/* Tell card not to interrupt anymore. */
		outb(0, brd->iobase + 0x4c);
		break;

	default:
		break;
	}

	if (brd->irq)
		free_irq(brd->irq, brd);

	tasklet_kill(&brd->helper_tasklet);

	if (brd->re_map_membase) {
		iounmap(brd->re_map_membase);
		brd->re_map_membase = NULL;
	}

	if (brd->msgbuf_head) {
		unsigned long flags;

		spin_lock_irqsave(&dgnc_global_lock, flags);
		brd->msgbuf = NULL;
		printk("%s", brd->msgbuf_head);
		kfree(brd->msgbuf_head);
		brd->msgbuf_head = NULL;
		spin_unlock_irqrestore(&dgnc_global_lock, flags);
	}

	/* Free all allocated channels structs */
	for (i = 0; i < MAXPORTS ; i++) {
		if (brd->channels[i]) {
			kfree(brd->channels[i]->ch_rqueue);
			kfree(brd->channels[i]->ch_equeue);
			kfree(brd->channels[i]->ch_wqueue);
			kfree(brd->channels[i]);
			brd->channels[i] = NULL;
		}
	}

	kfree(brd->flipbuf);

	dgnc_Board[brd->boardnum] = NULL;

	kfree(brd);
}


/*
 * dgnc_found_board()
 *
 * A board has been found, init it.
 */
static int dgnc_found_board(struct pci_dev *pdev, int id)
{
	struct dgnc_board *brd;
	unsigned int pci_irq;
	int i = 0;
	int rc = 0;
	unsigned long flags;

	/* get the board structure and prep it */
	dgnc_Board[dgnc_NumBoards] = kzalloc(sizeof(*brd), GFP_KERNEL);
	brd = dgnc_Board[dgnc_NumBoards];

	if (!brd)
		return -ENOMEM;

	/* make a temporary message buffer for the boot messages */
	brd->msgbuf_head = kzalloc(sizeof(u8) * 8192, GFP_KERNEL);
	brd->msgbuf = brd->msgbuf_head;

	if (!brd->msgbuf) {
		kfree(brd);
		return -ENOMEM;
	}

	/* store the info for the board we've found */
	brd->magic = DGNC_BOARD_MAGIC;
	brd->boardnum = dgnc_NumBoards;
	brd->vendor = dgnc_pci_tbl[id].vendor;
	brd->device = dgnc_pci_tbl[id].device;
	brd->pdev = pdev;
	brd->pci_bus = pdev->bus->number;
	brd->pci_slot = PCI_SLOT(pdev->devfn);
	brd->name = dgnc_Ids[id].name;
	brd->maxports = dgnc_Ids[id].maxports;
	if (dgnc_Ids[i].is_pci_express)
		brd->bd_flags |= BD_IS_PCI_EXPRESS;
	brd->dpastatus = BD_NOFEP;
	init_waitqueue_head(&brd->state_wait);

	spin_lock_init(&brd->bd_lock);
	spin_lock_init(&brd->bd_intr_lock);

	brd->state		= BOARD_FOUND;

	for (i = 0; i < MAXPORTS; i++)
		brd->channels[i] = NULL;

	/* store which card & revision we have */
	pci_read_config_word(pdev, PCI_SUBSYSTEM_VENDOR_ID, &brd->subvendor);
	pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &brd->subdevice);
	pci_read_config_byte(pdev, PCI_REVISION_ID, &brd->rev);

	pci_irq = pdev->irq;
	brd->irq = pci_irq;


	switch (brd->device) {

	case PCI_DEVICE_CLASSIC_4_DID:
	case PCI_DEVICE_CLASSIC_8_DID:
	case PCI_DEVICE_CLASSIC_4_422_DID:
	case PCI_DEVICE_CLASSIC_8_422_DID:

		brd->dpatype = T_CLASSIC | T_PCIBUS;

		/*
		 * For PCI ClassicBoards
		 * PCI Local Address (i.e. "resource" number) space
		 * 0	PLX Memory Mapped Config
		 * 1	PLX I/O Mapped Config
		 * 2	I/O Mapped UARTs and Status
		 * 3	Memory Mapped VPD
		 * 4	Memory Mapped UARTs and Status
		 */


		/* get the PCI Base Address Registers */
		brd->membase = pci_resource_start(pdev, 4);

		if (!brd->membase) {
			dev_err(&brd->pdev->dev,
				"Card has no PCI IO resources, failing.\n");
			return -ENODEV;
		}

		brd->membase_end = pci_resource_end(pdev, 4);

		if (brd->membase & 1)
			brd->membase &= ~3;
		else
			brd->membase &= ~15;

		brd->iobase	= pci_resource_start(pdev, 1);
		brd->iobase_end = pci_resource_end(pdev, 1);
		brd->iobase	= ((unsigned int) (brd->iobase)) & 0xFFFE;

		/* Assign the board_ops struct */
		brd->bd_ops = &dgnc_cls_ops;

		brd->bd_uart_offset = 0x8;
		brd->bd_dividend = 921600;

		dgnc_do_remap(brd);

		/* Get and store the board VPD, if it exists */
		brd->bd_ops->vpd(brd);

		/*
		 * Enable Local Interrupt 1		  (0x1),
		 * Local Interrupt 1 Polarity Active high (0x2),
		 * Enable PCI interrupt			  (0x40)
		 */
		outb(0x43, brd->iobase + 0x4c);

		break;


	case PCI_DEVICE_NEO_4_DID:
	case PCI_DEVICE_NEO_8_DID:
	case PCI_DEVICE_NEO_2DB9_DID:
	case PCI_DEVICE_NEO_2DB9PRI_DID:
	case PCI_DEVICE_NEO_2RJ45_DID:
	case PCI_DEVICE_NEO_2RJ45PRI_DID:
	case PCI_DEVICE_NEO_1_422_DID:
	case PCI_DEVICE_NEO_1_422_485_DID:
	case PCI_DEVICE_NEO_2_422_485_DID:
	case PCI_DEVICE_NEO_EXPRESS_8_DID:
	case PCI_DEVICE_NEO_EXPRESS_4_DID:
	case PCI_DEVICE_NEO_EXPRESS_4RJ45_DID:
	case PCI_DEVICE_NEO_EXPRESS_8RJ45_DID:

		/*
		 * This chip is set up 100% when we get to it.
		 * No need to enable global interrupts or anything.
		 */
		if (brd->bd_flags & BD_IS_PCI_EXPRESS)
			brd->dpatype = T_NEO_EXPRESS | T_PCIBUS;
		else
			brd->dpatype = T_NEO | T_PCIBUS;

		/* get the PCI Base Address Registers */
		brd->membase     = pci_resource_start(pdev, 0);
		brd->membase_end = pci_resource_end(pdev, 0);

		if (brd->membase & 1)
			brd->membase &= ~3;
		else
			brd->membase &= ~15;

		/* Assign the board_ops struct */
		brd->bd_ops = &dgnc_neo_ops;

		brd->bd_uart_offset = 0x200;
		brd->bd_dividend = 921600;

		dgnc_do_remap(brd);

		if (brd->re_map_membase) {

			/* After remap is complete, we need to read and store the dvid */
			brd->dvid = readb(brd->re_map_membase + 0x8D);

			/* Get and store the board VPD, if it exists */
			brd->bd_ops->vpd(brd);
		}
		break;

	default:
		dev_err(&brd->pdev->dev,
			"Didn't find any compatible Neo/Classic PCI boards.\n");
		return -ENXIO;

	}

	/*
	 * Do tty device initialization.
	 */

	rc = dgnc_tty_register(brd);
	if (rc < 0) {
		dgnc_tty_uninit(brd);
		pr_err(DRVSTR ": Can't register tty devices (%d)\n", rc);
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
		goto failed;
	}

	rc = dgnc_finalize_board_init(brd);
	if (rc < 0) {
		pr_err(DRVSTR ": Can't finalize board init (%d)\n", rc);
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;

		goto failed;
	}

	rc = dgnc_tty_init(brd);
	if (rc < 0) {
		dgnc_tty_uninit(brd);
		pr_err(DRVSTR ": Can't init tty devices (%d)\n", rc);
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;

		goto failed;
	}

	brd->state = BOARD_READY;
	brd->dpastatus = BD_RUNNING;

	dgnc_create_ports_sysfiles(brd);

	/* init our poll helper tasklet */
	tasklet_init(&brd->helper_tasklet, brd->bd_ops->tasklet, (unsigned long) brd);

	spin_lock_irqsave(&dgnc_global_lock, flags);
	brd->msgbuf = NULL;
	printk("%s", brd->msgbuf_head);
	kfree(brd->msgbuf_head);
	brd->msgbuf_head = NULL;
	spin_unlock_irqrestore(&dgnc_global_lock, flags);

	/*
	 * allocate flip buffer for board.
	 *
	 * Okay to malloc with GFP_KERNEL, we are not at interrupt
	 * context, and there are no locks held.
	 */
	brd->flipbuf = kzalloc(MYFLIPLEN, GFP_KERNEL);

	wake_up_interruptible(&brd->state_wait);

	return 0;

failed:

	return -ENXIO;

}


static int dgnc_finalize_board_init(struct dgnc_board *brd)
{
	int rc = 0;

	if (!brd || brd->magic != DGNC_BOARD_MAGIC)
		return -ENODEV;

	if (brd->irq) {
		rc = request_irq(brd->irq, brd->bd_ops->intr,
				 IRQF_SHARED, "DGNC", brd);

		if (rc) {
			dev_err(&brd->pdev->dev,
				"Failed to hook IRQ %d\n", brd->irq);
			brd->state = BOARD_FAILED;
			brd->dpastatus = BD_NOFEP;
			rc = -ENODEV;
		}
	}
	return rc;
}

/*
 * Remap PCI memory.
 */
static void dgnc_do_remap(struct dgnc_board *brd)
{

	if (!brd || brd->magic != DGNC_BOARD_MAGIC)
		return;

	brd->re_map_membase = ioremap(brd->membase, 0x1000);
}


/*****************************************************************************
*
* Function:
*
*    dgnc_poll_handler
*
* Author:
*
*    Scott H Kilau
*
* Parameters:
*
*    dummy -- ignored
*
* Return Values:
*
*    none
*
* Description:
*
*    As each timer expires, it determines (a) whether the "transmit"
*    waiter needs to be woken up, and (b) whether the poller needs to
*    be rescheduled.
*
******************************************************************************/

static void dgnc_poll_handler(ulong dummy)
{
	struct dgnc_board *brd;
	unsigned long flags;
	int i;
	unsigned long new_time;

	/* Go thru each board, kicking off a tasklet for each if needed */
	for (i = 0; i < dgnc_NumBoards; i++) {
		brd = dgnc_Board[i];

		spin_lock_irqsave(&brd->bd_lock, flags);

		/* If board is in a failed state, don't bother scheduling a tasklet */
		if (brd->state == BOARD_FAILED) {
			spin_unlock_irqrestore(&brd->bd_lock, flags);
			continue;
		}

		/* Schedule a poll helper task */
		tasklet_schedule(&brd->helper_tasklet);

		spin_unlock_irqrestore(&brd->bd_lock, flags);
	}

	/*
	 * Schedule ourself back at the nominal wakeup interval.
	 */
	spin_lock_irqsave(&dgnc_poll_lock, flags);
	dgnc_poll_time += dgnc_jiffies_from_ms(dgnc_poll_tick);

	new_time = dgnc_poll_time - jiffies;

	if ((ulong) new_time >= 2 * dgnc_poll_tick)
		dgnc_poll_time = jiffies +  dgnc_jiffies_from_ms(dgnc_poll_tick);

	init_timer(&dgnc_poll_timer);
	dgnc_poll_timer.function = dgnc_poll_handler;
	dgnc_poll_timer.data = 0;
	dgnc_poll_timer.expires = dgnc_poll_time;
	spin_unlock_irqrestore(&dgnc_poll_lock, flags);

	if (!dgnc_poll_stop)
		add_timer(&dgnc_poll_timer);
}

/*
 * dgnc_init_globals()
 *
 * This is where we initialize the globals from the static insmod
 * configuration variables.  These are declared near the head of
 * this file.
 */
static void dgnc_init_globals(void)
{
	int i = 0;

	dgnc_NumBoards		= 0;

	for (i = 0; i < MAXBOARDS; i++)
		dgnc_Board[i] = NULL;

	init_timer(&dgnc_poll_timer);
}

