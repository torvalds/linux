/* cpwatchdog.c - driver implementation for hardware watchdog
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/smp_lock.h>
#include <asm/irq.h>
#include <asm/ebus.h>
#include <asm/oplib.h>
#include <asm/uaccess.h>

#include <asm/watchdog.h>

#define WD_OBPNAME	"watchdog"
#define WD_BADMODEL "SUNW,501-5336"
#define WD_BTIMEOUT	(jiffies + (HZ * 1000))
#define WD_BLIMIT	0xFFFF

#define WD0_DEVNAME "watchdog0"
#define WD1_DEVNAME "watchdog1"
#define WD2_DEVNAME "watchdog2"

#define WD0_MINOR	212
#define WD1_MINOR	213	
#define WD2_MINOR	214	


/* Internal driver definitions
 */
#define WD0_ID			0		/* Watchdog0						*/
#define WD1_ID			1		/* Watchdog1						*/
#define WD2_ID			2		/* Watchdog2						*/
#define WD_NUMDEVS		3		/* Device contains 3 timers			*/

#define WD_INTR_OFF		0		/* Interrupt disable value			*/
#define WD_INTR_ON		1		/* Interrupt enable value			*/

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

/* Sun uses Altera PLD EPF8820ATC144-4 
 * providing three hardware watchdogs:
 *
 * 	1) RIC - sends an interrupt when triggered
 * 	2) XIR - asserts XIR_B_RESET when triggered, resets CPU
 * 	3) POR - asserts POR_B_RESET when triggered, resets CPU, backplane, board
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
 * 			Reading this register resets downcounter with 'limit' value.
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

/* Individual timer structure 
 */
struct wd_timer {
	__u16			timeout;
	__u8			intr_mask;
	unsigned char		runstatus;
	void __iomem		*regs;
};

/* Device structure
 */
struct wd_device {
	int				irq;
	spinlock_t		lock;
	unsigned char	isbaddoggie;	/* defective PLD */
	unsigned char	opt_enable;
	unsigned char	opt_reboot;
	unsigned short	opt_timeout;
	unsigned char	initialized;
	struct wd_timer	watchdog[WD_NUMDEVS];
	void __iomem	*regs;
};

static struct wd_device wd_dev = { 
		0, SPIN_LOCK_UNLOCKED, 0, 0, 0, 0,
};

static struct timer_list wd_timer;

static int wd0_timeout = 0;
static int wd1_timeout = 0;
static int wd2_timeout = 0;

#ifdef MODULE
module_param	(wd0_timeout, int, 0);
MODULE_PARM_DESC(wd0_timeout, "Default watchdog0 timeout in 1/10secs");
module_param 	(wd1_timeout, int, 0);
MODULE_PARM_DESC(wd1_timeout, "Default watchdog1 timeout in 1/10secs");
module_param 	(wd2_timeout, int, 0);
MODULE_PARM_DESC(wd2_timeout, "Default watchdog2 timeout in 1/10secs");

MODULE_AUTHOR
	("Eric Brower <ebrower@usa.net>");
MODULE_DESCRIPTION
	("Hardware watchdog driver for Sun Microsystems CP1400/1500");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE
	("watchdog");
#endif /* ifdef MODULE */

/* Forward declarations of internal methods
 */
#ifdef WD_DEBUG
static void wd_dumpregs(void);
#endif
static irqreturn_t wd_interrupt(int irq, void *dev_id);
static void wd_toggleintr(struct wd_timer* pTimer, int enable);
static void wd_pingtimer(struct wd_timer* pTimer);
static void wd_starttimer(struct wd_timer* pTimer);
static void wd_resetbrokentimer(struct wd_timer* pTimer);
static void wd_stoptimer(struct wd_timer* pTimer);
static void wd_brokentimer(unsigned long data);
static int  wd_getstatus(struct wd_timer* pTimer);

/* PLD expects words to be written in LSB format,
 * so we must flip all words prior to writing them to regs
 */
static inline unsigned short flip_word(unsigned short word)
{
	return ((word & 0xff) << 8) | ((word >> 8) & 0xff);
}

#define wd_writew(val, addr) 	(writew(flip_word(val), addr))
#define wd_readw(addr) 			(flip_word(readw(addr)))
#define wd_writeb(val, addr) 	(writeb(val, addr))
#define wd_readb(addr) 			(readb(addr))


/* CP1400s seem to have broken PLD implementations--
 * the interrupt_mask register cannot be written, so
 * no timer interrupts can be masked within the PLD.
 */
static inline int wd_isbroken(void)
{
	/* we could test this by read/write/read/restore
	 * on the interrupt mask register only if OBP
	 * 'watchdog-enable?' == FALSE, but it seems 
	 * ubiquitous on CP1400s
	 */
	char val[32];
	prom_getproperty(prom_root_node, "model", val, sizeof(val));
	return((!strcmp(val, WD_BADMODEL)) ? 1 : 0);
}
		
/* Retrieve watchdog-enable? option from OBP
 * Returns 0 if false, 1 if true
 */
static inline int wd_opt_enable(void)
{
	int opt_node;

	opt_node = prom_getchild(prom_root_node);
	opt_node = prom_searchsiblings(opt_node, "options");
	return((-1 == prom_getint(opt_node, "watchdog-enable?")) ? 0 : 1);
}

/* Retrieve watchdog-reboot? option from OBP
 * Returns 0 if false, 1 if true
 */
static inline int wd_opt_reboot(void)
{
	int opt_node;

	opt_node = prom_getchild(prom_root_node);
	opt_node = prom_searchsiblings(opt_node, "options");
	return((-1 == prom_getint(opt_node, "watchdog-reboot?")) ? 0 : 1);
}

/* Retrieve watchdog-timeout option from OBP
 * Returns OBP value, or 0 if not located
 */
static inline int wd_opt_timeout(void)
{
	int opt_node;
	char value[32];
	char *p = value;

	opt_node = prom_getchild(prom_root_node);
	opt_node = prom_searchsiblings(opt_node, "options");
	opt_node = prom_getproperty(opt_node, 
								"watchdog-timeout", 
								value, 
								sizeof(value));
	if(-1 != opt_node) {
		/* atoi implementation */
		for(opt_node = 0; /* nop */; p++) {
			if(*p >= '0' && *p <= '9') {
				opt_node = (10*opt_node)+(*p-'0');
			}
			else {
				break;
			}
		}
	}
	return((-1 == opt_node) ? (0) : (opt_node)); 
}

static int wd_open(struct inode *inode, struct file *f)
{
	switch(iminor(inode))
	{
		case WD0_MINOR:
			f->private_data = &wd_dev.watchdog[WD0_ID];
			break;
		case WD1_MINOR:
			f->private_data = &wd_dev.watchdog[WD1_ID];
			break;
		case WD2_MINOR:
			f->private_data = &wd_dev.watchdog[WD2_ID];
			break;
		default:
			return(-ENODEV);
	}

	/* Register IRQ on first open of device */
	if(0 == wd_dev.initialized)
	{	
		if (request_irq(wd_dev.irq, 
						&wd_interrupt, 
						IRQF_SHARED,
						WD_OBPNAME,
						(void *)wd_dev.regs)) {
			printk("%s: Cannot register IRQ %d\n", 
				WD_OBPNAME, wd_dev.irq);
			return(-EBUSY);
		}
		wd_dev.initialized = 1;
	}

	return(nonseekable_open(inode, f));
}

static int wd_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int wd_ioctl(struct inode *inode, struct file *file, 
		     unsigned int cmd, unsigned long arg)
{
	int 	setopt 				= 0;
	struct 	wd_timer* pTimer 	= (struct wd_timer*)file->private_data;
	void __user *argp = (void __user *)arg;
	struct 	watchdog_info info 	= {
		0,
		0,
		"Altera EPF8820ATC144-4"
	};

	if(NULL == pTimer) {
		return(-EINVAL);
	}

	switch(cmd)
	{
		/* Generic Linux IOCTLs */
		case WDIOC_GETSUPPORT:
			if(copy_to_user(argp, &info, sizeof(struct watchdog_info))) {
				return(-EFAULT);
			}
			break;
		case WDIOC_GETSTATUS:
		case WDIOC_GETBOOTSTATUS:
			if (put_user(0, (int __user *)argp))
				return -EFAULT;
			break;
		case WDIOC_KEEPALIVE:
			wd_pingtimer(pTimer);
			break;
		case WDIOC_SETOPTIONS:
			if(copy_from_user(&setopt, argp, sizeof(unsigned int))) {
				return -EFAULT;
			}
			if(setopt & WDIOS_DISABLECARD) {
				if(wd_dev.opt_enable) {
					printk(
						"%s: cannot disable watchdog in ENABLED mode\n",
						WD_OBPNAME);
					return(-EINVAL);
				}
				wd_stoptimer(pTimer);
			}
			else if(setopt & WDIOS_ENABLECARD) {
				wd_starttimer(pTimer);
			}
			else {
				return(-EINVAL);
			}	
			break;
		/* Solaris-compatible IOCTLs */
		case WIOCGSTAT:
			setopt = wd_getstatus(pTimer);
			if(copy_to_user(argp, &setopt, sizeof(unsigned int))) {
				return(-EFAULT);
			}
			break;
		case WIOCSTART:
			wd_starttimer(pTimer);
			break;
		case WIOCSTOP:
			if(wd_dev.opt_enable) {
				printk("%s: cannot disable watchdog in ENABLED mode\n",
					WD_OBPNAME);
				return(-EINVAL);
			}
			wd_stoptimer(pTimer);
			break;
		default:
			return(-EINVAL);
	}
	return(0);
}

static long wd_compat_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int rval = -ENOIOCTLCMD;

	switch (cmd) {
	/* solaris ioctls are specific to this driver */
	case WIOCSTART:
	case WIOCSTOP:
	case WIOCGSTAT:
		lock_kernel();
		rval = wd_ioctl(file->f_path.dentry->d_inode, file, cmd, arg);
		unlock_kernel();
		break;
	/* everything else is handled by the generic compat layer */
	default:
		break;
	}

	return rval;
}

static ssize_t wd_write(struct file 	*file, 
			const char	__user *buf, 
			size_t 		count, 
			loff_t 		*ppos)
{
	struct wd_timer* pTimer = (struct wd_timer*)file->private_data;

	if(NULL == pTimer) {
		return(-EINVAL);
	}

	if (count) {
		wd_pingtimer(pTimer);
		return 1;
	}
	return 0;
}

static ssize_t wd_read(struct file * file, char __user *buffer,
		        size_t count, loff_t *ppos)
{
#ifdef WD_DEBUG
	wd_dumpregs();
	return(0);
#else
	return(-EINVAL);
#endif /* ifdef WD_DEBUG */
}

static irqreturn_t wd_interrupt(int irq, void *dev_id)
{
	/* Only WD0 will interrupt-- others are NMI and we won't
	 * see them here....
	 */
	spin_lock_irq(&wd_dev.lock);
	if((unsigned long)wd_dev.regs == (unsigned long)dev_id)
	{
		wd_stoptimer(&wd_dev.watchdog[WD0_ID]);
		wd_dev.watchdog[WD0_ID].runstatus |=  WD_STAT_SVCD;
	}
	spin_unlock_irq(&wd_dev.lock);
	return IRQ_HANDLED;
}

static struct file_operations wd_fops = {
	.owner =	THIS_MODULE,
	.ioctl =	wd_ioctl,
	.compat_ioctl =	wd_compat_ioctl,
	.open =		wd_open,
	.write =	wd_write,
	.read =		wd_read,
	.release =	wd_release,
};

static struct miscdevice wd0_miscdev = { WD0_MINOR, WD0_DEVNAME, &wd_fops };
static struct miscdevice wd1_miscdev = { WD1_MINOR, WD1_DEVNAME, &wd_fops };
static struct miscdevice wd2_miscdev = { WD2_MINOR, WD2_DEVNAME, &wd_fops };

#ifdef WD_DEBUG
static void wd_dumpregs(void)
{
	/* Reading from downcounters initiates watchdog countdown--
	 * Example is included below for illustration purposes.
	 */
	int i;
	printk("%s: dumping register values\n", WD_OBPNAME);
	for(i = WD0_ID; i < WD_NUMDEVS; ++i) {
			/* printk("\t%s%i: dcntr  at 0x%lx: 0x%x\n", 
			 * 	WD_OBPNAME,
		 	 *	i,
			 *	(unsigned long)(&wd_dev.watchdog[i].regs->dcntr), 
			 *	readw(&wd_dev.watchdog[i].regs->dcntr));
			 */
			printk("\t%s%i: limit  at 0x%lx: 0x%x\n", 
				WD_OBPNAME,
				i,
				(unsigned long)(&wd_dev.watchdog[i].regs->limit), 
				readw(&wd_dev.watchdog[i].regs->limit));
			printk("\t%s%i: status at 0x%lx: 0x%x\n", 
				WD_OBPNAME,
				i,
				(unsigned long)(&wd_dev.watchdog[i].regs->status), 
				readb(&wd_dev.watchdog[i].regs->status));
			printk("\t%s%i: driver status: 0x%x\n",
				WD_OBPNAME,
				i,
				wd_getstatus(&wd_dev.watchdog[i]));
	}
	printk("\tintr_mask  at %p: 0x%x\n", 
		wd_dev.regs + PLD_IMASK,
		readb(wd_dev.regs + PLD_IMASK));
	printk("\tpld_status at %p: 0x%x\n", 
		wd_dev.regs + PLD_STATUS, 
		readb(wd_dev.regs + PLD_STATUS));
}
#endif

/* Enable or disable watchdog interrupts
 * Because of the CP1400 defect this should only be
 * called during initialzation or by wd_[start|stop]timer()
 *
 * pTimer 	- pointer to timer device, or NULL to indicate all timers 
 * enable	- non-zero to enable interrupts, zero to disable
 */
static void wd_toggleintr(struct wd_timer* pTimer, int enable)
{
	unsigned char curregs = wd_readb(wd_dev.regs + PLD_IMASK);
	unsigned char setregs = 
		(NULL == pTimer) ? 
			(WD0_INTR_MASK | WD1_INTR_MASK | WD2_INTR_MASK) : 
			(pTimer->intr_mask);

	(WD_INTR_ON == enable) ?
		(curregs &= ~setregs):
		(curregs |=  setregs);

	wd_writeb(curregs, wd_dev.regs + PLD_IMASK);
	return;
}

/* Reset countdown timer with 'limit' value and continue countdown.
 * This will not start a stopped timer.
 *
 * pTimer	- pointer to timer device
 */
static void wd_pingtimer(struct wd_timer* pTimer)
{
	if (wd_readb(pTimer->regs + WD_STATUS) & WD_S_RUNNING) {
		wd_readw(pTimer->regs + WD_DCNTR);
	}
}

/* Stop a running watchdog timer-- the timer actually keeps
 * running, but the interrupt is masked so that no action is
 * taken upon expiration.
 *
 * pTimer	- pointer to timer device
 */
static void wd_stoptimer(struct wd_timer* pTimer)
{
	if(wd_readb(pTimer->regs + WD_STATUS) & WD_S_RUNNING) {
		wd_toggleintr(pTimer, WD_INTR_OFF);

		if(wd_dev.isbaddoggie) {
			pTimer->runstatus |= WD_STAT_BSTOP;
			wd_brokentimer((unsigned long)&wd_dev);
		}
	}
}

/* Start a watchdog timer with the specified limit value
 * If the watchdog is running, it will be restarted with
 * the provided limit value.
 *
 * This function will enable interrupts on the specified
 * watchdog.
 *
 * pTimer	- pointer to timer device
 * limit	- limit (countdown) value in 1/10th seconds
 */
static void wd_starttimer(struct wd_timer* pTimer)
{
	if(wd_dev.isbaddoggie) {
		pTimer->runstatus &= ~WD_STAT_BSTOP;
	}
	pTimer->runstatus &= ~WD_STAT_SVCD;

	wd_writew(pTimer->timeout, pTimer->regs + WD_LIMIT);
	wd_toggleintr(pTimer, WD_INTR_ON);
}

/* Restarts timer with maximum limit value and
 * does not unset 'brokenstop' value.
 */
static void wd_resetbrokentimer(struct wd_timer* pTimer)
{
	wd_toggleintr(pTimer, WD_INTR_ON);
	wd_writew(WD_BLIMIT, pTimer->regs + WD_LIMIT);
}

/* Timer device initialization helper.
 * Returns 0 on success, other on failure
 */
static int wd_inittimer(int whichdog)
{
	struct miscdevice 				*whichmisc;
	void __iomem *whichregs;
	char 							whichident[8];
	int								whichmask;
	__u16							whichlimit;

	switch(whichdog)
	{
		case WD0_ID:
			whichmisc = &wd0_miscdev;
			strcpy(whichident, "RIC");
			whichregs = wd_dev.regs + WD0_OFF;
			whichmask = WD0_INTR_MASK;
			whichlimit= (0 == wd0_timeout) 	? 
						(wd_dev.opt_timeout): 
						(wd0_timeout);
			break;
		case WD1_ID:
			whichmisc = &wd1_miscdev;
			strcpy(whichident, "XIR");
			whichregs = wd_dev.regs + WD1_OFF;
			whichmask = WD1_INTR_MASK;
			whichlimit= (0 == wd1_timeout) 	? 
						(wd_dev.opt_timeout): 
						(wd1_timeout);
			break;
		case WD2_ID:
			whichmisc = &wd2_miscdev;
			strcpy(whichident, "POR");
			whichregs = wd_dev.regs + WD2_OFF;
			whichmask = WD2_INTR_MASK;
			whichlimit= (0 == wd2_timeout) 	? 
						(wd_dev.opt_timeout): 
						(wd2_timeout);
			break;
		default:
			printk("%s: %s: invalid watchdog id: %i\n",
				WD_OBPNAME, __FUNCTION__, whichdog);
			return(1);
	}
	if(0 != misc_register(whichmisc))
	{
		return(1);
	}
	wd_dev.watchdog[whichdog].regs			= whichregs;
	wd_dev.watchdog[whichdog].timeout 		= whichlimit;
	wd_dev.watchdog[whichdog].intr_mask		= whichmask;
	wd_dev.watchdog[whichdog].runstatus 	&= ~WD_STAT_BSTOP;
	wd_dev.watchdog[whichdog].runstatus 	|= WD_STAT_INIT;

	printk("%s%i: %s hardware watchdog [%01i.%i sec] %s\n", 
		WD_OBPNAME, 
		whichdog, 
		whichident, 
		wd_dev.watchdog[whichdog].timeout / 10,
		wd_dev.watchdog[whichdog].timeout % 10,
		(0 != wd_dev.opt_enable) ? "in ENABLED mode" : "");
	return(0);
}

/* Timer method called to reset stopped watchdogs--
 * because of the PLD bug on CP1400, we cannot mask
 * interrupts within the PLD so me must continually
 * reset the timers ad infinitum.
 */
static void wd_brokentimer(unsigned long data)
{
	struct wd_device* pDev = (struct wd_device*)data;
	int id, tripped = 0;

	/* kill a running timer instance, in case we
	 * were called directly instead of by kernel timer
	 */
	if(timer_pending(&wd_timer)) {
		del_timer(&wd_timer);
	}

	for(id = WD0_ID; id < WD_NUMDEVS; ++id) {
		if(pDev->watchdog[id].runstatus & WD_STAT_BSTOP) {
			++tripped;
			wd_resetbrokentimer(&pDev->watchdog[id]);
		}
	}

	if(tripped) {
		/* there is at least one timer brokenstopped-- reschedule */
		init_timer(&wd_timer);
		wd_timer.expires = WD_BTIMEOUT;
		add_timer(&wd_timer);
	}
}

static int wd_getstatus(struct wd_timer* pTimer)
{
	unsigned char stat = wd_readb(pTimer->regs + WD_STATUS);
	unsigned char intr = wd_readb(wd_dev.regs + PLD_IMASK);
	unsigned char ret  = WD_STOPPED;

	/* determine STOPPED */
	if(0 == stat ) { 
		return(ret);
	}
	/* determine EXPIRED vs FREERUN vs RUNNING */
	else if(WD_S_EXPIRED & stat) {
		ret = WD_EXPIRED;
	}
	else if(WD_S_RUNNING & stat) {
		if(intr & pTimer->intr_mask) {
			ret = WD_FREERUN;
		}
		else {
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
			if(wd_dev.isbaddoggie && (pTimer->runstatus & WD_STAT_BSTOP)) {
				if(pTimer->runstatus & WD_STAT_SVCD) {
					ret = WD_EXPIRED;
				}
				else {
					/* we could as well pretend we are expired */
					ret = WD_FREERUN;
				}
			}
			else {
				ret = WD_RUNNING;
			}
		}
	}

	/* determine SERVICED */
	if(pTimer->runstatus & WD_STAT_SVCD) {
		ret |= WD_SERVICED;
	}

	return(ret);
}

static int __init wd_init(void)
{
	int 	id;
	struct 	linux_ebus *ebus = NULL;
	struct 	linux_ebus_device *edev = NULL;

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->ofdev.node->name, WD_OBPNAME))
				goto ebus_done;
		}
	}

ebus_done:
	if(!edev) {
		printk("%s: unable to locate device\n", WD_OBPNAME);
		return -ENODEV;
	}

	wd_dev.regs = 
		ioremap(edev->resource[0].start, 4 * WD_TIMER_REGSZ); /* ? */

	if(NULL == wd_dev.regs) {
		printk("%s: unable to map registers\n", WD_OBPNAME);
		return(-ENODEV);
	}

	/* initialize device structure from OBP parameters */
	wd_dev.irq 			= edev->irqs[0];
	wd_dev.opt_enable	= wd_opt_enable();
	wd_dev.opt_reboot	= wd_opt_reboot();
	wd_dev.opt_timeout	= wd_opt_timeout();
	wd_dev.isbaddoggie	= wd_isbroken();

	/* disable all interrupts unless watchdog-enabled? == true */
	if(! wd_dev.opt_enable) {
		wd_toggleintr(NULL, WD_INTR_OFF);
	}

	/* register miscellaneous devices */
	for(id = WD0_ID; id < WD_NUMDEVS; ++id) {
		if(0 != wd_inittimer(id)) {
			printk("%s%i: unable to initialize\n", WD_OBPNAME, id);
		}
	}

	/* warn about possible defective PLD */
	if(wd_dev.isbaddoggie) {
		init_timer(&wd_timer);
		wd_timer.function 	= wd_brokentimer;
		wd_timer.data		= (unsigned long)&wd_dev;
		wd_timer.expires	= WD_BTIMEOUT;

		printk("%s: PLD defect workaround enabled for model %s\n",
			WD_OBPNAME, WD_BADMODEL);
	}
	return(0);
}

static void __exit wd_cleanup(void)
{
	int id;

	/* if 'watchdog-enable?' == TRUE, timers are not stopped 
	 * when module is unloaded.  All brokenstopped timers will
	 * also now eventually trip. 
	 */
	for(id = WD0_ID; id < WD_NUMDEVS; ++id) {
		if(WD_S_RUNNING == wd_readb(wd_dev.watchdog[id].regs + WD_STATUS)) {
			if(wd_dev.opt_enable) {
				printk(KERN_WARNING "%s%i: timer not stopped at release\n",
					WD_OBPNAME, id);
			}
			else {
				wd_stoptimer(&wd_dev.watchdog[id]);
				if(wd_dev.watchdog[id].runstatus & WD_STAT_BSTOP) {
					wd_resetbrokentimer(&wd_dev.watchdog[id]);
					printk(KERN_WARNING 
							"%s%i: defect workaround disabled at release, "\
							"timer expires in ~%01i sec\n",
							WD_OBPNAME, id, 
							wd_readw(wd_dev.watchdog[id].regs + WD_LIMIT) / 10);
				}
			}
		}
	}

	if(wd_dev.isbaddoggie && timer_pending(&wd_timer)) {
		del_timer(&wd_timer);
	}
	if(0 != (wd_dev.watchdog[WD0_ID].runstatus & WD_STAT_INIT)) {
		misc_deregister(&wd0_miscdev);
	}
	if(0 != (wd_dev.watchdog[WD1_ID].runstatus & WD_STAT_INIT)) {
		misc_deregister(&wd1_miscdev);
	}
	if(0 != (wd_dev.watchdog[WD2_ID].runstatus & WD_STAT_INIT)) {
		misc_deregister(&wd2_miscdev);
	}
	if(0 != wd_dev.initialized) {
		free_irq(wd_dev.irq, (void *)wd_dev.regs);
	}
	iounmap(wd_dev.regs);
}

module_init(wd_init);
module_exit(wd_cleanup);
