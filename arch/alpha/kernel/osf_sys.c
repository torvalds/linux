// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/alpha/kernel/osf_sys.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles some of the stranger OSF/1 system call interfaces.
 * Some of the system calls expect a non-C calling standard, others have
 * special parameter blocks..
 */

#include <linux/errno.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/cputime.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/utsname.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/shm.h>
#include <linux/poll.h>
#include <linux/file.h>
#include <linux/types.h>
#include <linux/ipc.h>
#include <linux/namei.h>
#include <linux/uio.h>
#include <linux/vfs.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>

#include <asm/fpu.h>
#include <asm/io.h>
#include <linux/uaccess.h>
#include <asm/sysinfo.h>
#include <asm/thread_info.h>
#include <asm/hwrpb.h>
#include <asm/processor.h>

/*
 * Brk needs to return an error.  Still support Linux's brk(0) query idiom,
 * which OSF programs just shouldn't be doing.  We're still not quite
 * identical to OSF as we don't return 0 on success, but doing otherwise
 * would require changes to libc.  Hopefully this is good enough.
 */
SYSCALL_DEFINE1(osf_brk, unsigned long, brk)
{
	unsigned long retval = sys_brk(brk);
	if (brk && brk != retval)
		retval = -ENOMEM;
	return retval;
}
 
/*
 * This is pure guess-work..
 */
SYSCALL_DEFINE4(osf_set_program_attributes, unsigned long, text_start,
		unsigned long, text_len, unsigned long, bss_start,
		unsigned long, bss_len)
{
	struct mm_struct *mm;

	mm = current->mm;
	mm->end_code = bss_start + bss_len;
	mm->start_brk = bss_start + bss_len;
	mm->brk = bss_start + bss_len;
#if 0
	printk("set_program_attributes(%lx %lx %lx %lx)\n",
		text_start, text_len, bss_start, bss_len);
#endif
	return 0;
}

/*
 * OSF/1 directory handling functions...
 *
 * The "getdents()" interface is much more sane: the "basep" stuff is
 * braindamage (it can't really handle filesystems where the directory
 * offset differences aren't the same as "d_reclen").
 */
#define NAME_OFFSET	offsetof (struct osf_dirent, d_name)

struct osf_dirent {
	unsigned int d_ino;
	unsigned short d_reclen;
	unsigned short d_namlen;
	char d_name[1];
};

struct osf_dirent_callback {
	struct dir_context ctx;
	struct osf_dirent __user *dirent;
	long __user *basep;
	unsigned int count;
	int error;
};

static int
osf_filldir(struct dir_context *ctx, const char *name, int namlen,
	    loff_t offset, u64 ino, unsigned int d_type)
{
	struct osf_dirent __user *dirent;
	struct osf_dirent_callback *buf =
		container_of(ctx, struct osf_dirent_callback, ctx);
	unsigned int reclen = ALIGN(NAME_OFFSET + namlen + 1, sizeof(u32));
	unsigned int d_ino;

	buf->error = -EINVAL;	/* only used if we fail */
	if (reclen > buf->count)
		return -EINVAL;
	d_ino = ino;
	if (sizeof(d_ino) < sizeof(ino) && d_ino != ino) {
		buf->error = -EOVERFLOW;
		return -EOVERFLOW;
	}
	if (buf->basep) {
		if (put_user(offset, buf->basep))
			goto Efault;
		buf->basep = NULL;
	}
	dirent = buf->dirent;
	if (put_user(d_ino, &dirent->d_ino) ||
	    put_user(namlen, &dirent->d_namlen) ||
	    put_user(reclen, &dirent->d_reclen) ||
	    copy_to_user(dirent->d_name, name, namlen) ||
	    put_user(0, dirent->d_name + namlen))
		goto Efault;
	dirent = (void __user *)dirent + reclen;
	buf->dirent = dirent;
	buf->count -= reclen;
	return 0;
Efault:
	buf->error = -EFAULT;
	return -EFAULT;
}

SYSCALL_DEFINE4(osf_getdirentries, unsigned int, fd,
		struct osf_dirent __user *, dirent, unsigned int, count,
		long __user *, basep)
{
	int error;
	struct fd arg = fdget_pos(fd);
	struct osf_dirent_callback buf = {
		.ctx.actor = osf_filldir,
		.dirent = dirent,
		.basep = basep,
		.count = count
	};

	if (!arg.file)
		return -EBADF;

	error = iterate_dir(arg.file, &buf.ctx);
	if (error >= 0)
		error = buf.error;
	if (count != buf.count)
		error = count - buf.count;

	fdput_pos(arg);
	return error;
}

#undef NAME_OFFSET

SYSCALL_DEFINE6(osf_mmap, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags, unsigned long, fd,
		unsigned long, off)
{
	unsigned long ret = -EINVAL;

#if 0
	if (flags & (_MAP_HASSEMAPHORE | _MAP_INHERIT | _MAP_UNALIGNED))
		printk("%s: unimplemented OSF mmap flags %04lx\n", 
			current->comm, flags);
#endif
	if ((off + PAGE_ALIGN(len)) < off)
		goto out;
	if (off & ~PAGE_MASK)
		goto out;
	ret = sys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
 out:
	return ret;
}

struct osf_stat {
	int		st_dev;
	int		st_pad1;
	unsigned	st_mode;
	unsigned short	st_nlink;
	short		st_nlink_reserved;
	unsigned	st_uid;
	unsigned	st_gid;
	int		st_rdev;
	int		st_ldev;
	long		st_size;
	int		st_pad2;
	int		st_uatime;
	int		st_pad3;
	int		st_umtime;
	int		st_pad4;
	int		st_uctime;
	int		st_pad5;
	int		st_pad6;
	unsigned	st_flags;
	unsigned	st_gen;
	long		st_spare[4];
	unsigned	st_ino;
	int		st_ino_reserved;
	int		st_atime;
	int		st_atime_reserved;
	int		st_mtime;
	int		st_mtime_reserved;
	int		st_ctime;
	int		st_ctime_reserved;
	long		st_blksize;
	long		st_blocks;
};

/*
 * The OSF/1 statfs structure is much larger, but this should
 * match the beginning, at least.
 */
struct osf_statfs {
	short f_type;
	short f_flags;
	int f_fsize;
	int f_bsize;
	int f_blocks;
	int f_bfree;
	int f_bavail;
	int f_files;
	int f_ffree;
	__kernel_fsid_t f_fsid;
};

struct osf_statfs64 {
	short f_type;
	short f_flags;
	int f_pad1;
	int f_pad2;
	int f_pad3;
	int f_pad4;
	int f_pad5;
	int f_pad6;
	int f_pad7;
	__kernel_fsid_t f_fsid;
	u_short f_namemax;
	short f_reserved1;
	int f_spare[8];
	char f_pad8[90];
	char f_pad9[90];
	long mount_info[10];
	u_long f_flags2;
	long f_spare2[14];
	long f_fsize;
	long f_bsize;
	long f_blocks;
	long f_bfree;
	long f_bavail;
	long f_files;
	long f_ffree;
};

static int
linux_to_osf_stat(struct kstat *lstat, struct osf_stat __user *osf_stat)
{
	struct osf_stat tmp = { 0 };

	tmp.st_dev	= lstat->dev;
	tmp.st_mode	= lstat->mode;
	tmp.st_nlink	= lstat->nlink;
	tmp.st_uid	= from_kuid_munged(current_user_ns(), lstat->uid);
	tmp.st_gid	= from_kgid_munged(current_user_ns(), lstat->gid);
	tmp.st_rdev	= lstat->rdev;
	tmp.st_ldev	= lstat->rdev;
	tmp.st_size	= lstat->size;
	tmp.st_uatime	= lstat->atime.tv_nsec / 1000;
	tmp.st_umtime	= lstat->mtime.tv_nsec / 1000;
	tmp.st_uctime	= lstat->ctime.tv_nsec / 1000;
	tmp.st_ino	= lstat->ino;
	tmp.st_atime	= lstat->atime.tv_sec;
	tmp.st_mtime	= lstat->mtime.tv_sec;
	tmp.st_ctime	= lstat->ctime.tv_sec;
	tmp.st_blksize	= lstat->blksize;
	tmp.st_blocks	= lstat->blocks;

	return copy_to_user(osf_stat, &tmp, sizeof(tmp)) ? -EFAULT : 0;
}

static int
linux_to_osf_statfs(struct kstatfs *linux_stat, struct osf_statfs __user *osf_stat,
		    unsigned long bufsiz)
{
	struct osf_statfs tmp_stat;

	tmp_stat.f_type = linux_stat->f_type;
	tmp_stat.f_flags = 0;	/* mount flags */
	tmp_stat.f_fsize = linux_stat->f_frsize;
	tmp_stat.f_bsize = linux_stat->f_bsize;
	tmp_stat.f_blocks = linux_stat->f_blocks;
	tmp_stat.f_bfree = linux_stat->f_bfree;
	tmp_stat.f_bavail = linux_stat->f_bavail;
	tmp_stat.f_files = linux_stat->f_files;
	tmp_stat.f_ffree = linux_stat->f_ffree;
	tmp_stat.f_fsid = linux_stat->f_fsid;
	if (bufsiz > sizeof(tmp_stat))
		bufsiz = sizeof(tmp_stat);
	return copy_to_user(osf_stat, &tmp_stat, bufsiz) ? -EFAULT : 0;
}

static int
linux_to_osf_statfs64(struct kstatfs *linux_stat, struct osf_statfs64 __user *osf_stat,
		      unsigned long bufsiz)
{
	struct osf_statfs64 tmp_stat = { 0 };

	tmp_stat.f_type = linux_stat->f_type;
	tmp_stat.f_fsize = linux_stat->f_frsize;
	tmp_stat.f_bsize = linux_stat->f_bsize;
	tmp_stat.f_blocks = linux_stat->f_blocks;
	tmp_stat.f_bfree = linux_stat->f_bfree;
	tmp_stat.f_bavail = linux_stat->f_bavail;
	tmp_stat.f_files = linux_stat->f_files;
	tmp_stat.f_ffree = linux_stat->f_ffree;
	tmp_stat.f_fsid = linux_stat->f_fsid;
	if (bufsiz > sizeof(tmp_stat))
		bufsiz = sizeof(tmp_stat);
	return copy_to_user(osf_stat, &tmp_stat, bufsiz) ? -EFAULT : 0;
}

SYSCALL_DEFINE3(osf_statfs, const char __user *, pathname,
		struct osf_statfs __user *, buffer, unsigned long, bufsiz)
{
	struct kstatfs linux_stat;
	int error = user_statfs(pathname, &linux_stat);
	if (!error)
		error = linux_to_osf_statfs(&linux_stat, buffer, bufsiz);
	return error;	
}

SYSCALL_DEFINE2(osf_stat, char __user *, name, struct osf_stat __user *, buf)
{
	struct kstat stat;
	int error;

	error = vfs_stat(name, &stat);
	if (error)
		return error;

	return linux_to_osf_stat(&stat, buf);
}

SYSCALL_DEFINE2(osf_lstat, char __user *, name, struct osf_stat __user *, buf)
{
	struct kstat stat;
	int error;

	error = vfs_lstat(name, &stat);
	if (error)
		return error;

	return linux_to_osf_stat(&stat, buf);
}

SYSCALL_DEFINE2(osf_fstat, int, fd, struct osf_stat __user *, buf)
{
	struct kstat stat;
	int error;

	error = vfs_fstat(fd, &stat);
	if (error)
		return error;

	return linux_to_osf_stat(&stat, buf);
}

SYSCALL_DEFINE3(osf_fstatfs, unsigned long, fd,
		struct osf_statfs __user *, buffer, unsigned long, bufsiz)
{
	struct kstatfs linux_stat;
	int error = fd_statfs(fd, &linux_stat);
	if (!error)
		error = linux_to_osf_statfs(&linux_stat, buffer, bufsiz);
	return error;
}

SYSCALL_DEFINE3(osf_statfs64, char __user *, pathname,
		struct osf_statfs64 __user *, buffer, unsigned long, bufsiz)
{
	struct kstatfs linux_stat;
	int error = user_statfs(pathname, &linux_stat);
	if (!error)
		error = linux_to_osf_statfs64(&linux_stat, buffer, bufsiz);
	return error;
}

SYSCALL_DEFINE3(osf_fstatfs64, unsigned long, fd,
		struct osf_statfs64 __user *, buffer, unsigned long, bufsiz)
{
	struct kstatfs linux_stat;
	int error = fd_statfs(fd, &linux_stat);
	if (!error)
		error = linux_to_osf_statfs64(&linux_stat, buffer, bufsiz);
	return error;
}

/*
 * Uhh.. OSF/1 mount parameters aren't exactly obvious..
 *
 * Although to be frank, neither are the native Linux/i386 ones..
 */
struct ufs_args {
	char __user *devname;
	int flags;
	uid_t exroot;
};

struct cdfs_args {
	char __user *devname;
	int flags;
	uid_t exroot;

	/* This has lots more here, which Linux handles with the option block
	   but I'm too lazy to do the translation into ASCII.  */
};

struct procfs_args {
	char __user *devname;
	int flags;
	uid_t exroot;
};

/*
 * We can't actually handle ufs yet, so we translate UFS mounts to
 * ext2fs mounts. I wouldn't mind a UFS filesystem, but the UFS
 * layout is so braindead it's a major headache doing it.
 *
 * Just how long ago was it written? OTOH our UFS driver may be still
 * unhappy with OSF UFS. [CHECKME]
 */
static int
osf_ufs_mount(const char __user *dirname,
	      struct ufs_args __user *args, int flags)
{
	int retval;
	struct cdfs_args tmp;
	struct filename *devname;

	retval = -EFAULT;
	if (copy_from_user(&tmp, args, sizeof(tmp)))
		goto out;
	devname = getname(tmp.devname);
	retval = PTR_ERR(devname);
	if (IS_ERR(devname))
		goto out;
	retval = do_mount(devname->name, dirname, "ext2", flags, NULL);
	putname(devname);
 out:
	return retval;
}

static int
osf_cdfs_mount(const char __user *dirname,
	       struct cdfs_args __user *args, int flags)
{
	int retval;
	struct cdfs_args tmp;
	struct filename *devname;

	retval = -EFAULT;
	if (copy_from_user(&tmp, args, sizeof(tmp)))
		goto out;
	devname = getname(tmp.devname);
	retval = PTR_ERR(devname);
	if (IS_ERR(devname))
		goto out;
	retval = do_mount(devname->name, dirname, "iso9660", flags, NULL);
	putname(devname);
 out:
	return retval;
}

static int
osf_procfs_mount(const char __user *dirname,
		 struct procfs_args __user *args, int flags)
{
	struct procfs_args tmp;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	return do_mount("", dirname, "proc", flags, NULL);
}

SYSCALL_DEFINE4(osf_mount, unsigned long, typenr, const char __user *, path,
		int, flag, void __user *, data)
{
	int retval;

	switch (typenr) {
	case 1:
		retval = osf_ufs_mount(path, data, flag);
		break;
	case 6:
		retval = osf_cdfs_mount(path, data, flag);
		break;
	case 9:
		retval = osf_procfs_mount(path, data, flag);
		break;
	default:
		retval = -EINVAL;
		printk("osf_mount(%ld, %x)\n", typenr, flag);
	}

	return retval;
}

SYSCALL_DEFINE1(osf_utsname, char __user *, name)
{
	int error;

	down_read(&uts_sem);
	error = -EFAULT;
	if (copy_to_user(name + 0, utsname()->sysname, 32))
		goto out;
	if (copy_to_user(name + 32, utsname()->nodename, 32))
		goto out;
	if (copy_to_user(name + 64, utsname()->release, 32))
		goto out;
	if (copy_to_user(name + 96, utsname()->version, 32))
		goto out;
	if (copy_to_user(name + 128, utsname()->machine, 32))
		goto out;

	error = 0;
 out:
	up_read(&uts_sem);	
	return error;
}

SYSCALL_DEFINE0(getpagesize)
{
	return PAGE_SIZE;
}

SYSCALL_DEFINE0(getdtablesize)
{
	return sysctl_nr_open;
}

/*
 * For compatibility with OSF/1 only.  Use utsname(2) instead.
 */
SYSCALL_DEFINE2(osf_getdomainname, char __user *, name, int, namelen)
{
	int len, err = 0;
	char *kname;

	if (namelen > 32)
		namelen = 32;

	down_read(&uts_sem);
	kname = utsname()->domainname;
	len = strnlen(kname, namelen);
	if (copy_to_user(name, kname, min(len + 1, namelen)))
		err = -EFAULT;
	up_read(&uts_sem);

	return err;
}

/*
 * The following stuff should move into a header file should it ever
 * be labeled "officially supported."  Right now, there is just enough
 * support to avoid applications (such as tar) printing error
 * messages.  The attributes are not really implemented.
 */

/*
 * Values for Property list entry flag
 */
#define PLE_PROPAGATE_ON_COPY		0x1	/* cp(1) will copy entry
						   by default */
#define PLE_FLAG_MASK			0x1	/* Valid flag values */
#define PLE_FLAG_ALL			-1	/* All flag value */

struct proplistname_args {
	unsigned int pl_mask;
	unsigned int pl_numnames;
	char **pl_names;
};

union pl_args {
	struct setargs {
		char __user *path;
		long follow;
		long nbytes;
		char __user *buf;
	} set;
	struct fsetargs {
		long fd;
		long nbytes;
		char __user *buf;
	} fset;
	struct getargs {
		char __user *path;
		long follow;
		struct proplistname_args __user *name_args;
		long nbytes;
		char __user *buf;
		int __user *min_buf_size;
	} get;
	struct fgetargs {
		long fd;
		struct proplistname_args __user *name_args;
		long nbytes;
		char __user *buf;
		int __user *min_buf_size;
	} fget;
	struct delargs {
		char __user *path;
		long follow;
		struct proplistname_args __user *name_args;
	} del;
	struct fdelargs {
		long fd;
		struct proplistname_args __user *name_args;
	} fdel;
};

enum pl_code {
	PL_SET = 1, PL_FSET = 2,
	PL_GET = 3, PL_FGET = 4,
	PL_DEL = 5, PL_FDEL = 6
};

SYSCALL_DEFINE2(osf_proplist_syscall, enum pl_code, code,
		union pl_args __user *, args)
{
	long error;
	int __user *min_buf_size_ptr;

	switch (code) {
	case PL_SET:
		if (get_user(error, &args->set.nbytes))
			error = -EFAULT;
		break;
	case PL_FSET:
		if (get_user(error, &args->fset.nbytes))
			error = -EFAULT;
		break;
	case PL_GET:
		error = get_user(min_buf_size_ptr, &args->get.min_buf_size);
		if (error)
			break;
		error = put_user(0, min_buf_size_ptr);
		break;
	case PL_FGET:
		error = get_user(min_buf_size_ptr, &args->fget.min_buf_size);
		if (error)
			break;
		error = put_user(0, min_buf_size_ptr);
		break;
	case PL_DEL:
	case PL_FDEL:
		error = 0;
		break;
	default:
		error = -EOPNOTSUPP;
		break;
	};
	return error;
}

SYSCALL_DEFINE2(osf_sigstack, struct sigstack __user *, uss,
		struct sigstack __user *, uoss)
{
	unsigned long usp = rdusp();
	unsigned long oss_sp = current->sas_ss_sp + current->sas_ss_size;
	unsigned long oss_os = on_sig_stack(usp);
	int error;

	if (uss) {
		void __user *ss_sp;

		error = -EFAULT;
		if (get_user(ss_sp, &uss->ss_sp))
			goto out;

		/* If the current stack was set with sigaltstack, don't
		   swap stacks while we are on it.  */
		error = -EPERM;
		if (current->sas_ss_sp && on_sig_stack(usp))
			goto out;

		/* Since we don't know the extent of the stack, and we don't
		   track onstack-ness, but rather calculate it, we must 
		   presume a size.  Ho hum this interface is lossy.  */
		current->sas_ss_sp = (unsigned long)ss_sp - SIGSTKSZ;
		current->sas_ss_size = SIGSTKSZ;
	}

	if (uoss) {
		error = -EFAULT;
		if (put_user(oss_sp, &uoss->ss_sp) ||
		    put_user(oss_os, &uoss->ss_onstack))
			goto out;
	}

	error = 0;
 out:
	return error;
}

SYSCALL_DEFINE3(osf_sysinfo, int, command, char __user *, buf, long, count)
{
	const char *sysinfo_table[] = {
		utsname()->sysname,
		utsname()->nodename,
		utsname()->release,
		utsname()->version,
		utsname()->machine,
		"alpha",	/* instruction set architecture */
		"dummy",	/* hardware serial number */
		"dummy",	/* hardware manufacturer */
		"dummy",	/* secure RPC domain */
	};
	unsigned long offset;
	const char *res;
	long len, err = -EINVAL;

	offset = command-1;
	if (offset >= ARRAY_SIZE(sysinfo_table)) {
		/* Digital UNIX has a few unpublished interfaces here */
		printk("sysinfo(%d)", command);
		goto out;
	}

	down_read(&uts_sem);
	res = sysinfo_table[offset];
	len = strlen(res)+1;
	if ((unsigned long)len > (unsigned long)count)
		len = count;
	if (copy_to_user(buf, res, len))
		err = -EFAULT;
	else
		err = 0;
	up_read(&uts_sem);
 out:
	return err;
}

SYSCALL_DEFINE5(osf_getsysinfo, unsigned long, op, void __user *, buffer,
		unsigned long, nbytes, int __user *, start, void __user *, arg)
{
	unsigned long w;
	struct percpu_struct *cpu;

	switch (op) {
	case GSI_IEEE_FP_CONTROL:
		/* Return current software fp control & status bits.  */
		/* Note that DU doesn't verify available space here.  */

 		w = current_thread_info()->ieee_state & IEEE_SW_MASK;
 		w = swcr_update_status(w, rdfpcr());
		if (put_user(w, (unsigned long __user *) buffer))
			return -EFAULT;
		return 0;

	case GSI_IEEE_STATE_AT_SIGNAL:
		/*
		 * Not sure anybody will ever use this weird stuff.  These
		 * ops can be used (under OSF/1) to set the fpcr that should
		 * be used when a signal handler starts executing.
		 */
		break;

 	case GSI_UACPROC:
		if (nbytes < sizeof(unsigned int))
			return -EINVAL;
		w = current_thread_info()->status & UAC_BITMASK;
		if (put_user(w, (unsigned int __user *)buffer))
			return -EFAULT;
 		return 1;

	case GSI_PROC_TYPE:
		if (nbytes < sizeof(unsigned long))
			return -EINVAL;
		cpu = (struct percpu_struct*)
		  ((char*)hwrpb + hwrpb->processor_offset);
		w = cpu->type;
		if (put_user(w, (unsigned long  __user*)buffer))
			return -EFAULT;
		return 1;

	case GSI_GET_HWRPB:
		if (nbytes > sizeof(*hwrpb))
			return -EINVAL;
		if (copy_to_user(buffer, hwrpb, nbytes) != 0)
			return -EFAULT;
		return 1;

	default:
		break;
	}

	return -EOPNOTSUPP;
}

SYSCALL_DEFINE5(osf_setsysinfo, unsigned long, op, void __user *, buffer,
		unsigned long, nbytes, int __user *, start, void __user *, arg)
{
	switch (op) {
	case SSI_IEEE_FP_CONTROL: {
		unsigned long swcr, fpcr;
		unsigned int *state;

		/* 
		 * Alpha Architecture Handbook 4.7.7.3:
		 * To be fully IEEE compiant, we must track the current IEEE
		 * exception state in software, because spurious bits can be
		 * set in the trap shadow of a software-complete insn.
		 */

		if (get_user(swcr, (unsigned long __user *)buffer))
			return -EFAULT;
		state = &current_thread_info()->ieee_state;

		/* Update softare trap enable bits.  */
		*state = (*state & ~IEEE_SW_MASK) | (swcr & IEEE_SW_MASK);

		/* Update the real fpcr.  */
		fpcr = rdfpcr() & FPCR_DYN_MASK;
		fpcr |= ieee_swcr_to_fpcr(swcr);
		wrfpcr(fpcr);

		return 0;
	}

	case SSI_IEEE_RAISE_EXCEPTION: {
		unsigned long exc, swcr, fpcr, fex;
		unsigned int *state;

		if (get_user(exc, (unsigned long __user *)buffer))
			return -EFAULT;
		state = &current_thread_info()->ieee_state;
		exc &= IEEE_STATUS_MASK;

		/* Update softare trap enable bits.  */
 		swcr = (*state & IEEE_SW_MASK) | exc;
		*state |= exc;

		/* Update the real fpcr.  */
		fpcr = rdfpcr();
		fpcr |= ieee_swcr_to_fpcr(swcr);
		wrfpcr(fpcr);

 		/* If any exceptions set by this call, and are unmasked,
		   send a signal.  Old exceptions are not signaled.  */
		fex = (exc >> IEEE_STATUS_TO_EXCSUM_SHIFT) & swcr;
 		if (fex) {
			siginfo_t info;
			int si_code = 0;

			if (fex & IEEE_TRAP_ENABLE_DNO) si_code = FPE_FLTUND;
			if (fex & IEEE_TRAP_ENABLE_INE) si_code = FPE_FLTRES;
			if (fex & IEEE_TRAP_ENABLE_UNF) si_code = FPE_FLTUND;
			if (fex & IEEE_TRAP_ENABLE_OVF) si_code = FPE_FLTOVF;
			if (fex & IEEE_TRAP_ENABLE_DZE) si_code = FPE_FLTDIV;
			if (fex & IEEE_TRAP_ENABLE_INV) si_code = FPE_FLTINV;

			info.si_signo = SIGFPE;
			info.si_errno = 0;
			info.si_code = si_code;
			info.si_addr = NULL;  /* FIXME */
 			send_sig_info(SIGFPE, &info, current);
 		}
		return 0;
	}

	case SSI_IEEE_STATE_AT_SIGNAL:
	case SSI_IEEE_IGNORE_STATE_AT_SIGNAL:
		/*
		 * Not sure anybody will ever use this weird stuff.  These
		 * ops can be used (under OSF/1) to set the fpcr that should
		 * be used when a signal handler starts executing.
		 */
		break;

 	case SSI_NVPAIRS: {
		unsigned __user *p = buffer;
		unsigned i;
		
		for (i = 0, p = buffer; i < nbytes; ++i, p += 2) {
			unsigned v, w, status;

			if (get_user(v, p) || get_user(w, p + 1))
 				return -EFAULT;
 			switch (v) {
 			case SSIN_UACPROC:
				w &= UAC_BITMASK;
				status = current_thread_info()->status;
				status = (status & ~UAC_BITMASK) | w;
				current_thread_info()->status = status;
 				break;
 
 			default:
 				return -EOPNOTSUPP;
 			}
 		}
 		return 0;
	}
 
	case SSI_LMF:
		return 0;

	default:
		break;
	}

	return -EOPNOTSUPP;
}

/* Translations due to the fact that OSF's time_t is an int.  Which
   affects all sorts of things, like timeval and itimerval.  */

extern struct timezone sys_tz;

struct timeval32
{
    int tv_sec, tv_usec;
};

struct itimerval32
{
    struct timeval32 it_interval;
    struct timeval32 it_value;
};

static inline long
get_tv32(struct timeval *o, struct timeval32 __user *i)
{
	struct timeval32 tv;
	if (copy_from_user(&tv, i, sizeof(struct timeval32)))
		return -EFAULT;
	o->tv_sec = tv.tv_sec;
	o->tv_usec = tv.tv_usec;
	return 0;
}

static inline long
put_tv32(struct timeval32 __user *o, struct timeval *i)
{
	return copy_to_user(o, &(struct timeval32){
				.tv_sec = i->tv_sec,
				.tv_usec = i->tv_usec},
			    sizeof(struct timeval32));
}

static inline long
get_it32(struct itimerval *o, struct itimerval32 __user *i)
{
	struct itimerval32 itv;
	if (copy_from_user(&itv, i, sizeof(struct itimerval32)))
		return -EFAULT;
	o->it_interval.tv_sec = itv.it_interval.tv_sec;
	o->it_interval.tv_usec = itv.it_interval.tv_usec;
	o->it_value.tv_sec = itv.it_value.tv_sec;
	o->it_value.tv_usec = itv.it_value.tv_usec;
	return 0;
}

static inline long
put_it32(struct itimerval32 __user *o, struct itimerval *i)
{
	return copy_to_user(o, &(struct itimerval32){
				.it_interval.tv_sec = o->it_interval.tv_sec,
				.it_interval.tv_usec = o->it_interval.tv_usec,
				.it_value.tv_sec = o->it_value.tv_sec,
				.it_value.tv_usec = o->it_value.tv_usec},
			    sizeof(struct itimerval32));
}

static inline void
jiffies_to_timeval32(unsigned long jiffies, struct timeval32 *value)
{
	value->tv_usec = (jiffies % HZ) * (1000000L / HZ);
	value->tv_sec = jiffies / HZ;
}

SYSCALL_DEFINE2(osf_gettimeofday, struct timeval32 __user *, tv,
		struct timezone __user *, tz)
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

SYSCALL_DEFINE2(osf_settimeofday, struct timeval32 __user *, tv,
		struct timezone __user *, tz)
{
	struct timespec64 kts64;
	struct timespec kts;
	struct timezone ktz;

 	if (tv) {
		if (get_tv32((struct timeval *)&kts, tv))
			return -EFAULT;
		kts.tv_nsec *= 1000;
		kts64 = timespec_to_timespec64(kts);
	}
	if (tz) {
		if (copy_from_user(&ktz, tz, sizeof(*tz)))
			return -EFAULT;
	}

	return do_sys_settimeofday64(tv ? &kts64 : NULL, tz ? &ktz : NULL);
}

asmlinkage long sys_ni_posix_timers(void);

SYSCALL_DEFINE2(osf_getitimer, int, which, struct itimerval32 __user *, it)
{
	struct itimerval kit;
	int error;

	if (!IS_ENABLED(CONFIG_POSIX_TIMERS))
		return sys_ni_posix_timers();

	error = do_getitimer(which, &kit);
	if (!error && put_it32(it, &kit))
		error = -EFAULT;

	return error;
}

SYSCALL_DEFINE3(osf_setitimer, int, which, struct itimerval32 __user *, in,
		struct itimerval32 __user *, out)
{
	struct itimerval kin, kout;
	int error;

	if (!IS_ENABLED(CONFIG_POSIX_TIMERS))
		return sys_ni_posix_timers();

	if (in) {
		if (get_it32(&kin, in))
			return -EFAULT;
	} else
		memset(&kin, 0, sizeof(kin));

	error = do_setitimer(which, &kin, out ? &kout : NULL);
	if (error || !out)
		return error;

	if (put_it32(out, &kout))
		return -EFAULT;

	return 0;

}

SYSCALL_DEFINE2(osf_utimes, const char __user *, filename,
		struct timeval32 __user *, tvs)
{
	struct timespec tv[2];

	if (tvs) {
		struct timeval ktvs[2];
		if (get_tv32(&ktvs[0], &tvs[0]) ||
		    get_tv32(&ktvs[1], &tvs[1]))
			return -EFAULT;

		if (ktvs[0].tv_usec < 0 || ktvs[0].tv_usec >= 1000000 ||
		    ktvs[1].tv_usec < 0 || ktvs[1].tv_usec >= 1000000)
			return -EINVAL;

		tv[0].tv_sec = ktvs[0].tv_sec;
		tv[0].tv_nsec = 1000 * ktvs[0].tv_usec;
		tv[1].tv_sec = ktvs[1].tv_sec;
		tv[1].tv_nsec = 1000 * ktvs[1].tv_usec;
	}

	return do_utimes(AT_FDCWD, filename, tvs ? tv : NULL, 0);
}

SYSCALL_DEFINE5(osf_select, int, n, fd_set __user *, inp, fd_set __user *, outp,
		fd_set __user *, exp, struct timeval32 __user *, tvp)
{
	struct timespec end_time, *to = NULL;
	if (tvp) {
		struct timeval tv;
		to = &end_time;

		if (get_tv32(&tv, tvp))
		    	return -EFAULT;

		if (tv.tv_sec < 0 || tv.tv_usec < 0)
			return -EINVAL;

		if (poll_select_set_timeout(to, tv.tv_sec,
					    tv.tv_usec * NSEC_PER_USEC))
			return -EINVAL;		

	}

	/* OSF does not copy back the remaining time.  */
	return core_sys_select(n, inp, outp, exp, to);
}

struct rusage32 {
	struct timeval32 ru_utime;	/* user time used */
	struct timeval32 ru_stime;	/* system time used */
	long	ru_maxrss;		/* maximum resident set size */
	long	ru_ixrss;		/* integral shared memory size */
	long	ru_idrss;		/* integral unshared data size */
	long	ru_isrss;		/* integral unshared stack size */
	long	ru_minflt;		/* page reclaims */
	long	ru_majflt;		/* page faults */
	long	ru_nswap;		/* swaps */
	long	ru_inblock;		/* block input operations */
	long	ru_oublock;		/* block output operations */
	long	ru_msgsnd;		/* messages sent */
	long	ru_msgrcv;		/* messages received */
	long	ru_nsignals;		/* signals received */
	long	ru_nvcsw;		/* voluntary context switches */
	long	ru_nivcsw;		/* involuntary " */
};

SYSCALL_DEFINE2(osf_getrusage, int, who, struct rusage32 __user *, ru)
{
	struct rusage32 r;
	u64 utime, stime;
	unsigned long utime_jiffies, stime_jiffies;

	if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN)
		return -EINVAL;

	memset(&r, 0, sizeof(r));
	switch (who) {
	case RUSAGE_SELF:
		task_cputime(current, &utime, &stime);
		utime_jiffies = nsecs_to_jiffies(utime);
		stime_jiffies = nsecs_to_jiffies(stime);
		jiffies_to_timeval32(utime_jiffies, &r.ru_utime);
		jiffies_to_timeval32(stime_jiffies, &r.ru_stime);
		r.ru_minflt = current->min_flt;
		r.ru_majflt = current->maj_flt;
		break;
	case RUSAGE_CHILDREN:
		utime_jiffies = nsecs_to_jiffies(current->signal->cutime);
		stime_jiffies = nsecs_to_jiffies(current->signal->cstime);
		jiffies_to_timeval32(utime_jiffies, &r.ru_utime);
		jiffies_to_timeval32(stime_jiffies, &r.ru_stime);
		r.ru_minflt = current->signal->cmin_flt;
		r.ru_majflt = current->signal->cmaj_flt;
		break;
	}

	return copy_to_user(ru, &r, sizeof(r)) ? -EFAULT : 0;
}

SYSCALL_DEFINE4(osf_wait4, pid_t, pid, int __user *, ustatus, int, options,
		struct rusage32 __user *, ur)
{
	struct rusage r;
	long err = kernel_wait4(pid, ustatus, options, &r);
	if (err <= 0)
		return err;
	if (!ur)
		return err;
	if (put_tv32(&ur->ru_utime, &r.ru_utime))
		return -EFAULT;
	if (put_tv32(&ur->ru_stime, &r.ru_stime))
		return -EFAULT;
	if (copy_to_user(&ur->ru_maxrss, &r.ru_maxrss,
	      sizeof(struct rusage32) - offsetof(struct rusage32, ru_maxrss)))
		return -EFAULT;
	return err;
}

/*
 * I don't know what the parameters are: the first one
 * seems to be a timeval pointer, and I suspect the second
 * one is the time remaining.. Ho humm.. No documentation.
 */
SYSCALL_DEFINE2(osf_usleep_thread, struct timeval32 __user *, sleep,
		struct timeval32 __user *, remain)
{
	struct timeval tmp;
	unsigned long ticks;

	if (get_tv32(&tmp, sleep))
		goto fault;

	ticks = timeval_to_jiffies(&tmp);

	ticks = schedule_timeout_interruptible(ticks);

	if (remain) {
		jiffies_to_timeval(ticks, &tmp);
		if (put_tv32(remain, &tmp))
			goto fault;
	}
	
	return 0;
 fault:
	return -EFAULT;
}


struct timex32 {
	unsigned int modes;	/* mode selector */
	long offset;		/* time offset (usec) */
	long freq;		/* frequency offset (scaled ppm) */
	long maxerror;		/* maximum error (usec) */
	long esterror;		/* estimated error (usec) */
	int status;		/* clock command/status */
	long constant;		/* pll time constant */
	long precision;		/* clock precision (usec) (read only) */
	long tolerance;		/* clock frequency tolerance (ppm)
				 * (read only)
				 */
	struct timeval32 time;	/* (read only) */
	long tick;		/* (modified) usecs between clock ticks */

	long ppsfreq;           /* pps frequency (scaled ppm) (ro) */
	long jitter;            /* pps jitter (us) (ro) */
	int shift;              /* interval duration (s) (shift) (ro) */
	long stabil;            /* pps stability (scaled ppm) (ro) */
	long jitcnt;            /* jitter limit exceeded (ro) */
	long calcnt;            /* calibration intervals (ro) */
	long errcnt;            /* calibration errors (ro) */
	long stbcnt;            /* stability limit exceeded (ro) */

	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32; int  :32;
};

SYSCALL_DEFINE1(old_adjtimex, struct timex32 __user *, txc_p)
{
        struct timex txc;
	int ret;

	/* copy relevant bits of struct timex. */
	if (copy_from_user(&txc, txc_p, offsetof(struct timex32, time)) ||
	    copy_from_user(&txc.tick, &txc_p->tick, sizeof(struct timex32) - 
			   offsetof(struct timex32, tick)))
	  return -EFAULT;

	ret = do_adjtimex(&txc);	
	if (ret < 0)
	  return ret;
	
	/* copy back to timex32 */
	if (copy_to_user(txc_p, &txc, offsetof(struct timex32, time)) ||
	    (copy_to_user(&txc_p->tick, &txc.tick, sizeof(struct timex32) - 
			  offsetof(struct timex32, tick))) ||
	    (put_tv32(&txc_p->time, &txc.time)))
	  return -EFAULT;

	return ret;
}

/* Get an address range which is currently unmapped.  Similar to the
   generic version except that we know how to honor ADDR_LIMIT_32BIT.  */

static unsigned long
arch_get_unmapped_area_1(unsigned long addr, unsigned long len,
		         unsigned long limit)
{
	struct vm_unmapped_area_info info;

	info.flags = 0;
	info.length = len;
	info.low_limit = addr;
	info.high_limit = limit;
	info.align_mask = 0;
	info.align_offset = 0;
	return vm_unmapped_area(&info);
}

unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		       unsigned long len, unsigned long pgoff,
		       unsigned long flags)
{
	unsigned long limit;

	/* "32 bit" actually means 31 bit, since pointers sign extend.  */
	if (current->personality & ADDR_LIMIT_32BIT)
		limit = 0x80000000;
	else
		limit = TASK_SIZE;

	if (len > limit)
		return -ENOMEM;

	if (flags & MAP_FIXED)
		return addr;

	/* First, see if the given suggestion fits.

	   The OSF/1 loader (/sbin/loader) relies on us returning an
	   address larger than the requested if one exists, which is
	   a terribly broken way to program.

	   That said, I can see the use in being able to suggest not
	   merely specific addresses, but regions of memory -- perhaps
	   this feature should be incorporated into all ports?  */

	if (addr) {
		addr = arch_get_unmapped_area_1 (PAGE_ALIGN(addr), len, limit);
		if (addr != (unsigned long) -ENOMEM)
			return addr;
	}

	/* Next, try allocating at TASK_UNMAPPED_BASE.  */
	addr = arch_get_unmapped_area_1 (PAGE_ALIGN(TASK_UNMAPPED_BASE),
					 len, limit);
	if (addr != (unsigned long) -ENOMEM)
		return addr;

	/* Finally, try allocating in low memory.  */
	addr = arch_get_unmapped_area_1 (PAGE_SIZE, len, limit);

	return addr;
}

#ifdef CONFIG_OSF4_COMPAT

/* Clear top 32 bits of iov_len in the user's buffer for
   compatibility with old versions of OSF/1 where iov_len
   was defined as int. */
static int
osf_fix_iov_len(const struct iovec __user *iov, unsigned long count)
{
	unsigned long i;

	for (i = 0 ; i < count ; i++) {
		int __user *iov_len_high = (int __user *)&iov[i].iov_len + 1;

		if (put_user(0, iov_len_high))
			return -EFAULT;
	}
	return 0;
}

SYSCALL_DEFINE3(osf_readv, unsigned long, fd,
		const struct iovec __user *, vector, unsigned long, count)
{
	if (unlikely(personality(current->personality) == PER_OSF4))
		if (osf_fix_iov_len(vector, count))
			return -EFAULT;
	return sys_readv(fd, vector, count);
}

SYSCALL_DEFINE3(osf_writev, unsigned long, fd,
		const struct iovec __user *, vector, unsigned long, count)
{
	if (unlikely(personality(current->personality) == PER_OSF4))
		if (osf_fix_iov_len(vector, count))
			return -EFAULT;
	return sys_writev(fd, vector, count);
}

#endif

SYSCALL_DEFINE2(osf_getpriority, int, which, int, who)
{
	int prio = sys_getpriority(which, who);
	if (prio >= 0) {
		/* Return value is the unbiased priority, i.e. 20 - prio.
		   This does result in negative return values, so signal
		   no error */
		force_successful_syscall_return();
		prio = 20 - prio;
	}
	return prio;
}

SYSCALL_DEFINE0(getxuid)
{
	current_pt_regs()->r20 = sys_geteuid();
	return sys_getuid();
}

SYSCALL_DEFINE0(getxgid)
{
	current_pt_regs()->r20 = sys_getegid();
	return sys_getgid();
}

SYSCALL_DEFINE0(getxpid)
{
	current_pt_regs()->r20 = sys_getppid();
	return sys_getpid();
}

SYSCALL_DEFINE0(alpha_pipe)
{
	int fd[2];
	int res = do_pipe_flags(fd, 0);
	if (!res) {
		/* The return values are in $0 and $20.  */
		current_pt_regs()->r20 = fd[1];
		res = fd[0];
	}
	return res;
}

SYSCALL_DEFINE1(sethae, unsigned long, val)
{
	current_pt_regs()->hae = val;
	return 0;
}
