/* MN10300 Non-trivial bit operations
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/module.h>
#include <asm/bitops.h>
#include <asm/system.h>

/*
 * try flipping a bit using BSET and BCLR
 */
void change_bit(unsigned long nr, volatile void *addr)
{
	if (test_bit(nr, addr))
		goto try_clear_bit;

try_set_bit:
	if (!test_and_set_bit(nr, addr))
		return;

try_clear_bit:
	if (test_and_clear_bit(nr, addr))
		return;

	goto try_set_bit;
}

/*
 * try flipping a bit using BSET and BCLR and returning the old value
 */
int test_and_change_bit(unsigned long nr, volatile void *addr)
{
	if (test_bit(nr, addr))
		goto try_clear_bit;

try_set_bit:
	if (!test_and_set_bit(nr, addr))
		return 0;

try_clear_bit:
	if (test_and_clear_bit(nr, addr))
		return 1;

	goto try_set_bit;
}
