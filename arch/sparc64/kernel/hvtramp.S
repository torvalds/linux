/* hvtramp.S: Hypervisor start-cpu trampoline code.
 *
 * Copyright (C) 2007 David S. Miller <davem@davemloft.net>
 */

#include <asm/thread_info.h>
#include <asm/hypervisor.h>
#include <asm/scratchpad.h>
#include <asm/spitfire.h>
#include <asm/hvtramp.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/head.h>
#include <asm/asi.h>

	.text
	.align		8
	.globl		hv_cpu_startup, hv_cpu_startup_end

	/* This code executes directly out of the hypervisor
	 * with physical addressing (va==pa).  %o0 contains
	 * our client argument which for Linux points to
	 * a descriptor data structure which defines the
	 * MMU entries we need to load up.
	 *
	 * After we set things up we enable the MMU and call
	 * into the kernel.
	 *
	 * First setup basic privileged cpu state.
	 */
hv_cpu_startup:
	SET_GL(0)
	wrpr		%g0, 15, %pil
	wrpr		%g0, 0, %canrestore
	wrpr		%g0, 0, %otherwin
	wrpr		%g0, 6, %cansave
	wrpr		%g0, 6, %cleanwin
	wrpr		%g0, 0, %cwp
	wrpr		%g0, 0, %wstate
	wrpr		%g0, 0, %tl

	sethi		%hi(sparc64_ttable_tl0), %g1
	wrpr		%g1, %tba

	mov		%o0, %l0

	lduw		[%l0 + HVTRAMP_DESCR_CPU], %g1
	mov		SCRATCHPAD_CPUID, %g2
	stxa		%g1, [%g2] ASI_SCRATCHPAD

	ldx		[%l0 + HVTRAMP_DESCR_FAULT_INFO_VA], %g2
	stxa		%g2, [%g0] ASI_SCRATCHPAD

	mov		0, %l1
	lduw		[%l0 + HVTRAMP_DESCR_NUM_MAPPINGS], %l2
	add		%l0, HVTRAMP_DESCR_MAPS, %l3

1:	ldx		[%l3 + HVTRAMP_MAPPING_VADDR], %o0
	clr		%o1
	ldx		[%l3 + HVTRAMP_MAPPING_TTE], %o2
	mov		HV_MMU_IMMU | HV_MMU_DMMU, %o3
	mov		HV_FAST_MMU_MAP_PERM_ADDR, %o5
	ta		HV_FAST_TRAP

	brnz,pn		%o0, 80f
	 nop

	add		%l1, 1, %l1
	cmp		%l1, %l2
	blt,a,pt	%xcc, 1b
	 add		%l3, HVTRAMP_MAPPING_SIZE, %l3

	ldx		[%l0 + HVTRAMP_DESCR_FAULT_INFO_PA], %o0
	mov		HV_FAST_MMU_FAULT_AREA_CONF, %o5
	ta		HV_FAST_TRAP

	brnz,pn		%o0, 80f
	 nop

	wrpr		%g0, (PSTATE_PRIV | PSTATE_PEF), %pstate

	ldx		[%l0 + HVTRAMP_DESCR_THREAD_REG], %l6

	mov		1, %o0
	set		1f, %o1
	mov		HV_FAST_MMU_ENABLE, %o5
	ta		HV_FAST_TRAP

	ba,pt		%xcc, 80f
	 nop

1:
	wr		%g0, 0, %fprs
	wr		%g0, ASI_P, %asi

	mov		PRIMARY_CONTEXT, %g7
	stxa		%g0, [%g7] ASI_MMU
	membar		#Sync

	mov		SECONDARY_CONTEXT, %g7
	stxa		%g0, [%g7] ASI_MMU
	membar		#Sync

	mov		%l6, %g6
	ldx		[%g6 + TI_TASK], %g4

	mov		1, %g5
	sllx		%g5, THREAD_SHIFT, %g5
	sub		%g5, (STACKFRAME_SZ + STACK_BIAS), %g5
	add		%g6, %g5, %sp
	mov		0, %fp

	call		init_irqwork_curcpu
	 nop
	call		hard_smp_processor_id
	 nop

	call		sun4v_register_mondo_queues
	 nop

	call		init_cur_cpu_trap
	 mov		%g6, %o0

	wrpr		%g0, (PSTATE_PRIV | PSTATE_PEF | PSTATE_IE), %pstate

	call		smp_callin
	 nop
	call		cpu_idle
	 mov		0, %o0
	call		cpu_panic
	 nop

80:	ba,pt		%xcc, 80b
	 nop

	.align		8
hv_cpu_startup_end:
