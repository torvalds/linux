/* apc - Driver implementation for power management functions
 * of Aurora Personality Chip (APC) on SPARCstation-4/5 and
 * derivatives.
 *
 * Copyright (c) 2002 Eric Brower (ebrower@usa.net)
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/pm.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <asm/io.h>
#include <asm/oplib.h>
#include <asm/uaccess.h>
#include <asm/auxio.h>
#include <asm/apc.h>

/* Debugging
 * 
 * #define APC_DEBUG_LED
 */

#define APC_MINOR	MISC_DYNAMIC_MINOR
#define APC_OBPNAME	"power-management"
#define APC_DEVNAME "apc"

static u8 __iomem *regs;
static int apc_no_idle __devinitdata = 0;

#define apc_readb(offs)		(sbus_readb(regs+offs))
#define apc_writeb(val, offs) 	(sbus_writeb(val, regs+offs))

/* Specify "apc=noidle" on the kernel command line to 
 * disable APC CPU standby support.  Certain prototype
 * systems (SPARCstation-Fox) do not play well with APC
 * CPU idle, so disable this if your system has APC and 
 * crashes randomly.
 */
static int __init apc_setup(char *str) 
{
	if(!strncmp(str, "noidle", strlen("noidle"))) {
		apc_no_idle = 1;
		return 1;
	}
	return 0;
}
__setup("apc=", apc_setup);

/* 
 * CPU idle callback function
 * See .../arch/sparc/kernel/process.c
 */
static void apc_swift_idle(void)
{
#ifdef APC_DEBUG_LED
	set_auxio(0x00, AUXIO_LED); 
#endif

	apc_writeb(apc_readb(APC_IDLE_REG) | APC_IDLE_ON, APC_IDLE_REG);

#ifdef APC_DEBUG_LED
	set_auxio(AUXIO_LED, 0x00); 
#endif
} 

static inline void apc_free(struct platform_device *op)
{
	of_iounmap(&op->resource[0], regs, resource_size(&op->resource[0]));
}

static int apc_open(struct inode *inode, struct file *f)
{
	return 0;
}

static int apc_release(struct inode *inode, struct file *f)
{
	return 0;
}

static long apc_ioctl(struct file *f, unsigned int cmd, unsigned long __arg)
{
	__u8 inarg, __user *arg = (__u8 __user *) __arg;

	switch (cmd) {
	case APCIOCGFANCTL:
		if (put_user(apc_readb(APC_FANCTL_REG) & APC_REGMASK, arg))
			return -EFAULT;
		break;

	case APCIOCGCPWR:
		if (put_user(apc_readb(APC_CPOWER_REG) & APC_REGMASK, arg))
			return -EFAULT;
		break;

	case APCIOCGBPORT:
		if (put_user(apc_readb(APC_BPORT_REG) & APC_BPMASK, arg))
			return -EFAULT;
		break;

	case APCIOCSFANCTL:
		if (get_user(inarg, arg))
			return -EFAULT;
		apc_writeb(inarg & APC_REGMASK, APC_FANCTL_REG);
		break;

	case APCIOCSCPWR:
		if (get_user(inarg, arg))
			return -EFAULT;
		apc_writeb(inarg & APC_REGMASK, APC_CPOWER_REG);
		break;

	case APCIOCSBPORT:
		if (get_user(inarg, arg))
			return -EFAULT;
		apc_writeb(inarg & APC_BPMASK, APC_BPORT_REG);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations apc_fops = {
	.unlocked_ioctl =	apc_ioctl,
	.open =			apc_open,
	.release =		apc_release,
	.llseek =		noop_llseek,
};

static struct miscdevice apc_miscdev = { APC_MINOR, APC_DEVNAME, &apc_fops };

static int __devinit apc_probe(struct platform_device *op)
{
	int err;

	regs = of_ioremap(&op->resource[0], 0,
			  resource_size(&op->resource[0]), APC_OBPNAME);
	if (!regs) {
		printk(KERN_ERR "%s: unable to map registers\n", APC_DEVNAME);
		return -ENODEV;
	}

	err = misc_register(&apc_miscdev);
	if (err) {
		printk(KERN_ERR "%s: unable to register device\n", APC_DEVNAME);
		apc_free(op);
		return -ENODEV;
	}

	/* Assign power management IDLE handler */
	if (!apc_no_idle)
		pm_idle = apc_swift_idle;	

	printk(KERN_INFO "%s: power management initialized%s\n", 
	       APC_DEVNAME, apc_no_idle ? " (CPU idle disabled)" : "");

	return 0;
}

static struct of_device_id apc_match[] = {
	{
		.name = APC_OBPNAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, apc_match);

static struct platform_driver apc_driver = {
	.driver = {
		.name = "apc",
		.owner = THIS_MODULE,
		.of_match_table = apc_match,
	},
	.probe		= apc_probe,
};

static int __init apc_init(void)
{
	return platform_driver_register(&apc_driver);
}

/* This driver is not critical to the boot process
 * and is easiest to ioremap when SBus is already
 * initialized, so we install ourselves thusly:
 */
__initcall(apc_init);
