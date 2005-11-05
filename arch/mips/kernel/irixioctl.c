/*
 * irixioctl.c: A fucking mess...
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sockios.h>
#include <linux/syscalls.h>
#include <linux/tty.h>
#include <linux/file.h>
#include <linux/rcupdate.h>

#include <asm/uaccess.h>
#include <asm/ioctl.h>
#include <asm/ioctls.h>

#undef DEBUG_IOCTLS
#undef DEBUG_MISSING_IOCTL

struct irix_termios {
	tcflag_t c_iflag, c_oflag, c_cflag, c_lflag;
	cc_t c_cc[NCCS];
};

extern void start_tty(struct tty_struct *tty);
static struct tty_struct *get_tty(int fd)
{
	struct file *filp;
	struct tty_struct *ttyp = NULL;

	rcu_read_lock();
	filp = fcheck(fd);
	if(filp && filp->private_data) {
		ttyp = (struct tty_struct *) filp->private_data;

		if(ttyp->magic != TTY_MAGIC)
			ttyp =NULL;
	}
	rcu_read_unlock();
	return ttyp;
}

static struct tty_struct *get_real_tty(struct tty_struct *tp)
{
	if (tp->driver->type == TTY_DRIVER_TYPE_PTY &&
	   tp->driver->subtype == PTY_TYPE_MASTER)
		return tp->link;
	else
		return tp;
}

asmlinkage int irix_ioctl(int fd, unsigned long cmd, unsigned long arg)
{
	struct tty_struct *tp, *rtp;
	mm_segment_t old_fs;
	int i, error = 0;

#ifdef DEBUG_IOCTLS
	printk("[%s:%d] irix_ioctl(%d, ", current->comm, current->pid, fd);
#endif
	switch(cmd) {
	case 0x00005401:
#ifdef DEBUG_IOCTLS
		printk("TCGETA, %08lx) ", arg);
#endif
		error = sys_ioctl(fd, TCGETA, arg);
		break;

	case 0x0000540d: {
		struct termios kt;
		struct irix_termios __user *it =
			(struct irix_termios __user *) arg;

#ifdef DEBUG_IOCTLS
		printk("TCGETS, %08lx) ", arg);
#endif
		if (!access_ok(VERIFY_WRITE, it, sizeof(*it))) {
			error = -EFAULT;
			break;
		}
		old_fs = get_fs(); set_fs(get_ds());
		error = sys_ioctl(fd, TCGETS, (unsigned long) &kt);
		set_fs(old_fs);
		if (error)
			break;

		error = __put_user(kt.c_iflag, &it->c_iflag);
		error |= __put_user(kt.c_oflag, &it->c_oflag);
		error |= __put_user(kt.c_cflag, &it->c_cflag);
		error |= __put_user(kt.c_lflag, &it->c_lflag);

		for (i = 0; i < NCCS; i++)
			error |= __put_user(kt.c_cc[i], &it->c_cc[i]);
		break;
	}

	case 0x0000540e: {
		struct termios kt;
		struct irix_termios *it = (struct irix_termios *) arg;

#ifdef DEBUG_IOCTLS
		printk("TCSETS, %08lx) ", arg);
#endif
		if (!access_ok(VERIFY_READ, it, sizeof(*it))) {
			error = -EFAULT;
			break;
		}
		old_fs = get_fs(); set_fs(get_ds());
		error = sys_ioctl(fd, TCGETS, (unsigned long) &kt);
		set_fs(old_fs);
		if (error)
			break;

		error = __get_user(kt.c_iflag, &it->c_iflag);
		error |= __get_user(kt.c_oflag, &it->c_oflag);
		error |= __get_user(kt.c_cflag, &it->c_cflag);
		error |= __get_user(kt.c_lflag, &it->c_lflag);

		for (i = 0; i < NCCS; i++)
			error |= __get_user(kt.c_cc[i], &it->c_cc[i]);

		if (error)
			break;
		old_fs = get_fs(); set_fs(get_ds());
		error = sys_ioctl(fd, TCSETS, (unsigned long) &kt);
		set_fs(old_fs);
		break;
	}

	case 0x0000540f:
#ifdef DEBUG_IOCTLS
		printk("TCSETSW, %08lx) ", arg);
#endif
		error = sys_ioctl(fd, TCSETSW, arg);
		break;

	case 0x00005471:
#ifdef DEBUG_IOCTLS
		printk("TIOCNOTTY, %08lx) ", arg);
#endif
		error = sys_ioctl(fd, TIOCNOTTY, arg);
		break;

	case 0x00007416:
#ifdef DEBUG_IOCTLS
		printk("TIOCGSID, %08lx) ", arg);
#endif
		tp = get_tty(fd);
		if(!tp) {
			error = -EINVAL;
			break;
		}
		rtp = get_real_tty(tp);
#ifdef DEBUG_IOCTLS
		printk("rtp->session=%d ", rtp->session);
#endif
		error = put_user(rtp->session, (unsigned long __user *) arg);
		break;

	case 0x746e:
		/* TIOCSTART, same effect as hitting ^Q */
#ifdef DEBUG_IOCTLS
		printk("TIOCSTART, %08lx) ", arg);
#endif
		tp = get_tty(fd);
		if(!tp) {
			error = -EINVAL;
			break;
		}
		rtp = get_real_tty(tp);
		start_tty(rtp);
		break;

	case 0x20006968:
#ifdef DEBUG_IOCTLS
		printk("SIOCGETLABEL, %08lx) ", arg);
#endif
		error = -ENOPKG;
		break;

	case 0x40047477:
#ifdef DEBUG_IOCTLS
		printk("TIOCGPGRP, %08lx) ", arg);
#endif
		error = sys_ioctl(fd, TIOCGPGRP, arg);
#ifdef DEBUG_IOCTLS
		printk("arg=%d ", *(int *)arg);
#endif
		break;

	case 0x40087468:
#ifdef DEBUG_IOCTLS
		printk("TIOCGWINSZ, %08lx) ", arg);
#endif
		error = sys_ioctl(fd, TIOCGWINSZ, arg);
		break;

	case 0x8004667e:
		error = sys_ioctl(fd, FIONBIO, arg);
		break;

	case 0x80047476:
		error = sys_ioctl(fd, TIOCSPGRP, arg);
		break;

	case 0x8020690c:
		error = sys_ioctl(fd, SIOCSIFADDR, arg);
		break;

	case 0x80206910:
		error = sys_ioctl(fd, SIOCSIFFLAGS, arg);
		break;

	case 0xc0206911:
		error = sys_ioctl(fd, SIOCGIFFLAGS, arg);
		break;

	case 0xc020691b:
		error = sys_ioctl(fd, SIOCGIFMETRIC, arg);
		break;

	default: {
#ifdef DEBUG_MISSING_IOCTL
		char *msg = "Unimplemented IOCTL cmd tell linux-mips@linux-mips.org\n";

#ifdef DEBUG_IOCTLS
		printk("UNIMP_IOCTL, %08lx)\n", arg);
#endif
		old_fs = get_fs(); set_fs(get_ds());
		sys_write(2, msg, strlen(msg));
		set_fs(old_fs);
		printk("[%s:%d] Does unimplemented IRIX ioctl cmd %08lx\n",
		       current->comm, current->pid, cmd);
		do_exit(255);
#else
		error = sys_ioctl (fd, cmd, arg);
#endif
	}

	};
#ifdef DEBUG_IOCTLS
	printk("error=%d\n", error);
#endif
	return error;
}
