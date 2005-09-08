/* $Id: sys_sparc32.c,v 1.184 2002/02/09 19:49:31 davem Exp $
 * sys_sparc32.c: Conversion between 32bit and 64bit native syscalls.
 *
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
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
#include <linux/dnotify.h>
#include <linux/security.h>
#include <linux/compat.h>
#include <linux/vfs.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/ptrace.h>
#include <linux/highuid.h>

#include <asm/types.h>
#include <asm/ipc.h>
#include <asm/uaccess.h>
#include <asm/fpumacro.h>
#include <asm/semaphore.h>
#include <asm/mmu_context.h>

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

	if (!(retval = put_user(high2lowuid(current->uid), ruid)) &&
	    !(retval = put_user(high2lowuid(current->euid), euid)))
		retval = put_user(high2lowuid(current->suid), suid);

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

	if (!(retval = put_user(high2lowgid(current->gid), rgid)) &&
	    !(retval = put_user(high2lowgid(current->egid), egid)))
		retval = put_user(high2lowgid(current->sgid), sgid);

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

	get_group_info(current->group_info);
	i = current->group_info->ngroups;
	if (gidsetsize) {
		if (i > gidsetsize) {
			i = -EINVAL;
			goto out;
		}
		if (groups16_to_user(grouplist, current->group_info)) {
			i = -EFAULT;
			goto out;
		}
	}
out:
	put_group_info(current->group_info);
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
	return high2lowuid(current->uid);
}

asmlinkage long sys32_geteuid16(void)
{
	return high2lowuid(current->euid);
}

asmlinkage long sys32_getgid16(void)
{
	return high2lowgid(current->gid);
}

asmlinkage long sys32_getegid16(void)
{
	return high2lowgid(current->egid);
}

/* 32-bit timeval and related flotsam.  */

static long get_tv32(struct timeval *o, struct compat_timeval __user *i)
{
	return (!access_ok(VERIFY_READ, i, sizeof(*i)) ||
		(__get_user(o->tv_sec, &i->tv_sec) |
		 __get_user(o->tv_usec, &i->tv_usec)));
}

static inline long put_tv32(struct compat_timeval __user *o, struct timeval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->tv_sec, &o->tv_sec) |
		 __put_user(i->tv_usec, &o->tv_usec)));
}

#ifdef CONFIG_SYSVIPC                                                        
asmlinkage long compat_sys_ipc(u32 call, u32 first, u32 second, u32 third, compat_uptr_t ptr, u32 fifth)
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
	};

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

int cp_compat_stat(struct kstat *stat, struct compat_stat __user *statbuf)
{
	int err;

	if (stat->size > MAX_NON_LFS || !old_valid_dev(stat->dev) ||
	    !old_valid_dev(stat->rdev))
		return -EOVERFLOW;

	err  = put_user(old_encode_dev(stat->dev), &statbuf->st_dev);
	err |= put_user(stat->ino, &statbuf->st_ino);
	err |= put_user(stat->mode, &statbuf->st_mode);
	err |= put_user(stat->nlink, &statbuf->st_nlink);
	err |= put_user(high2lowuid(stat->uid), &statbuf->st_uid);
	err |= put_user(high2lowgid(stat->gid), &statbuf->st_gid);
	err |= put_user(old_encode_dev(stat->rdev), &statbuf->st_rdev);
	err |= put_user(stat->size, &statbuf->st_size);
	err |= put_user(stat->atime.tv_sec, &statbuf->st_atime);
	err |= put_user(stat->atime.tv_nsec, &statbuf->st_atime_nsec);
	err |= put_user(stat->mtime.tv_sec, &statbuf->st_mtime);
	err |= put_user(stat->mtime.tv_nsec, &statbuf->st_mtime_nsec);
	err |= put_user(stat->ctime.tv_sec, &statbuf->st_ctime);
	err |= put_user(stat->ctime.tv_nsec, &statbuf->st_ctime_nsec);
	err |= put_user(stat->blksize, &statbuf->st_blksize);
	err |= put_user(stat->blocks, &statbuf->st_blocks);
	err |= put_user(0, &statbuf->__unused4[0]);
	err |= put_user(0, &statbuf->__unused4[1]);

	return err;
}

int cp_compat_stat64(struct kstat *stat, struct compat_stat64 __user *statbuf)
{
	int err;

	err  = put_user(huge_encode_dev(stat->dev), &statbuf->st_dev);
	err |= put_user(stat->ino, &statbuf->st_ino);
	err |= put_user(stat->mode, &statbuf->st_mode);
	err |= put_user(stat->nlink, &statbuf->st_nlink);
	err |= put_user(stat->uid, &statbuf->st_uid);
	err |= put_user(stat->gid, &statbuf->st_gid);
	err |= put_user(huge_encode_dev(stat->rdev), &statbuf->st_rdev);
	err |= put_user(0, (unsigned long __user *) &statbuf->__pad3[0]);
	err |= put_user(stat->size, &statbuf->st_size);
	err |= put_user(stat->blksize, &statbuf->st_blksize);
	err |= put_user(0, (unsigned int __user *) &statbuf->__pad4[0]);
	err |= put_user(0, (unsigned int __user *) &statbuf->__pad4[4]);
	err |= put_user(stat->blocks, &statbuf->st_blocks);
	err |= put_user(stat->atime.tv_sec, &statbuf->st_atime);
	err |= put_user(stat->atime.tv_nsec, &statbuf->st_atime_nsec);
	err |= put_user(stat->mtime.tv_sec, &statbuf->st_mtime);
	err |= put_user(stat->mtime.tv_nsec, &statbuf->st_mtime_nsec);
	err |= put_user(stat->ctime.tv_sec, &statbuf->st_ctime);
	err |= put_user(stat->ctime.tv_nsec, &statbuf->st_ctime_nsec);
	err |= put_user(0, &statbuf->__unused4);
	err |= put_user(0, &statbuf->__unused5);

	return err;
}

asmlinkage long compat_sys_stat64(char __user * filename,
		struct compat_stat64 __user *statbuf)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);

	if (!error)
		error = cp_compat_stat64(&stat, statbuf);
	return error;
}

asmlinkage long compat_sys_lstat64(char __user * filename,
		struct compat_stat64 __user *statbuf)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);

	if (!error)
		error = cp_compat_stat64(&stat, statbuf);
	return error;
}

asmlinkage long compat_sys_fstat64(unsigned int fd,
		struct compat_stat64 __user * statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_compat_stat64(&stat, statbuf);
	return error;
}

asmlinkage long compat_sys_sysfs(int option, u32 arg1, u32 arg2)
{
	return sys_sysfs(option, arg1, arg2);
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

asmlinkage long sys32_sysinfo(struct sysinfo32 __user *info)
{
	struct sysinfo s;
	int ret, err;
	int bitcount = 0;
	mm_segment_t old_fs = get_fs ();
	
	set_fs(KERNEL_DS);
	ret = sys_sysinfo((struct sysinfo __user *) &s);
	set_fs(old_fs);
	/* Check to see if any memory value is too large for 32-bit and
         * scale down if needed.
         */
	if ((s.totalram >> 32) || (s.totalswap >> 32)) {
		while (s.mem_unit < PAGE_SIZE) {
			s.mem_unit <<= 1;
			bitcount++;
		}
		s.totalram >>= bitcount;
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

asmlinkage long compat_sys_sched_rr_get_interval(compat_pid_t pid, struct compat_timespec __user *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid, (struct timespec __user *) &t);
	set_fs (old_fs);
	if (put_compat_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

asmlinkage long compat_sys_rt_sigprocmask(int how,
					  compat_sigset_t __user *set,
					  compat_sigset_t __user *oset,
					  compat_size_t sigsetsize)
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
				 set ? (sigset_t __user *) &s : NULL,
				 oset ? (sigset_t __user *) &s : NULL,
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
				    compat_size_t sigsetsize)
{
	sigset_t s;
	compat_sigset_t s32;
	int ret;
	mm_segment_t old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_rt_sigpending((sigset_t __user *) &s, sigsetsize);
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

asmlinkage long compat_sys_rt_sigqueueinfo(int pid, int sig,
					   struct compat_siginfo __user *uinfo)
{
	siginfo_t info;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (copy_siginfo_from_user32(&info, uinfo))
		return -EFAULT;

	set_fs (KERNEL_DS);
	ret = sys_rt_sigqueueinfo(pid, sig, (siginfo_t __user *) &info);
	set_fs (old_fs);
	return ret;
}

asmlinkage long compat_sys_sigaction(int sig, struct old_sigaction32 __user *act,
				     struct old_sigaction32 __user *oact)
{
        struct k_sigaction new_ka, old_ka;
        int ret;

	if (sig < 0) {
		set_thread_flag(TIF_NEWSIGNALS);
		sig = -sig;
	}

        if (act) {
		compat_old_sigset_t mask;
		u32 u_handler, u_restorer;
		
		ret = get_user(u_handler, &act->sa_handler);
		new_ka.sa.sa_handler =  compat_ptr(u_handler);
		ret |= __get_user(u_restorer, &act->sa_restorer);
		new_ka.sa.sa_restorer = compat_ptr(u_restorer);
		ret |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		ret |= __get_user(mask, &act->sa_mask);
		if (ret)
			return ret;
		new_ka.ka_restorer = NULL;
		siginitset(&new_ka.sa.sa_mask, mask);
        }

        ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		ret = put_user(ptr_to_compat(old_ka.sa.sa_handler), &oact->sa_handler);
		ret |= __put_user(ptr_to_compat(old_ka.sa.sa_restorer), &oact->sa_restorer);
		ret |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		ret |= __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
        }

	return ret;
}

asmlinkage long compat_sys_rt_sigaction(int sig,
					struct sigaction32 __user *act,
					struct sigaction32 __user *oact,
					void __user *restorer,
					compat_size_t sigsetsize)
{
        struct k_sigaction new_ka, old_ka;
        int ret;
	compat_sigset_t set32;

        /* XXX: Don't preclude handling different sized sigset_t's.  */
        if (sigsetsize != sizeof(compat_sigset_t))
                return -EINVAL;

	/* All tasks which use RT signals (effectively) use
	 * new style signals.
	 */
	set_thread_flag(TIF_NEWSIGNALS);

        if (act) {
		u32 u_handler, u_restorer;

		new_ka.ka_restorer = restorer;
		ret = get_user(u_handler, &act->sa_handler);
		new_ka.sa.sa_handler =  compat_ptr(u_handler);
		ret |= __copy_from_user(&set32, &act->sa_mask, sizeof(compat_sigset_t));
		switch (_NSIG_WORDS) {
		case 4: new_ka.sa.sa_mask.sig[3] = set32.sig[6] | (((long)set32.sig[7]) << 32);
		case 3: new_ka.sa.sa_mask.sig[2] = set32.sig[4] | (((long)set32.sig[5]) << 32);
		case 2: new_ka.sa.sa_mask.sig[1] = set32.sig[2] | (((long)set32.sig[3]) << 32);
		case 1: new_ka.sa.sa_mask.sig[0] = set32.sig[0] | (((long)set32.sig[1]) << 32);
		}
		ret |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		ret |= __get_user(u_restorer, &act->sa_restorer);
		new_ka.sa.sa_restorer = compat_ptr(u_restorer);
                if (ret)
                	return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		switch (_NSIG_WORDS) {
		case 4: set32.sig[7] = (old_ka.sa.sa_mask.sig[3] >> 32); set32.sig[6] = old_ka.sa.sa_mask.sig[3];
		case 3: set32.sig[5] = (old_ka.sa.sa_mask.sig[2] >> 32); set32.sig[4] = old_ka.sa.sa_mask.sig[2];
		case 2: set32.sig[3] = (old_ka.sa.sa_mask.sig[1] >> 32); set32.sig[2] = old_ka.sa.sa_mask.sig[1];
		case 1: set32.sig[1] = (old_ka.sa.sa_mask.sig[0] >> 32); set32.sig[0] = old_ka.sa.sa_mask.sig[0];
		}
		ret = put_user(ptr_to_compat(old_ka.sa.sa_handler), &oact->sa_handler);
		ret |= __copy_to_user(&oact->sa_mask, &set32, sizeof(compat_sigset_t));
		ret |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		ret |= __put_user(ptr_to_compat(old_ka.sa.sa_restorer), &oact->sa_restorer);
		if (ret)
			ret = -EFAULT;
        }

        return ret;
}

/*
 * sparc32_execve() executes a new program after the asm stub has set
 * things up for us.  This should basically do what I want it to.
 */
asmlinkage long sparc32_execve(struct pt_regs *regs)
{
	int error, base = 0;
	char *filename;

	/* User register window flush is done by entry.S */

	/* Check for indirect call. */
	if ((u32)regs->u_regs[UREG_G1] == 0)
		base = 1;

	filename = getname(compat_ptr(regs->u_regs[base + UREG_I0]));
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;

	error = compat_do_execve(filename,
				 compat_ptr(regs->u_regs[base + UREG_I1]),
				 compat_ptr(regs->u_regs[base + UREG_I2]), regs);

	putname(filename);

	if (!error) {
		fprs_write(0);
		current_thread_info()->xfsr[0] = 0;
		current_thread_info()->fpsaved[0] = 0;
		regs->tstate &= ~TSTATE_PEF;
		task_lock(current);
		current->ptrace &= ~PT_DTRACE;
		task_unlock(current);
	}
out:
	return error;
}

#ifdef CONFIG_MODULES

asmlinkage long sys32_init_module(void __user *umod, u32 len,
				  const char __user *uargs)
{
	return sys_init_module(umod, len, uargs);
}

asmlinkage long sys32_delete_module(const char __user *name_user,
				    unsigned int flags)
{
	return sys_delete_module(name_user, flags);
}

#else /* CONFIG_MODULES */

asmlinkage long sys32_init_module(const char __user *name_user,
				  struct module __user *mod_user)
{
	return -ENOSYS;
}

asmlinkage long sys32_delete_module(const char __user *name_user)
{
	return -ENOSYS;
}

#endif  /* CONFIG_MODULES */

/* Translations due to time_t size differences.  Which affects all
   sorts of things, like timeval and itimerval.  */

extern struct timezone sys_tz;

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

asmlinkage long sys32_settimeofday(struct compat_timeval __user *tv,
				   struct timezone __user *tz)
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

asmlinkage long sys32_utimes(char __user *filename,
			     struct compat_timeval __user *tvs)
{
	struct timeval ktvs[2];

	if (tvs) {
		if (get_tv32(&ktvs[0], tvs) ||
		    get_tv32(&ktvs[1], 1+tvs))
			return -EFAULT;
	}

	return do_utimes(filename, (tvs ? &ktvs[0] : NULL));
}

/* These are here just in case some old sparc32 binary calls it. */
asmlinkage long sys32_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

asmlinkage compat_ssize_t sys32_pread64(unsigned int fd,
					char __user *ubuf,
					compat_size_t count,
					unsigned long poshi,
					unsigned long poslo)
{
	return sys_pread64(fd, ubuf, count, (poshi << 32) | poslo);
}

asmlinkage compat_ssize_t sys32_pwrite64(unsigned int fd,
					 char __user *ubuf,
					 compat_size_t count,
					 unsigned long poshi,
					 unsigned long poslo)
{
	return sys_pwrite64(fd, ubuf, count, (poshi << 32) | poslo);
}

asmlinkage long compat_sys_readahead(int fd,
				     unsigned long offhi,
				     unsigned long offlo,
				     compat_size_t count)
{
	return sys_readahead(fd, (offhi << 32) | offlo, count);
}

long compat_sys_fadvise64(int fd,
			  unsigned long offhi,
			  unsigned long offlo,
			  compat_size_t len, int advice)
{
	return sys_fadvise64_64(fd, (offhi << 32) | offlo, len, advice);
}

long compat_sys_fadvise64_64(int fd,
			     unsigned long offhi, unsigned long offlo,
			     unsigned long lenhi, unsigned long lenlo,
			     int advice)
{
	return sys_fadvise64_64(fd,
				(offhi << 32) | offlo,
				(lenhi << 32) | lenlo,
				advice);
}

asmlinkage long compat_sys_sendfile(int out_fd, int in_fd,
				    compat_off_t __user *offset,
				    compat_size_t count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	off_t of;
	
	if (offset && get_user(of, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile(out_fd, in_fd,
			   offset ? (off_t __user *) &of : NULL,
			   count);
	set_fs(old_fs);
	
	if (offset && put_user(of, offset))
		return -EFAULT;
		
	return ret;
}

asmlinkage long compat_sys_sendfile64(int out_fd, int in_fd,
				      compat_loff_t __user *offset,
				      compat_size_t count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	loff_t lof;
	
	if (offset && get_user(lof, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile64(out_fd, in_fd,
			     offset ? (loff_t __user *) &lof : NULL,
			     count);
	set_fs(old_fs);
	
	if (offset && put_user(lof, offset))
		return -EFAULT;
		
	return ret;
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

asmlinkage long sys32_adjtimex(struct timex32 __user *utp)
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

/* This is just a version for 32-bit applications which does
 * not force O_LARGEFILE on.
 */

asmlinkage long sparc32_open(const char __user *filename,
			     int flags, int mode)
{
	return do_sys_open(filename, flags, mode);
}

extern unsigned long do_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr);
                
asmlinkage unsigned long sys32_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, u32 __new_addr)
{
	struct vm_area_struct *vma;
	unsigned long ret = -EINVAL;
	unsigned long new_addr = __new_addr;

	if (old_len > 0xf0000000UL || new_len > 0xf0000000UL)
		goto out;
	if (addr > 0xf0000000UL - old_len)
		goto out;
	down_write(&current->mm->mmap_sem);
	if (flags & MREMAP_FIXED) {
		if (new_addr > 0xf0000000UL - new_len)
			goto out_sem;
	} else if (addr > 0xf0000000UL - new_len) {
		unsigned long map_flags = 0;
		struct file *file = NULL;

		ret = -ENOMEM;
		if (!(flags & MREMAP_MAYMOVE))
			goto out_sem;

		vma = find_vma(current->mm, addr);
		if (vma) {
			if (vma->vm_flags & VM_SHARED)
				map_flags |= MAP_SHARED;
			file = vma->vm_file;
		}

		/* MREMAP_FIXED checked above. */
		new_addr = get_unmapped_area(file, addr, new_len,
				    vma ? vma->vm_pgoff : 0,
				    map_flags);
		ret = new_addr;
		if (new_addr & ~PAGE_MASK)
			goto out_sem;
		flags |= MREMAP_FIXED;
	}
	ret = do_mremap(addr, old_len, new_len, flags, new_addr);
out_sem:
	up_write(&current->mm->mmap_sem);
out:
	return ret;       
}

struct __sysctl_args32 {
	u32 name;
	int nlen;
	u32 oldval;
	u32 oldlenp;
	u32 newval;
	u32 newlen;
	u32 __unused[4];
};

asmlinkage long sys32_sysctl(struct __sysctl_args32 __user *args)
{
#ifndef CONFIG_SYSCTL
	return -ENOSYS;
#else
	struct __sysctl_args32 tmp;
	int error;
	size_t oldlen, __user *oldlenp = NULL;
	unsigned long addr = (((unsigned long)&args->__unused[0]) + 7UL) & ~7UL;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	if (tmp.oldval && tmp.oldlenp) {
		/* Duh, this is ugly and might not work if sysctl_args
		   is in read-only memory, but do_sysctl does indirectly
		   a lot of uaccess in both directions and we'd have to
		   basically copy the whole sysctl.c here, and
		   glibc's __sysctl uses rw memory for the structure
		   anyway.  */
		if (get_user(oldlen, (u32 __user *)(unsigned long)tmp.oldlenp) ||
		    put_user(oldlen, (size_t __user *)addr))
			return -EFAULT;
		oldlenp = (size_t __user *)addr;
	}

	lock_kernel();
	error = do_sysctl((int __user *)(unsigned long) tmp.name,
			  tmp.nlen,
			  (void __user *)(unsigned long) tmp.oldval,
			  oldlenp,
			  (void __user *)(unsigned long) tmp.newval,
			  tmp.newlen);
	unlock_kernel();
	if (oldlenp) {
		if (!error) {
			if (get_user(oldlen, (size_t __user *)addr) ||
			    put_user(oldlen, (u32 __user *)(unsigned long) tmp.oldlenp))
				error = -EFAULT;
		}
		if (copy_to_user(args->__unused, tmp.__unused, sizeof(tmp.__unused)))
			error = -EFAULT;
	}
	return error;
#endif
}

long sys32_lookup_dcookie(unsigned long cookie_high,
			  unsigned long cookie_low,
			  char __user *buf, size_t len)
{
	return sys_lookup_dcookie((cookie_high << 32) | cookie_low,
				  buf, len);
}

extern asmlinkage long
sys_timer_create(clockid_t which_clock,
		 struct sigevent __user *timer_event_spec,
		 timer_t __user *created_timer_id);

long
sys32_timer_create(u32 clock, struct compat_sigevent __user *se32,
		   timer_t __user *timer_id)
{
	struct sigevent se;
	mm_segment_t oldfs;
	timer_t t;
	long err;

	if (se32 == NULL)
		return sys_timer_create(clock, NULL, timer_id);

	if (get_compat_sigevent(&se, se32))
		return -EFAULT;

	if (!access_ok(VERIFY_WRITE,timer_id,sizeof(timer_t)))
		return -EFAULT;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_timer_create(clock,
			       (struct sigevent __user *) &se,
			       (timer_t __user *) &t);
	set_fs(oldfs);

	if (!err)
		err = __put_user (t, timer_id);

	return err;
}
