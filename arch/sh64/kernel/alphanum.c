/*
 * arch/sh64/kernel/alphanum.c
 *
 * Copyright (C) 2002 Stuart Menefy <stuart.menefy@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Machine-independent functions for handling 8-digit alphanumeric display
 * (e.g. Agilent HDSP-253x)
 */
#include <linux/stddef.h>
#include <linux/sched.h>

void mach_alphanum(int pos, unsigned char val);
void mach_led(int pos, int val);

void print_seg(char *file, int line)
{
	int i;
	unsigned int nibble;

	for (i = 0; i < 5; i++) {
		mach_alphanum(i, file[i]);
	}

	for (i = 0; i < 3; i++) {
		nibble = ((line >> (i * 4)) & 0xf);
		mach_alphanum(7 - i, nibble + ((nibble > 9) ? 55 : 48));
	}
}

void print_seg_num(unsigned num)
{
	int i;
	unsigned int nibble;

	for (i = 0; i < 8; i++) {
		nibble = ((num >> (i * 4)) & 0xf);

		mach_alphanum(7 - i, nibble + ((nibble > 9) ? 55 : 48));
	}
}

