/*
 * arch/sh/kernel/cpu/sh2/entry.S
 *
 * The SH-2 exception entry
 *
 * Copyright (C) 2005-2008 Yoshinori Sato
 * Copyright (C) 2005  AXE,Inc.
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
	mov.l	r3,@-sp
	cli
	mov.l	$cpu_mode,r2
#ifdef CONFIG_SMP
	mov.l	$cpuid,r3
	mov.l	@r3,r3
	mov.l	@r3,r3
	shll2	r3
	add	r3,r2
#endif
	mov.l	@r2,r0
	mov.l	@(5*4,r15),r3	! previous SR
	or	r0,r3		! set MD
	tst	r0,r0
	bf/s	1f		! previous mode check
	 mov.l	r3,@(5*4,r15)	! update SR
	! switch to kernel mode
	mov.l	__md_bit,r0
	mov.l	r0,@r2		! enter kernel mode
	mov.l	$current_thread_info,r2
#ifdef CONFIG_SMP
	mov.l	$cpuid,r0
	mov.l	@r0,r0
	mov.l	@r0,r0
	shll2	r0
	add	r0,r2
#endif
	mov.l	@r2,r2
	mov	#(THREAD_SIZE >> 8),r0
	shll8	r0
	add	r2,r0
	mov	r15,r2		! r2 = user stack top
	mov	r0,r15		! switch kernel stack
	mov.l	r1,@-r15	! TRA
	sts.l	macl, @-r15
	sts.l	mach, @-r15
	stc.l	gbr, @-r15
	mov.l	@(5*4,r2),r0
	mov.l	r0,@-r15	! original SR
	sts.l	pr,@-r15
	mov.l	@(4*4,r2),r0
	mov.l	r0,@-r15	! original PC
	mov	r2,r3
	add	#(4+2)*4,r3	! rewind r0 - r3 + exception frame
	mov.l	r3,@-r15	! original SP
	mov.l	r14,@-r15
	mov.l	r13,@-r15
	mov.l	r12,@-r15
	mov.l	r11,@-r15
	mov.l	r10,@-r15
	mov.l	r9,@-r15
	mov.l	r8,@-r15
	mov.l	r7,@-r15
	mov.l	r6,@-r15
	mov.l	r5,@-r15
	mov.l	r4,@-r15
	mov	r1,r9		! save TRA
	mov	r2,r8		! copy user -> kernel stack
	mov.l	@(0,r8),r3
	mov.l	r3,@-r15
	mov.l	@(4,r8),r2
	mov.l	r2,@-r15
	mov.l	@(12,r8),r1
	mov.l	r1,@-r15
	mov.l	@(8,r8),r0
	bra	2f
	 mov.l	r0,@-r15
1:
	! in kernel exception
	mov	#(22-4-4-1)*4+4,r0
	mov	r15,r2
	sub	r0,r15
	mov.l	@r2+,r0		! old R3
	mov.l	r0,@-r15	
	mov.l	@r2+,r0		! old R2
	mov.l	r0,@-r15	
	mov.l	@(4,r2),r0	! old R1
	mov.l	r0,@-r15	
	mov.l	@r2,r0		! old R0
	mov.l	r0,@-r15
	add	#8,r2
	mov.l	@r2+,r3		! old PC
	mov.l	@r2+,r0		! old SR
	add	#-4,r2		! exception frame stub (sr)
	mov.l	r1,@-r2		! TRA
	sts.l	macl, @-r2
	sts.l	mach, @-r2
	stc.l	gbr, @-r2
	mov.l	r0,@-r2		! save old SR
	sts.l	pr,@-r2
	mov.l	r3,@-r2		! save old PC
	mov	r2,r0
	add	#8*4,r0
	mov.l	r0,@-r2		! save old SP
	mov.l	r14,@-r2
	mov.l	r13,@-r2
	mov.l	r12,@-r2
	mov.l	r11,@-r2
	mov.l	r10,@-r2
	mov.l	r9,@-r2
	mov.l	r8,@-r2
	mov.l	r7,@-r2
	mov.l	r6,@-r2
	mov.l	r5,@-r2
	mov.l	r4,@-r2
	mov	r1,r9
	mov.l	@(OFF_R0,r15),r0
	mov.l	@(OFF_R1,r15),r1
	mov.l	@(OFF_R2,r15),r2
	mov.l	@(OFF_R3,r15),r3
2:
	mov	#64,r8
	cmp/hs	r8,r9
	bt	interrupt_entry	! vec >= 64 is interrupt
	mov	#31,r8
	cmp/hs	r8,r9
	bt	trap_entry	! 64 > vec >= 31  is trap
#ifdef CONFIG_CPU_J2
	mov	#16,r8
	cmp/hs	r8,r9
	bt	interrupt_entry	! 31 > vec >= 16 is interrupt
#endif

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
	mov.l	6f,r9
	mov.l	7f,r8
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
	cmp/ge	r8,r9		! vector 0x1f-0x2f is systemcall
	bt	1f
	mov     #0x1f,r9	! convert to unified SH2/3/4 trap number
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
	mov	#OFF_SR,r2
	mov.l	@(r0,r2),r3
	mov.l	r3,@-r1
	mov	#OFF_SP,r2
	mov.l	@(r0,r2),r3
	mov.l	r3,@-r1
	mov	r15,r0
	add	#(22-4)*4-8,r0
	mov.l	1f,r2
	mov.l	@r2,r2
	stc	sr,r3
	mov.l	r2,@r0
	mov.l	r3,@(4,r0)
	mov.l	r1,@(8,r0)	
	mov.l	@r15+, r0
	mov.l	@r15+, r1
	mov.l	@r15+, r2
	mov.l	@r15+, r3
	mov.l	@r15+, r4
	mov.l	@r15+, r5
	mov.l	@r15+, r6
	mov.l	@r15+, r7
	mov.l	@r15+, r8
	mov.l	@r15+, r9
	mov.l	@r15+, r10
	mov.l	@r15+, r11
	mov.l	@r15+, r12
	mov.l	@r15+, r13
	mov.l	@r15+, r14
	add	#8,r15
	lds.l	@r15+, pr
	mov.l	@r15+,r15
	rte
	 nop
	.align	2
1:	.long	gdb_vbr_vector
#endif /* CONFIG_SH_STANDARD_BIOS */

ENTRY(address_error_trap_handler)
	mov	r15,r4				! regs
	mov	#OFF_PC,r0
	mov.l	@(r0,r15),r6			! pc
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
#ifdef CONFIG_SMP
	mov.l	$cpuid,r3
	mov.l	@r3,r3
	mov.l	@r3,r3
	shll2	r3
	add	r3,r2
#endif
	mov	#OFF_SR,r3
	mov.l	@(r0,r3),r1
	mov.l	__md_bit,r3
	and	r1,r3				! copy MD bit
	mov.l	r3,@r2
	shll2	r1				! clear MD bit
	shlr2	r1
	mov.l	@(OFF_SP,r0),r2
	add	#-8,r2
	mov.l	r2,@(OFF_SP,r0)			! point exception frame top
	mov.l	r1,@(4,r2)			! set sr
	mov	#OFF_PC,r3
	mov.l	@(r0,r3),r1
	mov.l	r1,@r2				! set pc
	get_current_thread_info r0, r1
	mov.l	$current_thread_info,r1
#ifdef CONFIG_SMP
	mov.l	$cpuid,r3
	mov.l	@r3,r3
	mov.l	@r3,r3
	shll2	r3
	add	r3,r1
#endif
	mov.l	r0,@r1
	mov.l	@r15+,r0
	mov.l	@r15+,r1
	mov.l	@r15+,r2
	mov.l	@r15+,r3
	mov.l	@r15+,r4
	mov.l	@r15+,r5
	mov.l	@r15+,r6
	mov.l	@r15+,r7
	mov.l	@r15+,r8
	mov.l	@r15+,r9
	mov.l	@r15+,r10
	mov.l	@r15+,r11
	mov.l	@r15+,r12
	mov.l	@r15+,r13
	mov.l	@r15+,r14
	mov.l	@r15,r15
	rte
	 nop

	.align 2
__md_bit:
	.long	0x40000000
$current_thread_info:
	.long	__current_thread_info
$cpu_mode:	
	.long	__cpu_mode
#ifdef CONFIG_SMP
$cpuid:
	.long sh2_cpuid_addr
#endif
		
! common exception handler
#include "../../entry-common.S"

#ifdef CONFIG_NR_CPUS
#define NR_CPUS CONFIG_NR_CPUS
#else
#define NR_CPUS 1
#endif
	
	.data
! cpu operation mode 
! bit30 = MD (compatible SH3/4)
__cpu_mode:
	.rept	NR_CPUS
	.long	0x40000000
	.endr

#ifdef CONFIG_SMP
.global sh2_cpuid_addr
sh2_cpuid_addr:
	.long	dummy_cpuid
dummy_cpuid:
	.long	0
#endif
		
	.section	.bss
__current_thread_info:
	.rept	NR_CPUS
	.long	0
	.endr

ENTRY(exception_handling_table)
	.space	4*32
