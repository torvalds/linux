/*
 *    Implements HPUX syscalls.
 *
 *    Copyright (C) 1999 Matthew Wilcox <willy with parisc-linux.org>
 *    Copyright (C) 2000 Philipp Rumpf
 *    Copyright (C) 2000 John Marvin <jsm with parisc-linux.org>
 *    Copyright (C) 2000 Michael Ang <mang with subcarrier.org>
 *    Copyright (C) 2001 Nathan Neulinger <nneul at umr.edu>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/capability.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/syscalls.h>
#include <linux/utsname.h>
#include <linux/vfs.h>
#include <linux/vmalloc.h>

#include <asm/errno.h>
#include <asm/pgalloc.h>
#include <asm/uaccess.h>

unsigned long hpux_brk(unsigned long addr)
{
	/* Sigh.  Looks like HP/UX libc relies on kernel bugs. */
	return sys_brk(addr + PAGE_SIZE);
}

int hpux_sbrk(void)
{
	return -ENOSYS;
}

/* Random other syscalls */

int hpux_nice(int priority_change)
{
	return -ENOSYS;
}

int hpux_ptrace(void)
{
	return -ENOSYS;
}

int hpux_wait(int *stat_loc)
{
	return sys_waitpid(-1, stat_loc, 0);
}

int hpux_setpgrp(void)
{
	return sys_setpgid(0,0);
}

int hpux_setpgrp3(void)
{
	return hpux_setpgrp();
}

#define _SC_CPU_VERSION	10001
#define _SC_OPEN_MAX	4
#define CPU_PA_RISC1_1	0x210

int hpux_sysconf(int which)
{
	switch (which) {
	case _SC_CPU_VERSION:
		return CPU_PA_RISC1_1;
	case _SC_OPEN_MAX:
		return INT_MAX;
	default:
		return -EINVAL;
	}
}

/*****************************************************************************/

#define HPUX_UTSLEN 9
#define HPUX_SNLEN 15

struct hpux_utsname {
	char sysname[HPUX_UTSLEN];
	char nodename[HPUX_UTSLEN];
	char release[HPUX_UTSLEN];
	char version[HPUX_UTSLEN];
	char machine[HPUX_UTSLEN];
	char idnumber[HPUX_SNLEN];
} ;

struct hpux_ustat {
	int32_t		f_tfree;	/* total free (daddr_t)  */
	u_int32_t	f_tinode;	/* total inodes free (ino_t)  */
	char		f_fname[6];	/* filsys name */
	char		f_fpack[6];	/* filsys pack name */
	u_int32_t	f_blksize;	/* filsys block size (int) */
};

/*
 * HPUX's utssys() call.  It's a collection of miscellaneous functions,
 * alas, so there's no nice way of splitting them up.
 */

/*  This function is called from hpux_utssys(); HP-UX implements
 *  ustat() as an option to utssys().
 *
 *  Now, struct ustat on HP-UX is exactly the same as on Linux, except
 *  that it contains one addition field on the end, int32_t f_blksize.
 *  So, we could have written this function to just call the Linux
 *  sys_ustat(), (defined in linux/fs/super.c), and then just
 *  added this additional field to the user's structure.  But I figure
 *  if we're gonna be digging through filesystem structures to get
 *  this, we might as well just do the whole enchilada all in one go.
 *
 *  So, most of this function is almost identical to sys_ustat().
 *  I have placed comments at the few lines changed or added, to
 *  aid in porting forward if and when sys_ustat() is changed from
 *  its form in kernel 2.2.5.
 */
static int hpux_ustat(dev_t dev, struct hpux_ustat __user *ubuf)
{
	struct super_block *s;
	struct hpux_ustat tmp;  /* Changed to hpux_ustat */
	struct kstatfs sbuf;
	int err = -EINVAL;

	s = user_get_super(dev);
	if (s == NULL)
		goto out;
	err = vfs_statfs(s->s_root, &sbuf);
	drop_super(s);
	if (err)
		goto out;

	memset(&tmp,0,sizeof(tmp));

	tmp.f_tfree = (int32_t)sbuf.f_bfree;
	tmp.f_tinode = (u_int32_t)sbuf.f_ffree;
	tmp.f_blksize = (u_int32_t)sbuf.f_bsize;  /*  Added this line  */

	err = copy_to_user(ubuf, &tmp, sizeof(tmp)) ? -EFAULT : 0;
out:
	return err;
}

/*
 * Wrapper for hpux statfs call. At the moment, just calls the linux native one
 * and ignores the extra fields at the end of the hpux statfs struct.
 *
 */

typedef int32_t hpux_fsid_t[2];              /* file system ID type */
typedef uint16_t hpux_site_t;

struct hpux_statfs {
     int32_t f_type;                    /* type of info, zero for now */
     int32_t f_bsize;                   /* fundamental file system block size */
     int32_t f_blocks;                  /* total blocks in file system */
     int32_t f_bfree;                   /* free block in fs */
     int32_t f_bavail;                  /* free blocks avail to non-superuser */
     int32_t f_files;                   /* total file nodes in file system */
     int32_t f_ffree;                   /* free file nodes in fs */
     hpux_fsid_t  f_fsid;                    /* file system ID */
     int32_t f_magic;                   /* file system magic number */
     int32_t f_featurebits;             /* file system features */
     int32_t f_spare[4];                /* spare for later */
     hpux_site_t  f_cnode;                   /* cluster node where mounted */
     int16_t f_pad;
};

static int vfs_statfs_hpux(struct dentry *dentry, struct hpux_statfs *buf)
{
	struct kstatfs st;
	int retval;
	
	retval = vfs_statfs(dentry, &st);
	if (retval)
		return retval;

	memset(buf, 0, sizeof(*buf));
	buf->f_type = st.f_type;
	buf->f_bsize = st.f_bsize;
	buf->f_blocks = st.f_blocks;
	buf->f_bfree = st.f_bfree;
	buf->f_bavail = st.f_bavail;
	buf->f_files = st.f_files;
	buf->f_ffree = st.f_ffree;
	buf->f_fsid[0] = st.f_fsid.val[0];
	buf->f_fsid[1] = st.f_fsid.val[1];

	return 0;
}

/* hpux statfs */
asmlinkage long hpux_statfs(const char __user *path,
						struct hpux_statfs __user *buf)
{
	struct nameidata nd;
	int error;

	error = user_path_walk(path, &nd);
	if (!error) {
		struct hpux_statfs tmp;
		error = vfs_statfs_hpux(nd.dentry, &tmp);
		if (!error && copy_to_user(buf, &tmp, sizeof(tmp)))
			error = -EFAULT;
		path_release(&nd);
	}
	return error;
}

asmlinkage long hpux_fstatfs(unsigned int fd, struct hpux_statfs __user * buf)
{
	struct file *file;
	struct hpux_statfs tmp;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;
	error = vfs_statfs_hpux(file->f_dentry, &tmp);
	if (!error && copy_to_user(buf, &tmp, sizeof(tmp)))
		error = -EFAULT;
	fput(file);
 out:
	return error;
}


/*  This function is called from hpux_utssys(); HP-UX implements
 *  uname() as an option to utssys().
 *
 *  The form of this function is pretty much copied from sys_olduname(),
 *  defined in linux/arch/i386/kernel/sys_i386.c.
 */
/*  TODO: Are these put_user calls OK?  Should they pass an int?
 *        (I copied it from sys_i386.c like this.)
 */
static int hpux_uname(struct hpux_utsname *name)
{
	int error;

	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct hpux_utsname)))
		return -EFAULT;

	down_read(&uts_sem);

	error = __copy_to_user(&name->sysname,&system_utsname.sysname,HPUX_UTSLEN-1);
	error |= __put_user(0,name->sysname+HPUX_UTSLEN-1);
	error |= __copy_to_user(&name->nodename,&system_utsname.nodename,HPUX_UTSLEN-1);
	error |= __put_user(0,name->nodename+HPUX_UTSLEN-1);
	error |= __copy_to_user(&name->release,&system_utsname.release,HPUX_UTSLEN-1);
	error |= __put_user(0,name->release+HPUX_UTSLEN-1);
	error |= __copy_to_user(&name->version,&system_utsname.version,HPUX_UTSLEN-1);
	error |= __put_user(0,name->version+HPUX_UTSLEN-1);
	error |= __copy_to_user(&name->machine,&system_utsname.machine,HPUX_UTSLEN-1);
	error |= __put_user(0,name->machine+HPUX_UTSLEN-1);

	up_read(&uts_sem);

	/*  HP-UX  utsname has no domainname field.  */

	/*  TODO:  Implement idnumber!!!  */
#if 0
	error |= __put_user(0,name->idnumber);
	error |= __put_user(0,name->idnumber+HPUX_SNLEN-1);
#endif

	error = error ? -EFAULT : 0;

	return error;
}

/*  Note: HP-UX just uses the old suser() function to check perms
 *  in this system call.  We'll use capable(CAP_SYS_ADMIN).
 */
int hpux_utssys(char *ubuf, int n, int type)
{
	int len;
	int error;
	switch( type ) {
	case 0:
		/*  uname():  */
		return( hpux_uname( (struct hpux_utsname *)ubuf ) );
		break ;
	case 1:
		/*  Obsolete (used to be umask().)  */
		return -EFAULT ;
		break ;
	case 2:
		/*  ustat():  */
		return( hpux_ustat(new_decode_dev(n), (struct hpux_ustat *)ubuf) );
		break ;
	case 3:
		/*  setuname():
		 *
		 *  On linux (unlike HP-UX), utsname.nodename
		 *  is the same as the hostname.
		 *
		 *  sys_sethostname() is defined in linux/kernel/sys.c.
		 */
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		/*  Unlike Linux, HP-UX returns an error if n==0:  */
		if ( n <= 0 )
			return -EINVAL ;
		/*  Unlike Linux, HP-UX truncates it if n is too big:  */
		len = (n <= __NEW_UTS_LEN) ? n : __NEW_UTS_LEN ;
		return( sys_sethostname(ubuf, len) );
		break ;
	case 4:
		/*  sethostname():
		 *
		 *  sys_sethostname() is defined in linux/kernel/sys.c.
		 */
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		/*  Unlike Linux, HP-UX returns an error if n==0:  */
		if ( n <= 0 )
			return -EINVAL ;
		/*  Unlike Linux, HP-UX truncates it if n is too big:  */
		len = (n <= __NEW_UTS_LEN) ? n : __NEW_UTS_LEN ;
		return( sys_sethostname(ubuf, len) );
		break ;
	case 5:
		/*  gethostname():
		 *
		 *  sys_gethostname() is defined in linux/kernel/sys.c.
		 */
		/*  Unlike Linux, HP-UX returns an error if n==0:  */
		if ( n <= 0 )
			return -EINVAL ;
		return( sys_gethostname(ubuf, n) );
		break ;
	case 6:
		/*  Supposedly called from setuname() in libc.
		 *  TODO: When and why is this called?
		 *        Is it ever even called?
		 *
		 *  This code should look a lot like sys_sethostname(),
		 *  defined in linux/kernel/sys.c.  If that gets updated,
		 *  update this code similarly.
		 */
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		/*  Unlike Linux, HP-UX returns an error if n==0:  */
		if ( n <= 0 )
			return -EINVAL ;
		/*  Unlike Linux, HP-UX truncates it if n is too big:  */
		len = (n <= __NEW_UTS_LEN) ? n : __NEW_UTS_LEN ;
		/**/
		/*  TODO:  print a warning about using this?  */
		down_write(&uts_sem);
		error = -EFAULT;
		if (!copy_from_user(system_utsname.sysname, ubuf, len)) {
			system_utsname.sysname[len] = 0;
			error = 0;
		}
		up_write(&uts_sem);
		return error;
		break ;
	case 7:
		/*  Sets utsname.release, if you're allowed.
		 *  Undocumented.  Used by swinstall to change the
		 *  OS version, during OS updates.  Yuck!!!
		 *
		 *  This code should look a lot like sys_sethostname()
		 *  in linux/kernel/sys.c.  If that gets updated, update
		 *  this code similarly.
		 */
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		/*  Unlike Linux, HP-UX returns an error if n==0:  */
		if ( n <= 0 )
			return -EINVAL ;
		/*  Unlike Linux, HP-UX truncates it if n is too big:  */
		len = (n <= __NEW_UTS_LEN) ? n : __NEW_UTS_LEN ;
		/**/
		/*  TODO:  print a warning about this?  */
		down_write(&uts_sem);
		error = -EFAULT;
		if (!copy_from_user(system_utsname.release, ubuf, len)) {
			system_utsname.release[len] = 0;
			error = 0;
		}
		up_write(&uts_sem);
		return error;
		break ;
	default:
		/*  This system call returns -EFAULT if given an unknown type.
	 	 *  Why not -EINVAL?  I don't know, it's just not what they did.
	 	 */
		return -EFAULT ;
	}
}

int hpux_getdomainname(char *name, int len)
{
 	int nlen;
 	int err = -EFAULT;
 	
 	down_read(&uts_sem);
 	
	nlen = strlen(system_utsname.domainname) + 1;

	if (nlen < len)
		len = nlen;
	if(len > __NEW_UTS_LEN)
		goto done;
	if(copy_to_user(name, system_utsname.domainname, len))
		goto done;
	err = 0;
done:
	up_read(&uts_sem);
	return err;
	
}

int hpux_pipe(int *kstack_fildes)
{
	int error;

	lock_kernel();
	error = do_pipe(kstack_fildes);
	unlock_kernel();
	return error;
}

/* lies - says it works, but it really didn't lock anything */
int hpux_lockf(int fildes, int function, off_t size)
{
	return 0;
}

int hpux_sysfs(int opcode, unsigned long arg1, unsigned long arg2)
{
	char *fsname = NULL;
	int len = 0;
	int fstype;

/*Unimplemented HP-UX syscall emulation. Syscall #334 (sysfs)
  Args: 1 80057bf4 0 400179f0 0 0 0 */
	printk(KERN_DEBUG "in hpux_sysfs\n");
	printk(KERN_DEBUG "hpux_sysfs called with opcode = %d\n", opcode);
	printk(KERN_DEBUG "hpux_sysfs called with arg1='%lx'\n", arg1);

	if ( opcode == 1 ) { /* GETFSIND */	
		len = strlen_user((char *)arg1);
		printk(KERN_DEBUG "len of arg1 = %d\n", len);
		if (len == 0)
			return 0;
		fsname = (char *) kmalloc(len, GFP_KERNEL);
		if ( !fsname ) {
			printk(KERN_DEBUG "failed to kmalloc fsname\n");
			return 0;
		}

		if ( copy_from_user(fsname, (char *)arg1, len) ) {
			printk(KERN_DEBUG "failed to copy_from_user fsname\n");
			kfree(fsname);
			return 0;
		}

		/* String could be altered by userspace after strlen_user() */
		fsname[len] = '\0';

		printk(KERN_DEBUG "that is '%s' as (char *)\n", fsname);
		if ( !strcmp(fsname, "hfs") ) {
			fstype = 0;
		} else {
			fstype = 0;
		};

		kfree(fsname);

		printk(KERN_DEBUG "returning fstype=%d\n", fstype);
		return fstype; /* something other than default */
	}


	return 0;
}


/* Table of syscall names and handle for unimplemented routines */
static const char *syscall_names[] = {
	"nosys",                  /* 0 */
	"exit",                  
	"fork",                  
	"read",                  
	"write",                 
	"open",                   /* 5 */
	"close",                 
	"wait",                  
	"creat",                 
	"link",                  
	"unlink",                 /* 10 */
	"execv",                 
	"chdir",                 
	"time",                  
	"mknod",                 
	"chmod",                  /* 15 */
	"chown",                 
	"brk",                   
	"lchmod",                
	"lseek",                 
	"getpid",                 /* 20 */
	"mount",                 
	"umount",                
	"setuid",                
	"getuid",                
	"stime",                  /* 25 */
	"ptrace",                
	"alarm",                 
	NULL,                    
	"pause",                 
	"utime",                  /* 30 */
	"stty",                  
	"gtty",                  
	"access",                
	"nice",                  
	"ftime",                  /* 35 */
	"sync",                  
	"kill",                  
	"stat",                  
	"setpgrp3",              
	"lstat",                  /* 40 */
	"dup",                   
	"pipe",                  
	"times",                 
	"profil",                
	"ki_call",                /* 45 */
	"setgid",                
	"getgid",                
	NULL,                    
	NULL,                    
	NULL,                     /* 50 */
	"acct",                  
	"set_userthreadid",      
	NULL,                    
	"ioctl",                 
	"reboot",                 /* 55 */
	"symlink",               
	"utssys",                
	"readlink",              
	"execve",                
	"umask",                  /* 60 */
	"chroot",                
	"fcntl",                 
	"ulimit",                
	NULL,                    
	NULL,                     /* 65 */
	"vfork",                 
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                     /* 70 */
	"mmap",                  
	NULL,                    
	"munmap",                
	"mprotect",              
	"madvise",                /* 75 */
	"vhangup",               
	"swapoff",               
	NULL,                    
	"getgroups",             
	"setgroups",              /* 80 */
	"getpgrp2",              
	"setpgid/setpgrp2",      
	"setitimer",             
	"wait3",                 
	"swapon",                 /* 85 */
	"getitimer",             
	NULL,                    
	NULL,                    
	NULL,                    
	"dup2",                   /* 90 */
	NULL,                    
	"fstat",                 
	"select",                
	NULL,                    
	"fsync",                  /* 95 */
	"setpriority",           
	NULL,                    
	NULL,                    
	NULL,                    
	"getpriority",            /* 100 */
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                     /* 105 */
	NULL,                    
	NULL,                    
	"sigvector",             
	"sigblock",              
	"sigsetmask",             /* 110 */
	"sigpause",              
	"sigstack",              
	NULL,                    
	NULL,                    
	NULL,                     /* 115 */
	"gettimeofday",          
	"getrusage",             
	NULL,                    
	NULL,                    
	"readv",                  /* 120 */
	"writev",                
	"settimeofday",          
	"fchown",                
	"fchmod",                
	NULL,                     /* 125 */
	"setresuid",             
	"setresgid",             
	"rename",                
	"truncate",              
	"ftruncate",              /* 130 */
	NULL,                    
	"sysconf",               
	NULL,                    
	NULL,                    
	NULL,                     /* 135 */
	"mkdir",                 
	"rmdir",                 
	NULL,                    
	"sigcleanup",            
	"setcore",                /* 140 */
	NULL,                    
	"gethostid",             
	"sethostid",             
	"getrlimit",             
	"setrlimit",              /* 145 */
	NULL,                    
	NULL,                    
	"quotactl",              
	"get_sysinfo",           
	NULL,                     /* 150 */
	"privgrp",               
	"rtprio",                
	"plock",                 
	NULL,                    
	"lockf",                  /* 155 */
	"semget",                
	NULL,                    
	"semop",                 
	"msgget",                
	NULL,                     /* 160 */
	"msgsnd",                
	"msgrcv",                
	"shmget",                
	NULL,                    
	"shmat",                  /* 165 */
	"shmdt",                 
	NULL,                    
	"csp/nsp_init",          
	"cluster",               
	"mkrnod",                 /* 170 */
	"test",                  
	"unsp_open",             
	NULL,                    
	"getcontext",            
	"osetcontext",            /* 175 */
	"bigio",                 
	"pipenode",              
	"lsync",                 
	"getmachineid",          
	"cnodeid/mysite",         /* 180 */
	"cnodes/sitels",         
	"swapclients",           
	"rmtprocess",            
	"dskless_stats",         
	"sigprocmask",            /* 185 */
	"sigpending",            
	"sigsuspend",            
	"sigaction",             
	NULL,                    
	"nfssvc",                 /* 190 */
	"getfh",                 
	"getdomainname",         
	"setdomainname",         
	"async_daemon",          
	"getdirentries",          /* 195 */
	NULL,                
	NULL,               
	"vfsmount",              
	NULL,                    
	"waitpid",                /* 200 */
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                     /* 205 */
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                     /* 210 */
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                     /* 215 */
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                     /* 220 */
	NULL,                    
	NULL,                    
	NULL,                    
	"sigsetreturn",          
	"sigsetstatemask",        /* 225 */
	"bfactl",                
	"cs",                    
	"cds",                   
	NULL,                    
	"pathconf",               /* 230 */
	"fpathconf",             
	NULL,                    
	NULL,                    
	"nfs_fcntl",             
	"ogetacl",                /* 235 */
	"ofgetacl",              
	"osetacl",               
	"ofsetacl",              
	"pstat",                 
	"getaudid",               /* 240 */
	"setaudid",              
	"getaudproc",            
	"setaudproc",            
	"getevent",              
	"setevent",               /* 245 */
	"audwrite",              
	"audswitch",             
	"audctl",                
	"ogetaccess",            
	"fsctl",                  /* 250 */
	"ulconnect",             
	"ulcontrol",             
	"ulcreate",              
	"uldest",                
	"ulrecv",                 /* 255 */
	"ulrecvcn",              
	"ulsend",                
	"ulshutdown",            
	"swapfs",                
	"fss",                    /* 260 */
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                     /* 265 */
	NULL,                    
	"tsync",                 
	"getnumfds",             
	"poll",                  
	"getmsg",                 /* 270 */
	"putmsg",                
	"fchdir",                
	"getmount_cnt",          
	"getmount_entry",        
	"accept",                 /* 275 */
	"bind",                  
	"connect",               
	"getpeername",           
	"getsockname",           
	"getsockopt",             /* 280 */
	"listen",                
	"recv",                  
	"recvfrom",              
	"recvmsg",               
	"send",                   /* 285 */
	"sendmsg",               
	"sendto",                
	"setsockopt",            
	"shutdown",              
	"socket",                 /* 290 */
	"socketpair",            
	"proc_open",             
	"proc_close",            
	"proc_send",             
	"proc_recv",              /* 295 */
	"proc_sendrecv",         
	"proc_syscall",          
	"ipccreate",             
	"ipcname",               
	"ipcnamerase",            /* 300 */
	"ipclookup",             
	"ipcselect",             
	"ipcconnect",            
	"ipcrecvcn",             
	"ipcsend",                /* 305 */
	"ipcrecv",               
	"ipcgetnodename",        
	"ipcsetnodename",        
	"ipccontrol",            
	"ipcshutdown",            /* 310 */
	"ipcdest",               
	"semctl",                
	"msgctl",                
	"shmctl",                
	"mpctl",                  /* 315 */
	"exportfs",              
	"getpmsg",               
	"putpmsg",               
	"strioctl",              
	"msync",                  /* 320 */
	"msleep",                
	"mwakeup",               
	"msem_init",             
	"msem_remove",           
	"adjtime",                /* 325 */
	"kload",                 
	"fattach",               
	"fdetach",               
	"serialize",             
	"statvfs",                /* 330 */
	"fstatvfs",              
	"lchown",                
	"getsid",                
	"sysfs",                 
	NULL,                     /* 335 */
	NULL,                    
	"sched_setparam",        
	"sched_getparam",        
	"sched_setscheduler",    
	"sched_getscheduler",     /* 340 */
	"sched_yield",           
	"sched_get_priority_max",
	"sched_get_priority_min",
	"sched_rr_get_interval", 
	"clock_settime",          /* 345 */
	"clock_gettime",         
	"clock_getres",          
	"timer_create",          
	"timer_delete",          
	"timer_settime",          /* 350 */
	"timer_gettime",         
	"timer_getoverrun",      
	"nanosleep",             
	"toolbox",               
	NULL,                     /* 355 */
	"getdents",              
	"getcontext",            
	"sysinfo",               
	"fcntl64",               
	"ftruncate64",            /* 360 */
	"fstat64",               
	"getdirentries64",       
	"getrlimit64",           
	"lockf64",               
	"lseek64",                /* 365 */
	"lstat64",               
	"mmap64",                
	"setrlimit64",           
	"stat64",                
	"truncate64",             /* 370 */
	"ulimit64",              
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                     /* 375 */
	NULL,                    
	NULL,                    
	NULL,                    
	NULL,                    
	"setcontext",             /* 380 */
	"sigaltstack",           
	"waitid",                
	"setpgrp",               
	"recvmsg2",              
	"sendmsg2",               /* 385 */
	"socket2",               
	"socketpair2",           
	"setregid",              
	"lwp_create",            
	"lwp_terminate",          /* 390 */
	"lwp_wait",              
	"lwp_suspend",           
	"lwp_resume",            
	"lwp_self",              
	"lwp_abort_syscall",      /* 395 */
	"lwp_info",              
	"lwp_kill",              
	"ksleep",                
	"kwakeup",               
	"ksleep_abort",           /* 400 */
	"lwp_proc_info",         
	"lwp_exit",              
	"lwp_continue",          
	"getacl",                
	"fgetacl",                /* 405 */
	"setacl",                
	"fsetacl",               
	"getaccess",             
	"lwp_mutex_init",        
	"lwp_mutex_lock_sys",     /* 410 */
	"lwp_mutex_unlock",      
	"lwp_cond_init",         
	"lwp_cond_signal",       
	"lwp_cond_broadcast",    
	"lwp_cond_wait_sys",      /* 415 */
	"lwp_getscheduler",      
	"lwp_setscheduler",      
	"lwp_getprivate",        
	"lwp_setprivate",        
	"lwp_detach",             /* 420 */
	"mlock",                 
	"munlock",               
	"mlockall",              
	"munlockall",            
	"shm_open",               /* 425 */
	"shm_unlink",            
	"sigqueue",              
	"sigwaitinfo",           
	"sigtimedwait",          
	"sigwait",                /* 430 */
	"aio_read",              
	"aio_write",             
	"lio_listio",            
	"aio_error",             
	"aio_return",             /* 435 */
	"aio_cancel",            
	"aio_suspend",           
	"aio_fsync",             
	"mq_open",               
	"mq_unlink",              /* 440 */
	"mq_send",               
	"mq_receive",            
	"mq_notify",             
	"mq_setattr",            
	"mq_getattr",             /* 445 */
	"ksem_open",             
	"ksem_unlink",           
	"ksem_close",            
	"ksem_destroy",          
	"lw_sem_incr",            /* 450 */
	"lw_sem_decr",           
	"lw_sem_read",           
	"mq_close",              
};
static const int syscall_names_max = 453;

int
hpux_unimplemented(unsigned long arg1,unsigned long arg2,unsigned long arg3,
		   unsigned long arg4,unsigned long arg5,unsigned long arg6,
		   unsigned long arg7,unsigned long sc_num)
{
	/* NOTE: sc_num trashes arg8 for the few syscalls that actually
	 * have a valid 8th argument.
	 */
	const char *name = NULL;
	if ( sc_num <= syscall_names_max && sc_num >= 0 ) {
		name = syscall_names[sc_num];
	}

	if ( name ) {
		printk(KERN_DEBUG "Unimplemented HP-UX syscall emulation. Syscall #%lu (%s)\n",
		sc_num, name);
	} else {
		printk(KERN_DEBUG "Unimplemented unknown HP-UX syscall emulation. Syscall #%lu\n",
		sc_num);
	}
	
	printk(KERN_DEBUG "  Args: %lx %lx %lx %lx %lx %lx %lx\n",
		arg1, arg2, arg3, arg4, arg5, arg6, arg7);

	return -ENOSYS;
}
