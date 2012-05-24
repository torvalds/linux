/*
 * System call table for UML/x86-64, copied from arch/x86/kernel/syscall_*.c
 * with some changes for UML.
 */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <linux/cache.h>
#include <generated/user_constants.h>

#define __NO_STUBS

/*
 * Below you can see, in terms of #define's, the differences between the x86-64
 * and the UML syscall table.
 */

/* Not going to be implemented by UML, since we have no hardware. */
#define stub_iopl sys_ni_syscall
#define sys_ioperm sys_ni_syscall

/*
 * The UML TLS problem. Note that x86_64 does not implement this, so the below
 * is needed only for the ia32 compatibility.
 */

/* On UML we call it this way ("old" means it's not mmap2) */
#define sys_mmap old_mmap

#define stub_clone sys_clone
#define stub_fork sys_fork
#define stub_vfork sys_vfork
#define stub_execve sys_execve
#define stub_rt_sigsuspend sys_rt_sigsuspend
#define stub_sigaltstack sys_sigaltstack
#define stub_rt_sigreturn sys_rt_sigreturn

#define __SYSCALL_COMMON(nr, sym, compat) __SYSCALL_64(nr, sym, compat)
#define __SYSCALL_X32(nr, sym, compat) /* Not supported */

#define __SYSCALL_64(nr, sym, compat) extern asmlinkage void sym(void) ;
#include <asm/syscalls_64.h>

#undef __SYSCALL_64
#define __SYSCALL_64(nr, sym, compat) [ nr ] = sym,

typedef void (*sys_call_ptr_t)(void);

extern void sys_ni_syscall(void);

const sys_call_ptr_t sys_call_table[] __cacheline_aligned = {
	/*
	 * Smells like a compiler bug -- it doesn't work
	 * when the & below is removed.
	 */
	[0 ... __NR_syscall_max] = &sys_ni_syscall,
#include <asm/syscalls_64.h>
};

int syscall_table_size = sizeof(sys_call_table);
