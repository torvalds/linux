/*
 *  arch/s390x/kernel/linux32.c
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Gerhard Tonn (ton@de.ibm.com)   
 *               Thomas Spatzier (tspat@de.ibm.com)
 *
 *  Conversion between 31bit and 64bit native syscalls.
 *
 * Heavily inspired by the 32-bit Sparc compat code which is 
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 */


#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h> 
#include <linux/mm.h> 
#include <linux/file.h> 
#include <linux/signal.h>
#include <linux/resource.h>
#include <linux/times.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/quota.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/personality.h>
#include <linux/stat.h>
#include <linux/filter.h>
#include <linux/highmem.h>
#include <linux/highuid.h>
#include <linux/mman.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/icmpv6.h>
#include <linux/syscalls.h>
#include <linux/sysctl.h>
#include <linux/binfmts.h>
#include <linux/capability.h>
#include <linux/compat.h>
#include <linux/vfs.h>
#include <linux/ptrace.h>
#include <linux/fadvise.h>
#include <linux/ipc.h>

#include <asm/types.h>
#include <asm/uaccess.h>

#include <net/scm.h>
#include <net/sock.h>

#include "compat_linux.h"

long psw_user32_bits	= (PSW_BASE32_BITS | PSW_MASK_DAT | PSW_ASC_HOME |
			   PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK |
			   PSW_MASK_PSTATE | PSW_DEFAULT_KEY);
long psw32_user_bits	= (PSW32_BASE_BITS | PSW32_MASK_DAT | PSW32_ASC_HOME |
			   PSW32_MASK_IO | PSW32_MASK_EXT | PSW32_MASK_MCHECK |
			   PSW32_MASK_PSTATE);
 
/* For this source file, we want overflow handling. */

#undef high2lowuid
#undef high2lowgid
#undef low2highuid
#undef low2highgid
#undef SET_UID16
#undef SET_GID16
#undef NEW_TO_OLD_UID
#undef NEW_TO_OLD_GID
#undef SET_OLDSTAT_UID
#undef SET_OLDSTAT_GID
#undef SET_STAT_UID
#undef SET_STAT_GID

#define high2lowuid(uid) ((uid) > 65535) ? (u16)overflowuid : (u16)(uid)
#define high2lowgid(gid) ((gid) > 65535) ? (u16)overflowgid : (u16)(gid)
#define low2highuid(uid) ((uid) == (u16)-1) ? (uid_t)-1 : (uid_t)(uid)
#define low2highgid(gid) ((gid) == (u16)-1) ? (gid_t)-1 : (gid_t)(gid)
#define SET_UID16(var, uid)	var = high2lowuid(uid)
#define SET_GID16(var, gid)	var = high2lowgid(gid)
#define NEW_TO_OLD_UID(uid)	high2lowuid(uid)
#define NEW_TO_OLD_GID(gid)	high2lowgid(gid)
#define SET_OLDSTAT_UID(stat, uid)	(stat).st_uid = high2lowuid(uid)
#define SET_OLDSTAT_GID(stat, gid)	(stat).st_gid = high2lowgid(gid)
#define SET_STAT_UID(stat, uid)		(stat).st_uid = high2lowuid(uid)
#define SET_STAT_GID(stat, gid)		(stat).st_gid = high2lowgid(gid)

asmlinkage long sys32_chown16(const char __user * filename, u16 user, u16 group)
{
	return sys_chown(filename, low2highuid(user), low2highgid(group));
}

asmlinkage long sys32_lchown16(const char __user * filename, u16 user, u16 group)
{
	return sys_lchown(filename, low2highuid(user), low2highgid(group));
}

asmlinkage long sys32_fchown16(unsigned int fd, u16 user, u16 group)
{
	return sys_fchown(fd, low2highuid(user), low2highgid(group));
}

asmlinkage long sys32_setregid16(u16 rgid, u16 egid)
{
	return sys_setregid(low2highgid(rgid), low2highgid(egid));
}

asmlinkage long sys32_setgid16(u16 gid)
{
	return sys_setgid((gid_t)gid);
}

asmlinkage long sys32_setreuid16(u16 ruid, u16 euid)
{
	return sys_setreuid(low2highuid(ruid), low2highuid(euid));
}

asmlinkage long sys32_setuid16(u16 uid)
{
	return sys_setuid((uid_t)uid);
}

asmlinkage long sys32_setresuid16(u16 ruid, u16 euid, u16 suid)
{
	return sys_setresuid(low2highuid(ruid), low2highuid(euid),
		low2highuid(suid));
}

asmlinkage long sys32_getresuid16(u16 __user *ruid, u16 __user *euid, u16 __user *suid)
{
	int retval;

	if (!(retval = put_user(high2lowuid(current->cred->uid), ruid)) &&
	    !(retval = put_user(high2lowuid(current->cred->euid), euid)))
		retval = put_user(high2lowuid(current->cred->suid), suid);

	return retval;
}

asmlinkage long sys32_setresgid16(u16 rgid, u16 egid, u16 sgid)
{
	return sys_setresgid(low2highgid(rgid), low2highgid(egid),
		low2highgid(sgid));
}

asmlinkage long sys32_getresgid16(u16 __user *rgid, u16 __user *egid, u16 __user *sgid)
{
	int retval;

	if (!(retval = put_user(high2lowgid(current->cred->gid), rgid)) &&
	    !(retval = put_user(high2lowgid(current->cred->egid), egid)))
		retval = put_user(high2lowgid(current->cred->sgid), sgid);

	return retval;
}

asmlinkage long sys32_setfsuid16(u16 uid)
{
	return sys_setfsuid((uid_t)uid);
}

asmlinkage long sys32_setfsgid16(u16 gid)
{
	return sys_setfsgid((gid_t)gid);
}

static int groups16_to_user(u16 __user *grouplist, struct group_info *group_info)
{
	int i;
	u16 group;

	for (i = 0; i < group_info->ngroups; i++) {
		group = (u16)GROUP_AT(group_info, i);
		if (put_user(group, grouplist+i))
			return -EFAULT;
	}

	return 0;
}

static int groups16_from_user(struct group_info *group_info, u16 __user *grouplist)
{
	int i;
	u16 group;

	for (i = 0; i < group_info->ngroups; i++) {
		if (get_user(group, grouplist+i))
			return  -EFAULT;
		GROUP_AT(group_info, i) = (gid_t)group;
	}

	return 0;
}

asmlinkage long sys32_getgroups16(int gidsetsize, u16 __user *grouplist)
{
	int i;

	if (gidsetsize < 0)
		return -EINVAL;

	get_group_info(current->cred->group_info);
	i = current->cred->group_info->ngroups;
	if (gidsetsize) {
		if (i > gidsetsize) {
			i = -EINVAL;
			goto out;
		}
		if (groups16_to_user(grouplist, current->cred->group_info)) {
			i = -EFAULT;
			goto out;
		}
	}
out:
	put_group_info(current->cred->group_info);
	return i;
}

asmlinkage long sys32_setgroups16(int gidsetsize, u16 __user *grouplist)
{
	struct group_info *group_info;
	int retval;

	if (!capable(CAP_SETGID))
		return -EPERM;
	if ((unsigned)gidsetsize > NGROUPS_MAX)
		return -EINVAL;

	group_info = groups_alloc(gidsetsize);
	if (!group_info)
		return -ENOMEM;
	retval = groups16_from_user(group_info, grouplist);
	if (retval) {
		put_group_info(group_info);
		return retval;
	}

	retval = set_current_groups(group_info);
	put_group_info(group_info);

	return retval;
}

asmlinkage long sys32_getuid16(void)
{
	return high2lowuid(current->cred->uid);
}

asmlinkage long sys32_geteuid16(void)
{
	return high2lowuid(current->cred->euid);
}

asmlinkage long sys32_getgid16(void)
{
	return high2lowgid(current->cred->gid);
}

asmlinkage long sys32_getegid16(void)
{
	return high2lowgid(current->cred->egid);
}

/*
 * sys32_ipc() is the de-multiplexer for the SysV IPC calls in 32bit emulation.
 *
 * This is really horribly ugly.
 */
#ifdef CONFIG_SYSVIPC
asmlinkage long sys32_ipc(u32 call, int first, int second, int third, u32 ptr)
{
	if (call >> 16)		/* hack for backward compatibility */
		return -EINVAL;

	call &= 0xffff;

	switch (call) {
	case SEMTIMEDOP:
		return compat_sys_semtimedop(first, compat_ptr(ptr),
					     second, compat_ptr(third));
	case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		return sys_semtimedop(first, compat_ptr(ptr),
				      second, NULL);
	case SEMGET:
		return sys_semget(first, second, third);
	case SEMCTL:
		return compat_sys_semctl(first, second, third,
					 compat_ptr(ptr));
	case MSGSND:
		return compat_sys_msgsnd(first, second, third,
					 compat_ptr(ptr));
	case MSGRCV:
		return compat_sys_msgrcv(first, second, 0, third,
					 0, compat_ptr(ptr));
	case MSGGET:
		return sys_msgget((key_t) first, second);
	case MSGCTL:
		return compat_sys_msgctl(first, second, compat_ptr(ptr));
	case SHMAT:
		return compat_sys_shmat(first, second, third,
					0, compat_ptr(ptr));
	case SHMDT:
		return sys_shmdt(compat_ptr(ptr));
	case SHMGET:
		return sys_shmget(first, (unsigned)second, third);
	case SHMCTL:
		return compat_sys_shmctl(first, second, compat_ptr(ptr));
	}

	return -ENOSYS;
}
#endif

asmlinkage long sys32_truncate64(const char __user * path, unsigned long high, unsigned long low)
{
	if ((int)high < 0)
		return -EINVAL;
	else
		return sys_truncate(path, (high << 32) | low);
}

asmlinkage long sys32_ftruncate64(unsigned int fd, unsigned long high, unsigned long low)
{
	if ((int)high < 0)
		return -EINVAL;
	else
		return sys_ftruncate(fd, (high << 32) | low);
}

asmlinkage long sys32_sched_rr_get_interval(compat_pid_t pid,
				struct compat_timespec __user *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid,
					(struct timespec __force __user *) &t);
	set_fs (old_fs);
	if (put_compat_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

asmlinkage long sys32_rt_sigprocmask(int how, compat_sigset_t __user *set,
			compat_sigset_t __user *oset, size_t sigsetsize)
{
	sigset_t s;
	compat_sigset_t s32;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (set) {
		if (copy_from_user (&s32, set, sizeof(compat_sigset_t)))
			return -EFAULT;
		switch (_NSIG_WORDS) {
		case 4: s.sig[3] = s32.sig[6] | (((long)s32.sig[7]) << 32);
		case 3: s.sig[2] = s32.sig[4] | (((long)s32.sig[5]) << 32);
		case 2: s.sig[1] = s32.sig[2] | (((long)s32.sig[3]) << 32);
		case 1: s.sig[0] = s32.sig[0] | (((long)s32.sig[1]) << 32);
		}
	}
	set_fs (KERNEL_DS);
	ret = sys_rt_sigprocmask(how,
				 set ? (sigset_t __force __user *) &s : NULL,
				 oset ? (sigset_t __force __user *) &s : NULL,
				 sigsetsize);
	set_fs (old_fs);
	if (ret) return ret;
	if (oset) {
		switch (_NSIG_WORDS) {
		case 4: s32.sig[7] = (s.sig[3] >> 32); s32.sig[6] = s.sig[3];
		case 3: s32.sig[5] = (s.sig[2] >> 32); s32.sig[4] = s.sig[2];
		case 2: s32.sig[3] = (s.sig[1] >> 32); s32.sig[2] = s.sig[1];
		case 1: s32.sig[1] = (s.sig[0] >> 32); s32.sig[0] = s.sig[0];
		}
		if (copy_to_user (oset, &s32, sizeof(compat_sigset_t)))
			return -EFAULT;
	}
	return 0;
}

asmlinkage long sys32_rt_sigpending(compat_sigset_t __user *set,
				size_t sigsetsize)
{
	sigset_t s;
	compat_sigset_t s32;
	int ret;
	mm_segment_t old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_rt_sigpending((sigset_t __force __user *) &s, sigsetsize);
	set_fs (old_fs);
	if (!ret) {
		switch (_NSIG_WORDS) {
		case 4: s32.sig[7] = (s.sig[3] >> 32); s32.sig[6] = s.sig[3];
		case 3: s32.sig[5] = (s.sig[2] >> 32); s32.sig[4] = s.sig[2];
		case 2: s32.sig[3] = (s.sig[1] >> 32); s32.sig[2] = s.sig[1];
		case 1: s32.sig[1] = (s.sig[0] >> 32); s32.sig[0] = s.sig[0];
		}
		if (copy_to_user (set, &s32, sizeof(compat_sigset_t)))
			return -EFAULT;
	}
	return ret;
}

asmlinkage long
sys32_rt_sigqueueinfo(int pid, int sig, compat_siginfo_t __user *uinfo)
{
	siginfo_t info;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (copy_siginfo_from_user32(&info, uinfo))
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_rt_sigqueueinfo(pid, sig, (siginfo_t __force __user *) &info);
	set_fs (old_fs);
	return ret;
}

/*
 * sys32_execve() executes a new program after the asm stub has set
 * things up for us.  This should basically do what I want it to.
 */
asmlinkage long sys32_execve(char __user *name, compat_uptr_t __user *argv,
			     compat_uptr_t __user *envp)
{
	struct pt_regs *regs = task_pt_regs(current);
	char *filename;
	long rc;

	filename = getname(name);
	rc = PTR_ERR(filename);
	if (IS_ERR(filename))
		return rc;
	rc = compat_do_execve(filename, argv, envp, regs);
	if (rc)
		goto out;
	current->thread.fp_regs.fpc=0;
	asm volatile("sfpc %0,0" : : "d" (0));
	rc = regs->gprs[2];
out:
	putname(filename);
	return rc;
}

asmlinkage long sys32_pread64(unsigned int fd, char __user *ubuf,
				size_t count, u32 poshi, u32 poslo)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL;
	return sys_pread64(fd, ubuf, count, ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage long sys32_pwrite64(unsigned int fd, const char __user *ubuf,
				size_t count, u32 poshi, u32 poslo)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL;
	return sys_pwrite64(fd, ubuf, count, ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage compat_ssize_t sys32_readahead(int fd, u32 offhi, u32 offlo, s32 count)
{
	return sys_readahead(fd, ((loff_t)AA(offhi) << 32) | AA(offlo), count);
}

asmlinkage long sys32_sendfile(int out_fd, int in_fd, compat_off_t __user *offset, size_t count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	off_t of;
	
	if (offset && get_user(of, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile(out_fd, in_fd,
			   offset ? (off_t __force __user *) &of : NULL, count);
	set_fs(old_fs);
	
	if (offset && put_user(of, offset))
		return -EFAULT;
		
	return ret;
}

asmlinkage long sys32_sendfile64(int out_fd, int in_fd,
				compat_loff_t __user *offset, s32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	loff_t lof;
	
	if (offset && get_user(lof, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile64(out_fd, in_fd,
			     offset ? (loff_t __force __user *) &lof : NULL,
			     count);
	set_fs(old_fs);
	
	if (offset && put_user(lof, offset))
		return -EFAULT;
		
	return ret;
}

struct stat64_emu31 {
	unsigned long long  st_dev;
	unsigned int    __pad1;
#define STAT64_HAS_BROKEN_ST_INO        1
	u32             __st_ino;
	unsigned int    st_mode;
	unsigned int    st_nlink;
	u32             st_uid;
	u32             st_gid;
	unsigned long long  st_rdev;
	unsigned int    __pad3;
	long            st_size;
	u32             st_blksize;
	unsigned char   __pad4[4];
	u32             __pad5;     /* future possible st_blocks high bits */
	u32             st_blocks;  /* Number 512-byte blocks allocated. */
	u32             st_atime;
	u32             __pad6;
	u32             st_mtime;
	u32             __pad7;
	u32             st_ctime;
	u32             __pad8;     /* will be high 32 bits of ctime someday */
	unsigned long   st_ino;
};	

static int cp_stat64(struct stat64_emu31 __user *ubuf, struct kstat *stat)
{
	struct stat64_emu31 tmp;

	memset(&tmp, 0, sizeof(tmp));

	tmp.st_dev = huge_encode_dev(stat->dev);
	tmp.st_ino = stat->ino;
	tmp.__st_ino = (u32)stat->ino;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = (unsigned int)stat->nlink;
	tmp.st_uid = stat->uid;
	tmp.st_gid = stat->gid;
	tmp.st_rdev = huge_encode_dev(stat->rdev);
	tmp.st_size = stat->size;
	tmp.st_blksize = (u32)stat->blksize;
	tmp.st_blocks = (u32)stat->blocks;
	tmp.st_atime = (u32)stat->atime.tv_sec;
	tmp.st_mtime = (u32)stat->mtime.tv_sec;
	tmp.st_ctime = (u32)stat->ctime.tv_sec;

	return copy_to_user(ubuf,&tmp,sizeof(tmp)) ? -EFAULT : 0; 
}

asmlinkage long sys32_stat64(char __user * filename, struct stat64_emu31 __user * statbuf)
{
	struct kstat stat;
	int ret = vfs_stat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long sys32_lstat64(char __user * filename, struct stat64_emu31 __user * statbuf)
{
	struct kstat stat;
	int ret = vfs_lstat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long sys32_fstat64(unsigned long fd, struct stat64_emu31 __user * statbuf)
{
	struct kstat stat;
	int ret = vfs_fstat(fd, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long sys32_fstatat64(unsigned int dfd, char __user *filename,
				struct stat64_emu31 __user* statbuf, int flag)
{
	struct kstat stat;
	int error;

	error = vfs_fstatat(dfd, filename, &stat, flag);
	if (error)
		return error;
	return cp_stat64(statbuf, &stat);
}

/*
 * Linux/i386 didn't use to be able to handle more than
 * 4 system call parameters, so these system calls used a memory
 * block for parameter passing..
 */

struct mmap_arg_struct_emu31 {
	compat_ulong_t addr;
	compat_ulong_t len;
	compat_ulong_t prot;
	compat_ulong_t flags;
	compat_ulong_t fd;
	compat_ulong_t offset;
};

asmlinkage unsigned long old32_mmap(struct mmap_arg_struct_emu31 __user *arg)
{
	struct mmap_arg_struct_emu31 a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	if (a.offset & ~PAGE_MASK)
		return -EINVAL;
	a.addr = (unsigned long) compat_ptr(a.addr);
	return sys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd,
			      a.offset >> PAGE_SHIFT);
}

asmlinkage long sys32_mmap2(struct mmap_arg_struct_emu31 __user *arg)
{
	struct mmap_arg_struct_emu31 a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	a.addr = (unsigned long) compat_ptr(a.addr);
	return sys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd, a.offset);
}

asmlinkage long sys32_read(unsigned int fd, char __user * buf, size_t count)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL; 

	return sys_read(fd, buf, count);
}

asmlinkage long sys32_write(unsigned int fd, char __user * buf, size_t count)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL; 

	return sys_write(fd, buf, count);
}

/*
 * 31 bit emulation wrapper functions for sys_fadvise64/fadvise64_64.
 * These need to rewrite the advise values for POSIX_FADV_{DONTNEED,NOREUSE}
 * because the 31 bit values differ from the 64 bit values.
 */

asmlinkage long
sys32_fadvise64(int fd, loff_t offset, size_t len, int advise)
{
	if (advise == 4)
		advise = POSIX_FADV_DONTNEED;
	else if (advise == 5)
		advise = POSIX_FADV_NOREUSE;
	return sys_fadvise64(fd, offset, len, advise);
}

struct fadvise64_64_args {
	int fd;
	long long offset;
	long long len;
	int advice;
};

asmlinkage long
sys32_fadvise64_64(struct fadvise64_64_args __user *args)
{
	struct fadvise64_64_args a;

	if ( copy_from_user(&a, args, sizeof(a)) )
		return -EFAULT;
	if (a.advice == 4)
		a.advice = POSIX_FADV_DONTNEED;
	else if (a.advice == 5)
		a.advice = POSIX_FADV_NOREUSE;
	return sys_fadvise64_64(a.fd, a.offset, a.len, a.advice);
}
