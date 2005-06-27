/* 
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/file.h"
#include "linux/smp_lock.h"
#include "linux/mm.h"
#include "linux/utsname.h"
#include "linux/msg.h"
#include "linux/shm.h"
#include "linux/sys.h"
#include "linux/syscalls.h"
#include "linux/unistd.h"
#include "linux/slab.h"
#include "linux/utime.h"
#include "asm/mman.h"
#include "asm/uaccess.h"
#include "kern_util.h"
#include "user_util.h"
#include "sysdep/syscalls.h"
#include "mode_kern.h"
#include "choose-mode.h"

/*  Unlocked, I don't care if this is a bit off */
int nsyscalls = 0;

long sys_fork(void)
{
	long ret;

	current->thread.forking = 1;
	ret = do_fork(SIGCHLD, UPT_SP(&current->thread.regs.regs),
		      &current->thread.regs, 0, NULL, NULL);
	current->thread.forking = 0;
	return(ret);
}

long sys_vfork(void)
{
	long ret;

	current->thread.forking = 1;
	ret = do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD,
		      UPT_SP(&current->thread.regs.regs),
		      &current->thread.regs, 0, NULL, NULL);
	current->thread.forking = 0;
	return(ret);
}

/* common code for old and new mmaps */
long sys_mmap2(unsigned long addr, unsigned long len,
	       unsigned long prot, unsigned long flags,
	       unsigned long fd, unsigned long pgoff)
{
	long error = -EBADF;
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

long old_mmap(unsigned long addr, unsigned long len,
	      unsigned long prot, unsigned long flags,
	      unsigned long fd, unsigned long offset)
{
	long err = -EINVAL;
	if (offset & ~PAGE_MASK)
		goto out;

	err = sys_mmap2(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
 out:
	return err;
}
/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
long sys_pipe(unsigned long __user * fildes)
{
        int fd[2];
        long error;

        error = do_pipe(fd);
        if (!error) {
		if (copy_to_user(fildes, fd, sizeof(fd)))
                        error = -EFAULT;
        }
        return error;
}


long sys_uname(struct old_utsname * name)
{
	long err;
	if (!name)
		return -EFAULT;
	down_read(&uts_sem);
	err=copy_to_user(name, &system_utsname, sizeof (*name));
	up_read(&uts_sem);
	return err?-EFAULT:0;
}

long sys_olduname(struct oldold_utsname * name)
{
	long error;

	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct oldold_utsname)))
		return -EFAULT;
  
  	down_read(&uts_sem);
	
	error = __copy_to_user(&name->sysname,&system_utsname.sysname,
			       __OLD_UTS_LEN);
	error |= __put_user(0,name->sysname+__OLD_UTS_LEN);
	error |= __copy_to_user(&name->nodename,&system_utsname.nodename,
				__OLD_UTS_LEN);
	error |= __put_user(0,name->nodename+__OLD_UTS_LEN);
	error |= __copy_to_user(&name->release,&system_utsname.release,
				__OLD_UTS_LEN);
	error |= __put_user(0,name->release+__OLD_UTS_LEN);
	error |= __copy_to_user(&name->version,&system_utsname.version,
				__OLD_UTS_LEN);
	error |= __put_user(0,name->version+__OLD_UTS_LEN);
	error |= __copy_to_user(&name->machine,&system_utsname.machine,
				__OLD_UTS_LEN);
	error |= __put_user(0,name->machine+__OLD_UTS_LEN);
	
	up_read(&uts_sem);
	
	error = error ? -EFAULT : 0;

	return error;
}

DEFINE_SPINLOCK(syscall_lock);

static int syscall_index = 0;

int next_syscall_index(int limit)
{
	int ret;

	spin_lock(&syscall_lock);
	ret = syscall_index;
	if(++syscall_index == limit)
		syscall_index = 0;
	spin_unlock(&syscall_lock);
	return(ret);
}
