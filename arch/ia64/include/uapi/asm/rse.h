/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_IA64_RSE_H
#define _ASM_IA64_RSE_H

/*
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Register stack engine related helper functions.  This file may be
 * used in applications, so be careful about the name-space and give
 * some consideration to non-GNU C compilers (though __inline__ is
 * fine).
 */

static __inline__ unsigned long
ia64_rse_slot_num (unsigned long *addr)
{
	return (((unsigned long) addr) >> 3) & 0x3f;
}

/*
 * Return TRUE if ADDR is the address of an RNAT slot.
 */
static __inline__ unsigned long
ia64_rse_is_rnat_slot (unsigned long *addr)
{
	return ia64_rse_slot_num(addr) == 0x3f;
}

/*
 * Returns the address of the RNAT slot that covers the slot at
 * address SLOT_ADDR.
 */
static __inline__ unsigned long *
ia64_rse_rnat_addr (unsigned long *slot_addr)
{
	return (unsigned long *) ((unsigned long) slot_addr | (0x3f << 3));
}

/*
 * Calculate the number of registers in the dirty partition starting at BSPSTORE and
 * ending at BSP.  This isn't simply (BSP-BSPSTORE)/8 because every 64th slot stores
 * ar.rnat.
 */
static __inline__ unsigned long
ia64_rse_num_regs (unsigned long *bspstore, unsigned long *bsp)
{
	unsigned long slots = (bsp - bspstore);

	return slots - (ia64_rse_slot_num(bspstore) + slots)/0x40;
}

/*
 * The inverse of the above: given bspstore and the number of
 * registers, calculate ar.bsp.
 */
static __inline__ unsigned long *
ia64_rse_skip_regs (unsigned long *addr, long num_regs)
{
	long delta = ia64_rse_slot_num(addr) + num_regs;

	if (num_regs < 0)
		delta -= 0x3e;
	return addr + num_regs + delta/0x3f;
}

#endif /* _ASM_IA64_RSE_H */
