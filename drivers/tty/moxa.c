/*****************************************************************************/
/*
 *           moxa.c  -- MOXA Intellio family multiport serial driver.
 *
 *      Copyright (C) 1999-2000  Moxa Technologies (support@moxa.com).
 *      Copyright (c) 2007 Jiri Slaby <jirislaby@gmail.com>
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
#include <linux/firmware.h>
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
#include <linux/slab.h>
#include <linux/ratelimit.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include "moxa.h"

#define MOXA_VERSION		"6.0k"

#define MOXA_FW_HDRLEN		32

#define MOXAMAJOR		172

#define MAX_BOARDS		4	/* Don't change this value */
#define MAX_PORTS_PER_BOARD	32	/* Don't change this value */
#define MAX_PORTS		(MAX_BOARDS * MAX_PORTS_PER_BOARD)

#define MOXA_IS_320(brd) ((brd)->boardType == MOXA_BOARD_C320_ISA || \
		(brd)->boardType == MOXA_BOARD_C320_PCI)

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

struct moxa_port;

static struct moxa_board_conf {
	int boardType;
	int numPorts;
	int busType;

	unsigned int ready;

	struct moxa_port *ports;

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
	struct tty_port port;
	struct moxa_board_conf *board;
	void __iomem *tableAddr;

	int type;
	int cflag;
	unsigned long statusflags;

	u8 DCDState;		/* Protected by the port lock */
	u8 lineCtrl;
	u8 lowChkFlag;
};

struct mon_str {
	int tick;
	int rxcnt[MAX_PORTS];
	int txcnt[MAX_PORTS];
};

/* statusflags */
#define TXSTOPPED	1
#define LOWWAIT 	2
#define EMPTYWAIT	3

#define SERIAL_DO_RESTART

#define WAKEUP_CHARS		256

static int ttymajor = MOXAMAJOR;
static struct mon_str moxaLog;
static unsigned int moxaFuncTout = HZ / 2;
static unsigned int moxaLowWaterChk;
static DEFINE_MUTEX(moxa_openlock);
static DEFINE_SPINLOCK(moxa_lock);

static unsigned long baseaddr[MAX_BOARDS];
static unsigned int type[MAX_BOARDS];
static unsigned int numports[MAX_BOARDS];

MODULE_AUTHOR("William Chen");
MODULE_DESCRIPTION("MOXA Intellio Family Multiport Board Device Driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("c218tunx.cod");
MODULE_FIRMWARE("cp204unx.cod");
MODULE_FIRMWARE("c320tunx.cod");

module_param_array(type, uint, NULL, 0);
MODULE_PARM_DESC(type, "card type: C218=2, C320=4");
module_param_array(baseaddr, ulong, NULL, 0);
MODULE_PARM_DESC(baseaddr, "base address");
module_param_array(numports, uint, NULL, 0);
MODULE_PARM_DESC(numports, "numports (ignored for C218)");

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
static void moxa_set_termios(struct tty_struct *, struct ktermios *);
static void moxa_stop(struct tty_struct *);
static void moxa_start(struct tty_struct *);
static void moxa_hangup(struct tty_struct *);
static int moxa_tiocmget(struct tty_struct *tty);
static int moxa_tiocmset(struct tty_struct *tty,
			 unsigned int set, unsigned int clear);
static void moxa_poll(unsigned long);
static void moxa_set_tty_param(struct tty_struct *, struct ktermios *);
static void moxa_shutdown(struct tty_port *);
static int moxa_carrier_raised(struct tty_port *);
static void moxa_dtr_rts(struct tty_port *, int);
/*
 * moxa board interface functions:
 */
static void MoxaPortEnable(struct moxa_port *);
static void MoxaPortDisable(struct moxa_port *);
static int MoxaPortSetTermio(struct moxa_port *, struct ktermios *, speed_t);
static int MoxaPortGetLineOut(struct moxa_port *, int *, int *);
static void MoxaPortLineCtrl(struct moxa_port *, int, int);
static void MoxaPortFlowCtrl(struct moxa_port *, int, int, int, int, int);
static int MoxaPortLineStatus(struct moxa_port *);
static void MoxaPortFlushData(struct moxa_port *, int);
static int MoxaPortWriteData(struct tty_struct *, const unsigned char *, int);
static int MoxaPortReadData(struct moxa_port *);
static int MoxaPortTxQueue(struct moxa_port *);
static int MoxaPortRxQueue(struct moxa_port *);
static int MoxaPortTxFree(struct moxa_port *);
static void MoxaPortTxDisable(struct moxa_port *);
static void MoxaPortTxEnable(struct moxa_port *);
static int moxa_get_serial_info(struct moxa_port *, struct serial_struct __user *);
static int moxa_set_serial_info(struct moxa_port *, struct serial_struct __user *);
static void MoxaSetFifo(struct moxa_port *port, int enable);

/*
 * I/O functions
 */

static DEFINE_SPINLOCK(moxafunc_lock);

static void moxa_wait_finish(void __iomem *ofsAddr)
{
	unsigned long end = jiffies + moxaFuncTout;

	while (readw(ofsAddr + FuncCode) != 0)
		if (time_after(jiffies, end))
			return;
	if (readw(ofsAddr + FuncCode) != 0)
		printk_ratelimited(KERN_WARNING "moxa function expired\n");
}

static void moxafunc(void __iomem *ofsAddr, u16 cmd, u16 arg)
{
        unsigned long flags;
        spin_lock_irqsave(&moxafunc_lock, flags);
	writew(arg, ofsAddr + FuncArg);
	writew(cmd, ofsAddr + FuncCode);
	moxa_wait_finish(ofsAddr);
	spin_unlock_irqrestore(&moxafunc_lock, flags);
}

static int moxafuncret(void __iomem *ofsAddr, u16 cmd, u16 arg)
{
        unsigned long flags;
        u16 ret;
        spin_lock_irqsave(&moxafunc_lock, flags);
	writew(arg, ofsAddr + FuncArg);
	writew(cmd, ofsAddr + FuncCode);
	moxa_wait_finish(ofsAddr);
	ret = readw(ofsAddr + FuncArg);
	spin_unlock_irqrestore(&moxafunc_lock, flags);
	return ret;
}

static void moxa_low_water_check(void __iomem *ofsAddr)
{
	u16 rptr, wptr, mask, len;

	if (readb(ofsAddr + FlagStat) & Xoff_state) {
		rptr = readw(ofsAddr + RXrptr);
		wptr = readw(ofsAddr + RXwptr);
		mask = readw(ofsAddr + RX_mask);
		len = (wptr - rptr) & mask;
		if (len <= Low_water)
			moxafunc(ofsAddr, FC_SendXon, 0);
	}
}

/*
 * TTY operations
 */

static int moxa_ioctl(struct tty_struct *tty,
		      unsigned int cmd, unsigned long arg)
{
	struct moxa_port *ch = tty->driver_data;
	void __user *argp = (void __user *)arg;
	int status, ret = 0;

	if (tty->index == MAX_PORTS) {
		if (cmd != MOXA_GETDATACOUNT && cmd != MOXA_GET_IOQUEUE &&
				cmd != MOXA_GETMSTATUS)
			return -EINVAL;
	} else if (!ch)
		return -ENODEV;

	switch (cmd) {
	case MOXA_GETDATACOUNT:
		moxaLog.tick = jiffies;
		if (copy_to_user(argp, &moxaLog, sizeof(moxaLog)))
			ret = -EFAULT;
		break;
	case MOXA_FLUSH_QUEUE:
		MoxaPortFlushData(ch, arg);
		break;
	case MOXA_GET_IOQUEUE: {
		struct moxaq_str __user *argm = argp;
		struct moxaq_str tmp;
		struct moxa_port *p;
		unsigned int i, j;

		for (i = 0; i < MAX_BOARDS; i++) {
			p = moxa_boards[i].ports;
			for (j = 0; j < MAX_PORTS_PER_BOARD; j++, p++, argm++) {
				memset(&tmp, 0, sizeof(tmp));
				spin_lock_bh(&moxa_lock);
				if (moxa_boards[i].ready) {
					tmp.inq = MoxaPortRxQueue(p);
					tmp.outq = MoxaPortTxQueue(p);
				}
				spin_unlock_bh(&moxa_lock);
				if (copy_to_user(argm, &tmp, sizeof(tmp)))
					return -EFAULT;
			}
		}
		break;
	} case MOXA_GET_OQUEUE:
		status = MoxaPortTxQueue(ch);
		ret = put_user(status, (unsigned long __user *)argp);
		break;
	case MOXA_GET_IQUEUE:
		status = MoxaPortRxQueue(ch);
		ret = put_user(status, (unsigned long __user *)argp);
		break;
	case MOXA_GETMSTATUS: {
		struct mxser_mstatus __user *argm = argp;
		struct mxser_mstatus tmp;
		struct moxa_port *p;
		unsigned int i, j;

		for (i = 0; i < MAX_BOARDS; i++) {
			p = moxa_boards[i].ports;
			for (j = 0; j < MAX_PORTS_PER_BOARD; j++, p++, argm++) {
				struct tty_struct *ttyp;
				memset(&tmp, 0, sizeof(tmp));
				spin_lock_bh(&moxa_lock);
				if (!moxa_boards[i].ready) {
				        spin_unlock_bh(&moxa_lock);
					goto copy;
                                }

				status = MoxaPortLineStatus(p);
				spin_unlock_bh(&moxa_lock);

				if (status & 1)
					tmp.cts = 1;
				if (status & 2)
					tmp.dsr = 1;
				if (status & 4)
					tmp.dcd = 1;

				ttyp = tty_port_tty_get(&p->port);
				if (!ttyp || !ttyp->termios)
					tmp.cflag = p->cflag;
				else
					tmp.cflag = ttyp->termios->c_cflag;
				tty_kref_put(ttyp);
copy:
				if (copy_to_user(argm, &tmp, sizeof(tmp)))
					return -EFAULT;
			}
		}
		break;
	}
	case TIOCGSERIAL:
	        mutex_lock(&ch->port.mutex);
		ret = moxa_get_serial_info(ch, argp);
		mutex_unlock(&ch->port.mutex);
		break;
	case TIOCSSERIAL:
	        mutex_lock(&ch->port.mutex);
		ret = moxa_set_serial_info(ch, argp);
		mutex_unlock(&ch->port.mutex);
		break;
	default:
		ret = -ENOIOCTLCMD;
	}
	return ret;
}

static int moxa_break_ctl(struct tty_struct *tty, int state)
{
	struct moxa_port *port = tty->driver_data;

	moxafunc(port->tableAddr, state ? FC_SendBreak : FC_StopBreak,
			Magic_code);
	return 0;
}

static const struct tty_operations moxa_ops = {
	.open = moxa_open,
	.close = moxa_close,
	.write = moxa_write,
	.write_room = moxa_write_room,
	.flush_buffer = moxa_flush_buffer,
	.chars_in_buffer = moxa_chars_in_buffer,
	.ioctl = moxa_ioctl,
	.set_termios = moxa_set_termios,
	.stop = moxa_stop,
	.start = moxa_start,
	.hangup = moxa_hangup,
	.break_ctl = moxa_break_ctl,
	.tiocmget = moxa_tiocmget,
	.tiocmset = moxa_tiocmset,
};

static const struct tty_port_operations moxa_port_ops = {
	.carrier_raised = moxa_carrier_raised,
	.dtr_rts = moxa_dtr_rts,
	.shutdown = moxa_shutdown,
};

static struct tty_driver *moxaDriver;
static DEFINE_TIMER(moxaTimer, moxa_poll, 0, 0);

/*
 * HW init
 */

static int moxa_check_fw_model(struct moxa_board_conf *brd, u8 model)
{
	switch (brd->boardType) {
	case MOXA_BOARD_C218_ISA:
	case MOXA_BOARD_C218_PCI:
		if (model != 1)
			goto err;
		break;
	case MOXA_BOARD_CP204J:
		if (model != 3)
			goto err;
		break;
	default:
		if (model != 2)
			goto err;
		break;
	}
	return 0;
err:
	return -EINVAL;
}

static int moxa_check_fw(const void *ptr)
{
	const __le16 *lptr = ptr;

	if (*lptr != cpu_to_le16(0x7980))
		return -EINVAL;

	return 0;
}

static int moxa_load_bios(struct moxa_board_conf *brd, const u8 *buf,
		size_t len)
{
	void __iomem *baseAddr = brd->basemem;
	u16 tmp;

	writeb(HW_reset, baseAddr + Control_reg);	/* reset */
	msleep(10);
	memset_io(baseAddr, 0, 4096);
	memcpy_toio(baseAddr, buf, len);	/* download BIOS */
	writeb(0, baseAddr + Control_reg);	/* restart */

	msleep(2000);

	switch (brd->boardType) {
	case MOXA_BOARD_C218_ISA:
	case MOXA_BOARD_C218_PCI:
		tmp = readw(baseAddr + C218_key);
		if (tmp != C218_KeyCode)
			goto err;
		break;
	case MOXA_BOARD_CP204J:
		tmp = readw(baseAddr + C218_key);
		if (tmp != CP204J_KeyCode)
			goto err;
		break;
	default:
		tmp = readw(baseAddr + C320_key);
		if (tmp != C320_KeyCode)
			goto err;
		tmp = readw(baseAddr + C320_status);
		if (tmp != STS_init) {
			printk(KERN_ERR "MOXA: bios upload failed -- CPU/Basic "
					"module not found\n");
			return -EIO;
		}
		break;
	}

	return 0;
err:
	printk(KERN_ERR "MOXA: bios upload failed -- board not found\n");
	return -EIO;
}

static int moxa_load_320b(struct moxa_board_conf *brd, const u8 *ptr,
		size_t len)
{
	void __iomem *baseAddr = brd->basemem;

	if (len < 7168) {
		printk(KERN_ERR "MOXA: invalid 320 bios -- too short\n");
		return -EINVAL;
	}

	writew(len - 7168 - 2, baseAddr + C320bapi_len);
	writeb(1, baseAddr + Control_reg);	/* Select Page 1 */
	memcpy_toio(baseAddr + DynPage_addr, ptr, 7168);
	writeb(2, baseAddr + Control_reg);	/* Select Page 2 */
	memcpy_toio(baseAddr + DynPage_addr, ptr + 7168, len - 7168);

	return 0;
}

static int moxa_real_load_code(struct moxa_board_conf *brd, const void *ptr,
		size_t len)
{
	void __iomem *baseAddr = brd->basemem;
	const __le16 *uptr = ptr;
	size_t wlen, len2, j;
	unsigned long key, loadbuf, loadlen, checksum, checksum_ok;
	unsigned int i, retry;
	u16 usum, keycode;

	keycode = (brd->boardType == MOXA_BOARD_CP204J) ? CP204J_KeyCode :
				C218_KeyCode;

	switch (brd->boardType) {
	case MOXA_BOARD_CP204J:
	case MOXA_BOARD_C218_ISA:
	case MOXA_BOARD_C218_PCI:
		key = C218_key;
		loadbuf = C218_LoadBuf;
		loadlen = C218DLoad_len;
		checksum = C218check_sum;
		checksum_ok = C218chksum_ok;
		break;
	default:
		key = C320_key;
		keycode = C320_KeyCode;
		loadbuf = C320_LoadBuf;
		loadlen = C320DLoad_len;
		checksum = C320check_sum;
		checksum_ok = C320chksum_ok;
		break;
	}

	usum = 0;
	wlen = len >> 1;
	for (i = 0; i < wlen; i++)
		usum += le16_to_cpu(uptr[i]);
	retry = 0;
	do {
		wlen = len >> 1;
		j = 0;
		while (wlen) {
			len2 = (wlen > 2048) ? 2048 : wlen;
			wlen -= len2;
			memcpy_toio(baseAddr + loadbuf, ptr + j, len2 << 1);
			j += len2 << 1;

			writew(len2, baseAddr + loadlen);
			writew(0, baseAddr + key);
			for (i = 0; i < 100; i++) {
				if (readw(baseAddr + key) == keycode)
					break;
				msleep(10);
			}
			if (readw(baseAddr + key) != keycode)
				return -EIO;
		}
		writew(0, baseAddr + loadlen);
		writew(usum, baseAddr + checksum);
		writew(0, baseAddr + key);
		for (i = 0; i < 100; i++) {
			if (readw(baseAddr + key) == keycode)
				break;
			msleep(10);
		}
		retry++;
	} while ((readb(baseAddr + checksum_ok) != 1) && (retry < 3));
	if (readb(baseAddr + checksum_ok) != 1)
		return -EIO;

	writew(0, baseAddr + key);
	for (i = 0; i < 600; i++) {
		if (readw(baseAddr + Magic_no) == Magic_code)
			break;
		msleep(10);
	}
	if (readw(baseAddr + Magic_no) != Magic_code)
		return -EIO;

	if (MOXA_IS_320(brd)) {
		if (brd->busType == MOXA_BUS_TYPE_PCI) {	/* ASIC board */
			writew(0x3800, baseAddr + TMS320_PORT1);
			writew(0x3900, baseAddr + TMS320_PORT2);
			writew(28499, baseAddr + TMS320_CLOCK);
		} else {
			writew(0x3200, baseAddr + TMS320_PORT1);
			writew(0x3400, baseAddr + TMS320_PORT2);
			writew(19999, baseAddr + TMS320_CLOCK);
		}
	}
	writew(1, baseAddr + Disable_IRQ);
	writew(0, baseAddr + Magic_no);
	for (i = 0; i < 500; i++) {
		if (readw(baseAddr + Magic_no) == Magic_code)
			break;
		msleep(10);
	}
	if (readw(baseAddr + Magic_no) != Magic_code)
		return -EIO;

	if (MOXA_IS_320(brd)) {
		j = readw(baseAddr + Module_cnt);
		if (j <= 0)
			return -EIO;
		brd->numPorts = j * 8;
		writew(j, baseAddr + Module_no);
		writew(0, baseAddr + Magic_no);
		for (i = 0; i < 600; i++) {
			if (readw(baseAddr + Magic_no) == Magic_code)
				break;
			msleep(10);
		}
		if (readw(baseAddr + Magic_no) != Magic_code)
			return -EIO;
	}
	brd->intNdx = baseAddr + IRQindex;
	brd->intPend = baseAddr + IRQpending;
	brd->intTable = baseAddr + IRQtable;

	return 0;
}

static int moxa_load_code(struct moxa_board_conf *brd, const void *ptr,
		size_t len)
{
	void __iomem *ofsAddr, *baseAddr = brd->basemem;
	struct moxa_port *port;
	int retval, i;

	if (len % 2) {
		printk(KERN_ERR "MOXA: bios length is not even\n");
		return -EINVAL;
	}

	retval = moxa_real_load_code(brd, ptr, len); /* may change numPorts */
	if (retval)
		return retval;

	switch (brd->boardType) {
	case MOXA_BOARD_C218_ISA:
	case MOXA_BOARD_C218_PCI:
	case MOXA_BOARD_CP204J:
		port = brd->ports;
		for (i = 0; i < brd->numPorts; i++, port++) {
			port->board = brd;
			port->DCDState = 0;
			port->tableAddr = baseAddr + Extern_table +
					Extern_size * i;
			ofsAddr = port->tableAddr;
			writew(C218rx_mask, ofsAddr + RX_mask);
			writew(C218tx_mask, ofsAddr + TX_mask);
			writew(C218rx_spage + i * C218buf_pageno, ofsAddr + Page_rxb);
			writew(readw(ofsAddr + Page_rxb) + C218rx_pageno, ofsAddr + EndPage_rxb);

			writew(C218tx_spage + i * C218buf_pageno, ofsAddr + Page_txb);
			writew(readw(ofsAddr + Page_txb) + C218tx_pageno, ofsAddr + EndPage_txb);

		}
		break;
	default:
		port = brd->ports;
		for (i = 0; i < brd->numPorts; i++, port++) {
			port->board = brd;
			port->DCDState = 0;
			port->tableAddr = baseAddr + Extern_table +
					Extern_size * i;
			ofsAddr = port->tableAddr;
			switch (brd->numPorts) {
			case 8:
				writew(C320p8rx_mask, ofsAddr + RX_mask);
				writew(C320p8tx_mask, ofsAddr + TX_mask);
				writew(C320p8rx_spage + i * C320p8buf_pgno, ofsAddr + Page_rxb);
				writew(readw(ofsAddr + Page_rxb) + C320p8rx_pgno, ofsAddr + EndPage_rxb);
				writew(C320p8tx_spage + i * C320p8buf_pgno, ofsAddr + Page_txb);
				writew(readw(ofsAddr + Page_txb) + C320p8tx_pgno, ofsAddr + EndPage_txb);

				break;
			case 16:
				writew(C320p16rx_mask, ofsAddr + RX_mask);
				writew(C320p16tx_mask, ofsAddr + TX_mask);
				writew(C320p16rx_spage + i * C320p16buf_pgno, ofsAddr + Page_rxb);
				writew(readw(ofsAddr + Page_rxb) + C320p16rx_pgno, ofsAddr + EndPage_rxb);
				writew(C320p16tx_spage + i * C320p16buf_pgno, ofsAddr + Page_txb);
				writew(readw(ofsAddr + Page_txb) + C320p16tx_pgno, ofsAddr + EndPage_txb);
				break;

			case 24:
				writew(C320p24rx_mask, ofsAddr + RX_mask);
				writew(C320p24tx_mask, ofsAddr + TX_mask);
				writew(C320p24rx_spage + i * C320p24buf_pgno, ofsAddr + Page_rxb);
				writew(readw(ofsAddr + Page_rxb) + C320p24rx_pgno, ofsAddr + EndPage_rxb);
				writew(C320p24tx_spage + i * C320p24buf_pgno, ofsAddr + Page_txb);
				writew(readw(ofsAddr + Page_txb), ofsAddr + EndPage_txb);
				break;
			case 32:
				writew(C320p32rx_mask, ofsAddr + RX_mask);
				writew(C320p32tx_mask, ofsAddr + TX_mask);
				writew(C320p32tx_ofs, ofsAddr + Ofs_txb);
				writew(C320p32rx_spage + i * C320p32buf_pgno, ofsAddr + Page_rxb);
				writew(readb(ofsAddr + Page_rxb), ofsAddr + EndPage_rxb);
				writew(C320p32tx_spage + i * C320p32buf_pgno, ofsAddr + Page_txb);
				writew(readw(ofsAddr + Page_txb), ofsAddr + EndPage_txb);
				break;
			}
		}
		break;
	}
	return 0;
}

static int moxa_load_fw(struct moxa_board_conf *brd, const struct firmware *fw)
{
	const void *ptr = fw->data;
	char rsn[64];
	u16 lens[5];
	size_t len;
	unsigned int a, lenp, lencnt;
	int ret = -EINVAL;
	struct {
		__le32 magic;	/* 0x34303430 */
		u8 reserved1[2];
		u8 type;	/* UNIX = 3 */
		u8 model;	/* C218T=1, C320T=2, CP204=3 */
		u8 reserved2[8];
		__le16 len[5];
	} const *hdr = ptr;

	BUILD_BUG_ON(ARRAY_SIZE(hdr->len) != ARRAY_SIZE(lens));

	if (fw->size < MOXA_FW_HDRLEN) {
		strcpy(rsn, "too short (even header won't fit)");
		goto err;
	}
	if (hdr->magic != cpu_to_le32(0x30343034)) {
		sprintf(rsn, "bad magic: %.8x", le32_to_cpu(hdr->magic));
		goto err;
	}
	if (hdr->type != 3) {
		sprintf(rsn, "not for linux, type is %u", hdr->type);
		goto err;
	}
	if (moxa_check_fw_model(brd, hdr->model)) {
		sprintf(rsn, "not for this card, model is %u", hdr->model);
		goto err;
	}

	len = MOXA_FW_HDRLEN;
	lencnt = hdr->model == 2 ? 5 : 3;
	for (a = 0; a < ARRAY_SIZE(lens); a++) {
		lens[a] = le16_to_cpu(hdr->len[a]);
		if (lens[a] && len + lens[a] <= fw->size &&
				moxa_check_fw(&fw->data[len]))
			printk(KERN_WARNING "MOXA firmware: unexpected input "
				"at offset %u, but going on\n", (u32)len);
		if (!lens[a] && a < lencnt) {
			sprintf(rsn, "too few entries in fw file");
			goto err;
		}
		len += lens[a];
	}

	if (len != fw->size) {
		sprintf(rsn, "bad length: %u (should be %u)", (u32)fw->size,
				(u32)len);
		goto err;
	}

	ptr += MOXA_FW_HDRLEN;
	lenp = 0; /* bios */

	strcpy(rsn, "read above");

	ret = moxa_load_bios(brd, ptr, lens[lenp]);
	if (ret)
		goto err;

	/* we skip the tty section (lens[1]), since we don't need it */
	ptr += lens[lenp] + lens[lenp + 1];
	lenp += 2; /* comm */

	if (hdr->model == 2) {
		ret = moxa_load_320b(brd, ptr, lens[lenp]);
		if (ret)
			goto err;
		/* skip another tty */
		ptr += lens[lenp] + lens[lenp + 1];
		lenp += 2;
	}

	ret = moxa_load_code(brd, ptr, lens[lenp]);
	if (ret)
		goto err;

	return 0;
err:
	printk(KERN_ERR "firmware failed to load, reason: %s\n", rsn);
	return ret;
}

static int moxa_init_board(struct moxa_board_conf *brd, struct device *dev)
{
	const struct firmware *fw;
	const char *file;
	struct moxa_port *p;
	unsigned int i;
	int ret;

	brd->ports = kcalloc(MAX_PORTS_PER_BOARD, sizeof(*brd->ports),
			GFP_KERNEL);
	if (brd->ports == NULL) {
		printk(KERN_ERR "cannot allocate memory for ports\n");
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0, p = brd->ports; i < MAX_PORTS_PER_BOARD; i++, p++) {
		tty_port_init(&p->port);
		p->port.ops = &moxa_port_ops;
		p->type = PORT_16550A;
		p->cflag = B9600 | CS8 | CREAD | CLOCAL | HUPCL;
	}

	switch (brd->boardType) {
	case MOXA_BOARD_C218_ISA:
	case MOXA_BOARD_C218_PCI:
		file = "c218tunx.cod";
		break;
	case MOXA_BOARD_CP204J:
		file = "cp204unx.cod";
		break;
	default:
		file = "c320tunx.cod";
		break;
	}

	ret = request_firmware(&fw, file, dev);
	if (ret) {
		printk(KERN_ERR "MOXA: request_firmware failed. Make sure "
				"you've placed '%s' file into your firmware "
				"loader directory (e.g. /lib/firmware)\n",
				file);
		goto err_free;
	}

	ret = moxa_load_fw(brd, fw);

	release_firmware(fw);

	if (ret)
		goto err_free;

	spin_lock_bh(&moxa_lock);
	brd->ready = 1;
	if (!timer_pending(&moxaTimer))
		mod_timer(&moxaTimer, jiffies + HZ / 50);
	spin_unlock_bh(&moxa_lock);

	return 0;
err_free:
	kfree(brd->ports);
err:
	return ret;
}

static void moxa_board_deinit(struct moxa_board_conf *brd)
{
	unsigned int a, opened;

	mutex_lock(&moxa_openlock);
	spin_lock_bh(&moxa_lock);
	brd->ready = 0;
	spin_unlock_bh(&moxa_lock);

	/* pci hot-un-plug support */
	for (a = 0; a < brd->numPorts; a++)
		if (brd->ports[a].port.flags & ASYNC_INITIALIZED) {
			struct tty_struct *tty = tty_port_tty_get(
						&brd->ports[a].port);
			if (tty) {
				tty_hangup(tty);
				tty_kref_put(tty);
			}
		}
	while (1) {
		opened = 0;
		for (a = 0; a < brd->numPorts; a++)
			if (brd->ports[a].port.flags & ASYNC_INITIALIZED)
				opened++;
		mutex_unlock(&moxa_openlock);
		if (!opened)
			break;
		msleep(50);
		mutex_lock(&moxa_openlock);
	}

	iounmap(brd->basemem);
	brd->basemem = NULL;
	kfree(brd->ports);
}

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

	retval = pci_request_region(pdev, 2, "moxa-base");
	if (retval) {
		dev_err(&pdev->dev, "can't request pci region 2\n");
		goto err;
	}

	board->basemem = ioremap_nocache(pci_resource_start(pdev, 2), 0x4000);
	if (board->basemem == NULL) {
		dev_err(&pdev->dev, "can't remap io space 2\n");
		goto err_reg;
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

	retval = moxa_init_board(board, &pdev->dev);
	if (retval)
		goto err_base;

	pci_set_drvdata(pdev, board);

	dev_info(&pdev->dev, "board '%s' ready (%u ports, firmware loaded)\n",
			moxa_brdname[board_type - 1], board->numPorts);

	return 0;
err_base:
	iounmap(board->basemem);
	board->basemem = NULL;
err_reg:
	pci_release_region(pdev, 2);
err:
	return retval;
}

static void __devexit moxa_pci_remove(struct pci_dev *pdev)
{
	struct moxa_board_conf *brd = pci_get_drvdata(pdev);

	moxa_board_deinit(brd);

	pci_release_region(pdev, 2);
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
	unsigned int isabrds = 0;
	int retval = 0;
	struct moxa_board_conf *brd = moxa_boards;
	unsigned int i;

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

	if (tty_register_driver(moxaDriver)) {
		printk(KERN_ERR "can't register MOXA Smartio tty driver!\n");
		put_tty_driver(moxaDriver);
		return -1;
	}

	/* Find the boards defined from module args. */

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
			brd->basemem = ioremap_nocache(baseaddr[i], 0x4000);
			if (!brd->basemem) {
				printk(KERN_ERR "MOXA: can't remap %lx\n",
						baseaddr[i]);
				continue;
			}
			if (moxa_init_board(brd, NULL)) {
				iounmap(brd->basemem);
				brd->basemem = NULL;
				continue;
			}

			printk(KERN_INFO "MOXA isa board found at 0x%.8lu and "
					"ready (%u ports, firmware loaded)\n",
					baseaddr[i], brd->numPorts);

			brd++;
			isabrds++;
		}
	}

#ifdef CONFIG_PCI
	retval = pci_register_driver(&moxa_pci_driver);
	if (retval) {
		printk(KERN_ERR "Can't register MOXA pci driver!\n");
		if (isabrds)
			retval = 0;
	}
#endif

	return retval;
}

static void __exit moxa_exit(void)
{
	unsigned int i;

#ifdef CONFIG_PCI
	pci_unregister_driver(&moxa_pci_driver);
#endif

	for (i = 0; i < MAX_BOARDS; i++) /* ISA boards */
		if (moxa_boards[i].ready)
			moxa_board_deinit(&moxa_boards[i]);

	del_timer_sync(&moxaTimer);

	if (tty_unregister_driver(moxaDriver))
		printk(KERN_ERR "Couldn't unregister MOXA Intellio family "
				"serial driver\n");
	put_tty_driver(moxaDriver);
}

module_init(moxa_init);
module_exit(moxa_exit);

static void moxa_shutdown(struct tty_port *port)
{
	struct moxa_port *ch = container_of(port, struct moxa_port, port);
        MoxaPortDisable(ch);
	MoxaPortFlushData(ch, 2);
}

static int moxa_carrier_raised(struct tty_port *port)
{
	struct moxa_port *ch = container_of(port, struct moxa_port, port);
	int dcd;

	spin_lock_irq(&port->lock);
	dcd = ch->DCDState;
	spin_unlock_irq(&port->lock);
	return dcd;
}

static void moxa_dtr_rts(struct tty_port *port, int onoff)
{
	struct moxa_port *ch = container_of(port, struct moxa_port, port);
	MoxaPortLineCtrl(ch, onoff, onoff);
}


static int moxa_open(struct tty_struct *tty, struct file *filp)
{
	struct moxa_board_conf *brd;
	struct moxa_port *ch;
	int port;

	port = tty->index;
	if (port == MAX_PORTS) {
		return capable(CAP_SYS_ADMIN) ? 0 : -EPERM;
	}
	if (mutex_lock_interruptible(&moxa_openlock))
		return -ERESTARTSYS;
	brd = &moxa_boards[port / MAX_PORTS_PER_BOARD];
	if (!brd->ready) {
		mutex_unlock(&moxa_openlock);
		return -ENODEV;
	}

	if (port % MAX_PORTS_PER_BOARD >= brd->numPorts) {
		mutex_unlock(&moxa_openlock);
		return -ENODEV;
	}

	ch = &brd->ports[port % MAX_PORTS_PER_BOARD];
	ch->port.count++;
	tty->driver_data = ch;
	tty_port_tty_set(&ch->port, tty);
	mutex_lock(&ch->port.mutex);
	if (!(ch->port.flags & ASYNC_INITIALIZED)) {
		ch->statusflags = 0;
		moxa_set_tty_param(tty, tty->termios);
		MoxaPortLineCtrl(ch, 1, 1);
		MoxaPortEnable(ch);
		MoxaSetFifo(ch, ch->type == PORT_16550A);
		ch->port.flags |= ASYNC_INITIALIZED;
	}
	mutex_unlock(&ch->port.mutex);
	mutex_unlock(&moxa_openlock);

	return tty_port_block_til_ready(&ch->port, tty, filp);
}

static void moxa_close(struct tty_struct *tty, struct file *filp)
{
	struct moxa_port *ch = tty->driver_data;
	ch->cflag = tty->termios->c_cflag;
	tty_port_close(&ch->port, tty, filp);
}

static int moxa_write(struct tty_struct *tty,
		      const unsigned char *buf, int count)
{
	struct moxa_port *ch = tty->driver_data;
	unsigned long flags;
	int len;

	if (ch == NULL)
		return 0;

	spin_lock_irqsave(&moxa_lock, flags);
	len = MoxaPortWriteData(tty, buf, count);
	spin_unlock_irqrestore(&moxa_lock, flags);

	set_bit(LOWWAIT, &ch->statusflags);
	return len;
}

static int moxa_write_room(struct tty_struct *tty)
{
	struct moxa_port *ch;

	if (tty->stopped)
		return 0;
	ch = tty->driver_data;
	if (ch == NULL)
		return 0;
	return MoxaPortTxFree(ch);
}

static void moxa_flush_buffer(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;

	if (ch == NULL)
		return;
	MoxaPortFlushData(ch, 1);
	tty_wakeup(tty);
}

static int moxa_chars_in_buffer(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;
	int chars;

	chars = MoxaPortTxQueue(ch);
	if (chars)
		/*
		 * Make it possible to wakeup anything waiting for output
		 * in tty_ioctl.c, etc.
		 */
        	set_bit(EMPTYWAIT, &ch->statusflags);
	return chars;
}

static int moxa_tiocmget(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;
	int flag = 0, dtr, rts;

	MoxaPortGetLineOut(ch, &dtr, &rts);
	if (dtr)
		flag |= TIOCM_DTR;
	if (rts)
		flag |= TIOCM_RTS;
	dtr = MoxaPortLineStatus(ch);
	if (dtr & 1)
		flag |= TIOCM_CTS;
	if (dtr & 2)
		flag |= TIOCM_DSR;
	if (dtr & 4)
		flag |= TIOCM_CD;
	return flag;
}

static int moxa_tiocmset(struct tty_struct *tty,
			 unsigned int set, unsigned int clear)
{
	struct moxa_port *ch;
	int dtr, rts;

	mutex_lock(&moxa_openlock);
	ch = tty->driver_data;
	if (!ch) {
		mutex_unlock(&moxa_openlock);
		return -EINVAL;
	}

	MoxaPortGetLineOut(ch, &dtr, &rts);
	if (set & TIOCM_RTS)
		rts = 1;
	if (set & TIOCM_DTR)
		dtr = 1;
	if (clear & TIOCM_RTS)
		rts = 0;
	if (clear & TIOCM_DTR)
		dtr = 0;
	MoxaPortLineCtrl(ch, dtr, rts);
	mutex_unlock(&moxa_openlock);
	return 0;
}

static void moxa_set_termios(struct tty_struct *tty,
		struct ktermios *old_termios)
{
	struct moxa_port *ch = tty->driver_data;

	if (ch == NULL)
		return;
	moxa_set_tty_param(tty, old_termios);
	if (!(old_termios->c_cflag & CLOCAL) && C_CLOCAL(tty))
		wake_up_interruptible(&ch->port.open_wait);
}

static void moxa_stop(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;

	if (ch == NULL)
		return;
	MoxaPortTxDisable(ch);
	set_bit(TXSTOPPED, &ch->statusflags);
}


static void moxa_start(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;

	if (ch == NULL)
		return;

	if (!(ch->statusflags & TXSTOPPED))
		return;

	MoxaPortTxEnable(ch);
	clear_bit(TXSTOPPED, &ch->statusflags);
}

static void moxa_hangup(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;
	tty_port_hangup(&ch->port);
}

static void moxa_new_dcdstate(struct moxa_port *p, u8 dcd)
{
	struct tty_struct *tty;
	unsigned long flags;
	dcd = !!dcd;

	spin_lock_irqsave(&p->port.lock, flags);
	if (dcd != p->DCDState) {
        	p->DCDState = dcd;
        	spin_unlock_irqrestore(&p->port.lock, flags);
		tty = tty_port_tty_get(&p->port);
		if (tty && C_CLOCAL(tty) && !dcd)
			tty_hangup(tty);
		tty_kref_put(tty);
	}
	else
		spin_unlock_irqrestore(&p->port.lock, flags);
}

static int moxa_poll_port(struct moxa_port *p, unsigned int handle,
		u16 __iomem *ip)
{
	struct tty_struct *tty = tty_port_tty_get(&p->port);
	void __iomem *ofsAddr;
	unsigned int inited = p->port.flags & ASYNC_INITIALIZED;
	u16 intr;

	if (tty) {
		if (test_bit(EMPTYWAIT, &p->statusflags) &&
				MoxaPortTxQueue(p) == 0) {
			clear_bit(EMPTYWAIT, &p->statusflags);
			tty_wakeup(tty);
		}
		if (test_bit(LOWWAIT, &p->statusflags) && !tty->stopped &&
				MoxaPortTxQueue(p) <= WAKEUP_CHARS) {
			clear_bit(LOWWAIT, &p->statusflags);
			tty_wakeup(tty);
		}

		if (inited && !test_bit(TTY_THROTTLED, &tty->flags) &&
				MoxaPortRxQueue(p) > 0) { /* RX */
			MoxaPortReadData(p);
			tty_schedule_flip(tty);
		}
	} else {
		clear_bit(EMPTYWAIT, &p->statusflags);
		MoxaPortFlushData(p, 0); /* flush RX */
	}

	if (!handle) /* nothing else to do */
		goto put;

	intr = readw(ip); /* port irq status */
	if (intr == 0)
		goto put;

	writew(0, ip); /* ACK port */
	ofsAddr = p->tableAddr;
	if (intr & IntrTx) /* disable tx intr */
		writew(readw(ofsAddr + HostStat) & ~WakeupTx,
				ofsAddr + HostStat);

	if (!inited)
		goto put;

	if (tty && (intr & IntrBreak) && !I_IGNBRK(tty)) { /* BREAK */
		tty_insert_flip_char(tty, 0, TTY_BREAK);
		tty_schedule_flip(tty);
	}

	if (intr & IntrLine)
		moxa_new_dcdstate(p, readb(ofsAddr + FlagStat) & DCD_state);
put:
	tty_kref_put(tty);

	return 0;
}

static void moxa_poll(unsigned long ignored)
{
	struct moxa_board_conf *brd;
	u16 __iomem *ip;
	unsigned int card, port, served = 0;

	spin_lock(&moxa_lock);
	for (card = 0; card < MAX_BOARDS; card++) {
		brd = &moxa_boards[card];
		if (!brd->ready)
			continue;

		served++;

		ip = NULL;
		if (readb(brd->intPend) == 0xff)
			ip = brd->intTable + readb(brd->intNdx);

		for (port = 0; port < brd->numPorts; port++)
			moxa_poll_port(&brd->ports[port], !!ip, ip + port);

		if (ip)
			writeb(0, brd->intPend); /* ACK */

		if (moxaLowWaterChk) {
			struct moxa_port *p = brd->ports;
			for (port = 0; port < brd->numPorts; port++, p++)
				if (p->lowChkFlag) {
					p->lowChkFlag = 0;
					moxa_low_water_check(p->tableAddr);
				}
		}
	}
	moxaLowWaterChk = 0;

	if (served)
		mod_timer(&moxaTimer, jiffies + HZ / 50);
	spin_unlock(&moxa_lock);
}

/******************************************************************************/

static void moxa_set_tty_param(struct tty_struct *tty, struct ktermios *old_termios)
{
	register struct ktermios *ts = tty->termios;
	struct moxa_port *ch = tty->driver_data;
	int rts, cts, txflow, rxflow, xany, baud;

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
	MoxaPortFlowCtrl(ch, rts, cts, txflow, rxflow, xany);
	baud = MoxaPortSetTermio(ch, ts, tty_get_baud_rate(tty));
	if (baud == -1)
		baud = tty_termios_baud_rate(old_termios);
	/* Not put the baud rate into the termios data */
	tty_encode_baud_rate(tty, baud, baud);
}

/*****************************************************************************
 *	Driver level functions: 					     *
 *****************************************************************************/

static void MoxaPortFlushData(struct moxa_port *port, int mode)
{
	void __iomem *ofsAddr;
	if (mode < 0 || mode > 2)
		return;
	ofsAddr = port->tableAddr;
	moxafunc(ofsAddr, FC_FlushQueue, mode);
	if (mode != 1) {
		port->lowChkFlag = 0;
		moxa_low_water_check(ofsAddr);
	}
}

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
 *      Function 10:    Setting baud rate of this port.
 *      Syntax:
 *      speed_t MoxaPortSetBaud(int port, speed_t baud);
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
 */

static void MoxaPortEnable(struct moxa_port *port)
{
	void __iomem *ofsAddr;
	u16 lowwater = 512;

	ofsAddr = port->tableAddr;
	writew(lowwater, ofsAddr + Low_water);
	if (MOXA_IS_320(port->board))
		moxafunc(ofsAddr, FC_SetBreakIrq, 0);
	else
		writew(readw(ofsAddr + HostStat) | WakeupBreak,
				ofsAddr + HostStat);

	moxafunc(ofsAddr, FC_SetLineIrq, Magic_code);
	moxafunc(ofsAddr, FC_FlushQueue, 2);

	moxafunc(ofsAddr, FC_EnableCH, Magic_code);
	MoxaPortLineStatus(port);
}

static void MoxaPortDisable(struct moxa_port *port)
{
	void __iomem *ofsAddr = port->tableAddr;

	moxafunc(ofsAddr, FC_SetFlowCtl, 0);	/* disable flow control */
	moxafunc(ofsAddr, FC_ClrLineIrq, Magic_code);
	writew(0, ofsAddr + HostStat);
	moxafunc(ofsAddr, FC_DisableCH, Magic_code);
}

static speed_t MoxaPortSetBaud(struct moxa_port *port, speed_t baud)
{
	void __iomem *ofsAddr = port->tableAddr;
	unsigned int clock, val;
	speed_t max;

	max = MOXA_IS_320(port->board) ? 460800 : 921600;
	if (baud < 50)
		return 0;
	if (baud > max)
		baud = max;
	clock = 921600;
	val = clock / baud;
	moxafunc(ofsAddr, FC_SetBaud, val);
	baud = clock / val;
	return baud;
}

static int MoxaPortSetTermio(struct moxa_port *port, struct ktermios *termio,
		speed_t baud)
{
	void __iomem *ofsAddr;
	tcflag_t mode = 0;

	ofsAddr = port->tableAddr;

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

	moxafunc(ofsAddr, FC_SetDataMode, (u16)mode);

	if (MOXA_IS_320(port->board) && baud >= 921600)
		return -1;

	baud = MoxaPortSetBaud(port, baud);

	if (termio->c_iflag & (IXON | IXOFF | IXANY)) {
	        spin_lock_irq(&moxafunc_lock);
		writeb(termio->c_cc[VSTART], ofsAddr + FuncArg);
		writeb(termio->c_cc[VSTOP], ofsAddr + FuncArg1);
		writeb(FC_SetXonXoff, ofsAddr + FuncCode);
		moxa_wait_finish(ofsAddr);
		spin_unlock_irq(&moxafunc_lock);

	}
	return baud;
}

static int MoxaPortGetLineOut(struct moxa_port *port, int *dtrState,
		int *rtsState)
{
	if (dtrState)
		*dtrState = !!(port->lineCtrl & DTR_ON);
	if (rtsState)
		*rtsState = !!(port->lineCtrl & RTS_ON);

	return 0;
}

static void MoxaPortLineCtrl(struct moxa_port *port, int dtr, int rts)
{
	u8 mode = 0;

	if (dtr)
		mode |= DTR_ON;
	if (rts)
		mode |= RTS_ON;
	port->lineCtrl = mode;
	moxafunc(port->tableAddr, FC_LineControl, mode);
}

static void MoxaPortFlowCtrl(struct moxa_port *port, int rts, int cts,
		int txflow, int rxflow, int txany)
{
	int mode = 0;

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
	moxafunc(port->tableAddr, FC_SetFlowCtl, mode);
}

static int MoxaPortLineStatus(struct moxa_port *port)
{
	void __iomem *ofsAddr;
	int val;

	ofsAddr = port->tableAddr;
	if (MOXA_IS_320(port->board))
		val = moxafuncret(ofsAddr, FC_LineStatus, 0);
	else
		val = readw(ofsAddr + FlagStat) >> 4;
	val &= 0x0B;
	if (val & 8)
		val |= 4;
	moxa_new_dcdstate(port, val & 8);
	val &= 7;
	return val;
}

static int MoxaPortWriteData(struct tty_struct *tty,
		const unsigned char *buffer, int len)
{
	struct moxa_port *port = tty->driver_data;
	void __iomem *baseAddr, *ofsAddr, *ofs;
	unsigned int c, total;
	u16 head, tail, tx_mask, spage, epage;
	u16 pageno, pageofs, bufhead;

	ofsAddr = port->tableAddr;
	baseAddr = port->board->basemem;
	tx_mask = readw(ofsAddr + TX_mask);
	spage = readw(ofsAddr + Page_txb);
	epage = readw(ofsAddr + EndPage_txb);
	tail = readw(ofsAddr + TXwptr);
	head = readw(ofsAddr + TXrptr);
	c = (head > tail) ? (head - tail - 1) : (head - tail + tx_mask);
	if (c > len)
		c = len;
	moxaLog.txcnt[port->port.tty->index] += c;
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
			memcpy_toio(ofs, buffer, len);
			buffer += len;
			tail = (tail + len) & tx_mask;
			c -= len;
		}
	} else {
		pageno = spage + (tail >> 13);
		pageofs = tail & Page_mask;
		while (c > 0) {
			len = Page_size - pageofs;
			if (len > c)
				len = c;
			writeb(pageno, baseAddr + Control_reg);
			ofs = baseAddr + DynPage_addr + pageofs;
			memcpy_toio(ofs, buffer, len);
			buffer += len;
			if (++pageno == epage)
				pageno = spage;
			pageofs = 0;
			c -= len;
		}
		tail = (tail + total) & tx_mask;
	}
	writew(tail, ofsAddr + TXwptr);
	writeb(1, ofsAddr + CD180TXirq);	/* start to send */
	return total;
}

static int MoxaPortReadData(struct moxa_port *port)
{
	struct tty_struct *tty = port->port.tty;
	unsigned char *dst;
	void __iomem *baseAddr, *ofsAddr, *ofs;
	unsigned int count, len, total;
	u16 tail, rx_mask, spage, epage;
	u16 pageno, pageofs, bufhead, head;

	ofsAddr = port->tableAddr;
	baseAddr = port->board->basemem;
	head = readw(ofsAddr + RXrptr);
	tail = readw(ofsAddr + RXwptr);
	rx_mask = readw(ofsAddr + RX_mask);
	spage = readw(ofsAddr + Page_rxb);
	epage = readw(ofsAddr + EndPage_rxb);
	count = (tail >= head) ? (tail - head) : (tail - head + rx_mask + 1);
	if (count == 0)
		return 0;

	total = count;
	moxaLog.rxcnt[tty->index] += total;
	if (spage == epage) {
		bufhead = readw(ofsAddr + Ofs_rxb);
		writew(spage, baseAddr + Control_reg);
		while (count > 0) {
			ofs = baseAddr + DynPage_addr + bufhead + head;
			len = (tail >= head) ? (tail - head) :
					(rx_mask + 1 - head);
			len = tty_prepare_flip_string(tty, &dst,
					min(len, count));
			memcpy_fromio(dst, ofs, len);
			head = (head + len) & rx_mask;
			count -= len;
		}
	} else {
		pageno = spage + (head >> 13);
		pageofs = head & Page_mask;
		while (count > 0) {
			writew(pageno, baseAddr + Control_reg);
			ofs = baseAddr + DynPage_addr + pageofs;
			len = tty_prepare_flip_string(tty, &dst,
					min(Page_size - pageofs, count));
			memcpy_fromio(dst, ofs, len);

			count -= len;
			pageofs = (pageofs + len) & Page_mask;
			if (pageofs == 0 && ++pageno == epage)
				pageno = spage;
		}
		head = (head + total) & rx_mask;
	}
	writew(head, ofsAddr + RXrptr);
	if (readb(ofsAddr + FlagStat) & Xoff_state) {
		moxaLowWaterChk = 1;
		port->lowChkFlag = 1;
	}
	return total;
}


static int MoxaPortTxQueue(struct moxa_port *port)
{
	void __iomem *ofsAddr = port->tableAddr;
	u16 rptr, wptr, mask;

	rptr = readw(ofsAddr + TXrptr);
	wptr = readw(ofsAddr + TXwptr);
	mask = readw(ofsAddr + TX_mask);
	return (wptr - rptr) & mask;
}

static int MoxaPortTxFree(struct moxa_port *port)
{
	void __iomem *ofsAddr = port->tableAddr;
	u16 rptr, wptr, mask;

	rptr = readw(ofsAddr + TXrptr);
	wptr = readw(ofsAddr + TXwptr);
	mask = readw(ofsAddr + TX_mask);
	return mask - ((wptr - rptr) & mask);
}

static int MoxaPortRxQueue(struct moxa_port *port)
{
	void __iomem *ofsAddr = port->tableAddr;
	u16 rptr, wptr, mask;

	rptr = readw(ofsAddr + RXrptr);
	wptr = readw(ofsAddr + RXwptr);
	mask = readw(ofsAddr + RX_mask);
	return (wptr - rptr) & mask;
}

static void MoxaPortTxDisable(struct moxa_port *port)
{
	moxafunc(port->tableAddr, FC_SetXoffState, Magic_code);
}

static void MoxaPortTxEnable(struct moxa_port *port)
{
	moxafunc(port->tableAddr, FC_SetXonState, Magic_code);
}

static int moxa_get_serial_info(struct moxa_port *info,
		struct serial_struct __user *retinfo)
{
	struct serial_struct tmp = {
		.type = info->type,
		.line = info->port.tty->index,
		.flags = info->port.flags,
		.baud_base = 921600,
		.close_delay = info->port.close_delay
	};
	return copy_to_user(retinfo, &tmp, sizeof(*retinfo)) ? -EFAULT : 0;
}


static int moxa_set_serial_info(struct moxa_port *info,
		struct serial_struct __user *new_info)
{
	struct serial_struct new_serial;

	if (copy_from_user(&new_serial, new_info, sizeof(new_serial)))
		return -EFAULT;

	if (new_serial.irq != 0 || new_serial.port != 0 ||
			new_serial.custom_divisor != 0 ||
			new_serial.baud_base != 921600)
		return -EPERM;

	if (!capable(CAP_SYS_ADMIN)) {
		if (((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (info->port.flags & ~ASYNC_USR_MASK)))
			return -EPERM;
	} else
		info->port.close_delay = new_serial.close_delay * HZ / 100;

	new_serial.flags = (new_serial.flags & ~ASYNC_FLAGS);
	new_serial.flags |= (info->port.flags & ASYNC_FLAGS);

	MoxaSetFifo(info, new_serial.type == PORT_16550A);

	info->type = new_serial.type;
	return 0;
}



/*****************************************************************************
 *	Static local functions: 					     *
 *****************************************************************************/

static void MoxaSetFifo(struct moxa_port *port, int enable)
{
	void __iomem *ofsAddr = port->tableAddr;

	if (!enable) {
		moxafunc(ofsAddr, FC_SetRxFIFOTrig, 0);
		moxafunc(ofsAddr, FC_SetTxFIFOCnt, 1);
	} else {
		moxafunc(ofsAddr, FC_SetRxFIFOTrig, 3);
		moxafunc(ofsAddr, FC_SetTxFIFOCnt, 16);
	}
}
