/*
 * Conversion between 32-bit and 64-bit native system calls.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Written by Ulf Carlsson (ulfc@engr.sgi.com)
 * sys32_execve from ia64/ia32 code, Feb 2000, Kanoj Sarcar (kanoj@sgi.com)
 */
#include <linux/config.h>
#include <linux/compiler.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/smp_lock.h>
#include <linux/highuid.h>
#include <linux/dirent.h>
#include <linux/resource.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/times.h>
#include <linux/poll.h>
#include <linux/slab.h>
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
#include <linux/timex.h>
#include <linux/dnotify.h>
#include <linux/module.h>
#include <linux/binfmts.h>
#include <linux/security.h>
#include <linux/compat.h>
#include <linux/vfs.h>

#include <net/sock.h>
#include <net/scm.h>

#include <asm/ipc.h>
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
#define merge_64(r1,r2)	((((r1) & 0xffffffffUL) << 32) + ((r2) & 0xffffffffUL))
#endif
#ifdef __MIPSEL__
#define merge_64(r1,r2)	((((r2) & 0xffffffffUL) << 32) + ((r1) & 0xffffffffUL))
#endif

/*
 * Revalidate the inode. This is required for proper NFS attribute caching.
 */

int cp_compat_stat(struct kstat *stat, struct compat_stat *statbuf)
{
	struct compat_stat tmp;

	if (!new_valid_dev(stat->dev) || !new_valid_dev(stat->rdev))
		return -EOVERFLOW;

	memset(&tmp, 0, sizeof(tmp));
	tmp.st_dev = new_encode_dev(stat->dev);
	tmp.st_ino = stat->ino;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	SET_UID(tmp.st_uid, stat->uid);
	SET_GID(tmp.st_gid, stat->gid);
	tmp.st_rdev = new_encode_dev(stat->rdev);
	tmp.st_size = stat->size;
	tmp.st_atime = stat->atime.tv_sec;
	tmp.st_mtime = stat->mtime.tv_sec;
	tmp.st_ctime = stat->ctime.tv_sec;
#ifdef STAT_HAVE_NSEC
	tmp.st_atime_nsec = stat->atime.tv_nsec;
	tmp.st_mtime_nsec = stat->mtime.tv_nsec;
	tmp.st_ctime_nsec = stat->ctime.tv_nsec;
#endif
	tmp.st_blocks = stat->blocks;
	tmp.st_blksize = stat->blksize;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

asmlinkage unsigned long
sys32_mmap2(unsigned long addr, unsigned long len, unsigned long prot,
         unsigned long flags, unsigned long fd, unsigned long pgoff)
{
	struct file * file = NULL;
	unsigned long error;

	error = -EINVAL;
	if (!(flags & MAP_ANONYMOUS)) {
		error = -EBADF;
		file = fget(fd);
		if (!file)
			goto out;
	}
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);
	if (file)
		fput(file);

out:
	return error;
}


asmlinkage int sys_truncate64(const char *path, unsigned int high,
			      unsigned int low)
{
	if ((int)high < 0)
		return -EINVAL;
	return sys_truncate(path, ((long) high << 32) | low);
}

asmlinkage int sys_ftruncate64(unsigned int fd, unsigned int high,
			       unsigned int low)
{
	if ((int)high < 0)
		return -EINVAL;
	return sys_ftruncate(fd, ((long) high << 32) | low);
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

struct dirent32 {
	unsigned int	d_ino;
	unsigned int	d_off;
	unsigned short	d_reclen;
	char		d_name[NAME_MAX + 1];
};

static void
xlate_dirent(void *dirent64, void *dirent32, long n)
{
	long off;
	struct dirent *dirp;
	struct dirent32 *dirp32;

	off = 0;
	while (off < n) {
		dirp = (struct dirent *)(dirent64 + off);
		dirp32 = (struct dirent32 *)(dirent32 + off);
		off += dirp->d_reclen;
		dirp32->d_ino = dirp->d_ino;
		dirp32->d_off = (unsigned int)dirp->d_off;
		dirp32->d_reclen = dirp->d_reclen;
		strncpy(dirp32->d_name, dirp->d_name, dirp->d_reclen - ((3 * 4) + 2));
	}
	return;
}

asmlinkage long
sys32_getdents(unsigned int fd, void * dirent32, unsigned int count)
{
	long n;
	void *dirent64;

	dirent64 = (void *)((unsigned long)(dirent32 + (sizeof(long) - 1)) & ~(sizeof(long) - 1));
	if ((n = sys_getdents(fd, dirent64, count - (dirent64 - dirent32))) < 0)
		return(n);
	xlate_dirent(dirent64, dirent32, n);
	return(n);
}

asmlinkage int old_readdir(unsigned int fd, void * dirent, unsigned int count);

asmlinkage int
sys32_readdir(unsigned int fd, void * dirent32, unsigned int count)
{
	int n;
	struct dirent dirent64;

	if ((n = old_readdir(fd, &dirent64, count)) < 0)
		return(n);
	xlate_dirent(&dirent64, dirent32, dirent64.d_reclen);
	return(n);
}

asmlinkage int
sys32_waitpid(compat_pid_t pid, unsigned int *stat_addr, int options)
{
	return compat_sys_wait4(pid, stat_addr, options, NULL);
}

asmlinkage long
sysn32_waitid(int which, compat_pid_t pid,
	      siginfo_t __user *uinfo, int options,
	      struct compat_rusage __user *uru)
{
	struct rusage ru;
	long ret;
	mm_segment_t old_fs = get_fs();

	set_fs (KERNEL_DS);
	ret = sys_waitid(which, pid, uinfo, options,
			 uru ? (struct rusage __user *) &ru : NULL);
	set_fs (old_fs);

	if (ret < 0 || uinfo->si_signo == 0)
		return ret;

	if (uru)
		ret = put_compat_rusage(&ru, uru);
	return ret;
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
        u16 procs;
	u32 totalhigh;
	u32 freehigh;
	u32 mem_unit;
	char _f[8];
};

asmlinkage int sys32_sysinfo(struct sysinfo32 *info)
{
	struct sysinfo s;
	int ret, err;
	mm_segment_t old_fs = get_fs ();

	set_fs (KERNEL_DS);
	ret = sys_sysinfo(&s);
	set_fs (old_fs);
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

#define RLIM_INFINITY32	0x7fffffff
#define RESOURCE32(x) ((x > RLIM_INFINITY32) ? RLIM_INFINITY32 : x)

struct rlimit32 {
	int	rlim_cur;
	int	rlim_max;
};

#ifdef __MIPSEB__
asmlinkage long sys32_truncate64(const char * path, unsigned long __dummy,
	int length_hi, int length_lo)
#endif
#ifdef __MIPSEL__
asmlinkage long sys32_truncate64(const char * path, unsigned long __dummy,
	int length_lo, int length_hi)
#endif
{
	loff_t length;

	length = ((unsigned long) length_hi << 32) | (unsigned int) length_lo;

	return sys_truncate(path, length);
}

#ifdef __MIPSEB__
asmlinkage long sys32_ftruncate64(unsigned int fd, unsigned long __dummy,
	int length_hi, int length_lo)
#endif
#ifdef __MIPSEL__
asmlinkage long sys32_ftruncate64(unsigned int fd, unsigned long __dummy,
	int length_lo, int length_hi)
#endif
{
	loff_t length;

	length = ((unsigned long) length_hi << 32) | (unsigned int) length_lo;

	return sys_ftruncate(fd, length);
}

static inline long
get_tv32(struct timeval *o, struct compat_timeval *i)
{
	return (!access_ok(VERIFY_READ, i, sizeof(*i)) ||
		(__get_user(o->tv_sec, &i->tv_sec) |
		 __get_user(o->tv_usec, &i->tv_usec)));
}

static inline long
put_tv32(struct compat_timeval *o, struct timeval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->tv_sec, &o->tv_sec) |
		 __put_user(i->tv_usec, &o->tv_usec)));
}

extern struct timezone sys_tz;

asmlinkage int
sys32_gettimeofday(struct compat_timeval *tv, struct timezone *tz)
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

static inline long get_ts32(struct timespec *o, struct compat_timeval *i)
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

asmlinkage int
sys32_settimeofday(struct compat_timeval *tv, struct timezone *tz)
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

asmlinkage int sys32_llseek(unsigned int fd, unsigned int offset_high,
			    unsigned int offset_low, loff_t * result,
			    unsigned int origin)
{
	return sys_llseek(fd, offset_high, offset_low, result, origin);
}

/* From the Single Unix Spec: pread & pwrite act like lseek to pos + op +
   lseek back to original location.  They fail just like lseek does on
   non-seekable files.  */

asmlinkage ssize_t sys32_pread(unsigned int fd, char * buf,
			       size_t count, u32 unused, u64 a4, u64 a5)
{
	ssize_t ret;
	struct file * file;
	ssize_t (*read)(struct file *, char *, size_t, loff_t *);
	loff_t pos;

	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (!(file->f_mode & FMODE_READ))
		goto out;
	pos = merge_64(a4, a5);
	ret = rw_verify_area(READ, file, &pos, count);
	if (ret)
		goto out;
	ret = -EINVAL;
	if (!file->f_op || !(read = file->f_op->read))
		goto out;
	if (pos < 0)
		goto out;
	ret = -ESPIPE;
	if (!(file->f_mode & FMODE_PREAD))
		goto out;
	ret = read(file, buf, count, &pos);
	if (ret > 0)
		dnotify_parent(file->f_dentry, DN_ACCESS);
out:
	fput(file);
bad_file:
	return ret;
}

asmlinkage ssize_t sys32_pwrite(unsigned int fd, const char * buf,
			        size_t count, u32 unused, u64 a4, u64 a5)
{
	ssize_t ret;
	struct file * file;
	ssize_t (*write)(struct file *, const char *, size_t, loff_t *);
	loff_t pos;

	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (!(file->f_mode & FMODE_WRITE))
		goto out;
	pos = merge_64(a4, a5);
	ret = rw_verify_area(WRITE, file, &pos, count);
	if (ret)
		goto out;
	ret = -EINVAL;
	if (!file->f_op || !(write = file->f_op->write))
		goto out;
	if (pos < 0)
		goto out;

	ret = -ESPIPE;
	if (!(file->f_mode & FMODE_PWRITE))
		goto out;

	ret = write(file, buf, count, &pos);
	if (ret > 0)
		dnotify_parent(file->f_dentry, DN_MODIFY);
out:
	fput(file);
bad_file:
	return ret;
}

asmlinkage int sys32_sched_rr_get_interval(compat_pid_t pid,
	struct compat_timespec *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();

	set_fs (KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid, &t);
	set_fs (old_fs);
	if (put_user (t.tv_sec, &interval->tv_sec) ||
	    __put_user (t.tv_nsec, &interval->tv_nsec))
		return -EFAULT;
	return ret;
}

struct msgbuf32 { s32 mtype; char mtext[1]; };

struct ipc_perm32
{
	key_t    	  key;
        __compat_uid_t  uid;
        __compat_gid_t  gid;
        __compat_uid_t  cuid;
        __compat_gid_t  cgid;
        compat_mode_t	mode;
        unsigned short  seq;
};

struct ipc64_perm32 {
	key_t key;
	__compat_uid_t uid;
	__compat_gid_t gid;
	__compat_uid_t cuid;
	__compat_gid_t cgid;
	compat_mode_t	mode;
	unsigned short	seq;
	unsigned short __pad1;
	unsigned int __unused1;
	unsigned int __unused2;
};

struct semid_ds32 {
        struct ipc_perm32 sem_perm;               /* permissions .. see ipc.h */
        compat_time_t   sem_otime;              /* last semop time */
        compat_time_t   sem_ctime;              /* last change time */
        u32 sem_base;              /* ptr to first semaphore in array */
        u32 sem_pending;          /* pending operations to be processed */
        u32 sem_pending_last;    /* last pending operation */
        u32 undo;                  /* undo requests on this array */
        unsigned short  sem_nsems;              /* no. of semaphores in array */
};

struct semid64_ds32 {
	struct ipc64_perm32	sem_perm;
	compat_time_t	sem_otime;
	compat_time_t	sem_ctime;
	unsigned int		sem_nsems;
	unsigned int		__unused1;
	unsigned int		__unused2;
};

struct msqid_ds32
{
        struct ipc_perm32 msg_perm;
        u32 msg_first;
        u32 msg_last;
        compat_time_t   msg_stime;
        compat_time_t   msg_rtime;
        compat_time_t   msg_ctime;
        u32 wwait;
        u32 rwait;
        unsigned short msg_cbytes;
        unsigned short msg_qnum;
        unsigned short msg_qbytes;
        compat_ipc_pid_t msg_lspid;
        compat_ipc_pid_t msg_lrpid;
};

struct msqid64_ds32 {
	struct ipc64_perm32 msg_perm;
	compat_time_t msg_stime;
	unsigned int __unused1;
	compat_time_t msg_rtime;
	unsigned int __unused2;
	compat_time_t msg_ctime;
	unsigned int __unused3;
	unsigned int msg_cbytes;
	unsigned int msg_qnum;
	unsigned int msg_qbytes;
	compat_pid_t msg_lspid;
	compat_pid_t msg_lrpid;
	unsigned int __unused4;
	unsigned int __unused5;
};

struct shmid_ds32 {
        struct ipc_perm32       shm_perm;
        int                     shm_segsz;
        compat_time_t		shm_atime;
        compat_time_t		shm_dtime;
        compat_time_t		shm_ctime;
        compat_ipc_pid_t    shm_cpid;
        compat_ipc_pid_t    shm_lpid;
        unsigned short          shm_nattch;
};

struct shmid64_ds32 {
	struct ipc64_perm32	shm_perm;
	compat_size_t		shm_segsz;
	compat_time_t		shm_atime;
	compat_time_t		shm_dtime;
	compat_time_t shm_ctime;
	compat_pid_t shm_cpid;
	compat_pid_t shm_lpid;
	unsigned int shm_nattch;
	unsigned int __unused1;
	unsigned int __unused2;
};

struct ipc_kludge32 {
	u32 msgp;
	s32 msgtyp;
};

static int
do_sys32_semctl(int first, int second, int third, void *uptr)
{
	union semun fourth;
	u32 pad;
	int err, err2;
	struct semid64_ds s;
	mm_segment_t old_fs;

	if (!uptr)
		return -EINVAL;
	err = -EFAULT;
	if (get_user (pad, (u32 *)uptr))
		return err;
	if ((third & ~IPC_64) == SETVAL)
		fourth.val = (int)pad;
	else
		fourth.__pad = (void *)A(pad);
	switch (third & ~IPC_64) {
	case IPC_INFO:
	case IPC_RMID:
	case IPC_SET:
	case SEM_INFO:
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
	case SETVAL:
	case SETALL:
		err = sys_semctl (first, second, third, fourth);
		break;

	case IPC_STAT:
	case SEM_STAT:
		fourth.__pad = &s;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = sys_semctl(first, second, third | IPC_64, fourth);
		set_fs(old_fs);

		if (third & IPC_64) {
			struct semid64_ds32 *usp64 = (struct semid64_ds32 *) A(pad);

			if (!access_ok(VERIFY_WRITE, usp64, sizeof(*usp64))) {
				err = -EFAULT;
				break;
			}
			err2 = __put_user(s.sem_perm.key, &usp64->sem_perm.key);
			err2 |= __put_user(s.sem_perm.uid, &usp64->sem_perm.uid);
			err2 |= __put_user(s.sem_perm.gid, &usp64->sem_perm.gid);
			err2 |= __put_user(s.sem_perm.cuid, &usp64->sem_perm.cuid);
			err2 |= __put_user(s.sem_perm.cgid, &usp64->sem_perm.cgid);
			err2 |= __put_user(s.sem_perm.mode, &usp64->sem_perm.mode);
			err2 |= __put_user(s.sem_perm.seq, &usp64->sem_perm.seq);
			err2 |= __put_user(s.sem_otime, &usp64->sem_otime);
			err2 |= __put_user(s.sem_ctime, &usp64->sem_ctime);
			err2 |= __put_user(s.sem_nsems, &usp64->sem_nsems);
		} else {
			struct semid_ds32 *usp32 = (struct semid_ds32 *) A(pad);

			if (!access_ok(VERIFY_WRITE, usp32, sizeof(*usp32))) {
				err = -EFAULT;
				break;
			}
			err2 = __put_user(s.sem_perm.key, &usp32->sem_perm.key);
			err2 |= __put_user(s.sem_perm.uid, &usp32->sem_perm.uid);
			err2 |= __put_user(s.sem_perm.gid, &usp32->sem_perm.gid);
			err2 |= __put_user(s.sem_perm.cuid, &usp32->sem_perm.cuid);
			err2 |= __put_user(s.sem_perm.cgid, &usp32->sem_perm.cgid);
			err2 |= __put_user(s.sem_perm.mode, &usp32->sem_perm.mode);
			err2 |= __put_user(s.sem_perm.seq, &usp32->sem_perm.seq);
			err2 |= __put_user(s.sem_otime, &usp32->sem_otime);
			err2 |= __put_user(s.sem_ctime, &usp32->sem_ctime);
			err2 |= __put_user(s.sem_nsems, &usp32->sem_nsems);
		}
		if (err2)
			err = -EFAULT;
		break;

	default:
		err = - EINVAL;
		break;
	}

	return err;
}

static int
do_sys32_msgsnd (int first, int second, int third, void *uptr)
{
	struct msgbuf32 *up = (struct msgbuf32 *)uptr;
	struct msgbuf *p;
	mm_segment_t old_fs;
	int err;

	if (second < 0)
		return -EINVAL;
	p = kmalloc (second + sizeof (struct msgbuf)
				    + 4, GFP_USER);
	if (!p)
		return -ENOMEM;
	err = get_user (p->mtype, &up->mtype);
	if (err)
		goto out;
	err |= __copy_from_user (p->mtext, &up->mtext, second);
	if (err)
		goto out;
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	err = sys_msgsnd (first, p, second, third);
	set_fs (old_fs);
out:
	kfree (p);

	return err;
}

static int
do_sys32_msgrcv (int first, int second, int msgtyp, int third,
		 int version, void *uptr)
{
	struct msgbuf32 *up;
	struct msgbuf *p;
	mm_segment_t old_fs;
	int err;

	if (!version) {
		struct ipc_kludge32 *uipck = (struct ipc_kludge32 *)uptr;
		struct ipc_kludge32 ipck;

		err = -EINVAL;
		if (!uptr)
			goto out;
		err = -EFAULT;
		if (copy_from_user (&ipck, uipck, sizeof (struct ipc_kludge32)))
			goto out;
		uptr = (void *)AA(ipck.msgp);
		msgtyp = ipck.msgtyp;
	}

	if (second < 0)
		return -EINVAL;
	err = -ENOMEM;
	p = kmalloc (second + sizeof (struct msgbuf) + 4, GFP_USER);
	if (!p)
		goto out;
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	err = sys_msgrcv (first, p, second + 4, msgtyp, third);
	set_fs (old_fs);
	if (err < 0)
		goto free_then_out;
	up = (struct msgbuf32 *)uptr;
	if (put_user (p->mtype, &up->mtype) ||
	    __copy_to_user (&up->mtext, p->mtext, err))
		err = -EFAULT;
free_then_out:
	kfree (p);
out:
	return err;
}

static int
do_sys32_msgctl (int first, int second, void *uptr)
{
	int err = -EINVAL, err2;
	struct msqid64_ds m;
	struct msqid_ds32 *up32 = (struct msqid_ds32 *)uptr;
	struct msqid64_ds32 *up64 = (struct msqid64_ds32 *)uptr;
	mm_segment_t old_fs;

	switch (second & ~IPC_64) {
	case IPC_INFO:
	case IPC_RMID:
	case MSG_INFO:
		err = sys_msgctl (first, second, (struct msqid_ds *)uptr);
		break;

	case IPC_SET:
		if (second & IPC_64) {
			if (!access_ok(VERIFY_READ, up64, sizeof(*up64))) {
				err = -EFAULT;
				break;
			}
			err = __get_user(m.msg_perm.uid, &up64->msg_perm.uid);
			err |= __get_user(m.msg_perm.gid, &up64->msg_perm.gid);
			err |= __get_user(m.msg_perm.mode, &up64->msg_perm.mode);
			err |= __get_user(m.msg_qbytes, &up64->msg_qbytes);
		} else {
			if (!access_ok(VERIFY_READ, up32, sizeof(*up32))) {
				err = -EFAULT;
				break;
			}
			err = __get_user(m.msg_perm.uid, &up32->msg_perm.uid);
			err |= __get_user(m.msg_perm.gid, &up32->msg_perm.gid);
			err |= __get_user(m.msg_perm.mode, &up32->msg_perm.mode);
			err |= __get_user(m.msg_qbytes, &up32->msg_qbytes);
		}
		if (err)
			break;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = sys_msgctl(first, second | IPC_64, (struct msqid_ds *)&m);
		set_fs(old_fs);
		break;

	case IPC_STAT:
	case MSG_STAT:
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = sys_msgctl(first, second | IPC_64, (struct msqid_ds *)&m);
		set_fs(old_fs);
		if (second & IPC_64) {
			if (!access_ok(VERIFY_WRITE, up64, sizeof(*up64))) {
				err = -EFAULT;
				break;
			}
			err2 = __put_user(m.msg_perm.key, &up64->msg_perm.key);
			err2 |= __put_user(m.msg_perm.uid, &up64->msg_perm.uid);
			err2 |= __put_user(m.msg_perm.gid, &up64->msg_perm.gid);
			err2 |= __put_user(m.msg_perm.cuid, &up64->msg_perm.cuid);
			err2 |= __put_user(m.msg_perm.cgid, &up64->msg_perm.cgid);
			err2 |= __put_user(m.msg_perm.mode, &up64->msg_perm.mode);
			err2 |= __put_user(m.msg_perm.seq, &up64->msg_perm.seq);
			err2 |= __put_user(m.msg_stime, &up64->msg_stime);
			err2 |= __put_user(m.msg_rtime, &up64->msg_rtime);
			err2 |= __put_user(m.msg_ctime, &up64->msg_ctime);
			err2 |= __put_user(m.msg_cbytes, &up64->msg_cbytes);
			err2 |= __put_user(m.msg_qnum, &up64->msg_qnum);
			err2 |= __put_user(m.msg_qbytes, &up64->msg_qbytes);
			err2 |= __put_user(m.msg_lspid, &up64->msg_lspid);
			err2 |= __put_user(m.msg_lrpid, &up64->msg_lrpid);
			if (err2)
				err = -EFAULT;
		} else {
			if (!access_ok(VERIFY_WRITE, up32, sizeof(*up32))) {
				err = -EFAULT;
				break;
			}
			err2 = __put_user(m.msg_perm.key, &up32->msg_perm.key);
			err2 |= __put_user(m.msg_perm.uid, &up32->msg_perm.uid);
			err2 |= __put_user(m.msg_perm.gid, &up32->msg_perm.gid);
			err2 |= __put_user(m.msg_perm.cuid, &up32->msg_perm.cuid);
			err2 |= __put_user(m.msg_perm.cgid, &up32->msg_perm.cgid);
			err2 |= __put_user(m.msg_perm.mode, &up32->msg_perm.mode);
			err2 |= __put_user(m.msg_perm.seq, &up32->msg_perm.seq);
			err2 |= __put_user(m.msg_stime, &up32->msg_stime);
			err2 |= __put_user(m.msg_rtime, &up32->msg_rtime);
			err2 |= __put_user(m.msg_ctime, &up32->msg_ctime);
			err2 |= __put_user(m.msg_cbytes, &up32->msg_cbytes);
			err2 |= __put_user(m.msg_qnum, &up32->msg_qnum);
			err2 |= __put_user(m.msg_qbytes, &up32->msg_qbytes);
			err2 |= __put_user(m.msg_lspid, &up32->msg_lspid);
			err2 |= __put_user(m.msg_lrpid, &up32->msg_lrpid);
			if (err2)
				err = -EFAULT;
		}
		break;
	}

	return err;
}

static int
do_sys32_shmat (int first, int second, int third, int version, void *uptr)
{
	unsigned long raddr;
	u32 *uaddr = (u32 *)A((u32)third);
	int err = -EINVAL;

	if (version == 1)
		return err;
	err = do_shmat (first, uptr, second, &raddr);
	if (err)
		return err;
	err = put_user (raddr, uaddr);
	return err;
}

struct shm_info32 {
	int used_ids;
	u32 shm_tot, shm_rss, shm_swp;
	u32 swap_attempts, swap_successes;
};

static int
do_sys32_shmctl (int first, int second, void *uptr)
{
	struct shmid64_ds32 *up64 = (struct shmid64_ds32 *)uptr;
	struct shmid_ds32 *up32 = (struct shmid_ds32 *)uptr;
	struct shm_info32 *uip = (struct shm_info32 *)uptr;
	int err = -EFAULT, err2;
	struct shmid64_ds s64;
	mm_segment_t old_fs;
	struct shm_info si;
	struct shmid_ds s;

	switch (second & ~IPC_64) {
	case IPC_INFO:
		second = IPC_INFO; /* So that we don't have to translate it */
	case IPC_RMID:
	case SHM_LOCK:
	case SHM_UNLOCK:
		err = sys_shmctl(first, second, (struct shmid_ds *)uptr);
		break;
	case IPC_SET:
		if (second & IPC_64) {
			err = get_user(s.shm_perm.uid, &up64->shm_perm.uid);
			err |= get_user(s.shm_perm.gid, &up64->shm_perm.gid);
			err |= get_user(s.shm_perm.mode, &up64->shm_perm.mode);
		} else {
			err = get_user(s.shm_perm.uid, &up32->shm_perm.uid);
			err |= get_user(s.shm_perm.gid, &up32->shm_perm.gid);
			err |= get_user(s.shm_perm.mode, &up32->shm_perm.mode);
		}
		if (err)
			break;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = sys_shmctl(first, second & ~IPC_64, &s);
		set_fs(old_fs);
		break;

	case IPC_STAT:
	case SHM_STAT:
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = sys_shmctl(first, second | IPC_64, (void *) &s64);
		set_fs(old_fs);
		if (err < 0)
			break;
		if (second & IPC_64) {
			if (!access_ok(VERIFY_WRITE, up64, sizeof(*up64))) {
				err = -EFAULT;
				break;
			}
			err2 = __put_user(s64.shm_perm.key, &up64->shm_perm.key);
			err2 |= __put_user(s64.shm_perm.uid, &up64->shm_perm.uid);
			err2 |= __put_user(s64.shm_perm.gid, &up64->shm_perm.gid);
			err2 |= __put_user(s64.shm_perm.cuid, &up64->shm_perm.cuid);
			err2 |= __put_user(s64.shm_perm.cgid, &up64->shm_perm.cgid);
			err2 |= __put_user(s64.shm_perm.mode, &up64->shm_perm.mode);
			err2 |= __put_user(s64.shm_perm.seq, &up64->shm_perm.seq);
			err2 |= __put_user(s64.shm_atime, &up64->shm_atime);
			err2 |= __put_user(s64.shm_dtime, &up64->shm_dtime);
			err2 |= __put_user(s64.shm_ctime, &up64->shm_ctime);
			err2 |= __put_user(s64.shm_segsz, &up64->shm_segsz);
			err2 |= __put_user(s64.shm_nattch, &up64->shm_nattch);
			err2 |= __put_user(s64.shm_cpid, &up64->shm_cpid);
			err2 |= __put_user(s64.shm_lpid, &up64->shm_lpid);
		} else {
			if (!access_ok(VERIFY_WRITE, up32, sizeof(*up32))) {
				err = -EFAULT;
				break;
			}
			err2 = __put_user(s64.shm_perm.key, &up32->shm_perm.key);
			err2 |= __put_user(s64.shm_perm.uid, &up32->shm_perm.uid);
			err2 |= __put_user(s64.shm_perm.gid, &up32->shm_perm.gid);
			err2 |= __put_user(s64.shm_perm.cuid, &up32->shm_perm.cuid);
			err2 |= __put_user(s64.shm_perm.cgid, &up32->shm_perm.cgid);
			err2 |= __put_user(s64.shm_perm.mode, &up32->shm_perm.mode);
			err2 |= __put_user(s64.shm_perm.seq, &up32->shm_perm.seq);
			err2 |= __put_user(s64.shm_atime, &up32->shm_atime);
			err2 |= __put_user(s64.shm_dtime, &up32->shm_dtime);
			err2 |= __put_user(s64.shm_ctime, &up32->shm_ctime);
			err2 |= __put_user(s64.shm_segsz, &up32->shm_segsz);
			err2 |= __put_user(s64.shm_nattch, &up32->shm_nattch);
			err2 |= __put_user(s64.shm_cpid, &up32->shm_cpid);
			err2 |= __put_user(s64.shm_lpid, &up32->shm_lpid);
		}
		if (err2)
			err = -EFAULT;
		break;

	case SHM_INFO:
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = sys_shmctl(first, second, (void *)&si);
		set_fs(old_fs);
		if (err < 0)
			break;
		err2 = put_user(si.used_ids, &uip->used_ids);
		err2 |= __put_user(si.shm_tot, &uip->shm_tot);
		err2 |= __put_user(si.shm_rss, &uip->shm_rss);
		err2 |= __put_user(si.shm_swp, &uip->shm_swp);
		err2 |= __put_user(si.swap_attempts, &uip->swap_attempts);
		err2 |= __put_user (si.swap_successes, &uip->swap_successes);
		if (err2)
			err = -EFAULT;
		break;

	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int sys32_semtimedop(int semid, struct sembuf *tsems, int nsems,
                            const struct compat_timespec *timeout32)
{
	struct compat_timespec t32;
	struct timespec *t64 = compat_alloc_user_space(sizeof(*t64));

	if (copy_from_user(&t32, timeout32, sizeof(t32)))
		return -EFAULT;

	if (put_user(t32.tv_sec, &t64->tv_sec) ||
	    put_user(t32.tv_nsec, &t64->tv_nsec))
		return -EFAULT;

	return sys_semtimedop(semid, tsems, nsems, t64);
}

asmlinkage long
sys32_ipc (u32 call, int first, int second, int third, u32 ptr, u32 fifth)
{
	int version, err;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		err = sys_semtimedop (first, (struct sembuf *)AA(ptr), second,
		                      NULL);
		break;
	case SEMTIMEDOP:
		err = sys32_semtimedop (first, (struct sembuf *)AA(ptr), second,
		                      (const struct compat_timespec __user *)AA(fifth));
		break;
	case SEMGET:
		err = sys_semget (first, second, third);
		break;
	case SEMCTL:
		err = do_sys32_semctl (first, second, third,
				       (void *)AA(ptr));
		break;

	case MSGSND:
		err = do_sys32_msgsnd (first, second, third,
				       (void *)AA(ptr));
		break;
	case MSGRCV:
		err = do_sys32_msgrcv (first, second, fifth, third,
				       version, (void *)AA(ptr));
		break;
	case MSGGET:
		err = sys_msgget ((key_t) first, second);
		break;
	case MSGCTL:
		err = do_sys32_msgctl (first, second, (void *)AA(ptr));
		break;

	case SHMAT:
		err = do_sys32_shmat (first, second, third,
				      version, (void *)AA(ptr));
		break;
	case SHMDT:
		err = sys_shmdt ((char *)A(ptr));
		break;
	case SHMGET:
		err = sys_shmget (first, (unsigned)second, third);
		break;
	case SHMCTL:
		err = do_sys32_shmctl (first, second, (void *)AA(ptr));
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

asmlinkage long sys32_shmat(int shmid, char __user *shmaddr,
			  int shmflg, int32_t *addr)
{
	unsigned long raddr;
	int err;

	err = do_shmat(shmid, shmaddr, shmflg, &raddr);
	if (err)
		return err;

	return put_user(raddr, addr);
}

struct sysctl_args32
{
	compat_caddr_t name;
	int nlen;
	compat_caddr_t oldval;
	compat_caddr_t oldlenp;
	compat_caddr_t newval;
	compat_size_t newlen;
	unsigned int __unused[4];
};

#ifdef CONFIG_SYSCTL

asmlinkage long sys32_sysctl(struct sysctl_args32 *args)
{
	struct sysctl_args32 tmp;
	int error;
	size_t oldlen, *oldlenp = NULL;
	unsigned long addr = (((long)&args->__unused[0]) + 7) & ~7;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	if (tmp.oldval && tmp.oldlenp) {
		/* Duh, this is ugly and might not work if sysctl_args
		   is in read-only memory, but do_sysctl does indirectly
		   a lot of uaccess in both directions and we'd have to
		   basically copy the whole sysctl.c here, and
		   glibc's __sysctl uses rw memory for the structure
		   anyway.  */
		if (get_user(oldlen, (u32 *)A(tmp.oldlenp)) ||
		    put_user(oldlen, (size_t *)addr))
			return -EFAULT;
		oldlenp = (size_t *)addr;
	}

	lock_kernel();
	error = do_sysctl((int *)A(tmp.name), tmp.nlen, (void *)A(tmp.oldval),
			  oldlenp, (void *)A(tmp.newval), tmp.newlen);
	unlock_kernel();
	if (oldlenp) {
		if (!error) {
			if (get_user(oldlen, (size_t *)addr) ||
			    put_user(oldlen, (u32 *)A(tmp.oldlenp)))
				error = -EFAULT;
		}
		copy_to_user(args->__unused, tmp.__unused, sizeof(tmp.__unused));
	}
	return error;
}

#endif /* CONFIG_SYSCTL */

asmlinkage long sys32_newuname(struct new_utsname * name)
{
	int ret = 0;

	down_read(&uts_sem);
	if (copy_to_user(name,&system_utsname,sizeof *name))
		ret = -EFAULT;
	up_read(&uts_sem);

	if (current->personality == PER_LINUX32 && !ret)
		if (copy_to_user(name->machine, "mips\0\0\0", 8))
			ret = -EFAULT;

	return ret;
}

asmlinkage int sys32_personality(unsigned long personality)
{
	int ret;
	if (current->personality == PER_LINUX32 && personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	if (ret == PER_LINUX32)
		ret = PER_LINUX;
	return ret;
}

/* ustat compatibility */
struct ustat32 {
	compat_daddr_t	f_tfree;
	compat_ino_t	f_tinode;
	char		f_fname[6];
	char		f_fpack[6];
};

extern asmlinkage long sys_ustat(dev_t dev, struct ustat * ubuf);

asmlinkage int sys32_ustat(dev_t dev, struct ustat32 * ubuf32)
{
	int err;
        struct ustat tmp;
	struct ustat32 tmp32;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	err = sys_ustat(dev, &tmp);
	set_fs (old_fs);

	if (err)
		goto out;

        memset(&tmp32,0,sizeof(struct ustat32));
        tmp32.f_tfree = tmp.f_tfree;
        tmp32.f_tinode = tmp.f_tinode;

        err = copy_to_user(ubuf32,&tmp32,sizeof(struct ustat32)) ? -EFAULT : 0;

out:
	return err;
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

asmlinkage int sys32_adjtimex(struct timex32 *utp)
{
	struct timex txc;
	int ret;

	memset(&txc, 0, sizeof(struct timex));

	if (get_user(txc.modes, &utp->modes) ||
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

	if (put_user(txc.modes, &utp->modes) ||
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

asmlinkage int sys32_sendfile(int out_fd, int in_fd, compat_off_t *offset,
	s32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	off_t of;

	if (offset && get_user(of, offset))
		return -EFAULT;

	set_fs(KERNEL_DS);
	ret = sys_sendfile(out_fd, in_fd, offset ? &of : NULL, count);
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

/* Argument list sizes for sys_socketcall */
#define AL(x) ((x) * sizeof(unsigned int))
static unsigned char socketcall_nargs[18]={AL(0),AL(3),AL(3),AL(3),AL(2),AL(3),
				AL(3),AL(3),AL(4),AL(4),AL(4),AL(6),
				AL(6),AL(2),AL(5),AL(5),AL(3),AL(3)};
#undef AL

/*
 *	System call vectors.
 *
 *	Argument checking cleaned up. Saved 20% in size.
 *  This function doesn't need to set the kernel lock because
 *  it is set by the callees.
 */

asmlinkage long sys32_socketcall(int call, unsigned int *args32)
{
	unsigned int a[6];
	unsigned int a0,a1;
	int err;

	extern asmlinkage long sys_socket(int family, int type, int protocol);
	extern asmlinkage long sys_bind(int fd, struct sockaddr __user *umyaddr, int addrlen);
	extern asmlinkage long sys_connect(int fd, struct sockaddr __user *uservaddr, int addrlen);
	extern asmlinkage long sys_listen(int fd, int backlog);
	extern asmlinkage long sys_accept(int fd, struct sockaddr __user *upeer_sockaddr, int __user *upeer_addrlen);
	extern asmlinkage long sys_getsockname(int fd, struct sockaddr __user *usockaddr, int __user *usockaddr_len);
	extern asmlinkage long sys_getpeername(int fd, struct sockaddr __user *usockaddr, int __user *usockaddr_len);
	extern asmlinkage long sys_socketpair(int family, int type, int protocol, int __user *usockvec);
	extern asmlinkage long sys_send(int fd, void __user * buff, size_t len, unsigned flags);
	extern asmlinkage long sys_sendto(int fd, void __user * buff, size_t len, unsigned flags,
					  struct sockaddr __user *addr, int addr_len);
	extern asmlinkage long sys_recv(int fd, void __user * ubuf, size_t size, unsigned flags);
	extern asmlinkage long sys_recvfrom(int fd, void __user * ubuf, size_t size, unsigned flags,
					    struct sockaddr __user *addr, int __user *addr_len);
	extern asmlinkage long sys_shutdown(int fd, int how);
	extern asmlinkage long sys_setsockopt(int fd, int level, int optname, char __user *optval, int optlen);
	extern asmlinkage long sys_getsockopt(int fd, int level, int optname, char __user *optval, int *optlen);
	extern asmlinkage long sys_sendmsg(int fd, struct msghdr __user *msg, unsigned flags);
	extern asmlinkage long sys_recvmsg(int fd, struct msghdr __user *msg, unsigned int flags);


	if(call<1||call>SYS_RECVMSG)
		return -EINVAL;

	/* copy_from_user should be SMP safe. */
	if (copy_from_user(a, args32, socketcall_nargs[call]))
		return -EFAULT;

	a0=a[0];
	a1=a[1];

	switch(call)
	{
		case SYS_SOCKET:
			err = sys_socket(a0,a1,a[2]);
			break;
		case SYS_BIND:
			err = sys_bind(a0,(struct sockaddr __user *)A(a1), a[2]);
			break;
		case SYS_CONNECT:
			err = sys_connect(a0, (struct sockaddr __user *)A(a1), a[2]);
			break;
		case SYS_LISTEN:
			err = sys_listen(a0,a1);
			break;
		case SYS_ACCEPT:
			err = sys_accept(a0,(struct sockaddr __user *)A(a1), (int __user *)A(a[2]));
			break;
		case SYS_GETSOCKNAME:
			err = sys_getsockname(a0,(struct sockaddr __user *)A(a1), (int __user *)A(a[2]));
			break;
		case SYS_GETPEERNAME:
			err = sys_getpeername(a0, (struct sockaddr __user *)A(a1), (int __user *)A(a[2]));
			break;
		case SYS_SOCKETPAIR:
			err = sys_socketpair(a0,a1, a[2], (int __user *)A(a[3]));
			break;
		case SYS_SEND:
			err = sys_send(a0, (void __user *)A(a1), a[2], a[3]);
			break;
		case SYS_SENDTO:
			err = sys_sendto(a0,(void __user *)A(a1), a[2], a[3],
					 (struct sockaddr __user *)A(a[4]), a[5]);
			break;
		case SYS_RECV:
			err = sys_recv(a0, (void __user *)A(a1), a[2], a[3]);
			break;
		case SYS_RECVFROM:
			err = sys_recvfrom(a0, (void __user *)A(a1), a[2], a[3],
					   (struct sockaddr __user *)A(a[4]), (int __user *)A(a[5]));
			break;
		case SYS_SHUTDOWN:
			err = sys_shutdown(a0,a1);
			break;
		case SYS_SETSOCKOPT:
			err = sys_setsockopt(a0, a1, a[2], (char __user *)A(a[3]), a[4]);
			break;
		case SYS_GETSOCKOPT:
			err = sys_getsockopt(a0, a1, a[2], (char __user *)A(a[3]), (int __user *)A(a[4]));
			break;
		case SYS_SENDMSG:
			err = sys_sendmsg(a0, (struct msghdr __user *) A(a1), a[2]);
			break;
		case SYS_RECVMSG:
			err = sys_recvmsg(a0, (struct msghdr __user *) A(a1), a[2]);
			break;
		default:
			err = -EINVAL;
			break;
	}
	return err;
}

struct sigevent32 {
	u32 sigev_value;
	u32 sigev_signo;
	u32 sigev_notify;
	u32 payload[(64 / 4) - 3];
};

extern asmlinkage long
sys_timer_create(clockid_t which_clock,
		 struct sigevent __user *timer_event_spec,
		 timer_t __user * created_timer_id);

long
sys32_timer_create(u32 clock, struct sigevent32 __user *se32, timer_t __user *timer_id)
{
	struct sigevent __user *p = NULL;
	if (se32) {
		struct sigevent se;
		p = compat_alloc_user_space(sizeof(struct sigevent));
		memset(&se, 0, sizeof(struct sigevent));
		if (get_user(se.sigev_value.sival_int,  &se32->sigev_value) ||
		    __get_user(se.sigev_signo, &se32->sigev_signo) ||
		    __get_user(se.sigev_notify, &se32->sigev_notify) ||
		    __copy_from_user(&se._sigev_un._pad, &se32->payload,
				     sizeof(se32->payload)) ||
		    copy_to_user(p, &se, sizeof(se)))
			return -EFAULT;
	}
	return sys_timer_create(clock, p, timer_id);
}

asmlinkage long
sysn32_rt_sigtimedwait(const sigset_t __user *uthese,
		       siginfo_t __user *uinfo,
		       const struct compat_timespec __user *uts32,
		       size_t sigsetsize)
{
	struct timespec __user *uts = NULL;

	if (uts32) {
		struct timespec ts;
		uts = compat_alloc_user_space(sizeof(struct timespec));
		if (get_user(ts.tv_sec, &uts32->tv_sec) ||
		    get_user(ts.tv_nsec, &uts32->tv_nsec) ||
		    copy_to_user (uts, &ts, sizeof (ts)))
			return -EFAULT;
	}
	return sys_rt_sigtimedwait(uthese, uinfo, uts, sigsetsize);
}

save_static_function(sys32_clone);
__attribute_used__ noinline static int
_sys32_clone(nabi_no_regargs struct pt_regs regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int __user *parent_tidptr, *child_tidptr;

	clone_flags = regs.regs[4];
	newsp = regs.regs[5];
	if (!newsp)
		newsp = regs.regs[29];
	parent_tidptr = (int *) regs.regs[6];

	/* Use __dummy4 instead of getting it off the stack, so that
	   syscall() works.  */
	child_tidptr = (int __user *) __dummy4;
	return do_fork(clone_flags, newsp, &regs, 0,
	               parent_tidptr, child_tidptr);
}

extern asmlinkage void sys_set_thread_area(u32 addr);
asmlinkage void sys32_set_thread_area(u32 addr)
{
	sys_set_thread_area(AA(addr));
}
