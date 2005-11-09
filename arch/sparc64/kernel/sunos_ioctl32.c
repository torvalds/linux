/* $Id: sunos_ioctl32.c,v 1.11 2000/07/30 23:12:24 davem Exp $
 * sunos_ioctl32.c: SunOS ioctl compatibility on sparc64.
 *
 * Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1995, 1996, 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/uaccess.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/termios.h>
#include <linux/ioctl.h>
#include <linux/route.h>
#include <linux/sockios.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/syscalls.h>
#include <linux/compat.h>

#define SUNOS_NR_OPEN	256

struct rtentry32 {
        u32   		rt_pad1;
        struct sockaddr rt_dst;         /* target address               */
        struct sockaddr rt_gateway;     /* gateway addr (RTF_GATEWAY)   */
        struct sockaddr rt_genmask;     /* target network mask (IP)     */
        unsigned short  rt_flags;
        short           rt_pad2;
        u32   		rt_pad3;
        unsigned char   rt_tos;
        unsigned char   rt_class;
        short           rt_pad4;
        short           rt_metric;      /* +1 for binary compatibility! */
        /* char * */ u32 rt_dev;        /* forcing the device at add    */
        u32   		rt_mtu;         /* per route MTU/Window         */
        u32   		rt_window;      /* Window clamping              */
        unsigned short  rt_irtt;        /* Initial RTT                  */

};

struct ifmap32 {
	u32 mem_start;
	u32 mem_end;
	unsigned short base_addr;
	unsigned char irq;
	unsigned char dma;
	unsigned char port;
};

struct ifreq32 {
#define IFHWADDRLEN     6
#define IFNAMSIZ        16
        union {
                char    ifrn_name[IFNAMSIZ];            /* if name, e.g. "en0" */
        } ifr_ifrn;
        union {
                struct  sockaddr ifru_addr;
                struct  sockaddr ifru_dstaddr;
                struct  sockaddr ifru_broadaddr;
                struct  sockaddr ifru_netmask;
                struct  sockaddr ifru_hwaddr;
                short   ifru_flags;
                int     ifru_ivalue;
                int     ifru_mtu;
                struct  ifmap32 ifru_map;
                char    ifru_slave[IFNAMSIZ];   /* Just fits the size */
                compat_caddr_t ifru_data;
        } ifr_ifru;
};

struct ifconf32 {
        int     ifc_len;                        /* size of buffer       */
        compat_caddr_t  ifcbuf;
};

extern asmlinkage int compat_sys_ioctl(unsigned int, unsigned int, u32);

asmlinkage int sunos_ioctl (int fd, u32 cmd, u32 arg)
{
	int ret = -EBADF;

	if(fd >= SUNOS_NR_OPEN)
		goto out;
	if(!fcheck(fd))
		goto out;

	if(cmd == TIOCSETD) {
		mm_segment_t old_fs = get_fs();
		int __user *p;
		int ntty = N_TTY;
		int tmp;

		p = (int __user *) (unsigned long) arg;
		ret = -EFAULT;
		if(get_user(tmp, p))
			goto out;
		if(tmp == 2) {
			set_fs(KERNEL_DS);
			ret = sys_ioctl(fd, cmd, (unsigned long) &ntty);
			set_fs(old_fs);
			ret = (ret == -EINVAL ? -EOPNOTSUPP : ret);
			goto out;
		}
	}
	if(cmd == TIOCNOTTY) {
		ret = sys_setsid();
		goto out;
	}
	switch(cmd) {
	case _IOW('r', 10, struct rtentry32):
		ret = compat_sys_ioctl(fd, SIOCADDRT, arg);
		goto out;
	case _IOW('r', 11, struct rtentry32):
		ret = compat_sys_ioctl(fd, SIOCDELRT, arg);
		goto out;

	case _IOW('i', 12, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCSIFADDR, arg);
		goto out;
	case _IOWR('i', 13, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCGIFADDR, arg);
		goto out;
	case _IOW('i', 14, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCSIFDSTADDR, arg);
		goto out;
	case _IOWR('i', 15, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCGIFDSTADDR, arg);
		goto out;
	case _IOW('i', 16, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCSIFFLAGS, arg);
		goto out;
	case _IOWR('i', 17, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCGIFFLAGS, arg);
		goto out;
	case _IOW('i', 18, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCSIFMEM, arg);
		goto out;
	case _IOWR('i', 19, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCGIFMEM, arg);
		goto out;

	case _IOWR('i', 20, struct ifconf32):
		ret = compat_sys_ioctl(fd, SIOCGIFCONF, arg);
		goto out;

	case _IOW('i', 21, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCSIFMTU, arg);
		goto out;

	case _IOWR('i', 22, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCGIFMTU, arg);
		goto out;

	case _IOWR('i', 23, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCGIFBRDADDR, arg);
		goto out;
	case _IOW('i', 24, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCSIFBRDADDR, arg);
		goto out;
	case _IOWR('i', 25, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCGIFNETMASK, arg);
		goto out;
	case _IOW('i', 26, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCSIFNETMASK, arg);
		goto out;
	case _IOWR('i', 27, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCGIFMETRIC, arg);
		goto out;
	case _IOW('i', 28, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCSIFMETRIC, arg);
		goto out;

	case _IOW('i', 30, struct arpreq):
		ret = compat_sys_ioctl(fd, SIOCSARP, arg);
		goto out;
	case _IOWR('i', 31, struct arpreq):
		ret = compat_sys_ioctl(fd, SIOCGARP, arg);
		goto out;
	case _IOW('i', 32, struct arpreq):
		ret = compat_sys_ioctl(fd, SIOCDARP, arg);
		goto out;

	case _IOW('i', 40, struct ifreq32): /* SIOCUPPER */
	case _IOW('i', 41, struct ifreq32): /* SIOCLOWER */
	case _IOW('i', 44, struct ifreq32): /* SIOCSETSYNC */
	case _IOW('i', 45, struct ifreq32): /* SIOCGETSYNC */
	case _IOW('i', 46, struct ifreq32): /* SIOCSSDSTATS */
	case _IOW('i', 47, struct ifreq32): /* SIOCSSESTATS */
	case _IOW('i', 48, struct ifreq32): /* SIOCSPROMISC */
		ret = -EOPNOTSUPP;
		goto out;

	case _IOW('i', 49, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCADDMULTI, arg);
		goto out;
	case _IOW('i', 50, struct ifreq32):
		ret = compat_sys_ioctl(fd, SIOCDELMULTI, arg);
		goto out;

	/* FDDI interface ioctls, unsupported. */
		
	case _IOW('i', 51, struct ifreq32): /* SIOCFDRESET */
	case _IOW('i', 52, struct ifreq32): /* SIOCFDSLEEP */
	case _IOW('i', 53, struct ifreq32): /* SIOCSTRTFMWAR */
	case _IOW('i', 54, struct ifreq32): /* SIOCLDNSTRTFW */
	case _IOW('i', 55, struct ifreq32): /* SIOCGETFDSTAT */
	case _IOW('i', 56, struct ifreq32): /* SIOCFDNMIINT */
	case _IOW('i', 57, struct ifreq32): /* SIOCFDEXUSER */
	case _IOW('i', 58, struct ifreq32): /* SIOCFDGNETMAP */
	case _IOW('i', 59, struct ifreq32): /* SIOCFDGIOCTL */
		printk("FDDI ioctl, returning EOPNOTSUPP\n");
		ret = -EOPNOTSUPP;
		goto out;

	case _IOW('t', 125, int):
		/* More stupid tty sunos ioctls, just
		 * say it worked.
		 */
		ret = 0;
		goto out;

	/* Non posix grp */
	case _IOW('t', 118, int): {
		int oldval, newval, __user *ptr;

		cmd = TIOCSPGRP;
		ptr = (int __user *) (unsigned long) arg;
		ret = -EFAULT;
		if(get_user(oldval, ptr))
			goto out;
		ret = compat_sys_ioctl(fd, cmd, arg);
		__get_user(newval, ptr);
		if(newval == -1) {
			__put_user(oldval, ptr);
			ret = -EIO;
		}
		if(ret == -ENOTTY)
			ret = -EIO;
		goto out;
	}

	case _IOR('t', 119, int): {
		int oldval, newval, __user *ptr;

		cmd = TIOCGPGRP;
		ptr = (int __user *) (unsigned long) arg;
		ret = -EFAULT;
		if(get_user(oldval, ptr))
			goto out;
		ret = compat_sys_ioctl(fd, cmd, arg);
		__get_user(newval, ptr);
		if(newval == -1) {
			__put_user(oldval, ptr);
			ret = -EIO;
		}
		if(ret == -ENOTTY)
			ret = -EIO;
		goto out;
	}
	};

	ret = compat_sys_ioctl(fd, cmd, arg);
	/* so stupid... */
	ret = (ret == -EINVAL ? -EOPNOTSUPP : ret);
out:
	return ret;
}
