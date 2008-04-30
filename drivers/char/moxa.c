/*****************************************************************************/
/*
 *           moxa.c  -- MOXA Intellio family multiport serial driver.
 *
 *      Copyright (C) 1999-2000  Moxa Technologies (support@moxa.com.tw).
 *
 *      This code is loosely based on the Linux serial driver, written by
 *      Linus Torvalds, Theodore T'so and others.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

/*
 *    MOXA Intellio Series Driver
 *      for             : LINUX
 *      date            : 1999/1/7
 *      version         : 5.1
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/serial.h>
#include <linux/tty_driver.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/completion.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#define MOXA_VERSION		"5.1k"

#define MOXAMAJOR		172
#define MOXACUMAJOR		173

#define MAX_BOARDS		4	/* Don't change this value */
#define MAX_PORTS_PER_BOARD	32	/* Don't change this value */
#define MAX_PORTS		(MAX_BOARDS * MAX_PORTS_PER_BOARD)

/*
 *    Define the Moxa PCI vendor and device IDs.
 */
#define MOXA_BUS_TYPE_ISA	0
#define MOXA_BUS_TYPE_PCI	1

enum {
	MOXA_BOARD_C218_PCI = 1,
	MOXA_BOARD_C218_ISA,
	MOXA_BOARD_C320_PCI,
	MOXA_BOARD_C320_ISA,
	MOXA_BOARD_CP204J,
};

static char *moxa_brdname[] =
{
	"C218 Turbo PCI series",
	"C218 Turbo ISA series",
	"C320 Turbo PCI series",
	"C320 Turbo ISA series",
	"CP-204J series",
};

#ifdef CONFIG_PCI
static struct pci_device_id moxa_pcibrds[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_C218),
		.driver_data = MOXA_BOARD_C218_PCI },
	{ PCI_DEVICE(PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_C320),
		.driver_data = MOXA_BOARD_C320_PCI },
	{ PCI_DEVICE(PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_MOXA_CP204J),
		.driver_data = MOXA_BOARD_CP204J },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, moxa_pcibrds);
#endif /* CONFIG_PCI */

static struct moxa_board_conf {
	int boardType;
	int numPorts;
	int busType;

	int loadstat;

	void __iomem *basemem;
	void __iomem *intNdx;
	void __iomem *intPend;
	void __iomem *intTable;
} moxa_boards[MAX_BOARDS];

struct mxser_mstatus {
	tcflag_t cflag;
	int cts;
	int dsr;
	int ri;
	int dcd;
};

struct moxaq_str {
	int inq;
	int outq;
};

struct moxa_port {
	int type;
	int port;
	int close_delay;
	unsigned short closing_wait;
	int count;
	int blocked_open;
	long event; /* long req'd for set_bit --RR */
	int asyncflags;
	unsigned long statusflags;
	struct tty_struct *tty;
	int cflag;
	wait_queue_head_t open_wait;
	struct completion close_wait;

	struct timer_list emptyTimer;

	char chkPort;
	char lineCtrl;
	void __iomem *tableAddr;
	long curBaud;
	char DCDState;
	char lowChkFlag;

	ushort breakCnt;
};

/* statusflags */
#define TXSTOPPED	0x1
#define LOWWAIT 	0x2
#define EMPTYWAIT	0x4
#define THROTTLE	0x8

#define SERIAL_DO_RESTART

#define WAKEUP_CHARS		256

static int ttymajor = MOXAMAJOR;
/* Variables for insmod */
#ifdef MODULE
static unsigned long baseaddr[MAX_BOARDS];
static unsigned int type[MAX_BOARDS];
static unsigned int numports[MAX_BOARDS];
#endif

MODULE_AUTHOR("William Chen");
MODULE_DESCRIPTION("MOXA Intellio Family Multiport Board Device Driver");
MODULE_LICENSE("GPL");
#ifdef MODULE
module_param_array(type, uint, NULL, 0);
MODULE_PARM_DESC(type, "card type: C218=2, C320=4");
module_param_array(baseaddr, ulong, NULL, 0);
MODULE_PARM_DESC(baseaddr, "base address");
module_param_array(numports, uint, NULL, 0);
MODULE_PARM_DESC(numports, "numports (ignored for C218)");
#endif
module_param(ttymajor, int, 0);

/*
 * static functions:
 */
static int moxa_open(struct tty_struct *, struct file *);
static void moxa_close(struct tty_struct *, struct file *);
static int moxa_write(struct tty_struct *, const unsigned char *, int);
static int moxa_write_room(struct tty_struct *);
static void moxa_flush_buffer(struct tty_struct *);
static int moxa_chars_in_buffer(struct tty_struct *);
static void moxa_flush_chars(struct tty_struct *);
static void moxa_put_char(struct tty_struct *, unsigned char);
static int moxa_ioctl(struct tty_struct *, struct file *, unsigned int, unsigned long);
static void moxa_throttle(struct tty_struct *);
static void moxa_unthrottle(struct tty_struct *);
static void moxa_set_termios(struct tty_struct *, struct ktermios *);
static void moxa_stop(struct tty_struct *);
static void moxa_start(struct tty_struct *);
static void moxa_hangup(struct tty_struct *);
static int moxa_tiocmget(struct tty_struct *tty, struct file *file);
static int moxa_tiocmset(struct tty_struct *tty, struct file *file,
			 unsigned int set, unsigned int clear);
static void moxa_poll(unsigned long);
static void moxa_set_tty_param(struct tty_struct *, struct ktermios *);
static int moxa_block_till_ready(struct tty_struct *, struct file *,
			    struct moxa_port *);
static void moxa_setup_empty_event(struct tty_struct *);
static void moxa_check_xmit_empty(unsigned long);
static void moxa_shut_down(struct moxa_port *);
static void moxa_receive_data(struct moxa_port *);
/*
 * moxa board interface functions:
 */
static void MoxaDriverInit(void);
static int MoxaDriverIoctl(unsigned int, unsigned long, int);
static int MoxaDriverPoll(void);
static int MoxaPortsOfCard(int);
static int MoxaPortIsValid(int);
static void MoxaPortEnable(int);
static void MoxaPortDisable(int);
static long MoxaPortGetMaxBaud(int);
static long MoxaPortSetBaud(int, long);
static int MoxaPortSetTermio(int, struct ktermios *, speed_t);
static int MoxaPortGetLineOut(int, int *, int *);
static void MoxaPortLineCtrl(int, int, int);
static void MoxaPortFlowCtrl(int, int, int, int, int, int);
static int MoxaPortLineStatus(int);
static int MoxaPortDCDChange(int);
static int MoxaPortDCDON(int);
static void MoxaPortFlushData(int, int);
static int MoxaPortWriteData(int, unsigned char *, int);
static int MoxaPortReadData(int, struct tty_struct *tty);
static int MoxaPortTxQueue(int);
static int MoxaPortRxQueue(int);
static int MoxaPortTxFree(int);
static void MoxaPortTxDisable(int);
static void MoxaPortTxEnable(int);
static int MoxaPortResetBrkCnt(int);
static void MoxaPortSendBreak(int, int);
static int moxa_get_serial_info(struct moxa_port *, struct serial_struct __user *);
static int moxa_set_serial_info(struct moxa_port *, struct serial_struct __user *);
static void MoxaSetFifo(int port, int enable);

static const struct tty_operations moxa_ops = {
	.open = moxa_open,
	.close = moxa_close,
	.write = moxa_write,
	.write_room = moxa_write_room,
	.flush_buffer = moxa_flush_buffer,
	.chars_in_buffer = moxa_chars_in_buffer,
	.flush_chars = moxa_flush_chars,
	.put_char = moxa_put_char,
	.ioctl = moxa_ioctl,
	.throttle = moxa_throttle,
	.unthrottle = moxa_unthrottle,
	.set_termios = moxa_set_termios,
	.stop = moxa_stop,
	.start = moxa_start,
	.hangup = moxa_hangup,
	.tiocmget = moxa_tiocmget,
	.tiocmset = moxa_tiocmset,
};

static struct tty_driver *moxaDriver;
static struct moxa_port moxa_ports[MAX_PORTS];
static DEFINE_TIMER(moxaTimer, moxa_poll, 0, 0);
static DEFINE_SPINLOCK(moxa_lock);

#ifdef CONFIG_PCI
static int __devinit moxa_pci_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	struct moxa_board_conf *board;
	unsigned int i;
	int board_type = ent->driver_data;
	int retval;

	retval = pci_enable_device(pdev);
	if (retval) {
		dev_err(&pdev->dev, "can't enable pci device\n");
		goto err;
	}

	for (i = 0; i < MAX_BOARDS; i++)
		if (moxa_boards[i].basemem == NULL)
			break;

	retval = -ENODEV;
	if (i >= MAX_BOARDS) {
		dev_warn(&pdev->dev, "more than %u MOXA Intellio family boards "
				"found. Board is ignored.\n", MAX_BOARDS);
		goto err;
	}

	board = &moxa_boards[i];
	board->basemem = pci_iomap(pdev, 2, 0x4000);
	if (board->basemem == NULL) {
		dev_err(&pdev->dev, "can't remap io space 2\n");
		goto err;
	}

	board->boardType = board_type;
	switch (board_type) {
	case MOXA_BOARD_C218_ISA:
	case MOXA_BOARD_C218_PCI:
		board->numPorts = 8;
		break;

	case MOXA_BOARD_CP204J:
		board->numPorts = 4;
		break;
	default:
		board->numPorts = 0;
		break;
	}
	board->busType = MOXA_BUS_TYPE_PCI;

	pci_set_drvdata(pdev, board);

	return (0);
err:
	return retval;
}

static void __devexit moxa_pci_remove(struct pci_dev *pdev)
{
	struct moxa_board_conf *brd = pci_get_drvdata(pdev);

	pci_iounmap(pdev, brd->basemem);
	brd->basemem = NULL;
}

static struct pci_driver moxa_pci_driver = {
	.name = "moxa",
	.id_table = moxa_pcibrds,
	.probe = moxa_pci_probe,
	.remove = __devexit_p(moxa_pci_remove)
};
#endif /* CONFIG_PCI */

static int __init moxa_init(void)
{
	struct moxa_port *ch;
	unsigned int i, isabrds = 0;
	int retval = 0;

	printk(KERN_INFO "MOXA Intellio family driver version %s\n",
			MOXA_VERSION);
	moxaDriver = alloc_tty_driver(MAX_PORTS + 1);
	if (!moxaDriver)
		return -ENOMEM;

	moxaDriver->owner = THIS_MODULE;
	moxaDriver->name = "ttyMX";
	moxaDriver->major = ttymajor;
	moxaDriver->minor_start = 0;
	moxaDriver->type = TTY_DRIVER_TYPE_SERIAL;
	moxaDriver->subtype = SERIAL_TYPE_NORMAL;
	moxaDriver->init_termios = tty_std_termios;
	moxaDriver->init_termios.c_cflag = B9600 | CS8 | CREAD | CLOCAL | HUPCL;
	moxaDriver->init_termios.c_ispeed = 9600;
	moxaDriver->init_termios.c_ospeed = 9600;
	moxaDriver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(moxaDriver, &moxa_ops);

	for (i = 0, ch = moxa_ports; i < MAX_PORTS; i++, ch++) {
		ch->type = PORT_16550A;
		ch->port = i;
		ch->close_delay = 5 * HZ / 10;
		ch->closing_wait = 30 * HZ;
		ch->cflag = B9600 | CS8 | CREAD | CLOCAL | HUPCL;
		init_waitqueue_head(&ch->open_wait);
		init_completion(&ch->close_wait);

		setup_timer(&ch->emptyTimer, moxa_check_xmit_empty,
				(unsigned long)ch);
	}

	pr_debug("Moxa tty devices major number = %d\n", ttymajor);

	if (tty_register_driver(moxaDriver)) {
		printk(KERN_ERR "Couldn't install MOXA Smartio family driver !\n");
		put_tty_driver(moxaDriver);
		return -1;
	}

	mod_timer(&moxaTimer, jiffies + HZ / 50);

	/* Find the boards defined from module args. */
#ifdef MODULE
	{
	struct moxa_board_conf *brd = moxa_boards;
	for (i = 0; i < MAX_BOARDS; i++) {
		if (!baseaddr[i])
			break;
		if (type[i] == MOXA_BOARD_C218_ISA ||
				type[i] == MOXA_BOARD_C320_ISA) {
			pr_debug("Moxa board %2d: %s board(baseAddr=%lx)\n",
					isabrds + 1, moxa_brdname[type[i] - 1],
					baseaddr[i]);
			brd->boardType = type[i];
			brd->numPorts = type[i] == MOXA_BOARD_C218_ISA ? 8 :
					numports[i];
			brd->busType = MOXA_BUS_TYPE_ISA;
			brd->basemem = ioremap(baseaddr[i], 0x4000);
			if (!brd->basemem) {
				printk(KERN_ERR "moxa: can't remap %lx\n",
						baseaddr[i]);
				continue;
			}

			brd++;
			isabrds++;
		}
	}
	}
#endif

#ifdef CONFIG_PCI
	retval = pci_register_driver(&moxa_pci_driver);
	if (retval) {
		printk(KERN_ERR "Can't register moxa pci driver!\n");
		if (isabrds)
			retval = 0;
	}
#endif

	return retval;
}

static void __exit moxa_exit(void)
{
	int i;

	del_timer_sync(&moxaTimer);

	for (i = 0; i < MAX_PORTS; i++)
		del_timer_sync(&moxa_ports[i].emptyTimer);

	if (tty_unregister_driver(moxaDriver))
		printk(KERN_ERR "Couldn't unregister MOXA Intellio family "
				"serial driver\n");
	put_tty_driver(moxaDriver);

#ifdef CONFIG_PCI
	pci_unregister_driver(&moxa_pci_driver);
#endif

	for (i = 0; i < MAX_BOARDS; i++)
		if (moxa_boards[i].basemem)
			iounmap(moxa_boards[i].basemem);
}

module_init(moxa_init);
module_exit(moxa_exit);

static int moxa_open(struct tty_struct *tty, struct file *filp)
{
	struct moxa_port *ch;
	int port;
	int retval;

	port = tty->index;
	if (port == MAX_PORTS) {
		return (0);
	}
	if (!MoxaPortIsValid(port)) {
		tty->driver_data = NULL;
		return (-ENODEV);
	}

	ch = &moxa_ports[port];
	ch->count++;
	tty->driver_data = ch;
	ch->tty = tty;
	if (!(ch->asyncflags & ASYNC_INITIALIZED)) {
		ch->statusflags = 0;
		moxa_set_tty_param(tty, tty->termios);
		MoxaPortLineCtrl(ch->port, 1, 1);
		MoxaPortEnable(ch->port);
		ch->asyncflags |= ASYNC_INITIALIZED;
	}
	retval = moxa_block_till_ready(tty, filp, ch);

	moxa_unthrottle(tty);

	if (ch->type == PORT_16550A) {
		MoxaSetFifo(ch->port, 1);
	} else {
		MoxaSetFifo(ch->port, 0);
	}

	return (retval);
}

static void moxa_close(struct tty_struct *tty, struct file *filp)
{
	struct moxa_port *ch;
	int port;

	port = tty->index;
	if (port == MAX_PORTS) {
		return;
	}
	if (!MoxaPortIsValid(port)) {
		pr_debug("Invalid portno in moxa_close\n");
		tty->driver_data = NULL;
		return;
	}
	if (tty->driver_data == NULL) {
		return;
	}
	if (tty_hung_up_p(filp)) {
		return;
	}
	ch = (struct moxa_port *) tty->driver_data;

	if ((tty->count == 1) && (ch->count != 1)) {
		printk(KERN_WARNING "moxa_close: bad serial port count; "
			"tty->count is 1, ch->count is %d\n", ch->count);
		ch->count = 1;
	}
	if (--ch->count < 0) {
		printk(KERN_WARNING "moxa_close: bad serial port count, "
			"device=%s\n", tty->name);
		ch->count = 0;
	}
	if (ch->count) {
		return;
	}
	ch->asyncflags |= ASYNC_CLOSING;

	ch->cflag = tty->termios->c_cflag;
	if (ch->asyncflags & ASYNC_INITIALIZED) {
		moxa_setup_empty_event(tty);
		tty_wait_until_sent(tty, 30 * HZ);	/* 30 seconds timeout */
		del_timer_sync(&moxa_ports[ch->port].emptyTimer);
	}
	moxa_shut_down(ch);
	MoxaPortFlushData(port, 2);

	if (tty->driver->flush_buffer)
		tty->driver->flush_buffer(tty);
	tty_ldisc_flush(tty);
			
	tty->closing = 0;
	ch->event = 0;
	ch->tty = NULL;
	if (ch->blocked_open) {
		if (ch->close_delay) {
			msleep_interruptible(jiffies_to_msecs(ch->close_delay));
		}
		wake_up_interruptible(&ch->open_wait);
	}
	ch->asyncflags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CLOSING);
	complete_all(&ch->close_wait);
}

static int moxa_write(struct tty_struct *tty,
		      const unsigned char *buf, int count)
{
	struct moxa_port *ch;
	int len, port;
	unsigned long flags;

	ch = (struct moxa_port *) tty->driver_data;
	if (ch == NULL)
		return (0);
	port = ch->port;

	spin_lock_irqsave(&moxa_lock, flags);
	len = MoxaPortWriteData(port, (unsigned char *) buf, count);
	spin_unlock_irqrestore(&moxa_lock, flags);

	/*********************************************
	if ( !(ch->statusflags & LOWWAIT) &&
	     ((len != count) || (MoxaPortTxFree(port) <= 100)) )
	************************************************/
	ch->statusflags |= LOWWAIT;
	return (len);
}

static int moxa_write_room(struct tty_struct *tty)
{
	struct moxa_port *ch;

	if (tty->stopped)
		return (0);
	ch = (struct moxa_port *) tty->driver_data;
	if (ch == NULL)
		return (0);
	return (MoxaPortTxFree(ch->port));
}

static void moxa_flush_buffer(struct tty_struct *tty)
{
	struct moxa_port *ch = (struct moxa_port *) tty->driver_data;

	if (ch == NULL)
		return;
	MoxaPortFlushData(ch->port, 1);
	tty_wakeup(tty);
}

static int moxa_chars_in_buffer(struct tty_struct *tty)
{
	int chars;
	struct moxa_port *ch = (struct moxa_port *) tty->driver_data;

	/*
	 * Sigh...I have to check if driver_data is NULL here, because
	 * if an open() fails, the TTY subsystem eventually calls
	 * tty_wait_until_sent(), which calls the driver's chars_in_buffer()
	 * routine.  And since the open() failed, we return 0 here.  TDJ
	 */
	if (ch == NULL)
		return (0);
	chars = MoxaPortTxQueue(ch->port);
	if (chars) {
		/*
		 * Make it possible to wakeup anything waiting for output
		 * in tty_ioctl.c, etc.
		 */
		if (!(ch->statusflags & EMPTYWAIT))
			moxa_setup_empty_event(tty);
	}
	return (chars);
}

static void moxa_flush_chars(struct tty_struct *tty)
{
	/*
	 * Don't think I need this, because this is called to empty the TX
	 * buffer for the 16450, 16550, etc.
	 */
}

static void moxa_put_char(struct tty_struct *tty, unsigned char c)
{
	struct moxa_port *ch;
	int port;
	unsigned long flags;

	ch = (struct moxa_port *) tty->driver_data;
	if (ch == NULL)
		return;
	port = ch->port;
	spin_lock_irqsave(&moxa_lock, flags);
	MoxaPortWriteData(port, &c, 1);
	spin_unlock_irqrestore(&moxa_lock, flags);
	/************************************************
	if ( !(ch->statusflags & LOWWAIT) && (MoxaPortTxFree(port) <= 100) )
	*************************************************/
	ch->statusflags |= LOWWAIT;
}

static int moxa_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct moxa_port *ch = (struct moxa_port *) tty->driver_data;
	int port;
	int flag = 0, dtr, rts;

	port = tty->index;
	if ((port != MAX_PORTS) && (!ch))
		return (-EINVAL);

	MoxaPortGetLineOut(ch->port, &dtr, &rts);
	if (dtr)
		flag |= TIOCM_DTR;
	if (rts)
		flag |= TIOCM_RTS;
	dtr = MoxaPortLineStatus(ch->port);
	if (dtr & 1)
		flag |= TIOCM_CTS;
	if (dtr & 2)
		flag |= TIOCM_DSR;
	if (dtr & 4)
		flag |= TIOCM_CD;
	return flag;
}

static int moxa_tiocmset(struct tty_struct *tty, struct file *file,
			 unsigned int set, unsigned int clear)
{
	struct moxa_port *ch = (struct moxa_port *) tty->driver_data;
	int port;
	int dtr, rts;

	port = tty->index;
	if ((port != MAX_PORTS) && (!ch))
		return (-EINVAL);

	MoxaPortGetLineOut(ch->port, &dtr, &rts);
	if (set & TIOCM_RTS)
		rts = 1;
	if (set & TIOCM_DTR)
		dtr = 1;
	if (clear & TIOCM_RTS)
		rts = 0;
	if (clear & TIOCM_DTR)
		dtr = 0;
	MoxaPortLineCtrl(ch->port, dtr, rts);
	return 0;
}

static int moxa_ioctl(struct tty_struct *tty, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	struct moxa_port *ch = (struct moxa_port *) tty->driver_data;
	register int port;
	void __user *argp = (void __user *)arg;
	int retval;

	port = tty->index;
	if ((port != MAX_PORTS) && (!ch))
		return (-EINVAL);

	switch (cmd) {
	case TCSBRK:		/* SVID version: non-zero arg --> no break */
		retval = tty_check_change(tty);
		if (retval)
			return (retval);
		moxa_setup_empty_event(tty);
		tty_wait_until_sent(tty, 0);
		if (!arg)
			MoxaPortSendBreak(ch->port, 0);
		return (0);
	case TCSBRKP:		/* support for POSIX tcsendbreak() */
		retval = tty_check_change(tty);
		if (retval)
			return (retval);
		moxa_setup_empty_event(tty);
		tty_wait_until_sent(tty, 0);
		MoxaPortSendBreak(ch->port, arg);
		return (0);
	case TIOCGSOFTCAR:
		return put_user(C_CLOCAL(tty) ? 1 : 0, (unsigned long __user *) argp);
	case TIOCSSOFTCAR:
		if(get_user(retval, (unsigned long __user *) argp))
			return -EFAULT;
		arg = retval;
		tty->termios->c_cflag = ((tty->termios->c_cflag & ~CLOCAL) |
					 (arg ? CLOCAL : 0));
		if (C_CLOCAL(tty))
			ch->asyncflags &= ~ASYNC_CHECK_CD;
		else
			ch->asyncflags |= ASYNC_CHECK_CD;
		return (0);
	case TIOCGSERIAL:
		return moxa_get_serial_info(ch, argp);

	case TIOCSSERIAL:
		return moxa_set_serial_info(ch, argp);
	default:
		retval = MoxaDriverIoctl(cmd, arg, port);
	}
	return (retval);
}

static void moxa_throttle(struct tty_struct *tty)
{
	struct moxa_port *ch = (struct moxa_port *) tty->driver_data;

	ch->statusflags |= THROTTLE;
}

static void moxa_unthrottle(struct tty_struct *tty)
{
	struct moxa_port *ch = (struct moxa_port *) tty->driver_data;

	ch->statusflags &= ~THROTTLE;
}

static void moxa_set_termios(struct tty_struct *tty,
			     struct ktermios *old_termios)
{
	struct moxa_port *ch = (struct moxa_port *) tty->driver_data;

	if (ch == NULL)
		return;
	moxa_set_tty_param(tty, old_termios);
	if (!(old_termios->c_cflag & CLOCAL) &&
	    (tty->termios->c_cflag & CLOCAL))
		wake_up_interruptible(&ch->open_wait);
}

static void moxa_stop(struct tty_struct *tty)
{
	struct moxa_port *ch = (struct moxa_port *) tty->driver_data;

	if (ch == NULL)
		return;
	MoxaPortTxDisable(ch->port);
	ch->statusflags |= TXSTOPPED;
}


static void moxa_start(struct tty_struct *tty)
{
	struct moxa_port *ch = (struct moxa_port *) tty->driver_data;

	if (ch == NULL)
		return;

	if (!(ch->statusflags & TXSTOPPED))
		return;

	MoxaPortTxEnable(ch->port);
	ch->statusflags &= ~TXSTOPPED;
}

static void moxa_hangup(struct tty_struct *tty)
{
	struct moxa_port *ch = (struct moxa_port *) tty->driver_data;

	moxa_flush_buffer(tty);
	moxa_shut_down(ch);
	ch->event = 0;
	ch->count = 0;
	ch->asyncflags &= ~ASYNC_NORMAL_ACTIVE;
	ch->tty = NULL;
	wake_up_interruptible(&ch->open_wait);
}

static void moxa_poll(unsigned long ignored)
{
	register int card;
	struct moxa_port *ch;
	struct tty_struct *tp;
	int i, ports;

	del_timer(&moxaTimer);

	if (MoxaDriverPoll() < 0) {
		mod_timer(&moxaTimer, jiffies + HZ / 50);
		return;
	}
	for (card = 0; card < MAX_BOARDS; card++) {
		if ((ports = MoxaPortsOfCard(card)) <= 0)
			continue;
		ch = &moxa_ports[card * MAX_PORTS_PER_BOARD];
		for (i = 0; i < ports; i++, ch++) {
			if ((ch->asyncflags & ASYNC_INITIALIZED) == 0)
				continue;
			if (!(ch->statusflags & THROTTLE) &&
			    (MoxaPortRxQueue(ch->port) > 0))
				moxa_receive_data(ch);
			if ((tp = ch->tty) == 0)
				continue;
			if (ch->statusflags & LOWWAIT) {
				if (MoxaPortTxQueue(ch->port) <= WAKEUP_CHARS) {
					if (!tp->stopped) {
						ch->statusflags &= ~LOWWAIT;
						tty_wakeup(tp);
					}
				}
			}
			if (!I_IGNBRK(tp) && (MoxaPortResetBrkCnt(ch->port) > 0)) {
				tty_insert_flip_char(tp, 0, TTY_BREAK);
				tty_schedule_flip(tp);
			}
			if (MoxaPortDCDChange(ch->port)) {
				if (ch->asyncflags & ASYNC_CHECK_CD) {
					if (MoxaPortDCDON(ch->port))
						wake_up_interruptible(&ch->open_wait);
					else {
						tty_hangup(tp);
						wake_up_interruptible(&ch->open_wait);
						ch->asyncflags &= ~ASYNC_NORMAL_ACTIVE;
					}
				}
			}
		}
	}

	mod_timer(&moxaTimer, jiffies + HZ / 50);
}

/******************************************************************************/

static void moxa_set_tty_param(struct tty_struct *tty, struct ktermios *old_termios)
{
	register struct ktermios *ts;
	struct moxa_port *ch;
	int rts, cts, txflow, rxflow, xany, baud;

	ch = (struct moxa_port *) tty->driver_data;
	ts = tty->termios;
	if (ts->c_cflag & CLOCAL)
		ch->asyncflags &= ~ASYNC_CHECK_CD;
	else
		ch->asyncflags |= ASYNC_CHECK_CD;
	rts = cts = txflow = rxflow = xany = 0;
	if (ts->c_cflag & CRTSCTS)
		rts = cts = 1;
	if (ts->c_iflag & IXON)
		txflow = 1;
	if (ts->c_iflag & IXOFF)
		rxflow = 1;
	if (ts->c_iflag & IXANY)
		xany = 1;

	/* Clear the features we don't support */
	ts->c_cflag &= ~CMSPAR;
	MoxaPortFlowCtrl(ch->port, rts, cts, txflow, rxflow, xany);
	baud = MoxaPortSetTermio(ch->port, ts, tty_get_baud_rate(tty));
	if (baud == -1)
		baud = tty_termios_baud_rate(old_termios);
	/* Not put the baud rate into the termios data */
	tty_encode_baud_rate(tty, baud, baud);
}

static int moxa_block_till_ready(struct tty_struct *tty, struct file *filp,
			    struct moxa_port *ch)
{
	DECLARE_WAITQUEUE(wait,current);
	unsigned long flags;
	int retval;
	int do_clocal = C_CLOCAL(tty);

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) || (ch->asyncflags & ASYNC_CLOSING)) {
		if (ch->asyncflags & ASYNC_CLOSING)
			wait_for_completion_interruptible(&ch->close_wait);
#ifdef SERIAL_DO_RESTART
		if (ch->asyncflags & ASYNC_HUP_NOTIFY)
			return (-EAGAIN);
		else
			return (-ERESTARTSYS);
#else
		return (-EAGAIN);
#endif
	}
	/*
	 * If non-blocking mode is set, then make the check up front
	 * and then exit.
	 */
	if (filp->f_flags & O_NONBLOCK) {
		ch->asyncflags |= ASYNC_NORMAL_ACTIVE;
		return (0);
	}
	/*
	 * Block waiting for the carrier detect and the line to become free
	 */
	retval = 0;
	add_wait_queue(&ch->open_wait, &wait);
	pr_debug("block_til_ready before block: ttys%d, count = %d\n",
		ch->port, ch->count);
	spin_lock_irqsave(&moxa_lock, flags);
	if (!tty_hung_up_p(filp))
		ch->count--;
	ch->blocked_open++;
	spin_unlock_irqrestore(&moxa_lock, flags);

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) ||
		    !(ch->asyncflags & ASYNC_INITIALIZED)) {
#ifdef SERIAL_DO_RESTART
			if (ch->asyncflags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(ch->asyncflags & ASYNC_CLOSING) && (do_clocal ||
						MoxaPortDCDON(ch->port)))
			break;

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&ch->open_wait, &wait);

	spin_lock_irqsave(&moxa_lock, flags);
	if (!tty_hung_up_p(filp))
		ch->count++;
	ch->blocked_open--;
	spin_unlock_irqrestore(&moxa_lock, flags);
	pr_debug("block_til_ready after blocking: ttys%d, count = %d\n",
		ch->port, ch->count);
	if (retval)
		return (retval);
	/* FIXME: review to see if we need to use set_bit on these */
	ch->asyncflags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}

static void moxa_setup_empty_event(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&moxa_lock, flags);
	ch->statusflags |= EMPTYWAIT;
	mod_timer(&moxa_ports[ch->port].emptyTimer, jiffies + HZ);
	spin_unlock_irqrestore(&moxa_lock, flags);
}

static void moxa_check_xmit_empty(unsigned long data)
{
	struct moxa_port *ch;

	ch = (struct moxa_port *) data;
	if (ch->tty && (ch->statusflags & EMPTYWAIT)) {
		if (MoxaPortTxQueue(ch->port) == 0) {
			ch->statusflags &= ~EMPTYWAIT;
			tty_wakeup(ch->tty);
			return;
		}
		mod_timer(&moxa_ports[ch->port].emptyTimer,
				round_jiffies(jiffies + HZ));
	} else
		ch->statusflags &= ~EMPTYWAIT;
}

static void moxa_shut_down(struct moxa_port *ch)
{
	struct tty_struct *tp;

	if (!(ch->asyncflags & ASYNC_INITIALIZED))
		return;

	tp = ch->tty;

	MoxaPortDisable(ch->port);

	/*
	 * If we're a modem control device and HUPCL is on, drop RTS & DTR.
	 */
	if (tp->termios->c_cflag & HUPCL)
		MoxaPortLineCtrl(ch->port, 0, 0);

	ch->asyncflags &= ~ASYNC_INITIALIZED;
}

static void moxa_receive_data(struct moxa_port *ch)
{
	struct tty_struct *tp;
	struct ktermios *ts;
	unsigned long flags;

	ts = NULL;
	tp = ch->tty;
	if (tp)
		ts = tp->termios;
	/**************************************************
	if ( !tp || !ts || !(ts->c_cflag & CREAD) ) {
	*****************************************************/
	if (!tp || !ts) {
		MoxaPortFlushData(ch->port, 0);
		return;
	}
	spin_lock_irqsave(&moxa_lock, flags);
	MoxaPortReadData(ch->port, tp);
	spin_unlock_irqrestore(&moxa_lock, flags);
	tty_schedule_flip(tp);
}

#define Magic_code	0x404

/*
 *    System Configuration
 */
/*
 *    for C218 BIOS initialization
 */
#define C218_ConfBase	0x800
#define C218_status	(C218_ConfBase + 0)	/* BIOS running status    */
#define C218_diag	(C218_ConfBase + 2)	/* diagnostic status      */
#define C218_key	(C218_ConfBase + 4)	/* WORD (0x218 for C218) */
#define C218DLoad_len	(C218_ConfBase + 6)	/* WORD           */
#define C218check_sum	(C218_ConfBase + 8)	/* BYTE           */
#define C218chksum_ok	(C218_ConfBase + 0x0a)	/* BYTE (1:ok)            */
#define C218_TestRx	(C218_ConfBase + 0x10)	/* 8 bytes for 8 ports    */
#define C218_TestTx	(C218_ConfBase + 0x18)	/* 8 bytes for 8 ports    */
#define C218_RXerr	(C218_ConfBase + 0x20)	/* 8 bytes for 8 ports    */
#define C218_ErrFlag	(C218_ConfBase + 0x28)	/* 8 bytes for 8 ports    */

#define C218_LoadBuf	0x0F00
#define C218_KeyCode	0x218
#define CP204J_KeyCode	0x204

/*
 *    for C320 BIOS initialization
 */
#define C320_ConfBase	0x800
#define C320_LoadBuf	0x0f00
#define STS_init	0x05	/* for C320_status        */

#define C320_status	C320_ConfBase + 0	/* BIOS running status    */
#define C320_diag	C320_ConfBase + 2	/* diagnostic status      */
#define C320_key	C320_ConfBase + 4	/* WORD (0320H for C320) */
#define C320DLoad_len	C320_ConfBase + 6	/* WORD           */
#define C320check_sum	C320_ConfBase + 8	/* WORD           */
#define C320chksum_ok	C320_ConfBase + 0x0a	/* WORD (1:ok)            */
#define C320bapi_len	C320_ConfBase + 0x0c	/* WORD           */
#define C320UART_no	C320_ConfBase + 0x0e	/* WORD           */

#define C320_KeyCode	0x320

#define FixPage_addr	0x0000	/* starting addr of static page  */
#define DynPage_addr	0x2000	/* starting addr of dynamic page */
#define C218_start	0x3000	/* starting addr of C218 BIOS prg */
#define Control_reg	0x1ff0	/* select page and reset control */
#define HW_reset	0x80

/*
 *    Function Codes
 */
#define FC_CardReset	0x80
#define FC_ChannelReset 1	/* C320 firmware not supported */
#define FC_EnableCH	2
#define FC_DisableCH	3
#define FC_SetParam	4
#define FC_SetMode	5
#define FC_SetRate	6
#define FC_LineControl	7
#define FC_LineStatus	8
#define FC_XmitControl	9
#define FC_FlushQueue	10
#define FC_SendBreak	11
#define FC_StopBreak	12
#define FC_LoopbackON	13
#define FC_LoopbackOFF	14
#define FC_ClrIrqTable	15
#define FC_SendXon	16
#define FC_SetTermIrq	17	/* C320 firmware not supported */
#define FC_SetCntIrq	18	/* C320 firmware not supported */
#define FC_SetBreakIrq	19
#define FC_SetLineIrq	20
#define FC_SetFlowCtl	21
#define FC_GenIrq	22
#define FC_InCD180	23
#define FC_OutCD180	24
#define FC_InUARTreg	23
#define FC_OutUARTreg	24
#define FC_SetXonXoff	25
#define FC_OutCD180CCR	26
#define FC_ExtIQueue	27
#define FC_ExtOQueue	28
#define FC_ClrLineIrq	29
#define FC_HWFlowCtl	30
#define FC_GetClockRate 35
#define FC_SetBaud	36
#define FC_SetDataMode  41
#define FC_GetCCSR      43
#define FC_GetDataError 45
#define FC_RxControl	50
#define FC_ImmSend	51
#define FC_SetXonState	52
#define FC_SetXoffState	53
#define FC_SetRxFIFOTrig 54
#define FC_SetTxFIFOCnt 55
#define FC_UnixRate	56
#define FC_UnixResetTimer 57

#define	RxFIFOTrig1	0
#define	RxFIFOTrig4	1
#define	RxFIFOTrig8	2
#define	RxFIFOTrig14	3

/*
 *    Dual-Ported RAM
 */
#define DRAM_global	0
#define INT_data	(DRAM_global + 0)
#define Config_base	(DRAM_global + 0x108)

#define IRQindex	(INT_data + 0)
#define IRQpending	(INT_data + 4)
#define IRQtable	(INT_data + 8)

/*
 *    Interrupt Status
 */
#define IntrRx		0x01	/* receiver data O.K.             */
#define IntrTx		0x02	/* transmit buffer empty  */
#define IntrFunc	0x04	/* function complete              */
#define IntrBreak	0x08	/* received break         */
#define IntrLine	0x10	/* line status change
				   for transmitter                */
#define IntrIntr	0x20	/* received INTR code             */
#define IntrQuit	0x40	/* received QUIT code             */
#define IntrEOF 	0x80	/* received EOF code              */

#define IntrRxTrigger 	0x100	/* rx data count reach tigger value */
#define IntrTxTrigger 	0x200	/* tx data count below trigger value */

#define Magic_no	(Config_base + 0)
#define Card_model_no	(Config_base + 2)
#define Total_ports	(Config_base + 4)
#define Module_cnt	(Config_base + 8)
#define Module_no	(Config_base + 10)
#define Timer_10ms	(Config_base + 14)
#define Disable_IRQ	(Config_base + 20)
#define TMS320_PORT1	(Config_base + 22)
#define TMS320_PORT2	(Config_base + 24)
#define TMS320_CLOCK	(Config_base + 26)

/*
 *    DATA BUFFER in DRAM
 */
#define Extern_table	0x400	/* Base address of the external table
				   (24 words *    64) total 3K bytes
				   (24 words * 128) total 6K bytes */
#define Extern_size	0x60	/* 96 bytes                       */
#define RXrptr		0x00	/* read pointer for RX buffer     */
#define RXwptr		0x02	/* write pointer for RX buffer    */
#define TXrptr		0x04	/* read pointer for TX buffer     */
#define TXwptr		0x06	/* write pointer for TX buffer    */
#define HostStat	0x08	/* IRQ flag and general flag      */
#define FlagStat	0x0A
#define FlowControl	0x0C	/* B7 B6 B5 B4 B3 B2 B1 B0              */
					/*  x  x  x  x  |  |  |  |            */
					/*              |  |  |  + CTS flow   */
					/*              |  |  +--- RTS flow   */
					/*              |  +------ TX Xon/Xoff */
					/*              +--------- RX Xon/Xoff */
#define Break_cnt	0x0E	/* received break count   */
#define CD180TXirq	0x10	/* if non-0: enable TX irq        */
#define RX_mask 	0x12
#define TX_mask 	0x14
#define Ofs_rxb 	0x16
#define Ofs_txb 	0x18
#define Page_rxb	0x1A
#define Page_txb	0x1C
#define EndPage_rxb	0x1E
#define EndPage_txb	0x20
#define Data_error	0x22
#define RxTrigger	0x28
#define TxTrigger	0x2a

#define rRXwptr 	0x34
#define Low_water	0x36

#define FuncCode	0x40
#define FuncArg 	0x42
#define FuncArg1	0x44

#define C218rx_size	0x2000	/* 8K bytes */
#define C218tx_size	0x8000	/* 32K bytes */

#define C218rx_mask	(C218rx_size - 1)
#define C218tx_mask	(C218tx_size - 1)

#define C320p8rx_size	0x2000
#define C320p8tx_size	0x8000
#define C320p8rx_mask	(C320p8rx_size - 1)
#define C320p8tx_mask	(C320p8tx_size - 1)

#define C320p16rx_size	0x2000
#define C320p16tx_size	0x4000
#define C320p16rx_mask	(C320p16rx_size - 1)
#define C320p16tx_mask	(C320p16tx_size - 1)

#define C320p24rx_size	0x2000
#define C320p24tx_size	0x2000
#define C320p24rx_mask	(C320p24rx_size - 1)
#define C320p24tx_mask	(C320p24tx_size - 1)

#define C320p32rx_size	0x1000
#define C320p32tx_size	0x1000
#define C320p32rx_mask	(C320p32rx_size - 1)
#define C320p32tx_mask	(C320p32tx_size - 1)

#define Page_size	0x2000
#define Page_mask	(Page_size - 1)
#define C218rx_spage	3
#define C218tx_spage	4
#define C218rx_pageno	1
#define C218tx_pageno	4
#define C218buf_pageno	5

#define C320p8rx_spage	3
#define C320p8tx_spage	4
#define C320p8rx_pgno	1
#define C320p8tx_pgno	4
#define C320p8buf_pgno	5

#define C320p16rx_spage 3
#define C320p16tx_spage 4
#define C320p16rx_pgno	1
#define C320p16tx_pgno	2
#define C320p16buf_pgno 3

#define C320p24rx_spage 3
#define C320p24tx_spage 4
#define C320p24rx_pgno	1
#define C320p24tx_pgno	1
#define C320p24buf_pgno 2

#define C320p32rx_spage 3
#define C320p32tx_ofs	C320p32rx_size
#define C320p32tx_spage 3
#define C320p32buf_pgno 1

/*
 *    Host Status
 */
#define WakeupRx	0x01
#define WakeupTx	0x02
#define WakeupBreak	0x08
#define WakeupLine	0x10
#define WakeupIntr	0x20
#define WakeupQuit	0x40
#define WakeupEOF	0x80	/* used in VTIME control */
#define WakeupRxTrigger	0x100
#define WakeupTxTrigger	0x200
/*
 *    Flag status
 */
#define Rx_over		0x01
#define Xoff_state	0x02
#define Tx_flowOff	0x04
#define Tx_enable	0x08
#define CTS_state	0x10
#define DSR_state	0x20
#define DCD_state	0x80
/*
 *    FlowControl
 */
#define CTS_FlowCtl	1
#define RTS_FlowCtl	2
#define Tx_FlowCtl	4
#define Rx_FlowCtl	8
#define IXM_IXANY	0x10

#define LowWater	128

#define DTR_ON		1
#define RTS_ON		2
#define CTS_ON		1
#define DSR_ON		2
#define DCD_ON		8

/* mode definition */
#define	MX_CS8		0x03
#define	MX_CS7		0x02
#define	MX_CS6		0x01
#define	MX_CS5		0x00

#define	MX_STOP1	0x00
#define	MX_STOP15	0x04
#define	MX_STOP2	0x08

#define	MX_PARNONE	0x00
#define	MX_PAREVEN	0x40
#define	MX_PARODD	0xC0

/*
 *    Query
 */

struct mon_str {
	int tick;
	int rxcnt[MAX_PORTS];
	int txcnt[MAX_PORTS];
};

#define 	DCD_changed	0x01
#define 	DCD_oldstate	0x80

static unsigned char moxaBuff[10240];
static int moxaLowWaterChk;
static int moxaCard;
static struct mon_str moxaLog;
static int moxaFuncTout = HZ / 2;

static void moxafunc(void __iomem *, int, ushort);
static void moxa_wait_finish(void __iomem *);
static void moxa_low_water_check(void __iomem *);
static int moxaloadbios(int, unsigned char __user *, int);
static int moxafindcard(int);
static int moxaload320b(int, unsigned char __user *, int);
static int moxaloadcode(int, unsigned char __user *, int);
static int moxaloadc218(int, void __iomem *, int);
static int moxaloadc320(int, void __iomem *, int, int *);

/*****************************************************************************
 *	Driver level functions: 					     *
 *	1. MoxaDriverInit(void);					     *
 *	2. MoxaDriverIoctl(unsigned int cmd, unsigned long arg, int port);   *
 *	3. MoxaDriverPoll(void);					     *
 *****************************************************************************/
void MoxaDriverInit(void)
{
	struct moxa_port *p;
	unsigned int i;

	moxaFuncTout = HZ / 2;	/* 500 mini-seconds */
	moxaCard = 0;
	moxaLog.tick = 0;
	moxaLowWaterChk = 0;
	for (i = 0; i < MAX_PORTS; i++) {
		p = &moxa_ports[i];
		p->chkPort = 0;
		p->lowChkFlag = 0;
		p->lineCtrl = 0;
		moxaLog.rxcnt[i] = 0;
		moxaLog.txcnt[i] = 0;
	}
}

#define	MOXA		0x400
#define MOXA_GET_IQUEUE 	(MOXA + 1)	/* get input buffered count */
#define MOXA_GET_OQUEUE 	(MOXA + 2)	/* get output buffered count */
#define MOXA_INIT_DRIVER	(MOXA + 6)	/* moxaCard=0 */
#define MOXA_LOAD_BIOS		(MOXA + 9)	/* download BIOS */
#define MOXA_FIND_BOARD		(MOXA + 10)	/* Check if MOXA card exist? */
#define MOXA_LOAD_C320B		(MOXA + 11)	/* download 320B firmware */
#define MOXA_LOAD_CODE		(MOXA + 12)	/* download firmware */
#define MOXA_GETDATACOUNT       (MOXA + 23)
#define MOXA_GET_IOQUEUE	(MOXA + 27)
#define MOXA_FLUSH_QUEUE	(MOXA + 28)
#define MOXA_GET_CONF		(MOXA + 35)	/* configuration */
#define MOXA_GET_MAJOR          (MOXA + 63)
#define MOXA_GET_CUMAJOR        (MOXA + 64)
#define MOXA_GETMSTATUS         (MOXA + 65)

struct dl_str {
	char __user *buf;
	int len;
	int cardno;
};

static struct dl_str dltmp;

void MoxaPortFlushData(int port, int mode)
{
	void __iomem *ofsAddr;
	if ((mode < 0) || (mode > 2))
		return;
	ofsAddr = moxa_ports[port].tableAddr;
	moxafunc(ofsAddr, FC_FlushQueue, mode);
	if (mode != 1) {
		moxa_ports[port].lowChkFlag = 0;
		moxa_low_water_check(ofsAddr);
	}
}

int MoxaDriverIoctl(unsigned int cmd, unsigned long arg, int port)
{
	int i;
	int status;
	int MoxaPortTxQueue(int), MoxaPortRxQueue(int);
	void __user *argp = (void __user *)arg;

	if (port == MAX_PORTS) {
		if ((cmd != MOXA_GET_CONF) && (cmd != MOXA_INIT_DRIVER) &&
		    (cmd != MOXA_LOAD_BIOS) && (cmd != MOXA_FIND_BOARD) && (cmd != MOXA_LOAD_C320B) &&
		 (cmd != MOXA_LOAD_CODE) && (cmd != MOXA_GETDATACOUNT) &&
		  (cmd != MOXA_GET_IOQUEUE) && (cmd != MOXA_GET_MAJOR) &&
		    (cmd != MOXA_GET_CUMAJOR) && (cmd != MOXA_GETMSTATUS))
			return (-EINVAL);
	}
	switch (cmd) {
	case MOXA_GET_CONF:
		if(copy_to_user(argp, &moxa_boards, MAX_BOARDS *
				sizeof(struct moxa_board_conf)))
			return -EFAULT;
		return (0);
	case MOXA_INIT_DRIVER:
		if ((int) arg == 0x404)
			MoxaDriverInit();
		return (0);
	case MOXA_GETDATACOUNT:
		moxaLog.tick = jiffies;
		if(copy_to_user(argp, &moxaLog, sizeof(struct mon_str)))
			return -EFAULT;
		return (0);
	case MOXA_FLUSH_QUEUE:
		MoxaPortFlushData(port, arg);
		return (0);
	case MOXA_GET_IOQUEUE: {
		struct moxaq_str __user *argm = argp;
		struct moxaq_str tmp;

		for (i = 0; i < MAX_PORTS; i++, argm++) {
			memset(&tmp, 0, sizeof(tmp));
			if (moxa_ports[i].chkPort) {
				tmp.inq = MoxaPortRxQueue(i);
				tmp.outq = MoxaPortTxQueue(i);
			}
			if (copy_to_user(argm, &tmp, sizeof(tmp)))
				return -EFAULT;
		}
		return (0);
	} case MOXA_GET_OQUEUE:
		i = MoxaPortTxQueue(port);
		return put_user(i, (unsigned long __user *)argp);
	case MOXA_GET_IQUEUE:
		i = MoxaPortRxQueue(port);
		return put_user(i, (unsigned long __user *)argp);
	case MOXA_GET_MAJOR:
		if(copy_to_user(argp, &ttymajor, sizeof(int)))
			return -EFAULT;
		return 0;
	case MOXA_GET_CUMAJOR:
		i = 0;
		if(copy_to_user(argp, &i, sizeof(int)))
			return -EFAULT;
		return 0;
	case MOXA_GETMSTATUS: {
		struct mxser_mstatus __user *argm = argp;
		struct mxser_mstatus tmp;
		struct moxa_port *p;

		for (i = 0; i < MAX_PORTS; i++, argm++) {
			p = &moxa_ports[i];
			memset(&tmp, 0, sizeof(tmp));
			if (!p->chkPort) {
				goto copy;
			} else {
				status = MoxaPortLineStatus(p->port);
				if (status & 1)
					tmp.cts = 1;
				if (status & 2)
					tmp.dsr = 1;
				if (status & 4)
					tmp.dcd = 1;
			}

			if (!p->tty || !p->tty->termios)
				tmp.cflag = p->cflag;
			else
				tmp.cflag = p->tty->termios->c_cflag;
copy:
			if (copy_to_user(argm, &tmp, sizeof(tmp)))
				return -EFAULT;
		}
		return 0;
	} default:
		return (-ENOIOCTLCMD);
	case MOXA_LOAD_BIOS:
	case MOXA_FIND_BOARD:
	case MOXA_LOAD_C320B:
	case MOXA_LOAD_CODE:
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		break;
	}

	if(copy_from_user(&dltmp, argp, sizeof(struct dl_str)))
		return -EFAULT;
	if(dltmp.cardno < 0 || dltmp.cardno >= MAX_BOARDS || dltmp.len < 0)
		return -EINVAL;

	switch(cmd)
	{
	case MOXA_LOAD_BIOS:
		i = moxaloadbios(dltmp.cardno, dltmp.buf, dltmp.len);
		return (i);
	case MOXA_FIND_BOARD:
		return moxafindcard(dltmp.cardno);
	case MOXA_LOAD_C320B:
		moxaload320b(dltmp.cardno, dltmp.buf, dltmp.len);
	default: /* to keep gcc happy */
		return (0);
	case MOXA_LOAD_CODE:
		i = moxaloadcode(dltmp.cardno, dltmp.buf, dltmp.len);
		if (i == -1)
			return (-EFAULT);
		return (i);

	}
}

int MoxaDriverPoll(void)
{
	struct moxa_board_conf *brd;
	register ushort temp;
	register int card;
	void __iomem *ofsAddr;
	void __iomem *ip;
	int port, p, ports;

	if (moxaCard == 0)
		return (-1);
	for (card = 0; card < MAX_BOARDS; card++) {
		brd = &moxa_boards[card];
	        if (brd->loadstat == 0)
			continue;
		if ((ports = brd->numPorts) == 0)
			continue;
		if (readb(brd->intPend) == 0xff) {
			ip = brd->intTable + readb(brd->intNdx);
			p = card * MAX_PORTS_PER_BOARD;
			ports <<= 1;
			for (port = 0; port < ports; port += 2, p++) {
				if ((temp = readw(ip + port)) != 0) {
					writew(0, ip + port);
					ofsAddr = moxa_ports[p].tableAddr;
					if (temp & IntrTx)
						writew(readw(ofsAddr + HostStat) & ~WakeupTx, ofsAddr + HostStat);
					if (temp & IntrBreak) {
						moxa_ports[p].breakCnt++;
					}
					if (temp & IntrLine) {
						if (readb(ofsAddr + FlagStat) & DCD_state) {
							if ((moxa_ports[p].DCDState & DCD_oldstate) == 0)
								moxa_ports[p].DCDState = (DCD_oldstate |
										   DCD_changed);
						} else {
							if (moxa_ports[p].DCDState & DCD_oldstate)
								moxa_ports[p].DCDState = DCD_changed;
						}
					}
				}
			}
			writeb(0, brd->intPend);
		}
		if (moxaLowWaterChk) {
			p = card * MAX_PORTS_PER_BOARD;
			for (port = 0; port < ports; port++, p++) {
				if (moxa_ports[p].lowChkFlag) {
					moxa_ports[p].lowChkFlag = 0;
					ofsAddr = moxa_ports[p].tableAddr;
					moxa_low_water_check(ofsAddr);
				}
			}
		}
	}
	moxaLowWaterChk = 0;
	return (0);
}

/*****************************************************************************
 *	Card level function:						     *
 *	1. MoxaPortsOfCard(int cardno); 				     *
 *****************************************************************************/
int MoxaPortsOfCard(int cardno)
{

	if (moxa_boards[cardno].boardType == 0)
		return (0);
	return (moxa_boards[cardno].numPorts);
}

/*****************************************************************************
 *	Port level functions:						     *
 *	1.  MoxaPortIsValid(int port);					     *
 *	2.  MoxaPortEnable(int port);					     *
 *	3.  MoxaPortDisable(int port);					     *
 *	4.  MoxaPortGetMaxBaud(int port);				     *
 *	6.  MoxaPortSetBaud(int port, long baud);			     *
 *	8.  MoxaPortSetTermio(int port, unsigned char *termio); 	     *
 *	9.  MoxaPortGetLineOut(int port, int *dtrState, int *rtsState);      *
 *	10. MoxaPortLineCtrl(int port, int dtrState, int rtsState);	     *
 *	11. MoxaPortFlowCtrl(int port, int rts, int cts, int rx, int tx,int xany);    *
 *	12. MoxaPortLineStatus(int port);				     *
 *	13. MoxaPortDCDChange(int port);				     *
 *	14. MoxaPortDCDON(int port);					     *
 *	15. MoxaPortFlushData(int port, int mode);	                     *
 *	16. MoxaPortWriteData(int port, unsigned char * buffer, int length); *
 *	17. MoxaPortReadData(int port, struct tty_struct *tty); 	     *
 *	20. MoxaPortTxQueue(int port);					     *
 *	21. MoxaPortTxFree(int port);					     *
 *	22. MoxaPortRxQueue(int port);					     *
 *	24. MoxaPortTxDisable(int port);				     *
 *	25. MoxaPortTxEnable(int port); 				     *
 *	27. MoxaPortResetBrkCnt(int port);				     *
 *	30. MoxaPortSendBreak(int port, int ticks);			     *
 *****************************************************************************/
/*
 *    Moxa Port Number Description:
 *
 *      MOXA serial driver supports up to 4 MOXA-C218/C320 boards. And,
 *      the port number using in MOXA driver functions will be 0 to 31 for
 *      first MOXA board, 32 to 63 for second, 64 to 95 for third and 96
 *      to 127 for fourth. For example, if you setup three MOXA boards,
 *      first board is C218, second board is C320-16 and third board is
 *      C320-32. The port number of first board (C218 - 8 ports) is from
 *      0 to 7. The port number of second board (C320 - 16 ports) is form
 *      32 to 47. The port number of third board (C320 - 32 ports) is from
 *      64 to 95. And those port numbers form 8 to 31, 48 to 63 and 96 to
 *      127 will be invalid.
 *
 *
 *      Moxa Functions Description:
 *
 *      Function 1:     Driver initialization routine, this routine must be
 *                      called when initialized driver.
 *      Syntax:
 *      void MoxaDriverInit();
 *
 *
 *      Function 2:     Moxa driver private IOCTL command processing.
 *      Syntax:
 *      int  MoxaDriverIoctl(unsigned int cmd, unsigned long arg, int port);
 *
 *           unsigned int cmd   : IOCTL command
 *           unsigned long arg  : IOCTL argument
 *           int port           : port number (0 - 127)
 *
 *           return:    0  (OK)
 *                      -EINVAL
 *                      -ENOIOCTLCMD
 *
 *
 *      Function 3:     Moxa driver polling process routine.
 *      Syntax:
 *      int  MoxaDriverPoll(void);
 *
 *           return:    0       ; polling O.K.
 *                      -1      : no any Moxa card.             
 *
 *
 *      Function 4:     Get the ports of this card.
 *      Syntax:
 *      int  MoxaPortsOfCard(int cardno);
 *
 *           int cardno         : card number (0 - 3)
 *
 *           return:    0       : this card is invalid
 *                      8/16/24/32
 *
 *
 *      Function 5:     Check this port is valid or invalid
 *      Syntax:
 *      int  MoxaPortIsValid(int port);
 *           int port           : port number (0 - 127, ref port description)
 *
 *           return:    0       : this port is invalid
 *                      1       : this port is valid
 *
 *
 *      Function 6:     Enable this port to start Tx/Rx data.
 *      Syntax:
 *      void MoxaPortEnable(int port);
 *           int port           : port number (0 - 127)
 *
 *
 *      Function 7:     Disable this port
 *      Syntax:
 *      void MoxaPortDisable(int port);
 *           int port           : port number (0 - 127)
 *
 *
 *      Function 8:     Get the maximun available baud rate of this port.
 *      Syntax:
 *      long MoxaPortGetMaxBaud(int port);
 *           int port           : port number (0 - 127)
 *
 *           return:    0       : this port is invalid
 *                      38400/57600/115200 bps
 *
 *
 *      Function 10:    Setting baud rate of this port.
 *      Syntax:
 *      long MoxaPortSetBaud(int port, long baud);
 *           int port           : port number (0 - 127)
 *           long baud          : baud rate (50 - 115200)
 *
 *           return:    0       : this port is invalid or baud < 50
 *                      50 - 115200 : the real baud rate set to the port, if
 *                                    the argument baud is large than maximun
 *                                    available baud rate, the real setting
 *                                    baud rate will be the maximun baud rate.
 *
 *
 *      Function 12:    Configure the port.
 *      Syntax:
 *      int  MoxaPortSetTermio(int port, struct ktermios *termio, speed_t baud);
 *           int port           : port number (0 - 127)
 *           struct ktermios * termio : termio structure pointer
 *	     speed_t baud	: baud rate
 *
 *           return:    -1      : this port is invalid or termio == NULL
 *                      0       : setting O.K.
 *
 *
 *      Function 13:    Get the DTR/RTS state of this port.
 *      Syntax:
 *      int  MoxaPortGetLineOut(int port, int *dtrState, int *rtsState);
 *           int port           : port number (0 - 127)
 *           int * dtrState     : pointer to INT to receive the current DTR
 *                                state. (if NULL, this function will not
 *                                write to this address)
 *           int * rtsState     : pointer to INT to receive the current RTS
 *                                state. (if NULL, this function will not
 *                                write to this address)
 *
 *           return:    -1      : this port is invalid
 *                      0       : O.K.
 *
 *
 *      Function 14:    Setting the DTR/RTS output state of this port.
 *      Syntax:
 *      void MoxaPortLineCtrl(int port, int dtrState, int rtsState);
 *           int port           : port number (0 - 127)
 *           int dtrState       : DTR output state (0: off, 1: on)
 *           int rtsState       : RTS output state (0: off, 1: on)
 *
 *
 *      Function 15:    Setting the flow control of this port.
 *      Syntax:
 *      void MoxaPortFlowCtrl(int port, int rtsFlow, int ctsFlow, int rxFlow,
 *                            int txFlow,int xany);
 *           int port           : port number (0 - 127)
 *           int rtsFlow        : H/W RTS flow control (0: no, 1: yes)
 *           int ctsFlow        : H/W CTS flow control (0: no, 1: yes)
 *           int rxFlow         : S/W Rx XON/XOFF flow control (0: no, 1: yes)
 *           int txFlow         : S/W Tx XON/XOFF flow control (0: no, 1: yes)
 *           int xany           : S/W XANY flow control (0: no, 1: yes)
 *
 *
 *      Function 16:    Get ths line status of this port
 *      Syntax:
 *      int  MoxaPortLineStatus(int port);
 *           int port           : port number (0 - 127)
 *
 *           return:    Bit 0 - CTS state (0: off, 1: on)
 *                      Bit 1 - DSR state (0: off, 1: on)
 *                      Bit 2 - DCD state (0: off, 1: on)
 *
 *
 *      Function 17:    Check the DCD state has changed since the last read
 *                      of this function.
 *      Syntax:
 *      int  MoxaPortDCDChange(int port);
 *           int port           : port number (0 - 127)
 *
 *           return:    0       : no changed
 *                      1       : DCD has changed
 *
 *
 *      Function 18:    Check ths current DCD state is ON or not.
 *      Syntax:
 *      int  MoxaPortDCDON(int port);
 *           int port           : port number (0 - 127)
 *
 *           return:    0       : DCD off
 *                      1       : DCD on
 *
 *
 *      Function 19:    Flush the Rx/Tx buffer data of this port.
 *      Syntax:
 *      void MoxaPortFlushData(int port, int mode);
 *           int port           : port number (0 - 127)
 *           int mode    
 *                      0       : flush the Rx buffer 
 *                      1       : flush the Tx buffer 
 *                      2       : flush the Rx and Tx buffer 
 *
 *
 *      Function 20:    Write data.
 *      Syntax:
 *      int  MoxaPortWriteData(int port, unsigned char * buffer, int length);
 *           int port           : port number (0 - 127)
 *           unsigned char * buffer     : pointer to write data buffer.
 *           int length         : write data length
 *
 *           return:    0 - length      : real write data length
 *
 *
 *      Function 21:    Read data.
 *      Syntax:
 *      int  MoxaPortReadData(int port, struct tty_struct *tty);
 *           int port           : port number (0 - 127)
 *	     struct tty_struct *tty : tty for data
 *
 *           return:    0 - length      : real read data length
 *
 *
 *      Function 24:    Get the Tx buffer current queued data bytes
 *      Syntax:
 *      int  MoxaPortTxQueue(int port);
 *           int port           : port number (0 - 127)
 *
 *           return:    ..      : Tx buffer current queued data bytes
 *
 *
 *      Function 25:    Get the Tx buffer current free space
 *      Syntax:
 *      int  MoxaPortTxFree(int port);
 *           int port           : port number (0 - 127)
 *
 *           return:    ..      : Tx buffer current free space
 *
 *
 *      Function 26:    Get the Rx buffer current queued data bytes
 *      Syntax:
 *      int  MoxaPortRxQueue(int port);
 *           int port           : port number (0 - 127)
 *
 *           return:    ..      : Rx buffer current queued data bytes
 *
 *
 *      Function 28:    Disable port data transmission.
 *      Syntax:
 *      void MoxaPortTxDisable(int port);
 *           int port           : port number (0 - 127)
 *
 *
 *      Function 29:    Enable port data transmission.
 *      Syntax:
 *      void MoxaPortTxEnable(int port);
 *           int port           : port number (0 - 127)
 *
 *
 *      Function 31:    Get the received BREAK signal count and reset it.
 *      Syntax:
 *      int  MoxaPortResetBrkCnt(int port);
 *           int port           : port number (0 - 127)
 *
 *           return:    0 - ..  : BREAK signal count
 *
 *
 *      Function 34:    Send out a BREAK signal.
 *      Syntax:
 *      void MoxaPortSendBreak(int port, int ms100);
 *           int port           : port number (0 - 127)
 *           int ms100          : break signal time interval.
 *                                unit: 100 mini-second. if ms100 == 0, it will
 *                                send out a about 250 ms BREAK signal.
 *
 */
int MoxaPortIsValid(int port)
{

	if (moxaCard == 0)
		return (0);
	if (moxa_ports[port].chkPort == 0)
		return (0);
	return (1);
}

void MoxaPortEnable(int port)
{
	void __iomem *ofsAddr;
	int MoxaPortLineStatus(int);
	short lowwater = 512;

	ofsAddr = moxa_ports[port].tableAddr;
	writew(lowwater, ofsAddr + Low_water);
	moxa_ports[port].breakCnt = 0;
	if ((moxa_boards[port / MAX_PORTS_PER_BOARD].boardType == MOXA_BOARD_C320_ISA) ||
	    (moxa_boards[port / MAX_PORTS_PER_BOARD].boardType == MOXA_BOARD_C320_PCI)) {
		moxafunc(ofsAddr, FC_SetBreakIrq, 0);
	} else {
		writew(readw(ofsAddr + HostStat) | WakeupBreak, ofsAddr + HostStat);
	}

	moxafunc(ofsAddr, FC_SetLineIrq, Magic_code);
	moxafunc(ofsAddr, FC_FlushQueue, 2);

	moxafunc(ofsAddr, FC_EnableCH, Magic_code);
	MoxaPortLineStatus(port);
}

void MoxaPortDisable(int port)
{
	void __iomem *ofsAddr = moxa_ports[port].tableAddr;

	moxafunc(ofsAddr, FC_SetFlowCtl, 0);	/* disable flow control */
	moxafunc(ofsAddr, FC_ClrLineIrq, Magic_code);
	writew(0, ofsAddr + HostStat);
	moxafunc(ofsAddr, FC_DisableCH, Magic_code);
}

long MoxaPortGetMaxBaud(int port)
{
	if ((moxa_boards[port / MAX_PORTS_PER_BOARD].boardType == MOXA_BOARD_C320_ISA) ||
	    (moxa_boards[port / MAX_PORTS_PER_BOARD].boardType == MOXA_BOARD_C320_PCI))
		return (460800L);
	else
		return (921600L);
}


long MoxaPortSetBaud(int port, long baud)
{
	void __iomem *ofsAddr;
	long max, clock;
	unsigned int val;

	if ((baud < 50L) || ((max = MoxaPortGetMaxBaud(port)) == 0))
		return (0);
	ofsAddr = moxa_ports[port].tableAddr;
	if (baud > max)
		baud = max;
	if (max == 38400L)
		clock = 614400L;	/* for 9.8304 Mhz : max. 38400 bps */
	else if (max == 57600L)
		clock = 691200L;	/* for 11.0592 Mhz : max. 57600 bps */
	else
		clock = 921600L;	/* for 14.7456 Mhz : max. 115200 bps */
	val = clock / baud;
	moxafunc(ofsAddr, FC_SetBaud, val);
	baud = clock / val;
	moxa_ports[port].curBaud = baud;
	return (baud);
}

int MoxaPortSetTermio(int port, struct ktermios *termio, speed_t baud)
{
	void __iomem *ofsAddr;
	tcflag_t cflag;
	tcflag_t mode = 0;

	if (moxa_ports[port].chkPort == 0 || termio == 0)
		return (-1);
	ofsAddr = moxa_ports[port].tableAddr;
	cflag = termio->c_cflag;	/* termio->c_cflag */

	mode = termio->c_cflag & CSIZE;
	if (mode == CS5)
		mode = MX_CS5;
	else if (mode == CS6)
		mode = MX_CS6;
	else if (mode == CS7)
		mode = MX_CS7;
	else if (mode == CS8)
		mode = MX_CS8;

	if (termio->c_cflag & CSTOPB) {
		if (mode == MX_CS5)
			mode |= MX_STOP15;
		else
			mode |= MX_STOP2;
	} else
		mode |= MX_STOP1;

	if (termio->c_cflag & PARENB) {
		if (termio->c_cflag & PARODD)
			mode |= MX_PARODD;
		else
			mode |= MX_PAREVEN;
	} else
		mode |= MX_PARNONE;

	moxafunc(ofsAddr, FC_SetDataMode, (ushort) mode);

	if ((moxa_boards[port / MAX_PORTS_PER_BOARD].boardType == MOXA_BOARD_C320_ISA) ||
	    (moxa_boards[port / MAX_PORTS_PER_BOARD].boardType == MOXA_BOARD_C320_PCI)) {
		if (baud >= 921600L)
			return (-1);
	}
	baud = MoxaPortSetBaud(port, baud);

	if (termio->c_iflag & (IXON | IXOFF | IXANY)) {
		writeb(termio->c_cc[VSTART], ofsAddr + FuncArg);
		writeb(termio->c_cc[VSTOP], ofsAddr + FuncArg1);
		writeb(FC_SetXonXoff, ofsAddr + FuncCode);
		moxa_wait_finish(ofsAddr);

	}
	return (baud);
}

int MoxaPortGetLineOut(int port, int *dtrState, int *rtsState)
{

	if (!MoxaPortIsValid(port))
		return (-1);
	if (dtrState) {
		if (moxa_ports[port].lineCtrl & DTR_ON)
			*dtrState = 1;
		else
			*dtrState = 0;
	}
	if (rtsState) {
		if (moxa_ports[port].lineCtrl & RTS_ON)
			*rtsState = 1;
		else
			*rtsState = 0;
	}
	return (0);
}

void MoxaPortLineCtrl(int port, int dtr, int rts)
{
	void __iomem *ofsAddr;
	int mode;

	ofsAddr = moxa_ports[port].tableAddr;
	mode = 0;
	if (dtr)
		mode |= DTR_ON;
	if (rts)
		mode |= RTS_ON;
	moxa_ports[port].lineCtrl = mode;
	moxafunc(ofsAddr, FC_LineControl, mode);
}

void MoxaPortFlowCtrl(int port, int rts, int cts, int txflow, int rxflow, int txany)
{
	void __iomem *ofsAddr;
	int mode;

	ofsAddr = moxa_ports[port].tableAddr;
	mode = 0;
	if (rts)
		mode |= RTS_FlowCtl;
	if (cts)
		mode |= CTS_FlowCtl;
	if (txflow)
		mode |= Tx_FlowCtl;
	if (rxflow)
		mode |= Rx_FlowCtl;
	if (txany)
		mode |= IXM_IXANY;
	moxafunc(ofsAddr, FC_SetFlowCtl, mode);
}

int MoxaPortLineStatus(int port)
{
	void __iomem *ofsAddr;
	int val;

	ofsAddr = moxa_ports[port].tableAddr;
	if ((moxa_boards[port / MAX_PORTS_PER_BOARD].boardType == MOXA_BOARD_C320_ISA) ||
	    (moxa_boards[port / MAX_PORTS_PER_BOARD].boardType == MOXA_BOARD_C320_PCI)) {
		moxafunc(ofsAddr, FC_LineStatus, 0);
		val = readw(ofsAddr + FuncArg);
	} else {
		val = readw(ofsAddr + FlagStat) >> 4;
	}
	val &= 0x0B;
	if (val & 8) {
		val |= 4;
		if ((moxa_ports[port].DCDState & DCD_oldstate) == 0)
			moxa_ports[port].DCDState = (DCD_oldstate | DCD_changed);
	} else {
		if (moxa_ports[port].DCDState & DCD_oldstate)
			moxa_ports[port].DCDState = DCD_changed;
	}
	val &= 7;
	return (val);
}

int MoxaPortDCDChange(int port)
{
	int n;

	if (moxa_ports[port].chkPort == 0)
		return (0);
	n = moxa_ports[port].DCDState;
	moxa_ports[port].DCDState &= ~DCD_changed;
	n &= DCD_changed;
	return (n);
}

int MoxaPortDCDON(int port)
{
	int n;

	if (moxa_ports[port].chkPort == 0)
		return (0);
	if (moxa_ports[port].DCDState & DCD_oldstate)
		n = 1;
	else
		n = 0;
	return (n);
}

int MoxaPortWriteData(int port, unsigned char * buffer, int len)
{
	int c, total, i;
	ushort tail;
	int cnt;
	ushort head, tx_mask, spage, epage;
	ushort pageno, pageofs, bufhead;
	void __iomem *baseAddr, *ofsAddr, *ofs;

	ofsAddr = moxa_ports[port].tableAddr;
	baseAddr = moxa_boards[port / MAX_PORTS_PER_BOARD].basemem;
	tx_mask = readw(ofsAddr + TX_mask);
	spage = readw(ofsAddr + Page_txb);
	epage = readw(ofsAddr + EndPage_txb);
	tail = readw(ofsAddr + TXwptr);
	head = readw(ofsAddr + TXrptr);
	c = (head > tail) ? (head - tail - 1)
	    : (head - tail + tx_mask);
	if (c > len)
		c = len;
	moxaLog.txcnt[port] += c;
	total = c;
	if (spage == epage) {
		bufhead = readw(ofsAddr + Ofs_txb);
		writew(spage, baseAddr + Control_reg);
		while (c > 0) {
			if (head > tail)
				len = head - tail - 1;
			else
				len = tx_mask + 1 - tail;
			len = (c > len) ? len : c;
			ofs = baseAddr + DynPage_addr + bufhead + tail;
			for (i = 0; i < len; i++)
				writeb(*buffer++, ofs + i);
			tail = (tail + len) & tx_mask;
			c -= len;
		}
		writew(tail, ofsAddr + TXwptr);
	} else {
		len = c;
		pageno = spage + (tail >> 13);
		pageofs = tail & Page_mask;
		do {
			cnt = Page_size - pageofs;
			if (cnt > c)
				cnt = c;
			c -= cnt;
			writeb(pageno, baseAddr + Control_reg);
			ofs = baseAddr + DynPage_addr + pageofs;
			for (i = 0; i < cnt; i++)
				writeb(*buffer++, ofs + i);
			if (c == 0) {
				writew((tail + len) & tx_mask, ofsAddr + TXwptr);
				break;
			}
			if (++pageno == epage)
				pageno = spage;
			pageofs = 0;
		} while (1);
	}
	writeb(1, ofsAddr + CD180TXirq);	/* start to send */
	return (total);
}

int MoxaPortReadData(int port, struct tty_struct *tty)
{
	register ushort head, pageofs;
	int i, count, cnt, len, total, remain;
	ushort tail, rx_mask, spage, epage;
	ushort pageno, bufhead;
	void __iomem *baseAddr, *ofsAddr, *ofs;

	ofsAddr = moxa_ports[port].tableAddr;
	baseAddr = moxa_boards[port / MAX_PORTS_PER_BOARD].basemem;
	head = readw(ofsAddr + RXrptr);
	tail = readw(ofsAddr + RXwptr);
	rx_mask = readw(ofsAddr + RX_mask);
	spage = readw(ofsAddr + Page_rxb);
	epage = readw(ofsAddr + EndPage_rxb);
	count = (tail >= head) ? (tail - head)
	    : (tail - head + rx_mask + 1);
	if (count == 0)
		return 0;

	total = count;
	remain = count - total;
	moxaLog.rxcnt[port] += total;
	count = total;
	if (spage == epage) {
		bufhead = readw(ofsAddr + Ofs_rxb);
		writew(spage, baseAddr + Control_reg);
		while (count > 0) {
			if (tail >= head)
				len = tail - head;
			else
				len = rx_mask + 1 - head;
			len = (count > len) ? len : count;
			ofs = baseAddr + DynPage_addr + bufhead + head;
			for (i = 0; i < len; i++)
				tty_insert_flip_char(tty, readb(ofs + i), TTY_NORMAL);
			head = (head + len) & rx_mask;
			count -= len;
		}
		writew(head, ofsAddr + RXrptr);
	} else {
		len = count;
		pageno = spage + (head >> 13);
		pageofs = head & Page_mask;
		do {
			cnt = Page_size - pageofs;
			if (cnt > count)
				cnt = count;
			count -= cnt;
			writew(pageno, baseAddr + Control_reg);
			ofs = baseAddr + DynPage_addr + pageofs;
			for (i = 0; i < cnt; i++)
				tty_insert_flip_char(tty, readb(ofs + i), TTY_NORMAL);
			if (count == 0) {
				writew((head + len) & rx_mask, ofsAddr + RXrptr);
				break;
			}
			if (++pageno == epage)
				pageno = spage;
			pageofs = 0;
		} while (1);
	}
	if ((readb(ofsAddr + FlagStat) & Xoff_state) && (remain < LowWater)) {
		moxaLowWaterChk = 1;
		moxa_ports[port].lowChkFlag = 1;
	}
	return (total);
}


int MoxaPortTxQueue(int port)
{
	void __iomem *ofsAddr;
	ushort rptr, wptr, mask;
	int len;

	ofsAddr = moxa_ports[port].tableAddr;
	rptr = readw(ofsAddr + TXrptr);
	wptr = readw(ofsAddr + TXwptr);
	mask = readw(ofsAddr + TX_mask);
	len = (wptr - rptr) & mask;
	return (len);
}

int MoxaPortTxFree(int port)
{
	void __iomem *ofsAddr;
	ushort rptr, wptr, mask;
	int len;

	ofsAddr = moxa_ports[port].tableAddr;
	rptr = readw(ofsAddr + TXrptr);
	wptr = readw(ofsAddr + TXwptr);
	mask = readw(ofsAddr + TX_mask);
	len = mask - ((wptr - rptr) & mask);
	return (len);
}

int MoxaPortRxQueue(int port)
{
	void __iomem *ofsAddr;
	ushort rptr, wptr, mask;
	int len;

	ofsAddr = moxa_ports[port].tableAddr;
	rptr = readw(ofsAddr + RXrptr);
	wptr = readw(ofsAddr + RXwptr);
	mask = readw(ofsAddr + RX_mask);
	len = (wptr - rptr) & mask;
	return (len);
}


void MoxaPortTxDisable(int port)
{
	void __iomem *ofsAddr;

	ofsAddr = moxa_ports[port].tableAddr;
	moxafunc(ofsAddr, FC_SetXoffState, Magic_code);
}

void MoxaPortTxEnable(int port)
{
	void __iomem *ofsAddr;

	ofsAddr = moxa_ports[port].tableAddr;
	moxafunc(ofsAddr, FC_SetXonState, Magic_code);
}


int MoxaPortResetBrkCnt(int port)
{
	ushort cnt;
	cnt = moxa_ports[port].breakCnt;
	moxa_ports[port].breakCnt = 0;
	return (cnt);
}


void MoxaPortSendBreak(int port, int ms100)
{
	void __iomem *ofsAddr;

	ofsAddr = moxa_ports[port].tableAddr;
	if (ms100) {
		moxafunc(ofsAddr, FC_SendBreak, Magic_code);
		msleep(ms100 * 10);
	} else {
		moxafunc(ofsAddr, FC_SendBreak, Magic_code);
		msleep(250);
	}
	moxafunc(ofsAddr, FC_StopBreak, Magic_code);
}

static int moxa_get_serial_info(struct moxa_port *info,
				struct serial_struct __user *retinfo)
{
	struct serial_struct tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.type = info->type;
	tmp.line = info->port;
	tmp.port = 0;
	tmp.irq = 0;
	tmp.flags = info->asyncflags;
	tmp.baud_base = 921600;
	tmp.close_delay = info->close_delay;
	tmp.closing_wait = info->closing_wait;
	tmp.custom_divisor = 0;
	tmp.hub6 = 0;
	if(copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return (0);
}


static int moxa_set_serial_info(struct moxa_port *info,
				struct serial_struct __user *new_info)
{
	struct serial_struct new_serial;

	if(copy_from_user(&new_serial, new_info, sizeof(new_serial)))
		return -EFAULT;

	if ((new_serial.irq != 0) ||
	    (new_serial.port != 0) ||
//           (new_serial.type != info->type) ||
	    (new_serial.custom_divisor != 0) ||
	    (new_serial.baud_base != 921600))
		return (-EPERM);

	if (!capable(CAP_SYS_ADMIN)) {
		if (((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (info->asyncflags & ~ASYNC_USR_MASK)))
			return (-EPERM);
	} else {
		info->close_delay = new_serial.close_delay * HZ / 100;
		info->closing_wait = new_serial.closing_wait * HZ / 100;
	}

	new_serial.flags = (new_serial.flags & ~ASYNC_FLAGS);
	new_serial.flags |= (info->asyncflags & ASYNC_FLAGS);

	if (new_serial.type == PORT_16550A) {
		MoxaSetFifo(info->port, 1);
	} else {
		MoxaSetFifo(info->port, 0);
	}

	info->type = new_serial.type;
	return (0);
}



/*****************************************************************************
 *	Static local functions: 					     *
 *****************************************************************************/
static void moxafunc(void __iomem *ofsAddr, int cmd, ushort arg)
{

	writew(arg, ofsAddr + FuncArg);
	writew(cmd, ofsAddr + FuncCode);
	moxa_wait_finish(ofsAddr);
}

static void moxa_wait_finish(void __iomem *ofsAddr)
{
	unsigned long i, j;

	i = jiffies;
	while (readw(ofsAddr + FuncCode) != 0) {
		j = jiffies;
		if ((j - i) > moxaFuncTout) {
			return;
		}
	}
}

static void moxa_low_water_check(void __iomem *ofsAddr)
{
	int len;
	ushort rptr, wptr, mask;

	if (readb(ofsAddr + FlagStat) & Xoff_state) {
		rptr = readw(ofsAddr + RXrptr);
		wptr = readw(ofsAddr + RXwptr);
		mask = readw(ofsAddr + RX_mask);
		len = (wptr - rptr) & mask;
		if (len <= Low_water)
			moxafunc(ofsAddr, FC_SendXon, 0);
	}
}

static int moxaloadbios(int cardno, unsigned char __user *tmp, int len)
{
	void __iomem *baseAddr;
	int i;

	if(len < 0 || len > sizeof(moxaBuff))
		return -EINVAL;
	if(copy_from_user(moxaBuff, tmp, len))
		return -EFAULT;
	baseAddr = moxa_boards[cardno].basemem;
	writeb(HW_reset, baseAddr + Control_reg);	/* reset */
	msleep(10);
	for (i = 0; i < 4096; i++)
		writeb(0, baseAddr + i);	/* clear fix page */
	for (i = 0; i < len; i++)
		writeb(moxaBuff[i], baseAddr + i);	/* download BIOS */
	writeb(0, baseAddr + Control_reg);	/* restart */
	return (0);
}

static int moxafindcard(int cardno)
{
	void __iomem *baseAddr;
	ushort tmp;

	baseAddr = moxa_boards[cardno].basemem;
	switch (moxa_boards[cardno].boardType) {
	case MOXA_BOARD_C218_ISA:
	case MOXA_BOARD_C218_PCI:
		if ((tmp = readw(baseAddr + C218_key)) != C218_KeyCode) {
			return (-1);
		}
		break;
	case MOXA_BOARD_CP204J:
		if ((tmp = readw(baseAddr + C218_key)) != CP204J_KeyCode) {
			return (-1);
		}
		break;
	default:
		if ((tmp = readw(baseAddr + C320_key)) != C320_KeyCode) {
			return (-1);
		}
		if ((tmp = readw(baseAddr + C320_status)) != STS_init) {
			return (-2);
		}
	}
	return (0);
}

static int moxaload320b(int cardno, unsigned char __user *tmp, int len)
{
	void __iomem *baseAddr;
	int i;

	if(len < 0 || len > sizeof(moxaBuff))
		return -EINVAL;
	if(copy_from_user(moxaBuff, tmp, len))
		return -EFAULT;
	baseAddr = moxa_boards[cardno].basemem;
	writew(len - 7168 - 2, baseAddr + C320bapi_len);
	writeb(1, baseAddr + Control_reg);	/* Select Page 1 */
	for (i = 0; i < 7168; i++)
		writeb(moxaBuff[i], baseAddr + DynPage_addr + i);
	writeb(2, baseAddr + Control_reg);	/* Select Page 2 */
	for (i = 0; i < (len - 7168); i++)
		writeb(moxaBuff[i + 7168], baseAddr + DynPage_addr + i);
	return (0);
}

static int moxaloadcode(int cardno, unsigned char __user *tmp, int len)
{
	void __iomem *baseAddr, *ofsAddr;
	int retval, port, i;

	if(len < 0 || len > sizeof(moxaBuff))
		return -EINVAL;
	if(copy_from_user(moxaBuff, tmp, len))
		return -EFAULT;
	baseAddr = moxa_boards[cardno].basemem;
	switch (moxa_boards[cardno].boardType) {
	case MOXA_BOARD_C218_ISA:
	case MOXA_BOARD_C218_PCI:
	case MOXA_BOARD_CP204J:
		retval = moxaloadc218(cardno, baseAddr, len);
		if (retval)
			return (retval);
		port = cardno * MAX_PORTS_PER_BOARD;
		for (i = 0; i < moxa_boards[cardno].numPorts; i++, port++) {
			struct moxa_port *p = &moxa_ports[port];

			p->chkPort = 1;
			p->curBaud = 9600L;
			p->DCDState = 0;
			p->tableAddr = baseAddr + Extern_table + Extern_size * i;
			ofsAddr = p->tableAddr;
			writew(C218rx_mask, ofsAddr + RX_mask);
			writew(C218tx_mask, ofsAddr + TX_mask);
			writew(C218rx_spage + i * C218buf_pageno, ofsAddr + Page_rxb);
			writew(readw(ofsAddr + Page_rxb) + C218rx_pageno, ofsAddr + EndPage_rxb);

			writew(C218tx_spage + i * C218buf_pageno, ofsAddr + Page_txb);
			writew(readw(ofsAddr + Page_txb) + C218tx_pageno, ofsAddr + EndPage_txb);

		}
		break;
	default:
		retval = moxaloadc320(cardno, baseAddr, len,
				      &moxa_boards[cardno].numPorts);
		if (retval)
			return (retval);
		port = cardno * MAX_PORTS_PER_BOARD;
		for (i = 0; i < moxa_boards[cardno].numPorts; i++, port++) {
			struct moxa_port *p = &moxa_ports[port];

			p->chkPort = 1;
			p->curBaud = 9600L;
			p->DCDState = 0;
			p->tableAddr = baseAddr + Extern_table + Extern_size * i;
			ofsAddr = p->tableAddr;
			if (moxa_boards[cardno].numPorts == 8) {
				writew(C320p8rx_mask, ofsAddr + RX_mask);
				writew(C320p8tx_mask, ofsAddr + TX_mask);
				writew(C320p8rx_spage + i * C320p8buf_pgno, ofsAddr + Page_rxb);
				writew(readw(ofsAddr + Page_rxb) + C320p8rx_pgno, ofsAddr + EndPage_rxb);
				writew(C320p8tx_spage + i * C320p8buf_pgno, ofsAddr + Page_txb);
				writew(readw(ofsAddr + Page_txb) + C320p8tx_pgno, ofsAddr + EndPage_txb);

			} else if (moxa_boards[cardno].numPorts == 16) {
				writew(C320p16rx_mask, ofsAddr + RX_mask);
				writew(C320p16tx_mask, ofsAddr + TX_mask);
				writew(C320p16rx_spage + i * C320p16buf_pgno, ofsAddr + Page_rxb);
				writew(readw(ofsAddr + Page_rxb) + C320p16rx_pgno, ofsAddr + EndPage_rxb);
				writew(C320p16tx_spage + i * C320p16buf_pgno, ofsAddr + Page_txb);
				writew(readw(ofsAddr + Page_txb) + C320p16tx_pgno, ofsAddr + EndPage_txb);

			} else if (moxa_boards[cardno].numPorts == 24) {
				writew(C320p24rx_mask, ofsAddr + RX_mask);
				writew(C320p24tx_mask, ofsAddr + TX_mask);
				writew(C320p24rx_spage + i * C320p24buf_pgno, ofsAddr + Page_rxb);
				writew(readw(ofsAddr + Page_rxb) + C320p24rx_pgno, ofsAddr + EndPage_rxb);
				writew(C320p24tx_spage + i * C320p24buf_pgno, ofsAddr + Page_txb);
				writew(readw(ofsAddr + Page_txb), ofsAddr + EndPage_txb);
			} else if (moxa_boards[cardno].numPorts == 32) {
				writew(C320p32rx_mask, ofsAddr + RX_mask);
				writew(C320p32tx_mask, ofsAddr + TX_mask);
				writew(C320p32tx_ofs, ofsAddr + Ofs_txb);
				writew(C320p32rx_spage + i * C320p32buf_pgno, ofsAddr + Page_rxb);
				writew(readb(ofsAddr + Page_rxb), ofsAddr + EndPage_rxb);
				writew(C320p32tx_spage + i * C320p32buf_pgno, ofsAddr + Page_txb);
				writew(readw(ofsAddr + Page_txb), ofsAddr + EndPage_txb);
			}
		}
		break;
	}
	moxa_boards[cardno].loadstat = 1;
	return (0);
}

static int moxaloadc218(int cardno, void __iomem *baseAddr, int len)
{
	char retry;
	int i, j, len1, len2;
	ushort usum, *ptr, keycode;

	if (moxa_boards[cardno].boardType == MOXA_BOARD_CP204J)
		keycode = CP204J_KeyCode;
	else
		keycode = C218_KeyCode;
	usum = 0;
	len1 = len >> 1;
	ptr = (ushort *) moxaBuff;
	for (i = 0; i < len1; i++)
		usum += le16_to_cpu(*(ptr + i));
	retry = 0;
	do {
		len1 = len >> 1;
		j = 0;
		while (len1) {
			len2 = (len1 > 2048) ? 2048 : len1;
			len1 -= len2;
			for (i = 0; i < len2 << 1; i++)
				writeb(moxaBuff[i + j], baseAddr + C218_LoadBuf + i);
			j += i;

			writew(len2, baseAddr + C218DLoad_len);
			writew(0, baseAddr + C218_key);
			for (i = 0; i < 100; i++) {
				if (readw(baseAddr + C218_key) == keycode)
					break;
				msleep(10);
			}
			if (readw(baseAddr + C218_key) != keycode) {
				return (-1);
			}
		}
		writew(0, baseAddr + C218DLoad_len);
		writew(usum, baseAddr + C218check_sum);
		writew(0, baseAddr + C218_key);
		for (i = 0; i < 100; i++) {
			if (readw(baseAddr + C218_key) == keycode)
				break;
			msleep(10);
		}
		retry++;
	} while ((readb(baseAddr + C218chksum_ok) != 1) && (retry < 3));
	if (readb(baseAddr + C218chksum_ok) != 1) {
		return (-1);
	}
	writew(0, baseAddr + C218_key);
	for (i = 0; i < 100; i++) {
		if (readw(baseAddr + Magic_no) == Magic_code)
			break;
		msleep(10);
	}
	if (readw(baseAddr + Magic_no) != Magic_code) {
		return (-1);
	}
	writew(1, baseAddr + Disable_IRQ);
	writew(0, baseAddr + Magic_no);
	for (i = 0; i < 100; i++) {
		if (readw(baseAddr + Magic_no) == Magic_code)
			break;
		msleep(10);
	}
	if (readw(baseAddr + Magic_no) != Magic_code) {
		return (-1);
	}
	moxaCard = 1;
	moxa_boards[cardno].intNdx = baseAddr + IRQindex;
	moxa_boards[cardno].intPend = baseAddr + IRQpending;
	moxa_boards[cardno].intTable = baseAddr + IRQtable;
	return (0);
}

static int moxaloadc320(int cardno, void __iomem *baseAddr, int len, int *numPorts)
{
	ushort usum;
	int i, j, wlen, len2, retry;
	ushort *uptr;

	usum = 0;
	wlen = len >> 1;
	uptr = (ushort *) moxaBuff;
	for (i = 0; i < wlen; i++)
		usum += le16_to_cpu(uptr[i]);
	retry = 0;
	j = 0;
	do {
		while (wlen) {
			if (wlen > 2048)
				len2 = 2048;
			else
				len2 = wlen;
			wlen -= len2;
			len2 <<= 1;
			for (i = 0; i < len2; i++)
				writeb(moxaBuff[j + i], baseAddr + C320_LoadBuf + i);
			len2 >>= 1;
			j += i;
			writew(len2, baseAddr + C320DLoad_len);
			writew(0, baseAddr + C320_key);
			for (i = 0; i < 10; i++) {
				if (readw(baseAddr + C320_key) == C320_KeyCode)
					break;
				msleep(10);
			}
			if (readw(baseAddr + C320_key) != C320_KeyCode)
				return (-1);
		}
		writew(0, baseAddr + C320DLoad_len);
		writew(usum, baseAddr + C320check_sum);
		writew(0, baseAddr + C320_key);
		for (i = 0; i < 10; i++) {
			if (readw(baseAddr + C320_key) == C320_KeyCode)
				break;
			msleep(10);
		}
		retry++;
	} while ((readb(baseAddr + C320chksum_ok) != 1) && (retry < 3));
	if (readb(baseAddr + C320chksum_ok) != 1)
		return (-1);
	writew(0, baseAddr + C320_key);
	for (i = 0; i < 600; i++) {
		if (readw(baseAddr + Magic_no) == Magic_code)
			break;
		msleep(10);
	}
	if (readw(baseAddr + Magic_no) != Magic_code)
		return (-100);

	if (moxa_boards[cardno].busType == MOXA_BUS_TYPE_PCI) {		/* ASIC board */
		writew(0x3800, baseAddr + TMS320_PORT1);
		writew(0x3900, baseAddr + TMS320_PORT2);
		writew(28499, baseAddr + TMS320_CLOCK);
	} else {
		writew(0x3200, baseAddr + TMS320_PORT1);
		writew(0x3400, baseAddr + TMS320_PORT2);
		writew(19999, baseAddr + TMS320_CLOCK);
	}
	writew(1, baseAddr + Disable_IRQ);
	writew(0, baseAddr + Magic_no);
	for (i = 0; i < 500; i++) {
		if (readw(baseAddr + Magic_no) == Magic_code)
			break;
		msleep(10);
	}
	if (readw(baseAddr + Magic_no) != Magic_code)
		return (-102);

	j = readw(baseAddr + Module_cnt);
	if (j <= 0)
		return (-101);
	*numPorts = j * 8;
	writew(j, baseAddr + Module_no);
	writew(0, baseAddr + Magic_no);
	for (i = 0; i < 600; i++) {
		if (readw(baseAddr + Magic_no) == Magic_code)
			break;
		msleep(10);
	}
	if (readw(baseAddr + Magic_no) != Magic_code)
		return (-102);
	moxaCard = 1;
	moxa_boards[cardno].intNdx = baseAddr + IRQindex;
	moxa_boards[cardno].intPend = baseAddr + IRQpending;
	moxa_boards[cardno].intTable = baseAddr + IRQtable;
	return (0);
}

static void MoxaSetFifo(int port, int enable)
{
	void __iomem *ofsAddr = moxa_ports[port].tableAddr;

	if (!enable) {
		moxafunc(ofsAddr, FC_SetRxFIFOTrig, 0);
		moxafunc(ofsAddr, FC_SetTxFIFOCnt, 1);
	} else {
		moxafunc(ofsAddr, FC_SetRxFIFOTrig, 3);
		moxafunc(ofsAddr, FC_SetTxFIFOCnt, 16);
	}
}
