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
#include <linux/delay.h>	/* For udelay */
#include <linux/slab.h>
#include <asm/uaccess.h>	/* For copy_from_user/copy_to_user */
#include <linux/sched.h>

#include <linux/interrupt.h>	/* For tasklet and interrupt structs/defines */
#include <linux/ctype.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <asm/io.h>		/* For read[bwl]/write[bwl] */

#include <linux/string.h>
#include <linux/device.h>
#include <linux/kdev_t.h>

#include "dgap_driver.h"
#include "dgap_pci.h"
#include "dgap_fep5.h"
#include "dgap_tty.h"
#include "dgap_conf.h"
#include "dgap_parse.h"
#include "dgap_trace.h"
#include "dgap_sysfs.h"
#include "dgap_types.h"

#define init_MUTEX(sem)         sema_init(sem, 1)
#define DECLARE_MUTEX(name)     \
        struct semaphore name = __SEMAPHORE_INITIALIZER(name, 1)

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

/* Our function prototypes */
static int dgap_tty_open(struct tty_struct *tty, struct file *file);
static void dgap_tty_close(struct tty_struct *tty, struct file *file);
static int dgap_block_til_ready(struct tty_struct *tty, struct file *file, struct channel_t *ch);
static int dgap_tty_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg);
static int dgap_tty_digigeta(struct tty_struct *tty, struct digi_t __user *retinfo);
static int dgap_tty_digiseta(struct tty_struct *tty, struct digi_t __user *new_info);
static int dgap_tty_digigetedelay(struct tty_struct *tty, int __user *retinfo);
static int dgap_tty_digisetedelay(struct tty_struct *tty, int __user *new_info);
static int dgap_tty_write_room(struct tty_struct* tty);
static int dgap_tty_chars_in_buffer(struct tty_struct* tty);
static void dgap_tty_start(struct tty_struct *tty);
static void dgap_tty_stop(struct tty_struct *tty);
static void dgap_tty_throttle(struct tty_struct *tty);
static void dgap_tty_unthrottle(struct tty_struct *tty);
static void dgap_tty_flush_chars(struct tty_struct *tty);
static void dgap_tty_flush_buffer(struct tty_struct *tty);
static void dgap_tty_hangup(struct tty_struct *tty);
static int dgap_wait_for_drain(struct tty_struct *tty);
static int dgap_set_modem_info(struct tty_struct *tty, unsigned int command, unsigned int __user *value);
static int dgap_get_modem_info(struct channel_t *ch, unsigned int __user *value);
static int dgap_tty_digisetcustombaud(struct tty_struct *tty, int __user *new_info);
static int dgap_tty_digigetcustombaud(struct tty_struct *tty, int __user *retinfo);
static int dgap_tty_tiocmget(struct tty_struct *tty);
static int dgap_tty_tiocmset(struct tty_struct *tty, unsigned int set, unsigned int clear);
static int dgap_tty_send_break(struct tty_struct *tty, int msec);
static void dgap_tty_wait_until_sent(struct tty_struct *tty, int timeout);
static int dgap_tty_write(struct tty_struct *tty, const unsigned char *buf, int count);
static void dgap_tty_set_termios(struct tty_struct *tty, struct ktermios *old_termios);
static int dgap_tty_put_char(struct tty_struct *tty, unsigned char c);
static void dgap_tty_send_xchar(struct tty_struct *tty, char ch);

/*
 * Our function prototypes from dgap_fep5
 */
static void dgap_cmdw_ext(struct channel_t *ch, u16 cmd, u16 word, uint ncmds);
static int dgap_event(struct board_t *bd);

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

static struct board_t	*dgap_BoardsByMajor[256];
static uchar		*dgap_TmpWriteBuf = NULL;
static DECLARE_MUTEX(dgap_TmpWriteSem);
static uint dgap_count = 500;

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

/*
 * Default transparent print information.
 */
static struct digi_t dgap_digi_init = {
	.digi_flags =	DIGI_COOK,	/* Flags			*/
	.digi_maxcps =	100,		/* Max CPS			*/
	.digi_maxchar =	50,		/* Max chars in print queue	*/
	.digi_bufsize =	100,		/* Printer buffer size		*/
	.digi_onlen =	4,		/* size of printer on string	*/
	.digi_offlen =	4,		/* size of printer off string	*/
	.digi_onstr =	"\033[5i",	/* ANSI printer on string ]	*/
	.digi_offstr =	"\033[4i",	/* ANSI printer off string ]	*/
	.digi_term =	"ansi"		/* default terminal type	*/
};


/*
 * Define a local default termios struct. All ports will be created
 * with this termios initially.
 *
 * This defines a raw port at 9600 baud, 8 data bits, no parity,
 * 1 stop bit.
 */

static struct ktermios DgapDefaultTermios =
{
	.c_iflag =	(DEFAULT_IFLAGS),	/* iflags */
	.c_oflag =	(DEFAULT_OFLAGS),	/* oflags */
	.c_cflag =	(DEFAULT_CFLAGS),	/* cflags */
	.c_lflag =	(DEFAULT_LFLAGS),	/* lflags */
	.c_cc =		INIT_C_CC,
	.c_line = 	0,
};

static const struct tty_operations dgap_tty_ops = {
	.open = dgap_tty_open,
	.close = dgap_tty_close,
	.write = dgap_tty_write,
	.write_room = dgap_tty_write_room,
	.flush_buffer = dgap_tty_flush_buffer,
	.chars_in_buffer = dgap_tty_chars_in_buffer,
	.flush_chars = dgap_tty_flush_chars,
	.ioctl = dgap_tty_ioctl,
	.set_termios = dgap_tty_set_termios,
	.stop = dgap_tty_stop,
	.start = dgap_tty_start,
	.throttle = dgap_tty_throttle,
	.unthrottle = dgap_tty_unthrottle,
	.hangup = dgap_tty_hangup,
	.put_char = dgap_tty_put_char,
	.tiocmget = dgap_tty_tiocmget,
	.tiocmset = dgap_tty_tiocmset,
	.break_ctl = dgap_tty_send_break,
	.wait_until_sent = dgap_tty_wait_until_sent,
	.send_xchar = dgap_tty_send_xchar
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
                printk("%s", brd->msgbuf_head);
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

	kfree(brd->flipbuf);
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
	(struct board_t *) kzalloc(sizeof(struct board_t), GFP_KERNEL);
	if (!brd) {
		APR(("memory allocation for board structure failed\n"));
		return(-ENOMEM);
	}

	/* make a temporary message buffer for the boot messages */
	brd->msgbuf = brd->msgbuf_head =
		(char *) kzalloc(sizeof(char) * 8192, GFP_KERNEL);
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
	printk("%s", brd->msgbuf_head);
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
 * dgap_mbuf()
 *
 * Used to print to the message buffer during board init.
 */
static void dgap_mbuf(struct board_t *brd, const char *fmt, ...) {
	va_list		ap;
	char		buf[1024];
	int		i;
	unsigned long	flags;
	size_t		length;

	DGAP_LOCK(dgap_global_lock, flags);

	/* Format buf using fmt and arguments contained in ap. */
	va_start(ap, fmt);
	i = vsnprintf(buf, sizeof(buf), fmt,  ap);
	va_end(ap);

	DPR((buf));

	if (!brd || !brd->msgbuf) {
		printk("%s", buf);
		DGAP_UNLOCK(dgap_global_lock, flags);
		return;
	}

	length = strlen(buf) + 1;
	if (brd->msgbuf - brd->msgbuf_head < length)
		length = brd->msgbuf - brd->msgbuf_head;
	memcpy(brd->msgbuf, buf, length);
	brd->msgbuf += length;

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

/************************************************************************
 *
 * TTY Initialization/Cleanup Functions
 *
 ************************************************************************/

/*
 * dgap_tty_preinit()
 *
 * Initialize any global tty related data before we download any boards.
 */
int dgap_tty_preinit(void)
{
	unsigned long flags;

	DGAP_LOCK(dgap_global_lock, flags);

	/*
	 * Allocate a buffer for doing the copy from user space to
	 * kernel space in dgap_input().  We only use one buffer and
	 * control access to it with a semaphore.  If we are paging, we
	 * are already in trouble so one buffer won't hurt much anyway.
	 */
	dgap_TmpWriteBuf = kmalloc(WRITEBUFLEN, GFP_ATOMIC);

	if (!dgap_TmpWriteBuf) {
		DGAP_UNLOCK(dgap_global_lock, flags);
		DPR_INIT(("unable to allocate tmp write buf"));
		return (-ENOMEM);
	}

        DGAP_UNLOCK(dgap_global_lock, flags);
        return(0);
}


/*
 * dgap_tty_register()
 *
 * Init the tty subsystem for this board.
 */
int dgap_tty_register(struct board_t *brd)
{
	int rc = 0;

	DPR_INIT(("tty_register start"));

	brd->SerialDriver = alloc_tty_driver(MAXPORTS);

	snprintf(brd->SerialName, MAXTTYNAMELEN, "tty_dgap_%d_", brd->boardnum);
	brd->SerialDriver->name = brd->SerialName;
	brd->SerialDriver->name_base = 0;
	brd->SerialDriver->major = 0;
	brd->SerialDriver->minor_start = 0;
	brd->SerialDriver->type = TTY_DRIVER_TYPE_SERIAL;
	brd->SerialDriver->subtype = SERIAL_TYPE_NORMAL;
	brd->SerialDriver->init_termios = DgapDefaultTermios;
	brd->SerialDriver->driver_name = DRVSTR;
	brd->SerialDriver->flags = (TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV | TTY_DRIVER_HARDWARE_BREAK);

	/* The kernel wants space to store pointers to tty_structs */
	brd->SerialDriver->ttys = kzalloc(MAXPORTS * sizeof(struct tty_struct *), GFP_KERNEL);
	if (!brd->SerialDriver->ttys)
		return(-ENOMEM);

	/*
	 * Entry points for driver.  Called by the kernel from
	 * tty_io.c and n_tty.c.
	 */
	tty_set_operations(brd->SerialDriver, &dgap_tty_ops);

	/*
	 * If we're doing transparent print, we have to do all of the above
	 * again, separately so we don't get the LD confused about what major
	 * we are when we get into the dgap_tty_open() routine.
	 */
	brd->PrintDriver = alloc_tty_driver(MAXPORTS);

	snprintf(brd->PrintName, MAXTTYNAMELEN, "pr_dgap_%d_", brd->boardnum);
	brd->PrintDriver->name = brd->PrintName;
	brd->PrintDriver->name_base = 0;
	brd->PrintDriver->major = 0;
	brd->PrintDriver->minor_start = 0;
	brd->PrintDriver->type = TTY_DRIVER_TYPE_SERIAL;
	brd->PrintDriver->subtype = SERIAL_TYPE_NORMAL;
	brd->PrintDriver->init_termios = DgapDefaultTermios;
	brd->PrintDriver->driver_name = DRVSTR;
	brd->PrintDriver->flags = (TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV | TTY_DRIVER_HARDWARE_BREAK);

	/* The kernel wants space to store pointers to tty_structs */
	brd->PrintDriver->ttys = kzalloc(MAXPORTS * sizeof(struct tty_struct *), GFP_KERNEL);
	if (!brd->PrintDriver->ttys)
		return(-ENOMEM);

	/*
	 * Entry points for driver.  Called by the kernel from
	 * tty_io.c and n_tty.c.
	 */
	tty_set_operations(brd->PrintDriver, &dgap_tty_ops);

	if (!brd->dgap_Major_Serial_Registered) {
		/* Register tty devices */
		rc = tty_register_driver(brd->SerialDriver);
		if (rc < 0) {
			APR(("Can't register tty device (%d)\n", rc));
			return(rc);
		}
		brd->dgap_Major_Serial_Registered = TRUE;
		dgap_BoardsByMajor[brd->SerialDriver->major] = brd;
		brd->dgap_Serial_Major = brd->SerialDriver->major;
	}

	if (!brd->dgap_Major_TransparentPrint_Registered) {
		/* Register Transparent Print devices */
 		rc = tty_register_driver(brd->PrintDriver);
		if (rc < 0) {
			APR(("Can't register Transparent Print device (%d)\n", rc));
			return(rc);
		}
		brd->dgap_Major_TransparentPrint_Registered = TRUE;
		dgap_BoardsByMajor[brd->PrintDriver->major] = brd;
		brd->dgap_TransparentPrint_Major = brd->PrintDriver->major;
	}

	DPR_INIT(("DGAP REGISTER TTY: MAJORS: %d %d\n", brd->SerialDriver->major,
		brd->PrintDriver->major));

	return (rc);
}


/*
 * dgap_tty_init()
 *
 * Init the tty subsystem.  Called once per board after board has been
 * downloaded and init'ed.
 */
int dgap_tty_init(struct board_t *brd)
{
	int i;
	int tlw;
	uint true_count = 0;
	uchar *vaddr;
	uchar modem = 0;
	struct channel_t *ch;
	struct bs_t *bs;
	struct cm_t *cm;

	if (!brd)
		return (-ENXIO);

	DPR_INIT(("dgap_tty_init start\n"));

	/*
	 * Initialize board structure elements.
	 */

	vaddr = brd->re_map_membase;
	true_count = readw((vaddr + NCHAN));

	brd->nasync = dgap_config_get_number_of_ports(brd);

	if (!brd->nasync) {
		brd->nasync = brd->maxports;
	}

	if (brd->nasync > brd->maxports) {
		brd->nasync = brd->maxports;
	}

	if (true_count != brd->nasync) {
		if ((brd->type == PPCM) && (true_count == 64)) {
			APR(("***WARNING**** %s configured for %d ports, has %d ports.\nPlease make SURE the EBI cable running from the card\nto each EM module is plugged into EBI IN!\n",
				brd->name, brd->nasync, true_count));
		}
		else if ((brd->type == PPCM) && (true_count == 0)) {
			APR(("***WARNING**** %s configured for %d ports, has %d ports.\nPlease make SURE the EBI cable running from the card\nto each EM module is plugged into EBI IN!\n",
				brd->name, brd->nasync, true_count));
		}
		else {
			APR(("***WARNING**** %s configured for %d ports, has %d ports.\n",
				brd->name, brd->nasync, true_count));
		}

		brd->nasync = true_count;

		/* If no ports, don't bother going any further */
		if (!brd->nasync) {
			brd->state = BOARD_FAILED;
			brd->dpastatus = BD_NOFEP;
			return(-ENXIO);
		}
	}

	/*
	 * Allocate channel memory that might not have been allocated
	 * when the driver was first loaded.
	 */
	for (i = 0; i < brd->nasync; i++) {
		if (!brd->channels[i]) {
			brd->channels[i] = kzalloc(sizeof(struct channel_t), GFP_ATOMIC);
			if (!brd->channels[i]) {
				DPR_CORE(("%s:%d Unable to allocate memory for channel struct\n",
				    __FILE__, __LINE__));
			}
		}
	}

	ch = brd->channels[0];
	vaddr = brd->re_map_membase;

	bs = (struct bs_t *) ((ulong) vaddr + CHANBUF);
	cm = (struct cm_t *) ((ulong) vaddr + CMDBUF);

	brd->bd_bs = bs;

	/* Set up channel variables */
	for (i = 0; i < brd->nasync; i++, ch = brd->channels[i], bs++) {

		if (!brd->channels[i])
			continue;

		DGAP_SPINLOCK_INIT(ch->ch_lock);

		/* Store all our magic numbers */
		ch->magic = DGAP_CHANNEL_MAGIC;
		ch->ch_tun.magic = DGAP_UNIT_MAGIC;
		ch->ch_tun.un_type = DGAP_SERIAL;
		ch->ch_tun.un_ch = ch;
		ch->ch_tun.un_dev = i;

		ch->ch_pun.magic = DGAP_UNIT_MAGIC;
		ch->ch_pun.un_type = DGAP_PRINT;
		ch->ch_pun.un_ch = ch;
		ch->ch_pun.un_dev = i;

		ch->ch_vaddr = vaddr;
		ch->ch_bs = bs;
		ch->ch_cm = cm;
		ch->ch_bd = brd;
		ch->ch_portnum = i;
		ch->ch_digi = dgap_digi_init;

		/*
		 * Set up digi dsr and dcd bits based on altpin flag.
		 */
		if (dgap_config_get_altpin(brd)) {
			ch->ch_dsr	= DM_CD;
			ch->ch_cd	= DM_DSR;
			ch->ch_digi.digi_flags |= DIGI_ALTPIN;
		}
		else {
			ch->ch_cd	= DM_CD;
			ch->ch_dsr	= DM_DSR;
		}

		ch->ch_taddr = vaddr + ((ch->ch_bs->tx_seg) << 4);
		ch->ch_raddr = vaddr + ((ch->ch_bs->rx_seg) << 4);
		ch->ch_tx_win = 0;
		ch->ch_rx_win = 0;
		ch->ch_tsize = readw(&(ch->ch_bs->tx_max)) + 1;
		ch->ch_rsize = readw(&(ch->ch_bs->rx_max)) + 1;
		ch->ch_tstart = 0;
		ch->ch_rstart = 0;

		/* .25 second delay */
		ch->ch_close_delay = 250;

		/*
		 * Set queue water marks, interrupt mask,
		 * and general tty parameters.
		 */
		ch->ch_tlw = tlw = ch->ch_tsize >= 2000 ? ((ch->ch_tsize * 5) / 8) : ch->ch_tsize / 2;

		dgap_cmdw(ch, STLOW, tlw, 0);

		dgap_cmdw(ch, SRLOW, ch->ch_rsize / 2, 0);

		dgap_cmdw(ch, SRHIGH, 7 * ch->ch_rsize / 8, 0);

		ch->ch_mistat = readb(&(ch->ch_bs->m_stat));

		init_waitqueue_head(&ch->ch_flags_wait);
		init_waitqueue_head(&ch->ch_tun.un_flags_wait);
		init_waitqueue_head(&ch->ch_pun.un_flags_wait);
		init_waitqueue_head(&ch->ch_sniff_wait);

		/* Turn on all modem interrupts for now */
		modem = (DM_CD | DM_DSR | DM_CTS | DM_RI);
		writeb(modem, &(ch->ch_bs->m_int));

		/*
		 * Set edelay to 0 if interrupts are turned on,
		 * otherwise set edelay to the usual 100.
		 */
		if (brd->intr_used)
			writew(0, &(ch->ch_bs->edelay));
		else
			writew(100, &(ch->ch_bs->edelay));

		writeb(1, &(ch->ch_bs->idata));
	}


	DPR_INIT(("dgap_tty_init finish\n"));

	return (0);
}


/*
 * dgap_tty_post_uninit()
 *
 * UnInitialize any global tty related data.
 */
void dgap_tty_post_uninit(void)
{
	kfree(dgap_TmpWriteBuf);
	dgap_TmpWriteBuf = NULL;
}


/*
 * dgap_tty_uninit()
 *
 * Uninitialize the TTY portion of this driver.  Free all memory and
 * resources.
 */
void dgap_tty_uninit(struct board_t *brd)
{
	int i = 0;

	if (brd->dgap_Major_Serial_Registered) {
		dgap_BoardsByMajor[brd->SerialDriver->major] = NULL;
		brd->dgap_Serial_Major = 0;
		for (i = 0; i < brd->nasync; i++) {
			dgap_remove_tty_sysfs(brd->channels[i]->ch_tun.un_sysfs);
			tty_unregister_device(brd->SerialDriver, i);
		}
		tty_unregister_driver(brd->SerialDriver);
		kfree(brd->SerialDriver->ttys);
		brd->SerialDriver->ttys = NULL;
		put_tty_driver(brd->SerialDriver);
		brd->dgap_Major_Serial_Registered = FALSE;
	}

	if (brd->dgap_Major_TransparentPrint_Registered) {
		dgap_BoardsByMajor[brd->PrintDriver->major] = NULL;
		brd->dgap_TransparentPrint_Major = 0;
		for (i = 0; i < brd->nasync; i++) {
			dgap_remove_tty_sysfs(brd->channels[i]->ch_pun.un_sysfs);
			tty_unregister_device(brd->PrintDriver, i);
		}
		tty_unregister_driver(brd->PrintDriver);
		kfree(brd->PrintDriver->ttys);
		brd->PrintDriver->ttys = NULL;
		put_tty_driver(brd->PrintDriver);
		brd->dgap_Major_TransparentPrint_Registered = FALSE;
	}
}


#define TMPBUFLEN (1024)

/*
 * dgap_sniff - Dump data out to the "sniff" buffer if the
 * proc sniff file is opened...
 */
static void dgap_sniff_nowait_nolock(struct channel_t *ch, uchar *text, uchar *buf, int len)
{
	struct timeval tv;
	int n;
	int r;
	int nbuf;
	int i;
	int tmpbuflen;
	char tmpbuf[TMPBUFLEN];
	char *p = tmpbuf;
	int too_much_data;

	/* Leave if sniff not open */
	if (!(ch->ch_sniff_flags & SNIFF_OPEN))
		return;

	do_gettimeofday(&tv);

	/* Create our header for data dump */
	p += sprintf(p, "<%ld %ld><%s><", tv.tv_sec, tv.tv_usec, text);
	tmpbuflen = p - tmpbuf;

	do {
		too_much_data = 0;

		for (i = 0; i < len && tmpbuflen < (TMPBUFLEN - 4); i++) {
			p += sprintf(p, "%02x ", *buf);
			buf++;
			tmpbuflen = p - tmpbuf;
		}

		if (tmpbuflen < (TMPBUFLEN - 4)) {
			if (i > 0)
				p += sprintf(p - 1, "%s\n", ">");
			else
				p += sprintf(p, "%s\n", ">");
		} else {
			too_much_data = 1;
			len -= i;
		}

		nbuf = strlen(tmpbuf);
		p = tmpbuf;

		/*
		 *  Loop while data remains.
		 */
		while (nbuf > 0 && ch->ch_sniff_buf) {
			/*
			 *  Determine the amount of available space left in the
			 *  buffer.  If there's none, wait until some appears.
			 */
			n = (ch->ch_sniff_out - ch->ch_sniff_in - 1) & SNIFF_MASK;

			/*
			 * If there is no space left to write to in our sniff buffer,
			 * we have no choice but to drop the data.
			 * We *cannot* sleep here waiting for space, because this
			 * function was probably called by the interrupt/timer routines!
			 */
			if (n == 0) {
				return;
			}

			/*
			 * Copy as much data as will fit.
			 */

			if (n > nbuf)
				n = nbuf;

			r = SNIFF_MAX - ch->ch_sniff_in;

			if (r <= n) {
				memcpy(ch->ch_sniff_buf + ch->ch_sniff_in, p, r);

				n -= r;
				ch->ch_sniff_in = 0;
				p += r;
				nbuf -= r;
			}

			memcpy(ch->ch_sniff_buf + ch->ch_sniff_in, p, n);

			ch->ch_sniff_in += n;
			p += n;
			nbuf -= n;

			/*
			 *  Wakeup any thread waiting for data
			 */
			if (ch->ch_sniff_flags & SNIFF_WAIT_DATA) {
				ch->ch_sniff_flags &= ~SNIFF_WAIT_DATA;
				wake_up_interruptible(&ch->ch_sniff_wait);
			}
		}

		/*
		 * If the user sent us too much data to push into our tmpbuf,
		 * we need to keep looping around on all the data.
		 */
		if (too_much_data) {
			p = tmpbuf;
			tmpbuflen = 0;
		}

	} while (too_much_data);
}


/*=======================================================================
 *
 *      dgap_input - Process received data.
 *
 *              ch      - Pointer to channel structure.
 *
 *=======================================================================*/

void dgap_input(struct channel_t *ch)
{
	struct board_t *bd;
	struct bs_t	*bs;
	struct tty_struct *tp;
	struct tty_ldisc *ld;
	uint	rmask;
	uint	head;
	uint	tail;
	int	data_len;
	ulong	lock_flags;
	ulong   lock_flags2;
	int flip_len;
	int len = 0;
	int n = 0;
	uchar *buf;
	uchar tmpchar;
	int s = 0;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	tp = ch->ch_tun.un_tty;

	bs  = ch->ch_bs;
	if (!bs) {
		return;
	}

	bd = ch->ch_bd;
	if(!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_READ(("dgap_input start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	/*
	 *      Figure the number of characters in the buffer.
	 *      Exit immediately if none.
	 */

	rmask = ch->ch_rsize - 1;

	head = readw(&(bs->rx_head));
	head &= rmask;
	tail = readw(&(bs->rx_tail));
	tail &= rmask;

	data_len = (head - tail) & rmask;

	if (data_len == 0) {
		writeb(1, &(bs->idata));
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		DPR_READ(("No data on port %d\n", ch->ch_portnum));
		return;
	}

	/*
	 * If the device is not open, or CREAD is off, flush
	 * input data and return immediately.
	 */
	if ((bd->state != BOARD_READY) || !tp  || (tp->magic != TTY_MAGIC) ||
            !(ch->ch_tun.un_flags & UN_ISOPEN) || !(tp->termios.c_cflag & CREAD) ||
	    (ch->ch_tun.un_flags & UN_CLOSING)) {

		DPR_READ(("input. dropping %d bytes on port %d...\n", data_len, ch->ch_portnum));
		DPR_READ(("input. tp: %p tp->magic: %x MAGIC:%x ch flags: %x\n",
			tp, tp ? tp->magic : 0, TTY_MAGIC, ch->ch_tun.un_flags));
		writew(head, &(bs->rx_tail));
		writeb(1, &(bs->idata));
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return;
	}

	/*
	 * If we are throttled, simply don't read any data.
	 */
	if (ch->ch_flags & CH_RXBLOCK) {
		writeb(1, &(bs->idata));
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		DPR_READ(("Port %d throttled, not reading any data. head: %x tail: %x\n",
			ch->ch_portnum, head, tail));
		return;
	}

	/*
	 *      Ignore oruns.
	 */
	tmpchar = readb(&(bs->orun));
	if (tmpchar) {
		ch->ch_err_overrun++;
		writeb(0, &(bs->orun));
	}

	DPR_READ(("dgap_input start 2\n"));

	/* Decide how much data we can send into the tty layer */
	flip_len = TTY_FLIPBUF_SIZE;

	/* Chop down the length, if needed */
	len = min(data_len, flip_len);
	len = min(len, (N_TTY_BUF_SIZE - 1));

	ld = tty_ldisc_ref(tp);

#ifdef TTY_DONT_FLIP
	/*
	 * If the DONT_FLIP flag is on, don't flush our buffer, and act
	 * like the ld doesn't have any space to put the data right now.
	 */
	if (test_bit(TTY_DONT_FLIP, &tp->flags))
		len = 0;
#endif

	/*
	 * If we were unable to get a reference to the ld,
	 * don't flush our buffer, and act like the ld doesn't
	 * have any space to put the data right now.
	 */
	if (!ld) {
		len = 0;
	} else {
		/*
		 * If ld doesn't have a pointer to a receive_buf function,
		 * flush the data, then act like the ld doesn't have any
		 * space to put the data right now.
		 */
		if (!ld->ops->receive_buf) {
			writew(head, &(bs->rx_tail));
			len = 0;
		}
	}

	if (len <= 0) {
		writeb(1, &(bs->idata));
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		DPR_READ(("dgap_input 1 - finish\n"));
		if (ld)
			tty_ldisc_deref(ld);
		return;
	}

	buf = ch->ch_bd->flipbuf;
	n = len;

	/*
	 * n now contains the most amount of data we can copy,
	 * bounded either by our buffer size or the amount
	 * of data the card actually has pending...
	 */
	while (n) {

		s = ((head >= tail) ? head : ch->ch_rsize) - tail;
		s = min(s, n);

		if (s <= 0)
			break;

		memcpy_fromio(buf, (char *) ch->ch_raddr + tail, s);
		dgap_sniff_nowait_nolock(ch, "USER READ", buf, s);

		tail += s;
		buf += s;

		n -= s;
		/* Flip queue if needed */
		tail &= rmask;
	}

	writew(tail, &(bs->rx_tail));
	writeb(1, &(bs->idata));
	ch->ch_rxcount += len;

	/*
	 * If we are completely raw, we don't need to go through a lot
	 * of the tty layers that exist.
	 * In this case, we take the shortest and fastest route we
	 * can to relay the data to the user.
	 *
	 * On the other hand, if we are not raw, we need to go through
	 * the tty layer, which has its API more well defined.
	 */
	if (I_PARMRK(tp) || I_BRKINT(tp) || I_INPCK(tp)) {
		dgap_parity_scan(ch, ch->ch_bd->flipbuf, ch->ch_bd->flipflagbuf, &len);

		len = tty_buffer_request_room(tp->port, len);
		tty_insert_flip_string_flags(tp->port, ch->ch_bd->flipbuf,
			ch->ch_bd->flipflagbuf, len);
	}
	else {
		len = tty_buffer_request_room(tp->port, len);
		tty_insert_flip_string(tp->port, ch->ch_bd->flipbuf, len);
	}

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	/* Tell the tty layer its okay to "eat" the data now */
	tty_flip_buffer_push(tp->port);

	if (ld)
		tty_ldisc_deref(ld);

	DPR_READ(("dgap_input - finish\n"));
}


/************************************************************************
 * Determines when CARRIER changes state and takes appropriate
 * action.
 ************************************************************************/
void dgap_carrier(struct channel_t *ch)
{
	struct board_t *bd;

        int virt_carrier = 0;
        int phys_carrier = 0;

	DPR_CARR(("dgap_carrier called...\n"));

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;

	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	/* Make sure altpin is always set correctly */
	if (ch->ch_digi.digi_flags & DIGI_ALTPIN) {
		ch->ch_dsr      = DM_CD;
		ch->ch_cd       = DM_DSR;
	}
	else {
		ch->ch_dsr      = DM_DSR;
		ch->ch_cd       = DM_CD;
	}

	if (ch->ch_mistat & D_CD(ch)) {
		DPR_CARR(("mistat: %x  D_CD: %x\n", ch->ch_mistat, D_CD(ch)));
		phys_carrier = 1;
	}

	if (ch->ch_digi.digi_flags & DIGI_FORCEDCD) {
		virt_carrier = 1;
	}

	if (ch->ch_c_cflag & CLOCAL) {
		virt_carrier = 1;
	}


	DPR_CARR(("DCD: physical: %d virt: %d\n", phys_carrier, virt_carrier));

	/*
	 * Test for a VIRTUAL carrier transition to HIGH.
	 */
	if (((ch->ch_flags & CH_FCAR) == 0) && (virt_carrier == 1)) {

		/*
		 * When carrier rises, wake any threads waiting
		 * for carrier in the open routine.
		 */

		DPR_CARR(("carrier: virt DCD rose\n"));

		if (waitqueue_active(&(ch->ch_flags_wait)))
			wake_up_interruptible(&ch->ch_flags_wait);
	}

	/*
	 * Test for a PHYSICAL carrier transition to HIGH.
	 */
	if (((ch->ch_flags & CH_CD) == 0) && (phys_carrier == 1)) {

		/*
		 * When carrier rises, wake any threads waiting
		 * for carrier in the open routine.
		 */

		DPR_CARR(("carrier: physical DCD rose\n"));

		if (waitqueue_active(&(ch->ch_flags_wait)))
			wake_up_interruptible(&ch->ch_flags_wait);
	}

	/*
	 *  Test for a PHYSICAL transition to low, so long as we aren't
	 *  currently ignoring physical transitions (which is what "virtual
	 *  carrier" indicates).
	 *
	 *  The transition of the virtual carrier to low really doesn't
	 *  matter... it really only means "ignore carrier state", not
	 *  "make pretend that carrier is there".
	 */
	if ((virt_carrier == 0) && ((ch->ch_flags & CH_CD) != 0) &&
	    (phys_carrier == 0))
	{

		/*
		 *   When carrier drops:
		 *
		 *   Drop carrier on all open units.
		 *
		 *   Flush queues, waking up any task waiting in the
		 *   line discipline.
		 *
		 *   Send a hangup to the control terminal.
		 *
		 *   Enable all select calls.
		 */
		if (waitqueue_active(&(ch->ch_flags_wait)))
			wake_up_interruptible(&ch->ch_flags_wait);

		if (ch->ch_tun.un_open_count > 0) {
			DPR_CARR(("Sending tty hangup\n"));
			tty_hangup(ch->ch_tun.un_tty);
		}

		if (ch->ch_pun.un_open_count > 0) {
			DPR_CARR(("Sending pr hangup\n"));
			tty_hangup(ch->ch_pun.un_tty);
		}
	}

	/*
	 *  Make sure that our cached values reflect the current reality.
	 */
	if (virt_carrier == 1)
		ch->ch_flags |= CH_FCAR;
	else
		ch->ch_flags &= ~CH_FCAR;

	if (phys_carrier == 1)
		ch->ch_flags |= CH_CD;
	else
		ch->ch_flags &= ~CH_CD;
}


/************************************************************************
 *
 * TTY Entry points and helper functions
 *
 ************************************************************************/

/*
 * dgap_tty_open()
 *
 */
static int dgap_tty_open(struct tty_struct *tty, struct file *file)
{
	struct board_t	*brd;
	struct channel_t *ch;
	struct un_t	*un;
	struct bs_t	*bs;
	uint		major = 0;
	uint		minor = 0;
	int		rc = 0;
	ulong		lock_flags;
	ulong		lock_flags2;
	u16		head;

	rc = 0;

	major = MAJOR(tty_devnum(tty));
	minor = MINOR(tty_devnum(tty));

	if (major > 255) {
		return -ENXIO;
	}

	/* Get board pointer from our array of majors we have allocated */
	brd = dgap_BoardsByMajor[major];
	if (!brd) {
		return -ENXIO;
	}

	/*
	 * If board is not yet up to a state of READY, go to
	 * sleep waiting for it to happen or they cancel the open.
	 */
	rc = wait_event_interruptible(brd->state_wait,
		(brd->state & BOARD_READY));

	if (rc) {
		return rc;
	}

	DGAP_LOCK(brd->bd_lock, lock_flags);

	/* The wait above should guarantee this cannot happen */
	if (brd->state != BOARD_READY) {
		DGAP_UNLOCK(brd->bd_lock, lock_flags);
		return -ENXIO;
	}

	/* If opened device is greater than our number of ports, bail. */
	if (MINOR(tty_devnum(tty)) > brd->nasync) {
		DGAP_UNLOCK(brd->bd_lock, lock_flags);
		return -ENXIO;
	}

	ch = brd->channels[minor];
	if (!ch) {
		DGAP_UNLOCK(brd->bd_lock, lock_flags);
		return -ENXIO;
	}

	/* Grab channel lock */
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	/* Figure out our type */
	if (major == brd->dgap_Serial_Major) {
		un = &brd->channels[minor]->ch_tun;
		un->un_type = DGAP_SERIAL;
	}
	else if (major == brd->dgap_TransparentPrint_Major) {
		un = &brd->channels[minor]->ch_pun;
		un->un_type = DGAP_PRINT;
	}
	else {
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(brd->bd_lock, lock_flags);
		DPR_OPEN(("%d Unknown TYPE!\n", __LINE__));
		return -ENXIO;
	}

	/* Store our unit into driver_data, so we always have it available. */
	tty->driver_data = un;

	DPR_OPEN(("Open called. MAJOR: %d MINOR:%d unit: %p NAME: %s\n",
		MAJOR(tty_devnum(tty)), MINOR(tty_devnum(tty)), un, brd->name));

	/*
	 * Error if channel info pointer is NULL.
	 */
	bs = ch->ch_bs;
	if (!bs) {
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(brd->bd_lock, lock_flags);
		DPR_OPEN(("%d BS is 0!\n", __LINE__));
		return -ENXIO;
        }

	DPR_OPEN(("%d: tflag=%x  pflag=%x\n", __LINE__, ch->ch_tun.un_flags, ch->ch_pun.un_flags));

	/*
	 * Initialize tty's
	 */
	if (!(un->un_flags & UN_ISOPEN)) {
		/* Store important variables. */
		un->un_tty     = tty;

		/* Maybe do something here to the TTY struct as well? */
	}

	/*
	 * Initialize if neither terminal or printer is open.
	 */
	if (!((ch->ch_tun.un_flags | ch->ch_pun.un_flags) & UN_ISOPEN)) {

		DPR_OPEN(("dgap_open: initializing channel in open...\n"));

		ch->ch_mforce = 0;
		ch->ch_mval = 0;

		/*
		 * Flush input queue.
		 */
		head = readw(&(bs->rx_head));
		writew(head, &(bs->rx_tail));

		ch->ch_flags = 0;
		ch->pscan_state = 0;
		ch->pscan_savechar = 0;

		ch->ch_c_cflag   = tty->termios.c_cflag;
		ch->ch_c_iflag   = tty->termios.c_iflag;
		ch->ch_c_oflag   = tty->termios.c_oflag;
		ch->ch_c_lflag   = tty->termios.c_lflag;
		ch->ch_startc = tty->termios.c_cc[VSTART];
		ch->ch_stopc  = tty->termios.c_cc[VSTOP];

		/* TODO: flush our TTY struct here? */
	}

	dgap_carrier(ch);
	/*
	 * Run param in case we changed anything
	 */
	dgap_param(tty);

	/*
	 * follow protocol for opening port
	 */

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(brd->bd_lock, lock_flags);

	rc = dgap_block_til_ready(tty, file, ch);

	if (!un->un_tty) {
		return -ENODEV;
	}

	if (rc) {
		DPR_OPEN(("dgap_tty_open returning after dgap_block_til_ready "
			"with %d\n", rc));
	}

	/* No going back now, increment our unit and channel counters */
	DGAP_LOCK(ch->ch_lock, lock_flags);
	ch->ch_open_count++;
	un->un_open_count++;
	un->un_flags |= (UN_ISOPEN);
	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	DPR_OPEN(("dgap_tty_open finished\n"));
	return (rc);
}


/*
 * dgap_block_til_ready()
 *
 * Wait for DCD, if needed.
 */
static int dgap_block_til_ready(struct tty_struct *tty, struct file *file, struct channel_t *ch)
{
	int retval = 0;
	struct un_t *un = NULL;
	ulong   lock_flags;
	uint	old_flags = 0;
	int sleep_on_un_flags = 0;

	if (!tty || tty->magic != TTY_MAGIC || !file || !ch || ch->magic != DGAP_CHANNEL_MAGIC) {
		return (-ENXIO);
	}

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC) {
		return (-ENXIO);
	}

	DPR_OPEN(("dgap_block_til_ready - before block.\n"));

	DGAP_LOCK(ch->ch_lock, lock_flags);

	ch->ch_wopen++;

	/* Loop forever */
	while (1) {

		sleep_on_un_flags = 0;

		/*
		 * If board has failed somehow during our sleep, bail with error.
		 */
		if (ch->ch_bd->state == BOARD_FAILED) {
			retval = -ENXIO;
			break;
		}

		/* If tty was hung up, break out of loop and set error. */
		if (tty_hung_up_p(file)) {
			retval = -EAGAIN;
			break;
		}

		/*
		 * If either unit is in the middle of the fragile part of close,
		 * we just cannot touch the channel safely.
		 * Go back to sleep, knowing that when the channel can be
		 * touched safely, the close routine will signal the
		 * ch_wait_flags to wake us back up.
		 */
		if (!((ch->ch_tun.un_flags | ch->ch_pun.un_flags) & UN_CLOSING)) {

			/*
			 * Our conditions to leave cleanly and happily:
			 * 1) NONBLOCKING on the tty is set.
			 * 2) CLOCAL is set.
			 * 3) DCD (fake or real) is active.
			 */

			if (file->f_flags & O_NONBLOCK) {
				break;
			}

			if (tty->flags & (1 << TTY_IO_ERROR)) {
				break;
			}

			if (ch->ch_flags & CH_CD) {
				DPR_OPEN(("%d: ch_flags: %x\n", __LINE__, ch->ch_flags));
				break;
			}

			if (ch->ch_flags & CH_FCAR) {
				DPR_OPEN(("%d: ch_flags: %x\n", __LINE__, ch->ch_flags));
				break;
			}
		}
		else {
			sleep_on_un_flags = 1;
		}

		/*
		 * If there is a signal pending, the user probably
		 * interrupted (ctrl-c) us.
		 * Leave loop with error set.
		 */
		if (signal_pending(current)) {
			DPR_OPEN(("%d: signal pending...\n", __LINE__));
			retval = -ERESTARTSYS;
			break;
		}

		DPR_OPEN(("dgap_block_til_ready - blocking.\n"));

		/*
		 * Store the flags before we let go of channel lock
		 */
		if (sleep_on_un_flags)
			old_flags = ch->ch_tun.un_flags | ch->ch_pun.un_flags;
		else
			old_flags = ch->ch_flags;

		/*
		 * Let go of channel lock before calling schedule.
		 * Our poller will get any FEP events and wake us up when DCD
		 * eventually goes active.
		 */

		DGAP_UNLOCK(ch->ch_lock, lock_flags);

		DPR_OPEN(("Going to sleep on %s flags...\n",
			(sleep_on_un_flags ? "un" : "ch")));

		/*
		 * Wait for something in the flags to change from the current value.
		 */
		if (sleep_on_un_flags) {
			retval = wait_event_interruptible(un->un_flags_wait,
				(old_flags != (ch->ch_tun.un_flags | ch->ch_pun.un_flags)));
		}
		else {
			retval = wait_event_interruptible(ch->ch_flags_wait,
				(old_flags != ch->ch_flags));
		}

		DPR_OPEN(("After sleep... retval: %x\n", retval));

		/*
		 * We got woken up for some reason.
		 * Before looping around, grab our channel lock.
		 */
		DGAP_LOCK(ch->ch_lock, lock_flags);
	}

	ch->ch_wopen--;

	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	DPR_OPEN(("dgap_block_til_ready - after blocking.\n"));

	if (retval) {
		DPR_OPEN(("dgap_block_til_ready - done. error. retval: %x\n", retval));
		return(retval);
	}

	DPR_OPEN(("dgap_block_til_ready - done no error. jiffies: %lu\n", jiffies));

	return(0);
}


/*
 * dgap_tty_hangup()
 *
 * Hangup the port.  Like a close, but don't wait for output to drain.
 */
static void dgap_tty_hangup(struct tty_struct *tty)
{
	struct board_t	*bd;
	struct channel_t *ch;
	struct un_t	*un;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_CLOSE(("dgap_hangup called. ch->ch_open_count: %d un->un_open_count: %d\n",
		ch->ch_open_count, un->un_open_count));

	/* flush the transmit queues */
	dgap_tty_flush_buffer(tty);

	DPR_CLOSE(("dgap_hangup finished. ch->ch_open_count: %d un->un_open_count: %d\n",
		ch->ch_open_count, un->un_open_count));
}



/*
 * dgap_tty_close()
 *
 */
static void dgap_tty_close(struct tty_struct *tty, struct file *file)
{
	struct ktermios *ts;
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong lock_flags;
	int rc = 0;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	ts = &tty->termios;

	DPR_CLOSE(("Close called\n"));

	DGAP_LOCK(ch->ch_lock, lock_flags);

	/*
	 * Determine if this is the last close or not - and if we agree about
	 * which type of close it is with the Line Discipline
	 */
	if ((tty->count == 1) && (un->un_open_count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  un_open_count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		APR(("tty->count is 1, un open count is %d\n", un->un_open_count));
		un->un_open_count = 1;
	}

	if (--un->un_open_count < 0) {
		APR(("bad serial port open count of %d\n", un->un_open_count));
		un->un_open_count = 0;
	}

	ch->ch_open_count--;

	if (ch->ch_open_count && un->un_open_count) {
		DPR_CLOSE(("dgap_tty_close: not last close ch: %d un:%d\n",
			ch->ch_open_count, un->un_open_count));

		DGAP_UNLOCK(ch->ch_lock, lock_flags);
                return;
        }

	/* OK, its the last close on the unit */
	DPR_CLOSE(("dgap_tty_close - last close on unit procedures\n"));

	un->un_flags |= UN_CLOSING;

	tty->closing = 1;

	/*
	 * Only officially close channel if count is 0 and
         * DIGI_PRINTER bit is not set.
	 */
	if ((ch->ch_open_count == 0) && !(ch->ch_digi.digi_flags & DIGI_PRINTER)) {

		ch->ch_flags &= ~(CH_RXBLOCK);

		DGAP_UNLOCK(ch->ch_lock, lock_flags);

		/* wait for output to drain */
		/* This will also return if we take an interrupt */

		DPR_CLOSE(("Calling wait_for_drain\n"));
		rc = dgap_wait_for_drain(tty);
		DPR_CLOSE(("After calling wait_for_drain\n"));

		if (rc) {
			DPR_BASIC(("dgap_tty_close - bad return: %d ", rc));
		}

		dgap_tty_flush_buffer(tty);
		tty_ldisc_flush(tty);

		DGAP_LOCK(ch->ch_lock, lock_flags);

		tty->closing = 0;

		/*
		 * If we have HUPCL set, lower DTR and RTS
		 */
		if (ch->ch_c_cflag & HUPCL ) {
			DPR_CLOSE(("Close. HUPCL set, dropping DTR/RTS\n"));
			ch->ch_mostat &= ~(D_RTS(ch)|D_DTR(ch));
			dgap_cmdb( ch, SMODEM, 0, D_DTR(ch)|D_RTS(ch), 0 );

			/*
			 * Go to sleep to ensure RTS/DTR
			 * have been dropped for modems to see it.
			 */
			if (ch->ch_close_delay) {
				DPR_CLOSE(("Close. Sleeping for RTS/DTR drop\n"));

				DGAP_UNLOCK(ch->ch_lock, lock_flags);
				dgap_ms_sleep(ch->ch_close_delay);
				DGAP_LOCK(ch->ch_lock, lock_flags);

				DPR_CLOSE(("Close. After sleeping for RTS/DTR drop\n"));
			}
		}

		ch->pscan_state = 0;
		ch->pscan_savechar = 0;
		ch->ch_baud_info = 0;

	}

	/*
	 * turn off print device when closing print device.
	 */
	if ((un->un_type == DGAP_PRINT)  && (ch->ch_flags & CH_PRON) ) {
		dgap_wmove(ch, ch->ch_digi.digi_offstr,
			(int) ch->ch_digi.digi_offlen);
		ch->ch_flags &= ~CH_PRON;
	}

	un->un_tty = NULL;
	un->un_flags &= ~(UN_ISOPEN | UN_CLOSING);
	tty->driver_data = NULL;

	DPR_CLOSE(("Close. Doing wakeups\n"));
	wake_up_interruptible(&ch->ch_flags_wait);
	wake_up_interruptible(&un->un_flags_wait);

	DGAP_UNLOCK(ch->ch_lock, lock_flags);

        DPR_BASIC(("dgap_tty_close - complete\n"));
}


/*
 * dgap_tty_chars_in_buffer()
 *
 * Return number of characters that have not been transmitted yet.
 *
 * This routine is used by the line discipline to determine if there
 * is data waiting to be transmitted/drained/flushed or not.
 */
static int dgap_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct board_t *bd = NULL;
	struct channel_t *ch = NULL;
	struct un_t *un = NULL;
	struct bs_t *bs = NULL;
	uchar tbusy;
	uint chars = 0;
	u16 thead, ttail, tmask, chead, ctail;
	ulong   lock_flags = 0;
	ulong   lock_flags2 = 0;

	if (tty == NULL)
		return(0);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (0);

        bs = ch->ch_bs;
	if (!bs)
		return (0);

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	tmask = (ch->ch_tsize - 1);

	/* Get Transmit queue pointers */
	thead = readw(&(bs->tx_head)) & tmask;
	ttail = readw(&(bs->tx_tail)) & tmask;

	/* Get tbusy flag */
	tbusy = readb(&(bs->tbusy));

	/* Get Command queue pointers */
	chead = readw(&(ch->ch_cm->cm_head));
	ctail = readw(&(ch->ch_cm->cm_tail));

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	/*
	 * The only way we know for sure if there is no pending
	 * data left to be transferred, is if:
	 * 1) Transmit head and tail are equal (empty).
	 * 2) Command queue head and tail are equal (empty).
	 * 3) The "TBUSY" flag is 0. (Transmitter not busy).
 	 */

	if ((ttail == thead) && (tbusy == 0) && (chead == ctail)) {
		chars = 0;
	}
	else {
		if (thead >= ttail)
			chars = thead - ttail;
		else
			chars = thead - ttail + ch->ch_tsize;
		/*
		 * Fudge factor here.
		 * If chars is zero, we know that the command queue had
		 * something in it or tbusy was set.  Because we cannot
		 * be sure if there is still some data to be transmitted,
		 * lets lie, and tell ld we have 1 byte left.
		 */
		if (chars == 0) {
			/*
			 * If TBUSY is still set, and our tx buffers are empty,
			 * force the firmware to send me another wakeup after
			 * TBUSY has been cleared.
			 */
			if (tbusy != 0) {
				DGAP_LOCK(ch->ch_lock, lock_flags);
				un->un_flags |= UN_EMPTY;
				writeb(1, &(bs->iempty));
				DGAP_UNLOCK(ch->ch_lock, lock_flags);
			}
			chars = 1;
		}
	}

 	DPR_WRITE(("dgap_tty_chars_in_buffer. Port: %x - %d (head: %d tail: %d tsize: %d)\n",
		ch->ch_portnum, chars, thead, ttail, ch->ch_tsize));
        return(chars);
}


static int dgap_wait_for_drain(struct tty_struct *tty)
{
	struct channel_t *ch;
	struct un_t *un;
	struct bs_t *bs;
	int ret = -EIO;
	uint count = 1;
	ulong   lock_flags = 0;

	if (!tty || tty->magic != TTY_MAGIC)
		return ret;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return ret;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return ret;

        bs = ch->ch_bs;
	if (!bs)
		return ret;

	ret = 0;

	DPR_DRAIN(("dgap_wait_for_drain start\n"));

	/* Loop until data is drained */
	while (count != 0) {

		count = dgap_tty_chars_in_buffer(tty);

		if (count == 0)
			break;

		/* Set flag waiting for drain */
		DGAP_LOCK(ch->ch_lock, lock_flags);
		un->un_flags |= UN_EMPTY;
		writeb(1, &(bs->iempty));
		DGAP_UNLOCK(ch->ch_lock, lock_flags);

		/* Go to sleep till we get woken up */
		ret = wait_event_interruptible(un->un_flags_wait, ((un->un_flags & UN_EMPTY) == 0));
		/* If ret is non-zero, user ctrl-c'ed us */
		if (ret) {
			break;
		}
	}

	DGAP_LOCK(ch->ch_lock, lock_flags);
	un->un_flags &= ~(UN_EMPTY);
	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	DPR_DRAIN(("dgap_wait_for_drain finish\n"));
	return (ret);
}


/*
 * dgap_maxcps_room
 *
 * Reduces bytes_available to the max number of characters
 * that can be sent currently given the maxcps value, and
 * returns the new bytes_available.  This only affects printer
 * output.
 */
static int dgap_maxcps_room(struct tty_struct *tty, int bytes_available)
{
	struct channel_t *ch = NULL;
	struct un_t *un = NULL;

	if (tty == NULL)
		return (bytes_available);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (bytes_available);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (bytes_available);

	/*
	 * If its not the Transparent print device, return
	 * the full data amount.
	 */
	if (un->un_type != DGAP_PRINT)
		return (bytes_available);

	if (ch->ch_digi.digi_maxcps > 0 && ch->ch_digi.digi_bufsize > 0 ) {
		int cps_limit = 0;
		unsigned long current_time = jiffies;
		unsigned long buffer_time = current_time +
			(HZ * ch->ch_digi.digi_bufsize) / ch->ch_digi.digi_maxcps;

		if (ch->ch_cpstime < current_time) {
			/* buffer is empty */
			ch->ch_cpstime = current_time;            /* reset ch_cpstime */
			cps_limit = ch->ch_digi.digi_bufsize;
		}
		else if (ch->ch_cpstime < buffer_time) {
			/* still room in the buffer */
			cps_limit = ((buffer_time - ch->ch_cpstime) * ch->ch_digi.digi_maxcps) / HZ;
		}
		else {
			/* no room in the buffer */
			cps_limit = 0;
		}

		bytes_available = min(cps_limit, bytes_available);
	}

	return (bytes_available);
}


static inline void dgap_set_firmware_event(struct un_t *un, unsigned int event)
{
	struct channel_t *ch = NULL;
	struct bs_t *bs = NULL;

	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;
        bs = ch->ch_bs;
	if (!bs)
		return;

	if ((event & UN_LOW) != 0) {
		if ((un->un_flags & UN_LOW) == 0) {
			un->un_flags |= UN_LOW;
			writeb(1, &(bs->ilow));
		}
	}
	if ((event & UN_LOW) != 0) {
		if ((un->un_flags & UN_EMPTY) == 0) {
			un->un_flags |= UN_EMPTY;
			writeb(1, &(bs->iempty));
		}
	}
}


/*
 * dgap_tty_write_room()
 *
 * Return space available in Tx buffer
 */
static int dgap_tty_write_room(struct tty_struct *tty)
{
	struct channel_t *ch = NULL;
	struct un_t *un = NULL;
	struct bs_t *bs = NULL;
	u16 head, tail, tmask;
	int ret = 0;
	ulong   lock_flags = 0;

	if (tty == NULL || dgap_TmpWriteBuf == NULL)
		return(0);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);

        bs = ch->ch_bs;
	if (!bs)
		return (0);

	DGAP_LOCK(ch->ch_lock, lock_flags);

	tmask = ch->ch_tsize - 1;
	head = readw(&(bs->tx_head)) & tmask;
	tail = readw(&(bs->tx_tail)) & tmask;

        if ((ret = tail - head - 1) < 0)
                ret += ch->ch_tsize;

	/* Limit printer to maxcps */
	ret = dgap_maxcps_room(tty, ret);

	/*
	 * If we are printer device, leave space for
	 * possibly both the on and off strings.
	 */
	if (un->un_type == DGAP_PRINT) {
		if (!(ch->ch_flags & CH_PRON))
			ret -= ch->ch_digi.digi_onlen;
		ret -= ch->ch_digi.digi_offlen;
	}
	else {
		if (ch->ch_flags & CH_PRON)
			ret -= ch->ch_digi.digi_offlen;
	}

	if (ret < 0)
		ret = 0;

	/*
	 * Schedule FEP to wake us up if needed.
	 *
	 * TODO:  This might be overkill...
	 * Do we really need to schedule callbacks from the FEP
	 * in every case?  Can we get smarter based on ret?
	 */
	dgap_set_firmware_event(un, UN_LOW | UN_EMPTY);
	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	DPR_WRITE(("dgap_tty_write_room - %d tail: %d head: %d\n", ret, tail, head));

        return(ret);
}


/*
 * dgap_tty_put_char()
 *
 * Put a character into ch->ch_buf
 *
 *      - used by the line discipline for OPOST processing
 */
static int dgap_tty_put_char(struct tty_struct *tty, unsigned char c)
{
	/*
	 * Simply call tty_write.
	 */
	DPR_WRITE(("dgap_tty_put_char called\n"));
	dgap_tty_write(tty, &c, 1);
	return 1;
}


/*
 * dgap_tty_write()
 *
 * Take data from the user or kernel and send it out to the FEP.
 * In here exists all the Transparent Print magic as well.
 */
static int dgap_tty_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	struct channel_t *ch = NULL;
	struct un_t *un = NULL;
	struct bs_t *bs = NULL;
	char *vaddr = NULL;
	u16 head, tail, tmask, remain;
	int bufcount = 0, n = 0;
	int orig_count = 0;
	ulong lock_flags;
	int from_user = 0;

	if (tty == NULL || dgap_TmpWriteBuf == NULL)
		return(0);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return(0);

        bs = ch->ch_bs;
	if (!bs)
		return(0);

	if (!count)
		return(0);

	DPR_WRITE(("dgap_tty_write: Port: %x tty=%p user=%d len=%d\n",
		ch->ch_portnum, tty, from_user, count));

	/*
	 * Store original amount of characters passed in.
	 * This helps to figure out if we should ask the FEP
	 * to send us an event when it has more space available.
	 */
	orig_count = count;

	DGAP_LOCK(ch->ch_lock, lock_flags);

	/* Get our space available for the channel from the board */
	tmask = ch->ch_tsize - 1;
	head = readw(&(bs->tx_head)) & tmask;
	tail = readw(&(bs->tx_tail)) & tmask;

	if ((bufcount = tail - head - 1) < 0)
		bufcount += ch->ch_tsize;

	DPR_WRITE(("%d: bufcount: %x count: %x tail: %x head: %x tmask: %x\n",
		__LINE__, bufcount, count, tail, head, tmask));

	/*
	 * Limit printer output to maxcps overall, with bursts allowed
	 * up to bufsize characters.
	 */
	bufcount = dgap_maxcps_room(tty, bufcount);

	/*
	 * Take minimum of what the user wants to send, and the
	 * space available in the FEP buffer.
	 */
	count = min(count, bufcount);

	/*
	 * Bail if no space left.
	 */
	if (count <= 0) {
		dgap_set_firmware_event(un, UN_LOW | UN_EMPTY);
		DGAP_UNLOCK(ch->ch_lock, lock_flags);
		return(0);
	}

	/*
	 * Output the printer ON string, if we are in terminal mode, but
	 * need to be in printer mode.
	 */
	if ((un->un_type == DGAP_PRINT) && !(ch->ch_flags & CH_PRON)) {
		dgap_wmove(ch, ch->ch_digi.digi_onstr,
		    (int) ch->ch_digi.digi_onlen);
		head = readw(&(bs->tx_head)) & tmask;
		ch->ch_flags |= CH_PRON;
	}

	/*
	 * On the other hand, output the printer OFF string, if we are
	 * currently in printer mode, but need to output to the terminal.
	 */
	if ((un->un_type != DGAP_PRINT) && (ch->ch_flags & CH_PRON)) {
		dgap_wmove(ch, ch->ch_digi.digi_offstr,
			(int) ch->ch_digi.digi_offlen);
		head = readw(&(bs->tx_head)) & tmask;
		ch->ch_flags &= ~CH_PRON;
	}

	/*
	 * If there is nothing left to copy, or I can't handle any more data, leave.
	 */
	if (count <= 0) {
		dgap_set_firmware_event(un, UN_LOW | UN_EMPTY);
		DGAP_UNLOCK(ch->ch_lock, lock_flags);
		return(0);
	}

	if (from_user) {

		count = min(count, WRITEBUFLEN);

		DGAP_UNLOCK(ch->ch_lock, lock_flags);

		/*
		 * If data is coming from user space, copy it into a temporary
		 * buffer so we don't get swapped out while doing the copy to
		 * the board.
		 */
		/* we're allowed to block if it's from_user */
		if (down_interruptible(&dgap_TmpWriteSem)) {
			return (-EINTR);
		}

		if (copy_from_user(dgap_TmpWriteBuf, (const uchar __user *) buf, count)) {
			up(&dgap_TmpWriteSem);
			printk("Write: Copy from user failed!\n");
			return -EFAULT;
		}

		DGAP_LOCK(ch->ch_lock, lock_flags);

		buf = dgap_TmpWriteBuf;
	}

	n = count;

	/*
	 * If the write wraps over the top of the circular buffer,
	 * move the portion up to the wrap point, and reset the
	 * pointers to the bottom.
	 */
	remain = ch->ch_tstart + ch->ch_tsize - head;

	if (n >= remain) {
		n -= remain;
		vaddr = ch->ch_taddr + head;

		memcpy_toio(vaddr, (uchar *) buf, remain);
		dgap_sniff_nowait_nolock(ch, "USER WRITE", (uchar *) buf, remain);

		head = ch->ch_tstart;
		buf += remain;
	}

	if (n > 0) {

		/*
		 * Move rest of data.
		 */
		vaddr = ch->ch_taddr + head;
		remain = n;

		memcpy_toio(vaddr, (uchar *) buf, remain);
		dgap_sniff_nowait_nolock(ch, "USER WRITE", (uchar *) buf, remain);

		head += remain;

	}

	if (count) {
		ch->ch_txcount += count;
		head &= tmask;
		writew(head, &(bs->tx_head));
	}


	dgap_set_firmware_event(un, UN_LOW | UN_EMPTY);

	/*
	 * If this is the print device, and the
	 * printer is still on, we need to turn it
	 * off before going idle.  If the buffer is
	 * non-empty, wait until it goes empty.
	 * Otherwise turn it off right now.
	 */
	if ((un->un_type == DGAP_PRINT) && (ch->ch_flags & CH_PRON)) {
		tail = readw(&(bs->tx_tail)) & tmask;

		if (tail != head) {
			un->un_flags |= UN_EMPTY;
			writeb(1, &(bs->iempty));
		}
		else {
			dgap_wmove(ch, ch->ch_digi.digi_offstr,
				(int) ch->ch_digi.digi_offlen);
			head = readw(&(bs->tx_head)) & tmask;
			ch->ch_flags &= ~CH_PRON;
		}
	}

	/* Update printer buffer empty time. */
	if ((un->un_type == DGAP_PRINT) && (ch->ch_digi.digi_maxcps > 0)
	    && (ch->ch_digi.digi_bufsize > 0)) {
                ch->ch_cpstime += (HZ * count) / ch->ch_digi.digi_maxcps;
	}

	if (from_user) {
		DGAP_UNLOCK(ch->ch_lock, lock_flags);
		up(&dgap_TmpWriteSem);
	}
	else {
		DGAP_UNLOCK(ch->ch_lock, lock_flags);
	}

	DPR_WRITE(("Write finished - Write %d bytes of %d.\n", count, orig_count));

	return (count);
}



/*
 * Return modem signals to ld.
 */
static int dgap_tty_tiocmget(struct tty_struct *tty)
{
	struct channel_t *ch;
	struct un_t *un;
	int result = -EIO;
	uchar mstat = 0;
	ulong lock_flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return result;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return result;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return result;

	DPR_IOCTL(("dgap_tty_tiocmget start\n"));

	DGAP_LOCK(ch->ch_lock, lock_flags);

	mstat = readb(&(ch->ch_bs->m_stat));
        /* Append any outbound signals that might be pending... */
        mstat |= ch->ch_mostat;

	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	result = 0;

	if (mstat & D_DTR(ch))
		result |= TIOCM_DTR;
	if (mstat & D_RTS(ch))
		result |= TIOCM_RTS;
	if (mstat & D_CTS(ch))
		result |= TIOCM_CTS;
	if (mstat & D_DSR(ch))
		result |= TIOCM_DSR;
	if (mstat & D_RI(ch))
		result |= TIOCM_RI;
	if (mstat & D_CD(ch))
		result |= TIOCM_CD;

	DPR_IOCTL(("dgap_tty_tiocmget finish\n"));

	return result;
}


/*
 * dgap_tty_tiocmset()
 *
 * Set modem signals, called by ld.
 */

static int dgap_tty_tiocmset(struct tty_struct *tty,
                unsigned int set, unsigned int clear)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	int ret = -EIO;
	ulong lock_flags;
	ulong lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return ret;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return ret;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return ret;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return ret;

	DPR_IOCTL(("dgap_tty_tiocmset start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	if (set & TIOCM_RTS) {
		ch->ch_mforce |= D_RTS(ch);
		ch->ch_mval   |= D_RTS(ch);
        }

	if (set & TIOCM_DTR) {
		ch->ch_mforce |= D_DTR(ch);
		ch->ch_mval   |= D_DTR(ch);
        }

	if (clear & TIOCM_RTS) {
		ch->ch_mforce |= D_RTS(ch);
		ch->ch_mval   &= ~(D_RTS(ch));
        }

	if (clear & TIOCM_DTR) {
		ch->ch_mforce |= D_DTR(ch);
		ch->ch_mval   &= ~(D_DTR(ch));
        }

	dgap_param(tty);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_tiocmset finish\n"));

	return (0);
}



/*
 * dgap_tty_send_break()
 *
 * Send a Break, called by ld.
 */
static int dgap_tty_send_break(struct tty_struct *tty, int msec)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	int ret = -EIO;
	ulong lock_flags;
	ulong lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return ret;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return ret;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return ret;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return ret;

	switch (msec) {
	case -1:
		msec = 0xFFFF;
		break;
	case 0:
		msec = 1;
		break;
	default:
		msec /= 10;
		break;
	}

	DPR_IOCTL(("dgap_tty_send_break start 1.  %lx\n", jiffies));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);
#if 0
	dgap_cmdw(ch, SBREAK, (u16) SBREAK_TIME, 0);
#endif
	dgap_cmdw(ch, SBREAK, (u16) msec, 0);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_send_break finish\n"));

	return (0);
}




/*
 * dgap_tty_wait_until_sent()
 *
 * wait until data has been transmitted, called by ld.
 */
static void dgap_tty_wait_until_sent(struct tty_struct *tty, int timeout)
{
	int rc;
	rc = dgap_wait_for_drain(tty);
	if (rc) {
		DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
		return;
	}
	return;
}



/*
 * dgap_send_xchar()
 *
 * send a high priority character, called by ld.
 */
static void dgap_tty_send_xchar(struct tty_struct *tty, char c)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong lock_flags;
	ulong lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_send_xchar start 1.  %lx\n", jiffies));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	/*
	 * This is technically what we should do.
	 * However, the NIST tests specifically want
	 * to see each XON or XOFF character that it
	 * sends, so lets just send each character
	 * by hand...
	 */
#if 0
	if (c == STOP_CHAR(tty)) {
		dgap_cmdw(ch, RPAUSE, 0, 0);
	}
	else if (c == START_CHAR(tty)) {
		dgap_cmdw(ch, RRESUME, 0, 0);
	}
	else {
		dgap_wmove(ch, &c, 1);
	}
#else
	dgap_wmove(ch, &c, 1);
#endif

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_send_xchar finish\n"));

	return;
}




/*
 * Return modem signals to ld.
 */
static int dgap_get_modem_info(struct channel_t *ch, unsigned int __user *value)
{
	int result = 0;
	uchar mstat = 0;
	ulong lock_flags;
	int rc = 0;

	DPR_IOCTL(("dgap_get_modem_info start\n"));

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return(-ENXIO);

	DGAP_LOCK(ch->ch_lock, lock_flags);

	mstat = readb(&(ch->ch_bs->m_stat));
	/* Append any outbound signals that might be pending... */
	mstat |= ch->ch_mostat;

	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	result = 0;

	if (mstat & D_DTR(ch))
		result |= TIOCM_DTR;
	if (mstat & D_RTS(ch))
		result |= TIOCM_RTS;
	if (mstat & D_CTS(ch))
		result |= TIOCM_CTS;
	if (mstat & D_DSR(ch))
		result |= TIOCM_DSR;
	if (mstat & D_RI(ch))
		result |= TIOCM_RI;
	if (mstat & D_CD(ch))
		result |= TIOCM_CD;

	rc = put_user(result, value);

	DPR_IOCTL(("dgap_get_modem_info finish\n"));
	return(rc);
}


/*
 * dgap_set_modem_info()
 *
 * Set modem signals, called by ld.
 */
static int dgap_set_modem_info(struct tty_struct *tty, unsigned int command, unsigned int __user *value)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	int ret = -ENXIO;
	unsigned int arg = 0;
	ulong lock_flags;
	ulong lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return ret;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return ret;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return ret;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return ret;

	DPR_IOCTL(("dgap_set_modem_info() start\n"));

	ret = get_user(arg, value);
	if (ret) {
		DPR_IOCTL(("dgap_set_modem_info %d ret: %x. finished.\n", __LINE__, ret));
		return(ret);
	}

	DPR_IOCTL(("dgap_set_modem_info: command: %x arg: %x\n", command, arg));

	switch (command) {
	case TIOCMBIS:
		if (arg & TIOCM_RTS) {
			ch->ch_mforce |= D_RTS(ch);
			ch->ch_mval   |= D_RTS(ch);
        	}

		if (arg & TIOCM_DTR) {
			ch->ch_mforce |= D_DTR(ch);
			ch->ch_mval   |= D_DTR(ch);
        	}

		break;

	case TIOCMBIC:
		if (arg & TIOCM_RTS) {
			ch->ch_mforce |= D_RTS(ch);
			ch->ch_mval   &= ~(D_RTS(ch));
        	}

		if (arg & TIOCM_DTR) {
			ch->ch_mforce |= D_DTR(ch);
			ch->ch_mval   &= ~(D_DTR(ch));
        	}

		break;

        case TIOCMSET:
		ch->ch_mforce = D_DTR(ch)|D_RTS(ch);

		if (arg & TIOCM_RTS) {
			ch->ch_mval |= D_RTS(ch);
        	}
		else {
			ch->ch_mval &= ~(D_RTS(ch));
		}

		if (arg & TIOCM_DTR) {
			ch->ch_mval |= (D_DTR(ch));
        	}
		else {
			ch->ch_mval &= ~(D_DTR(ch));
		}

		break;

	default:
		return(-EINVAL);
	}

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	dgap_param(tty);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_set_modem_info finish\n"));

	return (0);
}


/*
 * dgap_tty_digigeta()
 *
 * Ioctl to get the information for ditty.
 *
 *
 *
 */
static int dgap_tty_digigeta(struct tty_struct *tty, struct digi_t __user *retinfo)
{
	struct channel_t *ch;
	struct un_t *un;
	struct digi_t tmp;
	ulong lock_flags;

	if (!retinfo)
		return (-EFAULT);

	if (!tty || tty->magic != TTY_MAGIC)
		return (-EFAULT);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-EFAULT);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-EFAULT);

	memset(&tmp, 0, sizeof(tmp));

	DGAP_LOCK(ch->ch_lock, lock_flags);
	memcpy(&tmp, &ch->ch_digi, sizeof(tmp));
	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return (-EFAULT);

	return (0);
}


/*
 * dgap_tty_digiseta()
 *
 * Ioctl to set the information for ditty.
 *
 *
 *
 */
static int dgap_tty_digiseta(struct tty_struct *tty, struct digi_t __user *new_info)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	struct digi_t new_digi;
	ulong   lock_flags = 0;
	unsigned long lock_flags2;

	DPR_IOCTL(("DIGI_SETA start\n"));

	if (!tty || tty->magic != TTY_MAGIC)
		return (-EFAULT);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-EFAULT);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-EFAULT);

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (-EFAULT);

        if (copy_from_user(&new_digi, new_info, sizeof(struct digi_t))) {
		DPR_IOCTL(("DIGI_SETA failed copy_from_user\n"));
                return(-EFAULT);
	}

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	memcpy(&ch->ch_digi, &new_digi, sizeof(struct digi_t));

	if (ch->ch_digi.digi_maxcps < 1)
		ch->ch_digi.digi_maxcps = 1;

	if (ch->ch_digi.digi_maxcps > 10000)
		ch->ch_digi.digi_maxcps = 10000;

	if (ch->ch_digi.digi_bufsize < 10)
		ch->ch_digi.digi_bufsize = 10;

	if (ch->ch_digi.digi_maxchar < 1)
		ch->ch_digi.digi_maxchar = 1;

	if (ch->ch_digi.digi_maxchar > ch->ch_digi.digi_bufsize)
		ch->ch_digi.digi_maxchar = ch->ch_digi.digi_bufsize;

	if (ch->ch_digi.digi_onlen > DIGI_PLEN)
		ch->ch_digi.digi_onlen = DIGI_PLEN;

	if (ch->ch_digi.digi_offlen > DIGI_PLEN)
		ch->ch_digi.digi_offlen = DIGI_PLEN;

	dgap_param(tty);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("DIGI_SETA finish\n"));

	return(0);
}


/*
 * dgap_tty_digigetedelay()
 *
 * Ioctl to get the current edelay setting.
 *
 *
 *
 */
static int dgap_tty_digigetedelay(struct tty_struct *tty, int __user *retinfo)
{
	struct channel_t *ch;
	struct un_t *un;
	int tmp;
	ulong lock_flags;

	if (!retinfo)
		return (-EFAULT);

	if (!tty || tty->magic != TTY_MAGIC)
		return (-EFAULT);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-EFAULT);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-EFAULT);

	memset(&tmp, 0, sizeof(tmp));

	DGAP_LOCK(ch->ch_lock, lock_flags);
	tmp = readw(&(ch->ch_bs->edelay));
	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return (-EFAULT);

	return (0);
}


/*
 * dgap_tty_digisetedelay()
 *
 * Ioctl to set the EDELAY setting
 *
 */
static int dgap_tty_digisetedelay(struct tty_struct *tty, int __user *new_info)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	int new_digi;
	ulong lock_flags;
	ulong lock_flags2;

	DPR_IOCTL(("DIGI_SETA start\n"));

	if (!tty || tty->magic != TTY_MAGIC)
		return (-EFAULT);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-EFAULT);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-EFAULT);

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (-EFAULT);

        if (copy_from_user(&new_digi, new_info, sizeof(int))) {
		DPR_IOCTL(("DIGI_SETEDELAY failed copy_from_user\n"));
                return(-EFAULT);
	}

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	writew((u16) new_digi, &(ch->ch_bs->edelay));

	dgap_param(tty);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("DIGI_SETA finish\n"));

	return(0);
}


/*
 * dgap_tty_digigetcustombaud()
 *
 * Ioctl to get the current custom baud rate setting.
 */
static int dgap_tty_digigetcustombaud(struct tty_struct *tty, int __user *retinfo)
{
	struct channel_t *ch;
	struct un_t *un;
	int tmp;
	ulong lock_flags;

	if (!retinfo)
		return (-EFAULT);

	if (!tty || tty->magic != TTY_MAGIC)
		return (-EFAULT);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-EFAULT);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-EFAULT);

	memset(&tmp, 0, sizeof(tmp));

	DGAP_LOCK(ch->ch_lock, lock_flags);
	tmp = dgap_get_custom_baud(ch);
	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	DPR_IOCTL(("DIGI_GETCUSTOMBAUD. Returning %d\n", tmp));

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return (-EFAULT);

	return (0);
}


/*
 * dgap_tty_digisetcustombaud()
 *
 * Ioctl to set the custom baud rate setting
 */
static int dgap_tty_digisetcustombaud(struct tty_struct *tty, int __user *new_info)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	uint new_rate;
	ulong lock_flags;
	ulong lock_flags2;

	DPR_IOCTL(("DIGI_SETCUSTOMBAUD start\n"));

	if (!tty || tty->magic != TTY_MAGIC)
		return (-EFAULT);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-EFAULT);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-EFAULT);

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (-EFAULT);


	if (copy_from_user(&new_rate, new_info, sizeof(unsigned int))) {
		DPR_IOCTL(("DIGI_SETCUSTOMBAUD failed copy_from_user\n"));
		return(-EFAULT);
	}

	if (bd->bd_flags & BD_FEP5PLUS) {

		DPR_IOCTL(("DIGI_SETCUSTOMBAUD. Setting %d\n", new_rate));

		DGAP_LOCK(bd->bd_lock, lock_flags);
		DGAP_LOCK(ch->ch_lock, lock_flags2);

		ch->ch_custom_speed = new_rate;

		dgap_param(tty);

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
	}

	DPR_IOCTL(("DIGI_SETCUSTOMBAUD finish\n"));

	return(0);
}


/*
 * dgap_set_termios()
 */
static void dgap_tty_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	unsigned long lock_flags;
	unsigned long lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	ch->ch_c_cflag   = tty->termios.c_cflag;
	ch->ch_c_iflag   = tty->termios.c_iflag;
	ch->ch_c_oflag   = tty->termios.c_oflag;
	ch->ch_c_lflag   = tty->termios.c_lflag;
	ch->ch_startc    = tty->termios.c_cc[VSTART];
	ch->ch_stopc     = tty->termios.c_cc[VSTOP];

	dgap_carrier(ch);
	dgap_param(tty);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);
}


static void dgap_tty_throttle(struct tty_struct *tty)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong   lock_flags;
	ulong   lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

        ch = un->un_ch;
        if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
                return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_throttle start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	ch->ch_flags |= (CH_RXBLOCK);
#if 1
	dgap_cmdw(ch, RPAUSE, 0, 0);
#endif

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_throttle finish\n"));
}


static void dgap_tty_unthrottle(struct tty_struct *tty)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong   lock_flags;
	ulong   lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

        ch = un->un_ch;
        if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
                return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_unthrottle start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	ch->ch_flags &= ~(CH_RXBLOCK);

#if 1
	dgap_cmdw(ch, RRESUME, 0, 0);
#endif

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_unthrottle finish\n"));
}


static void dgap_tty_start(struct tty_struct *tty)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong   lock_flags;
	ulong   lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

        ch = un->un_ch;
        if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
                return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_start start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	dgap_cmdw(ch, RESUMETX, 0, 0);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_start finish\n"));
}


static void dgap_tty_stop(struct tty_struct *tty)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong   lock_flags;
	ulong   lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

        ch = un->un_ch;
        if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
                return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_stop start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	dgap_cmdw(ch, PAUSETX, 0, 0);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_stop finish\n"));
}


/*
 * dgap_tty_flush_chars()
 *
 * Flush the cook buffer
 *
 * Note to self, and any other poor souls who venture here:
 *
 * flush in this case DOES NOT mean dispose of the data.
 * instead, it means "stop buffering and send it if you
 * haven't already."  Just guess how I figured that out...   SRW 2-Jun-98
 *
 * It is also always called in interrupt context - JAR 8-Sept-99
 */
static void dgap_tty_flush_chars(struct tty_struct *tty)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong   lock_flags;
	ulong   lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

        ch = un->un_ch;
        if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
                return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_flush_chars start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	/* TODO: Do something here */

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_flush_chars finish\n"));
}



/*
 * dgap_tty_flush_buffer()
 *
 * Flush Tx buffer (make in == out)
 */
static void dgap_tty_flush_buffer(struct tty_struct *tty)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong   lock_flags;
	ulong   lock_flags2;
	u16	head = 0;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

        ch = un->un_ch;
        if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
                return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_flush_buffer on port: %d start\n", ch->ch_portnum));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	ch->ch_flags &= ~CH_STOP;
	head = readw(&(ch->ch_bs->tx_head));
	dgap_cmdw(ch, FLUSHTX, (u16) head, 0);
	dgap_cmdw(ch, RESUMETX, 0, 0);
	if (ch->ch_tun.un_flags & (UN_LOW|UN_EMPTY)) {
		ch->ch_tun.un_flags &= ~(UN_LOW|UN_EMPTY);
		wake_up_interruptible(&ch->ch_tun.un_flags_wait);
	}
	if (ch->ch_pun.un_flags & (UN_LOW|UN_EMPTY)) {
		ch->ch_pun.un_flags &= ~(UN_LOW|UN_EMPTY);
		wake_up_interruptible(&ch->ch_pun.un_flags_wait);
	}

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);
	if (waitqueue_active(&tty->write_wait))
		wake_up_interruptible(&tty->write_wait);
	tty_wakeup(tty);

	DPR_IOCTL(("dgap_tty_flush_buffer finish\n"));
}



/*****************************************************************************
 *
 * The IOCTL function and all of its helpers
 *
 *****************************************************************************/

/*
 * dgap_tty_ioctl()
 *
 * The usual assortment of ioctl's
 */
static int dgap_tty_ioctl(struct tty_struct *tty, unsigned int cmd,
		unsigned long arg)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	int rc;
	u16	head = 0;
	ulong   lock_flags = 0;
	ulong   lock_flags2 = 0;
	void __user *uarg = (void __user *) arg;

	if (!tty || tty->magic != TTY_MAGIC)
		return (-ENODEV);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-ENODEV);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-ENODEV);

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (-ENODEV);

	DPR_IOCTL(("dgap_tty_ioctl start on port %d - cmd %s (%x), arg %lx\n",
		ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	if (un->un_open_count <= 0) {
		DPR_BASIC(("dgap_tty_ioctl - unit not open.\n"));
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(-EIO);
	}

	switch (cmd) {

	/* Here are all the standard ioctl's that we MUST implement */

	case TCSBRK:
		/*
		 * TCSBRK is SVID version: non-zero arg --> no break
		 * this behaviour is exploited by tcdrain().
		 *
		 * According to POSIX.1 spec (7.2.2.1.2) breaks should be
		 * between 0.25 and 0.5 seconds so we'll ask for something
		 * in the middle: 0.375 seconds.
		 */
		rc = tty_check_change(tty);
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		if (rc) {
			return(rc);
		}

		rc = dgap_wait_for_drain(tty);

		if (rc) {
			DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
			return(-EINTR);
		}

		DGAP_LOCK(bd->bd_lock, lock_flags);
		DGAP_LOCK(ch->ch_lock, lock_flags2);

		if(((cmd == TCSBRK) && (!arg)) || (cmd == TCSBRKP)) {
			dgap_cmdw(ch, SBREAK, (u16) SBREAK_TIME, 0);
		}

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		DPR_IOCTL(("dgap_tty_ioctl finish on port %d - cmd %s (%x), arg %lx\n",
			ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));

                return(0);


	case TCSBRKP:
 		/* support for POSIX tcsendbreak()

		 * According to POSIX.1 spec (7.2.2.1.2) breaks should be
		 * between 0.25 and 0.5 seconds so we'll ask for something
		 * in the middle: 0.375 seconds.
		 */
		rc = tty_check_change(tty);
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		if (rc) {
			return(rc);
		}

		rc = dgap_wait_for_drain(tty);
		if (rc) {
			DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
			return(-EINTR);
		}

		DGAP_LOCK(bd->bd_lock, lock_flags);
		DGAP_LOCK(ch->ch_lock, lock_flags2);

		dgap_cmdw(ch, SBREAK, (u16) SBREAK_TIME, 0);

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		DPR_IOCTL(("dgap_tty_ioctl finish on port %d - cmd %s (%x), arg %lx\n",
			ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));

		return(0);

        case TIOCSBRK:
		/*
		 * FEP5 doesn't support turning on a break unconditionally.
		 * The FEP5 device will stop sending a break automatically
		 * after the specified time value that was sent when turning on
		 * the break.
		 */
		rc = tty_check_change(tty);
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		if (rc) {
			return(rc);
		}

		rc = dgap_wait_for_drain(tty);
		if (rc) {
			DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
			return(-EINTR);
		}

		DGAP_LOCK(bd->bd_lock, lock_flags);
		DGAP_LOCK(ch->ch_lock, lock_flags2);

		dgap_cmdw(ch, SBREAK, (u16) SBREAK_TIME, 0);

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		DPR_IOCTL(("dgap_tty_ioctl finish on port %d - cmd %s (%x), arg %lx\n",
			ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));

		return 0;

        case TIOCCBRK:
		/*
		 * FEP5 doesn't support turning off a break unconditionally.
		 * The FEP5 device will stop sending a break automatically
		 * after the specified time value that was sent when turning on
		 * the break.
		 */
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return 0;

	case TIOCGSOFTCAR:

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		rc = put_user(C_CLOCAL(tty) ? 1 : 0, (unsigned long __user *) arg);
		return(rc);

	case TIOCSSOFTCAR:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		rc = get_user(arg, (unsigned long __user *) arg);
		if (rc)
			return(rc);

		DGAP_LOCK(bd->bd_lock, lock_flags);
		DGAP_LOCK(ch->ch_lock, lock_flags2);
		tty->termios.c_cflag = ((tty->termios.c_cflag & ~CLOCAL) | (arg ? CLOCAL : 0));
		dgap_param(tty);
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		return(0);

	case TIOCMGET:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
                return(dgap_get_modem_info(ch, uarg));

	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_set_modem_info(tty, cmd, uarg));

		/*
		 * Here are any additional ioctl's that we want to implement
		 */

	case TCFLSH:
		/*
		 * The linux tty driver doesn't have a flush
		 * input routine for the driver, assuming all backed
		 * up data is in the line disc. buffers.  However,
		 * we all know that's not the case.  Here, we
		 * act on the ioctl, but then lie and say we didn't
		 * so the line discipline will process the flush
		 * also.
		 */
		rc = tty_check_change(tty);
		if (rc) {
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			return(rc);
		}

		if ((arg == TCIFLUSH) || (arg == TCIOFLUSH)) {
			if (!(un->un_type == DGAP_PRINT)) {
				head = readw(&(ch->ch_bs->rx_head));
				writew(head, &(ch->ch_bs->rx_tail));
				writeb(0, &(ch->ch_bs->orun));
			}
		}

		if ((arg == TCOFLUSH) || (arg == TCIOFLUSH)) {
			ch->ch_flags &= ~CH_STOP;
			head = readw(&(ch->ch_bs->tx_head));
			dgap_cmdw(ch, FLUSHTX, (u16) head, 0 );
			dgap_cmdw(ch, RESUMETX, 0, 0);
			if (ch->ch_tun.un_flags & (UN_LOW|UN_EMPTY)) {
				ch->ch_tun.un_flags &= ~(UN_LOW|UN_EMPTY);
				wake_up_interruptible(&ch->ch_tun.un_flags_wait);
			}
			if (ch->ch_pun.un_flags & (UN_LOW|UN_EMPTY)) {
				ch->ch_pun.un_flags &= ~(UN_LOW|UN_EMPTY);
				wake_up_interruptible(&ch->ch_pun.un_flags_wait);
			}
			if (waitqueue_active(&tty->write_wait))
				wake_up_interruptible(&tty->write_wait);

			/* Can't hold any locks when calling tty_wakeup! */
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			tty_wakeup(tty);
			DGAP_LOCK(bd->bd_lock, lock_flags);
			DGAP_LOCK(ch->ch_lock, lock_flags2);
		}

		/* pretend we didn't recognize this IOCTL */
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		DPR_IOCTL(("dgap_tty_ioctl (LINE:%d) finish on port %d - cmd %s (%x), arg %lx\n",
			__LINE__, ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));

		return(-ENOIOCTLCMD);

	case TCSETSF:
	case TCSETSW:
		/*
		 * The linux tty driver doesn't have a flush
		 * input routine for the driver, assuming all backed
		 * up data is in the line disc. buffers.  However,
		 * we all know that's not the case.  Here, we
		 * act on the ioctl, but then lie and say we didn't
		 * so the line discipline will process the flush
		 * also.
		 */
		if (cmd == TCSETSF) {
			/* flush rx */
			ch->ch_flags &= ~CH_STOP;
			head = readw(&(ch->ch_bs->rx_head));
			writew(head, &(ch->ch_bs->rx_tail));
		}

		/* now wait for all the output to drain */
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		rc = dgap_wait_for_drain(tty);
		if (rc) {
			DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
			return(-EINTR);
		}

		DPR_IOCTL(("dgap_tty_ioctl finish on port %d - cmd %s (%x), arg %lx\n",
			ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));

		/* pretend we didn't recognize this */
		return(-ENOIOCTLCMD);

	case TCSETAW:

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		rc = dgap_wait_for_drain(tty);
		if (rc) {
			DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
			return(-EINTR);
		}

		/* pretend we didn't recognize this */
		return(-ENOIOCTLCMD);

	case TCXONC:
		/*
		 * The Linux Line Discipline (LD) would do this for us if we
		 * let it, but we have the special firmware options to do this
		 * the "right way" regardless of hardware or software flow
		 * control so we'll do it outselves instead of letting the LD
		 * do it.
		 */
		rc = tty_check_change(tty);
		if (rc) {
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			return(rc);
		}

		DPR_IOCTL(("dgap_ioctl - in TCXONC - %d\n", cmd));
		switch (arg) {

		case TCOON:
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			dgap_tty_start(tty);
			return(0);
		case TCOOFF:
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			dgap_tty_stop(tty);
			return(0);
		case TCION:
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			/* Make the ld do it */
			return(-ENOIOCTLCMD);
		case TCIOFF:
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			/* Make the ld do it */
			return(-ENOIOCTLCMD);
		default:
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			return(-EINVAL);
		}

	case DIGI_GETA:
		/* get information for ditty */
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_tty_digigeta(tty, uarg));

	case DIGI_SETAW:
	case DIGI_SETAF:

		/* set information for ditty */
		if (cmd == (DIGI_SETAW)) {

			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			rc = dgap_wait_for_drain(tty);
			if (rc) {
				DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
				return(-EINTR);
			}
			DGAP_LOCK(bd->bd_lock, lock_flags);
			DGAP_LOCK(ch->ch_lock, lock_flags2);
		}
		else {
			tty_ldisc_flush(tty);
		}
		/* fall thru */

	case DIGI_SETA:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_tty_digiseta(tty, uarg));

	case DIGI_GEDELAY:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_tty_digigetedelay(tty, uarg));

	case DIGI_SEDELAY:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_tty_digisetedelay(tty, uarg));

	case DIGI_GETCUSTOMBAUD:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_tty_digigetcustombaud(tty, uarg));

	case DIGI_SETCUSTOMBAUD:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_tty_digisetcustombaud(tty, uarg));

	case DIGI_RESET_PORT:
		dgap_firmware_reset_port(ch);
		dgap_param(tty);
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return 0;

	default:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		DPR_IOCTL(("dgap_tty_ioctl - in default\n"));
		DPR_IOCTL(("dgap_tty_ioctl end - cmd %s (%x), arg %lx\n",
			dgap_ioctl_name(cmd), cmd, arg));

		return(-ENOIOCTLCMD);
	}
}
/*
 * Loads the dgap.conf config file from the user.
 */
void dgap_do_config_load(uchar __user *uaddr, int len)
{
	int orig_len = len;
	char *to_addr;
	uchar __user *from_addr = uaddr;
	char buf[U2BSIZE];
	int n;

	to_addr = dgap_config_buf = kzalloc(len + 1, GFP_ATOMIC);
	if (!dgap_config_buf) {
		DPR_INIT(("dgap_do_config_load - unable to allocate memory for file\n"));
		dgap_driver_state = DRIVER_NEED_CONFIG_LOAD;
		return;
	}

	n = U2BSIZE;
	while (len) {

		if (n > len)
			n = len;

		if (copy_from_user((char *) &buf, from_addr, n) == -1 )
			return;

		/* Copy data from buffer to kernel memory */
		memcpy(to_addr, buf, n);

		/* increment counts */
		len -= n;
		to_addr += n;
		from_addr += n;
		n = U2BSIZE;
	}

	dgap_config_buf[orig_len] = '\0';

	to_addr = dgap_config_buf;
	dgap_parsefile(&to_addr, TRUE);

	DPR_INIT(("dgap_config_load() finish\n"));

	return;
}


int dgap_after_config_loaded(void)
{
	int i = 0;
	int rc = 0;

	/*
	 * Register our ttys, now that we have the config loaded.
	 */
	for (i = 0; i < dgap_NumBoards; ++i) {

		/*
		 * Initialize KME waitqueues...
		 */
		init_waitqueue_head(&(dgap_Board[i]->kme_wait));

		/*
		 * allocate flip buffer for board.
		 */
		dgap_Board[i]->flipbuf = kzalloc(MYFLIPLEN, GFP_ATOMIC);
		dgap_Board[i]->flipflagbuf = kzalloc(MYFLIPLEN, GFP_ATOMIC);
	}

	return rc;
}



/*=======================================================================
 *
 *      usertoboard - copy from user space to board space.
 *
 *=======================================================================*/
static int dgap_usertoboard(struct board_t *brd, char *to_addr, char __user *from_addr, int len)
{
	char buf[U2BSIZE];
	int n = U2BSIZE;

	if (!brd || brd->magic != DGAP_BOARD_MAGIC)
		return -EFAULT;

	while (len) {
		if (n > len)
			n = len;

		if (copy_from_user((char *) &buf, from_addr, n) == -1 ) {
			return -EFAULT;
		}

		/* Copy data from buffer to card memory */
		memcpy_toio(to_addr, buf, n);

		/* increment counts */
		len -= n;
		to_addr += n;
		from_addr += n;
		n = U2BSIZE;
	}
	return 0;
}


/*
 * Copies the BIOS code from the user to the board,
 * and starts the BIOS running.
 */
void dgap_do_bios_load(struct board_t *brd, uchar __user *ubios, int len)
{
	uchar *addr;
	uint offset;
	int i;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return;

	DPR_INIT(("dgap_do_bios_load() start\n"));

	addr = brd->re_map_membase;

	/*
	 * clear POST area
	 */
	for (i = 0; i < 16; i++)
		writeb(0, addr + POSTAREA + i);

	/*
	 * Download bios
	 */
	offset = 0x1000;
	if (dgap_usertoboard(brd, addr + offset, ubios, len) == -1 ) {
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
		return;
	}

	writel(0x0bf00401, addr);
	writel(0, (addr + 4));

	/* Clear the reset, and change states. */
	writeb(FEPCLR, brd->re_map_port);
	brd->state = WAIT_BIOS_LOAD;
}


/*
 * Checks to see if the BIOS completed running on the card.
 */
static void dgap_do_wait_for_bios(struct board_t *brd)
{
	uchar *addr;
	u16 word;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return;

	addr = brd->re_map_membase;
	word = readw(addr + POSTAREA);

	/* Check to see if BIOS thinks board is good. (GD). */
	if (word == *(u16 *) "GD") {
		DPR_INIT(("GOT GD in memory, moving states.\n"));
		brd->state = FINISHED_BIOS_LOAD;
		return;
	}

	/* Give up on board after too long of time taken */
	if (brd->wait_for_bios++ > 5000) {
		u16 err1 = readw(addr + SEQUENCE);
		u16 err2 = readw(addr + ERROR);
		APR(("***WARNING*** %s failed diagnostics.  Error #(%x,%x).\n",
			brd->name, err1, err2));
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
	}
}


/*
 * Copies the FEP code from the user to the board,
 * and starts the FEP running.
 */
void dgap_do_fep_load(struct board_t *brd, uchar __user *ufep, int len)
{
	uchar *addr;
	uint offset;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return;

	addr = brd->re_map_membase;

	DPR_INIT(("dgap_do_fep_load() for board %s : start\n", brd->name));

	/*
	 * Download FEP
	 */
	offset = 0x1000;
	if (dgap_usertoboard(brd, addr + offset, ufep, len) == -1 ) {
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
		return;
	}

	/*
	 * If board is a concentrator product, we need to give
	 * it its config string describing how the concentrators look.
	 */
	if ((brd->type == PCX) || (brd->type == PEPC)) {
		uchar string[100];
		uchar *config, *xconfig;
		int i = 0;

		xconfig = dgap_create_config_string(brd, string);

		/* Write string to board memory */
		config = addr + CONFIG;
		for (; i < CONFIGSIZE; i++, config++, xconfig++) {
			writeb(*xconfig, config);
			if ((*xconfig & 0xff) == 0xff)
				break;
		}
	}

	writel(0xbfc01004, (addr + 0xc34));
	writel(0x3, (addr + 0xc30));

	/* change states. */
	brd->state = WAIT_FEP_LOAD;

	DPR_INIT(("dgap_do_fep_load() for board %s : finish\n", brd->name));

}


/*
 * Waits for the FEP to report thats its ready for us to use.
 */
static void dgap_do_wait_for_fep(struct board_t *brd)
{
	uchar *addr;
	u16 word;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return;

	addr = brd->re_map_membase;

	DPR_INIT(("dgap_do_wait_for_fep() for board %s : start. addr: %p\n", brd->name, addr));

	word = readw(addr + FEPSTAT);

	/* Check to see if FEP is up and running now. */
	if (word == *(u16 *) "OS") {
		DPR_INIT(("GOT OS in memory for board %s, moving states.\n", brd->name));
		brd->state = FINISHED_FEP_LOAD;

		/*
		 * Check to see if the board can support FEP5+ commands.
		 */
		word = readw(addr + FEP5_PLUS);
		if (word == *(u16 *) "5A") {
			DPR_INIT(("GOT 5A in memory for board %s, board supports extended FEP5 commands.\n", brd->name));
			brd->bd_flags |= BD_FEP5PLUS;
		}

		return;
	}

	/* Give up on board after too long of time taken */
	if (brd->wait_for_fep++ > 5000) {
		u16 err1 = readw(addr + SEQUENCE);
		u16 err2 = readw(addr + ERROR);
		APR(("***WARNING*** FEPOS for %s not functioning.  Error #(%x,%x).\n",
			brd->name, err1, err2));
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
	}

	DPR_INIT(("dgap_do_wait_for_fep() for board %s : finish\n", brd->name));
}


/*
 * Physically forces the FEP5 card to reset itself.
 */
static void dgap_do_reset_board(struct board_t *brd)
{
	uchar check;
	u32 check1;
	u32 check2;
	int i = 0;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase || !brd->re_map_port) {
		DPR_INIT(("dgap_do_reset_board() start. bad values. brd: %p mem: %p io: %p\n",
			brd, brd ? brd->re_map_membase : 0, brd ? brd->re_map_port : 0));
		return;
	}

	DPR_INIT(("dgap_do_reset_board() start. io: %p\n", brd->re_map_port));

	/* FEPRST does not vary among supported boards */
	writeb(FEPRST, brd->re_map_port);

	for (i = 0; i <= 1000; i++) {
		check = readb(brd->re_map_port) & 0xe;
		if (check == FEPRST)
			break;
		udelay(10);

	}
	if (i > 1000) {
		APR(("*** WARNING *** Board not resetting...  Failing board.\n"));
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
		goto failed;
	}

	/*
	 * Make sure there really is memory out there.
	 */
	writel(0xa55a3cc3, (brd->re_map_membase + LOWMEM));
	writel(0x5aa5c33c, (brd->re_map_membase + HIGHMEM));
	check1 = readl(brd->re_map_membase + LOWMEM);
	check2 = readl(brd->re_map_membase + HIGHMEM);

	if ((check1 != 0xa55a3cc3) || (check2 != 0x5aa5c33c)) {
		APR(("*** Warning *** No memory at %p for board.\n", brd->re_map_membase));
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
		goto failed;
	}

	if (brd->state != BOARD_FAILED)
		brd->state = FINISHED_RESET;

failed:
	DPR_INIT(("dgap_do_reset_board() finish\n"));
}


/*
 * Sends a concentrator image into the FEP5 board.
 */
void dgap_do_conc_load(struct board_t *brd, uchar *uaddr, int len)
{
	char *vaddr;
	u16 offset = 0;
	struct downld_t *to_dp;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return;

	vaddr = brd->re_map_membase;

	offset = readw((u16 *) (vaddr + DOWNREQ));
	to_dp = (struct downld_t *) (vaddr + (int) offset);

	/*
	 * The image was already read into kernel space,
	 * we do NOT need a user space read here
	 */
	memcpy_toio((char *) to_dp, uaddr, sizeof(struct downld_t));

	/* Tell card we have data for it */
	writew(0, vaddr + (DOWNREQ));

	brd->conc_dl_status = NO_PENDING_CONCENTRATOR_REQUESTS;
}


#define EXPANSION_ROM_SIZE	(64 * 1024)
#define FEP5_ROM_MAGIC		(0xFEFFFFFF)

static void dgap_get_vpd(struct board_t *brd)
{
	u32 magic;
	u32 base_offset;
	u16 rom_offset;
	u16 vpd_offset;
	u16 image_length;
	u16 i;
	uchar byte1;
	uchar byte2;

	/*
	 * Poke the magic number at the PCI Rom Address location.
	 * If VPD is supported, the value read from that address
	 * will be non-zero.
	 */
	magic = FEP5_ROM_MAGIC;
	pci_write_config_dword(brd->pdev, PCI_ROM_ADDRESS, magic);
	pci_read_config_dword(brd->pdev, PCI_ROM_ADDRESS, &magic);

	/* VPD not supported, bail */
	if (!magic)
		return;

	/*
	 * To get to the OTPROM memory, we have to send the boards base
	 * address or'ed with 1 into the PCI Rom Address location.
	 */
	magic = brd->membase | 0x01;
	pci_write_config_dword(brd->pdev, PCI_ROM_ADDRESS, magic);
	pci_read_config_dword(brd->pdev, PCI_ROM_ADDRESS, &magic);

	byte1 = readb(brd->re_map_membase);
	byte2 = readb(brd->re_map_membase + 1);

	/*
	 * If the board correctly swapped to the OTPROM memory,
	 * the first 2 bytes (header) should be 0x55, 0xAA
	 */
	if (byte1 == 0x55 && byte2 == 0xAA) {

		base_offset = 0;

		/*
		 * We have to run through all the OTPROM memory looking
		 * for the VPD offset.
		 */
		while (base_offset <= EXPANSION_ROM_SIZE) {

			/*
			 * Lots of magic numbers here.
			 *
			 * The VPD offset is located inside the ROM Data Structure.
			 * We also have to remember the length of each
			 * ROM Data Structure, so we can "hop" to the next
			 * entry if the VPD isn't in the current
			 * ROM Data Structure.
			 */
			rom_offset = readw(brd->re_map_membase + base_offset + 0x18);
			image_length = readw(brd->re_map_membase + rom_offset + 0x10) * 512;
			vpd_offset = readw(brd->re_map_membase + rom_offset + 0x08);

			/* Found the VPD entry */
			if (vpd_offset)
				break;

			/* We didn't find a VPD entry, go to next ROM entry. */
			base_offset += image_length;

			byte1 = readb(brd->re_map_membase + base_offset);
			byte2 = readb(brd->re_map_membase + base_offset + 1);

			/*
			 * If the new ROM offset doesn't have 0x55, 0xAA
			 * as its header, we have run out of ROM.
			 */
			if (byte1 != 0x55 || byte2 != 0xAA)
				break;
		}

		/*
		 * If we have a VPD offset, then mark the board
		 * as having a valid VPD, and copy VPDSIZE (512) bytes of
		 * that VPD to the buffer we have in our board structure.
		 */
		if (vpd_offset) {
			brd->bd_flags |= BD_HAS_VPD;
			for (i = 0; i < VPDSIZE; i++)
				brd->vpd[i] = readb(brd->re_map_membase + vpd_offset + i);
		}
	}

	/*
	 * We MUST poke the magic number at the PCI Rom Address location again.
	 * This makes the card report the regular board memory back to us,
	 * rather than the OTPROM memory.
	 */
	magic = FEP5_ROM_MAGIC;
	pci_write_config_dword(brd->pdev, PCI_ROM_ADDRESS, magic);
}


/*
 * Our board poller function.
 */
void dgap_poll_tasklet(unsigned long data)
{
	struct board_t *bd = (struct board_t *) data;
	ulong  lock_flags;
	ulong  lock_flags2;
	char *vaddr;
	u16 head, tail;
	u16 *chk_addr;
	u16 check = 0;

	if (!bd || (bd->magic != DGAP_BOARD_MAGIC)) {
		APR(("dgap_poll_tasklet() - NULL or bad bd.\n"));
		return;
	}

	if (bd->inhibit_poller)
		return;

	DGAP_LOCK(bd->bd_lock, lock_flags);

	vaddr = bd->re_map_membase;

	/*
	 * If board is ready, parse deeper to see if there is anything to do.
	 */
	if (bd->state == BOARD_READY) {

		struct ev_t *eaddr = NULL;

		if (!bd->re_map_membase) {
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			return;
		}
		if (!bd->re_map_port) {
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			return;
		}

		if (!bd->nasync) {
			goto out;
		}

		/*
		 * If this is a CX or EPCX, we need to see if the firmware
		 * is requesting a concentrator image from us.
		 */
		if ((bd->type == PCX) || (bd->type == PEPC)) {
			chk_addr = (u16 *) (vaddr + DOWNREQ);
			check = readw(chk_addr);
			/* Nonzero if FEP is requesting concentrator image. */
			if (check) {
				if (bd->conc_dl_status == NO_PENDING_CONCENTRATOR_REQUESTS)
					bd->conc_dl_status = NEED_CONCENTRATOR;
				/*
				 * Signal downloader, its got some work to do.
				 */
				DGAP_LOCK(dgap_dl_lock, lock_flags2);
				if (dgap_dl_action != 1) {
					dgap_dl_action = 1;
					wake_up_interruptible(&dgap_dl_wait);
				}
				DGAP_UNLOCK(dgap_dl_lock, lock_flags2);

			}
		}

		eaddr = (struct ev_t *) (vaddr + EVBUF);

		/* Get our head and tail */
		head = readw(&(eaddr->ev_head));
		tail = readw(&(eaddr->ev_tail));

		/*
		 * If there is an event pending. Go service it.
		 */
		if (head != tail) {
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			dgap_event(bd);
			DGAP_LOCK(bd->bd_lock, lock_flags);
		}

out:
		/*
		 * If board is doing interrupts, ACK the interrupt.
		 */
		if (bd && bd->intr_running) {
			readb(bd->re_map_port + 2);
		}

		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return;
	}

	/* Our state machine to get the board up and running */

	/* Reset board */
	if (bd->state == NEED_RESET) {

		/* Get VPD info */
		dgap_get_vpd(bd);

		dgap_do_reset_board(bd);
	}

	/* Move to next state */
	if (bd->state == FINISHED_RESET) {
		bd->state = NEED_CONFIG;
	}

	if (bd->state == NEED_CONFIG) {
		/*
		 * Match this board to a config the user created for us.
		 */
		bd->bd_config = dgap_find_config(bd->type, bd->pci_bus, bd->pci_slot);

		/*
		 * Because the 4 port Xr products share the same PCI ID
		 * as the 8 port Xr products, if we receive a NULL config
		 * back, and this is a PAPORT8 board, retry with a
		 * PAPORT4 attempt as well.
		 */
		if (bd->type == PAPORT8 && !bd->bd_config) {
			bd->bd_config = dgap_find_config(PAPORT4, bd->pci_bus, bd->pci_slot);
		}

		/*
		 * Register the ttys (if any) into the kernel.
		 */
		if (bd->bd_config) {
			bd->state = FINISHED_CONFIG;
		}
		else {
			bd->state = CONFIG_NOT_FOUND;
		}
	}

	/* Move to next state */
	if (bd->state == FINISHED_CONFIG) {
		bd->state = NEED_DEVICE_CREATION;
	}

	/* Move to next state */
	if (bd->state == NEED_DEVICE_CREATION) {
		/*
		 * Signal downloader, its got some work to do.
		 */
		DGAP_LOCK(dgap_dl_lock, lock_flags2);
		if (dgap_dl_action != 1) {
			dgap_dl_action = 1;
			wake_up_interruptible(&dgap_dl_wait);
		}
		DGAP_UNLOCK(dgap_dl_lock, lock_flags2);
	}

	/* Move to next state */
	if (bd->state == FINISHED_DEVICE_CREATION) {
		bd->state = NEED_BIOS_LOAD;
	}

	/* Move to next state */
	if (bd->state == NEED_BIOS_LOAD) {
		/*
		 * Signal downloader, its got some work to do.
		 */
		DGAP_LOCK(dgap_dl_lock, lock_flags2);
		if (dgap_dl_action != 1) {
			dgap_dl_action = 1;
			wake_up_interruptible(&dgap_dl_wait);
		}
		DGAP_UNLOCK(dgap_dl_lock, lock_flags2);
	}

	/* Wait for BIOS to test board... */
	if (bd->state == WAIT_BIOS_LOAD) {
		dgap_do_wait_for_bios(bd);
	}

	/* Move to next state */
	if (bd->state == FINISHED_BIOS_LOAD) {
		bd->state = NEED_FEP_LOAD;

		/*
		 * Signal downloader, its got some work to do.
		 */
		DGAP_LOCK(dgap_dl_lock, lock_flags2);
		if (dgap_dl_action != 1) {
			dgap_dl_action = 1;
			wake_up_interruptible(&dgap_dl_wait);
		}
		DGAP_UNLOCK(dgap_dl_lock, lock_flags2);
	}

	/* Wait for FEP to load on board... */
	if (bd->state == WAIT_FEP_LOAD) {
		dgap_do_wait_for_fep(bd);
	}


	/* Move to next state */
	if (bd->state == FINISHED_FEP_LOAD) {

		/*
		 * Do tty device initialization.
		 */
		int rc = dgap_tty_init(bd);

		if (rc < 0) {
			dgap_tty_uninit(bd);
			APR(("Can't init tty devices (%d)\n", rc));
			bd->state = BOARD_FAILED;
			bd->dpastatus = BD_NOFEP;
		}
		else {
			bd->state = NEED_PROC_CREATION;

			/*
			 * Signal downloader, its got some work to do.
			 */
			DGAP_LOCK(dgap_dl_lock, lock_flags2);
			if (dgap_dl_action != 1) {
				dgap_dl_action = 1;
				wake_up_interruptible(&dgap_dl_wait);
			}
			DGAP_UNLOCK(dgap_dl_lock, lock_flags2);
		}
	}

	/* Move to next state */
	if (bd->state == FINISHED_PROC_CREATION) {

		bd->state = BOARD_READY;
		bd->dpastatus = BD_RUNNING;

		/*
		 * If user requested the board to run in interrupt mode,
		 * go and set it up on the board.
		 */
		if (bd->intr_used) {
			writew(1, (bd->re_map_membase + ENABLE_INTR));
			/*
			 * Tell the board to poll the UARTS as fast as possible.
			 */
			writew(FEPPOLL_MIN, (bd->re_map_membase + FEPPOLL));
			bd->intr_running = 1;
		}

		/* Wake up anyone waiting for board state to change to ready */
		wake_up_interruptible(&bd->state_wait);
	}

	DGAP_UNLOCK(bd->bd_lock, lock_flags);
}


/*=======================================================================
 *
 *      dgap_cmdb - Sends a 2 byte command to the FEP.
 *
 *              ch      - Pointer to channel structure.
 *              cmd     - Command to be sent.
 *              byte1   - Integer containing first byte to be sent.
 *              byte2   - Integer containing second byte to be sent.
 *              ncmds   - Wait until ncmds or fewer cmds are left
 *                        in the cmd buffer before returning.
 *
 *=======================================================================*/
void dgap_cmdb(struct channel_t *ch, uchar cmd, uchar byte1, uchar byte2, uint ncmds)
{
	char		*vaddr = NULL;
	struct cm_t	*cm_addr = NULL;
	uint		count;
	uint		n;
	u16		head;
	u16		tail;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	/*
	 * Check if board is still alive.
	 */
	if (ch->ch_bd->state == BOARD_FAILED) {
		DPR_CORE(("%s:%d board is in failed state.\n", __FILE__, __LINE__));
		return;
	}

	/*
	 * Make sure the pointers are in range before
	 * writing to the FEP memory.
	 */
	vaddr = ch->ch_bd->re_map_membase;

	if (!vaddr)
		return;

	cm_addr = (struct cm_t *) (vaddr + CMDBUF);
	head = readw(&(cm_addr->cm_head));

	/*
	 * Forget it if pointers out of range.
	 */
	if (head >= (CMDMAX - CMDSTART) || (head & 03)) {
		DPR_CORE(("%s:%d pointers out of range, failing board!\n", __FILE__, __LINE__));
		ch->ch_bd->state = BOARD_FAILED;
		return;
	}

	/*
	 * Put the data in the circular command buffer.
	 */
	writeb(cmd, (char *) (vaddr + head + CMDSTART + 0));
	writeb((uchar) ch->ch_portnum, (char *) (vaddr + head + CMDSTART + 1));
	writeb(byte1, (char *) (vaddr + head + CMDSTART + 2));
	writeb(byte2, (char *) (vaddr + head + CMDSTART + 3));

	head = (head + 4) & (CMDMAX - CMDSTART - 4);

	writew(head, &(cm_addr->cm_head));

	/*
	 * Wait if necessary before updating the head
	 * pointer to limit the number of outstanding
	 * commands to the FEP.   If the time spent waiting
	 * is outlandish, declare the FEP dead.
	 */
	for (count = dgap_count ;;) {

		head = readw(&(cm_addr->cm_head));
		tail = readw(&(cm_addr->cm_tail));

		n = (head - tail) & (CMDMAX - CMDSTART - 4);

		if (n <= ncmds * sizeof(struct cm_t))
			break;

		if (--count == 0) {
			DPR_CORE(("%s:%d failing board.\n",__FILE__, __LINE__));
			ch->ch_bd->state = BOARD_FAILED;
			return;
		}
		udelay(10);
	}
}


/*=======================================================================
 *
 *      dgap_cmdw - Sends a 1 word command to the FEP.
 *
 *              ch      - Pointer to channel structure.
 *              cmd     - Command to be sent.
 *              word    - Integer containing word to be sent.
 *              ncmds   - Wait until ncmds or fewer cmds are left
 *                        in the cmd buffer before returning.
 *
 *=======================================================================*/
void dgap_cmdw(struct channel_t *ch, uchar cmd, u16 word, uint ncmds)
{
	char		*vaddr = NULL;
	struct cm_t	*cm_addr = NULL;
	uint		count;
	uint		n;
	u16		head;
	u16		tail;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	/*
	 * Check if board is still alive.
	 */
	if (ch->ch_bd->state == BOARD_FAILED) {
		DPR_CORE(("%s:%d board is failed!\n", __FILE__, __LINE__));
		return;
	}

	/*
	 * Make sure the pointers are in range before
	 * writing to the FEP memory.
	 */
	vaddr = ch->ch_bd->re_map_membase;
	if (!vaddr)
		return;

	cm_addr = (struct cm_t *) (vaddr + CMDBUF);
	head = readw(&(cm_addr->cm_head));

	/*
	 * Forget it if pointers out of range.
	 */
	if (head >= (CMDMAX - CMDSTART) || (head & 03)) {
		DPR_CORE(("%s:%d Pointers out of range.  Failing board.\n",__FILE__, __LINE__));
		ch->ch_bd->state = BOARD_FAILED;
		return;
	}

	/*
	 * Put the data in the circular command buffer.
	 */
	writeb(cmd, (char *) (vaddr + head + CMDSTART + 0));
	writeb((uchar) ch->ch_portnum, (char *) (vaddr + head + CMDSTART + 1));
	writew((u16) word, (char *) (vaddr + head + CMDSTART + 2));

	head = (head + 4) & (CMDMAX - CMDSTART - 4);

	writew(head, &(cm_addr->cm_head));

	/*
	 * Wait if necessary before updating the head
	 * pointer to limit the number of outstanding
	 * commands to the FEP.   If the time spent waiting
	 * is outlandish, declare the FEP dead.
	 */
	for (count = dgap_count ;;) {

		head = readw(&(cm_addr->cm_head));
		tail = readw(&(cm_addr->cm_tail));

		n = (head - tail) & (CMDMAX - CMDSTART - 4);

		if (n <= ncmds * sizeof(struct cm_t))
			break;

		if (--count == 0) {
			DPR_CORE(("%s:%d Failing board.\n",__FILE__, __LINE__));
			ch->ch_bd->state = BOARD_FAILED;
			return;
		}
		udelay(10);
	}
}



/*=======================================================================
 *
 *      dgap_cmdw_ext - Sends a extended word command to the FEP.
 *
 *              ch      - Pointer to channel structure.
 *              cmd     - Command to be sent.
 *              word    - Integer containing word to be sent.
 *              ncmds   - Wait until ncmds or fewer cmds are left
 *                        in the cmd buffer before returning.
 *
 *=======================================================================*/
static void dgap_cmdw_ext(struct channel_t *ch, u16 cmd, u16 word, uint ncmds)
{
	char		*vaddr = NULL;
	struct cm_t	*cm_addr = NULL;
	uint		count;
	uint		n;
	u16		head;
	u16		tail;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	/*
	 * Check if board is still alive.
	 */
	if (ch->ch_bd->state == BOARD_FAILED) {
		DPR_CORE(("%s:%d board is failed!\n", __FILE__, __LINE__));
		return;
	}

	/*
	 * Make sure the pointers are in range before
	 * writing to the FEP memory.
	 */
	vaddr = ch->ch_bd->re_map_membase;
	if (!vaddr)
		return;

	cm_addr = (struct cm_t *) (vaddr + CMDBUF);
	head = readw(&(cm_addr->cm_head));

	/*
	 * Forget it if pointers out of range.
	 */
	if (head >= (CMDMAX - CMDSTART) || (head & 03)) {
		DPR_CORE(("%s:%d Pointers out of range.  Failing board.\n",__FILE__, __LINE__));
		ch->ch_bd->state = BOARD_FAILED;
		return;
	}

	/*
	 * Put the data in the circular command buffer.
	 */

	/* Write an FF to tell the FEP that we want an extended command */
	writeb((uchar) 0xff, (char *) (vaddr + head + CMDSTART + 0));

	writeb((uchar) ch->ch_portnum, (uchar *) (vaddr + head + CMDSTART + 1));
	writew((u16) cmd, (char *) (vaddr + head + CMDSTART + 2));

	/*
	 * If the second part of the command won't fit,
	 * put it at the beginning of the circular buffer.
	 */
	if (((head + 4) >= ((CMDMAX - CMDSTART)) || (head & 03))) {
		writew((u16) word, (char *) (vaddr + CMDSTART));
	} else {
		writew((u16) word, (char *) (vaddr + head + CMDSTART + 4));
	}

	head = (head + 8) & (CMDMAX - CMDSTART - 4);

	writew(head, &(cm_addr->cm_head));

	/*
	 * Wait if necessary before updating the head
	 * pointer to limit the number of outstanding
	 * commands to the FEP.   If the time spent waiting
	 * is outlandish, declare the FEP dead.
	 */
	for (count = dgap_count ;;) {

		head = readw(&(cm_addr->cm_head));
		tail = readw(&(cm_addr->cm_tail));

		n = (head - tail) & (CMDMAX - CMDSTART - 4);

		if (n <= ncmds * sizeof(struct cm_t))
			break;

		if (--count == 0) {
			DPR_CORE(("%s:%d Failing board.\n",__FILE__, __LINE__));
			ch->ch_bd->state = BOARD_FAILED;
			return;
		}
		udelay(10);
	}
}


/*=======================================================================
 *
 *      dgap_wmove - Write data to FEP buffer.
 *
 *              ch      - Pointer to channel structure.
 *              buf     - Poiter to characters to be moved.
 *              cnt     - Number of characters to move.
 *
 *=======================================================================*/
void dgap_wmove(struct channel_t *ch, char *buf, uint cnt)
{
	int    n;
	char   *taddr;
	struct bs_t    *bs;
	u16    head;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	/*
	 * Check parameters.
	 */
	bs   = ch->ch_bs;
	head = readw(&(bs->tx_head));

	/*
	 * If pointers are out of range, just return.
	 */
	if ((cnt > ch->ch_tsize) || (unsigned)(head - ch->ch_tstart) >= ch->ch_tsize) {
		DPR_CORE(("%s:%d pointer out of range", __FILE__, __LINE__));
		return;
	}

	/*
	 * If the write wraps over the top of the circular buffer,
	 * move the portion up to the wrap point, and reset the
	 * pointers to the bottom.
	 */
	n = ch->ch_tstart + ch->ch_tsize - head;

	if (cnt >= n) {
		cnt -= n;
		taddr = ch->ch_taddr + head;
		memcpy_toio(taddr, buf, n);
		head = ch->ch_tstart;
		buf += n;
	}

	/*
	 * Move rest of data.
	 */
	taddr = ch->ch_taddr + head;
	n = cnt;
	memcpy_toio(taddr, buf, n);
	head += cnt;

	writew(head, &(bs->tx_head));
}

/*
 * Retrives the current custom baud rate from FEP memory,
 * and returns it back to the user.
 * Returns 0 on error.
 */
uint dgap_get_custom_baud(struct channel_t *ch)
{
	uchar *vaddr;
	ulong offset = 0;
	uint value = 0;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC) {
		return 0;
	}

	if (!ch->ch_bd || ch->ch_bd->magic != DGAP_BOARD_MAGIC) {
		return 0;
	}

	if (!(ch->ch_bd->bd_flags & BD_FEP5PLUS))
		return 0;

	vaddr = ch->ch_bd->re_map_membase;

	if (!vaddr)
		return 0;

	/*
	 * Go get from fep mem, what the fep
	 * believes the custom baud rate is.
	 */
	offset = ((((*(unsigned short *)(vaddr + ECS_SEG)) << 4) +
		(ch->ch_portnum * 0x28) + LINE_SPEED));

	value = readw(vaddr + offset);
	return value;
}


/*
 * Calls the firmware to reset this channel.
 */
void dgap_firmware_reset_port(struct channel_t *ch)
{
	dgap_cmdb(ch, CHRESET, 0, 0, 0);

	/*
	 * Now that the channel is reset, we need to make sure
	 * all the current settings get reapplied to the port
	 * in the firmware.
	 *
	 * So we will set the driver's cache of firmware
	 * settings all to 0, and then call param.
	 */
	ch->ch_fepiflag = 0;
	ch->ch_fepcflag = 0;
	ch->ch_fepoflag = 0;
	ch->ch_fepstartc = 0;
	ch->ch_fepstopc = 0;
	ch->ch_fepastartc = 0;
	ch->ch_fepastopc = 0;
	ch->ch_mostat = 0;
	ch->ch_hflow = 0;
}


/*=======================================================================
 *
 *      dgap_param - Set Digi parameters.
 *
 *              struct tty_struct *     - TTY for port.
 *
 *=======================================================================*/
int dgap_param(struct tty_struct *tty)
{
	struct ktermios *ts;
	struct board_t *bd;
	struct channel_t *ch;
	struct bs_t   *bs;
	struct un_t   *un;
	u16	head;
	u16	cflag;
	u16	iflag;
	uchar	mval;
	uchar	hflow;

	if (!tty || tty->magic != TTY_MAGIC)
		return -ENXIO;

	un = (struct un_t *) tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -ENXIO;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -ENXIO;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return -ENXIO;

	bs = ch->ch_bs;
	if (!bs)
		return -ENXIO;

	DPR_PARAM(("param start: tdev: %x cflags: %x oflags: %x iflags: %x\n",
		ch->ch_tun.un_dev, ch->ch_c_cflag, ch->ch_c_oflag, ch->ch_c_iflag));

	ts = &tty->termios;

	/*
	 * If baud rate is zero, flush queues, and set mval to drop DTR.
	 */
	if ((ch->ch_c_cflag & (CBAUD)) == 0) {

		/* flush rx */
		head = readw(&(ch->ch_bs->rx_head));
		writew(head, &(ch->ch_bs->rx_tail));

		/* flush tx */
		head = readw(&(ch->ch_bs->tx_head));
		writew(head, &(ch->ch_bs->tx_tail));

		ch->ch_flags |= (CH_BAUD0);

		/* Drop RTS and DTR */
		ch->ch_mval &= ~(D_RTS(ch)|D_DTR(ch));
		mval = D_DTR(ch) | D_RTS(ch);
		ch->ch_baud_info = 0;

	} else if (ch->ch_custom_speed && (bd->bd_flags & BD_FEP5PLUS)) {
		/*
		 * Tell the fep to do the command
		 */

		DPR_PARAM(("param: Want %d speed\n", ch->ch_custom_speed));

		dgap_cmdw_ext(ch, 0xff01, ch->ch_custom_speed, 0);

		/*
		 * Now go get from fep mem, what the fep
		 * believes the custom baud rate is.
		 */
		ch->ch_baud_info = ch->ch_custom_speed = dgap_get_custom_baud(ch);

		DPR_PARAM(("param: Got %d speed\n", ch->ch_custom_speed));

		/* Handle transition from B0 */
		if (ch->ch_flags & CH_BAUD0) {
			ch->ch_flags &= ~(CH_BAUD0);
			ch->ch_mval |= (D_RTS(ch)|D_DTR(ch));
		}
		mval = D_DTR(ch) | D_RTS(ch);

	} else {
		/*
		 * Set baud rate, character size, and parity.
		 */


		int iindex = 0;
		int jindex = 0;
		int baud = 0;

		ulong bauds[4][16] = {
			{ /* slowbaud */
				0,	50,	75,	110,
				134,	150,	200,	300,
				600,	1200,	1800,	2400,
				4800,	9600,	19200,	38400 },
			{ /* slowbaud & CBAUDEX */
				0,	57600,	115200,	230400,
				460800,	150,	200,	921600,
				600,	1200,	1800,	2400,
				4800,	9600,	19200,	38400 },
			{ /* fastbaud */
				0,	57600,	76800,	115200,
				14400,	57600,	230400,	76800,
				115200,	230400,	28800,	460800,
				921600,	9600,	19200,	38400 },
			{ /* fastbaud & CBAUDEX */
				0,	57600,	115200,	230400,
				460800,	150,	200,	921600,
				600,	1200,	1800,	2400,
				4800,	9600,	19200,	38400 }
		};

		/* Only use the TXPrint baud rate if the terminal unit is NOT open */
		if (!(ch->ch_tun.un_flags & UN_ISOPEN) && (un->un_type == DGAP_PRINT))
			baud = C_BAUD(ch->ch_pun.un_tty) & 0xff;
		else
			baud = C_BAUD(ch->ch_tun.un_tty) & 0xff;

		if (ch->ch_c_cflag & CBAUDEX)
			iindex = 1;

		if (ch->ch_digi.digi_flags & DIGI_FAST)
			iindex += 2;

		jindex = baud;

		if ((iindex >= 0) && (iindex < 4) && (jindex >= 0) && (jindex < 16)) {
			baud = bauds[iindex][jindex];
		} else {
			DPR_IOCTL(("baud indices were out of range (%d)(%d)",
				iindex, jindex));
			baud = 0;
		}

		if (baud == 0)
			baud = 9600;

		ch->ch_baud_info = baud;


		/*
		 * CBAUD has bit position 0x1000 set these days to indicate Linux
		 * baud rate remap.
		 * We use a different bit assignment for high speed.  Clear this
		 * bit out while grabbing the parts of "cflag" we want.
		 */
		cflag = ch->ch_c_cflag & ((CBAUD ^ CBAUDEX) | PARODD | PARENB | CSTOPB | CSIZE);

		/*
		 * HUPCL bit is used by FEP to indicate fast baud
		 * table is to be used.
		 */
		if ((ch->ch_digi.digi_flags & DIGI_FAST) || (ch->ch_c_cflag & CBAUDEX))
			cflag |= HUPCL;


		if ((ch->ch_c_cflag & CBAUDEX) && !(ch->ch_digi.digi_flags & DIGI_FAST)) {
		/*
		 * The below code is trying to guarantee that only baud rates
		 * 115200, 230400, 460800, 921600 are remapped.  We use exclusive or
		 * because the various baud rates share common bit positions
		 * and therefore can't be tested for easily.
		 */
			tcflag_t tcflag = (ch->ch_c_cflag & CBAUD) | CBAUDEX;
			int baudpart = 0;

			/* Map high speed requests to index into FEP's baud table */
			switch (tcflag) {
			case B57600 :
				baudpart = 1;
				break;
#ifdef B76800
			case B76800 :
				baudpart = 2;
				break;
#endif
			case B115200 :
				baudpart = 3;
				break;
			case B230400 :
				baudpart = 9;
				break;
			case B460800 :
				baudpart = 11;
				break;
#ifdef B921600
			case B921600 :
				baudpart = 12;
				break;
#endif
			default:
				baudpart = 0;
			}

			if (baudpart)
				cflag = (cflag & ~(CBAUD | CBAUDEX)) | baudpart;
		}

		cflag &= 0xffff;

		if (cflag != ch->ch_fepcflag) {
			ch->ch_fepcflag = (u16) (cflag & 0xffff);

			/* Okay to have channel and board locks held calling this */
			dgap_cmdw(ch, SCFLAG, (u16) cflag, 0);
		}

		/* Handle transition from B0 */
		if (ch->ch_flags & CH_BAUD0) {
			ch->ch_flags &= ~(CH_BAUD0);
			ch->ch_mval |= (D_RTS(ch)|D_DTR(ch));
		}
		mval = D_DTR(ch) | D_RTS(ch);
	}

	/*
	 * Get input flags.
	 */
	iflag = ch->ch_c_iflag & (IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK | ISTRIP | IXON | IXANY | IXOFF);

	if ((ch->ch_startc == _POSIX_VDISABLE) || (ch->ch_stopc == _POSIX_VDISABLE)) {
		iflag &= ~(IXON | IXOFF);
		ch->ch_c_iflag &= ~(IXON | IXOFF);
	}

	/*
	 * Only the IBM Xr card can switch between
	 * 232 and 422 modes on the fly
	 */
	if (bd->device == PCI_DEVICE_XR_IBM_DID) {
		if (ch->ch_digi.digi_flags & DIGI_422)
			dgap_cmdb(ch, SCOMMODE, MODE_422, 0, 0);
		else
			dgap_cmdb(ch, SCOMMODE, MODE_232, 0, 0);
	}

	if (ch->ch_digi.digi_flags & DIGI_ALTPIN)
		iflag |= IALTPIN ;

	if (iflag != ch->ch_fepiflag) {
		ch->ch_fepiflag = iflag;

		/* Okay to have channel and board locks held calling this */
		dgap_cmdw(ch, SIFLAG, (u16) ch->ch_fepiflag, 0);
	}

	/*
	 * Select hardware handshaking.
	 */
	hflow = 0;

	if (ch->ch_c_cflag & CRTSCTS) {
		hflow |= (D_RTS(ch) | D_CTS(ch));
	}
	if (ch->ch_digi.digi_flags & RTSPACE)
		hflow |= D_RTS(ch);
	if (ch->ch_digi.digi_flags & DTRPACE)
		hflow |= D_DTR(ch);
	if (ch->ch_digi.digi_flags & CTSPACE)
		hflow |= D_CTS(ch);
	if (ch->ch_digi.digi_flags & DSRPACE)
		hflow |= D_DSR(ch);
	if (ch->ch_digi.digi_flags & DCDPACE)
		hflow |= D_CD(ch);

	if (hflow != ch->ch_hflow) {
		ch->ch_hflow = hflow;

		/* Okay to have channel and board locks held calling this */
		dgap_cmdb(ch, SHFLOW, (uchar) hflow, 0xff, 0);
	}


	/*
	 * Set RTS and/or DTR Toggle if needed, but only if product is FEP5+ based.
	 */
	if (bd->bd_flags & BD_FEP5PLUS) {
		u16 hflow2 = 0;
		if (ch->ch_digi.digi_flags & DIGI_RTS_TOGGLE) {
			hflow2 |= (D_RTS(ch));
		}
		if (ch->ch_digi.digi_flags & DIGI_DTR_TOGGLE) {
			hflow2 |= (D_DTR(ch));
		}

		dgap_cmdw_ext(ch, 0xff03, hflow2, 0);
	}

	/*
	 * Set modem control lines.
	 */

	mval ^= ch->ch_mforce & (mval ^ ch->ch_mval);

	DPR_PARAM(("dgap_param: mval: %x ch_mforce: %x ch_mval: %x ch_mostat: %x\n",
		mval, ch->ch_mforce, ch->ch_mval, ch->ch_mostat));

	if (ch->ch_mostat ^ mval) {
		ch->ch_mostat = mval;

		/* Okay to have channel and board locks held calling this */
		DPR_PARAM(("dgap_param: Sending SMODEM mval: %x\n", mval));
		dgap_cmdb(ch, SMODEM, (uchar) mval, D_RTS(ch)|D_DTR(ch), 0);
	}

	/*
	 * Read modem signals, and then call carrier function.
	 */
	ch->ch_mistat = readb(&(bs->m_stat));
	dgap_carrier(ch);

	/*
	 * Set the start and stop characters.
	 */
	if (ch->ch_startc != ch->ch_fepstartc || ch->ch_stopc != ch->ch_fepstopc) {
		ch->ch_fepstartc = ch->ch_startc;
		ch->ch_fepstopc =  ch->ch_stopc;

		/* Okay to have channel and board locks held calling this */
		dgap_cmdb(ch, SFLOWC, ch->ch_fepstartc, ch->ch_fepstopc, 0);
	}

	/*
	 * Set the Auxiliary start and stop characters.
	 */
	if (ch->ch_astartc != ch->ch_fepastartc || ch->ch_astopc != ch->ch_fepastopc) {
		ch->ch_fepastartc = ch->ch_astartc;
		ch->ch_fepastopc = ch->ch_astopc;

		/* Okay to have channel and board locks held calling this */
		dgap_cmdb(ch, SAFLOWC, ch->ch_fepastartc, ch->ch_fepastopc, 0);
	}

	DPR_PARAM(("param finish\n"));

	return 0;
}


/*
 * dgap_parity_scan()
 *
 * Convert the FEP5 way of reporting parity errors and breaks into
 * the Linux line discipline way.
 */
void dgap_parity_scan(struct channel_t *ch, unsigned char *cbuf, unsigned char *fbuf, int *len)
{
	int l = *len;
	int count = 0;
	unsigned char *in, *cout, *fout;
	unsigned char c;

	in = cbuf;
	cout = cbuf;
	fout = fbuf;

	DPR_PSCAN(("dgap_parity_scan start\n"));

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	while (l--) {
		c = *in++;
		switch (ch->pscan_state) {
		default:
			/* reset to sanity and fall through */
			ch->pscan_state = 0;

		case 0:
			/* No FF seen yet */
			if (c == (unsigned char) '\377') {
				/* delete this character from stream */
				ch->pscan_state = 1;
			} else {
				*cout++ = c;
				*fout++ = TTY_NORMAL;
				count += 1;
			}
			break;

		case 1:
			/* first FF seen */
			if (c == (unsigned char) '\377') {
				/* doubled ff, transform to single ff */
				*cout++ = c;
				*fout++ = TTY_NORMAL;
				count += 1;
				ch->pscan_state = 0;
			} else {
				/* save value examination in next state */
				ch->pscan_savechar = c;
				ch->pscan_state = 2;
			}
			break;

		case 2:
			/* third character of ff sequence */

			*cout++ = c;

			if (ch->pscan_savechar == 0x0) {

				if (c == 0x0) {
					DPR_PSCAN(("dgap_parity_scan in 3rd char of ff seq. c: %x setting break.\n", c));
					ch->ch_err_break++;
					*fout++ = TTY_BREAK;
				}
				else {
					DPR_PSCAN(("dgap_parity_scan in 3rd char of ff seq. c: %x setting parity.\n", c));
					ch->ch_err_parity++;
					*fout++ = TTY_PARITY;
				}
			}
			else {
				DPR_PSCAN(("%s:%d Logic Error.\n", __FILE__, __LINE__));
			}

			count += 1;
			ch->pscan_state = 0;
		}
	}
	*len = count;
	DPR_PSCAN(("dgap_parity_scan finish\n"));
}




/*=======================================================================
 *
 *      dgap_event - FEP to host event processing routine.
 *
 *              bd     - Board of current event.
 *
 *=======================================================================*/
static int dgap_event(struct board_t *bd)
{
	struct channel_t *ch;
	ulong		lock_flags;
	ulong		lock_flags2;
	struct bs_t	*bs;
	uchar		*event;
	uchar		*vaddr = NULL;
	struct ev_t	*eaddr = NULL;
	uint		head;
	uint		tail;
	int		port;
	int		reason;
	int		modem;
	int		b1;

	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return -ENXIO;

	DGAP_LOCK(bd->bd_lock, lock_flags);

	vaddr = bd->re_map_membase;

	if (!vaddr) {
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return -ENXIO;
	}

	eaddr = (struct ev_t *) (vaddr + EVBUF);

	/* Get our head and tail */
	head = readw(&(eaddr->ev_head));
	tail = readw(&(eaddr->ev_tail));

	/*
	 * Forget it if pointers out of range.
	 */

	if (head >= EVMAX - EVSTART || tail >= EVMAX - EVSTART ||
	    (head | tail) & 03) {
		DPR_EVENT(("should be calling xxfail %d\n", __LINE__));
		/* Let go of board lock */
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return -ENXIO;
	}

	/*
	 * Loop to process all the events in the buffer.
	 */
	while (tail != head) {

		/*
		 * Get interrupt information.
		 */

		event = bd->re_map_membase + tail + EVSTART;

		port   = event[0];
		reason = event[1];
		modem  = event[2];
		b1     = event[3];

		DPR_EVENT(("event: jiffies: %ld port: %d reason: %x modem: %x\n",
			jiffies, port, reason, modem));

		/*
		 * Make sure the interrupt is valid.
		 */
		if (port >= bd->nasync)
			goto next;

		if (!(reason & (IFMODEM | IFBREAK | IFTLW | IFTEM | IFDATA))) {
			goto next;
		}

		ch = bd->channels[port];

		if (!ch || ch->magic != DGAP_CHANNEL_MAGIC) {
			goto next;
		}

		/*
		 * If we have made it here, the event was valid.
		 * Lock down the channel.
		 */
		DGAP_LOCK(ch->ch_lock, lock_flags2);

		bs = ch->ch_bs;

		if (!bs) {
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			goto next;
		}

		/*
		 * Process received data.
		 */
		if (reason & IFDATA) {

			/*
			 * ALL LOCKS *MUST* BE DROPPED BEFORE CALLING INPUT!
			 * input could send some data to ld, which in turn
			 * could do a callback to one of our other functions.
			 */
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);

			dgap_input(ch);

			DGAP_LOCK(bd->bd_lock, lock_flags);
			DGAP_LOCK(ch->ch_lock, lock_flags2);

			if (ch->ch_flags & CH_RACTIVE)
				ch->ch_flags |= CH_RENABLE;
			else
				writeb(1, &(bs->idata));

			if (ch->ch_flags & CH_RWAIT) {
				ch->ch_flags &= ~CH_RWAIT;

				wake_up_interruptible(&ch->ch_tun.un_flags_wait);
			}
		}

		/*
		 * Process Modem change signals.
		 */
		if (reason & IFMODEM) {
			ch->ch_mistat = modem;
			dgap_carrier(ch);
		}

		/*
		 * Process break.
		 */
		if (reason & IFBREAK) {

			DPR_EVENT(("got IFBREAK\n"));

			if (ch->ch_tun.un_tty) {
				/* A break has been indicated */
				ch->ch_err_break++;
				tty_buffer_request_room(ch->ch_tun.un_tty->port, 1);
				tty_insert_flip_char(ch->ch_tun.un_tty->port, 0, TTY_BREAK);
				tty_flip_buffer_push(ch->ch_tun.un_tty->port);
			}
		}

		/*
		 * Process Transmit low.
		 */
		if (reason & IFTLW) {

			DPR_EVENT(("event: got low event\n"));

			if (ch->ch_tun.un_flags & UN_LOW) {
				ch->ch_tun.un_flags &= ~UN_LOW;

				if (ch->ch_tun.un_flags & UN_ISOPEN) {
					if ((ch->ch_tun.un_tty->flags &
					   (1 << TTY_DO_WRITE_WAKEUP)) &&
						ch->ch_tun.un_tty->ldisc->ops->write_wakeup)
					{
						DGAP_UNLOCK(ch->ch_lock, lock_flags2);
						DGAP_UNLOCK(bd->bd_lock, lock_flags);
						(ch->ch_tun.un_tty->ldisc->ops->write_wakeup)(ch->ch_tun.un_tty);
						DGAP_LOCK(bd->bd_lock, lock_flags);
						DGAP_LOCK(ch->ch_lock, lock_flags2);
					}
					wake_up_interruptible(&ch->ch_tun.un_tty->write_wait);
					wake_up_interruptible(&ch->ch_tun.un_flags_wait);

					DPR_EVENT(("event: Got low event. jiffies: %lu\n", jiffies));
				}
			}

			if (ch->ch_pun.un_flags & UN_LOW) {
				ch->ch_pun.un_flags &= ~UN_LOW;
				if (ch->ch_pun.un_flags & UN_ISOPEN) {
					if ((ch->ch_pun.un_tty->flags &
					   (1 << TTY_DO_WRITE_WAKEUP)) &&
						ch->ch_pun.un_tty->ldisc->ops->write_wakeup)
					{
						DGAP_UNLOCK(ch->ch_lock, lock_flags2);
						DGAP_UNLOCK(bd->bd_lock, lock_flags);
						(ch->ch_pun.un_tty->ldisc->ops->write_wakeup)(ch->ch_pun.un_tty);
						DGAP_LOCK(bd->bd_lock, lock_flags);
						DGAP_LOCK(ch->ch_lock, lock_flags2);
					}
					wake_up_interruptible(&ch->ch_pun.un_tty->write_wait);
					wake_up_interruptible(&ch->ch_pun.un_flags_wait);
				}
			}

			if (ch->ch_flags & CH_WLOW) {
				ch->ch_flags &= ~CH_WLOW;
				wake_up_interruptible(&ch->ch_flags_wait);
			}
		}

		/*
		 * Process Transmit empty.
		 */
		if (reason & IFTEM) {
			DPR_EVENT(("event: got empty event\n"));

			if (ch->ch_tun.un_flags & UN_EMPTY) {
				ch->ch_tun.un_flags &= ~UN_EMPTY;
				if (ch->ch_tun.un_flags & UN_ISOPEN) {
					if ((ch->ch_tun.un_tty->flags &
					   (1 << TTY_DO_WRITE_WAKEUP)) &&
						ch->ch_tun.un_tty->ldisc->ops->write_wakeup)
					{
						DGAP_UNLOCK(ch->ch_lock, lock_flags2);
						DGAP_UNLOCK(bd->bd_lock, lock_flags);

						(ch->ch_tun.un_tty->ldisc->ops->write_wakeup)(ch->ch_tun.un_tty);
						DGAP_LOCK(bd->bd_lock, lock_flags);
						DGAP_LOCK(ch->ch_lock, lock_flags2);
					}
					wake_up_interruptible(&ch->ch_tun.un_tty->write_wait);
					wake_up_interruptible(&ch->ch_tun.un_flags_wait);
				}
			}

			if (ch->ch_pun.un_flags & UN_EMPTY) {
				ch->ch_pun.un_flags &= ~UN_EMPTY;
				if (ch->ch_pun.un_flags & UN_ISOPEN) {
					if ((ch->ch_pun.un_tty->flags &
					   (1 << TTY_DO_WRITE_WAKEUP)) &&
						ch->ch_pun.un_tty->ldisc->ops->write_wakeup)
					{
						DGAP_UNLOCK(ch->ch_lock, lock_flags2);
						DGAP_UNLOCK(bd->bd_lock, lock_flags);
						(ch->ch_pun.un_tty->ldisc->ops->write_wakeup)(ch->ch_pun.un_tty);
						DGAP_LOCK(bd->bd_lock, lock_flags);
						DGAP_LOCK(ch->ch_lock, lock_flags2);
					}
					wake_up_interruptible(&ch->ch_pun.un_tty->write_wait);
					wake_up_interruptible(&ch->ch_pun.un_flags_wait);
				}
			}


			if (ch->ch_flags & CH_WEMPTY) {
				ch->ch_flags &= ~CH_WEMPTY;
				wake_up_interruptible(&ch->ch_flags_wait);
			}
		}

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);

next:
		tail = (tail + 4) & (EVMAX - EVSTART - 4);
	}

	writew(tail, &(eaddr->ev_tail));
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	return 0;
}

static ssize_t dgap_driver_version_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", DG_PART);
}
static DRIVER_ATTR(version, S_IRUSR, dgap_driver_version_show, NULL);


static ssize_t dgap_driver_boards_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", dgap_NumBoards);
}
static DRIVER_ATTR(boards, S_IRUSR, dgap_driver_boards_show, NULL);


static ssize_t dgap_driver_maxboards_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", MAXBOARDS);
}
static DRIVER_ATTR(maxboards, S_IRUSR, dgap_driver_maxboards_show, NULL);


static ssize_t dgap_driver_pollcounter_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%ld\n", dgap_poll_counter);
}
static DRIVER_ATTR(pollcounter, S_IRUSR, dgap_driver_pollcounter_show, NULL);


static ssize_t dgap_driver_state_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", dgap_driver_state_text[dgap_driver_state]);
}
static DRIVER_ATTR(state, S_IRUSR, dgap_driver_state_show, NULL);


static ssize_t dgap_driver_debug_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n", dgap_debug);
}

static ssize_t dgap_driver_debug_store(struct device_driver *ddp, const char *buf, size_t count)
{
	sscanf(buf, "0x%x\n", &dgap_debug);
	return count;
}
static DRIVER_ATTR(debug, (S_IRUSR | S_IWUSR), dgap_driver_debug_show, dgap_driver_debug_store);


static ssize_t dgap_driver_rawreadok_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n", dgap_rawreadok);
}

static ssize_t dgap_driver_rawreadok_store(struct device_driver *ddp, const char *buf, size_t count)
{
	sscanf(buf, "0x%x\n", &dgap_rawreadok);
	return count;
}
static DRIVER_ATTR(rawreadok, (S_IRUSR | S_IWUSR), dgap_driver_rawreadok_show, dgap_driver_rawreadok_store);


static ssize_t dgap_driver_pollrate_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%dms\n", dgap_poll_tick);
}

static ssize_t dgap_driver_pollrate_store(struct device_driver *ddp, const char *buf, size_t count)
{
	sscanf(buf, "%d\n", &dgap_poll_tick);
	return count;
}
static DRIVER_ATTR(pollrate, (S_IRUSR | S_IWUSR), dgap_driver_pollrate_show, dgap_driver_pollrate_store);


void dgap_create_driver_sysfiles(struct pci_driver *dgap_driver)
{
	int rc = 0;
	struct device_driver *driverfs = &dgap_driver->driver;

	rc |= driver_create_file(driverfs, &driver_attr_version);
	rc |= driver_create_file(driverfs, &driver_attr_boards);
	rc |= driver_create_file(driverfs, &driver_attr_maxboards);
	rc |= driver_create_file(driverfs, &driver_attr_debug);
	rc |= driver_create_file(driverfs, &driver_attr_rawreadok);
	rc |= driver_create_file(driverfs, &driver_attr_pollrate);
	rc |= driver_create_file(driverfs, &driver_attr_pollcounter);
	rc |= driver_create_file(driverfs, &driver_attr_state);
	if (rc) {
		printk(KERN_ERR "DGAP: sysfs driver_create_file failed!\n");
	}
}


void dgap_remove_driver_sysfiles(struct pci_driver *dgap_driver)
{
	struct device_driver *driverfs = &dgap_driver->driver;
	driver_remove_file(driverfs, &driver_attr_version);
	driver_remove_file(driverfs, &driver_attr_boards);
	driver_remove_file(driverfs, &driver_attr_maxboards);
	driver_remove_file(driverfs, &driver_attr_debug);
	driver_remove_file(driverfs, &driver_attr_rawreadok);
	driver_remove_file(driverfs, &driver_attr_pollrate);
	driver_remove_file(driverfs, &driver_attr_pollcounter);
	driver_remove_file(driverfs, &driver_attr_state);
}


#define DGAP_VERIFY_BOARD(p, bd)			\
	if (!p)						\
		return (0);				\
							\
	bd = dev_get_drvdata(p);			\
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)	\
		return (0);				\
	if (bd->state != BOARD_READY)			\
		return (0);				\


static ssize_t dgap_ports_state_show(struct device *p, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i = 0;

	DGAP_VERIFY_BOARD(p, bd);

	for (i = 0; i < bd->nasync; i++) {
		count += snprintf(buf + count, PAGE_SIZE - count,
			"%d %s\n", bd->channels[i]->ch_portnum,
			bd->channels[i]->ch_open_count ? "Open" : "Closed");
	}
	return count;
}
static DEVICE_ATTR(ports_state, S_IRUSR, dgap_ports_state_show, NULL);


static ssize_t dgap_ports_baud_show(struct device *p, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i = 0;

	DGAP_VERIFY_BOARD(p, bd);

	for (i = 0; i < bd->nasync; i++) {
		count +=  snprintf(buf + count, PAGE_SIZE - count,
			"%d %d\n", bd->channels[i]->ch_portnum, bd->channels[i]->ch_baud_info);
	}
	return count;
}
static DEVICE_ATTR(ports_baud, S_IRUSR, dgap_ports_baud_show, NULL);


static ssize_t dgap_ports_msignals_show(struct device *p, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i = 0;

	DGAP_VERIFY_BOARD(p, bd);

	for (i = 0; i < bd->nasync; i++) {
		if (bd->channels[i]->ch_open_count) {
			count += snprintf(buf + count, PAGE_SIZE - count,
				"%d %s %s %s %s %s %s\n", bd->channels[i]->ch_portnum,
				(bd->channels[i]->ch_mostat & UART_MCR_RTS) ? "RTS" : "",
				(bd->channels[i]->ch_mistat & UART_MSR_CTS) ? "CTS" : "",
				(bd->channels[i]->ch_mostat & UART_MCR_DTR) ? "DTR" : "",
				(bd->channels[i]->ch_mistat & UART_MSR_DSR) ? "DSR" : "",
				(bd->channels[i]->ch_mistat & UART_MSR_DCD) ? "DCD" : "",
				(bd->channels[i]->ch_mistat & UART_MSR_RI)  ? "RI"  : "");
		} else {
			count += snprintf(buf + count, PAGE_SIZE - count,
				"%d\n", bd->channels[i]->ch_portnum);
		}
	}
	return count;
}
static DEVICE_ATTR(ports_msignals, S_IRUSR, dgap_ports_msignals_show, NULL);


static ssize_t dgap_ports_iflag_show(struct device *p, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i = 0;

	DGAP_VERIFY_BOARD(p, bd);

	for (i = 0; i < bd->nasync; i++) {
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %x\n",
			bd->channels[i]->ch_portnum, bd->channels[i]->ch_c_iflag);
	}
	return count;
}
static DEVICE_ATTR(ports_iflag, S_IRUSR, dgap_ports_iflag_show, NULL);


static ssize_t dgap_ports_cflag_show(struct device *p, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i = 0;

	DGAP_VERIFY_BOARD(p, bd);

	for (i = 0; i < bd->nasync; i++) {
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %x\n",
			bd->channels[i]->ch_portnum, bd->channels[i]->ch_c_cflag);
	}
	return count;
}
static DEVICE_ATTR(ports_cflag, S_IRUSR, dgap_ports_cflag_show, NULL);


static ssize_t dgap_ports_oflag_show(struct device *p, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i = 0;

	DGAP_VERIFY_BOARD(p, bd);

	for (i = 0; i < bd->nasync; i++) {
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %x\n",
			bd->channels[i]->ch_portnum, bd->channels[i]->ch_c_oflag);
	}
	return count;
}
static DEVICE_ATTR(ports_oflag, S_IRUSR, dgap_ports_oflag_show, NULL);


static ssize_t dgap_ports_lflag_show(struct device *p, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i = 0;

	DGAP_VERIFY_BOARD(p, bd);

	for (i = 0; i < bd->nasync; i++) {
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %x\n",
			bd->channels[i]->ch_portnum, bd->channels[i]->ch_c_lflag);
	}
	return count;
}
static DEVICE_ATTR(ports_lflag, S_IRUSR, dgap_ports_lflag_show, NULL);


static ssize_t dgap_ports_digi_flag_show(struct device *p, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i = 0;

	DGAP_VERIFY_BOARD(p, bd);

	for (i = 0; i < bd->nasync; i++) {
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %x\n",
			bd->channels[i]->ch_portnum, bd->channels[i]->ch_digi.digi_flags);
	}
	return count;
}
static DEVICE_ATTR(ports_digi_flag, S_IRUSR, dgap_ports_digi_flag_show, NULL);


static ssize_t dgap_ports_rxcount_show(struct device *p, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i = 0;

	DGAP_VERIFY_BOARD(p, bd);

	for (i = 0; i < bd->nasync; i++) {
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %ld\n",
			bd->channels[i]->ch_portnum, bd->channels[i]->ch_rxcount);
	}
	return count;
}
static DEVICE_ATTR(ports_rxcount, S_IRUSR, dgap_ports_rxcount_show, NULL);


static ssize_t dgap_ports_txcount_show(struct device *p, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i = 0;

	DGAP_VERIFY_BOARD(p, bd);

	for (i = 0; i < bd->nasync; i++) {
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %ld\n",
			bd->channels[i]->ch_portnum, bd->channels[i]->ch_txcount);
	}
	return count;
}
static DEVICE_ATTR(ports_txcount, S_IRUSR, dgap_ports_txcount_show, NULL);


/* this function creates the sys files that will export each signal status
 * to sysfs each value will be put in a separate filename
 */
void dgap_create_ports_sysfiles(struct board_t *bd)
{
	int rc = 0;

	dev_set_drvdata(&bd->pdev->dev, bd);
	rc |= device_create_file(&(bd->pdev->dev), &dev_attr_ports_state);
	rc |= device_create_file(&(bd->pdev->dev), &dev_attr_ports_baud);
	rc |= device_create_file(&(bd->pdev->dev), &dev_attr_ports_msignals);
	rc |= device_create_file(&(bd->pdev->dev), &dev_attr_ports_iflag);
	rc |= device_create_file(&(bd->pdev->dev), &dev_attr_ports_cflag);
	rc |= device_create_file(&(bd->pdev->dev), &dev_attr_ports_oflag);
	rc |= device_create_file(&(bd->pdev->dev), &dev_attr_ports_lflag);
	rc |= device_create_file(&(bd->pdev->dev), &dev_attr_ports_digi_flag);
	rc |= device_create_file(&(bd->pdev->dev), &dev_attr_ports_rxcount);
	rc |= device_create_file(&(bd->pdev->dev), &dev_attr_ports_txcount);
	if (rc) {
		printk(KERN_ERR "DGAP: sysfs device_create_file failed!\n");
	}
}


/* removes all the sys files created for that port */
void dgap_remove_ports_sysfiles(struct board_t *bd)
{
	device_remove_file(&(bd->pdev->dev), &dev_attr_ports_state);
	device_remove_file(&(bd->pdev->dev), &dev_attr_ports_baud);
	device_remove_file(&(bd->pdev->dev), &dev_attr_ports_msignals);
	device_remove_file(&(bd->pdev->dev), &dev_attr_ports_iflag);
	device_remove_file(&(bd->pdev->dev), &dev_attr_ports_cflag);
	device_remove_file(&(bd->pdev->dev), &dev_attr_ports_oflag);
	device_remove_file(&(bd->pdev->dev), &dev_attr_ports_lflag);
	device_remove_file(&(bd->pdev->dev), &dev_attr_ports_digi_flag);
	device_remove_file(&(bd->pdev->dev), &dev_attr_ports_rxcount);
	device_remove_file(&(bd->pdev->dev), &dev_attr_ports_txcount);
}


static ssize_t dgap_tty_state_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return (0);
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (0);
	if (bd->state != BOARD_READY)
		return (0);

	return snprintf(buf, PAGE_SIZE, "%s", un->un_open_count ? "Open" : "Closed");
}
static DEVICE_ATTR(state, S_IRUSR, dgap_tty_state_show, NULL);


static ssize_t dgap_tty_baud_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return (0);
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (0);
	if (bd->state != BOARD_READY)
		return (0);

	return snprintf(buf, PAGE_SIZE, "%d\n", ch->ch_baud_info);
}
static DEVICE_ATTR(baud, S_IRUSR, dgap_tty_baud_show, NULL);


static ssize_t dgap_tty_msignals_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return (0);
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (0);
	if (bd->state != BOARD_READY)
		return (0);

	if (ch->ch_open_count) {
		return snprintf(buf, PAGE_SIZE, "%s %s %s %s %s %s\n",
			(ch->ch_mostat & UART_MCR_RTS) ? "RTS" : "",
			(ch->ch_mistat & UART_MSR_CTS) ? "CTS" : "",
			(ch->ch_mostat & UART_MCR_DTR) ? "DTR" : "",
			(ch->ch_mistat & UART_MSR_DSR) ? "DSR" : "",
			(ch->ch_mistat & UART_MSR_DCD) ? "DCD" : "",
			(ch->ch_mistat & UART_MSR_RI)  ? "RI"  : "");
	}
	return 0;
}
static DEVICE_ATTR(msignals, S_IRUSR, dgap_tty_msignals_show, NULL);


static ssize_t dgap_tty_iflag_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return (0);
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (0);
	if (bd->state != BOARD_READY)
		return (0);

	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_c_iflag);
}
static DEVICE_ATTR(iflag, S_IRUSR, dgap_tty_iflag_show, NULL);


static ssize_t dgap_tty_cflag_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return (0);
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (0);
	if (bd->state != BOARD_READY)
		return (0);

	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_c_cflag);
}
static DEVICE_ATTR(cflag, S_IRUSR, dgap_tty_cflag_show, NULL);


static ssize_t dgap_tty_oflag_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return (0);
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (0);
	if (bd->state != BOARD_READY)
		return (0);

	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_c_oflag);
}
static DEVICE_ATTR(oflag, S_IRUSR, dgap_tty_oflag_show, NULL);


static ssize_t dgap_tty_lflag_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return (0);
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (0);
	if (bd->state != BOARD_READY)
		return (0);

	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_c_lflag);
}
static DEVICE_ATTR(lflag, S_IRUSR, dgap_tty_lflag_show, NULL);


static ssize_t dgap_tty_digi_flag_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return (0);
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (0);
	if (bd->state != BOARD_READY)
		return (0);

	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_digi.digi_flags);
}
static DEVICE_ATTR(digi_flag, S_IRUSR, dgap_tty_digi_flag_show, NULL);


static ssize_t dgap_tty_rxcount_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return (0);
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (0);
	if (bd->state != BOARD_READY)
		return (0);

	return snprintf(buf, PAGE_SIZE, "%ld\n", ch->ch_rxcount);
}
static DEVICE_ATTR(rxcount, S_IRUSR, dgap_tty_rxcount_show, NULL);


static ssize_t dgap_tty_txcount_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return (0);
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (0);
	if (bd->state != BOARD_READY)
		return (0);

	return snprintf(buf, PAGE_SIZE, "%ld\n", ch->ch_txcount);
}
static DEVICE_ATTR(txcount, S_IRUSR, dgap_tty_txcount_show, NULL);


static ssize_t dgap_tty_name_show(struct device *d, struct device_attribute *attr, char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	int	cn;
	int	bn;
	struct cnode *cptr = NULL;
	int found = FALSE;
	int ncount = 0;
	int starto = 0;
	int i = 0;

	if (!d)
		return (0);
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (0);
	if (bd->state != BOARD_READY)
		return (0);

        bn = bd->boardnum;
	cn = ch->ch_portnum;

	for (cptr = bd->bd_config; cptr; cptr = cptr->next) {

		if ((cptr->type == BNODE) &&
		    ((cptr->u.board.type == APORT2_920P) || (cptr->u.board.type == APORT4_920P) ||
		     (cptr->u.board.type == APORT8_920P) || (cptr->u.board.type == PAPORT4) ||
		     (cptr->u.board.type == PAPORT8))) {

				found = TRUE;
				if (cptr->u.board.v_start)
					starto = cptr->u.board.start;
				else
					starto = 1;
		}

		if (cptr->type == TNODE && found == TRUE) {
			char *ptr1;
			if (strstr(cptr->u.ttyname, "tty")) {
				ptr1 = cptr->u.ttyname;
				ptr1 += 3;
			}
			else {
				ptr1 = cptr->u.ttyname;
			}

			for (i = 0; i < dgap_config_get_number_of_ports(bd); i++) {
				if (cn == i) {
					return snprintf(buf, PAGE_SIZE, "%s%s%02d\n",
						(un->un_type == DGAP_PRINT) ? "pr" : "tty",
						ptr1, i + starto);
				}
			}
		}

		if (cptr->type == CNODE) {

			for (i = 0; i < cptr->u.conc.nport; i++) {
				if (cn == (i + ncount)) {

					return snprintf(buf, PAGE_SIZE, "%s%s%02d\n",
						(un->un_type == DGAP_PRINT) ? "pr" : "tty",
						cptr->u.conc.id,
						i + (cptr->u.conc.v_start ? cptr->u.conc.start : 1));
				}
			}

			ncount += cptr->u.conc.nport;
		}

		if (cptr->type == MNODE) {

			for (i = 0; i < cptr->u.module.nport; i++) {
				if (cn == (i + ncount)) {
					return snprintf(buf, PAGE_SIZE, "%s%s%02d\n",
						(un->un_type == DGAP_PRINT) ? "pr" : "tty",
						cptr->u.module.id,
						i + (cptr->u.module.v_start ? cptr->u.module.start : 1));
				}
			}

			ncount += cptr->u.module.nport;

		}
	}

	return snprintf(buf, PAGE_SIZE, "%s_dgap_%d_%d\n",
		(un->un_type == DGAP_PRINT) ? "pr" : "tty", bn, cn);

}
static DEVICE_ATTR(custom_name, S_IRUSR, dgap_tty_name_show, NULL);


static struct attribute *dgap_sysfs_tty_entries[] = {
	&dev_attr_state.attr,
	&dev_attr_baud.attr,
	&dev_attr_msignals.attr,
	&dev_attr_iflag.attr,
	&dev_attr_cflag.attr,
	&dev_attr_oflag.attr,
	&dev_attr_lflag.attr,
	&dev_attr_digi_flag.attr,
	&dev_attr_rxcount.attr,
	&dev_attr_txcount.attr,
	&dev_attr_custom_name.attr,
	NULL
};


static struct attribute_group dgap_tty_attribute_group = {
	.name = NULL,
	.attrs = dgap_sysfs_tty_entries,
};




void dgap_create_tty_sysfs(struct un_t *un, struct device *c)
{
	int ret;

	ret = sysfs_create_group(&c->kobj, &dgap_tty_attribute_group);
	if (ret) {
		printk(KERN_ERR "dgap: failed to create sysfs tty device attributes.\n");
		sysfs_remove_group(&c->kobj, &dgap_tty_attribute_group);
		return;
	}

	dev_set_drvdata(c, un);

}


void dgap_remove_tty_sysfs(struct device *c)
{
	sysfs_remove_group(&c->kobj, &dgap_tty_attribute_group);
}
