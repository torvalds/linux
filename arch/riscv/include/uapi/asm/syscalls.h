/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017 SiFive
 */

#ifndef _ASM__UAPI__SYSCALLS_H
#define _ASM__UAPI__SYSCALLS_H

/*
 * Allows the instruction cache to be flushed from userspace.  Despite RISC-V
 * having a direct 'fence.i' instruction available to userspace (which we
 * can't trap!), that's not actually viable when running on Linux because the
 * kernel might schedule a process on another hart.  There is no way for
 * userspace to handle this without invoking the kernel (as it doesn't know the
 * thread->hart mappings), so we've defined a RISC-V specific system call to
 * flush the instruction cache.
 *
 * __NR_riscv_flush_icache is defined to flush the instruction cache over an
 * address range, with the flush applying to either all threads or just the
 * caller.  We don't currently do anything with the address range, that's just
 * in there for forwards compatibility.
 */
#define __NR_riscv_flush_icache (__NR_arch_specific_syscall + 15)
__SYSCALL(__NR_riscv_flush_icache, sys_riscv_flush_icache)

#endif
