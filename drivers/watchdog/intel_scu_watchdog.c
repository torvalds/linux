/*
 *      Intel_SCU 0.2:  An Intel SCU IOH Based Watchdog Device
 *			for Intel part #(s):
 *				- AF82MP20 PCH
 *
 *      Copyright (C) 2009-2010 Intel Corporation. All rights reserved.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of version 2 of the GNU General
 *      Public License as published by the Free Software Foundation.
 *
 *      This program is distributed in the hope that it will be
 *      useful, but WITHOUT ANY WARRANTY; without even the implied
 *      warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *      PURPOSE.  See the GNU General Public License for more details.
 *      You should have received a copy of the GNU General Public
 *      License along with this program; if not, write to the Free
 *      Software Foundation, Inc., 59 Temple Place - Suite 330,
 *      Boston, MA  02111-1307, USA.
 *      The full GNU General Public License is included in this
 *      distribution in the file called COPYING.
 *
 */

#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/sfi.h>
#include <asm/irq.h>
#include <asm/atomic.h>
#include <asm/intel_scu_ipc.h>
#include <asm/apb_timer.h>
#include <asm/mrst.h>

#include "intel_scu_watchdog.h"

/* Bounds number of times we will retry loading time count */
/* This retry is a work around for a silicon bug.	   */
#define MAX_RETRY 16

#define IPC_SET_WATCHDOG_TIMER	0xF8

static int timer_margin = DEFAULT_SOFT_TO_HARD_MARGIN;
module_param(timer_margin, int, 0);
MODULE_PARM_DESC(timer_margin,
		"Watchdog timer margin"
		"Time between interrupt and resetting the system"
		"The range is from 1 to 160"
		"This is the time for all keep alives to arrive");

static int timer_set = DEFAULT_TIME;
module_param(timer_set, int, 0);
MODULE_PARM_DESC(timer_set,
		"Default Watchdog timer setting"
		"Complete cycle time"
		"The range is from 1 to 170"
		"This is the time for all keep alives to arrive");

/* After watchdog device is closed, check force_boot. If:
 * force_boot == 0, then force boot on next watchdog interrupt after close,
 * force_boot == 1, then force boot immediately when device is closed.
 */
static int force_boot;
module_param(force_boot, int, 0);
MODULE_PARM_DESC(force_boot,
		"A value of 1 means that the driver will reboot"
		"the system immediately if the /dev/watchdog device is closed"
		"A value of 0 means that when /dev/watchdog device is closed"
		"the watchdog timer will be refreshed for one more interval"
		"of length: timer_set. At the end of this interval, the"
		"watchdog timer will reset the system."
		);

/* there is only one device in the system now; this can be made into
 * an array in the future if we have more than one device */

static struct intel_scu_watchdog_dev watchdog_device;

/* Forces restart, if force_reboot is set */
static void watchdog_fire(void)
{
	if (force_boot) {
		printk(KERN_CRIT PFX "Initiating system reboot.\n");
		emergency_restart();
		printk(KERN_CRIT PFX "Reboot didn't ?????\n");
	}

	else {
		printk(KERN_CRIT PFX "Immediate Reboot Disabled\n");
		printk(KERN_CRIT PFX
			"System will reset when watchdog timer times out!\n");
	}
}

static int check_timer_margin(int new_margin)
{
	if ((new_margin < MIN_TIME_CYCLE) ||
	    (new_margin > MAX_TIME - timer_set)) {
		pr_debug("Watchdog timer: value of new_margin %d is out of the range %d to %d\n",
			  new_margin, MIN_TIME_CYCLE, MAX_TIME - timer_set);
		return -EINVAL;
	}
	return 0;
}

/*
 * IPC operations
 */
static int watchdog_set_ipc(int soft_threshold, int threshold)
{
	u32	*ipc_wbuf;
	u8	 cbuf[16] = { '\0' };
	int	 ipc_ret = 0;

	ipc_wbuf = (u32 *)&cbuf;
	ipc_wbuf[0] = soft_threshold;
	ipc_wbuf[1] = threshold;

	ipc_ret = intel_scu_ipc_command(
			IPC_SET_WATCHDOG_TIMER,
			0,
			ipc_wbuf,
			2,
			NULL,
			0);

	if (ipc_ret != 0)
		pr_err("Error setting SCU watchdog timer: %x\n", ipc_ret);

	return ipc_ret;
};

/*
 *      Intel_SCU operations
 */

/* timer interrupt handler */
static irqreturn_t watchdog_timer_interrupt(int irq, void *dev_id)
{
	int int_status;
	int_status = ioread32(watchdog_device.timer_interrupt_status_addr);

	pr_debug("Watchdog timer: irq, int_status: %x\n", int_status);

	if (int_status != 0)
		return IRQ_NONE;

	/* has the timer been started? If not, then this is spurious */
	if (watchdog_device.timer_started == 0) {
		pr_debug("Watchdog timer: spurious interrupt received\n");
		return IRQ_HANDLED;
	}

	/* temporarily disable the timer */
	iowrite32(0x00000002, watchdog_device.timer_control_addr);

	/* set the timer to the threshold */
	iowrite32(watchdog_device.threshold,
		  watchdog_device.timer_load_count_addr);

	/* allow the timer to run */
	iowrite32(0x00000003, watchdog_device.timer_control_addr);

	return IRQ_HANDLED;
}

static int intel_scu_keepalive(void)
{

	/* read eoi register - clears interrupt */
	ioread32(watchdog_device.timer_clear_interrupt_addr);

	/* temporarily disable the timer */
	iowrite32(0x00000002, watchdog_device.timer_control_addr);

	/* set the timer to the soft_threshold */
	iowrite32(watchdog_device.soft_threshold,
		  watchdog_device.timer_load_count_addr);

	/* allow the timer to run */
	iowrite32(0x00000003, watchdog_device.timer_control_addr);

	return 0;
}

static int intel_scu_stop(void)
{
	iowrite32(0, watchdog_device.timer_control_addr);
	return 0;
}

static int intel_scu_set_heartbeat(u32 t)
{
	int			 ipc_ret;
	int			 retry_count;
	u32			 soft_value;
	u32			 hw_pre_value;
	u32			 hw_value;

	watchdog_device.timer_set = t;
	watchdog_device.threshold =
		timer_margin * watchdog_device.timer_tbl_ptr->freq_hz;
	watchdog_device.soft_threshold =
		(watchdog_device.timer_set - timer_margin)
		* watchdog_device.timer_tbl_ptr->freq_hz;

	pr_debug("Watchdog timer: set_heartbeat: timer freq is %d\n",
		watchdog_device.timer_tbl_ptr->freq_hz);
	pr_debug("Watchdog timer: set_heartbeat: timer_set is %x (hex)\n",
		watchdog_device.timer_set);
	pr_debug("Watchdog timer: set_hearbeat: timer_margin is %x (hex)\n",
		timer_margin);
	pr_debug("Watchdog timer: set_heartbeat: threshold is %x (hex)\n",
		watchdog_device.threshold);
	pr_debug("Watchdog timer: set_heartbeat: soft_threshold is %x (hex)\n",
		watchdog_device.soft_threshold);

	/* Adjust thresholds by FREQ_ADJUSTMENT factor, to make the */
	/* watchdog timing come out right. */
	watchdog_device.threshold =
		watchdog_device.threshold / FREQ_ADJUSTMENT;
	watchdog_device.soft_threshold =
		watchdog_device.soft_threshold / FREQ_ADJUSTMENT;

	/* temporarily disable the timer */
	iowrite32(0x00000002, watchdog_device.timer_control_addr);

	/* send the threshold and soft_threshold via IPC to the processor */
	ipc_ret = watchdog_set_ipc(watchdog_device.soft_threshold,
				   watchdog_device.threshold);

	if (ipc_ret != 0) {
		/* Make sure the watchdog timer is stopped */
		intel_scu_stop();
		return ipc_ret;
	}

	/* Soft Threshold set loop. Early versions of silicon did */
	/* not always set this count correctly.  This loop checks */
	/* the value and retries if it was not set correctly.     */

	retry_count = 0;
	soft_value = watchdog_device.soft_threshold & 0xFFFF0000;
	do {

		/* Make sure timer is stopped */
		intel_scu_stop();

		if (MAX_RETRY < retry_count++) {
			/* Unable to set timer value */
			pr_err("Watchdog timer: Unable to set timer\n");
			return -ENODEV;
		}

		/* set the timer to the soft threshold */
		iowrite32(watchdog_device.soft_threshold,
			watchdog_device.timer_load_count_addr);

		/* read count value before starting timer */
		hw_pre_value = ioread32(watchdog_device.timer_load_count_addr);
		hw_pre_value = hw_pre_value & 0xFFFF0000;

		/* Start the timer */
		iowrite32(0x00000003, watchdog_device.timer_control_addr);

		/* read the value the time loaded into its count reg */
		hw_value = ioread32(watchdog_device.timer_load_count_addr);
		hw_value = hw_value & 0xFFFF0000;


	} while (soft_value != hw_value);

	watchdog_device.timer_started = 1;

	return 0;
}

/*
 * /dev/watchdog handling
 */

static int intel_scu_open(struct inode *inode, struct file *file)
{

	/* Set flag to indicate that watchdog device is open */
	if (test_and_set_bit(0, &watchdog_device.driver_open))
		return -EBUSY;

	/* Check for reopen of driver. Reopens are not allowed */
	if (watchdog_device.driver_closed)
		return -EPERM;

	return nonseekable_open(inode, file);
}

static int intel_scu_release(struct inode *inode, struct file *file)
{
	/*
	 * This watchdog should not be closed, after the timer
	 * is started with the WDIPC_SETTIMEOUT ioctl
	 * If force_boot is set watchdog_fire() will cause an
	 * immediate reset. If force_boot is not set, the watchdog
	 * timer is refreshed for one more interval. At the end
	 * of that interval, the watchdog timer will reset the system.
	 */

	if (!test_and_clear_bit(0, &watchdog_device.driver_open)) {
		pr_debug("Watchdog timer: intel_scu_release, without open\n");
		return -ENOTTY;
	}

	if (!watchdog_device.timer_started) {
		/* Just close, since timer has not been started */
		pr_debug("Watchdog timer: closed, without starting timer\n");
		return 0;
	}

	printk(KERN_CRIT PFX
	       "Unexpected close of /dev/watchdog!\n");

	/* Since the timer was started, prevent future reopens */
	watchdog_device.driver_closed = 1;

	/* Refresh the timer for one more interval */
	intel_scu_keepalive();

	/* Reboot system (if force_boot is set) */
	watchdog_fire();

	/* We should only reach this point if force_boot is not set */
	return 0;
}

static ssize_t intel_scu_write(struct file *file,
			      char const *data,
			      size_t len,
			      loff_t *ppos)
{

	if (watchdog_device.timer_started)
		/* Watchdog already started, keep it alive */
		intel_scu_keepalive();
	else
		/* Start watchdog with timer value set by init */
		intel_scu_set_heartbeat(watchdog_device.timer_set);

	return len;
}

static long intel_scu_ioctl(struct file *file,
			   unsigned int cmd,
			   unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	u32 __user *p = argp;
	u32 new_margin;


	static const struct watchdog_info ident = {
		.options =          WDIOF_SETTIMEOUT
				    | WDIOF_KEEPALIVEPING,
		.firmware_version = 0,  /* @todo Get from SCU via
						 ipc_get_scu_fw_version()? */
		.identity =         "Intel_SCU IOH Watchdog"  /* len < 32 */
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp,
				    &ident,
				    sizeof(ident)) ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);
	case WDIOC_KEEPALIVE:
		intel_scu_keepalive();

		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_margin, p))
			return -EFAULT;

		if (check_timer_margin(new_margin))
			return -EINVAL;

		if (intel_scu_set_heartbeat(new_margin))
			return -EINVAL;
		return 0;
	case WDIOC_GETTIMEOUT:
		return put_user(watchdog_device.soft_threshold, p);

	default:
		return -ENOTTY;
	}
}

/*
 *      Notifier for system down
 */
static int intel_scu_notify_sys(struct notifier_block *this,
			       unsigned long code,
			       void *another_unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		/* Turn off the watchdog timer. */
		intel_scu_stop();
	return NOTIFY_DONE;
}

/*
 *      Kernel Interfaces
 */
static const struct file_operations intel_scu_fops = {
	.owner          = THIS_MODULE,
	.llseek         = no_llseek,
	.write          = intel_scu_write,
	.unlocked_ioctl = intel_scu_ioctl,
	.open           = intel_scu_open,
	.release        = intel_scu_release,
};

static int __init intel_scu_watchdog_init(void)
{
	int ret;
	u32 __iomem *tmp_addr;

	/*
	 * We don't really need to check this as the SFI timer get will fail
	 * but if we do so we can exit with a clearer reason and no noise.
	 *
	 * If it isn't an intel MID device then it doesn't have this watchdog
	 */
	if (!mrst_identify_cpu())
		return -ENODEV;

	/* Check boot parameters to verify that their initial values */
	/* are in range. */
	/* Check value of timer_set boot parameter */
	if ((timer_set < MIN_TIME_CYCLE) ||
	    (timer_set > MAX_TIME - MIN_TIME_CYCLE)) {
		pr_err("Watchdog timer: value of timer_set %x (hex) "
		  "is out of range from %x to %x (hex)\n",
		  timer_set, MIN_TIME_CYCLE, MAX_TIME - MIN_TIME_CYCLE);
		return -EINVAL;
	}

	/* Check value of timer_margin boot parameter */
	if (check_timer_margin(timer_margin))
		return -EINVAL;

	watchdog_device.timer_tbl_ptr = sfi_get_mtmr(sfi_mtimer_num-1);

	if (watchdog_device.timer_tbl_ptr == NULL) {
		pr_debug("Watchdog timer - Intel SCU watchdog: timer is not available\n");
		return -ENODEV;
	}
	/* make sure the timer exists */
	if (watchdog_device.timer_tbl_ptr->phys_addr == 0) {
		pr_debug("Watchdog timer - Intel SCU watchdog - timer %d does not have valid physical memory\n",
								sfi_mtimer_num);
		return -ENODEV;
	}

	if (watchdog_device.timer_tbl_ptr->irq == 0) {
		pr_debug("Watchdog timer: timer %d invalid irq\n",
							sfi_mtimer_num);
		return -ENODEV;
	}

	tmp_addr = ioremap_nocache(watchdog_device.timer_tbl_ptr->phys_addr,
			20);

	if (tmp_addr == NULL) {
		pr_debug("Watchdog timer: timer unable to ioremap\n");
		return -ENOMEM;
	}

	watchdog_device.timer_load_count_addr = tmp_addr++;
	watchdog_device.timer_current_value_addr = tmp_addr++;
	watchdog_device.timer_control_addr = tmp_addr++;
	watchdog_device.timer_clear_interrupt_addr = tmp_addr++;
	watchdog_device.timer_interrupt_status_addr = tmp_addr++;

	/* Set the default time values in device structure */

	watchdog_device.timer_set = timer_set;
	watchdog_device.threshold =
		timer_margin * watchdog_device.timer_tbl_ptr->freq_hz;
	watchdog_device.soft_threshold =
		(watchdog_device.timer_set - timer_margin)
		* watchdog_device.timer_tbl_ptr->freq_hz;


	watchdog_device.intel_scu_notifier.notifier_call =
		intel_scu_notify_sys;

	ret = register_reboot_notifier(&watchdog_device.intel_scu_notifier);
	if (ret) {
		pr_err("Watchdog timer: cannot register notifier %d)\n", ret);
		goto register_reboot_error;
	}

	watchdog_device.miscdev.minor = WATCHDOG_MINOR;
	watchdog_device.miscdev.name = "watchdog";
	watchdog_device.miscdev.fops = &intel_scu_fops;

	ret = misc_register(&watchdog_device.miscdev);
	if (ret) {
		pr_err("Watchdog timer: cannot register miscdev %d err =%d\n",
							WATCHDOG_MINOR, ret);
		goto misc_register_error;
	}

	ret = request_irq((unsigned int)watchdog_device.timer_tbl_ptr->irq,
		watchdog_timer_interrupt,
		IRQF_SHARED, "watchdog",
		&watchdog_device.timer_load_count_addr);
	if (ret) {
		pr_err("Watchdog timer: error requesting irq %d\n", ret);
		goto request_irq_error;
	}
	/* Make sure timer is disabled before returning */
	intel_scu_stop();
	return 0;

/* error cleanup */

request_irq_error:
	misc_deregister(&watchdog_device.miscdev);
misc_register_error:
	unregister_reboot_notifier(&watchdog_device.intel_scu_notifier);
register_reboot_error:
	intel_scu_stop();
	iounmap(watchdog_device.timer_load_count_addr);
	return ret;
}

static void __exit intel_scu_watchdog_exit(void)
{

	misc_deregister(&watchdog_device.miscdev);
	unregister_reboot_notifier(&watchdog_device.intel_scu_notifier);
	/* disable the timer */
	iowrite32(0x00000002, watchdog_device.timer_control_addr);
	iounmap(watchdog_device.timer_load_count_addr);
}

late_initcall(intel_scu_watchdog_init);
module_exit(intel_scu_watchdog_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel SCU Watchdog Device Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_VERSION(WDT_VER);
