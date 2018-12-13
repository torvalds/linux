/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 - 2000, 2001 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 * Copyright (C) 2004 Thiemo Seufer
 *
 * Hairy, the userspace application uses a different argument passing
 * convention than the kernel, so we have to translate things from o32
 * to ABI64 calling convention.	 64-bit syscalls are also processed
 * here for now.
 */
#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/irqflags.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>
#include <asm/sysmips.h>

	.align	5
NESTED(handle_sys, PT_SIZE, sp)
	.set	noat
	SAVE_SOME
	TRACE_IRQS_ON_RELOAD
	STI
	.set	at
	ld	t1, PT_EPC(sp)		# skip syscall on return

	dsubu	t0, v0, __NR_O32_Linux	# check syscall number
	sltiu	t0, t0, __NR_O32_Linux_syscalls
	daddiu	t1, 4			# skip to next instruction
	sd	t1, PT_EPC(sp)
	beqz	t0, not_o32_scall
#if 0
 SAVE_ALL
 move a1, v0
 PRINT("Scall %ld\n")
 RESTORE_ALL
#endif

	/* We don't want to stumble over broken sign extensions from
	   userland. O32 does never use the upper half. */
	sll	a0, a0, 0
	sll	a1, a1, 0
	sll	a2, a2, 0
	sll	a3, a3, 0

	sd	a3, PT_R26(sp)		# save a3 for syscall restarting

	/*
	 * More than four arguments.  Try to deal with it by copying the
	 * stack arguments from the user stack to the kernel stack.
	 * This Sucks (TM).
	 *
	 * We intentionally keep the kernel stack a little below the top of
	 * userspace so we don't have to do a slower byte accurate check here.
	 */
	ld	t0, PT_R29(sp)		# get old user stack pointer
	daddu	t1, t0, 32
	bltz	t1, bad_stack

load_a4: lw	a4, 16(t0)		# argument #5 from usp
load_a5: lw	a5, 20(t0)		# argument #6 from usp
load_a6: lw	a6, 24(t0)		# argument #7 from usp
load_a7: lw	a7, 28(t0)		# argument #8 from usp
loads_done:

	.section __ex_table,"a"
	PTR	load_a4, bad_stack_a4
	PTR	load_a5, bad_stack_a5
	PTR	load_a6, bad_stack_a6
	PTR	load_a7, bad_stack_a7
	.previous

	li	t1, _TIF_WORK_SYSCALL_ENTRY
	LONG_L	t0, TI_FLAGS($28)	# syscall tracing enabled?
	and	t0, t1, t0
	bnez	t0, trace_a_syscall

syscall_common:
	dsll	t0, v0, 3		# offset into table
	ld	t2, (sys32_call_table - (__NR_O32_Linux * 8))(t0)

	jalr	t2			# Do The Real Thing (TM)

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sd	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	ld	t1, PT_R2(sp)		# syscall number
	dnegu	v0			# error
	sd	t1, PT_R0(sp)		# save it for syscall restarting
1:	sd	v0, PT_R2(sp)		# result

o32_syscall_exit:
	j	syscall_exit_partial

/* ------------------------------------------------------------------------ */

trace_a_syscall:
	SAVE_STATIC
	sd	a4, PT_R8(sp)		# Save argument registers
	sd	a5, PT_R9(sp)
	sd	a6, PT_R10(sp)
	sd	a7, PT_R11(sp)		# For indirect syscalls

	move	a0, sp
	/*
	 * absolute syscall number is in v0 unless we called syscall(__NR_###)
	 * where the real syscall number is in a0
	 * note: NR_syscall is the first O32 syscall but the macro is
	 * only defined when compiling with -mabi=32 (CONFIG_32BIT)
	 * therefore __NR_O32_Linux is used (4000)
	 */
	.set	push
	.set	reorder
	subu	t1, v0,  __NR_O32_Linux
	move	a1, v0
	bnez	t1, 1f /* __NR_syscall at offset 0 */
	lw	a1, PT_R4(sp) /* Arg1 for __NR_syscall case */
	.set	pop

1:	jal	syscall_trace_enter

	bltz	v0, 1f			# seccomp failed? Skip syscall

	RESTORE_STATIC
	ld	v0, PT_R2(sp)		# Restore syscall (maybe modified)
	ld	a0, PT_R4(sp)		# Restore argument registers
	ld	a1, PT_R5(sp)
	ld	a2, PT_R6(sp)
	ld	a3, PT_R7(sp)
	ld	a4, PT_R8(sp)
	ld	a5, PT_R9(sp)
	ld	a6, PT_R10(sp)
	ld	a7, PT_R11(sp)		# For indirect syscalls

	dsubu	t0, v0, __NR_O32_Linux	# check (new) syscall number
	sltiu	t0, t0, __NR_O32_Linux_syscalls
	beqz	t0, not_o32_scall

	j	syscall_common

1:	j	syscall_exit

/* ------------------------------------------------------------------------ */

	/*
	 * The stackpointer for a call with more than 4 arguments is bad.
	 */
bad_stack:
	li	v0, EFAULT
	sd	v0, PT_R2(sp)
	li	t0, 1			# set error flag
	sd	t0, PT_R7(sp)
	j	o32_syscall_exit

bad_stack_a4:
	li	a4, 0
	b	load_a5

bad_stack_a5:
	li	a5, 0
	b	load_a6

bad_stack_a6:
	li	a6, 0
	b	load_a7

bad_stack_a7:
	li	a7, 0
	b	loads_done

not_o32_scall:
	/*
	 * This is not an o32 compatibility syscall, pass it on
	 * to the 64-bit syscall handlers.
	 */
#ifdef CONFIG_MIPS32_N32
	j	handle_sysn32
#else
	j	handle_sys64
#endif
	END(handle_sys)

LEAF(sys32_syscall)
	subu	t0, a0, __NR_O32_Linux	# check syscall number
	sltiu	v0, t0, __NR_O32_Linux_syscalls
	beqz	t0, einval		# do not recurse
	dsll	t1, t0, 3
	beqz	v0, einval
	ld	t2, sys32_call_table(t1)		# syscall routine

	move	a0, a1			# shift argument registers
	move	a1, a2
	move	a2, a3
	move	a3, a4
	move	a4, a5
	move	a5, a6
	move	a6, a7
	jr	t2
	/* Unreached */

einval: li	v0, -ENOSYS
	jr	ra
	END(sys32_syscall)

#define __SYSCALL(nr, entry, nargs) PTR entry
	.align	3
	.type	sys32_call_table,@object
EXPORT(sys32_call_table)
#include <asm/syscall_table_64_o32.h>
#undef __SYSCALL
