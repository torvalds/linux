/* $Id: sunos_ioctl.c,v 1.34 2000/09/03 14:10:56 anton Exp $
 * sunos_ioctl.c: The Linux Operating system: SunOS ioctl compatibility.
 * 
 * Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
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
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <asm/kbio.h>

#if 0
extern char sunkbd_type;
extern char sunkbd_layout;
#endif

/* NR_OPEN is now larger and dynamic in recent kernels. */
#define SUNOS_NR_OPEN	256

asmlinkage int sunos_ioctl (int fd, unsigned long cmd, unsigned long arg)
{
	int ret = -EBADF;

	if (fd >= SUNOS_NR_OPEN || !fcheck(fd))
		goto out;

	/* First handle an easy compat. case for tty ldisc. */
	if (cmd == TIOCSETD) {
		int __user *p;
		int ntty = N_TTY, tmp;
		mm_segment_t oldfs;

		p = (int __user *) arg;
		ret = -EFAULT;
		if (get_user(tmp, p))
			goto out;
		if (tmp == 2) {
			oldfs = get_fs();
			set_fs(KERNEL_DS);
			ret = sys_ioctl(fd, cmd, (unsigned long) &ntty);
			set_fs(oldfs);
			ret = (ret == -EINVAL ? -EOPNOTSUPP : ret);
			goto out;
		}
	}

	/* Binary compatibility is good American knowhow fuckin' up. */
	if (cmd == TIOCNOTTY) {
		ret = sys_setsid();
		goto out;
	}

	/* SunOS networking ioctls. */
	switch (cmd) {
	case _IOW('r', 10, struct rtentry):
		ret = sys_ioctl(fd, SIOCADDRT, arg);
		goto out;
	case _IOW('r', 11, struct rtentry):
		ret = sys_ioctl(fd, SIOCDELRT, arg);
		goto out;
	case _IOW('i', 12, struct ifreq):
		ret = sys_ioctl(fd, SIOCSIFADDR, arg);
		goto out;
	case _IOWR('i', 13, struct ifreq):
		ret = sys_ioctl(fd, SIOCGIFADDR, arg);
		goto out;
	case _IOW('i', 14, struct ifreq):
		ret = sys_ioctl(fd, SIOCSIFDSTADDR, arg);
		goto out;
	case _IOWR('i', 15, struct ifreq):
		ret = sys_ioctl(fd, SIOCGIFDSTADDR, arg);
		goto out;
	case _IOW('i', 16, struct ifreq):
		ret = sys_ioctl(fd, SIOCSIFFLAGS, arg);
		goto out;
	case _IOWR('i', 17, struct ifreq):
		ret = sys_ioctl(fd, SIOCGIFFLAGS, arg);
		goto out;
	case _IOW('i', 18, struct ifreq):
		ret = sys_ioctl(fd, SIOCSIFMEM, arg);
		goto out;
	case _IOWR('i', 19, struct ifreq):
		ret = sys_ioctl(fd, SIOCGIFMEM, arg);
		goto out;
	case _IOWR('i', 20, struct ifconf):
		ret = sys_ioctl(fd, SIOCGIFCONF, arg);
		goto out;
	case _IOW('i', 21, struct ifreq): /* SIOCSIFMTU */
		ret = sys_ioctl(fd, SIOCSIFMTU, arg);
		goto out;
	case _IOWR('i', 22, struct ifreq): /* SIOCGIFMTU */
		ret = sys_ioctl(fd, SIOCGIFMTU, arg);
		goto out;

	case _IOWR('i', 23, struct ifreq):
		ret = sys_ioctl(fd, SIOCGIFBRDADDR, arg);
		goto out;
	case _IOW('i', 24, struct ifreq):
		ret = sys_ioctl(fd, SIOCSIFBRDADDR, arg);
		goto out;
	case _IOWR('i', 25, struct ifreq):
		ret = sys_ioctl(fd, SIOCGIFNETMASK, arg);
		goto out;
	case _IOW('i', 26, struct ifreq):
		ret = sys_ioctl(fd, SIOCSIFNETMASK, arg);
		goto out;
	case _IOWR('i', 27, struct ifreq):
		ret = sys_ioctl(fd, SIOCGIFMETRIC, arg);
		goto out;
	case _IOW('i', 28, struct ifreq):
		ret = sys_ioctl(fd, SIOCSIFMETRIC, arg);
		goto out;

	case _IOW('i', 30, struct arpreq):
		ret = sys_ioctl(fd, SIOCSARP, arg);
		goto out;
	case _IOWR('i', 31, struct arpreq):
		ret = sys_ioctl(fd, SIOCGARP, arg);
		goto out;
	case _IOW('i', 32, struct arpreq):
		ret = sys_ioctl(fd, SIOCDARP, arg);
		goto out;

	case _IOW('i', 40, struct ifreq): /* SIOCUPPER */
	case _IOW('i', 41, struct ifreq): /* SIOCLOWER */
	case _IOW('i', 44, struct ifreq): /* SIOCSETSYNC */
	case _IOW('i', 45, struct ifreq): /* SIOCGETSYNC */
	case _IOW('i', 46, struct ifreq): /* SIOCSSDSTATS */
	case _IOW('i', 47, struct ifreq): /* SIOCSSESTATS */
	case _IOW('i', 48, struct ifreq): /* SIOCSPROMISC */
		ret = -EOPNOTSUPP;
		goto out;

	case _IOW('i', 49, struct ifreq):
		ret = sys_ioctl(fd, SIOCADDMULTI, arg);
		goto out;
	case _IOW('i', 50, struct ifreq):
		ret = sys_ioctl(fd, SIOCDELMULTI, arg);
		goto out;

	/* FDDI interface ioctls, unsupported. */
		
	case _IOW('i', 51, struct ifreq): /* SIOCFDRESET */
	case _IOW('i', 52, struct ifreq): /* SIOCFDSLEEP */
	case _IOW('i', 53, struct ifreq): /* SIOCSTRTFMWAR */
	case _IOW('i', 54, struct ifreq): /* SIOCLDNSTRTFW */
	case _IOW('i', 55, struct ifreq): /* SIOCGETFDSTAT */
	case _IOW('i', 56, struct ifreq): /* SIOCFDNMIINT */
	case _IOW('i', 57, struct ifreq): /* SIOCFDEXUSER */
	case _IOW('i', 58, struct ifreq): /* SIOCFDGNETMAP */
	case _IOW('i', 59, struct ifreq): /* SIOCFDGIOCTL */
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
		ptr = (int __user *) arg;
		ret = -EFAULT;
		if (get_user(oldval, ptr))
			goto out;
		ret = sys_ioctl(fd, cmd, arg);
		__get_user(newval, ptr);
		if (newval == -1) {
			__put_user(oldval, ptr);
			ret = -EIO;
		}
		if (ret == -ENOTTY)
			ret = -EIO;
		goto out;
	}

	case _IOR('t', 119, int): {
		int oldval, newval, __user *ptr;

		cmd = TIOCGPGRP;
		ptr = (int __user *) arg;
		ret = -EFAULT;
		if (get_user(oldval, ptr))
			goto out;
		ret = sys_ioctl(fd, cmd, arg);
		__get_user(newval, ptr);
		if (newval == -1) {
			__put_user(oldval, ptr);
			ret = -EIO;
		}
		if (ret == -ENOTTY)
			ret = -EIO;
		goto out;
	}
	}

#if 0
	if ((cmd & 0xff00) == ('k' << 8)) {
		printk ("[[KBIO: %8.8x\n", (unsigned int) cmd);
	}
#endif

	ret = sys_ioctl(fd, cmd, arg);
	/* so stupid... */
	ret = (ret == -EINVAL ? -EOPNOTSUPP : ret);
out:
	return ret;
}


