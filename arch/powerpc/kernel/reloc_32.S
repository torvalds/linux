/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Code to process dynamic relocations for PPC32.
 *
 * Copyrights (C) IBM Corporation, 2011.
 *	Author: Suzuki Poulose <suzuki@in.ibm.com>
 *
 *  - Based on ppc64 code - reloc_64.S
 */

#include <asm/ppc_asm.h>

/* Dynamic section table entry tags */
DT_RELA = 7			/* Tag for Elf32_Rela section */
DT_RELASZ = 8			/* Size of the Rela relocs */
DT_RELAENT = 9			/* Size of one Rela reloc entry */

STN_UNDEF = 0			/* Undefined symbol index */
STB_LOCAL = 0			/* Local binding for the symbol */

R_PPC_ADDR16_LO = 4		/* Lower half of (S+A) */
R_PPC_ADDR16_HI = 5		/* Upper half of (S+A) */
R_PPC_ADDR16_HA = 6		/* High Adjusted (S+A) */
R_PPC_RELATIVE = 22

/*
 * r3 = desired final address
 */

_GLOBAL(relocate)

	mflr	r0		/* Save our LR */
	bl	0f		/* Find our current runtime address */
0:	mflr	r12		/* Make it accessible */
	mtlr	r0

	lwz	r11, (p_dyn - 0b)(r12)
	add	r11, r11, r12	/* runtime address of .dynamic section */
	lwz	r9, (p_rela - 0b)(r12)
	add	r9, r9, r12	/* runtime address of .rela.dyn section */
	lwz	r10, (p_st - 0b)(r12)
	add	r10, r10, r12	/* runtime address of _stext section */
	lwz	r13, (p_sym - 0b)(r12)
	add	r13, r13, r12	/* runtime address of .dynsym section */

	/*
	 * Scan the dynamic section for RELA, RELASZ entries
	 */
	li	r6, 0
	li	r7, 0
	li	r8, 0
1:	lwz	r5, 0(r11)	/* ELF_Dyn.d_tag */
	cmpwi	r5, 0		/* End of ELF_Dyn[] */
	beq	eodyn
	cmpwi	r5, DT_RELA
	bne	relasz
	lwz	r7, 4(r11)	/* r7 = rela.link */
	b	skip
relasz:
	cmpwi	r5, DT_RELASZ
	bne	relaent
	lwz	r8, 4(r11)	/* r8 = Total Rela relocs size */
	b	skip
relaent:
	cmpwi	r5, DT_RELAENT
	bne	skip
	lwz	r6, 4(r11)	/* r6 = Size of one Rela reloc */
skip:
	addi	r11, r11, 8
	b	1b
eodyn:				/* End of Dyn Table scan */

	/* Check if we have found all the entries */
	cmpwi	r7, 0
	beq	done
	cmpwi	r8, 0
	beq	done
	cmpwi	r6, 0
	beq	done


	/*
	 * Work out the current offset from the link time address of .rela
	 * section.
	 *  cur_offset[r7] = rela.run[r9] - rela.link [r7]
	 *  _stext.link[r12] = _stext.run[r10] - cur_offset[r7]
	 *  final_offset[r3] = _stext.final[r3] - _stext.link[r12]
	 */
	subf	r7, r7, r9	/* cur_offset */
	subf	r12, r7, r10
	subf	r3, r12, r3	/* final_offset */

	subf	r8, r6, r8	/* relaz -= relaent */
	/*
	 * Scan through the .rela table and process each entry
	 * r9	- points to the current .rela table entry
	 * r13	- points to the symbol table
	 */

	/*
	 * Check if we have a relocation based on symbol
	 * r5 will hold the value of the symbol.
	 */
applyrela:
	lwz	r4, 4(r9)		/* r4 = rela.r_info */
	srwi	r5, r4, 8		/* ELF32_R_SYM(r_info) */
	cmpwi	r5, STN_UNDEF	/* sym == STN_UNDEF ? */
	beq	get_type	/* value = 0 */
	/* Find the value of the symbol at index(r5) */
	slwi	r5, r5, 4		/* r5 = r5 * sizeof(Elf32_Sym) */
	add	r12, r13, r5	/* r12 = &__dyn_sym[Index] */

	/*
	 * GNU ld has a bug, where dynamic relocs based on
	 * STB_LOCAL symbols, the value should be assumed
	 * to be zero. - Alan Modra
	 */
	/* XXX: Do we need to check if we are using GNU ld ? */
	lbz	r5, 12(r12)	/* r5 = dyn_sym[Index].st_info */
	extrwi	r5, r5, 4, 24	/* r5 = ELF32_ST_BIND(r5) */
	cmpwi	r5, STB_LOCAL	/* st_value = 0, ld bug */
	beq	get_type	/* We have r5 = 0 */
	lwz	r5, 4(r12)	/* r5 = __dyn_sym[Index].st_value */

get_type:
	/* Load the relocation type to r4 */
	extrwi	r4, r4, 8, 24	/* r4 = ELF32_R_TYPE(r_info) = ((char*)r4)[3] */

	/* R_PPC_RELATIVE */
	cmpwi	r4, R_PPC_RELATIVE
	bne	hi16
	lwz	r4, 0(r9)	/* r_offset */
	lwz	r0, 8(r9)	/* r_addend */
	add	r0, r0, r3	/* final addend */
	stwx	r0, r4, r7	/* memory[r4+r7]) = (u32)r0 */
	b	nxtrela		/* continue */

	/* R_PPC_ADDR16_HI */
hi16:
	cmpwi	r4, R_PPC_ADDR16_HI
	bne	ha16
	lwz	r4, 0(r9)	/* r_offset */
	lwz	r0, 8(r9)	/* r_addend */
	add	r0, r0, r3
	add	r0, r0, r5	/* r0 = (S+A+Offset) */
	extrwi	r0, r0, 16, 0	/* r0 = (r0 >> 16) */
	b	store_half

	/* R_PPC_ADDR16_HA */
ha16:
	cmpwi	r4, R_PPC_ADDR16_HA
	bne	lo16
	lwz	r4, 0(r9)	/* r_offset */
	lwz	r0, 8(r9)	/* r_addend */
	add	r0, r0, r3
	add	r0, r0, r5	/* r0 = (S+A+Offset) */
	extrwi	r5, r0, 1, 16	/* Extract bit 16 */
	extrwi	r0, r0, 16, 0	/* r0 = (r0 >> 16) */
	add	r0, r0, r5	/* Add it to r0 */
	b	store_half

	/* R_PPC_ADDR16_LO */
lo16:
	cmpwi	r4, R_PPC_ADDR16_LO
	bne	unknown_type
	lwz	r4, 0(r9)	/* r_offset */
	lwz	r0, 8(r9)	/* r_addend */
	add	r0, r0, r3
	add	r0, r0, r5	/* r0 = (S+A+Offset) */
	extrwi	r0, r0, 16, 16	/* r0 &= 0xffff */
	/* Fall through to */

	/* Store half word */
store_half:
	sthx	r0, r4, r7	/* memory[r4+r7] = (u16)r0 */

nxtrela:
	/*
	 * We have to flush the modified instructions to the
	 * main storage from the d-cache. And also, invalidate the
	 * cached instructions in i-cache which has been modified.
	 *
	 * We delay the sync / isync operation till the end, since
	 * we won't be executing the modified instructions until
	 * we return from here.
	 */
	dcbst	r4,r7
	sync			/* Ensure the data is flushed before icbi */
	icbi	r4,r7
unknown_type:
	cmpwi	r8, 0		/* relasz = 0 ? */
	ble	done
	add	r9, r9, r6	/* move to next entry in the .rela table */
	subf	r8, r6, r8	/* relasz -= relaent */
	b	applyrela

done:
	sync			/* Wait for the flush to finish */
	isync			/* Discard prefetched instructions */
	blr

p_dyn:		.long	__dynamic_start - 0b
p_rela:		.long	__rela_dyn_start - 0b
p_sym:		.long	__dynamic_symtab - 0b
p_st:		.long	_stext - 0b
