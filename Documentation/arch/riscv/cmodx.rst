.. SPDX-License-Identifier: GPL-2.0

==============================================================================
Concurrent Modification and Execution of Instructions (CMODX) for RISC-V Linux
==============================================================================

CMODX is a programming technique where a program executes instructions that were
modified by the program itself. Instruction storage and the instruction cache
(icache) are not guaranteed to be synchronized on RISC-V hardware. Therefore, the
program must enforce its own synchronization with the unprivileged fence.i
instruction.

However, the default Linux ABI prohibits the use of fence.i in userspace
applications. At any point the scheduler may migrate a task onto a new hart. If
migration occurs after the userspace synchronized the icache and instruction
storage with fence.i, the icache on the new hart will no longer be clean. This
is due to the behavior of fence.i only affecting the hart that it is called on.
Thus, the hart that the task has been migrated to may not have synchronized
instruction storage and icache.

There are two ways to solve this problem: use the riscv_flush_icache() syscall,
or use the ``PR_RISCV_SET_ICACHE_FLUSH_CTX`` prctl() and emit fence.i in
userspace. The syscall performs a one-off icache flushing operation. The prctl
changes the Linux ABI to allow userspace to emit icache flushing operations.

As an aside, "deferred" icache flushes can sometimes be triggered in the kernel.
At the time of writing, this only occurs during the riscv_flush_icache() syscall
and when the kernel uses copy_to_user_page(). These deferred flushes happen only
when the memory map being used by a hart changes. If the prctl() context caused
an icache flush, this deferred icache flush will be skipped as it is redundant.
Therefore, there will be no additional flush when using the riscv_flush_icache()
syscall inside of the prctl() context.

prctl() Interface
---------------------

Call prctl() with ``PR_RISCV_SET_ICACHE_FLUSH_CTX`` as the first argument. The
remaining arguments will be delegated to the riscv_set_icache_flush_ctx
function detailed below.

.. kernel-doc:: arch/riscv/mm/cacheflush.c
	:identifiers: riscv_set_icache_flush_ctx

Example usage:

The following files are meant to be compiled and linked with each other. The
modify_instruction() function replaces an add with 0 with an add with one,
causing the instruction sequence in get_value() to change from returning a zero
to returning a one.

cmodx.c::

	#include <stdio.h>
	#include <sys/prctl.h>

	extern int get_value();
	extern void modify_instruction();

	int main()
	{
		int value = get_value();
		printf("Value before cmodx: %d\n", value);

		// Call prctl before first fence.i is called inside modify_instruction
		prctl(PR_RISCV_SET_ICACHE_FLUSH_CTX_ON, PR_RISCV_CTX_SW_FENCEI, PR_RISCV_SCOPE_PER_PROCESS);
		modify_instruction();
		// Call prctl after final fence.i is called in process
		prctl(PR_RISCV_SET_ICACHE_FLUSH_CTX_OFF, PR_RISCV_CTX_SW_FENCEI, PR_RISCV_SCOPE_PER_PROCESS);

		value = get_value();
		printf("Value after cmodx: %d\n", value);
		return 0;
	}

cmodx.S::

	.option norvc

	.text
	.global modify_instruction
	modify_instruction:
	lw a0, new_insn
	lui a5,%hi(old_insn)
	sw  a0,%lo(old_insn)(a5)
	fence.i
	ret

	.section modifiable, "awx"
	.global get_value
	get_value:
	li a0, 0
	old_insn:
	addi a0, a0, 0
	ret

	.data
	new_insn:
	addi a0, a0, 1
