/* arch/sparc64/kernel/ktlb.S: Kernel mapping TLB miss handling.
 *
 * Copyright (C) 1995, 1997, 2005 David S. Miller <davem@davemloft.net>
 * Copyright (C) 1996 Eddie C. Dost        (ecd@brainaid.de)
 * Copyright (C) 1996 Miguel de Icaza      (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996,98,99 Jakub Jelinek  (jj@sunsite.mff.cuni.cz)
 */

#include <linux/config.h>
#include <asm/head.h>
#include <asm/asi.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/tsb.h>

	.text
	.align		32

	.globl		kvmap_itlb
kvmap_itlb:
	/* g6: TAG TARGET */
	mov		TLB_TAG_ACCESS, %g4
	ldxa		[%g4] ASI_IMMU, %g4

kvmap_itlb_nonlinear:
	/* Catch kernel NULL pointer calls.  */
	sethi		%hi(PAGE_SIZE), %g5
	cmp		%g4, %g5
	bleu,pn		%xcc, kvmap_dtlb_longpath
	 nop

	KERN_TSB_LOOKUP_TL1(%g4, %g6, %g5, %g1, %g2, %g3, kvmap_itlb_load)

kvmap_itlb_tsb_miss:
	sethi		%hi(LOW_OBP_ADDRESS), %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kvmap_itlb_vmalloc_addr
	 mov		0x1, %g5
	sllx		%g5, 32, %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kvmap_itlb_obp
	 nop

kvmap_itlb_vmalloc_addr:
	KERN_PGTABLE_WALK(%g4, %g5, %g2, kvmap_itlb_longpath)

	KTSB_LOCK_TAG(%g1, %g2, %g4)

	/* Load and check PTE.  */
	ldxa		[%g5] ASI_PHYS_USE_EC, %g5
	brgez,a,pn	%g5, kvmap_itlb_longpath
	 KTSB_STORE(%g1, %g0)

	KTSB_WRITE(%g1, %g5, %g6)

	/* fallthrough to TLB load */

kvmap_itlb_load:
	stxa		%g5, [%g0] ASI_ITLB_DATA_IN	! Reload TLB
	retry

kvmap_itlb_longpath:
	rdpr	%pstate, %g5
	wrpr	%g5, PSTATE_AG | PSTATE_MG, %pstate
	rdpr	%tpc, %g5
	ba,pt	%xcc, sparc64_realfault_common
	 mov	FAULT_CODE_ITLB, %g4

kvmap_itlb_obp:
	OBP_TRANS_LOOKUP(%g4, %g5, %g2, %g3, kvmap_itlb_longpath)

	KTSB_LOCK_TAG(%g1, %g2, %g4)

	KTSB_WRITE(%g1, %g5, %g6)

	ba,pt		%xcc, kvmap_itlb_load
	 nop

kvmap_dtlb_obp:
	OBP_TRANS_LOOKUP(%g4, %g5, %g2, %g3, kvmap_dtlb_longpath)

	KTSB_LOCK_TAG(%g1, %g2, %g4)

	KTSB_WRITE(%g1, %g5, %g6)

	ba,pt		%xcc, kvmap_dtlb_load
	 nop

	.align		32
	.globl		kvmap_dtlb
kvmap_dtlb:
	/* %g6: TAG TARGET */
	mov		TLB_TAG_ACCESS, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	brgez,pn	%g4, kvmap_dtlb_nonlinear
	 nop

#define KERN_HIGHBITS	((_PAGE_VALID|_PAGE_SZ4MB)^0xfffff80000000000)
#define KERN_LOWBITS	(_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W)

	sethi		%uhi(KERN_HIGHBITS), %g2
	or		%g2, %ulo(KERN_HIGHBITS), %g2
	sllx		%g2, 32, %g2
	or		%g2, KERN_LOWBITS, %g2

#undef KERN_HIGHBITS
#undef KERN_LOWBITS

	.globl		kvmap_linear_patch
kvmap_linear_patch:
	ba,pt		%xcc, kvmap_dtlb_load
	 xor		%g2, %g4, %g5

kvmap_dtlb_vmalloc_addr:
	KERN_PGTABLE_WALK(%g4, %g5, %g2, kvmap_dtlb_longpath)

	KTSB_LOCK_TAG(%g1, %g2, %g4)

	/* Load and check PTE.  */
	ldxa		[%g5] ASI_PHYS_USE_EC, %g5
	brgez,a,pn	%g5, kvmap_dtlb_longpath
	 KTSB_STORE(%g1, %g0)

	KTSB_WRITE(%g1, %g5, %g6)

	/* fallthrough to TLB load */

kvmap_dtlb_load:
	stxa		%g5, [%g0] ASI_DTLB_DATA_IN	! Reload TLB
	retry

kvmap_dtlb_nonlinear:
	/* Catch kernel NULL pointer derefs.  */
	sethi		%hi(PAGE_SIZE), %g5
	cmp		%g4, %g5
	bleu,pn		%xcc, kvmap_dtlb_longpath
	 nop

	KERN_TSB_LOOKUP_TL1(%g4, %g6, %g5, %g1, %g2, %g3, kvmap_dtlb_load)

kvmap_dtlb_tsbmiss:
	sethi		%hi(MODULES_VADDR), %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kvmap_dtlb_longpath
	 mov		(VMALLOC_END >> 24), %g5
	sllx		%g5, 24, %g5
	cmp		%g4, %g5
	bgeu,pn		%xcc, kvmap_dtlb_longpath
	 nop

kvmap_check_obp:
	sethi		%hi(LOW_OBP_ADDRESS), %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kvmap_dtlb_vmalloc_addr
	 mov		0x1, %g5
	sllx		%g5, 32, %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kvmap_dtlb_obp
	 nop
	ba,pt		%xcc, kvmap_dtlb_vmalloc_addr
	 nop

kvmap_dtlb_longpath:
	rdpr	%pstate, %g5
	wrpr	%g5, PSTATE_AG | PSTATE_MG, %pstate
	rdpr	%tl, %g4
	cmp	%g4, 1
	mov	TLB_TAG_ACCESS, %g4
	ldxa	[%g4] ASI_DMMU, %g5
	be,pt	%xcc, sparc64_realfault_common
	 mov	FAULT_CODE_DTLB, %g4
	ba,pt	%xcc, winfix_trampoline
	 nop
