/*
 * uClinux flat-format executables
 *
 * Copyright (C) 2005 John Williams <jwilliams@itee.uq.edu.au>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#ifndef _ASM_MICROBLAZE_FLAT_H
#define _ASM_MICROBLAZE_FLAT_H

#include <asm/unaligned.h>

/*
 * Microblaze works a little differently from other arches, because
 * of the MICROBLAZE_64 reloc type. Here, a 32 bit address is split
 * over two instructions, an 'imm' instruction which provides the top
 * 16 bits, then the instruction "proper" which provides the low 16
 * bits.
 */

/*
 * Crack open a symbol reference and extract the address to be
 * relocated. rp is a potentially unaligned pointer to the
 * reference
 */

static inline int flat_get_addr_from_rp(u32 __user *rp, u32 relval, u32 flags,
					u32 *addr)
{
	u32 *p = (__force u32 *)rp;

	/* Is it a split 64/32 reference? */
	if (relval & 0x80000000) {
		/* Grab the two halves of the reference */
		u32 val_hi, val_lo;

		val_hi = get_unaligned(p);
		val_lo = get_unaligned(p+1);

		/* Crack the address out */
		*addr = ((val_hi & 0xffff) << 16) + (val_lo & 0xffff);
	} else {
		/* Get the address straight out */
		*addr = get_unaligned(p);
	}

	return 0;
}

/*
 * Insert an address into the symbol reference at rp. rp is potentially
 * unaligned.
 */

static inline int
flat_put_addr_at_rp(u32 __user *rp, u32 addr, u32 relval)
{
	u32 *p = (__force u32 *)rp;
	/* Is this a split 64/32 reloc? */
	if (relval & 0x80000000) {
		/* Get the two "halves" */
		unsigned long val_hi = get_unaligned(p);
		unsigned long val_lo = get_unaligned(p + 1);

		/* insert the address */
		val_hi = (val_hi & 0xffff0000) | addr >> 16;
		val_lo = (val_lo & 0xffff0000) | (addr & 0xffff);

		/* store the two halves back into memory */
		put_unaligned(val_hi, p);
		put_unaligned(val_lo, p+1);
	} else {
		/* Put it straight in, no messing around */
		put_unaligned(addr, p);
	}
	return 0;
}

#define	flat_get_relocate_addr(rel)	(rel & 0x7fffffff)

#endif /* _ASM_MICROBLAZE_FLAT_H */
