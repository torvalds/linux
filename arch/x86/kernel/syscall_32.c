/* System call table for i386. */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <linux/cache.h>
#include <asm/asm-offsets.h>

#ifdef CONFIG_IA32_EMULATION
#define SYM(sym, compat) compat
#else
#define SYM(sym, compat) sym
#define ia32_sys_call_table sys_call_table
#define __NR_ia32_syscall_max __NR_syscall_max
#endif

#define __SYSCALL_I386(nr, sym, compat) extern asmlinkage void SYM(sym, compat)(void) ;
#include <asm/syscalls_32.h>
#undef __SYSCALL_I386

#define __SYSCALL_I386(nr, sym, compat) [nr] = SYM(sym, compat),

typedef asmlinkage void (*sys_call_ptr_t)(void);

extern asmlinkage void sys_ni_syscall(void);

__visible const sys_call_ptr_t ia32_sys_call_table[__NR_ia32_syscall_max+1] = {
	/*
	 * Smells like a compiler bug -- it doesn't work
	 * when the & below is removed.
	 */
	[0 ... __NR_ia32_syscall_max] = &sys_ni_syscall,
#include <asm/syscalls_32.h>
};
