/* $Id: ioctl.c,v 1.17 2002/02/08 03:57:14 davem Exp $
 * ioctl.c: Solaris ioctl emulation.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997,1998 Patrik Rak (prak3264@ss1000.ms.mff.cuni.cz)
 *
 * Streams & timod emulation based on code
 * Copyright (C) 1995, 1996 Mike Jagdis (jaggy@purplet.demon.co.uk)
 *
 * 1999-08-19 Implemented solaris 'm' (mag tape) and
 *            'O' (openprom) ioctls, by Jason Rappleye
 *             (rappleye@ccr.buffalo.edu)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/syscalls.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/netdevice.h>
#include <linux/mtio.h>
#include <linux/time.h>
#include <linux/rcupdate.h>
#include <linux/compat.h>

#include <net/sock.h>

#include <asm/uaccess.h>
#include <asm/termios.h>
#include <asm/openpromio.h>

#include "conv.h"
#include "socksys.h"

extern asmlinkage int compat_sys_ioctl(unsigned int fd, unsigned int cmd,
	u32 arg);
asmlinkage int solaris_ioctl(unsigned int fd, unsigned int cmd, u32 arg);

extern int timod_putmsg(unsigned int fd, char __user *ctl_buf, int ctl_len,
			char __user *data_buf, int data_len, int flags);
extern int timod_getmsg(unsigned int fd, char __user *ctl_buf, int ctl_maxlen, int __user *ctl_len,
			char __user *data_buf, int data_maxlen, int __user *data_len, int *flags);

/* termio* stuff {{{ */

struct solaris_termios {
	u32	c_iflag;
	u32	c_oflag;
	u32	c_cflag;
	u32	c_lflag;
	u8	c_cc[19];
};

struct solaris_termio {
	u16	c_iflag;
	u16	c_oflag;
	u16	c_cflag;
	u16	c_lflag;
	s8	c_line;
	u8	c_cc[8];
};

struct solaris_termiox {
	u16	x_hflag;
	u16	x_cflag;
	u16	x_rflag[5];
	u16	x_sflag;
};

static u32 solaris_to_linux_cflag(u32 cflag)
{
	cflag &= 0x7fdff000;
	if (cflag & 0x200000) {
		int baud = cflag & 0xf;
		cflag &= ~0x20000f;
		switch (baud) {
		case 0: baud = B57600; break;
		case 1: baud = B76800; break;
		case 2: baud = B115200; break;
		case 3: baud = B153600; break;
		case 4: baud = B230400; break;
		case 5: baud = B307200; break;
		case 6: baud = B460800; break;
		}
		cflag |= CBAUDEX | baud;
	}
	return cflag;
}

static u32 linux_to_solaris_cflag(u32 cflag)
{
	cflag &= ~(CMSPAR | CIBAUD);
	if (cflag & CBAUDEX) {
		int baud = cflag & CBAUD;
		cflag &= ~CBAUD;
		switch (baud) {
		case B57600: baud = 0; break;
		case B76800: baud = 1; break;
		case B115200: baud = 2; break;
		case B153600: baud = 3; break;
		case B230400: baud = 4; break;
		case B307200: baud = 5; break;
		case B460800: baud = 6; break;
		case B614400: baud = 7; break;
		case B921600: baud = 8; break;
#if 0		
		case B1843200: baud = 9; break;
#endif
		}
		cflag |= 0x200000 | baud;
	}
	return cflag;
}

static inline int linux_to_solaris_termio(unsigned int fd, unsigned int cmd, u32 arg)
{
	struct solaris_termio __user *p = A(arg);
	int ret;
	
	ret = sys_ioctl(fd, cmd, (unsigned long)p);
	if (!ret) {
		u32 cflag;
		
		if (__get_user (cflag, &p->c_cflag))
			return -EFAULT;
		cflag = linux_to_solaris_cflag(cflag);
		if (__put_user (cflag, &p->c_cflag))
			return -EFAULT;
	}
	return ret;
}

static int solaris_to_linux_termio(unsigned int fd, unsigned int cmd, u32 arg)
{
	int ret;
	struct solaris_termio s;
	mm_segment_t old_fs = get_fs();
	
	if (copy_from_user (&s, (struct solaris_termio __user *)A(arg), sizeof(struct solaris_termio)))
		return -EFAULT;
	s.c_cflag = solaris_to_linux_cflag(s.c_cflag);
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, cmd, (unsigned long)&s);
	set_fs(old_fs);
	return ret;
}

static inline int linux_to_solaris_termios(unsigned int fd, unsigned int cmd, u32 arg)
{
	int ret;
	struct solaris_termios s;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);	
	ret = sys_ioctl(fd, cmd, (unsigned long)&s);
	set_fs(old_fs);
	if (!ret) {
		struct solaris_termios __user *p = A(arg);
		if (put_user (s.c_iflag, &p->c_iflag) ||
		    __put_user (s.c_oflag, &p->c_oflag) ||
		    __put_user (linux_to_solaris_cflag(s.c_cflag), &p->c_cflag) ||
		    __put_user (s.c_lflag, &p->c_lflag) ||
		    __copy_to_user (p->c_cc, s.c_cc, 16) ||
		    __clear_user (p->c_cc + 16, 2))
			return -EFAULT;
	}
	return ret;
}

static int solaris_to_linux_termios(unsigned int fd, unsigned int cmd, u32 arg)
{
	int ret;
	struct solaris_termios s;
	struct solaris_termios __user *p = A(arg);
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, TCGETS, (unsigned long)&s);
	set_fs(old_fs);
	if (ret) return ret;
	if (put_user (s.c_iflag, &p->c_iflag) ||
	    __put_user (s.c_oflag, &p->c_oflag) ||
	    __put_user (s.c_cflag, &p->c_cflag) ||
	    __put_user (s.c_lflag, &p->c_lflag) ||
	    __copy_from_user (s.c_cc, p->c_cc, 16))
		return -EFAULT;
	s.c_cflag = solaris_to_linux_cflag(s.c_cflag);
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, cmd, (unsigned long)&s);
	set_fs(old_fs);
	return ret;
}

static inline int solaris_T(unsigned int fd, unsigned int cmd, u32 arg)
{
	switch (cmd & 0xff) {
	case 1: /* TCGETA */
		return linux_to_solaris_termio(fd, TCGETA, arg);
	case 2: /* TCSETA */
		return solaris_to_linux_termio(fd, TCSETA, arg);
	case 3: /* TCSETAW */
		return solaris_to_linux_termio(fd, TCSETAW, arg);
	case 4: /* TCSETAF */
		return solaris_to_linux_termio(fd, TCSETAF, arg);
	case 5: /* TCSBRK */
		return sys_ioctl(fd, TCSBRK, arg);
	case 6: /* TCXONC */
		return sys_ioctl(fd, TCXONC, arg);
	case 7: /* TCFLSH */
		return sys_ioctl(fd, TCFLSH, arg);
	case 13: /* TCGETS */
		return linux_to_solaris_termios(fd, TCGETS, arg);
	case 14: /* TCSETS */
		return solaris_to_linux_termios(fd, TCSETS, arg);
	case 15: /* TCSETSW */
		return solaris_to_linux_termios(fd, TCSETSW, arg);
	case 16: /* TCSETSF */
		return solaris_to_linux_termios(fd, TCSETSF, arg);
	case 103: /* TIOCSWINSZ */
		return sys_ioctl(fd, TIOCSWINSZ, arg);
	case 104: /* TIOCGWINSZ */
		return sys_ioctl(fd, TIOCGWINSZ, arg);
	}
	return -ENOSYS;
}

static inline int solaris_t(unsigned int fd, unsigned int cmd, u32 arg)
{
	switch (cmd & 0xff) {
	case 20: /* TIOCGPGRP */
		return sys_ioctl(fd, TIOCGPGRP, arg);
	case 21: /* TIOCSPGRP */
		return sys_ioctl(fd, TIOCSPGRP, arg);
	}
	return -ENOSYS;
}

/* }}} */

/* A pseudo STREAMS support {{{ */

struct strioctl {
	int cmd, timeout, len;
	u32 data;
};

struct solaris_si_sockparams {
	int sp_family;
	int sp_type;
	int sp_protocol;
};

struct solaris_o_si_udata {
	int tidusize;
	int addrsize;
	int optsize;
	int etsdusize;
	int servtype;
	int so_state;
	int so_options;
	int tsdusize;
};

struct solaris_si_udata {
	int tidusize;
	int addrsize;
	int optsize;
	int etsdusize;
	int servtype;
	int so_state;
	int so_options;
	int tsdusize;
	struct solaris_si_sockparams sockparams;
};

#define SOLARIS_MODULE_TIMOD    0
#define SOLARIS_MODULE_SOCKMOD  1
#define SOLARIS_MODULE_MAX      2

static struct module_info {
        const char *name;
        /* can be expanded further if needed */
} module_table[ SOLARIS_MODULE_MAX + 1 ] = {
        /* the ordering here must match the module numbers above! */
        { "timod" },
        { "sockmod" },
        { NULL }
};

static inline int solaris_sockmod(unsigned int fd, unsigned int cmd, u32 arg)
{
	struct inode *ino;
	struct fdtable *fdt;
	/* I wonder which of these tests are superfluous... --patrik */
	rcu_read_lock();
	fdt = files_fdtable(current->files);
	if (! fdt->fd[fd] ||
	    ! fdt->fd[fd]->f_path.dentry ||
	    ! (ino = fdt->fd[fd]->f_path.dentry->d_inode) ||
	    ! S_ISSOCK(ino->i_mode)) {
		rcu_read_unlock();
		return TBADF;
	}
	rcu_read_unlock();
	
	switch (cmd & 0xff) {
	case 109: /* SI_SOCKPARAMS */
	{
		struct solaris_si_sockparams si;
		if (copy_from_user (&si, A(arg), sizeof(si)))
			return (EFAULT << 8) | TSYSERR;

		/* Should we modify socket ino->socket_i.ops and type? */
		return 0;
	}
	case 110: /* SI_GETUDATA */
	{
		int etsdusize, servtype;
		struct solaris_si_udata __user *p = A(arg);
		switch (SOCKET_I(ino)->type) {
		case SOCK_STREAM:
			etsdusize = 1;
			servtype = 2;
			break;
		default:
			etsdusize = -2;
			servtype = 3;
			break;
		}
		if (put_user(16384, &p->tidusize) ||
		    __put_user(sizeof(struct sockaddr), &p->addrsize) ||
		    __put_user(-1, &p->optsize) ||
		    __put_user(etsdusize, &p->etsdusize) ||
		    __put_user(servtype, &p->servtype) ||
		    __put_user(0, &p->so_state) ||
		    __put_user(0, &p->so_options) ||
		    __put_user(16384, &p->tsdusize) ||
		    __put_user(SOCKET_I(ino)->ops->family, &p->sockparams.sp_family) ||
		    __put_user(SOCKET_I(ino)->type, &p->sockparams.sp_type) ||
		    __put_user(SOCKET_I(ino)->ops->family, &p->sockparams.sp_protocol))
			return (EFAULT << 8) | TSYSERR;
		return 0;
	}
	case 101: /* O_SI_GETUDATA */
	{
		int etsdusize, servtype;
		struct solaris_o_si_udata __user *p = A(arg);
		switch (SOCKET_I(ino)->type) {
		case SOCK_STREAM:
			etsdusize = 1;
			servtype = 2;
			break;
		default:
			etsdusize = -2;
			servtype = 3;
			break;
		}
		if (put_user(16384, &p->tidusize) ||
		    __put_user(sizeof(struct sockaddr), &p->addrsize) ||
		    __put_user(-1, &p->optsize) ||
		    __put_user(etsdusize, &p->etsdusize) ||
		    __put_user(servtype, &p->servtype) ||
		    __put_user(0, &p->so_state) ||
		    __put_user(0, &p->so_options) ||
		    __put_user(16384, &p->tsdusize))
			return (EFAULT << 8) | TSYSERR;
		return 0;
	}
	case 102: /* SI_SHUTDOWN */
	case 103: /* SI_LISTEN */
	case 104: /* SI_SETMYNAME */
	case 105: /* SI_SETPEERNAME */
	case 106: /* SI_GETINTRANSIT */
	case 107: /* SI_TCL_LINK */
	case 108: /* SI_TCL_UNLINK */
		;
	}
	return TNOTSUPPORT;
}

static inline int solaris_timod(unsigned int fd, unsigned int cmd, u32 arg,
                                    int len, int __user *len_p)
{
	int ret;
		
	switch (cmd & 0xff) {
	case 141: /* TI_OPTMGMT */
	{
		int i;
		u32 prim;
		SOLD("TI_OPMGMT entry");
		ret = timod_putmsg(fd, A(arg), len, NULL, -1, 0);
		SOLD("timod_putmsg() returned");
		if (ret)
			return (-ret << 8) | TSYSERR;
		i = MSG_HIPRI;
		SOLD("calling timod_getmsg()");
		ret = timod_getmsg(fd, A(arg), len, len_p, NULL, -1, NULL, &i);
		SOLD("timod_getmsg() returned");
		if (ret)
			return (-ret << 8) | TSYSERR;
		SOLD("ret ok");
		if (get_user(prim, (u32 __user *)A(arg)))
			return (EFAULT << 8) | TSYSERR;
		SOLD("got prim");
		if (prim == T_ERROR_ACK) {
			u32 tmp, tmp2;
			SOLD("prim is T_ERROR_ACK");
			if (get_user(tmp, (u32 __user *)A(arg)+3) ||
			    get_user(tmp2, (u32 __user *)A(arg)+2))
				return (EFAULT << 8) | TSYSERR;
			return (tmp2 << 8) | tmp;
		}
		SOLD("TI_OPMGMT return 0");
		return 0;
	}
	case 142: /* TI_BIND */
	{
		int i;
		u32 prim;
		SOLD("TI_BIND entry");
		ret = timod_putmsg(fd, A(arg), len, NULL, -1, 0);
		SOLD("timod_putmsg() returned");
		if (ret)
			return (-ret << 8) | TSYSERR;
		len = 1024; /* Solaris allows arbitrary return size */
		i = MSG_HIPRI;
		SOLD("calling timod_getmsg()");
		ret = timod_getmsg(fd, A(arg), len, len_p, NULL, -1, NULL, &i);
		SOLD("timod_getmsg() returned");
		if (ret)
			return (-ret << 8) | TSYSERR;
		SOLD("ret ok");
		if (get_user(prim, (u32 __user *)A(arg)))
			return (EFAULT << 8) | TSYSERR;
		SOLD("got prim");
		if (prim == T_ERROR_ACK) {
			u32 tmp, tmp2;
			SOLD("prim is T_ERROR_ACK");
			if (get_user(tmp, (u32 __user *)A(arg)+3) ||
			    get_user(tmp2, (u32 __user *)A(arg)+2))
				return (EFAULT << 8) | TSYSERR;
			return (tmp2 << 8) | tmp;
		}
		SOLD("no ERROR_ACK requested");
		if (prim != T_OK_ACK)
			return TBADSEQ;
		SOLD("OK_ACK requested");
		i = MSG_HIPRI;
		SOLD("calling timod_getmsg()");
		ret = timod_getmsg(fd, A(arg), len, len_p, NULL, -1, NULL, &i);
		SOLD("timod_getmsg() returned");
		if (ret)
			return (-ret << 8) | TSYSERR;
		SOLD("TI_BIND return ok");
		return 0;
	}
	case 140: /* TI_GETINFO */
	case 143: /* TI_UNBIND */
	case 144: /* TI_GETMYNAME */
	case 145: /* TI_GETPEERNAME */
	case 146: /* TI_SETMYNAME */
	case 147: /* TI_SETPEERNAME */
		;
	}
	return TNOTSUPPORT;
}

static inline int solaris_S(struct file *filp, unsigned int fd, unsigned int cmd, u32 arg)
{
	char *p;
	int ret;
	mm_segment_t old_fs;
	struct strioctl si;
	struct inode *ino;
        struct sol_socket_struct *sock;
        struct module_info *mi;

        ino = filp->f_path.dentry->d_inode;
        if (!S_ISSOCK(ino->i_mode))
		return -EBADF;
        sock = filp->private_data;
        if (! sock) {
                printk("solaris_S: NULL private_data\n");
                return -EBADF;
        }
        if (sock->magic != SOLARIS_SOCKET_MAGIC) {
                printk("solaris_S: invalid magic\n");
                return -EBADF;
        }
        

	switch (cmd & 0xff) {
	case 1: /* I_NREAD */
		return -ENOSYS;
	case 2: /* I_PUSH */
        {
		p = getname (A(arg));
		if (IS_ERR (p))
			return PTR_ERR(p);
                ret = -EINVAL;
                for (mi = module_table; mi->name; mi++) {
                        if (strcmp(mi->name, p) == 0) {
                                sol_module m;
                                if (sock->modcount >= MAX_NR_STREAM_MODULES) {
                                        ret = -ENXIO;
                                        break;
                                }
                                m = (sol_module) (mi - module_table);
                                sock->module[sock->modcount++] = m;
                                ret = 0;
                                break;
                        }
                }
		putname (p);
		return ret;
        }
	case 3: /* I_POP */
                if (sock->modcount <= 0) return -EINVAL;
                sock->modcount--;
		return 0;
        case 4: /* I_LOOK */
        {
        	const char *p;
                if (sock->modcount <= 0) return -EINVAL;
                p = module_table[(unsigned)sock->module[sock->modcount]].name;
                if (copy_to_user (A(arg), p, strlen(p)))
                	return -EFAULT;
                return 0;
        }
	case 5: /* I_FLUSH */
		return 0;
	case 8: /* I_STR */
		if (copy_from_user(&si, A(arg), sizeof(struct strioctl)))
			return -EFAULT;
                /* We ignore what module is actually at the top of stack. */
		switch ((si.cmd >> 8) & 0xff) {
		case 'I':
                        return solaris_sockmod(fd, si.cmd, si.data);
		case 'T':
                        return solaris_timod(fd, si.cmd, si.data, si.len,
				&((struct strioctl __user *)A(arg))->len);
		default:
			return solaris_ioctl(fd, si.cmd, si.data);
		}
	case 9: /* I_SETSIG */
		return sys_ioctl(fd, FIOSETOWN, current->pid);
	case 10: /* I_GETSIG */
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		sys_ioctl(fd, FIOGETOWN, (unsigned long)&ret);
		set_fs(old_fs);
		if (ret == current->pid) return 0x3ff;
		else return -EINVAL;
	case 11: /* I_FIND */
        {
                int i;
		p = getname (A(arg));
		if (IS_ERR (p))
			return PTR_ERR(p);
                ret = 0;
                for (i = 0; i < sock->modcount; i++) {
                        unsigned m = sock->module[i];
                        if (strcmp(module_table[m].name, p) == 0) {
                                ret = 1;
                                break;
                        } 
                }
		putname (p);
		return ret;
        }
	case 19: /* I_SWROPT */
	case 32: /* I_SETCLTIME */
		return 0;	/* Lie */
	}
	return -ENOSYS;
}

static inline int solaris_s(unsigned int fd, unsigned int cmd, u32 arg)
{
	switch (cmd & 0xff) {
	case 0: /* SIOCSHIWAT */
	case 2: /* SIOCSLOWAT */
		return 0; /* We don't support them */
	case 1: /* SIOCGHIWAT */
	case 3: /* SIOCGLOWAT */
		if (put_user (0, (u32 __user *)A(arg)))
			return -EFAULT;
		return 0; /* Lie */
	case 7: /* SIOCATMARK */
		return sys_ioctl(fd, SIOCATMARK, arg);
	case 8: /* SIOCSPGRP */
		return sys_ioctl(fd, SIOCSPGRP, arg);
	case 9: /* SIOCGPGRP */
		return sys_ioctl(fd, SIOCGPGRP, arg);
	}
	return -ENOSYS;
}

static inline int solaris_r(unsigned int fd, unsigned int cmd, u32 arg)
{
	switch (cmd & 0xff) {
	case 10: /* SIOCADDRT */
		return compat_sys_ioctl(fd, SIOCADDRT, arg);
	case 11: /* SIOCDELRT */
		return compat_sys_ioctl(fd, SIOCDELRT, arg);
	}
	return -ENOSYS;
}

static inline int solaris_i(unsigned int fd, unsigned int cmd, u32 arg)
{
	switch (cmd & 0xff) {
	case 12: /* SIOCSIFADDR */
		return compat_sys_ioctl(fd, SIOCSIFADDR, arg);
	case 13: /* SIOCGIFADDR */
		return compat_sys_ioctl(fd, SIOCGIFADDR, arg);
	case 14: /* SIOCSIFDSTADDR */
		return compat_sys_ioctl(fd, SIOCSIFDSTADDR, arg);
	case 15: /* SIOCGIFDSTADDR */
		return compat_sys_ioctl(fd, SIOCGIFDSTADDR, arg);
	case 16: /* SIOCSIFFLAGS */
		return compat_sys_ioctl(fd, SIOCSIFFLAGS, arg);
	case 17: /* SIOCGIFFLAGS */
		return compat_sys_ioctl(fd, SIOCGIFFLAGS, arg);
	case 18: /* SIOCSIFMEM */
		return compat_sys_ioctl(fd, SIOCSIFMEM, arg);
	case 19: /* SIOCGIFMEM */
		return compat_sys_ioctl(fd, SIOCGIFMEM, arg);
	case 20: /* SIOCGIFCONF */
		return compat_sys_ioctl(fd, SIOCGIFCONF, arg);
	case 21: /* SIOCSIFMTU */
		return compat_sys_ioctl(fd, SIOCSIFMTU, arg);
	case 22: /* SIOCGIFMTU */
		return compat_sys_ioctl(fd, SIOCGIFMTU, arg);
	case 23: /* SIOCGIFBRDADDR */
		return compat_sys_ioctl(fd, SIOCGIFBRDADDR, arg);
	case 24: /* SIOCSIFBRDADDR */
		return compat_sys_ioctl(fd, SIOCSIFBRDADDR, arg);
	case 25: /* SIOCGIFNETMASK */
		return compat_sys_ioctl(fd, SIOCGIFNETMASK, arg);
	case 26: /* SIOCSIFNETMASK */
		return compat_sys_ioctl(fd, SIOCSIFNETMASK, arg);
	case 27: /* SIOCGIFMETRIC */
		return compat_sys_ioctl(fd, SIOCGIFMETRIC, arg);
	case 28: /* SIOCSIFMETRIC */
		return compat_sys_ioctl(fd, SIOCSIFMETRIC, arg);
	case 30: /* SIOCSARP */
		return compat_sys_ioctl(fd, SIOCSARP, arg);
	case 31: /* SIOCGARP */
		return compat_sys_ioctl(fd, SIOCGARP, arg);
	case 32: /* SIOCDARP */
		return compat_sys_ioctl(fd, SIOCDARP, arg);
	case 52: /* SIOCGETNAME */
	case 53: /* SIOCGETPEER */
		{
			struct sockaddr uaddr;
			int uaddr_len = sizeof(struct sockaddr), ret;
			long args[3];
			mm_segment_t old_fs = get_fs();
			int (*sys_socketcall)(int, unsigned long *) =
				(int (*)(int, unsigned long *))SYS(socketcall);
			
			args[0] = fd; args[1] = (long)&uaddr; args[2] = (long)&uaddr_len;
			set_fs(KERNEL_DS);
			ret = sys_socketcall(((cmd & 0xff) == 52) ? SYS_GETSOCKNAME : SYS_GETPEERNAME,
					args);
			set_fs(old_fs);
			if (ret >= 0) {
				if (copy_to_user(A(arg), &uaddr, uaddr_len))
					return -EFAULT;
			}
			return ret;
		}
#if 0		
	case 86: /* SIOCSOCKSYS */
		return socksys_syscall(fd, arg);
#endif		
	case 87: /* SIOCGIFNUM */
		{
			struct net_device *d;
			int i = 0;
			
			read_lock_bh(&dev_base_lock);
			for (d = dev_base; d; d = d->next) i++;
			read_unlock_bh(&dev_base_lock);

			if (put_user (i, (int __user *)A(arg)))
				return -EFAULT;
			return 0;
		}
	}
	return -ENOSYS;
}

static int solaris_m(unsigned int fd, unsigned int cmd, u32 arg)
{
	int ret;

	switch (cmd & 0xff) {
	case 1: /* MTIOCTOP */
		ret = sys_ioctl(fd, MTIOCTOP, (unsigned long)&arg);
		break;
	case 2: /* MTIOCGET */
		ret = sys_ioctl(fd, MTIOCGET, (unsigned long)&arg);
		break;
	case 3: /* MTIOCGETDRIVETYPE */
	case 4: /* MTIOCPERSISTENT */
	case 5: /* MTIOCPERSISTENTSTATUS */
	case 6: /* MTIOCLRERR */
	case 7: /* MTIOCGUARANTEEDORDER */
	case 8: /* MTIOCRESERVE */
	case 9: /* MTIOCRELEASE */
	case 10: /* MTIOCFORCERESERVE */
	case 13: /* MTIOCSTATE */
	case 14: /* MTIOCREADIGNOREILI */
	case 15: /* MTIOCREADIGNOREEOFS */
	case 16: /* MTIOCSHORTFMK */
	default:
		ret = -ENOSYS; /* linux doesn't support these */
		break;
	};

	return ret;
}

static int solaris_O(unsigned int fd, unsigned int cmd, u32 arg)
{
	int ret = -EINVAL;

	switch (cmd & 0xff) {
	case 1: /* OPROMGETOPT */
		ret = sys_ioctl(fd, OPROMGETOPT, arg);
		break;
	case 2: /* OPROMSETOPT */
		ret = sys_ioctl(fd, OPROMSETOPT, arg);
		break;
	case 3: /* OPROMNXTOPT */
		ret = sys_ioctl(fd, OPROMNXTOPT, arg);
		break;
	case 4: /* OPROMSETOPT2 */
		ret = sys_ioctl(fd, OPROMSETOPT2, arg);
		break;
	case 5: /* OPROMNEXT */
		ret = sys_ioctl(fd, OPROMNEXT, arg);
		break;
	case 6: /* OPROMCHILD */
		ret = sys_ioctl(fd, OPROMCHILD, arg);
		break;
	case 7: /* OPROMGETPROP */
		ret = sys_ioctl(fd, OPROMGETPROP, arg);
		break;
	case 8: /* OPROMNXTPROP */
		ret = sys_ioctl(fd, OPROMNXTPROP, arg);
		break;
	case 9: /* OPROMU2P */
		ret = sys_ioctl(fd, OPROMU2P, arg);
		break;
	case 10: /* OPROMGETCONS */
		ret = sys_ioctl(fd, OPROMGETCONS, arg);
		break;
	case 11: /* OPROMGETFBNAME */
		ret = sys_ioctl(fd, OPROMGETFBNAME, arg);
		break;
	case 12: /* OPROMGETBOOTARGS */
		ret = sys_ioctl(fd, OPROMGETBOOTARGS, arg);
		break;
	case 13: /* OPROMGETVERSION */
	case 14: /* OPROMPATH2DRV */
	case 15: /* OPROMDEV2PROMNAME */
	case 16: /* OPROMPROM2DEVNAME */
	case 17: /* OPROMGETPROPLEN */
	default:
		ret = -EINVAL;
		break;
	};
	return ret;
}

/* }}} */

asmlinkage int solaris_ioctl(unsigned int fd, unsigned int cmd, u32 arg)
{
	struct file *filp;
	int error = -EBADF;

	filp = fget(fd);
	if (!filp)
		goto out;

	lock_kernel();
	error = -EFAULT;
	switch ((cmd >> 8) & 0xff) {
	case 'S': error = solaris_S(filp, fd, cmd, arg); break;
	case 'T': error = solaris_T(fd, cmd, arg); break;
	case 'i': error = solaris_i(fd, cmd, arg); break;
	case 'r': error = solaris_r(fd, cmd, arg); break;
	case 's': error = solaris_s(fd, cmd, arg); break;
	case 't': error = solaris_t(fd, cmd, arg); break;
	case 'f': error = sys_ioctl(fd, cmd, arg); break;
	case 'm': error = solaris_m(fd, cmd, arg); break;
	case 'O': error = solaris_O(fd, cmd, arg); break;
	default:
		error = -ENOSYS;
		break;
	}
	unlock_kernel();
	fput(filp);
out:
	if (error == -ENOSYS) {
		unsigned char c = cmd>>8;
		
		if (c < ' ' || c > 126) c = '.';
		printk("solaris_ioctl: Unknown cmd fd(%d) cmd(%08x '%c') arg(%08x)\n",
		       (int)fd, (unsigned int)cmd, c, (unsigned int)arg);
		error = -EINVAL;
	}
	return error;
}
