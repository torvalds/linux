// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - Google LLC
 */

#include <nvhe/pkvm.h>
#include <nvhe/spinlock.h>

static void (*__hyp_putc)(char c);

static inline void __hyp_putx4(unsigned int x)
{
	x &= 0xf;
	if (x <= 9)
		x += '0';
	else
		x += ('a' - 0xa);

	__hyp_putc(x);
}

static inline void __hyp_putx4n(unsigned long x, int n)
{
	int i = n >> 2;

	__hyp_putc('0');
	__hyp_putc('x');

	while (i--)
		__hyp_putx4(x >> (4 * i));

	__hyp_putc('\n');
	__hyp_putc('\r');
}

static inline bool hyp_serial_enabled(void)
{
	return !!READ_ONCE(__hyp_putc);
}

void hyp_puts(const char *s)
{
	if (!hyp_serial_enabled())
		return;

	while (*s)
		__hyp_putc(*s++);

	__hyp_putc('\n');
	__hyp_putc('\r');
}

void hyp_putx64(u64 x)
{
	if (hyp_serial_enabled())
		__hyp_putx4n(x, 64);
}

void hyp_putc(char c)
{
	if (hyp_serial_enabled())
		__hyp_putc(c);
}

int __pkvm_register_serial_driver(void (*cb)(char))
{
	return cmpxchg(&__hyp_putc, NULL, cb) ? -EBUSY : 0;
}
