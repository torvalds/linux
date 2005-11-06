/* $Id: winfixup.S,v 1.30 2002/02/09 19:49:30 davem Exp $
 *
 * winfixup.S: Handle cases where user stack pointer is found to be bogus.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/asi.h>
#include <asm/head.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/spitfire.h>
#include <asm/thread_info.h>

	.text

set_pcontext:
	sethi	%hi(sparc64_kern_pri_context), %l1
	ldx	[%l1 + %lo(sparc64_kern_pri_context)], %l1
	mov	PRIMARY_CONTEXT, %g1
	stxa	%l1, [%g1] ASI_DMMU
	flush	%g6
	retl
	 nop

	.align	32

	/* Here are the rules, pay attention.
	 *
	 * The kernel is disallowed from touching user space while
	 * the trap level is greater than zero, except for from within
	 * the window spill/fill handlers.  This must be followed
	 * so that we can easily detect the case where we tried to
	 * spill/fill with a bogus (or unmapped) user stack pointer.
	 *
	 * These are layed out in a special way for cache reasons,
	 * don't touch...
	 */
	.globl	fill_fixup, spill_fixup
fill_fixup:
	rdpr		%tstate, %g1
	andcc		%g1, TSTATE_PRIV, %g0
	or		%g4, FAULT_CODE_WINFIXUP, %g4
	be,pt		%xcc, window_scheisse_from_user_common
	 and		%g1, TSTATE_CWP, %g1

	/* This is the extremely complex case, but it does happen from
	 * time to time if things are just right.  Essentially the restore
	 * done in rtrap right before going back to user mode, with tl=1
	 * and that levels trap stack registers all setup, took a fill trap,
	 * the user stack was not mapped in the tlb, and tlb miss occurred,
	 * the pte found was not valid, and a simple ref bit watch update
	 * could not satisfy the miss, so we got here.
	 *
	 * We must carefully unwind the state so we get back to tl=0, preserve
	 * all the register values we were going to give to the user.  Luckily
	 * most things are where they need to be, we also have the address
	 * which triggered the fault handy as well.
	 *
	 * Also note that we must preserve %l5 and %l6.  If the user was
	 * returning from a system call, we must make it look this way
	 * after we process the fill fault on the users stack.
	 *
	 * First, get into the window where the original restore was executed.
	 */

	rdpr		%wstate, %g2			! Grab user mode wstate.
	wrpr		%g1, %cwp			! Get into the right window.
	sll		%g2, 3, %g2			! NORMAL-->OTHER

	wrpr		%g0, 0x0, %canrestore		! Standard etrap stuff.
	wrpr		%g2, 0x0, %wstate		! This must be consistent.
	wrpr		%g0, 0x0, %otherwin		! We know this.
	call		set_pcontext			! Change contexts...
	 nop
	rdpr		%pstate, %l1			! Prepare to change globals.
	mov		%g6, %o7			! Get current.

	andn		%l1, PSTATE_MM, %l1		! We want to be in RMO
	stb		%g4, [%g6 + TI_FAULT_CODE]
	stx		%g5, [%g6 + TI_FAULT_ADDR]
	wrpr		%g0, 0x0, %tl			! Out of trap levels.
	wrpr		%l1, (PSTATE_IE | PSTATE_AG | PSTATE_RMO), %pstate
	mov		%o7, %g6
	ldx		[%g6 + TI_TASK], %g4
#ifdef CONFIG_SMP
	mov		TSB_REG, %g1
	ldxa		[%g1] ASI_IMMU, %g5
#endif

	/* This is the same as below, except we handle this a bit special
	 * since we must preserve %l5 and %l6, see comment above.
	 */
	call		do_sparc64_fault
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 nop						! yes, nop is correct

	/* Be very careful about usage of the alternate globals here.
	 * You cannot touch %g4/%g5 as that has the fault information
	 * should this be from usermode.  Also be careful for the case
	 * where we get here from the save instruction in etrap.S when
	 * coming from either user or kernel (does not matter which, it
	 * is the same problem in both cases).  Essentially this means
	 * do not touch %g7 or %g2 so we handle the two cases fine.
	 */
spill_fixup:
	ldx		[%g6 + TI_FLAGS], %g1
	andcc		%g1, _TIF_32BIT, %g0
	ldub		[%g6 + TI_WSAVED], %g1

	sll		%g1, 3, %g3
	add		%g6, %g3, %g3
	stx		%sp, [%g3 + TI_RWIN_SPTRS]
	sll		%g1, 7, %g3
	bne,pt		%xcc, 1f
	 add		%g6, %g3, %g3
	stx		%l0, [%g3 + TI_REG_WINDOW + 0x00]
	stx		%l1, [%g3 + TI_REG_WINDOW + 0x08]

	stx		%l2, [%g3 + TI_REG_WINDOW + 0x10]
	stx		%l3, [%g3 + TI_REG_WINDOW + 0x18]
	stx		%l4, [%g3 + TI_REG_WINDOW + 0x20]
	stx		%l5, [%g3 + TI_REG_WINDOW + 0x28]
	stx		%l6, [%g3 + TI_REG_WINDOW + 0x30]
	stx		%l7, [%g3 + TI_REG_WINDOW + 0x38]
	stx		%i0, [%g3 + TI_REG_WINDOW + 0x40]
	stx		%i1, [%g3 + TI_REG_WINDOW + 0x48]

	stx		%i2, [%g3 + TI_REG_WINDOW + 0x50]
	stx		%i3, [%g3 + TI_REG_WINDOW + 0x58]
	stx		%i4, [%g3 + TI_REG_WINDOW + 0x60]
	stx		%i5, [%g3 + TI_REG_WINDOW + 0x68]
	stx		%i6, [%g3 + TI_REG_WINDOW + 0x70]
	b,pt		%xcc, 2f
	 stx		%i7, [%g3 + TI_REG_WINDOW + 0x78]
1:	stw		%l0, [%g3 + TI_REG_WINDOW + 0x00]

	stw		%l1, [%g3 + TI_REG_WINDOW + 0x04]
	stw		%l2, [%g3 + TI_REG_WINDOW + 0x08]
	stw		%l3, [%g3 + TI_REG_WINDOW + 0x0c]
	stw		%l4, [%g3 + TI_REG_WINDOW + 0x10]
	stw		%l5, [%g3 + TI_REG_WINDOW + 0x14]
	stw		%l6, [%g3 + TI_REG_WINDOW + 0x18]
	stw		%l7, [%g3 + TI_REG_WINDOW + 0x1c]
	stw		%i0, [%g3 + TI_REG_WINDOW + 0x20]

	stw		%i1, [%g3 + TI_REG_WINDOW + 0x24]
	stw		%i2, [%g3 + TI_REG_WINDOW + 0x28]
	stw		%i3, [%g3 + TI_REG_WINDOW + 0x2c]
	stw		%i4, [%g3 + TI_REG_WINDOW + 0x30]
	stw		%i5, [%g3 + TI_REG_WINDOW + 0x34]
	stw		%i6, [%g3 + TI_REG_WINDOW + 0x38]
	stw		%i7, [%g3 + TI_REG_WINDOW + 0x3c]
2:	add		%g1, 1, %g1

	stb		%g1, [%g6 + TI_WSAVED]
	rdpr		%tstate, %g1
	andcc		%g1, TSTATE_PRIV, %g0
	saved
	and		%g1, TSTATE_CWP, %g1
	be,pn		%xcc, window_scheisse_from_user_common
	 mov		FAULT_CODE_WRITE | FAULT_CODE_DTLB | FAULT_CODE_WINFIXUP, %g4
	retry

window_scheisse_from_user_common:
	stb		%g4, [%g6 + TI_FAULT_CODE]
	stx		%g5, [%g6 + TI_FAULT_ADDR]
	wrpr		%g1, %cwp
	ba,pt		%xcc, etrap
	 rd		%pc, %g7
	call		do_sparc64_fault
	 add		%sp, PTREGS_OFF, %o0
	ba,a,pt		%xcc, rtrap_clr_l6

	.globl		winfix_mna, fill_fixup_mna, spill_fixup_mna
winfix_mna:
	andn		%g3, 0x7f, %g3
	add		%g3, 0x78, %g3
	wrpr		%g3, %tnpc
	done
fill_fixup_mna:
	rdpr		%tstate, %g1
	andcc		%g1, TSTATE_PRIV, %g0
	be,pt		%xcc, window_mna_from_user_common
	 and		%g1, TSTATE_CWP, %g1

	/* Please, see fill_fixup commentary about why we must preserve
	 * %l5 and %l6 to preserve absolute correct semantics.
	 */
	rdpr		%wstate, %g2			! Grab user mode wstate.
	wrpr		%g1, %cwp			! Get into the right window.
	sll		%g2, 3, %g2			! NORMAL-->OTHER
	wrpr		%g0, 0x0, %canrestore		! Standard etrap stuff.

	wrpr		%g2, 0x0, %wstate		! This must be consistent.
	wrpr		%g0, 0x0, %otherwin		! We know this.
	call		set_pcontext			! Change contexts...
	 nop
	rdpr		%pstate, %l1			! Prepare to change globals.
	mov		%g4, %o2			! Setup args for
	mov		%g5, %o1			! final call to mem_address_unaligned.
	andn		%l1, PSTATE_MM, %l1		! We want to be in RMO

	mov		%g6, %o7			! Stash away current.
	wrpr		%g0, 0x0, %tl			! Out of trap levels.
	wrpr		%l1, (PSTATE_IE | PSTATE_AG | PSTATE_RMO), %pstate
	mov		%o7, %g6			! Get current back.
	ldx		[%g6 + TI_TASK], %g4		! Finish it.
#ifdef CONFIG_SMP
	mov		TSB_REG, %g1
	ldxa		[%g1] ASI_IMMU, %g5
#endif
	call		mem_address_unaligned
	 add		%sp, PTREGS_OFF, %o0

	b,pt		%xcc, rtrap
	 nop						! yes, the nop is correct
spill_fixup_mna:
	ldx		[%g6 + TI_FLAGS], %g1
	andcc		%g1, _TIF_32BIT, %g0
	ldub		[%g6 + TI_WSAVED], %g1
	sll		%g1, 3, %g3
	add		%g6, %g3, %g3
	stx		%sp, [%g3 + TI_RWIN_SPTRS]

	sll		%g1, 7, %g3
	bne,pt		%xcc, 1f
	 add		%g6, %g3, %g3
	stx		%l0, [%g3 + TI_REG_WINDOW + 0x00]
	stx		%l1, [%g3 + TI_REG_WINDOW + 0x08]
	stx		%l2, [%g3 + TI_REG_WINDOW + 0x10]
	stx		%l3, [%g3 + TI_REG_WINDOW + 0x18]
	stx		%l4, [%g3 + TI_REG_WINDOW + 0x20]

	stx		%l5, [%g3 + TI_REG_WINDOW + 0x28]
	stx		%l6, [%g3 + TI_REG_WINDOW + 0x30]
	stx		%l7, [%g3 + TI_REG_WINDOW + 0x38]
	stx		%i0, [%g3 + TI_REG_WINDOW + 0x40]
	stx		%i1, [%g3 + TI_REG_WINDOW + 0x48]
	stx		%i2, [%g3 + TI_REG_WINDOW + 0x50]
	stx		%i3, [%g3 + TI_REG_WINDOW + 0x58]
	stx		%i4, [%g3 + TI_REG_WINDOW + 0x60]

	stx		%i5, [%g3 + TI_REG_WINDOW + 0x68]
	stx		%i6, [%g3 + TI_REG_WINDOW + 0x70]
	stx		%i7, [%g3 + TI_REG_WINDOW + 0x78]
	b,pt		%xcc, 2f
	 add		%g1, 1, %g1
1:	std		%l0, [%g3 + TI_REG_WINDOW + 0x00]
	std		%l2, [%g3 + TI_REG_WINDOW + 0x08]
	std		%l4, [%g3 + TI_REG_WINDOW + 0x10]

	std		%l6, [%g3 + TI_REG_WINDOW + 0x18]
	std		%i0, [%g3 + TI_REG_WINDOW + 0x20]
	std		%i2, [%g3 + TI_REG_WINDOW + 0x28]
	std		%i4, [%g3 + TI_REG_WINDOW + 0x30]
	std		%i6, [%g3 + TI_REG_WINDOW + 0x38]
	add		%g1, 1, %g1
2:	stb		%g1, [%g6 + TI_WSAVED]
	rdpr		%tstate, %g1

	andcc		%g1, TSTATE_PRIV, %g0
	saved
	be,pn		%xcc, window_mna_from_user_common
	 and		%g1, TSTATE_CWP, %g1
	retry
window_mna_from_user_common:
	wrpr		%g1, %cwp
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o2
	mov		%l5, %o1
	call		mem_address_unaligned
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6
	
	/* These are only needed for 64-bit mode processes which
	 * put their stack pointer into the VPTE area and there
	 * happens to be a VPTE tlb entry mapped there during
	 * a spill/fill trap to that stack frame.
	 */
	.globl		winfix_dax, fill_fixup_dax, spill_fixup_dax
winfix_dax:
	andn		%g3, 0x7f, %g3
	add		%g3, 0x74, %g3
	wrpr		%g3, %tnpc
	done
fill_fixup_dax:
	rdpr		%tstate, %g1
	andcc		%g1, TSTATE_PRIV, %g0
	be,pt		%xcc, window_dax_from_user_common
	 and		%g1, TSTATE_CWP, %g1

	/* Please, see fill_fixup commentary about why we must preserve
	 * %l5 and %l6 to preserve absolute correct semantics.
	 */
	rdpr		%wstate, %g2			! Grab user mode wstate.
	wrpr		%g1, %cwp			! Get into the right window.
	sll		%g2, 3, %g2			! NORMAL-->OTHER
	wrpr		%g0, 0x0, %canrestore		! Standard etrap stuff.

	wrpr		%g2, 0x0, %wstate		! This must be consistent.
	wrpr		%g0, 0x0, %otherwin		! We know this.
	call		set_pcontext			! Change contexts...
	 nop
	rdpr		%pstate, %l1			! Prepare to change globals.
	mov		%g4, %o1			! Setup args for
	mov		%g5, %o2			! final call to spitfire_data_access_exception.
	andn		%l1, PSTATE_MM, %l1		! We want to be in RMO

	mov		%g6, %o7			! Stash away current.
	wrpr		%g0, 0x0, %tl			! Out of trap levels.
	wrpr		%l1, (PSTATE_IE | PSTATE_AG | PSTATE_RMO), %pstate
	mov		%o7, %g6			! Get current back.
	ldx		[%g6 + TI_TASK], %g4		! Finish it.
#ifdef CONFIG_SMP
	mov		TSB_REG, %g1
	ldxa		[%g1] ASI_IMMU, %g5
#endif
	call		spitfire_data_access_exception
	 add		%sp, PTREGS_OFF, %o0

	b,pt		%xcc, rtrap
	 nop						! yes, the nop is correct
spill_fixup_dax:
	ldx		[%g6 + TI_FLAGS], %g1
	andcc		%g1, _TIF_32BIT, %g0
	ldub		[%g6 + TI_WSAVED], %g1
	sll		%g1, 3, %g3
	add		%g6, %g3, %g3
	stx		%sp, [%g3 + TI_RWIN_SPTRS]

	sll		%g1, 7, %g3
	bne,pt		%xcc, 1f
	 add		%g6, %g3, %g3
	stx		%l0, [%g3 + TI_REG_WINDOW + 0x00]
	stx		%l1, [%g3 + TI_REG_WINDOW + 0x08]
	stx		%l2, [%g3 + TI_REG_WINDOW + 0x10]
	stx		%l3, [%g3 + TI_REG_WINDOW + 0x18]
	stx		%l4, [%g3 + TI_REG_WINDOW + 0x20]

	stx		%l5, [%g3 + TI_REG_WINDOW + 0x28]
	stx		%l6, [%g3 + TI_REG_WINDOW + 0x30]
	stx		%l7, [%g3 + TI_REG_WINDOW + 0x38]
	stx		%i0, [%g3 + TI_REG_WINDOW + 0x40]
	stx		%i1, [%g3 + TI_REG_WINDOW + 0x48]
	stx		%i2, [%g3 + TI_REG_WINDOW + 0x50]
	stx		%i3, [%g3 + TI_REG_WINDOW + 0x58]
	stx		%i4, [%g3 + TI_REG_WINDOW + 0x60]

	stx		%i5, [%g3 + TI_REG_WINDOW + 0x68]
	stx		%i6, [%g3 + TI_REG_WINDOW + 0x70]
	stx		%i7, [%g3 + TI_REG_WINDOW + 0x78]
	b,pt		%xcc, 2f
	 add		%g1, 1, %g1
1:	std		%l0, [%g3 + TI_REG_WINDOW + 0x00]
	std		%l2, [%g3 + TI_REG_WINDOW + 0x08]
	std		%l4, [%g3 + TI_REG_WINDOW + 0x10]

	std		%l6, [%g3 + TI_REG_WINDOW + 0x18]
	std		%i0, [%g3 + TI_REG_WINDOW + 0x20]
	std		%i2, [%g3 + TI_REG_WINDOW + 0x28]
	std		%i4, [%g3 + TI_REG_WINDOW + 0x30]
	std		%i6, [%g3 + TI_REG_WINDOW + 0x38]
	add		%g1, 1, %g1
2:	stb		%g1, [%g6 + TI_WSAVED]
	rdpr		%tstate, %g1

	andcc		%g1, TSTATE_PRIV, %g0
	saved
	be,pn		%xcc, window_dax_from_user_common
	 and		%g1, TSTATE_CWP, %g1
	retry
window_dax_from_user_common:
	wrpr		%g1, %cwp
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		spitfire_data_access_exception
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6
