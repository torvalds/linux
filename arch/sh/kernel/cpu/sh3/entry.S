/*
 * arch/sh/kernel/cpu/sh3/entry.S
 *
 *  Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 *  Copyright (C) 2003 - 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/sys.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>
#include <cpu/mmu_context.h>
#include <asm/page.h>
#include <asm/cache.h>

! NOTE:
! GNU as (as of 2.9.1) changes bf/s into bt/s and bra, when the address
! to be jumped is too far, but it causes illegal slot exception.

/*	
 * entry.S contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 * NOTE: This code uses a convention that instructions in the delay slot
 * of a transfer-control instruction are indented by an extra space, thus:
 *
 *    jmp	@k0	    ! control-transfer instruction
 *     ldc	k1, ssr     ! delay slot
 *
 * Stack layout in 'ret_from_syscall':
 * 	ptrace needs to have all regs on the stack.
 *	if the order here is changed, it needs to be
 *	updated in ptrace.c and ptrace.h
 *
 *	r0
 *      ...
 *	r15 = stack pointer
 *	spc
 *	pr
 *	ssr
 *	gbr
 *	mach
 *	macl
 *	syscall #
 *
 */
#if defined(CONFIG_KGDB)
NMI_VEC = 0x1c0			! Must catch early for debounce
#endif

/* Offsets to the stack */
OFF_R0  =  0		/* Return value. New ABI also arg4 */
OFF_R1  =  4     	/* New ABI: arg5 */
OFF_R2  =  8     	/* New ABI: arg6 */
OFF_R3  =  12     	/* New ABI: syscall_nr */
OFF_R4  =  16     	/* New ABI: arg0 */
OFF_R5  =  20     	/* New ABI: arg1 */
OFF_R6  =  24     	/* New ABI: arg2 */
OFF_R7  =  28     	/* New ABI: arg3 */
OFF_SP	=  (15*4)
OFF_PC  =  (16*4)
OFF_SR	=  (16*4+8)
OFF_TRA	=  (16*4+6*4)


#define k0	r0
#define k1	r1
#define k2	r2
#define k3	r3
#define k4	r4

#define g_imask		r6	/* r6_bank1 */
#define k_g_imask	r6_bank	/* r6_bank1 */
#define current		r7	/* r7_bank1 */

#include <asm/entry-macros.S>
	
/*
 * Kernel mode register usage:
 *	k0	scratch
 *	k1	scratch
 *	k2	scratch (Exception code)
 *	k3	scratch (Return address)
 *	k4	scratch
 *	k5	reserved
 *	k6	Global Interrupt Mask (0--15 << 4)
 *	k7	CURRENT_THREAD_INFO (pointer to current thread info)
 */

!
! TLB Miss / Initial Page write exception handling
!			_and_
! TLB hits, but the access violate the protection.
! It can be valid access, such as stack grow and/or C-O-W.
!
!
! Find the pmd/pte entry and loadtlb
! If it's not found, cause address error (SEGV)
!
! Although this could be written in assembly language (and it'd be faster),
! this first version depends *much* on C implementation.
!

#if defined(CONFIG_MMU)
	.align	2
ENTRY(tlb_miss_load)
	bra	call_dpf
	 mov	#0, r5

	.align	2
ENTRY(tlb_miss_store)
	bra	call_dpf
	 mov	#1, r5

	.align	2
ENTRY(initial_page_write)
	bra	call_dpf
	 mov	#1, r5

	.align	2
ENTRY(tlb_protection_violation_load)
	bra	call_dpf
	 mov	#0, r5

	.align	2
ENTRY(tlb_protection_violation_store)
	bra	call_dpf
	 mov	#1, r5

call_dpf:
	mov.l	1f, r0
	mov	r5, r8
	mov.l	@r0, r6
	mov	r6, r9
	mov.l	2f, r0
	sts	pr, r10
	jsr	@r0
	 mov	r15, r4
	!
	tst	r0, r0
	bf/s	0f
	 lds	r10, pr
	rts
	 nop
0:	mov.l	3f, r0
	mov	r9, r6
	mov	r8, r5
	jmp	@r0
	 mov	r15, r4

	.align 2
1:	.long	MMU_TEA
2:	.long	__do_page_fault
3:	.long	do_page_fault

	.align	2
ENTRY(address_error_load)
	bra	call_dae
	 mov	#0,r5		! writeaccess = 0

	.align	2
ENTRY(address_error_store)
	bra	call_dae
	 mov	#1,r5		! writeaccess = 1

	.align	2
call_dae:
	mov.l	1f, r0
	mov.l	@r0, r6		! address
	mov.l	2f, r0
	jmp	@r0
	 mov	r15, r4		! regs

	.align 2
1:	.long	MMU_TEA
2:	.long   do_address_error
#endif /* CONFIG_MMU */

#if defined(CONFIG_SH_STANDARD_BIOS)
	/* Unwind the stack and jmp to the debug entry */
ENTRY(sh_bios_handler)
	mov.l	1f, r8
	bsr	restore_regs
	 nop

	lds	k2, pr			! restore pr
	mov	k4, r15
	!
	mov.l	2f, k0
	mov.l	@k0, k0
	jmp	@k0
	 ldc	k3, ssr
	.align	2
1:	.long	0x300000f0
2:	.long	gdb_vbr_vector
#endif /* CONFIG_SH_STANDARD_BIOS */

! restore_regs()
! - restore r0, r1, r2, r3, r4, r5, r6, r7 from the stack
! - switch bank
! - restore r8, r9, r10, r11, r12, r13, r14, r15 from the stack
! - restore spc, pr*, ssr, gbr, mach, macl, skip default tra
! k2 returns original pr
! k3 returns original sr
! k4 returns original stack pointer
! r8 passes SR bitmask, overwritten with restored data on return
! r9 trashed
! BL=0 on entry, on exit BL=1 (depending on r8).

ENTRY(restore_regs)
	mov.l	@r15+, r0
	mov.l	@r15+, r1
	mov.l	@r15+, r2
	mov.l	@r15+, r3
	mov.l	@r15+, r4
	mov.l	@r15+, r5
	mov.l	@r15+, r6
	mov.l	@r15+, r7
	!
	stc	sr, r9
	or	r8, r9
	ldc	r9, sr
	!
	mov.l	@r15+, r8
	mov.l	@r15+, r9
	mov.l	@r15+, r10
	mov.l	@r15+, r11
	mov.l	@r15+, r12
	mov.l	@r15+, r13
	mov.l	@r15+, r14
	mov.l	@r15+, k4		! original stack pointer
	ldc.l	@r15+, spc
	mov.l	@r15+, k2		! original PR
	mov.l	@r15+, k3		! original SR
	ldc.l	@r15+, gbr
	lds.l	@r15+, mach
	lds.l	@r15+, macl
	rts
	 add	#4, r15			! Skip syscall number

restore_all:
	mov.l	7f, r8
	bsr	restore_regs
	 nop

	lds	k2, pr			! restore pr
	!
	! Calculate new SR value
	mov	k3, k2			! original SR value
	mov	#0xf0, k1
	extu.b	k1, k1
	not	k1, k1
	and	k1, k2			! Mask original SR value
	!
	mov	k3, k0			! Calculate IMASK-bits
	shlr2	k0
	and	#0x3c, k0
	cmp/eq	#0x3c, k0
	bt/s	6f
	 shll2	k0
	mov	g_imask, k0
	!
6:	or	k0, k2			! Set the IMASK-bits
	ldc	k2, ssr
	!
#if defined(CONFIG_KGDB)
	! Clear in_nmi
	mov.l	6f, k0
	mov	#0, k1
	mov.b	k1, @k0
#endif
	mov	k4, r15
	rte
	 nop

	.align	2
5:	.long	0x00001000	! DSP
#ifdef CONFIG_KGDB
6:	.long	in_nmi
#endif
7:	.long	0x30000000

! common exception handler
#include "../../entry-common.S"
	
! Exception Vector Base
!
!	Should be aligned page boundary.
!
	.balign 	4096,0,4096
ENTRY(vbr_base)
	.long	0
!
! 0x100: General exception vector
!
	.balign 	256,0,256
general_exception:
#ifndef CONFIG_CPU_SUBTYPE_SHX3
	bra	handle_exception
	 sts	pr, k3		! save original pr value in k3
#else
	mov.l	1f, k4
	mov.l	@k4, k4

	! Is EXPEVT larger than 0x800?
	mov	#0x8, k0
	shll8	k0
	cmp/hs	k0, k4
	bf	0f

	! then add 0x580 (k2 is 0xd80 or 0xda0)
	mov	#0x58, k0
	shll2	k0
	shll2	k0
	add	k0, k4
0:
	! Setup stack and save DSP context (k0 contains original r15 on return)
	bsr	prepare_stack
	 nop

	! Save registers / Switch to bank 0
	mov		k4, k2		! keep vector in k2
	mov.l	1f, k4		! SR bits to clear in k4
	bsr	save_regs	! needs original pr value in k3
	 nop

	bra	handle_exception_special
	 nop

	.align	2
1:	.long	EXPEVT
#endif

! prepare_stack()
! - roll back gRB
! - switch to kernel stack
! k0 returns original sp (after roll back)
! k1 trashed
! k2 trashed

prepare_stack:
#ifdef CONFIG_GUSA
	! Check for roll back gRB (User and Kernel)
	mov	r15, k0
	shll	k0
	bf/s	1f
	 shll	k0
	bf/s	1f
	 stc	spc, k1
	stc	r0_bank, k0
	cmp/hs	k0, k1		! test k1 (saved PC) >= k0 (saved r0)
	bt/s	2f
	 stc	r1_bank, k1

	add	#-2, k0
	add	r15, k0
	ldc	k0, spc		! PC = saved r0 + r15 - 2
2:	mov	k1, r15		! SP = r1
1:
#endif
	! Switch to kernel stack if needed
	stc	ssr, k0		! Is it from kernel space?
	shll	k0		! Check MD bit (bit30) by shifting it into...
	shll	k0		!       ...the T bit
	bt/s	1f		! It's a kernel to kernel transition.
	 mov	r15, k0		! save original stack to k0
	/* User space to kernel */
	mov	#(THREAD_SIZE >> 10), k1
	shll8	k1		! k1 := THREAD_SIZE
	shll2	k1
	add	current, k1
	mov	k1, r15		! change to kernel stack
	!
1:
	rts
	 nop

!
! 0x400: Instruction and Data TLB miss exception vector
!
	.balign 	1024,0,1024
tlb_miss:
	sts	pr, k3		! save original pr value in k3

handle_exception:
	mova	exception_data, k0

	! Setup stack and save DSP context (k0 contains original r15 on return)
	bsr	prepare_stack
	 PREF(k0)

	! Save registers / Switch to bank 0
	mov.l	5f, k2		! vector register address
	mov.l	1f, k4		! SR bits to clear in k4
	bsr	save_regs	! needs original pr value in k3
	 mov.l	@k2, k2		! read out vector and keep in k2

handle_exception_special:
	! Setup return address and jump to exception handler
	mov.l	7f, r9		! fetch return address
	stc	r2_bank, r0	! k2 (vector)
	mov.l	6f, r10
	shlr2	r0
	shlr	r0
	mov.l	@(r0, r10), r10
	jmp	@r10
	 lds	r9, pr		! put return address in pr

	.align	L1_CACHE_SHIFT

! save_regs()
! - save default tra, macl, mach, gbr, ssr, pr* and spc on the stack
! - save r15*, r14, r13, r12, r11, r10, r9, r8 on the stack
! - switch bank
! - save r7, r6, r5, r4, r3, r2, r1, r0 on the stack
! k0 contains original stack pointer*
! k1 trashed
! k3 passes original pr*
! k4 passes SR bitmask
! BL=1 on entry, on exit BL=0.

ENTRY(save_regs)
	mov	#-1, r1
	mov.l	k1, @-r15	! set TRA (default: -1)
	sts.l	macl, @-r15
	sts.l	mach, @-r15
	stc.l	gbr, @-r15
	stc.l	ssr, @-r15
	mov.l	k3, @-r15	! original pr in k3
	stc.l	spc, @-r15

	mov.l	k0, @-r15	! original stack pointer in k0
	mov.l	r14, @-r15
	mov.l	r13, @-r15
	mov.l	r12, @-r15
	mov.l	r11, @-r15
	mov.l	r10, @-r15
	mov.l	r9, @-r15
	mov.l	r8, @-r15

	mov.l	0f, k3		! SR bits to set in k3

	! fall-through

! save_low_regs()
! - modify SR for bank switch
! - save r7, r6, r5, r4, r3, r2, r1, r0 on the stack
! k3 passes bits to set in SR
! k4 passes bits to clear in SR

ENTRY(save_low_regs)
	stc	sr, r8
	or	k3, r8
	and	k4, r8
	ldc	r8, sr

	mov.l	r7, @-r15
	mov.l	r6, @-r15
	mov.l	r5, @-r15
	mov.l	r4, @-r15
	mov.l	r3, @-r15
	mov.l	r2, @-r15
	mov.l	r1, @-r15
	rts
	 mov.l	r0, @-r15

!
! 0x600: Interrupt / NMI vector
!
	.balign 	512,0,512
ENTRY(handle_interrupt)
#if defined(CONFIG_KGDB)
	mov.l	2f, k2
	! Debounce (filter nested NMI)
	mov.l	@k2, k0
	mov.l	9f, k1
	cmp/eq	k1, k0
	bf	11f
	mov.l	10f, k1
	tas.b	@k1
	bt	11f
	rte
	 nop
	.align	2
9:	.long	NMI_VEC
10:	.long	in_nmi
11:
#endif /* defined(CONFIG_KGDB) */
	sts	pr, k3		! save original pr value in k3
	mova	exception_data, k0

	! Setup stack and save DSP context (k0 contains original r15 on return)
	bsr	prepare_stack
	 PREF(k0)

	! Save registers / Switch to bank 0
	mov.l	1f, k4		! SR bits to clear in k4
	bsr	save_regs	! needs original pr value in k3
	 mov	#-1, k2		! default vector kept in k2

	! Setup return address and jump to do_IRQ
	mov.l	4f, r9		! fetch return address
	lds	r9, pr		! put return address in pr
	mov.l	2f, r4
	mov.l	3f, r9
	mov.l	@r4, r4		! pass INTEVT vector as arg0
	jmp	@r9
	 mov	r15, r5		! pass saved registers as arg1

ENTRY(exception_none)
	rts
	 nop

	.align	L1_CACHE_SHIFT
exception_data:
0:	.long	0x000080f0	! FD=1, IMASK=15
1:	.long	0xcfffffff	! RB=0, BL=0
2:	.long	INTEVT
3:	.long	do_IRQ
4:	.long	ret_from_irq
5:	.long	EXPEVT
6:	.long	exception_handling_table
7:	.long	ret_from_exception
