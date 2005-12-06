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

	.text
	.align		32

/*
 * On a second level vpte miss, check whether the original fault is to the OBP 
 * range (note that this is only possible for instruction miss, data misses to
 * obp range do not use vpte). If so, go back directly to the faulting address.
 * This is because we want to read the tpc, otherwise we have no way of knowing
 * the 8k aligned faulting address if we are using >8k kernel pagesize. This
 * also ensures no vpte range addresses are dropped into tlb while obp is
 * executing (see inherit_locked_prom_mappings() rant).
 */
sparc64_vpte_nucleus:
	/* Note that kvmap below has verified that the address is
	 * in the range MODULES_VADDR --> VMALLOC_END already.  So
	 * here we need only check if it is an OBP address or not.
	 */
	sethi		%hi(LOW_OBP_ADDRESS), %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kern_vpte
	 mov		0x1, %g5
	sllx		%g5, 32, %g5
	cmp		%g4, %g5
	blu,pn		%xcc, vpte_insn_obp
	 nop

	/* These two instructions are patched by paginig_init().  */
kern_vpte:
	sethi		%hi(swapper_pgd_zero), %g5
	lduw		[%g5 + %lo(swapper_pgd_zero)], %g5

	/* With kernel PGD in %g5, branch back into dtlb_backend.  */
	ba,pt		%xcc, sparc64_kpte_continue
	 andn		%g1, 0x3, %g1	/* Finish PMD offset adjustment.  */

vpte_noent:
	/* Restore previous TAG_ACCESS, %g5 is zero, and we will
	 * skip over the trap instruction so that the top level
	 * TLB miss handler will thing this %g5 value is just an
	 * invalid PTE, thus branching to full fault processing.
	 */
	mov		TLB_SFSR, %g1
	stxa		%g4, [%g1 + %g1] ASI_DMMU
	done

vpte_insn_obp:
	/* Behave as if we are at TL0.  */
	wrpr		%g0, 1, %tl
	rdpr		%tpc, %g4	/* Find original faulting iaddr */
	srlx		%g4, 13, %g4	/* Throw out context bits */
	sllx		%g4, 13, %g4	/* g4 has vpn + ctx0 now */

	/* Restore previous TAG_ACCESS.  */
	mov		TLB_SFSR, %g1
	stxa		%g4, [%g1 + %g1] ASI_IMMU

	sethi		%hi(prom_trans), %g5
	or		%g5, %lo(prom_trans), %g5

1:	ldx		[%g5 + 0x00], %g6	! base
	brz,a,pn	%g6, longpath		! no more entries, fail
	 mov		TLB_SFSR, %g1		! and restore %g1
	ldx		[%g5 + 0x08], %g1	! len
	add		%g6, %g1, %g1		! end
	cmp		%g6, %g4
	bgu,pt		%xcc, 2f
	 cmp		%g4, %g1
	bgeu,pt		%xcc, 2f
	 ldx		[%g5 + 0x10], %g1	! PTE

	/* TLB load, restore %g1, and return from trap.  */
	sub		%g4, %g6, %g6
	add		%g1, %g6, %g5
	mov		TLB_SFSR, %g1
	stxa		%g5, [%g0] ASI_ITLB_DATA_IN
	retry

2:	ba,pt		%xcc, 1b
	 add		%g5, (3 * 8), %g5	! next entry

kvmap_do_obp:
	sethi		%hi(prom_trans), %g5
	or		%g5, %lo(prom_trans), %g5
	srlx		%g4, 13, %g4
	sllx		%g4, 13, %g4

1:	ldx		[%g5 + 0x00], %g6	! base
	brz,a,pn	%g6, longpath		! no more entries, fail
	 mov		TLB_SFSR, %g1		! and restore %g1
	ldx		[%g5 + 0x08], %g1	! len
	add		%g6, %g1, %g1		! end
	cmp		%g6, %g4
	bgu,pt		%xcc, 2f
	 cmp		%g4, %g1
	bgeu,pt		%xcc, 2f
	 ldx		[%g5 + 0x10], %g1	! PTE

	/* TLB load, restore %g1, and return from trap.  */
	sub		%g4, %g6, %g6
	add		%g1, %g6, %g5
	mov		TLB_SFSR, %g1
	stxa		%g5, [%g0] ASI_DTLB_DATA_IN
	retry

2:	ba,pt		%xcc, 1b
	 add		%g5, (3 * 8), %g5	! next entry

/*
 * On a first level data miss, check whether this is to the OBP range (note
 * that such accesses can be made by prom, as well as by kernel using
 * prom_getproperty on "address"), and if so, do not use vpte access ...
 * rather, use information saved during inherit_prom_mappings() using 8k
 * pagesize.
 */
	.align		32
kvmap:
	brgez,pn	%g4, kvmap_nonlinear
	 nop

#ifdef CONFIG_DEBUG_PAGEALLOC
	.globl		kvmap_linear_patch
kvmap_linear_patch:
#endif
	ba,pt		%xcc, kvmap_load
	 xor		%g2, %g4, %g5

#ifdef CONFIG_DEBUG_PAGEALLOC
	sethi		%hi(swapper_pg_dir), %g5
	or		%g5, %lo(swapper_pg_dir), %g5
	sllx		%g4, 64 - (PGDIR_SHIFT + PGDIR_BITS), %g6
	srlx		%g6, 64 - PAGE_SHIFT, %g6
	andn		%g6, 0x3, %g6
	lduw		[%g5 + %g6], %g5
	brz,pn		%g5, longpath
	 sllx		%g4, 64 - (PMD_SHIFT + PMD_BITS), %g6
	srlx		%g6, 64 - PAGE_SHIFT, %g6
	sllx		%g5, 11, %g5
	andn		%g6, 0x3, %g6
	lduwa		[%g5 + %g6] ASI_PHYS_USE_EC, %g5
	brz,pn		%g5, longpath
	 sllx		%g4, 64 - PMD_SHIFT, %g6
	srlx		%g6, 64 - PAGE_SHIFT, %g6
	sllx		%g5, 11, %g5
	andn		%g6, 0x7, %g6
	ldxa		[%g5 + %g6] ASI_PHYS_USE_EC, %g5
	brz,pn		%g5, longpath
	 nop
	ba,a,pt		%xcc, kvmap_load
#endif

kvmap_nonlinear:
	sethi		%hi(MODULES_VADDR), %g5
	cmp		%g4, %g5
	blu,pn		%xcc, longpath
	 mov		(VMALLOC_END >> 24), %g5
	sllx		%g5, 24, %g5
	cmp		%g4, %g5
	bgeu,pn		%xcc, longpath
	 nop

kvmap_check_obp:
	sethi		%hi(LOW_OBP_ADDRESS), %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kvmap_vmalloc_addr
	 mov		0x1, %g5
	sllx		%g5, 32, %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kvmap_do_obp
	 nop

kvmap_vmalloc_addr:
	/* If we get here, a vmalloc addr was accessed, load kernel VPTE.  */
	ldxa		[%g3 + %g6] ASI_N, %g5
	brgez,pn	%g5, longpath
	 nop

kvmap_load:
	/* PTE is valid, load into TLB and return from trap.  */
	stxa		%g5, [%g0] ASI_DTLB_DATA_IN	! Reload TLB
	retry
