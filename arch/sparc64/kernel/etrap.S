/* $Id: etrap.S,v 1.46 2002/02/09 19:49:30 davem Exp $
 * etrap.S: Preparing for entry into the kernel on Sparc V9.
 *
 * Copyright (C) 1996, 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997, 1998, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 */

#include <linux/config.h>

#include <asm/asi.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/spitfire.h>
#include <asm/head.h>
#include <asm/processor.h>
#include <asm/mmu.h>

#define		TASK_REGOFF		(THREAD_SIZE-TRACEREG_SZ-STACKFRAME_SZ)
#define		ETRAP_PSTATE1		(PSTATE_RMO | PSTATE_PRIV)
#define		ETRAP_PSTATE2		\
		(PSTATE_RMO | PSTATE_PEF | PSTATE_PRIV | PSTATE_IE)

/*
 * On entry, %g7 is return address - 0x4.
 * %g4 and %g5 will be preserved %l4 and %l5 respectively.
 */

		.text		
		.align	64
		.globl	etrap, etrap_irq, etraptl1
etrap:		rdpr	%pil, %g2
etrap_irq:
		rdpr	%tstate, %g1
		sllx	%g2, 20, %g3
		andcc	%g1, TSTATE_PRIV, %g0
		or	%g1, %g3, %g1
		bne,pn	%xcc, 1f
		 sub	%sp, STACKFRAME_SZ+TRACEREG_SZ-STACK_BIAS, %g2
		wrpr	%g0, 7, %cleanwin

		sethi	%hi(TASK_REGOFF), %g2
		sethi	%hi(TSTATE_PEF), %g3
		or	%g2, %lo(TASK_REGOFF), %g2
		and	%g1, %g3, %g3
		brnz,pn	%g3, 1f
		 add	%g6, %g2, %g2
		wr	%g0, 0, %fprs
1:		rdpr	%tpc, %g3

		stx	%g1, [%g2 + STACKFRAME_SZ + PT_V9_TSTATE]
		rdpr	%tnpc, %g1
		stx	%g3, [%g2 + STACKFRAME_SZ + PT_V9_TPC]
		rd	%y, %g3
		stx	%g1, [%g2 + STACKFRAME_SZ + PT_V9_TNPC]
		st	%g3, [%g2 + STACKFRAME_SZ + PT_V9_Y]
		save	%g2, -STACK_BIAS, %sp	! Ordering here is critical
		mov	%g6, %l6

		bne,pn	%xcc, 3f
		 mov	PRIMARY_CONTEXT, %l4
		rdpr	%canrestore, %g3
		rdpr	%wstate, %g2
		wrpr	%g0, 0, %canrestore
		sll	%g2, 3, %g2
		mov	1, %l5
		stb	%l5, [%l6 + TI_FPDEPTH]

		wrpr	%g3, 0, %otherwin
		wrpr	%g2, 0, %wstate
		sethi	%hi(sparc64_kern_pri_context), %g2
		ldx	[%g2 + %lo(sparc64_kern_pri_context)], %g3
		stxa	%g3, [%l4] ASI_DMMU
		flush	%l6
		wr	%g0, ASI_AIUS, %asi
2:		wrpr	%g0, 0x0, %tl
		mov	%g4, %l4
		mov	%g5, %l5

		mov	%g7, %l2
		wrpr	%g0, ETRAP_PSTATE1, %pstate
		stx	%g1, [%sp + PTREGS_OFF + PT_V9_G1]
		stx	%g2, [%sp + PTREGS_OFF + PT_V9_G2]
		stx	%g3, [%sp + PTREGS_OFF + PT_V9_G3]
		stx	%g4, [%sp + PTREGS_OFF + PT_V9_G4]
		stx	%g5, [%sp + PTREGS_OFF + PT_V9_G5]
		stx	%g6, [%sp + PTREGS_OFF + PT_V9_G6]

		stx	%g7, [%sp + PTREGS_OFF + PT_V9_G7]
		stx	%i0, [%sp + PTREGS_OFF + PT_V9_I0]
		stx	%i1, [%sp + PTREGS_OFF + PT_V9_I1]
		stx	%i2, [%sp + PTREGS_OFF + PT_V9_I2]
		stx	%i3, [%sp + PTREGS_OFF + PT_V9_I3]
		stx	%i4, [%sp + PTREGS_OFF + PT_V9_I4]
		stx	%i5, [%sp + PTREGS_OFF + PT_V9_I5]

		stx	%i6, [%sp + PTREGS_OFF + PT_V9_I6]
		stx	%i7, [%sp + PTREGS_OFF + PT_V9_I7]
		wrpr	%g0, ETRAP_PSTATE2, %pstate
		mov	%l6, %g6
#ifdef CONFIG_SMP
		mov	TSB_REG, %g3
		ldxa	[%g3] ASI_IMMU, %g5
#endif
		jmpl	%l2 + 0x4, %g0
		 ldx	[%g6 + TI_TASK], %g4

3:		ldub	[%l6 + TI_FPDEPTH], %l5
		add	%l6, TI_FPSAVED + 1, %l4
		srl	%l5, 1, %l3
		add	%l5, 2, %l5
		stb	%l5, [%l6 + TI_FPDEPTH]
		ba,pt	%xcc, 2b
		 stb	%g0, [%l4 + %l3]
		nop

etraptl1:	/* Save tstate/tpc/tnpc of TL 1-->4 and the tl register itself.
		 * We place this right after pt_regs on the trap stack.
		 * The layout is:
		 *	0x00	TL1's TSTATE
		 *	0x08	TL1's TPC
		 *	0x10	TL1's TNPC
		 *	0x18	TL1's TT
		 *	 ...
		 *	0x58	TL4's TT
		 *	0x60	TL
		 */
		sub	%sp, ((4 * 8) * 4) + 8, %g2
		rdpr	%tl, %g1

		wrpr	%g0, 1, %tl
		rdpr	%tstate, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x00]
		rdpr	%tpc, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x08]
		rdpr	%tnpc, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x10]
		rdpr	%tt, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x18]

		wrpr	%g0, 2, %tl
		rdpr	%tstate, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x20]
		rdpr	%tpc, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x28]
		rdpr	%tnpc, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x30]
		rdpr	%tt, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x38]

		wrpr	%g0, 3, %tl
		rdpr	%tstate, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x40]
		rdpr	%tpc, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x48]
		rdpr	%tnpc, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x50]
		rdpr	%tt, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x58]

		wrpr	%g0, 4, %tl
		rdpr	%tstate, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x60]
		rdpr	%tpc, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x68]
		rdpr	%tnpc, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x70]
		rdpr	%tt, %g3
		stx	%g3, [%g2 + STACK_BIAS + 0x78]

		wrpr	%g1, %tl
		stx	%g1, [%g2 + STACK_BIAS + 0x80]

		rdpr	%tstate, %g1
		sub	%g2, STACKFRAME_SZ + TRACEREG_SZ - STACK_BIAS, %g2
		ba,pt	%xcc, 1b
		 andcc	%g1, TSTATE_PRIV, %g0

		.align	64
		.globl	scetrap
scetrap:	rdpr	%pil, %g2
		rdpr	%tstate, %g1
		sllx	%g2, 20, %g3
		andcc	%g1, TSTATE_PRIV, %g0
		or	%g1, %g3, %g1
		bne,pn	%xcc, 1f
		 sub	%sp, (STACKFRAME_SZ+TRACEREG_SZ-STACK_BIAS), %g2
		wrpr	%g0, 7, %cleanwin

		sllx	%g1, 51, %g3
		sethi	%hi(TASK_REGOFF), %g2
		or	%g2, %lo(TASK_REGOFF), %g2
		brlz,pn	%g3, 1f
		 add	%g6, %g2, %g2
		wr	%g0, 0, %fprs
1:		rdpr	%tpc, %g3
		stx	%g1, [%g2 + STACKFRAME_SZ + PT_V9_TSTATE]

		rdpr	%tnpc, %g1
		stx	%g3, [%g2 + STACKFRAME_SZ + PT_V9_TPC]
		stx	%g1, [%g2 + STACKFRAME_SZ + PT_V9_TNPC]
		save	%g2, -STACK_BIAS, %sp	! Ordering here is critical
		mov	%g6, %l6
		bne,pn	%xcc, 2f
		 mov	ASI_P, %l7
		rdpr	%canrestore, %g3

		rdpr	%wstate, %g2
		wrpr	%g0, 0, %canrestore
		sll	%g2, 3, %g2
		mov	PRIMARY_CONTEXT, %l4
		wrpr	%g3, 0, %otherwin
		wrpr	%g2, 0, %wstate
		sethi	%hi(sparc64_kern_pri_context), %g2
		ldx	[%g2 + %lo(sparc64_kern_pri_context)], %g3
		stxa	%g3, [%l4] ASI_DMMU
		flush	%l6

		mov	ASI_AIUS, %l7
2:		mov	%g4, %l4
		mov	%g5, %l5
		add	%g7, 0x4, %l2
		wrpr	%g0, ETRAP_PSTATE1, %pstate
		stx	%g1, [%sp + PTREGS_OFF + PT_V9_G1]
		stx	%g2, [%sp + PTREGS_OFF + PT_V9_G2]
		sllx	%l7, 24, %l7

		stx	%g3, [%sp + PTREGS_OFF + PT_V9_G3]
		rdpr	%cwp, %l0
		stx	%g4, [%sp + PTREGS_OFF + PT_V9_G4]
		stx	%g5, [%sp + PTREGS_OFF + PT_V9_G5]
		stx	%g6, [%sp + PTREGS_OFF + PT_V9_G6]
		stx	%g7, [%sp + PTREGS_OFF + PT_V9_G7]
		or	%l7, %l0, %l7
		sethi	%hi(TSTATE_RMO | TSTATE_PEF), %l0

		or	%l7, %l0, %l7
		wrpr	%l2, %tnpc
		wrpr	%l7, (TSTATE_PRIV | TSTATE_IE), %tstate
		stx	%i0, [%sp + PTREGS_OFF + PT_V9_I0]
		stx	%i1, [%sp + PTREGS_OFF + PT_V9_I1]
		stx	%i2, [%sp + PTREGS_OFF + PT_V9_I2]
		stx	%i3, [%sp + PTREGS_OFF + PT_V9_I3]
		stx	%i4, [%sp + PTREGS_OFF + PT_V9_I4]

		stx	%i5, [%sp + PTREGS_OFF + PT_V9_I5]
		stx	%i6, [%sp + PTREGS_OFF + PT_V9_I6]
		mov	%l6, %g6
		stx	%i7, [%sp + PTREGS_OFF + PT_V9_I7]
#ifdef CONFIG_SMP
		mov	TSB_REG, %g3
		ldxa	[%g3] ASI_IMMU, %g5
#endif
		ldx	[%g6 + TI_TASK], %g4
		done

#undef TASK_REGOFF
#undef ETRAP_PSTATE1
