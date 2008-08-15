/*
 * arch/sh/kernel/cpu/sh2a/entry.S
 *
 * The SH-2A exception entry
 *
 * Copyright (C) 2008 Yoshinori Sato
 * Based on arch/sh/kernel/cpu/sh2/entry.S
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <cpu/mmu_context.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/page.h>
	
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
OFF_SR	=  (16*4+2*4)
OFF_TRA	=  (16*4+6*4)

#include <asm/entry-macros.S>

ENTRY(exception_handler)
	! stack
	! r0 <- point sp
	! r1
	! pc
	! sr
	! r0 = temporary
	! r1 = vector (pseudo EXPEVT / INTEVT / TRA)
	mov.l	r2,@-sp
	cli
	mov.l	$cpu_mode,r2
	bld.b	#6,@(0,r2)	!previus SR.MD
	bst.b	#6,@(4*4,r15)	!set cpu mode to SR.MD
	bt	1f
	! switch to kernel mode
	bset.b	#6,@(0,r2)	!set SR.MD
	mov.l	$current_thread_info,r2
	mov.l	@r2,r2
	mov	#(THREAD_SIZE >> 8),r0
	shll8	r0
	add	r2,r0		! r0 = kernel stack tail
	mov	r15,r2		! r2 = user stack top
	mov	r0,r15		! switch kernel stack
	mov.l	r1,@-r15	! TRA
	sts.l	macl, @-r15
	sts.l	mach, @-r15
	stc.l	gbr, @-r15
	mov.l	@(4*4,r2),r0
	mov.l	r0,@-r15	! original SR
	sts.l	pr,@-r15
	mov.l	@(3*4,r2),r0
	mov.l	r0,@-r15	! original PC
	mov	r2,r0
	add	#(3+2)*4,r0	! rewind r0 - r3 + exception frame
	lds	r0,pr		! pr = original SP
	movmu.l	r3,@-r15	! save regs
	mov	r2,r8		! r8 =  previus stack top
	mov	r1,r9		! r9 = interrupt vector
	! restore previous stack
	mov.l	@r8+,r2
	mov.l	@r8+,r0
	mov.l	@r8+,r1
	bra	2f
	 movml.l r2,@-r15
1:
	! in kernel exception
	mov	r15,r2
	add	#-((OFF_TRA + 4) - OFF_PC) + 5*4,r15
	movmu.l	r3,@-r15
	mov	r2,r8		! r8 = previous stack top
	mov	r1,r9		! r9 = interrupt vector
	! restore exception frame & regs
	mov.l	@r8+,r2		! old R2
	mov.l	@r8+,r0		! old R0
	mov.l	@r8+,r1		! old R1
	mov.l	@r8+,r10	! old PC
	mov.l	@r8+,r11	! old SR
	movml.l	r2,@-r15
	mov.l	r10,@(OFF_PC,r15)
	mov.l	r11,@(OFF_SR,r15)
	mov.l	r8,@(OFF_SP,r15)	! save old sp
	mov	r15,r8
	add	#OFF_TRA + 4,r8
	mov.l	r9,@-r8
	sts.l	macl,@-r8
	sts.l	mach,@-r8
	stc.l	gbr,@-r8
	add	#-4,r8
	sts.l	pr,@-r8
2:
	! dispatch exception / interrupt
	mov	#64,r8
	cmp/hs	r8,r9
	bt	interrupt_entry	! vec >= 64 is interrupt
	mov	#32,r8
	cmp/hs	r8,r9
	bt	trap_entry	! 64 > vec >= 32  is trap

	mov.l	4f,r8
	mov	r9,r4
	shll2	r9
	add	r9,r8
	mov.l	@r8,r8		! exception handler address
	tst	r8,r8
	bf	3f
	mov.l	8f,r8		! unhandled exception
3:
	mov.l	5f,r10
	jmp	@r8
	 lds	r10,pr

interrupt_entry:
	mov	r9,r4
	mov	r15,r5
	mov.l	7f,r8
	mov.l	6f,r9
	jmp	@r8
	 lds	r9,pr

	.align	2
4:	.long	exception_handling_table
5:	.long	ret_from_exception
6:	.long	ret_from_irq
7:	.long	do_IRQ
8:	.long	exception_error

trap_entry:
	mov	#0x30,r8
	cmp/ge	r8,r9		! vector 0x20-0x2f is systemcall
	bt	1f
	add	#-0x10,r9	! convert SH2 to SH3/4 ABI
1:	
	shll2	r9			! TRA
	bra	system_call	! jump common systemcall entry
	 mov	r9,r8
	
#if defined(CONFIG_SH_STANDARD_BIOS)
	/* Unwind the stack and jmp to the debug entry */
ENTRY(sh_bios_handler)
	mov	r15,r0
	add	#(22-4)*4-4,r0
	ldc.l	@r0+,gbr
	lds.l	@r0+,mach
	lds.l	@r0+,macl
	mov	r15,r0
	mov.l	@(OFF_SP,r0),r1
	mov.l	@(OFF_SR,r2),r3
	mov.l	r3,@-r1
	mov.l	@(OFF_SP,r2),r3
	mov.l	r3,@-r1
	mov	r15,r0
	add	#(22-4)*4-8,r0
	mov.l	1f,r2
	mov.l	@r2,r2
	stc	sr,r3
	mov.l	r2,@r0
	mov.l	r3,@(4,r0)
	mov.l	r1,@(8,r0)
	movml.l	@r15+,r14
	add	#8,r15
	lds.l	@r15+, pr
	rte
	 mov.l	@r15+,r15
	.align	2
1:	.long	gdb_vbr_vector
#endif /* CONFIG_SH_STANDARD_BIOS */

ENTRY(address_error_trap_handler)
	mov	r15,r4				! regs
	mov.l	@(OFF_PC,r15),r6		! pc
	mov.l	1f,r0
	jmp	@r0
	 mov	#0,r5				! writeaccess is unknown

	.align	2
1:	.long	do_address_error

restore_all:
	stc	sr,r0
	or	#0xf0,r0
	ldc	r0,sr				! all interrupt block (same BL = 1)
	! restore special register
	! overlap exception frame
	mov	r15,r0
	add	#17*4,r0
	lds.l	@r0+,pr
	add	#4,r0
	ldc.l	@r0+,gbr
	lds.l	@r0+,mach
	lds.l	@r0+,macl
	mov	r15,r0
	mov.l	$cpu_mode,r2
	bld.b	#6,@(OFF_SR,r15)
	bst.b	#6,@(0,r2)			! save CPU mode
	mov.l	@(OFF_SR,r0),r1
	shll2	r1
	shlr2	r1				! clear MD bit
	mov.l	@(OFF_SP,r0),r2
	add	#-8,r2
	mov.l	r2,@(OFF_SP,r0)			! point exception frame top
	mov.l	r1,@(4,r2)			! set sr
	mov.l	@(OFF_PC,r0),r1
	mov.l	r1,@r2				! set pc
	get_current_thread_info r0, r1
	mov.l	$current_thread_info,r1
	mov.l	r0,@r1
	movml.l	@r15+,r14
	mov.l	@r15,r15
	rte
	 nop

	.align 2
$current_thread_info:
	.long	__current_thread_info
$cpu_mode:	
	.long	__cpu_mode
		
! common exception handler
#include "../../entry-common.S"
	
	.data
! cpu operation mode 
! bit30 = MD (compatible SH3/4)
__cpu_mode:
	.long	0x40000000
		
	.section	.bss
__current_thread_info:
	.long	0

ENTRY(exception_handling_table)
	.space	4*32
