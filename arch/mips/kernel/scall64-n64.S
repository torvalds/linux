/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2000, 01, 02 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/irqflags.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/asm-offsets.h>
#include <asm/sysmips.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>
#include <asm/war.h>

#ifndef CONFIG_MIPS32_COMPAT
/* Neither O32 nor N32, so define handle_sys here */
#define handle_sys64 handle_sys
#endif

	.align	5
NESTED(handle_sys64, PT_SIZE, sp)
#if !defined(CONFIG_MIPS32_O32) && !defined(CONFIG_MIPS32_N32)
	/*
	 * When 32-bit compatibility is configured scall_o32.S
	 * already did this.
	 */
	.set	noat
	SAVE_SOME
	TRACE_IRQS_ON_RELOAD
	STI
	.set	at
#endif

#if !defined(CONFIG_MIPS32_O32) && !defined(CONFIG_MIPS32_N32)
	ld	t1, PT_EPC(sp)		# skip syscall on return
	daddiu	t1, 4			# skip to next instruction
	sd	t1, PT_EPC(sp)
#endif

	sd	a3, PT_R26(sp)		# save a3 for syscall restarting

	li	t1, _TIF_WORK_SYSCALL_ENTRY
	LONG_L	t0, TI_FLAGS($28)	# syscall tracing enabled?
	and	t0, t1, t0
	bnez	t0, syscall_trace_entry

syscall_common:
	dsubu	t2, v0, __NR_64_Linux
	sltiu   t0, t2, __NR_64_Linux_syscalls
	beqz	t0, illegal_syscall

	dsll	t0, t2, 3		# offset into table
	dla	t2, sys_call_table
	daddu	t0, t2, t0
	ld	t2, (t0)		# syscall routine
	beqz	t2, illegal_syscall

	jalr	t2			# Do The Real Thing (TM)

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sd	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	ld	t1, PT_R2(sp)		# syscall number
	dnegu	v0			# error
	sd	t1, PT_R0(sp)		# save it for syscall restarting
1:	sd	v0, PT_R2(sp)		# result

n64_syscall_exit:
	j	syscall_exit_partial

/* ------------------------------------------------------------------------ */

syscall_trace_entry:
	SAVE_STATIC
	move	a0, sp
	move	a1, v0
	jal	syscall_trace_enter

	bltz	v0, 1f			# seccomp failed? Skip syscall

	RESTORE_STATIC
	ld	v0, PT_R2(sp)		# Restore syscall (maybe modified)
	ld	a0, PT_R4(sp)		# Restore argument registers
	ld	a1, PT_R5(sp)
	ld	a2, PT_R6(sp)
	ld	a3, PT_R7(sp)
	ld	a4, PT_R8(sp)
	ld	a5, PT_R9(sp)
	j	syscall_common

1:	j	syscall_exit

illegal_syscall:
	/* This also isn't a 64-bit syscall, throw an error.  */
	li	v0, ENOSYS			# error
	sd	v0, PT_R2(sp)
	li	t0, 1				# set error flag
	sd	t0, PT_R7(sp)
	j	n64_syscall_exit
	END(handle_sys64)

#define __SYSCALL(nr, entry)	PTR_WD entry
	.align	3
	.type	sys_call_table, @object
EXPORT(sys_call_table)
#include <asm/syscall_table_n64.h>
