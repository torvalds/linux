/* Debugging stuff for the MN10300-based processors
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/sched.h>
#include <asm/serial-regs.h>

#undef MN10300_CONSOLE_ON_SERIO

/*
 * write a string directly through one of the serial ports on-board the MN10300
 */
#ifdef MN10300_CONSOLE_ON_SERIO
void debug_to_serial_mnser(const char *p, int n)
{
	char ch;

	for (; n > 0; n--) {
		ch = *p++;

#if MN10300_CONSOLE_ON_SERIO == 0
		while (SC0STR & (SC01STR_TBF)) continue;
		SC0TXB = ch;
		while (SC0STR & (SC01STR_TBF)) continue;
		if (ch == 0x0a) {
			SC0TXB = 0x0d;
			while (SC0STR & (SC01STR_TBF)) continue;
		}

#elif MN10300_CONSOLE_ON_SERIO == 1
		while (SC1STR & (SC01STR_TBF)) continue;
		SC1TXB = ch;
		while (SC1STR & (SC01STR_TBF)) continue;
		if (ch == 0x0a) {
			SC1TXB = 0x0d;
			while (SC1STR & (SC01STR_TBF)) continue;
		}

#elif MN10300_CONSOLE_ON_SERIO == 2
		while (SC2STR & (SC2STR_TBF)) continue;
		SC2TXB = ch;
		while (SC2STR & (SC2STR_TBF)) continue;
		if (ch == 0x0a) {
			SC2TXB = 0x0d;
			while (SC2STR & (SC2STR_TBF)) continue;
		}

#endif
	}
}
#endif

