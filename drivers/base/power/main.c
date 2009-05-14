/*
 * drivers/base/power/main.c - Where the driver meets power management.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 *
 * This file is released under the GPLv2
 *
 *
 * The driver model core calls device_pm_add() when a device is registered.
 * This will intialize the embedded device_pm_info object in the device
 * and add it to the list of power-controlled devices. sysfs entries for
 * controlling device power management will also be added.
 *
 * A separate list is used for keeping track of power info, because the power
 * domain dependencies may differ from the ancestral dependencies that the
 * subsystem list maintains.
 */

#include <linux/device.h>
#include <linux/kallsyms.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/resume-trace.h>
#include <linux/rwsem.h>
#include <linux/interrupt.h>

#include "../base.h"
#include "power.h"

/*
 * The entries in the dpm_list list are in a depth first order, simply
 * because children are guaranteed to be discovered after parents, and
 * are inserted at the back of the list on discovery.
 *
 * Since device_pm_add() may be called with a device semaphore held,
 * we must never try to acquire a device semaphore while holding
 * dpm_list_mutex.
 */

LIST_HEAD(dpm_list);

static DEFINE_MUTEX(dpm_list_mtx);

/*
 * Set once the preparation of devices for a PM transition has started, reset
 * before starting to resume devices.  Protected by dpm_list_mtx.
 */
static bool transition_started;

/**
 *	device_pm_lock - lock the list of active devices used by the PM core
 */
void device_pm_lock(void)
{
	mutex_lock(&dpm_list_mtx);
}

/**
 *	device_pm_unlock - unlock the list of active devices used by the PM core
 */
void device_pm_unlock(void)
{
	mutex_unlock(&dpm_list_mtx);
}

/**
 *	device_pm_add - add a device to the list of active devices
 *	@dev:	Device to be added to the list
 */
void device_pm_add(struct device *dev)
{
	pr_debug("PM: Adding info for %s:%s\n",
		 dev->bus ? dev->bus->name : "No Bus",
		 kobject_name(&dev->kobj));
	mutex_lock(&dpm_list_mtx);
	if (dev->parent) {
		if (dev->parent->power.status >= DPM_SUSPENDING)
			dev_warn(dev, "parent %s should not be sleeping\n",
				 dev_name(dev->parent));
	} else if (transition_started) {
		/*
		 * We refuse to register parentless devices while a PM
		 * transition is in progress in order to avoid leaving them
		 * unhandled down the road
		 */
		dev_WARN(dev, "Parentless device registered during a PM transaction\n");
	}

	list_add_tail(&dev->power.entry, &dpm_list);
	mutex_unlock(&dpm_list_mtx);
}

/**
 *	device_pm_remove - remove a device from the list of active devices
 *	@dev:	Device to be removed from the list
 *
 *	This function also removes the device's PM-related sysfs attributes.
 */
void device_pm_remove(struct device *dev)
{
	pr_debug("PM: Removing info for %s:%s\n",
		 dev->bus ? dev->bus->name : "No Bus",
		 kobject_name(&dev->kobj));
	mutex_lock(&dpm_list_mtx);
	list_del_init(&dev->power.entry);
	mutex_unlock(&dpm_list_mtx);
}

/**
 *	device_pm_move_before - move device in dpm_list
 *	@deva:  Device to move in dpm_list
 *	@devb:  Device @deva should come before
 */
void device_pm_move_before(struct device *deva, struct device *devb)
{
	pr_debug("PM: Moving %s:%s before %s:%s\n",
		 deva->bus ? deva->bus->name : "No Bus",
		 kobject_name(&deva->kobj),
		 devb->bus ? devb->bus->name : "No Bus",
		 kobject_name(&devb->kobj));
	/* Delete deva from dpm_list and reinsert before devb. */
	list_move_tail(&deva->power.entry, &devb->power.entry);
}

/**
 *	device_pm_move_after - move device in dpm_list
 *	@deva:  Device to move in dpm_list
 *	@devb:  Device @deva should come after
 */
void device_pm_move_after(struct device *deva, struct device *devb)
{
	pr_debug("PM: Moving %s:%s after %s:%s\n",
		 deva->bus ? deva->bus->name : "No Bus",
		 kobject_name(&deva->kobj),
		 devb->bus ? devb->bus->name : "No Bus",
		 kobject_name(&devb->kobj));
	/* Delete deva from dpm_list and reinsert after devb. */
	list_move(&deva->power.entry, &devb->power.entry);
}

/**
 * 	device_pm_move_last - move device to end of dpm_list
 * 	@dev:   Device to move in dpm_list
 */
void device_pm_move_last(struct device *dev)
{
	pr_debug("PM: Moving %s:%s to end of list\n",
		 dev->bus ? dev->bus->name : "No Bus",
		 kobject_name(&dev->kobj));
	list_move_tail(&dev->power.entry, &dpm_list);
}

/**
 *	pm_op - execute the PM operation appropiate for given PM event
 *	@dev:	Device.
 *	@ops:	PM operations to choose from.
 *	@state:	PM transition of the system being carried out.
 */
static int pm_op(struct device *dev, struct dev_pm_ops *ops,
			pm_message_t state)
{
	int error = 0;

	switch (state.event) {
#ifdef CONFIG_SUSPEND
	case PM_EVENT_SUSPEND:
		if (ops->suspend) {
			error = ops->suspend(dev);
			suspend_report_result(ops->suspend, error);
		}
		break;
	case PM_EVENT_RESUME:
		if (ops->resume) {
			error = ops->resume(dev);
			suspend_report_result(ops->resume, error);
		}
		break;
#endif /* CONFIG_SUSPEND */
#ifdef CONFIG_HIBERNATION
	case PM_EVENT_FREEZE:
	case PM_EVENT_QUIESCE:
		if (ops->freeze) {
			error = ops->freeze(dev);
			suspend_report_result(ops->freeze, error);
		}
		break;
	case PM_EVENT_HIBERNATE:
		if (ops->poweroff) {
			error = ops->poweroff(dev);
			suspend_report_result(ops->poweroff, error);
		}
		break;
	case PM_EVENT_THAW:
	case PM_EVENT_RECOVER:
		if (ops->thaw) {
			error = ops->thaw(dev);
			suspend_report_result(ops->thaw, error);
		}
		break;
	case PM_EVENT_RESTORE:
		if (ops->restore) {
			error = ops->restore(dev);
			suspend_report_result(ops->restore, error);
		}
		break;
#endif /* CONFIG_HIBERNATION */
	default:
		error = -EINVAL;
	}
	return error;
}

/**
 *	pm_noirq_op - execute the PM operation appropiate for given PM event
 *	@dev:	Device.
 *	@ops:	PM operations to choose from.
 *	@state: PM transition of the system being carried out.
 *
 *	The operation is executed with interrupts disabled by the only remaining
 *	functional CPU in the system.
 */
static int pm_noirq_op(struct device *dev, struct dev_pm_ops *ops,
			pm_message_t state)
{
	int error = 0;

	switch (state.event) {
#ifdef CONFIG_SUSPEND
	case PM_EVENT_SUSPEND:
		if (ops->suspend_noirq) {
			error = ops->suspend_noirq(dev);
			suspend_report_result(ops->suspend_noirq, error);
		}
		break;
	case PM_EVENT_RESUME:
		if (ops->resume_noirq) {
			error = ops->resume_noirq(dev);
			suspend_report_result(ops->resume_noirq, error);
		}
		break;
#endif /* CONFIG_SUSPEND */
#ifdef CONFIG_HIBERNATION
	case PM_EVENT_FREEZE:
	case PM_EVENT_QUIESCE:
		if (ops->freeze_noirq) {
			error = ops->freeze_noirq(dev);
			suspend_report_result(ops->freeze_noirq, error);
		}
		break;
	case PM_EVENT_HIBERNATE:
		if (ops->poweroff_noirq) {
			error = ops->poweroff_noirq(dev);
			suspend_report_result(ops->poweroff_noirq, error);
		}
		break;
	case PM_EVENT_THAW:
	case PM_EVENT_RECOVER:
		if (ops->thaw_noirq) {
			error = ops->thaw_noirq(dev);
			suspend_report_result(ops->thaw_noirq, error);
		}
		break;
	case PM_EVENT_RESTORE:
		if (ops->restore_noirq) {
			error = ops->restore_noirq(dev);
			suspend_report_result(ops->restore_noirq, error);
		}
		break;
#endif /* CONFIG_HIBERNATION */
	default:
		error = -EINVAL;
	}
	return error;
}

static char *pm_verb(int event)
{
	switch (event) {
	case PM_EVENT_SUSPEND:
		return "suspend";
	case PM_EVENT_RESUME:
		return "resume";
	case PM_EVENT_FREEZE:
		return "freeze";
	case PM_EVENT_QUIESCE:
		return "quiesce";
	case PM_EVENT_HIBERNATE:
		return "hibernate";
	case PM_EVENT_THAW:
		return "thaw";
	case PM_EVENT_RESTORE:
		return "restore";
	case PM_EVENT_RECOVER:
		return "recover";
	default:
		return "(unknown PM event)";
	}
}

static void pm_dev_dbg(struct device *dev, pm_message_t state, char *info)
{
	dev_dbg(dev, "%s%s%s\n", info, pm_verb(state.event),
		((state.event & PM_EVENT_SLEEP) && device_may_wakeup(dev)) ?
		", may wakeup" : "");
}

static void pm_dev_err(struct device *dev, pm_message_t state, char *info,
			int error)
{
	printk(KERN_ERR "PM: Device %s failed to %s%s: error %d\n",
		kobject_name(&dev->kobj), pm_verb(state.event), info, error);
}

/*------------------------- Resume routines -------------------------*/

/**
 *	__device_resume_noirq - Power on one device (early resume).
 *	@dev:	Device.
 *	@state: PM transition of the system being carried out.
 *
 *	Must be called with interrupts disabled.
 */
static int __device_resume_noirq(struct device *dev, pm_message_t state)
{
	int error = 0;

	TRACE_DEVICE(dev);
	TRACE_RESUME(0);

	if (!dev->bus)
		goto End;

	if (dev->bus->pm) {
		pm_dev_dbg(dev, state, "EARLY ");
		error = pm_noirq_op(dev, dev->bus->pm, state);
	} else if (dev->bus->resume_early) {
		pm_dev_dbg(dev, state, "legacy EARLY ");
		error = dev->bus->resume_early(dev);
	}
 End:
	TRACE_RESUME(error);
	return error;
}

/**
 *	dpm_power_up - Power on all regular (non-sysdev) devices.
 *	@state: PM transition of the system being carried out.
 *
 *	Execute the appropriate "noirq resume" callback for all devices marked
 *	as DPM_OFF_IRQ.
 *
 *	Must be called under dpm_list_mtx.  Device drivers should not receive
 *	interrupts while it's being executed.
 */
static void dpm_power_up(pm_message_t state)
{
	struct device *dev;

	mutex_lock(&dpm_list_mtx);
	list_for_each_entry(dev, &dpm_list, power.entry)
		if (dev->power.status > DPM_OFF) {
			int error;

			dev->power.status = DPM_OFF;
			error = __device_resume_noirq(dev, state);
			if (error)
				pm_dev_err(dev, state, " early", error);
		}
	mutex_unlock(&dpm_list_mtx);
}

/**
 *	device_resume_noirq - Turn on all devices that need special attention.
 *	@state: PM transition of the system being carried out.
 *
 *	Call the "early" resume handlers and enable device drivers to receive
 *	interrupts.
 */
void device_resume_noirq(pm_message_t state)
{
	dpm_power_up(state);
	resume_device_irqs();
}
EXPORT_SYMBOL_GPL(device_resume_noirq);

/**
 *	resume_device - Restore state for one device.
 *	@dev:	Device.
 *	@state: PM transition of the system being carried out.
 */
static int resume_device(struct device *dev, pm_message_t state)
{
	int error = 0;

	TRACE_DEVICE(dev);
	TRACE_RESUME(0);

	down(&dev->sem);

	if (dev->bus) {
		if (dev->bus->pm) {
			pm_dev_dbg(dev, state, "");
			error = pm_op(dev, dev->bus->pm, state);
		} else if (dev->bus->resume) {
			pm_dev_dbg(dev, state, "legacy ");
			error = dev->bus->resume(dev);
		}
		if (error)
			goto End;
	}

	if (dev->type) {
		if (dev->type->pm) {
			pm_dev_dbg(dev, state, "type ");
			error = pm_op(dev, dev->type->pm, state);
		} else if (dev->type->resume) {
			pm_dev_dbg(dev, state, "legacy type ");
			error = dev->type->resume(dev);
		}
		if (error)
			goto End;
	}

	if (dev->class) {
		if (dev->class->pm) {
			pm_dev_dbg(dev, state, "class ");
			error = pm_op(dev, dev->class->pm, state);
		} else if (dev->class->resume) {
			pm_dev_dbg(dev, state, "legacy class ");
			error = dev->class->resume(dev);
		}
	}
 End:
	up(&dev->sem);

	TRACE_RESUME(error);
	return error;
}

/**
 *	dpm_resume - Resume every device.
 *	@state: PM transition of the system being carried out.
 *
 *	Execute the appropriate "resume" callback for all devices the status of
 *	which indicates that they are inactive.
 */
static void dpm_resume(pm_message_t state)
{
	struct list_head list;

	INIT_LIST_HEAD(&list);
	mutex_lock(&dpm_list_mtx);
	transition_started = false;
	while (!list_empty(&dpm_list)) {
		struct device *dev = to_device(dpm_list.next);

		get_device(dev);
		if (dev->power.status >= DPM_OFF) {
			int error;

			dev->power.status = DPM_RESUMING;
			mutex_unlock(&dpm_list_mtx);

			error = resume_device(dev, state);

			mutex_lock(&dpm_list_mtx);
			if (error)
				pm_dev_err(dev, state, "", error);
		} else if (dev->power.status == DPM_SUSPENDING) {
			/* Allow new children of the device to be registered */
			dev->power.status = DPM_RESUMING;
		}
		if (!list_empty(&dev->power.entry))
			list_move_tail(&dev->power.entry, &list);
		put_device(dev);
	}
	list_splice(&list, &dpm_list);
	mutex_unlock(&dpm_list_mtx);
}

/**
 *	complete_device - Complete a PM transition for given device
 *	@dev:	Device.
 *	@state: PM transition of the system being carried out.
 */
static void complete_device(struct device *dev, pm_message_t state)
{
	down(&dev->sem);

	if (dev->class && dev->class->pm && dev->class->pm->complete) {
		pm_dev_dbg(dev, state, "completing class ");
		dev->class->pm->complete(dev);
	}

	if (dev->type && dev->type->pm && dev->type->pm->complete) {
		pm_dev_dbg(dev, state, "completing type ");
		dev->type->pm->complete(dev);
	}

	if (dev->bus && dev->bus->pm && dev->bus->pm->complete) {
		pm_dev_dbg(dev, state, "completing ");
		dev->bus->pm->complete(dev);
	}

	up(&dev->sem);
}

/**
 *	dpm_complete - Complete a PM transition for all devices.
 *	@state: PM transition of the system being carried out.
 *
 *	Execute the ->complete() callbacks for all devices that are not marked
 *	as DPM_ON.
 */
static void dpm_complete(pm_message_t state)
{
	struct list_head list;

	INIT_LIST_HEAD(&list);
	mutex_lock(&dpm_list_mtx);
	while (!list_empty(&dpm_list)) {
		struct device *dev = to_device(dpm_list.prev);

		get_device(dev);
		if (dev->power.status > DPM_ON) {
			dev->power.status = DPM_ON;
			mutex_unlock(&dpm_list_mtx);

			complete_device(dev, state);

			mutex_lock(&dpm_list_mtx);
		}
		if (!list_empty(&dev->power.entry))
			list_move(&dev->power.entry, &list);
		put_device(dev);
	}
	list_splice(&list, &dpm_list);
	mutex_unlock(&dpm_list_mtx);
}

/**
 *	device_resume - Restore state of each device in system.
 *	@state: PM transition of the system being carried out.
 *
 *	Resume all the devices, unlock them all, and allow new
 *	devices to be registered once again.
 */
void device_resume(pm_message_t state)
{
	might_sleep();
	dpm_resume(state);
	dpm_complete(state);
}
EXPORT_SYMBOL_GPL(device_resume);


/*------------------------- Suspend routines -------------------------*/

/**
 *	resume_event - return a PM message representing the resume event
 *	               corresponding to given sleep state.
 *	@sleep_state: PM message representing a sleep state.
 */
static pm_message_t resume_event(pm_message_t sleep_state)
{
	switch (sleep_state.event) {
	case PM_EVENT_SUSPEND:
		return PMSG_RESUME;
	case PM_EVENT_FREEZE:
	case PM_EVENT_QUIESCE:
		return PMSG_RECOVER;
	case PM_EVENT_HIBERNATE:
		return PMSG_RESTORE;
	}
	return PMSG_ON;
}

/**
 *	__device_suspend_noirq - Shut down one device (late suspend).
 *	@dev:	Device.
 *	@state: PM transition of the system being carried out.
 *
 *	This is called with interrupts off and only a single CPU running.
 */
static int __device_suspend_noirq(struct device *dev, pm_message_t state)
{
	int error = 0;

	if (!dev->bus)
		return 0;

	if (dev->bus->pm) {
		pm_dev_dbg(dev, state, "LATE ");
		error = pm_noirq_op(dev, dev->bus->pm, state);
	} else if (dev->bus->suspend_late) {
		pm_dev_dbg(dev, state, "legacy LATE ");
		error = dev->bus->suspend_late(dev, state);
		suspend_report_result(dev->bus->suspend_late, error);
	}
	return error;
}

/**
 *	device_suspend_noirq - Shut down special devices.
 *	@state: PM transition of the system being carried out.
 *
 *	Prevent device drivers from receiving interrupts and call the "late"
 *	suspend handlers.
 *
 *	Must be called under dpm_list_mtx.
 */
int device_suspend_noirq(pm_message_t state)
{
	struct device *dev;
	int error = 0;

	suspend_device_irqs();
	mutex_lock(&dpm_list_mtx);
	list_for_each_entry_reverse(dev, &dpm_list, power.entry) {
		error = __device_suspend_noirq(dev, state);
		if (error) {
			pm_dev_err(dev, state, " late", error);
			break;
		}
		dev->power.status = DPM_OFF_IRQ;
	}
	mutex_unlock(&dpm_list_mtx);
	if (error)
		device_resume_noirq(resume_event(state));
	return error;
}
EXPORT_SYMBOL_GPL(device_suspend_noirq);

/**
 *	suspend_device - Save state of one device.
 *	@dev:	Device.
 *	@state: PM transition of the system being carried out.
 */
static int suspend_device(struct device *dev, pm_message_t state)
{
	int error = 0;

	down(&dev->sem);

	if (dev->class) {
		if (dev->class->pm) {
			pm_dev_dbg(dev, state, "class ");
			error = pm_op(dev, dev->class->pm, state);
		} else if (dev->class->suspend) {
			pm_dev_dbg(dev, state, "legacy class ");
			error = dev->class->suspend(dev, state);
			suspend_report_result(dev->class->suspend, error);
		}
		if (error)
			goto End;
	}

	if (dev->type) {
		if (dev->type->pm) {
			pm_dev_dbg(dev, state, "type ");
			error = pm_op(dev, dev->type->pm, state);
		} else if (dev->type->suspend) {
			pm_dev_dbg(dev, state, "legacy type ");
			error = dev->type->suspend(dev, state);
			suspend_report_result(dev->type->suspend, error);
		}
		if (error)
			goto End;
	}

	if (dev->bus) {
		if (dev->bus->pm) {
			pm_dev_dbg(dev, state, "");
			error = pm_op(dev, dev->bus->pm, state);
		} else if (dev->bus->suspend) {
			pm_dev_dbg(dev, state, "legacy ");
			error = dev->bus->suspend(dev, state);
			suspend_report_result(dev->bus->suspend, error);
		}
	}
 End:
	up(&dev->sem);

	return error;
}

/**
 *	dpm_suspend - Suspend every device.
 *	@state: PM transition of the system being carried out.
 *
 *	Execute the appropriate "suspend" callbacks for all devices.
 */
static int dpm_suspend(pm_message_t state)
{
	struct list_head list;
	int error = 0;

	INIT_LIST_HEAD(&list);
	mutex_lock(&dpm_list_mtx);
	while (!list_empty(&dpm_list)) {
		struct device *dev = to_device(dpm_list.prev);

		get_device(dev);
		mutex_unlock(&dpm_list_mtx);

		error = suspend_device(dev, state);

		mutex_lock(&dpm_list_mtx);
		if (error) {
			pm_dev_err(dev, state, "", error);
			put_device(dev);
			break;
		}
		dev->power.status = DPM_OFF;
		if (!list_empty(&dev->power.entry))
			list_move(&dev->power.entry, &list);
		put_device(dev);
	}
	list_splice(&list, dpm_list.prev);
	mutex_unlock(&dpm_list_mtx);
	return error;
}

/**
 *	prepare_device - Execute the ->prepare() callback(s) for given device.
 *	@dev:	Device.
 *	@state: PM transition of the system being carried out.
 */
static int prepare_device(struct device *dev, pm_message_t state)
{
	int error = 0;

	down(&dev->sem);

	if (dev->bus && dev->bus->pm && dev->bus->pm->prepare) {
		pm_dev_dbg(dev, state, "preparing ");
		error = dev->bus->pm->prepare(dev);
		suspend_report_result(dev->bus->pm->prepare, error);
		if (error)
			goto End;
	}

	if (dev->type && dev->type->pm && dev->type->pm->prepare) {
		pm_dev_dbg(dev, state, "preparing type ");
		error = dev->type->pm->prepare(dev);
		suspend_report_result(dev->type->pm->prepare, error);
		if (error)
			goto End;
	}

	if (dev->class && dev->class->pm && dev->class->pm->prepare) {
		pm_dev_dbg(dev, state, "preparing class ");
		error = dev->class->pm->prepare(dev);
		suspend_report_result(dev->class->pm->prepare, error);
	}
 End:
	up(&dev->sem);

	return error;
}

/**
 *	dpm_prepare - Prepare all devices for a PM transition.
 *	@state: PM transition of the system being carried out.
 *
 *	Execute the ->prepare() callback for all devices.
 */
static int dpm_prepare(pm_message_t state)
{
	struct list_head list;
	int error = 0;

	INIT_LIST_HEAD(&list);
	mutex_lock(&dpm_list_mtx);
	transition_started = true;
	while (!list_empty(&dpm_list)) {
		struct device *dev = to_device(dpm_list.next);

		get_device(dev);
		dev->power.status = DPM_PREPARING;
		mutex_unlock(&dpm_list_mtx);

		error = prepare_device(dev, state);

		mutex_lock(&dpm_list_mtx);
		if (error) {
			dev->power.status = DPM_ON;
			if (error == -EAGAIN) {
				put_device(dev);
				continue;
			}
			printk(KERN_ERR "PM: Failed to prepare device %s "
				"for power transition: error %d\n",
				kobject_name(&dev->kobj), error);
			put_device(dev);
			break;
		}
		dev->power.status = DPM_SUSPENDING;
		if (!list_empty(&dev->power.entry))
			list_move_tail(&dev->power.entry, &list);
		put_device(dev);
	}
	list_splice(&list, &dpm_list);
	mutex_unlock(&dpm_list_mtx);
	return error;
}

/**
 *	device_suspend - Save state and stop all devices in system.
 *	@state: PM transition of the system being carried out.
 *
 *	Prepare and suspend all devices.
 */
int device_suspend(pm_message_t state)
{
	int error;

	might_sleep();
	error = dpm_prepare(state);
	if (!error)
		error = dpm_suspend(state);
	return error;
}
EXPORT_SYMBOL_GPL(device_suspend);

void __suspend_report_result(const char *function, void *fn, int ret)
{
	if (ret)
		printk(KERN_ERR "%s(): %pF returns %d\n", function, fn, ret);
}
EXPORT_SYMBOL_GPL(__suspend_report_result);
