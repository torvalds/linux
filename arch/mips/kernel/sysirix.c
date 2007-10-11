/*
 * sysirix.c: IRIX system call emulation.
 *
 * Copyright (C) 1996 David S. Miller
 * Copyright (C) 1997 Miguel de Icaza
 * Copyright (C) 1997, 1998, 1999, 2000 Ralf Baechle
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/binfmts.h>
#include <linux/capability.h>
#include <linux/highuid.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/times.h>
#include <linux/elf.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/utsname.h>
#include <linux/file.h>
#include <linux/vfs.h>
#include <linux/namei.h>
#include <linux/socket.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/resource.h>

#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/uaccess.h>
#include <asm/inventory.h>

/* 2,191 lines of complete and utter shit coming up... */

extern int max_threads;

/* The sysmp commands supported thus far. */
#define MP_NPROCS       	1 /* # processor in complex */
#define MP_NAPROCS      	2 /* # active processors in complex */
#define MP_PGSIZE           	14 /* Return system page size in v1. */

asmlinkage int irix_sysmp(struct pt_regs *regs)
{
	unsigned long cmd;
	int base = 0;
	int error = 0;

	if(regs->regs[2] == 1000)
		base = 1;
	cmd = regs->regs[base + 4];
	switch(cmd) {
	case MP_PGSIZE:
		error = PAGE_SIZE;
		break;
	case MP_NPROCS:
	case MP_NAPROCS:
		error = num_online_cpus();
		break;
	default:
		printk("SYSMP[%s:%d]: Unsupported opcode %d\n",
		       current->comm, current->pid, (int)cmd);
		error = -EINVAL;
		break;
	}

	return error;
}

/* The prctl commands. */
#define PR_MAXPROCS		 1 /* Tasks/user. */
#define PR_ISBLOCKED		 2 /* If blocked, return 1. */
#define PR_SETSTACKSIZE		 3 /* Set largest task stack size. */
#define PR_GETSTACKSIZE		 4 /* Get largest task stack size. */
#define PR_MAXPPROCS		 5 /* Num parallel tasks. */
#define PR_UNBLKONEXEC		 6 /* When task exec/exit's, unblock. */
#define PR_SETEXITSIG		 8 /* When task exit's, set signal. */
#define PR_RESIDENT		 9 /* Make task unswappable. */
#define PR_ATTACHADDR		10 /* (Re-)Connect a vma to a task. */
#define PR_DETACHADDR		11 /* Disconnect a vma from a task. */
#define PR_TERMCHILD		12 /* Kill child if the parent dies. */
#define PR_GETSHMASK		13 /* Get the sproc() share mask. */
#define PR_GETNSHARE		14 /* Number of share group members. */
#define PR_COREPID		15 /* Add task pid to name when it core. */
#define PR_ATTACHADDRPERM	16 /* (Re-)Connect vma, with specified prot. */
#define PR_PTHREADEXIT		17 /* Kill a pthread, only for IRIX 6.[234] */

asmlinkage int irix_prctl(unsigned option, ...)
{
	va_list args;
	int error = 0;

	va_start(args, option);
	switch (option) {
	case PR_MAXPROCS:
		printk("irix_prctl[%s:%d]: Wants PR_MAXPROCS\n",
		       current->comm, current->pid);
		error = max_threads;
		break;

	case PR_ISBLOCKED: {
		struct task_struct *task;

		printk("irix_prctl[%s:%d]: Wants PR_ISBLOCKED\n",
		       current->comm, current->pid);
		read_lock(&tasklist_lock);
		task = find_task_by_pid(va_arg(args, pid_t));
		error = -ESRCH;
		if (error)
			error = (task->run_list.next != NULL);
		read_unlock(&tasklist_lock);
		/* Can _your_ OS find this out that fast? */
		break;
	}

	case PR_SETSTACKSIZE: {
		long value = va_arg(args, long);

		printk("irix_prctl[%s:%d]: Wants PR_SETSTACKSIZE<%08lx>\n",
		       current->comm, current->pid, (unsigned long) value);
		if (value > RLIM_INFINITY)
			value = RLIM_INFINITY;
		if (capable(CAP_SYS_ADMIN)) {
			task_lock(current->group_leader);
			current->signal->rlim[RLIMIT_STACK].rlim_max =
				current->signal->rlim[RLIMIT_STACK].rlim_cur = value;
			task_unlock(current->group_leader);
			error = value;
			break;
		}
		task_lock(current->group_leader);
		if (value > current->signal->rlim[RLIMIT_STACK].rlim_max) {
			error = -EINVAL;
			task_unlock(current->group_leader);
			break;
		}
		current->signal->rlim[RLIMIT_STACK].rlim_cur = value;
		task_unlock(current->group_leader);
		error = value;
		break;
	}

	case PR_GETSTACKSIZE:
		printk("irix_prctl[%s:%d]: Wants PR_GETSTACKSIZE\n",
		       current->comm, current->pid);
		error = current->signal->rlim[RLIMIT_STACK].rlim_cur;
		break;

	case PR_MAXPPROCS:
		printk("irix_prctl[%s:%d]: Wants PR_MAXPROCS\n",
		       current->comm, current->pid);
		error = 1;
		break;

	case PR_UNBLKONEXEC:
		printk("irix_prctl[%s:%d]: Wants PR_UNBLKONEXEC\n",
		       current->comm, current->pid);
		error = -EINVAL;
		break;

	case PR_SETEXITSIG:
		printk("irix_prctl[%s:%d]: Wants PR_SETEXITSIG\n",
		       current->comm, current->pid);

		/* We can probably play some game where we set the task
		 * exit_code to some non-zero value when this is requested,
		 * and check whether exit_code is already set in do_exit().
		 */
		error = -EINVAL;
		break;

	case PR_RESIDENT:
		printk("irix_prctl[%s:%d]: Wants PR_RESIDENT\n",
		       current->comm, current->pid);
		error = 0; /* Compatibility indeed. */
		break;

	case PR_ATTACHADDR:
		printk("irix_prctl[%s:%d]: Wants PR_ATTACHADDR\n",
		       current->comm, current->pid);
		error = -EINVAL;
		break;

	case PR_DETACHADDR:
		printk("irix_prctl[%s:%d]: Wants PR_DETACHADDR\n",
		       current->comm, current->pid);
		error = -EINVAL;
		break;

	case PR_TERMCHILD:
		printk("irix_prctl[%s:%d]: Wants PR_TERMCHILD\n",
		       current->comm, current->pid);
		error = -EINVAL;
		break;

	case PR_GETSHMASK:
		printk("irix_prctl[%s:%d]: Wants PR_GETSHMASK\n",
		       current->comm, current->pid);
		error = -EINVAL; /* Until I have the sproc() stuff in. */
		break;

	case PR_GETNSHARE:
		error = 0;       /* Until I have the sproc() stuff in. */
		break;

	case PR_COREPID:
		printk("irix_prctl[%s:%d]: Wants PR_COREPID\n",
		       current->comm, current->pid);
		error = -EINVAL;
		break;

	case PR_ATTACHADDRPERM:
		printk("irix_prctl[%s:%d]: Wants PR_ATTACHADDRPERM\n",
		       current->comm, current->pid);
		error = -EINVAL;
		break;

	default:
		printk("irix_prctl[%s:%d]: Non-existant opcode %d\n",
		       current->comm, current->pid, option);
		error = -EINVAL;
		break;
	}
	va_end(args);

	return error;
}

#undef DEBUG_PROCGRPS

extern unsigned long irix_mapelf(int fd, struct elf_phdr __user *user_phdrp, int cnt);
extern char *prom_getenv(char *name);
extern long prom_setenv(char *name, char *value);

/* The syssgi commands supported thus far. */
#define SGI_SYSID         1       /* Return unique per-machine identifier. */
#define SGI_INVENT        5       /* Fetch inventory  */
#   define SGI_INV_SIZEOF 1
#   define SGI_INV_READ   2
#define SGI_RDNAME        6       /* Return string name of a process. */
#define SGI_SETNVRAM	  8	  /* Set PROM variable. */
#define SGI_GETNVRAM	  9	  /* Get PROM variable. */
#define SGI_SETPGID      21       /* Set process group id. */
#define SGI_SYSCONF      22       /* POSIX sysconf garbage. */
#define SGI_PATHCONF     24       /* POSIX sysconf garbage. */
#define SGI_SETGROUPS    40       /* POSIX sysconf garbage. */
#define SGI_GETGROUPS    41       /* POSIX sysconf garbage. */
#define SGI_RUSAGE       56       /* BSD style rusage(). */
#define SGI_SSYNC        62       /* Synchronous fs sync. */
#define SGI_GETSID       65       /* SysVr4 get session id. */
#define SGI_ELFMAP       68       /* Map an elf image. */
#define SGI_TOSSTSAVE   108       /* Toss saved vma's. */
#define SGI_FP_BCOPY    129       /* Should FPU bcopy be used on this machine? */
#define SGI_PHYSP      1011       /* Translate virtual into physical page. */

asmlinkage int irix_syssgi(struct pt_regs *regs)
{
	unsigned long cmd;
	int retval, base = 0;

	if (regs->regs[2] == 1000)
		base = 1;

	cmd = regs->regs[base + 4];
	switch(cmd) {
	case SGI_SYSID: {
		char __user *buf = (char __user *) regs->regs[base + 5];

		/* XXX Use ethernet addr.... */
		retval = clear_user(buf, 64) ? -EFAULT : 0;
		break;
	}
#if 0
	case SGI_RDNAME: {
		int pid = (int) regs->regs[base + 5];
		char __user *buf = (char __user *) regs->regs[base + 6];
		struct task_struct *p;
		char tcomm[sizeof(current->comm)];

		read_lock(&tasklist_lock);
		p = find_task_by_pid(pid);
		if (!p) {
			read_unlock(&tasklist_lock);
			retval = -ESRCH;
			break;
		}
		get_task_comm(tcomm, p);
		read_unlock(&tasklist_lock);

		/* XXX Need to check sizes. */
		retval = copy_to_user(buf, tcomm, sizeof(tcomm)) ? -EFAULT : 0;
		break;
	}

	case SGI_GETNVRAM: {
		char __user *name = (char __user *) regs->regs[base+5];
		char __user *buf = (char __user *) regs->regs[base+6];
		char *value;
		return -EINVAL;	/* til I fix it */
		value = prom_getenv(name);	/* PROM lock?  */
		if (!value) {
			retval = -EINVAL;
			break;
		}
		/* Do I strlen() for the length? */
		retval = copy_to_user(buf, value, 128) ? -EFAULT : 0;
		break;
	}

	case SGI_SETNVRAM: {
		char __user *name = (char __user *) regs->regs[base+5];
		char __user *value = (char __user *) regs->regs[base+6];
		return -EINVAL;	/* til I fix it */
		retval = prom_setenv(name, value);
		/* XXX make sure retval conforms to syssgi(2) */
		printk("[%s:%d] setnvram(\"%s\", \"%s\"): retval %d",
		       current->comm, current->pid, name, value, retval);
/*		if (retval == PROM_ENOENT)
		  	retval = -ENOENT; */
		break;
	}
#endif

	case SGI_SETPGID: {
#ifdef DEBUG_PROCGRPS
		printk("[%s:%d] setpgid(%d, %d) ",
		       current->comm, current->pid,
		       (int) regs->regs[base + 5], (int)regs->regs[base + 6]);
#endif
		retval = sys_setpgid(regs->regs[base + 5], regs->regs[base + 6]);

#ifdef DEBUG_PROCGRPS
		printk("retval=%d\n", retval);
#endif
	}

	case SGI_SYSCONF: {
		switch(regs->regs[base + 5]) {
		case 1:
			retval = (MAX_ARG_PAGES >> 4); /* XXX estimate... */
			goto out;
		case 2:
			retval = max_threads;
			goto out;
		case 3:
			retval = HZ;
			goto out;
		case 4:
			retval = NGROUPS_MAX;
			goto out;
		case 5:
			retval = NR_OPEN;
			goto out;
		case 6:
			retval = 1;
			goto out;
		case 7:
			retval = 1;
			goto out;
		case 8:
			retval = 199009;
			goto out;
		case 11:
			retval = PAGE_SIZE;
			goto out;
		case 12:
			retval = 4;
			goto out;
		case 25:
		case 26:
		case 27:
		case 28:
		case 29:
		case 30:
			retval = 0;
			goto out;
		case 31:
			retval = 32;
			goto out;
		default:
			retval = -EINVAL;
			goto out;
		};
	}

	case SGI_SETGROUPS:
		retval = sys_setgroups((int) regs->regs[base + 5],
		                       (gid_t __user *) regs->regs[base + 6]);
		break;

	case SGI_GETGROUPS:
		retval = sys_getgroups((int) regs->regs[base + 5],
		                       (gid_t __user *) regs->regs[base + 6]);
		break;

	case SGI_RUSAGE: {
		struct rusage __user *ru = (struct rusage __user *) regs->regs[base + 6];

		switch((int) regs->regs[base + 5]) {
		case 0:
			/* rusage self */
			retval = getrusage(current, RUSAGE_SELF, ru);
			goto out;

		case -1:
			/* rusage children */
			retval = getrusage(current, RUSAGE_CHILDREN, ru);
			goto out;

		default:
			retval = -EINVAL;
			goto out;
		};
	}

	case SGI_SSYNC:
		sys_sync();
		retval = 0;
		break;

	case SGI_GETSID:
#ifdef DEBUG_PROCGRPS
		printk("[%s:%d] getsid(%d) ", current->comm, current->pid,
		       (int) regs->regs[base + 5]);
#endif
		retval = sys_getsid(regs->regs[base + 5]);
#ifdef DEBUG_PROCGRPS
		printk("retval=%d\n", retval);
#endif
		break;

	case SGI_ELFMAP:
		retval = irix_mapelf((int) regs->regs[base + 5],
				     (struct elf_phdr __user *) regs->regs[base + 6],
				     (int) regs->regs[base + 7]);
		break;

	case SGI_TOSSTSAVE:
		/* XXX We don't need to do anything? */
		retval = 0;
		break;

	case SGI_FP_BCOPY:
		retval = 0;
		break;

	case SGI_PHYSP: {
		unsigned long addr = regs->regs[base + 5];
		int __user *pageno = (int __user *) (regs->regs[base + 6]);
		struct mm_struct *mm = current->mm;
		pgd_t *pgdp;
		pud_t *pudp;
		pmd_t *pmdp;
		pte_t *ptep;

		down_read(&mm->mmap_sem);
		pgdp = pgd_offset(mm, addr);
		pudp = pud_offset(pgdp, addr);
		pmdp = pmd_offset(pudp, addr);
		ptep = pte_offset(pmdp, addr);
		retval = -EINVAL;
		if (ptep) {
			pte_t pte = *ptep;

			if (pte_val(pte) & (_PAGE_VALID | _PAGE_PRESENT)) {
				/* b0rked on 64-bit */
				retval =  put_user((pte_val(pte) & PAGE_MASK) >>
				                   PAGE_SHIFT, pageno);
			}
		}
		up_read(&mm->mmap_sem);
		break;
	}

	case SGI_INVENT: {
		int  arg1    = (int)    regs->regs [base + 5];
		void __user *buffer = (void __user *) regs->regs [base + 6];
		int  count   = (int)    regs->regs [base + 7];

		switch (arg1) {
		case SGI_INV_SIZEOF:
			retval = sizeof(inventory_t);
			break;
		case SGI_INV_READ:
			retval = dump_inventory_to_user(buffer, count);
			break;
		default:
			retval = -EINVAL;
		}
		break;
	}

	default:
		printk("irix_syssgi: Unsupported command %d\n", (int)cmd);
		retval = -EINVAL;
		break;
	};

out:
	return retval;
}

asmlinkage int irix_gtime(struct pt_regs *regs)
{
	return get_seconds();
}

/*
 * IRIX is completely broken... it returns 0 on success, otherwise
 * ENOMEM.
 */
asmlinkage int irix_brk(unsigned long brk)
{
	unsigned long rlim;
	unsigned long newbrk, oldbrk;
	struct mm_struct *mm = current->mm;
	int ret;

	down_write(&mm->mmap_sem);
	if (brk < mm->end_code) {
		ret = -ENOMEM;
		goto out;
	}

	newbrk = PAGE_ALIGN(brk);
	oldbrk = PAGE_ALIGN(mm->brk);
	if (oldbrk == newbrk) {
		mm->brk = brk;
		ret = 0;
		goto out;
	}

	/*
	 * Always allow shrinking brk
	 */
	if (brk <= mm->brk) {
		mm->brk = brk;
		do_munmap(mm, newbrk, oldbrk-newbrk);
		ret = 0;
		goto out;
	}
	/*
	 * Check against rlimit and stack..
	 */
	rlim = current->signal->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (brk - mm->end_code > rlim) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Check against existing mmap mappings.
	 */
	if (find_vma_intersection(mm, oldbrk, newbrk+PAGE_SIZE)) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Ok, looks good - let it rip.
	 */
	if (do_brk(oldbrk, newbrk-oldbrk) != oldbrk) {
		ret = -ENOMEM;
		goto out;
	}
	mm->brk = brk;
	ret = 0;

out:
	up_write(&mm->mmap_sem);
	return ret;
}

asmlinkage int irix_getpid(struct pt_regs *regs)
{
	regs->regs[3] = current->real_parent->pid;
	return current->pid;
}

asmlinkage int irix_getuid(struct pt_regs *regs)
{
	regs->regs[3] = current->euid;
	return current->uid;
}

asmlinkage int irix_getgid(struct pt_regs *regs)
{
	regs->regs[3] = current->egid;
	return current->gid;
}

asmlinkage int irix_stime(int value)
{
	int err;
	struct timespec tv;

	tv.tv_sec = value;
	tv.tv_nsec = 0;
	err = security_settime(&tv, NULL);
	if (err)
		return err;

	write_seqlock_irq(&xtime_lock);
	xtime.tv_sec = value;
	xtime.tv_nsec = 0;
	ntp_clear();
	write_sequnlock_irq(&xtime_lock);

	return 0;
}

static inline void jiffiestotv(unsigned long jiffies, struct timeval *value)
{
	value->tv_usec = (jiffies % HZ) * (1000000 / HZ);
	value->tv_sec = jiffies / HZ;
}

static inline void getitimer_real(struct itimerval *value)
{
	register unsigned long val, interval;

	interval = current->it_real_incr;
	val = 0;
	if (del_timer(&current->real_timer)) {
		unsigned long now = jiffies;
		val = current->real_timer.expires;
		add_timer(&current->real_timer);
		/* look out for negative/zero itimer.. */
		if (val <= now)
			val = now+1;
		val -= now;
	}
	jiffiestotv(val, &value->it_value);
	jiffiestotv(interval, &value->it_interval);
}

asmlinkage unsigned int irix_alarm(unsigned int seconds)
{
	return alarm_setitimer(seconds);
}

asmlinkage int irix_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();

	return -EINTR;
}

/* XXX need more than this... */
asmlinkage int irix_mount(char __user *dev_name, char __user *dir_name,
	unsigned long flags, char __user *type, void __user *data, int datalen)
{
	printk("[%s:%d] irix_mount(%p,%p,%08lx,%p,%p,%d)\n",
	       current->comm, current->pid,
	       dev_name, dir_name, flags, type, data, datalen);

	return sys_mount(dev_name, dir_name, type, flags, data);
}

struct irix_statfs {
	short f_type;
	long  f_bsize, f_frsize, f_blocks, f_bfree, f_files, f_ffree;
	char  f_fname[6], f_fpack[6];
};

asmlinkage int irix_statfs(const char __user *path,
	struct irix_statfs __user *buf, int len, int fs_type)
{
	struct nameidata nd;
	struct kstatfs kbuf;
	int error, i;

	/* We don't support this feature yet. */
	if (fs_type) {
		error = -EINVAL;
		goto out;
	}
	if (!access_ok(VERIFY_WRITE, buf, sizeof(struct irix_statfs))) {
		error = -EFAULT;
		goto out;
	}

	error = user_path_walk(path, &nd);
	if (error)
		goto out;

	error = vfs_statfs(nd.dentry, &kbuf);
	if (error)
		goto dput_and_out;

	error = __put_user(kbuf.f_type, &buf->f_type);
	error |= __put_user(kbuf.f_bsize, &buf->f_bsize);
	error |= __put_user(kbuf.f_frsize, &buf->f_frsize);
	error |= __put_user(kbuf.f_blocks, &buf->f_blocks);
	error |= __put_user(kbuf.f_bfree, &buf->f_bfree);
	error |= __put_user(kbuf.f_files, &buf->f_files);
	error |= __put_user(kbuf.f_ffree, &buf->f_ffree);
	for (i = 0; i < 6; i++) {
		error |= __put_user(0, &buf->f_fname[i]);
		error |= __put_user(0, &buf->f_fpack[i]);
	}

dput_and_out:
	path_release(&nd);
out:
	return error;
}

asmlinkage int irix_fstatfs(unsigned int fd, struct irix_statfs __user *buf)
{
	struct kstatfs kbuf;
	struct file *file;
	int error, i;

	if (!access_ok(VERIFY_WRITE, buf, sizeof(struct irix_statfs))) {
		error = -EFAULT;
		goto out;
	}

	if (!(file = fget(fd))) {
		error = -EBADF;
		goto out;
	}

	error = vfs_statfs(file->f_path.dentry, &kbuf);
	if (error)
		goto out_f;

	error = __put_user(kbuf.f_type, &buf->f_type);
	error |= __put_user(kbuf.f_bsize, &buf->f_bsize);
	error |= __put_user(kbuf.f_frsize, &buf->f_frsize);
	error |= __put_user(kbuf.f_blocks, &buf->f_blocks);
	error |= __put_user(kbuf.f_bfree, &buf->f_bfree);
	error |= __put_user(kbuf.f_files, &buf->f_files);
	error |= __put_user(kbuf.f_ffree, &buf->f_ffree);

	for (i = 0; i < 6; i++) {
		error |= __put_user(0, &buf->f_fname[i]);
		error |= __put_user(0, &buf->f_fpack[i]);
	}

out_f:
	fput(file);
out:
	return error;
}

asmlinkage int irix_setpgrp(int flags)
{
	int error;

#ifdef DEBUG_PROCGRPS
	printk("[%s:%d] setpgrp(%d) ", current->comm, current->pid, flags);
#endif
	if(!flags)
		error = process_group(current);
	else
		error = sys_setsid();
#ifdef DEBUG_PROCGRPS
	printk("returning %d\n", process_group(current));
#endif

	return error;
}

asmlinkage int irix_times(struct tms __user *tbuf)
{
	int err = 0;

	if (tbuf) {
		if (!access_ok(VERIFY_WRITE, tbuf, sizeof *tbuf))
			return -EFAULT;

		err = __put_user(current->utime, &tbuf->tms_utime);
		err |= __put_user(current->stime, &tbuf->tms_stime);
		err |= __put_user(current->signal->cutime, &tbuf->tms_cutime);
		err |= __put_user(current->signal->cstime, &tbuf->tms_cstime);
	}

	return err;
}

asmlinkage int irix_exec(struct pt_regs *regs)
{
	int error, base = 0;
	char *filename;

	if(regs->regs[2] == 1000)
		base = 1;
	filename = getname((char __user *) (long)regs->regs[base + 4]);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		return error;

	error = do_execve(filename, (char __user * __user *) (long)regs->regs[base + 5],
	                  NULL, regs);
	putname(filename);

	return error;
}

asmlinkage int irix_exece(struct pt_regs *regs)
{
	int error, base = 0;
	char *filename;

	if (regs->regs[2] == 1000)
		base = 1;
	filename = getname((char __user *) (long)regs->regs[base + 4]);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		return error;
	error = do_execve(filename, (char __user * __user *) (long)regs->regs[base + 5],
	                  (char __user * __user *) (long)regs->regs[base + 6], regs);
	putname(filename);

	return error;
}

asmlinkage unsigned long irix_gethostid(void)
{
	printk("[%s:%d]: irix_gethostid() called...\n",
	       current->comm, current->pid);

	return -EINVAL;
}

asmlinkage unsigned long irix_sethostid(unsigned long val)
{
	printk("[%s:%d]: irix_sethostid(%08lx) called...\n",
	       current->comm, current->pid, val);

	return -EINVAL;
}

asmlinkage int irix_socket(int family, int type, int protocol)
{
	switch(type) {
	case 1:
		type = SOCK_DGRAM;
		break;

	case 2:
		type = SOCK_STREAM;
		break;

	case 3:
		type = 9; /* Invalid... */
		break;

	case 4:
		type = SOCK_RAW;
		break;

	case 5:
		type = SOCK_RDM;
		break;

	case 6:
		type = SOCK_SEQPACKET;
		break;

	default:
		break;
	}

	return sys_socket(family, type, protocol);
}

asmlinkage int irix_getdomainname(char __user *name, int len)
{
	int err;

	down_read(&uts_sem);
	if (len > __NEW_UTS_LEN)
		len = __NEW_UTS_LEN;
	err = copy_to_user(name, utsname()->domainname, len) ? -EFAULT : 0;
	up_read(&uts_sem);

	return err;
}

asmlinkage unsigned long irix_getpagesize(void)
{
	return PAGE_SIZE;
}

asmlinkage int irix_msgsys(int opcode, unsigned long arg0, unsigned long arg1,
			   unsigned long arg2, unsigned long arg3,
			   unsigned long arg4)
{
	switch (opcode) {
	case 0:
		return sys_msgget((key_t) arg0, (int) arg1);
	case 1:
		return sys_msgctl((int) arg0, (int) arg1,
		                  (struct msqid_ds __user *)arg2);
	case 2:
		return sys_msgrcv((int) arg0, (struct msgbuf __user *) arg1,
				  (size_t) arg2, (long) arg3, (int) arg4);
	case 3:
		return sys_msgsnd((int) arg0, (struct msgbuf __user *) arg1,
				  (size_t) arg2, (int) arg3);
	default:
		return -EINVAL;
	}
}

asmlinkage int irix_shmsys(int opcode, unsigned long arg0, unsigned long arg1,
			   unsigned long arg2, unsigned long arg3)
{
	switch (opcode) {
	case 0:
		return do_shmat((int) arg0, (char __user *) arg1, (int) arg2,
				 (unsigned long *) arg3);
	case 1:
		return sys_shmctl((int)arg0, (int)arg1,
		                  (struct shmid_ds __user *)arg2);
	case 2:
		return sys_shmdt((char __user *)arg0);
	case 3:
		return sys_shmget((key_t) arg0, (int) arg1, (int) arg2);
	default:
		return -EINVAL;
	}
}

asmlinkage int irix_semsys(int opcode, unsigned long arg0, unsigned long arg1,
			   unsigned long arg2, int arg3)
{
	switch (opcode) {
	case 0:
		return sys_semctl((int) arg0, (int) arg1, (int) arg2,
				  (union semun) arg3);
	case 1:
		return sys_semget((key_t) arg0, (int) arg1, (int) arg2);
	case 2:
		return sys_semop((int) arg0, (struct sembuf __user *)arg1,
				 (unsigned int) arg2);
	default:
		return -EINVAL;
	}
}

static inline loff_t llseek(struct file *file, loff_t offset, int origin)
{
	loff_t (*fn)(struct file *, loff_t, int);
	loff_t retval;

	fn = default_llseek;
	if (file->f_op && file->f_op->llseek)
	fn = file->f_op->llseek;
	lock_kernel();
	retval = fn(file, offset, origin);
	unlock_kernel();

	return retval;
}

asmlinkage int irix_lseek64(int fd, int _unused, int offhi, int offlow,
                            int origin)
{
	struct file * file;
	loff_t offset;
	int retval;

	retval = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad;
	retval = -EINVAL;
	if (origin > 2)
		goto out_putf;

	offset = llseek(file, ((loff_t) offhi << 32) | offlow, origin);
	retval = (int) offset;

out_putf:
	fput(file);
bad:
	return retval;
}

asmlinkage int irix_sginap(int ticks)
{
	schedule_timeout_interruptible(ticks);
	return 0;
}

asmlinkage int irix_sgikopt(char __user *istring, char __user *ostring, int len)
{
	return -EINVAL;
}

asmlinkage int irix_gettimeofday(struct timeval __user *tv)
{
	time_t sec;
	long nsec, seq;
	int err;

	if (!access_ok(VERIFY_WRITE, tv, sizeof(struct timeval)))
		return -EFAULT;

	do {
		seq = read_seqbegin(&xtime_lock);
		sec = xtime.tv_sec;
		nsec = xtime.tv_nsec;
	} while (read_seqretry(&xtime_lock, seq));

	err = __put_user(sec, &tv->tv_sec);
	err |= __put_user((nsec / 1000), &tv->tv_usec);

	return err;
}

#define IRIX_MAP_AUTOGROW 0x40

asmlinkage unsigned long irix_mmap32(unsigned long addr, size_t len, int prot,
				     int flags, int fd, off_t offset)
{
	struct file *file = NULL;
	unsigned long retval;

	if (!(flags & MAP_ANONYMOUS)) {
		if (!(file = fget(fd)))
			return -EBADF;

		/* Ok, bad taste hack follows, try to think in something else
		 * when reading this.  */
		if (flags & IRIX_MAP_AUTOGROW) {
			unsigned long old_pos;
			long max_size = offset + len;

			if (max_size > file->f_path.dentry->d_inode->i_size) {
				old_pos = sys_lseek(fd, max_size - 1, 0);
				sys_write(fd, (void __user *) "", 1);
				sys_lseek(fd, old_pos, 0);
			}
		}
	}

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	down_write(&current->mm->mmap_sem);
	retval = do_mmap(file, addr, len, prot, flags, offset);
	up_write(&current->mm->mmap_sem);
	if (file)
		fput(file);

	return retval;
}

asmlinkage int irix_madvise(unsigned long addr, int len, int behavior)
{
	printk("[%s:%d] Wheee.. irix_madvise(%08lx,%d,%d)\n",
	       current->comm, current->pid, addr, len, behavior);

	return -EINVAL;
}

asmlinkage int irix_pagelock(char __user *addr, int len, int op)
{
	printk("[%s:%d] Wheee.. irix_pagelock(%p,%d,%d)\n",
	       current->comm, current->pid, addr, len, op);

	return -EINVAL;
}

asmlinkage int irix_quotactl(struct pt_regs *regs)
{
	printk("[%s:%d] Wheee.. irix_quotactl()\n",
	       current->comm, current->pid);

	return -EINVAL;
}

asmlinkage int irix_BSDsetpgrp(int pid, int pgrp)
{
	int error;

#ifdef DEBUG_PROCGRPS
	printk("[%s:%d] BSDsetpgrp(%d, %d) ", current->comm, current->pid,
	       pid, pgrp);
#endif
	if(!pid)
		pid = current->pid;

	/* Wheee, weird sysv thing... */
	if ((pgrp == 0) && (pid == current->pid))
		error = sys_setsid();
	else
		error = sys_setpgid(pid, pgrp);

#ifdef DEBUG_PROCGRPS
	printk("error = %d\n", error);
#endif

	return error;
}

asmlinkage int irix_systeminfo(int cmd, char __user *buf, int cnt)
{
	printk("[%s:%d] Wheee.. irix_systeminfo(%d,%p,%d)\n",
	       current->comm, current->pid, cmd, buf, cnt);

	return -EINVAL;
}

struct iuname {
	char sysname[257], nodename[257], release[257];
	char version[257], machine[257];
	char m_type[257], base_rel[257];
	char _unused0[257], _unused1[257], _unused2[257];
	char _unused3[257], _unused4[257], _unused5[257];
};

asmlinkage int irix_uname(struct iuname __user *buf)
{
	down_read(&uts_sem);
	if (copy_from_user(utsname()->sysname, buf->sysname, 65)
	    || copy_from_user(utsname()->nodename, buf->nodename, 65)
	    || copy_from_user(utsname()->release, buf->release, 65)
	    || copy_from_user(utsname()->version, buf->version, 65)
	    || copy_from_user(utsname()->machine, buf->machine, 65)) {
		return -EFAULT;
	}
	up_read(&uts_sem);

	return 1;
}

#undef DEBUG_XSTAT

static int irix_xstat32_xlate(struct kstat *stat, void __user *ubuf)
{
	struct xstat32 {
		u32 st_dev, st_pad1[3], st_ino, st_mode, st_nlink, st_uid, st_gid;
		u32 st_rdev, st_pad2[2], st_size, st_pad3;
		u32 st_atime0, st_atime1;
		u32 st_mtime0, st_mtime1;
		u32 st_ctime0, st_ctime1;
		u32 st_blksize, st_blocks;
		char st_fstype[16];
		u32 st_pad4[8];
	} ub;

	if (!sysv_valid_dev(stat->dev) || !sysv_valid_dev(stat->rdev))
		return -EOVERFLOW;
	ub.st_dev     = sysv_encode_dev(stat->dev);
	ub.st_ino     = stat->ino;
	ub.st_mode    = stat->mode;
	ub.st_nlink   = stat->nlink;
	SET_UID(ub.st_uid, stat->uid);
	SET_GID(ub.st_gid, stat->gid);
	ub.st_rdev    = sysv_encode_dev(stat->rdev);
#if BITS_PER_LONG == 32
	if (stat->size > MAX_NON_LFS)
		return -EOVERFLOW;
#endif
	ub.st_size    = stat->size;
	ub.st_atime0  = stat->atime.tv_sec;
	ub.st_atime1  = stat->atime.tv_nsec;
	ub.st_mtime0  = stat->mtime.tv_sec;
	ub.st_mtime1  = stat->atime.tv_nsec;
	ub.st_ctime0  = stat->ctime.tv_sec;
	ub.st_ctime1  = stat->atime.tv_nsec;
	ub.st_blksize = stat->blksize;
	ub.st_blocks  = stat->blocks;
	strcpy(ub.st_fstype, "efs");

	return copy_to_user(ubuf, &ub, sizeof(ub)) ? -EFAULT : 0;
}

static int irix_xstat64_xlate(struct kstat *stat, void __user *ubuf)
{
	struct xstat64 {
		u32 st_dev; s32 st_pad1[3];
		unsigned long long st_ino;
		u32 st_mode;
		u32 st_nlink; s32 st_uid; s32 st_gid; u32 st_rdev;
		s32 st_pad2[2];
		long long st_size;
		s32 st_pad3;
		struct { s32 tv_sec, tv_nsec; } st_atime, st_mtime, st_ctime;
		s32 st_blksize;
		long long  st_blocks;
		char st_fstype[16];
		s32 st_pad4[8];
	} ks;

	if (!sysv_valid_dev(stat->dev) || !sysv_valid_dev(stat->rdev))
		return -EOVERFLOW;

	ks.st_dev = sysv_encode_dev(stat->dev);
	ks.st_pad1[0] = ks.st_pad1[1] = ks.st_pad1[2] = 0;
	ks.st_ino = (unsigned long long) stat->ino;
	ks.st_mode = (u32) stat->mode;
	ks.st_nlink = (u32) stat->nlink;
	ks.st_uid = (s32) stat->uid;
	ks.st_gid = (s32) stat->gid;
	ks.st_rdev = sysv_encode_dev(stat->rdev);
	ks.st_pad2[0] = ks.st_pad2[1] = 0;
	ks.st_size = (long long) stat->size;
	ks.st_pad3 = 0;

	/* XXX hackety hack... */
	ks.st_atime.tv_sec = (s32) stat->atime.tv_sec;
	ks.st_atime.tv_nsec = stat->atime.tv_nsec;
	ks.st_mtime.tv_sec = (s32) stat->mtime.tv_sec;
	ks.st_mtime.tv_nsec = stat->mtime.tv_nsec;
	ks.st_ctime.tv_sec = (s32) stat->ctime.tv_sec;
	ks.st_ctime.tv_nsec = stat->ctime.tv_nsec;

	ks.st_blksize = (s32) stat->blksize;
	ks.st_blocks = (long long) stat->blocks;
	memset(ks.st_fstype, 0, 16);
	ks.st_pad4[0] = ks.st_pad4[1] = ks.st_pad4[2] = ks.st_pad4[3] = 0;
	ks.st_pad4[4] = ks.st_pad4[5] = ks.st_pad4[6] = ks.st_pad4[7] = 0;

	/* Now write it all back. */
	return copy_to_user(ubuf, &ks, sizeof(ks)) ? -EFAULT : 0;
}

asmlinkage int irix_xstat(int version, char __user *filename, struct stat __user *statbuf)
{
	int retval;
	struct kstat stat;

#ifdef DEBUG_XSTAT
	printk("[%s:%d] Wheee.. irix_xstat(%d,%s,%p) ",
	       current->comm, current->pid, version, filename, statbuf);
#endif

	retval = vfs_stat(filename, &stat);
	if (!retval) {
		switch(version) {
			case 2:
				retval = irix_xstat32_xlate(&stat, statbuf);
				break;
			case 3:
				retval = irix_xstat64_xlate(&stat, statbuf);
				break;
			default:
				retval = -EINVAL;
		}
	}
	return retval;
}

asmlinkage int irix_lxstat(int version, char __user *filename, struct stat __user *statbuf)
{
	int error;
	struct kstat stat;

#ifdef DEBUG_XSTAT
	printk("[%s:%d] Wheee.. irix_lxstat(%d,%s,%p) ",
	       current->comm, current->pid, version, filename, statbuf);
#endif

	error = vfs_lstat(filename, &stat);

	if (!error) {
		switch (version) {
			case 2:
				error = irix_xstat32_xlate(&stat, statbuf);
				break;
			case 3:
				error = irix_xstat64_xlate(&stat, statbuf);
				break;
			default:
				error = -EINVAL;
		}
	}
	return error;
}

asmlinkage int irix_fxstat(int version, int fd, struct stat __user *statbuf)
{
	int error;
	struct kstat stat;

#ifdef DEBUG_XSTAT
	printk("[%s:%d] Wheee.. irix_fxstat(%d,%d,%p) ",
	       current->comm, current->pid, version, fd, statbuf);
#endif

	error = vfs_fstat(fd, &stat);
	if (!error) {
		switch (version) {
			case 2:
				error = irix_xstat32_xlate(&stat, statbuf);
				break;
			case 3:
				error = irix_xstat64_xlate(&stat, statbuf);
				break;
			default:
				error = -EINVAL;
		}
	}
	return error;
}

asmlinkage int irix_xmknod(int ver, char __user *filename, int mode, unsigned dev)
{
	int retval;
	printk("[%s:%d] Wheee.. irix_xmknod(%d,%s,%x,%x)\n",
	       current->comm, current->pid, ver, filename, mode, dev);

	switch(ver) {
	case 2:
		/* shouldn't we convert here as well as on stat()? */
		retval = sys_mknod(filename, mode, dev);
		break;

	default:
		retval = -EINVAL;
		break;
	};

	return retval;
}

asmlinkage int irix_swapctl(int cmd, char __user *arg)
{
	printk("[%s:%d] Wheee.. irix_swapctl(%d,%p)\n",
	       current->comm, current->pid, cmd, arg);

	return -EINVAL;
}

struct irix_statvfs {
	u32 f_bsize; u32 f_frsize; u32 f_blocks;
	u32 f_bfree; u32 f_bavail; u32 f_files; u32 f_ffree; u32 f_favail;
	u32 f_fsid; char f_basetype[16];
	u32 f_flag; u32 f_namemax;
	char	f_fstr[32]; u32 f_filler[16];
};

asmlinkage int irix_statvfs(char __user *fname, struct irix_statvfs __user *buf)
{
	struct nameidata nd;
	struct kstatfs kbuf;
	int error, i;

	printk("[%s:%d] Wheee.. irix_statvfs(%s,%p)\n",
	       current->comm, current->pid, fname, buf);
	if (!access_ok(VERIFY_WRITE, buf, sizeof(struct irix_statvfs)))
		return -EFAULT;

	error = user_path_walk(fname, &nd);
	if (error)
		goto out;
	error = vfs_statfs(nd.dentry, &kbuf);
	if (error)
		goto dput_and_out;

	error |= __put_user(kbuf.f_bsize, &buf->f_bsize);
	error |= __put_user(kbuf.f_frsize, &buf->f_frsize);
	error |= __put_user(kbuf.f_blocks, &buf->f_blocks);
	error |= __put_user(kbuf.f_bfree, &buf->f_bfree);
	error |= __put_user(kbuf.f_bfree, &buf->f_bavail);  /* XXX hackety hack... */
	error |= __put_user(kbuf.f_files, &buf->f_files);
	error |= __put_user(kbuf.f_ffree, &buf->f_ffree);
	error |= __put_user(kbuf.f_ffree, &buf->f_favail);  /* XXX hackety hack... */
#ifdef __MIPSEB__
	error |= __put_user(kbuf.f_fsid.val[1], &buf->f_fsid);
#else
	error |= __put_user(kbuf.f_fsid.val[0], &buf->f_fsid);
#endif
	for (i = 0; i < 16; i++)
		error |= __put_user(0, &buf->f_basetype[i]);
	error |= __put_user(0, &buf->f_flag);
	error |= __put_user(kbuf.f_namelen, &buf->f_namemax);
	for (i = 0; i < 32; i++)
		error |= __put_user(0, &buf->f_fstr[i]);

dput_and_out:
	path_release(&nd);
out:
	return error;
}

asmlinkage int irix_fstatvfs(int fd, struct irix_statvfs __user *buf)
{
	struct kstatfs kbuf;
	struct file *file;
	int error, i;

	printk("[%s:%d] Wheee.. irix_fstatvfs(%d,%p)\n",
	       current->comm, current->pid, fd, buf);

	if (!access_ok(VERIFY_WRITE, buf, sizeof(struct irix_statvfs)))
		return -EFAULT;

	if (!(file = fget(fd))) {
		error = -EBADF;
		goto out;
	}
	error = vfs_statfs(file->f_path.dentry, &kbuf);
	if (error)
		goto out_f;

	error = __put_user(kbuf.f_bsize, &buf->f_bsize);
	error |= __put_user(kbuf.f_frsize, &buf->f_frsize);
	error |= __put_user(kbuf.f_blocks, &buf->f_blocks);
	error |= __put_user(kbuf.f_bfree, &buf->f_bfree);
	error |= __put_user(kbuf.f_bfree, &buf->f_bavail); /* XXX hackety hack... */
	error |= __put_user(kbuf.f_files, &buf->f_files);
	error |= __put_user(kbuf.f_ffree, &buf->f_ffree);
	error |= __put_user(kbuf.f_ffree, &buf->f_favail); /* XXX hackety hack... */
#ifdef __MIPSEB__
	error |= __put_user(kbuf.f_fsid.val[1], &buf->f_fsid);
#else
	error |= __put_user(kbuf.f_fsid.val[0], &buf->f_fsid);
#endif
	for(i = 0; i < 16; i++)
		error |= __put_user(0, &buf->f_basetype[i]);
	error |= __put_user(0, &buf->f_flag);
	error |= __put_user(kbuf.f_namelen, &buf->f_namemax);
	error |= __clear_user(&buf->f_fstr, sizeof(buf->f_fstr)) ? -EFAULT : 0;

out_f:
	fput(file);
out:
	return error;
}

asmlinkage int irix_priocntl(struct pt_regs *regs)
{
	printk("[%s:%d] Wheee.. irix_priocntl()\n",
	       current->comm, current->pid);

	return -EINVAL;
}

asmlinkage int irix_sigqueue(int pid, int sig, int code, int val)
{
	printk("[%s:%d] Wheee.. irix_sigqueue(%d,%d,%d,%d)\n",
	       current->comm, current->pid, pid, sig, code, val);

	return -EINVAL;
}

asmlinkage int irix_truncate64(char __user *name, int pad, int size1, int size2)
{
	int retval;

	if (size1) {
		retval = -EINVAL;
		goto out;
	}
	retval = sys_truncate(name, size2);

out:
	return retval;
}

asmlinkage int irix_ftruncate64(int fd, int pad, int size1, int size2)
{
	int retval;

	if (size1) {
		retval = -EINVAL;
		goto out;
	}
	retval = sys_ftruncate(fd, size2);

out:
	return retval;
}

asmlinkage int irix_mmap64(struct pt_regs *regs)
{
	int len, prot, flags, fd, off1, off2, error, base = 0;
	unsigned long addr, pgoff, *sp;
	struct file *file = NULL;
	int err;

	if (regs->regs[2] == 1000)
		base = 1;
	sp = (unsigned long *) (regs->regs[29] + 16);
	addr = regs->regs[base + 4];
	len = regs->regs[base + 5];
	prot = regs->regs[base + 6];
	if (!base) {
		flags = regs->regs[base + 7];
		if (!access_ok(VERIFY_READ, sp, (4 * sizeof(unsigned long))))
			return -EFAULT;
		fd = sp[0];
		err = __get_user(off1, &sp[1]);
		err |= __get_user(off2, &sp[2]);
	} else {
		if (!access_ok(VERIFY_READ, sp, (5 * sizeof(unsigned long))))
			return -EFAULT;
		err = __get_user(flags, &sp[0]);
		err |= __get_user(fd, &sp[1]);
		err |= __get_user(off1, &sp[2]);
		err |= __get_user(off2, &sp[3]);
	}

	if (err)
		return err;

	if (off1 & PAGE_MASK)
		return -EOVERFLOW;

	pgoff = (off1 << (32 - PAGE_SHIFT)) | (off2 >> PAGE_SHIFT);

	if (!(flags & MAP_ANONYMOUS)) {
		if (!(file = fget(fd)))
			return -EBADF;

		/* Ok, bad taste hack follows, try to think in something else
		   when reading this */
		if (flags & IRIX_MAP_AUTOGROW) {
			unsigned long old_pos;
			long max_size = off2 + len;

			if (max_size > file->f_path.dentry->d_inode->i_size) {
				old_pos = sys_lseek(fd, max_size - 1, 0);
				sys_write(fd, (void __user *) "", 1);
				sys_lseek(fd, old_pos, 0);
			}
		}
	}

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);

	return error;
}

asmlinkage int irix_dmi(struct pt_regs *regs)
{
	printk("[%s:%d] Wheee.. irix_dmi()\n",
	       current->comm, current->pid);

	return -EINVAL;
}

asmlinkage int irix_pread(int fd, char __user *buf, int cnt, int off64,
			  int off1, int off2)
{
	printk("[%s:%d] Wheee.. irix_pread(%d,%p,%d,%d,%d,%d)\n",
	       current->comm, current->pid, fd, buf, cnt, off64, off1, off2);

	return -EINVAL;
}

asmlinkage int irix_pwrite(int fd, char __user *buf, int cnt, int off64,
			   int off1, int off2)
{
	printk("[%s:%d] Wheee.. irix_pwrite(%d,%p,%d,%d,%d,%d)\n",
	       current->comm, current->pid, fd, buf, cnt, off64, off1, off2);

	return -EINVAL;
}

asmlinkage int irix_sgifastpath(int cmd, unsigned long arg0, unsigned long arg1,
				unsigned long arg2, unsigned long arg3,
				unsigned long arg4, unsigned long arg5)
{
	printk("[%s:%d] Wheee.. irix_fastpath(%d,%08lx,%08lx,%08lx,%08lx,"
	       "%08lx,%08lx)\n",
	       current->comm, current->pid, cmd, arg0, arg1, arg2,
	       arg3, arg4, arg5);

	return -EINVAL;
}

struct irix_statvfs64 {
	u32  f_bsize; u32 f_frsize;
	u64  f_blocks; u64 f_bfree; u64 f_bavail;
	u64  f_files; u64 f_ffree; u64 f_favail;
	u32  f_fsid;
	char f_basetype[16];
	u32  f_flag; u32 f_namemax;
	char f_fstr[32];
	u32  f_filler[16];
};

asmlinkage int irix_statvfs64(char __user *fname, struct irix_statvfs64 __user *buf)
{
	struct nameidata nd;
	struct kstatfs kbuf;
	int error, i;

	printk("[%s:%d] Wheee.. irix_statvfs64(%s,%p)\n",
	       current->comm, current->pid, fname, buf);
	if (!access_ok(VERIFY_WRITE, buf, sizeof(struct irix_statvfs64))) {
		error = -EFAULT;
		goto out;
	}

	error = user_path_walk(fname, &nd);
	if (error)
		goto out;
	error = vfs_statfs(nd.dentry, &kbuf);
	if (error)
		goto dput_and_out;

	error = __put_user(kbuf.f_bsize, &buf->f_bsize);
	error |= __put_user(kbuf.f_frsize, &buf->f_frsize);
	error |= __put_user(kbuf.f_blocks, &buf->f_blocks);
	error |= __put_user(kbuf.f_bfree, &buf->f_bfree);
	error |= __put_user(kbuf.f_bfree, &buf->f_bavail);  /* XXX hackety hack... */
	error |= __put_user(kbuf.f_files, &buf->f_files);
	error |= __put_user(kbuf.f_ffree, &buf->f_ffree);
	error |= __put_user(kbuf.f_ffree, &buf->f_favail);  /* XXX hackety hack... */
#ifdef __MIPSEB__
	error |= __put_user(kbuf.f_fsid.val[1], &buf->f_fsid);
#else
	error |= __put_user(kbuf.f_fsid.val[0], &buf->f_fsid);
#endif
	for(i = 0; i < 16; i++)
		error |= __put_user(0, &buf->f_basetype[i]);
	error |= __put_user(0, &buf->f_flag);
	error |= __put_user(kbuf.f_namelen, &buf->f_namemax);
	for(i = 0; i < 32; i++)
		error |= __put_user(0, &buf->f_fstr[i]);

dput_and_out:
	path_release(&nd);
out:
	return error;
}

asmlinkage int irix_fstatvfs64(int fd, struct irix_statvfs __user *buf)
{
	struct kstatfs kbuf;
	struct file *file;
	int error, i;

	printk("[%s:%d] Wheee.. irix_fstatvfs64(%d,%p)\n",
	       current->comm, current->pid, fd, buf);

	if (!access_ok(VERIFY_WRITE, buf, sizeof(struct irix_statvfs))) {
		error = -EFAULT;
		goto out;
	}
	if (!(file = fget(fd))) {
		error = -EBADF;
		goto out;
	}
	error = vfs_statfs(file->f_path.dentry, &kbuf);
	if (error)
		goto out_f;

	error = __put_user(kbuf.f_bsize, &buf->f_bsize);
	error |= __put_user(kbuf.f_frsize, &buf->f_frsize);
	error |= __put_user(kbuf.f_blocks, &buf->f_blocks);
	error |= __put_user(kbuf.f_bfree, &buf->f_bfree);
	error |= __put_user(kbuf.f_bfree, &buf->f_bavail);  /* XXX hackety hack... */
	error |= __put_user(kbuf.f_files, &buf->f_files);
	error |= __put_user(kbuf.f_ffree, &buf->f_ffree);
	error |= __put_user(kbuf.f_ffree, &buf->f_favail);  /* XXX hackety hack... */
#ifdef __MIPSEB__
	error |= __put_user(kbuf.f_fsid.val[1], &buf->f_fsid);
#else
	error |= __put_user(kbuf.f_fsid.val[0], &buf->f_fsid);
#endif
	for(i = 0; i < 16; i++)
		error |= __put_user(0, &buf->f_basetype[i]);
	error |= __put_user(0, &buf->f_flag);
	error |= __put_user(kbuf.f_namelen, &buf->f_namemax);
	error |= __clear_user(buf->f_fstr, sizeof(buf->f_fstr[i])) ? -EFAULT : 0;

out_f:
	fput(file);
out:
	return error;
}

asmlinkage int irix_getmountid(char __user *fname, unsigned long __user *midbuf)
{
	int err;

	printk("[%s:%d] irix_getmountid(%s, %p)\n",
	       current->comm, current->pid, fname, midbuf);
	if (!access_ok(VERIFY_WRITE, midbuf, (sizeof(unsigned long) * 4)))
		return -EFAULT;

	/*
	 * The idea with this system call is that when trying to determine
	 * 'pwd' and it's a toss-up for some reason, userland can use the
	 * fsid of the filesystem to try and make the right decision, but
	 * we don't have this so for now. XXX
	 */
	err = __put_user(0, &midbuf[0]);
	err |= __put_user(0, &midbuf[1]);
	err |= __put_user(0, &midbuf[2]);
	err |= __put_user(0, &midbuf[3]);

	return err;
}

asmlinkage int irix_nsproc(unsigned long entry, unsigned long mask,
			   unsigned long arg, unsigned long sp, int slen)
{
	printk("[%s:%d] Wheee.. irix_nsproc(%08lx,%08lx,%08lx,%08lx,%d)\n",
	       current->comm, current->pid, entry, mask, arg, sp, slen);

	return -EINVAL;
}

#undef DEBUG_GETDENTS

struct irix_dirent32 {
	u32  d_ino;
	u32  d_off;
	unsigned short  d_reclen;
	char d_name[1];
};

struct irix_dirent32_callback {
	struct irix_dirent32 __user *current_dir;
	struct irix_dirent32 __user *previous;
	int count;
	int error;
};

#define NAME_OFFSET32(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP32(x) (((x)+sizeof(u32)-1) & ~(sizeof(u32)-1))

static int irix_filldir32(void *__buf, const char *name,
	int namlen, loff_t offset, u64 ino, unsigned int d_type)
{
	struct irix_dirent32 __user *dirent;
	struct irix_dirent32_callback *buf = __buf;
	unsigned short reclen = ROUND_UP32(NAME_OFFSET32(dirent) + namlen + 1);
	int err = 0;
	u32 d_ino;

#ifdef DEBUG_GETDENTS
	printk("\nirix_filldir32[reclen<%d>namlen<%d>count<%d>]",
	       reclen, namlen, buf->count);
#endif
	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	d_ino = ino;
	if (sizeof(d_ino) < sizeof(ino) && d_ino != ino)
		return -EOVERFLOW;
	dirent = buf->previous;
	if (dirent)
		err = __put_user(offset, &dirent->d_off);
	dirent = buf->current_dir;
	err |= __put_user(dirent, &buf->previous);
	err |= __put_user(d_ino, &dirent->d_ino);
	err |= __put_user(reclen, &dirent->d_reclen);
	err |= copy_to_user((char __user *)dirent->d_name, name, namlen) ? -EFAULT : 0;
	err |= __put_user(0, &dirent->d_name[namlen]);
	dirent = (struct irix_dirent32 __user *) ((char __user *) dirent + reclen);

	buf->current_dir = dirent;
	buf->count -= reclen;

	return err;
}

asmlinkage int irix_ngetdents(unsigned int fd, void __user * dirent,
	unsigned int count, int __user *eob)
{
	struct file *file;
	struct irix_dirent32 __user *lastdirent;
	struct irix_dirent32_callback buf;
	int error;

#ifdef DEBUG_GETDENTS
	printk("[%s:%d] ngetdents(%d, %p, %d, %p) ", current->comm,
	       current->pid, fd, dirent, count, eob);
#endif
	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = (struct irix_dirent32 __user *) dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, irix_filldir32, &buf);
	if (error < 0)
		goto out_putf;

	error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		put_user(file->f_pos, &lastdirent->d_off);
		error = count - buf.count;
	}

	if (put_user(0, eob) < 0) {
		error = -EFAULT;
		goto out_putf;
	}

#ifdef DEBUG_GETDENTS
	printk("eob=%d returning %d\n", *eob, count - buf.count);
#endif
	error = count - buf.count;

out_putf:
	fput(file);
out:
	return error;
}

struct irix_dirent64 {
	u64            d_ino;
	u64            d_off;
	unsigned short d_reclen;
	char           d_name[1];
};

struct irix_dirent64_callback {
	struct irix_dirent64 __user *curr;
	struct irix_dirent64 __user *previous;
	int count;
	int error;
};

#define NAME_OFFSET64(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP64(x) (((x)+sizeof(u64)-1) & ~(sizeof(u64)-1))

static int irix_filldir64(void *__buf, const char *name,
	int namlen, loff_t offset, u64 ino, unsigned int d_type)
{
	struct irix_dirent64 __user *dirent;
	struct irix_dirent64_callback * buf = __buf;
	unsigned short reclen = ROUND_UP64(NAME_OFFSET64(dirent) + namlen + 1);
	int err = 0;

	if (!access_ok(VERIFY_WRITE, buf, sizeof(*buf)))
		return -EFAULT;

	if (__put_user(-EINVAL, &buf->error))	/* only used if we fail.. */
		return -EFAULT;
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	if (dirent)
		err = __put_user(offset, &dirent->d_off);
	dirent = buf->curr;
	buf->previous = dirent;
	err |= __put_user(ino, &dirent->d_ino);
	err |= __put_user(reclen, &dirent->d_reclen);
	err |= __copy_to_user((char __user *)dirent->d_name, name, namlen)
	       ? -EFAULT : 0;
	err |= __put_user(0, &dirent->d_name[namlen]);

	dirent = (struct irix_dirent64 __user *) ((char __user *) dirent + reclen);

	buf->curr = dirent;
	buf->count -= reclen;

	return err;
}

asmlinkage int irix_getdents64(int fd, void __user *dirent, int cnt)
{
	struct file *file;
	struct irix_dirent64 __user *lastdirent;
	struct irix_dirent64_callback buf;
	int error;

#ifdef DEBUG_GETDENTS
	printk("[%s:%d] getdents64(%d, %p, %d) ", current->comm,
	       current->pid, fd, dirent, cnt);
#endif
	error = -EBADF;
	if (!(file = fget(fd)))
		goto out;

	error = -EFAULT;
	if (!access_ok(VERIFY_WRITE, dirent, cnt))
		goto out_f;

	error = -EINVAL;
	if (cnt < (sizeof(struct irix_dirent64) + 255))
		goto out_f;

	buf.curr = (struct irix_dirent64 __user *) dirent;
	buf.previous = NULL;
	buf.count = cnt;
	buf.error = 0;
	error = vfs_readdir(file, irix_filldir64, &buf);
	if (error < 0)
		goto out_f;
	lastdirent = buf.previous;
	if (!lastdirent) {
		error = buf.error;
		goto out_f;
	}
	if (put_user(file->f_pos, &lastdirent->d_off))
		return -EFAULT;
#ifdef DEBUG_GETDENTS
	printk("returning %d\n", cnt - buf.count);
#endif
	error = cnt - buf.count;

out_f:
	fput(file);
out:
	return error;
}

asmlinkage int irix_ngetdents64(int fd, void __user *dirent, int cnt, int *eob)
{
	struct file *file;
	struct irix_dirent64 __user *lastdirent;
	struct irix_dirent64_callback buf;
	int error;

#ifdef DEBUG_GETDENTS
	printk("[%s:%d] ngetdents64(%d, %p, %d) ", current->comm,
	       current->pid, fd, dirent, cnt);
#endif
	error = -EBADF;
	if (!(file = fget(fd)))
		goto out;

	error = -EFAULT;
	if (!access_ok(VERIFY_WRITE, dirent, cnt) ||
	    !access_ok(VERIFY_WRITE, eob, sizeof(*eob)))
		goto out_f;

	error = -EINVAL;
	if (cnt < (sizeof(struct irix_dirent64) + 255))
		goto out_f;

	*eob = 0;
	buf.curr = (struct irix_dirent64 __user *) dirent;
	buf.previous = NULL;
	buf.count = cnt;
	buf.error = 0;
	error = vfs_readdir(file, irix_filldir64, &buf);
	if (error < 0)
		goto out_f;
	lastdirent = buf.previous;
	if (!lastdirent) {
		error = buf.error;
		goto out_f;
	}
	if (put_user(file->f_pos, &lastdirent->d_off))
		return -EFAULT;
#ifdef DEBUG_GETDENTS
	printk("eob=%d returning %d\n", *eob, cnt - buf.count);
#endif
	error = cnt - buf.count;

out_f:
	fput(file);
out:
	return error;
}

asmlinkage int irix_uadmin(unsigned long op, unsigned long func, unsigned long arg)
{
	int retval;

	switch (op) {
	case 1:
		/* Reboot */
		printk("[%s:%d] irix_uadmin: Wants to reboot...\n",
		       current->comm, current->pid);
		retval = -EINVAL;
		goto out;

	case 2:
		/* Shutdown */
		printk("[%s:%d] irix_uadmin: Wants to shutdown...\n",
		       current->comm, current->pid);
		retval = -EINVAL;
		goto out;

	case 4:
		/* Remount-root */
		printk("[%s:%d] irix_uadmin: Wants to remount root...\n",
		       current->comm, current->pid);
		retval = -EINVAL;
		goto out;

	case 8:
		/* Kill all tasks. */
		printk("[%s:%d] irix_uadmin: Wants to kill all tasks...\n",
		       current->comm, current->pid);
		retval = -EINVAL;
		goto out;

	case 256:
		/* Set magic mushrooms... */
		printk("[%s:%d] irix_uadmin: Wants to set magic mushroom[%d]...\n",
		       current->comm, current->pid, (int) func);
		retval = -EINVAL;
		goto out;

	default:
		printk("[%s:%d] irix_uadmin: Unknown operation [%d]...\n",
		       current->comm, current->pid, (int) op);
		retval = -EINVAL;
		goto out;
	};

out:
	return retval;
}

asmlinkage int irix_utssys(char __user *inbuf, int arg, int type, char __user *outbuf)
{
	int retval;

	switch(type) {
	case 0:
		/* uname() */
		retval = irix_uname((struct iuname __user *)inbuf);
		goto out;

	case 2:
		/* ustat() */
		printk("[%s:%d] irix_utssys: Wants to do ustat()\n",
		       current->comm, current->pid);
		retval = -EINVAL;
		goto out;

	case 3:
		/* fusers() */
		printk("[%s:%d] irix_utssys: Wants to do fusers()\n",
		       current->comm, current->pid);
		retval = -EINVAL;
		goto out;

	default:
		printk("[%s:%d] irix_utssys: Wants to do unknown type[%d]\n",
		       current->comm, current->pid, (int) type);
		retval = -EINVAL;
		goto out;
	}

out:
	return retval;
}

#undef DEBUG_FCNTL

#define IRIX_F_ALLOCSP 10

asmlinkage int irix_fcntl(int fd, int cmd, int arg)
{
	int retval;

#ifdef DEBUG_FCNTL
	printk("[%s:%d] irix_fcntl(%d, %d, %d) ", current->comm,
	       current->pid, fd, cmd, arg);
#endif
	if (cmd == IRIX_F_ALLOCSP){
		return 0;
	}
	retval = sys_fcntl(fd, cmd, arg);
#ifdef DEBUG_FCNTL
	printk("%d\n", retval);
#endif
	return retval;
}

asmlinkage int irix_ulimit(int cmd, int arg)
{
	int retval;

	switch(cmd) {
	case 1:
		printk("[%s:%d] irix_ulimit: Wants to get file size limit.\n",
		       current->comm, current->pid);
		retval = -EINVAL;
		goto out;

	case 2:
		printk("[%s:%d] irix_ulimit: Wants to set file size limit.\n",
		       current->comm, current->pid);
		retval = -EINVAL;
		goto out;

	case 3:
		printk("[%s:%d] irix_ulimit: Wants to get brk limit.\n",
		       current->comm, current->pid);
		retval = -EINVAL;
		goto out;

	case 4:
#if 0
		printk("[%s:%d] irix_ulimit: Wants to get fd limit.\n",
		       current->comm, current->pid);
		retval = -EINVAL;
		goto out;
#endif
		retval = current->signal->rlim[RLIMIT_NOFILE].rlim_cur;
		goto out;

	case 5:
		printk("[%s:%d] irix_ulimit: Wants to get txt offset.\n",
		       current->comm, current->pid);
		retval = -EINVAL;
		goto out;

	default:
		printk("[%s:%d] irix_ulimit: Unknown command [%d].\n",
		       current->comm, current->pid, cmd);
		retval = -EINVAL;
		goto out;
	}
out:
	return retval;
}

asmlinkage int irix_unimp(struct pt_regs *regs)
{
	printk("irix_unimp [%s:%d] v0=%d v1=%d a0=%08lx a1=%08lx a2=%08lx "
	       "a3=%08lx\n", current->comm, current->pid,
	       (int) regs->regs[2], (int) regs->regs[3],
	       regs->regs[4], regs->regs[5], regs->regs[6], regs->regs[7]);

	return -ENOSYS;
}
