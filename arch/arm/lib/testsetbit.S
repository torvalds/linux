/*
 *  linux/arch/arm/lib/testsetbit.S
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

ENTRY(_test_and_set_bit_be)
		eor	r0, r0, #0x18		@ big endian byte ordering
ENTRY(_test_and_set_bit_le)
	testop	orreq, streqb
ENDPROC(_test_and_set_bit_be)
ENDPROC(_test_and_set_bit_le)
