#ifndef _PPC_BOOT_REG_H
#define _PPC_BOOT_REG_H
/*
 * Copyright 2007 Davud Gibson, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

static inline u32 mfpvr(void)
{
	u32 pvr;
	asm volatile ("mfpvr	%0" : "=r"(pvr));
	return pvr;
}

register void *__stack_pointer asm("r1");
#define get_sp()	(__stack_pointer)

#endif	/* _PPC_BOOT_REG_H */
