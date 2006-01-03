/*
 * sys_ppc32.c: Conversion between 32bit and 64bit native syscalls.
 *
 * Copyright (C) 2001 IBM
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h> 
#include <linux/mm.h> 
#include <linux/file.h> 
#include <linux/signal.h>
#include <linux/resource.h>
#include <linux/times.h>
#include <linux/utsname.h>
#include <linux/timex.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/poll.h>
#include <linux/personality.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/in.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/sysctl.h>
#include <linux/binfmts.h>
#include <linux/security.h>
#include <linux/compat.h>
#include <linux/ptrace.h>
#include <linux/elf.h>

#include <asm/ptrace.h>
#include <asm/types.h>
#include <asm/ipc.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/semaphore.h>
#include <asm/time.h>
#include <asm/mmu_context.h>
#include <asm/ppc-pci.h>

/* readdir & getdents */
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char __user *) (de)))
#define ROUND_UP(x) (((x)+sizeof(u32)-1) & ~(sizeof(u32)-1))

struct old_linux_dirent32 {
	u32		d_ino;
	u32		d_offset;
	unsigned short	d_namlen;
	char		d_name[1];
};

struct readdir_callback32 {
	struct old_linux_dirent32 __user * dirent;
	int count;
};

static int fillonedir(void * __buf, const char * name, int namlen,
		                  off_t offset, ino_t ino, unsigned int d_type)
{
	struct readdir_callback32 * buf = (struct readdir_callback32 *) __buf;
	struct old_linux_dirent32 __user * dirent;

	if (buf->count)
		return -EINVAL;
	buf->count++;
	dirent = buf->dirent;
	put_user(ino, &dirent->d_ino);
	put_user(offset, &dirent->d_offset);
	put_user(namlen, &dirent->d_namlen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	return 0;
}

asmlinkage int old32_readdir(unsigned int fd, struct old_linux_dirent32 __user *dirent, unsigned int count)
{
	int error = -EBADF;
	struct file * file;
	struct readdir_callback32 buf;

	file = fget(fd);
	if (!file)
		goto out;

	buf.count = 0;
	buf.dirent = dirent;

	error = vfs_readdir(file, (filldir_t)fillonedir, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.count;

out_putf:
	fput(file);
out:
	return error;
}

asmlinkage long ppc32_select(u32 n, compat_ulong_t __user *inp,
		compat_ulong_t __user *outp, compat_ulong_t __user *exp,
		compat_uptr_t tvp_x)
{
	/* sign extend n */
	return compat_sys_select((int)n, inp, outp, exp, compat_ptr(tvp_x));
}

int cp_compat_stat(struct kstat *stat, struct compat_stat __user *statbuf)
{
	long err;

	if (stat->size > MAX_NON_LFS || !new_valid_dev(stat->dev) ||
	    !new_valid_dev(stat->rdev))
		return -EOVERFLOW;

	err  = access_ok(VERIFY_WRITE, statbuf, sizeof(*statbuf)) ? 0 : -EFAULT;
	err |= __put_user(new_encode_dev(stat->dev), &statbuf->st_dev);
	err |= __put_user(stat->ino, &statbuf->st_ino);
	err |= __put_user(stat->mode, &statbuf->st_mode);
	err |= __put_user(stat->nlink, &statbuf->st_nlink);
	err |= __put_user(stat->uid, &statbuf->st_uid);
	err |= __put_user(stat->gid, &statbuf->st_gid);
	err |= __put_user(new_encode_dev(stat->rdev), &statbuf->st_rdev);
	err |= __put_user(stat->size, &statbuf->st_size);
	err |= __put_user(stat->atime.tv_sec, &statbuf->st_atime);
	err |= __put_user(stat->atime.tv_nsec, &statbuf->st_atime_nsec);
	err |= __put_user(stat->mtime.tv_sec, &statbuf->st_mtime);
	err |= __put_user(stat->mtime.tv_nsec, &statbuf->st_mtime_nsec);
	err |= __put_user(stat->ctime.tv_sec, &statbuf->st_ctime);
	err |= __put_user(stat->ctime.tv_nsec, &statbuf->st_ctime_nsec);
	err |= __put_user(stat->blksize, &statbuf->st_blksize);
	err |= __put_user(stat->blocks, &statbuf->st_blocks);
	err |= __put_user(0, &statbuf->__unused4[0]);
	err |= __put_user(0, &statbuf->__unused4[1]);

	return err;
}

/* Note: it is necessary to treat option as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_sysfs(u32 option, u32 arg1, u32 arg2)
{
	return sys_sysfs((int)option, arg1, arg2);
}

/* Handle adjtimex compatibility. */
struct timex32 {
	u32 modes;
	s32 offset, freq, maxerror, esterror;
	s32 status, constant, precision, tolerance;
	struct compat_timeval time;
	s32 tick;
	s32 ppsfreq, jitter, shift, stabil;
	s32 jitcnt, calcnt, errcnt, stbcnt;
	s32  :32; s32  :32; s32  :32; s32  :32;
	s32  :32; s32  :32; s32  :32; s32  :32;
	s32  :32; s32  :32; s32  :32; s32  :32;
};

extern int do_adjtimex(struct timex *);
extern void ppc_adjtimex(void);

asmlinkage long compat_sys_adjtimex(struct timex32 __user *utp)
{
	struct timex txc;
	int ret;
	
	memset(&txc, 0, sizeof(struct timex));

	if(get_user(txc.modes, &utp->modes) ||
	   __get_user(txc.offset, &utp->offset) ||
	   __get_user(txc.freq, &utp->freq) ||
	   __get_user(txc.maxerror, &utp->maxerror) ||
	   __get_user(txc.esterror, &utp->esterror) ||
	   __get_user(txc.status, &utp->status) ||
	   __get_user(txc.constant, &utp->constant) ||
	   __get_user(txc.precision, &utp->precision) ||
	   __get_user(txc.tolerance, &utp->tolerance) ||
	   __get_user(txc.time.tv_sec, &utp->time.tv_sec) ||
	   __get_user(txc.time.tv_usec, &utp->time.tv_usec) ||
	   __get_user(txc.tick, &utp->tick) ||
	   __get_user(txc.ppsfreq, &utp->ppsfreq) ||
	   __get_user(txc.jitter, &utp->jitter) ||
	   __get_user(txc.shift, &utp->shift) ||
	   __get_user(txc.stabil, &utp->stabil) ||
	   __get_user(txc.jitcnt, &utp->jitcnt) ||
	   __get_user(txc.calcnt, &utp->calcnt) ||
	   __get_user(txc.errcnt, &utp->errcnt) ||
	   __get_user(txc.stbcnt, &utp->stbcnt))
		return -EFAULT;

	ret = do_adjtimex(&txc);

	/* adjust the conversion of TB to time of day to track adjtimex */
	ppc_adjtimex();

	if(put_user(txc.modes, &utp->modes) ||
	   __put_user(txc.offset, &utp->offset) ||
	   __put_user(txc.freq, &utp->freq) ||
	   __put_user(txc.maxerror, &utp->maxerror) ||
	   __put_user(txc.esterror, &utp->esterror) ||
	   __put_user(txc.status, &utp->status) ||
	   __put_user(txc.constant, &utp->constant) ||
	   __put_user(txc.precision, &utp->precision) ||
	   __put_user(txc.tolerance, &utp->tolerance) ||
	   __put_user(txc.time.tv_sec, &utp->time.tv_sec) ||
	   __put_user(txc.time.tv_usec, &utp->time.tv_usec) ||
	   __put_user(txc.tick, &utp->tick) ||
	   __put_user(txc.ppsfreq, &utp->ppsfreq) ||
	   __put_user(txc.jitter, &utp->jitter) ||
	   __put_user(txc.shift, &utp->shift) ||
	   __put_user(txc.stabil, &utp->stabil) ||
	   __put_user(txc.jitcnt, &utp->jitcnt) ||
	   __put_user(txc.calcnt, &utp->calcnt) ||
	   __put_user(txc.errcnt, &utp->errcnt) ||
	   __put_user(txc.stbcnt, &utp->stbcnt))
		ret = -EFAULT;

	return ret;
}

asmlinkage long compat_sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	
	return -ERESTARTNOHAND;
}

static inline long get_ts32(struct timespec *o, struct compat_timeval __user *i)
{
	long usec;

	if (!access_ok(VERIFY_READ, i, sizeof(*i)))
		return -EFAULT;
	if (__get_user(o->tv_sec, &i->tv_sec))
		return -EFAULT;
	if (__get_user(usec, &i->tv_usec))
		return -EFAULT;
	o->tv_nsec = usec * 1000;
	return 0;
}

static inline long put_tv32(struct compat_timeval __user *o, struct timeval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->tv_sec, &o->tv_sec) |
		 __put_user(i->tv_usec, &o->tv_usec)));
}

struct sysinfo32 {
        s32 uptime;
        u32 loads[3];
        u32 totalram;
        u32 freeram;
        u32 sharedram;
        u32 bufferram;
        u32 totalswap;
        u32 freeswap;
        unsigned short procs;
	unsigned short pad;
	u32 totalhigh;
	u32 freehigh;
	u32 mem_unit;
	char _f[20-2*sizeof(int)-sizeof(int)];
};

asmlinkage long compat_sys_sysinfo(struct sysinfo32 __user *info)
{
	struct sysinfo s;
	int ret, err;
	int bitcount=0;
	mm_segment_t old_fs = get_fs ();
	
	/* The __user cast is valid due to set_fs() */
	set_fs (KERNEL_DS);
	ret = sys_sysinfo((struct sysinfo __user *)&s);
	set_fs (old_fs);

	/* Check to see if any memory value is too large for 32-bit and
         * scale down if needed.
         */
	if ((s.totalram >> 32) || (s.totalswap >> 32)) {
	    while (s.mem_unit < PAGE_SIZE) {
		s.mem_unit <<= 1;
		bitcount++;
	    }
	    s.totalram >>=bitcount;
	    s.freeram >>= bitcount;
	    s.sharedram >>= bitcount;
	    s.bufferram >>= bitcount;
	    s.totalswap >>= bitcount;
	    s.freeswap >>= bitcount;
	    s.totalhigh >>= bitcount;
	    s.freehigh >>= bitcount;
	}

	err = put_user (s.uptime, &info->uptime);
	err |= __put_user (s.loads[0], &info->loads[0]);
	err |= __put_user (s.loads[1], &info->loads[1]);
	err |= __put_user (s.loads[2], &info->loads[2]);
	err |= __put_user (s.totalram, &info->totalram);
	err |= __put_user (s.freeram, &info->freeram);
	err |= __put_user (s.sharedram, &info->sharedram);
	err |= __put_user (s.bufferram, &info->bufferram);
	err |= __put_user (s.totalswap, &info->totalswap);
	err |= __put_user (s.freeswap, &info->freeswap);
	err |= __put_user (s.procs, &info->procs);
	err |= __put_user (s.totalhigh, &info->totalhigh);
	err |= __put_user (s.freehigh, &info->freehigh);
	err |= __put_user (s.mem_unit, &info->mem_unit);
	if (err)
		return -EFAULT;
	
	return ret;
}




/* Translations due to time_t size differences.  Which affects all
   sorts of things, like timeval and itimerval.  */
extern struct timezone sys_tz;

asmlinkage long compat_sys_gettimeofday(struct compat_timeval __user *tv, struct timezone __user *tz)
{
	if (tv) {
		struct timeval ktv;
		do_gettimeofday(&ktv);
		if (put_tv32(tv, &ktv))
			return -EFAULT;
	}
	if (tz) {
		if (copy_to_user(tz, &sys_tz, sizeof(sys_tz)))
			return -EFAULT;
	}
	
	return 0;
}



asmlinkage long compat_sys_settimeofday(struct compat_timeval __user *tv, struct timezone __user *tz)
{
	struct timespec kts;
	struct timezone ktz;
	
 	if (tv) {
		if (get_ts32(&kts, tv))
			return -EFAULT;
	}
	if (tz) {
		if (copy_from_user(&ktz, tz, sizeof(ktz)))
			return -EFAULT;
	}

	return do_sys_settimeofday(tv ? &kts : NULL, tz ? &ktz : NULL);
}

#ifdef CONFIG_SYSVIPC
long compat_sys_ipc(u32 call, u32 first, u32 second, u32 third, compat_uptr_t ptr,
	       u32 fifth)
{
	int version;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {

	case SEMTIMEDOP:
		if (fifth)
			/* sign extend semid */
			return compat_sys_semtimedop((int)first,
						     compat_ptr(ptr), second,
						     compat_ptr(fifth));
		/* else fall through for normal semop() */
	case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		/* sign extend semid */
		return sys_semtimedop((int)first, compat_ptr(ptr), second,
				      NULL);
	case SEMGET:
		/* sign extend key, nsems */
		return sys_semget((int)first, (int)second, third);
	case SEMCTL:
		/* sign extend semid, semnum */
		return compat_sys_semctl((int)first, (int)second, third,
					 compat_ptr(ptr));

	case MSGSND:
		/* sign extend msqid */
		return compat_sys_msgsnd((int)first, (int)second, third,
					 compat_ptr(ptr));
	case MSGRCV:
		/* sign extend msqid, msgtyp */
		return compat_sys_msgrcv((int)first, second, (int)fifth,
					 third, version, compat_ptr(ptr));
	case MSGGET:
		/* sign extend key */
		return sys_msgget((int)first, second);
	case MSGCTL:
		/* sign extend msqid */
		return compat_sys_msgctl((int)first, second, compat_ptr(ptr));

	case SHMAT:
		/* sign extend shmid */
		return compat_sys_shmat((int)first, second, third, version,
					compat_ptr(ptr));
	case SHMDT:
		return sys_shmdt(compat_ptr(ptr));
	case SHMGET:
		/* sign extend key_t */
		return sys_shmget((int)first, second, third);
	case SHMCTL:
		/* sign extend shmid */
		return compat_sys_shmctl((int)first, second, compat_ptr(ptr));

	default:
		return -ENOSYS;
	}

	return -ENOSYS;
}
#endif

/* Note: it is necessary to treat out_fd and in_fd as unsigned ints, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_sendfile(u32 out_fd, u32 in_fd, compat_off_t __user * offset, u32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	off_t of;
	off_t __user *up;

	if (offset && get_user(of, offset))
		return -EFAULT;

	/* The __user pointer cast is valid because of the set_fs() */		
	set_fs(KERNEL_DS);
	up = offset ? (off_t __user *) &of : NULL;
	ret = sys_sendfile((int)out_fd, (int)in_fd, up, count);
	set_fs(old_fs);
	
	if (offset && put_user(of, offset))
		return -EFAULT;
		
	return ret;
}

asmlinkage int compat_sys_sendfile64(int out_fd, int in_fd, compat_loff_t __user *offset, s32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	loff_t lof;
	loff_t __user *up;
	
	if (offset && get_user(lof, offset))
		return -EFAULT;
		
	/* The __user pointer cast is valid because of the set_fs() */		
	set_fs(KERNEL_DS);
	up = offset ? (loff_t __user *) &lof : NULL;
	ret = sys_sendfile64(out_fd, in_fd, up, count);
	set_fs(old_fs);
	
	if (offset && put_user(lof, offset))
		return -EFAULT;
		
	return ret;
}

long compat_sys_execve(unsigned long a0, unsigned long a1, unsigned long a2,
		  unsigned long a3, unsigned long a4, unsigned long a5,
		  struct pt_regs *regs)
{
	int error;
	char * filename;
	
	filename = getname((char __user *) a0);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	flush_fp_to_thread(current);
	flush_altivec_to_thread(current);

	error = compat_do_execve(filename, compat_ptr(a1), compat_ptr(a2), regs);

	if (error == 0) {
		task_lock(current);
		current->ptrace &= ~PT_DTRACE;
		task_unlock(current);
	}
	putname(filename);

out:
	return error;
}

/* Note: it is necessary to treat option as an unsigned int, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_prctl(u32 option, u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
	return sys_prctl((int)option,
			 (unsigned long) arg2,
			 (unsigned long) arg3,
			 (unsigned long) arg4,
			 (unsigned long) arg5);
}

/* Note: it is necessary to treat pid as an unsigned int, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_sched_rr_get_interval(u32 pid, struct compat_timespec __user *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();

	/* The __user pointer cast is valid because of the set_fs() */
	set_fs (KERNEL_DS);
	ret = sys_sched_rr_get_interval((int)pid, (struct timespec __user *) &t);
	set_fs (old_fs);
	if (put_compat_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

asmlinkage int compat_sys_pciconfig_read(u32 bus, u32 dfn, u32 off, u32 len, u32 ubuf)
{
	return sys_pciconfig_read((unsigned long) bus,
				  (unsigned long) dfn,
				  (unsigned long) off,
				  (unsigned long) len,
				  compat_ptr(ubuf));
}

asmlinkage int compat_sys_pciconfig_write(u32 bus, u32 dfn, u32 off, u32 len, u32 ubuf)
{
	return sys_pciconfig_write((unsigned long) bus,
				   (unsigned long) dfn,
				   (unsigned long) off,
				   (unsigned long) len,
				   compat_ptr(ubuf));
}

asmlinkage int compat_sys_pciconfig_iobase(u32 which, u32 in_bus, u32 in_devfn)
{
	return sys_pciconfig_iobase(which, in_bus, in_devfn);
}


/* Note: it is necessary to treat mode as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_access(const char __user * filename, u32 mode)
{
	return sys_access(filename, (int)mode);
}


/* Note: it is necessary to treat mode as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_creat(const char __user * pathname, u32 mode)
{
	return sys_creat(pathname, (int)mode);
}


/* Note: it is necessary to treat pid and options as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_waitpid(u32 pid, unsigned int __user * stat_addr, u32 options)
{
	return sys_waitpid((int)pid, stat_addr, (int)options);
}


/* Note: it is necessary to treat gidsetsize as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_getgroups(u32 gidsetsize, gid_t __user *grouplist)
{
	return sys_getgroups((int)gidsetsize, grouplist);
}


/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_getpgid(u32 pid)
{
	return sys_getpgid((int)pid);
}



/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_getsid(u32 pid)
{
	return sys_getsid((int)pid);
}


/* Note: it is necessary to treat pid and sig as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_kill(u32 pid, u32 sig)
{
	return sys_kill((int)pid, (int)sig);
}


/* Note: it is necessary to treat mode as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_mkdir(const char __user * pathname, u32 mode)
{
	return sys_mkdir(pathname, (int)mode);
}

long compat_sys_nice(u32 increment)
{
	/* sign extend increment */
	return sys_nice((int)increment);
}

off_t ppc32_lseek(unsigned int fd, u32 offset, unsigned int origin)
{
	/* sign extend n */
	return sys_lseek(fd, (int)offset, origin);
}

/* Note: it is necessary to treat bufsiz as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_readlink(const char __user * path, char __user * buf, u32 bufsiz)
{
	return sys_readlink(path, buf, (int)bufsiz);
}

/* Note: it is necessary to treat option as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_sched_get_priority_max(u32 policy)
{
	return sys_sched_get_priority_max((int)policy);
}


/* Note: it is necessary to treat policy as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_sched_get_priority_min(u32 policy)
{
	return sys_sched_get_priority_min((int)policy);
}


/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_sched_getparam(u32 pid, struct sched_param __user *param)
{
	return sys_sched_getparam((int)pid, param);
}


/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_sched_getscheduler(u32 pid)
{
	return sys_sched_getscheduler((int)pid);
}


/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_sched_setparam(u32 pid, struct sched_param __user *param)
{
	return sys_sched_setparam((int)pid, param);
}


/* Note: it is necessary to treat pid and policy as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_sched_setscheduler(u32 pid, u32 policy, struct sched_param __user *param)
{
	return sys_sched_setscheduler((int)pid, (int)policy, param);
}


/* Note: it is necessary to treat len as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_setdomainname(char __user *name, u32 len)
{
	return sys_setdomainname(name, (int)len);
}


/* Note: it is necessary to treat gidsetsize as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_setgroups(u32 gidsetsize, gid_t __user *grouplist)
{
	return sys_setgroups((int)gidsetsize, grouplist);
}


asmlinkage long compat_sys_sethostname(char __user *name, u32 len)
{
	/* sign extend len */
	return sys_sethostname(name, (int)len);
}


/* Note: it is necessary to treat pid and pgid as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_setpgid(u32 pid, u32 pgid)
{
	return sys_setpgid((int)pid, (int)pgid);
}

long compat_sys_getpriority(u32 which, u32 who)
{
	/* sign extend which and who */
	return sys_getpriority((int)which, (int)who);
}

long compat_sys_setpriority(u32 which, u32 who, u32 niceval)
{
	/* sign extend which, who and niceval */
	return sys_setpriority((int)which, (int)who, (int)niceval);
}

long compat_sys_ioprio_get(u32 which, u32 who)
{
	/* sign extend which and who */
	return sys_ioprio_get((int)which, (int)who);
}

long compat_sys_ioprio_set(u32 which, u32 who, u32 ioprio)
{
	/* sign extend which, who and ioprio */
	return sys_ioprio_set((int)which, (int)who, (int)ioprio);
}

/* Note: it is necessary to treat newmask as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_ssetmask(u32 newmask)
{
	return sys_ssetmask((int) newmask);
}

asmlinkage long compat_sys_syslog(u32 type, char __user * buf, u32 len)
{
	/* sign extend len */
	return sys_syslog(type, buf, (int)len);
}


/* Note: it is necessary to treat mask as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_umask(u32 mask)
{
	return sys_umask((int)mask);
}

#ifdef CONFIG_SYSCTL
struct __sysctl_args32 {
	u32 name;
	int nlen;
	u32 oldval;
	u32 oldlenp;
	u32 newval;
	u32 newlen;
	u32 __unused[4];
};

asmlinkage long compat_sys_sysctl(struct __sysctl_args32 __user *args)
{
	struct __sysctl_args32 tmp;
	int error;
	size_t oldlen;
	size_t __user *oldlenp = NULL;
	unsigned long addr = (((unsigned long)&args->__unused[0]) + 7) & ~7;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	if (tmp.oldval && tmp.oldlenp) {
		/* Duh, this is ugly and might not work if sysctl_args
		   is in read-only memory, but do_sysctl does indirectly
		   a lot of uaccess in both directions and we'd have to
		   basically copy the whole sysctl.c here, and
		   glibc's __sysctl uses rw memory for the structure
		   anyway.  */
		oldlenp = (size_t __user *)addr;
		if (get_user(oldlen, (compat_size_t __user *)compat_ptr(tmp.oldlenp)) ||
		    put_user(oldlen, oldlenp))
			return -EFAULT;
	}

	lock_kernel();
	error = do_sysctl(compat_ptr(tmp.name), tmp.nlen,
			  compat_ptr(tmp.oldval), oldlenp,
			  compat_ptr(tmp.newval), tmp.newlen);
	unlock_kernel();
	if (oldlenp) {
		if (!error) {
			if (get_user(oldlen, oldlenp) ||
			    put_user(oldlen, (compat_size_t __user *)compat_ptr(tmp.oldlenp)))
				error = -EFAULT;
		}
		copy_to_user(args->__unused, tmp.__unused, sizeof(tmp.__unused));
	}
	return error;
}
#endif

unsigned long compat_sys_mmap2(unsigned long addr, size_t len,
			  unsigned long prot, unsigned long flags,
			  unsigned long fd, unsigned long pgoff)
{
	/* This should remain 12 even if PAGE_SIZE changes */
	return sys_mmap(addr, len, prot, flags, fd, pgoff << 12);
}

long compat_sys_tgkill(u32 tgid, u32 pid, int sig)
{
	/* sign extend tgid, pid */
	return sys_tgkill((int)tgid, (int)pid, sig);
}

/* 
 * long long munging:
 * The 32 bit ABI passes long longs in an odd even register pair.
 */

compat_ssize_t compat_sys_pread64(unsigned int fd, char __user *ubuf, compat_size_t count,
			     u32 reg6, u32 poshi, u32 poslo)
{
	return sys_pread64(fd, ubuf, count, ((loff_t)poshi << 32) | poslo);
}

compat_ssize_t compat_sys_pwrite64(unsigned int fd, char __user *ubuf, compat_size_t count,
			      u32 reg6, u32 poshi, u32 poslo)
{
	return sys_pwrite64(fd, ubuf, count, ((loff_t)poshi << 32) | poslo);
}

compat_ssize_t compat_sys_readahead(int fd, u32 r4, u32 offhi, u32 offlo, u32 count)
{
	return sys_readahead(fd, ((loff_t)offhi << 32) | offlo, count);
}

asmlinkage int compat_sys_truncate64(const char __user * path, u32 reg4,
				unsigned long high, unsigned long low)
{
	return sys_truncate(path, (high << 32) | low);
}

asmlinkage int compat_sys_ftruncate64(unsigned int fd, u32 reg4, unsigned long high,
				 unsigned long low)
{
	return sys_ftruncate(fd, (high << 32) | low);
}

long ppc32_lookup_dcookie(u32 cookie_high, u32 cookie_low, char __user *buf,
			  size_t len)
{
	return sys_lookup_dcookie((u64)cookie_high << 32 | cookie_low,
				  buf, len);
}

long ppc32_fadvise64(int fd, u32 unused, u32 offset_high, u32 offset_low,
		     size_t len, int advice)
{
	return sys_fadvise64(fd, (u64)offset_high << 32 | offset_low, len,
			     advice);
}

long ppc32_timer_create(clockid_t clock,
			struct compat_sigevent __user *ev32,
			timer_t __user *timer_id)
{
	sigevent_t event;
	timer_t t;
	long err;
	mm_segment_t savefs;

	if (ev32 == NULL)
		return sys_timer_create(clock, NULL, timer_id);

	if (get_compat_sigevent(&event, ev32))
		return -EFAULT;

	if (!access_ok(VERIFY_WRITE, timer_id, sizeof(timer_t)))
		return -EFAULT;

	savefs = get_fs();
	set_fs(KERNEL_DS);
	/* The __user pointer casts are valid due to the set_fs() */
	err = sys_timer_create(clock,
		(sigevent_t __user *) &event,
		(timer_t __user *) &t);
	set_fs(savefs);

	if (err == 0)
		err = __put_user(t, timer_id);

	return err;
}

asmlinkage long compat_sys_add_key(const char __user *_type,
			      const char __user *_description,
			      const void __user *_payload,
			      u32 plen,
			      u32 ringid)
{
	return sys_add_key(_type, _description, _payload, plen, ringid);
}

asmlinkage long compat_sys_request_key(const char __user *_type,
				  const char __user *_description,
				  const char __user *_callout_info,
				  u32 destringid)
{
	return sys_request_key(_type, _description, _callout_info, destringid);
}

