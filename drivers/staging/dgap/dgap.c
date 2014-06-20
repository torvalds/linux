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
 */

/*
 *      In the original out of kernel Digi dgap driver, firmware
 *      loading was done via user land to driver handshaking.
 *
 *      For cards that support a concentrator (port expander),
 *      I believe the concentrator its self told the card which
 *      concentrator is actually attached and then that info
 *      was used to tell user land which concentrator firmware
 *      image was to be downloaded. I think even the BIOS or
 *      FEP images required could change with the connection
 *      of a particular concentrator.
 *
 *      Since I have no access to any of these cards or
 *      concentrators, I cannot put the correct concentrator
 *      firmware file names into the firmware_info structure
 *      as is now done for the BIOS and FEP images.
 *
 *      I think, but am not certain, that the cards supporting
 *      concentrators will function without them. So support
 *      of these cards has been left in this driver.
 *
 *      In order to fully support those cards, they would
 *      either have to be acquired for dissection or maybe
 *      Digi International could provide some assistance.
 */
#undef DIGI_CONCENTRATORS_SUPPORTED

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>	/* For udelay */
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched.h>

#include <linux/interrupt.h>	/* For tasklet and interrupt structs/defines */
#include <linux/ctype.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/io.h>		/* For read[bwl]/write[bwl] */

#include <linux/string.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/firmware.h>

#include "dgap.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Digi International, http://www.digi.com");
MODULE_DESCRIPTION("Driver for the Digi International EPCA PCI based product line");
MODULE_SUPPORTED_DEVICE("dgap");

static int dgap_start(void);
static void dgap_init_globals(void);
static struct board_t *dgap_found_board(struct pci_dev *pdev, int id,
					int boardnum);
static void dgap_cleanup_board(struct board_t *brd);
static void dgap_poll_handler(ulong dummy);
static int dgap_init_pci(void);
static int dgap_init_one(struct pci_dev *pdev, const struct pci_device_id *ent);
static void dgap_remove_one(struct pci_dev *dev);
static int dgap_do_remap(struct board_t *brd);
static void dgap_release_remap(struct board_t *brd);
static irqreturn_t dgap_intr(int irq, void *voidbrd);

static int dgap_tty_open(struct tty_struct *tty, struct file *file);
static void dgap_tty_close(struct tty_struct *tty, struct file *file);
static int dgap_block_til_ready(struct tty_struct *tty, struct file *file,
				struct channel_t *ch);
static int dgap_tty_ioctl(struct tty_struct *tty, unsigned int cmd,
				unsigned long arg);
static int dgap_tty_digigeta(struct tty_struct *tty,
				struct digi_t __user *retinfo);
static int dgap_tty_digiseta(struct tty_struct *tty,
				struct digi_t __user *new_info);
static int dgap_tty_digigetedelay(struct tty_struct *tty, int __user *retinfo);
static int dgap_tty_digisetedelay(struct tty_struct *tty, int __user *new_info);
static int dgap_tty_write_room(struct tty_struct *tty);
static int dgap_tty_chars_in_buffer(struct tty_struct *tty);
static void dgap_tty_start(struct tty_struct *tty);
static void dgap_tty_stop(struct tty_struct *tty);
static void dgap_tty_throttle(struct tty_struct *tty);
static void dgap_tty_unthrottle(struct tty_struct *tty);
static void dgap_tty_flush_chars(struct tty_struct *tty);
static void dgap_tty_flush_buffer(struct tty_struct *tty);
static void dgap_tty_hangup(struct tty_struct *tty);
static int dgap_wait_for_drain(struct tty_struct *tty);
static int dgap_set_modem_info(struct tty_struct *tty, unsigned int command,
				unsigned int __user *value);
static int dgap_get_modem_info(struct channel_t *ch,
				unsigned int __user *value);
static int dgap_tty_digisetcustombaud(struct tty_struct *tty,
				int __user *new_info);
static int dgap_tty_digigetcustombaud(struct tty_struct *tty,
				int __user *retinfo);
static int dgap_tty_tiocmget(struct tty_struct *tty);
static int dgap_tty_tiocmset(struct tty_struct *tty, unsigned int set,
				unsigned int clear);
static int dgap_tty_send_break(struct tty_struct *tty, int msec);
static void dgap_tty_wait_until_sent(struct tty_struct *tty, int timeout);
static int dgap_tty_write(struct tty_struct *tty, const unsigned char *buf,
				int count);
static void dgap_tty_set_termios(struct tty_struct *tty,
				struct ktermios *old_termios);
static int dgap_tty_put_char(struct tty_struct *tty, unsigned char c);
static void dgap_tty_send_xchar(struct tty_struct *tty, char ch);

static int dgap_tty_register(struct board_t *brd);
static void dgap_tty_unregister(struct board_t *brd);
static int dgap_tty_init(struct board_t *);
static void dgap_tty_free(struct board_t *);
static void dgap_cleanup_tty(struct board_t *);
static void dgap_carrier(struct channel_t *ch);
static void dgap_input(struct channel_t *ch);

/*
 * Our function prototypes from dgap_fep5
 */
static void dgap_cmdw_ext(struct channel_t *ch, u16 cmd, u16 word, uint ncmds);
static int dgap_event(struct board_t *bd);

static void dgap_poll_tasklet(unsigned long data);
static void dgap_cmdb(struct channel_t *ch, u8 cmd, u8 byte1,
			u8 byte2, uint ncmds);
static void dgap_cmdw(struct channel_t *ch, u8 cmd, u16 word, uint ncmds);
static void dgap_wmove(struct channel_t *ch, char *buf, uint cnt);
static int dgap_param(struct tty_struct *tty);
static void dgap_parity_scan(struct channel_t *ch, unsigned char *cbuf,
				unsigned char *fbuf, int *len);
static uint dgap_get_custom_baud(struct channel_t *ch);
static void dgap_firmware_reset_port(struct channel_t *ch);

/*
 * Function prototypes from dgap_parse.c.
 */
static int dgap_gettok(char **in, struct cnode *p);
static char *dgap_getword(char **in);
static struct cnode *dgap_newnode(int t);
static int dgap_checknode(struct cnode *p);
static void dgap_err(char *s);

/*
 * Function prototypes from dgap_sysfs.h
 */
struct board_t;
struct channel_t;
struct un_t;
struct pci_driver;
struct class_device;

static void dgap_create_ports_sysfiles(struct board_t *bd);
static void dgap_remove_ports_sysfiles(struct board_t *bd);

static int dgap_create_driver_sysfiles(struct pci_driver *);
static void dgap_remove_driver_sysfiles(struct pci_driver *);

static void dgap_create_tty_sysfs(struct un_t *un, struct device *c);
static void dgap_remove_tty_sysfs(struct device *c);

/*
 * Function prototypes from dgap_parse.h
 */
static int dgap_parsefile(char **in);
static struct cnode *dgap_find_config(int type, int bus, int slot);
static uint dgap_config_get_num_prts(struct board_t *bd);
static char *dgap_create_config_string(struct board_t *bd, char *string);
static uint dgap_config_get_useintr(struct board_t *bd);
static uint dgap_config_get_altpin(struct board_t *bd);

static int dgap_ms_sleep(ulong ms);
static void dgap_do_bios_load(struct board_t *brd, const u8 *ubios, int len);
static void dgap_do_fep_load(struct board_t *brd, const u8 *ufep, int len);
#ifdef DIGI_CONCENTRATORS_SUPPORTED
static void dgap_do_conc_load(struct board_t *brd, u8 *uaddr, int len);
#endif
static int dgap_alloc_flipbuf(struct board_t *brd);
static void dgap_free_flipbuf(struct board_t *brd);
static int dgap_request_irq(struct board_t *brd);
static void dgap_free_irq(struct board_t *brd);

static void dgap_get_vpd(struct board_t *brd);
static void dgap_do_reset_board(struct board_t *brd);
static int dgap_test_bios(struct board_t *brd);
static int dgap_test_fep(struct board_t *brd);
static int dgap_tty_register_ports(struct board_t *brd);
static int dgap_firmware_load(struct pci_dev *pdev, int card_type,
			      struct board_t* brd);

static void dgap_cleanup_module(void);

module_exit(dgap_cleanup_module);

/*
 * File operations permitted on Control/Management major.
 */
static const struct file_operations dgap_board_fops = {
	.owner	= THIS_MODULE,
};

static uint dgap_numboards;
static struct board_t *dgap_board[MAXBOARDS];
static ulong dgap_poll_counter;
static int dgap_driver_state = DRIVER_INITIALIZED;
static wait_queue_head_t dgap_dl_wait;
static int dgap_poll_tick = 20;	/* Poll interval - 20 ms */

static struct class *dgap_class;

static struct board_t *dgap_boards_by_major[256];
static uint dgap_count = 500;

/*
 * Poller stuff
 */
static DEFINE_SPINLOCK(dgap_poll_lock);	/* Poll scheduling lock */
static ulong dgap_poll_time;		/* Time of next poll */
static uint dgap_poll_stop;		/* Used to tell poller to stop */
static struct timer_list dgap_poll_timer;

/*
     SUPPORTED PRODUCTS

     Card Model               Number of Ports      Interface
     ----------------------------------------------------------------
     Acceleport Xem           4 - 64              (EIA232 & EIA422)
     Acceleport Xr            4 & 8               (EIA232)
     Acceleport Xr 920        4 & 8               (EIA232)
     Acceleport C/X           8 - 128             (EIA232)
     Acceleport EPC/X         8 - 224             (EIA232)
     Acceleport Xr/422        4 & 8               (EIA422)
     Acceleport 2r/920        2                   (EIA232)
     Acceleport 4r/920        4                   (EIA232)
     Acceleport 8r/920        8                   (EIA232)

     IBM 8-Port Asynchronous PCI Adapter          (EIA232)
     IBM 128-Port Asynchronous PCI Adapter        (EIA232 & EIA422)
*/

static struct pci_device_id dgap_pci_tbl[] = {
	{ DIGI_VID, PCI_DEV_XEM_DID,      PCI_ANY_ID, PCI_ANY_ID, 0, 0,  0 },
	{ DIGI_VID, PCI_DEV_CX_DID,       PCI_ANY_ID, PCI_ANY_ID, 0, 0,  1 },
	{ DIGI_VID, PCI_DEV_CX_IBM_DID,   PCI_ANY_ID, PCI_ANY_ID, 0, 0,  2 },
	{ DIGI_VID, PCI_DEV_EPCJ_DID,     PCI_ANY_ID, PCI_ANY_ID, 0, 0,  3 },
	{ DIGI_VID, PCI_DEV_920_2_DID,    PCI_ANY_ID, PCI_ANY_ID, 0, 0,  4 },
	{ DIGI_VID, PCI_DEV_920_4_DID,    PCI_ANY_ID, PCI_ANY_ID, 0, 0,  5 },
	{ DIGI_VID, PCI_DEV_920_8_DID,    PCI_ANY_ID, PCI_ANY_ID, 0, 0,  6 },
	{ DIGI_VID, PCI_DEV_XR_DID,       PCI_ANY_ID, PCI_ANY_ID, 0, 0,  7 },
	{ DIGI_VID, PCI_DEV_XRJ_DID,      PCI_ANY_ID, PCI_ANY_ID, 0, 0,  8 },
	{ DIGI_VID, PCI_DEV_XR_422_DID,   PCI_ANY_ID, PCI_ANY_ID, 0, 0,  9 },
	{ DIGI_VID, PCI_DEV_XR_IBM_DID,   PCI_ANY_ID, PCI_ANY_ID, 0, 0, 10 },
	{ DIGI_VID, PCI_DEV_XR_SAIP_DID,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 11 },
	{ DIGI_VID, PCI_DEV_XR_BULL_DID,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 12 },
	{ DIGI_VID, PCI_DEV_920_8_HP_DID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 13 },
	{ DIGI_VID, PCI_DEV_XEM_HP_DID,   PCI_ANY_ID, PCI_ANY_ID, 0, 0, 14 },
	{0,}					/* 0 terminated list. */
};
MODULE_DEVICE_TABLE(pci, dgap_pci_tbl);

/*
 * A generic list of Product names, PCI Vendor ID, and PCI Device ID.
 */
struct board_id {
	uint config_type;
	u8 *name;
	uint maxports;
	uint dpatype;
};

static struct board_id dgap_ids[] = {
	{ PPCM,        PCI_DEV_XEM_NAME,     64, (T_PCXM|T_PCLITE|T_PCIBUS) },
	{ PCX,         PCI_DEV_CX_NAME,     128, (T_CX|T_PCIBUS)            },
	{ PCX,         PCI_DEV_CX_IBM_NAME, 128, (T_CX|T_PCIBUS)            },
	{ PEPC,        PCI_DEV_EPCJ_NAME,   224, (T_EPC|T_PCIBUS)           },
	{ APORT2_920P, PCI_DEV_920_2_NAME,    2, (T_PCXR|T_PCLITE|T_PCIBUS) },
	{ APORT4_920P, PCI_DEV_920_4_NAME,    4, (T_PCXR|T_PCLITE|T_PCIBUS) },
	{ APORT8_920P, PCI_DEV_920_8_NAME,    8, (T_PCXR|T_PCLITE|T_PCIBUS) },
	{ PAPORT8,     PCI_DEV_XR_NAME,       8, (T_PCXR|T_PCLITE|T_PCIBUS) },
	{ PAPORT8,     PCI_DEV_XRJ_NAME,      8, (T_PCXR|T_PCLITE|T_PCIBUS) },
	{ PAPORT8,     PCI_DEV_XR_422_NAME,   8, (T_PCXR|T_PCLITE|T_PCIBUS) },
	{ PAPORT8,     PCI_DEV_XR_IBM_NAME,   8, (T_PCXR|T_PCLITE|T_PCIBUS) },
	{ PAPORT8,     PCI_DEV_XR_SAIP_NAME,  8, (T_PCXR|T_PCLITE|T_PCIBUS) },
	{ PAPORT8,     PCI_DEV_XR_BULL_NAME,  8, (T_PCXR|T_PCLITE|T_PCIBUS) },
	{ APORT8_920P, PCI_DEV_920_8_HP_NAME, 8, (T_PCXR|T_PCLITE|T_PCIBUS) },
	{ PPCM,        PCI_DEV_XEM_HP_NAME,  64, (T_PCXM|T_PCLITE|T_PCIBUS) },
	{0,}						/* 0 terminated list. */
};

static struct pci_driver dgap_driver = {
	.name		= "dgap",
	.probe		= dgap_init_one,
	.id_table	= dgap_pci_tbl,
	.remove		= dgap_remove_one,
};

struct firmware_info {
	u8 *conf_name;  /* dgap.conf */
	u8 *bios_name;	/* BIOS filename */
	u8 *fep_name;	/* FEP  filename */
	u8 *con_name;	/* Concentrator filename  FIXME*/
	int num;        /* sequence number */
};

/*
 * Firmware - BIOS, FEP, and CONC filenames
 */
static struct firmware_info fw_info[] = {
	{ "dgap/dgap.conf", "dgap/sxbios.bin",  "dgap/sxfep.bin",  NULL, 0 },
	{ "dgap/dgap.conf", "dgap/cxpbios.bin", "dgap/cxpfep.bin", NULL, 1 },
	{ "dgap/dgap.conf", "dgap/cxpbios.bin", "dgap/cxpfep.bin", NULL, 2 },
	{ "dgap/dgap.conf", "dgap/pcibios.bin", "dgap/pcifep.bin", NULL, 3 },
	{ "dgap/dgap.conf", "dgap/xrbios.bin",  "dgap/xrfep.bin",  NULL, 4 },
	{ "dgap/dgap.conf", "dgap/xrbios.bin",  "dgap/xrfep.bin",  NULL, 5 },
	{ "dgap/dgap.conf", "dgap/xrbios.bin",  "dgap/xrfep.bin",  NULL, 6 },
	{ "dgap/dgap.conf", "dgap/xrbios.bin",  "dgap/xrfep.bin",  NULL, 7 },
	{ "dgap/dgap.conf", "dgap/xrbios.bin",  "dgap/xrfep.bin",  NULL, 8 },
	{ "dgap/dgap.conf", "dgap/xrbios.bin",  "dgap/xrfep.bin",  NULL, 9 },
	{ "dgap/dgap.conf", "dgap/xrbios.bin",  "dgap/xrfep.bin",  NULL, 10 },
	{ "dgap/dgap.conf", "dgap/xrbios.bin",  "dgap/xrfep.bin",  NULL, 11 },
	{ "dgap/dgap.conf", "dgap/xrbios.bin",  "dgap/xrfep.bin",  NULL, 12 },
	{ "dgap/dgap.conf", "dgap/xrbios.bin",  "dgap/xrfep.bin",  NULL, 13 },
	{ "dgap/dgap.conf", "dgap/sxbios.bin",  "dgap/sxfep.bin",  NULL, 14 },
	{NULL,}
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

static struct ktermios dgap_default_termios = {
	.c_iflag =	(DEFAULT_IFLAGS),	/* iflags */
	.c_oflag =	(DEFAULT_OFLAGS),	/* oflags */
	.c_cflag =	(DEFAULT_CFLAGS),	/* cflags */
	.c_lflag =	(DEFAULT_LFLAGS),	/* lflags */
	.c_cc =		INIT_C_CC,
	.c_line =	0,
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

/*
 * Our needed internal static variables from dgap_parse.c
 */
static struct cnode dgap_head;
#define MAXCWORD 200
static char dgap_cword[MAXCWORD];

struct toklist {
	int token;
	char *string;
};

static struct toklist dgap_tlist[] = {
	{ BEGIN,	"config_begin" },
	{ END,		"config_end" },
	{ BOARD,	"board"	},
	{ PCX,		"Digi_AccelePort_C/X_PCI" },
	{ PEPC,		"Digi_AccelePort_EPC/X_PCI" },
	{ PPCM,		"Digi_AccelePort_Xem_PCI" },
	{ APORT2_920P,	"Digi_AccelePort_2r_920_PCI" },
	{ APORT4_920P,	"Digi_AccelePort_4r_920_PCI" },
	{ APORT8_920P,	"Digi_AccelePort_8r_920_PCI" },
	{ PAPORT4,	"Digi_AccelePort_4r_PCI(EIA-232/RS-422)" },
	{ PAPORT8,	"Digi_AccelePort_8r_PCI(EIA-232/RS-422)" },
	{ IO,		"io" },
	{ PCIINFO,	"pciinfo" },
	{ LINE,		"line" },
	{ CONC,		"conc" },
	{ CONC,		"concentrator" },
	{ CX,		"cx" },
	{ CX,		"ccon" },
	{ EPC,		"epccon" },
	{ EPC,		"epc" },
	{ MOD,		"module" },
	{ ID,		"id" },
	{ STARTO,	"start" },
	{ SPEED,	"speed"	},
	{ CABLE,	"cable"	},
	{ CONNECT,	"connect" },
	{ METHOD,	"method" },
	{ STATUS,	"status" },
	{ CUSTOM,	"Custom" },
	{ BASIC,	"Basic"	},
	{ MEM,		"mem" },
	{ MEM,		"memory" },
	{ PORTS,	"ports"	},
	{ MODEM,	"modem"	},
	{ NPORTS,	"nports" },
	{ TTYN,		"ttyname" },
	{ CU,		"cuname" },
	{ PRINT,	"prname" },
	{ CMAJOR,	"major"	 },
	{ ALTPIN,	"altpin" },
	{ USEINTR,	"useintr" },
	{ TTSIZ,	"ttysize" },
	{ CHSIZ,	"chsize" },
	{ BSSIZ,	"boardsize" },
	{ UNTSIZ,	"schedsize" },
	{ F2SIZ,	"f2200size" },
	{ VPSIZ,	"vpixsize" },
	{ 0,		NULL }
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
static int dgap_init_module(void)
{
	int rc;

	pr_info("%s, Digi International Part Number %s\n", DG_NAME, DG_PART);

	rc = dgap_start();
	if (rc)
		return rc;

	rc = dgap_init_pci();
	if (rc)
		goto err_cleanup;

	rc = dgap_create_driver_sysfiles(&dgap_driver);
	if (rc)
		goto err_cleanup;

	dgap_driver_state = DRIVER_READY;

	return 0;

err_cleanup:

	dgap_cleanup_module();

	return rc;
}
module_init(dgap_init_module);

/*
 * Start of driver.
 */
static int dgap_start(void)
{
	int rc;
	unsigned long flags;
	struct device *device;

	/*
	 * make sure that the globals are
	 * init'd before we do anything else
	 */
	dgap_init_globals();

	dgap_numboards = 0;

	pr_info("For the tools package please visit http://www.digi.com\n");

	/*
	 * Register our base character device into the kernel.
	 */

	/*
	 * Register management/dpa devices
	 */
	rc = register_chrdev(DIGI_DGAP_MAJOR, "dgap", &dgap_board_fops);
	if (rc < 0)
		return rc;

	dgap_class = class_create(THIS_MODULE, "dgap_mgmt");
	if (IS_ERR(dgap_class)) {
		rc = PTR_ERR(dgap_class);
		goto failed_class;
	}

	device = device_create(dgap_class, NULL,
		MKDEV(DIGI_DGAP_MAJOR, 0),
		NULL, "dgap_mgmt");
	if (IS_ERR(device)) {
		rc = PTR_ERR(device);
		goto failed_device;
	}

	/* Start the poller */
	spin_lock_irqsave(&dgap_poll_lock, flags);
	init_timer(&dgap_poll_timer);
	dgap_poll_timer.function = dgap_poll_handler;
	dgap_poll_timer.data = 0;
	dgap_poll_time = jiffies + dgap_jiffies_from_ms(dgap_poll_tick);
	dgap_poll_timer.expires = dgap_poll_time;
	spin_unlock_irqrestore(&dgap_poll_lock, flags);

	add_timer(&dgap_poll_timer);

	return rc;

failed_device:
	class_destroy(dgap_class);
failed_class:
	unregister_chrdev(DIGI_DGAP_MAJOR, "dgap");
	return rc;
}

/*
 * Register pci driver, and return how many boards we have.
 */
static int dgap_init_pci(void)
{
	return pci_register_driver(&dgap_driver);
}

static int dgap_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int rc;
	struct board_t* brd;

	if (dgap_numboards >= MAXBOARDS)
		return -EPERM;

	rc = pci_enable_device(pdev);
	if (rc)
		return -EIO;

	brd = dgap_found_board(pdev, ent->driver_data, dgap_numboards);
	if (IS_ERR(brd))
		return PTR_ERR(brd);

	rc = dgap_firmware_load(pdev, ent->driver_data, brd);
	if (rc)
		goto cleanup_brd;

	rc = dgap_alloc_flipbuf(brd);
	if (rc)
		goto cleanup_brd;

	rc = dgap_tty_register(brd);
	if (rc)
		goto free_flipbuf;

	rc = dgap_request_irq(brd);
	if (rc)
		goto unregister_tty;

	/*
	 * Do tty device initialization.
	 */
	rc = dgap_tty_init(brd);
	if (rc < 0)
		goto free_irq;

	rc = dgap_tty_register_ports(brd);
	if (rc)
		goto tty_free;

	brd->state = BOARD_READY;
	brd->dpastatus = BD_RUNNING;

	dgap_board[dgap_numboards++] = brd;

	return 0;

tty_free:
	dgap_tty_free(brd);
free_irq:
	dgap_free_irq(brd);
unregister_tty:
	dgap_tty_unregister(brd);
free_flipbuf:
	dgap_free_flipbuf(brd);
cleanup_brd:
	dgap_release_remap(brd);
	kfree(brd);

	return rc;
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
static void dgap_cleanup_module(void)
{
	int i;
	ulong lock_flags;

	spin_lock_irqsave(&dgap_poll_lock, lock_flags);
	dgap_poll_stop = 1;
	spin_unlock_irqrestore(&dgap_poll_lock, lock_flags);

	/* Turn off poller right away. */
	del_timer_sync(&dgap_poll_timer);

	dgap_remove_driver_sysfiles(&dgap_driver);

	device_destroy(dgap_class, MKDEV(DIGI_DGAP_MAJOR, 0));
	class_destroy(dgap_class);
	unregister_chrdev(DIGI_DGAP_MAJOR, "dgap");

	for (i = 0; i < dgap_numboards; ++i) {
		dgap_remove_ports_sysfiles(dgap_board[i]);
		dgap_cleanup_tty(dgap_board[i]);
		dgap_cleanup_board(dgap_board[i]);
	}

	if (dgap_numboards)
		pci_unregister_driver(&dgap_driver);
}

/*
 * dgap_cleanup_board()
 *
 * Free all the memory associated with a board
 */
static void dgap_cleanup_board(struct board_t *brd)
{
	int i;

	if (!brd || brd->magic != DGAP_BOARD_MAGIC)
		return;

	dgap_free_irq(brd);

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

	/* Free all allocated channels structs */
	for (i = 0; i < MAXPORTS ; i++)
		kfree(brd->channels[i]);

	kfree(brd->flipbuf);
	kfree(brd->flipflagbuf);

	dgap_board[brd->boardnum] = NULL;

	kfree(brd);
}

/*
 * dgap_found_board()
 *
 * A board has been found, init it.
 */
static struct board_t *dgap_found_board(struct pci_dev *pdev, int id,
					int boardnum)
{
	struct board_t *brd;
	unsigned int pci_irq;
	int i;
	int ret;

	/* get the board structure and prep it */
	brd = kzalloc(sizeof(struct board_t), GFP_KERNEL);
	if (!brd)
		return ERR_PTR(-ENOMEM);

	/* store the info for the board we've found */
	brd->magic = DGAP_BOARD_MAGIC;
	brd->boardnum = boardnum;
	brd->vendor = dgap_pci_tbl[id].vendor;
	brd->device = dgap_pci_tbl[id].device;
	brd->pdev = pdev;
	brd->pci_bus = pdev->bus->number;
	brd->pci_slot = PCI_SLOT(pdev->devfn);
	brd->name = dgap_ids[id].name;
	brd->maxports = dgap_ids[id].maxports;
	brd->type = dgap_ids[id].config_type;
	brd->dpatype = dgap_ids[id].dpatype;
	brd->dpastatus = BD_NOFEP;
	init_waitqueue_head(&brd->state_wait);

	spin_lock_init(&brd->bd_lock);

	brd->runwait		= 0;
	brd->inhibit_poller	= FALSE;
	brd->wait_for_bios	= 0;
	brd->wait_for_fep	= 0;

	for (i = 0; i < MAXPORTS; i++)
		brd->channels[i] = NULL;

	/* store which card & revision we have */
	pci_read_config_word(pdev, PCI_SUBSYSTEM_VENDOR_ID, &brd->subvendor);
	pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &brd->subdevice);
	pci_read_config_byte(pdev, PCI_REVISION_ID, &brd->rev);

	pci_irq = pdev->irq;
	brd->irq = pci_irq;

	/* get the PCI Base Address Registers */

	/* Xr Jupiter and EPC use BAR 2 */
	if (brd->device == PCI_DEV_XRJ_DID || brd->device == PCI_DEV_EPCJ_DID) {
		brd->membase     = pci_resource_start(pdev, 2);
		brd->membase_end = pci_resource_end(pdev, 2);
	}
	/* Everyone else uses BAR 0 */
	else {
		brd->membase     = pci_resource_start(pdev, 0);
		brd->membase_end = pci_resource_end(pdev, 0);
	}

	if (!brd->membase) {
		ret = -ENODEV;
		goto free_brd;
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
	if (brd->device != PCI_DEV_XRJ_DID && brd->device != PCI_DEV_EPCJ_DID) {
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
	tasklet_init(&brd->helper_tasklet, dgap_poll_tasklet,
			(unsigned long) brd);

	ret = dgap_do_remap(brd);
	if (ret)
		goto free_brd;

	pr_info("dgap: board %d: %s (rev %d), irq %ld\n",
		boardnum, brd->name, brd->rev, brd->irq);

	return brd;

free_brd:
	kfree(brd);

	return ERR_PTR(ret);
}


static int dgap_request_irq(struct board_t *brd)
{
	int rc;

	if (!brd || brd->magic != DGAP_BOARD_MAGIC)
		return -ENODEV;

	brd->use_interrupts = dgap_config_get_useintr(brd);

	/*
	 * Set up our interrupt handler if we are set to do interrupts.
	 */
	if (brd->use_interrupts && brd->irq) {

		rc = request_irq(brd->irq, dgap_intr, IRQF_SHARED, "DGAP", brd);

		if (rc)
			brd->intr_used = 0;
		else
			brd->intr_used = 1;
	} else {
		brd->intr_used = 0;
	}

	return 0;
}

static void dgap_free_irq(struct board_t *brd)
{
	if (brd->intr_used && brd->irq)
		free_irq(brd->irq, brd);
}

static int dgap_firmware_load(struct pci_dev *pdev, int card_type,
			      struct board_t* brd)
{
	const struct firmware *fw;
	char *tmp_ptr;
	int ret;
	char *dgap_config_buf;

	dgap_get_vpd(brd);
	dgap_do_reset_board(brd);

	if (fw_info[card_type].conf_name) {
		ret = request_firmware(&fw, fw_info[card_type].conf_name,
					 &pdev->dev);
		if (ret) {
			pr_err("dgap: config file %s not found\n",
				fw_info[card_type].conf_name);
			return ret;
		}

		dgap_config_buf = kzalloc(fw->size + 1, GFP_KERNEL);
		if (!dgap_config_buf) {
			release_firmware(fw);
			return -ENOMEM;
		}

		memcpy(dgap_config_buf, fw->data, fw->size);
		release_firmware(fw);

		/*
		 * preserve dgap_config_buf
		 * as dgap_parsefile would
		 * otherwise alter it.
		 */
		tmp_ptr = dgap_config_buf;

		if (dgap_parsefile(&tmp_ptr) != 0) {
			kfree(dgap_config_buf);
			return -EINVAL;
		}
		kfree(dgap_config_buf);
	}

	/*
	 * Match this board to a config the user created for us.
	 */
	brd->bd_config =
		dgap_find_config(brd->type, brd->pci_bus, brd->pci_slot);

	/*
	 * Because the 4 port Xr products share the same PCI ID
	 * as the 8 port Xr products, if we receive a NULL config
	 * back, and this is a PAPORT8 board, retry with a
	 * PAPORT4 attempt as well.
	 */
	if (brd->type == PAPORT8 && !brd->bd_config)
		brd->bd_config =
			dgap_find_config(PAPORT4, brd->pci_bus, brd->pci_slot);

	if (!brd->bd_config) {
		pr_err("dgap: No valid configuration found\n");
		return -EINVAL;
	}

	if (fw_info[card_type].bios_name) {
		ret = request_firmware(&fw, fw_info[card_type].bios_name,
					&pdev->dev);
		if (ret) {
			pr_err("dgap: bios file %s not found\n",
				fw_info[card_type].bios_name);
			return ret;
		}
		dgap_do_bios_load(brd, fw->data, fw->size);
		release_firmware(fw);

		/* Wait for BIOS to test board... */
		ret = dgap_test_bios(brd);
		if (ret)
			return ret;
	}

	if (fw_info[card_type].fep_name) {
		ret = request_firmware(&fw, fw_info[card_type].fep_name,
					&pdev->dev);
		if (ret) {
			pr_err("dgap: fep file %s not found\n",
				fw_info[card_type].fep_name);
			return ret;
		}
		dgap_do_fep_load(brd, fw->data, fw->size);
		release_firmware(fw);

		/* Wait for FEP to load on board... */
		ret = dgap_test_fep(brd);
		if (ret)
			return ret;
	}

#ifdef DIGI_CONCENTRATORS_SUPPORTED
	/*
	 * If this is a CX or EPCX, we need to see if the firmware
	 * is requesting a concentrator image from us.
	 */
	if ((bd->type == PCX) || (bd->type == PEPC)) {
		chk_addr = (u16 *) (vaddr + DOWNREQ);
		/* Nonzero if FEP is requesting concentrator image. */
		check = readw(chk_addr);
		vaddr = brd->re_map_membase;
	}

	if (fw_info[card_type].con_name && check && vaddr) {
		ret = request_firmware(&fw, fw_info[card_type].con_name,
					&pdev->dev);
		if (ret) {
			pr_err("dgap: conc file %s not found\n",
				fw_info[card_type].con_name);
			return ret;
		}
		/* Put concentrator firmware loading code here */
		offset = readw((u16 *) (vaddr + DOWNREQ));
		memcpy_toio(offset, fw->data, fw->size);

		dgap_do_conc_load(brd, (char *)fw->data, fw->size)
		release_firmware(fw);
	}
#endif

	return 0;
}

/*
 * Remap PCI memory.
 */
static int dgap_do_remap(struct board_t *brd)
{
	if (!brd || brd->magic != DGAP_BOARD_MAGIC)
		return -EIO;

	if (!request_mem_region(brd->membase, 0x200000, "dgap"))
		return -ENOMEM;

	if (!request_mem_region(brd->membase + PCI_IO_OFFSET, 0x200000,
					"dgap")) {
		release_mem_region(brd->membase, 0x200000);
		return -ENOMEM;
	}

	brd->re_map_membase = ioremap(brd->membase, 0x200000);
	if (!brd->re_map_membase) {
		release_mem_region(brd->membase, 0x200000);
		release_mem_region(brd->membase + PCI_IO_OFFSET, 0x200000);
		return -ENOMEM;
	}

	brd->re_map_port = ioremap((brd->membase + PCI_IO_OFFSET), 0x200000);
	if (!brd->re_map_port) {
		release_mem_region(brd->membase, 0x200000);
		release_mem_region(brd->membase + PCI_IO_OFFSET, 0x200000);
		iounmap(brd->re_map_membase);
		return -ENOMEM;
	}

	return 0;
}

static void dgap_release_remap(struct board_t *brd)
{
	release_mem_region(brd->membase, 0x200000);
	release_mem_region(brd->membase + PCI_IO_OFFSET, 0x200000);
	iounmap(brd->re_map_membase);
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
	ulong new_time;

	dgap_poll_counter++;

	/*
	 * Do not start the board state machine until
	 * driver tells us its up and running, and has
	 * everything it needs.
	 */
	if (dgap_driver_state != DRIVER_READY)
		goto schedule_poller;

	/*
	 * If we have just 1 board, or the system is not SMP,
	 * then use the typical old style poller.
	 * Otherwise, use our new tasklet based poller, which should
	 * speed things up for multiple boards.
	 */
	if ((dgap_numboards == 1) || (num_online_cpus() <= 1)) {
		for (i = 0; i < dgap_numboards; i++) {

			brd = dgap_board[i];

			if (brd->state == BOARD_FAILED)
				continue;
			if (!brd->intr_running)
				/* Call the real board poller directly */
				dgap_poll_tasklet((unsigned long) brd);
		}
	} else {
		/*
		 * Go thru each board, kicking off a
		 * tasklet for each if needed
		 */
		for (i = 0; i < dgap_numboards; i++) {
			brd = dgap_board[i];

			/*
			 * Attempt to grab the board lock.
			 *
			 * If we can't get it, no big deal, the next poll
			 * will get it. Basically, I just really don't want
			 * to spin in here, because I want to kick off my
			 * tasklets as fast as I can, and then get out the
			 * poller.
			 */
			if (!spin_trylock(&brd->bd_lock))
				continue;

			/*
			 * If board is in a failed state, don't bother
			 *  scheduling a tasklet
			 */
			if (brd->state == BOARD_FAILED) {
				spin_unlock(&brd->bd_lock);
				continue;
			}

			/* Schedule a poll helper task */
			if (!brd->intr_running)
				tasklet_schedule(&brd->helper_tasklet);

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
	spin_lock_irqsave(&dgap_poll_lock, lock_flags);
	dgap_poll_time +=  dgap_jiffies_from_ms(dgap_poll_tick);

	new_time = dgap_poll_time - jiffies;

	if ((ulong) new_time >= 2 * dgap_poll_tick) {
		dgap_poll_time =
			jiffies +  dgap_jiffies_from_ms(dgap_poll_tick);
	}

	dgap_poll_timer.function = dgap_poll_handler;
	dgap_poll_timer.data = 0;
	dgap_poll_timer.expires = dgap_poll_time;
	spin_unlock_irqrestore(&dgap_poll_lock, lock_flags);

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

	if (!brd)
		return IRQ_NONE;

	/*
	 * Check to make sure its for us.
	 */
	if (brd->magic != DGAP_BOARD_MAGIC)
		return IRQ_NONE;

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
	int i;

	for (i = 0; i < MAXBOARDS; i++)
		dgap_board[i] = NULL;

	init_timer(&dgap_poll_timer);

	init_waitqueue_head(&dgap_dl_wait);
}

/************************************************************************
 *
 * Utility functions
 *
 ************************************************************************/

/*
 * dgap_ms_sleep()
 *
 * Put the driver to sleep for x ms's
 *
 * Returns 0 if timed out, !0 (showing signal) if interrupted by a signal.
 */
static int dgap_ms_sleep(ulong ms)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout((ms * HZ) / 1000);
	return signal_pending(current);
}

/************************************************************************
 *
 * TTY Initialization/Cleanup Functions
 *
 ************************************************************************/

/*
 * dgap_tty_register()
 *
 * Init the tty subsystem for this board.
 */
static int dgap_tty_register(struct board_t *brd)
{
	int rc;

	brd->serial_driver = tty_alloc_driver(MAXPORTS, 0);
	if (IS_ERR(brd->serial_driver))
		return PTR_ERR(brd->serial_driver);

	snprintf(brd->serial_name, MAXTTYNAMELEN, "tty_dgap_%d_",
		 brd->boardnum);
	brd->serial_driver->name = brd->serial_name;
	brd->serial_driver->name_base = 0;
	brd->serial_driver->major = 0;
	brd->serial_driver->minor_start = 0;
	brd->serial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	brd->serial_driver->subtype = SERIAL_TYPE_NORMAL;
	brd->serial_driver->init_termios = dgap_default_termios;
	brd->serial_driver->driver_name = DRVSTR;
	brd->serial_driver->flags = (TTY_DRIVER_REAL_RAW |
				    TTY_DRIVER_DYNAMIC_DEV |
				    TTY_DRIVER_HARDWARE_BREAK);

	/* The kernel wants space to store pointers to tty_structs */
	brd->serial_driver->ttys =
		kzalloc(MAXPORTS * sizeof(struct tty_struct *), GFP_KERNEL);
	if (!brd->serial_driver->ttys) {
		rc = -ENOMEM;
		goto free_serial_drv;
	}

	/*
	 * Entry points for driver.  Called by the kernel from
	 * tty_io.c and n_tty.c.
	 */
	tty_set_operations(brd->serial_driver, &dgap_tty_ops);

	/*
	 * If we're doing transparent print, we have to do all of the above
	 * again, separately so we don't get the LD confused about what major
	 * we are when we get into the dgap_tty_open() routine.
	 */
	brd->print_driver = tty_alloc_driver(MAXPORTS, 0);
	if (IS_ERR(brd->print_driver)) {
		rc = PTR_ERR(brd->print_driver);
		goto free_serial_drv;
	}

	snprintf(brd->print_name, MAXTTYNAMELEN, "pr_dgap_%d_",
		 brd->boardnum);
	brd->print_driver->name = brd->print_name;
	brd->print_driver->name_base = 0;
	brd->print_driver->major = 0;
	brd->print_driver->minor_start = 0;
	brd->print_driver->type = TTY_DRIVER_TYPE_SERIAL;
	brd->print_driver->subtype = SERIAL_TYPE_NORMAL;
	brd->print_driver->init_termios = dgap_default_termios;
	brd->print_driver->driver_name = DRVSTR;
	brd->print_driver->flags = (TTY_DRIVER_REAL_RAW |
				   TTY_DRIVER_DYNAMIC_DEV |
				   TTY_DRIVER_HARDWARE_BREAK);

	/* The kernel wants space to store pointers to tty_structs */
	brd->print_driver->ttys =
		kzalloc(MAXPORTS * sizeof(struct tty_struct *), GFP_KERNEL);
	if (!brd->print_driver->ttys) {
		rc = -ENOMEM;
		goto free_print_drv;
	}

	/*
	 * Entry points for driver.  Called by the kernel from
	 * tty_io.c and n_tty.c.
	 */
	tty_set_operations(brd->print_driver, &dgap_tty_ops);

	/* Register tty devices */
	rc = tty_register_driver(brd->serial_driver);
	if (rc < 0)
		goto free_print_drv;

	/* Register Transparent Print devices */
	rc = tty_register_driver(brd->print_driver);
	if (rc < 0)
		goto unregister_serial_drv;

	brd->dgap_major_serial_registered = TRUE;
	dgap_boards_by_major[brd->serial_driver->major] = brd;
	brd->dgap_serial_major = brd->serial_driver->major;

	brd->dgap_major_transparent_print_registered = TRUE;
	dgap_boards_by_major[brd->print_driver->major] = brd;
	brd->dgap_transparent_print_major = brd->print_driver->major;

	return 0;

unregister_serial_drv:
	tty_unregister_driver(brd->serial_driver);
free_print_drv:
	put_tty_driver(brd->print_driver);
free_serial_drv:
	put_tty_driver(brd->serial_driver);

	return rc;
}

static void dgap_tty_unregister(struct board_t *brd)
{
	tty_unregister_driver(brd->print_driver);
	tty_unregister_driver(brd->serial_driver);
	put_tty_driver(brd->print_driver);
	put_tty_driver(brd->serial_driver);
}

/*
 * dgap_tty_init()
 *
 * Init the tty subsystem.  Called once per board after board has been
 * downloaded and init'ed.
 */
static int dgap_tty_init(struct board_t *brd)
{
	int i;
	int tlw;
	uint true_count;
	u8 __iomem *vaddr;
	u8 modem;
	struct channel_t *ch;
	struct bs_t __iomem *bs;
	struct cm_t __iomem *cm;
	int ret;

	if (!brd)
		return -EIO;

	/*
	 * Initialize board structure elements.
	 */

	vaddr = brd->re_map_membase;
	true_count = readw((vaddr + NCHAN));

	brd->nasync = dgap_config_get_num_prts(brd);

	if (!brd->nasync)
		brd->nasync = brd->maxports;

	if (brd->nasync > brd->maxports)
		brd->nasync = brd->maxports;

	if (true_count != brd->nasync) {
		if ((brd->type == PPCM) && (true_count == 64)) {
			pr_warn("dgap: %s configured for %d ports, has %d ports.\n",
				brd->name, brd->nasync, true_count);
			pr_warn("dgap: Please make SURE the EBI cable running from the card\n");
			pr_warn("dgap: to each EM module is plugged into EBI IN!\n");
		} else if ((brd->type == PPCM) && (true_count == 0)) {
			pr_warn("dgap: %s configured for %d ports, has %d ports.\n",
				brd->name, brd->nasync, true_count);
			pr_warn("dgap: Please make SURE the EBI cable running from the card\n");
			pr_warn("dgap: to each EM module is plugged into EBI IN!\n");
		} else
			pr_warn("dgap: %s configured for %d ports, has %d ports.\n",
				brd->name, brd->nasync, true_count);

		brd->nasync = true_count;

		/* If no ports, don't bother going any further */
		if (!brd->nasync) {
			brd->state = BOARD_FAILED;
			brd->dpastatus = BD_NOFEP;
			return -EIO;
		}
	}

	/*
	 * Allocate channel memory that might not have been allocated
	 * when the driver was first loaded.
	 */
	for (i = 0; i < brd->nasync; i++) {
		brd->channels[i] =
			kzalloc(sizeof(struct channel_t), GFP_KERNEL);
		if (!brd->channels[i]) {
			ret = -ENOMEM;
			goto free_chan;
		}
	}

	ch = brd->channels[0];
	vaddr = brd->re_map_membase;

	bs = (struct bs_t __iomem *) ((ulong) vaddr + CHANBUF);
	cm = (struct cm_t __iomem *) ((ulong) vaddr + CMDBUF);

	brd->bd_bs = bs;

	/* Set up channel variables */
	for (i = 0; i < brd->nasync; i++, ch = brd->channels[i], bs++) {

		spin_lock_init(&ch->ch_lock);

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
		} else {
			ch->ch_cd	= DM_CD;
			ch->ch_dsr	= DM_DSR;
		}

		ch->ch_taddr = vaddr + (ioread16(&(ch->ch_bs->tx_seg)) << 4);
		ch->ch_raddr = vaddr + (ioread16(&(ch->ch_bs->rx_seg)) << 4);
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
		tlw = ch->ch_tsize >= 2000 ? ((ch->ch_tsize * 5) / 8) :
						ch->ch_tsize / 2;
		ch->ch_tlw = tlw;

		dgap_cmdw(ch, STLOW, tlw, 0);

		dgap_cmdw(ch, SRLOW, ch->ch_rsize / 2, 0);

		dgap_cmdw(ch, SRHIGH, 7 * ch->ch_rsize / 8, 0);

		ch->ch_mistat = readb(&(ch->ch_bs->m_stat));

		init_waitqueue_head(&ch->ch_flags_wait);
		init_waitqueue_head(&ch->ch_tun.un_flags_wait);
		init_waitqueue_head(&ch->ch_pun.un_flags_wait);

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

	return 0;

free_chan:
	while (--i >= 0) {
		kfree(brd->channels[i]);
		brd->channels[i] = NULL;
	}
	return ret;
}

/*
 * dgap_tty_free()
 *
 * Free the channles which are allocated in dgap_tty_init().
 */
static void dgap_tty_free(struct board_t *brd)
{
	int i;

	for (i = 0; i < brd->nasync; i++)
		kfree(brd->channels[i]);
}
/*
 * dgap_cleanup_tty()
 *
 * Uninitialize the TTY portion of this driver.  Free all memory and
 * resources.
 */
static void dgap_cleanup_tty(struct board_t *brd)
{
	struct device *dev;
	int i;

	if (brd->dgap_major_serial_registered) {
		dgap_boards_by_major[brd->serial_driver->major] = NULL;
		brd->dgap_serial_major = 0;
		for (i = 0; i < brd->nasync; i++) {
			tty_port_destroy(&brd->serial_ports[i]);
			dev = brd->channels[i]->ch_tun.un_sysfs;
			dgap_remove_tty_sysfs(dev);
			tty_unregister_device(brd->serial_driver, i);
		}
		tty_unregister_driver(brd->serial_driver);
		put_tty_driver(brd->serial_driver);
		kfree(brd->serial_ports);
		brd->dgap_major_serial_registered = FALSE;
	}

	if (brd->dgap_major_transparent_print_registered) {
		dgap_boards_by_major[brd->print_driver->major] = NULL;
		brd->dgap_transparent_print_major = 0;
		for (i = 0; i < brd->nasync; i++) {
			tty_port_destroy(&brd->printer_ports[i]);
			dev = brd->channels[i]->ch_pun.un_sysfs;
			dgap_remove_tty_sysfs(dev);
			tty_unregister_device(brd->print_driver, i);
		}
		tty_unregister_driver(brd->print_driver);
		put_tty_driver(brd->print_driver);
		kfree(brd->printer_ports);
		brd->dgap_major_transparent_print_registered = FALSE;
	}
}

/*=======================================================================
 *
 *      dgap_input - Process received data.
 *
 *              ch      - Pointer to channel structure.
 *
 *=======================================================================*/

static void dgap_input(struct channel_t *ch)
{
	struct board_t *bd;
	struct bs_t __iomem *bs;
	struct tty_struct *tp;
	struct tty_ldisc *ld;
	uint rmask;
	uint head;
	uint tail;
	int data_len;
	ulong lock_flags;
	ulong lock_flags2;
	int flip_len;
	int len;
	int n;
	u8 *buf;
	u8 tmpchar;
	int s;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	tp = ch->ch_tun.un_tty;

	bs  = ch->ch_bs;
	if (!bs)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

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
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return;
	}

	/*
	 * If the device is not open, or CREAD is off, flush
	 * input data and return immediately.
	 */
	if ((bd->state != BOARD_READY) || !tp  ||
	    (tp->magic != TTY_MAGIC) ||
	    !(ch->ch_tun.un_flags & UN_ISOPEN) ||
	    !(tp->termios.c_cflag & CREAD) ||
	    (ch->ch_tun.un_flags & UN_CLOSING)) {

		writew(head, &(bs->rx_tail));
		writeb(1, &(bs->idata));
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return;
	}

	/*
	 * If we are throttled, simply don't read any data.
	 */
	if (ch->ch_flags & CH_RXBLOCK) {
		writeb(1, &(bs->idata));
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
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
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
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

		memcpy_fromio(buf, ch->ch_raddr + tail, s);

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
		dgap_parity_scan(ch, ch->ch_bd->flipbuf,
				 ch->ch_bd->flipflagbuf, &len);

		len = tty_buffer_request_room(tp->port, len);
		tty_insert_flip_string_flags(tp->port, ch->ch_bd->flipbuf,
			ch->ch_bd->flipflagbuf, len);
	} else {
		len = tty_buffer_request_room(tp->port, len);
		tty_insert_flip_string(tp->port, ch->ch_bd->flipbuf, len);
	}

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

	/* Tell the tty layer its okay to "eat" the data now */
	tty_flip_buffer_push(tp->port);

	if (ld)
		tty_ldisc_deref(ld);

}

/************************************************************************
 * Determines when CARRIER changes state and takes appropriate
 * action.
 ************************************************************************/
static void dgap_carrier(struct channel_t *ch)
{
	struct board_t *bd;

	int virt_carrier = 0;
	int phys_carrier = 0;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;

	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	/* Make sure altpin is always set correctly */
	if (ch->ch_digi.digi_flags & DIGI_ALTPIN) {
		ch->ch_dsr      = DM_CD;
		ch->ch_cd       = DM_DSR;
	} else {
		ch->ch_dsr      = DM_DSR;
		ch->ch_cd       = DM_CD;
	}

	if (ch->ch_mistat & D_CD(ch))
		phys_carrier = 1;

	if (ch->ch_digi.digi_flags & DIGI_FORCEDCD)
		virt_carrier = 1;

	if (ch->ch_c_cflag & CLOCAL)
		virt_carrier = 1;

	/*
	 * Test for a VIRTUAL carrier transition to HIGH.
	 */
	if (((ch->ch_flags & CH_FCAR) == 0) && (virt_carrier == 1)) {

		/*
		 * When carrier rises, wake any threads waiting
		 * for carrier in the open routine.
		 */

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
	if ((virt_carrier == 0) &&
	    ((ch->ch_flags & CH_CD) != 0) &&
	    (phys_carrier == 0)) {

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

		if (ch->ch_tun.un_open_count > 0)
			tty_hangup(ch->ch_tun.un_tty);

		if (ch->ch_pun.un_open_count > 0)
			tty_hangup(ch->ch_pun.un_tty);
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
	struct board_t *brd;
	struct channel_t *ch;
	struct un_t *un;
	struct bs_t __iomem *bs;
	uint major;
	uint minor;
	int rc;
	ulong lock_flags;
	ulong lock_flags2;
	u16 head;

	major = MAJOR(tty_devnum(tty));
	minor = MINOR(tty_devnum(tty));

	if (major > 255)
		return -EIO;

	/* Get board pointer from our array of majors we have allocated */
	brd = dgap_boards_by_major[major];
	if (!brd)
		return -EIO;

	/*
	 * If board is not yet up to a state of READY, go to
	 * sleep waiting for it to happen or they cancel the open.
	 */
	rc = wait_event_interruptible(brd->state_wait,
		(brd->state & BOARD_READY));

	if (rc)
		return rc;

	spin_lock_irqsave(&brd->bd_lock, lock_flags);

	/* The wait above should guarantee this cannot happen */
	if (brd->state != BOARD_READY) {
		spin_unlock_irqrestore(&brd->bd_lock, lock_flags);
		return -EIO;
	}

	/* If opened device is greater than our number of ports, bail. */
	if (MINOR(tty_devnum(tty)) > brd->nasync) {
		spin_unlock_irqrestore(&brd->bd_lock, lock_flags);
		return -EIO;
	}

	ch = brd->channels[minor];
	if (!ch) {
		spin_unlock_irqrestore(&brd->bd_lock, lock_flags);
		return -EIO;
	}

	/* Grab channel lock */
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

	/* Figure out our type */
	if (major == brd->dgap_serial_major) {
		un = &brd->channels[minor]->ch_tun;
		un->un_type = DGAP_SERIAL;
	} else if (major == brd->dgap_transparent_print_major) {
		un = &brd->channels[minor]->ch_pun;
		un->un_type = DGAP_PRINT;
	} else {
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&brd->bd_lock, lock_flags);
		return -EIO;
	}

	/* Store our unit into driver_data, so we always have it available. */
	tty->driver_data = un;

	/*
	 * Error if channel info pointer is NULL.
	 */
	bs = ch->ch_bs;
	if (!bs) {
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&brd->bd_lock, lock_flags);
		return -EIO;
	}

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

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&brd->bd_lock, lock_flags);

	rc = dgap_block_til_ready(tty, file, ch);

	if (!un->un_tty)
		return -ENODEV;

	/* No going back now, increment our unit and channel counters */
	spin_lock_irqsave(&ch->ch_lock, lock_flags);
	ch->ch_open_count++;
	un->un_open_count++;
	un->un_flags |= (UN_ISOPEN);
	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

	return rc;
}

/*
 * dgap_block_til_ready()
 *
 * Wait for DCD, if needed.
 */
static int dgap_block_til_ready(struct tty_struct *tty, struct file *file,
				struct channel_t *ch)
{
	int retval = 0;
	struct un_t *un;
	ulong lock_flags;
	uint old_flags;
	int sleep_on_un_flags;

	if (!tty || tty->magic != TTY_MAGIC || !file || !ch ||
		ch->magic != DGAP_CHANNEL_MAGIC)
		return -EIO;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -EIO;

	spin_lock_irqsave(&ch->ch_lock, lock_flags);

	ch->ch_wopen++;

	/* Loop forever */
	while (1) {

		sleep_on_un_flags = 0;

		/*
		 * If board has failed somehow during our sleep,
		 * bail with error.
		 */
		if (ch->ch_bd->state == BOARD_FAILED) {
			retval = -EIO;
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
		if (!((ch->ch_tun.un_flags | ch->ch_pun.un_flags) &
		      UN_CLOSING)) {

			/*
			 * Our conditions to leave cleanly and happily:
			 * 1) NONBLOCKING on the tty is set.
			 * 2) CLOCAL is set.
			 * 3) DCD (fake or real) is active.
			 */

			if (file->f_flags & O_NONBLOCK)
				break;

			if (tty->flags & (1 << TTY_IO_ERROR))
				break;

			if (ch->ch_flags & CH_CD)
				break;

			if (ch->ch_flags & CH_FCAR)
				break;
		} else {
			sleep_on_un_flags = 1;
		}

		/*
		 * If there is a signal pending, the user probably
		 * interrupted (ctrl-c) us.
		 * Leave loop with error set.
		 */
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}

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

		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

		/*
		 * Wait for something in the flags to change
		 * from the current value.
		 */
		if (sleep_on_un_flags) {
			retval = wait_event_interruptible(un->un_flags_wait,
				(old_flags != (ch->ch_tun.un_flags |
					       ch->ch_pun.un_flags)));
		} else {
			retval = wait_event_interruptible(ch->ch_flags_wait,
				(old_flags != ch->ch_flags));
		}

		/*
		 * We got woken up for some reason.
		 * Before looping around, grab our channel lock.
		 */
		spin_lock_irqsave(&ch->ch_lock, lock_flags);
	}

	ch->ch_wopen--;

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

	if (retval)
		return retval;

	return 0;
}

/*
 * dgap_tty_hangup()
 *
 * Hangup the port.  Like a close, but don't wait for output to drain.
 */
static void dgap_tty_hangup(struct tty_struct *tty)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

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

	/* flush the transmit queues */
	dgap_tty_flush_buffer(tty);
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

	spin_lock_irqsave(&ch->ch_lock, lock_flags);

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
		un->un_open_count = 1;
	}

	if (--un->un_open_count < 0)
		un->un_open_count = 0;

	ch->ch_open_count--;

	if (ch->ch_open_count && un->un_open_count) {
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
		return;
	}

	/* OK, its the last close on the unit */

	un->un_flags |= UN_CLOSING;

	tty->closing = 1;

	/*
	 * Only officially close channel if count is 0 and
	 * DIGI_PRINTER bit is not set.
	 */
	if ((ch->ch_open_count == 0) &&
	    !(ch->ch_digi.digi_flags & DIGI_PRINTER)) {

		ch->ch_flags &= ~(CH_RXBLOCK);

		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

		/* wait for output to drain */
		/* This will also return if we take an interrupt */

		dgap_wait_for_drain(tty);

		dgap_tty_flush_buffer(tty);
		tty_ldisc_flush(tty);

		spin_lock_irqsave(&ch->ch_lock, lock_flags);

		tty->closing = 0;

		/*
		 * If we have HUPCL set, lower DTR and RTS
		 */
		if (ch->ch_c_cflag & HUPCL) {
			ch->ch_mostat &= ~(D_RTS(ch)|D_DTR(ch));
			dgap_cmdb(ch, SMODEM, 0, D_DTR(ch)|D_RTS(ch), 0);

			/*
			 * Go to sleep to ensure RTS/DTR
			 * have been dropped for modems to see it.
			 */
			if (ch->ch_close_delay) {
				spin_unlock_irqrestore(&ch->ch_lock,
						       lock_flags);
				dgap_ms_sleep(ch->ch_close_delay);
				spin_lock_irqsave(&ch->ch_lock, lock_flags);
			}
		}

		ch->pscan_state = 0;
		ch->pscan_savechar = 0;
		ch->ch_baud_info = 0;

	}

	/*
	 * turn off print device when closing print device.
	 */
	if ((un->un_type == DGAP_PRINT)  && (ch->ch_flags & CH_PRON)) {
		dgap_wmove(ch, ch->ch_digi.digi_offstr,
			(int) ch->ch_digi.digi_offlen);
		ch->ch_flags &= ~CH_PRON;
	}

	un->un_tty = NULL;
	un->un_flags &= ~(UN_ISOPEN | UN_CLOSING);
	tty->driver_data = NULL;

	wake_up_interruptible(&ch->ch_flags_wait);
	wake_up_interruptible(&un->un_flags_wait);

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
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
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	struct bs_t __iomem *bs;
	u8 tbusy;
	uint chars;
	u16 thead, ttail, tmask, chead, ctail;
	ulong lock_flags = 0;
	ulong lock_flags2 = 0;

	if (!tty)
		return 0;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return 0;

	bs = ch->ch_bs;
	if (!bs)
		return 0;

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

	tmask = (ch->ch_tsize - 1);

	/* Get Transmit queue pointers */
	thead = readw(&(bs->tx_head)) & tmask;
	ttail = readw(&(bs->tx_tail)) & tmask;

	/* Get tbusy flag */
	tbusy = readb(&(bs->tbusy));

	/* Get Command queue pointers */
	chead = readw(&(ch->ch_cm->cm_head));
	ctail = readw(&(ch->ch_cm->cm_tail));

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

	/*
	 * The only way we know for sure if there is no pending
	 * data left to be transferred, is if:
	 * 1) Transmit head and tail are equal (empty).
	 * 2) Command queue head and tail are equal (empty).
	 * 3) The "TBUSY" flag is 0. (Transmitter not busy).
	 */

	if ((ttail == thead) && (tbusy == 0) && (chead == ctail)) {
		chars = 0;
	} else {
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
				spin_lock_irqsave(&ch->ch_lock, lock_flags);
				un->un_flags |= UN_EMPTY;
				writeb(1, &(bs->iempty));
				spin_unlock_irqrestore(&ch->ch_lock,
						       lock_flags);
			}
			chars = 1;
		}
	}

	return chars;
}

static int dgap_wait_for_drain(struct tty_struct *tty)
{
	struct channel_t *ch;
	struct un_t *un;
	struct bs_t __iomem *bs;
	int ret = 0;
	uint count = 1;
	ulong lock_flags = 0;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EIO;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -EIO;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -EIO;

	bs = ch->ch_bs;
	if (!bs)
		return -EIO;

	/* Loop until data is drained */
	while (count != 0) {

		count = dgap_tty_chars_in_buffer(tty);

		if (count == 0)
			break;

		/* Set flag waiting for drain */
		spin_lock_irqsave(&ch->ch_lock, lock_flags);
		un->un_flags |= UN_EMPTY;
		writeb(1, &(bs->iempty));
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

		/* Go to sleep till we get woken up */
		ret = wait_event_interruptible(un->un_flags_wait,
					((un->un_flags & UN_EMPTY) == 0));
		/* If ret is non-zero, user ctrl-c'ed us */
		if (ret)
			break;
	}

	spin_lock_irqsave(&ch->ch_lock, lock_flags);
	un->un_flags &= ~(UN_EMPTY);
	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

	return ret;
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
	struct channel_t *ch;
	struct un_t *un;

	if (!tty)
		return bytes_available;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return bytes_available;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return bytes_available;

	/*
	 * If its not the Transparent print device, return
	 * the full data amount.
	 */
	if (un->un_type != DGAP_PRINT)
		return bytes_available;

	if (ch->ch_digi.digi_maxcps > 0 && ch->ch_digi.digi_bufsize > 0) {
		int cps_limit = 0;
		unsigned long current_time = jiffies;
		unsigned long buffer_time = current_time +
			(HZ * ch->ch_digi.digi_bufsize) /
			ch->ch_digi.digi_maxcps;

		if (ch->ch_cpstime < current_time) {
			/* buffer is empty */
			ch->ch_cpstime = current_time;   /* reset ch_cpstime */
			cps_limit = ch->ch_digi.digi_bufsize;
		} else if (ch->ch_cpstime < buffer_time) {
			/* still room in the buffer */
			cps_limit = ((buffer_time - ch->ch_cpstime) *
				     ch->ch_digi.digi_maxcps) / HZ;
		} else {
			/* no room in the buffer */
			cps_limit = 0;
		}

		bytes_available = min(cps_limit, bytes_available);
	}

	return bytes_available;
}

static inline void dgap_set_firmware_event(struct un_t *un, unsigned int event)
{
	struct channel_t *ch;
	struct bs_t __iomem *bs;

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
	struct channel_t *ch;
	struct un_t *un;
	struct bs_t __iomem *bs;
	u16 head, tail, tmask;
	int ret;
	ulong lock_flags = 0;

	if (!tty)
		return 0;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;

	bs = ch->ch_bs;
	if (!bs)
		return 0;

	spin_lock_irqsave(&ch->ch_lock, lock_flags);

	tmask = ch->ch_tsize - 1;
	head = readw(&(bs->tx_head)) & tmask;
	tail = readw(&(bs->tx_tail)) & tmask;

	ret = tail - head - 1;
	if (ret < 0)
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
	} else {
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
	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

	return ret;
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
	dgap_tty_write(tty, &c, 1);
	return 1;
}

/*
 * dgap_tty_write()
 *
 * Take data from the user or kernel and send it out to the FEP.
 * In here exists all the Transparent Print magic as well.
 */
static int dgap_tty_write(struct tty_struct *tty, const unsigned char *buf,
				int count)
{
	struct channel_t *ch;
	struct un_t *un;
	struct bs_t __iomem *bs;
	char __iomem *vaddr;
	u16 head, tail, tmask, remain;
	int bufcount, n;
	int orig_count;
	ulong lock_flags;

	if (!tty)
		return 0;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;

	bs = ch->ch_bs;
	if (!bs)
		return 0;

	if (!count)
		return 0;

	/*
	 * Store original amount of characters passed in.
	 * This helps to figure out if we should ask the FEP
	 * to send us an event when it has more space available.
	 */
	orig_count = count;

	spin_lock_irqsave(&ch->ch_lock, lock_flags);

	/* Get our space available for the channel from the board */
	tmask = ch->ch_tsize - 1;
	head = readw(&(bs->tx_head)) & tmask;
	tail = readw(&(bs->tx_tail)) & tmask;

	bufcount = tail - head - 1;
	if (bufcount < 0)
		bufcount += ch->ch_tsize;

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
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
		return 0;
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

		memcpy_toio(vaddr, (u8 *) buf, remain);

		head = ch->ch_tstart;
		buf += remain;
	}

	if (n > 0) {

		/*
		 * Move rest of data.
		 */
		vaddr = ch->ch_taddr + head;
		remain = n;

		memcpy_toio(vaddr, (u8 *) buf, remain);
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
		} else {
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

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

	return count;
}

/*
 * Return modem signals to ld.
 */
static int dgap_tty_tiocmget(struct tty_struct *tty)
{
	struct channel_t *ch;
	struct un_t *un;
	int result;
	u8 mstat;
	ulong lock_flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EIO;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -EIO;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -EIO;

	spin_lock_irqsave(&ch->ch_lock, lock_flags);

	mstat = readb(&(ch->ch_bs->m_stat));
	/* Append any outbound signals that might be pending... */
	mstat |= ch->ch_mostat;

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

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
	ulong lock_flags;
	ulong lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EIO;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -EIO;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -EIO;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return -EIO;

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

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

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

	return 0;
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
	ulong lock_flags;
	ulong lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EIO;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -EIO;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -EIO;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return -EIO;

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

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);
#if 0
	dgap_cmdw(ch, SBREAK, (u16) SBREAK_TIME, 0);
#endif
	dgap_cmdw(ch, SBREAK, (u16) msec, 0);

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

	return 0;
}

/*
 * dgap_tty_wait_until_sent()
 *
 * wait until data has been transmitted, called by ld.
 */
static void dgap_tty_wait_until_sent(struct tty_struct *tty, int timeout)
{
	dgap_wait_for_drain(tty);
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

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

	/*
	 * This is technically what we should do.
	 * However, the NIST tests specifically want
	 * to see each XON or XOFF character that it
	 * sends, so lets just send each character
	 * by hand...
	 */
#if 0
	if (c == STOP_CHAR(tty))
		dgap_cmdw(ch, RPAUSE, 0, 0);
	else if (c == START_CHAR(tty))
		dgap_cmdw(ch, RRESUME, 0, 0);
	else
		dgap_wmove(ch, &c, 1);
#else
	dgap_wmove(ch, &c, 1);
#endif

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

	return;
}

/*
 * Return modem signals to ld.
 */
static int dgap_get_modem_info(struct channel_t *ch, unsigned int __user *value)
{
	int result;
	u8 mstat;
	ulong lock_flags;
	int rc;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -EIO;

	spin_lock_irqsave(&ch->ch_lock, lock_flags);

	mstat = readb(&(ch->ch_bs->m_stat));
	/* Append any outbound signals that might be pending... */
	mstat |= ch->ch_mostat;

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

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

	return rc;
}

/*
 * dgap_set_modem_info()
 *
 * Set modem signals, called by ld.
 */
static int dgap_set_modem_info(struct tty_struct *tty, unsigned int command,
				unsigned int __user *value)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	int ret;
	unsigned int arg;
	ulong lock_flags;
	ulong lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EIO;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -EIO;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -EIO;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return -EIO;

	ret = get_user(arg, value);
	if (ret)
		return ret;

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

		if (arg & TIOCM_RTS)
			ch->ch_mval |= D_RTS(ch);
		else
			ch->ch_mval &= ~(D_RTS(ch));

		if (arg & TIOCM_DTR)
			ch->ch_mval |= (D_DTR(ch));
		else
			ch->ch_mval &= ~(D_DTR(ch));

		break;

	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

	dgap_param(tty);

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

	return 0;
}

/*
 * dgap_tty_digigeta()
 *
 * Ioctl to get the information for ditty.
 *
 *
 *
 */
static int dgap_tty_digigeta(struct tty_struct *tty,
				struct digi_t __user *retinfo)
{
	struct channel_t *ch;
	struct un_t *un;
	struct digi_t tmp;
	ulong lock_flags;

	if (!retinfo)
		return -EFAULT;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EFAULT;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -EFAULT;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));

	spin_lock_irqsave(&ch->ch_lock, lock_flags);
	memcpy(&tmp, &ch->ch_digi, sizeof(tmp));
	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;

	return 0;
}

/*
 * dgap_tty_digiseta()
 *
 * Ioctl to set the information for ditty.
 *
 *
 *
 */
static int dgap_tty_digiseta(struct tty_struct *tty,
				struct digi_t __user *new_info)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	struct digi_t new_digi;
	ulong lock_flags = 0;
	unsigned long lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EFAULT;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -EFAULT;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -EFAULT;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return -EFAULT;

	if (copy_from_user(&new_digi, new_info, sizeof(struct digi_t)))
		return -EFAULT;

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

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

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

	return 0;
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
		return -EFAULT;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EFAULT;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -EFAULT;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));

	spin_lock_irqsave(&ch->ch_lock, lock_flags);
	tmp = readw(&(ch->ch_bs->edelay));
	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;

	return 0;
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

	if (!tty || tty->magic != TTY_MAGIC)
		return -EFAULT;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -EFAULT;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -EFAULT;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return -EFAULT;

	if (copy_from_user(&new_digi, new_info, sizeof(int)))
		return -EFAULT;

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

	writew((u16) new_digi, &(ch->ch_bs->edelay));

	dgap_param(tty);

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

	return 0;
}

/*
 * dgap_tty_digigetcustombaud()
 *
 * Ioctl to get the current custom baud rate setting.
 */
static int dgap_tty_digigetcustombaud(struct tty_struct *tty,
					int __user *retinfo)
{
	struct channel_t *ch;
	struct un_t *un;
	int tmp;
	ulong lock_flags;

	if (!retinfo)
		return -EFAULT;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EFAULT;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -EFAULT;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));

	spin_lock_irqsave(&ch->ch_lock, lock_flags);
	tmp = dgap_get_custom_baud(ch);
	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;

	return 0;
}

/*
 * dgap_tty_digisetcustombaud()
 *
 * Ioctl to set the custom baud rate setting
 */
static int dgap_tty_digisetcustombaud(struct tty_struct *tty,
					int __user *new_info)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	uint new_rate;
	ulong lock_flags;
	ulong lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EFAULT;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -EFAULT;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -EFAULT;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return -EFAULT;


	if (copy_from_user(&new_rate, new_info, sizeof(unsigned int)))
		return -EFAULT;

	if (bd->bd_flags & BD_FEP5PLUS) {

		spin_lock_irqsave(&bd->bd_lock, lock_flags);
		spin_lock_irqsave(&ch->ch_lock, lock_flags2);

		ch->ch_custom_speed = new_rate;

		dgap_param(tty);

		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
	}

	return 0;
}

/*
 * dgap_set_termios()
 */
static void dgap_tty_set_termios(struct tty_struct *tty,
				struct ktermios *old_termios)
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

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

	ch->ch_c_cflag   = tty->termios.c_cflag;
	ch->ch_c_iflag   = tty->termios.c_iflag;
	ch->ch_c_oflag   = tty->termios.c_oflag;
	ch->ch_c_lflag   = tty->termios.c_lflag;
	ch->ch_startc    = tty->termios.c_cc[VSTART];
	ch->ch_stopc     = tty->termios.c_cc[VSTOP];

	dgap_carrier(ch);
	dgap_param(tty);

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
}

static void dgap_tty_throttle(struct tty_struct *tty)
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

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

	ch->ch_flags |= (CH_RXBLOCK);
#if 1
	dgap_cmdw(ch, RPAUSE, 0, 0);
#endif

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

}

static void dgap_tty_unthrottle(struct tty_struct *tty)
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

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

	ch->ch_flags &= ~(CH_RXBLOCK);

#if 1
	dgap_cmdw(ch, RRESUME, 0, 0);
#endif

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
}

static void dgap_tty_start(struct tty_struct *tty)
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

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

	dgap_cmdw(ch, RESUMETX, 0, 0);

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
}

static void dgap_tty_stop(struct tty_struct *tty)
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

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

	dgap_cmdw(ch, PAUSETX, 0, 0);

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
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

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

	/* TODO: Do something here */

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
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
	ulong lock_flags;
	ulong lock_flags2;
	u16 head;

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

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

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

	spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
	if (waitqueue_active(&tty->write_wait))
		wake_up_interruptible(&tty->write_wait);
	tty_wakeup(tty);
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
	u16 head;
	ulong lock_flags = 0;
	ulong lock_flags2 = 0;
	void __user *uarg = (void __user *) arg;

	if (!tty || tty->magic != TTY_MAGIC)
		return -ENODEV;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -ENODEV;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -ENODEV;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return -ENODEV;

	spin_lock_irqsave(&bd->bd_lock, lock_flags);
	spin_lock_irqsave(&ch->ch_lock, lock_flags2);

	if (un->un_open_count <= 0) {
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return -EIO;
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
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		if (rc)
			return rc;

		rc = dgap_wait_for_drain(tty);

		if (rc)
			return -EINTR;

		spin_lock_irqsave(&bd->bd_lock, lock_flags);
		spin_lock_irqsave(&ch->ch_lock, lock_flags2);

		if (((cmd == TCSBRK) && (!arg)) || (cmd == TCSBRKP))
			dgap_cmdw(ch, SBREAK, (u16) SBREAK_TIME, 0);

		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

		return 0;

	case TCSBRKP:
		/* support for POSIX tcsendbreak()

		 * According to POSIX.1 spec (7.2.2.1.2) breaks should be
		 * between 0.25 and 0.5 seconds so we'll ask for something
		 * in the middle: 0.375 seconds.
		 */
		rc = tty_check_change(tty);
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		if (rc)
			return rc;

		rc = dgap_wait_for_drain(tty);
		if (rc)
			return -EINTR;

		spin_lock_irqsave(&bd->bd_lock, lock_flags);
		spin_lock_irqsave(&ch->ch_lock, lock_flags2);

		dgap_cmdw(ch, SBREAK, (u16) SBREAK_TIME, 0);

		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

		return 0;

	case TIOCSBRK:
		/*
		 * FEP5 doesn't support turning on a break unconditionally.
		 * The FEP5 device will stop sending a break automatically
		 * after the specified time value that was sent when turning on
		 * the break.
		 */
		rc = tty_check_change(tty);
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		if (rc)
			return rc;

		rc = dgap_wait_for_drain(tty);
		if (rc)
			return -EINTR;

		spin_lock_irqsave(&bd->bd_lock, lock_flags);
		spin_lock_irqsave(&ch->ch_lock, lock_flags2);

		dgap_cmdw(ch, SBREAK, (u16) SBREAK_TIME, 0);

		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

		return 0;

	case TIOCCBRK:
		/*
		 * FEP5 doesn't support turning off a break unconditionally.
		 * The FEP5 device will stop sending a break automatically
		 * after the specified time value that was sent when turning on
		 * the break.
		 */
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return 0;

	case TIOCGSOFTCAR:

		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

		rc = put_user(C_CLOCAL(tty) ? 1 : 0,
				(unsigned long __user *) arg);
		return rc;

	case TIOCSSOFTCAR:
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

		rc = get_user(arg, (unsigned long __user *) arg);
		if (rc)
			return rc;

		spin_lock_irqsave(&bd->bd_lock, lock_flags);
		spin_lock_irqsave(&ch->ch_lock, lock_flags2);
		tty->termios.c_cflag = ((tty->termios.c_cflag & ~CLOCAL) |
						(arg ? CLOCAL : 0));
		dgap_param(tty);
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

		return 0;

	case TIOCMGET:
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return dgap_get_modem_info(ch, uarg);

	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return dgap_set_modem_info(tty, cmd, uarg);

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
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
			spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
			return rc;
		}

		if ((arg == TCIFLUSH) || (arg == TCIOFLUSH)) {
			if (!(un->un_type == DGAP_PRINT)) {
				head = readw(&(ch->ch_bs->rx_head));
				writew(head, &(ch->ch_bs->rx_tail));
				writeb(0, &(ch->ch_bs->orun));
			}
		}

		if ((arg != TCOFLUSH) && (arg != TCIOFLUSH)) {
			/* pretend we didn't recognize this IOCTL */
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
			spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

			return -ENOIOCTLCMD;
		}

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
		if (waitqueue_active(&tty->write_wait))
			wake_up_interruptible(&tty->write_wait);

		/* Can't hold any locks when calling tty_wakeup! */
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		tty_wakeup(tty);

		/* pretend we didn't recognize this IOCTL */
		return -ENOIOCTLCMD;

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
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		rc = dgap_wait_for_drain(tty);
		if (rc)
			return -EINTR;

		/* pretend we didn't recognize this */
		return -ENOIOCTLCMD;

	case TCSETAW:

		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		rc = dgap_wait_for_drain(tty);
		if (rc)
			return -EINTR;

		/* pretend we didn't recognize this */
		return -ENOIOCTLCMD;

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
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
			spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
			return rc;
		}

		switch (arg) {

		case TCOON:
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
			spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
			dgap_tty_start(tty);
			return 0;
		case TCOOFF:
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
			spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
			dgap_tty_stop(tty);
			return 0;
		case TCION:
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
			spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
			/* Make the ld do it */
			return -ENOIOCTLCMD;
		case TCIOFF:
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
			spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
			/* Make the ld do it */
			return -ENOIOCTLCMD;
		default:
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
			spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
			return -EINVAL;
		}

	case DIGI_GETA:
		/* get information for ditty */
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return dgap_tty_digigeta(tty, uarg);

	case DIGI_SETAW:
	case DIGI_SETAF:

		/* set information for ditty */
		if (cmd == (DIGI_SETAW)) {

			spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
			spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
			rc = dgap_wait_for_drain(tty);
			if (rc)
				return -EINTR;
			spin_lock_irqsave(&bd->bd_lock, lock_flags);
			spin_lock_irqsave(&ch->ch_lock, lock_flags2);
		} else
			tty_ldisc_flush(tty);
		/* fall thru */

	case DIGI_SETA:
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return dgap_tty_digiseta(tty, uarg);

	case DIGI_GEDELAY:
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return dgap_tty_digigetedelay(tty, uarg);

	case DIGI_SEDELAY:
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return dgap_tty_digisetedelay(tty, uarg);

	case DIGI_GETCUSTOMBAUD:
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return dgap_tty_digigetcustombaud(tty, uarg);

	case DIGI_SETCUSTOMBAUD:
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return dgap_tty_digisetcustombaud(tty, uarg);

	case DIGI_RESET_PORT:
		dgap_firmware_reset_port(ch);
		dgap_param(tty);
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return 0;

	default:
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

		return -ENOIOCTLCMD;
	}
}

static int dgap_alloc_flipbuf(struct board_t *brd)
{
	/*
	 * Initialize KME waitqueues...
	 */
	init_waitqueue_head(&brd->kme_wait);

	/*
	 * allocate flip buffer for board.
	 */
	brd->flipbuf = kmalloc(MYFLIPLEN, GFP_KERNEL);
	if (!brd->flipbuf)
		return -ENOMEM;

	brd->flipflagbuf = kmalloc(MYFLIPLEN, GFP_KERNEL);
	if (!brd->flipflagbuf) {
		kfree(brd->flipbuf);
		return -ENOMEM;
	}

	return 0;
}

static void dgap_free_flipbuf(struct board_t *brd)
{
	kfree(brd->flipbuf);
	kfree(brd->flipflagbuf);
}

/*
 * Create pr and tty device entries
 */
static int dgap_tty_register_ports(struct board_t *brd)
{
	struct channel_t *ch;
	int i;
	int ret;

	brd->serial_ports = kcalloc(brd->nasync, sizeof(*brd->serial_ports),
					GFP_KERNEL);
	if (!brd->serial_ports)
		return -ENOMEM;

	brd->printer_ports = kcalloc(brd->nasync, sizeof(*brd->printer_ports),
					GFP_KERNEL);
	if (!brd->printer_ports) {
		ret = -ENOMEM;
		goto free_serial_ports;
	}

	for (i = 0; i < brd->nasync; i++) {
		tty_port_init(&brd->serial_ports[i]);
		tty_port_init(&brd->printer_ports[i]);
	}

	ch = brd->channels[0];
	for (i = 0; i < brd->nasync; i++, ch = brd->channels[i]) {

		struct device *classp;

		classp = tty_port_register_device(&brd->serial_ports[i],
						  brd->serial_driver,
						  i, NULL);

		if (IS_ERR(classp)) {
			ret = PTR_ERR(classp);
			goto unregister_ttys;
		}

		dgap_create_tty_sysfs(&ch->ch_tun, classp);
		ch->ch_tun.un_sysfs = classp;

		classp = tty_port_register_device(&brd->printer_ports[i],
						  brd->print_driver,
						  i, NULL);

		if (IS_ERR(classp)) {
			ret = PTR_ERR(classp);
			goto unregister_ttys;
		}

		dgap_create_tty_sysfs(&ch->ch_pun, classp);
		ch->ch_pun.un_sysfs = classp;
	}
	dgap_create_ports_sysfiles(brd);

	return 0;

unregister_ttys:
	while (i >= 0) {
		ch = brd->channels[i];
		if (ch->ch_tun.un_sysfs) {
			dgap_remove_tty_sysfs(ch->ch_tun.un_sysfs);
			tty_unregister_device(brd->serial_driver, i);
		}

		if (ch->ch_pun.un_sysfs) {
			dgap_remove_tty_sysfs(ch->ch_pun.un_sysfs);
			tty_unregister_device(brd->print_driver, i);
		}
		i--;
	}

	for (i = 0; i < brd->nasync; i++) {
		tty_port_destroy(&brd->serial_ports[i]);
		tty_port_destroy(&brd->printer_ports[i]);
	}

	kfree(brd->printer_ports);
	brd->printer_ports = NULL;

free_serial_ports:
	kfree(brd->serial_ports);
	brd->serial_ports = NULL;

	return ret;
}

/*
 * Copies the BIOS code from the user to the board,
 * and starts the BIOS running.
 */
static void dgap_do_bios_load(struct board_t *brd, const u8 *ubios, int len)
{
	u8 __iomem *addr;
	uint offset;
	int i;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return;

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
	memcpy_toio(addr + offset, ubios, len);

	writel(0x0bf00401, addr);
	writel(0, (addr + 4));

	/* Clear the reset, and change states. */
	writeb(FEPCLR, brd->re_map_port);
}

/*
 * Checks to see if the BIOS completed running on the card.
 */
static int dgap_test_bios(struct board_t *brd)
{
	u8 __iomem *addr;
	u16 word;
	u16 err1;
	u16 err2;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return -EINVAL;

	addr = brd->re_map_membase;
	word = readw(addr + POSTAREA);

	/*
	 * It can take 5-6 seconds for a board to
	 * pass the bios self test and post results.
	 * Give it 10 seconds.
	 */
	brd->wait_for_bios = 0;
	while (brd->wait_for_bios < 1000) {
		/* Check to see if BIOS thinks board is good. (GD). */
		if (word == *(u16 *) "GD")
			return 0;
		msleep_interruptible(10);
		brd->wait_for_bios++;
		word = readw(addr + POSTAREA);
	}

	/* Gave up on board after too long of time taken */
	err1 = readw(addr + SEQUENCE);
	err2 = readw(addr + ERROR);
	pr_warn("dgap: %s failed diagnostics.  Error #(%x,%x).\n",
		brd->name, err1, err2);
	brd->state = BOARD_FAILED;
	brd->dpastatus = BD_NOBIOS;

	return -EIO;
}

/*
 * Copies the FEP code from the user to the board,
 * and starts the FEP running.
 */
static void dgap_do_fep_load(struct board_t *brd, const u8 *ufep, int len)
{
	u8 __iomem *addr;
	uint offset;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return;

	addr = brd->re_map_membase;

	/*
	 * Download FEP
	 */
	offset = 0x1000;
	memcpy_toio(addr + offset, ufep, len);

	/*
	 * If board is a concentrator product, we need to give
	 * it its config string describing how the concentrators look.
	 */
	if ((brd->type == PCX) || (brd->type == PEPC)) {
		u8 string[100];
		u8 __iomem *config;
		u8 *xconfig;
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

}

/*
 * Waits for the FEP to report thats its ready for us to use.
 */
static int dgap_test_fep(struct board_t *brd)
{
	u8 __iomem *addr;
	u16 word;
	u16 err1;
	u16 err2;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return -EINVAL;

	addr = brd->re_map_membase;
	word = readw(addr + FEPSTAT);

	/*
	 * It can take 2-3 seconds for the FEP to
	 * be up and running. Give it 5 secs.
	 */
	brd->wait_for_fep = 0;
	while (brd->wait_for_fep < 500) {
		/* Check to see if FEP is up and running now. */
		if (word == *(u16 *) "OS") {
			/*
			 * Check to see if the board can support FEP5+ commands.
			*/
			word = readw(addr + FEP5_PLUS);
			if (word == *(u16 *) "5A")
				brd->bd_flags |= BD_FEP5PLUS;

			return 0;
		}
		msleep_interruptible(10);
		brd->wait_for_fep++;
		word = readw(addr + FEPSTAT);
	}

	/* Gave up on board after too long of time taken */
	err1 = readw(addr + SEQUENCE);
	err2 = readw(addr + ERROR);
	pr_warn("dgap: FEPOS for %s not functioning.  Error #(%x,%x).\n",
		brd->name, err1, err2);
	brd->state = BOARD_FAILED;
	brd->dpastatus = BD_NOFEP;

	return -EIO;
}

/*
 * Physically forces the FEP5 card to reset itself.
 */
static void dgap_do_reset_board(struct board_t *brd)
{
	u8 check;
	u32 check1;
	u32 check2;
	int i;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) ||
	    !brd->re_map_membase || !brd->re_map_port)
		return;

	/* FEPRST does not vary among supported boards */
	writeb(FEPRST, brd->re_map_port);

	for (i = 0; i <= 1000; i++) {
		check = readb(brd->re_map_port) & 0xe;
		if (check == FEPRST)
			break;
		udelay(10);

	}
	if (i > 1000) {
		pr_warn("dgap: Board not resetting...  Failing board.\n");
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
		return;
	}

	/*
	 * Make sure there really is memory out there.
	 */
	writel(0xa55a3cc3, (brd->re_map_membase + LOWMEM));
	writel(0x5aa5c33c, (brd->re_map_membase + HIGHMEM));
	check1 = readl(brd->re_map_membase + LOWMEM);
	check2 = readl(brd->re_map_membase + HIGHMEM);

	if ((check1 != 0xa55a3cc3) || (check2 != 0x5aa5c33c)) {
		pr_warn("dgap: No memory at %p for board.\n",
			brd->re_map_membase);
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
		return;
	}
}

#ifdef DIGI_CONCENTRATORS_SUPPORTED
/*
 * Sends a concentrator image into the FEP5 board.
 */
static void dgap_do_conc_load(struct board_t *brd, u8 *uaddr, int len)
{
	char __iomem *vaddr;
	u16 offset;
	struct downld_t *to_dp;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return;

	vaddr = brd->re_map_membase;

	offset = readw((u16 *) (vaddr + DOWNREQ));
	to_dp = (struct downld_t *) (vaddr + (int) offset);
	memcpy_toio(to_dp, uaddr, len);

	/* Tell card we have data for it */
	writew(0, vaddr + (DOWNREQ));

	brd->conc_dl_status = NO_PENDING_CONCENTRATOR_REQUESTS;
}
#endif

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
	u8 byte1;
	u8 byte2;

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
			 * The VPD offset is located inside the ROM Data
			 * Structure.
			 *
			 * We also have to remember the length of each
			 * ROM Data Structure, so we can "hop" to the next
			 * entry if the VPD isn't in the current
			 * ROM Data Structure.
			 */
			rom_offset = readw(brd->re_map_membase +
						base_offset + 0x18);
			image_length = readw(brd->re_map_membase +
						rom_offset + 0x10) * 512;
			vpd_offset = readw(brd->re_map_membase +
						rom_offset + 0x08);

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
			for (i = 0; i < VPDSIZE; i++) {
				brd->vpd[i] = readb(brd->re_map_membase +
							vpd_offset + i);
			}
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
static void dgap_poll_tasklet(unsigned long data)
{
	struct board_t *bd = (struct board_t *) data;
	ulong lock_flags;
	char __iomem *vaddr;
	u16 head, tail;

	if (!bd || (bd->magic != DGAP_BOARD_MAGIC))
		return;

	if (bd->inhibit_poller)
		return;

	spin_lock_irqsave(&bd->bd_lock, lock_flags);

	vaddr = bd->re_map_membase;

	/*
	 * If board is ready, parse deeper to see if there is anything to do.
	 */
	if (bd->state == BOARD_READY) {

		struct ev_t __iomem *eaddr;

		if (!bd->re_map_membase) {
			spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
			return;
		}
		if (!bd->re_map_port) {
			spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
			return;
		}

		if (!bd->nasync)
			goto out;

		eaddr = (struct ev_t __iomem *) (vaddr + EVBUF);

		/* Get our head and tail */
		head = readw(&(eaddr->ev_head));
		tail = readw(&(eaddr->ev_tail));

		/*
		 * If there is an event pending. Go service it.
		 */
		if (head != tail) {
			spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
			dgap_event(bd);
			spin_lock_irqsave(&bd->bd_lock, lock_flags);
		}

out:
		/*
		 * If board is doing interrupts, ACK the interrupt.
		 */
		if (bd && bd->intr_running)
			readb(bd->re_map_port + 2);

		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return;
	}

	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
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
static void dgap_cmdb(struct channel_t *ch, u8 cmd, u8 byte1,
			u8 byte2, uint ncmds)
{
	char __iomem *vaddr;
	struct __iomem cm_t *cm_addr;
	uint count;
	uint n;
	u16 head;
	u16 tail;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	/*
	 * Check if board is still alive.
	 */
	if (ch->ch_bd->state == BOARD_FAILED)
		return;

	/*
	 * Make sure the pointers are in range before
	 * writing to the FEP memory.
	 */
	vaddr = ch->ch_bd->re_map_membase;

	if (!vaddr)
		return;

	cm_addr = (struct cm_t __iomem *) (vaddr + CMDBUF);
	head = readw(&(cm_addr->cm_head));

	/*
	 * Forget it if pointers out of range.
	 */
	if (head >= (CMDMAX - CMDSTART) || (head & 03)) {
		ch->ch_bd->state = BOARD_FAILED;
		return;
	}

	/*
	 * Put the data in the circular command buffer.
	 */
	writeb(cmd, (vaddr + head + CMDSTART + 0));
	writeb((u8) ch->ch_portnum, (vaddr + head + CMDSTART + 1));
	writeb(byte1, (vaddr + head + CMDSTART + 2));
	writeb(byte2, (vaddr + head + CMDSTART + 3));

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
static void dgap_cmdw(struct channel_t *ch, u8 cmd, u16 word, uint ncmds)
{
	char __iomem *vaddr;
	struct __iomem cm_t *cm_addr;
	uint count;
	uint n;
	u16 head;
	u16 tail;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	/*
	 * Check if board is still alive.
	 */
	if (ch->ch_bd->state == BOARD_FAILED)
		return;

	/*
	 * Make sure the pointers are in range before
	 * writing to the FEP memory.
	 */
	vaddr = ch->ch_bd->re_map_membase;
	if (!vaddr)
		return;

	cm_addr = (struct cm_t __iomem *) (vaddr + CMDBUF);
	head = readw(&(cm_addr->cm_head));

	/*
	 * Forget it if pointers out of range.
	 */
	if (head >= (CMDMAX - CMDSTART) || (head & 03)) {
		ch->ch_bd->state = BOARD_FAILED;
		return;
	}

	/*
	 * Put the data in the circular command buffer.
	 */
	writeb(cmd, (vaddr + head + CMDSTART + 0));
	writeb((u8) ch->ch_portnum, (vaddr + head + CMDSTART + 1));
	writew((u16) word, (vaddr + head + CMDSTART + 2));

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
	char __iomem *vaddr;
	struct __iomem cm_t *cm_addr;
	uint count;
	uint n;
	u16 head;
	u16 tail;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	/*
	 * Check if board is still alive.
	 */
	if (ch->ch_bd->state == BOARD_FAILED)
		return;

	/*
	 * Make sure the pointers are in range before
	 * writing to the FEP memory.
	 */
	vaddr = ch->ch_bd->re_map_membase;
	if (!vaddr)
		return;

	cm_addr = (struct cm_t __iomem *) (vaddr + CMDBUF);
	head = readw(&(cm_addr->cm_head));

	/*
	 * Forget it if pointers out of range.
	 */
	if (head >= (CMDMAX - CMDSTART) || (head & 03)) {
		ch->ch_bd->state = BOARD_FAILED;
		return;
	}

	/*
	 * Put the data in the circular command buffer.
	 */

	/* Write an FF to tell the FEP that we want an extended command */
	writeb((u8) 0xff, (vaddr + head + CMDSTART + 0));

	writeb((u8) ch->ch_portnum, (vaddr + head + CMDSTART + 1));
	writew((u16) cmd, (vaddr + head + CMDSTART + 2));

	/*
	 * If the second part of the command won't fit,
	 * put it at the beginning of the circular buffer.
	 */
	if (((head + 4) >= ((CMDMAX - CMDSTART)) || (head & 03)))
		writew((u16) word, (vaddr + CMDSTART));
	else
		writew((u16) word, (vaddr + head + CMDSTART + 4));

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
static void dgap_wmove(struct channel_t *ch, char *buf, uint cnt)
{
	int n;
	char __iomem *taddr;
	struct bs_t __iomem *bs;
	u16 head;

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
	if ((cnt > ch->ch_tsize) ||
	    (unsigned)(head - ch->ch_tstart) >= ch->ch_tsize)
		return;

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
static uint dgap_get_custom_baud(struct channel_t *ch)
{
	u8 __iomem *vaddr;
	ulong offset;
	uint value;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;

	if (!ch->ch_bd || ch->ch_bd->magic != DGAP_BOARD_MAGIC)
		return 0;

	if (!(ch->ch_bd->bd_flags & BD_FEP5PLUS))
		return 0;

	vaddr = ch->ch_bd->re_map_membase;

	if (!vaddr)
		return 0;

	/*
	 * Go get from fep mem, what the fep
	 * believes the custom baud rate is.
	 */
	offset = (ioread16(vaddr + ECS_SEG) << 4) + (ch->ch_portnum * 0x28)
	       + LINE_SPEED;

	value = readw(vaddr + offset);
	return value;
}

/*
 * Calls the firmware to reset this channel.
 */
static void dgap_firmware_reset_port(struct channel_t *ch)
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
static int dgap_param(struct tty_struct *tty)
{
	struct ktermios *ts;
	struct board_t *bd;
	struct channel_t *ch;
	struct bs_t __iomem *bs;
	struct un_t *un;
	u16 head;
	u16 cflag;
	u16 iflag;
	u8 mval;
	u8 hflow;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EIO;

	un = (struct un_t *) tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return -EIO;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return -EIO;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return -EIO;

	bs = ch->ch_bs;
	if (!bs)
		return -EIO;

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

		dgap_cmdw_ext(ch, 0xff01, ch->ch_custom_speed, 0);

		/*
		 * Now go get from fep mem, what the fep
		 * believes the custom baud rate is.
		 */
		ch->ch_custom_speed = dgap_get_custom_baud(ch);
		ch->ch_baud_info = ch->ch_custom_speed;

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

		/*
		 * Only use the TXPrint baud rate if the
		 * terminal unit is NOT open
		 */
		if (!(ch->ch_tun.un_flags & UN_ISOPEN) &&
		     (un->un_type == DGAP_PRINT))
			baud = C_BAUD(ch->ch_pun.un_tty) & 0xff;
		else
			baud = C_BAUD(ch->ch_tun.un_tty) & 0xff;

		if (ch->ch_c_cflag & CBAUDEX)
			iindex = 1;

		if (ch->ch_digi.digi_flags & DIGI_FAST)
			iindex += 2;

		jindex = baud;

		if ((iindex >= 0) && (iindex < 4) &&
		    (jindex >= 0) && (jindex < 16))
			baud = bauds[iindex][jindex];
		else
			baud = 0;

		if (baud == 0)
			baud = 9600;

		ch->ch_baud_info = baud;

		/*
		 * CBAUD has bit position 0x1000 set these days to
		 * indicate Linux baud rate remap.
		 * We use a different bit assignment for high speed.
		 * Clear this bit out while grabbing the parts of
		 * "cflag" we want.
		 */
		cflag = ch->ch_c_cflag & ((CBAUD ^ CBAUDEX) | PARODD | PARENB |
						   CSTOPB | CSIZE);

		/*
		 * HUPCL bit is used by FEP to indicate fast baud
		 * table is to be used.
		 */
		if ((ch->ch_digi.digi_flags & DIGI_FAST) ||
		    (ch->ch_c_cflag & CBAUDEX))
			cflag |= HUPCL;

		if ((ch->ch_c_cflag & CBAUDEX) &&
		    !(ch->ch_digi.digi_flags & DIGI_FAST)) {
			/*
			 * The below code is trying to guarantee that only
			 * baud rates 115200, 230400, 460800, 921600 are
			 * remapped. We use exclusive or  because the various
			 * baud rates share common bit positions and therefore
			 * can't be tested for easily.
			 */
			tcflag_t tcflag = (ch->ch_c_cflag & CBAUD) | CBAUDEX;
			int baudpart = 0;

			/*
			 * Map high speed requests to index
			 * into FEP's baud table
			 */
			switch (tcflag) {
			case B57600:
				baudpart = 1;
				break;
#ifdef B76800
			case B76800:
				baudpart = 2;
				break;
#endif
			case B115200:
				baudpart = 3;
				break;
			case B230400:
				baudpart = 9;
				break;
			case B460800:
				baudpart = 11;
				break;
#ifdef B921600
			case B921600:
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

			/*
			 * Okay to have channel and board
			 * locks held calling this
			 */
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
	iflag = ch->ch_c_iflag & (IGNBRK | BRKINT | IGNPAR | PARMRK |
				  INPCK | ISTRIP | IXON | IXANY | IXOFF);

	if ((ch->ch_startc == _POSIX_VDISABLE) ||
	    (ch->ch_stopc == _POSIX_VDISABLE)) {
		iflag &= ~(IXON | IXOFF);
		ch->ch_c_iflag &= ~(IXON | IXOFF);
	}

	/*
	 * Only the IBM Xr card can switch between
	 * 232 and 422 modes on the fly
	 */
	if (bd->device == PCI_DEV_XR_IBM_DID) {
		if (ch->ch_digi.digi_flags & DIGI_422)
			dgap_cmdb(ch, SCOMMODE, MODE_422, 0, 0);
		else
			dgap_cmdb(ch, SCOMMODE, MODE_232, 0, 0);
	}

	if (ch->ch_digi.digi_flags & DIGI_ALTPIN)
		iflag |= IALTPIN;

	if (iflag != ch->ch_fepiflag) {
		ch->ch_fepiflag = iflag;

		/* Okay to have channel and board locks held calling this */
		dgap_cmdw(ch, SIFLAG, (u16) ch->ch_fepiflag, 0);
	}

	/*
	 * Select hardware handshaking.
	 */
	hflow = 0;

	if (ch->ch_c_cflag & CRTSCTS)
		hflow |= (D_RTS(ch) | D_CTS(ch));
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
		dgap_cmdb(ch, SHFLOW, (u8) hflow, 0xff, 0);
	}

	/*
	 * Set RTS and/or DTR Toggle if needed,
	 * but only if product is FEP5+ based.
	 */
	if (bd->bd_flags & BD_FEP5PLUS) {
		u16 hflow2 = 0;
		if (ch->ch_digi.digi_flags & DIGI_RTS_TOGGLE)
			hflow2 |= (D_RTS(ch));
		if (ch->ch_digi.digi_flags & DIGI_DTR_TOGGLE)
			hflow2 |= (D_DTR(ch));

		dgap_cmdw_ext(ch, 0xff03, hflow2, 0);
	}

	/*
	 * Set modem control lines.
	 */

	mval ^= ch->ch_mforce & (mval ^ ch->ch_mval);

	if (ch->ch_mostat ^ mval) {
		ch->ch_mostat = mval;

		/* Okay to have channel and board locks held calling this */
		dgap_cmdb(ch, SMODEM, (u8) mval, D_RTS(ch)|D_DTR(ch), 0);
	}

	/*
	 * Read modem signals, and then call carrier function.
	 */
	ch->ch_mistat = readb(&(bs->m_stat));
	dgap_carrier(ch);

	/*
	 * Set the start and stop characters.
	 */
	if (ch->ch_startc != ch->ch_fepstartc ||
	    ch->ch_stopc != ch->ch_fepstopc) {
		ch->ch_fepstartc = ch->ch_startc;
		ch->ch_fepstopc =  ch->ch_stopc;

		/* Okay to have channel and board locks held calling this */
		dgap_cmdb(ch, SFLOWC, ch->ch_fepstartc, ch->ch_fepstopc, 0);
	}

	/*
	 * Set the Auxiliary start and stop characters.
	 */
	if (ch->ch_astartc != ch->ch_fepastartc ||
	    ch->ch_astopc != ch->ch_fepastopc) {
		ch->ch_fepastartc = ch->ch_astartc;
		ch->ch_fepastopc = ch->ch_astopc;

		/* Okay to have channel and board locks held calling this */
		dgap_cmdb(ch, SAFLOWC, ch->ch_fepastartc, ch->ch_fepastopc, 0);
	}

	return 0;
}

/*
 * dgap_parity_scan()
 *
 * Convert the FEP5 way of reporting parity errors and breaks into
 * the Linux line discipline way.
 */
static void dgap_parity_scan(struct channel_t *ch, unsigned char *cbuf,
				unsigned char *fbuf, int *len)
{
	int l = *len;
	int count = 0;
	unsigned char *in, *cout, *fout;
	unsigned char c;

	in = cbuf;
	cout = cbuf;
	fout = fbuf;

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
			if (c == (unsigned char) '\377')
				/* delete this character from stream */
				ch->pscan_state = 1;
			else {
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
					ch->ch_err_break++;
					*fout++ = TTY_BREAK;
				} else {
					ch->ch_err_parity++;
					*fout++ = TTY_PARITY;
				}
			}

			count += 1;
			ch->pscan_state = 0;
		}
	}
	*len = count;
}

static void dgap_write_wakeup(struct board_t *bd, struct channel_t *ch,
			      struct un_t *un, u32 mask,
			      unsigned long *irq_flags1,
			      unsigned long *irq_flags2)
{
	if (!(un->un_flags & mask))
		return;

	un->un_flags &= ~mask;

	if (!(un->un_flags & UN_ISOPEN))
		return;

	if ((un->un_tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    un->un_tty->ldisc->ops->write_wakeup) {
		spin_unlock_irqrestore(&ch->ch_lock, *irq_flags2);
		spin_unlock_irqrestore(&bd->bd_lock, *irq_flags1);

		(un->un_tty->ldisc->ops->write_wakeup)(un->un_tty);

		spin_lock_irqsave(&bd->bd_lock, *irq_flags1);
		spin_lock_irqsave(&ch->ch_lock, *irq_flags2);
	}
	wake_up_interruptible(&un->un_tty->write_wait);
	wake_up_interruptible(&un->un_flags_wait);
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
	ulong lock_flags;
	ulong lock_flags2;
	struct bs_t __iomem *bs;
	u8 __iomem *event;
	u8 __iomem *vaddr;
	struct ev_t __iomem *eaddr;
	uint head;
	uint tail;
	int port;
	int reason;
	int modem;
	int b1;

	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return -EIO;

	spin_lock_irqsave(&bd->bd_lock, lock_flags);

	vaddr = bd->re_map_membase;

	if (!vaddr) {
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return -EIO;
	}

	eaddr = (struct ev_t __iomem *) (vaddr + EVBUF);

	/* Get our head and tail */
	head = readw(&(eaddr->ev_head));
	tail = readw(&(eaddr->ev_tail));

	/*
	 * Forget it if pointers out of range.
	 */

	if (head >= EVMAX - EVSTART || tail >= EVMAX - EVSTART ||
	    (head | tail) & 03) {
		/* Let go of board lock */
		spin_unlock_irqrestore(&bd->bd_lock, lock_flags);
		return -EIO;
	}

	/*
	 * Loop to process all the events in the buffer.
	 */
	while (tail != head) {

		/*
		 * Get interrupt information.
		 */

		event = bd->re_map_membase + tail + EVSTART;

		port   = ioread8(event);
		reason = ioread8(event + 1);
		modem  = ioread8(event + 2);
		b1     = ioread8(event + 3);

		/*
		 * Make sure the interrupt is valid.
		 */
		if (port >= bd->nasync)
			goto next;

		if (!(reason & (IFMODEM | IFBREAK | IFTLW | IFTEM | IFDATA)))
			goto next;

		ch = bd->channels[port];

		if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
			goto next;

		/*
		 * If we have made it here, the event was valid.
		 * Lock down the channel.
		 */
		spin_lock_irqsave(&ch->ch_lock, lock_flags2);

		bs = ch->ch_bs;

		if (!bs) {
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
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
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);
			spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

			dgap_input(ch);

			spin_lock_irqsave(&bd->bd_lock, lock_flags);
			spin_lock_irqsave(&ch->ch_lock, lock_flags2);

			if (ch->ch_flags & CH_RACTIVE)
				ch->ch_flags |= CH_RENABLE;
			else
				writeb(1, &(bs->idata));

			if (ch->ch_flags & CH_RWAIT) {
				ch->ch_flags &= ~CH_RWAIT;

				wake_up_interruptible
					(&ch->ch_tun.un_flags_wait);
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

			if (ch->ch_tun.un_tty) {
				/* A break has been indicated */
				ch->ch_err_break++;
				tty_buffer_request_room
					(ch->ch_tun.un_tty->port, 1);
				tty_insert_flip_char(ch->ch_tun.un_tty->port,
						     0, TTY_BREAK);
				tty_flip_buffer_push(ch->ch_tun.un_tty->port);
			}
		}

		/*
		 * Process Transmit low.
		 */
		if (reason & IFTLW) {
			dgap_write_wakeup(bd, ch, &ch->ch_tun, UN_LOW,
					  &lock_flags, &lock_flags2);
			dgap_write_wakeup(bd, ch, &ch->ch_pun, UN_LOW,
					  &lock_flags, &lock_flags2);
			if (ch->ch_flags & CH_WLOW) {
				ch->ch_flags &= ~CH_WLOW;
				wake_up_interruptible(&ch->ch_flags_wait);
			}
		}

		/*
		 * Process Transmit empty.
		 */
		if (reason & IFTEM) {
			dgap_write_wakeup(bd, ch, &ch->ch_tun, UN_EMPTY,
					  &lock_flags, &lock_flags2);
			dgap_write_wakeup(bd, ch, &ch->ch_pun, UN_EMPTY,
					  &lock_flags, &lock_flags2);
			if (ch->ch_flags & CH_WEMPTY) {
				ch->ch_flags &= ~CH_WEMPTY;
				wake_up_interruptible(&ch->ch_flags_wait);
			}
		}

		spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);

next:
		tail = (tail + 4) & (EVMAX - EVSTART - 4);
	}

	writew(tail, &(eaddr->ev_tail));
	spin_unlock_irqrestore(&bd->bd_lock, lock_flags);

	return 0;
}

static ssize_t dgap_driver_version_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", DG_PART);
}
static DRIVER_ATTR(version, S_IRUSR, dgap_driver_version_show, NULL);


static ssize_t dgap_driver_boards_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", dgap_numboards);
}
static DRIVER_ATTR(boards, S_IRUSR, dgap_driver_boards_show, NULL);


static ssize_t dgap_driver_maxboards_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", MAXBOARDS);
}
static DRIVER_ATTR(maxboards, S_IRUSR, dgap_driver_maxboards_show, NULL);


static ssize_t dgap_driver_pollcounter_show(struct device_driver *ddp,
					    char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%ld\n", dgap_poll_counter);
}
static DRIVER_ATTR(pollcounter, S_IRUSR, dgap_driver_pollcounter_show, NULL);

static ssize_t dgap_driver_pollrate_show(struct device_driver *ddp, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%dms\n", dgap_poll_tick);
}

static ssize_t dgap_driver_pollrate_store(struct device_driver *ddp,
					  const char *buf, size_t count)
{
	if (sscanf(buf, "%d\n", &dgap_poll_tick) != 1)
		return -EINVAL;
	return count;
}
static DRIVER_ATTR(pollrate, (S_IRUSR | S_IWUSR), dgap_driver_pollrate_show,
		   dgap_driver_pollrate_store);

static int dgap_create_driver_sysfiles(struct pci_driver *dgap_driver)
{
	int rc = 0;
	struct device_driver *driverfs = &dgap_driver->driver;

	rc |= driver_create_file(driverfs, &driver_attr_version);
	rc |= driver_create_file(driverfs, &driver_attr_boards);
	rc |= driver_create_file(driverfs, &driver_attr_maxboards);
	rc |= driver_create_file(driverfs, &driver_attr_pollrate);
	rc |= driver_create_file(driverfs, &driver_attr_pollcounter);

	return rc;
}

static void dgap_remove_driver_sysfiles(struct pci_driver *dgap_driver)
{
	struct device_driver *driverfs = &dgap_driver->driver;
	driver_remove_file(driverfs, &driver_attr_version);
	driver_remove_file(driverfs, &driver_attr_boards);
	driver_remove_file(driverfs, &driver_attr_maxboards);
	driver_remove_file(driverfs, &driver_attr_pollrate);
	driver_remove_file(driverfs, &driver_attr_pollcounter);
}

static struct board_t *dgap_verify_board(struct device *p)
{
	struct board_t *bd;

	if (!p)
		return NULL;

	bd = dev_get_drvdata(p);
	if (!bd || bd->magic != DGAP_BOARD_MAGIC || bd->state != BOARD_READY)
		return NULL;

	return bd;
}

static ssize_t dgap_ports_state_show(struct device *p,
				     struct device_attribute *attr,
				     char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i;

	bd = dgap_verify_board(p);
	if (!bd)
		return 0;

	for (i = 0; i < bd->nasync; i++) {
		count += snprintf(buf + count, PAGE_SIZE - count,
			"%d %s\n", bd->channels[i]->ch_portnum,
			bd->channels[i]->ch_open_count ? "Open" : "Closed");
	}
	return count;
}
static DEVICE_ATTR(ports_state, S_IRUSR, dgap_ports_state_show, NULL);

static ssize_t dgap_ports_baud_show(struct device *p,
				    struct device_attribute *attr,
				    char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i;

	bd = dgap_verify_board(p);
	if (!bd)
		return 0;

	for (i = 0; i < bd->nasync; i++) {
		count +=  snprintf(buf + count, PAGE_SIZE - count, "%d %d\n",
				   bd->channels[i]->ch_portnum,
				   bd->channels[i]->ch_baud_info);
	}
	return count;
}
static DEVICE_ATTR(ports_baud, S_IRUSR, dgap_ports_baud_show, NULL);

static ssize_t dgap_ports_msignals_show(struct device *p,
					struct device_attribute *attr,
					char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i;

	bd = dgap_verify_board(p);
	if (!bd)
		return 0;

	for (i = 0; i < bd->nasync; i++) {
		if (bd->channels[i]->ch_open_count)
			count += snprintf(buf + count, PAGE_SIZE - count,
				"%d %s %s %s %s %s %s\n",
				bd->channels[i]->ch_portnum,
				(bd->channels[i]->ch_mostat &
				 UART_MCR_RTS) ? "RTS" : "",
				(bd->channels[i]->ch_mistat &
				 UART_MSR_CTS) ? "CTS" : "",
				(bd->channels[i]->ch_mostat &
				 UART_MCR_DTR) ? "DTR" : "",
				(bd->channels[i]->ch_mistat &
				 UART_MSR_DSR) ? "DSR" : "",
				(bd->channels[i]->ch_mistat &
				 UART_MSR_DCD) ? "DCD" : "",
				(bd->channels[i]->ch_mistat &
				 UART_MSR_RI)  ? "RI"  : "");
		else
			count += snprintf(buf + count, PAGE_SIZE - count,
				"%d\n", bd->channels[i]->ch_portnum);
	}
	return count;
}
static DEVICE_ATTR(ports_msignals, S_IRUSR, dgap_ports_msignals_show, NULL);

static ssize_t dgap_ports_iflag_show(struct device *p,
				     struct device_attribute *attr,
				     char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i;

	bd = dgap_verify_board(p);
	if (!bd)
		return 0;

	for (i = 0; i < bd->nasync; i++)
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %x\n",
				  bd->channels[i]->ch_portnum,
				  bd->channels[i]->ch_c_iflag);
	return count;
}
static DEVICE_ATTR(ports_iflag, S_IRUSR, dgap_ports_iflag_show, NULL);

static ssize_t dgap_ports_cflag_show(struct device *p,
				     struct device_attribute *attr,
				     char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i;

	bd = dgap_verify_board(p);
	if (!bd)
		return 0;

	for (i = 0; i < bd->nasync; i++)
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %x\n",
				  bd->channels[i]->ch_portnum,
				  bd->channels[i]->ch_c_cflag);
	return count;
}
static DEVICE_ATTR(ports_cflag, S_IRUSR, dgap_ports_cflag_show, NULL);

static ssize_t dgap_ports_oflag_show(struct device *p,
				     struct device_attribute *attr,
				     char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i;

	bd = dgap_verify_board(p);
	if (!bd)
		return 0;

	for (i = 0; i < bd->nasync; i++)
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %x\n",
				  bd->channels[i]->ch_portnum,
				  bd->channels[i]->ch_c_oflag);
	return count;
}
static DEVICE_ATTR(ports_oflag, S_IRUSR, dgap_ports_oflag_show, NULL);

static ssize_t dgap_ports_lflag_show(struct device *p,
				     struct device_attribute *attr,
				     char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i;

	bd = dgap_verify_board(p);
	if (!bd)
		return 0;

	for (i = 0; i < bd->nasync; i++)
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %x\n",
				  bd->channels[i]->ch_portnum,
				  bd->channels[i]->ch_c_lflag);
	return count;
}
static DEVICE_ATTR(ports_lflag, S_IRUSR, dgap_ports_lflag_show, NULL);

static ssize_t dgap_ports_digi_flag_show(struct device *p,
					 struct device_attribute *attr,
					 char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i;

	bd = dgap_verify_board(p);
	if (!bd)
		return 0;

	for (i = 0; i < bd->nasync; i++)
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %x\n",
				  bd->channels[i]->ch_portnum,
				  bd->channels[i]->ch_digi.digi_flags);
	return count;
}
static DEVICE_ATTR(ports_digi_flag, S_IRUSR, dgap_ports_digi_flag_show, NULL);

static ssize_t dgap_ports_rxcount_show(struct device *p,
				       struct device_attribute *attr,
				       char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i;

	bd = dgap_verify_board(p);
	if (!bd)
		return 0;

	for (i = 0; i < bd->nasync; i++)
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %ld\n",
				  bd->channels[i]->ch_portnum,
				  bd->channels[i]->ch_rxcount);
	return count;
}
static DEVICE_ATTR(ports_rxcount, S_IRUSR, dgap_ports_rxcount_show, NULL);

static ssize_t dgap_ports_txcount_show(struct device *p,
				       struct device_attribute *attr,
				       char *buf)
{
	struct board_t *bd;
	int count = 0;
	int i;

	bd = dgap_verify_board(p);
	if (!bd)
		return 0;

	for (i = 0; i < bd->nasync; i++)
		count += snprintf(buf + count, PAGE_SIZE - count, "%d %ld\n",
				  bd->channels[i]->ch_portnum,
				  bd->channels[i]->ch_txcount);
	return count;
}
static DEVICE_ATTR(ports_txcount, S_IRUSR, dgap_ports_txcount_show, NULL);

/* this function creates the sys files that will export each signal status
 * to sysfs each value will be put in a separate filename
 */
static void dgap_create_ports_sysfiles(struct board_t *bd)
{
	dev_set_drvdata(&bd->pdev->dev, bd);
	device_create_file(&(bd->pdev->dev), &dev_attr_ports_state);
	device_create_file(&(bd->pdev->dev), &dev_attr_ports_baud);
	device_create_file(&(bd->pdev->dev), &dev_attr_ports_msignals);
	device_create_file(&(bd->pdev->dev), &dev_attr_ports_iflag);
	device_create_file(&(bd->pdev->dev), &dev_attr_ports_cflag);
	device_create_file(&(bd->pdev->dev), &dev_attr_ports_oflag);
	device_create_file(&(bd->pdev->dev), &dev_attr_ports_lflag);
	device_create_file(&(bd->pdev->dev), &dev_attr_ports_digi_flag);
	device_create_file(&(bd->pdev->dev), &dev_attr_ports_rxcount);
	device_create_file(&(bd->pdev->dev), &dev_attr_ports_txcount);
}

/* removes all the sys files created for that port */
static void dgap_remove_ports_sysfiles(struct board_t *bd)
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

static ssize_t dgap_tty_state_show(struct device *d,
				   struct device_attribute *attr,
				   char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return 0;
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return 0;
	if (bd->state != BOARD_READY)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%s", un->un_open_count ?
			"Open" : "Closed");
}
static DEVICE_ATTR(state, S_IRUSR, dgap_tty_state_show, NULL);

static ssize_t dgap_tty_baud_show(struct device *d,
				  struct device_attribute *attr,
				  char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return 0;
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return 0;
	if (bd->state != BOARD_READY)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%d\n", ch->ch_baud_info);
}
static DEVICE_ATTR(baud, S_IRUSR, dgap_tty_baud_show, NULL);

static ssize_t dgap_tty_msignals_show(struct device *d,
				      struct device_attribute *attr,
				      char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return 0;
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return 0;
	if (bd->state != BOARD_READY)
		return 0;

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

static ssize_t dgap_tty_iflag_show(struct device *d,
				   struct device_attribute *attr,
				   char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return 0;
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return 0;
	if (bd->state != BOARD_READY)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_c_iflag);
}
static DEVICE_ATTR(iflag, S_IRUSR, dgap_tty_iflag_show, NULL);

static ssize_t dgap_tty_cflag_show(struct device *d,
				   struct device_attribute *attr,
				   char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return 0;
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return 0;
	if (bd->state != BOARD_READY)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_c_cflag);
}
static DEVICE_ATTR(cflag, S_IRUSR, dgap_tty_cflag_show, NULL);

static ssize_t dgap_tty_oflag_show(struct device *d,
				   struct device_attribute *attr,
				   char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return 0;
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return 0;
	if (bd->state != BOARD_READY)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_c_oflag);
}
static DEVICE_ATTR(oflag, S_IRUSR, dgap_tty_oflag_show, NULL);

static ssize_t dgap_tty_lflag_show(struct device *d,
				   struct device_attribute *attr,
				   char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return 0;
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return 0;
	if (bd->state != BOARD_READY)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_c_lflag);
}
static DEVICE_ATTR(lflag, S_IRUSR, dgap_tty_lflag_show, NULL);

static ssize_t dgap_tty_digi_flag_show(struct device *d,
				       struct device_attribute *attr,
				       char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return 0;
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return 0;
	if (bd->state != BOARD_READY)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%x\n", ch->ch_digi.digi_flags);
}
static DEVICE_ATTR(digi_flag, S_IRUSR, dgap_tty_digi_flag_show, NULL);

static ssize_t dgap_tty_rxcount_show(struct device *d,
				     struct device_attribute *attr,
				     char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return 0;
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return 0;
	if (bd->state != BOARD_READY)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%ld\n", ch->ch_rxcount);
}
static DEVICE_ATTR(rxcount, S_IRUSR, dgap_tty_rxcount_show, NULL);

static ssize_t dgap_tty_txcount_show(struct device *d,
				     struct device_attribute *attr,
				     char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;

	if (!d)
		return 0;
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return 0;
	if (bd->state != BOARD_READY)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%ld\n", ch->ch_txcount);
}
static DEVICE_ATTR(txcount, S_IRUSR, dgap_tty_txcount_show, NULL);

static ssize_t dgap_tty_name_show(struct device *d,
				  struct device_attribute *attr,
				  char *buf)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	int cn;
	int bn;
	struct cnode *cptr;
	int found = FALSE;
	int ncount = 0;
	int starto = 0;
	int i;

	if (!d)
		return 0;
	un = dev_get_drvdata(d);
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return 0;
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return 0;
	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return 0;
	if (bd->state != BOARD_READY)
		return 0;

	bn = bd->boardnum;
	cn = ch->ch_portnum;

	for (cptr = bd->bd_config; cptr; cptr = cptr->next) {

		if ((cptr->type == BNODE) &&
		    ((cptr->u.board.type == APORT2_920P) ||
		     (cptr->u.board.type == APORT4_920P) ||
		     (cptr->u.board.type == APORT8_920P) ||
		     (cptr->u.board.type == PAPORT4) ||
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
			} else
				ptr1 = cptr->u.ttyname;

			for (i = 0; i < dgap_config_get_num_prts(bd); i++) {
				if (cn != i)
					continue;

				return snprintf(buf, PAGE_SIZE, "%s%s%02d\n",
						(un->un_type == DGAP_PRINT) ?
						 "pr" : "tty",
						ptr1, i + starto);
			}
		}

		if (cptr->type == CNODE) {

			for (i = 0; i < cptr->u.conc.nport; i++) {
				if (cn != (i + ncount))
					continue;

				return snprintf(buf, PAGE_SIZE, "%s%s%02ld\n",
						(un->un_type == DGAP_PRINT) ?
						 "pr" : "tty",
						cptr->u.conc.id,
						i + (cptr->u.conc.v_start ?
						     cptr->u.conc.start : 1));
			}

			ncount += cptr->u.conc.nport;
		}

		if (cptr->type == MNODE) {

			for (i = 0; i < cptr->u.module.nport; i++) {
				if (cn != (i + ncount))
					continue;

				return snprintf(buf, PAGE_SIZE, "%s%s%02ld\n",
						(un->un_type == DGAP_PRINT) ?
						 "pr" : "tty",
						cptr->u.module.id,
						i + (cptr->u.module.v_start ?
						     cptr->u.module.start : 1));
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

static void dgap_create_tty_sysfs(struct un_t *un, struct device *c)
{
	int ret;

	ret = sysfs_create_group(&c->kobj, &dgap_tty_attribute_group);
	if (ret)
		return;

	dev_set_drvdata(c, un);

}

static void dgap_remove_tty_sysfs(struct device *c)
{
	sysfs_remove_group(&c->kobj, &dgap_tty_attribute_group);
}

/*
 * Parse a configuration file read into memory as a string.
 */
static int dgap_parsefile(char **in)
{
	struct cnode *p, *brd, *line, *conc;
	int rc;
	char *s;
	int linecnt = 0;

	p = &dgap_head;
	brd = line = conc = NULL;

	/* perhaps we are adding to an existing list? */
	while (p->next)
		p = p->next;

	/* file must start with a BEGIN */
	while ((rc = dgap_gettok(in, p)) != BEGIN) {
		if (rc == 0) {
			dgap_err("unexpected EOF");
			return -1;
		}
	}

	for (; ;) {
		rc = dgap_gettok(in, p);
		if (rc == 0) {
			dgap_err("unexpected EOF");
			return -1;
		}

		switch (rc) {
		case 0:
			dgap_err("unexpected end of file");
			return -1;

		case BEGIN:	/* should only be 1 begin */
			dgap_err("unexpected config_begin\n");
			return -1;

		case END:
			return 0;

		case BOARD:	/* board info */
			if (dgap_checknode(p))
				return -1;
			p->next = dgap_newnode(BNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;

			p->u.board.status = kstrdup("No", GFP_KERNEL);
			line = conc = NULL;
			brd = p;
			linecnt = -1;
			break;

		case APORT2_920P:	/* AccelePort_4 */
			if (p->type != BNODE) {
				dgap_err("unexpected Digi_2r_920 string");
				return -1;
			}
			p->u.board.type = APORT2_920P;
			p->u.board.v_type = 1;
			break;

		case APORT4_920P:	/* AccelePort_4 */
			if (p->type != BNODE) {
				dgap_err("unexpected Digi_4r_920 string");
				return -1;
			}
			p->u.board.type = APORT4_920P;
			p->u.board.v_type = 1;
			break;

		case APORT8_920P:	/* AccelePort_8 */
			if (p->type != BNODE) {
				dgap_err("unexpected Digi_8r_920 string");
				return -1;
			}
			p->u.board.type = APORT8_920P;
			p->u.board.v_type = 1;
			break;

		case PAPORT4:	/* AccelePort_4 PCI */
			if (p->type != BNODE) {
				dgap_err("unexpected Digi_4r(PCI) string");
				return -1;
			}
			p->u.board.type = PAPORT4;
			p->u.board.v_type = 1;
			break;

		case PAPORT8:	/* AccelePort_8 PCI */
			if (p->type != BNODE) {
				dgap_err("unexpected Digi_8r string");
				return -1;
			}
			p->u.board.type = PAPORT8;
			p->u.board.v_type = 1;
			break;

		case PCX:	/* PCI C/X */
			if (p->type != BNODE) {
				dgap_err("unexpected Digi_C/X_(PCI) string");
				return -1;
			}
			p->u.board.type = PCX;
			p->u.board.v_type = 1;
			p->u.board.conc1 = 0;
			p->u.board.conc2 = 0;
			p->u.board.module1 = 0;
			p->u.board.module2 = 0;
			break;

		case PEPC:	/* PCI EPC/X */
			if (p->type != BNODE) {
				dgap_err("unexpected \"Digi_EPC/X_(PCI)\" string");
				return -1;
			}
			p->u.board.type = PEPC;
			p->u.board.v_type = 1;
			p->u.board.conc1 = 0;
			p->u.board.conc2 = 0;
			p->u.board.module1 = 0;
			p->u.board.module2 = 0;
			break;

		case PPCM:	/* PCI/Xem */
			if (p->type != BNODE) {
				dgap_err("unexpected PCI/Xem string");
				return -1;
			}
			p->u.board.type = PPCM;
			p->u.board.v_type = 1;
			p->u.board.conc1 = 0;
			p->u.board.conc2 = 0;
			break;

		case IO:	/* i/o port */
			if (p->type != BNODE) {
				dgap_err("IO port only vaild for boards");
				return -1;
			}
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			p->u.board.portstr = kstrdup(s, GFP_KERNEL);
			if (kstrtol(s, 0, &p->u.board.port)) {
				dgap_err("bad number for IO port");
				return -1;
			}
			p->u.board.v_port = 1;
			break;

		case MEM:	/* memory address */
			if (p->type != BNODE) {
				dgap_err("memory address only vaild for boards");
				return -1;
			}
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			p->u.board.addrstr = kstrdup(s, GFP_KERNEL);
			if (kstrtoul(s, 0, &p->u.board.addr)) {
				dgap_err("bad number for memory address");
				return -1;
			}
			p->u.board.v_addr = 1;
			break;

		case PCIINFO:	/* pci information */
			if (p->type != BNODE) {
				dgap_err("memory address only vaild for boards");
				return -1;
			}
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			p->u.board.pcibusstr = kstrdup(s, GFP_KERNEL);
			if (kstrtoul(s, 0, &p->u.board.pcibus)) {
				dgap_err("bad number for pci bus");
				return -1;
			}
			p->u.board.v_pcibus = 1;
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			p->u.board.pcislotstr = kstrdup(s, GFP_KERNEL);
			if (kstrtoul(s, 0, &p->u.board.pcislot)) {
				dgap_err("bad number for pci slot");
				return -1;
			}
			p->u.board.v_pcislot = 1;
			break;

		case METHOD:
			if (p->type != BNODE) {
				dgap_err("install method only vaild for boards");
				return -1;
			}
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			p->u.board.method = kstrdup(s, GFP_KERNEL);
			p->u.board.v_method = 1;
			break;

		case STATUS:
			if (p->type != BNODE) {
				dgap_err("config status only vaild for boards");
				return -1;
			}
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			p->u.board.status = kstrdup(s, GFP_KERNEL);
			break;

		case NPORTS:	/* number of ports */
			if (p->type == BNODE) {
				s = dgap_getword(in);
				if (!s) {
					dgap_err("unexpected end of file");
					return -1;
				}
				if (kstrtol(s, 0, &p->u.board.nport)) {
					dgap_err("bad number for number of ports");
					return -1;
				}
				p->u.board.v_nport = 1;
			} else if (p->type == CNODE) {
				s = dgap_getword(in);
				if (!s) {
					dgap_err("unexpected end of file");
					return -1;
				}
				if (kstrtol(s, 0, &p->u.conc.nport)) {
					dgap_err("bad number for number of ports");
					return -1;
				}
				p->u.conc.v_nport = 1;
			} else if (p->type == MNODE) {
				s = dgap_getword(in);
				if (!s) {
					dgap_err("unexpected end of file");
					return -1;
				}
				if (kstrtol(s, 0, &p->u.module.nport)) {
					dgap_err("bad number for number of ports");
					return -1;
				}
				p->u.module.v_nport = 1;
			} else {
				dgap_err("nports only valid for concentrators or modules");
				return -1;
			}
			break;

		case ID:	/* letter ID used in tty name */
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}

			p->u.board.status = kstrdup(s, GFP_KERNEL);

			if (p->type == CNODE) {
				p->u.conc.id = kstrdup(s, GFP_KERNEL);
				p->u.conc.v_id = 1;
			} else if (p->type == MNODE) {
				p->u.module.id = kstrdup(s, GFP_KERNEL);
				p->u.module.v_id = 1;
			} else {
				dgap_err("id only valid for concentrators or modules");
				return -1;
			}
			break;

		case STARTO:	/* start offset of ID */
			if (p->type == BNODE) {
				s = dgap_getword(in);
				if (!s) {
					dgap_err("unexpected end of file");
					return -1;
				}
				if (kstrtol(s, 0, &p->u.board.start)) {
					dgap_err("bad number for start of tty count");
					return -1;
				}
				p->u.board.v_start = 1;
			} else if (p->type == CNODE) {
				s = dgap_getword(in);
				if (!s) {
					dgap_err("unexpected end of file");
					return -1;
				}
				if (kstrtol(s, 0, &p->u.conc.start)) {
					dgap_err("bad number for start of tty count");
					return -1;
				}
				p->u.conc.v_start = 1;
			} else if (p->type == MNODE) {
				s = dgap_getword(in);
				if (!s) {
					dgap_err("unexpected end of file");
					return -1;
				}
				if (kstrtol(s, 0, &p->u.module.start)) {
					dgap_err("bad number for start of tty count");
					return -1;
				}
				p->u.module.v_start = 1;
			} else {
				dgap_err("start only valid for concentrators or modules");
				return -1;
			}
			break;

		case TTYN:	/* tty name prefix */
			if (dgap_checknode(p))
				return -1;
			p->next = dgap_newnode(TNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpeced end of file");
				return -1;
			}
			p->u.ttyname = kstrdup(s, GFP_KERNEL);
			if (!p->u.ttyname) {
				dgap_err("out of memory");
				return -1;
			}
			break;

		case CU:	/* cu name prefix */
			if (dgap_checknode(p))
				return -1;
			p->next = dgap_newnode(CUNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpeced end of file");
				return -1;
			}
			p->u.cuname = kstrdup(s, GFP_KERNEL);
			if (!p->u.cuname) {
				dgap_err("out of memory");
				return -1;
			}
			break;

		case LINE:	/* line information */
			if (dgap_checknode(p))
				return -1;
			if (!brd) {
				dgap_err("must specify board before line info");
				return -1;
			}
			switch (brd->u.board.type) {
			case PPCM:
				dgap_err("line not vaild for PC/em");
				return -1;
			}
			p->next = dgap_newnode(LNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			conc = NULL;
			line = p;
			linecnt++;
			break;

		case CONC:	/* concentrator information */
			if (dgap_checknode(p))
				return -1;
			if (!line) {
				dgap_err("must specify line info before concentrator");
				return -1;
			}
			p->next = dgap_newnode(CNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			conc = p;
			if (linecnt)
				brd->u.board.conc2++;
			else
				brd->u.board.conc1++;

			break;

		case CX:	/* c/x type concentrator */
			if (p->type != CNODE) {
				dgap_err("cx only valid for concentrators");
				return -1;
			}
			p->u.conc.type = CX;
			p->u.conc.v_type = 1;
			break;

		case EPC:	/* epc type concentrator */
			if (p->type != CNODE) {
				dgap_err("cx only valid for concentrators");
				return -1;
			}
			p->u.conc.type = EPC;
			p->u.conc.v_type = 1;
			break;

		case MOD:	/* EBI module */
			if (dgap_checknode(p))
				return -1;
			if (!brd) {
				dgap_err("must specify board info before EBI modules");
				return -1;
			}
			switch (brd->u.board.type) {
			case PPCM:
				linecnt = 0;
				break;
			default:
				if (!conc) {
					dgap_err("must specify concentrator info before EBI module");
					return -1;
				}
			}
			p->next = dgap_newnode(MNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			if (linecnt)
				brd->u.board.module2++;
			else
				brd->u.board.module1++;

			break;

		case PORTS:	/* ports type EBI module */
			if (p->type != MNODE) {
				dgap_err("ports only valid for EBI modules");
				return -1;
			}
			p->u.module.type = PORTS;
			p->u.module.v_type = 1;
			break;

		case MODEM:	/* ports type EBI module */
			if (p->type != MNODE) {
				dgap_err("modem only valid for modem modules");
				return -1;
			}
			p->u.module.type = MODEM;
			p->u.module.v_type = 1;
			break;

		case CABLE:
			if (p->type == LNODE) {
				s = dgap_getword(in);
				if (!s) {
					dgap_err("unexpected end of file");
					return -1;
				}
				p->u.line.cable = kstrdup(s, GFP_KERNEL);
				p->u.line.v_cable = 1;
			}
			break;

		case SPEED:	/* sync line speed indication */
			if (p->type == LNODE) {
				s = dgap_getword(in);
				if (!s) {
					dgap_err("unexpected end of file");
					return -1;
				}
				if (kstrtol(s, 0, &p->u.line.speed)) {
					dgap_err("bad number for line speed");
					return -1;
				}
				p->u.line.v_speed = 1;
			} else if (p->type == CNODE) {
				s = dgap_getword(in);
				if (!s) {
					dgap_err("unexpected end of file");
					return -1;
				}
				if (kstrtol(s, 0, &p->u.conc.speed)) {
					dgap_err("bad number for line speed");
					return -1;
				}
				p->u.conc.v_speed = 1;
			} else {
				dgap_err("speed valid only for lines or concentrators.");
				return -1;
			}
			break;

		case CONNECT:
			if (p->type == CNODE) {
				s = dgap_getword(in);
				if (!s) {
					dgap_err("unexpected end of file");
					return -1;
				}
				p->u.conc.connect = kstrdup(s, GFP_KERNEL);
				p->u.conc.v_connect = 1;
			}
			break;
		case PRINT:	/* transparent print name prefix */
			if (dgap_checknode(p))
				return -1;
			p->next = dgap_newnode(PNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpeced end of file");
				return -1;
			}
			p->u.printname = kstrdup(s, GFP_KERNEL);
			if (!p->u.printname) {
				dgap_err("out of memory");
				return -1;
			}
			break;

		case CMAJOR:	/* major number */
			if (dgap_checknode(p))
				return -1;
			p->next = dgap_newnode(JNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			if (kstrtol(s, 0, &p->u.majornumber)) {
				dgap_err("bad number for major number");
				return -1;
			}
			break;

		case ALTPIN:	/* altpin setting */
			if (dgap_checknode(p))
				return -1;
			p->next = dgap_newnode(ANODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			if (kstrtol(s, 0, &p->u.altpin)) {
				dgap_err("bad number for altpin");
				return -1;
			}
			break;

		case USEINTR:		/* enable interrupt setting */
			if (dgap_checknode(p))
				return -1;
			p->next = dgap_newnode(INTRNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			if (kstrtol(s, 0, &p->u.useintr)) {
				dgap_err("bad number for useintr");
				return -1;
			}
			break;

		case TTSIZ:	/* size of tty structure */
			if (dgap_checknode(p))
				return -1;
			p->next = dgap_newnode(TSNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			if (kstrtol(s, 0, &p->u.ttysize)) {
				dgap_err("bad number for ttysize");
				return -1;
			}
			break;

		case CHSIZ:	/* channel structure size */
			if (dgap_checknode(p))
				return -1;
			p->next = dgap_newnode(CSNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			if (kstrtol(s, 0, &p->u.chsize)) {
				dgap_err("bad number for chsize");
				return -1;
			}
			break;

		case BSSIZ:	/* board structure size */
			if (dgap_checknode(p))
				return -1;
			p->next = dgap_newnode(BSNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			if (kstrtol(s, 0, &p->u.bssize)) {
				dgap_err("bad number for bssize");
				return -1;
			}
			break;

		case UNTSIZ:	/* sched structure size */
			if (dgap_checknode(p))
				return -1;
			p->next = dgap_newnode(USNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			if (kstrtol(s, 0, &p->u.unsize)) {
				dgap_err("bad number for schedsize");
				return -1;
			}
			break;

		case F2SIZ:	/* f2200 structure size */
			if (dgap_checknode(p))
				return -1;
			p->next = dgap_newnode(FSNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			if (kstrtol(s, 0, &p->u.f2size)) {
				dgap_err("bad number for f2200size");
				return -1;
			}
			break;

		case VPSIZ:	/* vpix structure size */
			if (dgap_checknode(p))
				return -1;
			p->next = dgap_newnode(VSNODE);
			if (!p->next) {
				dgap_err("out of memory");
				return -1;
			}
			p = p->next;
			s = dgap_getword(in);
			if (!s) {
				dgap_err("unexpected end of file");
				return -1;
			}
			if (kstrtol(s, 0, &p->u.vpixsize)) {
				dgap_err("bad number for vpixsize");
				return -1;
			}
			break;
		}
	}
}

/*
 * dgap_sindex: much like index(), but it looks for a match of any character in
 * the group, and returns that position.  If the first character is a ^, then
 * this will match the first occurrence not in that group.
 */
static char *dgap_sindex(char *string, char *group)
{
	char *ptr;

	if (!string || !group)
		return (char *) NULL;

	if (*group == '^') {
		group++;
		for (; *string; string++) {
			for (ptr = group; *ptr; ptr++) {
				if (*ptr == *string)
					break;
			}
			if (*ptr == '\0')
				return string;
		}
	} else {
		for (; *string; string++) {
			for (ptr = group; *ptr; ptr++) {
				if (*ptr == *string)
					return string;
			}
		}
	}

	return (char *) NULL;
}

/*
 * Get a token from the input file; return 0 if end of file is reached
 */
static int dgap_gettok(char **in, struct cnode *p)
{
	char *w;
	struct toklist *t;

	if (strstr(dgap_cword, "boar")) {
		w = dgap_getword(in);
		snprintf(dgap_cword, MAXCWORD, "%s", w);
		for (t = dgap_tlist; t->token != 0; t++) {
			if (!strcmp(w, t->string))
				return t->token;
		}
		dgap_err("board !!type not specified");
		return 1;
	} else {
		while ((w = dgap_getword(in))) {
			snprintf(dgap_cword, MAXCWORD, "%s", w);
			for (t = dgap_tlist; t->token != 0; t++) {
				if (!strcmp(w, t->string))
					return t->token;
			}
		}
		return 0;
	}
}

/*
 * get a word from the input stream, also keep track of current line number.
 * words are separated by whitespace.
 */
static char *dgap_getword(char **in)
{
	char *ret_ptr = *in;

	char *ptr = dgap_sindex(*in, " \t\n");

	/* If no word found, return null */
	if (!ptr)
		return NULL;

	/* Mark new location for our buffer */
	*ptr = '\0';
	*in = ptr + 1;

	/* Eat any extra spaces/tabs/newlines that might be present */
	while (*in && **in && ((**in == ' ') ||
			       (**in == '\t') ||
			       (**in == '\n'))) {
		**in = '\0';
		*in = *in + 1;
	}

	return ret_ptr;
}

/*
 * print an error message, giving the line number in the file where
 * the error occurred.
 */
static void dgap_err(char *s)
{
	pr_err("dgap: parse: %s\n", s);
}

/*
 * allocate a new configuration node of type t
 */
static struct cnode *dgap_newnode(int t)
{
	struct cnode *n;

	n = kmalloc(sizeof(struct cnode), GFP_KERNEL);
	if (n) {
		memset((char *)n, 0, sizeof(struct cnode));
		n->type = t;
	}
	return n;
}

/*
 * dgap_checknode: see if all the necessary info has been supplied for a node
 * before creating the next node.
 */
static int dgap_checknode(struct cnode *p)
{
	switch (p->type) {
	case BNODE:
		if (p->u.board.v_type == 0) {
			dgap_err("board type !not specified");
			return 1;
		}

		return 0;

	case LNODE:
		if (p->u.line.v_speed == 0) {
			dgap_err("line speed not specified");
			return 1;
		}
		return 0;

	case CNODE:
		if (p->u.conc.v_type == 0) {
			dgap_err("concentrator type not specified");
			return 1;
		}
		if (p->u.conc.v_speed == 0) {
			dgap_err("concentrator line speed not specified");
			return 1;
		}
		if (p->u.conc.v_nport == 0) {
			dgap_err("number of ports on concentrator not specified");
			return 1;
		}
		if (p->u.conc.v_id == 0) {
			dgap_err("concentrator id letter not specified");
			return 1;
		}
		return 0;

	case MNODE:
		if (p->u.module.v_type == 0) {
			dgap_err("EBI module type not specified");
			return 1;
		}
		if (p->u.module.v_nport == 0) {
			dgap_err("number of ports on EBI module not specified");
			return 1;
		}
		if (p->u.module.v_id == 0) {
			dgap_err("EBI module id letter not specified");
			return 1;
		}
		return 0;
	}
	return 0;
}

/*
 * Given a board pointer, returns whether we should use interrupts or not.
 */
static uint dgap_config_get_useintr(struct board_t *bd)
{
	struct cnode *p;

	if (!bd)
		return 0;

	for (p = bd->bd_config; p; p = p->next) {
		if (p->type == INTRNODE) {
			/*
			 * check for pcxr types.
			 */
			return p->u.useintr;
		}
	}

	/* If not found, then don't turn on interrupts. */
	return 0;
}

/*
 * Given a board pointer, returns whether we turn on altpin or not.
 */
static uint dgap_config_get_altpin(struct board_t *bd)
{
	struct cnode *p;

	if (!bd)
		return 0;

	for (p = bd->bd_config; p; p = p->next) {
		if (p->type == ANODE) {
			/*
			 * check for pcxr types.
			 */
			return p->u.altpin;
		}
	}

	/* If not found, then don't turn on interrupts. */
	return 0;
}

/*
 * Given a specific type of board, if found, detached link and
 * returns the first occurrence in the list.
 */
static struct cnode *dgap_find_config(int type, int bus, int slot)
{
	struct cnode *p, *prev, *prev2, *found;

	p = &dgap_head;

	while (p->next) {
		prev = p;
		p = p->next;

		if (p->type != BNODE)
			continue;

		if (p->u.board.type != type)
			continue;

		if (p->u.board.v_pcibus &&
		    p->u.board.pcibus != bus)
			continue;

		if (p->u.board.v_pcislot &&
		    p->u.board.pcislot != slot)
			continue;

		found = p;
		/*
		 * Keep walking thru the list till we
		 * find the next board.
		 */
		while (p->next) {
			prev2 = p;
			p = p->next;

			if (p->type != BNODE)
				continue;

			/*
			 * Mark the end of our 1 board
			 * chain of configs.
			 */
			prev2->next = NULL;

			/*
			 * Link the "next" board to the
			 * previous board, effectively
			 * "unlinking" our board from
			 * the main config.
			 */
			prev->next = p;

			return found;
		}
		/*
		 * It must be the last board in the list.
		 */
		prev->next = NULL;
		return found;
	}
	return NULL;
}

/*
 * Given a board pointer, walks the config link, counting up
 * all ports user specified should be on the board.
 * (This does NOT mean they are all actually present right now tho)
 */
static uint dgap_config_get_num_prts(struct board_t *bd)
{
	int count = 0;
	struct cnode *p;

	if (!bd)
		return 0;

	for (p = bd->bd_config; p; p = p->next) {

		switch (p->type) {
		case BNODE:
			/*
			 * check for pcxr types.
			 */
			if (p->u.board.type > EPCFE)
				count += p->u.board.nport;
			break;
		case CNODE:
			count += p->u.conc.nport;
			break;
		case MNODE:
			count += p->u.module.nport;
			break;
		}
	}
	return count;
}

static char *dgap_create_config_string(struct board_t *bd, char *string)
{
	char *ptr = string;
	struct cnode *p;
	struct cnode *q;
	int speed;

	if (!bd) {
		*ptr = 0xff;
		return string;
	}

	for (p = bd->bd_config; p; p = p->next) {

		switch (p->type) {
		case LNODE:
			*ptr = '\0';
			ptr++;
			*ptr = p->u.line.speed;
			ptr++;
			break;
		case CNODE:
			/*
			 * Because the EPC/con concentrators can have EM modules
			 * hanging off of them, we have to walk ahead in the
			 * list and keep adding the number of ports on each EM
			 * to the config. UGH!
			 */
			speed = p->u.conc.speed;
			q = p->next;
			if (q && (q->type == MNODE)) {
				*ptr = (p->u.conc.nport + 0x80);
				ptr++;
				p = q;
				while (q->next && (q->next->type) == MNODE) {
					*ptr = (q->u.module.nport + 0x80);
					ptr++;
					p = q;
					q = q->next;
				}
				*ptr = q->u.module.nport;
				ptr++;
			} else {
				*ptr = p->u.conc.nport;
				ptr++;
			}

			*ptr = speed;
			ptr++;
			break;
		}
	}

	*ptr = 0xff;
	return string;
}
