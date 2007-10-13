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
#include <linux/types.h>
#include <asm/udbg.h>
#include <asm/io.h>

extern u8 real_readb(volatile u8 __iomem  *addr);
extern void real_writeb(u8 data, volatile u8 __iomem *addr);
extern u8 real_205_readb(volatile u8 __iomem  *addr);
extern void real_205_writeb(u8 data, volatile u8 __iomem *addr);

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

#define LCR_DLAB 0x80

static volatile struct NS16550 __iomem *udbg_comport;

static void udbg_550_putc(char c)
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

static int udbg_550_getc(void)
{
	if (udbg_comport) {
		while ((in_8(&udbg_comport->lsr) & LSR_DR) == 0)
			/* wait for char */;
		return in_8(&udbg_comport->rbr);
	}
	return -1;
}

void udbg_init_uart(void __iomem *comport, unsigned int speed,
		    unsigned int clock)
{
	unsigned int dll, base_bauds;

	if (clock == 0)
		clock = 1843200;
	if (speed == 0)
		speed = 9600;

	base_bauds = clock / 16;
	dll = base_bauds / speed;

	if (comport) {
		udbg_comport = (struct NS16550 __iomem *)comport;
		out_8(&udbg_comport->lcr, 0x00);
		out_8(&udbg_comport->ier, 0xff);
		out_8(&udbg_comport->ier, 0x00);
		out_8(&udbg_comport->lcr, LCR_DLAB);
		out_8(&udbg_comport->dll, dll & 0xff);
		out_8(&udbg_comport->dlm, dll >> 8);
		/* 8 data, 1 stop, no parity */
		out_8(&udbg_comport->lcr, 0x03);
		/* RTS/DTR */
		out_8(&udbg_comport->mcr, 0x03);
		/* Clear & enable FIFOs */
		out_8(&udbg_comport->fcr ,0x07);
		udbg_putc = udbg_550_putc;
		udbg_getc = udbg_550_getc;
		udbg_getc_poll = udbg_550_getc_poll;
	}
}

unsigned int udbg_probe_uart_speed(void __iomem *comport, unsigned int clock)
{
	unsigned int dll, dlm, divisor, prescaler, speed;
	u8 old_lcr;
	volatile struct NS16550 __iomem *port = comport;

	old_lcr = in_8(&port->lcr);

	/* select divisor latch registers.  */
	out_8(&port->lcr, LCR_DLAB);

	/* now, read the divisor */
	dll = in_8(&port->dll);
	dlm = in_8(&port->dlm);
	divisor = dlm << 8 | dll;

	/* check prescaling */
	if (in_8(&port->mcr) & 0x80)
		prescaler = 4;
	else
		prescaler = 1;

	/* restore the LCR */
	out_8(&port->lcr, old_lcr);

	/* calculate speed */
	speed = (clock / prescaler) / (divisor * 16);

	/* sanity check */
	if (speed < 0 || speed > (clock / 16))
		speed = 9600;

	return speed;
}

#ifdef CONFIG_PPC_MAPLE
void udbg_maple_real_putc(char c)
{
	if (udbg_comport) {
		while ((real_readb(&udbg_comport->lsr) & LSR_THRE) == 0)
			/* wait for idle */;
		real_writeb(c, &udbg_comport->thr); eieio();
		if (c == '\n')
			udbg_maple_real_putc('\r');
	}
}

void __init udbg_init_maple_realmode(void)
{
	udbg_comport = (volatile struct NS16550 __iomem *)0xf40003f8;

	udbg_putc = udbg_maple_real_putc;
	udbg_getc = NULL;
	udbg_getc_poll = NULL;
}
#endif /* CONFIG_PPC_MAPLE */

#ifdef CONFIG_PPC_PASEMI
void udbg_pas_real_putc(char c)
{
	if (udbg_comport) {
		while ((real_205_readb(&udbg_comport->lsr) & LSR_THRE) == 0)
			/* wait for idle */;
		real_205_writeb(c, &udbg_comport->thr); eieio();
		if (c == '\n')
			udbg_pas_real_putc('\r');
	}
}

void udbg_init_pas_realmode(void)
{
	udbg_comport = (volatile struct NS16550 __iomem *)0xfcff03f8UL;

	udbg_putc = udbg_pas_real_putc;
	udbg_getc = NULL;
	udbg_getc_poll = NULL;
}
#endif /* CONFIG_PPC_MAPLE */

#ifdef CONFIG_PPC_EARLY_DEBUG_44x
#include <platforms/44x/44x.h>

static void udbg_44x_as1_putc(char c)
{
	if (udbg_comport) {
		while ((as1_readb(&udbg_comport->lsr) & LSR_THRE) == 0)
			/* wait for idle */;
		as1_writeb(c, &udbg_comport->thr); eieio();
		if (c == '\n')
			udbg_44x_as1_putc('\r');
	}
}

static int udbg_44x_as1_getc(void)
{
	if (udbg_comport) {
		while ((as1_readb(&udbg_comport->lsr) & LSR_DR) == 0)
			; /* wait for char */
		return as1_readb(&udbg_comport->rbr);
	}
	return -1;
}

void __init udbg_init_44x_as1(void)
{
	udbg_comport =
		(volatile struct NS16550 __iomem *)PPC44x_EARLY_DEBUG_VIRTADDR;

	udbg_putc = udbg_44x_as1_putc;
	udbg_getc = udbg_44x_as1_getc;
}
#endif /* CONFIG_PPC_EARLY_DEBUG_44x */
