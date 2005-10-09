/* $Id: trampoline.S,v 1.26 2002/02/09 19:49:30 davem Exp $
 * trampoline.S: Jump start slave processors on sparc64.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/lsu.h>
#include <asm/dcr.h>
#include <asm/dcu.h>
#include <asm/pstate.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/spitfire.h>
#include <asm/processor.h>
#include <asm/thread_info.h>
#include <asm/mmu.h>

	.data
	.align	8
call_method:
	.asciz	"call-method"
	.align	8
itlb_load:
	.asciz	"SUNW,itlb-load"
	.align	8
dtlb_load:
	.asciz	"SUNW,dtlb-load"

	.text
	.align		8
	.globl		sparc64_cpu_startup, sparc64_cpu_startup_end
sparc64_cpu_startup:
	flushw

	BRANCH_IF_CHEETAH_BASE(g1,g5,cheetah_startup)
	BRANCH_IF_CHEETAH_PLUS_OR_FOLLOWON(g1,g5,cheetah_plus_startup)

	ba,pt	%xcc, spitfire_startup
	 nop

cheetah_plus_startup:
	/* Preserve OBP chosen DCU and DCR register settings.  */
	ba,pt	%xcc, cheetah_generic_startup
	 nop

cheetah_startup:
	mov	DCR_BPE | DCR_RPE | DCR_SI | DCR_IFPOE | DCR_MS, %g1
	wr	%g1, %asr18

	sethi	%uhi(DCU_ME|DCU_RE|DCU_HPE|DCU_SPE|DCU_SL|DCU_WE), %g5
	or	%g5, %ulo(DCU_ME|DCU_RE|DCU_HPE|DCU_SPE|DCU_SL|DCU_WE), %g5
	sllx	%g5, 32, %g5
	or	%g5, DCU_DM | DCU_IM | DCU_DC | DCU_IC, %g5
	stxa	%g5, [%g0] ASI_DCU_CONTROL_REG
	membar	#Sync

cheetah_generic_startup:
	mov	TSB_EXTENSION_P, %g3
	stxa	%g0, [%g3] ASI_DMMU
	stxa	%g0, [%g3] ASI_IMMU
	membar	#Sync

	mov	TSB_EXTENSION_S, %g3
	stxa	%g0, [%g3] ASI_DMMU
	membar	#Sync

	mov	TSB_EXTENSION_N, %g3
	stxa	%g0, [%g3] ASI_DMMU
	stxa	%g0, [%g3] ASI_IMMU
	membar	#Sync

	/* Disable STICK_INT interrupts. */
	sethi		%hi(0x80000000), %g5
	sllx		%g5, 32, %g5
	wr		%g5, %asr25

	ba,pt		%xcc, startup_continue
	 nop

spitfire_startup:
	mov		(LSU_CONTROL_IC | LSU_CONTROL_DC | LSU_CONTROL_IM | LSU_CONTROL_DM), %g1
	stxa		%g1, [%g0] ASI_LSU_CONTROL
	membar		#Sync

startup_continue:
	wrpr		%g0, 15, %pil

	sethi		%hi(0x80000000), %g2
	sllx		%g2, 32, %g2
	wr		%g2, 0, %tick_cmpr

	/* Call OBP by hand to lock KERNBASE into i/d tlbs.
	 * We lock 2 consequetive entries if we are 'bigkernel'.
	 */
	mov		%o0, %l0

	sethi		%hi(prom_entry_lock), %g2
1:	ldstub		[%g2 + %lo(prom_entry_lock)], %g1
	membar		#StoreLoad | #StoreStore
	brnz,pn		%g1, 1b
	 nop

	sethi		%hi(p1275buf), %g2
	or		%g2, %lo(p1275buf), %g2
	ldx		[%g2 + 0x10], %l2
	mov		%sp, %l1
	add		%l2, -(192 + 128), %sp
	flushw

	sethi		%hi(call_method), %g2
	or		%g2, %lo(call_method), %g2
	stx		%g2, [%sp + 2047 + 128 + 0x00]
	mov		5, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x08]
	mov		1, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x10]
	sethi		%hi(itlb_load), %g2
	or		%g2, %lo(itlb_load), %g2
	stx		%g2, [%sp + 2047 + 128 + 0x18]
	sethi		%hi(prom_mmu_ihandle_cache), %g2
	lduw		[%g2 + %lo(prom_mmu_ihandle_cache)], %g2
	stx		%g2, [%sp + 2047 + 128 + 0x20]
	sethi		%hi(KERNBASE), %g2
	stx		%g2, [%sp + 2047 + 128 + 0x28]
	sethi		%hi(kern_locked_tte_data), %g2
	ldx		[%g2 + %lo(kern_locked_tte_data)], %g2
	stx		%g2, [%sp + 2047 + 128 + 0x30]

	mov		15, %g2
	BRANCH_IF_ANY_CHEETAH(g1,g5,1f)

	mov		63, %g2
1:
	stx		%g2, [%sp + 2047 + 128 + 0x38]
	sethi		%hi(p1275buf), %g2
	or		%g2, %lo(p1275buf), %g2
	ldx		[%g2 + 0x08], %o1
	call		%o1
	 add		%sp, (2047 + 128), %o0

	sethi		%hi(bigkernel), %g2
	lduw		[%g2 + %lo(bigkernel)], %g2
	cmp		%g2, 0
	be,pt		%icc, do_dtlb
	 nop

	sethi		%hi(call_method), %g2
	or		%g2, %lo(call_method), %g2
	stx		%g2, [%sp + 2047 + 128 + 0x00]
	mov		5, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x08]
	mov		1, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x10]
	sethi		%hi(itlb_load), %g2
	or		%g2, %lo(itlb_load), %g2
	stx		%g2, [%sp + 2047 + 128 + 0x18]
	sethi		%hi(prom_mmu_ihandle_cache), %g2
	lduw		[%g2 + %lo(prom_mmu_ihandle_cache)], %g2
	stx		%g2, [%sp + 2047 + 128 + 0x20]
	sethi		%hi(KERNBASE + 0x400000), %g2
	stx		%g2, [%sp + 2047 + 128 + 0x28]
	sethi		%hi(kern_locked_tte_data), %g2
	ldx		[%g2 + %lo(kern_locked_tte_data)], %g2
	sethi		%hi(0x400000), %g1
	add		%g2, %g1, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x30]

	mov		14, %g2
	BRANCH_IF_ANY_CHEETAH(g1,g5,1f)

	mov		62, %g2
1:
	stx		%g2, [%sp + 2047 + 128 + 0x38]
	sethi		%hi(p1275buf), %g2
	or		%g2, %lo(p1275buf), %g2
	ldx		[%g2 + 0x08], %o1
	call		%o1
	 add		%sp, (2047 + 128), %o0

do_dtlb:
	sethi		%hi(call_method), %g2
	or		%g2, %lo(call_method), %g2
	stx		%g2, [%sp + 2047 + 128 + 0x00]
	mov		5, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x08]
	mov		1, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x10]
	sethi		%hi(dtlb_load), %g2
	or		%g2, %lo(dtlb_load), %g2
	stx		%g2, [%sp + 2047 + 128 + 0x18]
	sethi		%hi(prom_mmu_ihandle_cache), %g2
	lduw		[%g2 + %lo(prom_mmu_ihandle_cache)], %g2
	stx		%g2, [%sp + 2047 + 128 + 0x20]
	sethi		%hi(KERNBASE), %g2
	stx		%g2, [%sp + 2047 + 128 + 0x28]
	sethi		%hi(kern_locked_tte_data), %g2
	ldx		[%g2 + %lo(kern_locked_tte_data)], %g2
	stx		%g2, [%sp + 2047 + 128 + 0x30]

	mov		15, %g2
	BRANCH_IF_ANY_CHEETAH(g1,g5,1f)

	mov		63, %g2
1:

	stx		%g2, [%sp + 2047 + 128 + 0x38]
	sethi		%hi(p1275buf), %g2
	or		%g2, %lo(p1275buf), %g2
	ldx		[%g2 + 0x08], %o1
	call		%o1
	 add		%sp, (2047 + 128), %o0

	sethi		%hi(bigkernel), %g2
	lduw		[%g2 + %lo(bigkernel)], %g2
	cmp		%g2, 0
	be,pt		%icc, do_unlock
	 nop

	sethi		%hi(call_method), %g2
	or		%g2, %lo(call_method), %g2
	stx		%g2, [%sp + 2047 + 128 + 0x00]
	mov		5, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x08]
	mov		1, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x10]
	sethi		%hi(dtlb_load), %g2
	or		%g2, %lo(dtlb_load), %g2
	stx		%g2, [%sp + 2047 + 128 + 0x18]
	sethi		%hi(prom_mmu_ihandle_cache), %g2
	lduw		[%g2 + %lo(prom_mmu_ihandle_cache)], %g2
	stx		%g2, [%sp + 2047 + 128 + 0x20]
	sethi		%hi(KERNBASE + 0x400000), %g2
	stx		%g2, [%sp + 2047 + 128 + 0x28]
	sethi		%hi(kern_locked_tte_data), %g2
	ldx		[%g2 + %lo(kern_locked_tte_data)], %g2
	sethi		%hi(0x400000), %g1
	add		%g2, %g1, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x30]

	mov		14, %g2
	BRANCH_IF_ANY_CHEETAH(g1,g5,1f)

	mov		62, %g2
1:

	stx		%g2, [%sp + 2047 + 128 + 0x38]
	sethi		%hi(p1275buf), %g2
	or		%g2, %lo(p1275buf), %g2
	ldx		[%g2 + 0x08], %o1
	call		%o1
	 add		%sp, (2047 + 128), %o0

do_unlock:
	sethi		%hi(prom_entry_lock), %g2
	stb		%g0, [%g2 + %lo(prom_entry_lock)]
	membar		#StoreStore | #StoreLoad

	mov		%l1, %sp
	flushw

	mov		%l0, %o0

	wrpr		%g0, (PSTATE_PRIV | PSTATE_PEF), %pstate
	wr		%g0, 0, %fprs

	/* XXX Buggy PROM... */
	srl		%o0, 0, %o0
	ldx		[%o0], %g6

	wr		%g0, ASI_P, %asi

	mov		PRIMARY_CONTEXT, %g7
	stxa		%g0, [%g7] ASI_DMMU
	membar		#Sync
	mov		SECONDARY_CONTEXT, %g7
	stxa		%g0, [%g7] ASI_DMMU
	membar		#Sync

	mov		1, %g5
	sllx		%g5, THREAD_SHIFT, %g5
	sub		%g5, (STACKFRAME_SZ + STACK_BIAS), %g5
	add		%g6, %g5, %sp
	mov		0, %fp

	wrpr		%g0, 0, %wstate
	wrpr		%g0, 0, %tl

	/* Setup the trap globals, then we can resurface. */
	rdpr		%pstate, %o1
	mov		%g6, %o2
	wrpr		%o1, PSTATE_AG, %pstate
	sethi		%hi(sparc64_ttable_tl0), %g5
	wrpr		%g5, %tba
	mov		%o2, %g6

	wrpr		%o1, PSTATE_MG, %pstate
#define KERN_HIGHBITS		((_PAGE_VALID|_PAGE_SZ4MB)^0xfffff80000000000)
#define KERN_LOWBITS		(_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W)

	mov		TSB_REG, %g1
	stxa		%g0, [%g1] ASI_DMMU
	membar		#Sync
	mov		TLB_SFSR, %g1
	sethi		%uhi(KERN_HIGHBITS), %g2
	or		%g2, %ulo(KERN_HIGHBITS), %g2
	sllx		%g2, 32, %g2
	or		%g2, KERN_LOWBITS, %g2

	BRANCH_IF_ANY_CHEETAH(g3,g7,9f)

	ba,pt		%xcc, 1f
	 nop

9:
	sethi		%uhi(VPTE_BASE_CHEETAH), %g3
	or		%g3, %ulo(VPTE_BASE_CHEETAH), %g3
	ba,pt		%xcc, 2f
	 sllx		%g3, 32, %g3
1:
	sethi		%uhi(VPTE_BASE_SPITFIRE), %g3
	or		%g3, %ulo(VPTE_BASE_SPITFIRE), %g3
	sllx		%g3, 32, %g3

2:
	clr	%g7
#undef KERN_HIGHBITS
#undef KERN_LOWBITS

	wrpr		%o1, 0x0, %pstate
	ldx		[%g6 + TI_TASK], %g4

	wrpr		%g0, 0, %wstate

	call		init_irqwork_curcpu
	 nop

	/* Start using proper page size encodings in ctx register.  */
	sethi	%hi(sparc64_kern_pri_context), %g3
	ldx	[%g3 + %lo(sparc64_kern_pri_context)], %g2
	mov	PRIMARY_CONTEXT, %g1
	stxa	%g2, [%g1] ASI_DMMU
	membar	#Sync

	rdpr		%pstate, %o1
	or		%o1, PSTATE_IE, %o1
	wrpr		%o1, 0, %pstate

	call		prom_set_trap_table
	 sethi		%hi(sparc64_ttable_tl0), %o0

	call		smp_callin
	 nop
	call		cpu_idle
	 mov		0, %o0
	call		cpu_panic
	 nop
1:	b,a,pt		%xcc, 1b

	.align		8
sparc64_cpu_startup_end:
