/* $Id: display7seg.c,v 1.6 2002/01/08 16:00:16 davem Exp $
 *
 * display7seg - Driver implementation for the 7-segment display
 * present on Sun Microsystems CP1400 and CP1500
 *
 * Copyright (c) 2000 Eric Brower (ebrower@usa.net)
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>		/* request_region */
#include <linux/smp_lock.h>
#include <asm/atomic.h>
#include <asm/ebus.h>			/* EBus device					*/
#include <asm/oplib.h>			/* OpenProm Library 			*/
#include <asm/uaccess.h>		/* put_/get_user			*/

#include <asm/display7seg.h>

#define D7S_MINOR	193
#define D7S_OBPNAME	"display7seg"
#define D7S_DEVNAME "d7s"

static int sol_compat = 0;		/* Solaris compatibility mode	*/

#ifdef MODULE

/* Solaris compatibility flag -
 * The Solaris implementation omits support for several
 * documented driver features (ref Sun doc 806-0180-03).  
 * By default, this module supports the documented driver 
 * abilities, rather than the Solaris implementation:
 *
 * 	1) Device ALWAYS reverts to OBP-specified FLIPPED mode
 * 	   upon closure of device or module unload.
 * 	2) Device ioctls D7SIOCRD/D7SIOCWR honor toggling of
 * 	   FLIP bit
 *
 * If you wish the device to operate as under Solaris,
 * omitting above features, set this parameter to non-zero.
 */
module_param
	(sol_compat, int, 0);
MODULE_PARM_DESC
	(sol_compat, 
	 "Disables documented functionality omitted from Solaris driver");

MODULE_AUTHOR
	("Eric Brower <ebrower@usa.net>");
MODULE_DESCRIPTION
	("7-Segment Display driver for Sun Microsystems CP1400/1500");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE
	("d7s");
#endif /* ifdef MODULE */

/*
 * Register block address- see header for details
 * -----------------------------------------
 * | DP | ALARM | FLIP | 4 | 3 | 2 | 1 | 0 |
 * -----------------------------------------
 *
 * DP 		- Toggles decimal point on/off 
 * ALARM	- Toggles "Alarm" LED green/red
 * FLIP		- Inverts display for upside-down mounted board
 * bits 0-4	- 7-segment display contents
 */
static void __iomem* d7s_regs;

static inline void d7s_free(void)
{
	iounmap(d7s_regs);
}

static inline int d7s_obpflipped(void)
{
	int opt_node;

	opt_node = prom_getchild(prom_root_node);
	opt_node = prom_searchsiblings(opt_node, "options");
	return ((-1 != prom_getintdefault(opt_node, "d7s-flipped?", -1)) ? 0 : 1);
}

static atomic_t d7s_users = ATOMIC_INIT(0);

static int d7s_open(struct inode *inode, struct file *f)
{
	if (D7S_MINOR != iminor(inode))
		return -ENODEV;
	atomic_inc(&d7s_users);
	return 0;
}

static int d7s_release(struct inode *inode, struct file *f)
{
	/* Reset flipped state to OBP default only if
	 * no other users have the device open and we
	 * are not operating in solaris-compat mode
	 */
	if (atomic_dec_and_test(&d7s_users) && !sol_compat) {
		int regval = 0;

		regval = readb(d7s_regs);
		(0 == d7s_obpflipped())	? 
			writeb(regval |= D7S_FLIP,  d7s_regs): 
			writeb(regval &= ~D7S_FLIP, d7s_regs);
	}

	return 0;
}

static long d7s_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	__u8 regs = readb(d7s_regs);
	__u8 ireg = 0;
	int error = 0;

	if (D7S_MINOR != iminor(file->f_dentry->d_inode))
		return -ENODEV;

	lock_kernel();
	switch (cmd) {
	case D7SIOCWR:
		/* assign device register values
		 * we mask-out D7S_FLIP if in sol_compat mode
		 */
		if (get_user(ireg, (int __user *) arg)) {
			error = -EFAULT;
			break;
		}
		if (0 != sol_compat) {
			(regs & D7S_FLIP) ? 
				(ireg |= D7S_FLIP) : (ireg &= ~D7S_FLIP);
		}
		writeb(ireg, d7s_regs);
		break;

	case D7SIOCRD:
		/* retrieve device register values
		 * NOTE: Solaris implementation returns D7S_FLIP bit
		 * as toggled by user, even though it does not honor it.
		 * This driver will not misinform you about the state
		 * of your hardware while in sol_compat mode
		 */
		if (put_user(regs, (int __user *) arg)) {
			error = -EFAULT;
			break;
		}
		break;

	case D7SIOCTM:
		/* toggle device mode-- flip display orientation */
		(regs & D7S_FLIP) ? 
			(regs &= ~D7S_FLIP) : (regs |= D7S_FLIP);
		writeb(regs, d7s_regs);
		break;
	};
	unlock_kernel();

	return error;
}

static struct file_operations d7s_fops = {
	.owner =		THIS_MODULE,
	.unlocked_ioctl =	d7s_ioctl,
	.compat_ioctl =		d7s_ioctl,
	.open =			d7s_open,
	.release =		d7s_release,
};

static struct miscdevice d7s_miscdev = { D7S_MINOR, D7S_DEVNAME, &d7s_fops };

static int __init d7s_init(void)
{
	struct linux_ebus *ebus = NULL;
	struct linux_ebus_device *edev = NULL;
	int iTmp = 0, regs = 0;

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_node->name, D7S_OBPNAME))
				goto ebus_done;
		}
	}

ebus_done:
	if(!edev) {
		printk("%s: unable to locate device\n", D7S_DEVNAME);
		return -ENODEV;
	}

	d7s_regs = ioremap(edev->resource[0].start, sizeof(__u8));

	iTmp = misc_register(&d7s_miscdev);
	if (0 != iTmp) {
		printk("%s: unable to acquire miscdevice minor %i\n",
		       D7S_DEVNAME, D7S_MINOR);
		iounmap(d7s_regs);
		return iTmp;
	}

	/* OBP option "d7s-flipped?" is honored as default
	 * for the device, and reset default when detached
	 */
	regs = readb(d7s_regs);
	iTmp = d7s_obpflipped();
	(0 == iTmp) ? 
		writeb(regs |= D7S_FLIP,  d7s_regs): 
		writeb(regs &= ~D7S_FLIP, d7s_regs);

	printk("%s: 7-Segment Display%s at 0x%lx %s\n", 
	       D7S_DEVNAME,
	       (0 == iTmp) ? (" (FLIPPED)") : (""),
	       edev->resource[0].start,
	       (0 != sol_compat) ? ("in sol_compat mode") : (""));

	return 0;
}

static void __exit d7s_cleanup(void)
{
	int regs = readb(d7s_regs);

	/* Honor OBP d7s-flipped? unless operating in solaris-compat mode */
	if (0 == sol_compat) {
		(0 == d7s_obpflipped())	? 
			writeb(regs |= D7S_FLIP,  d7s_regs):
			writeb(regs &= ~D7S_FLIP, d7s_regs);
	}

	misc_deregister(&d7s_miscdev);
	d7s_free();
}

module_init(d7s_init);
module_exit(d7s_cleanup);
