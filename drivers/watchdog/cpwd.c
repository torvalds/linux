/* cpwd.c - driver implementation for hardware watchdog
 * timers found on Sun Microsystems CP1400 and CP1500 boards.
 *
 * This device supports both the generic Linux watchdog
 * interface and Solaris-compatible ioctls as best it is
 * able.
 *
 * NOTE: 	CP1400 systems appear to have a defective intr_mask
 * 			register on the PLD, preventing the disabling of
 * 			timer interrupts.  We use a timer to periodically
 * 			reset 'stopped' watchdogs on affected platforms.
 *
 * Copyright (c) 2000 Eric Brower (ebrower@usa.net)
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/uaccess.h>

#include <asm/irq.h>
#include <asm/watchdog.h>

#define DRIVER_NAME	"cpwd"
#define PFX		DRIVER_NAME ": "

#define WD_OBPNAME	"watchdog"
#define WD_BADMODEL	"SUNW,501-5336"
#define WD_BTIMEOUT	(jiffies + (HZ * 1000))
#define WD_BLIMIT	0xFFFF

#define WD0_MINOR	212
#define WD1_MINOR	213
#define WD2_MINOR	214

/* Internal driver definitions.  */
#define WD0_ID			0
#define WD1_ID			1
#define WD2_ID			2
#define WD_NUMDEVS		3

#define WD_INTR_OFF		0
#define WD_INTR_ON		1

#define WD_STAT_INIT	0x01	/* Watchdog timer is initialized	*/
#define WD_STAT_BSTOP	0x02	/* Watchdog timer is brokenstopped	*/
#define WD_STAT_SVCD	0x04	/* Watchdog interrupt occurred		*/

/* Register value definitions
 */
#define WD0_INTR_MASK	0x01	/* Watchdog device interrupt masks	*/
#define WD1_INTR_MASK	0x02
#define WD2_INTR_MASK	0x04

#define WD_S_RUNNING	0x01	/* Watchdog device status running	*/
#define WD_S_EXPIRED	0x02	/* Watchdog device status expired	*/

struct cpwd {
	void __iomem	*regs;
	spinlock_t	lock;

	unsigned int	irq;

	unsigned long	timeout;
	bool		enabled;
	bool		reboot;
	bool		broken;
	bool		initialized;

	struct {
		struct miscdevice	misc;
		void __iomem		*regs;
		u8			intr_mask;
		u8			runstatus;
		u16			timeout;
	} devs[WD_NUMDEVS];
};

static struct cpwd *cpwd_device;

/* Sun uses Altera PLD EPF8820ATC144-4
 * providing three hardware watchdogs:
 *
 * 1) RIC - sends an interrupt when triggered
 * 2) XIR - asserts XIR_B_RESET when triggered, resets CPU
 * 3) POR - asserts POR_B_RESET when triggered, resets CPU, backplane, board
 *
 *** Timer register block definition (struct wd_timer_regblk)
 *
 * dcntr and limit registers (halfword access):
 * -------------------
 * | 15 | ...| 1 | 0 |
 * -------------------
 * |-  counter val  -|
 * -------------------
 * dcntr - 	Current 16-bit downcounter value.
 * 			When downcounter reaches '0' watchdog expires.
 * 			Reading this register resets downcounter with
 * 			'limit' value.
 * limit - 	16-bit countdown value in 1/10th second increments.
 * 			Writing this register begins countdown with input value.
 * 			Reading from this register does not affect counter.
 * NOTES:	After watchdog reset, dcntr and limit contain '1'
 *
 * status register (byte access):
 * ---------------------------
 * | 7 | ... | 2 |  1  |  0  |
 * --------------+------------
 * |-   UNUSED  -| EXP | RUN |
 * ---------------------------
 * status-	Bit 0 - Watchdog is running
 * 			Bit 1 - Watchdog has expired
 *
 *** PLD register block definition (struct wd_pld_regblk)
 *
 * intr_mask register (byte access):
 * ---------------------------------
 * | 7 | ... | 3 |  2  |  1  |  0  |
 * +-------------+------------------
 * |-   UNUSED  -| WD3 | WD2 | WD1 |
 * ---------------------------------
 * WD3 -  1 == Interrupt disabled for watchdog 3
 * WD2 -  1 == Interrupt disabled for watchdog 2
 * WD1 -  1 == Interrupt disabled for watchdog 1
 *
 * pld_status register (byte access):
 * UNKNOWN, MAGICAL MYSTERY REGISTER
 *
 */
#define WD_TIMER_REGSZ	16
#define WD0_OFF		0
#define WD1_OFF		(WD_TIMER_REGSZ * 1)
#define WD2_OFF		(WD_TIMER_REGSZ * 2)
#define PLD_OFF		(WD_TIMER_REGSZ * 3)

#define WD_DCNTR	0x00
#define WD_LIMIT	0x04
#define WD_STATUS	0x08

#define PLD_IMASK	(PLD_OFF + 0x00)
#define PLD_STATUS	(PLD_OFF + 0x04)

static struct timer_list cpwd_timer;

static int wd0_timeout;
static int wd1_timeout;
static int wd2_timeout;

module_param(wd0_timeout, int, 0);
MODULE_PARM_DESC(wd0_timeout, "Default watchdog0 timeout in 1/10secs");
module_param(wd1_timeout, int, 0);
MODULE_PARM_DESC(wd1_timeout, "Default watchdog1 timeout in 1/10secs");
module_param(wd2_timeout, int, 0);
MODULE_PARM_DESC(wd2_timeout, "Default watchdog2 timeout in 1/10secs");

MODULE_AUTHOR("Eric Brower <ebrower@usa.net>");
MODULE_DESCRIPTION("Hardware watchdog driver for Sun Microsystems CP1400/1500");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("watchdog");

static void cpwd_writew(u16 val, void __iomem *addr)
{
	writew(cpu_to_le16(val), addr);
}
static u16 cpwd_readw(void __iomem *addr)
{
	u16 val = readw(addr);

	return le16_to_cpu(val);
}

static void cpwd_writeb(u8 val, void __iomem *addr)
{
	writeb(val, addr);
}

static u8 cpwd_readb(void __iomem *addr)
{
	return readb(addr);
}

/* Enable or disable watchdog interrupts
 * Because of the CP1400 defect this should only be
 * called during initialzation or by wd_[start|stop]timer()
 *
 * index 	- sub-device index, or -1 for 'all'
 * enable	- non-zero to enable interrupts, zero to disable
 */
static void cpwd_toggleintr(struct cpwd *p, int index, int enable)
{
	unsigned char curregs = cpwd_readb(p->regs + PLD_IMASK);
	unsigned char setregs =
		(index == -1) ?
		(WD0_INTR_MASK | WD1_INTR_MASK | WD2_INTR_MASK) :
		(p->devs[index].intr_mask);

	if (enable == WD_INTR_ON)
		curregs &= ~setregs;
	else
		curregs |= setregs;

	cpwd_writeb(curregs, p->regs + PLD_IMASK);
}

/* Restarts timer with maximum limit value and
 * does not unset 'brokenstop' value.
 */
static void cpwd_resetbrokentimer(struct cpwd *p, int index)
{
	cpwd_toggleintr(p, index, WD_INTR_ON);
	cpwd_writew(WD_BLIMIT, p->devs[index].regs + WD_LIMIT);
}

/* Timer method called to reset stopped watchdogs--
 * because of the PLD bug on CP1400, we cannot mask
 * interrupts within the PLD so me must continually
 * reset the timers ad infinitum.
 */
static void cpwd_brokentimer(unsigned long data)
{
	struct cpwd *p = (struct cpwd *) data;
	int id, tripped = 0;

	/* kill a running timer instance, in case we
	 * were called directly instead of by kernel timer
	 */
	if (timer_pending(&cpwd_timer))
		del_timer(&cpwd_timer);

	for (id = 0; id < WD_NUMDEVS; id++) {
		if (p->devs[id].runstatus & WD_STAT_BSTOP) {
			++tripped;
			cpwd_resetbrokentimer(p, id);
		}
	}

	if (tripped) {
		/* there is at least one timer brokenstopped-- reschedule */
		cpwd_timer.expires = WD_BTIMEOUT;
		add_timer(&cpwd_timer);
	}
}

/* Reset countdown timer with 'limit' value and continue countdown.
 * This will not start a stopped timer.
 */
static void cpwd_pingtimer(struct cpwd *p, int index)
{
	if (cpwd_readb(p->devs[index].regs + WD_STATUS) & WD_S_RUNNING)
		cpwd_readw(p->devs[index].regs + WD_DCNTR);
}

/* Stop a running watchdog timer-- the timer actually keeps
 * running, but the interrupt is masked so that no action is
 * taken upon expiration.
 */
static void cpwd_stoptimer(struct cpwd *p, int index)
{
	if (cpwd_readb(p->devs[index].regs + WD_STATUS) & WD_S_RUNNING) {
		cpwd_toggleintr(p, index, WD_INTR_OFF);

		if (p->broken) {
			p->devs[index].runstatus |= WD_STAT_BSTOP;
			cpwd_brokentimer((unsigned long) p);
		}
	}
}

/* Start a watchdog timer with the specified limit value
 * If the watchdog is running, it will be restarted with
 * the provided limit value.
 *
 * This function will enable interrupts on the specified
 * watchdog.
 */
static void cpwd_starttimer(struct cpwd *p, int index)
{
	if (p->broken)
		p->devs[index].runstatus &= ~WD_STAT_BSTOP;

	p->devs[index].runstatus &= ~WD_STAT_SVCD;

	cpwd_writew(p->devs[index].timeout, p->devs[index].regs + WD_LIMIT);
	cpwd_toggleintr(p, index, WD_INTR_ON);
}

static int cpwd_getstatus(struct cpwd *p, int index)
{
	unsigned char stat = cpwd_readb(p->devs[index].regs + WD_STATUS);
	unsigned char intr = cpwd_readb(p->devs[index].regs + PLD_IMASK);
	unsigned char ret  = WD_STOPPED;

	/* determine STOPPED */
	if (!stat)
		return ret;

	/* determine EXPIRED vs FREERUN vs RUNNING */
	else if (WD_S_EXPIRED & stat) {
		ret = WD_EXPIRED;
	} else if (WD_S_RUNNING & stat) {
		if (intr & p->devs[index].intr_mask) {
			ret = WD_FREERUN;
		} else {
			/* Fudge WD_EXPIRED status for defective CP1400--
			 * IF timer is running
			 * 	AND brokenstop is set
			 * 	AND an interrupt has been serviced
			 * we are WD_EXPIRED.
			 *
			 * IF timer is running
			 * 	AND brokenstop is set
			 * 	AND no interrupt has been serviced
			 * we are WD_FREERUN.
			 */
			if (p->broken &&
			    (p->devs[index].runstatus & WD_STAT_BSTOP)) {
				if (p->devs[index].runstatus & WD_STAT_SVCD) {
					ret = WD_EXPIRED;
				} else {
					/* we could as well pretend
					 * we are expired */
					ret = WD_FREERUN;
				}
			} else {
				ret = WD_RUNNING;
			}
		}
	}

	/* determine SERVICED */
	if (p->devs[index].runstatus & WD_STAT_SVCD)
		ret |= WD_SERVICED;

	return ret;
}

static irqreturn_t cpwd_interrupt(int irq, void *dev_id)
{
	struct cpwd *p = dev_id;

	/* Only WD0 will interrupt-- others are NMI and we won't
	 * see them here....
	 */
	spin_lock_irq(&p->lock);

	cpwd_stoptimer(p, WD0_ID);
	p->devs[WD0_ID].runstatus |=  WD_STAT_SVCD;

	spin_unlock_irq(&p->lock);

	return IRQ_HANDLED;
}

static int cpwd_open(struct inode *inode, struct file *f)
{
	struct cpwd *p = cpwd_device;

	lock_kernel();
	switch (iminor(inode)) {
	case WD0_MINOR:
	case WD1_MINOR:
	case WD2_MINOR:
		break;

	default:
		unlock_kernel();
		return -ENODEV;
	}

	/* Register IRQ on first open of device */
	if (!p->initialized) {
		if (request_irq(p->irq, &cpwd_interrupt,
				IRQF_SHARED, DRIVER_NAME, p)) {
			printk(KERN_ERR PFX "Cannot register IRQ %d\n",
				p->irq);
			unlock_kernel();
			return -EBUSY;
		}
		p->initialized = true;
	}

	unlock_kernel();

	return nonseekable_open(inode, f);
}

static int cpwd_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long cpwd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	static const struct watchdog_info info = {
		.options		= WDIOF_SETTIMEOUT,
		.firmware_version	= 1,
		.identity		= DRIVER_NAME,
	};
	void __user *argp = (void __user *)arg;
	struct inode *inode = file->f_path.dentry->d_inode;
	int index = iminor(inode) - WD0_MINOR;
	struct cpwd *p = cpwd_device;
	int setopt = 0;

	switch (cmd) {
	/* Generic Linux IOCTLs */
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &info, sizeof(struct watchdog_info)))
			return -EFAULT;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		if (put_user(0, (int __user *)argp))
			return -EFAULT;
		break;

	case WDIOC_KEEPALIVE:
		cpwd_pingtimer(p, index);
		break;

	case WDIOC_SETOPTIONS:
		if (copy_from_user(&setopt, argp, sizeof(unsigned int)))
			return -EFAULT;

		if (setopt & WDIOS_DISABLECARD) {
			if (p->enabled)
				return -EINVAL;
			cpwd_stoptimer(p, index);
		} else if (setopt & WDIOS_ENABLECARD) {
			cpwd_starttimer(p, index);
		} else {
			return -EINVAL;
		}
		break;

	/* Solaris-compatible IOCTLs */
	case WIOCGSTAT:
		setopt = cpwd_getstatus(p, index);
		if (copy_to_user(argp, &setopt, sizeof(unsigned int)))
			return -EFAULT;
		break;

	case WIOCSTART:
		cpwd_starttimer(p, index);
		break;

	case WIOCSTOP:
		if (p->enabled)
			return -EINVAL;

		cpwd_stoptimer(p, index);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static long cpwd_compat_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	int rval = -ENOIOCTLCMD;

	switch (cmd) {
	/* solaris ioctls are specific to this driver */
	case WIOCSTART:
	case WIOCSTOP:
	case WIOCGSTAT:
		lock_kernel();
		rval = cpwd_ioctl(file, cmd, arg);
		unlock_kernel();
		break;

	/* everything else is handled by the generic compat layer */
	default:
		break;
	}

	return rval;
}

static ssize_t cpwd_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct cpwd *p = cpwd_device;
	int index = iminor(inode);

	if (count) {
		cpwd_pingtimer(p, index);
		return 1;
	}

	return 0;
}

static ssize_t cpwd_read(struct file *file, char __user *buffer,
			 size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static const struct file_operations cpwd_fops = {
	.owner =		THIS_MODULE,
	.unlocked_ioctl =	cpwd_ioctl,
	.compat_ioctl =		cpwd_compat_ioctl,
	.open =			cpwd_open,
	.write =		cpwd_write,
	.read =			cpwd_read,
	.release =		cpwd_release,
};

static int __devinit cpwd_probe(struct of_device *op,
				const struct of_device_id *match)
{
	struct device_node *options;
	const char *str_prop;
	const void *prop_val;
	int i, err = -EINVAL;
	struct cpwd *p;

	if (cpwd_device)
		return -EINVAL;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	err = -ENOMEM;
	if (!p) {
		printk(KERN_ERR PFX "Unable to allocate struct cpwd.\n");
		goto out;
	}

	p->irq = op->irqs[0];

	spin_lock_init(&p->lock);

	p->regs = of_ioremap(&op->resource[0], 0,
			     4 * WD_TIMER_REGSZ, DRIVER_NAME);
	if (!p->regs) {
		printk(KERN_ERR PFX "Unable to map registers.\n");
		goto out_free;
	}

	options = of_find_node_by_path("/options");
	err = -ENODEV;
	if (!options) {
		printk(KERN_ERR PFX "Unable to find /options node.\n");
		goto out_iounmap;
	}

	prop_val = of_get_property(options, "watchdog-enable?", NULL);
	p->enabled = (prop_val ? true : false);

	prop_val = of_get_property(options, "watchdog-reboot?", NULL);
	p->reboot = (prop_val ? true : false);

	str_prop = of_get_property(options, "watchdog-timeout", NULL);
	if (str_prop)
		p->timeout = simple_strtoul(str_prop, NULL, 10);

	/* CP1400s seem to have broken PLD implementations-- the
	 * interrupt_mask register cannot be written, so no timer
	 * interrupts can be masked within the PLD.
	 */
	str_prop = of_get_property(op->dev.of_node, "model", NULL);
	p->broken = (str_prop && !strcmp(str_prop, WD_BADMODEL));

	if (!p->enabled)
		cpwd_toggleintr(p, -1, WD_INTR_OFF);

	for (i = 0; i < WD_NUMDEVS; i++) {
		static const char *cpwd_names[] = { "RIC", "XIR", "POR" };
		static int *parms[] = { &wd0_timeout,
					&wd1_timeout,
					&wd2_timeout };
		struct miscdevice *mp = &p->devs[i].misc;

		mp->minor = WD0_MINOR + i;
		mp->name = cpwd_names[i];
		mp->fops = &cpwd_fops;

		p->devs[i].regs = p->regs + (i * WD_TIMER_REGSZ);
		p->devs[i].intr_mask = (WD0_INTR_MASK << i);
		p->devs[i].runstatus &= ~WD_STAT_BSTOP;
		p->devs[i].runstatus |= WD_STAT_INIT;
		p->devs[i].timeout = p->timeout;
		if (*parms[i])
			p->devs[i].timeout = *parms[i];

		err = misc_register(&p->devs[i].misc);
		if (err) {
			printk(KERN_ERR "Could not register misc device for "
			       "dev %d\n", i);
			goto out_unregister;
		}
	}

	if (p->broken) {
		init_timer(&cpwd_timer);
		cpwd_timer.function 	= cpwd_brokentimer;
		cpwd_timer.data		= (unsigned long) p;
		cpwd_timer.expires	= WD_BTIMEOUT;

		printk(KERN_INFO PFX "PLD defect workaround enabled for "
		       "model " WD_BADMODEL ".\n");
	}

	dev_set_drvdata(&op->dev, p);
	cpwd_device = p;
	err = 0;

out:
	return err;

out_unregister:
	for (i--; i >= 0; i--)
		misc_deregister(&p->devs[i].misc);

out_iounmap:
	of_iounmap(&op->resource[0], p->regs, 4 * WD_TIMER_REGSZ);

out_free:
	kfree(p);
	goto out;
}

static int __devexit cpwd_remove(struct of_device *op)
{
	struct cpwd *p = dev_get_drvdata(&op->dev);
	int i;

	for (i = 0; i < 4; i++) {
		misc_deregister(&p->devs[i].misc);

		if (!p->enabled) {
			cpwd_stoptimer(p, i);
			if (p->devs[i].runstatus & WD_STAT_BSTOP)
				cpwd_resetbrokentimer(p, i);
		}
	}

	if (p->broken)
		del_timer_sync(&cpwd_timer);

	if (p->initialized)
		free_irq(p->irq, p);

	of_iounmap(&op->resource[0], p->regs, 4 * WD_TIMER_REGSZ);
	kfree(p);

	cpwd_device = NULL;

	return 0;
}

static const struct of_device_id cpwd_match[] = {
	{
		.name = "watchdog",
	},
	{},
};
MODULE_DEVICE_TABLE(of, cpwd_match);

static struct of_platform_driver cpwd_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = cpwd_match,
	},
	.probe		= cpwd_probe,
	.remove		= __devexit_p(cpwd_remove),
};

static int __init cpwd_init(void)
{
	return of_register_driver(&cpwd_driver, &of_bus_type);
}

static void __exit cpwd_exit(void)
{
	of_unregister_driver(&cpwd_driver);
}

module_init(cpwd_init);
module_exit(cpwd_exit);
