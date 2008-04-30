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
#include <linux/completion.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include "moxa.h"

#define MOXA_VERSION		"5.1k"

#define MOXA_FW_HDRLEN		32

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
	struct moxa_board_conf *board;
	struct tty_struct *tty;
	void __iomem *tableAddr;

	int type;
	int close_delay;
	int count;
	int blocked_open;
	int asyncflags;
	int cflag;
	unsigned long statusflags;
	wait_queue_head_t open_wait;
	struct completion close_wait;

	u8 DCDState;
	u8 lineCtrl;
	u8 lowChkFlag;
};

struct mon_str {
	int tick;
	int rxcnt[MAX_PORTS];
	int txcnt[MAX_PORTS];
};

/* statusflags */
#define TXSTOPPED	0x1
#define LOWWAIT 	0x2
#define EMPTYWAIT	0x4
#define THROTTLE	0x8

#define SERIAL_DO_RESTART

#define WAKEUP_CHARS		256

static int ttymajor = MOXAMAJOR;
static struct mon_str moxaLog;
static unsigned int moxaFuncTout = HZ / 2;
static unsigned int moxaLowWaterChk;
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
static void moxa_shut_down(struct moxa_port *);
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
static int MoxaPortWriteData(struct moxa_port *, unsigned char *, int);
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

static void moxa_wait_finish(void __iomem *ofsAddr)
{
	unsigned long end = jiffies + moxaFuncTout;

	while (readw(ofsAddr + FuncCode) != 0)
		if (time_after(jiffies, end))
			return;
	if (readw(ofsAddr + FuncCode) != 0 && printk_ratelimit())
		printk(KERN_WARNING "moxa function expired\n");
}

static void moxafunc(void __iomem *ofsAddr, int cmd, ushort arg)
{
	writew(arg, ofsAddr + FuncArg);
	writew(cmd, ofsAddr + FuncCode);
	moxa_wait_finish(ofsAddr);
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

static int moxa_ioctl(struct tty_struct *tty, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	struct moxa_port *ch = tty->driver_data;
	void __user *argp = (void __user *)arg;
	int status;

	if (tty->index == MAX_PORTS) {
		if (cmd != MOXA_GETDATACOUNT && cmd != MOXA_GET_IOQUEUE &&
				cmd != MOXA_GETMSTATUS)
			return -EINVAL;
	} else if (!ch)
		return -ENODEV;

	switch (cmd) {
	case MOXA_GETDATACOUNT:
		moxaLog.tick = jiffies;
		return copy_to_user(argp, &moxaLog, sizeof(moxaLog)) ?
			-EFAULT : 0;
	case MOXA_FLUSH_QUEUE:
		MoxaPortFlushData(ch, arg);
		return 0;
	case MOXA_GET_IOQUEUE: {
		struct moxaq_str __user *argm = argp;
		struct moxaq_str tmp;
		struct moxa_port *p;
		unsigned int i, j;

		for (i = 0; i < MAX_BOARDS; i++) {
			p = moxa_boards[i].ports;
			for (j = 0; j < MAX_PORTS_PER_BOARD; j++, p++, argm++) {
				memset(&tmp, 0, sizeof(tmp));
				if (moxa_boards[i].ready) {
					tmp.inq = MoxaPortRxQueue(p);
					tmp.outq = MoxaPortTxQueue(p);
				}
				if (copy_to_user(argm, &tmp, sizeof(tmp)))
					return -EFAULT;
			}
		}
		return 0;
	} case MOXA_GET_OQUEUE:
		status = MoxaPortTxQueue(ch);
		return put_user(status, (unsigned long __user *)argp);
	case MOXA_GET_IQUEUE:
		status = MoxaPortRxQueue(ch);
		return put_user(status, (unsigned long __user *)argp);
	case MOXA_GETMSTATUS: {
		struct mxser_mstatus __user *argm = argp;
		struct mxser_mstatus tmp;
		struct moxa_port *p;
		unsigned int i, j;

		for (i = 0; i < MAX_BOARDS; i++) {
			p = moxa_boards[i].ports;
			for (j = 0; j < MAX_PORTS_PER_BOARD; j++, p++, argm++) {
				memset(&tmp, 0, sizeof(tmp));
				if (!moxa_boards[i].ready)
					goto copy;

				status = MoxaPortLineStatus(p);
				if (status & 1)
					tmp.cts = 1;
				if (status & 2)
					tmp.dsr = 1;
				if (status & 4)
					tmp.dcd = 1;

				if (!p->tty || !p->tty->termios)
					tmp.cflag = p->cflag;
				else
					tmp.cflag = p->tty->termios->c_cflag;
copy:
				if (copy_to_user(argm, &tmp, sizeof(tmp)))
					return -EFAULT;
			}
		}
		return 0;
	}
	case TIOCGSERIAL:
		return moxa_get_serial_info(ch, argp);
	case TIOCSSERIAL:
		return moxa_set_serial_info(ch, argp);
	}
	return -ENOIOCTLCMD;
}

static void moxa_break_ctl(struct tty_struct *tty, int state)
{
	struct moxa_port *port = tty->driver_data;

	moxafunc(port->tableAddr, state ? FC_SendBreak : FC_StopBreak,
			Magic_code);
}

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
	.break_ctl = moxa_break_ctl,
	.tiocmget = moxa_tiocmget,
	.tiocmset = moxa_tiocmset,
};

static struct tty_driver *moxaDriver;
static DEFINE_TIMER(moxaTimer, moxa_poll, 0, 0);
static DEFINE_SPINLOCK(moxa_lock);

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
			printk(KERN_ERR "moxa: bios upload failed -- CPU/Basic "
					"module not found\n");
			return -EIO;
		}
		break;
	}

	return 0;
err:
	printk(KERN_ERR "moxa: bios upload failed -- board not found\n");
	return -EIO;
}

static int moxa_load_320b(struct moxa_board_conf *brd, const u8 *ptr,
		size_t len)
{
	void __iomem *baseAddr = brd->basemem;

	if (len < 7168) {
		printk(KERN_ERR "moxa: invalid 320 bios -- too short\n");
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
	const u16 *uptr = ptr;
	size_t wlen, len2, j;
	unsigned long key, loadbuf, loadlen, checksum, checksum_ok;
	unsigned int i, retry, c320;
	u16 usum, keycode;

	c320 = brd->boardType == MOXA_BOARD_C320_PCI ||
			brd->boardType == MOXA_BOARD_C320_ISA;
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

	if (c320) {
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

	if (c320) {
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
		printk(KERN_ERR "moxa: bios length is not even\n");
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
	void *ptr = fw->data;
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
	} *hdr = ptr;

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
			printk(KERN_WARNING "moxa firmware: unexpected input "
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
		p->type = PORT_16550A;
		p->close_delay = 5 * HZ / 10;
		p->cflag = B9600 | CS8 | CREAD | CLOCAL | HUPCL;
		init_waitqueue_head(&p->open_wait);
		init_completion(&p->close_wait);
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
		printk(KERN_ERR "request_firmware failed\n");
		goto err_free;
	}

	ret = moxa_load_fw(brd, fw);

	release_firmware(fw);

	if (ret)
		goto err_free;

	brd->ready = 1;

	if (!timer_pending(&moxaTimer))
		mod_timer(&moxaTimer, jiffies + HZ / 50);

	return 0;
err_free:
	kfree(brd->ports);
err:
	return ret;
}

static void moxa_board_deinit(struct moxa_board_conf *brd)
{
	spin_lock_bh(&moxa_lock);
	brd->ready = 0;
	spin_unlock_bh(&moxa_lock);
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

	board->basemem = ioremap(pci_resource_start(pdev, 2), 0x4000);
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

	return (0);
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

	pr_debug("Moxa tty devices major number = %d\n", ttymajor);

	if (tty_register_driver(moxaDriver)) {
		printk(KERN_ERR "Couldn't install MOXA Smartio family driver !\n");
		put_tty_driver(moxaDriver);
		return -1;
	}

	/* Find the boards defined from module args. */
#ifdef MODULE
	{
	struct moxa_board_conf *brd = moxa_boards;
	unsigned int i;
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
			if (moxa_init_board(brd, NULL)) {
				iounmap(brd->basemem);
				brd->basemem = NULL;
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

	if (tty_unregister_driver(moxaDriver))
		printk(KERN_ERR "Couldn't unregister MOXA Intellio family "
				"serial driver\n");
	put_tty_driver(moxaDriver);

#ifdef CONFIG_PCI
	pci_unregister_driver(&moxa_pci_driver);
#endif

	for (i = 0; i < MAX_BOARDS; i++) /* ISA boards */
		if (moxa_boards[i].ready)
			moxa_board_deinit(&moxa_boards[i]);
}

module_init(moxa_init);
module_exit(moxa_exit);

static int moxa_open(struct tty_struct *tty, struct file *filp)
{
	struct moxa_board_conf *brd;
	struct moxa_port *ch;
	int port;
	int retval;

	port = tty->index;
	if (port == MAX_PORTS) {
		return capable(CAP_SYS_ADMIN) ? 0 : -EPERM;
	}
	brd = &moxa_boards[port / MAX_PORTS_PER_BOARD];
	if (!brd->ready)
		return -ENODEV;

	ch = &brd->ports[port % MAX_PORTS_PER_BOARD];
	ch->count++;
	tty->driver_data = ch;
	ch->tty = tty;
	if (!(ch->asyncflags & ASYNC_INITIALIZED)) {
		ch->statusflags = 0;
		moxa_set_tty_param(tty, tty->termios);
		MoxaPortLineCtrl(ch, 1, 1);
		MoxaPortEnable(ch);
		ch->asyncflags |= ASYNC_INITIALIZED;
	}
	retval = moxa_block_till_ready(tty, filp, ch);

	moxa_unthrottle(tty);

	if (ch->type == PORT_16550A) {
		MoxaSetFifo(ch, 1);
	} else {
		MoxaSetFifo(ch, 0);
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
	}
	moxa_shut_down(ch);
	MoxaPortFlushData(ch, 2);

	if (tty->driver->flush_buffer)
		tty->driver->flush_buffer(tty);
	tty_ldisc_flush(tty);
			
	tty->closing = 0;
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
	struct moxa_port *ch = tty->driver_data;
	int len;

	if (ch == NULL)
		return 0;

	spin_lock_bh(&moxa_lock);
	len = MoxaPortWriteData(ch, (unsigned char *) buf, count);
	spin_unlock_bh(&moxa_lock);

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
	ch = tty->driver_data;
	if (ch == NULL)
		return (0);
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

	/*
	 * Sigh...I have to check if driver_data is NULL here, because
	 * if an open() fails, the TTY subsystem eventually calls
	 * tty_wait_until_sent(), which calls the driver's chars_in_buffer()
	 * routine.  And since the open() failed, we return 0 here.  TDJ
	 */
	if (ch == NULL)
		return (0);
	chars = MoxaPortTxQueue(ch);
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
	struct moxa_port *ch = tty->driver_data;

	if (ch == NULL)
		return;
	spin_lock_bh(&moxa_lock);
	MoxaPortWriteData(ch, &c, 1);
	spin_unlock_bh(&moxa_lock);
	/************************************************
	if ( !(ch->statusflags & LOWWAIT) && (MoxaPortTxFree(port) <= 100) )
	*************************************************/
	ch->statusflags |= LOWWAIT;
}

static int moxa_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct moxa_port *ch = tty->driver_data;
	int flag = 0, dtr, rts;

	if (!ch)
		return -EINVAL;

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

static int moxa_tiocmset(struct tty_struct *tty, struct file *file,
			 unsigned int set, unsigned int clear)
{
	struct moxa_port *ch = tty->driver_data;
	int port;
	int dtr, rts;

	port = tty->index;
	if (!ch)
		return -EINVAL;

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
	return 0;
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
	MoxaPortTxDisable(ch);
	ch->statusflags |= TXSTOPPED;
}


static void moxa_start(struct tty_struct *tty)
{
	struct moxa_port *ch = (struct moxa_port *) tty->driver_data;

	if (ch == NULL)
		return;

	if (!(ch->statusflags & TXSTOPPED))
		return;

	MoxaPortTxEnable(ch);
	ch->statusflags &= ~TXSTOPPED;
}

static void moxa_hangup(struct tty_struct *tty)
{
	struct moxa_port *ch = (struct moxa_port *) tty->driver_data;

	moxa_flush_buffer(tty);
	moxa_shut_down(ch);
	ch->count = 0;
	ch->asyncflags &= ~ASYNC_NORMAL_ACTIVE;
	ch->tty = NULL;
	wake_up_interruptible(&ch->open_wait);
}

static void moxa_new_dcdstate(struct moxa_port *p, u8 dcd)
{
	dcd = !!dcd;

	if ((dcd != p->DCDState) && p->tty && C_CLOCAL(p->tty)) {
		if (!dcd) {
			tty_hangup(p->tty);
			p->asyncflags &= ~ASYNC_NORMAL_ACTIVE;
		}
		wake_up_interruptible(&p->open_wait);
	}
	p->DCDState = dcd;
}

static int moxa_poll_port(struct moxa_port *p, unsigned int handle,
		u16 __iomem *ip)
{
	struct tty_struct *tty = p->tty;
	void __iomem *ofsAddr;
	unsigned int inited = p->asyncflags & ASYNC_INITIALIZED;
	u16 intr;

	if (tty) {
		if ((p->statusflags & EMPTYWAIT) &&
				MoxaPortTxQueue(p) == 0) {
			p->statusflags &= ~EMPTYWAIT;
			tty_wakeup(tty);
		}
		if ((p->statusflags & LOWWAIT) && !tty->stopped &&
				MoxaPortTxQueue(p) <= WAKEUP_CHARS) {
			p->statusflags &= ~LOWWAIT;
			tty_wakeup(tty);
		}

		if (inited && !(p->statusflags & THROTTLE) &&
				MoxaPortRxQueue(p) > 0) { /* RX */
			MoxaPortReadData(p);
			tty_schedule_flip(tty);
		}
	} else {
		p->statusflags &= ~EMPTYWAIT;
		MoxaPortFlushData(p, 0); /* flush RX */
	}

	if (!handle) /* nothing else to do */
		return 0;

	intr = readw(ip); /* port irq status */
	if (intr == 0)
		return 0;

	writew(0, ip); /* ACK port */
	ofsAddr = p->tableAddr;
	if (intr & IntrTx) /* disable tx intr */
		writew(readw(ofsAddr + HostStat) & ~WakeupTx,
				ofsAddr + HostStat);

	if (!inited)
		return 0;

	if (tty && (intr & IntrBreak) && !I_IGNBRK(tty)) { /* BREAK */
		tty_insert_flip_char(tty, 0, TTY_BREAK);
		tty_schedule_flip(tty);
	}

	if (intr & IntrLine)
		moxa_new_dcdstate(p, readb(ofsAddr + FlagStat) & DCD_state);

	return 0;
}

static void moxa_poll(unsigned long ignored)
{
	struct moxa_board_conf *brd;
	u16 __iomem *ip;
	unsigned int card, port;

	spin_lock(&moxa_lock);
	for (card = 0; card < MAX_BOARDS; card++) {
		brd = &moxa_boards[card];
		if (!brd->ready)
			continue;

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
	spin_unlock(&moxa_lock);

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

static int moxa_block_till_ready(struct tty_struct *tty, struct file *filp,
			    struct moxa_port *ch)
{
	DECLARE_WAITQUEUE(wait,current);
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
		tty->index, ch->count);
	spin_lock_bh(&moxa_lock);
	if (!tty_hung_up_p(filp))
		ch->count--;
	ch->blocked_open++;
	spin_unlock_bh(&moxa_lock);

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
				ch->DCDState))
			break;

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&ch->open_wait, &wait);

	spin_lock_bh(&moxa_lock);
	if (!tty_hung_up_p(filp))
		ch->count++;
	ch->blocked_open--;
	spin_unlock_bh(&moxa_lock);
	pr_debug("block_til_ready after blocking: ttys%d, count = %d\n",
		tty->index, ch->count);
	if (retval)
		return (retval);
	/* FIXME: review to see if we need to use set_bit on these */
	ch->asyncflags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}

static void moxa_setup_empty_event(struct tty_struct *tty)
{
	struct moxa_port *ch = tty->driver_data;

	spin_lock_bh(&moxa_lock);
	ch->statusflags |= EMPTYWAIT;
	spin_unlock_bh(&moxa_lock);
}

static void moxa_shut_down(struct moxa_port *ch)
{
	struct tty_struct *tp;

	if (!(ch->asyncflags & ASYNC_INITIALIZED))
		return;

	tp = ch->tty;

	MoxaPortDisable(ch);

	/*
	 * If we're a modem control device and HUPCL is on, drop RTS & DTR.
	 */
	if (tp->termios->c_cflag & HUPCL)
		MoxaPortLineCtrl(ch, 0, 0);

	ch->asyncflags &= ~ASYNC_INITIALIZED;
}

/*****************************************************************************
 *	Driver level functions: 					     *
 *****************************************************************************/

static void MoxaPortFlushData(struct moxa_port *port, int mode)
{
	void __iomem *ofsAddr;
	if ((mode < 0) || (mode > 2))
		return;
	ofsAddr = port->tableAddr;
	moxafunc(ofsAddr, FC_FlushQueue, mode);
	if (mode != 1) {
		port->lowChkFlag = 0;
		moxa_low_water_check(ofsAddr);
	}
}

/*****************************************************************************
 *	Port level functions:						     *
 *	2.  MoxaPortEnable(int port);					     *
 *	3.  MoxaPortDisable(int port);					     *
 *	4.  MoxaPortGetMaxBaud(int port);				     *
 *	6.  MoxaPortSetBaud(int port, long baud);			     *
 *	8.  MoxaPortSetTermio(int port, unsigned char *termio); 	     *
 *	9.  MoxaPortGetLineOut(int port, int *dtrState, int *rtsState);      *
 *	10. MoxaPortLineCtrl(int port, int dtrState, int rtsState);	     *
 *	11. MoxaPortFlowCtrl(int port, int rts, int cts, int rx, int tx,int xany);    *
 *	12. MoxaPortLineStatus(int port);				     *
 *	15. MoxaPortFlushData(int port, int mode);	                     *
 *	16. MoxaPortWriteData(int port, unsigned char * buffer, int length); *
 *	17. MoxaPortReadData(int port, struct tty_struct *tty); 	     *
 *	20. MoxaPortTxQueue(int port);					     *
 *	21. MoxaPortTxFree(int port);					     *
 *	22. MoxaPortRxQueue(int port);					     *
 *	24. MoxaPortTxDisable(int port);				     *
 *	25. MoxaPortTxEnable(int port); 				     *
 *	27. MoxaPortResetBrkCnt(int port);				     *
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
	short lowwater = 512;

	ofsAddr = port->tableAddr;
	writew(lowwater, ofsAddr + Low_water);
	if (port->board->boardType == MOXA_BOARD_C320_ISA ||
	    port->board->boardType == MOXA_BOARD_C320_PCI) {
		moxafunc(ofsAddr, FC_SetBreakIrq, 0);
	} else {
		writew(readw(ofsAddr + HostStat) | WakeupBreak, ofsAddr + HostStat);
	}

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

static long MoxaPortGetMaxBaud(struct moxa_port *port)
{
	if (port->board->boardType == MOXA_BOARD_C320_ISA ||
			port->board->boardType == MOXA_BOARD_C320_PCI)
		return (460800L);
	else
		return (921600L);
}


static long MoxaPortSetBaud(struct moxa_port *port, long baud)
{
	void __iomem *ofsAddr;
	long max, clock;
	unsigned int val;

	if ((baud < 50L) || ((max = MoxaPortGetMaxBaud(port)) == 0))
		return (0);
	ofsAddr = port->tableAddr;
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
	return (baud);
}

static int MoxaPortSetTermio(struct moxa_port *port, struct ktermios *termio,
		speed_t baud)
{
	void __iomem *ofsAddr;
	tcflag_t cflag;
	tcflag_t mode = 0;

	ofsAddr = port->tableAddr;
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

	if (port->board->boardType == MOXA_BOARD_C320_ISA ||
			port->board->boardType == MOXA_BOARD_C320_PCI) {
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

static int MoxaPortGetLineOut(struct moxa_port *port, int *dtrState,
		int *rtsState)
{

	if (dtrState)
		*dtrState = !!(port->lineCtrl & DTR_ON);
	if (rtsState)
		*rtsState = !!(port->lineCtrl & RTS_ON);

	return (0);
}

static void MoxaPortLineCtrl(struct moxa_port *port, int dtr, int rts)
{
	int mode = 0;

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
	if (port->board->boardType == MOXA_BOARD_C320_ISA ||
			port->board->boardType == MOXA_BOARD_C320_PCI) {
		moxafunc(ofsAddr, FC_LineStatus, 0);
		val = readw(ofsAddr + FuncArg);
	} else {
		val = readw(ofsAddr + FlagStat) >> 4;
	}
	val &= 0x0B;
	if (val & 8)
		val |= 4;
	moxa_new_dcdstate(port, val & 8);
	val &= 7;
	return val;
}

static int MoxaPortWriteData(struct moxa_port *port, unsigned char *buffer,
		int len)
{
	int c, total, i;
	ushort tail;
	int cnt;
	ushort head, tx_mask, spage, epage;
	ushort pageno, pageofs, bufhead;
	void __iomem *baseAddr, *ofsAddr, *ofs;

	ofsAddr = port->tableAddr;
	baseAddr = port->board->basemem;
	tx_mask = readw(ofsAddr + TX_mask);
	spage = readw(ofsAddr + Page_txb);
	epage = readw(ofsAddr + EndPage_txb);
	tail = readw(ofsAddr + TXwptr);
	head = readw(ofsAddr + TXrptr);
	c = (head > tail) ? (head - tail - 1)
	    : (head - tail + tx_mask);
	if (c > len)
		c = len;
	moxaLog.txcnt[port->tty->index] += c;
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

static int MoxaPortReadData(struct moxa_port *port)
{
	struct tty_struct *tty = port->tty;
	register ushort head, pageofs;
	int i, count, cnt, len, total, remain;
	ushort tail, rx_mask, spage, epage;
	ushort pageno, bufhead;
	void __iomem *baseAddr, *ofsAddr, *ofs;

	ofsAddr = port->tableAddr;
	baseAddr = port->board->basemem;
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
	moxaLog.rxcnt[tty->index] += total;
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
		port->lowChkFlag = 1;
	}
	return (total);
}


static int MoxaPortTxQueue(struct moxa_port *port)
{
	void __iomem *ofsAddr = port->tableAddr;
	ushort rptr, wptr, mask;
	int len;

	rptr = readw(ofsAddr + TXrptr);
	wptr = readw(ofsAddr + TXwptr);
	mask = readw(ofsAddr + TX_mask);
	len = (wptr - rptr) & mask;
	return (len);
}

static int MoxaPortTxFree(struct moxa_port *port)
{
	void __iomem *ofsAddr = port->tableAddr;
	ushort rptr, wptr, mask;
	int len;

	rptr = readw(ofsAddr + TXrptr);
	wptr = readw(ofsAddr + TXwptr);
	mask = readw(ofsAddr + TX_mask);
	len = mask - ((wptr - rptr) & mask);
	return (len);
}

static int MoxaPortRxQueue(struct moxa_port *port)
{
	void __iomem *ofsAddr = port->tableAddr;
	ushort rptr, wptr, mask;
	int len;

	rptr = readw(ofsAddr + RXrptr);
	wptr = readw(ofsAddr + RXwptr);
	mask = readw(ofsAddr + RX_mask);
	len = (wptr - rptr) & mask;
	return (len);
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
	struct serial_struct tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.type = info->type;
	tmp.line = info->tty->index;
	tmp.port = 0;
	tmp.irq = 0;
	tmp.flags = info->asyncflags;
	tmp.baud_base = 921600;
	tmp.close_delay = info->close_delay;
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
	}

	new_serial.flags = (new_serial.flags & ~ASYNC_FLAGS);
	new_serial.flags |= (info->asyncflags & ASYNC_FLAGS);

	if (new_serial.type == PORT_16550A) {
		MoxaSetFifo(info, 1);
	} else {
		MoxaSetFifo(info, 0);
	}

	info->type = new_serial.type;
	return (0);
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
