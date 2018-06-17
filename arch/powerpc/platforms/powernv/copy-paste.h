/*
 * Copyright 2016-17 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/ppc-opcode.h>
#include <asm/reg.h>

/*
 * Copy/paste instructions:
 *
 *	copy RA,RB
 *		Copy contents of address (RA) + effective_address(RB)
 *		to internal copy-buffer.
 *
 *	paste RA,RB
 *		Paste contents of internal copy-buffer to the address
 *		(RA) + effective_address(RB)
 */
static inline int vas_copy(void *crb, int offset)
{
	asm volatile(PPC_COPY(%0, %1)";"
		:
		: "b" (offset), "b" (crb)
		: "memory");

	return 0;
}

static inline int vas_paste(void *paste_address, int offset)
{
	u32 cr;

	cr = 0;
	asm volatile(PPC_PASTE(%1, %2)";"
		"mfocrf %0, 0x80;"
		: "=r" (cr)
		: "b" (offset), "b" (paste_address)
		: "memory", "cr0");

	/* We mask with 0xE to ignore SO */
	return (cr >> CR0_SHIFT) & 0xE;
}
