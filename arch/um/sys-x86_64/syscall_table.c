/*
 * System call table for UML/x86-64, copied from arch/x86_64/kernel/syscall.c
 * with some changes for UML.
 */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <linux/cache.h>
#include <kern_constants.h>

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
/*
 * On x86-64 sys_uname is actually sys_newuname plus a compatibility trick.
 * See arch/x86_64/kernel/sys_x86_64.c
 */
#define sys_uname sys_uname64

#define stub_clone sys_clone
#define stub_fork sys_fork
#define stub_vfork sys_vfork
#define stub_execve sys_execve
#define stub_rt_sigsuspend sys_rt_sigsuspend
#define stub_sigaltstack sys_sigaltstack
#define stub_rt_sigreturn sys_rt_sigreturn

#define __SYSCALL(nr, sym) extern asmlinkage void sym(void) ;
#undef _ASM_X86_64_UNISTD_H_
#include <asm-x86/unistd_64.h>

#undef __SYSCALL
#define __SYSCALL(nr, sym) [ nr ] = sym,
#undef _ASM_X86_64_UNISTD_H_

typedef void (*sys_call_ptr_t)(void);

extern void sys_ni_syscall(void);

sys_call_ptr_t sys_call_table[UM_NR_syscall_max+1] __cacheline_aligned = {
	/*
	 * Smells like a like a compiler bug -- it doesn't work when the &
	 * below is removed.
	 */
	[0 ... UM_NR_syscall_max] = &sys_ni_syscall,
#include <asm-x86/unistd_64.h>
};
