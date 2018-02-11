/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_EXTABLE_H
#define _ASM_EXTABLE_H

/*
 * About the exception table:
 *
 * - insn is a 32-bit pc-relative offset from the faulting insn.
 * - nextinsn is a 16-bit offset off of the faulting instruction
 *   (not off of the *next* instruction as branches are).
 * - errreg is the register in which to place -EFAULT.
 * - valreg is the final target register for the load sequence
 *   and will be zeroed.
 *
 * Either errreg or valreg may be $31, in which case nothing happens.
 *
 * The exception fixup information "just so happens" to be arranged
 * as in a MEM format instruction.  This lets us emit our three
 * values like so:
 *
 *      lda valreg, nextinsn(errreg)
 *
 */

struct exception_table_entry
{
	signed int insn;
	union exception_fixup {
		unsigned unit;
		struct {
			signed int nextinsn : 16;
			unsigned int errreg : 5;
			unsigned int valreg : 5;
		} bits;
	} fixup;
};

/* Returns the new pc */
#define fixup_exception(map_reg, _fixup, pc)			\
({								\
	if ((_fixup)->fixup.bits.valreg != 31)			\
		map_reg((_fixup)->fixup.bits.valreg) = 0;	\
	if ((_fixup)->fixup.bits.errreg != 31)			\
		map_reg((_fixup)->fixup.bits.errreg) = -EFAULT;	\
	(pc) + (_fixup)->fixup.bits.nextinsn;			\
})

#define ARCH_HAS_RELATIVE_EXTABLE

#define swap_ex_entry_fixup(a, b, tmp, delta)			\
	do {							\
		(a)->fixup.unit = (b)->fixup.unit;		\
		(b)->fixup.unit = (tmp).fixup.unit;		\
	} while (0)

#endif
