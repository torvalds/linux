/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2000, 01 by Ralf Baechle
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
#include <asm/thread_info.h>
#include <asm/unistd.h>

#ifndef CONFIG_MIPS32_O32
/* No O32, so define handle_sys here */
#define handle_sysn32 handle_sys
#endif

	.align	5
NESTED(handle_sysn32, PT_SIZE, sp)
#ifndef CONFIG_MIPS32_O32
	.set	noat
	SAVE_SOME
	TRACE_IRQS_ON_RELOAD
	STI
	.set	at
#endif

	dsubu	t0, v0, __NR_N32_Linux	# check syscall number
	sltiu	t0, t0, __NR_N32_Linux_syscalls

#ifndef CONFIG_MIPS32_O32
	ld	t1, PT_EPC(sp)		# skip syscall on return
	daddiu	t1, 4			# skip to next instruction
	sd	t1, PT_EPC(sp)
#endif
	beqz	t0, not_n32_scall

	sd	a3, PT_R26(sp)		# save a3 for syscall restarting

	li	t1, _TIF_WORK_SYSCALL_ENTRY
	LONG_L	t0, TI_FLAGS($28)	# syscall tracing enabled?
	and	t0, t1, t0
	bnez	t0, n32_syscall_trace_entry

syscall_common:
	dsll	t0, v0, 3		# offset into table
	ld	t2, (sysn32_call_table - (__NR_N32_Linux * 8))(t0)

	jalr	t2			# Do The Real Thing (TM)

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sd	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	ld	t1, PT_R2(sp)		# syscall number
	dnegu	v0			# error
	sd	t1, PT_R0(sp)		# save it for syscall restarting
1:	sd	v0, PT_R2(sp)		# result

	j	syscall_exit_partial

/* ------------------------------------------------------------------------ */

n32_syscall_trace_entry:
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

	dsubu	t2, v0, __NR_N32_Linux	# check (new) syscall number
	sltiu   t0, t2, __NR_N32_Linux_syscalls
	beqz	t0, not_n32_scall

	j	syscall_common

1:	j	syscall_exit

not_n32_scall:
	/* This is not an n32 compatibility syscall, pass it on to
	   the n64 syscall handlers.  */
	j	handle_sys64

	END(handle_sysn32)

#define __SYSCALL(nr, entry)	PTR entry
	.type	sysn32_call_table, @object
EXPORT(sysn32_call_table)
#include <asm/syscall_table_64_n32.h>
#undef __SYSCALL
