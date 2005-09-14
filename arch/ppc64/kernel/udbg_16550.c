/*
 * udbg for for NS16550 compatable serial ports
 *
 * Copyright (C) 2001-2005 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <asm/udbg.h>
#include <asm/io.h>

extern u8 real_readb(volatile u8 __iomem  *addr);
extern void real_writeb(u8 data, volatile u8 __iomem *addr);

struct NS16550 {
	/* this struct must be packed */
	unsigned char rbr;  /* 0 */
	unsigned char ier;  /* 1 */
	unsigned char fcr;  /* 2 */
	unsigned char lcr;  /* 3 */
	unsigned char mcr;  /* 4 */
	unsigned char lsr;  /* 5 */
	unsigned char msr;  /* 6 */
	unsigned char scr;  /* 7 */
};

#define thr rbr
#define iir fcr
#define dll rbr
#define dlm ier
#define dlab lcr

#define LSR_DR   0x01  /* Data ready */
#define LSR_OE   0x02  /* Overrun */
#define LSR_PE   0x04  /* Parity error */
#define LSR_FE   0x08  /* Framing error */
#define LSR_BI   0x10  /* Break */
#define LSR_THRE 0x20  /* Xmit holding register empty */
#define LSR_TEMT 0x40  /* Xmitter empty */
#define LSR_ERR  0x80  /* Error */

static volatile struct NS16550 __iomem *udbg_comport;

static void udbg_550_putc(unsigned char c)
{
	if (udbg_comport) {
		while ((in_8(&udbg_comport->lsr) & LSR_THRE) == 0)
			/* wait for idle */;
		out_8(&udbg_comport->thr, c);
		if (c == '\n')
			udbg_550_putc('\r');
	}
}

static int udbg_550_getc_poll(void)
{
	if (udbg_comport) {
		if ((in_8(&udbg_comport->lsr) & LSR_DR) != 0)
			return in_8(&udbg_comport->rbr);
		else
			return -1;
	}
	return -1;
}

static unsigned char udbg_550_getc(void)
{
	if (udbg_comport) {
		while ((in_8(&udbg_comport->lsr) & LSR_DR) == 0)
			/* wait for char */;
		return in_8(&udbg_comport->rbr);
	}
	return 0;
}

void udbg_init_uart(void __iomem *comport, unsigned int speed)
{
	u16 dll = speed ? (115200 / speed) : 12;

	if (comport) {
		udbg_comport = (struct NS16550 __iomem *)comport;
		out_8(&udbg_comport->lcr, 0x00);
		out_8(&udbg_comport->ier, 0xff);
		out_8(&udbg_comport->ier, 0x00);
		out_8(&udbg_comport->lcr, 0x80);	/* Access baud rate */
		out_8(&udbg_comport->dll, dll & 0xff);	/* 1 = 115200,  2 = 57600,
							   3 = 38400, 12 = 9600 baud */
		out_8(&udbg_comport->dlm, dll >> 8);	/* dll >> 8 which should be zero
							   for fast rates; */
		out_8(&udbg_comport->lcr, 0x03);	/* 8 data, 1 stop, no parity */
		out_8(&udbg_comport->mcr, 0x03);	/* RTS/DTR */
		out_8(&udbg_comport->fcr ,0x07);	/* Clear & enable FIFOs */
		udbg_putc = udbg_550_putc;
		udbg_getc = udbg_550_getc;
		udbg_getc_poll = udbg_550_getc_poll;
	}
}

#ifdef CONFIG_PPC_MAPLE
void udbg_maple_real_putc(unsigned char c)
{
	if (udbg_comport) {
		while ((real_readb(&udbg_comport->lsr) & LSR_THRE) == 0)
			/* wait for idle */;
		real_writeb(c, &udbg_comport->thr); eieio();
		if (c == '\n')
			udbg_maple_real_putc('\r');
	}
}

void udbg_init_maple_realmode(void)
{
	udbg_comport = (volatile struct NS16550 __iomem *)0xf40003f8;

	udbg_putc = udbg_maple_real_putc;
	udbg_getc = NULL;
	udbg_getc_poll = NULL;
}
#endif /* CONFIG_PPC_MAPLE */
