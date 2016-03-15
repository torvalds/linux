/* System call table for i386. */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <linux/cache.h>
#include <asm/asm-offsets.h>
#include <asm/syscall.h>

#define __SYSCALL_I386(nr, sym, qual) extern asmlinkage long sym(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long) ;
#include <asm/syscalls_32.h>
#undef __SYSCALL_I386

#define __SYSCALL_I386(nr, sym, qual) [nr] = sym,

extern asmlinkage long sys_ni_syscall(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);

__visible const sys_call_ptr_t ia32_sys_call_table[__NR_syscall_compat_max+1] = {
	/*
	 * Smells like a compiler bug -- it doesn't work
	 * when the & below is removed.
	 */
	[0 ... __NR_syscall_compat_max] = &sys_ni_syscall,
#include <asm/syscalls_32.h>
};
