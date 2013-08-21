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
#include <linux/version.h>
#include <linux/module.h>
#include <linux/pci.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
#include <linux/sched.h>
#endif

#include "dgnc_driver.h"
#include "dgnc_pci.h"
#include "dpacompat.h"
#include "dgnc_mgmt.h"
#include "dgnc_tty.h"
#include "dgnc_trace.h"
#include "dgnc_cls.h"
#include "dgnc_neo.h"
#include "dgnc_sysfs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Digi International, http://www.digi.com");
MODULE_DESCRIPTION("Driver for the Digi International Neo and Classic PCI based product line");
MODULE_SUPPORTED_DEVICE("dgnc");

/*
 * insmod command line overrideable parameters
 *
 * NOTE: we use a set of macros to create the variables, which allows
 * us to specify the variable type, name, initial value, and description.
 */
PARM_INT(debug,		0x00,		0644,	"Driver debugging level");
PARM_INT(rawreadok,	1,		0644,	"Bypass flip buffers on input");
PARM_INT(trcbuf_size,	0x100000,	0644, 	"Debugging trace buffer size.");

/**************************************************************************
 *
 * protos for this file
 *
 */
static int		dgnc_start(void);
static int		dgnc_finalize_board_init(struct board_t *brd);
static void		dgnc_init_globals(void);
static int		dgnc_found_board(struct pci_dev *pdev, int id);
static void		dgnc_cleanup_board(struct board_t *brd);
static void		dgnc_poll_handler(ulong dummy);
static int		dgnc_init_pci(void);
static int		dgnc_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);
static void		dgnc_remove_one(struct pci_dev *dev);
static int		dgnc_probe1(struct pci_dev *pdev, int card_type);
static void		dgnc_do_remap(struct board_t *brd);
static void		dgnc_mbuf(struct board_t *brd, const char *fmt, ...);


/* Driver load/unload functions */
int		dgnc_init_module(void);
void		dgnc_cleanup_module(void);

module_init(dgnc_init_module);
module_exit(dgnc_cleanup_module);


/*
 * File operations permitted on Control/Management major.
 */
static struct file_operations dgnc_BoardFops =
{
	.owner		=	THIS_MODULE,
        .unlocked_ioctl =  	dgnc_mgmt_ioctl,
	.open		=	dgnc_mgmt_open,
	.release	=	dgnc_mgmt_close
};


/*
 * Globals
 */
uint			dgnc_NumBoards;
struct board_t		*dgnc_Board[MAXBOARDS];
DEFINE_SPINLOCK(dgnc_global_lock);
int			dgnc_driver_state = DRIVER_INITIALIZED;
ulong			dgnc_poll_counter;
uint			dgnc_Major;
int			dgnc_poll_tick = 20;	/* Poll interval - 20 ms */

/*
 * Static vars.
 */
static uint		dgnc_Major_Control_Registered = FALSE;
static uint		dgnc_driver_start = FALSE;

static struct class *dgnc_class;

/*
 * Poller stuff
 */
static 			DEFINE_SPINLOCK(dgnc_poll_lock);	/* Poll scheduling lock */
static ulong		dgnc_poll_time;				/* Time of next poll */
static uint		dgnc_poll_stop;				/* Used to tell poller to stop */
static struct timer_list dgnc_poll_timer;


static struct pci_device_id dgnc_pci_tbl[] = {
	{	DIGI_VID, PCI_DEVICE_CLASSIC_4_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	0 },
	{	DIGI_VID, PCI_DEVICE_CLASSIC_4_422_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	1 },
	{	DIGI_VID, PCI_DEVICE_CLASSIC_8_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	2 },
	{	DIGI_VID, PCI_DEVICE_CLASSIC_8_422_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	3 },
	{	DIGI_VID, PCI_DEVICE_NEO_4_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,		4 },
	{	DIGI_VID, PCI_DEVICE_NEO_8_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,		5 },
	{	DIGI_VID, PCI_DEVICE_NEO_2DB9_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	6 },
	{	DIGI_VID, PCI_DEVICE_NEO_2DB9PRI_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	7 },
	{	DIGI_VID, PCI_DEVICE_NEO_2RJ45_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	8 },
	{	DIGI_VID, PCI_DEVICE_NEO_2RJ45PRI_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	9 },
	{	DIGI_VID, PCI_DEVICE_NEO_1_422_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	10 },
	{	DIGI_VID, PCI_DEVICE_NEO_1_422_485_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	11 },
	{	DIGI_VID, PCI_DEVICE_NEO_2_422_485_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	12 },
	{	DIGI_VID, PCI_DEVICE_NEO_EXPRESS_8_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	 13 },
	{	DIGI_VID, PCI_DEVICE_NEO_EXPRESS_4_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	14 },
	{	DIGI_VID, PCI_DEVICE_NEO_EXPRESS_4RJ45_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	15 },
	{	DIGI_VID, PCI_DEVICE_NEO_EXPRESS_8RJ45_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	16 },
	{0,}						/* 0 terminated list. */
};
MODULE_DEVICE_TABLE(pci, dgnc_pci_tbl);

struct board_id {
	uchar *name;
	uint maxports;
	unsigned int is_pci_express;
};

static struct board_id dgnc_Ids[] =
{
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
	.remove		= dgnc_remove_one,
};


char *dgnc_state_text[] = {
	"Board Failed",
	"Board Found",
	"Board READY",
};

char *dgnc_driver_state_text[] = {
	"Driver Initialized",
	"Driver Ready."
};



/************************************************************************
 *
 * Driver load/unload functions
 *
 ************************************************************************/


/*
 * init_module()
 *
 * Module load.  This is where it all starts.
 */
int dgnc_init_module(void)
{
	int rc = 0;

	APR(("%s, Digi International Part Number %s\n", DG_NAME, DG_PART));

	/*
	 * Initialize global stuff
	 */
	rc = dgnc_start();

	if (rc < 0) {
		return(rc);
	}

	/*
	 * Find and configure all the cards
	 */
	rc = dgnc_init_pci();

	/*
	 * If something went wrong in the scan, bail out of driver.
	 */
	if (rc < 0) {
		/* Only unregister the pci driver if it was actually registered. */
		if (dgnc_NumBoards)
			pci_unregister_driver(&dgnc_driver);
		else
			printk("WARNING: dgnc driver load failed.  No Digi Neo or Classic boards found.\n");

		dgnc_cleanup_module();
	}
	else {
		dgnc_create_driver_sysfiles(&dgnc_driver);
	}

	DPR_INIT(("Finished init_module. Returning %d\n", rc));
	return (rc);
}


/*
 * Start of driver.
 */
static int dgnc_start(void)
{
	int rc = 0;
	unsigned long flags;

	if (dgnc_driver_start == FALSE) {

		dgnc_driver_start = TRUE;

		/* make sure that the globals are init'd before we do anything else */
		dgnc_init_globals();

		dgnc_NumBoards = 0;

		APR(("For the tools package or updated drivers please visit http://www.digi.com\n"));

		/*
		 * Register our base character device into the kernel.
		 * This allows the download daemon to connect to the downld device
		 * before any of the boards are init'ed.
		 */
		if (!dgnc_Major_Control_Registered) {
			/*
			 * Register management/dpa devices
			 */
			rc = register_chrdev(0, "dgnc", &dgnc_BoardFops);
			if (rc <= 0) {
				APR(("Can't register dgnc driver device (%d)\n", rc));
				rc = -ENXIO;
				return(rc);
			}
			dgnc_Major = rc;

			dgnc_class = class_create(THIS_MODULE, "dgnc_mgmt");
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
			device_create_drvdata(dgnc_class, NULL,
				MKDEV(dgnc_Major, 0),
				NULL, "dgnc_mgmt");
#else
                        device_create(dgnc_class, NULL,
                                MKDEV(dgnc_Major, 0),
                                NULL, "dgnc_mgmt");
#endif

			dgnc_Major_Control_Registered = TRUE;
		}

		/*
		 * Init any global tty stuff.
		 */
		rc = dgnc_tty_preinit();

		if (rc < 0) {
			APR(("tty preinit - not enough memory (%d)\n", rc));
			return(rc);
		}

		/* Start the poller */
		DGNC_LOCK(dgnc_poll_lock, flags);
		init_timer(&dgnc_poll_timer);
		dgnc_poll_timer.function = dgnc_poll_handler;
		dgnc_poll_timer.data = 0;
		dgnc_poll_time = jiffies + dgnc_jiffies_from_ms(dgnc_poll_tick);
		dgnc_poll_timer.expires = dgnc_poll_time;
		DGNC_UNLOCK(dgnc_poll_lock, flags);

		add_timer(&dgnc_poll_timer);

		dgnc_driver_state = DRIVER_READY;
	}

	return(rc);
}

/*
 * Register pci driver, and return how many boards we have.
 */
static int dgnc_init_pci(void)
{
	return pci_register_driver(&dgnc_driver);
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
		rc = dgnc_probe1(pdev, ent->driver_data);
		if (rc == 0) {
			dgnc_NumBoards++;
	                DPR_INIT(("Incrementing numboards to %d\n", dgnc_NumBoards));
		}
	}
	return rc;
}

static int dgnc_probe1(struct pci_dev *pdev, int card_type)
{
	return dgnc_found_board(pdev, card_type);
}


static void dgnc_remove_one(struct pci_dev *dev)
{
	/* Do Nothing */
}

/*
 * dgnc_cleanup_module()
 *
 * Module unload.  This is where it all ends.
 */
void dgnc_cleanup_module(void)
{
	int i;
	ulong lock_flags;

	DGNC_LOCK(dgnc_poll_lock, lock_flags);
	dgnc_poll_stop = 1;
	DGNC_UNLOCK(dgnc_poll_lock, lock_flags);

	/* Turn off poller right away. */
	del_timer_sync(&dgnc_poll_timer);

	dgnc_remove_driver_sysfiles(&dgnc_driver);

	if (dgnc_Major_Control_Registered) {
		device_destroy(dgnc_class, MKDEV(dgnc_Major, 0));
		class_destroy(dgnc_class);
		unregister_chrdev(dgnc_Major, "dgnc");
	}

	for (i = 0; i < dgnc_NumBoards; ++i) {
		dgnc_remove_ports_sysfiles(dgnc_Board[i]);
		dgnc_tty_uninit(dgnc_Board[i]);
		dgnc_cleanup_board(dgnc_Board[i]);
	}

	dgnc_tty_post_uninit();

#if defined(DGNC_TRACER)
	/* last thing, make sure we release the tracebuffer */
	dgnc_tracer_free();
#endif
	if (dgnc_NumBoards)
		pci_unregister_driver(&dgnc_driver);
}


/*
 * dgnc_cleanup_board()
 *
 * Free all the memory associated with a board
 */
static void dgnc_cleanup_board(struct board_t *brd)
{
	int i = 0;

        if(!brd || brd->magic != DGNC_BOARD_MAGIC)
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

		DGNC_LOCK(dgnc_global_lock, flags);
		brd->msgbuf = NULL;
		printk(brd->msgbuf_head);
		kfree(brd->msgbuf_head);
		brd->msgbuf_head = NULL;
		DGNC_UNLOCK(dgnc_global_lock, flags);
        }

	/* Free all allocated channels structs */
	for (i = 0; i < MAXPORTS ; i++) {
		if (brd->channels[i]) {
			if (brd->channels[i]->ch_rqueue)
				kfree(brd->channels[i]->ch_rqueue);
			if (brd->channels[i]->ch_equeue)
				kfree(brd->channels[i]->ch_equeue);
			if (brd->channels[i]->ch_wqueue)
				kfree(brd->channels[i]->ch_wqueue);

			kfree(brd->channels[i]);
			brd->channels[i] = NULL;
		}
	}

	if (brd->flipbuf)
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
        struct board_t *brd;
	unsigned int pci_irq;
	int i = 0;
	int rc = 0;
	unsigned long flags;

        /* get the board structure and prep it */
        brd = dgnc_Board[dgnc_NumBoards] =
        (struct board_t *) dgnc_driver_kzmalloc(sizeof(struct board_t), GFP_KERNEL);
	if (!brd) {
		APR(("memory allocation for board structure failed\n"));
		return(-ENOMEM);
	}

        /* make a temporary message buffer for the boot messages */
        brd->msgbuf = brd->msgbuf_head =
                (char *) dgnc_driver_kzmalloc(sizeof(char) * 8192, GFP_KERNEL);
        if (!brd->msgbuf) {
		kfree(brd);
		APR(("memory allocation for board msgbuf failed\n"));
		return(-ENOMEM);
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

	DGNC_SPINLOCK_INIT(brd->bd_lock);
	DGNC_SPINLOCK_INIT(brd->bd_intr_lock);

	brd->state		= BOARD_FOUND;

	for (i = 0; i < MAXPORTS; i++) {
		brd->channels[i] = NULL;
	}

	/* store which card & revision we have */
	pci_read_config_word(pdev, PCI_SUBSYSTEM_VENDOR_ID, &brd->subvendor);
	pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &brd->subdevice);
	pci_read_config_byte(pdev, PCI_REVISION_ID, &brd->rev);

	pci_irq = pdev->irq;
	brd->irq = pci_irq;


	switch(brd->device) {

	case PCI_DEVICE_CLASSIC_4_DID:
	case PCI_DEVICE_CLASSIC_8_DID:
	case PCI_DEVICE_CLASSIC_4_422_DID:
	case PCI_DEVICE_CLASSIC_8_422_DID:

		brd->dpatype = T_CLASSIC | T_PCIBUS;

                DPR_INIT(("dgnc_found_board - Classic.\n"));

		/*
		 * For PCI ClassicBoards
		 * PCI Local Address (i.e. "resource" number) space
		 * 0        PLX Memory Mapped Config
		 * 1        PLX I/O Mapped Config
		 * 2        I/O Mapped UARTs and Status
		 * 3        Memory Mapped VPD
		 * 4        Memory Mapped UARTs and Status
		 */


		/* get the PCI Base Address Registers */
		brd->membase = pci_resource_start(pdev, 4);

		if (!brd->membase) {
			APR(("card has no PCI IO resources, failing board.\n"));
			return -ENODEV;
		}

		brd->membase_end = pci_resource_end(pdev, 4);

		if (brd->membase & 1)
			brd->membase &= ~3;
		else
			brd->membase &= ~15;

		brd->iobase     = pci_resource_start(pdev, 1);
		brd->iobase_end = pci_resource_end(pdev, 1);
 		brd->iobase     = ((unsigned int) (brd->iobase)) & 0xFFFE;

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

                DPR_INIT(("dgnc_found_board - NEO.\n"));

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
		APR(("Did not find any compatible Neo or Classic PCI boards in system.\n"));
		return (-ENXIO);

	}

	/*
	 * Do tty device initialization.
	 */

	rc = dgnc_tty_register(brd);
	if (rc < 0) {
		dgnc_tty_uninit(brd);
		APR(("Can't register tty devices (%d)\n", rc));
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
		goto failed;
	}

	rc = dgnc_finalize_board_init(brd);
	if (rc < 0) {
		APR(("Can't finalize board init (%d)\n", rc));
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;

		goto failed;
	}

	rc = dgnc_tty_init(brd);
	if (rc < 0) {
		dgnc_tty_uninit(brd);
		APR(("Can't init tty devices (%d)\n", rc));
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;

		goto failed;
	}

	brd->state = BOARD_READY;
	brd->dpastatus = BD_RUNNING;

	dgnc_create_ports_sysfiles(brd);

	/* init our poll helper tasklet */
	tasklet_init(&brd->helper_tasklet, brd->bd_ops->tasklet, (unsigned long) brd);

	 /* Log the information about the board */
	dgnc_mbuf(brd, DRVSTR": board %d: %s (rev %d), irq %d\n",
		dgnc_NumBoards, brd->name, brd->rev, brd->irq);

	DPR_INIT(("dgnc_scan(%d) - printing out the msgbuf\n", i));
	DGNC_LOCK(dgnc_global_lock, flags);
	brd->msgbuf = NULL;
	printk(brd->msgbuf_head);
	kfree(brd->msgbuf_head);
	brd->msgbuf_head = NULL;
	DGNC_UNLOCK(dgnc_global_lock, flags);

	/*
	 * allocate flip buffer for board.
	 *
	 * Okay to malloc with GFP_KERNEL, we are not at interrupt
	 * context, and there are no locks held.
	 */
	brd->flipbuf = dgnc_driver_kzmalloc(MYFLIPLEN, GFP_KERNEL);

	wake_up_interruptible(&brd->state_wait);

        return(0);

failed:

	return (-ENXIO);

}


static int dgnc_finalize_board_init(struct board_t *brd) {
	int rc = 0;

        DPR_INIT(("dgnc_finalize_board_init() - start\n"));

	if (!brd || brd->magic != DGNC_BOARD_MAGIC)
                return(-ENODEV);

        DPR_INIT(("dgnc_finalize_board_init() - start #2\n"));

	if (brd->irq) {
		rc = request_irq(brd->irq, brd->bd_ops->intr, IRQF_SHARED, "DGNC", brd);

		if (rc) {
			printk("Failed to hook IRQ %d\n",brd->irq);
			brd->state = BOARD_FAILED;
			brd->dpastatus = BD_NOFEP;
			rc = -ENODEV;
		} else {
			DPR_INIT(("Requested and received usage of IRQ %d\n", brd->irq));
		}
	}
	return(rc);
}

/*
 * Remap PCI memory.
 */
static void dgnc_do_remap(struct board_t *brd)
{

	if (!brd || brd->magic != DGNC_BOARD_MAGIC)
		return;

	brd->re_map_membase = ioremap(brd->membase, 0x1000);

	DPR_INIT(("remapped mem: 0x%p\n", brd->re_map_membase));
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
        struct board_t *brd;
        unsigned long lock_flags;
	int i;
	unsigned long new_time;

	dgnc_poll_counter++;

	/*
	 * Do not start the board state machine until
	 * driver tells us its up and running, and has
	 * everything it needs.
	 */
	if (dgnc_driver_state != DRIVER_READY) {
		goto schedule_poller;
	}

	/* Go thru each board, kicking off a tasklet for each if needed */
	for (i = 0; i < dgnc_NumBoards; i++) {
		brd = dgnc_Board[i];

		DGNC_LOCK(brd->bd_lock, lock_flags);

		/* If board is in a failed state, don't bother scheduling a tasklet */
		if (brd->state == BOARD_FAILED) {
			DGNC_UNLOCK(brd->bd_lock, lock_flags);
			continue;
		}

		/* Schedule a poll helper task */
		tasklet_schedule(&brd->helper_tasklet);

		DGNC_UNLOCK(brd->bd_lock, lock_flags);
	}

schedule_poller:

	/*
	 * Schedule ourself back at the nominal wakeup interval.
	 */
	DGNC_LOCK(dgnc_poll_lock, lock_flags);
	dgnc_poll_time += dgnc_jiffies_from_ms(dgnc_poll_tick);

	new_time = dgnc_poll_time - jiffies;

	if ((ulong) new_time >= 2 * dgnc_poll_tick) {
		dgnc_poll_time = jiffies +  dgnc_jiffies_from_ms(dgnc_poll_tick);
	}

	init_timer(&dgnc_poll_timer);
	dgnc_poll_timer.function = dgnc_poll_handler;
	dgnc_poll_timer.data = 0;
	dgnc_poll_timer.expires = dgnc_poll_time;
	DGNC_UNLOCK(dgnc_poll_lock, lock_flags);

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

	dgnc_rawreadok		= rawreadok;
        dgnc_trcbuf_size	= trcbuf_size;
	dgnc_debug		= debug;

	for (i = 0; i < MAXBOARDS; i++) {
		dgnc_Board[i] = NULL;
	}

	init_timer(&dgnc_poll_timer);
}


/************************************************************************
 *
 * Utility functions
 *
 ************************************************************************/


/*
 * dgnc_driver_kzmalloc()
 *
 * Malloc and clear memory,
 */
void *dgnc_driver_kzmalloc(size_t size, int priority)
{
 	void *p = kmalloc(size, priority);
	if(p)
		memset(p, 0, size);
	return(p);
}


/*
 * dgnc_mbuf()
 *
 * Used to print to the message buffer during board init.
 */
static void dgnc_mbuf(struct board_t *brd, const char *fmt, ...) {
	va_list		ap;
	char		buf[1024];
	int		i;
	unsigned long	flags;

	DGNC_LOCK(dgnc_global_lock, flags);

	/* Format buf using fmt and arguments contained in ap. */
	va_start(ap, fmt);
	i = vsprintf(buf, fmt,  ap);
	va_end(ap);

	DPR((buf));

	if (!brd || !brd->msgbuf) {
		printk(buf);
		DGNC_UNLOCK(dgnc_global_lock, flags);
		return;
	}

	memcpy(brd->msgbuf, buf, strlen(buf));
	brd->msgbuf += strlen(buf);
	*brd->msgbuf = '\0';

	DGNC_UNLOCK(dgnc_global_lock, flags);
}


/*
 * dgnc_ms_sleep()
 *
 * Put the driver to sleep for x ms's
 *
 * Returns 0 if timed out, !0 (showing signal) if interrupted by a signal.
 */
int dgnc_ms_sleep(ulong ms)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout((ms * HZ) / 1000);
	return (signal_pending(current));
}



/*
 *      dgnc_ioctl_name() : Returns a text version of each ioctl value.
 */
char *dgnc_ioctl_name(int cmd)
{
	switch(cmd) {

	case TCGETA:		return("TCGETA");
	case TCGETS:		return("TCGETS");
	case TCSETA:		return("TCSETA");
	case TCSETS:		return("TCSETS");
	case TCSETAW:		return("TCSETAW");
	case TCSETSW:		return("TCSETSW");
	case TCSETAF:		return("TCSETAF");
	case TCSETSF:		return("TCSETSF");
	case TCSBRK:		return("TCSBRK");
	case TCXONC:		return("TCXONC");
	case TCFLSH:		return("TCFLSH");
	case TIOCGSID:		return("TIOCGSID");

	case TIOCGETD:		return("TIOCGETD");
	case TIOCSETD:		return("TIOCSETD");
	case TIOCGWINSZ:	return("TIOCGWINSZ");
	case TIOCSWINSZ:	return("TIOCSWINSZ");

	case TIOCMGET:		return("TIOCMGET");
	case TIOCMSET:		return("TIOCMSET");
	case TIOCMBIS:		return("TIOCMBIS");
	case TIOCMBIC:		return("TIOCMBIC");

	/* from digi.h */
	case DIGI_SETA:		return("DIGI_SETA");
	case DIGI_SETAW:	return("DIGI_SETAW");
	case DIGI_SETAF:	return("DIGI_SETAF");
	case DIGI_SETFLOW:	return("DIGI_SETFLOW");
	case DIGI_SETAFLOW:	return("DIGI_SETAFLOW");
	case DIGI_GETFLOW:	return("DIGI_GETFLOW");
	case DIGI_GETAFLOW:	return("DIGI_GETAFLOW");
	case DIGI_GETA:		return("DIGI_GETA");
	case DIGI_GEDELAY:	return("DIGI_GEDELAY");
	case DIGI_SEDELAY:	return("DIGI_SEDELAY");
	case DIGI_GETCUSTOMBAUD: return("DIGI_GETCUSTOMBAUD");
	case DIGI_SETCUSTOMBAUD: return("DIGI_SETCUSTOMBAUD");
	case TIOCMODG:		return("TIOCMODG");
	case TIOCMODS:		return("TIOCMODS");
	case TIOCSDTR:		return("TIOCSDTR");
	case TIOCCDTR:		return("TIOCCDTR");

	default:		return("unknown");
	}
}
