/*
 * trampoline.S: Jump start slave processors on sparc64.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/init.h>

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
#include <asm/hypervisor.h>
#include <asm/cpudata.h>

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

#define TRAMP_STACK_SIZE	1024
	.align	16
tramp_stack:
	.skip	TRAMP_STACK_SIZE

	.align		8
	.globl		sparc64_cpu_startup, sparc64_cpu_startup_end
sparc64_cpu_startup:
	BRANCH_IF_SUN4V(g1, niagara_startup)
	BRANCH_IF_CHEETAH_BASE(g1, g5, cheetah_startup)
	BRANCH_IF_CHEETAH_PLUS_OR_FOLLOWON(g1, g5, cheetah_plus_startup)

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
	/* fallthru */

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
	/* fallthru */

niagara_startup:
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
	mov		%o0, %l0
	BRANCH_IF_SUN4V(g1, niagara_lock_tlb)

	sethi		%hi(0x80000000), %g2
	sllx		%g2, 32, %g2
	wr		%g2, 0, %tick_cmpr

	/* Call OBP by hand to lock KERNBASE into i/d tlbs.
	 * We lock 'num_kernel_image_mappings' consequetive entries.
	 */
	sethi		%hi(prom_entry_lock), %g2
1:	ldstub		[%g2 + %lo(prom_entry_lock)], %g1
	brnz,pn		%g1, 1b
	 nop

	sethi		%hi(p1275buf), %g2
	or		%g2, %lo(p1275buf), %g2
	ldx		[%g2 + 0x10], %l2
	add		%l2, -(192 + 128), %sp
	flushw

	/* Setup the loop variables:
	 * %l3: VADDR base
	 * %l4: TTE base
	 * %l5: Loop iterator, iterates from 0 to 'num_kernel_image_mappings'
	 * %l6: Number of TTE entries to map
	 * %l7: Highest TTE entry number, we count down
	 */
	sethi		%hi(KERNBASE), %l3
	sethi		%hi(kern_locked_tte_data), %l4
	ldx		[%l4 + %lo(kern_locked_tte_data)], %l4
	clr		%l5
	sethi		%hi(num_kernel_image_mappings), %l6
	lduw		[%l6 + %lo(num_kernel_image_mappings)], %l6
	add		%l6, 1, %l6

	mov		15, %l7
	BRANCH_IF_ANY_CHEETAH(g1,g5,2f)

	mov		63, %l7
2:

3:
	/* Lock into I-MMU */
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

	/* Each TTE maps 4MB, convert index to offset.  */
	sllx		%l5, 22, %g1

	add		%l3, %g1, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x28]	! VADDR
	add		%l4, %g1, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x30]	! TTE

	/* TTE index is highest minus loop index.  */
	sub		%l7, %l5, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x38]

	sethi		%hi(p1275buf), %g2
	or		%g2, %lo(p1275buf), %g2
	ldx		[%g2 + 0x08], %o1
	call		%o1
	 add		%sp, (2047 + 128), %o0

	/* Lock into D-MMU */
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

	/* Each TTE maps 4MB, convert index to offset.  */
	sllx		%l5, 22, %g1

	add		%l3, %g1, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x28]	! VADDR
	add		%l4, %g1, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x30]	! TTE

	/* TTE index is highest minus loop index.  */
	sub		%l7, %l5, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x38]

	sethi		%hi(p1275buf), %g2
	or		%g2, %lo(p1275buf), %g2
	ldx		[%g2 + 0x08], %o1
	call		%o1
	 add		%sp, (2047 + 128), %o0

	add		%l5, 1, %l5
	cmp		%l5, %l6
	bne,pt		%xcc, 3b
	 nop

	sethi		%hi(prom_entry_lock), %g2
	stb		%g0, [%g2 + %lo(prom_entry_lock)]

	ba,pt		%xcc, after_lock_tlb
	 nop

niagara_lock_tlb:
	sethi		%hi(KERNBASE), %l3
	sethi		%hi(kern_locked_tte_data), %l4
	ldx		[%l4 + %lo(kern_locked_tte_data)], %l4
	clr		%l5
	sethi		%hi(num_kernel_image_mappings), %l6
	lduw		[%l6 + %lo(num_kernel_image_mappings)], %l6
	add		%l6, 1, %l6

1:
	mov		HV_FAST_MMU_MAP_PERM_ADDR, %o5
	sllx		%l5, 22, %g2
	add		%l3, %g2, %o0
	clr		%o1
	add		%l4, %g2, %o2
	mov		HV_MMU_IMMU, %o3
	ta		HV_FAST_TRAP

	mov		HV_FAST_MMU_MAP_PERM_ADDR, %o5
	sllx		%l5, 22, %g2
	add		%l3, %g2, %o0
	clr		%o1
	add		%l4, %g2, %o2
	mov		HV_MMU_DMMU, %o3
	ta		HV_FAST_TRAP

	add		%l5, 1, %l5
	cmp		%l5, %l6
	bne,pt		%xcc, 1b
	 nop

after_lock_tlb:
	wrpr		%g0, (PSTATE_PRIV | PSTATE_PEF), %pstate
	wr		%g0, 0, %fprs

	wr		%g0, ASI_P, %asi

	mov		PRIMARY_CONTEXT, %g7

661:	stxa		%g0, [%g7] ASI_DMMU
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	stxa		%g0, [%g7] ASI_MMU
	.previous

	membar		#Sync
	mov		SECONDARY_CONTEXT, %g7

661:	stxa		%g0, [%g7] ASI_DMMU
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	stxa		%g0, [%g7] ASI_MMU
	.previous

	membar		#Sync

	/* Everything we do here, until we properly take over the
	 * trap table, must be done with extreme care.  We cannot
	 * make any references to %g6 (current thread pointer),
	 * %g4 (current task pointer), or %g5 (base of current cpu's
	 * per-cpu area) until we properly take over the trap table
	 * from the firmware and hypervisor.
	 *
	 * Get onto temporary stack which is in the locked kernel image.
	 */
	sethi		%hi(tramp_stack), %g1
	or		%g1, %lo(tramp_stack), %g1
	add		%g1, TRAMP_STACK_SIZE, %g1
	sub		%g1, STACKFRAME_SZ + STACK_BIAS + 256, %sp
	mov		0, %fp

	/* Put garbage in these registers to trap any access to them.  */
	set		0xdeadbeef, %g4
	set		0xdeadbeef, %g5
	set		0xdeadbeef, %g6

	call		init_irqwork_curcpu
	 nop

	sethi		%hi(tlb_type), %g3
	lduw		[%g3 + %lo(tlb_type)], %g2
	cmp		%g2, 3
	bne,pt		%icc, 1f
	 nop

	call		hard_smp_processor_id
	 nop
	
	call		sun4v_register_mondo_queues
	 nop

1:	call		init_cur_cpu_trap
	 ldx		[%l0], %o0

	/* Start using proper page size encodings in ctx register.  */
	sethi		%hi(sparc64_kern_pri_context), %g3
	ldx		[%g3 + %lo(sparc64_kern_pri_context)], %g2
	mov		PRIMARY_CONTEXT, %g1

661:	stxa		%g2, [%g1] ASI_DMMU
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	stxa		%g2, [%g1] ASI_MMU
	.previous

	membar		#Sync

	wrpr		%g0, 0, %wstate

	sethi		%hi(prom_entry_lock), %g2
1:	ldstub		[%g2 + %lo(prom_entry_lock)], %g1
	brnz,pn		%g1, 1b
	 nop

	/* As a hack, put &init_thread_union into %g6.
	 * prom_world() loads from here to restore the %asi
	 * register.
	 */
	sethi		%hi(init_thread_union), %g6
	or		%g6, %lo(init_thread_union), %g6

	sethi		%hi(is_sun4v), %o0
	lduw		[%o0 + %lo(is_sun4v)], %o0
	brz,pt		%o0, 2f
	 nop

	TRAP_LOAD_TRAP_BLOCK(%g2, %g3)
	add		%g2, TRAP_PER_CPU_FAULT_INFO, %g2
	stxa		%g2, [%g0] ASI_SCRATCHPAD

	/* Compute physical address:
	 *
	 * paddr = kern_base + (mmfsa_vaddr - KERNBASE)
	 */
	sethi		%hi(KERNBASE), %g3
	sub		%g2, %g3, %g2
	sethi		%hi(kern_base), %g3
	ldx		[%g3 + %lo(kern_base)], %g3
	add		%g2, %g3, %o1
	sethi		%hi(sparc64_ttable_tl0), %o0

	set		prom_set_trap_table_name, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x00]
	mov		2, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x08]
	mov		0, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x10]
	stx		%o0, [%sp + 2047 + 128 + 0x18]
	stx		%o1, [%sp + 2047 + 128 + 0x20]
	sethi		%hi(p1275buf), %g2
	or		%g2, %lo(p1275buf), %g2
	ldx		[%g2 + 0x08], %o1
	call		%o1
	 add		%sp, (2047 + 128), %o0

	ba,pt		%xcc, 3f
	 nop

2:	sethi		%hi(sparc64_ttable_tl0), %o0
	set		prom_set_trap_table_name, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x00]
	mov		1, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x08]
	mov		0, %g2
	stx		%g2, [%sp + 2047 + 128 + 0x10]
	stx		%o0, [%sp + 2047 + 128 + 0x18]
	sethi		%hi(p1275buf), %g2
	or		%g2, %lo(p1275buf), %g2
	ldx		[%g2 + 0x08], %o1
	call		%o1
	 add		%sp, (2047 + 128), %o0

3:	sethi		%hi(prom_entry_lock), %g2
	stb		%g0, [%g2 + %lo(prom_entry_lock)]

	ldx		[%l0], %g6
	ldx		[%g6 + TI_TASK], %g4

	mov		1, %g5
	sllx		%g5, THREAD_SHIFT, %g5
	sub		%g5, (STACKFRAME_SZ + STACK_BIAS), %g5
	add		%g6, %g5, %sp
	mov		0, %fp

	rdpr		%pstate, %o1
	or		%o1, PSTATE_IE, %o1
	wrpr		%o1, 0, %pstate

	call		smp_callin
	 nop

	call		cpu_panic
	 nop
1:	b,a,pt		%xcc, 1b

	.align		8
sparc64_cpu_startup_end:
