// SPDX-License-Identifier: GPL-2.0
/*
 *  S390 version
 *    Copyright IBM Corp. 2000
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
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/uio.h>
#include <linux/quota.h>
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
#include <linux/slab.h>

#include <asm/types.h>
#include <linux/uaccess.h>

#include <net/scm.h>
#include <net/sock.h>

#include "compat_linux.h"

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

COMPAT_SYSCALL_DEFINE3(s390_chown16, const char __user *, filename,
		       u16, user, u16, group)
{
	return ksys_chown(filename, low2highuid(user), low2highgid(group));
}

COMPAT_SYSCALL_DEFINE3(s390_lchown16, const char __user *,
		       filename, u16, user, u16, group)
{
	return ksys_lchown(filename, low2highuid(user), low2highgid(group));
}

COMPAT_SYSCALL_DEFINE3(s390_fchown16, unsigned int, fd, u16, user, u16, group)
{
	return ksys_fchown(fd, low2highuid(user), low2highgid(group));
}

COMPAT_SYSCALL_DEFINE2(s390_setregid16, u16, rgid, u16, egid)
{
	return sys_setregid(low2highgid(rgid), low2highgid(egid));
}

COMPAT_SYSCALL_DEFINE1(s390_setgid16, u16, gid)
{
	return sys_setgid(low2highgid(gid));
}

COMPAT_SYSCALL_DEFINE2(s390_setreuid16, u16, ruid, u16, euid)
{
	return sys_setreuid(low2highuid(ruid), low2highuid(euid));
}

COMPAT_SYSCALL_DEFINE1(s390_setuid16, u16, uid)
{
	return sys_setuid(low2highuid(uid));
}

COMPAT_SYSCALL_DEFINE3(s390_setresuid16, u16, ruid, u16, euid, u16, suid)
{
	return sys_setresuid(low2highuid(ruid), low2highuid(euid),
			     low2highuid(suid));
}

COMPAT_SYSCALL_DEFINE3(s390_getresuid16, u16 __user *, ruidp,
		       u16 __user *, euidp, u16 __user *, suidp)
{
	const struct cred *cred = current_cred();
	int retval;
	u16 ruid, euid, suid;

	ruid = high2lowuid(from_kuid_munged(cred->user_ns, cred->uid));
	euid = high2lowuid(from_kuid_munged(cred->user_ns, cred->euid));
	suid = high2lowuid(from_kuid_munged(cred->user_ns, cred->suid));

	if (!(retval   = put_user(ruid, ruidp)) &&
	    !(retval   = put_user(euid, euidp)))
		retval = put_user(suid, suidp);

	return retval;
}

COMPAT_SYSCALL_DEFINE3(s390_setresgid16, u16, rgid, u16, egid, u16, sgid)
{
	return sys_setresgid(low2highgid(rgid), low2highgid(egid),
			     low2highgid(sgid));
}

COMPAT_SYSCALL_DEFINE3(s390_getresgid16, u16 __user *, rgidp,
		       u16 __user *, egidp, u16 __user *, sgidp)
{
	const struct cred *cred = current_cred();
	int retval;
	u16 rgid, egid, sgid;

	rgid = high2lowgid(from_kgid_munged(cred->user_ns, cred->gid));
	egid = high2lowgid(from_kgid_munged(cred->user_ns, cred->egid));
	sgid = high2lowgid(from_kgid_munged(cred->user_ns, cred->sgid));

	if (!(retval   = put_user(rgid, rgidp)) &&
	    !(retval   = put_user(egid, egidp)))
		retval = put_user(sgid, sgidp);

	return retval;
}

COMPAT_SYSCALL_DEFINE1(s390_setfsuid16, u16, uid)
{
	return sys_setfsuid(low2highuid(uid));
}

COMPAT_SYSCALL_DEFINE1(s390_setfsgid16, u16, gid)
{
	return sys_setfsgid(low2highgid(gid));
}

static int groups16_to_user(u16 __user *grouplist, struct group_info *group_info)
{
	struct user_namespace *user_ns = current_user_ns();
	int i;
	u16 group;
	kgid_t kgid;

	for (i = 0; i < group_info->ngroups; i++) {
		kgid = group_info->gid[i];
		group = (u16)from_kgid_munged(user_ns, kgid);
		if (put_user(group, grouplist+i))
			return -EFAULT;
	}

	return 0;
}

static int groups16_from_user(struct group_info *group_info, u16 __user *grouplist)
{
	struct user_namespace *user_ns = current_user_ns();
	int i;
	u16 group;
	kgid_t kgid;

	for (i = 0; i < group_info->ngroups; i++) {
		if (get_user(group, grouplist+i))
			return  -EFAULT;

		kgid = make_kgid(user_ns, (gid_t)group);
		if (!gid_valid(kgid))
			return -EINVAL;

		group_info->gid[i] = kgid;
	}

	return 0;
}

COMPAT_SYSCALL_DEFINE2(s390_getgroups16, int, gidsetsize, u16 __user *, grouplist)
{
	const struct cred *cred = current_cred();
	int i;

	if (gidsetsize < 0)
		return -EINVAL;

	get_group_info(cred->group_info);
	i = cred->group_info->ngroups;
	if (gidsetsize) {
		if (i > gidsetsize) {
			i = -EINVAL;
			goto out;
		}
		if (groups16_to_user(grouplist, cred->group_info)) {
			i = -EFAULT;
			goto out;
		}
	}
out:
	put_group_info(cred->group_info);
	return i;
}

COMPAT_SYSCALL_DEFINE2(s390_setgroups16, int, gidsetsize, u16 __user *, grouplist)
{
	struct group_info *group_info;
	int retval;

	if (!may_setgroups())
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

	groups_sort(group_info);
	retval = set_current_groups(group_info);
	put_group_info(group_info);

	return retval;
}

COMPAT_SYSCALL_DEFINE0(s390_getuid16)
{
	return high2lowuid(from_kuid_munged(current_user_ns(), current_uid()));
}

COMPAT_SYSCALL_DEFINE0(s390_geteuid16)
{
	return high2lowuid(from_kuid_munged(current_user_ns(), current_euid()));
}

COMPAT_SYSCALL_DEFINE0(s390_getgid16)
{
	return high2lowgid(from_kgid_munged(current_user_ns(), current_gid()));
}

COMPAT_SYSCALL_DEFINE0(s390_getegid16)
{
	return high2lowgid(from_kgid_munged(current_user_ns(), current_egid()));
}

#ifdef CONFIG_SYSVIPC
COMPAT_SYSCALL_DEFINE5(s390_ipc, uint, call, int, first, compat_ulong_t, second,
		compat_ulong_t, third, compat_uptr_t, ptr)
{
	if (call >> 16)		/* hack for backward compatibility */
		return -EINVAL;
	return compat_sys_ipc(call, first, second, third, ptr, third);
}
#endif

COMPAT_SYSCALL_DEFINE3(s390_truncate64, const char __user *, path, u32, high, u32, low)
{
	return ksys_truncate(path, (unsigned long)high << 32 | low);
}

COMPAT_SYSCALL_DEFINE3(s390_ftruncate64, unsigned int, fd, u32, high, u32, low)
{
	return ksys_ftruncate(fd, (unsigned long)high << 32 | low);
}

COMPAT_SYSCALL_DEFINE5(s390_pread64, unsigned int, fd, char __user *, ubuf,
		       compat_size_t, count, u32, high, u32, low)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL;
	return ksys_pread64(fd, ubuf, count, (unsigned long)high << 32 | low);
}

COMPAT_SYSCALL_DEFINE5(s390_pwrite64, unsigned int, fd, const char __user *, ubuf,
		       compat_size_t, count, u32, high, u32, low)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL;
	return ksys_pwrite64(fd, ubuf, count, (unsigned long)high << 32 | low);
}

COMPAT_SYSCALL_DEFINE4(s390_readahead, int, fd, u32, high, u32, low, s32, count)
{
	return sys_readahead(fd, (unsigned long)high << 32 | low, count);
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
	tmp.st_uid = from_kuid_munged(current_user_ns(), stat->uid);
	tmp.st_gid = from_kgid_munged(current_user_ns(), stat->gid);
	tmp.st_rdev = huge_encode_dev(stat->rdev);
	tmp.st_size = stat->size;
	tmp.st_blksize = (u32)stat->blksize;
	tmp.st_blocks = (u32)stat->blocks;
	tmp.st_atime = (u32)stat->atime.tv_sec;
	tmp.st_mtime = (u32)stat->mtime.tv_sec;
	tmp.st_ctime = (u32)stat->ctime.tv_sec;

	return copy_to_user(ubuf,&tmp,sizeof(tmp)) ? -EFAULT : 0; 
}

COMPAT_SYSCALL_DEFINE2(s390_stat64, const char __user *, filename, struct stat64_emu31 __user *, statbuf)
{
	struct kstat stat;
	int ret = vfs_stat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

COMPAT_SYSCALL_DEFINE2(s390_lstat64, const char __user *, filename, struct stat64_emu31 __user *, statbuf)
{
	struct kstat stat;
	int ret = vfs_lstat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

COMPAT_SYSCALL_DEFINE2(s390_fstat64, unsigned int, fd, struct stat64_emu31 __user *, statbuf)
{
	struct kstat stat;
	int ret = vfs_fstat(fd, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

COMPAT_SYSCALL_DEFINE4(s390_fstatat64, unsigned int, dfd, const char __user *, filename,
		       struct stat64_emu31 __user *, statbuf, int, flag)
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

COMPAT_SYSCALL_DEFINE1(s390_old_mmap, struct mmap_arg_struct_emu31 __user *, arg)
{
	struct mmap_arg_struct_emu31 a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	if (a.offset & ~PAGE_MASK)
		return -EINVAL;
	return sys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd,
			      a.offset >> PAGE_SHIFT);
}

COMPAT_SYSCALL_DEFINE1(s390_mmap2, struct mmap_arg_struct_emu31 __user *, arg)
{
	struct mmap_arg_struct_emu31 a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	return sys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd, a.offset);
}

COMPAT_SYSCALL_DEFINE3(s390_read, unsigned int, fd, char __user *, buf, compat_size_t, count)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL; 

	return ksys_read(fd, buf, count);
}

COMPAT_SYSCALL_DEFINE3(s390_write, unsigned int, fd, const char __user *, buf, compat_size_t, count)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL; 

	return ksys_write(fd, buf, count);
}

/*
 * 31 bit emulation wrapper functions for sys_fadvise64/fadvise64_64.
 * These need to rewrite the advise values for POSIX_FADV_{DONTNEED,NOREUSE}
 * because the 31 bit values differ from the 64 bit values.
 */

COMPAT_SYSCALL_DEFINE5(s390_fadvise64, int, fd, u32, high, u32, low, compat_size_t, len, int, advise)
{
	if (advise == 4)
		advise = POSIX_FADV_DONTNEED;
	else if (advise == 5)
		advise = POSIX_FADV_NOREUSE;
	return sys_fadvise64(fd, (unsigned long)high << 32 | low, len, advise);
}

struct fadvise64_64_args {
	int fd;
	long long offset;
	long long len;
	int advice;
};

COMPAT_SYSCALL_DEFINE1(s390_fadvise64_64, struct fadvise64_64_args __user *, args)
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

COMPAT_SYSCALL_DEFINE6(s390_sync_file_range, int, fd, u32, offhigh, u32, offlow,
		       u32, nhigh, u32, nlow, unsigned int, flags)
{
	return ksys_sync_file_range(fd, ((loff_t)offhigh << 32) + offlow,
				   ((u64)nhigh << 32) + nlow, flags);
}

COMPAT_SYSCALL_DEFINE6(s390_fallocate, int, fd, int, mode, u32, offhigh, u32, offlow,
		       u32, lenhigh, u32, lenlow)
{
	return ksys_fallocate(fd, mode, ((loff_t)offhigh << 32) + offlow,
			      ((u64)lenhigh << 32) + lenlow);
}
