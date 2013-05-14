/*
 * include/asm-nios2/flat.h -- uClinux bFLT relocations
 *
 *  Copyright (C) 2004,05  Microtronix Datacom Ltd
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Wentao Xu <wentao@microtronix.com>
 */

#ifndef _ASM_NIOS2_FLAT_H__
#define _ASM_NIOS2_FLAT_H__

#define	flat_reloc_valid(reloc, size)	((reloc) <= (size + 0x8000))

/* The stack is 64-bit aligned for Nios II, so (sp - 1) shall
 * be 64-bit aligned, where -1 is for argc
 */
#define	flat_stack_align(sp)		\
	(sp = (unsigned long *)(((unsigned long)sp - 1) & (-8)))

/* The uClibc port for Nios II expects the argc is followed by argv and envp */
#define	flat_argvp_envp_on_stack()	1

#define	flat_old_ram_flag(flags)	(flags)

/* We store the type of relocation in the top 4 bits of the `relval.' */

/* Convert a relocation entry into an address.  */
static inline unsigned long flat_get_relocate_addr(unsigned long relval)
{
	return relval & 0x0fffffff; /* Mask out top 4-bits */
}

#define FLAT_NIOS2_RELOC_TYPE(relval) ((relval) >> 28)

#define FLAT_NIOS2_R_32			0 /* Normal 32-bit reloc */
#define FLAT_NIOS2_R_HI_LO		1 /* High 16-bits + low 16-bits field */
#define FLAT_NIOS2_R_HIADJ_LO	2 /* High 16-bits adjust + low 16-bits field */
#define FLAT_NIOS2_R_CALL26		4 /* Call imm26 */

#define flat_set_persistent(relval, p)	0

/* Extract the address to be relocated from the symbol reference at rp;
 * relval is the raw relocation-table entry from which RP is derived.
 * rp shall always be 32-bit aligned
 */
static inline unsigned long flat_get_addr_from_rp(unsigned long *rp,
						unsigned long relval,
						unsigned long flags,
						unsigned long *persistent)
{
	switch (FLAT_NIOS2_RELOC_TYPE(relval)) {
	case FLAT_NIOS2_R_32:
		/* Simple 32-bit address. The loader expect it in bigger
		 * endian */
		return htonl(*rp);

	case FLAT_NIOS2_R_HI_LO:
		/* get the two 16-bit immediate value from instructions, then
		 * construct a 32-bit value. Again the loader expect bigger
		 * endian
		 */
		return htonl((((rp[0] >> 6) & 0xFFFF) << 16) |
					  ((rp[1] >> 6) & 0xFFFF));

	case FLAT_NIOS2_R_HIADJ_LO:
		{
		/* get the two 16-bit immediate value from instructions, then
		 * construct a 32-bit value. Again the loader expect bigger
		 * endian
		 */
		unsigned int low, high;
		high = (rp[0] >> 6) & 0xFFFF;
		low  = (rp[1] >> 6) & 0xFFFF;

		if ((low >> 15) & 1)
			high--;

		return htonl((high << 16) | low);
		}
	case FLAT_NIOS2_R_CALL26:
		/* the 26-bit immediate value is actually 28-bit */
		return htonl(((*rp) >> 6) << 2);

	default:
		return ~0;	/* bogus value */
	}
}

/* Insert the address addr into the symbol reference at rp;
 * relval is the raw relocation-table entry from which rp is derived.
 * rp shall always be 32-bit aligned
 */
static inline void flat_put_addr_at_rp(unsigned long *rp, unsigned long addr,
					unsigned long relval)
{
	unsigned long exist_val;
	switch (FLAT_NIOS2_RELOC_TYPE(relval)) {
	case FLAT_NIOS2_R_32:
		/* Simple 32-bit address.  */
		*rp = addr;
		break;

	case FLAT_NIOS2_R_HI_LO:
		exist_val = rp[0];
		rp[0] = ((((exist_val >> 22) << 16) | (addr >> 16)) << 6) |
				(exist_val & 0x3F);
		exist_val = rp[1];
		rp[1] = ((((exist_val >> 22) << 16) | (addr & 0xFFFF)) << 6) |
				(exist_val & 0x3F);
		break;

	case FLAT_NIOS2_R_HIADJ_LO:
		{
		unsigned int high = (addr >> 16);
		if ((addr >> 15) & 1)
			high = (high + 1) & 0xFFFF;
		exist_val = rp[0];
		rp[0] = ((((exist_val >> 22) << 16) | high) << 6) |
				(exist_val & 0x3F);
		exist_val = rp[1];
		rp[1] = ((((exist_val >> 22) << 16) | (addr & 0xFFFF)) << 6) |
				(exist_val & 0x3F);
		break;
		}
	case FLAT_NIOS2_R_CALL26:
		/* the opcode of CALL is 0, so just store the value */
		*rp = ((addr >> 2) << 6);
		break;
	}
}

#endif /* _ASM_NIOS2_FLAT_H__ */
