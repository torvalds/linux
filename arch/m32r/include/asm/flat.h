/*
 * include/asm-m32r/flat.h
 *
 * uClinux flat-format executables
 *
 * Copyright (C) 2004  Kazuhiro Inaoka
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive for
 * more details.
 */
#ifndef __ASM_M32R_FLAT_H
#define __ASM_M32R_FLAT_H

#define	flat_argvp_envp_on_stack()		0
#define	flat_old_ram_flag(flags)		(flags)
#define	flat_set_persistent(relval, p)		0
#define	flat_reloc_valid(reloc, size)		\
	(((reloc) - textlen_for_m32r_lo16_data) <= (size))
#define flat_get_addr_from_rp(rp, relval, flags, persistent) \
	m32r_flat_get_addr_from_rp(rp, relval, (text_len) )

#define flat_put_addr_at_rp(rp, addr, relval) \
	m32r_flat_put_addr_at_rp(rp, addr, relval)

/* Convert a relocation entry into an address.  */
static inline unsigned long
flat_get_relocate_addr (unsigned long relval)
{
        return relval & 0x00ffffff; /* Mask out top 8-bits */
}

#define	flat_m32r_get_reloc_type(relval)	((relval) >> 24)

#define M32R_SETH_OPCODE	0xd0c00000 /* SETH instruction code */

#define FLAT_M32R_32		0x00	/* 32bits reloc */
#define FLAT_M32R_24		0x01	/* unsigned 24bits reloc */
#define FLAT_M32R_16		0x02	/* 16bits reloc */
#define FLAT_M32R_LO16		0x03	/* signed low 16bits reloc (low()) */
#define FLAT_M32R_LO16_DATA	0x04	/* signed low 16bits reloc (low())
					   for a symbol in .data section */
					/* High 16bits of an address used
					   when the lower 16bbits are treated
					   as unsigned.
                                           To create SETH instruction only.
					   0x1X: X means a number of register.
					   0x10 - 0x3F are reserved. */
#define FLAT_M32R_HI16_ULO	0x10	/* reloc for SETH Rn,#high(imm16) */
					/* High 16bits of an address used
					   when the lower 16bbits are treated
					   as signed.
                                           To create SETH instruction only.
					   0x2X: X means a number of register.
					   0x20 - 0x4F are reserved. */
#define FLAT_M32R_HI16_SLO	0x20	/* reloc for SETH Rn,#shigh(imm16) */

static unsigned long textlen_for_m32r_lo16_data = 0;

static inline unsigned long m32r_flat_get_addr_from_rp (unsigned long *rp,
                                                        unsigned long relval,
						        unsigned long textlen)
{
        unsigned int reloc = flat_m32r_get_reloc_type (relval);
	textlen_for_m32r_lo16_data = 0;
	if (reloc & 0xf0) {
		unsigned long addr = htonl(*rp);
		switch (reloc & 0xf0)
		{
		case FLAT_M32R_HI16_ULO:
		case FLAT_M32R_HI16_SLO:
			if (addr == 0) {
				/* put "seth Rn,#0x0" instead of 0 (addr). */
				*rp = (M32R_SETH_OPCODE | ((reloc & 0x0f)<<24));
			}
			return addr;
		default:
			break;
		}
	} else {
		switch (reloc)
		{
		case FLAT_M32R_LO16:
			return htonl(*rp) & 0xFFFF;
		case FLAT_M32R_LO16_DATA:
                        /* FIXME: The return value will decrease by textlen
			   at m32r_flat_put_addr_at_rp () */
			textlen_for_m32r_lo16_data = textlen;
			return (htonl(*rp) & 0xFFFF) + textlen;
		case FLAT_M32R_16:
			return htons(*(unsigned short *)rp) & 0xFFFF;
		case FLAT_M32R_24:
			return htonl(*rp) & 0xFFFFFF;
		case FLAT_M32R_32:
			return htonl(*rp);
		default:
			break;
		}
	}
	return ~0;      /* bogus value */
}

static inline void m32r_flat_put_addr_at_rp (unsigned long *rp,
					     unsigned long addr,
                                             unsigned long relval)
{
        unsigned int reloc = flat_m32r_get_reloc_type (relval);
	if (reloc & 0xf0) {
		unsigned long Rn = reloc & 0x0f; /* get a number of register */
		Rn <<= 24; /* 0x0R000000 */
		reloc &= 0xf0;
		switch (reloc)
		{
		case FLAT_M32R_HI16_ULO: /* To create SETH Rn,#high(imm16) */
			*rp = (M32R_SETH_OPCODE | Rn
			       | ((addr >> 16) & 0xFFFF));
			break;
		case FLAT_M32R_HI16_SLO: /* To create SETH Rn,#shigh(imm16) */
			*rp = (M32R_SETH_OPCODE | Rn
			       | (((addr >> 16) + ((addr & 0x8000) ? 1 : 0))
				  & 0xFFFF));
			break;
		}
	} else {
		switch (reloc) {
		case FLAT_M32R_LO16_DATA:
			addr -= textlen_for_m32r_lo16_data;
			textlen_for_m32r_lo16_data = 0;
		case FLAT_M32R_LO16:
			*rp = (htonl(*rp) & 0xFFFF0000) | (addr & 0xFFFF);
			break;
		case FLAT_M32R_16:
			*(unsigned short *)rp = addr & 0xFFFF;
			break;
		case FLAT_M32R_24:
			*rp = (htonl(*rp) & 0xFF000000) | (addr & 0xFFFFFF);
			break;
		case FLAT_M32R_32:
			*rp = addr;
			break;
		}
	}
}

#endif /* __ASM_M32R_FLAT_H */
