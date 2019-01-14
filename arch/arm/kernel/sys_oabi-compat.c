/*
 *  arch/arm/kernel/sys_oabi-compat.c
 *
 *  Compatibility wrappers for syscalls that are used from
 *  old ABI user space binaries with an EABI kernel.
 *
 *  Author:	Nicolas Pitre
 *  Created:	Oct 7, 2005
 *  Copyright:	MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

/*
 * The legacy ABI and the new ARM EABI have different rules making some
 * syscalls incompatible especially with structure arguments.
 * Most notably, Eabi says 64-bit members should be 64-bit aligned instead of
 * simply word aligned.  EABI also pads structures to the size of the largest
 * member it contains instead of the invariant 32-bit.
 *
 * The following syscalls are affected:
 *
 * sys_stat64:
 * sys_lstat64:
 * sys_fstat64:
 * sys_fstatat64:
 *
 *   struct stat64 has different sizes and some members are shifted
 *   Compatibility wrappers are needed for them and provided below.
 *
 * sys_fcntl64:
 *
 *   struct flock64 has different sizes and some members are shifted
 *   A compatibility wrapper is needed and provided below.
 *
 * sys_statfs64:
 * sys_fstatfs64:
 *
 *   struct statfs64 has extra padding with EABI growing its size from
 *   84 to 88.  This struct is now __attribute__((packed,aligned(4)))
 *   with a small assembly wrapper to force the sz argument to 84 if it is 88
 *   to avoid copying the extra padding over user space unexpecting it.
 *
 * sys_newuname:
 *
 *   struct new_utsname has no padding with EABI.  No problem there.
 *
 * sys_epoll_ctl:
 * sys_epoll_wait:
 *
 *   struct epoll_event has its second member shifted also affecting the
 *   structure size. Compatibility wrappers are needed and provided below.
 *
 * sys_ipc:
 * sys_semop:
 * sys_semtimedop:
 *
 *   struct sembuf loses its padding with EABI.  Since arrays of them are
 *   used they have to be copyed to remove the padding. Compatibility wrappers
 *   provided below.
 *
 * sys_bind:
 * sys_connect:
 * sys_sendmsg:
 * sys_sendto:
 * sys_socketcall:
 *
 *   struct sockaddr_un loses its padding with EABI.  Since the size of the
 *   structure is used as a validation test in unix_mkname(), we need to
 *   change the length argument to 110 whenever it is 112.  Compatibility
 *   wrappers provided below.
 */

#include <linux/syscalls.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/cred.h>
#include <linux/fcntl.h>
#include <linux/eventpoll.h>
#include <linux/sem.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/ipc.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

struct oldabi_stat64 {
	unsigned long long st_dev;
	unsigned int	__pad1;
	unsigned long	__st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;

	unsigned long	st_uid;
	unsigned long	st_gid;

	unsigned long long st_rdev;
	unsigned int	__pad2;

	long long	st_size;
	unsigned long	st_blksize;
	unsigned long long st_blocks;

	unsigned long	st_atime;
	unsigned long	st_atime_nsec;

	unsigned long	st_mtime;
	unsigned long	st_mtime_nsec;

	unsigned long	st_ctime;
	unsigned long	st_ctime_nsec;

	unsigned long long st_ino;
} __attribute__ ((packed,aligned(4)));

static long cp_oldabi_stat64(struct kstat *stat,
			     struct oldabi_stat64 __user *statbuf)
{
	struct oldabi_stat64 tmp;

	tmp.st_dev = huge_encode_dev(stat->dev);
	tmp.__pad1 = 0;
	tmp.__st_ino = stat->ino;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	tmp.st_uid = from_kuid_munged(current_user_ns(), stat->uid);
	tmp.st_gid = from_kgid_munged(current_user_ns(), stat->gid);
	tmp.st_rdev = huge_encode_dev(stat->rdev);
	tmp.st_size = stat->size;
	tmp.st_blocks = stat->blocks;
	tmp.__pad2 = 0;
	tmp.st_blksize = stat->blksize;
	tmp.st_atime = stat->atime.tv_sec;
	tmp.st_atime_nsec = stat->atime.tv_nsec;
	tmp.st_mtime = stat->mtime.tv_sec;
	tmp.st_mtime_nsec = stat->mtime.tv_nsec;
	tmp.st_ctime = stat->ctime.tv_sec;
	tmp.st_ctime_nsec = stat->ctime.tv_nsec;
	tmp.st_ino = stat->ino;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

asmlinkage long sys_oabi_stat64(const char __user * filename,
				struct oldabi_stat64 __user * statbuf)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);
	if (!error)
		error = cp_oldabi_stat64(&stat, statbuf);
	return error;
}

asmlinkage long sys_oabi_lstat64(const char __user * filename,
				 struct oldabi_stat64 __user * statbuf)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);
	if (!error)
		error = cp_oldabi_stat64(&stat, statbuf);
	return error;
}

asmlinkage long sys_oabi_fstat64(unsigned long fd,
				 struct oldabi_stat64 __user * statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);
	if (!error)
		error = cp_oldabi_stat64(&stat, statbuf);
	return error;
}

asmlinkage long sys_oabi_fstatat64(int dfd,
				   const char __user *filename,
				   struct oldabi_stat64  __user *statbuf,
				   int flag)
{
	struct kstat stat;
	int error;

	error = vfs_fstatat(dfd, filename, &stat, flag);
	if (error)
		return error;
	return cp_oldabi_stat64(&stat, statbuf);
}

struct oabi_flock64 {
	short	l_type;
	short	l_whence;
	loff_t	l_start;
	loff_t	l_len;
	pid_t	l_pid;
} __attribute__ ((packed,aligned(4)));

static long do_locks(unsigned int fd, unsigned int cmd,
				 unsigned long arg)
{
	struct flock64 kernel;
	struct oabi_flock64 user;
	mm_segment_t fs;
	long ret;

	if (copy_from_user(&user, (struct oabi_flock64 __user *)arg,
			   sizeof(user)))
		return -EFAULT;
	kernel.l_type	= user.l_type;
	kernel.l_whence	= user.l_whence;
	kernel.l_start	= user.l_start;
	kernel.l_len	= user.l_len;
	kernel.l_pid	= user.l_pid;

	fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_fcntl64(fd, cmd, (unsigned long)&kernel);
	set_fs(fs);

	if (!ret && (cmd == F_GETLK64 || cmd == F_OFD_GETLK)) {
		user.l_type	= kernel.l_type;
		user.l_whence	= kernel.l_whence;
		user.l_start	= kernel.l_start;
		user.l_len	= kernel.l_len;
		user.l_pid	= kernel.l_pid;
		if (copy_to_user((struct oabi_flock64 __user *)arg,
				 &user, sizeof(user)))
			ret = -EFAULT;
	}
	return ret;
}

asmlinkage long sys_oabi_fcntl64(unsigned int fd, unsigned int cmd,
				 unsigned long arg)
{
	switch (cmd) {
	case F_OFD_GETLK:
	case F_OFD_SETLK:
	case F_OFD_SETLKW:
	case F_GETLK64:
	case F_SETLK64:
	case F_SETLKW64:
		return do_locks(fd, cmd, arg);

	default:
		return sys_fcntl64(fd, cmd, arg);
	}
}

struct oabi_epoll_event {
	__u32 events;
	__u64 data;
} __attribute__ ((packed,aligned(4)));

asmlinkage long sys_oabi_epoll_ctl(int epfd, int op, int fd,
				   struct oabi_epoll_event __user *event)
{
	struct oabi_epoll_event user;
	struct epoll_event kernel;
	mm_segment_t fs;
	long ret;

	if (op == EPOLL_CTL_DEL)
		return sys_epoll_ctl(epfd, op, fd, NULL);
	if (copy_from_user(&user, event, sizeof(user)))
		return -EFAULT;
	kernel.events = user.events;
	kernel.data   = user.data;
	fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_epoll_ctl(epfd, op, fd, &kernel);
	set_fs(fs);
	return ret;
}

asmlinkage long sys_oabi_epoll_wait(int epfd,
				    struct oabi_epoll_event __user *events,
				    int maxevents, int timeout)
{
	struct epoll_event *kbuf;
	mm_segment_t fs;
	long ret, err, i;

	if (maxevents <= 0 ||
			maxevents > (INT_MAX/sizeof(*kbuf)) ||
			maxevents > (INT_MAX/sizeof(*events)))
		return -EINVAL;
	if (!access_ok(VERIFY_WRITE, events, sizeof(*events) * maxevents))
		return -EFAULT;
	kbuf = kmalloc_array(maxevents, sizeof(*kbuf), GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;
	fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_epoll_wait(epfd, kbuf, maxevents, timeout);
	set_fs(fs);
	err = 0;
	for (i = 0; i < ret; i++) {
		__put_user_error(kbuf[i].events, &events->events, err);
		__put_user_error(kbuf[i].data,   &events->data,   err);
		events++;
	}
	kfree(kbuf);
	return err ? -EFAULT : ret;
}

struct oabi_sembuf {
	unsigned short	sem_num;
	short		sem_op;
	short		sem_flg;
	unsigned short	__pad;
};

asmlinkage long sys_oabi_semtimedop(int semid,
				    struct oabi_sembuf __user *tsops,
				    unsigned nsops,
				    const struct timespec __user *timeout)
{
	struct sembuf *sops;
	struct timespec local_timeout;
	long err;
	int i;

	if (nsops < 1 || nsops > SEMOPM)
		return -EINVAL;
	if (!access_ok(VERIFY_READ, tsops, sizeof(*tsops) * nsops))
		return -EFAULT;
	sops = kmalloc_array(nsops, sizeof(*sops), GFP_KERNEL);
	if (!sops)
		return -ENOMEM;
	err = 0;
	for (i = 0; i < nsops; i++) {
		struct oabi_sembuf osb;
		err |= __copy_from_user(&osb, tsops, sizeof(osb));
		sops[i].sem_num = osb.sem_num;
		sops[i].sem_op = osb.sem_op;
		sops[i].sem_flg = osb.sem_flg;
		tsops++;
	}
	if (timeout) {
		/* copy this as well before changing domain protection */
		err |= copy_from_user(&local_timeout, timeout, sizeof(*timeout));
		timeout = &local_timeout;
	}
	if (err) {
		err = -EFAULT;
	} else {
		mm_segment_t fs = get_fs();
		set_fs(KERNEL_DS);
		err = sys_semtimedop(semid, sops, nsops, timeout);
		set_fs(fs);
	}
	kfree(sops);
	return err;
}

asmlinkage long sys_oabi_semop(int semid, struct oabi_sembuf __user *tsops,
			       unsigned nsops)
{
	return sys_oabi_semtimedop(semid, tsops, nsops, NULL);
}

asmlinkage int sys_oabi_ipc(uint call, int first, int second, int third,
			    void __user *ptr, long fifth)
{
	switch (call & 0xffff) {
	case SEMOP:
		return  sys_oabi_semtimedop(first,
					    (struct oabi_sembuf __user *)ptr,
					    second, NULL);
	case SEMTIMEDOP:
		return  sys_oabi_semtimedop(first,
					    (struct oabi_sembuf __user *)ptr,
					    second,
					    (const struct timespec __user *)fifth);
	default:
		return sys_ipc(call, first, second, third, ptr, fifth);
	}
}

asmlinkage long sys_oabi_bind(int fd, struct sockaddr __user *addr, int addrlen)
{
	sa_family_t sa_family;
	if (addrlen == 112 &&
	    get_user(sa_family, &addr->sa_family) == 0 &&
	    sa_family == AF_UNIX)
			addrlen = 110;
	return sys_bind(fd, addr, addrlen);
}

asmlinkage long sys_oabi_connect(int fd, struct sockaddr __user *addr, int addrlen)
{
	sa_family_t sa_family;
	if (addrlen == 112 &&
	    get_user(sa_family, &addr->sa_family) == 0 &&
	    sa_family == AF_UNIX)
			addrlen = 110;
	return sys_connect(fd, addr, addrlen);
}

asmlinkage long sys_oabi_sendto(int fd, void __user *buff,
				size_t len, unsigned flags,
				struct sockaddr __user *addr,
				int addrlen)
{
	sa_family_t sa_family;
	if (addrlen == 112 &&
	    get_user(sa_family, &addr->sa_family) == 0 &&
	    sa_family == AF_UNIX)
			addrlen = 110;
	return sys_sendto(fd, buff, len, flags, addr, addrlen);
}

asmlinkage long sys_oabi_sendmsg(int fd, struct user_msghdr __user *msg, unsigned flags)
{
	struct sockaddr __user *addr;
	int msg_namelen;
	sa_family_t sa_family;
	if (msg &&
	    get_user(msg_namelen, &msg->msg_namelen) == 0 &&
	    msg_namelen == 112 &&
	    get_user(addr, &msg->msg_name) == 0 &&
	    get_user(sa_family, &addr->sa_family) == 0 &&
	    sa_family == AF_UNIX)
	{
		/*
		 * HACK ALERT: there is a limit to how much backward bending
		 * we should do for what is actually a transitional
		 * compatibility layer.  This already has known flaws with
		 * a few ioctls that we don't intend to fix.  Therefore
		 * consider this blatent hack as another one... and take care
		 * to run for cover.  In most cases it will "just work fine".
		 * If it doesn't, well, tough.
		 */
		put_user(110, &msg->msg_namelen);
	}
	return sys_sendmsg(fd, msg, flags);
}

asmlinkage long sys_oabi_socketcall(int call, unsigned long __user *args)
{
	unsigned long r = -EFAULT, a[6];

	switch (call) {
	case SYS_BIND:
		if (copy_from_user(a, args, 3 * sizeof(long)) == 0)
			r = sys_oabi_bind(a[0], (struct sockaddr __user *)a[1], a[2]);
		break;
	case SYS_CONNECT:
		if (copy_from_user(a, args, 3 * sizeof(long)) == 0)
			r = sys_oabi_connect(a[0], (struct sockaddr __user *)a[1], a[2]);
		break;
	case SYS_SENDTO:
		if (copy_from_user(a, args, 6 * sizeof(long)) == 0)
			r = sys_oabi_sendto(a[0], (void __user *)a[1], a[2], a[3],
					    (struct sockaddr __user *)a[4], a[5]);
		break;
	case SYS_SENDMSG:
		if (copy_from_user(a, args, 3 * sizeof(long)) == 0)
			r = sys_oabi_sendmsg(a[0], (struct user_msghdr __user *)a[1], a[2]);
		break;
	default:
		r = sys_socketcall(call, args);
	}

	return r;
}
