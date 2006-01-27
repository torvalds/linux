/* $Id: fs.c,v 1.27 2002/02/08 03:57:14 davem Exp $
 * fs.c: fs related syscall emulation for Solaris
 *
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * 1999-08-19 Implemented solaris F_FREESP (truncate)
 *            fcntl, by Jason Rappleye (rappleye@ccr.buffalo.edu)
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/smp_lock.h>
#include <linux/limits.h>
#include <linux/resource.h>
#include <linux/quotaops.h>
#include <linux/mount.h>
#include <linux/vfs.h>

#include <asm/uaccess.h>
#include <asm/string.h>
#include <asm/ptrace.h>

#include "conv.h"

#define R3_VERSION	1
#define R4_VERSION	2

typedef struct {
	s32	tv_sec;
	s32	tv_nsec;
} timestruct_t;

struct sol_stat {
	u32		st_dev;
	s32		st_pad1[3];     /* network id */
	u32		st_ino;
	u32		st_mode;
	u32		st_nlink;
	u32		st_uid;
	u32		st_gid;
	u32		st_rdev;
	s32		st_pad2[2];
	s32		st_size;
	s32		st_pad3;	/* st_size, off_t expansion */
	timestruct_t	st_atime;
	timestruct_t	st_mtime;
	timestruct_t	st_ctime;
	s32		st_blksize;
	s32		st_blocks;
	char		st_fstype[16];
	s32		st_pad4[8];     /* expansion area */
};

struct sol_stat64 {
	u32		st_dev;
	s32		st_pad1[3];     /* network id */
	u64		st_ino;
	u32		st_mode;
	u32		st_nlink;
	u32		st_uid;
	u32		st_gid;
	u32		st_rdev;
	s32		st_pad2[2];
	s64		st_size;
	timestruct_t	st_atime;
	timestruct_t	st_mtime;
	timestruct_t	st_ctime;
	s64		st_blksize;
	s32		st_blocks;
	char		st_fstype[16];
	s32		st_pad4[4];     /* expansion area */
};

#define UFSMAGIC (((unsigned)'u'<<24)||((unsigned)'f'<<16)||((unsigned)'s'<<8))

static inline int putstat(struct sol_stat __user *ubuf, struct kstat *kbuf)
{
	if (kbuf->size > MAX_NON_LFS ||
	    !sysv_valid_dev(kbuf->dev) ||
	    !sysv_valid_dev(kbuf->rdev))
		return -EOVERFLOW;
	if (put_user (sysv_encode_dev(kbuf->dev), &ubuf->st_dev)	||
	    __put_user (kbuf->ino, &ubuf->st_ino)		||
	    __put_user (kbuf->mode, &ubuf->st_mode)		||
	    __put_user (kbuf->nlink, &ubuf->st_nlink)	||
	    __put_user (kbuf->uid, &ubuf->st_uid)		||
	    __put_user (kbuf->gid, &ubuf->st_gid)		||
	    __put_user (sysv_encode_dev(kbuf->rdev), &ubuf->st_rdev)	||
	    __put_user (kbuf->size, &ubuf->st_size)		||
	    __put_user (kbuf->atime.tv_sec, &ubuf->st_atime.tv_sec)	||
	    __put_user (kbuf->atime.tv_nsec, &ubuf->st_atime.tv_nsec)	||
	    __put_user (kbuf->mtime.tv_sec, &ubuf->st_mtime.tv_sec)	||
	    __put_user (kbuf->mtime.tv_nsec, &ubuf->st_mtime.tv_nsec)	||
	    __put_user (kbuf->ctime.tv_sec, &ubuf->st_ctime.tv_sec)	||
	    __put_user (kbuf->ctime.tv_nsec, &ubuf->st_ctime.tv_nsec)	||
	    __put_user (kbuf->blksize, &ubuf->st_blksize)	||
	    __put_user (kbuf->blocks, &ubuf->st_blocks)	||
	    __put_user (UFSMAGIC, (unsigned __user *)ubuf->st_fstype))
		return -EFAULT;
	return 0;
}

static inline int putstat64(struct sol_stat64 __user *ubuf, struct kstat *kbuf)
{
	if (!sysv_valid_dev(kbuf->dev) || !sysv_valid_dev(kbuf->rdev))
		return -EOVERFLOW;
	if (put_user (sysv_encode_dev(kbuf->dev), &ubuf->st_dev)	||
	    __put_user (kbuf->ino, &ubuf->st_ino)		||
	    __put_user (kbuf->mode, &ubuf->st_mode)		||
	    __put_user (kbuf->nlink, &ubuf->st_nlink)	||
	    __put_user (kbuf->uid, &ubuf->st_uid)		||
	    __put_user (kbuf->gid, &ubuf->st_gid)		||
	    __put_user (sysv_encode_dev(kbuf->rdev), &ubuf->st_rdev)	||
	    __put_user (kbuf->size, &ubuf->st_size)		||
	    __put_user (kbuf->atime.tv_sec, &ubuf->st_atime.tv_sec)	||
	    __put_user (kbuf->atime.tv_nsec, &ubuf->st_atime.tv_nsec)	||
	    __put_user (kbuf->mtime.tv_sec, &ubuf->st_mtime.tv_sec)	||
	    __put_user (kbuf->mtime.tv_nsec, &ubuf->st_mtime.tv_nsec)	||
	    __put_user (kbuf->ctime.tv_sec, &ubuf->st_ctime.tv_sec)	||
	    __put_user (kbuf->ctime.tv_nsec, &ubuf->st_ctime.tv_nsec)	||
	    __put_user (kbuf->blksize, &ubuf->st_blksize)	||
	    __put_user (kbuf->blocks, &ubuf->st_blocks)	||
	    __put_user (UFSMAGIC, (unsigned __user *)ubuf->st_fstype))
		return -EFAULT;
	return 0;
}

asmlinkage int solaris_stat(u32 filename, u32 statbuf)
{
	struct kstat s;
	int ret = vfs_stat(A(filename), &s);
	if (!ret)
		return putstat(A(statbuf), &s);
	return ret;
}

asmlinkage int solaris_xstat(int vers, u32 filename, u32 statbuf)
{
	/* Solaris doesn't bother with looking at vers, so we do neither */
	return solaris_stat(filename, statbuf);
}

asmlinkage int solaris_stat64(u32 filename, u32 statbuf)
{
	struct kstat s;
	int ret = vfs_stat(A(filename), &s);
	if (!ret)
		return putstat64(A(statbuf), &s);
	return ret;
}

asmlinkage int solaris_lstat(u32 filename, u32 statbuf)
{
	struct kstat s;
	int ret = vfs_lstat(A(filename), &s);
	if (!ret)
		return putstat(A(statbuf), &s);
	return ret;
}

asmlinkage int solaris_lxstat(int vers, u32 filename, u32 statbuf)
{
	return solaris_lstat(filename, statbuf);
}

asmlinkage int solaris_lstat64(u32 filename, u32 statbuf)
{
	struct kstat s;
	int ret = vfs_lstat(A(filename), &s);
	if (!ret)
		return putstat64(A(statbuf), &s);
	return ret;
}

asmlinkage int solaris_fstat(unsigned int fd, u32 statbuf)
{
	struct kstat s;
	int ret = vfs_fstat(fd, &s);
	if (!ret)
		return putstat(A(statbuf), &s);
	return ret;
}

asmlinkage int solaris_fxstat(int vers, u32 fd, u32 statbuf)
{
	return solaris_fstat(fd, statbuf);
}

asmlinkage int solaris_fstat64(unsigned int fd, u32 statbuf)
{
	struct kstat s;
	int ret = vfs_fstat(fd, &s);
	if (!ret)
		return putstat64(A(statbuf), &s);
	return ret;
}

asmlinkage int solaris_mknod(u32 path, u32 mode, s32 dev)
{
	int (*sys_mknod)(const char __user *,int,unsigned) = 
		(int (*)(const char __user *,int,unsigned))SYS(mknod);
	int major = sysv_major(dev);
	int minor = sysv_minor(dev);

	/* minor is guaranteed to be OK for MKDEV, major might be not */
	if (major > 0xfff)
		return -EINVAL;
	return sys_mknod(A(path), mode, new_encode_dev(MKDEV(major,minor)));
}

asmlinkage int solaris_xmknod(int vers, u32 path, u32 mode, s32 dev)
{
	return solaris_mknod(path, mode, dev);
}

asmlinkage int solaris_getdents64(unsigned int fd, void __user *dirent, unsigned int count)
{
	int (*sys_getdents)(unsigned int, void __user *, unsigned int) =
		(int (*)(unsigned int, void __user *, unsigned int))SYS(getdents);
		
	return sys_getdents(fd, dirent, count);
}

/* This statfs thingie probably will go in the near future, but... */

struct sol_statfs {
	short	f_type;
	s32	f_bsize;
	s32	f_frsize;
	s32	f_blocks;
	s32	f_bfree;
	u32	f_files;
	u32	f_ffree;
	char	f_fname[6];
	char	f_fpack[6];
};

asmlinkage int solaris_statfs(u32 path, u32 buf, int len, int fstype)
{
	int ret;
	struct statfs s;
	mm_segment_t old_fs = get_fs();
	int (*sys_statfs)(const char __user *,struct statfs __user *) = 
		(int (*)(const char __user *,struct statfs __user *))SYS(statfs);
	struct sol_statfs __user *ss = A(buf);
	
	if (len != sizeof(struct sol_statfs)) return -EINVAL;
	if (!fstype) {
		/* FIXME: mixing userland and kernel pointers */
		set_fs (KERNEL_DS);
		ret = sys_statfs(A(path), &s);
		set_fs (old_fs);
		if (!ret) {
			if (put_user (s.f_type, &ss->f_type)		||
			    __put_user (s.f_bsize, &ss->f_bsize)	||
			    __put_user (0, &ss->f_frsize)		||
			    __put_user (s.f_blocks, &ss->f_blocks)	||
			    __put_user (s.f_bfree, &ss->f_bfree)	||
			    __put_user (s.f_files, &ss->f_files)	||
			    __put_user (s.f_ffree, &ss->f_ffree)	||
			    __clear_user (&ss->f_fname, 12))
				return -EFAULT;
		}
		return ret;
	}
/* Linux can't stat unmounted filesystems so we
 * simply lie and claim 100MB of 1GB is free. Sorry.
 */
	if (put_user (fstype, &ss->f_type)		||
	    __put_user (1024, &ss->f_bsize)		||
	    __put_user (0, &ss->f_frsize)		||
	    __put_user (1024*1024, &ss->f_blocks)	||
	    __put_user (100*1024, &ss->f_bfree)		||
	    __put_user (60000, &ss->f_files)		||
	    __put_user (50000, &ss->f_ffree)		||
	    __clear_user (&ss->f_fname, 12))
		return -EFAULT;
	return 0;
}

asmlinkage int solaris_fstatfs(u32 fd, u32 buf, int len, int fstype)
{
	int ret;
	struct statfs s;
	mm_segment_t old_fs = get_fs();
	int (*sys_fstatfs)(unsigned,struct statfs __user *) = 
		(int (*)(unsigned,struct statfs __user *))SYS(fstatfs);
	struct sol_statfs __user *ss = A(buf);
	
	if (len != sizeof(struct sol_statfs)) return -EINVAL;
	if (!fstype) {
		set_fs (KERNEL_DS);
		ret = sys_fstatfs(fd, &s);
		set_fs (old_fs);
		if (!ret) {
			if (put_user (s.f_type, &ss->f_type)		||
			    __put_user (s.f_bsize, &ss->f_bsize)	||
			    __put_user (0, &ss->f_frsize)		||
			    __put_user (s.f_blocks, &ss->f_blocks)	||
			    __put_user (s.f_bfree, &ss->f_bfree)	||
			    __put_user (s.f_files, &ss->f_files)	||
			    __put_user (s.f_ffree, &ss->f_ffree)	||
			    __clear_user (&ss->f_fname, 12))
				return -EFAULT;
		}
		return ret;
	}
	/* Otherwise fstatfs is the same as statfs */
	return solaris_statfs(0, buf, len, fstype);
}

struct sol_statvfs {
	u32	f_bsize;
	u32	f_frsize;
	u32	f_blocks;
	u32	f_bfree;
	u32	f_bavail;
	u32	f_files;
	u32	f_ffree;
	u32	f_favail;
	u32	f_fsid;
	char	f_basetype[16];
	u32	f_flag;
	u32	f_namemax;
	char	f_fstr[32];
	u32	f_filler[16];
};

struct sol_statvfs64 {
	u32	f_bsize;
	u32	f_frsize;
	u64	f_blocks;
	u64	f_bfree;
	u64	f_bavail;
	u64	f_files;
	u64	f_ffree;
	u64	f_favail;
	u32	f_fsid;
	char	f_basetype[16];
	u32	f_flag;
	u32	f_namemax;
	char	f_fstr[32];
	u32	f_filler[16];
};

static int report_statvfs(struct vfsmount *mnt, struct inode *inode, u32 buf)
{
	struct kstatfs s;
	int error;
	struct sol_statvfs __user *ss = A(buf);

	error = vfs_statfs(mnt->mnt_sb, &s);
	if (!error) {
		const char *p = mnt->mnt_sb->s_type->name;
		int i = 0;
		int j = strlen (p);
		
		if (j > 15) j = 15;
		if (IS_RDONLY(inode)) i = 1;
		if (mnt->mnt_flags & MNT_NOSUID) i |= 2;
		if (!sysv_valid_dev(inode->i_sb->s_dev))
			return -EOVERFLOW;
		if (put_user (s.f_bsize, &ss->f_bsize)		||
		    __put_user (0, &ss->f_frsize)		||
		    __put_user (s.f_blocks, &ss->f_blocks)	||
		    __put_user (s.f_bfree, &ss->f_bfree)	||
		    __put_user (s.f_bavail, &ss->f_bavail)	||
		    __put_user (s.f_files, &ss->f_files)	||
		    __put_user (s.f_ffree, &ss->f_ffree)	||
		    __put_user (s.f_ffree, &ss->f_favail)	||
		    __put_user (sysv_encode_dev(inode->i_sb->s_dev), &ss->f_fsid) ||
		    __copy_to_user (ss->f_basetype,p,j)		||
		    __put_user (0, (char __user *)&ss->f_basetype[j])	||
		    __put_user (s.f_namelen, &ss->f_namemax)	||
		    __put_user (i, &ss->f_flag)			||		    
		    __clear_user (&ss->f_fstr, 32))
			return -EFAULT;
	}
	return error;
}

static int report_statvfs64(struct vfsmount *mnt, struct inode *inode, u32 buf)
{
	struct kstatfs s;
	int error;
	struct sol_statvfs64 __user *ss = A(buf);
			
	error = vfs_statfs(mnt->mnt_sb, &s);
	if (!error) {
		const char *p = mnt->mnt_sb->s_type->name;
		int i = 0;
		int j = strlen (p);
		
		if (j > 15) j = 15;
		if (IS_RDONLY(inode)) i = 1;
		if (mnt->mnt_flags & MNT_NOSUID) i |= 2;
		if (!sysv_valid_dev(inode->i_sb->s_dev))
			return -EOVERFLOW;
		if (put_user (s.f_bsize, &ss->f_bsize)		||
		    __put_user (0, &ss->f_frsize)		||
		    __put_user (s.f_blocks, &ss->f_blocks)	||
		    __put_user (s.f_bfree, &ss->f_bfree)	||
		    __put_user (s.f_bavail, &ss->f_bavail)	||
		    __put_user (s.f_files, &ss->f_files)	||
		    __put_user (s.f_ffree, &ss->f_ffree)	||
		    __put_user (s.f_ffree, &ss->f_favail)	||
		    __put_user (sysv_encode_dev(inode->i_sb->s_dev), &ss->f_fsid) ||
		    __copy_to_user (ss->f_basetype,p,j)		||
		    __put_user (0, (char __user *)&ss->f_basetype[j])	||
		    __put_user (s.f_namelen, &ss->f_namemax)	||
		    __put_user (i, &ss->f_flag)			||		    
		    __clear_user (&ss->f_fstr, 32))
			return -EFAULT;
	}
	return error;
}

asmlinkage int solaris_statvfs(u32 path, u32 buf)
{
	struct nameidata nd;
	int error;

	error = user_path_walk(A(path),&nd);
	if (!error) {
		struct inode * inode = nd.dentry->d_inode;
		error = report_statvfs(nd.mnt, inode, buf);
		path_release(&nd);
	}
	return error;
}

asmlinkage int solaris_fstatvfs(unsigned int fd, u32 buf)
{
	struct file * file;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (file) {
		error = report_statvfs(file->f_vfsmnt, file->f_dentry->d_inode, buf);
		fput(file);
	}

	return error;
}

asmlinkage int solaris_statvfs64(u32 path, u32 buf)
{
	struct nameidata nd;
	int error;

	lock_kernel();
	error = user_path_walk(A(path), &nd);
	if (!error) {
		struct inode * inode = nd.dentry->d_inode;
		error = report_statvfs64(nd.mnt, inode, buf);
		path_release(&nd);
	}
	unlock_kernel();
	return error;
}

asmlinkage int solaris_fstatvfs64(unsigned int fd, u32 buf)
{
	struct file * file;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (file) {
		lock_kernel();
		error = report_statvfs64(file->f_vfsmnt, file->f_dentry->d_inode, buf);
		unlock_kernel();
		fput(file);
	}
	return error;
}

extern asmlinkage long sparc32_open(const char * filename, int flags, int mode);

asmlinkage int solaris_open(u32 fname, int flags, u32 mode)
{
	const char *filename = (const char *)(long)fname;
	int fl = flags & 0xf;

	/* Translate flags first. */
	if (flags & 0x2000) fl |= O_LARGEFILE;
	if (flags & 0x8050) fl |= O_SYNC;
	if (flags & 0x80) fl |= O_NONBLOCK;
	if (flags & 0x100) fl |= O_CREAT;
	if (flags & 0x200) fl |= O_TRUNC;
	if (flags & 0x400) fl |= O_EXCL;
	if (flags & 0x800) fl |= O_NOCTTY;
	flags = fl;

	return sparc32_open(filename, flags, mode);
}

#define SOL_F_SETLK	6
#define SOL_F_SETLKW	7
#define SOL_F_FREESP    11
#define SOL_F_ISSTREAM  13
#define SOL_F_GETLK     14
#define SOL_F_PRIV      15
#define SOL_F_NPRIV     16
#define SOL_F_QUOTACTL  17
#define SOL_F_BLOCKS    18
#define SOL_F_BLKSIZE   19
#define SOL_F_GETOWN    23
#define SOL_F_SETOWN    24

struct sol_flock {
	short	l_type;
	short	l_whence;
	u32	l_start;
	u32	l_len;
	s32	l_sysid;
	s32	l_pid;
	s32	l_pad[4];
};

asmlinkage int solaris_fcntl(unsigned fd, unsigned cmd, u32 arg)
{
	int (*sys_fcntl)(unsigned,unsigned,unsigned long) = 
		(int (*)(unsigned,unsigned,unsigned long))SYS(fcntl);
	int ret, flags;

	switch (cmd) {
	case F_DUPFD:
	case F_GETFD:
	case F_SETFD: return sys_fcntl(fd, cmd, (unsigned long)arg);
	case F_GETFL:
		flags = sys_fcntl(fd, cmd, 0);
		ret = flags & 0xf;
		if (flags & O_SYNC) ret |= 0x8050;
		if (flags & O_NONBLOCK) ret |= 0x80;
		return ret;
	case F_SETFL:
		flags = arg & 0xf;
		if (arg & 0x8050) flags |= O_SYNC;
		if (arg & 0x80) flags |= O_NONBLOCK;
		return sys_fcntl(fd, cmd, (long)flags);
	case SOL_F_GETLK:
	case SOL_F_SETLK:
	case SOL_F_SETLKW:
		{
			struct flock f;
			struct sol_flock __user *p = A(arg);
			mm_segment_t old_fs = get_fs();

			switch (cmd) {
			case SOL_F_GETLK: cmd = F_GETLK; break;
			case SOL_F_SETLK: cmd = F_SETLK; break;
			case SOL_F_SETLKW: cmd = F_SETLKW; break;
			}

			if (get_user (f.l_type, &p->l_type) ||
			    __get_user (f.l_whence, &p->l_whence) ||
			    __get_user (f.l_start, &p->l_start) ||
			    __get_user (f.l_len, &p->l_len) ||
			    __get_user (f.l_pid, &p->l_sysid))
				return -EFAULT;

			set_fs(KERNEL_DS);
			ret = sys_fcntl(fd, cmd, (unsigned long)&f);
			set_fs(old_fs);

			if (__put_user (f.l_type, &p->l_type) ||
			    __put_user (f.l_whence, &p->l_whence) ||
			    __put_user (f.l_start, &p->l_start) ||
			    __put_user (f.l_len, &p->l_len) ||
			    __put_user (f.l_pid, &p->l_pid) ||
			    __put_user (0, &p->l_sysid))
				return -EFAULT;

			return ret;
		}
	case SOL_F_FREESP:
	        { 
		    int length;
		    int (*sys_newftruncate)(unsigned int, unsigned long)=
			    (int (*)(unsigned int, unsigned long))SYS(ftruncate);

		    if (get_user(length, &((struct sol_flock __user *)A(arg))->l_start))
			    return -EFAULT;

		    return sys_newftruncate(fd, length);
		}
	};
	return -EINVAL;
}

asmlinkage int solaris_ulimit(int cmd, int val)
{
	switch (cmd) {
	case 1: /* UL_GETFSIZE - in 512B chunks */
		return current->signal->rlim[RLIMIT_FSIZE].rlim_cur >> 9;
	case 2: /* UL_SETFSIZE */
		if ((unsigned long)val > (LONG_MAX>>9)) return -ERANGE;
		val <<= 9;
		task_lock(current->group_leader);
		if (val > current->signal->rlim[RLIMIT_FSIZE].rlim_max) {
			if (!capable(CAP_SYS_RESOURCE)) {
				task_unlock(current->group_leader);
				return -EPERM;
			}
			current->signal->rlim[RLIMIT_FSIZE].rlim_max = val;
		}
		current->signal->rlim[RLIMIT_FSIZE].rlim_cur = val;
		task_unlock(current->group_leader);
		return 0;
	case 3: /* UL_GMEMLIM */
		return current->signal->rlim[RLIMIT_DATA].rlim_cur;
	case 4: /* UL_GDESLIM */
		return NR_OPEN;
	}
	return -EINVAL;
}

/* At least at the time I'm writing this, Linux doesn't have ACLs, so we
   just fake this */
asmlinkage int solaris_acl(u32 filename, int cmd, int nentries, u32 aclbufp)
{
	return -ENOSYS;
}

asmlinkage int solaris_facl(unsigned int fd, int cmd, int nentries, u32 aclbufp)
{
	return -ENOSYS;
}

asmlinkage int solaris_pread(unsigned int fd, char __user *buf, u32 count, u32 pos)
{
	ssize_t (*sys_pread64)(unsigned int, char __user *, size_t, loff_t) =
		(ssize_t (*)(unsigned int, char __user *, size_t, loff_t))SYS(pread64);

	return sys_pread64(fd, buf, count, (loff_t)pos);
}

asmlinkage int solaris_pwrite(unsigned int fd, char __user *buf, u32 count, u32 pos)
{
	ssize_t (*sys_pwrite64)(unsigned int, char __user *, size_t, loff_t) =
		(ssize_t (*)(unsigned int, char __user *, size_t, loff_t))SYS(pwrite64);

	return sys_pwrite64(fd, buf, count, (loff_t)pos);
}

/* POSIX.1 names */
#define _PC_LINK_MAX    1
#define _PC_MAX_CANON   2
#define _PC_MAX_INPUT   3
#define _PC_NAME_MAX    4
#define _PC_PATH_MAX    5
#define _PC_PIPE_BUF    6
#define _PC_NO_TRUNC    7
#define _PC_VDISABLE    8
#define _PC_CHOWN_RESTRICTED    9
/* POSIX.4 names */
#define _PC_ASYNC_IO    10
#define _PC_PRIO_IO     11
#define _PC_SYNC_IO     12
#define _PC_LAST        12

/* This is not a real and complete implementation yet, just to keep
 * the easy Solaris binaries happy.
 */
asmlinkage int solaris_fpathconf(int fd, int name)
{
	int ret;

	switch(name) {
	case _PC_LINK_MAX:
		ret = LINK_MAX;
		break;
	case _PC_MAX_CANON:
		ret = MAX_CANON;
		break;
	case _PC_MAX_INPUT:
		ret = MAX_INPUT;
		break;
	case _PC_NAME_MAX:
		ret = NAME_MAX;
		break;
	case _PC_PATH_MAX:
		ret = PATH_MAX;
		break;
	case _PC_PIPE_BUF:
		ret = PIPE_BUF;
		break;
	case _PC_CHOWN_RESTRICTED:
		ret = 1;
		break;
	case _PC_NO_TRUNC:
	case _PC_VDISABLE:
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

asmlinkage int solaris_pathconf(u32 path, int name)
{
	return solaris_fpathconf(0, name);
}

/* solaris_llseek returns long long - quite difficult */
asmlinkage long solaris_llseek(struct pt_regs *regs, u32 off_hi, u32 off_lo, int whence)
{
	int (*sys_llseek)(unsigned int, unsigned long, unsigned long, loff_t __user *, unsigned int) =
		(int (*)(unsigned int, unsigned long, unsigned long, loff_t __user *, unsigned int))SYS(_llseek);
	int ret;
	mm_segment_t old_fs = get_fs();
	loff_t retval;
	
	set_fs(KERNEL_DS);
	ret = sys_llseek((unsigned int)regs->u_regs[UREG_I0], off_hi, off_lo, &retval, whence);
	set_fs(old_fs);
	if (ret < 0) return ret;
	regs->u_regs[UREG_I1] = (u32)retval;
	return (retval >> 32);
}

/* Have to mask out all but lower 3 bits */
asmlinkage int solaris_access(u32 filename, long mode)
{
	int (*sys_access)(const char __user *, int) = 
		(int (*)(const char __user *, int))SYS(access);
		
	return sys_access(A(filename), mode & 7);
}
