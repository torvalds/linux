/*
 *          mxser.c  -- MOXA Smartio/Industio family multiport serial driver.
 *
 *      Copyright (C) 1999-2001  Moxa Technologies (support@moxa.com.tw).
 *
 *      This code is loosely based on the Linux serial driver, written by
 *      Linus Torvalds, Theodore T'so and others.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	Original release	10/26/00
 *
 *	02/06/01	Support MOXA Industio family boards.
 *	02/06/01	Support TIOCGICOUNT.
 *	02/06/01	Fix the problem for connecting to serial mouse.
 *	02/06/01	Fix the problem for H/W flow control.
 *	02/06/01	Fix the compling warning when CONFIG_PCI
 *			don't be defined.
 *
 *	Fed through a cleanup, indent and remove of non 2.6 code by Alan Cox
 *	<alan@redhat.com>. The original 1.8 code is available on www.moxa.com.
 *	- Fixed x86_64 cleanness
 *	- Fixed sleep with spinlock held in mxser_send_break
 */


#include <linux/module.h>
#include <linux/autoconf.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/gfp.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "mxser.h"

#define	MXSER_VERSION	"1.8"
#define	MXSERMAJOR	 174
#define	MXSERCUMAJOR	 175

#define	MXSER_EVENT_TXLOW	1
#define	MXSER_EVENT_HANGUP	2

#define MXSER_BOARDS		4	/* Max. boards */
#define MXSER_PORTS		32	/* Max. ports */
#define MXSER_PORTS_PER_BOARD	8	/* Max. ports per board */
#define MXSER_ISR_PASS_LIMIT	256

#define	MXSER_ERR_IOADDR	-1
#define	MXSER_ERR_IRQ		-2
#define	MXSER_ERR_IRQ_CONFLIT	-3
#define	MXSER_ERR_VECTOR	-4

#define SERIAL_TYPE_NORMAL	1
#define SERIAL_TYPE_CALLOUT	2

#define WAKEUP_CHARS		256

#define UART_MCR_AFE		0x20
#define UART_LSR_SPECIAL	0x1E


#define IRQ_T(info) ((info->flags & ASYNC_SHARE_IRQ) ? IRQF_SHARED : IRQF_DISABLED)

#define C168_ASIC_ID    1
#define C104_ASIC_ID    2
#define C102_ASIC_ID	0xB
#define CI132_ASIC_ID	4
#define CI134_ASIC_ID	3
#define CI104J_ASIC_ID  5

enum {
	MXSER_BOARD_C168_ISA = 1,
	MXSER_BOARD_C104_ISA,
	MXSER_BOARD_CI104J,
	MXSER_BOARD_C168_PCI,
	MXSER_BOARD_C104_PCI,
	MXSER_BOARD_C102_ISA,
	MXSER_BOARD_CI132,
	MXSER_BOARD_CI134,
	MXSER_BOARD_CP132,
	MXSER_BOARD_CP114,
	MXSER_BOARD_CT114,
	MXSER_BOARD_CP102,
	MXSER_BOARD_CP104U,
	MXSER_BOARD_CP168U,
	MXSER_BOARD_CP132U,
	MXSER_BOARD_CP134U,
	MXSER_BOARD_CP104JU,
	MXSER_BOARD_RC7000,
	MXSER_BOARD_CP118U,
	MXSER_BOARD_CP102UL,
	MXSER_BOARD_CP102U,
};

static char *mxser_brdname[] = {
	"C168 series",
	"C104 series",
	"CI-104J series",
	"C168H/PCI series",
	"C104H/PCI series",
	"C102 series",
	"CI-132 series",
	"CI-134 series",
	"CP-132 series",
	"CP-114 series",
	"CT-114 series",
	"CP-102 series",
	"CP-104U series",
	"CP-168U series",
	"CP-132U series",
	"CP-134U series",
	"CP-104JU series",
	"Moxa UC7000 Serial",
	"CP-118U series",
	"CP-102UL series",
	"CP-102U series",
};

static int mxser_numports[] = {
	8,			/* C168-ISA */
	4,			/* C104-ISA */
	4,			/* CI104J */
	8,			/* C168-PCI */
	4,			/* C104-PCI */
	2,			/* C102-ISA */
	2,			/* CI132 */
	4,			/* CI134 */
	2,			/* CP132 */
	4,			/* CP114 */
	4,			/* CT114 */
	2,			/* CP102 */
	4,			/* CP104U */
	8,			/* CP168U */
	2,			/* CP132U */
	4,			/* CP134U */
	4,			/* CP104JU */
	8,			/* RC7000 */
	8,			/* CP118U */
	2,			/* CP102UL */
	2,			/* CP102U */
};

#define UART_TYPE_NUM	2

static const unsigned int Gmoxa_uart_id[UART_TYPE_NUM] = {
	MOXA_MUST_MU150_HWID,
	MOXA_MUST_MU860_HWID
};

/* This is only for PCI */
#define UART_INFO_NUM	3
struct mxpciuart_info {
	int type;
	int tx_fifo;
	int rx_fifo;
	int xmit_fifo_size;
	int rx_high_water;
	int rx_trigger;
	int rx_low_water;
	long max_baud;
};

static const struct mxpciuart_info Gpci_uart_info[UART_INFO_NUM] = {
	{MOXA_OTHER_UART, 16, 16, 16, 14, 14, 1, 921600L},
	{MOXA_MUST_MU150_HWID, 64, 64, 64, 48, 48, 16, 230400L},
	{MOXA_MUST_MU860_HWID, 128, 128, 128, 96, 96, 32, 921600L}
};


#ifdef CONFIG_PCI

static struct pci_device_id mxser_pcibrds[] = {
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_C168, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_C168_PCI},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_C104, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_C104_PCI},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CP132, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_CP132},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CP114, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_CP114},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CT114, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_CT114},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CP102, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_CP102},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CP104U, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_CP104U},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CP168U, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_CP168U},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CP132U, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_CP132U},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CP134U, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_CP134U},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CP104JU, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_CP104JU},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_RC7000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_RC7000},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CP118U, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_CP118U},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CP102UL, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_CP102UL},
	{PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CP102U, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MXSER_BOARD_CP102U},
	{0}
};

MODULE_DEVICE_TABLE(pci, mxser_pcibrds);


#endif

typedef struct _moxa_pci_info {
	unsigned short busNum;
	unsigned short devNum;
	struct pci_dev *pdev;	/* add by Victor Yu. 06-23-2003 */
} moxa_pci_info;

static int ioaddr[MXSER_BOARDS] = { 0, 0, 0, 0 };
static int ttymajor = MXSERMAJOR;
static int calloutmajor = MXSERCUMAJOR;
static int verbose = 0;

/* Variables for insmod */

MODULE_AUTHOR("Casper Yang");
MODULE_DESCRIPTION("MOXA Smartio/Industio Family Multiport Board Device Driver");
module_param_array(ioaddr, int, NULL, 0);
module_param(ttymajor, int, 0);
module_param(calloutmajor, int, 0);
module_param(verbose, bool, 0);
MODULE_LICENSE("GPL");

struct mxser_log {
	int tick;
	unsigned long rxcnt[MXSER_PORTS];
	unsigned long txcnt[MXSER_PORTS];
};


struct mxser_mon {
	unsigned long rxcnt;
	unsigned long txcnt;
	unsigned long up_rxcnt;
	unsigned long up_txcnt;
	int modem_status;
	unsigned char hold_reason;
};

struct mxser_mon_ext {
	unsigned long rx_cnt[32];
	unsigned long tx_cnt[32];
	unsigned long up_rxcnt[32];
	unsigned long up_txcnt[32];
	int modem_status[32];

	long baudrate[32];
	int databits[32];
	int stopbits[32];
	int parity[32];
	int flowctrl[32];
	int fifo[32];
	int iftype[32];
};

struct mxser_hwconf {
	int board_type;
	int ports;
	int irq;
	int vector;
	int vector_mask;
	int uart_type;
	int ioaddr[MXSER_PORTS_PER_BOARD];
	int baud_base[MXSER_PORTS_PER_BOARD];
	moxa_pci_info pciInfo;
	int IsMoxaMustChipFlag;	/* add by Victor Yu. 08-30-2002 */
	int MaxCanSetBaudRate[MXSER_PORTS_PER_BOARD];	/* add by Victor Yu. 09-04-2002 */
	int opmode_ioaddr[MXSER_PORTS_PER_BOARD];	/* add by Victor Yu. 01-05-2004 */
};

struct mxser_struct {
	int port;
	int base;		/* port base address */
	int irq;		/* port using irq no. */
	int vector;		/* port irq vector */
	int vectormask;		/* port vector mask */
	int rx_high_water;
	int rx_trigger;		/* Rx fifo trigger level */
	int rx_low_water;
	int baud_base;		/* max. speed */
	int flags;		/* defined in tty.h */
	int type;		/* UART type */
	struct tty_struct *tty;
	int read_status_mask;
	int ignore_status_mask;
	int xmit_fifo_size;
	int custom_divisor;
	int x_char;		/* xon/xoff character */
	int close_delay;
	unsigned short closing_wait;
	int IER;		/* Interrupt Enable Register */
	int MCR;		/* Modem control register */
	unsigned long event;
	int count;		/* # of fd on device */
	int blocked_open;	/* # of blocked opens */
	unsigned char *xmit_buf;
	int xmit_head;
	int xmit_tail;
	int xmit_cnt;
	struct work_struct tqueue;
	struct ktermios normal_termios;
	struct ktermios callout_termios;
	wait_queue_head_t open_wait;
	wait_queue_head_t close_wait;
	wait_queue_head_t delta_msr_wait;
	struct async_icount icount;	/* kernel counters for the 4 input interrupts */
	int timeout;
	int IsMoxaMustChipFlag;	/* add by Victor Yu. 08-30-2002 */
	int MaxCanSetBaudRate;	/* add by Victor Yu. 09-04-2002 */
	int opmode_ioaddr;	/* add by Victor Yu. 01-05-2004 */
	unsigned char stop_rx;
	unsigned char ldisc_stop_rx;
	long realbaud;
	struct mxser_mon mon_data;
	unsigned char err_shadow;
	spinlock_t slock;
};

struct mxser_mstatus {
	tcflag_t cflag;
	int cts;
	int dsr;
	int ri;
	int dcd;
};

static struct mxser_mstatus GMStatus[MXSER_PORTS];

static int mxserBoardCAP[MXSER_BOARDS] = {
	0, 0, 0, 0
	/*  0x180, 0x280, 0x200, 0x320 */
};

static struct tty_driver *mxvar_sdriver;
static struct mxser_struct mxvar_table[MXSER_PORTS];
static struct tty_struct *mxvar_tty[MXSER_PORTS + 1];
static struct ktermios *mxvar_termios[MXSER_PORTS + 1];
static struct ktermios *mxvar_termios_locked[MXSER_PORTS + 1];
static struct mxser_log mxvar_log;
static int mxvar_diagflag;
static unsigned char mxser_msr[MXSER_PORTS + 1];
static struct mxser_mon_ext mon_data_ext;
static int mxser_set_baud_method[MXSER_PORTS + 1];
static spinlock_t gm_lock;

/*
 * This is used to figure out the divisor speeds and the timeouts
 */

static struct mxser_hwconf mxsercfg[MXSER_BOARDS];

/*
 * static functions:
 */

static void mxser_getcfg(int board, struct mxser_hwconf *hwconf);
static int mxser_init(void);

/* static void   mxser_poll(unsigned long); */
static int mxser_get_ISA_conf(int, struct mxser_hwconf *);
static void mxser_do_softint(struct work_struct *);
static int mxser_open(struct tty_struct *, struct file *);
static void mxser_close(struct tty_struct *, struct file *);
static int mxser_write(struct tty_struct *, const unsigned char *, int);
static int mxser_write_room(struct tty_struct *);
static void mxser_flush_buffer(struct tty_struct *);
static int mxser_chars_in_buffer(struct tty_struct *);
static void mxser_flush_chars(struct tty_struct *);
static void mxser_put_char(struct tty_struct *, unsigned char);
static int mxser_ioctl(struct tty_struct *, struct file *, uint, ulong);
static int mxser_ioctl_special(unsigned int, void __user *);
static void mxser_throttle(struct tty_struct *);
static void mxser_unthrottle(struct tty_struct *);
static void mxser_set_termios(struct tty_struct *, struct ktermios *);
static void mxser_stop(struct tty_struct *);
static void mxser_start(struct tty_struct *);
static void mxser_hangup(struct tty_struct *);
static void mxser_rs_break(struct tty_struct *, int);
static irqreturn_t mxser_interrupt(int, void *);
static void mxser_receive_chars(struct mxser_struct *, int *);
static void mxser_transmit_chars(struct mxser_struct *);
static void mxser_check_modem_status(struct mxser_struct *, int);
static int mxser_block_til_ready(struct tty_struct *, struct file *, struct mxser_struct *);
static int mxser_startup(struct mxser_struct *);
static void mxser_shutdown(struct mxser_struct *);
static int mxser_change_speed(struct mxser_struct *, struct ktermios *old_termios);
static int mxser_get_serial_info(struct mxser_struct *, struct serial_struct __user *);
static int mxser_set_serial_info(struct mxser_struct *, struct serial_struct __user *);
static int mxser_get_lsr_info(struct mxser_struct *, unsigned int __user *);
static void mxser_send_break(struct mxser_struct *, int);
static int mxser_tiocmget(struct tty_struct *, struct file *);
static int mxser_tiocmset(struct tty_struct *, struct file *, unsigned int, unsigned int);
static int mxser_set_baud(struct mxser_struct *info, long newspd);
static void mxser_wait_until_sent(struct tty_struct *tty, int timeout);

static void mxser_startrx(struct tty_struct *tty);
static void mxser_stoprx(struct tty_struct *tty);

#ifdef CONFIG_PCI
static int CheckIsMoxaMust(int io)
{
	u8 oldmcr, hwid;
	int i;

	outb(0, io + UART_LCR);
	DISABLE_MOXA_MUST_ENCHANCE_MODE(io);
	oldmcr = inb(io + UART_MCR);
	outb(0, io + UART_MCR);
	SET_MOXA_MUST_XON1_VALUE(io, 0x11);
	if ((hwid = inb(io + UART_MCR)) != 0) {
		outb(oldmcr, io + UART_MCR);
		return MOXA_OTHER_UART;
	}

	GET_MOXA_MUST_HARDWARE_ID(io, &hwid);
	for (i = 0; i < UART_TYPE_NUM; i++) {
		if (hwid == Gmoxa_uart_id[i])
			return (int)hwid;
	}
	return MOXA_OTHER_UART;
}
#endif

/* above is modified by Victor Yu. 08-15-2002 */

static const struct tty_operations mxser_ops = {
	.open = mxser_open,
	.close = mxser_close,
	.write = mxser_write,
	.put_char = mxser_put_char,
	.flush_chars = mxser_flush_chars,
	.write_room = mxser_write_room,
	.chars_in_buffer = mxser_chars_in_buffer,
	.flush_buffer = mxser_flush_buffer,
	.ioctl = mxser_ioctl,
	.throttle = mxser_throttle,
	.unthrottle = mxser_unthrottle,
	.set_termios = mxser_set_termios,
	.stop = mxser_stop,
	.start = mxser_start,
	.hangup = mxser_hangup,
	.break_ctl = mxser_rs_break,
	.wait_until_sent = mxser_wait_until_sent,
	.tiocmget = mxser_tiocmget,
	.tiocmset = mxser_tiocmset,
};

/*
 * The MOXA Smartio/Industio serial driver boot-time initialization code!
 */

static int __init mxser_module_init(void)
{
	int ret;

	if (verbose)
		printk(KERN_DEBUG "Loading module mxser ...\n");
	ret = mxser_init();
	if (verbose)
		printk(KERN_DEBUG "Done.\n");
	return ret;
}

static void __exit mxser_module_exit(void)
{
	int i, err;

	if (verbose)
		printk(KERN_DEBUG "Unloading module mxser ...\n");

	err = tty_unregister_driver(mxvar_sdriver);
	if (!err)
		put_tty_driver(mxvar_sdriver);
	else
		printk(KERN_ERR "Couldn't unregister MOXA Smartio/Industio family serial driver\n");

	for (i = 0; i < MXSER_BOARDS; i++) {
		struct pci_dev *pdev;

		if (mxsercfg[i].board_type == -1)
			continue;
		else {
			pdev = mxsercfg[i].pciInfo.pdev;
			free_irq(mxsercfg[i].irq, &mxvar_table[i * MXSER_PORTS_PER_BOARD]);
			if (pdev != NULL) {	/* PCI */
				release_region(pci_resource_start(pdev, 2), pci_resource_len(pdev, 2));
				release_region(pci_resource_start(pdev, 3), pci_resource_len(pdev, 3));
				pci_dev_put(pdev);
			} else {
				release_region(mxsercfg[i].ioaddr[0], 8 * mxsercfg[i].ports);
				release_region(mxsercfg[i].vector, 1);
			}
		}
	}
	if (verbose)
		printk(KERN_DEBUG "Done.\n");
}

static void process_txrx_fifo(struct mxser_struct *info)
{
	int i;

	if ((info->type == PORT_16450) || (info->type == PORT_8250)) {
		info->rx_trigger = 1;
		info->rx_high_water = 1;
		info->rx_low_water = 1;
		info->xmit_fifo_size = 1;
	} else {
		for (i = 0; i < UART_INFO_NUM; i++) {
			if (info->IsMoxaMustChipFlag == Gpci_uart_info[i].type) {
				info->rx_trigger = Gpci_uart_info[i].rx_trigger;
				info->rx_low_water = Gpci_uart_info[i].rx_low_water;
				info->rx_high_water = Gpci_uart_info[i].rx_high_water;
				info->xmit_fifo_size = Gpci_uart_info[i].xmit_fifo_size;
				break;
			}
		}
	}
}

static int mxser_initbrd(int board, struct mxser_hwconf *hwconf)
{
	struct mxser_struct *info;
	int retval;
	int i, n;

	n = board * MXSER_PORTS_PER_BOARD;
	info = &mxvar_table[n];
	/*if (verbose) */  {
		printk(KERN_DEBUG "        ttyMI%d - ttyMI%d ",
			n, n + hwconf->ports - 1);
		printk(" max. baud rate = %d bps.\n",
			hwconf->MaxCanSetBaudRate[0]);
	}

	for (i = 0; i < hwconf->ports; i++, n++, info++) {
		info->port = n;
		info->base = hwconf->ioaddr[i];
		info->irq = hwconf->irq;
		info->vector = hwconf->vector;
		info->vectormask = hwconf->vector_mask;
		info->opmode_ioaddr = hwconf->opmode_ioaddr[i];	/* add by Victor Yu. 01-05-2004 */
		info->stop_rx = 0;
		info->ldisc_stop_rx = 0;

		info->IsMoxaMustChipFlag = hwconf->IsMoxaMustChipFlag;
		/* Enhance mode enabled here */
		if (info->IsMoxaMustChipFlag != MOXA_OTHER_UART) {
			ENABLE_MOXA_MUST_ENCHANCE_MODE(info->base);
		}

		info->flags = ASYNC_SHARE_IRQ;
		info->type = hwconf->uart_type;
		info->baud_base = hwconf->baud_base[i];

		info->MaxCanSetBaudRate = hwconf->MaxCanSetBaudRate[i];

		process_txrx_fifo(info);


		info->custom_divisor = hwconf->baud_base[i] * 16;
		info->close_delay = 5 * HZ / 10;
		info->closing_wait = 30 * HZ;
		INIT_WORK(&info->tqueue, mxser_do_softint);
		info->normal_termios = mxvar_sdriver->init_termios;
		init_waitqueue_head(&info->open_wait);
		init_waitqueue_head(&info->close_wait);
		init_waitqueue_head(&info->delta_msr_wait);
		memset(&info->mon_data, 0, sizeof(struct mxser_mon));
		info->err_shadow = 0;
		spin_lock_init(&info->slock);
	}
	/*
	 * Allocate the IRQ if necessary
	 */


	/* before set INT ISR, disable all int */
	for (i = 0; i < hwconf->ports; i++) {
		outb(inb(hwconf->ioaddr[i] + UART_IER) & 0xf0,
			hwconf->ioaddr[i] + UART_IER);
	}

	n = board * MXSER_PORTS_PER_BOARD;
	info = &mxvar_table[n];

	retval = request_irq(hwconf->irq, mxser_interrupt, IRQ_T(info),
				"mxser", info);
	if (retval) {
		printk(KERN_ERR "Board %d: %s",
			board, mxser_brdname[hwconf->board_type - 1]);
		printk("  Request irq failed, IRQ (%d) may conflict with"
			" another device.\n", info->irq);
		return retval;
	}
	return 0;
}

static void mxser_getcfg(int board, struct mxser_hwconf *hwconf)
{
	mxsercfg[board] = *hwconf;
}

#ifdef CONFIG_PCI
static int mxser_get_PCI_conf(int busnum, int devnum, int board_type, struct mxser_hwconf *hwconf)
{
	int i, j;
	/* unsigned int val; */
	unsigned int ioaddress;
	struct pci_dev *pdev = hwconf->pciInfo.pdev;

	/* io address */
	hwconf->board_type = board_type;
	hwconf->ports = mxser_numports[board_type - 1];
	ioaddress = pci_resource_start(pdev, 2);
	request_region(pci_resource_start(pdev, 2), pci_resource_len(pdev, 2),
			"mxser(IO)");

	for (i = 0; i < hwconf->ports; i++)
		hwconf->ioaddr[i] = ioaddress + 8 * i;

	/* vector */
	ioaddress = pci_resource_start(pdev, 3);
	request_region(pci_resource_start(pdev, 3), pci_resource_len(pdev, 3),
			"mxser(vector)");
	hwconf->vector = ioaddress;

	/* irq */
	hwconf->irq = hwconf->pciInfo.pdev->irq;

	hwconf->IsMoxaMustChipFlag = CheckIsMoxaMust(hwconf->ioaddr[0]);
	hwconf->uart_type = PORT_16550A;
	hwconf->vector_mask = 0;


	for (i = 0; i < hwconf->ports; i++) {
		for (j = 0; j < UART_INFO_NUM; j++) {
			if (Gpci_uart_info[j].type == hwconf->IsMoxaMustChipFlag) {
				hwconf->MaxCanSetBaudRate[i] = Gpci_uart_info[j].max_baud;

				/* exception....CP-102 */
				if (board_type == MXSER_BOARD_CP102)
					hwconf->MaxCanSetBaudRate[i] = 921600;
				break;
			}
		}
	}

	if (hwconf->IsMoxaMustChipFlag == MOXA_MUST_MU860_HWID) {
		for (i = 0; i < hwconf->ports; i++) {
			if (i < 4)
				hwconf->opmode_ioaddr[i] = ioaddress + 4;
			else
				hwconf->opmode_ioaddr[i] = ioaddress + 0x0c;
		}
		outb(0, ioaddress + 4);	/* default set to RS232 mode */
		outb(0, ioaddress + 0x0c);	/* default set to RS232 mode */
	}

	for (i = 0; i < hwconf->ports; i++) {
		hwconf->vector_mask |= (1 << i);
		hwconf->baud_base[i] = 921600;
	}
	return 0;
}
#endif

static int mxser_init(void)
{
	int i, m, retval, b, n;
	struct pci_dev *pdev = NULL;
	int index;
	unsigned char busnum, devnum;
	struct mxser_hwconf hwconf;

	mxvar_sdriver = alloc_tty_driver(MXSER_PORTS + 1);
	if (!mxvar_sdriver)
		return -ENOMEM;
	spin_lock_init(&gm_lock);

	for (i = 0; i < MXSER_BOARDS; i++) {
		mxsercfg[i].board_type = -1;
	}

	printk(KERN_INFO "MOXA Smartio/Industio family driver version %s\n",
		MXSER_VERSION);

	/* Initialize the tty_driver structure */
	memset(mxvar_sdriver, 0, sizeof(struct tty_driver));
	mxvar_sdriver->owner = THIS_MODULE;
	mxvar_sdriver->magic = TTY_DRIVER_MAGIC;
	mxvar_sdriver->name = "ttyMI";
	mxvar_sdriver->major = ttymajor;
	mxvar_sdriver->minor_start = 0;
	mxvar_sdriver->num = MXSER_PORTS + 1;
	mxvar_sdriver->type = TTY_DRIVER_TYPE_SERIAL;
	mxvar_sdriver->subtype = SERIAL_TYPE_NORMAL;
	mxvar_sdriver->init_termios = tty_std_termios;
	mxvar_sdriver->init_termios.c_cflag = B9600|CS8|CREAD|HUPCL|CLOCAL;
	mxvar_sdriver->init_termios.c_ispeed = 9600;
	mxvar_sdriver->init_termios.c_ospeed = 9600;
	mxvar_sdriver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(mxvar_sdriver, &mxser_ops);
	mxvar_sdriver->ttys = mxvar_tty;
	mxvar_sdriver->termios = mxvar_termios;
	mxvar_sdriver->termios_locked = mxvar_termios_locked;

	mxvar_diagflag = 0;
	memset(mxvar_table, 0, MXSER_PORTS * sizeof(struct mxser_struct));
	memset(&mxvar_log, 0, sizeof(struct mxser_log));

	memset(&mxser_msr, 0, sizeof(unsigned char) * (MXSER_PORTS + 1));
	memset(&mon_data_ext, 0, sizeof(struct mxser_mon_ext));
	memset(&mxser_set_baud_method, 0, sizeof(int) * (MXSER_PORTS + 1));
	memset(&hwconf, 0, sizeof(struct mxser_hwconf));

	m = 0;
	/* Start finding ISA boards here */
	for (b = 0; b < MXSER_BOARDS && m < MXSER_BOARDS; b++) {
		int cap;

		if (!(cap = mxserBoardCAP[b]))
			continue;

		retval = mxser_get_ISA_conf(cap, &hwconf);

		if (retval != 0)
			printk(KERN_INFO "Found MOXA %s board (CAP=0x%x)\n",
				mxser_brdname[hwconf.board_type - 1], ioaddr[b]);

		if (retval <= 0) {
			if (retval == MXSER_ERR_IRQ)
				printk(KERN_ERR "Invalid interrupt number, "
					"board not configured\n");
			else if (retval == MXSER_ERR_IRQ_CONFLIT)
				printk(KERN_ERR "Invalid interrupt number, "
					"board not configured\n");
			else if (retval == MXSER_ERR_VECTOR)
				printk(KERN_ERR "Invalid interrupt vector, "
					"board not configured\n");
			else if (retval == MXSER_ERR_IOADDR)
				printk(KERN_ERR "Invalid I/O address, "
					"board not configured\n");

			continue;
		}

		hwconf.pciInfo.busNum = 0;
		hwconf.pciInfo.devNum = 0;
		hwconf.pciInfo.pdev = NULL;

		mxser_getcfg(m, &hwconf);
		/*
		 * init mxsercfg first,
		 * or mxsercfg data is not correct on ISR.
		 */
		/* mxser_initbrd will hook ISR. */
		if (mxser_initbrd(m, &hwconf) < 0)
			continue;

		m++;
	}

	/* Start finding ISA boards from module arg */
	for (b = 0; b < MXSER_BOARDS && m < MXSER_BOARDS; b++) {
		int cap;

		if (!(cap = ioaddr[b]))
			continue;

		retval = mxser_get_ISA_conf(cap, &hwconf);

		if (retval != 0)
			printk(KERN_INFO "Found MOXA %s board (CAP=0x%x)\n",
				mxser_brdname[hwconf.board_type - 1], ioaddr[b]);

		if (retval <= 0) {
			if (retval == MXSER_ERR_IRQ)
				printk(KERN_ERR "Invalid interrupt number, "
					"board not configured\n");
			else if (retval == MXSER_ERR_IRQ_CONFLIT)
				printk(KERN_ERR "Invalid interrupt number, "
					"board not configured\n");
			else if (retval == MXSER_ERR_VECTOR)
				printk(KERN_ERR "Invalid interrupt vector, "
					"board not configured\n");
			else if (retval == MXSER_ERR_IOADDR)
				printk(KERN_ERR "Invalid I/O address, "
					"board not configured\n");

			continue;
		}

		hwconf.pciInfo.busNum = 0;
		hwconf.pciInfo.devNum = 0;
		hwconf.pciInfo.pdev = NULL;

		mxser_getcfg(m, &hwconf);
		/*
		 * init mxsercfg first,
		 * or mxsercfg data is not correct on ISR.
		 */
		/* mxser_initbrd will hook ISR. */
		if (mxser_initbrd(m, &hwconf) < 0)
			continue;

		m++;
	}

	/* start finding PCI board here */
#ifdef CONFIG_PCI
	n = ARRAY_SIZE(mxser_pcibrds) - 1;
	index = 0;
	b = 0;
	while (b < n) {
		pdev = pci_get_device(mxser_pcibrds[b].vendor,
				mxser_pcibrds[b].device, pdev);
		if (pdev == NULL) {
			b++;
			continue;
		}
		hwconf.pciInfo.busNum = busnum = pdev->bus->number;
		hwconf.pciInfo.devNum = devnum = PCI_SLOT(pdev->devfn) << 3;
		hwconf.pciInfo.pdev = pdev;
		printk(KERN_INFO "Found MOXA %s board(BusNo=%d,DevNo=%d)\n",
			mxser_brdname[(int) (mxser_pcibrds[b].driver_data) - 1],
			busnum, devnum >> 3);
		index++;
		if (m >= MXSER_BOARDS)
			printk(KERN_ERR
				"Too many Smartio/Industio family boards find "
				"(maximum %d), board not configured\n",
				MXSER_BOARDS);
		else {
			if (pci_enable_device(pdev)) {
				printk(KERN_ERR "Moxa SmartI/O PCI enable "
					"fail !\n");
				continue;
			}
			retval = mxser_get_PCI_conf(busnum, devnum,
					(int)mxser_pcibrds[b].driver_data,
					&hwconf);
			if (retval < 0) {
				if (retval == MXSER_ERR_IRQ)
					printk(KERN_ERR
						"Invalid interrupt number, "
						"board not configured\n");
				else if (retval == MXSER_ERR_IRQ_CONFLIT)
					printk(KERN_ERR
						"Invalid interrupt number, "
						"board not configured\n");
				else if (retval == MXSER_ERR_VECTOR)
					printk(KERN_ERR
						"Invalid interrupt vector, "
						"board not configured\n");
				else if (retval == MXSER_ERR_IOADDR)
					printk(KERN_ERR
						"Invalid I/O address, "
						"board not configured\n");
				continue;
			}
			mxser_getcfg(m, &hwconf);
			/* init mxsercfg first,
			 * or mxsercfg data is not correct on ISR.
			 */
			/* mxser_initbrd will hook ISR. */
			if (mxser_initbrd(m, &hwconf) < 0)
				continue;
			m++;
			/* Keep an extra reference if we succeeded. It will
			   be returned at unload time */
			pci_dev_get(pdev);
		}
	}
#endif

	retval = tty_register_driver(mxvar_sdriver);
	if (retval) {
		printk(KERN_ERR "Couldn't install MOXA Smartio/Industio family"
				" driver !\n");
		put_tty_driver(mxvar_sdriver);

		for (i = 0; i < MXSER_BOARDS; i++) {
			if (mxsercfg[i].board_type == -1)
				continue;
			else {
				free_irq(mxsercfg[i].irq, &mxvar_table[i * MXSER_PORTS_PER_BOARD]);
				/* todo: release io, vector */
			}
		}
		return retval;
	}

	return 0;
}

static void mxser_do_softint(struct work_struct *work)
{
	struct mxser_struct *info =
		container_of(work, struct mxser_struct, tqueue);
	struct tty_struct *tty;

	tty = info->tty;

	if (tty) {
		if (test_and_clear_bit(MXSER_EVENT_TXLOW, &info->event))
			tty_wakeup(tty);
		if (test_and_clear_bit(MXSER_EVENT_HANGUP, &info->event))
			tty_hangup(tty);
	}
}

static unsigned char mxser_get_msr(int baseaddr, int mode, int port, struct mxser_struct *info)
{
	unsigned char status = 0;

	status = inb(baseaddr + UART_MSR);

	mxser_msr[port] &= 0x0F;
	mxser_msr[port] |= status;
	status = mxser_msr[port];
	if (mode)
		mxser_msr[port] = 0;

	return status;
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its async structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
static int mxser_open(struct tty_struct *tty, struct file *filp)
{
	struct mxser_struct *info;
	int retval, line;

	/* initialize driver_data in case something fails */
	tty->driver_data = NULL;

	line = tty->index;
	if (line == MXSER_PORTS)
		return 0;
	if (line < 0 || line > MXSER_PORTS)
		return -ENODEV;
	info = mxvar_table + line;
	if (!info->base)
		return -ENODEV;

	tty->driver_data = info;
	info->tty = tty;
	/*
	 * Start up serial port
	 */
	retval = mxser_startup(info);
	if (retval)
		return retval;

	retval = mxser_block_til_ready(tty, filp, info);
	if (retval)
		return retval;

	info->count++;

	if ((info->count == 1) && (info->flags & ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver->subtype == SERIAL_TYPE_NORMAL)
			*tty->termios = info->normal_termios;
		else
			*tty->termios = info->callout_termios;
		mxser_change_speed(info, NULL);
	}

	/*
	status = mxser_get_msr(info->base, 0, info->port);
	mxser_check_modem_status(info, status);
	*/

	/* unmark here for very high baud rate (ex. 921600 bps) used */
	tty->low_latency = 1;
	return 0;
}

/*
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * async structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 */
static void mxser_close(struct tty_struct *tty, struct file *filp)
{
	struct mxser_struct *info = tty->driver_data;

	unsigned long timeout;
	unsigned long flags;
	struct tty_ldisc *ld;

	if (tty->index == MXSER_PORTS)
		return;
	if (!info)
		return;

	spin_lock_irqsave(&info->slock, flags);

	if (tty_hung_up_p(filp)) {
		spin_unlock_irqrestore(&info->slock, flags);
		return;
	}
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk(KERN_ERR "mxser_close: bad serial port count; "
			"tty->count is 1, info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk(KERN_ERR "mxser_close: bad serial port count for "
			"ttys%d: %d\n", info->port, info->count);
		info->count = 0;
	}
	if (info->count) {
		spin_unlock_irqrestore(&info->slock, flags);
		return;
	}
	info->flags |= ASYNC_CLOSING;
	spin_unlock_irqrestore(&info->slock, flags);
	/*
	 * Save the termios structure, since this port may have
	 * separate termios for callout and dialin.
	 */
	if (info->flags & ASYNC_NORMAL_ACTIVE)
		info->normal_termios = *tty->termios;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	info->IER &= ~UART_IER_RLSI;
	if (info->IsMoxaMustChipFlag)
		info->IER &= ~MOXA_MUST_RECV_ISR;
/* by William
	info->read_status_mask &= ~UART_LSR_DR;
*/
	if (info->flags & ASYNC_INITIALIZED) {
		outb(info->IER, info->base + UART_IER);
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		timeout = jiffies + HZ;
		while (!(inb(info->base + UART_LSR) & UART_LSR_TEMT)) {
			schedule_timeout_interruptible(5);
			if (time_after(jiffies, timeout))
				break;
		}
	}
	mxser_shutdown(info);

	if (tty->driver->flush_buffer)
		tty->driver->flush_buffer(tty);
		
	ld = tty_ldisc_ref(tty);
	if (ld) {
		if (ld->flush_buffer)
			ld->flush_buffer(tty);
		tty_ldisc_deref(ld);
	}
		
	tty->closing = 0;
	info->event = 0;
	info->tty = NULL;
	if (info->blocked_open) {
		if (info->close_delay)
			schedule_timeout_interruptible(info->close_delay);
		wake_up_interruptible(&info->open_wait);
	}

	info->flags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CLOSING);
	wake_up_interruptible(&info->close_wait);

}

static int mxser_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	int c, total = 0;
	struct mxser_struct *info = tty->driver_data;
	unsigned long flags;

	if (!info->xmit_buf)
		return 0;

	while (1) {
		c = min_t(int, count, min(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
					  SERIAL_XMIT_SIZE - info->xmit_head));
		if (c <= 0)
			break;

		memcpy(info->xmit_buf + info->xmit_head, buf, c);
		spin_lock_irqsave(&info->slock, flags);
		info->xmit_head = (info->xmit_head + c) &
				  (SERIAL_XMIT_SIZE - 1);
		info->xmit_cnt += c;
		spin_unlock_irqrestore(&info->slock, flags);

		buf += c;
		count -= c;
		total += c;
	}

	if (info->xmit_cnt && !tty->stopped && !(info->IER & UART_IER_THRI)) {
		if (!tty->hw_stopped ||
				(info->type == PORT_16550A) ||
				(info->IsMoxaMustChipFlag)) {
			spin_lock_irqsave(&info->slock, flags);
			info->IER |= UART_IER_THRI;
			outb(info->IER, info->base + UART_IER);
			spin_unlock_irqrestore(&info->slock, flags);
		}
	}
	return total;
}

static void mxser_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct mxser_struct *info = tty->driver_data;
	unsigned long flags;

	if (!info->xmit_buf)
		return;

	if (info->xmit_cnt >= SERIAL_XMIT_SIZE - 1)
		return;

	spin_lock_irqsave(&info->slock, flags);
	info->xmit_buf[info->xmit_head++] = ch;
	info->xmit_head &= SERIAL_XMIT_SIZE - 1;
	info->xmit_cnt++;
	spin_unlock_irqrestore(&info->slock, flags);
	if (!tty->stopped && !(info->IER & UART_IER_THRI)) {
		if (!tty->hw_stopped ||
				(info->type == PORT_16550A) ||
				info->IsMoxaMustChipFlag) {
			spin_lock_irqsave(&info->slock, flags);
			info->IER |= UART_IER_THRI;
			outb(info->IER, info->base + UART_IER);
			spin_unlock_irqrestore(&info->slock, flags);
		}
	}
}


static void mxser_flush_chars(struct tty_struct *tty)
{
	struct mxser_struct *info = tty->driver_data;
	unsigned long flags;

	if (info->xmit_cnt <= 0 ||
			tty->stopped ||
			!info->xmit_buf ||
			(tty->hw_stopped &&
			 (info->type != PORT_16550A) &&
			 (!info->IsMoxaMustChipFlag)
			))
		return;

	spin_lock_irqsave(&info->slock, flags);

	info->IER |= UART_IER_THRI;
	outb(info->IER, info->base + UART_IER);

	spin_unlock_irqrestore(&info->slock, flags);
}

static int mxser_write_room(struct tty_struct *tty)
{
	struct mxser_struct *info = tty->driver_data;
	int ret;

	ret = SERIAL_XMIT_SIZE - info->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
	return ret;
}

static int mxser_chars_in_buffer(struct tty_struct *tty)
{
	struct mxser_struct *info = tty->driver_data;
	return info->xmit_cnt;
}

static void mxser_flush_buffer(struct tty_struct *tty)
{
	struct mxser_struct *info = tty->driver_data;
	char fcr;
	unsigned long flags;


	spin_lock_irqsave(&info->slock, flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	/* below added by shinhay */
	fcr = inb(info->base + UART_FCR);
	outb((fcr | UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT),
		info->base + UART_FCR);
	outb(fcr, info->base + UART_FCR);

	spin_unlock_irqrestore(&info->slock, flags);
	/* above added by shinhay */

	tty_wakeup(tty);
}

static int mxser_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct mxser_struct *info = tty->driver_data;
	int retval;
	struct async_icount cprev, cnow;	/* kernel counter temps */
	struct serial_icounter_struct __user *p_cuser;
	unsigned long templ;
	unsigned long flags;
	void __user *argp = (void __user *)arg;

	if (tty->index == MXSER_PORTS)
		return mxser_ioctl_special(cmd, argp);

	/* following add by Victor Yu. 01-05-2004 */
	if (cmd == MOXA_SET_OP_MODE || cmd == MOXA_GET_OP_MODE) {
		int opmode, p;
		static unsigned char ModeMask[] = { 0xfc, 0xf3, 0xcf, 0x3f };
		int shiftbit;
		unsigned char val, mask;

		p = info->port % 4;
		if (cmd == MOXA_SET_OP_MODE) {
			if (get_user(opmode, (int __user *) argp))
				return -EFAULT;
			if (opmode != RS232_MODE &&
					opmode != RS485_2WIRE_MODE &&
					opmode != RS422_MODE &&
					opmode != RS485_4WIRE_MODE)
				return -EFAULT;
			mask = ModeMask[p];
			shiftbit = p * 2;
			val = inb(info->opmode_ioaddr);
			val &= mask;
			val |= (opmode << shiftbit);
			outb(val, info->opmode_ioaddr);
		} else {
			shiftbit = p * 2;
			opmode = inb(info->opmode_ioaddr) >> shiftbit;
			opmode &= OP_MODE_MASK;
			if (copy_to_user(argp, &opmode, sizeof(int)))
				return -EFAULT;
		}
		return 0;
	}
	/* above add by Victor Yu. 01-05-2004 */

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
			return -EIO;
	}
	switch (cmd) {
	case TCSBRK:		/* SVID version: non-zero arg --> no break */
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		tty_wait_until_sent(tty, 0);
		if (!arg)
			mxser_send_break(info, HZ / 4);	/* 1/4 second */
		return 0;
	case TCSBRKP:		/* support for POSIX tcsendbreak() */
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		tty_wait_until_sent(tty, 0);
		mxser_send_break(info, arg ? arg * (HZ / 10) : HZ / 4);
		return 0;
	case TIOCGSOFTCAR:
		return put_user(C_CLOCAL(tty) ? 1 : 0, (unsigned long __user *)argp);
	case TIOCSSOFTCAR:
		if (get_user(templ, (unsigned long __user *) argp))
			return -EFAULT;
		arg = templ;
		tty->termios->c_cflag = ((tty->termios->c_cflag & ~CLOCAL) | (arg ? CLOCAL : 0));
		return 0;
	case TIOCGSERIAL:
		return mxser_get_serial_info(info, argp);
	case TIOCSSERIAL:
		return mxser_set_serial_info(info, argp);
	case TIOCSERGETLSR:	/* Get line status register */
		return mxser_get_lsr_info(info, argp);
		/*
		 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
		 * - mask passed in arg for lines of interest
		 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
		 * Caller should use TIOCGICOUNT to see which one it was
		 */
	case TIOCMIWAIT:
		spin_lock_irqsave(&info->slock, flags);
		cnow = info->icount;	/* note the counters on entry */
		spin_unlock_irqrestore(&info->slock, flags);

		wait_event_interruptible(info->delta_msr_wait, ({
			cprev = cnow;
			spin_lock_irqsave(&info->slock, flags);
			cnow = info->icount;	/* atomic copy */
			spin_unlock_irqrestore(&info->slock, flags);

			((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
			((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
			((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
			((arg & TIOCM_CTS) && (cnow.cts != cprev.cts));
		}));
		break;
		/*
		 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
		 * Return: write counters to the user passed counter struct
		 * NB: both 1->0 and 0->1 transitions are counted except for
		 *     RI where only 0->1 is counted.
		 */
	case TIOCGICOUNT:
		spin_lock_irqsave(&info->slock, flags);
		cnow = info->icount;
		spin_unlock_irqrestore(&info->slock, flags);
		p_cuser = argp;
		/* modified by casper 1/11/2000 */
		if (put_user(cnow.frame, &p_cuser->frame))
			return -EFAULT;
		if (put_user(cnow.brk, &p_cuser->brk))
			return -EFAULT;
		if (put_user(cnow.overrun, &p_cuser->overrun))
			return -EFAULT;
		if (put_user(cnow.buf_overrun, &p_cuser->buf_overrun))
			return -EFAULT;
		if (put_user(cnow.parity, &p_cuser->parity))
			return -EFAULT;
		if (put_user(cnow.rx, &p_cuser->rx))
			return -EFAULT;
		if (put_user(cnow.tx, &p_cuser->tx))
			return -EFAULT;
		put_user(cnow.cts, &p_cuser->cts);
		put_user(cnow.dsr, &p_cuser->dsr);
		put_user(cnow.rng, &p_cuser->rng);
		put_user(cnow.dcd, &p_cuser->dcd);
		return 0;
	case MOXA_HighSpeedOn:
		return put_user(info->baud_base != 115200 ? 1 : 0, (int __user *)argp);
	case MOXA_SDS_RSTICOUNTER: {
			info->mon_data.rxcnt = 0;
			info->mon_data.txcnt = 0;
			return 0;
		}
/* (above) added by James. */
	case MOXA_ASPP_SETBAUD:{
			long baud;
			if (get_user(baud, (long __user *)argp))
				return -EFAULT;
			mxser_set_baud(info, baud);
			return 0;
		}
	case MOXA_ASPP_GETBAUD:
		if (copy_to_user(argp, &info->realbaud, sizeof(long)))
			return -EFAULT;

		return 0;

	case MOXA_ASPP_OQUEUE:{
			int len, lsr;

			len = mxser_chars_in_buffer(tty);

			lsr = inb(info->base + UART_LSR) & UART_LSR_TEMT;

			len += (lsr ? 0 : 1);

			if (copy_to_user(argp, &len, sizeof(int)))
				return -EFAULT;

			return 0;
		}
	case MOXA_ASPP_MON: {
			int mcr, status;

			/* info->mon_data.ser_param = tty->termios->c_cflag; */

			status = mxser_get_msr(info->base, 1, info->port, info);
			mxser_check_modem_status(info, status);

			mcr = inb(info->base + UART_MCR);
			if (mcr & MOXA_MUST_MCR_XON_FLAG)
				info->mon_data.hold_reason &= ~NPPI_NOTIFY_XOFFHOLD;
			else
				info->mon_data.hold_reason |= NPPI_NOTIFY_XOFFHOLD;

			if (mcr & MOXA_MUST_MCR_TX_XON)
				info->mon_data.hold_reason &= ~NPPI_NOTIFY_XOFFXENT;
			else
				info->mon_data.hold_reason |= NPPI_NOTIFY_XOFFXENT;

			if (info->tty->hw_stopped)
				info->mon_data.hold_reason |= NPPI_NOTIFY_CTSHOLD;
			else
				info->mon_data.hold_reason &= ~NPPI_NOTIFY_CTSHOLD;

			if (copy_to_user(argp, &info->mon_data,
					sizeof(struct mxser_mon)))
				return -EFAULT;

			return 0;
		}

	case MOXA_ASPP_LSTATUS: {
			if (copy_to_user(argp, &info->err_shadow,
					sizeof(unsigned char)))
				return -EFAULT;

			info->err_shadow = 0;
			return 0;
		}
	case MOXA_SET_BAUD_METHOD: {
			int method;

			if (get_user(method, (int __user *)argp))
				return -EFAULT;
			mxser_set_baud_method[info->port] = method;
			if (copy_to_user(argp, &method, sizeof(int)))
				return -EFAULT;

			return 0;
		}
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

#ifndef CMSPAR
#define	CMSPAR 010000000000
#endif

static int mxser_ioctl_special(unsigned int cmd, void __user *argp)
{
	int i, result, status;

	switch (cmd) {
	case MOXA_GET_CONF:
		if (copy_to_user(argp, mxsercfg,
				sizeof(struct mxser_hwconf) * 4))
			return -EFAULT;
		return 0;
	case MOXA_GET_MAJOR:
		if (copy_to_user(argp, &ttymajor, sizeof(int)))
			return -EFAULT;
		return 0;

	case MOXA_GET_CUMAJOR:
		if (copy_to_user(argp, &calloutmajor, sizeof(int)))
			return -EFAULT;
		return 0;

	case MOXA_CHKPORTENABLE:
		result = 0;
		for (i = 0; i < MXSER_PORTS; i++) {
			if (mxvar_table[i].base)
				result |= (1 << i);
		}
		return put_user(result, (unsigned long __user *)argp);
	case MOXA_GETDATACOUNT:
		if (copy_to_user(argp, &mxvar_log, sizeof(mxvar_log)))
			return -EFAULT;
		return 0;
	case MOXA_GETMSTATUS:
		for (i = 0; i < MXSER_PORTS; i++) {
			GMStatus[i].ri = 0;
			if (!mxvar_table[i].base) {
				GMStatus[i].dcd = 0;
				GMStatus[i].dsr = 0;
				GMStatus[i].cts = 0;
				continue;
			}

			if (!mxvar_table[i].tty || !mxvar_table[i].tty->termios)
				GMStatus[i].cflag = mxvar_table[i].normal_termios.c_cflag;
			else
				GMStatus[i].cflag = mxvar_table[i].tty->termios->c_cflag;

			status = inb(mxvar_table[i].base + UART_MSR);
			if (status & 0x80 /*UART_MSR_DCD */ )
				GMStatus[i].dcd = 1;
			else
				GMStatus[i].dcd = 0;

			if (status & 0x20 /*UART_MSR_DSR */ )
				GMStatus[i].dsr = 1;
			else
				GMStatus[i].dsr = 0;


			if (status & 0x10 /*UART_MSR_CTS */ )
				GMStatus[i].cts = 1;
			else
				GMStatus[i].cts = 0;
		}
		if (copy_to_user(argp, GMStatus,
				sizeof(struct mxser_mstatus) * MXSER_PORTS))
			return -EFAULT;
		return 0;
	case MOXA_ASPP_MON_EXT: {
			int status;
			int opmode, p;
			int shiftbit;
			unsigned cflag, iflag;

			for (i = 0; i < MXSER_PORTS; i++) {
				if (!mxvar_table[i].base)
					continue;

				status = mxser_get_msr(mxvar_table[i].base, 0,
							i, &(mxvar_table[i]));
				/*
				mxser_check_modem_status(&mxvar_table[i],
								status);
				*/
				if (status & UART_MSR_TERI)
					mxvar_table[i].icount.rng++;
				if (status & UART_MSR_DDSR)
					mxvar_table[i].icount.dsr++;
				if (status & UART_MSR_DDCD)
					mxvar_table[i].icount.dcd++;
				if (status & UART_MSR_DCTS)
					mxvar_table[i].icount.cts++;

				mxvar_table[i].mon_data.modem_status = status;
				mon_data_ext.rx_cnt[i] = mxvar_table[i].mon_data.rxcnt;
				mon_data_ext.tx_cnt[i] = mxvar_table[i].mon_data.txcnt;
				mon_data_ext.up_rxcnt[i] = mxvar_table[i].mon_data.up_rxcnt;
				mon_data_ext.up_txcnt[i] = mxvar_table[i].mon_data.up_txcnt;
				mon_data_ext.modem_status[i] = mxvar_table[i].mon_data.modem_status;
				mon_data_ext.baudrate[i] = mxvar_table[i].realbaud;

				if (!mxvar_table[i].tty || !mxvar_table[i].tty->termios) {
					cflag = mxvar_table[i].normal_termios.c_cflag;
					iflag = mxvar_table[i].normal_termios.c_iflag;
				} else {
					cflag = mxvar_table[i].tty->termios->c_cflag;
					iflag = mxvar_table[i].tty->termios->c_iflag;
				}

				mon_data_ext.databits[i] = cflag & CSIZE;

				mon_data_ext.stopbits[i] = cflag & CSTOPB;

				mon_data_ext.parity[i] = cflag & (PARENB | PARODD | CMSPAR);

				mon_data_ext.flowctrl[i] = 0x00;

				if (cflag & CRTSCTS)
					mon_data_ext.flowctrl[i] |= 0x03;

				if (iflag & (IXON | IXOFF))
					mon_data_ext.flowctrl[i] |= 0x0C;

				if (mxvar_table[i].type == PORT_16550A)
					mon_data_ext.fifo[i] = 1;
				else
					mon_data_ext.fifo[i] = 0;

				p = i % 4;
				shiftbit = p * 2;
				opmode = inb(mxvar_table[i].opmode_ioaddr) >> shiftbit;
				opmode &= OP_MODE_MASK;

				mon_data_ext.iftype[i] = opmode;

			}
			if (copy_to_user(argp, &mon_data_ext, sizeof(struct mxser_mon_ext)))
				return -EFAULT;

			return 0;

		}
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static void mxser_stoprx(struct tty_struct *tty)
{
	struct mxser_struct *info = tty->driver_data;
	/* unsigned long flags; */

	info->ldisc_stop_rx = 1;
	if (I_IXOFF(tty)) {
		/* MX_LOCK(&info->slock); */
		/* following add by Victor Yu. 09-02-2002 */
		if (info->IsMoxaMustChipFlag) {
			info->IER &= ~MOXA_MUST_RECV_ISR;
			outb(info->IER, info->base + UART_IER);
		} else {
			/* above add by Victor Yu. 09-02-2002 */
			info->x_char = STOP_CHAR(tty);
			/* mask by Victor Yu. 09-02-2002 */
			/* outb(info->IER, 0); */
			outb(0, info->base + UART_IER);
			info->IER |= UART_IER_THRI;
			/* force Tx interrupt */
			outb(info->IER, info->base + UART_IER);
		}		/* add by Victor Yu. 09-02-2002 */
		/* MX_UNLOCK(&info->slock); */
	}

	if (info->tty->termios->c_cflag & CRTSCTS) {
		/* MX_LOCK(&info->slock); */
		info->MCR &= ~UART_MCR_RTS;
		outb(info->MCR, info->base + UART_MCR);
		/* MX_UNLOCK(&info->slock); */
	}
}

static void mxser_startrx(struct tty_struct *tty)
{
	struct mxser_struct *info = tty->driver_data;
	/* unsigned long flags; */

	info->ldisc_stop_rx = 0;
	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else {
			/* MX_LOCK(&info->slock); */

			/* following add by Victor Yu. 09-02-2002 */
			if (info->IsMoxaMustChipFlag) {
				info->IER |= MOXA_MUST_RECV_ISR;
				outb(info->IER, info->base + UART_IER);
			} else {
				/* above add by Victor Yu. 09-02-2002 */

				info->x_char = START_CHAR(tty);
				/* mask by Victor Yu. 09-02-2002 */
				/* outb(info->IER, 0); */
				/* add by Victor Yu. 09-02-2002 */
				outb(0, info->base + UART_IER);
				/* force Tx interrupt */
				info->IER |= UART_IER_THRI;
				outb(info->IER, info->base + UART_IER);
			}	/* add by Victor Yu. 09-02-2002 */
			/* MX_UNLOCK(&info->slock); */
		}
	}

	if (info->tty->termios->c_cflag & CRTSCTS) {
		/* MX_LOCK(&info->slock); */
		info->MCR |= UART_MCR_RTS;
		outb(info->MCR, info->base + UART_MCR);
		/* MX_UNLOCK(&info->slock); */
	}
}

/*
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 */
static void mxser_throttle(struct tty_struct *tty)
{
	/* struct mxser_struct *info = tty->driver_data; */
	/* unsigned long flags; */

	/* MX_LOCK(&info->slock); */
	mxser_stoprx(tty);
	/* MX_UNLOCK(&info->slock); */
}

static void mxser_unthrottle(struct tty_struct *tty)
{
	/* struct mxser_struct *info = tty->driver_data; */
	/* unsigned long flags; */

	/* MX_LOCK(&info->slock); */
	mxser_startrx(tty);
	/* MX_UNLOCK(&info->slock); */
}

static void mxser_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	struct mxser_struct *info = tty->driver_data;
	unsigned long flags;

	mxser_change_speed(info, old_termios);

	if ((old_termios->c_cflag & CRTSCTS) &&
			!(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		mxser_start(tty);
	}

/* Handle sw stopped */
	if ((old_termios->c_iflag & IXON) &&
			!(tty->termios->c_iflag & IXON)) {
		tty->stopped = 0;

		/* following add by Victor Yu. 09-02-2002 */
		if (info->IsMoxaMustChipFlag) {
			spin_lock_irqsave(&info->slock, flags);
			DISABLE_MOXA_MUST_RX_SOFTWARE_FLOW_CONTROL(info->base);
			spin_unlock_irqrestore(&info->slock, flags);
		}
		/* above add by Victor Yu. 09-02-2002 */

		mxser_start(tty);
	}
}

/*
 * mxser_stop() and mxser_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter interrupts, as necessary.
 */
static void mxser_stop(struct tty_struct *tty)
{
	struct mxser_struct *info = tty->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&info->slock, flags);
	if (info->IER & UART_IER_THRI) {
		info->IER &= ~UART_IER_THRI;
		outb(info->IER, info->base + UART_IER);
	}
	spin_unlock_irqrestore(&info->slock, flags);
}

static void mxser_start(struct tty_struct *tty)
{
	struct mxser_struct *info = tty->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&info->slock, flags);
	if (info->xmit_cnt && info->xmit_buf && !(info->IER & UART_IER_THRI)) {
		info->IER |= UART_IER_THRI;
		outb(info->IER, info->base + UART_IER);
	}
	spin_unlock_irqrestore(&info->slock, flags);
}

/*
 * mxser_wait_until_sent() --- wait until the transmitter is empty
 */
static void mxser_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct mxser_struct *info = tty->driver_data;
	unsigned long orig_jiffies, char_time;
	int lsr;

	if (info->type == PORT_UNKNOWN)
		return;

	if (info->xmit_fifo_size == 0)
		return;		/* Just in case.... */

	orig_jiffies = jiffies;
	/*
	 * Set the check interval to be 1/5 of the estimated time to
	 * send a single character, and make it at least 1.  The check
	 * interval should also be less than the timeout.
	 *
	 * Note: we have to use pretty tight timings here to satisfy
	 * the NIST-PCTS.
	 */
	char_time = (info->timeout - HZ / 50) / info->xmit_fifo_size;
	char_time = char_time / 5;
	if (char_time == 0)
		char_time = 1;
	if (timeout && timeout < char_time)
		char_time = timeout;
	/*
	 * If the transmitter hasn't cleared in twice the approximate
	 * amount of time to send the entire FIFO, it probably won't
	 * ever clear.  This assumes the UART isn't doing flow
	 * control, which is currently the case.  Hence, if it ever
	 * takes longer than info->timeout, this is probably due to a
	 * UART bug of some kind.  So, we clamp the timeout parameter at
	 * 2*info->timeout.
	 */
	if (!timeout || timeout > 2 * info->timeout)
		timeout = 2 * info->timeout;
#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
	printk(KERN_DEBUG "In rs_wait_until_sent(%d) check=%lu...",
		timeout, char_time);
	printk("jiff=%lu...", jiffies);
#endif
	while (!((lsr = inb(info->base + UART_LSR)) & UART_LSR_TEMT)) {
#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
		printk("lsr = %d (jiff=%lu)...", lsr, jiffies);
#endif
		schedule_timeout_interruptible(char_time);
		if (signal_pending(current))
			break;
		if (timeout && time_after(jiffies, orig_jiffies + timeout))
			break;
	}
	set_current_state(TASK_RUNNING);

#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
	printk("lsr = %d (jiff=%lu)...done\n", lsr, jiffies);
#endif
}


/*
 * This routine is called by tty_hangup() when a hangup is signaled.
 */
void mxser_hangup(struct tty_struct *tty)
{
	struct mxser_struct *info = tty->driver_data;

	mxser_flush_buffer(tty);
	mxser_shutdown(info);
	info->event = 0;
	info->count = 0;
	info->flags &= ~ASYNC_NORMAL_ACTIVE;
	info->tty = NULL;
	wake_up_interruptible(&info->open_wait);
}


/* added by James 03-12-2004. */
/*
 * mxser_rs_break() --- routine which turns the break handling on or off
 */
static void mxser_rs_break(struct tty_struct *tty, int break_state)
{
	struct mxser_struct *info = tty->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&info->slock, flags);
	if (break_state == -1)
		outb(inb(info->base + UART_LCR) | UART_LCR_SBC,
			info->base + UART_LCR);
	else
		outb(inb(info->base + UART_LCR) & ~UART_LCR_SBC,
			info->base + UART_LCR);
	spin_unlock_irqrestore(&info->slock, flags);
}

/* (above) added by James. */


/*
 * This is the serial driver's generic interrupt routine
 */
static irqreturn_t mxser_interrupt(int irq, void *dev_id)
{
	int status, iir, i;
	struct mxser_struct *info;
	struct mxser_struct *port;
	int max, irqbits, bits, msr;
	int pass_counter = 0;
	int handled = IRQ_NONE;

	port = NULL;
	/* spin_lock(&gm_lock); */

	for (i = 0; i < MXSER_BOARDS; i++) {
		if (dev_id == &(mxvar_table[i * MXSER_PORTS_PER_BOARD])) {
			port = dev_id;
			break;
		}
	}

	if (i == MXSER_BOARDS)
		goto irq_stop;
	if (port == 0)
		goto irq_stop;
	max = mxser_numports[mxsercfg[i].board_type - 1];
	while (1) {
		irqbits = inb(port->vector) & port->vectormask;
		if (irqbits == port->vectormask)
			break;

		handled = IRQ_HANDLED;
		for (i = 0, bits = 1; i < max; i++, irqbits |= bits, bits <<= 1) {
			if (irqbits == port->vectormask)
				break;
			if (bits & irqbits)
				continue;
			info = port + i;

			/* following add by Victor Yu. 09-13-2002 */
			iir = inb(info->base + UART_IIR);
			if (iir & UART_IIR_NO_INT)
				continue;
			iir &= MOXA_MUST_IIR_MASK;
			if (!info->tty) {
				status = inb(info->base + UART_LSR);
				outb(0x27, info->base + UART_FCR);
				inb(info->base + UART_MSR);
				continue;
			}

			/* mask by Victor Yu. 09-13-2002
			   if ( !info->tty ||
			   (inb(info->base + UART_IIR) & UART_IIR_NO_INT) )
			   continue;
			 */
			/* mask by Victor Yu. 09-02-2002
			   status = inb(info->base + UART_LSR) & info->read_status_mask;
			 */

			/* following add by Victor Yu. 09-02-2002 */
			status = inb(info->base + UART_LSR);

			if (status & UART_LSR_PE)
				info->err_shadow |= NPPI_NOTIFY_PARITY;
			if (status & UART_LSR_FE)
				info->err_shadow |= NPPI_NOTIFY_FRAMING;
			if (status & UART_LSR_OE)
				info->err_shadow |= NPPI_NOTIFY_HW_OVERRUN;
			if (status & UART_LSR_BI)
				info->err_shadow |= NPPI_NOTIFY_BREAK;

			if (info->IsMoxaMustChipFlag) {
				/*
				   if ( (status & 0x02) && !(status & 0x01) ) {
				   outb(info->base+UART_FCR,  0x23);
				   continue;
				   }
				 */
				if (iir == MOXA_MUST_IIR_GDA ||
						iir == MOXA_MUST_IIR_RDA ||
						iir == MOXA_MUST_IIR_RTO ||
						iir == MOXA_MUST_IIR_LSR)
					mxser_receive_chars(info, &status);

			} else {
				/* above add by Victor Yu. 09-02-2002 */

				status &= info->read_status_mask;
				if (status & UART_LSR_DR)
					mxser_receive_chars(info, &status);
			}
			msr = inb(info->base + UART_MSR);
			if (msr & UART_MSR_ANY_DELTA) {
				mxser_check_modem_status(info, msr);
			}
			/* following add by Victor Yu. 09-13-2002 */
			if (info->IsMoxaMustChipFlag) {
				if ((iir == 0x02) && (status & UART_LSR_THRE)) {
					mxser_transmit_chars(info);
				}
			} else {
				/* above add by Victor Yu. 09-13-2002 */

				if (status & UART_LSR_THRE) {
/* 8-2-99 by William
			    if ( info->x_char || (info->xmit_cnt > 0) )
*/
					mxser_transmit_chars(info);
				}
			}
		}
		if (pass_counter++ > MXSER_ISR_PASS_LIMIT) {
			break;	/* Prevent infinite loops */
		}
	}

      irq_stop:
	/* spin_unlock(&gm_lock); */
	return handled;
}

static void mxser_receive_chars(struct mxser_struct *info, int *status)
{
	struct tty_struct *tty = info->tty;
	unsigned char ch, gdl;
	int ignored = 0;
	int cnt = 0;
	int recv_room;
	int max = 256;
	unsigned long flags;

	spin_lock_irqsave(&info->slock, flags);

	recv_room = tty->receive_room;
	if ((recv_room == 0) && (!info->ldisc_stop_rx)) {
		/* mxser_throttle(tty); */
		mxser_stoprx(tty);
		/* return; */
	}

	/* following add by Victor Yu. 09-02-2002 */
	if (info->IsMoxaMustChipFlag != MOXA_OTHER_UART) {

		if (*status & UART_LSR_SPECIAL) {
			goto intr_old;
		}
		/* following add by Victor Yu. 02-11-2004 */
		if (info->IsMoxaMustChipFlag == MOXA_MUST_MU860_HWID &&
				(*status & MOXA_MUST_LSR_RERR))
			goto intr_old;
		/* above add by Victor Yu. 02-14-2004 */
		if (*status & MOXA_MUST_LSR_RERR)
			goto intr_old;

		gdl = inb(info->base + MOXA_MUST_GDL_REGISTER);

		/* add by Victor Yu. 02-11-2004 */
		if (info->IsMoxaMustChipFlag == MOXA_MUST_MU150_HWID)
			gdl &= MOXA_MUST_GDL_MASK;
		if (gdl >= recv_room) {
			if (!info->ldisc_stop_rx) {
				/* mxser_throttle(tty); */
				mxser_stoprx(tty);
			}
			/* return; */
		}
		while (gdl--) {
			ch = inb(info->base + UART_RX);
			tty_insert_flip_char(tty, ch, 0);
			cnt++;
			/*
			   if ((cnt >= HI_WATER) && (info->stop_rx == 0)) {
			   mxser_stoprx(tty);
			   info->stop_rx = 1;
			   break;
			   } */
		}
		goto end_intr;
	}
 intr_old:
	/* above add by Victor Yu. 09-02-2002 */

	do {
		if (max-- < 0)
			break;
		/*
		   if ((cnt >= HI_WATER) && (info->stop_rx == 0)) {
		   mxser_stoprx(tty);
		   info->stop_rx=1;
		   break;
		   }
		 */

		ch = inb(info->base + UART_RX);
		/* following add by Victor Yu. 09-02-2002 */
		if (info->IsMoxaMustChipFlag && (*status & UART_LSR_OE) /*&& !(*status&UART_LSR_DR) */ )
			outb(0x23, info->base + UART_FCR);
		*status &= info->read_status_mask;
		/* above add by Victor Yu. 09-02-2002 */
		if (*status & info->ignore_status_mask) {
			if (++ignored > 100)
				break;
		} else {
			char flag = 0;
			if (*status & UART_LSR_SPECIAL) {
				if (*status & UART_LSR_BI) {
					flag = TTY_BREAK;
/* added by casper 1/11/2000 */
					info->icount.brk++;
/* */
					if (info->flags & ASYNC_SAK)
						do_SAK(tty);
				} else if (*status & UART_LSR_PE) {
					flag = TTY_PARITY;
/* added by casper 1/11/2000 */
					info->icount.parity++;
/* */
				} else if (*status & UART_LSR_FE) {
					flag = TTY_FRAME;
/* added by casper 1/11/2000 */
					info->icount.frame++;
/* */
				} else if (*status & UART_LSR_OE) {
					flag = TTY_OVERRUN;
/* added by casper 1/11/2000 */
					info->icount.overrun++;
/* */
				}
			}
			tty_insert_flip_char(tty, ch, flag);
			cnt++;
			if (cnt >= recv_room) {
				if (!info->ldisc_stop_rx) {
					/* mxser_throttle(tty); */
					mxser_stoprx(tty);
				}
				break;
			}

		}

		/* following add by Victor Yu. 09-02-2002 */
		if (info->IsMoxaMustChipFlag)
			break;
		/* above add by Victor Yu. 09-02-2002 */

		/* mask by Victor Yu. 09-02-2002
		 *status = inb(info->base + UART_LSR) & info->read_status_mask;
		 */
		/* following add by Victor Yu. 09-02-2002 */
		*status = inb(info->base + UART_LSR);
		/* above add by Victor Yu. 09-02-2002 */
	} while (*status & UART_LSR_DR);

end_intr:		/* add by Victor Yu. 09-02-2002 */
	mxvar_log.rxcnt[info->port] += cnt;
	info->mon_data.rxcnt += cnt;
	info->mon_data.up_rxcnt += cnt;
	spin_unlock_irqrestore(&info->slock, flags);

	tty_flip_buffer_push(tty);
}

static void mxser_transmit_chars(struct mxser_struct *info)
{
	int count, cnt;
	unsigned long flags;

	spin_lock_irqsave(&info->slock, flags);

	if (info->x_char) {
		outb(info->x_char, info->base + UART_TX);
		info->x_char = 0;
		mxvar_log.txcnt[info->port]++;
		info->mon_data.txcnt++;
		info->mon_data.up_txcnt++;

/* added by casper 1/11/2000 */
		info->icount.tx++;
/* */
		spin_unlock_irqrestore(&info->slock, flags);
		return;
	}

	if (info->xmit_buf == 0) {
		spin_unlock_irqrestore(&info->slock, flags);
		return;
	}

	if ((info->xmit_cnt <= 0) || info->tty->stopped ||
			(info->tty->hw_stopped &&
			(info->type != PORT_16550A) &&
			(!info->IsMoxaMustChipFlag))) {
		info->IER &= ~UART_IER_THRI;
		outb(info->IER, info->base + UART_IER);
		spin_unlock_irqrestore(&info->slock, flags);
		return;
	}

	cnt = info->xmit_cnt;
	count = info->xmit_fifo_size;
	do {
		outb(info->xmit_buf[info->xmit_tail++],
			info->base + UART_TX);
		info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE - 1);
		if (--info->xmit_cnt <= 0)
			break;
	} while (--count > 0);
	mxvar_log.txcnt[info->port] += (cnt - info->xmit_cnt);

/* added by James 03-12-2004. */
	info->mon_data.txcnt += (cnt - info->xmit_cnt);
	info->mon_data.up_txcnt += (cnt - info->xmit_cnt);
/* (above) added by James. */

/* added by casper 1/11/2000 */
	info->icount.tx += (cnt - info->xmit_cnt);
/* */

	if (info->xmit_cnt < WAKEUP_CHARS) {
		set_bit(MXSER_EVENT_TXLOW, &info->event);
		schedule_work(&info->tqueue);
	}
	if (info->xmit_cnt <= 0) {
		info->IER &= ~UART_IER_THRI;
		outb(info->IER, info->base + UART_IER);
	}
	spin_unlock_irqrestore(&info->slock, flags);
}

static void mxser_check_modem_status(struct mxser_struct *info, int status)
{
	/* update input line counters */
	if (status & UART_MSR_TERI)
		info->icount.rng++;
	if (status & UART_MSR_DDSR)
		info->icount.dsr++;
	if (status & UART_MSR_DDCD)
		info->icount.dcd++;
	if (status & UART_MSR_DCTS)
		info->icount.cts++;
	info->mon_data.modem_status = status;
	wake_up_interruptible(&info->delta_msr_wait);

	if ((info->flags & ASYNC_CHECK_CD) && (status & UART_MSR_DDCD)) {
		if (status & UART_MSR_DCD)
			wake_up_interruptible(&info->open_wait);
		schedule_work(&info->tqueue);
	}

	if (info->flags & ASYNC_CTS_FLOW) {
		if (info->tty->hw_stopped) {
			if (status & UART_MSR_CTS) {
				info->tty->hw_stopped = 0;

				if ((info->type != PORT_16550A) &&
						(!info->IsMoxaMustChipFlag)) {
					info->IER |= UART_IER_THRI;
					outb(info->IER, info->base + UART_IER);
				}
				set_bit(MXSER_EVENT_TXLOW, &info->event);
				schedule_work(&info->tqueue);			}
		} else {
			if (!(status & UART_MSR_CTS)) {
				info->tty->hw_stopped = 1;
				if ((info->type != PORT_16550A) &&
						(!info->IsMoxaMustChipFlag)) {
					info->IER &= ~UART_IER_THRI;
					outb(info->IER, info->base + UART_IER);
				}
			}
		}
	}
}

static int mxser_block_til_ready(struct tty_struct *tty, struct file *filp, struct mxser_struct *info)
{
	DECLARE_WAITQUEUE(wait, current);
	int retval;
	int do_clocal = 0;
	unsigned long flags;

	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) || (tty->flags & (1 << TTY_IO_ERROR))) {
		info->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	if (tty->termios->c_cflag & CLOCAL)
		do_clocal = 1;

	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * mxser_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);

	spin_lock_irqsave(&info->slock, flags);
	if (!tty_hung_up_p(filp))
		info->count--;
	spin_unlock_irqrestore(&info->slock, flags);
	info->blocked_open++;
	while (1) {
		spin_lock_irqsave(&info->slock, flags);
		outb(inb(info->base + UART_MCR) |
			UART_MCR_DTR | UART_MCR_RTS, info->base + UART_MCR);
		spin_unlock_irqrestore(&info->slock, flags);
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) || !(info->flags & ASYNC_INITIALIZED)) {
			if (info->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
			break;
		}
		if (!(info->flags & ASYNC_CLOSING) &&
				(do_clocal ||
				(inb(info->base + UART_MSR) & UART_MSR_DCD)))
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;
	if (retval)
		return retval;
	info->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}

static int mxser_startup(struct mxser_struct *info)
{
	unsigned long page;
	unsigned long flags;

	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	spin_lock_irqsave(&info->slock, flags);

	if (info->flags & ASYNC_INITIALIZED) {
		free_page(page);
		spin_unlock_irqrestore(&info->slock, flags);
		return 0;
	}

	if (!info->base || !info->type) {
		if (info->tty)
			set_bit(TTY_IO_ERROR, &info->tty->flags);
		free_page(page);
		spin_unlock_irqrestore(&info->slock, flags);
		return 0;
	}
	if (info->xmit_buf)
		free_page(page);
	else
		info->xmit_buf = (unsigned char *) page;

	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in mxser_change_speed())
	 */
	if (info->IsMoxaMustChipFlag)
		outb((UART_FCR_CLEAR_RCVR |
			UART_FCR_CLEAR_XMIT |
			MOXA_MUST_FCR_GDA_MODE_ENABLE), info->base + UART_FCR);
	else
		outb((UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT),
			info->base + UART_FCR);

	/*
	 * At this point there's no way the LSR could still be 0xFF;
	 * if it is, then bail out, because there's likely no UART
	 * here.
	 */
	if (inb(info->base + UART_LSR) == 0xff) {
		spin_unlock_irqrestore(&info->slock, flags);
		if (capable(CAP_SYS_ADMIN)) {
			if (info->tty)
				set_bit(TTY_IO_ERROR, &info->tty->flags);
			return 0;
		} else
			return -ENODEV;
	}

	/*
	 * Clear the interrupt registers.
	 */
	(void) inb(info->base + UART_LSR);
	(void) inb(info->base + UART_RX);
	(void) inb(info->base + UART_IIR);
	(void) inb(info->base + UART_MSR);

	/*
	 * Now, initialize the UART
	 */
	outb(UART_LCR_WLEN8, info->base + UART_LCR);	/* reset DLAB */
	info->MCR = UART_MCR_DTR | UART_MCR_RTS;
	outb(info->MCR, info->base + UART_MCR);

	/*
	 * Finally, enable interrupts
	 */
	info->IER = UART_IER_MSI | UART_IER_RLSI | UART_IER_RDI;
	/* info->IER = UART_IER_RLSI | UART_IER_RDI; */

	/* following add by Victor Yu. 08-30-2002 */
	if (info->IsMoxaMustChipFlag)
		info->IER |= MOXA_MUST_IER_EGDAI;
	/* above add by Victor Yu. 08-30-2002 */
	outb(info->IER, info->base + UART_IER);	/* enable interrupts */

	/*
	 * And clear the interrupt registers again for luck.
	 */
	(void) inb(info->base + UART_LSR);
	(void) inb(info->base + UART_RX);
	(void) inb(info->base + UART_IIR);
	(void) inb(info->base + UART_MSR);

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	/*
	 * and set the speed of the serial port
	 */
	spin_unlock_irqrestore(&info->slock, flags);
	mxser_change_speed(info, NULL);

	info->flags |= ASYNC_INITIALIZED;
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts maybe disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void mxser_shutdown(struct mxser_struct *info)
{
	unsigned long flags;

	if (!(info->flags & ASYNC_INITIALIZED))
		return;

	spin_lock_irqsave(&info->slock, flags);

	/*
	 * clear delta_msr_wait queue to avoid mem leaks: we may free the irq
	 * here so the queue might never be waken up
	 */
	wake_up_interruptible(&info->delta_msr_wait);

	/*
	 * Free the IRQ, if necessary
	 */
	if (info->xmit_buf) {
		free_page((unsigned long) info->xmit_buf);
		info->xmit_buf = NULL;
	}

	info->IER = 0;
	outb(0x00, info->base + UART_IER);

	if (!info->tty || (info->tty->termios->c_cflag & HUPCL))
		info->MCR &= ~(UART_MCR_DTR | UART_MCR_RTS);
	outb(info->MCR, info->base + UART_MCR);

	/* clear Rx/Tx FIFO's */
	/* following add by Victor Yu. 08-30-2002 */
	if (info->IsMoxaMustChipFlag)
		outb((UART_FCR_CLEAR_RCVR |
			UART_FCR_CLEAR_XMIT |
			MOXA_MUST_FCR_GDA_MODE_ENABLE), info->base + UART_FCR);
	else
		/* above add by Victor Yu. 08-30-2002 */
		outb((UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT),
			info->base + UART_FCR);

	/* read data port to reset things */
	(void) inb(info->base + UART_RX);

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	info->flags &= ~ASYNC_INITIALIZED;

	/* following add by Victor Yu. 09-23-2002 */
	if (info->IsMoxaMustChipFlag)
		SET_MOXA_MUST_NO_SOFTWARE_FLOW_CONTROL(info->base);
	/* above add by Victor Yu. 09-23-2002 */

	spin_unlock_irqrestore(&info->slock, flags);
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static int mxser_change_speed(struct mxser_struct *info, struct ktermios *old_termios)
{
	unsigned cflag, cval, fcr;
	int ret = 0;
	unsigned char status;
	long baud;
	unsigned long flags;

	if (!info->tty || !info->tty->termios)
		return ret;
	cflag = info->tty->termios->c_cflag;
	if (!(info->base))
		return ret;

#ifndef B921600
#define B921600 (B460800 +1)
#endif
	if (mxser_set_baud_method[info->port] == 0) {
		baud = tty_get_baud_rate(info->tty);
		mxser_set_baud(info, baud);
	}

	/* byte size and parity */
	switch (cflag & CSIZE) {
	case CS5:
		cval = 0x00;
		break;
	case CS6:
		cval = 0x01;
		break;
	case CS7:
		cval = 0x02;
		break;
	case CS8:
		cval = 0x03;
		break;
	default:
		cval = 0x00;
		break;		/* too keep GCC shut... */
	}
	if (cflag & CSTOPB)
		cval |= 0x04;
	if (cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;
	if (cflag & CMSPAR)
		cval |= UART_LCR_SPAR;

	if ((info->type == PORT_8250) || (info->type == PORT_16450)) {
		if (info->IsMoxaMustChipFlag) {
			fcr = UART_FCR_ENABLE_FIFO;
			fcr |= MOXA_MUST_FCR_GDA_MODE_ENABLE;
			SET_MOXA_MUST_FIFO_VALUE(info);
		} else
			fcr = 0;
	} else {
		fcr = UART_FCR_ENABLE_FIFO;
		/* following add by Victor Yu. 08-30-2002 */
		if (info->IsMoxaMustChipFlag) {
			fcr |= MOXA_MUST_FCR_GDA_MODE_ENABLE;
			SET_MOXA_MUST_FIFO_VALUE(info);
		} else {
			/* above add by Victor Yu. 08-30-2002 */
			switch (info->rx_trigger) {
			case 1:
				fcr |= UART_FCR_TRIGGER_1;
				break;
			case 4:
				fcr |= UART_FCR_TRIGGER_4;
				break;
			case 8:
				fcr |= UART_FCR_TRIGGER_8;
				break;
			default:
				fcr |= UART_FCR_TRIGGER_14;
				break;
			}
		}
	}

	/* CTS flow control flag and modem status interrupts */
	info->IER &= ~UART_IER_MSI;
	info->MCR &= ~UART_MCR_AFE;
	if (cflag & CRTSCTS) {
		info->flags |= ASYNC_CTS_FLOW;
		info->IER |= UART_IER_MSI;
		if ((info->type == PORT_16550A) || (info->IsMoxaMustChipFlag)) {
			info->MCR |= UART_MCR_AFE;
		} else {
			status = inb(info->base + UART_MSR);
			if (info->tty->hw_stopped) {
				if (status & UART_MSR_CTS) {
					info->tty->hw_stopped = 0;
					if ((info->type != PORT_16550A) &&
							(!info->IsMoxaMustChipFlag)) {
						info->IER |= UART_IER_THRI;
						outb(info->IER, info->base + UART_IER);
					}
					set_bit(MXSER_EVENT_TXLOW, &info->event);
					schedule_work(&info->tqueue);				}
			} else {
				if (!(status & UART_MSR_CTS)) {
					info->tty->hw_stopped = 1;
					if ((info->type != PORT_16550A) &&
							(!info->IsMoxaMustChipFlag)) {
						info->IER &= ~UART_IER_THRI;
						outb(info->IER, info->base + UART_IER);
					}
				}
			}
		}
	} else {
		info->flags &= ~ASYNC_CTS_FLOW;
	}
	outb(info->MCR, info->base + UART_MCR);
	if (cflag & CLOCAL) {
		info->flags &= ~ASYNC_CHECK_CD;
	} else {
		info->flags |= ASYNC_CHECK_CD;
		info->IER |= UART_IER_MSI;
	}
	outb(info->IER, info->base + UART_IER);

	/*
	 * Set up parity check flag
	 */
	info->read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (I_INPCK(info->tty))
		info->read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (I_BRKINT(info->tty) || I_PARMRK(info->tty))
		info->read_status_mask |= UART_LSR_BI;

	info->ignore_status_mask = 0;

	if (I_IGNBRK(info->tty)) {
		info->ignore_status_mask |= UART_LSR_BI;
		info->read_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignore parity and break indicators, ignore
		 * overruns too.  (For real raw support).
		 */
		if (I_IGNPAR(info->tty)) {
			info->ignore_status_mask |=
						UART_LSR_OE |
						UART_LSR_PE |
						UART_LSR_FE;
			info->read_status_mask |=
						UART_LSR_OE |
						UART_LSR_PE |
						UART_LSR_FE;
		}
	}
	/* following add by Victor Yu. 09-02-2002 */
	if (info->IsMoxaMustChipFlag) {
		spin_lock_irqsave(&info->slock, flags);
		SET_MOXA_MUST_XON1_VALUE(info->base, START_CHAR(info->tty));
		SET_MOXA_MUST_XOFF1_VALUE(info->base, STOP_CHAR(info->tty));
		if (I_IXON(info->tty)) {
			ENABLE_MOXA_MUST_RX_SOFTWARE_FLOW_CONTROL(info->base);
		} else {
			DISABLE_MOXA_MUST_RX_SOFTWARE_FLOW_CONTROL(info->base);
		}
		if (I_IXOFF(info->tty)) {
			ENABLE_MOXA_MUST_TX_SOFTWARE_FLOW_CONTROL(info->base);
		} else {
			DISABLE_MOXA_MUST_TX_SOFTWARE_FLOW_CONTROL(info->base);
		}
		/*
		   if ( I_IXANY(info->tty) ) {
		   info->MCR |= MOXA_MUST_MCR_XON_ANY;
		   ENABLE_MOXA_MUST_XON_ANY_FLOW_CONTROL(info->base);
		   } else {
		   info->MCR &= ~MOXA_MUST_MCR_XON_ANY;
		   DISABLE_MOXA_MUST_XON_ANY_FLOW_CONTROL(info->base);
		   }
		 */
		spin_unlock_irqrestore(&info->slock, flags);
	}
	/* above add by Victor Yu. 09-02-2002 */


	outb(fcr, info->base + UART_FCR);	/* set fcr */
	outb(cval, info->base + UART_LCR);

	return ret;
}


static int mxser_set_baud(struct mxser_struct *info, long newspd)
{
	int quot = 0;
	unsigned char cval;
	int ret = 0;
	unsigned long flags;

	if (!info->tty || !info->tty->termios)
		return ret;

	if (!(info->base))
		return ret;

	if (newspd > info->MaxCanSetBaudRate)
		return 0;

	info->realbaud = newspd;
	if (newspd == 134) {
		quot = (2 * info->baud_base / 269);
	} else if (newspd) {
		quot = info->baud_base / newspd;
		if (quot == 0)
			quot = 1;
	} else {
		quot = 0;
	}

	info->timeout = ((info->xmit_fifo_size * HZ * 10 * quot) / info->baud_base);
	info->timeout += HZ / 50;	/* Add .02 seconds of slop */

	if (quot) {
		spin_lock_irqsave(&info->slock, flags);
		info->MCR |= UART_MCR_DTR;
		outb(info->MCR, info->base + UART_MCR);
		spin_unlock_irqrestore(&info->slock, flags);
	} else {
		spin_lock_irqsave(&info->slock, flags);
		info->MCR &= ~UART_MCR_DTR;
		outb(info->MCR, info->base + UART_MCR);
		spin_unlock_irqrestore(&info->slock, flags);
		return ret;
	}

	cval = inb(info->base + UART_LCR);

	outb(cval | UART_LCR_DLAB, info->base + UART_LCR);	/* set DLAB */

	outb(quot & 0xff, info->base + UART_DLL);	/* LS of divisor */
	outb(quot >> 8, info->base + UART_DLM);	/* MS of divisor */
	outb(cval, info->base + UART_LCR);	/* reset DLAB */


	return ret;
}

/*
 * ------------------------------------------------------------
 * friends of mxser_ioctl()
 * ------------------------------------------------------------
 */
static int mxser_get_serial_info(struct mxser_struct *info, struct serial_struct __user *retinfo)
{
	struct serial_struct tmp;

	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = info->type;
	tmp.line = info->port;
	tmp.port = info->base;
	tmp.irq = info->irq;
	tmp.flags = info->flags;
	tmp.baud_base = info->baud_base;
	tmp.close_delay = info->close_delay;
	tmp.closing_wait = info->closing_wait;
	tmp.custom_divisor = info->custom_divisor;
	tmp.hub6 = 0;
	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int mxser_set_serial_info(struct mxser_struct *info, struct serial_struct __user *new_info)
{
	struct serial_struct new_serial;
	unsigned int flags;
	int retval = 0;

	if (!new_info || !info->base)
		return -EFAULT;
	if (copy_from_user(&new_serial, new_info, sizeof(new_serial)))
		return -EFAULT;

	if ((new_serial.irq != info->irq) ||
			(new_serial.port != info->base) ||
			(new_serial.custom_divisor != info->custom_divisor) ||
			(new_serial.baud_base != info->baud_base))
		return -EPERM;

	flags = info->flags & ASYNC_SPD_MASK;

	if (!capable(CAP_SYS_ADMIN)) {
		if ((new_serial.baud_base != info->baud_base) ||
				(new_serial.close_delay != info->close_delay) ||
				((new_serial.flags & ~ASYNC_USR_MASK) != (info->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		info->flags = ((info->flags & ~ASYNC_USR_MASK) |
				(new_serial.flags & ASYNC_USR_MASK));
	} else {
		/*
		 * OK, past this point, all the error checking has been done.
		 * At this point, we start making changes.....
		 */
		info->flags = ((info->flags & ~ASYNC_FLAGS) |
				(new_serial.flags & ASYNC_FLAGS));
		info->close_delay = new_serial.close_delay * HZ / 100;
		info->closing_wait = new_serial.closing_wait * HZ / 100;
		info->tty->low_latency =
				(info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;
		info->tty->low_latency = 0;	/* (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0; */
	}

	/* added by casper, 3/17/2000, for mouse */
	info->type = new_serial.type;

	process_txrx_fifo(info);

	if (info->flags & ASYNC_INITIALIZED) {
		if (flags != (info->flags & ASYNC_SPD_MASK)) {
			mxser_change_speed(info, NULL);
		}
	} else {
		retval = mxser_startup(info);
	}
	return retval;
}

/*
 * mxser_get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 *	    is emptied.  On bus types like RS485, the transmitter must
 *	    release the bus after transmitting. This must be done when
 *	    the transmit shift register is empty, not be done when the
 *	    transmit holding register is empty.  This functionality
 *	    allows an RS485 driver to be written in user space.
 */
static int mxser_get_lsr_info(struct mxser_struct *info, unsigned int __user *value)
{
	unsigned char status;
	unsigned int result;
	unsigned long flags;

	spin_lock_irqsave(&info->slock, flags);
	status = inb(info->base + UART_LSR);
	spin_unlock_irqrestore(&info->slock, flags);
	result = ((status & UART_LSR_TEMT) ? TIOCSER_TEMT : 0);
	return put_user(result, value);
}

/*
 * This routine sends a break character out the serial port.
 */
static void mxser_send_break(struct mxser_struct *info, int duration)
{
	unsigned long flags;

	if (!info->base)
		return;
	set_current_state(TASK_INTERRUPTIBLE);
	spin_lock_irqsave(&info->slock, flags);
	outb(inb(info->base + UART_LCR) | UART_LCR_SBC,
		info->base + UART_LCR);
	spin_unlock_irqrestore(&info->slock, flags);
	schedule_timeout(duration);
	spin_lock_irqsave(&info->slock, flags);
	outb(inb(info->base + UART_LCR) & ~UART_LCR_SBC,
		info->base + UART_LCR);
	spin_unlock_irqrestore(&info->slock, flags);
}

static int mxser_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct mxser_struct *info = tty->driver_data;
	unsigned char control, status;
	unsigned long flags;


	if (tty->index == MXSER_PORTS)
		return -ENOIOCTLCMD;
	if (tty->flags & (1 << TTY_IO_ERROR))
		return -EIO;

	control = info->MCR;

	spin_lock_irqsave(&info->slock, flags);
	status = inb(info->base + UART_MSR);
	if (status & UART_MSR_ANY_DELTA)
		mxser_check_modem_status(info, status);
	spin_unlock_irqrestore(&info->slock, flags);
	return ((control & UART_MCR_RTS) ? TIOCM_RTS : 0) |
		    ((control & UART_MCR_DTR) ? TIOCM_DTR : 0) |
		    ((status & UART_MSR_DCD) ? TIOCM_CAR : 0) |
		    ((status & UART_MSR_RI) ? TIOCM_RNG : 0) |
		    ((status & UART_MSR_DSR) ? TIOCM_DSR : 0) |
		    ((status & UART_MSR_CTS) ? TIOCM_CTS : 0);
}

static int mxser_tiocmset(struct tty_struct *tty, struct file *file, unsigned int set, unsigned int clear)
{
	struct mxser_struct *info = tty->driver_data;
	unsigned long flags;


	if (tty->index == MXSER_PORTS)
		return -ENOIOCTLCMD;
	if (tty->flags & (1 << TTY_IO_ERROR))
		return -EIO;

	spin_lock_irqsave(&info->slock, flags);

	if (set & TIOCM_RTS)
		info->MCR |= UART_MCR_RTS;
	if (set & TIOCM_DTR)
		info->MCR |= UART_MCR_DTR;

	if (clear & TIOCM_RTS)
		info->MCR &= ~UART_MCR_RTS;
	if (clear & TIOCM_DTR)
		info->MCR &= ~UART_MCR_DTR;

	outb(info->MCR, info->base + UART_MCR);
	spin_unlock_irqrestore(&info->slock, flags);
	return 0;
}


static int mxser_read_register(int, unsigned short *);
static int mxser_program_mode(int);
static void mxser_normal_mode(int);

static int mxser_get_ISA_conf(int cap, struct mxser_hwconf *hwconf)
{
	int id, i, bits;
	unsigned short regs[16], irq;
	unsigned char scratch, scratch2;

	hwconf->IsMoxaMustChipFlag = MOXA_OTHER_UART;

	id = mxser_read_register(cap, regs);
	if (id == C168_ASIC_ID) {
		hwconf->board_type = MXSER_BOARD_C168_ISA;
		hwconf->ports = 8;
	} else if (id == C104_ASIC_ID) {
		hwconf->board_type = MXSER_BOARD_C104_ISA;
		hwconf->ports = 4;
	} else if (id == C102_ASIC_ID) {
		hwconf->board_type = MXSER_BOARD_C102_ISA;
		hwconf->ports = 2;
	} else if (id == CI132_ASIC_ID) {
		hwconf->board_type = MXSER_BOARD_CI132;
		hwconf->ports = 2;
	} else if (id == CI134_ASIC_ID) {
		hwconf->board_type = MXSER_BOARD_CI134;
		hwconf->ports = 4;
	} else if (id == CI104J_ASIC_ID) {
		hwconf->board_type = MXSER_BOARD_CI104J;
		hwconf->ports = 4;
	} else
		return 0;

	irq = 0;
	if (hwconf->ports == 2) {
		irq = regs[9] & 0xF000;
		irq = irq | (irq >> 4);
		if (irq != (regs[9] & 0xFF00))
			return MXSER_ERR_IRQ_CONFLIT;
	} else if (hwconf->ports == 4) {
		irq = regs[9] & 0xF000;
		irq = irq | (irq >> 4);
		irq = irq | (irq >> 8);
		if (irq != regs[9])
			return MXSER_ERR_IRQ_CONFLIT;
	} else if (hwconf->ports == 8) {
		irq = regs[9] & 0xF000;
		irq = irq | (irq >> 4);
		irq = irq | (irq >> 8);
		if ((irq != regs[9]) || (irq != regs[10]))
			return MXSER_ERR_IRQ_CONFLIT;
	}

	if (!irq)
		return MXSER_ERR_IRQ;
	hwconf->irq = ((int)(irq & 0xF000) >> 12);
	for (i = 0; i < 8; i++)
		hwconf->ioaddr[i] = (int) regs[i + 1] & 0xFFF8;
	if ((regs[12] & 0x80) == 0)
		return MXSER_ERR_VECTOR;
	hwconf->vector = (int)regs[11];	/* interrupt vector */
	if (id == 1)
		hwconf->vector_mask = 0x00FF;
	else
		hwconf->vector_mask = 0x000F;
	for (i = 7, bits = 0x0100; i >= 0; i--, bits <<= 1) {
		if (regs[12] & bits) {
			hwconf->baud_base[i] = 921600;
			hwconf->MaxCanSetBaudRate[i] = 921600;	/* add by Victor Yu. 09-04-2002 */
		} else {
			hwconf->baud_base[i] = 115200;
			hwconf->MaxCanSetBaudRate[i] = 115200;	/* add by Victor Yu. 09-04-2002 */
		}
	}
	scratch2 = inb(cap + UART_LCR) & (~UART_LCR_DLAB);
	outb(scratch2 | UART_LCR_DLAB, cap + UART_LCR);
	outb(0, cap + UART_EFR);	/* EFR is the same as FCR */
	outb(scratch2, cap + UART_LCR);
	outb(UART_FCR_ENABLE_FIFO, cap + UART_FCR);
	scratch = inb(cap + UART_IIR);

	if (scratch & 0xC0)
		hwconf->uart_type = PORT_16550A;
	else
		hwconf->uart_type = PORT_16450;
	if (id == 1)
		hwconf->ports = 8;
	else
		hwconf->ports = 4;
	request_region(hwconf->ioaddr[0], 8 * hwconf->ports, "mxser(IO)");
	request_region(hwconf->vector, 1, "mxser(vector)");
	return hwconf->ports;
}

#define CHIP_SK 	0x01	/* Serial Data Clock  in Eprom */
#define CHIP_DO 	0x02	/* Serial Data Output in Eprom */
#define CHIP_CS 	0x04	/* Serial Chip Select in Eprom */
#define CHIP_DI 	0x08	/* Serial Data Input  in Eprom */
#define EN_CCMD 	0x000	/* Chip's command register     */
#define EN0_RSARLO	0x008	/* Remote start address reg 0  */
#define EN0_RSARHI	0x009	/* Remote start address reg 1  */
#define EN0_RCNTLO	0x00A	/* Remote byte count reg WR    */
#define EN0_RCNTHI	0x00B	/* Remote byte count reg WR    */
#define EN0_DCFG	0x00E	/* Data configuration reg WR   */
#define EN0_PORT	0x010	/* Rcv missed frame error counter RD */
#define ENC_PAGE0	0x000	/* Select page 0 of chip registers   */
#define ENC_PAGE3	0x0C0	/* Select page 3 of chip registers   */
static int mxser_read_register(int port, unsigned short *regs)
{
	int i, k, value, id;
	unsigned int j;

	id = mxser_program_mode(port);
	if (id < 0)
		return id;
	for (i = 0; i < 14; i++) {
		k = (i & 0x3F) | 0x180;
		for (j = 0x100; j > 0; j >>= 1) {
			outb(CHIP_CS, port);
			if (k & j) {
				outb(CHIP_CS | CHIP_DO, port);
				outb(CHIP_CS | CHIP_DO | CHIP_SK, port);	/* A? bit of read */
			} else {
				outb(CHIP_CS, port);
				outb(CHIP_CS | CHIP_SK, port);	/* A? bit of read */
			}
		}
		(void)inb(port);
		value = 0;
		for (k = 0, j = 0x8000; k < 16; k++, j >>= 1) {
			outb(CHIP_CS, port);
			outb(CHIP_CS | CHIP_SK, port);
			if (inb(port) & CHIP_DI)
				value |= j;
		}
		regs[i] = value;
		outb(0, port);
	}
	mxser_normal_mode(port);
	return id;
}

static int mxser_program_mode(int port)
{
	int id, i, j, n;
	/* unsigned long flags; */

	spin_lock(&gm_lock);
	outb(0, port);
	outb(0, port);
	outb(0, port);
	(void)inb(port);
	(void)inb(port);
	outb(0, port);
	(void)inb(port);
	/* restore_flags(flags); */
	spin_unlock(&gm_lock);

	id = inb(port + 1) & 0x1F;
	if ((id != C168_ASIC_ID) &&
			(id != C104_ASIC_ID) &&
			(id != C102_ASIC_ID) &&
			(id != CI132_ASIC_ID) &&
			(id != CI134_ASIC_ID) &&
			(id != CI104J_ASIC_ID))
		return -1;
	for (i = 0, j = 0; i < 4; i++) {
		n = inb(port + 2);
		if (n == 'M') {
			j = 1;
		} else if ((j == 1) && (n == 1)) {
			j = 2;
			break;
		} else
			j = 0;
	}
	if (j != 2)
		id = -2;
	return id;
}

static void mxser_normal_mode(int port)
{
	int i, n;

	outb(0xA5, port + 1);
	outb(0x80, port + 3);
	outb(12, port + 0);	/* 9600 bps */
	outb(0, port + 1);
	outb(0x03, port + 3);	/* 8 data bits */
	outb(0x13, port + 4);	/* loop back mode */
	for (i = 0; i < 16; i++) {
		n = inb(port + 5);
		if ((n & 0x61) == 0x60)
			break;
		if ((n & 1) == 1)
			(void)inb(port);
	}
	outb(0x00, port + 4);
}

module_init(mxser_module_init);
module_exit(mxser_module_exit);
