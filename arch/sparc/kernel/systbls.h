#ifndef _SYSTBLS_H
#define _SYSTBLS_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/signal.h>
#include <asm/utrap.h>

asmlinkage unsigned long sys_getpagesize(void);
asmlinkage long sparc_pipe(struct pt_regs *regs);
asmlinkage unsigned long c_sys_nis_syscall(struct pt_regs *regs);
asmlinkage long sys_getdomainname(char __user *name, int len);
void do_rt_sigreturn(struct pt_regs *regs);
asmlinkage long sys_mmap(unsigned long addr, unsigned long len,
			 unsigned long prot, unsigned long flags,
			 unsigned long fd, unsigned long off);
asmlinkage void sparc_breakpoint(struct pt_regs *regs);

#ifdef CONFIG_SPARC32
asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
			  unsigned long prot, unsigned long flags,
			  unsigned long fd, unsigned long pgoff);
long sparc_remap_file_pages(unsigned long start, unsigned long size,
			    unsigned long prot, unsigned long pgoff,
			    unsigned long flags);

#endif /* CONFIG_SPARC32 */

#ifdef CONFIG_SPARC64
asmlinkage long sys_sparc_ipc(unsigned int call, int first,
			      unsigned long second,
			      unsigned long third,
			      void __user *ptr, long fifth);
asmlinkage long sparc64_personality(unsigned long personality);
asmlinkage long sys64_munmap(unsigned long addr, size_t len);
asmlinkage unsigned long sys64_mremap(unsigned long addr,
				      unsigned long old_len,
				      unsigned long new_len,
				      unsigned long flags,
				      unsigned long new_addr);
asmlinkage long sys_utrap_install(utrap_entry_t type,
				  utrap_handler_t new_p,
				  utrap_handler_t new_d,
				  utrap_handler_t __user *old_p,
				  utrap_handler_t __user *old_d);
asmlinkage long sparc_memory_ordering(unsigned long model,
				      struct pt_regs *regs);
asmlinkage void sparc64_set_context(struct pt_regs *regs);
asmlinkage void sparc64_get_context(struct pt_regs *regs);

#endif /* CONFIG_SPARC64 */
#endif /* _SYSTBLS_H */
