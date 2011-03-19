/*
 * Conversion between 32-bit and 64-bit native system calls.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Written by Ulf Carlsson (ulfc@engr.sgi.com)
 * sys32_execve from ia64/ia32 code, Feb 2000, Kanoj Sarcar (kanoj@sgi.com)
 */
#include <linux/compiler.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/highuid.h>
#include <linux/resource.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/times.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/filter.h>
#include <linux/shm.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/icmpv6.h>
#include <linux/syscalls.h>
#include <linux/sysctl.h>
#include <linux/utime.h>
#include <linux/utsname.h>
#include <linux/personality.h>
#include <linux/dnotify.h>
#include <linux/module.h>
#include <linux/binfmts.h>
#include <linux/security.h>
#include <linux/compat.h>
#include <linux/vfs.h>
#include <linux/ipc.h>
#include <linux/slab.h>

#include <net/sock.h>
#include <net/scm.h>

#include <asm/compat-signal.h>
#include <asm/sim.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/mman.h>

/* Use this to get at 32-bit user passed pointers. */
/* A() macro should be used for places where you e.g.
   have some internal variable u32 and just want to get
   rid of a compiler warning. AA() has to be used in
   places where you want to convert a function argument
   to 32bit pointer or when you e.g. access pt_regs
   structure and want to consider 32bit registers only.
 */
#define A(__x) ((unsigned long)(__x))
#define AA(__x) ((unsigned long)((int)__x))

#ifdef __MIPSEB__
#define merge_64(r1, r2) ((((r1) & 0xffffffffUL) << 32) + ((r2) & 0xffffffffUL))
#endif
#ifdef __MIPSEL__
#define merge_64(r1, r2) ((((r2) & 0xffffffffUL) << 32) + ((r1) & 0xffffffffUL))
#endif

SYSCALL_DEFINE6(32_mmap2, unsigned long, addr, unsigned long, len,
	unsigned long, prot, unsigned long, flags, unsigned long, fd,
	unsigned long, pgoff)
{
	unsigned long error;

	error = -EINVAL;
	if (pgoff & (~PAGE_MASK >> 12))
		goto out;
	error = sys_mmap_pgoff(addr, len, prot, flags, fd,
			       pgoff >> (PAGE_SHIFT-12));
out:
	return error;
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys32_execve(nabi_no_regargs struct pt_regs regs)
{
	int error;
	char * filename;

	filename = getname(compat_ptr(regs.regs[4]));
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = compat_do_execve(filename, compat_ptr(regs.regs[5]),
				 compat_ptr(regs.regs[6]), &regs);
	putname(filename);

out:
	return error;
}

#define RLIM_INFINITY32	0x7fffffff
#define RESOURCE32(x) ((x > RLIM_INFINITY32) ? RLIM_INFINITY32 : x)

struct rlimit32 {
	int	rlim_cur;
	int	rlim_max;
};

SYSCALL_DEFINE4(32_truncate64, const char __user *, path,
	unsigned long, __dummy, unsigned long, a2, unsigned long, a3)
{
	return sys_truncate(path, merge_64(a2, a3));
}

SYSCALL_DEFINE4(32_ftruncate64, unsigned long, fd, unsigned long, __dummy,
	unsigned long, a2, unsigned long, a3)
{
	return sys_ftruncate(fd, merge_64(a2, a3));
}

SYSCALL_DEFINE5(32_llseek, unsigned int, fd, unsigned int, offset_high,
		unsigned int, offset_low, loff_t __user *, result,
		unsigned int, origin)
{
	return sys_llseek(fd, offset_high, offset_low, result, origin);
}

/* From the Single Unix Spec: pread & pwrite act like lseek to pos + op +
   lseek back to original location.  They fail just like lseek does on
   non-seekable files.  */

SYSCALL_DEFINE6(32_pread, unsigned long, fd, char __user *, buf, size_t, count,
	unsigned long, unused, unsigned long, a4, unsigned long, a5)
{
	return sys_pread64(fd, buf, count, merge_64(a4, a5));
}

SYSCALL_DEFINE6(32_pwrite, unsigned int, fd, const char __user *, buf,
	size_t, count, u32, unused, u64, a4, u64, a5)
{
	return sys_pwrite64(fd, buf, count, merge_64(a4, a5));
}

SYSCALL_DEFINE2(32_sched_rr_get_interval, compat_pid_t, pid,
	struct compat_timespec __user *, interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid, (struct timespec __user *)&t);
	set_fs(old_fs);
	if (put_user (t.tv_sec, &interval->tv_sec) ||
	    __put_user(t.tv_nsec, &interval->tv_nsec))
		return -EFAULT;
	return ret;
}

#ifdef CONFIG_SYSVIPC

SYSCALL_DEFINE6(32_ipc, u32, call, long, first, long, second, long, third,
	unsigned long, ptr, unsigned long, fifth)
{
	int version, err;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		err = sys_semtimedop(first, compat_ptr(ptr), second, NULL);
		break;
	case SEMTIMEDOP:
		err = compat_sys_semtimedop(first, compat_ptr(ptr), second,
					    compat_ptr(fifth));
		break;
	case SEMGET:
		err = sys_semget(first, second, third);
		break;
	case SEMCTL:
		err = compat_sys_semctl(first, second, third, compat_ptr(ptr));
		break;
	case MSGSND:
		err = compat_sys_msgsnd(first, second, third, compat_ptr(ptr));
		break;
	case MSGRCV:
		err = compat_sys_msgrcv(first, second, fifth, third,
					version, compat_ptr(ptr));
		break;
	case MSGGET:
		err = sys_msgget((key_t) first, second);
		break;
	case MSGCTL:
		err = compat_sys_msgctl(first, second, compat_ptr(ptr));
		break;
	case SHMAT:
		err = compat_sys_shmat(first, second, third, version,
				       compat_ptr(ptr));
		break;
	case SHMDT:
		err = sys_shmdt(compat_ptr(ptr));
		break;
	case SHMGET:
		err = sys_shmget(first, (unsigned)second, third);
		break;
	case SHMCTL:
		err = compat_sys_shmctl(first, second, compat_ptr(ptr));
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

#else

SYSCALL_DEFINE6(32_ipc, u32, call, int, first, int, second, int, third,
	u32, ptr, u32, fifth)
{
	return -ENOSYS;
}

#endif /* CONFIG_SYSVIPC */

#ifdef CONFIG_MIPS32_N32
SYSCALL_DEFINE4(n32_semctl, int, semid, int, semnum, int, cmd, u32, arg)
{
	/* compat_sys_semctl expects a pointer to union semun */
	u32 __user *uptr = compat_alloc_user_space(sizeof(u32));
	if (put_user(arg, uptr))
		return -EFAULT;
	return compat_sys_semctl(semid, semnum, cmd, uptr);
}

SYSCALL_DEFINE4(n32_msgsnd, int, msqid, u32, msgp, unsigned int, msgsz,
	int, msgflg)
{
	return compat_sys_msgsnd(msqid, msgsz, msgflg, compat_ptr(msgp));
}

SYSCALL_DEFINE5(n32_msgrcv, int, msqid, u32, msgp, size_t, msgsz,
	int, msgtyp, int, msgflg)
{
	return compat_sys_msgrcv(msqid, msgsz, msgtyp, msgflg, IPC_64,
				 compat_ptr(msgp));
}
#endif

SYSCALL_DEFINE1(32_personality, unsigned long, personality)
{
	unsigned int p = personality & 0xffffffff;
	int ret;

	if (personality(current->personality) == PER_LINUX32 &&
	    personality(p) == PER_LINUX)
		p = (p & ~PER_MASK) | PER_LINUX32;
	ret = sys_personality(p);
	if (ret != -1 && personality(ret) == PER_LINUX32)
		ret = (ret & ~PER_MASK) | PER_LINUX;
	return ret;
}

SYSCALL_DEFINE4(32_sendfile, long, out_fd, long, in_fd,
	compat_off_t __user *, offset, s32, count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	off_t of;

	if (offset && get_user(of, offset))
		return -EFAULT;

	set_fs(KERNEL_DS);
	ret = sys_sendfile(out_fd, in_fd, offset ? (off_t __user *)&of : NULL, count);
	set_fs(old_fs);

	if (offset && put_user(of, offset))
		return -EFAULT;

	return ret;
}

asmlinkage ssize_t sys32_readahead(int fd, u32 pad0, u64 a2, u64 a3,
                                   size_t count)
{
	return sys_readahead(fd, merge_64(a2, a3), count);
}

asmlinkage long sys32_sync_file_range(int fd, int __pad,
	unsigned long a2, unsigned long a3,
	unsigned long a4, unsigned long a5,
	int flags)
{
	return sys_sync_file_range(fd,
			merge_64(a2, a3), merge_64(a4, a5),
			flags);
}

asmlinkage long sys32_fadvise64_64(int fd, int __pad,
	unsigned long a2, unsigned long a3,
	unsigned long a4, unsigned long a5,
	int flags)
{
	return sys_fadvise64_64(fd,
			merge_64(a2, a3), merge_64(a4, a5),
			flags);
}

asmlinkage long sys32_fallocate(int fd, int mode, unsigned offset_a2,
	unsigned offset_a3, unsigned len_a4, unsigned len_a5)
{
	return sys_fallocate(fd, mode, merge_64(offset_a2, offset_a3),
	                     merge_64(len_a4, len_a5));
}

save_static_function(sys32_clone);
static int noinline __used
_sys32_clone(nabi_no_regargs struct pt_regs regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int __user *parent_tidptr, *child_tidptr;

	clone_flags = regs.regs[4];
	newsp = regs.regs[5];
	if (!newsp)
		newsp = regs.regs[29];
	parent_tidptr = (int __user *) regs.regs[6];

	/* Use __dummy4 instead of getting it off the stack, so that
	   syscall() works.  */
	child_tidptr = (int __user *) __dummy4;
	return do_fork(clone_flags, newsp, &regs, 0,
	               parent_tidptr, child_tidptr);
}

asmlinkage long sys32_lookup_dcookie(u32 a0, u32 a1, char __user *buf,
	size_t len)
{
	return sys_lookup_dcookie(merge_64(a0, a1), buf, len);
}

SYSCALL_DEFINE6(32_fanotify_mark, int, fanotify_fd, unsigned int, flags,
		u64, a3, u64, a4, int, dfd, const char  __user *, pathname)
{
	return sys_fanotify_mark(fanotify_fd, flags, merge_64(a3, a4),
				 dfd, pathname);
}
