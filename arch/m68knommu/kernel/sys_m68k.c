/*
 * linux/arch/m68knommu/kernel/sys_m68k.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/m68k
 * platform.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/ipc.h>
#include <linux/fs.h>

#include <asm/setup.h>
#include <asm/uaccess.h>
#include <asm/cachectl.h>
#include <asm/traps.h>
#include <asm/cacheflush.h>
#include <asm/unistd.h>

/* sys_cacheflush -- flush (part of) the processor cache.  */
asmlinkage int
sys_cacheflush (unsigned long addr, int scope, int cache, unsigned long len)
{
	flush_cache_all();
	return(0);
}

asmlinkage int sys_getpagesize(void)
{
	return PAGE_SIZE;
}

/*
 * Do a system call from kernel instead of calling sys_execve so we
 * end up with proper pt_regs.
 */
int kernel_execve(const char *filename, char *const argv[], char *const envp[])
{
	register long __res asm ("%d0") = __NR_execve;
	register long __a asm ("%d1") = (long)(filename);
	register long __b asm ("%d2") = (long)(argv);
	register long __c asm ("%d3") = (long)(envp);
	asm volatile ("trap  #0" : "+d" (__res)
			: "d" (__a), "d" (__b), "d" (__c));
	return __res;
}

asmlinkage unsigned long sys_get_thread_area(void)
{
	return current_thread_info()->tp_value;
}

asmlinkage int sys_set_thread_area(unsigned long tp)
{
	current_thread_info()->tp_value = tp;
	return 0;
}

/* This syscall gets its arguments in A0 (mem), D2 (oldval) and
   D1 (newval).  */
asmlinkage int
sys_atomic_cmpxchg_32(unsigned long newval, int oldval, int d3, int d4, int d5,
		      unsigned long __user * mem)
{
	struct mm_struct *mm = current->mm;
	unsigned long mem_value;

	down_read(&mm->mmap_sem);

	mem_value = *mem;
	if (mem_value == oldval)
		*mem = newval;

	up_read(&mm->mmap_sem);
	return mem_value;
}

asmlinkage int sys_atomic_barrier(void)
{
	/* no code needed for uniprocs */
	return 0;
}
