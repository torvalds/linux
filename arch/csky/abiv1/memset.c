// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/types.h>

void *memset(void *dest, int c, size_t l)
{
	char *d = dest;
	int ch = c & 0xff;
	int tmp = (ch | ch << 8 | ch << 16 | ch << 24);

	while (((uintptr_t)d & 0x3) && l--)
		*d++ = ch;

	while (l >= 16) {
		*(((u32 *)d))   = tmp;
		*(((u32 *)d)+1) = tmp;
		*(((u32 *)d)+2) = tmp;
		*(((u32 *)d)+3) = tmp;
		l -= 16;
		d += 16;
	}

	while (l > 3) {
		*(((u32 *)d)) = tmp;
		l -= 4;
		d += 4;
	}

	while (l) {
		*d = ch;
		l--;
		d++;
	}

	return dest;
}
