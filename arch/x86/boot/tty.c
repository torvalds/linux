/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Very simple screen and serial I/O
 */

#include "boot.h"

#define DEFAULT_SERIAL_PORT 0x3f8 /* ttyS0 */

static int	early_serial_base;

#define XMTRDY          0x20

#define DLAB		0x80

#define TXR             0       /*  Transmit register (WRITE) */
#define RXR             0       /*  Receive register  (READ)  */
#define IER             1       /*  Interrupt Enable          */
#define IIR             2       /*  Interrupt ID              */
#define FCR             2       /*  FIFO control              */
#define LCR             3       /*  Line control              */
#define MCR             4       /*  Modem control             */
#define LSR             5       /*  Line Status               */
#define MSR             6       /*  Modem Status              */
#define DLL             0       /*  Divisor Latch Low         */
#define DLH             1       /*  Divisor latch High        */

#define DEFAULT_BAUD 9600

/*
 * These functions are in .inittext so they can be used to signal
 * error during initialization.
 */

static void __attribute__((section(".inittext"))) serial_putchar(int ch)
{
	unsigned timeout = 0xffff;

	while ((inb(early_serial_base + LSR) & XMTRDY) == 0 && --timeout)
		cpu_relax();

	outb(ch, early_serial_base + TXR);
}

static void __attribute__((section(".inittext"))) bios_putchar(int ch)
{
	struct biosregs ireg;

	initregs(&ireg);
	ireg.bx = 0x0007;
	ireg.cx = 0x0001;
	ireg.ah = 0x0e;
	ireg.al = ch;
	intcall(0x10, &ireg, NULL);
}

void __attribute__((section(".inittext"))) putchar(int ch)
{
	if (ch == '\n')
		putchar('\r');	/* \n -> \r\n */

	bios_putchar(ch);

	if (early_serial_base != 0)
		serial_putchar(ch);
}

void __attribute__((section(".inittext"))) puts(const char *str)
{
	while (*str)
		putchar(*str++);
}

/*
 * Read the CMOS clock through the BIOS, and return the
 * seconds in BCD.
 */

static u8 gettime(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ah = 0x02;
	intcall(0x1a, &ireg, &oreg);

	return oreg.dh;
}

/*
 * Read from the keyboard
 */
int getchar(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	/* ireg.ah = 0x00; */
	intcall(0x16, &ireg, &oreg);

	return oreg.al;
}

static int kbd_pending(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ah = 0x01;
	intcall(0x16, &ireg, &oreg);

	return !(oreg.eflags & X86_EFLAGS_ZF);
}

void kbd_flush(void)
{
	for (;;) {
		if (!kbd_pending())
			break;
		getchar();
	}
}

int getchar_timeout(void)
{
	int cnt = 30;
	int t0, t1;

	t0 = gettime();

	while (cnt) {
		if (kbd_pending())
			return getchar();

		t1 = gettime();
		if (t0 != t1) {
			cnt--;
			t0 = t1;
		}
	}

	return 0;		/* Timeout! */
}

static void early_serial_init(int baud)
{
	unsigned char c;
	unsigned divisor;

	outb(0x3, early_serial_base + LCR);	/* 8n1 */
	outb(0, early_serial_base + IER);	/* no interrupt */
	outb(0, early_serial_base + FCR);	/* no fifo */
	outb(0x3, early_serial_base + MCR);	/* DTR + RTS */

	divisor	= 115200 / baud;
	c = inb(early_serial_base + LCR);
	outb(c | DLAB, early_serial_base + LCR);
	outb(divisor & 0xff, early_serial_base + DLL);
	outb((divisor >> 8) & 0xff, early_serial_base + DLH);
	outb(c & ~DLAB, early_serial_base + LCR);
}

static int parse_earlyprintk(void)
{
	int baud = DEFAULT_BAUD;
	char arg[32];
	int pos = 0;

	if (cmdline_find_option("earlyprintk", arg, sizeof arg) > 0) {
		char *e;

		if (!strncmp(arg, "serial", 6)) {
			early_serial_base = DEFAULT_SERIAL_PORT;
			pos += 6;
		}

		if (arg[pos] == ',')
			pos++;

		if (!strncmp(arg, "ttyS", 4)) {
			static const int bases[] = { 0x3f8, 0x2f8 };
			int port = 0;

			if (!strncmp(arg + pos, "ttyS", 4))
				pos += 4;

			if (arg[pos++] == '1')
				port = 1;

			early_serial_base = bases[port];
		}

		if (arg[pos] == ',')
			pos++;

		baud = simple_strtoull(arg + pos, &e, 0);
		if (baud == 0 || arg + pos == e)
			baud = DEFAULT_BAUD;
	}

	return baud;
}

#define BASE_BAUD (1843200/16)
static unsigned int probe_baud(int port)
{
	unsigned char lcr, dll, dlh;
	unsigned int quot;

	lcr = inb(port + LCR);
	outb(lcr | DLAB, port + LCR);
	dll = inb(port + DLL);
	dlh = inb(port + DLH);
	outb(lcr, port + LCR);
	quot = (dlh << 8) | dll;

	return BASE_BAUD / quot;
}

static int parse_console_uart8250(void)
{
	char optstr[64], *options;
	int baud = DEFAULT_BAUD;

	/*
	 * console=uart8250,io,0x3f8,115200n8
	 * need to make sure it is last one console !
	 */
	if (cmdline_find_option("console", optstr, sizeof optstr) <= 0)
		return baud;

	options = optstr;

	if (!strncmp(options, "uart8250,io,", 12))
		early_serial_base = simple_strtoull(options + 12, &options, 0);
	else if (!strncmp(options, "uart,io,", 8))
		early_serial_base = simple_strtoull(options + 8, &options, 0);
	else
		return baud;

	if (options && (options[0] == ','))
		baud = simple_strtoull(options + 1, &options, 0);
	else
		baud = probe_baud(early_serial_base);

	return baud;
}

void console_init(void)
{
	int baud;

	baud = parse_earlyprintk();

	if (!early_serial_base)
		baud = parse_console_uart8250();

	if (early_serial_base != 0)
		early_serial_init(baud);
}
