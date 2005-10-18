/* $Id: dtlb_backend.S,v 1.16 2001/10/09 04:02:11 davem Exp $
 * dtlb_backend.S: Back end to DTLB miss replacement strategy.
 *                 This is included directly into the trap table.
 *
 * Copyright (C) 1996,1998 David S. Miller (davem@redhat.com)
 * Copyright (C) 1997,1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#include <asm/pgtable.h>
#include <asm/mmu.h>

#define VALID_SZ_BITS	(_PAGE_VALID | _PAGE_SZBITS)

#define VPTE_BITS		(_PAGE_CP | _PAGE_CV | _PAGE_P )
#define VPTE_SHIFT		(PAGE_SHIFT - 3)

/* Ways we can get here:
 *
 * 1) Nucleus loads and stores to/from PA-->VA direct mappings at tl>1.
 * 2) Nucleus loads and stores to/from user/kernel window save areas.
 * 3) VPTE misses from dtlb_base and itlb_base.
 *
 * We need to extract out the PMD and PGDIR indexes from the
 * linear virtual page table access address.  The PTE index
 * is at the bottom, but we are not concerned with it.  Bits
 * 0 to 2 are clear since each PTE is 8 bytes in size.  Each
 * PMD and PGDIR entry are 4 bytes in size.   Thus, this
 * address looks something like:
 *
 * |---------------------------------------------------------------|
 * |  ...   |    PGDIR index    |    PMD index    | PTE index  |   |
 * |---------------------------------------------------------------|
 *   63   F   E               D   C             B   A         3 2 0  <- bit nr
 *
 *  The variable bits above are defined as:
 *  A --> 3 + (PAGE_SHIFT - log2(8))
 *    --> 3 + (PAGE_SHIFT - 3) - 1
 *        (ie. this is "bit 3" + PAGE_SIZE - size of PTE entry in bits - 1)
 *  B --> A + 1
 *  C --> B + (PAGE_SHIFT - log2(4))
 *    -->  B + (PAGE_SHIFT - 2) - 1
 *        (ie. this is "bit B" + PAGE_SIZE - size of PMD entry in bits - 1)
 *  D --> C + 1
 *  E --> D + (PAGE_SHIFT - log2(4))
 *    --> D + (PAGE_SHIFT - 2) - 1
 *        (ie. this is "bit D" + PAGE_SIZE - size of PGDIR entry in bits - 1)
 *  F --> E + 1
 *
 * (Note how "B" always evalutes to PAGE_SHIFT, all the other constants
 *  cancel out.)
 *
 * For 8K PAGE_SIZE (thus, PAGE_SHIFT of 13) the bit numbers are:
 * A --> 12
 * B --> 13
 * C --> 23
 * D --> 24
 * E --> 34
 * F --> 35
 *
 * For 64K PAGE_SIZE (thus, PAGE_SHIFT of 16) the bit numbers are:
 * A --> 15
 * B --> 16
 * C --> 29
 * D --> 30
 * E --> 43
 * F --> 44
 *
 * Because bits both above and below each PGDIR and PMD index need to
 * be masked out, and the index can be as long as 14 bits (when using a
 * 64K PAGE_SIZE, and thus a PAGE_SHIFT of 16), we need 3 instructions
 * to extract each index out.
 *
 * Shifts do not pair very well on UltraSPARC-I, II, IIi, and IIe, so
 * we try to avoid using them for the entire operation.  We could setup
 * a mask anywhere from bit 31 down to bit 10 using the sethi instruction.
 *
 * We need a mask covering bits B --> C and one covering D --> E.
 * For 8K PAGE_SIZE these masks are 0x00ffe000 and 0x7ff000000.
 * For 64K PAGE_SIZE these masks are 0x3fff0000 and 0xfffc0000000.
 * The second in each set cannot be loaded with a single sethi
 * instruction, because the upper bits are past bit 32.  We would
 * need to use a sethi + a shift.
 *
 * For the time being, we use 2 shifts and a simple "and" mask.
 * We shift left to clear the bits above the index, we shift down
 * to clear the bits below the index (sans the log2(4 or 8) bits)
 * and a mask to clear the log2(4 or 8) bits.  We need therefore
 * define 4 shift counts, all of which are relative to PAGE_SHIFT.
 *
 * Although unsupportable for other reasons, this does mean that
 * 512K and 4MB page sizes would be generaally supported by the
 * kernel.  (ELF binaries would break with > 64K PAGE_SIZE since
 * the sections are only aligned that strongly).
 *
 * The operations performed for extraction are thus:
 *
 *      ((X << FOO_SHIFT_LEFT) >> FOO_SHIFT_RIGHT) & ~0x3
 *
 */

#define A (3 + (PAGE_SHIFT - 3) - 1)
#define B (A + 1)
#define C (B + (PAGE_SHIFT - 2) - 1)
#define D (C + 1)
#define E (D + (PAGE_SHIFT - 2) - 1)
#define F (E + 1)

#define PMD_SHIFT_LEFT		(64 - D)
#define PMD_SHIFT_RIGHT		(64 - (D - B) - 2)
#define PGDIR_SHIFT_LEFT 	(64 - F)
#define PGDIR_SHIFT_RIGHT	(64 - (F - D) - 2)
#define LOW_MASK_BITS		0x3

/* TLB1 ** ICACHE line 1: tl1 DTLB and quick VPTE miss	*/
	ldxa		[%g1 + %g1] ASI_DMMU, %g4	! Get TAG_ACCESS
	add		%g3, %g3, %g5			! Compute VPTE base
	cmp		%g4, %g5			! VPTE miss?
	bgeu,pt		%xcc, 1f			! Continue here
	 andcc		%g4, TAG_CONTEXT_BITS, %g5	! tl0 miss Nucleus test
	ba,a,pt		%xcc, from_tl1_trap		! Fall to tl0 miss
1:	sllx		%g6, VPTE_SHIFT, %g4		! Position TAG_ACCESS
	or		%g4, %g5, %g4			! Prepare TAG_ACCESS

/* TLB1 ** ICACHE line 2: Quick VPTE miss	  	*/
	mov		TSB_REG, %g1			! Grab TSB reg
	ldxa		[%g1] ASI_DMMU, %g5		! Doing PGD caching?
	sllx		%g6, PMD_SHIFT_LEFT, %g1	! Position PMD offset
	be,pn		%xcc, sparc64_vpte_nucleus	! Is it from Nucleus?
	 srlx		%g1, PMD_SHIFT_RIGHT, %g1	! Mask PMD offset bits
	brnz,pt		%g5, sparc64_vpte_continue	! Yep, go like smoke
	 andn		%g1, LOW_MASK_BITS, %g1		! Final PMD mask
	sllx		%g6, PGDIR_SHIFT_LEFT, %g5	! Position PGD offset

/* TLB1 ** ICACHE line 3: Quick VPTE miss	  	*/
	srlx		%g5, PGDIR_SHIFT_RIGHT, %g5	! Mask PGD offset bits
	andn		%g5, LOW_MASK_BITS, %g5		! Final PGD mask
	lduwa		[%g7 + %g5] ASI_PHYS_USE_EC, %g5! Load PGD
	brz,pn		%g5, vpte_noent			! Valid?
sparc64_kpte_continue:
	 sllx		%g5, 11, %g5			! Shift into place
sparc64_vpte_continue:
	lduwa		[%g5 + %g1] ASI_PHYS_USE_EC, %g5! Load PMD
	sllx		%g5, 11, %g5			! Shift into place
	brz,pn		%g5, vpte_noent			! Valid?

/* TLB1 ** ICACHE line 4: Quick VPTE miss	  	*/
	 mov		(VALID_SZ_BITS >> 61), %g1	! upper vpte into %g1
	sllx		%g1, 61, %g1			! finish calc
	or		%g5, VPTE_BITS, %g5		! Prepare VPTE data
	or		%g5, %g1, %g5			! ...
	mov		TLB_SFSR, %g1			! Restore %g1 value
	stxa		%g5, [%g0] ASI_DTLB_DATA_IN	! Load VPTE into TLB
	stxa		%g4, [%g1 + %g1] ASI_DMMU	! Restore previous TAG_ACCESS
	retry						! Load PTE once again

#undef VALID_SZ_BITS
#undef VPTE_SHIFT
#undef VPTE_BITS
#undef A
#undef B
#undef C
#undef D
#undef E
#undef F
#undef PMD_SHIFT_LEFT
#undef PMD_SHIFT_RIGHT
#undef PGDIR_SHIFT_LEFT
#undef PGDIR_SHIFT_RIGHT
#undef LOW_MASK_BITS

