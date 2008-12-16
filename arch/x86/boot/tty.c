/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Very simple screen I/O
 * XXX: Probably should add very simple serial I/O?
 */

#include "boot.h"

/*
 * These functions are in .inittext so they can be used to signal
 * error during initialization.
 */

void __attribute__((section(".inittext"))) putchar(int ch)
{
	unsigned char c = ch;

	if (c == '\n')
		putchar('\r');	/* \n -> \r\n */

	/* int $0x10 is known to have bugs involving touching registers
	   it shouldn't.  Be extra conservative... */
	asm volatile("pushal; pushw %%ds; int $0x10; popw %%ds; popal"
		     : : "b" (0x0007), "c" (0x0001), "a" (0x0e00|ch));
}

void __attribute__((section(".inittext"))) puts(const char *str)
{
	int n = 0;
	while (*str) {
		putchar(*str++);
		n++;
	}
}

/*
 * Read the CMOS clock through the BIOS, and return the
 * seconds in BCD.
 */

static u8 gettime(void)
{
	u16 ax = 0x0200;
	u16 cx, dx;

	asm volatile("int $0x1a"
		     : "+a" (ax), "=c" (cx), "=d" (dx)
		     : : "ebx", "esi", "edi");

	return dx >> 8;
}

/*
 * Read from the keyboard
 */
int getchar(void)
{
	u16 ax = 0;
	asm volatile("int $0x16" : "+a" (ax));

	return ax & 0xff;
}

static int kbd_pending(void)
{
	u8 pending;
	asm volatile("int $0x16; setnz %0"
		     : "=qm" (pending)
		     : "a" (0x0100));
	return pending;
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
