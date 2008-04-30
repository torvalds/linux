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

	int loadstat;

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
static int moxaCard;
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

static int moxa_load_c218(struct moxa_board_conf *brd, const void *ptr,
		size_t len)
{
	void __iomem *baseAddr = brd->basemem;
	const u16 *uptr = ptr;
	size_t wlen, len2, j;
	unsigned int i, retry;
	u16 usum, keycode;

	if (brd->boardType == MOXA_BOARD_CP204J)
		keycode = CP204J_KeyCode;
	else
		keycode = C218_KeyCode;
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
			memcpy_toio(baseAddr + C218_LoadBuf, ptr + j,
					len2 << 1);
			j += len2 << 1;

			writew(len2, baseAddr + C218DLoad_len);
			writew(0, baseAddr + C218_key);
			for (i = 0; i < 100; i++) {
				if (readw(baseAddr + C218_key) == keycode)
					break;
				msleep(10);
			}
			if (readw(baseAddr + C218_key) != keycode)
				return -EIO;
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
	if (readb(baseAddr + C218chksum_ok) != 1)
		return -EIO;

	writew(0, baseAddr + C218_key);
	for (i = 0; i < 100; i++) {
		if (readw(baseAddr + Magic_no) == Magic_code)
			break;
		msleep(10);
	}
	if (readw(baseAddr + Magic_no) != Magic_code)
		return -EIO;

	writew(1, baseAddr + Disable_IRQ);
	writew(0, baseAddr + Magic_no);
	for (i = 0; i < 100; i++) {
		if (readw(baseAddr + Magic_no) == Magic_code)
			break;
		msleep(10);
	}
	if (readw(baseAddr + Magic_no) != Magic_code)
		return -EIO;

	moxaCard = 1;
	brd->intNdx = baseAddr + IRQindex;
	brd->intPend = baseAddr + IRQpending;
	brd->intTable = baseAddr + IRQtable;

	return 0;
}

static int moxa_load_c320(struct moxa_board_conf *brd, const void *ptr,
		size_t len)
{
	void __iomem *baseAddr = brd->basemem;
	const u16 *uptr = ptr;
	size_t wlen, len2, j;
	unsigned int i, retry;
	u16 usum;

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
			memcpy_toio(baseAddr + C320_LoadBuf, ptr + j,
					len2 << 1);
			j += len2 << 1;
			writew(len2, baseAddr + C320DLoad_len);
			writew(0, baseAddr + C320_key);
			for (i = 0; i < 10; i++) {
				if (readw(baseAddr + C320_key) == C320_KeyCode)
					break;
				msleep(10);
			}
			if (readw(baseAddr + C320_key) != C320_KeyCode)
				return -EIO;
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
		return -EIO;

	writew(0, baseAddr + C320_key);
	for (i = 0; i < 600; i++) {
		if (readw(baseAddr + Magic_no) == Magic_code)
			break;
		msleep(10);
	}
	if (readw(baseAddr + Magic_no) != Magic_code)
		return -EIO;

	if (brd->busType == MOXA_BUS_TYPE_PCI) {	/* ASIC board */
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
		return -EIO;

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
	moxaCard = 1;
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
		printk(KERN_ERR "moxa: C2XX bios length is not even\n");
		return -EINVAL;
	}

	switch (brd->boardType) {
	case MOXA_BOARD_C218_ISA:
	case MOXA_BOARD_C218_PCI:
	case MOXA_BOARD_CP204J:
		retval = moxa_load_c218(brd, ptr, len);
		if (retval)
			return retval;
		port = brd->ports;
		for (i = 0; i < brd->numPorts; i++, port++) {
			port->chkPort = 1;
			port->curBaud = 9600L;
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
		retval = moxa_load_c320(brd, ptr, len); /* fills in numPorts */
		if (retval)
			return retval;
		port = brd->ports;
		for (i = 0; i < brd->numPorts; i++, port++) {
			port->chkPort = 1;
			port->curBaud = 9600L;
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
	brd->loadstat = 1;
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
	int ret;

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
		goto end;
	}

	ret = moxa_load_fw(brd, fw);

	release_firmware(fw);
end:
	return ret;
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
	board->ports = &moxa_ports[i * MAX_PORTS_PER_BOARD];

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

	iounmap(brd->basemem);
	brd->basemem = NULL;
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
			brd->ports = &moxa_ports[isabrds * MAX_PORTS_PER_BOARD];
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
		return put_user(C_CLOCAL(tty) ? 1 : 0, (int __user *)argp);
	case TIOCSSOFTCAR:
		if (get_user(retval, (int __user *)argp))
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

static int moxaLowWaterChk;
static struct mon_str moxaLog;
static int moxaFuncTout = HZ / 2;

static void moxafunc(void __iomem *, int, ushort);
static void moxa_wait_finish(void __iomem *);
static void moxa_low_water_check(void __iomem *);

/*****************************************************************************
 *	Driver level functions: 					     *
 *	2. MoxaDriverIoctl(unsigned int cmd, unsigned long arg, int port);   *
 *	3. MoxaDriverPoll(void);					     *
 *****************************************************************************/
#define	MOXA		0x400
#define MOXA_GET_IQUEUE 	(MOXA + 1)	/* get input buffered count */
#define MOXA_GET_OQUEUE 	(MOXA + 2)	/* get output buffered count */
#define MOXA_GETDATACOUNT       (MOXA + 23)
#define MOXA_GET_IOQUEUE	(MOXA + 27)
#define MOXA_FLUSH_QUEUE	(MOXA + 28)
#define MOXA_GET_CONF		(MOXA + 35)	/* configuration */
#define MOXA_GET_MAJOR          (MOXA + 63)
#define MOXA_GET_CUMAJOR        (MOXA + 64)
#define MOXA_GETMSTATUS         (MOXA + 65)

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
		if ((cmd != MOXA_GET_CONF) && (cmd != MOXA_GETDATACOUNT) &&
		    (cmd != MOXA_GET_IOQUEUE) && (cmd != MOXA_GET_MAJOR) &&
		    (cmd != MOXA_GET_CUMAJOR) && (cmd != MOXA_GETMSTATUS))
			return (-EINVAL);
	}
	switch (cmd) {
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
	}
	}

	return -ENOIOCTLCMD;
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
