/* display7seg.c - Driver implementation for the 7-segment display
 *                 present on Sun Microsystems CP1400 and CP1500
 *
 * Copyright (c) 2000 Eric Brower (ebrower@usa.net)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>		/* request_region */
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>		/* put_/get_user			*/
#include <asm/io.h>

#include <asm/display7seg.h>

#define D7S_MINOR	193
#define DRIVER_NAME	"d7s"
#define PFX		DRIVER_NAME ": "

static int sol_compat = 0;		/* Solaris compatibility mode	*/

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
module_param(sol_compat, int, 0);
MODULE_PARM_DESC(sol_compat, 
		 "Disables documented functionality omitted from Solaris driver");

MODULE_AUTHOR("Eric Brower <ebrower@usa.net>");
MODULE_DESCRIPTION("7-Segment Display driver for Sun Microsystems CP1400/1500");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("d7s");

struct d7s {
	void __iomem	*regs;
	bool		flipped;
};
struct d7s *d7s_device;

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
static atomic_t d7s_users = ATOMIC_INIT(0);

static int d7s_open(struct inode *inode, struct file *f)
{
	if (D7S_MINOR != iminor(inode))
		return -ENODEV;
	cycle_kernel_lock();
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
		struct d7s *p = d7s_device;
		u8 regval = 0;

		regval = readb(p->regs);
		if (p->flipped)
			regval |= D7S_FLIP;
		else
			regval &= ~D7S_FLIP;
		writeb(regval, p->regs);
	}

	return 0;
}

static long d7s_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct d7s *p = d7s_device;
	u8 regs = readb(p->regs);
	int error = 0;
	u8 ireg = 0;

	if (D7S_MINOR != iminor(file->f_path.dentry->d_inode))
		return -ENODEV;

	lock_kernel();
	switch (cmd) {
	case D7SIOCWR:
		/* assign device register values we mask-out D7S_FLIP
		 * if in sol_compat mode
		 */
		if (get_user(ireg, (int __user *) arg)) {
			error = -EFAULT;
			break;
		}
		if (sol_compat) {
			if (regs & D7S_FLIP)
				ireg |= D7S_FLIP;
			else
				ireg &= ~D7S_FLIP;
		}
		writeb(ireg, p->regs);
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
		if (regs & D7S_FLIP)
			regs &= ~D7S_FLIP;
		else
			regs |= D7S_FLIP;
		writeb(regs, p->regs);
		break;
	};
	unlock_kernel();

	return error;
}

static const struct file_operations d7s_fops = {
	.owner =		THIS_MODULE,
	.unlocked_ioctl =	d7s_ioctl,
	.compat_ioctl =		d7s_ioctl,
	.open =			d7s_open,
	.release =		d7s_release,
};

static struct miscdevice d7s_miscdev = {
	.minor		= D7S_MINOR,
	.name		= DRIVER_NAME,
	.fops		= &d7s_fops
};

static int __devinit d7s_probe(struct of_device *op,
			       const struct of_device_id *match)
{
	struct device_node *opts;
	int err = -EINVAL;
	struct d7s *p;
	u8 regs;

	if (d7s_device)
		goto out;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	err = -ENOMEM;
	if (!p)
		goto out;

	p->regs = of_ioremap(&op->resource[0], 0, sizeof(u8), "d7s");
	if (!p->regs) {
		printk(KERN_ERR PFX "Cannot map chip registers\n");
		goto out_free;
	}

	err = misc_register(&d7s_miscdev);
	if (err) {
		printk(KERN_ERR PFX "Unable to acquire miscdevice minor %i\n",
		       D7S_MINOR);
		goto out_iounmap;
	}

	/* OBP option "d7s-flipped?" is honored as default for the
	 * device, and reset default when detached
	 */
	regs = readb(p->regs);
	opts = of_find_node_by_path("/options");
	if (opts &&
	    of_get_property(opts, "d7s-flipped?", NULL))
		p->flipped = true;

	if (p->flipped)
		regs |= D7S_FLIP;
	else
		regs &= ~D7S_FLIP;

	writeb(regs,  p->regs);

	printk(KERN_INFO PFX "7-Segment Display%s at [%s:0x%llx] %s\n",
	       op->node->full_name,
	       (regs & D7S_FLIP) ? " (FLIPPED)" : "",
	       op->resource[0].start,
	       sol_compat ? "in sol_compat mode" : "");

	dev_set_drvdata(&op->dev, p);
	d7s_device = p;
	err = 0;

out:
	return err;

out_iounmap:
	of_iounmap(&op->resource[0], p->regs, sizeof(u8));

out_free:
	kfree(p);
	goto out;
}

static int __devexit d7s_remove(struct of_device *op)
{
	struct d7s *p = dev_get_drvdata(&op->dev);
	u8 regs = readb(p->regs);

	/* Honor OBP d7s-flipped? unless operating in solaris-compat mode */
	if (sol_compat) {
		if (p->flipped)
			regs |= D7S_FLIP;
		else
			regs &= ~D7S_FLIP;
		writeb(regs, p->regs);
	}

	misc_deregister(&d7s_miscdev);
	of_iounmap(&op->resource[0], p->regs, sizeof(u8));
	kfree(p);

	return 0;
}

static const struct of_device_id d7s_match[] = {
	{
		.name = "display7seg",
	},
	{},
};
MODULE_DEVICE_TABLE(of, d7s_match);

static struct of_platform_driver d7s_driver = {
	.name		= DRIVER_NAME,
	.match_table	= d7s_match,
	.probe		= d7s_probe,
	.remove		= __devexit_p(d7s_remove),
};

static int __init d7s_init(void)
{
	return of_register_driver(&d7s_driver, &of_bus_type);
}

static void __exit d7s_exit(void)
{
	of_unregister_driver(&d7s_driver);
}

module_init(d7s_init);
module_exit(d7s_exit);
