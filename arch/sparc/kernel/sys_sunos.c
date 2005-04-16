/* $Id: sys_sunos.c,v 1.137 2002/02/08 03:57:14 davem Exp $
 * sys_sunos.c: SunOS specific syscall compatibility support.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * Based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/resource.h>
#include <linux/ipc.h>
#include <linux/shm.h>
#include <linux/msg.h>
#include <linux/sem.h>
#include <linux/signal.h>
#include <linux/uio.h>
#include <linux/utsname.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/syscalls.h>

#include <net/sock.h>

#include <asm/uaccess.h>
#ifndef KERNEL_DS
#include <linux/segment.h>
#endif

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pconf.h>
#include <asm/idprom.h> /* for gethostid() */
#include <asm/unistd.h>
#include <asm/system.h>

/* For the nfs mount emulation */
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/nfs_mount.h>

/* for sunos_select */
#include <linux/time.h>
#include <linux/personality.h>

/* NR_OPEN is now larger and dynamic in recent kernels. */
#define SUNOS_NR_OPEN	256

/* We use the SunOS mmap() semantics. */
asmlinkage unsigned long sunos_mmap(unsigned long addr, unsigned long len,
				    unsigned long prot, unsigned long flags,
				    unsigned long fd, unsigned long off)
{
	struct file * file = NULL;
	unsigned long retval, ret_type;

	if (flags & MAP_NORESERVE) {
		static int cnt;
		if (cnt++ < 10)
			printk("%s: unimplemented SunOS MAP_NORESERVE mmap() flag\n",
			       current->comm);
		flags &= ~MAP_NORESERVE;
	}
	retval = -EBADF;
	if (!(flags & MAP_ANONYMOUS)) {
		if (fd >= SUNOS_NR_OPEN)
			goto out;
		file = fget(fd);
		if (!file)
			goto out;
	}

	retval = -EINVAL;
	/* If this is ld.so or a shared library doing an mmap
	 * of /dev/zero, transform it into an anonymous mapping.
	 * SunOS is so stupid some times... hmph!
	 */
	if (file) {
		if (imajor(file->f_dentry->d_inode) == MEM_MAJOR &&
		    iminor(file->f_dentry->d_inode) == 5) {
			flags |= MAP_ANONYMOUS;
			fput(file);
			file = NULL;
		}
	}
	ret_type = flags & _MAP_NEW;
	flags &= ~_MAP_NEW;

	if (!(flags & MAP_FIXED))
		addr = 0;
	else {
		if (ARCH_SUN4C_SUN4 &&
		    (len > 0x20000000 ||
		     ((flags & MAP_FIXED) &&
		      addr < 0xe0000000 && addr + len > 0x20000000)))
			goto out_putf;

		/* See asm-sparc/uaccess.h */
		if (len > TASK_SIZE - PAGE_SIZE ||
		    addr + len > TASK_SIZE - PAGE_SIZE)
			goto out_putf;
	}

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	down_write(&current->mm->mmap_sem);
	retval = do_mmap(file, addr, len, prot, flags, off);
	up_write(&current->mm->mmap_sem);
	if (!ret_type)
		retval = ((retval < PAGE_OFFSET) ? 0 : retval);

out_putf:
	if (file)
		fput(file);
out:
	return retval;
}

/* lmbench calls this, just say "yeah, ok" */
asmlinkage int sunos_mctl(unsigned long addr, unsigned long len, int function, char *arg)
{
	return 0;
}

/* SunOS is completely broken... it returns 0 on success, otherwise
 * ENOMEM.  For sys_sbrk() it wants the old brk value as a return
 * on success and ENOMEM as before on failure.
 */
asmlinkage int sunos_brk(unsigned long brk)
{
	int freepages, retval = -ENOMEM;
	unsigned long rlim;
	unsigned long newbrk, oldbrk;

	down_write(&current->mm->mmap_sem);
	if (ARCH_SUN4C_SUN4) {
		if (brk >= 0x20000000 && brk < 0xe0000000) {
			goto out;
		}
	}

	if (brk < current->mm->end_code)
		goto out;

	newbrk = PAGE_ALIGN(brk);
	oldbrk = PAGE_ALIGN(current->mm->brk);
	retval = 0;
	if (oldbrk == newbrk) {
		current->mm->brk = brk;
		goto out;
	}

	/*
	 * Always allow shrinking brk
	 */
	if (brk <= current->mm->brk) {
		current->mm->brk = brk;
		do_munmap(current->mm, newbrk, oldbrk-newbrk);
		goto out;
	}
	/*
	 * Check against rlimit and stack..
	 */
	retval = -ENOMEM;
	rlim = current->signal->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (brk - current->mm->end_code > rlim)
		goto out;

	/*
	 * Check against existing mmap mappings.
	 */
	if (find_vma_intersection(current->mm, oldbrk, newbrk+PAGE_SIZE))
		goto out;

	/*
	 * stupid algorithm to decide if we have enough memory: while
	 * simple, it hopefully works in most obvious cases.. Easy to
	 * fool it, but this should catch most mistakes.
	 */
	freepages = get_page_cache_size();
	freepages >>= 1;
	freepages += nr_free_pages();
	freepages += nr_swap_pages;
	freepages -= num_physpages >> 4;
	freepages -= (newbrk-oldbrk) >> PAGE_SHIFT;
	if (freepages < 0)
		goto out;
	/*
	 * Ok, we have probably got enough memory - let it rip.
	 */
	current->mm->brk = brk;
	do_brk(oldbrk, newbrk-oldbrk);
	retval = 0;
out:
	up_write(&current->mm->mmap_sem);
	return retval;
}

asmlinkage unsigned long sunos_sbrk(int increment)
{
	int error;
	unsigned long oldbrk;

	/* This should do it hopefully... */
	lock_kernel();
	oldbrk = current->mm->brk;
	error = sunos_brk(((int) current->mm->brk) + increment);
	if (!error)
		error = oldbrk;
	unlock_kernel();
	return error;
}

/* XXX Completely undocumented, and completely magic...
 * XXX I believe it is to increase the size of the stack by
 * XXX argument 'increment' and return the new end of stack
 * XXX area.  Wheee...
 */
asmlinkage unsigned long sunos_sstk(int increment)
{
	lock_kernel();
	printk("%s: Call to sunos_sstk(increment<%d>) is unsupported\n",
	       current->comm, increment);
	unlock_kernel();
	return -1;
}

/* Give hints to the kernel as to what paging strategy to use...
 * Completely bogus, don't remind me.
 */
#define VA_NORMAL     0 /* Normal vm usage expected */
#define VA_ABNORMAL   1 /* Abnormal/random vm usage probable */
#define VA_SEQUENTIAL 2 /* Accesses will be of a sequential nature */
#define VA_INVALIDATE 3 /* Page table entries should be flushed ??? */
static char *vstrings[] = {
	"VA_NORMAL",
	"VA_ABNORMAL",
	"VA_SEQUENTIAL",
	"VA_INVALIDATE",
};

asmlinkage void sunos_vadvise(unsigned long strategy)
{
	/* I wanna see who uses this... */
	lock_kernel();
	printk("%s: Advises us to use %s paging strategy\n",
	       current->comm,
	       strategy <= 3 ? vstrings[strategy] : "BOGUS");
	unlock_kernel();
}

/* This just wants the soft limit (ie. rlim_cur element) of the RLIMIT_NOFILE
 * resource limit and is for backwards compatibility with older sunos
 * revs.
 */
asmlinkage long sunos_getdtablesize(void)
{
	return SUNOS_NR_OPEN;
}

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage unsigned long sunos_sigblock(unsigned long blk_mask)
{
	unsigned long old;

	spin_lock_irq(&current->sighand->siglock);
	old = current->blocked.sig[0];
	current->blocked.sig[0] |= (blk_mask & _BLOCKABLE);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	return old;
}

asmlinkage unsigned long sunos_sigsetmask(unsigned long newmask)
{
	unsigned long retval;

	spin_lock_irq(&current->sighand->siglock);
	retval = current->blocked.sig[0];
	current->blocked.sig[0] = (newmask & _BLOCKABLE);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	return retval;
}

/* SunOS getdents is very similar to the newer Linux (iBCS2 compliant)    */
/* getdents system call, the format of the structure just has a different */
/* layout (d_off+d_ino instead of d_ino+d_off) */
struct sunos_dirent {
    long           d_off;
    unsigned long  d_ino;
    unsigned short d_reclen;
    unsigned short d_namlen;
    char           d_name[1];
};

struct sunos_dirent_callback {
    struct sunos_dirent __user *curr;
    struct sunos_dirent __user *previous;
    int count;
    int error;
};

#define NAME_OFFSET(de) ((int) ((de)->d_name - (char __user *) (de)))
#define ROUND_UP(x) (((x)+sizeof(long)-1) & ~(sizeof(long)-1))

static int sunos_filldir(void * __buf, const char * name, int namlen,
			 loff_t offset, ino_t ino, unsigned int d_type)
{
	struct sunos_dirent __user *dirent;
	struct sunos_dirent_callback * buf = __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	if (dirent)
		put_user(offset, &dirent->d_off);
	dirent = buf->curr;
	buf->previous = dirent;
	put_user(ino, &dirent->d_ino);
	put_user(namlen, &dirent->d_namlen);
	put_user(reclen, &dirent->d_reclen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	dirent = (void __user *) dirent + reclen;
	buf->curr = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage int sunos_getdents(unsigned int fd, void __user *dirent, int cnt)
{
	struct file * file;
	struct sunos_dirent __user *lastdirent;
	struct sunos_dirent_callback buf;
	int error = -EBADF;

	if (fd >= SUNOS_NR_OPEN)
		goto out;

	file = fget(fd);
	if (!file)
		goto out;

	error = -EINVAL;
	if (cnt < (sizeof(struct sunos_dirent) + 255))
		goto out_putf;

	buf.curr = (struct sunos_dirent __user *) dirent;
	buf.previous = NULL;
	buf.count = cnt;
	buf.error = 0;

	error = vfs_readdir(file, sunos_filldir, &buf);
	if (error < 0)
		goto out_putf;

	lastdirent = buf.previous;
	error = buf.error;
	if (lastdirent) {
		put_user(file->f_pos, &lastdirent->d_off);
		error = cnt - buf.count;
	}

out_putf:
	fput(file);
out:
	return error;
}

/* Old sunos getdirentries, severely broken compatibility stuff here. */
struct sunos_direntry {
    unsigned long  d_ino;
    unsigned short d_reclen;
    unsigned short d_namlen;
    char           d_name[1];
};

struct sunos_direntry_callback {
    struct sunos_direntry __user *curr;
    struct sunos_direntry __user *previous;
    int count;
    int error;
};

static int sunos_filldirentry(void * __buf, const char * name, int namlen,
			      loff_t offset, ino_t ino, unsigned int d_type)
{
	struct sunos_direntry __user *dirent;
	struct sunos_direntry_callback *buf = __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	dirent = buf->curr;
	buf->previous = dirent;
	put_user(ino, &dirent->d_ino);
	put_user(namlen, &dirent->d_namlen);
	put_user(reclen, &dirent->d_reclen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	dirent = (void __user *) dirent + reclen;
	buf->curr = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage int sunos_getdirentries(unsigned int fd, void __user *dirent,
				   int cnt, unsigned int __user *basep)
{
	struct file * file;
	struct sunos_direntry __user *lastdirent;
	struct sunos_direntry_callback buf;
	int error = -EBADF;

	if (fd >= SUNOS_NR_OPEN)
		goto out;

	file = fget(fd);
	if (!file)
		goto out;

	error = -EINVAL;
	if (cnt < (sizeof(struct sunos_direntry) + 255))
		goto out_putf;

	buf.curr = (struct sunos_direntry __user *) dirent;
	buf.previous = NULL;
	buf.count = cnt;
	buf.error = 0;

	error = vfs_readdir(file, sunos_filldirentry, &buf);
	if (error < 0)
		goto out_putf;

	lastdirent = buf.previous;
	error = buf.error;
	if (lastdirent) {
		put_user(file->f_pos, basep);
		error = cnt - buf.count;
	}

out_putf:
	fput(file);
out:
	return error;
}

struct sunos_utsname {
	char sname[9];
	char nname[9];
	char nnext[56];
	char rel[9];
	char ver[9];
	char mach[9];
};

asmlinkage int sunos_uname(struct sunos_utsname __user *name)
{
	int ret;
	down_read(&uts_sem);
	ret = copy_to_user(&name->sname[0], &system_utsname.sysname[0], sizeof(name->sname) - 1);
	if (!ret) {
		ret |= __copy_to_user(&name->nname[0], &system_utsname.nodename[0], sizeof(name->nname) - 1);
		ret |= __put_user('\0', &name->nname[8]);
		ret |= __copy_to_user(&name->rel[0], &system_utsname.release[0], sizeof(name->rel) - 1);
		ret |= __copy_to_user(&name->ver[0], &system_utsname.version[0], sizeof(name->ver) - 1);
		ret |= __copy_to_user(&name->mach[0], &system_utsname.machine[0], sizeof(name->mach) - 1);
	}
	up_read(&uts_sem);
	return ret ? -EFAULT : 0;
}

asmlinkage int sunos_nosys(void)
{
	struct pt_regs *regs;
	siginfo_t info;
	static int cnt;

	lock_kernel();
	regs = current->thread.kregs;
	info.si_signo = SIGSYS;
	info.si_errno = 0;
	info.si_code = __SI_FAULT|0x100;
	info.si_addr = (void __user *)regs->pc;
	info.si_trapno = regs->u_regs[UREG_G1];
	send_sig_info(SIGSYS, &info, current);
	if (cnt++ < 4) {
		printk("Process makes ni_syscall number %d, register dump:\n",
		       (int) regs->u_regs[UREG_G1]);
		show_regs(regs);
	}
	unlock_kernel();
	return -ENOSYS;
}

/* This is not a real and complete implementation yet, just to keep
 * the easy SunOS binaries happy.
 */
asmlinkage int sunos_fpathconf(int fd, int name)
{
	int ret;

	switch(name) {
	case _PCONF_LINK:
		ret = LINK_MAX;
		break;
	case _PCONF_CANON:
		ret = MAX_CANON;
		break;
	case _PCONF_INPUT:
		ret = MAX_INPUT;
		break;
	case _PCONF_NAME:
		ret = NAME_MAX;
		break;
	case _PCONF_PATH:
		ret = PATH_MAX;
		break;
	case _PCONF_PIPE:
		ret = PIPE_BUF;
		break;
	case _PCONF_CHRESTRICT:		/* XXX Investigate XXX */
		ret = 1;
		break;
	case _PCONF_NOTRUNC:		/* XXX Investigate XXX */
	case _PCONF_VDISABLE:
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

asmlinkage int sunos_pathconf(char __user *path, int name)
{
	int ret;

	ret = sunos_fpathconf(0, name); /* XXX cheese XXX */
	return ret;
}

/* SunOS mount system call emulation */

asmlinkage int sunos_select(int width, fd_set __user *inp, fd_set __user *outp,
			    fd_set __user *exp, struct timeval __user *tvp)
{
	int ret;

	/* SunOS binaries expect that select won't change the tvp contents */
	ret = sys_select (width, inp, outp, exp, tvp);
	if (ret == -EINTR && tvp) {
		time_t sec, usec;

		__get_user(sec, &tvp->tv_sec);
		__get_user(usec, &tvp->tv_usec);

		if (sec == 0 && usec == 0)
			ret = 0;
	}
	return ret;
}

asmlinkage void sunos_nop(void)
{
	return;
}

/* SunOS mount/umount. */
#define SMNT_RDONLY       1
#define SMNT_NOSUID       2
#define SMNT_NEWTYPE      4
#define SMNT_GRPID        8
#define SMNT_REMOUNT      16
#define SMNT_NOSUB        32
#define SMNT_MULTI        64
#define SMNT_SYS5         128

struct sunos_fh_t {
	char fh_data [NFS_FHSIZE];
};

struct sunos_nfs_mount_args {
	struct sockaddr_in  __user *addr; /* file server address */
	struct nfs_fh __user *fh;     /* File handle to be mounted */
	int        flags;      /* flags */
	int        wsize;      /* write size in bytes */
	int        rsize;      /* read size in bytes */
	int        timeo;      /* initial timeout in .1 secs */
	int        retrans;    /* times to retry send */
	char       __user *hostname;  /* server's hostname */
	int        acregmin;   /* attr cache file min secs */
	int        acregmax;   /* attr cache file max secs */
	int        acdirmin;   /* attr cache dir min secs */
	int        acdirmax;   /* attr cache dir max secs */
	char       __user *netname;   /* server's netname */
};


/* Bind the socket on a local reserved port and connect it to the
 * remote server.  This on Linux/i386 is done by the mount program,
 * not by the kernel.
 */
static int
sunos_nfs_get_server_fd (int fd, struct sockaddr_in *addr)
{
	struct sockaddr_in local;
	struct sockaddr_in server;
	int    try_port;
	struct socket *socket;
	struct inode  *inode;
	struct file   *file;
	int    ret, result = 0;

	file = fget(fd);
	if (!file)
		goto out;

	inode = file->f_dentry->d_inode;

	socket = SOCKET_I(inode);
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;

	/* IPPORT_RESERVED = 1024, can't find the definition in the kernel */
	try_port = 1024;
	do {
		local.sin_port = htons (--try_port);
		ret = socket->ops->bind(socket, (struct sockaddr*)&local,
					sizeof(local));
	} while (ret && try_port > (1024 / 2));

	if (ret)
		goto out_putf;

	server.sin_family = AF_INET;
	server.sin_addr = addr->sin_addr;
	server.sin_port = NFS_PORT;

	/* Call sys_connect */
	ret = socket->ops->connect (socket, (struct sockaddr *) &server,
				    sizeof (server), file->f_flags);
	if (ret >= 0)
		result = 1;

out_putf:
	fput(file);
out:
	return result;
}

static int get_default (int value, int def_value)
{
    if (value)
	return value;
    else
	return def_value;
}

static int sunos_nfs_mount(char *dir_name, int linux_flags, void __user *data)
{
	int  server_fd, err;
	char *the_name, *mount_page;
	struct nfs_mount_data linux_nfs_mount;
	struct sunos_nfs_mount_args sunos_mount;

	/* Ok, here comes the fun part: Linux's nfs mount needs a
	 * socket connection to the server, but SunOS mount does not
	 * require this, so we use the information on the destination
	 * address to create a socket and bind it to a reserved
	 * port on this system
	 */
	if (copy_from_user(&sunos_mount, data, sizeof(sunos_mount)))
		return -EFAULT;

	server_fd = sys_socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (server_fd < 0)
		return -ENXIO;

	if (copy_from_user(&linux_nfs_mount.addr,sunos_mount.addr,
				sizeof(*sunos_mount.addr)) ||
	    copy_from_user(&linux_nfs_mount.root,sunos_mount.fh,
				sizeof(*sunos_mount.fh))) {
		sys_close (server_fd);
		return -EFAULT;
	}

	if (!sunos_nfs_get_server_fd (server_fd, &linux_nfs_mount.addr)){
		sys_close (server_fd);
		return -ENXIO;
	}

	/* Now, bind it to a locally reserved port */
	linux_nfs_mount.version  = NFS_MOUNT_VERSION;
	linux_nfs_mount.flags    = sunos_mount.flags;
	linux_nfs_mount.fd       = server_fd;
	
	linux_nfs_mount.rsize    = get_default (sunos_mount.rsize, 8192);
	linux_nfs_mount.wsize    = get_default (sunos_mount.wsize, 8192);
	linux_nfs_mount.timeo    = get_default (sunos_mount.timeo, 10);
	linux_nfs_mount.retrans  = sunos_mount.retrans;
	
	linux_nfs_mount.acregmin = sunos_mount.acregmin;
	linux_nfs_mount.acregmax = sunos_mount.acregmax;
	linux_nfs_mount.acdirmin = sunos_mount.acdirmin;
	linux_nfs_mount.acdirmax = sunos_mount.acdirmax;

	the_name = getname(sunos_mount.hostname);
	if (IS_ERR(the_name))
		return PTR_ERR(the_name);

	strlcpy(linux_nfs_mount.hostname, the_name,
		sizeof(linux_nfs_mount.hostname));
	putname (the_name);
	
	mount_page = (char *) get_zeroed_page(GFP_KERNEL);
	if (!mount_page)
		return -ENOMEM;

	memcpy(mount_page, &linux_nfs_mount, sizeof(linux_nfs_mount));

	err = do_mount("", dir_name, "nfs", linux_flags, mount_page);

	free_page((unsigned long) mount_page);
	return err;
}

asmlinkage int
sunos_mount(char __user *type, char __user *dir, int flags, void __user *data)
{
	int linux_flags = 0;
	int ret = -EINVAL;
	char *dev_fname = NULL;
	char *dir_page, *type_page;

	if (!capable (CAP_SYS_ADMIN))
		return -EPERM;
		
	lock_kernel();
	/* We don't handle the integer fs type */
	if ((flags & SMNT_NEWTYPE) == 0)
		goto out;

	/* Do not allow for those flags we don't support */
	if (flags & (SMNT_GRPID|SMNT_NOSUB|SMNT_MULTI|SMNT_SYS5))
		goto out;

	if (flags & SMNT_REMOUNT)
		linux_flags |= MS_REMOUNT;
	if (flags & SMNT_RDONLY)
		linux_flags |= MS_RDONLY;
	if (flags & SMNT_NOSUID)
		linux_flags |= MS_NOSUID;

	dir_page = getname(dir);
	ret = PTR_ERR(dir_page);
	if (IS_ERR(dir_page))
		goto out;

	type_page = getname(type);
	ret = PTR_ERR(type_page);
	if (IS_ERR(type_page))
		goto out1;

	if (strcmp(type_page, "ext2") == 0) {
		dev_fname = getname(data);
	} else if (strcmp(type_page, "iso9660") == 0) {
		dev_fname = getname(data);
	} else if (strcmp(type_page, "minix") == 0) {
		dev_fname = getname(data);
	} else if (strcmp(type_page, "nfs") == 0) {
		ret = sunos_nfs_mount (dir_page, flags, data);
		goto out2;
        } else if (strcmp(type_page, "ufs") == 0) {
		printk("Warning: UFS filesystem mounts unsupported.\n");
		ret = -ENODEV;
		goto out2;
	} else if (strcmp(type_page, "proc")) {
		ret = -ENODEV;
		goto out2;
	}
	ret = PTR_ERR(dev_fname);
	if (IS_ERR(dev_fname))
		goto out2;
	ret = do_mount(dev_fname, dir_page, type_page, linux_flags, NULL);
	if (dev_fname)
		putname(dev_fname);
out2:
	putname(type_page);
out1:
	putname(dir_page);
out:
	unlock_kernel();
	return ret;
}


asmlinkage int sunos_setpgrp(pid_t pid, pid_t pgid)
{
	int ret;

	/* So stupid... */
	if ((!pid || pid == current->pid) &&
	    !pgid) {
		sys_setsid();
		ret = 0;
	} else {
		ret = sys_setpgid(pid, pgid);
	}
	return ret;
}

/* So stupid... */
asmlinkage int sunos_wait4(pid_t pid, unsigned int __user *stat_addr,
			   int options, struct rusage __user*ru)
{
	int ret;

	ret = sys_wait4((pid ? pid : -1), stat_addr, options, ru);
	return ret;
}

extern int kill_pg(int, int, int);
asmlinkage int sunos_killpg(int pgrp, int sig)
{
	int ret;

	lock_kernel();
	ret = kill_pg(pgrp, sig, 0);
	unlock_kernel();
	return ret;
}

asmlinkage int sunos_audit(void)
{
	lock_kernel();
	printk ("sys_audit\n");
	unlock_kernel();
	return -1;
}

asmlinkage unsigned long sunos_gethostid(void)
{
	unsigned long ret;

	lock_kernel();
	ret = ((unsigned long)idprom->id_machtype << 24) |
		(unsigned long)idprom->id_sernum;
	unlock_kernel();
	return ret;
}

/* sysconf options, for SunOS compatibility */
#define   _SC_ARG_MAX             1
#define   _SC_CHILD_MAX           2
#define   _SC_CLK_TCK             3
#define   _SC_NGROUPS_MAX         4
#define   _SC_OPEN_MAX            5
#define   _SC_JOB_CONTROL         6
#define   _SC_SAVED_IDS           7
#define   _SC_VERSION             8

asmlinkage long sunos_sysconf (int name)
{
	long ret;

	switch (name){
	case _SC_ARG_MAX:
		ret = ARG_MAX;
		break;
	case _SC_CHILD_MAX:
		ret = CHILD_MAX;
		break;
	case _SC_CLK_TCK:
		ret = HZ;
		break;
	case _SC_NGROUPS_MAX:
		ret = NGROUPS_MAX;
		break;
	case _SC_OPEN_MAX:
		ret = OPEN_MAX;
		break;
	case _SC_JOB_CONTROL:
		ret = 1;	/* yes, we do support job control */
		break;
	case _SC_SAVED_IDS:
		ret = 1;	/* yes, we do support saved uids  */
		break;
	case _SC_VERSION:
		/* mhm, POSIX_VERSION is in /usr/include/unistd.h
		 * should it go on /usr/include/linux?
		 */
		ret = 199009L; 
		break;
	default:
		ret = -1;
		break;
	};
	return ret;
}

asmlinkage int sunos_semsys(int op, unsigned long arg1, unsigned long arg2,
			    unsigned long arg3, void *ptr)
{
	union semun arg4;
	int ret;

	switch (op) {
	case 0:
		/* Most arguments match on a 1:1 basis but cmd doesn't */
		switch(arg3) {
		case 4:
			arg3=GETPID; break;
		case 5:
			arg3=GETVAL; break;
		case 6:
			arg3=GETALL; break;
		case 3:
			arg3=GETNCNT; break;
		case 7:
			arg3=GETZCNT; break;
		case 8:
			arg3=SETVAL; break;
		case 9:
			arg3=SETALL; break;
		}
		/* sys_semctl(): */
		/* value to modify semaphore to */
		arg4.__pad = (void __user *) ptr;
		ret = sys_semctl((int)arg1, (int)arg2, (int)arg3, arg4 );
		break;
	case 1:
		/* sys_semget(): */
		ret = sys_semget((key_t)arg1, (int)arg2, (int)arg3);
		break;
	case 2:
		/* sys_semop(): */
		ret = sys_semop((int)arg1, (struct sembuf __user *)arg2, (unsigned)arg3);
		break;
	default:
		ret = -EINVAL;
		break;
	};
	return ret;
}

asmlinkage int sunos_msgsys(int op, unsigned long arg1, unsigned long arg2,
			    unsigned long arg3, unsigned long arg4)
{
	struct sparc_stackf *sp;
	unsigned long arg5;
	int rval;

	switch(op) {
	case 0:
		rval = sys_msgget((key_t)arg1, (int)arg2);
		break;
	case 1:
		rval = sys_msgctl((int)arg1, (int)arg2,
				  (struct msqid_ds __user *)arg3);
		break;
	case 2:
		lock_kernel();
		sp = (struct sparc_stackf *)current->thread.kregs->u_regs[UREG_FP];
		arg5 = sp->xxargs[0];
		unlock_kernel();
		rval = sys_msgrcv((int)arg1, (struct msgbuf __user *)arg2,
				  (size_t)arg3, (long)arg4, (int)arg5);
		break;
	case 3:
		rval = sys_msgsnd((int)arg1, (struct msgbuf __user *)arg2,
				  (size_t)arg3, (int)arg4);
		break;
	default:
		rval = -EINVAL;
		break;
	}
	return rval;
}

asmlinkage int sunos_shmsys(int op, unsigned long arg1, unsigned long arg2,
			    unsigned long arg3)
{
	unsigned long raddr;
	int rval;

	switch(op) {
	case 0:
		/* do_shmat(): attach a shared memory area */
		rval = do_shmat((int)arg1,(char __user *)arg2,(int)arg3,&raddr);
		if (!rval)
			rval = (int) raddr;
		break;
	case 1:
		/* sys_shmctl(): modify shared memory area attr. */
		rval = sys_shmctl((int)arg1,(int)arg2,(struct shmid_ds __user *)arg3);
		break;
	case 2:
		/* sys_shmdt(): detach a shared memory area */
		rval = sys_shmdt((char __user *)arg1);
		break;
	case 3:
		/* sys_shmget(): get a shared memory area */
		rval = sys_shmget((key_t)arg1,(int)arg2,(int)arg3);
		break;
	default:
		rval = -EINVAL;
		break;
	};
	return rval;
}

#define SUNOS_EWOULDBLOCK 35

/* see the sunos man page read(2v) for an explanation
   of this garbage. We use O_NDELAY to mark
   file descriptors that have been set non-blocking 
   using 4.2BSD style calls. (tridge) */

static inline int check_nonblock(int ret, int fd)
{
	if (ret == -EAGAIN) {
		struct file * file = fget(fd);
		if (file) {
			if (file->f_flags & O_NDELAY)
				ret = -SUNOS_EWOULDBLOCK;
			fput(file);
		}
	}
	return ret;
}

asmlinkage int sunos_read(unsigned int fd, char __user *buf, int count)
{
	int ret;

	ret = check_nonblock(sys_read(fd,buf,count),fd);
	return ret;
}

asmlinkage int sunos_readv(unsigned long fd, const struct iovec __user *vector,
			   long count)
{
	int ret;

	ret = check_nonblock(sys_readv(fd,vector,count),fd);
	return ret;
}

asmlinkage int sunos_write(unsigned int fd, char __user *buf, int count)
{
	int ret;

	ret = check_nonblock(sys_write(fd,buf,count),fd);
	return ret;
}

asmlinkage int sunos_writev(unsigned long fd,
			    const struct iovec __user *vector, long count)
{
	int ret;

	ret = check_nonblock(sys_writev(fd,vector,count),fd);
	return ret;
}

asmlinkage int sunos_recv(int fd, void __user *ubuf, int size, unsigned flags)
{
	int ret;

	ret = check_nonblock(sys_recv(fd,ubuf,size,flags),fd);
	return ret;
}

asmlinkage int sunos_send(int fd, void __user *buff, int len, unsigned flags)
{
	int ret;

	ret = check_nonblock(sys_send(fd,buff,len,flags),fd);
	return ret;
}

asmlinkage int sunos_accept(int fd, struct sockaddr __user *sa,
			    int __user *addrlen)
{
	int ret;

	while (1) {
		ret = check_nonblock(sys_accept(fd,sa,addrlen),fd);	
		if (ret != -ENETUNREACH && ret != -EHOSTUNREACH)
			break;
	}

	return ret;
}

#define SUNOS_SV_INTERRUPT 2

asmlinkage int
sunos_sigaction(int sig, const struct old_sigaction __user *act,
		struct old_sigaction __user *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags))
			return -EFAULT;
		__get_user(mask, &act->sa_mask);
		new_ka.sa.sa_restorer = NULL;
		new_ka.ka_restorer = NULL;
		siginitset(&new_ka.sa.sa_mask, mask);
		new_ka.sa.sa_flags ^= SUNOS_SV_INTERRUPT;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		/* In the clone() case we could copy half consistent
		 * state to the user, however this could sleep and
		 * deadlock us if we held the signal lock on SMP.  So for
		 * now I take the easy way out and do no locking.
		 * But then again we don't support SunOS lwp's anyways ;-)
		 */
		old_ka.sa.sa_flags ^= SUNOS_SV_INTERRUPT;
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags))
			 return -EFAULT;
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}


asmlinkage int sunos_setsockopt(int fd, int level, int optname,
				char __user *optval, int optlen)
{
	int tr_opt = optname;
	int ret;

	if (level == SOL_IP) {
		/* Multicast socketopts (ttl, membership) */
		if (tr_opt >=2 && tr_opt <= 6)
			tr_opt += 30;
	}
	ret = sys_setsockopt(fd, level, tr_opt, optval, optlen);
	return ret;
}

asmlinkage int sunos_getsockopt(int fd, int level, int optname,
				char __user *optval, int __user *optlen)
{
	int tr_opt = optname;
	int ret;

	if (level == SOL_IP) {
		/* Multicast socketopts (ttl, membership) */
		if (tr_opt >=2 && tr_opt <= 6)
			tr_opt += 30;
	}
	ret = sys_getsockopt(fd, level, tr_opt, optval, optlen);
	return ret;
}
