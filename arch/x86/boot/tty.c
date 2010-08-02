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

int early_serial_base;

#define XMTRDY          0x20

#define TXR             0       /*  Transmit register (WRITE) */
#define LSR             5       /*  Line Status               */

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

