/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/thread_info.h>
#include <asm/trap_block.h>
#include <asm/spitfire.h>
#include <asm/ptrace.h>
#include <asm/head.h>

		.text
		.align	8
		.globl	user_rtt_fill_fixup_common
user_rtt_fill_fixup_common:
		rdpr	%cwp, %g1
		add	%g1, 1, %g1
		wrpr	%g1, 0x0, %cwp

		rdpr	%wstate, %g2
		sll	%g2, 3, %g2
		wrpr	%g2, 0x0, %wstate

		/* We know %canrestore and %otherwin are both zero.  */

		sethi	%hi(sparc64_kern_pri_context), %g2
		ldx	[%g2 + %lo(sparc64_kern_pri_context)], %g2
		mov	PRIMARY_CONTEXT, %g1

661:		stxa	%g2, [%g1] ASI_DMMU
		.section .sun4v_1insn_patch, "ax"
		.word	661b
		stxa	%g2, [%g1] ASI_MMU
		.previous

		sethi	%hi(KERNBASE), %g1
		flush	%g1

		mov	%g4, %l4
		mov	%g5, %l5
		brnz,pn	%g3, 1f
		 mov	%g3, %l3

		or	%g4, FAULT_CODE_WINFIXUP, %g4
		stb	%g4, [%g6 + TI_FAULT_CODE]
		stx	%g5, [%g6 + TI_FAULT_ADDR]
1:
		mov	%g6, %l1
		wrpr	%g0, 0x0, %tl

661:		nop
		.section		.sun4v_1insn_patch, "ax"
		.word			661b
		SET_GL(0)
		.previous

		wrpr	%g0, RTRAP_PSTATE, %pstate

		mov	%l1, %g6
		ldx	[%g6 + TI_TASK], %g4
		LOAD_PER_CPU_BASE(%g5, %g6, %g1, %g2, %g3)

		brnz,pn	%l3, 1f
		 nop

		call	do_sparc64_fault
		 add	%sp, PTREGS_OFF, %o0
		ba,pt	%xcc, rtrap
		 nop

1:		cmp	%g3, 2
		bne,pn	%xcc, 2f
		 nop

		sethi	%hi(tlb_type), %g1
		lduw	[%g1 + %lo(tlb_type)], %g1
		cmp	%g1, 3
		bne,pt	%icc, 1f
		 add	%sp, PTREGS_OFF, %o0
		mov	%l4, %o2
		call	sun4v_do_mna
		 mov	%l5, %o1
		ba,a,pt	%xcc, rtrap
1:		mov	%l4, %o1
		mov	%l5, %o2
		call	mem_address_unaligned
		 nop
		ba,a,pt	%xcc, rtrap

2:		sethi	%hi(tlb_type), %g1
		mov	%l4, %o1
		lduw	[%g1 + %lo(tlb_type)], %g1
		mov	%l5, %o2
		cmp	%g1, 3
		bne,pt	%icc, 1f
		 add	%sp, PTREGS_OFF, %o0
		call	sun4v_data_access_exception
		 nop
		ba,a,pt	%xcc, rtrap
		 nop

1:		call	spitfire_data_access_exception
		 nop
		ba,a,pt	%xcc, rtrap
