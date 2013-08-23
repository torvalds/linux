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
 * $Id: dgap_driver.c,v 1.3 2011/06/21 10:35:16 markh Exp $
 */


#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>	/* For udelay */
#include <asm/uaccess.h>	/* For copy_from_user/copy_to_user */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
#include <linux/sched.h>
#endif

#include "dgap_driver.h"
#include "dgap_pci.h"
#include "dgap_proc.h"
#include "dgap_fep5.h"
#include "dgap_tty.h"
#include "dgap_conf.h"
#include "dgap_parse.h"
#include "dgap_trace.h"
#include "dgap_sysfs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Digi International, http://www.digi.com");
MODULE_DESCRIPTION("Driver for the Digi International EPCA PCI based product line");
MODULE_SUPPORTED_DEVICE("dgap");

/*
 * insmod command line overrideable parameters
 *
 * NOTE: we use a set of macros to create the variables, which allows
 * us to specify the variable type, name, initial value, and description.
 */
PARM_INT(debug,		0x00,		0644,	"Driver debugging level");
PARM_INT(rawreadok,	1,		0644,	"Bypass flip buffers on input");
PARM_INT(trcbuf_size,	0x100000,	0644,	"Debugging trace buffer size.");


/**************************************************************************
 *
 * protos for this file
 *
 */

static int		dgap_start(void);
static void		dgap_init_globals(void);
static int		dgap_found_board(struct pci_dev *pdev, int id);
static void		dgap_cleanup_board(struct board_t *brd);
static void		dgap_poll_handler(ulong dummy);
static int		dgap_init_pci(void);
static int		dgap_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);
static void		dgap_remove_one(struct pci_dev *dev);
static int		dgap_probe1(struct pci_dev *pdev, int card_type);
static void		dgap_mbuf(struct board_t *brd, const char *fmt, ...);
static int		dgap_do_remap(struct board_t *brd);
static irqreturn_t	dgap_intr(int irq, void *voidbrd);

/* Driver load/unload functions */
int			dgap_init_module(void);
void			dgap_cleanup_module(void);

module_init(dgap_init_module);
module_exit(dgap_cleanup_module);


/*
 * File operations permitted on Control/Management major.
 */
static struct file_operations DgapBoardFops =
{
	.owner		=	THIS_MODULE,
};


/*
 * Globals
 */
uint			dgap_NumBoards;
struct board_t		*dgap_Board[MAXBOARDS];
DEFINE_SPINLOCK(dgap_global_lock);
ulong			dgap_poll_counter;
char			*dgap_config_buf;
int			dgap_driver_state = DRIVER_INITIALIZED;
DEFINE_SPINLOCK(dgap_dl_lock);
wait_queue_head_t	dgap_dl_wait;
int			dgap_dl_action;
int			dgap_poll_tick = 20;	/* Poll interval - 20 ms */

/*
 * Static vars.
 */
static int		dgap_Major_Control_Registered = FALSE;
static uint		dgap_driver_start = FALSE;

static struct class *	dgap_class;

/*
 * Poller stuff
 */
static 			DEFINE_SPINLOCK(dgap_poll_lock);	/* Poll scheduling lock */
static ulong		dgap_poll_time;				/* Time of next poll */
static uint		dgap_poll_stop;				/* Used to tell poller to stop */
static struct timer_list dgap_poll_timer;


static struct pci_device_id dgap_pci_tbl[] = {
	{       DIGI_VID, PCI_DEVICE_XEM_DID,	PCI_ANY_ID, PCI_ANY_ID, 0, 0,	0 },
	{       DIGI_VID, PCI_DEVICE_CX_DID,	PCI_ANY_ID, PCI_ANY_ID, 0, 0,   1 },
	{       DIGI_VID, PCI_DEVICE_CX_IBM_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	2 },
	{       DIGI_VID, PCI_DEVICE_EPCJ_DID,	PCI_ANY_ID, PCI_ANY_ID, 0, 0,	3 },
	{       DIGI_VID, PCI_DEVICE_920_2_DID,	PCI_ANY_ID, PCI_ANY_ID, 0, 0,	4 },
	{       DIGI_VID, PCI_DEVICE_920_4_DID,	PCI_ANY_ID, PCI_ANY_ID, 0, 0,	5 },
	{       DIGI_VID, PCI_DEVICE_920_8_DID,	PCI_ANY_ID, PCI_ANY_ID, 0, 0,	6 },
	{       DIGI_VID, PCI_DEVICE_XR_DID,	PCI_ANY_ID, PCI_ANY_ID, 0, 0,	7 },
	{       DIGI_VID, PCI_DEVICE_XRJ_DID,	PCI_ANY_ID, PCI_ANY_ID, 0, 0,	8 },
	{       DIGI_VID, PCI_DEVICE_XR_422_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	9 },
	{       DIGI_VID, PCI_DEVICE_XR_IBM_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	10 },
	{       DIGI_VID, PCI_DEVICE_XR_SAIP_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	11 },
	{       DIGI_VID, PCI_DEVICE_XR_BULL_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	12 },
	{       DIGI_VID, PCI_DEVICE_920_8_HP_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 13 },
	{       DIGI_VID, PCI_DEVICE_XEM_HP_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,	14 },
	{0,}					/* 0 terminated list. */
};
MODULE_DEVICE_TABLE(pci, dgap_pci_tbl);


/*
 * A generic list of Product names, PCI Vendor ID, and PCI Device ID.
 */
struct board_id {
	uint config_type;
	uchar *name;
	uint maxports;
	uint dpatype;
};

static struct board_id dgap_Ids[] =
{
	{	PPCM,		PCI_DEVICE_XEM_NAME,	64,	(T_PCXM | T_PCLITE | T_PCIBUS)	},
	{	PCX,		PCI_DEVICE_CX_NAME,	128,	(T_CX | T_PCIBUS)		},
	{	PCX,		PCI_DEVICE_CX_IBM_NAME,	128,	(T_CX | T_PCIBUS)		},
	{	PEPC,		PCI_DEVICE_EPCJ_NAME,	224,	(T_EPC  | T_PCIBUS)		},
	{	APORT2_920P,	PCI_DEVICE_920_2_NAME,	2,	(T_PCXR | T_PCLITE | T_PCIBUS)	},
	{	APORT4_920P,	PCI_DEVICE_920_4_NAME,	4,	(T_PCXR | T_PCLITE | T_PCIBUS)	},
	{	APORT8_920P,	PCI_DEVICE_920_8_NAME,	8,	(T_PCXR | T_PCLITE | T_PCIBUS)	},
	{	PAPORT8,	PCI_DEVICE_XR_NAME,	8,	(T_PCXR | T_PCLITE | T_PCIBUS)	},
	{	PAPORT8,	PCI_DEVICE_XRJ_NAME,	8,	(T_PCXR | T_PCLITE | T_PCIBUS)	},
	{	PAPORT8,	PCI_DEVICE_XR_422_NAME,	8,	(T_PCXR | T_PCLITE | T_PCIBUS)	},
	{	PAPORT8,	PCI_DEVICE_XR_IBM_NAME,	8,	(T_PCXR | T_PCLITE | T_PCIBUS)	},
	{	PAPORT8,	PCI_DEVICE_XR_SAIP_NAME, 8,	(T_PCXR | T_PCLITE | T_PCIBUS)	},
	{	PAPORT8,	PCI_DEVICE_XR_BULL_NAME, 8,	(T_PCXR | T_PCLITE | T_PCIBUS)	},
	{	APORT8_920P,	PCI_DEVICE_920_8_HP_NAME, 8,	(T_PCXR | T_PCLITE | T_PCIBUS)	},
	{	PPCM,		PCI_DEVICE_XEM_HP_NAME,	64,	(T_PCXM | T_PCLITE | T_PCIBUS)	},
	{0,}						/* 0 terminated list. */
};

static struct pci_driver dgap_driver = {
	.name		= "dgap",
	.probe		= dgap_init_one,
	.id_table	= dgap_pci_tbl,
	.remove		= dgap_remove_one,
};


char *dgap_state_text[] = {
	"Board Failed",
	"Configuration for board not found.\n\t\t\tRun mpi to configure board.",
	"Board Found",
	"Need Reset",
	"Finished Reset",
	"Need Config",
	"Finished Config",
	"Need Device Creation",
	"Requested Device Creation",
	"Finished Device Creation",
	"Need BIOS Load", 
	"Requested BIOS", 
	"Doing BIOS Load",
	"Finished BIOS Load",
	"Need FEP Load", 
	"Requested FEP",
	"Doing FEP Load",
	"Finished FEP Load",
	"Requested PROC creation",
	"Finished PROC creation",
	"Board READY",
};

char *dgap_driver_state_text[] = {
	"Driver Initialized",
	"Driver needs configuration load.",
	"Driver requested configuration from download daemon.",
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
int dgap_init_module(void)
{
	int rc = 0;

	APR(("%s, Digi International Part Number %s\n", DG_NAME, DG_PART));

	/*
	 * Initialize global stuff
	 */
	rc = dgap_start();

	if (rc < 0) {
		return(rc);
	}

	/*
	 * Find and configure all the cards
	 */
	rc = dgap_init_pci();

	/*
	 * If something went wrong in the scan, bail out of driver.
	 */
	if (rc < 0) {
		/* Only unregister the pci driver if it was actually registered. */
		if (dgap_NumBoards)
			pci_unregister_driver(&dgap_driver);
		else
			printk("WARNING: dgap driver load failed.  No DGAP boards found.\n");

		dgap_cleanup_module();
	}
	else {
		dgap_create_driver_sysfiles(&dgap_driver);
	}
  
	DPR_INIT(("Finished init_module. Returning %d\n", rc));
	return (rc);
}


/*
 * Start of driver.
 */
static int dgap_start(void)
{
	int rc = 0;
	unsigned long flags;

	if (dgap_driver_start == FALSE) {

		dgap_driver_start = TRUE;

	        /* make sure that the globals are init'd before we do anything else */
	        dgap_init_globals();

		dgap_NumBoards = 0;

		APR(("For the tools package or updated drivers please visit http://www.digi.com\n"));

		/*
		 * Register our base character device into the kernel.
		 * This allows the download daemon to connect to the downld device
		 * before any of the boards are init'ed.
		 */
		if (!dgap_Major_Control_Registered) {
			/*
			 * Register management/dpa devices
			 */
			rc = register_chrdev(DIGI_DGAP_MAJOR, "dgap", &DgapBoardFops);
			if (rc < 0) {
				APR(("Can't register dgap driver device (%d)\n", rc));
				return (rc);
			}

			dgap_class = class_create(THIS_MODULE, "dgap_mgmt");
			device_create(dgap_class, NULL,
				MKDEV(DIGI_DGAP_MAJOR, 0),
				NULL, "dgap_mgmt");
			device_create(dgap_class, NULL,
				MKDEV(DIGI_DGAP_MAJOR, 1),
				NULL, "dgap_downld");
			dgap_Major_Control_Registered = TRUE;
		}

		/*
		 * Init any global tty stuff.
		 */
		rc = dgap_tty_preinit();

		if (rc < 0) {
			APR(("tty preinit - not enough memory (%d)\n", rc));
			return(rc); 
		}

		/* Start the poller */
		DGAP_LOCK(dgap_poll_lock, flags);
		init_timer(&dgap_poll_timer);
		dgap_poll_timer.function = dgap_poll_handler;
		dgap_poll_timer.data = 0;
		dgap_poll_time = jiffies + dgap_jiffies_from_ms(dgap_poll_tick);
		dgap_poll_timer.expires = dgap_poll_time;
		DGAP_UNLOCK(dgap_poll_lock, flags);

		add_timer(&dgap_poll_timer);

		dgap_driver_state = DRIVER_NEED_CONFIG_LOAD;
	}

	return (rc);
}


/*
 * Register pci driver, and return how many boards we have.
 */
static int dgap_init_pci(void)
{
	return pci_register_driver(&dgap_driver);
}


/* returns count (>= 0), or negative on error */
static int dgap_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int rc;

	/* wake up and enable device */
	rc = pci_enable_device(pdev);

	if (rc < 0) {
		rc = -EIO;
	} else {  
		rc = dgap_probe1(pdev, ent->driver_data);
		if (rc == 0) {
			dgap_NumBoards++;
			DPR_INIT(("Incrementing numboards to %d\n", dgap_NumBoards));
		}
	}
	return rc;
}               


static int dgap_probe1(struct pci_dev *pdev, int card_type)
{
	return dgap_found_board(pdev, card_type);
}
         
        
static void dgap_remove_one(struct pci_dev *dev)
{
	/* Do Nothing */
}


/*
 * dgap_cleanup_module()
 *
 * Module unload.  This is where it all ends.
 */
void dgap_cleanup_module(void)
{
	int i;
	ulong lock_flags;

	DGAP_LOCK(dgap_poll_lock, lock_flags);
	dgap_poll_stop = 1;
	DGAP_UNLOCK(dgap_poll_lock, lock_flags);

	/* Turn off poller right away. */
	del_timer_sync( &dgap_poll_timer);

	dgap_remove_driver_sysfiles(&dgap_driver);


	if (dgap_Major_Control_Registered) {
		device_destroy(dgap_class, MKDEV(DIGI_DGAP_MAJOR, 0));
		device_destroy(dgap_class, MKDEV(DIGI_DGAP_MAJOR, 1));
		class_destroy(dgap_class);
		unregister_chrdev(DIGI_DGAP_MAJOR, "dgap");
	}

	if (dgap_config_buf)
		kfree(dgap_config_buf);

	for (i = 0; i < dgap_NumBoards; ++i) {
		dgap_remove_ports_sysfiles(dgap_Board[i]);
		dgap_tty_uninit(dgap_Board[i]);
		dgap_cleanup_board(dgap_Board[i]);
	}

	dgap_tty_post_uninit();

#if defined(DGAP_TRACER)
	/* last thing, make sure we release the tracebuffer */
	dgap_tracer_free();
#endif
	if (dgap_NumBoards)
		pci_unregister_driver(&dgap_driver);
}


/*
 * dgap_cleanup_board()
 *
 * Free all the memory associated with a board
 */
static void dgap_cleanup_board(struct board_t *brd)
{
	int i = 0;

        if(!brd || brd->magic != DGAP_BOARD_MAGIC)
                return;

	if (brd->intr_used && brd->irq)
		free_irq(brd->irq, brd);

	tasklet_kill(&brd->helper_tasklet);

	if (brd->re_map_port) {
		release_mem_region(brd->membase + 0x200000, 0x200000);
		iounmap(brd->re_map_port);
		brd->re_map_port = NULL;
	}

	if (brd->re_map_membase) {
		release_mem_region(brd->membase, 0x200000);
		iounmap(brd->re_map_membase);
		brd->re_map_membase = NULL;
	}

        if (brd->msgbuf_head) {
                unsigned long flags;

                DGAP_LOCK(dgap_global_lock, flags);
                brd->msgbuf = NULL;
                printk(brd->msgbuf_head);
                kfree(brd->msgbuf_head);
                brd->msgbuf_head = NULL;
                DGAP_UNLOCK(dgap_global_lock, flags);
        }

	/* Free all allocated channels structs */
	for (i = 0; i < MAXPORTS ; i++) {
		if (brd->channels[i]) {
			kfree(brd->channels[i]);
			brd->channels[i] = NULL;
		}
	}

	if (brd->flipbuf)
		kfree(brd->flipbuf);
	if (brd->flipflagbuf)
		kfree(brd->flipflagbuf);

	dgap_Board[brd->boardnum] = NULL;

        kfree(brd);
}


/*
 * dgap_found_board()
 *
 * A board has been found, init it.
 */
static int dgap_found_board(struct pci_dev *pdev, int id)
{
	struct board_t *brd;
	unsigned int pci_irq;
	int i = 0;
	unsigned long flags;

	/* get the board structure and prep it */
	brd = dgap_Board[dgap_NumBoards] =
	(struct board_t *) dgap_driver_kzmalloc(sizeof(struct board_t), GFP_KERNEL);
	if (!brd) {
		APR(("memory allocation for board structure failed\n"));
		return(-ENOMEM);
	}

	/* make a temporary message buffer for the boot messages */
	brd->msgbuf = brd->msgbuf_head =
		(char *) dgap_driver_kzmalloc(sizeof(char) * 8192, GFP_KERNEL);
	if(!brd->msgbuf) {
		kfree(brd);
		APR(("memory allocation for board msgbuf failed\n"));
		return(-ENOMEM);
	}

	/* store the info for the board we've found */
	brd->magic = DGAP_BOARD_MAGIC;
	brd->boardnum = dgap_NumBoards;
	brd->firstminor = 0;
	brd->vendor = dgap_pci_tbl[id].vendor;
	brd->device = dgap_pci_tbl[id].device;
	brd->pdev = pdev;
	brd->pci_bus = pdev->bus->number;
	brd->pci_slot = PCI_SLOT(pdev->devfn);
	brd->name = dgap_Ids[id].name;
	brd->maxports = dgap_Ids[id].maxports;
	brd->type = dgap_Ids[id].config_type;
	brd->dpatype = dgap_Ids[id].dpatype;
	brd->dpastatus = BD_NOFEP;
	init_waitqueue_head(&brd->state_wait);

	DGAP_SPINLOCK_INIT(brd->bd_lock);

	brd->state		= BOARD_FOUND;
	brd->runwait		= 0;
	brd->inhibit_poller	= FALSE;
	brd->wait_for_bios	= 0;
	brd->wait_for_fep	= 0;

	for (i = 0; i < MAXPORTS; i++) {
		brd->channels[i] = NULL;
	}

	/* store which card & revision we have */
	pci_read_config_word(pdev, PCI_SUBSYSTEM_VENDOR_ID, &brd->subvendor);
	pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &brd->subdevice);
	pci_read_config_byte(pdev, PCI_REVISION_ID, &brd->rev);

	pci_irq = pdev->irq;
	brd->irq = pci_irq;

	/* get the PCI Base Address Registers */

	/* Xr Jupiter and EPC use BAR 2 */
	if (brd->device == PCI_DEVICE_XRJ_DID || brd->device == PCI_DEVICE_EPCJ_DID) {
		brd->membase     = pci_resource_start(pdev, 2);
		brd->membase_end = pci_resource_end(pdev, 2);
	}
	/* Everyone else uses BAR 0 */
	else {
		brd->membase     = pci_resource_start(pdev, 0);
		brd->membase_end = pci_resource_end(pdev, 0);
	}

	if (!brd->membase) {
		APR(("card has no PCI IO resources, failing board.\n"));
		return -ENODEV;
	}

	if (brd->membase & 1)
		brd->membase &= ~3;
	else
		brd->membase &= ~15;

	/*
	 * On the PCI boards, there is no IO space allocated
	 * The I/O registers will be in the first 3 bytes of the
	 * upper 2MB of the 4MB memory space.  The board memory
	 * will be mapped into the low 2MB of the 4MB memory space
	 */
	brd->port = brd->membase + PCI_IO_OFFSET;
	brd->port_end = brd->port + PCI_IO_SIZE;


	/*
	 * Special initialization for non-PLX boards
	 */
	if (brd->device != PCI_DEVICE_XRJ_DID && brd->device != PCI_DEVICE_EPCJ_DID) {
		unsigned short cmd;

		pci_write_config_byte(pdev, 0x40, 0);
		pci_write_config_byte(pdev, 0x46, 0);

		/* Limit burst length to 2 doubleword transactions */ 
		pci_write_config_byte(pdev, 0x42, 1);

		/*
		 * Enable IO and mem if not already done.
		 * This was needed for support on Itanium.
		 */
		pci_read_config_word(pdev, PCI_COMMAND, &cmd);
		cmd |= (PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
		pci_write_config_word(pdev, PCI_COMMAND, cmd);
	}

	/* init our poll helper tasklet */
	tasklet_init(&brd->helper_tasklet, dgap_poll_tasklet, (unsigned long) brd);

	 /* Log the information about the board */
	dgap_mbuf(brd, DRVSTR": board %d: %s (rev %d), irq %d\n",
		dgap_NumBoards, brd->name, brd->rev, brd->irq);

	DPR_INIT(("dgap_scan(%d) - printing out the msgbuf\n", i));
	DGAP_LOCK(dgap_global_lock, flags);
	brd->msgbuf = NULL;
	printk(brd->msgbuf_head);
	kfree(brd->msgbuf_head);
	brd->msgbuf_head = NULL;
	DGAP_UNLOCK(dgap_global_lock, flags);

	i = dgap_do_remap(brd);
	if (i)
		brd->state = BOARD_FAILED;
	else
		brd->state = NEED_RESET;

        return(0);
}


int dgap_finalize_board_init(struct board_t *brd) {

        int rc;

        DPR_INIT(("dgap_finalize_board_init() - start\n"));

	if (!brd || brd->magic != DGAP_BOARD_MAGIC)
                return(-ENODEV);

        DPR_INIT(("dgap_finalize_board_init() - start #2\n"));

	brd->use_interrupts = dgap_config_get_useintr(brd);

	/*
	 * Set up our interrupt handler if we are set to do interrupts.
	 */
	if (brd->use_interrupts && brd->irq) {

		rc = request_irq(brd->irq, dgap_intr, IRQF_SHARED, "DGAP", brd);

		if (rc) {
			dgap_mbuf(brd, DRVSTR": Failed to hook IRQ %d. Board will work in poll mode.\n",
                                  brd->irq);
			brd->intr_used = 0;
		}
		else
			brd->intr_used = 1;
	} else {
		brd->intr_used = 0;
	}

	return(0);
}


/*
 * Remap PCI memory.
 */
static int dgap_do_remap(struct board_t *brd)
{
	if (!brd || brd->magic != DGAP_BOARD_MAGIC)
		return -ENXIO;

	if (!request_mem_region(brd->membase, 0x200000, "dgap")) {
		APR(("dgap: mem_region %lx already in use.\n", brd->membase));
		return -ENOMEM;
        }

	if (!request_mem_region(brd->membase + PCI_IO_OFFSET, 0x200000, "dgap")) {
		APR(("dgap: mem_region IO %lx already in use.\n",
			brd->membase + PCI_IO_OFFSET));
		release_mem_region(brd->membase, 0x200000);
		return -ENOMEM;
        }

	brd->re_map_membase = ioremap(brd->membase, 0x200000);
	if (!brd->re_map_membase) {
		APR(("dgap: ioremap mem %lx cannot be mapped.\n", brd->membase));
		release_mem_region(brd->membase, 0x200000);
		release_mem_region(brd->membase + PCI_IO_OFFSET, 0x200000);
		return -ENOMEM;
	}

	brd->re_map_port = ioremap((brd->membase + PCI_IO_OFFSET), 0x200000);
	if (!brd->re_map_port) {
		release_mem_region(brd->membase, 0x200000);
		release_mem_region(brd->membase + PCI_IO_OFFSET, 0x200000);
		iounmap(brd->re_map_membase);
		APR(("dgap: ioremap IO mem %lx cannot be mapped.\n",
			brd->membase + PCI_IO_OFFSET));
		return -ENOMEM;
	}

	DPR_INIT(("remapped io: 0x%p  remapped mem: 0x%p\n",
		brd->re_map_port, brd->re_map_membase));
	return 0;
}


/*****************************************************************************
*
* Function:
*                                       
*    dgap_poll_handler
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

static void dgap_poll_handler(ulong dummy)
{
	int i;
        struct board_t *brd;
        unsigned long lock_flags;
        unsigned long lock_flags2;
	ulong new_time;

	dgap_poll_counter++;


	/*
	 * If driver needs the config file still,
	 * keep trying to wake up the downloader to
	 * send us the file.
	 */
        if (dgap_driver_state == DRIVER_NEED_CONFIG_LOAD) {
		/*
		 * Signal downloader, its got some work to do.
		 */
		DGAP_LOCK(dgap_dl_lock, lock_flags2);
		if (dgap_dl_action != 1) {
			dgap_dl_action = 1;
			wake_up_interruptible(&dgap_dl_wait);
		}
		DGAP_UNLOCK(dgap_dl_lock, lock_flags2);
		goto schedule_poller;
        }
	/*
	 * Do not start the board state machine until
	 * driver tells us its up and running, and has
	 * everything it needs.
	 */
	else if (dgap_driver_state != DRIVER_READY) {
		goto schedule_poller;
	}

	/*
	 * If we have just 1 board, or the system is not SMP,
	 * then use the typical old style poller.
	 * Otherwise, use our new tasklet based poller, which should
	 * speed things up for multiple boards.
	 */
	if ( (dgap_NumBoards == 1) || (num_online_cpus() <= 1) ) {
		for (i = 0; i < dgap_NumBoards; i++) {

			brd = dgap_Board[i];

			if (brd->state == BOARD_FAILED) {
				continue;
			}
			if (!brd->intr_running) {
				/* Call the real board poller directly */
				dgap_poll_tasklet((unsigned long) brd);
			}
		}
	}
	else {
		/* Go thru each board, kicking off a tasklet for each if needed */
		for (i = 0; i < dgap_NumBoards; i++) {
			brd = dgap_Board[i];

			/*
			 * Attempt to grab the board lock.
			 *
			 * If we can't get it, no big deal, the next poll will get it.
			 * Basically, I just really don't want to spin in here, because I want
			 * to kick off my tasklets as fast as I can, and then get out the poller.
			 */
			if (!spin_trylock(&brd->bd_lock)) {
				continue;
			}

			/* If board is in a failed state, don't bother scheduling a tasklet */
			if (brd->state == BOARD_FAILED) {
				spin_unlock(&brd->bd_lock);
				continue;
			}

			/* Schedule a poll helper task */
			if (!brd->intr_running) {
				tasklet_schedule(&brd->helper_tasklet);
			}

			/*
			 * Can't do DGAP_UNLOCK here, as we don't have
			 * lock_flags because we did a trylock above.
			 */
			spin_unlock(&brd->bd_lock);
		}
	}

schedule_poller:

	/*
	 * Schedule ourself back at the nominal wakeup interval.
	 */
	DGAP_LOCK(dgap_poll_lock, lock_flags );
	dgap_poll_time +=  dgap_jiffies_from_ms(dgap_poll_tick);

	new_time = dgap_poll_time - jiffies;

	if ((ulong) new_time >= 2 * dgap_poll_tick) {
		dgap_poll_time = jiffies +  dgap_jiffies_from_ms(dgap_poll_tick);
	}

	dgap_poll_timer.function = dgap_poll_handler;
	dgap_poll_timer.data = 0;
	dgap_poll_timer.expires = dgap_poll_time;
	DGAP_UNLOCK(dgap_poll_lock, lock_flags );

	if (!dgap_poll_stop)
		add_timer(&dgap_poll_timer);
}




/*
 * dgap_intr()
 *
 * Driver interrupt handler.
 */
static irqreturn_t dgap_intr(int irq, void *voidbrd)
{
	struct board_t *brd = (struct board_t *) voidbrd;

	if (!brd) {
		APR(("Received interrupt (%d) with null board associated\n", irq));
		return IRQ_NONE;
	}

	/*
	 * Check to make sure its for us.
	 */
	if (brd->magic != DGAP_BOARD_MAGIC) {
		APR(("Received interrupt (%d) with a board pointer that wasn't ours!\n", irq));
		return IRQ_NONE;
	}

	brd->intr_count++;

	/*
	 * Schedule tasklet to run at a better time.
	 */
	tasklet_schedule(&brd->helper_tasklet);
	return IRQ_HANDLED;
}


/*
 * dgap_init_globals()
 *
 * This is where we initialize the globals from the static insmod
 * configuration variables.  These are declared near the head of
 * this file.
 */
static void dgap_init_globals(void)
{
	int i = 0;

	dgap_rawreadok		= rawreadok;
        dgap_trcbuf_size	= trcbuf_size;
	dgap_debug		= debug;

	for (i = 0; i < MAXBOARDS; i++) {
		dgap_Board[i] = NULL;
	}

	init_timer( &dgap_poll_timer ); 

	init_waitqueue_head(&dgap_dl_wait);
	dgap_dl_action = 0;
}


/************************************************************************
 *
 * Utility functions
 *
 ************************************************************************/


/*
 * dgap_driver_kzmalloc()
 *
 * Malloc and clear memory,
 */
void *dgap_driver_kzmalloc(size_t size, int priority)
{
 	void *p = kmalloc(size, priority);
	if(p)
		memset(p, 0, size);
	return(p);
}


/*
 * dgap_mbuf()
 *
 * Used to print to the message buffer during board init.
 */
static void dgap_mbuf(struct board_t *brd, const char *fmt, ...) {
	va_list		ap;
	char		buf[1024];
	int		i;
	unsigned long	flags;

	DGAP_LOCK(dgap_global_lock, flags);

	/* Format buf using fmt and arguments contained in ap. */
	va_start(ap, fmt);
	i = vsprintf(buf, fmt,  ap);
	va_end(ap);

	DPR((buf));

	if (!brd || !brd->msgbuf) {
		printk(buf);
		DGAP_UNLOCK(dgap_global_lock, flags);
		return;
	}

	memcpy(brd->msgbuf, buf, strlen(buf));
	brd->msgbuf += strlen(buf);
	*brd->msgbuf = 0;

	DGAP_UNLOCK(dgap_global_lock, flags);
}


/*
 * dgap_ms_sleep()
 *
 * Put the driver to sleep for x ms's
 *
 * Returns 0 if timed out, !0 (showing signal) if interrupted by a signal.
 */
int dgap_ms_sleep(ulong ms)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout((ms * HZ) / 1000);
	return (signal_pending(current));
}



/*
 *      dgap_ioctl_name() : Returns a text version of each ioctl value.
 */
char *dgap_ioctl_name(int cmd)
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
