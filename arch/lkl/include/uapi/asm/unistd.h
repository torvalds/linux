#ifndef _ASM_UAPI_LKL_UNISTD_H
#define _ASM_UAPI_LKL_UNISTD_H

#ifdef __KERNEL__
#define __NR_ni_syscall		0
#define __NR_reboot		1
#endif
#define __NR_getpid		2
#define __NR_write		3
#define __NR_close		4
#define __NR_unlink		5
#define __NR_open		6
#define __NR_poll		7
#define __NR_read		8
#define __NR_rename		10
#define __NR_flock		11
#define __NR_newfstat		12
#define __NR_chmod		13
#define __NR_newlstat		14
#define __NR_mkdir		15
#define __NR_rmdir		16
#define __NR_getdents64		17
#define __NR_newstat		18
#define __NR_utimes		19
#define __NR_utime		20
#define __NR_nanosleep		21
#define __NR_mknod		22
#define __NR_mount		23
#define __NR_umount		24
#define __NR_chdir		25
#define __NR_chroot		26
#define __NR_getcwd		27
#define __NR_chown		28
#define __NR_umask		29
#define __NR_getuid		30
#define __NR_getgid		31
#define __NR_socketcall		32
#define __NR_ioctl		33
#define __NR_readlink		34
#define __NR_access		35
#define __NR_truncate		36
#define __NR_sync		37
#define __NR_creat		38
#define __NR_llseek		39
#define __NR_stat64		40
#define __NR_lstat64		41
#define __NR_fstat64		42
#define __NR_fstatat64		43
#define __NR_statfs64		44
#define __NR_fstatfs64		45
#define __NR_listxattr		46
#define __NR_llistxattr		47
#define __NR_flistxattr		48
#define __NR_getxattr		49
#define __NR_lgetxattr		50
#define __NR_fgetxattr		51
#define __NR_setxattr		52
#define __NR_lsetxattr		53
#define __NR_fsetxattr		54
#define __NR_symlink		55
#define __NR_fallocate		56
#define __NR_link		57
#define __NR_pread64		58
#define __NR_pwrite64		59
#define __NR_fsync		60
#define __NR_fdatasync		61
#define __NR_removexattr	62
#define __NR_utimensat		63
#ifdef __KERNEL__
#define NR_syscalls		64
#endif

#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_LLSEEK

long lkl_syscall(long no, long *params);

#ifndef __KERNEL__

#define LKL_SYSCALL0(_syscall)						\
	static inline							\
	long lkl_sys_##_syscall(void)					\
	{								\
		long params[6];						\
		return lkl_syscall(__lkl__NR_##_syscall, params);	\
	}

#define LKL_SYSCALL1(_syscall, arg1_t, arg1)				\
	static inline							\
	long lkl_sys_##_syscall(arg1_t arg1)				\
	{								\
		long params[6];						\
		params[0] = (long)arg1;					\
		return lkl_syscall(__lkl__NR_##_syscall, params);	\
	}

#define LKL_SYSCALL2(_syscall, arg1_t, arg1, arg2_t, arg2)		\
	static inline							\
	long lkl_sys_##_syscall(arg1_t arg1, arg2_t arg2)		\
	{								\
		long params[6];						\
		params[0] = (long)arg1;					\
		params[1] = (long)arg2;					\
		return lkl_syscall(__lkl__NR_##_syscall, params);	\
	}

#define LKL_SYSCALL3(_syscall, arg1_t, arg1, arg2_t, arg2, arg3_t, arg3) \
	static inline							\
	long lkl_sys_##_syscall(arg1_t arg1, arg2_t arg2, arg3_t arg3)	\
	{								\
		long params[6];						\
		params[0] = (long)arg1;					\
		params[1] = (long)arg2;					\
		params[2] = (long)arg3;					\
		return lkl_syscall(__lkl__NR_##_syscall, params);	\
	}

#define LKL_SYSCALL4(_syscall, arg1_t, arg1, arg2_t, arg2, arg3_t, arg3, \
		      arg4_t, arg4)					\
	static inline							\
	long lkl_sys_##_syscall(arg1_t arg1, arg2_t arg2, arg3_t arg3,	\
				arg4_t arg4)				\
	{								\
		long params[6];						\
		params[0] = (long)arg1;					\
		params[1] = (long)arg2;					\
		params[2] = (long)arg3;					\
		params[3] = (long)arg4;					\
		return lkl_syscall(__lkl__NR_##_syscall, params);	\
	}

#define LKL_SYSCALL5(_syscall, arg1_t, arg1, arg2_t, arg2, arg3_t, arg3, \
		     arg4_t, arg4, arg5_t, arg5)			\
	static inline							\
	long lkl_sys_##_syscall(arg1_t arg1, arg2_t arg2, arg3_t arg3,	\
				arg4_t arg4, arg5_t arg5)		\
	{								\
		long params[6];						\
		params[0] = (long)arg1;					\
		params[1] = (long)arg2;					\
		params[2] = (long)arg3;					\
		params[3] = (long)arg4;					\
		params[4] = (long)arg5;					\
		return lkl_syscall(__lkl__NR_##_syscall, params);	\
	}

#define LKL_SYSCALL6(_syscall, arg1_t, arg1, arg2_t, arg2, arg3_t, arg3, \
		     arg4_t, arg4, arg5_t, arg5, arg6_t, arg6)		\
	static inline							\
	long lkl_sys_##_syscall(arg1_t arg1, arg2_t arg2, arg3_t arg3,	\
				arg4_t arg4, arg5_t arg5, arg6_t arg6)	\
	{								\
		long params[6];						\
		params[0] = (long)arg1;					\
		params[1] = (long)arg2;					\
		params[2] = (long)arg3;					\
		params[3] = (long)arg4;					\
		params[4] = (long)arg5;					\
		params[5] = (long)arg6;					\
		return lkl_syscall(__lkl__NR_##_syscall, params);	\
	}

#include <autoconf.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/time.h>
#include <linux/utime.h>
#include <asm/stat.h>
#include <asm/statfs.h>
#define __KERNEL__ /* to pull in S_ definitions */
#include <linux/stat.h>
#undef __KERNEL__
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <asm/irq.h>
#include <linux/kdev_t.h>

/* these types are not exported to userspace so we have to do it here */
typedef unsigned short lkl_umode_t;

struct lkl_dirent64 {
	unsigned long long	d_ino;
	long long		d_off;
	unsigned short		d_reclen;
	unsigned char		d_type;
	char			d_name[0];
};

#define LKL_DT_UNKNOWN		0
#define LKL_DT_FIFO		1
#define LKL_DT_CHR		2
#define LKL_DT_DIR		4
#define LKL_DT_BLK		6
#define LKL_DT_REG		8
#define LKL_DT_LNK		10
#define LKL_DT_SOCK		12
#define LKL_DT_WHT		14

LKL_SYSCALL0(getpid);
LKL_SYSCALL3(write, unsigned int, fd, const char *, buf,
	     __lkl__kernel_size_t, count);
LKL_SYSCALL1(close, unsigned int, fd);
LKL_SYSCALL1(unlink, const char *, pathname);
LKL_SYSCALL3(open, const char *, filename, int, flags, lkl_umode_t, mode);
LKL_SYSCALL2(creat, const char *, filename, lkl_umode_t, mode);
LKL_SYSCALL3(poll, struct lkl_pollfd *, ufds, unsigned int, nfds, int, timeout);
LKL_SYSCALL3(read, unsigned int, fd, char *, buf, __lkl__kernel_size_t, count);
LKL_SYSCALL2(rename, const char *, oldname, const char *, newname);
LKL_SYSCALL2(flock, unsigned int, fd, unsigned int, cmd);
LKL_SYSCALL2(chmod, const char *, filename, lkl_umode_t, mode);

LKL_SYSCALL2(mkdir, const char *, pathname, lkl_umode_t, mode);
LKL_SYSCALL1(rmdir, const char *, pathname);
LKL_SYSCALL3(getdents64, unsigned int, fd, void *, dirent, unsigned int, size);
LKL_SYSCALL2(utimes, const char *, filename, struct lkl_timeval *, utimes);
LKL_SYSCALL2(nanosleep, struct lkl_timespec *, rqtp,
	     struct lkl_timespec *, rmtp);
LKL_SYSCALL3(mknod, const char *, filename, lkl_umode_t, mode,
	     unsigned int, dev);
LKL_SYSCALL5(mount, const char *, dev_name, const char *, dir_name,
	     const char *, type, unsigned long,	 flags, void *, data);
LKL_SYSCALL2(umount, const char *, name, int, flags);
LKL_SYSCALL1(chdir, const char *, filename);
LKL_SYSCALL1(chroot, const char *, filename);
LKL_SYSCALL2(getcwd, char *, buf, unsigned long, size);
LKL_SYSCALL2(utime, const char *, filename, const struct lkl_utimbuf *, buf);
LKL_SYSCALL3(ioctl, unsigned int, fd, unsigned int, cmd, unsigned long, arg);
LKL_SYSCALL1(umask, int, mask);
LKL_SYSCALL0(getuid);
LKL_SYSCALL0(getgid);
LKL_SYSCALL2(access, const char *, filename, int, mode);
LKL_SYSCALL2(truncate, const char *, path, long, length);
LKL_SYSCALL0(sync);
LKL_SYSCALL5(llseek, unsigned int, fd, unsigned long, offset_high,
	     unsigned long, offset_low, __lkl__kernel_loff_t *, result,
	     unsigned int, whence);
LKL_SYSCALL2(fstat64, unsigned long, fd, struct lkl_stat64 *, statbuf);
LKL_SYSCALL4(fstatat64, unsigned int, dfd, const char *, filname,
	     struct lkl_stat64 *, statbuf, int, flag);
LKL_SYSCALL2(stat64, const char *, filename, struct lkl_stat64 *, statbuf);
LKL_SYSCALL2(lstat64, const char *, filename, struct lkl_stat64 *, statbuf);
LKL_SYSCALL3(statfs64, const char *, path, __lkl__kernel_size_t, sz,
	     struct lkl_statfs64 *, buf);
LKL_SYSCALL3(readlink, const char *, path, char *, buf, int, bufsiz);
LKL_SYSCALL3(listxattr, const char *, path, char *, list, int, bufsiz);
LKL_SYSCALL3(llistxattr, const char *, path, char *, list, int, bufsiz);
LKL_SYSCALL3(flistxattr, int, fd, char *, list, int, bufsiz);
LKL_SYSCALL4(getxattr, const char *, path, const char *, name, void *, value,
	     __lkl__kernel_size_t, size);
LKL_SYSCALL4(lgetxattr, const char *, path, const char *, name, void *, value,
	     __lkl__kernel_size_t, size);
LKL_SYSCALL4(fgetxattr, int, fd, const char *, name, void *, value,
	     __lkl__kernel_size_t, size);
LKL_SYSCALL5(setxattr, const char *, path, const char *, name,
	     const void *, value, __lkl__kernel_size_t, size, int, flags);
LKL_SYSCALL5(lsetxattr, const char *, path, const char *, name,
	     const void *, value, __lkl__kernel_size_t, size, int, flags);
LKL_SYSCALL5(fsetxattr, int, fd, const char *, name, const void *, value,
	     __lkl__kernel_size_t, size, int, flags);
LKL_SYSCALL2(symlink, const char *, oldname, const char *, newname);
LKL_SYSCALL2(link, const char *, oldname, const char *, newname);
LKL_SYSCALL3(chown, const char *, filename, __lkl__kernel_uid32_t, uid,
	     __lkl__kernel_gid32_t, gid);
LKL_SYSCALL4(pread64, unsigned int, fd, char *, buf,
	     __lkl__kernel_size_t, count, __lkl__kernel_loff_t, pos);
LKL_SYSCALL4(pwrite64, unsigned int, fd, const char *, buf,
	     __lkl__kernel_size_t, count, __lkl__kernel_loff_t, pos);
LKL_SYSCALL1(fsync, unsigned int, fd);
LKL_SYSCALL1(fdatasync, unsigned int, fd);
LKL_SYSCALL2(removexattr, const char *, path, const char *, name);
LKL_SYSCALL4(utimensat, int, dirfd, const char *, path,
	     struct lkl_timespec *, utimes, int, flags);
LKL_SYSCALL4(fallocate, int, fd, int, mode, __lkl__kernel_loff_t, offset,
	     __lkl__kernel_loff_t, len);

long lkl_sys_halt(void);

#endif /* __KERNEL__ */

#endif /* _ASM_UAPI_LKL_UNISTD_H */
