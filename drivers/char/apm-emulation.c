/*
 * bios-less APM driver for ARM Linux
 *  Jamey Hicks <jamey@crl.dec.com>
 *  adapted from the APM BIOS driver for Linux by Stephen Rothwell (sfr@linuxcare.com)
 *
 * APM 1.2 Reference:
 *   Intel Corporation, Microsoft Corporation. Advanced Power Management
 *   (APM) BIOS Interface Specification, Revision 1.2, February 1996.
 *
 * [This document is available from Microsoft at:
 *    http://www.microsoft.com/hwdev/busbios/amp_12.htm]
 */
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/miscdevice.h>
#include <linux/apm_bios.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/suspend.h>
#include <linux/apm-emulation.h>
#include <linux/freezer.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#include <asm/system.h>

/*
 * The apm_bios device is one of the misc char devices.
 * This is its minor number.
 */
#define APM_MINOR_DEV	134

/*
 * See Documentation/Config.help for the configuration options.
 *
 * Various options can be changed at boot time as follows:
 * (We allow underscores for compatibility with the modules code)
 *	apm=on/off			enable/disable APM
 */

/*
 * Maximum number of events stored
 */
#define APM_MAX_EVENTS		16

struct apm_queue {
	unsigned int		event_head;
	unsigned int		event_tail;
	apm_event_t		events[APM_MAX_EVENTS];
};

/*
 * thread states (for threads using a writable /dev/apm_bios fd):
 *
 * SUSPEND_NONE:	nothing happening
 * SUSPEND_PENDING:	suspend event queued for thread and pending to be read
 * SUSPEND_READ:	suspend event read, pending acknowledgement
 * SUSPEND_ACKED:	acknowledgement received from thread (via ioctl),
 *			waiting for resume
 * SUSPEND_ACKTO:	acknowledgement timeout
 * SUSPEND_DONE:	thread had acked suspend and is now notified of
 *			resume
 *
 * SUSPEND_WAIT:	this thread invoked suspend and is waiting for resume
 *
 * A thread migrates in one of three paths:
 *	NONE -1-> PENDING -2-> READ -3-> ACKED -4-> DONE -5-> NONE
 *				    -6-> ACKTO -7-> NONE
 *	NONE -8-> WAIT -9-> NONE
 *
 * While in PENDING or READ, the thread is accounted for in the
 * suspend_acks_pending counter.
 *
 * The transitions are invoked as follows:
 *	1: suspend event is signalled from the core PM code
 *	2: the suspend event is read from the fd by the userspace thread
 *	3: userspace thread issues the APM_IOC_SUSPEND ioctl (as ack)
 *	4: core PM code signals that we have resumed
 *	5: APM_IOC_SUSPEND ioctl returns
 *
 *	6: the notifier invoked from the core PM code timed out waiting
 *	   for all relevant threds to enter ACKED state and puts those
 *	   that haven't into ACKTO
 *	7: those threads issue APM_IOC_SUSPEND ioctl too late,
 *	   get an error
 *
 *	8: userspace thread issues the APM_IOC_SUSPEND ioctl (to suspend),
 *	   ioctl code invokes pm_suspend()
 *	9: pm_suspend() returns indicating resume
 */
enum apm_suspend_state {
	SUSPEND_NONE,
	SUSPEND_PENDING,
	SUSPEND_READ,
	SUSPEND_ACKED,
	SUSPEND_ACKTO,
	SUSPEND_WAIT,
	SUSPEND_DONE,
};

/*
 * The per-file APM data
 */
struct apm_user {
	struct list_head	list;

	unsigned int		suser: 1;
	unsigned int		writer: 1;
	unsigned int		reader: 1;

	int			suspend_result;
	enum apm_suspend_state	suspend_state;

	struct apm_queue	queue;
};

/*
 * Local variables
 */
static atomic_t suspend_acks_pending = ATOMIC_INIT(0);
static atomic_t userspace_notification_inhibit = ATOMIC_INIT(0);
static int apm_disabled;
static struct task_struct *kapmd_tsk;

static DECLARE_WAIT_QUEUE_HEAD(apm_waitqueue);
static DECLARE_WAIT_QUEUE_HEAD(apm_suspend_waitqueue);

/*
 * This is a list of everyone who has opened /dev/apm_bios
 */
static DECLARE_RWSEM(user_list_lock);
static LIST_HEAD(apm_user_list);

/*
 * kapmd info.  kapmd provides us a process context to handle
 * "APM" events within - specifically necessary if we're going
 * to be suspending the system.
 */
static DECLARE_WAIT_QUEUE_HEAD(kapmd_wait);
static DEFINE_SPINLOCK(kapmd_queue_lock);
static struct apm_queue kapmd_queue;

static DEFINE_MUTEX(state_lock);

static const char driver_version[] = "1.13";	/* no spaces */



/*
 * Compatibility cruft until the IPAQ people move over to the new
 * interface.
 */
static void __apm_get_power_status(struct apm_power_info *info)
{
}

/*
 * This allows machines to provide their own "apm get power status" function.
 */
void (*apm_get_power_status)(struct apm_power_info *) = __apm_get_power_status;
EXPORT_SYMBOL(apm_get_power_status);


/*
 * APM event queue management.
 */
static inline int queue_empty(struct apm_queue *q)
{
	return q->event_head == q->event_tail;
}

static inline apm_event_t queue_get_event(struct apm_queue *q)
{
	q->event_tail = (q->event_tail + 1) % APM_MAX_EVENTS;
	return q->events[q->event_tail];
}

static void queue_add_event(struct apm_queue *q, apm_event_t event)
{
	q->event_head = (q->event_head + 1) % APM_MAX_EVENTS;
	if (q->event_head == q->event_tail) {
		static int notified;

		if (notified++ == 0)
		    printk(KERN_ERR "apm: an event queue overflowed\n");
		q->event_tail = (q->event_tail + 1) % APM_MAX_EVENTS;
	}
	q->events[q->event_head] = event;
}

static void queue_event(apm_event_t event)
{
	struct apm_user *as;

	down_read(&user_list_lock);
	list_for_each_entry(as, &apm_user_list, list) {
		if (as->reader)
			queue_add_event(&as->queue, event);
	}
	up_read(&user_list_lock);
	wake_up_interruptible(&apm_waitqueue);
}

static ssize_t apm_read(struct file *fp, char __user *buf, size_t count, loff_t *ppos)
{
	struct apm_user *as = fp->private_data;
	apm_event_t event;
	int i = count, ret = 0;

	if (count < sizeof(apm_event_t))
		return -EINVAL;

	if (queue_empty(&as->queue) && fp->f_flags & O_NONBLOCK)
		return -EAGAIN;

	wait_event_interruptible(apm_waitqueue, !queue_empty(&as->queue));

	while ((i >= sizeof(event)) && !queue_empty(&as->queue)) {
		event = queue_get_event(&as->queue);

		ret = -EFAULT;
		if (copy_to_user(buf, &event, sizeof(event)))
			break;

		mutex_lock(&state_lock);
		if (as->suspend_state == SUSPEND_PENDING &&
		    (event == APM_SYS_SUSPEND || event == APM_USER_SUSPEND))
			as->suspend_state = SUSPEND_READ;
		mutex_unlock(&state_lock);

		buf += sizeof(event);
		i -= sizeof(event);
	}

	if (i < count)
		ret = count - i;

	return ret;
}

static unsigned int apm_poll(struct file *fp, poll_table * wait)
{
	struct apm_user *as = fp->private_data;

	poll_wait(fp, &apm_waitqueue, wait);
	return queue_empty(&as->queue) ? 0 : POLLIN | POLLRDNORM;
}

/*
 * apm_ioctl - handle APM ioctl
 *
 * APM_IOC_SUSPEND
 *   This IOCTL is overloaded, and performs two functions.  It is used to:
 *     - initiate a suspend
 *     - acknowledge a suspend read from /dev/apm_bios.
 *   Only when everyone who has opened /dev/apm_bios with write permission
 *   has acknowledge does the actual suspend happen.
 */
static int
apm_ioctl(struct inode * inode, struct file *filp, u_int cmd, u_long arg)
{
	struct apm_user *as = filp->private_data;
	int err = -EINVAL;

	if (!as->suser || !as->writer)
		return -EPERM;

	switch (cmd) {
	case APM_IOC_SUSPEND:
		mutex_lock(&state_lock);

		as->suspend_result = -EINTR;

		switch (as->suspend_state) {
		case SUSPEND_READ:
			/*
			 * If we read a suspend command from /dev/apm_bios,
			 * then the corresponding APM_IOC_SUSPEND ioctl is
			 * interpreted as an acknowledge.
			 */
			as->suspend_state = SUSPEND_ACKED;
			atomic_dec(&suspend_acks_pending);
			mutex_unlock(&state_lock);

			/*
			 * suspend_acks_pending changed, the notifier needs to
			 * be woken up for this
			 */
			wake_up(&apm_suspend_waitqueue);

			/*
			 * Wait for the suspend/resume to complete.  If there
			 * are pending acknowledges, we wait here for them.
			 */
			freezer_do_not_count();

			wait_event(apm_suspend_waitqueue,
				   as->suspend_state == SUSPEND_DONE);

			/*
			 * Since we are waiting until the suspend is done, the
			 * try_to_freeze() in freezer_count() will not trigger
			 */
			freezer_count();
			break;
		case SUSPEND_ACKTO:
			as->suspend_result = -ETIMEDOUT;
			mutex_unlock(&state_lock);
			break;
		default:
			as->suspend_state = SUSPEND_WAIT;
			mutex_unlock(&state_lock);

			/*
			 * Otherwise it is a request to suspend the system.
			 * Just invoke pm_suspend(), we'll handle it from
			 * there via the notifier.
			 */
			as->suspend_result = pm_suspend(PM_SUSPEND_MEM);
		}

		mutex_lock(&state_lock);
		err = as->suspend_result;
		as->suspend_state = SUSPEND_NONE;
		mutex_unlock(&state_lock);
		break;
	}

	return err;
}

static int apm_release(struct inode * inode, struct file * filp)
{
	struct apm_user *as = filp->private_data;

	filp->private_data = NULL;

	down_write(&user_list_lock);
	list_del(&as->list);
	up_write(&user_list_lock);

	/*
	 * We are now unhooked from the chain.  As far as new
	 * events are concerned, we no longer exist.
	 */
	mutex_lock(&state_lock);
	if (as->suspend_state == SUSPEND_PENDING ||
	    as->suspend_state == SUSPEND_READ)
		atomic_dec(&suspend_acks_pending);
	mutex_unlock(&state_lock);

	wake_up(&apm_suspend_waitqueue);

	kfree(as);
	return 0;
}

static int apm_open(struct inode * inode, struct file * filp)
{
	struct apm_user *as;

	lock_kernel();
	as = kzalloc(sizeof(*as), GFP_KERNEL);
	if (as) {
		/*
		 * XXX - this is a tiny bit broken, when we consider BSD
		 * process accounting. If the device is opened by root, we
		 * instantly flag that we used superuser privs. Who knows,
		 * we might close the device immediately without doing a
		 * privileged operation -- cevans
		 */
		as->suser = capable(CAP_SYS_ADMIN);
		as->writer = (filp->f_mode & FMODE_WRITE) == FMODE_WRITE;
		as->reader = (filp->f_mode & FMODE_READ) == FMODE_READ;

		down_write(&user_list_lock);
		list_add(&as->list, &apm_user_list);
		up_write(&user_list_lock);

		filp->private_data = as;
	}
	unlock_kernel();

	return as ? 0 : -ENOMEM;
}

static struct file_operations apm_bios_fops = {
	.owner		= THIS_MODULE,
	.read		= apm_read,
	.poll		= apm_poll,
	.ioctl		= apm_ioctl,
	.open		= apm_open,
	.release	= apm_release,
};

static struct miscdevice apm_device = {
	.minor		= APM_MINOR_DEV,
	.name		= "apm_bios",
	.fops		= &apm_bios_fops
};


#ifdef CONFIG_PROC_FS
/*
 * Arguments, with symbols from linux/apm_bios.h.
 *
 *   0) Linux driver version (this will change if format changes)
 *   1) APM BIOS Version.  Usually 1.0, 1.1 or 1.2.
 *   2) APM flags from APM Installation Check (0x00):
 *	bit 0: APM_16_BIT_SUPPORT
 *	bit 1: APM_32_BIT_SUPPORT
 *	bit 2: APM_IDLE_SLOWS_CLOCK
 *	bit 3: APM_BIOS_DISABLED
 *	bit 4: APM_BIOS_DISENGAGED
 *   3) AC line status
 *	0x00: Off-line
 *	0x01: On-line
 *	0x02: On backup power (BIOS >= 1.1 only)
 *	0xff: Unknown
 *   4) Battery status
 *	0x00: High
 *	0x01: Low
 *	0x02: Critical
 *	0x03: Charging
 *	0x04: Selected battery not present (BIOS >= 1.2 only)
 *	0xff: Unknown
 *   5) Battery flag
 *	bit 0: High
 *	bit 1: Low
 *	bit 2: Critical
 *	bit 3: Charging
 *	bit 7: No system battery
 *	0xff: Unknown
 *   6) Remaining battery life (percentage of charge):
 *	0-100: valid
 *	-1: Unknown
 *   7) Remaining battery life (time units):
 *	Number of remaining minutes or seconds
 *	-1: Unknown
 *   8) min = minutes; sec = seconds
 */
static int proc_apm_show(struct seq_file *m, void *v)
{
	struct apm_power_info info;
	char *units;

	info.ac_line_status = 0xff;
	info.battery_status = 0xff;
	info.battery_flag   = 0xff;
	info.battery_life   = -1;
	info.time	    = -1;
	info.units	    = -1;

	if (apm_get_power_status)
		apm_get_power_status(&info);

	switch (info.units) {
	default:	units = "?";	break;
	case 0: 	units = "min";	break;
	case 1: 	units = "sec";	break;
	}

	seq_printf(m, "%s 1.2 0x%02x 0x%02x 0x%02x 0x%02x %d%% %d %s\n",
		     driver_version, APM_32_BIT_SUPPORT,
		     info.ac_line_status, info.battery_status,
		     info.battery_flag, info.battery_life,
		     info.time, units);

	return 0;
}

static int proc_apm_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_apm_show, NULL);
}

static const struct file_operations apm_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_apm_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static int kapmd(void *arg)
{
	do {
		apm_event_t event;

		wait_event_interruptible(kapmd_wait,
				!queue_empty(&kapmd_queue) || kthread_should_stop());

		if (kthread_should_stop())
			break;

		spin_lock_irq(&kapmd_queue_lock);
		event = 0;
		if (!queue_empty(&kapmd_queue))
			event = queue_get_event(&kapmd_queue);
		spin_unlock_irq(&kapmd_queue_lock);

		switch (event) {
		case 0:
			break;

		case APM_LOW_BATTERY:
		case APM_POWER_STATUS_CHANGE:
			queue_event(event);
			break;

		case APM_USER_SUSPEND:
		case APM_SYS_SUSPEND:
			pm_suspend(PM_SUSPEND_MEM);
			break;

		case APM_CRITICAL_SUSPEND:
			atomic_inc(&userspace_notification_inhibit);
			pm_suspend(PM_SUSPEND_MEM);
			atomic_dec(&userspace_notification_inhibit);
			break;
		}
	} while (1);

	return 0;
}

static int apm_suspend_notifier(struct notifier_block *nb,
				unsigned long event,
				void *dummy)
{
	struct apm_user *as;
	int err;

	/* short-cut emergency suspends */
	if (atomic_read(&userspace_notification_inhibit))
		return NOTIFY_DONE;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		/*
		 * Queue an event to all "writer" users that we want
		 * to suspend and need their ack.
		 */
		mutex_lock(&state_lock);
		down_read(&user_list_lock);

		list_for_each_entry(as, &apm_user_list, list) {
			if (as->suspend_state != SUSPEND_WAIT && as->reader &&
			    as->writer && as->suser) {
				as->suspend_state = SUSPEND_PENDING;
				atomic_inc(&suspend_acks_pending);
				queue_add_event(&as->queue, APM_USER_SUSPEND);
			}
		}

		up_read(&user_list_lock);
		mutex_unlock(&state_lock);
		wake_up_interruptible(&apm_waitqueue);

		/*
		 * Wait for the the suspend_acks_pending variable to drop to
		 * zero, meaning everybody acked the suspend event (or the
		 * process was killed.)
		 *
		 * If the app won't answer within a short while we assume it
		 * locked up and ignore it.
		 */
		err = wait_event_interruptible_timeout(
			apm_suspend_waitqueue,
			atomic_read(&suspend_acks_pending) == 0,
			5*HZ);

		/* timed out */
		if (err == 0) {
			/*
			 * Move anybody who timed out to "ack timeout" state.
			 *
			 * We could time out and the userspace does the ACK
			 * right after we time out but before we enter the
			 * locked section here, but that's fine.
			 */
			mutex_lock(&state_lock);
			down_read(&user_list_lock);
			list_for_each_entry(as, &apm_user_list, list) {
				if (as->suspend_state == SUSPEND_PENDING ||
				    as->suspend_state == SUSPEND_READ) {
					as->suspend_state = SUSPEND_ACKTO;
					atomic_dec(&suspend_acks_pending);
				}
			}
			up_read(&user_list_lock);
			mutex_unlock(&state_lock);
		}

		/* let suspend proceed */
		if (err >= 0)
			return NOTIFY_OK;

		/* interrupted by signal */
		return NOTIFY_BAD;

	case PM_POST_SUSPEND:
		/*
		 * Anyone on the APM queues will think we're still suspended.
		 * Send a message so everyone knows we're now awake again.
		 */
		queue_event(APM_NORMAL_RESUME);

		/*
		 * Finally, wake up anyone who is sleeping on the suspend.
		 */
		mutex_lock(&state_lock);
		down_read(&user_list_lock);
		list_for_each_entry(as, &apm_user_list, list) {
			if (as->suspend_state == SUSPEND_ACKED) {
				/*
				 * TODO: maybe grab error code, needs core
				 * changes to push the error to the notifier
				 * chain (could use the second parameter if
				 * implemented)
				 */
				as->suspend_result = 0;
				as->suspend_state = SUSPEND_DONE;
			}
		}
		up_read(&user_list_lock);
		mutex_unlock(&state_lock);

		wake_up(&apm_suspend_waitqueue);
		return NOTIFY_OK;

	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block apm_notif_block = {
	.notifier_call = apm_suspend_notifier,
};

static int __init apm_init(void)
{
	int ret;

	if (apm_disabled) {
		printk(KERN_NOTICE "apm: disabled on user request.\n");
		return -ENODEV;
	}

	kapmd_tsk = kthread_create(kapmd, NULL, "kapmd");
	if (IS_ERR(kapmd_tsk)) {
		ret = PTR_ERR(kapmd_tsk);
		kapmd_tsk = NULL;
		goto out;
	}
	wake_up_process(kapmd_tsk);

#ifdef CONFIG_PROC_FS
	proc_create("apm", 0, NULL, &apm_proc_fops);
#endif

	ret = misc_register(&apm_device);
	if (ret)
		goto out_stop;

	ret = register_pm_notifier(&apm_notif_block);
	if (ret)
		goto out_unregister;

	return 0;

 out_unregister:
	misc_deregister(&apm_device);
 out_stop:
	remove_proc_entry("apm", NULL);
	kthread_stop(kapmd_tsk);
 out:
	return ret;
}

static void __exit apm_exit(void)
{
	unregister_pm_notifier(&apm_notif_block);
	misc_deregister(&apm_device);
	remove_proc_entry("apm", NULL);

	kthread_stop(kapmd_tsk);
}

module_init(apm_init);
module_exit(apm_exit);

MODULE_AUTHOR("Stephen Rothwell");
MODULE_DESCRIPTION("Advanced Power Management");
MODULE_LICENSE("GPL");

#ifndef MODULE
static int __init apm_setup(char *str)
{
	while ((str != NULL) && (*str != '\0')) {
		if (strncmp(str, "off", 3) == 0)
			apm_disabled = 1;
		if (strncmp(str, "on", 2) == 0)
			apm_disabled = 0;
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}
	return 1;
}

__setup("apm=", apm_setup);
#endif

/**
 * apm_queue_event - queue an APM event for kapmd
 * @event: APM event
 *
 * Queue an APM event for kapmd to process and ultimately take the
 * appropriate action.  Only a subset of events are handled:
 *   %APM_LOW_BATTERY
 *   %APM_POWER_STATUS_CHANGE
 *   %APM_USER_SUSPEND
 *   %APM_SYS_SUSPEND
 *   %APM_CRITICAL_SUSPEND
 */
void apm_queue_event(apm_event_t event)
{
	unsigned long flags;

	spin_lock_irqsave(&kapmd_queue_lock, flags);
	queue_add_event(&kapmd_queue, event);
	spin_unlock_irqrestore(&kapmd_queue_lock, flags);

	wake_up_interruptible(&kapmd_wait);
}
EXPORT_SYMBOL(apm_queue_event);
