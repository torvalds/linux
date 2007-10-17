/*
 * linux/arch/m68k/kernel/sys_m68k.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/m68k
 * platform.
 */

#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/ipc.h>

#include <asm/setup.h>
#include <asm/uaccess.h>
#include <asm/cachectl.h>
#include <asm/traps.h>
#include <asm/page.h>
#include <asm/unistd.h>

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage int sys_pipe(unsigned long __user * fildes)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (!error) {
		if (copy_to_user(fildes, fd, 2*sizeof(int)))
			error = -EFAULT;
	}
	return error;
}

/* common code for old and new mmaps */
static inline long do_mmap2(
	unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	int error = -EBADF;
	struct file * file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:
	return error;
}

asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	return do_mmap2(addr, len, prot, flags, fd, pgoff);
}

/*
 * Perform the select(nd, in, out, ex, tv) and mmap() system
 * calls. Linux/m68k cloned Linux/i386, which didn't use to be able to
 * handle more than 4 system call parameters, so these system calls
 * used a memory block for parameter passing..
 */

struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

asmlinkage int old_mmap(struct mmap_arg_struct __user *arg)
{
	struct mmap_arg_struct a;
	int error = -EFAULT;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;

	error = -EINVAL;
	if (a.offset & ~PAGE_MASK)
		goto out;

	a.flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	error = do_mmap2(a.addr, a.len, a.prot, a.flags, a.fd, a.offset >> PAGE_SHIFT);
out:
	return error;
}

#if 0
struct mmap_arg_struct64 {
	__u32 addr;
	__u32 len;
	__u32 prot;
	__u32 flags;
	__u64 offset; /* 64 bits */
	__u32 fd;
};

asmlinkage long sys_mmap64(struct mmap_arg_struct64 *arg)
{
	int error = -EFAULT;
	struct file * file = NULL;
	struct mmap_arg_struct64 a;
	unsigned long pgoff;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;

	if ((long)a.offset & ~PAGE_MASK)
		return -EINVAL;

	pgoff = a.offset >> PAGE_SHIFT;
	if ((a.offset >> PAGE_SHIFT) != pgoff)
		return -EINVAL;

	if (!(a.flags & MAP_ANONYMOUS)) {
		error = -EBADF;
		file = fget(a.fd);
		if (!file)
			goto out;
	}
	a.flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, a.addr, a.len, a.prot, a.flags, pgoff);
	up_write(&current->mm->mmap_sem);
	if (file)
		fput(file);
out:
	return error;
}
#endif

struct sel_arg_struct {
	unsigned long n;
	fd_set __user *inp, *outp, *exp;
	struct timeval __user *tvp;
};

asmlinkage int old_select(struct sel_arg_struct __user *arg)
{
	struct sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	/* sys_select() does the appropriate kernel locking */
	return sys_select(a.n, a.inp, a.outp, a.exp, a.tvp);
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int sys_ipc (uint call, int first, int second,
			int third, void __user *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			return sys_semop (first, ptr, second);
		case SEMGET:
			return sys_semget (first, second, third);
		case SEMCTL: {
			union semun fourth;
			if (!ptr)
				return -EINVAL;
			if (get_user(fourth.__pad, (void __user *__user *) ptr))
				return -EFAULT;
			return sys_semctl (first, second, third, fourth);
			}
		default:
			return -ENOSYS;
		}
	if (call <= MSGCTL)
		switch (call) {
		case MSGSND:
			return sys_msgsnd (first, ptr, second, third);
		case MSGRCV:
			switch (version) {
			case 0: {
				struct ipc_kludge tmp;
				if (!ptr)
					return -EINVAL;
				if (copy_from_user (&tmp, ptr, sizeof (tmp)))
					return -EFAULT;
				return sys_msgrcv (first, tmp.msgp, second,
						   tmp.msgtyp, third);
				}
			default:
				return sys_msgrcv (first, ptr,
						   second, fifth, third);
			}
		case MSGGET:
			return sys_msgget ((key_t) first, second);
		case MSGCTL:
			return sys_msgctl (first, second, ptr);
		default:
			return -ENOSYS;
		}
	if (call <= SHMCTL)
		switch (call) {
		case SHMAT:
			switch (version) {
			default: {
				ulong raddr;
				ret = do_shmat (first, ptr, second, &raddr);
				if (ret)
					return ret;
				return put_user (raddr, (ulong __user *) third);
			}
			}
		case SHMDT:
			return sys_shmdt (ptr);
		case SHMGET:
			return sys_shmget (first, second, third);
		case SHMCTL:
			return sys_shmctl (first, second, ptr);
		default:
			return -ENOSYS;
		}

	return -EINVAL;
}

/* Convert virtual (user) address VADDR to physical address PADDR */
#define virt_to_phys_040(vaddr)						\
({									\
  unsigned long _mmusr, _paddr;						\
									\
  __asm__ __volatile__ (".chip 68040\n\t"				\
			"ptestr (%1)\n\t"				\
			"movec %%mmusr,%0\n\t"				\
			".chip 68k"					\
			: "=r" (_mmusr)					\
			: "a" (vaddr));					\
  _paddr = (_mmusr & MMU_R_040) ? (_mmusr & PAGE_MASK) : 0;		\
  _paddr;								\
})

static inline int
cache_flush_040 (unsigned long addr, int scope, int cache, unsigned long len)
{
  unsigned long paddr, i;

  switch (scope)
    {
    case FLUSH_SCOPE_ALL:
      switch (cache)
	{
	case FLUSH_CACHE_DATA:
	  /* This nop is needed for some broken versions of the 68040.  */
	  __asm__ __volatile__ ("nop\n\t"
				".chip 68040\n\t"
				"cpusha %dc\n\t"
				".chip 68k");
	  break;
	case FLUSH_CACHE_INSN:
	  __asm__ __volatile__ ("nop\n\t"
				".chip 68040\n\t"
				"cpusha %ic\n\t"
				".chip 68k");
	  break;
	default:
	case FLUSH_CACHE_BOTH:
	  __asm__ __volatile__ ("nop\n\t"
				".chip 68040\n\t"
				"cpusha %bc\n\t"
				".chip 68k");
	  break;
	}
      break;

    case FLUSH_SCOPE_LINE:
      /* Find the physical address of the first mapped page in the
	 address range.  */
      if ((paddr = virt_to_phys_040(addr))) {
        paddr += addr & ~(PAGE_MASK | 15);
        len = (len + (addr & 15) + 15) >> 4;
      } else {
	unsigned long tmp = PAGE_SIZE - (addr & ~PAGE_MASK);

	if (len <= tmp)
	  return 0;
	addr += tmp;
	len -= tmp;
	tmp = PAGE_SIZE;
	for (;;)
	  {
	    if ((paddr = virt_to_phys_040(addr)))
	      break;
	    if (len <= tmp)
	      return 0;
	    addr += tmp;
	    len -= tmp;
	  }
	len = (len + 15) >> 4;
      }
      i = (PAGE_SIZE - (paddr & ~PAGE_MASK)) >> 4;
      while (len--)
	{
	  switch (cache)
	    {
	    case FLUSH_CACHE_DATA:
	      __asm__ __volatile__ ("nop\n\t"
				    ".chip 68040\n\t"
				    "cpushl %%dc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    case FLUSH_CACHE_INSN:
	      __asm__ __volatile__ ("nop\n\t"
				    ".chip 68040\n\t"
				    "cpushl %%ic,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    default:
	    case FLUSH_CACHE_BOTH:
	      __asm__ __volatile__ ("nop\n\t"
				    ".chip 68040\n\t"
				    "cpushl %%bc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    }
	  if (!--i && len)
	    {
	      /*
	       * No need to page align here since it is done by
	       * virt_to_phys_040().
	       */
	      addr += PAGE_SIZE;
	      i = PAGE_SIZE / 16;
	      /* Recompute physical address when crossing a page
	         boundary. */
	      for (;;)
		{
		  if ((paddr = virt_to_phys_040(addr)))
		    break;
		  if (len <= i)
		    return 0;
		  len -= i;
		  addr += PAGE_SIZE;
		}
	    }
	  else
	    paddr += 16;
	}
      break;

    default:
    case FLUSH_SCOPE_PAGE:
      len += (addr & ~PAGE_MASK) + (PAGE_SIZE - 1);
      for (len >>= PAGE_SHIFT; len--; addr += PAGE_SIZE)
	{
	  if (!(paddr = virt_to_phys_040(addr)))
	    continue;
	  switch (cache)
	    {
	    case FLUSH_CACHE_DATA:
	      __asm__ __volatile__ ("nop\n\t"
				    ".chip 68040\n\t"
				    "cpushp %%dc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    case FLUSH_CACHE_INSN:
	      __asm__ __volatile__ ("nop\n\t"
				    ".chip 68040\n\t"
				    "cpushp %%ic,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    default:
	    case FLUSH_CACHE_BOTH:
	      __asm__ __volatile__ ("nop\n\t"
				    ".chip 68040\n\t"
				    "cpushp %%bc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    }
	}
      break;
    }
  return 0;
}

#define virt_to_phys_060(vaddr)				\
({							\
  unsigned long paddr;					\
  __asm__ __volatile__ (".chip 68060\n\t"		\
			"plpar (%0)\n\t"		\
			".chip 68k"			\
			: "=a" (paddr)			\
			: "0" (vaddr));			\
  (paddr); /* XXX */					\
})

static inline int
cache_flush_060 (unsigned long addr, int scope, int cache, unsigned long len)
{
  unsigned long paddr, i;

  /*
   * 68060 manual says:
   *  cpush %dc : flush DC, remains valid (with our %cacr setup)
   *  cpush %ic : invalidate IC
   *  cpush %bc : flush DC + invalidate IC
   */
  switch (scope)
    {
    case FLUSH_SCOPE_ALL:
      switch (cache)
	{
	case FLUSH_CACHE_DATA:
	  __asm__ __volatile__ (".chip 68060\n\t"
				"cpusha %dc\n\t"
				".chip 68k");
	  break;
	case FLUSH_CACHE_INSN:
	  __asm__ __volatile__ (".chip 68060\n\t"
				"cpusha %ic\n\t"
				".chip 68k");
	  break;
	default:
	case FLUSH_CACHE_BOTH:
	  __asm__ __volatile__ (".chip 68060\n\t"
				"cpusha %bc\n\t"
				".chip 68k");
	  break;
	}
      break;

    case FLUSH_SCOPE_LINE:
      /* Find the physical address of the first mapped page in the
	 address range.  */
      len += addr & 15;
      addr &= -16;
      if (!(paddr = virt_to_phys_060(addr))) {
	unsigned long tmp = PAGE_SIZE - (addr & ~PAGE_MASK);

	if (len <= tmp)
	  return 0;
	addr += tmp;
	len -= tmp;
	tmp = PAGE_SIZE;
	for (;;)
	  {
	    if ((paddr = virt_to_phys_060(addr)))
	      break;
	    if (len <= tmp)
	      return 0;
	    addr += tmp;
	    len -= tmp;
	  }
      }
      len = (len + 15) >> 4;
      i = (PAGE_SIZE - (paddr & ~PAGE_MASK)) >> 4;
      while (len--)
	{
	  switch (cache)
	    {
	    case FLUSH_CACHE_DATA:
	      __asm__ __volatile__ (".chip 68060\n\t"
				    "cpushl %%dc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    case FLUSH_CACHE_INSN:
	      __asm__ __volatile__ (".chip 68060\n\t"
				    "cpushl %%ic,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    default:
	    case FLUSH_CACHE_BOTH:
	      __asm__ __volatile__ (".chip 68060\n\t"
				    "cpushl %%bc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    }
	  if (!--i && len)
	    {

	      /*
	       * We just want to jump to the first cache line
	       * in the next page.
	       */
	      addr += PAGE_SIZE;
	      addr &= PAGE_MASK;

	      i = PAGE_SIZE / 16;
	      /* Recompute physical address when crossing a page
	         boundary. */
	      for (;;)
	        {
	          if ((paddr = virt_to_phys_060(addr)))
	            break;
	          if (len <= i)
	            return 0;
	          len -= i;
	          addr += PAGE_SIZE;
	        }
	    }
	  else
	    paddr += 16;
	}
      break;

    default:
    case FLUSH_SCOPE_PAGE:
      len += (addr & ~PAGE_MASK) + (PAGE_SIZE - 1);
      addr &= PAGE_MASK;	/* Workaround for bug in some
				   revisions of the 68060 */
      for (len >>= PAGE_SHIFT; len--; addr += PAGE_SIZE)
	{
	  if (!(paddr = virt_to_phys_060(addr)))
	    continue;
	  switch (cache)
	    {
	    case FLUSH_CACHE_DATA:
	      __asm__ __volatile__ (".chip 68060\n\t"
				    "cpushp %%dc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    case FLUSH_CACHE_INSN:
	      __asm__ __volatile__ (".chip 68060\n\t"
				    "cpushp %%ic,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    default:
	    case FLUSH_CACHE_BOTH:
	      __asm__ __volatile__ (".chip 68060\n\t"
				    "cpushp %%bc,(%0)\n\t"
				    ".chip 68k"
				    : : "a" (paddr));
	      break;
	    }
	}
      break;
    }
  return 0;
}

/* sys_cacheflush -- flush (part of) the processor cache.  */
asmlinkage int
sys_cacheflush (unsigned long addr, int scope, int cache, unsigned long len)
{
	struct vm_area_struct *vma;
	int ret = -EINVAL;

	lock_kernel();
	if (scope < FLUSH_SCOPE_LINE || scope > FLUSH_SCOPE_ALL ||
	    cache & ~FLUSH_CACHE_BOTH)
		goto out;

	if (scope == FLUSH_SCOPE_ALL) {
		/* Only the superuser may explicitly flush the whole cache. */
		ret = -EPERM;
		if (!capable(CAP_SYS_ADMIN))
			goto out;
	} else {
		/*
		 * Verify that the specified address region actually belongs
		 * to this process.
		 */
		vma = find_vma (current->mm, addr);
		ret = -EINVAL;
		/* Check for overflow.  */
		if (addr + len < addr)
			goto out;
		if (vma == NULL || addr < vma->vm_start || addr + len > vma->vm_end)
			goto out;
	}

	if (CPU_IS_020_OR_030) {
		if (scope == FLUSH_SCOPE_LINE && len < 256) {
			unsigned long cacr;
			__asm__ ("movec %%cacr, %0" : "=r" (cacr));
			if (cache & FLUSH_CACHE_INSN)
				cacr |= 4;
			if (cache & FLUSH_CACHE_DATA)
				cacr |= 0x400;
			len >>= 2;
			while (len--) {
				__asm__ __volatile__ ("movec %1, %%caar\n\t"
						      "movec %0, %%cacr"
						      : /* no outputs */
						      : "r" (cacr), "r" (addr));
				addr += 4;
			}
		} else {
			/* Flush the whole cache, even if page granularity requested. */
			unsigned long cacr;
			__asm__ ("movec %%cacr, %0" : "=r" (cacr));
			if (cache & FLUSH_CACHE_INSN)
				cacr |= 8;
			if (cache & FLUSH_CACHE_DATA)
				cacr |= 0x800;
			__asm__ __volatile__ ("movec %0, %%cacr" : : "r" (cacr));
		}
		ret = 0;
		goto out;
	} else {
	    /*
	     * 040 or 060: don't blindly trust 'scope', someone could
	     * try to flush a few megs of memory.
	     */

	    if (len>=3*PAGE_SIZE && scope<FLUSH_SCOPE_PAGE)
	        scope=FLUSH_SCOPE_PAGE;
	    if (len>=10*PAGE_SIZE && scope<FLUSH_SCOPE_ALL)
	        scope=FLUSH_SCOPE_ALL;
	    if (CPU_IS_040) {
		ret = cache_flush_040 (addr, scope, cache, len);
	    } else if (CPU_IS_060) {
		ret = cache_flush_060 (addr, scope, cache, len);
	    }
	}
out:
	unlock_kernel();
	return ret;
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
