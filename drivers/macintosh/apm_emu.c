/* APM emulation layer for PowerMac
 * 
 * Copyright 2001 Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *
 * Lots of code inherited from apm.c, see appropriate notice in
 *  arch/i386/kernel/apm.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 *
 */

#include <linux/module.h>

#include <linux/poll.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/apm_bios.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/kernel.h>
#include <linux/smp_lock.h>

#include <linux/adb.h>
#include <linux/pmu.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(args...) printk(KERN_DEBUG args)
//#define DBG(args...) xmon_printf(args)
#else
#define DBG(args...) do { } while (0)
#endif

/*
 * The apm_bios device is one of the misc char devices.
 * This is its minor number.
 */
#define	APM_MINOR_DEV	134

/*
 * Maximum number of events stored
 */
#define APM_MAX_EVENTS		20

#define FAKE_APM_BIOS_VERSION	0x0101

#define APM_USER_NOTIFY_TIMEOUT	(5*HZ)

/*
 * The per-file APM data
 */
struct apm_user {
	int		magic;
	struct apm_user *	next;
	int		suser: 1;
	int		suspend_waiting: 1;
	int		suspends_pending;
	int		suspends_read;
	int		event_head;
	int		event_tail;
	apm_event_t	events[APM_MAX_EVENTS];
};

/*
 * The magic number in apm_user
 */
#define APM_BIOS_MAGIC		0x4101

/*
 * Local variables
 */
static int			suspends_pending;

static DECLARE_WAIT_QUEUE_HEAD(apm_waitqueue);
static DECLARE_WAIT_QUEUE_HEAD(apm_suspend_waitqueue);
static struct apm_user *	user_list;

static void apm_notify_sleep(struct pmu_sleep_notifier *self, int when);
static struct pmu_sleep_notifier apm_sleep_notifier = {
	apm_notify_sleep,
	SLEEP_LEVEL_USERLAND,
};

static const char driver_version[] = "0.5";	/* no spaces */

#ifdef DEBUG
static char *	apm_event_name[] = {
	"system standby",
	"system suspend",
	"normal resume",
	"critical resume",
	"low battery",
	"power status change",
	"update time",
	"critical suspend",
	"user standby",
	"user suspend",
	"system standby resume",
	"capabilities change"
};
#define NR_APM_EVENT_NAME	\
		(sizeof(apm_event_name) / sizeof(apm_event_name[0]))

#endif

static int queue_empty(struct apm_user *as)
{
	return as->event_head == as->event_tail;
}

static apm_event_t get_queued_event(struct apm_user *as)
{
	as->event_tail = (as->event_tail + 1) % APM_MAX_EVENTS;
	return as->events[as->event_tail];
}

static void queue_event(apm_event_t event, struct apm_user *sender)
{
	struct apm_user *	as;

	DBG("apm_emu: queue_event(%s)\n", apm_event_name[event-1]);
	if (user_list == NULL)
		return;
	for (as = user_list; as != NULL; as = as->next) {
		if (as == sender)
			continue;
		as->event_head = (as->event_head + 1) % APM_MAX_EVENTS;
		if (as->event_head == as->event_tail) {
			static int notified;

			if (notified++ == 0)
			    printk(KERN_ERR "apm_emu: an event queue overflowed\n");
			as->event_tail = (as->event_tail + 1) % APM_MAX_EVENTS;
		}
		as->events[as->event_head] = event;
		if (!as->suser)
			continue;
		switch (event) {
		case APM_SYS_SUSPEND:
		case APM_USER_SUSPEND:
			as->suspends_pending++;
			suspends_pending++;
			break;
		case APM_NORMAL_RESUME:
			as->suspend_waiting = 0;
			break;
		}
	}
	wake_up_interruptible(&apm_waitqueue);
}

static int check_apm_user(struct apm_user *as, const char *func)
{
	if ((as == NULL) || (as->magic != APM_BIOS_MAGIC)) {
		printk(KERN_ERR "apm_emu: %s passed bad filp\n", func);
		return 1;
	}
	return 0;
}

static ssize_t do_read(struct file *fp, char __user *buf, size_t count, loff_t *ppos)
{
	struct apm_user *	as;
	size_t			i;
	apm_event_t		event;
	DECLARE_WAITQUEUE(wait, current);

	as = fp->private_data;
	if (check_apm_user(as, "read"))
		return -EIO;
	if (count < sizeof(apm_event_t))
		return -EINVAL;
	if (queue_empty(as)) {
		if (fp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		add_wait_queue(&apm_waitqueue, &wait);
repeat:
		set_current_state(TASK_INTERRUPTIBLE);
		if (queue_empty(as) && !signal_pending(current)) {
			schedule();
			goto repeat;
		}
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&apm_waitqueue, &wait);
	}
	i = count;
	while ((i >= sizeof(event)) && !queue_empty(as)) {
		event = get_queued_event(as);
		DBG("apm_emu: do_read, returning: %s\n", apm_event_name[event-1]);
		if (copy_to_user(buf, &event, sizeof(event))) {
			if (i < count)
				break;
			return -EFAULT;
		}
		switch (event) {
		case APM_SYS_SUSPEND:
		case APM_USER_SUSPEND:
			as->suspends_read++;
			break;
		}
		buf += sizeof(event);
		i -= sizeof(event);
	}
	if (i < count)
		return count - i;
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

static unsigned int do_poll(struct file *fp, poll_table * wait)
{
	struct apm_user * as;

	as = fp->private_data;
	if (check_apm_user(as, "poll"))
		return 0;
	poll_wait(fp, &apm_waitqueue, wait);
	if (!queue_empty(as))
		return POLLIN | POLLRDNORM;
	return 0;
}

static int do_ioctl(struct inode * inode, struct file *filp,
		    u_int cmd, u_long arg)
{
	struct apm_user *	as;
	DECLARE_WAITQUEUE(wait, current);

	as = filp->private_data;
	if (check_apm_user(as, "ioctl"))
		return -EIO;
	if (!as->suser)
		return -EPERM;
	switch (cmd) {
	case APM_IOC_SUSPEND:
		/* If a suspend message was sent to userland, we
		 * consider this as a confirmation message
		 */
		if (as->suspends_read > 0) {
			as->suspends_read--;
			as->suspends_pending--;
			suspends_pending--;
		} else {
			// Route to PMU suspend ?
			break;
		}
		as->suspend_waiting = 1;
		add_wait_queue(&apm_waitqueue, &wait);
		DBG("apm_emu: ioctl waking up sleep waiter !\n");
		wake_up(&apm_suspend_waitqueue);
		mb();
		while(as->suspend_waiting && !signal_pending(current)) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		}
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&apm_waitqueue, &wait);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int do_release(struct inode * inode, struct file * filp)
{
	struct apm_user *	as;

	as = filp->private_data;
	if (check_apm_user(as, "release"))
		return 0;
	filp->private_data = NULL;
	lock_kernel();
	if (as->suspends_pending > 0) {
		suspends_pending -= as->suspends_pending;
		if (suspends_pending <= 0)
			wake_up(&apm_suspend_waitqueue);
	}
	if (user_list == as)
		user_list = as->next;
	else {
		struct apm_user *	as1;

		for (as1 = user_list;
		     (as1 != NULL) && (as1->next != as);
		     as1 = as1->next)
			;
		if (as1 == NULL)
			printk(KERN_ERR "apm: filp not in user list\n");
		else
			as1->next = as->next;
	}
	unlock_kernel();
	kfree(as);
	return 0;
}

static int do_open(struct inode * inode, struct file * filp)
{
	struct apm_user *	as;

	as = kmalloc(sizeof(*as), GFP_KERNEL);
	if (as == NULL) {
		printk(KERN_ERR "apm: cannot allocate struct of size %d bytes\n",
		       sizeof(*as));
		return -ENOMEM;
	}
	as->magic = APM_BIOS_MAGIC;
	as->event_tail = as->event_head = 0;
	as->suspends_pending = 0;
	as->suspends_read = 0;
	/*
	 * XXX - this is a tiny bit broken, when we consider BSD
         * process accounting. If the device is opened by root, we
	 * instantly flag that we used superuser privs. Who knows,
	 * we might close the device immediately without doing a
	 * privileged operation -- cevans
	 */
	as->suser = capable(CAP_SYS_ADMIN);
	as->next = user_list;
	user_list = as;
	filp->private_data = as;

	DBG("apm_emu: opened by %s, suser: %d\n", current->comm, (int)as->suser);

	return 0;
}

/* Wait for all clients to ack the suspend request. APM API
 * doesn't provide a way to NAK, but this could be added
 * here.
 */
static void wait_all_suspend(void)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&apm_suspend_waitqueue, &wait);
	DBG("apm_emu: wait_all_suspend(), suspends_pending: %d\n", suspends_pending);
	while(suspends_pending > 0) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&apm_suspend_waitqueue, &wait);

	DBG("apm_emu: wait_all_suspend() - complete !\n");
}

static void apm_notify_sleep(struct pmu_sleep_notifier *self, int when)
{
	switch(when) {
		case PBOOK_SLEEP_REQUEST:
			queue_event(APM_SYS_SUSPEND, NULL);
			wait_all_suspend();
			break;
		case PBOOK_WAKE:
			queue_event(APM_NORMAL_RESUME, NULL);
			break;
	}
}

#define APM_CRITICAL		10
#define APM_LOW			30

static int apm_emu_get_info(char *buf, char **start, off_t fpos, int length)
{
	/* Arguments, with symbols from linux/apm_bios.h.  Information is
	   from the Get Power Status (0x0a) call unless otherwise noted.

	   0) Linux driver version (this will change if format changes)
	   1) APM BIOS Version.  Usually 1.0, 1.1 or 1.2.
	   2) APM flags from APM Installation Check (0x00):
	      bit 0: APM_16_BIT_SUPPORT
	      bit 1: APM_32_BIT_SUPPORT
	      bit 2: APM_IDLE_SLOWS_CLOCK
	      bit 3: APM_BIOS_DISABLED
	      bit 4: APM_BIOS_DISENGAGED
	   3) AC line status
	      0x00: Off-line
	      0x01: On-line
	      0x02: On backup power (BIOS >= 1.1 only)
	      0xff: Unknown
	   4) Battery status
	      0x00: High
	      0x01: Low
	      0x02: Critical
	      0x03: Charging
	      0x04: Selected battery not present (BIOS >= 1.2 only)
	      0xff: Unknown
	   5) Battery flag
	      bit 0: High
	      bit 1: Low
	      bit 2: Critical
	      bit 3: Charging
	      bit 7: No system battery
	      0xff: Unknown
	   6) Remaining battery life (percentage of charge):
	      0-100: valid
	      -1: Unknown
	   7) Remaining battery life (time units):
	      Number of remaining minutes or seconds
	      -1: Unknown
	   8) min = minutes; sec = seconds */

	unsigned short  ac_line_status;
	unsigned short  battery_status = 0;
	unsigned short  battery_flag   = 0xff;
	int		percentage     = -1;
	int             time_units     = -1;
	int		real_count     = 0;
	int		i;
	char *		p = buf;
	char		charging       = 0;
	long		charge	       = -1;
	long		amperage       = 0;
	unsigned long	btype          = 0;

	ac_line_status = ((pmu_power_flags & PMU_PWR_AC_PRESENT) != 0);
	for (i=0; i<pmu_battery_count; i++) {
		if (pmu_batteries[i].flags & PMU_BATT_PRESENT) {
			battery_status++;
			if (percentage < 0)
				percentage = 0;
			if (charge < 0)
				charge = 0;
			percentage += (pmu_batteries[i].charge * 100) /
				pmu_batteries[i].max_charge;
			charge += pmu_batteries[i].charge;
			amperage += pmu_batteries[i].amperage;
			if (btype == 0)
				btype = (pmu_batteries[i].flags & PMU_BATT_TYPE_MASK);
			real_count++;
			if ((pmu_batteries[i].flags & PMU_BATT_CHARGING))
				charging++;
		}
	}
	if (0 == battery_status)
		ac_line_status = 1;
	battery_status = 0xff;
	if (real_count) {
		if (amperage < 0) {
			if (btype == PMU_BATT_TYPE_SMART)
				time_units = (charge * 59) / (amperage * -1);
			else
				time_units = (charge * 16440) / (amperage * -60);
		}
		percentage /= real_count;
		if (charging > 0) {
			battery_status = 0x03;
			battery_flag = 0x08;
		} else if (percentage <= APM_CRITICAL) {
			battery_status = 0x02;
			battery_flag = 0x04;
		} else if (percentage <= APM_LOW) {
			battery_status = 0x01;
			battery_flag = 0x02;
		} else {
			battery_status = 0x00;
			battery_flag = 0x01;
		}
	}
	p += sprintf(p, "%s %d.%d 0x%02x 0x%02x 0x%02x 0x%02x %d%% %d %s\n",
		     driver_version,
		     (FAKE_APM_BIOS_VERSION >> 8) & 0xff,
		     FAKE_APM_BIOS_VERSION & 0xff,
		     0,
		     ac_line_status,
		     battery_status,
		     battery_flag,
		     percentage,
		     time_units,
		     "min");

	return p - buf;
}

static const struct file_operations apm_bios_fops = {
	.owner		= THIS_MODULE,
	.read		= do_read,
	.poll		= do_poll,
	.ioctl		= do_ioctl,
	.open		= do_open,
	.release	= do_release,
};

static struct miscdevice apm_device = {
	APM_MINOR_DEV,
	"apm_bios",
	&apm_bios_fops
};

static int __init apm_emu_init(void)
{
	struct proc_dir_entry *apm_proc;

	if (sys_ctrler != SYS_CTRLER_PMU) {
		printk(KERN_INFO "apm_emu: Requires a machine with a PMU.\n");
		return -ENODEV;
	}
		
	apm_proc = create_proc_info_entry("apm", 0, NULL, apm_emu_get_info);
	if (apm_proc)
		apm_proc->owner = THIS_MODULE;

	if (misc_register(&apm_device) != 0)
		printk(KERN_INFO "Could not create misc. device for apm\n");

	pmu_register_sleep_notifier(&apm_sleep_notifier);

	printk(KERN_INFO "apm_emu: APM Emulation %s initialized.\n", driver_version);

	return 0;
}

static void __exit apm_emu_exit(void)
{
	pmu_unregister_sleep_notifier(&apm_sleep_notifier);
	misc_deregister(&apm_device);
	remove_proc_entry("apm", NULL);

	printk(KERN_INFO "apm_emu: APM Emulation removed.\n");
}

module_init(apm_emu_init);
module_exit(apm_emu_exit);

MODULE_AUTHOR("Benjamin Herrenschmidt");
MODULE_DESCRIPTION("APM emulation layer for PowerMac");
MODULE_LICENSE("GPL");

