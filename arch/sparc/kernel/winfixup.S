/* winfixup.S: Handle cases where user stack pointer is found to be bogus.
 *
 * Copyright (C) 1997, 2006 David S. Miller (davem@davemloft.net)
 */

#include <asm/asi.h>
#include <asm/head.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/spitfire.h>
#include <asm/thread_info.h>

	.text

	/* It used to be the case that these register window fault
	 * handlers could run via the save and restore instructions
	 * done by the trap entry and exit code.  They now do the
	 * window spill/fill by hand, so that case no longer can occur.
	 */

	.align	32
fill_fixup:
	TRAP_LOAD_THREAD_REG(%g6, %g1)
	rdpr	%tstate, %g1
	and	%g1, TSTATE_CWP, %g1
	or	%g4, FAULT_CODE_WINFIXUP, %g4
	stb	%g4, [%g6 + TI_FAULT_CODE]
	stx	%g5, [%g6 + TI_FAULT_ADDR]
	wrpr	%g1, %cwp
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	call	do_sparc64_fault
	 add	%sp, PTREGS_OFF, %o0
	ba,pt	%xcc, rtrap
	 nop

	/* Be very careful about usage of the trap globals here.
	 * You cannot touch %g5 as that has the fault information.
	 */
spill_fixup:
spill_fixup_mna:
spill_fixup_dax:
	TRAP_LOAD_THREAD_REG(%g6, %g1)
	ldx	[%g6 + TI_FLAGS], %g1
	andcc	%g1, _TIF_32BIT, %g0
	ldub	[%g6 + TI_WSAVED], %g1
	sll	%g1, 3, %g3
	add	%g6, %g3, %g3
	stx	%sp, [%g3 + TI_RWIN_SPTRS]
	sll	%g1, 7, %g3
	bne,pt	%xcc, 1f
	 add	%g6, %g3, %g3
	stx	%l0, [%g3 + TI_REG_WINDOW + 0x00]
	stx	%l1, [%g3 + TI_REG_WINDOW + 0x08]
	stx	%l2, [%g3 + TI_REG_WINDOW + 0x10]
	stx	%l3, [%g3 + TI_REG_WINDOW + 0x18]
	stx	%l4, [%g3 + TI_REG_WINDOW + 0x20]
	stx	%l5, [%g3 + TI_REG_WINDOW + 0x28]
	stx	%l6, [%g3 + TI_REG_WINDOW + 0x30]
	stx	%l7, [%g3 + TI_REG_WINDOW + 0x38]
	stx	%i0, [%g3 + TI_REG_WINDOW + 0x40]
	stx	%i1, [%g3 + TI_REG_WINDOW + 0x48]
	stx	%i2, [%g3 + TI_REG_WINDOW + 0x50]
	stx	%i3, [%g3 + TI_REG_WINDOW + 0x58]
	stx	%i4, [%g3 + TI_REG_WINDOW + 0x60]
	stx	%i5, [%g3 + TI_REG_WINDOW + 0x68]
	stx	%i6, [%g3 + TI_REG_WINDOW + 0x70]
	ba,pt	%xcc, 2f
	 stx	%i7, [%g3 + TI_REG_WINDOW + 0x78]
1:	stw	%l0, [%g3 + TI_REG_WINDOW + 0x00]
	stw	%l1, [%g3 + TI_REG_WINDOW + 0x04]
	stw	%l2, [%g3 + TI_REG_WINDOW + 0x08]
	stw	%l3, [%g3 + TI_REG_WINDOW + 0x0c]
	stw	%l4, [%g3 + TI_REG_WINDOW + 0x10]
	stw	%l5, [%g3 + TI_REG_WINDOW + 0x14]
	stw	%l6, [%g3 + TI_REG_WINDOW + 0x18]
	stw	%l7, [%g3 + TI_REG_WINDOW + 0x1c]
	stw	%i0, [%g3 + TI_REG_WINDOW + 0x20]
	stw	%i1, [%g3 + TI_REG_WINDOW + 0x24]
	stw	%i2, [%g3 + TI_REG_WINDOW + 0x28]
	stw	%i3, [%g3 + TI_REG_WINDOW + 0x2c]
	stw	%i4, [%g3 + TI_REG_WINDOW + 0x30]
	stw	%i5, [%g3 + TI_REG_WINDOW + 0x34]
	stw	%i6, [%g3 + TI_REG_WINDOW + 0x38]
	stw	%i7, [%g3 + TI_REG_WINDOW + 0x3c]
2:	add	%g1, 1, %g1
	stb	%g1, [%g6 + TI_WSAVED]
	rdpr	%tstate, %g1
	andcc	%g1, TSTATE_PRIV, %g0
	saved
	be,pn	%xcc, 1f
	 and	%g1, TSTATE_CWP, %g1
	retry
1:	mov	FAULT_CODE_WRITE | FAULT_CODE_DTLB | FAULT_CODE_WINFIXUP, %g4
	stb	%g4, [%g6 + TI_FAULT_CODE]
	stx	%g5, [%g6 + TI_FAULT_ADDR]
	wrpr	%g1, %cwp
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	call	do_sparc64_fault
	 add	%sp, PTREGS_OFF, %o0
	ba,a,pt	%xcc, rtrap

winfix_mna:
	andn	%g3, 0x7f, %g3
	add	%g3, 0x78, %g3
	wrpr	%g3, %tnpc
	done

fill_fixup_mna:
	rdpr	%tstate, %g1
	and	%g1, TSTATE_CWP, %g1
	wrpr	%g1, %cwp
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	sethi	%hi(tlb_type), %g1
	lduw	[%g1 + %lo(tlb_type)], %g1
	cmp	%g1, 3
	bne,pt	%icc, 1f
	 add	%sp, PTREGS_OFF, %o0
	mov	%l4, %o2
	call	sun4v_do_mna
	 mov	%l5, %o1
	ba,a,pt	%xcc, rtrap
1:	mov	%l4, %o1
	mov	%l5, %o2
	call	mem_address_unaligned
	 nop
	ba,a,pt	%xcc, rtrap

winfix_dax:
	andn	%g3, 0x7f, %g3
	add	%g3, 0x74, %g3
	wrpr	%g3, %tnpc
	done

fill_fixup_dax:
	rdpr	%tstate, %g1
	and	%g1, TSTATE_CWP, %g1
	wrpr	%g1, %cwp
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	sethi	%hi(tlb_type), %g1
	mov	%l4, %o1
	lduw	[%g1 + %lo(tlb_type)], %g1
	mov	%l5, %o2
	cmp	%g1, 3
	bne,pt	%icc, 1f
	 add	%sp, PTREGS_OFF, %o0
	call	sun4v_data_access_exception
	 nop
	ba,a,pt	%xcc, rtrap
1:	call	spitfire_data_access_exception
	 nop
	ba,a,pt	%xcc, rtrap
