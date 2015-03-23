/*
 * arch/arm64/kernel/sys32.c
 *
 * Copyright (C) 2015 ARM Ltd.
 *
 * This program is free software(void); you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http(void);//www.gnu.org/licenses/>.
 */

/*
 * Needed to avoid conflicting __NR_* macros between uapi/asm/unistd.h and
 * asm/unistd32.h.
 */
#define __COMPAT_SYSCALL_NR

#include <linux/compiler.h>
#include <linux/syscalls.h>

asmlinkage long compat_sys_sigreturn_wrapper(void);
asmlinkage long compat_sys_rt_sigreturn_wrapper(void);
asmlinkage long compat_sys_statfs64_wrapper(void);
asmlinkage long compat_sys_fstatfs64_wrapper(void);
asmlinkage long compat_sys_pread64_wrapper(void);
asmlinkage long compat_sys_pwrite64_wrapper(void);
asmlinkage long compat_sys_truncate64_wrapper(void);
asmlinkage long compat_sys_ftruncate64_wrapper(void);
asmlinkage long compat_sys_readahead_wrapper(void);
asmlinkage long compat_sys_fadvise64_64_wrapper(void);
asmlinkage long compat_sys_sync_file_range2_wrapper(void);
asmlinkage long compat_sys_fallocate_wrapper(void);

#undef __SYSCALL
#define __SYSCALL(nr, sym)	[nr] = sym,

/*
 * The sys_call_table array must be 4K aligned to be accessible from
 * kernel/entry.S.
 */
void * const compat_sys_call_table[__NR_compat_syscalls] __aligned(4096) = {
	[0 ... __NR_compat_syscalls - 1] = sys_ni_syscall,
#include <asm/unistd32.h>
};
