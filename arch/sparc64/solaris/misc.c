/* $Id: misc.c,v 1.36 2002/02/09 19:49:31 davem Exp $
 * misc.c: Miscellaneous syscall emulation for Solaris
 *
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/config.h>
#include <linux/module.h> 
#include <linux/types.h>
#include <linux/smp_lock.h>
#include <linux/utsname.h>
#include <linux/limits.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/timex.h>
#include <linux/major.h>
#include <linux/compat.h>

#include <asm/uaccess.h>
#include <asm/string.h>
#include <asm/oplib.h>
#include <asm/idprom.h>
#include <asm/smp.h>
#include <asm/prom.h>

#include "conv.h"

/* Conversion from Linux to Solaris errnos. 0-34 are identity mapped.
   Some Linux errnos (EPROCLIM, EDOTDOT, ERREMOTE, EUCLEAN, ENOTNAM, 
   ENAVAIL, EISNAM, EREMOTEIO, ENOMEDIUM, EMEDIUMTYPE) have no Solaris
   equivalents. I return EINVAL in that case, which is very wrong. If
   someone suggest a better value for them, you're welcomed.
   On the other side, Solaris ECANCELED and ENOTSUP have no Linux equivalents,
   but that doesn't matter here. --jj */
int solaris_err_table[] = {
/* 0 */  0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
/* 10 */  10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
/* 20 */  20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
/* 30 */  30, 31, 32, 33, 34, 22, 150, 149, 95, 96,
/* 40 */  97, 98, 99, 120, 121, 122, 123, 124, 125, 126, 
/* 50 */ 127, 128, 129, 130, 131, 132, 133, 134, 143, 144,
/* 60 */ 145, 146, 90, 78, 147, 148, 93, 22, 94, 49,
/* 70 */ 151, 66, 60, 62, 63, 35, 77, 36, 45, 46, 
/* 80 */ 64, 22, 67, 68, 69, 70, 71, 74, 22, 82, 
/* 90 */ 89, 92, 79, 81, 37, 38, 39, 40, 41, 42,
/* 100 */ 43, 44, 50, 51, 52, 53, 54, 55, 56, 57,
/* 110 */ 87, 61, 84, 65, 83, 80, 91, 22, 22, 22,
/* 120 */ 22, 22, 88, 86, 85, 22, 22,
};

#define SOLARIS_NR_OPEN	256

static u32 do_solaris_mmap(u32 addr, u32 len, u32 prot, u32 flags, u32 fd, u64 off)
{
	struct file *file = NULL;
	unsigned long retval, ret_type;

	/* Do we need it here? */
	set_personality(PER_SVR4);
	if (flags & MAP_NORESERVE) {
		static int cnt;
		
		if (cnt < 5) {
			printk("%s:  unimplemented Solaris MAP_NORESERVE mmap() flag\n",
			       current->comm);
			cnt++;
		}
		flags &= ~MAP_NORESERVE;
	}
	retval = -EBADF;
	if(!(flags & MAP_ANONYMOUS)) {
		if(fd >= SOLARIS_NR_OPEN)
			goto out;
 		file = fget(fd);
		if (!file)
			goto out;
		else {
			struct inode * inode = file->f_dentry->d_inode;
			if(imajor(inode) == MEM_MAJOR &&
			   iminor(inode) == 5) {
				flags |= MAP_ANONYMOUS;
				fput(file);
				file = NULL;
			}
		}
	}

	retval = -EINVAL;
	len = PAGE_ALIGN(len);
	if(!(flags & MAP_FIXED))
		addr = 0;
	else if (len > STACK_TOP32 || addr > STACK_TOP32 - len)
		goto out_putf;
	ret_type = flags & _MAP_NEW;
	flags &= ~_MAP_NEW;

	down_write(&current->mm->mmap_sem);
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	retval = do_mmap(file,
			 (unsigned long) addr, (unsigned long) len,
			 (unsigned long) prot, (unsigned long) flags, off);
	up_write(&current->mm->mmap_sem);
	if(!ret_type)
		retval = ((retval < STACK_TOP32) ? 0 : retval);
	                        
out_putf:
	if (file)
		fput(file);
out:
	return (u32) retval;
}

asmlinkage u32 solaris_mmap(u32 addr, u32 len, u32 prot, u32 flags, u32 fd, u32 off)
{
	return do_solaris_mmap(addr, len, prot, flags, fd, (u64) off);
}

asmlinkage u32 solaris_mmap64(struct pt_regs *regs, u32 len, u32 prot, u32 flags, u32 fd, u32 offhi)
{
	u32 offlo;
	
	if (regs->u_regs[UREG_G1]) {
		if (get_user (offlo, (u32 __user *)(long)((u32)regs->u_regs[UREG_I6] + 0x5c)))
			return -EFAULT;
	} else {
		if (get_user (offlo, (u32 __user *)(long)((u32)regs->u_regs[UREG_I6] + 0x60)))
			return -EFAULT;
	}
	return do_solaris_mmap((u32)regs->u_regs[UREG_I0], len, prot, flags, fd, (((u64)offhi)<<32)|offlo);
}

asmlinkage int solaris_brk(u32 brk)
{
	int (*sunos_brk)(u32) = (int (*)(u32))SUNOS(17);
	
	return sunos_brk(brk);
}

static int __set_utsfield(char __user *to, int to_size,
			  const char *from, int from_size,
			  int dotchop, int countfrom)
{
	int len = countfrom ? (to_size > from_size ?
			       from_size : to_size) : to_size;
	int off;

	if (copy_to_user(to, from, len))
		return -EFAULT;

	off = len < to_size? len: len - 1;
	if (dotchop) {
		const char *p = strnchr(from, len, '.');
		if (p) off = p - from;
	}

	if (__put_user('\0', to + off))
		return -EFAULT;

	return 0;
}

#define set_utsfield(to, from, dotchop, countfrom) \
	__set_utsfield((to), sizeof(to), \
		       (from), sizeof(from), \
		       (dotchop), (countfrom))

struct sol_uname {
	char sysname[9];
	char nodename[9];
	char release[9];
	char version[9];
	char machine[9];
};

struct sol_utsname {
	char sysname[257];
	char nodename[257];
	char release[257];
	char version[257];
	char machine[257];
};

static char *machine(void)
{
	switch (sparc_cpu_model) {
	case sun4: return "sun4";
	case sun4c: return "sun4c";
	case sun4e: return "sun4e";
	case sun4m: return "sun4m";
	case sun4d: return "sun4d";
	case sun4u: return "sun4u";
	default: return "sparc";
	}
}

static char *platform(char *buffer, int sz)
{
	struct device_node *dp = of_find_node_by_path("/");
	int len;

	*buffer = 0;
	len = strlen(dp->name);
	if (len > sz)
		len = sz;
	memcpy(buffer, dp->name, len);
	buffer[len] = 0;
	if (*buffer) {
		char *p;

		for (p = buffer; *p; p++)
			if (*p == '/' || *p == ' ') *p = '_';
		return buffer;
	}

	return "sun4u";
}

static char *serial(char *buffer, int sz)
{
	struct device_node *dp = of_find_node_by_path("/options");
	int len;

	*buffer = 0;
	if (dp) {
		char *val = of_get_property(dp, "system-board-serial#", &len);

		if (val && len > 0) {
			if (len > sz)
				len = sz;
			memcpy(buffer, val, len);
			buffer[len] = 0;
		}
	}
	if (!*buffer)
		return "4512348717234";
	else
		return buffer;
}

asmlinkage int solaris_utssys(u32 buf, u32 flags, int which, u32 buf2)
{
	struct sol_uname __user *v = A(buf);
	int err;

	switch (which) {
	case 0:	/* old uname */
		/* Let's cheat */
		err  = set_utsfield(v->sysname, "SunOS", 1, 0);
		down_read(&uts_sem);
		err |= set_utsfield(v->nodename, system_utsname.nodename,
				    1, 1);
		up_read(&uts_sem);
		err |= set_utsfield(v->release, "2.6", 0, 0);
		err |= set_utsfield(v->version, "Generic", 0, 0);
		err |= set_utsfield(v->machine, machine(), 0, 0);
		return (err ? -EFAULT : 0);
	case 2: /* ustat */
		return -ENOSYS;
	case 3: /* fusers */
		return -ENOSYS;
	default:
		return -ENOSYS;
	}
}

asmlinkage int solaris_utsname(u32 buf)
{
	struct sol_utsname __user *v = A(buf);
	int err;

	/* Why should we not lie a bit? */
	down_read(&uts_sem);
	err  = set_utsfield(v->sysname, "SunOS", 0, 0);
	err |= set_utsfield(v->nodename, system_utsname.nodename, 1, 1);
	err |= set_utsfield(v->release, "5.6", 0, 0);
	err |= set_utsfield(v->version, "Generic", 0, 0);
	err |= set_utsfield(v->machine, machine(), 0, 0);
	up_read(&uts_sem);

	return (err ? -EFAULT : 0);
}

#define SI_SYSNAME		1       /* return name of operating system */
#define SI_HOSTNAME		2       /* return name of node */
#define SI_RELEASE		3       /* return release of operating system */
#define SI_VERSION		4       /* return version field of utsname */
#define SI_MACHINE		5       /* return kind of machine */
#define SI_ARCHITECTURE		6       /* return instruction set arch */
#define SI_HW_SERIAL		7       /* return hardware serial number */
#define SI_HW_PROVIDER		8       /* return hardware manufacturer */
#define SI_SRPC_DOMAIN		9       /* return secure RPC domain */
#define SI_PLATFORM		513     /* return platform identifier */

asmlinkage int solaris_sysinfo(int cmd, u32 buf, s32 count)
{
	char *p, *q, *r;
	char buffer[256];
	int len;
	
	/* Again, we cheat :)) */
	switch (cmd) {
	case SI_SYSNAME: r = "SunOS"; break;
	case SI_HOSTNAME:
		r = buffer + 256;
		down_read(&uts_sem);
		for (p = system_utsname.nodename, q = buffer; 
		     q < r && *p && *p != '.'; *q++ = *p++);
		up_read(&uts_sem);
		*q = 0;
		r = buffer;
		break;
	case SI_RELEASE: r = "5.6"; break;
	case SI_MACHINE: r = machine(); break;
	case SI_ARCHITECTURE: r = "sparc"; break;
	case SI_HW_PROVIDER: r = "Sun_Microsystems"; break;
	case SI_HW_SERIAL: r = serial(buffer, sizeof(buffer)); break;
	case SI_PLATFORM: r = platform(buffer, sizeof(buffer)); break;
	case SI_SRPC_DOMAIN: r = ""; break;
	case SI_VERSION: r = "Generic"; break;
	default: return -EINVAL;
	}
	len = strlen(r) + 1;
	if (count < len) {
		if (copy_to_user(A(buf), r, count - 1) ||
		    __put_user(0, (char __user *)A(buf) + count - 1))
			return -EFAULT;
	} else {
		if (copy_to_user(A(buf), r, len))
			return -EFAULT;
	}
	return len;
}

#define	SOLARIS_CONFIG_NGROUPS			2
#define	SOLARIS_CONFIG_CHILD_MAX		3
#define	SOLARIS_CONFIG_OPEN_FILES		4
#define	SOLARIS_CONFIG_POSIX_VER		5
#define	SOLARIS_CONFIG_PAGESIZE			6
#define	SOLARIS_CONFIG_CLK_TCK			7
#define	SOLARIS_CONFIG_XOPEN_VER		8
#define	SOLARIS_CONFIG_PROF_TCK			10
#define	SOLARIS_CONFIG_NPROC_CONF		11
#define	SOLARIS_CONFIG_NPROC_ONLN		12
#define	SOLARIS_CONFIG_AIO_LISTIO_MAX		13
#define	SOLARIS_CONFIG_AIO_MAX			14
#define	SOLARIS_CONFIG_AIO_PRIO_DELTA_MAX	15
#define	SOLARIS_CONFIG_DELAYTIMER_MAX		16
#define	SOLARIS_CONFIG_MQ_OPEN_MAX		17
#define	SOLARIS_CONFIG_MQ_PRIO_MAX		18
#define	SOLARIS_CONFIG_RTSIG_MAX		19
#define	SOLARIS_CONFIG_SEM_NSEMS_MAX		20
#define	SOLARIS_CONFIG_SEM_VALUE_MAX		21
#define	SOLARIS_CONFIG_SIGQUEUE_MAX		22
#define	SOLARIS_CONFIG_SIGRT_MIN		23
#define	SOLARIS_CONFIG_SIGRT_MAX		24
#define	SOLARIS_CONFIG_TIMER_MAX		25
#define	SOLARIS_CONFIG_PHYS_PAGES		26
#define	SOLARIS_CONFIG_AVPHYS_PAGES		27

asmlinkage int solaris_sysconf(int id)
{
	switch (id) {
	case SOLARIS_CONFIG_NGROUPS:	return NGROUPS_MAX;
	case SOLARIS_CONFIG_CHILD_MAX:	return -1; /* no limit */
	case SOLARIS_CONFIG_OPEN_FILES:	return OPEN_MAX;
	case SOLARIS_CONFIG_POSIX_VER:	return 199309;
	case SOLARIS_CONFIG_PAGESIZE:	return PAGE_SIZE;
	case SOLARIS_CONFIG_XOPEN_VER:	return 3;
	case SOLARIS_CONFIG_CLK_TCK:
	case SOLARIS_CONFIG_PROF_TCK:
		return sparc64_get_clock_tick(smp_processor_id());
#ifdef CONFIG_SMP	
	case SOLARIS_CONFIG_NPROC_CONF:	return NR_CPUS;
	case SOLARIS_CONFIG_NPROC_ONLN:	return num_online_cpus();
#else
	case SOLARIS_CONFIG_NPROC_CONF:	return 1;
	case SOLARIS_CONFIG_NPROC_ONLN:	return 1;
#endif
	case SOLARIS_CONFIG_SIGRT_MIN:		return 37;
	case SOLARIS_CONFIG_SIGRT_MAX:		return 44;
	case SOLARIS_CONFIG_PHYS_PAGES:
	case SOLARIS_CONFIG_AVPHYS_PAGES:
		{
			struct sysinfo s;
			
			si_meminfo(&s);
			if (id == SOLARIS_CONFIG_PHYS_PAGES)
				return s.totalram >>= PAGE_SHIFT;
			else
				return s.freeram >>= PAGE_SHIFT;
		}
	/* XXX support these as well -jj */
	case SOLARIS_CONFIG_AIO_LISTIO_MAX:	return -EINVAL;
	case SOLARIS_CONFIG_AIO_MAX:		return -EINVAL;
	case SOLARIS_CONFIG_AIO_PRIO_DELTA_MAX:	return -EINVAL;
	case SOLARIS_CONFIG_DELAYTIMER_MAX:	return -EINVAL;
	case SOLARIS_CONFIG_MQ_OPEN_MAX:	return -EINVAL;
	case SOLARIS_CONFIG_MQ_PRIO_MAX:	return -EINVAL;
	case SOLARIS_CONFIG_RTSIG_MAX:		return -EINVAL;
	case SOLARIS_CONFIG_SEM_NSEMS_MAX:	return -EINVAL;
	case SOLARIS_CONFIG_SEM_VALUE_MAX:	return -EINVAL;
	case SOLARIS_CONFIG_SIGQUEUE_MAX:	return -EINVAL;
	case SOLARIS_CONFIG_TIMER_MAX:		return -EINVAL;
	default: return -EINVAL;
	}
}

asmlinkage int solaris_procids(int cmd, s32 pid, s32 pgid)
{
	int ret;
	
	switch (cmd) {
	case 0: /* getpgrp */
		return process_group(current);
	case 1: /* setpgrp */
		{
			int (*sys_setpgid)(pid_t,pid_t) =
				(int (*)(pid_t,pid_t))SYS(setpgid);
				
			/* can anyone explain me the difference between
			   Solaris setpgrp and setsid? */
			ret = sys_setpgid(0, 0);
			if (ret) return ret;
			current->signal->tty = NULL;
			return process_group(current);
		}
	case 2: /* getsid */
		{
			int (*sys_getsid)(pid_t) = (int (*)(pid_t))SYS(getsid);
			return sys_getsid(pid);
		}
	case 3: /* setsid */
		{
			int (*sys_setsid)(void) = (int (*)(void))SYS(setsid);
			return sys_setsid();
		}
	case 4: /* getpgid */
		{
			int (*sys_getpgid)(pid_t) = (int (*)(pid_t))SYS(getpgid);
			return sys_getpgid(pid);
		}
	case 5: /* setpgid */
		{
			int (*sys_setpgid)(pid_t,pid_t) = 
				(int (*)(pid_t,pid_t))SYS(setpgid);
			return sys_setpgid(pid,pgid);
		}
	}
	return -EINVAL;
}

asmlinkage int solaris_gettimeofday(u32 tim)
{
	int (*sys_gettimeofday)(struct timeval *, struct timezone *) =
		(int (*)(struct timeval *, struct timezone *))SYS(gettimeofday);
		
	return sys_gettimeofday((struct timeval *)(u64)tim, NULL);
}

#define RLIM_SOL_INFINITY32	0x7fffffff
#define RLIM_SOL_SAVED_MAX32	0x7ffffffe
#define RLIM_SOL_SAVED_CUR32	0x7ffffffd
#define RLIM_SOL_INFINITY	((u64)-3)
#define RLIM_SOL_SAVED_MAX	((u64)-2)
#define RLIM_SOL_SAVED_CUR	((u64)-1)
#define RESOURCE32(x) ((x > RLIM_INFINITY32) ? RLIM_INFINITY32 : x)
#define RLIMIT_SOL_NOFILE	5
#define RLIMIT_SOL_VMEM		6

struct rlimit32 {
	u32	rlim_cur;
	u32	rlim_max;
};

asmlinkage int solaris_getrlimit(unsigned int resource, struct rlimit32 __user *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs ();
	int (*sys_getrlimit)(unsigned int, struct rlimit *) =
		(int (*)(unsigned int, struct rlimit *))SYS(getrlimit);

	if (resource > RLIMIT_SOL_VMEM)
		return -EINVAL;	
	switch (resource) {
	case RLIMIT_SOL_NOFILE: resource = RLIMIT_NOFILE; break;
	case RLIMIT_SOL_VMEM: resource = RLIMIT_AS; break;
	default: break;
	}
	set_fs (KERNEL_DS);
	ret = sys_getrlimit(resource, &r);
	set_fs (old_fs);
	if (!ret) {
		if (r.rlim_cur == RLIM_INFINITY)
			r.rlim_cur = RLIM_SOL_INFINITY32;
		else if ((u64)r.rlim_cur > RLIM_SOL_INFINITY32)
			r.rlim_cur = RLIM_SOL_SAVED_CUR32;
		if (r.rlim_max == RLIM_INFINITY)
			r.rlim_max = RLIM_SOL_INFINITY32;
		else if ((u64)r.rlim_max > RLIM_SOL_INFINITY32)
			r.rlim_max = RLIM_SOL_SAVED_MAX32;
		ret = put_user (r.rlim_cur, &rlim->rlim_cur);
		ret |= __put_user (r.rlim_max, &rlim->rlim_max);
	}
	return ret;
}

asmlinkage int solaris_setrlimit(unsigned int resource, struct rlimit32 __user *rlim)
{
	struct rlimit r, rold;
	int ret;
	mm_segment_t old_fs = get_fs ();
	int (*sys_getrlimit)(unsigned int, struct rlimit __user *) =
		(int (*)(unsigned int, struct rlimit __user *))SYS(getrlimit);
	int (*sys_setrlimit)(unsigned int, struct rlimit __user *) =
		(int (*)(unsigned int, struct rlimit __user *))SYS(setrlimit);

	if (resource > RLIMIT_SOL_VMEM)
		return -EINVAL;	
	switch (resource) {
	case RLIMIT_SOL_NOFILE: resource = RLIMIT_NOFILE; break;
	case RLIMIT_SOL_VMEM: resource = RLIMIT_AS; break;
	default: break;
	}
	if (get_user (r.rlim_cur, &rlim->rlim_cur) ||
	    __get_user (r.rlim_max, &rlim->rlim_max))
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_getrlimit(resource, &rold);
	if (!ret) {
		if (r.rlim_cur == RLIM_SOL_INFINITY32)
			r.rlim_cur = RLIM_INFINITY;
		else if (r.rlim_cur == RLIM_SOL_SAVED_CUR32)
			r.rlim_cur = rold.rlim_cur;
		else if (r.rlim_cur == RLIM_SOL_SAVED_MAX32)
			r.rlim_cur = rold.rlim_max;
		if (r.rlim_max == RLIM_SOL_INFINITY32)
			r.rlim_max = RLIM_INFINITY;
		else if (r.rlim_max == RLIM_SOL_SAVED_CUR32)
			r.rlim_max = rold.rlim_cur;
		else if (r.rlim_max == RLIM_SOL_SAVED_MAX32)
			r.rlim_max = rold.rlim_max;
		ret = sys_setrlimit(resource, &r);
	}
	set_fs (old_fs);
	return ret;
}

asmlinkage int solaris_getrlimit64(unsigned int resource, struct rlimit __user *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs ();
	int (*sys_getrlimit)(unsigned int, struct rlimit __user *) =
		(int (*)(unsigned int, struct rlimit __user *))SYS(getrlimit);

	if (resource > RLIMIT_SOL_VMEM)
		return -EINVAL;	
	switch (resource) {
	case RLIMIT_SOL_NOFILE: resource = RLIMIT_NOFILE; break;
	case RLIMIT_SOL_VMEM: resource = RLIMIT_AS; break;
	default: break;
	}
	set_fs (KERNEL_DS);
	ret = sys_getrlimit(resource, &r);
	set_fs (old_fs);
	if (!ret) {
		if (r.rlim_cur == RLIM_INFINITY)
			r.rlim_cur = RLIM_SOL_INFINITY;
		if (r.rlim_max == RLIM_INFINITY)
			r.rlim_max = RLIM_SOL_INFINITY;
		ret = put_user (r.rlim_cur, &rlim->rlim_cur);
		ret |= __put_user (r.rlim_max, &rlim->rlim_max);
	}
	return ret;
}

asmlinkage int solaris_setrlimit64(unsigned int resource, struct rlimit __user *rlim)
{
	struct rlimit r, rold;
	int ret;
	mm_segment_t old_fs = get_fs ();
	int (*sys_getrlimit)(unsigned int, struct rlimit __user *) =
		(int (*)(unsigned int, struct rlimit __user *))SYS(getrlimit);
	int (*sys_setrlimit)(unsigned int, struct rlimit __user *) =
		(int (*)(unsigned int, struct rlimit __user *))SYS(setrlimit);

	if (resource > RLIMIT_SOL_VMEM)
		return -EINVAL;	
	switch (resource) {
	case RLIMIT_SOL_NOFILE: resource = RLIMIT_NOFILE; break;
	case RLIMIT_SOL_VMEM: resource = RLIMIT_AS; break;
	default: break;
	}
	if (get_user (r.rlim_cur, &rlim->rlim_cur) ||
	    __get_user (r.rlim_max, &rlim->rlim_max))
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_getrlimit(resource, &rold);
	if (!ret) {
		if (r.rlim_cur == RLIM_SOL_INFINITY)
			r.rlim_cur = RLIM_INFINITY;
		else if (r.rlim_cur == RLIM_SOL_SAVED_CUR)
			r.rlim_cur = rold.rlim_cur;
		else if (r.rlim_cur == RLIM_SOL_SAVED_MAX)
			r.rlim_cur = rold.rlim_max;
		if (r.rlim_max == RLIM_SOL_INFINITY)
			r.rlim_max = RLIM_INFINITY;
		else if (r.rlim_max == RLIM_SOL_SAVED_CUR)
			r.rlim_max = rold.rlim_cur;
		else if (r.rlim_max == RLIM_SOL_SAVED_MAX)
			r.rlim_max = rold.rlim_max;
		ret = sys_setrlimit(resource, &r);
	}
	set_fs (old_fs);
	return ret;
}

struct sol_ntptimeval {
	struct compat_timeval time;
	s32 maxerror;
	s32 esterror;
};

struct sol_timex {
	u32 modes;
	s32 offset;
	s32 freq;
	s32 maxerror;
	s32 esterror;
	s32 status;
	s32 constant;
	s32 precision;
	s32 tolerance;
	s32 ppsfreq;
	s32 jitter;
	s32 shift;
	s32 stabil;
	s32 jitcnt;
	s32 calcnt;
	s32 errcnt;
	s32 stbcnt;
};

asmlinkage int solaris_ntp_gettime(struct sol_ntptimeval __user *ntp)
{
	int (*sys_adjtimex)(struct timex __user *) =
		(int (*)(struct timex __user *))SYS(adjtimex);
	struct timex t;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	set_fs(KERNEL_DS);
	t.modes = 0;
	ret = sys_adjtimex(&t);
	set_fs(old_fs);
	if (ret < 0)
		return ret;
	ret = put_user (t.time.tv_sec, &ntp->time.tv_sec);
	ret |= __put_user (t.time.tv_usec, &ntp->time.tv_usec);
	ret |= __put_user (t.maxerror, &ntp->maxerror);
	ret |= __put_user (t.esterror, &ntp->esterror);
	return ret;	                        
}

asmlinkage int solaris_ntp_adjtime(struct sol_timex __user *txp)
{
	int (*sys_adjtimex)(struct timex __user *) =
		(int (*)(struct timex __user *))SYS(adjtimex);
	struct timex t;
	int ret, err;
	mm_segment_t old_fs = get_fs();

	ret = get_user (t.modes, &txp->modes);
	ret |= __get_user (t.offset, &txp->offset);
	ret |= __get_user (t.freq, &txp->freq);
	ret |= __get_user (t.maxerror, &txp->maxerror);
	ret |= __get_user (t.esterror, &txp->esterror);
	ret |= __get_user (t.status, &txp->status);
	ret |= __get_user (t.constant, &txp->constant);
	set_fs(KERNEL_DS);
	ret = sys_adjtimex(&t);
	set_fs(old_fs);
	if (ret < 0)
		return ret;
	err = put_user (t.offset, &txp->offset);
	err |= __put_user (t.freq, &txp->freq);
	err |= __put_user (t.maxerror, &txp->maxerror);
	err |= __put_user (t.esterror, &txp->esterror);
	err |= __put_user (t.status, &txp->status);
	err |= __put_user (t.constant, &txp->constant);
	err |= __put_user (t.precision, &txp->precision);
	err |= __put_user (t.tolerance, &txp->tolerance);
	err |= __put_user (t.ppsfreq, &txp->ppsfreq);
	err |= __put_user (t.jitter, &txp->jitter);
	err |= __put_user (t.shift, &txp->shift);
	err |= __put_user (t.stabil, &txp->stabil);
	err |= __put_user (t.jitcnt, &txp->jitcnt);
	err |= __put_user (t.calcnt, &txp->calcnt);
	err |= __put_user (t.errcnt, &txp->errcnt);
	err |= __put_user (t.stbcnt, &txp->stbcnt);
	if (err)
		return -EFAULT;
	return ret;
}

asmlinkage int do_sol_unimplemented(struct pt_regs *regs)
{
	printk ("Unimplemented Solaris syscall %d %08x %08x %08x %08x\n", 
			(int)regs->u_regs[UREG_G1], 
			(int)regs->u_regs[UREG_I0],
			(int)regs->u_regs[UREG_I1],
			(int)regs->u_regs[UREG_I2],
			(int)regs->u_regs[UREG_I3]);
	return -ENOSYS;
}

asmlinkage void solaris_register(void)
{
	set_personality(PER_SVR4);
}

extern long solaris_to_linux_signals[], linux_to_solaris_signals[];

struct exec_domain solaris_exec_domain = {
	.name =		"Solaris",
	.handler =	NULL,
	.pers_low =	1,		/* PER_SVR4 personality */
	.pers_high =	1,
	.signal_map =	solaris_to_linux_signals,
	.signal_invmap =linux_to_solaris_signals,
	.module =	THIS_MODULE,
	.next =		NULL
};

extern int init_socksys(void);

#ifdef MODULE

MODULE_AUTHOR("Jakub Jelinek (jj@ultra.linux.cz), Patrik Rak (prak3264@ss1000.ms.mff.cuni.cz)");
MODULE_DESCRIPTION("Solaris binary emulation module");
MODULE_LICENSE("GPL");

#ifdef __sparc_v9__
extern u32 tl0_solaris[8];
#define update_ttable(x) 										\
	tl0_solaris[3] = (((long)(x) - (long)tl0_solaris - 3) >> 2) | 0x40000000;			\
	wmb();		\
	__asm__ __volatile__ ("flush %0" : : "r" (&tl0_solaris[3]))
#else
#endif	

extern u32 solaris_sparc_syscall[];
extern u32 solaris_syscall[];
extern void cleanup_socksys(void);

extern u32 entry64_personality_patch;

int init_module(void)
{
	int ret;

	SOLDD(("Solaris module at %p\n", solaris_sparc_syscall));
	register_exec_domain(&solaris_exec_domain);
	if ((ret = init_socksys())) {
		unregister_exec_domain(&solaris_exec_domain);
		return ret;
	}
	update_ttable(solaris_sparc_syscall);
	entry64_personality_patch |=
		(offsetof(struct task_struct, personality) +
		 (sizeof(unsigned long) - 1));
	wmb();
	__asm__ __volatile__("flush %0"
			     : : "r" (&entry64_personality_patch));
	return 0;
}

void cleanup_module(void)
{
	update_ttable(solaris_syscall);
	cleanup_socksys();
	unregister_exec_domain(&solaris_exec_domain);
}

#else
int init_solaris_emul(void)
{
	register_exec_domain(&solaris_exec_domain);
	init_socksys();
	return 0;
}
#endif

