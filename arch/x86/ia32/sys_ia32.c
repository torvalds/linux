/*
 * sys_ia32.c: Conversion between 32bit and 64bit native syscalls. Based on
 *             sys_sparc32
 *
 * Copyright (C) 2000		VA Linux Co
 * Copyright (C) 2000		Don Dugger <n0ano@valinux.com>
 * Copyright (C) 1999		Arun Sharma <arun.sharma@intel.com>
 * Copyright (C) 1997,1998	Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997		David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 2000		Hewlett-Packard Co.
 * Copyright (C) 2000		David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000,2001,2002	Andi Kleen, SuSE Labs (x86-64 port)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment. In 2.5 most of this should be moved to a generic directory.
 *
 * This file assumes that there is a hole at the end of user address space.
 *
 * Some of the functions are LE specific currently. These are
 * hopefully all marked.  This should be fixed.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/signal.h>
#include <linux/syscalls.h>
#include <linux/resource.h>
#include <linux/times.h>
#include <linux/utsname.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/nfs_fs.h>
#include <linux/quota.h>
#include <linux/module.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr.h>
#include <linux/nfsd/syscall.h>
#include <linux/poll.h>
#include <linux/personality.h>
#include <linux/stat.h>
#include <linux/ipc.h>
#include <linux/rwsem.h>
#include <linux/binfmts.h>
#include <linux/init.h>
#include <linux/aio_abi.h>
#include <linux/aio.h>
#include <linux/compat.h>
#include <linux/vfs.h>
#include <linux/ptrace.h>
#include <linux/highuid.h>
#include <linux/vmalloc.h>
#include <linux/fsnotify.h>
#include <linux/sysctl.h>
#include <asm/mman.h>
#include <asm/types.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/atomic.h>
#include <asm/ldt.h>

#include <net/scm.h>
#include <net/sock.h>
#include <asm/ia32.h>

#define AA(__x)		((unsigned long)(__x))

int cp_compat_stat(struct kstat *kbuf, struct compat_stat __user *ubuf)
{
	compat_ino_t ino;

	typeof(ubuf->st_uid) uid = 0;
	typeof(ubuf->st_gid) gid = 0;
	SET_UID(uid, kbuf->uid);
	SET_GID(gid, kbuf->gid);
	if (!old_valid_dev(kbuf->dev) || !old_valid_dev(kbuf->rdev))
		return -EOVERFLOW;
	if (kbuf->size >= 0x7fffffff)
		return -EOVERFLOW;
	ino = kbuf->ino;
	if (sizeof(ino) < sizeof(kbuf->ino) && ino != kbuf->ino)
		return -EOVERFLOW;
	if (!access_ok(VERIFY_WRITE, ubuf, sizeof(struct compat_stat)) ||
	    __put_user(old_encode_dev(kbuf->dev), &ubuf->st_dev) ||
	    __put_user(ino, &ubuf->st_ino) ||
	    __put_user(kbuf->mode, &ubuf->st_mode) ||
	    __put_user(kbuf->nlink, &ubuf->st_nlink) ||
	    __put_user(uid, &ubuf->st_uid) ||
	    __put_user(gid, &ubuf->st_gid) ||
	    __put_user(old_encode_dev(kbuf->rdev), &ubuf->st_rdev) ||
	    __put_user(kbuf->size, &ubuf->st_size) ||
	    __put_user(kbuf->atime.tv_sec, &ubuf->st_atime) ||
	    __put_user(kbuf->atime.tv_nsec, &ubuf->st_atime_nsec) ||
	    __put_user(kbuf->mtime.tv_sec, &ubuf->st_mtime) ||
	    __put_user(kbuf->mtime.tv_nsec, &ubuf->st_mtime_nsec) ||
	    __put_user(kbuf->ctime.tv_sec, &ubuf->st_ctime) ||
	    __put_user(kbuf->ctime.tv_nsec, &ubuf->st_ctime_nsec) ||
	    __put_user(kbuf->blksize, &ubuf->st_blksize) ||
	    __put_user(kbuf->blocks, &ubuf->st_blocks))
		return -EFAULT;
	return 0;
}

asmlinkage long sys32_truncate64(char __user *filename,
				 unsigned long offset_low,
				 unsigned long offset_high)
{
       return sys_truncate(filename, ((loff_t) offset_high << 32) | offset_low);
}

asmlinkage long sys32_ftruncate64(unsigned int fd, unsigned long offset_low,
				  unsigned long offset_high)
{
       return sys_ftruncate(fd, ((loff_t) offset_high << 32) | offset_low);
}

/*
 * Another set for IA32/LFS -- x86_64 struct stat is different due to
 * support for 64bit inode numbers.
 */
static int cp_stat64(struct stat64 __user *ubuf, struct kstat *stat)
{
	typeof(ubuf->st_uid) uid = 0;
	typeof(ubuf->st_gid) gid = 0;
	SET_UID(uid, stat->uid);
	SET_GID(gid, stat->gid);
	if (!access_ok(VERIFY_WRITE, ubuf, sizeof(struct stat64)) ||
	    __put_user(huge_encode_dev(stat->dev), &ubuf->st_dev) ||
	    __put_user(stat->ino, &ubuf->__st_ino) ||
	    __put_user(stat->ino, &ubuf->st_ino) ||
	    __put_user(stat->mode, &ubuf->st_mode) ||
	    __put_user(stat->nlink, &ubuf->st_nlink) ||
	    __put_user(uid, &ubuf->st_uid) ||
	    __put_user(gid, &ubuf->st_gid) ||
	    __put_user(huge_encode_dev(stat->rdev), &ubuf->st_rdev) ||
	    __put_user(stat->size, &ubuf->st_size) ||
	    __put_user(stat->atime.tv_sec, &ubuf->st_atime) ||
	    __put_user(stat->atime.tv_nsec, &ubuf->st_atime_nsec) ||
	    __put_user(stat->mtime.tv_sec, &ubuf->st_mtime) ||
	    __put_user(stat->mtime.tv_nsec, &ubuf->st_mtime_nsec) ||
	    __put_user(stat->ctime.tv_sec, &ubuf->st_ctime) ||
	    __put_user(stat->ctime.tv_nsec, &ubuf->st_ctime_nsec) ||
	    __put_user(stat->blksize, &ubuf->st_blksize) ||
	    __put_user(stat->blocks, &ubuf->st_blocks))
		return -EFAULT;
	return 0;
}

asmlinkage long sys32_stat64(char __user *filename,
			     struct stat64 __user *statbuf)
{
	struct kstat stat;
	int ret = vfs_stat(filename, &stat);

	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long sys32_lstat64(char __user *filename,
			      struct stat64 __user *statbuf)
{
	struct kstat stat;
	int ret = vfs_lstat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long sys32_fstat64(unsigned int fd, struct stat64 __user *statbuf)
{
	struct kstat stat;
	int ret = vfs_fstat(fd, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long sys32_fstatat(unsigned int dfd, char __user *filename,
			      struct stat64 __user *statbuf, int flag)
{
	struct kstat stat;
	int error = -EINVAL;

	if ((flag & ~AT_SYMLINK_NOFOLLOW) != 0)
		goto out;

	if (flag & AT_SYMLINK_NOFOLLOW)
		error = vfs_lstat_fd(dfd, filename, &stat);
	else
		error = vfs_stat_fd(dfd, filename, &stat);

	if (!error)
		error = cp_stat64(statbuf, &stat);

out:
	return error;
}

/*
 * Linux/i386 didn't use to be able to handle more than
 * 4 system call parameters, so these system calls used a memory
 * block for parameter passing..
 */

struct mmap_arg_struct {
	unsigned int addr;
	unsigned int len;
	unsigned int prot;
	unsigned int flags;
	unsigned int fd;
	unsigned int offset;
};

asmlinkage long sys32_mmap(struct mmap_arg_struct __user *arg)
{
	struct mmap_arg_struct a;
	struct file *file = NULL;
	unsigned long retval;
	struct mm_struct *mm ;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;

	if (a.offset & ~PAGE_MASK)
		return -EINVAL;

	if (!(a.flags & MAP_ANONYMOUS)) {
		file = fget(a.fd);
		if (!file)
			return -EBADF;
	}

	mm = current->mm;
	down_write(&mm->mmap_sem);
	retval = do_mmap_pgoff(file, a.addr, a.len, a.prot, a.flags,
			       a.offset>>PAGE_SHIFT);
	if (file)
		fput(file);

	up_write(&mm->mmap_sem);

	return retval;
}

asmlinkage long sys32_mprotect(unsigned long start, size_t len,
			       unsigned long prot)
{
	return sys_mprotect(start, len, prot);
}

asmlinkage long sys32_pipe(int __user *fd)
{
	int retval;
	int fds[2];

	retval = do_pipe(fds);
	if (retval)
		goto out;
	if (copy_to_user(fd, fds, sizeof(fds)))
		retval = -EFAULT;
out:
	return retval;
}

asmlinkage long sys32_rt_sigaction(int sig, struct sigaction32 __user *act,
				   struct sigaction32 __user *oact,
				   unsigned int sigsetsize)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	compat_sigset_t set32;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(compat_sigset_t))
		return -EINVAL;

	if (act) {
		compat_uptr_t handler, restorer;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user(restorer, &act->sa_restorer) ||
		    __copy_from_user(&set32, &act->sa_mask,
				     sizeof(compat_sigset_t)))
			return -EFAULT;
		new_ka.sa.sa_handler = compat_ptr(handler);
		new_ka.sa.sa_restorer = compat_ptr(restorer);

		/*
		 * FIXME: here we rely on _COMPAT_NSIG_WORS to be >=
		 * than _NSIG_WORDS << 1
		 */
		switch (_NSIG_WORDS) {
		case 4: new_ka.sa.sa_mask.sig[3] = set32.sig[6]
				| (((long)set32.sig[7]) << 32);
		case 3: new_ka.sa.sa_mask.sig[2] = set32.sig[4]
				| (((long)set32.sig[5]) << 32);
		case 2: new_ka.sa.sa_mask.sig[1] = set32.sig[2]
				| (((long)set32.sig[3]) << 32);
		case 1: new_ka.sa.sa_mask.sig[0] = set32.sig[0]
				| (((long)set32.sig[1]) << 32);
		}
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		/*
		 * FIXME: here we rely on _COMPAT_NSIG_WORS to be >=
		 * than _NSIG_WORDS << 1
		 */
		switch (_NSIG_WORDS) {
		case 4:
			set32.sig[7] = (old_ka.sa.sa_mask.sig[3] >> 32);
			set32.sig[6] = old_ka.sa.sa_mask.sig[3];
		case 3:
			set32.sig[5] = (old_ka.sa.sa_mask.sig[2] >> 32);
			set32.sig[4] = old_ka.sa.sa_mask.sig[2];
		case 2:
			set32.sig[3] = (old_ka.sa.sa_mask.sig[1] >> 32);
			set32.sig[2] = old_ka.sa.sa_mask.sig[1];
		case 1:
			set32.sig[1] = (old_ka.sa.sa_mask.sig[0] >> 32);
			set32.sig[0] = old_ka.sa.sa_mask.sig[0];
		}
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(ptr_to_compat(old_ka.sa.sa_handler),
			       &oact->sa_handler) ||
		    __put_user(ptr_to_compat(old_ka.sa.sa_restorer),
			       &oact->sa_restorer) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags) ||
		    __copy_to_user(&oact->sa_mask, &set32,
				   sizeof(compat_sigset_t)))
			return -EFAULT;
	}

	return ret;
}

asmlinkage long sys32_sigaction(int sig, struct old_sigaction32 __user *act,
				struct old_sigaction32 __user *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		compat_old_sigset_t mask;
		compat_uptr_t handler, restorer;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user(restorer, &act->sa_restorer) ||
		    __get_user(mask, &act->sa_mask))
			return -EFAULT;

		new_ka.sa.sa_handler = compat_ptr(handler);
		new_ka.sa.sa_restorer = compat_ptr(restorer);

		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(ptr_to_compat(old_ka.sa.sa_handler),
			       &oact->sa_handler) ||
		    __put_user(ptr_to_compat(old_ka.sa.sa_restorer),
			       &oact->sa_restorer) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags) ||
		    __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask))
			return -EFAULT;
	}

	return ret;
}

asmlinkage long sys32_rt_sigprocmask(int how, compat_sigset_t __user *set,
				     compat_sigset_t __user *oset,
				     unsigned int sigsetsize)
{
	sigset_t s;
	compat_sigset_t s32;
	int ret;
	mm_segment_t old_fs = get_fs();

	if (set) {
		if (copy_from_user(&s32, set, sizeof(compat_sigset_t)))
			return -EFAULT;
		switch (_NSIG_WORDS) {
		case 4: s.sig[3] = s32.sig[6] | (((long)s32.sig[7]) << 32);
		case 3: s.sig[2] = s32.sig[4] | (((long)s32.sig[5]) << 32);
		case 2: s.sig[1] = s32.sig[2] | (((long)s32.sig[3]) << 32);
		case 1: s.sig[0] = s32.sig[0] | (((long)s32.sig[1]) << 32);
		}
	}
	set_fs(KERNEL_DS);
	ret = sys_rt_sigprocmask(how,
				 set ? (sigset_t __user *)&s : NULL,
				 oset ? (sigset_t __user *)&s : NULL,
				 sigsetsize);
	set_fs(old_fs);
	if (ret)
		return ret;
	if (oset) {
		switch (_NSIG_WORDS) {
		case 4: s32.sig[7] = (s.sig[3] >> 32); s32.sig[6] = s.sig[3];
		case 3: s32.sig[5] = (s.sig[2] >> 32); s32.sig[4] = s.sig[2];
		case 2: s32.sig[3] = (s.sig[1] >> 32); s32.sig[2] = s.sig[1];
		case 1: s32.sig[1] = (s.sig[0] >> 32); s32.sig[0] = s.sig[0];
		}
		if (copy_to_user(oset, &s32, sizeof(compat_sigset_t)))
			return -EFAULT;
	}
	return 0;
}

static inline long get_tv32(struct timeval *o, struct compat_timeval __user *i)
{
	int err = -EFAULT;

	if (access_ok(VERIFY_READ, i, sizeof(*i))) {
		err = __get_user(o->tv_sec, &i->tv_sec);
		err |= __get_user(o->tv_usec, &i->tv_usec);
	}
	return err;
}

static inline long put_tv32(struct compat_timeval __user *o, struct timeval *i)
{
	int err = -EFAULT;

	if (access_ok(VERIFY_WRITE, o, sizeof(*o))) {
		err = __put_user(i->tv_sec, &o->tv_sec);
		err |= __put_user(i->tv_usec, &o->tv_usec);
	}
	return err;
}

asmlinkage long sys32_alarm(unsigned int seconds)
{
	return alarm_setitimer(seconds);
}

/*
 * Translations due to time_t size differences. Which affects all
 * sorts of things, like timeval and itimerval.
 */
asmlinkage long sys32_gettimeofday(struct compat_timeval __user *tv,
				   struct timezone __user *tz)
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

asmlinkage long sys32_settimeofday(struct compat_timeval __user *tv,
				   struct timezone __user *tz)
{
	struct timeval ktv;
	struct timespec kts;
	struct timezone ktz;

	if (tv) {
		if (get_tv32(&ktv, tv))
			return -EFAULT;
		kts.tv_sec = ktv.tv_sec;
		kts.tv_nsec = ktv.tv_usec * NSEC_PER_USEC;
	}
	if (tz) {
		if (copy_from_user(&ktz, tz, sizeof(ktz)))
			return -EFAULT;
	}

	return do_sys_settimeofday(tv ? &kts : NULL, tz ? &ktz : NULL);
}

struct sel_arg_struct {
	unsigned int n;
	unsigned int inp;
	unsigned int outp;
	unsigned int exp;
	unsigned int tvp;
};

asmlinkage long sys32_old_select(struct sel_arg_struct __user *arg)
{
	struct sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	return compat_sys_select(a.n, compat_ptr(a.inp), compat_ptr(a.outp),
				 compat_ptr(a.exp), compat_ptr(a.tvp));
}

asmlinkage long sys32_waitpid(compat_pid_t pid, unsigned int *stat_addr,
			      int options)
{
	return compat_sys_wait4(pid, stat_addr, options, NULL);
}

/* 32-bit timeval and related flotsam.  */

asmlinkage long sys32_sysfs(int option, u32 arg1, u32 arg2)
{
	return sys_sysfs(option, arg1, arg2);
}

asmlinkage long sys32_sched_rr_get_interval(compat_pid_t pid,
				    struct compat_timespec __user *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid, (struct timespec __user *)&t);
	set_fs(old_fs);
	if (put_compat_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

asmlinkage long sys32_rt_sigpending(compat_sigset_t __user *set,
				    compat_size_t sigsetsize)
{
	sigset_t s;
	compat_sigset_t s32;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_rt_sigpending((sigset_t __user *)&s, sigsetsize);
	set_fs(old_fs);
	if (!ret) {
		switch (_NSIG_WORDS) {
		case 4: s32.sig[7] = (s.sig[3] >> 32); s32.sig[6] = s.sig[3];
		case 3: s32.sig[5] = (s.sig[2] >> 32); s32.sig[4] = s.sig[2];
		case 2: s32.sig[3] = (s.sig[1] >> 32); s32.sig[2] = s.sig[1];
		case 1: s32.sig[1] = (s.sig[0] >> 32); s32.sig[0] = s.sig[0];
		}
		if (copy_to_user(set, &s32, sizeof(compat_sigset_t)))
			return -EFAULT;
	}
	return ret;
}

asmlinkage long sys32_rt_sigqueueinfo(int pid, int sig,
				      compat_siginfo_t __user *uinfo)
{
	siginfo_t info;
	int ret;
	mm_segment_t old_fs = get_fs();

	if (copy_siginfo_from_user32(&info, uinfo))
		return -EFAULT;
	set_fs(KERNEL_DS);
	ret = sys_rt_sigqueueinfo(pid, sig, (siginfo_t __user *)&info);
	set_fs(old_fs);
	return ret;
}

/* These are here just in case some old ia32 binary calls it. */
asmlinkage long sys32_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}


#ifdef CONFIG_SYSCTL_SYSCALL
struct sysctl_ia32 {
	unsigned int	name;
	int		nlen;
	unsigned int	oldval;
	unsigned int	oldlenp;
	unsigned int	newval;
	unsigned int	newlen;
	unsigned int	__unused[4];
};


asmlinkage long sys32_sysctl(struct sysctl_ia32 __user *args32)
{
	struct sysctl_ia32 a32;
	mm_segment_t old_fs = get_fs();
	void __user *oldvalp, *newvalp;
	size_t oldlen;
	int __user *namep;
	long ret;

	if (copy_from_user(&a32, args32, sizeof(a32)))
		return -EFAULT;

	/*
	 * We need to pre-validate these because we have to disable
	 * address checking before calling do_sysctl() because of
	 * OLDLEN but we can't run the risk of the user specifying bad
	 * addresses here.  Well, since we're dealing with 32 bit
	 * addresses, we KNOW that access_ok() will always succeed, so
	 * this is an expensive NOP, but so what...
	 */
	namep = compat_ptr(a32.name);
	oldvalp = compat_ptr(a32.oldval);
	newvalp =  compat_ptr(a32.newval);

	if ((oldvalp && get_user(oldlen, (int __user *)compat_ptr(a32.oldlenp)))
	    || !access_ok(VERIFY_WRITE, namep, 0)
	    || !access_ok(VERIFY_WRITE, oldvalp, 0)
	    || !access_ok(VERIFY_WRITE, newvalp, 0))
		return -EFAULT;

	set_fs(KERNEL_DS);
	lock_kernel();
	ret = do_sysctl(namep, a32.nlen, oldvalp, (size_t __user *)&oldlen,
			newvalp, (size_t) a32.newlen);
	unlock_kernel();
	set_fs(old_fs);

	if (oldvalp && put_user(oldlen, (int __user *)compat_ptr(a32.oldlenp)))
		return -EFAULT;

	return ret;
}
#endif

/* warning: next two assume little endian */
asmlinkage long sys32_pread(unsigned int fd, char __user *ubuf, u32 count,
			    u32 poslo, u32 poshi)
{
	return sys_pread64(fd, ubuf, count,
			 ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage long sys32_pwrite(unsigned int fd, char __user *ubuf, u32 count,
			     u32 poslo, u32 poshi)
{
	return sys_pwrite64(fd, ubuf, count,
			  ((loff_t)AA(poshi) << 32) | AA(poslo));
}


asmlinkage long sys32_personality(unsigned long personality)
{
	int ret;

	if (personality(current->personality) == PER_LINUX32 &&
		personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	if (ret == PER_LINUX32)
		ret = PER_LINUX;
	return ret;
}

asmlinkage long sys32_sendfile(int out_fd, int in_fd,
			       compat_off_t __user *offset, s32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	off_t of;

	if (offset && get_user(of, offset))
		return -EFAULT;

	set_fs(KERNEL_DS);
	ret = sys_sendfile(out_fd, in_fd, offset ? (off_t __user *)&of : NULL,
			   count);
	set_fs(old_fs);

	if (offset && put_user(of, offset))
		return -EFAULT;
	return ret;
}

asmlinkage long sys32_mmap2(unsigned long addr, unsigned long len,
			    unsigned long prot, unsigned long flags,
			    unsigned long fd, unsigned long pgoff)
{
	struct mm_struct *mm = current->mm;
	unsigned long error;
	struct file *file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			return -EBADF;
	}

	down_write(&mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&mm->mmap_sem);

	if (file)
		fput(file);
	return error;
}

asmlinkage long sys32_olduname(struct oldold_utsname __user *name)
{
	char *arch = "x86_64";
	int err;

	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE, name, sizeof(struct oldold_utsname)))
		return -EFAULT;

	down_read(&uts_sem);

	err = __copy_to_user(&name->sysname, &utsname()->sysname,
			     __OLD_UTS_LEN);
	err |= __put_user(0, name->sysname+__OLD_UTS_LEN);
	err |= __copy_to_user(&name->nodename, &utsname()->nodename,
			      __OLD_UTS_LEN);
	err |= __put_user(0, name->nodename+__OLD_UTS_LEN);
	err |= __copy_to_user(&name->release, &utsname()->release,
			      __OLD_UTS_LEN);
	err |= __put_user(0, name->release+__OLD_UTS_LEN);
	err |= __copy_to_user(&name->version, &utsname()->version,
			      __OLD_UTS_LEN);
	err |= __put_user(0, name->version+__OLD_UTS_LEN);

	if (personality(current->personality) == PER_LINUX32)
		arch = "i686";

	err |= __copy_to_user(&name->machine, arch, strlen(arch) + 1);

	up_read(&uts_sem);

	err = err ? -EFAULT : 0;

	return err;
}

long sys32_uname(struct old_utsname __user *name)
{
	int err;

	if (!name)
		return -EFAULT;
	down_read(&uts_sem);
	err = copy_to_user(name, utsname(), sizeof(*name));
	up_read(&uts_sem);
	if (personality(current->personality) == PER_LINUX32)
		err |= copy_to_user(&name->machine, "i686", 5);

	return err ? -EFAULT : 0;
}

long sys32_ustat(unsigned dev, struct ustat32 __user *u32p)
{
	struct ustat u;
	mm_segment_t seg;
	int ret;

	seg = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ustat(dev, (struct ustat __user *)&u);
	set_fs(seg);
	if (ret < 0)
		return ret;

	if (!access_ok(VERIFY_WRITE, u32p, sizeof(struct ustat32)) ||
	    __put_user((__u32) u.f_tfree, &u32p->f_tfree) ||
	    __put_user((__u32) u.f_tinode, &u32p->f_tfree) ||
	    __copy_to_user(&u32p->f_fname, u.f_fname, sizeof(u.f_fname)) ||
	    __copy_to_user(&u32p->f_fpack, u.f_fpack, sizeof(u.f_fpack)))
		ret = -EFAULT;
	return ret;
}

asmlinkage long sys32_execve(char __user *name, compat_uptr_t __user *argv,
			     compat_uptr_t __user *envp, struct pt_regs *regs)
{
	long error;
	char *filename;

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		return error;
	error = compat_do_execve(filename, argv, envp, regs);
	if (error == 0) {
		task_lock(current);
		current->ptrace &= ~PT_DTRACE;
		task_unlock(current);
	}
	putname(filename);
	return error;
}

asmlinkage long sys32_clone(unsigned int clone_flags, unsigned int newsp,
			    struct pt_regs *regs)
{
	void __user *parent_tid = (void __user *)regs->dx;
	void __user *child_tid = (void __user *)regs->di;

	if (!newsp)
		newsp = regs->sp;
	return do_fork(clone_flags, newsp, regs, 0, parent_tid, child_tid);
}

/*
 * Some system calls that need sign extended arguments. This could be
 * done by a generic wrapper.
 */
long sys32_lseek(unsigned int fd, int offset, unsigned int whence)
{
	return sys_lseek(fd, offset, whence);
}

long sys32_kill(int pid, int sig)
{
	return sys_kill(pid, sig);
}

long sys32_fadvise64_64(int fd, __u32 offset_low, __u32 offset_high,
			__u32 len_low, __u32 len_high, int advice)
{
	return sys_fadvise64_64(fd,
			       (((u64)offset_high)<<32) | offset_low,
			       (((u64)len_high)<<32) | len_low,
				advice);
}

long sys32_vm86_warning(void)
{
	struct task_struct *me = current;
	static char lastcomm[sizeof(me->comm)];

	if (strncmp(lastcomm, me->comm, sizeof(lastcomm))) {
		compat_printk(KERN_INFO
			      "%s: vm86 mode not supported on 64 bit kernel\n",
			      me->comm);
		strncpy(lastcomm, me->comm, sizeof(lastcomm));
	}
	return -ENOSYS;
}

long sys32_lookup_dcookie(u32 addr_low, u32 addr_high,
			  char __user *buf, size_t len)
{
	return sys_lookup_dcookie(((u64)addr_high << 32) | addr_low, buf, len);
}

asmlinkage ssize_t sys32_readahead(int fd, unsigned off_lo, unsigned off_hi,
				   size_t count)
{
	return sys_readahead(fd, ((u64)off_hi << 32) | off_lo, count);
}

asmlinkage long sys32_sync_file_range(int fd, unsigned off_low, unsigned off_hi,
				      unsigned n_low, unsigned n_hi,  int flags)
{
	return sys_sync_file_range(fd,
				   ((u64)off_hi << 32) | off_low,
				   ((u64)n_hi << 32) | n_low, flags);
}

asmlinkage long sys32_fadvise64(int fd, unsigned offset_lo, unsigned offset_hi,
				size_t len, int advice)
{
	return sys_fadvise64_64(fd, ((u64)offset_hi << 32) | offset_lo,
				len, advice);
}

asmlinkage long sys32_fallocate(int fd, int mode, unsigned offset_lo,
				unsigned offset_hi, unsigned len_lo,
				unsigned len_hi)
{
	return sys_fallocate(fd, mode, ((u64)offset_hi << 32) | offset_lo,
			     ((u64)len_hi << 32) | len_lo);
}
