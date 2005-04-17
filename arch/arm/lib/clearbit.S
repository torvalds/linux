/*
 *  linux/arch/arm/lib/clearbit.S
 *
 *  Copyright (C) 1995-1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include "bitops.h"
                .text

/*
 * Purpose  : Function to clear a bit
 * Prototype: int clear_bit(int bit, void *addr)
 */
ENTRY(_clear_bit_be)
		eor	r0, r0, #0x18		@ big endian byte ordering
ENTRY(_clear_bit_le)
	bitop	bic
