/* UML hardware watchdog, shamelessly stolen from:
 *
 *	SoftDog	0.05:	A Software Watchdog Device
 *
 *	(c) Copyright 1996 Alan Cox <alan@redhat.com>, All Rights Reserved.
 *				http://www.redhat.com
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *	
 *	Neither Alan Cox nor CymruNet Ltd. admit liability nor provide 
 *	warranty for any of this software. This material is provided 
 *	"AS-IS" and at no charge.	
 *
 *	(c) Copyright 1995    Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 *	Software only watchdog driver. Unlike its big brother the WDT501P
 *	driver this won't always recover a failed machine.
 *
 *  03/96: Angelo Haritsis <ah@doc.ic.ac.uk> :
 *	Modularised.
 *	Added soft_margin; use upon insmod to change the timer delay.
 *	NB: uses same minor as wdt (WATCHDOG_MINOR); we could use separate
 *	    minors.
 *
 *  19980911 Alan Cox
 *	Made SMP safe for 2.3.x
 *
 *  20011127 Joel Becker (jlbec@evilplan.org>
 *	Added soft_noboot; Allows testing the softdog trigger without 
 *	requiring a recompile.
 *	Added WDIOC_GETTIMEOUT and WDIOC_SETTIMOUT.
 */
 
#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/reboot.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include "mconsole.h"

MODULE_LICENSE("GPL");

/* Locked by the BKL in harddog_open and harddog_release */
static int timer_alive;
static int harddog_in_fd = -1;
static int harddog_out_fd = -1;

/*
 *	Allow only one person to hold it open
 */
 
extern int start_watchdog(int *in_fd_ret, int *out_fd_ret, char *sock);

static int harddog_open(struct inode *inode, struct file *file)
{
	int err;
	char *sock = NULL;

	lock_kernel();
	if(timer_alive)
		return -EBUSY;
#ifdef CONFIG_HARDDOG_NOWAYOUT	 
	__module_get(THIS_MODULE);
#endif

#ifdef CONFIG_MCONSOLE
	sock = mconsole_notify_socket();
#endif
	err = start_watchdog(&harddog_in_fd, &harddog_out_fd, sock);
	if(err) return(err);

	timer_alive = 1;
	unlock_kernel();
	return nonseekable_open(inode, file);
}

extern void stop_watchdog(int in_fd, int out_fd);

static int harddog_release(struct inode *inode, struct file *file)
{
	/*
	 *	Shut off the timer.
	 */
	lock_kernel();

	stop_watchdog(harddog_in_fd, harddog_out_fd);
	harddog_in_fd = -1;
	harddog_out_fd = -1;

	timer_alive=0;
	unlock_kernel();
	return 0;
}

extern int ping_watchdog(int fd);

static ssize_t harddog_write(struct file *file, const char __user *data, size_t len,
			     loff_t *ppos)
{
	/*
	 *	Refresh the timer.
	 */
	if(len)
		return(ping_watchdog(harddog_out_fd));
	return 0;
}

static int harddog_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	void __user *argp= (void __user *)arg;
	static struct watchdog_info ident = {
		WDIOC_SETTIMEOUT,
		0,
		"UML Hardware Watchdog"
	};
	switch (cmd) {
		default:
			return -ENOTTY;
		case WDIOC_GETSUPPORT:
			if(copy_to_user(argp, &ident, sizeof(ident)))
				return -EFAULT;
			return 0;
		case WDIOC_GETSTATUS:
		case WDIOC_GETBOOTSTATUS:
			return put_user(0,(int __user *)argp);
		case WDIOC_KEEPALIVE:
			return(ping_watchdog(harddog_out_fd));
	}
}

static struct file_operations harddog_fops = {
	.owner		= THIS_MODULE,
	.write		= harddog_write,
	.ioctl		= harddog_ioctl,
	.open		= harddog_open,
	.release	= harddog_release,
};

static struct miscdevice harddog_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &harddog_fops,
};

static char banner[] __initdata = KERN_INFO "UML Watchdog Timer\n";

static int __init harddog_init(void)
{
	int ret;

	ret = misc_register(&harddog_miscdev);

	if (ret)
		return ret;

	printk(banner);

	return(0);
}

static void __exit harddog_exit(void)
{
	misc_deregister(&harddog_miscdev);
}

module_init(harddog_init);
module_exit(harddog_exit);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
