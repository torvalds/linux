// SPDX-License-Identifier: GPL-2.0
/* Device wakeirq helper functions */
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>

#include "power.h"

/**
 * dev_pm_attach_wake_irq - Attach device interrupt as a wake IRQ
 * @dev: Device entry
 * @wirq: Wake irq specific data
 *
 * Internal function to attach a dedicated wake-up interrupt as a wake IRQ.
 */
static int dev_pm_attach_wake_irq(struct device *dev, struct wake_irq *wirq)
{
	unsigned long flags;

	if (!dev || !wirq)
		return -EINVAL;

	spin_lock_irqsave(&dev->power.lock, flags);
	if (dev_WARN_ONCE(dev, dev->power.wakeirq,
			  "wake irq already initialized\n")) {
		spin_unlock_irqrestore(&dev->power.lock, flags);
		return -EEXIST;
	}

	dev->power.wakeirq = wirq;
	device_wakeup_attach_irq(dev, wirq);

	spin_unlock_irqrestore(&dev->power.lock, flags);
	return 0;
}

/**
 * dev_pm_set_wake_irq - Attach device IO interrupt as wake IRQ
 * @dev: Device entry
 * @irq: Device IO interrupt
 *
 * Attach a device IO interrupt as a wake IRQ. The wake IRQ gets
 * automatically configured for wake-up from suspend  based
 * on the device specific sysfs wakeup entry. Typically called
 * during driver probe after calling device_init_wakeup().
 */
int dev_pm_set_wake_irq(struct device *dev, int irq)
{
	struct wake_irq *wirq;
	int err;

	if (irq < 0)
		return -EINVAL;

	wirq = kzalloc(sizeof(*wirq), GFP_KERNEL);
	if (!wirq)
		return -ENOMEM;

	wirq->dev = dev;
	wirq->irq = irq;

	err = dev_pm_attach_wake_irq(dev, wirq);
	if (err)
		kfree(wirq);

	return err;
}
EXPORT_SYMBOL_GPL(dev_pm_set_wake_irq);

/**
 * dev_pm_clear_wake_irq - Detach a device IO interrupt wake IRQ
 * @dev: Device entry
 *
 * Detach a device wake IRQ and free resources.
 *
 * Note that it's OK for drivers to call this without calling
 * dev_pm_set_wake_irq() as all the driver instances may not have
 * a wake IRQ configured. This avoid adding wake IRQ specific
 * checks into the drivers.
 */
void dev_pm_clear_wake_irq(struct device *dev)
{
	struct wake_irq *wirq = dev->power.wakeirq;
	unsigned long flags;

	if (!wirq)
		return;

	spin_lock_irqsave(&dev->power.lock, flags);
	device_wakeup_detach_irq(dev);
	dev->power.wakeirq = NULL;
	spin_unlock_irqrestore(&dev->power.lock, flags);

	if (wirq->status & WAKE_IRQ_DEDICATED_ALLOCATED) {
		free_irq(wirq->irq, wirq);
		wirq->status &= ~WAKE_IRQ_DEDICATED_MASK;
	}
	kfree(wirq->name);
	kfree(wirq);
}
EXPORT_SYMBOL_GPL(dev_pm_clear_wake_irq);

static void devm_pm_clear_wake_irq(void *dev)
{
	dev_pm_clear_wake_irq(dev);
}

/**
 * devm_pm_set_wake_irq - device-managed variant of dev_pm_set_wake_irq
 * @dev: Device entry
 * @irq: Device IO interrupt
 *
 *
 * Attach a device IO interrupt as a wake IRQ, same with dev_pm_set_wake_irq,
 * but the device will be auto clear wake capability on driver detach.
 */
int devm_pm_set_wake_irq(struct device *dev, int irq)
{
	int ret;

	ret = dev_pm_set_wake_irq(dev, irq);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, devm_pm_clear_wake_irq, dev);
}
EXPORT_SYMBOL_GPL(devm_pm_set_wake_irq);

/**
 * handle_threaded_wake_irq - Handler for dedicated wake-up interrupts
 * @irq: Device specific dedicated wake-up interrupt
 * @_wirq: Wake IRQ data
 *
 * Some devices have a separate wake-up interrupt in addition to the
 * device IO interrupt. The wake-up interrupt signals that a device
 * should be woken up from it's idle state. This handler uses device
 * specific pm_runtime functions to wake the device, and then it's
 * up to the device to do whatever it needs to. Note that as the
 * device may need to restore context and start up regulators, we
 * use a threaded IRQ.
 *
 * Also note that we are not resending the lost device interrupts.
 * We assume that the wake-up interrupt just needs to wake-up the
 * device, and then device's pm_runtime_resume() can deal with the
 * situation.
 */
static irqreturn_t handle_threaded_wake_irq(int irq, void *_wirq)
{
	struct wake_irq *wirq = _wirq;
	int res;

	/* Maybe abort suspend? */
	if (irqd_is_wakeup_set(irq_get_irq_data(irq))) {
		pm_wakeup_event(wirq->dev, 0);

		return IRQ_HANDLED;
	}

	/* We don't want RPM_ASYNC or RPM_NOWAIT here */
	res = pm_runtime_resume(wirq->dev);
	if (res < 0)
		dev_warn(wirq->dev,
			 "wake IRQ with no resume: %i\n", res);

	return IRQ_HANDLED;
}

static int __dev_pm_set_dedicated_wake_irq(struct device *dev, int irq, unsigned int flag)
{
	struct wake_irq *wirq;
	int err;

	if (irq < 0)
		return -EINVAL;

	wirq = kzalloc(sizeof(*wirq), GFP_KERNEL);
	if (!wirq)
		return -ENOMEM;

	wirq->name = kasprintf(GFP_KERNEL, "%s:wakeup", dev_name(dev));
	if (!wirq->name) {
		err = -ENOMEM;
		goto err_free;
	}

	wirq->dev = dev;
	wirq->irq = irq;

	/* Prevent deferred spurious wakeirqs with disable_irq_nosync() */
	irq_set_status_flags(irq, IRQ_DISABLE_UNLAZY);

	/*
	 * Consumer device may need to power up and restore state
	 * so we use a threaded irq.
	 */
	err = request_threaded_irq(irq, NULL, handle_threaded_wake_irq,
				   IRQF_ONESHOT | IRQF_NO_AUTOEN,
				   wirq->name, wirq);
	if (err)
		goto err_free_name;

	err = dev_pm_attach_wake_irq(dev, wirq);
	if (err)
		goto err_free_irq;

	wirq->status = WAKE_IRQ_DEDICATED_ALLOCATED | flag;

	return err;

err_free_irq:
	free_irq(irq, wirq);
err_free_name:
	kfree(wirq->name);
err_free:
	kfree(wirq);

	return err;
}

/**
 * dev_pm_set_dedicated_wake_irq - Request a dedicated wake-up interrupt
 * @dev: Device entry
 * @irq: Device wake-up interrupt
 *
 * Unless your hardware has separate wake-up interrupts in addition
 * to the device IO interrupts, you don't need this.
 *
 * Sets up a threaded interrupt handler for a device that has
 * a dedicated wake-up interrupt in addition to the device IO
 * interrupt.
 */
int dev_pm_set_dedicated_wake_irq(struct device *dev, int irq)
{
	return __dev_pm_set_dedicated_wake_irq(dev, irq, 0);
}
EXPORT_SYMBOL_GPL(dev_pm_set_dedicated_wake_irq);

/**
 * dev_pm_set_dedicated_wake_irq_reverse - Request a dedicated wake-up interrupt
 *                                         with reverse enable ordering
 * @dev: Device entry
 * @irq: Device wake-up interrupt
 *
 * Unless your hardware has separate wake-up interrupts in addition
 * to the device IO interrupts, you don't need this.
 *
 * Sets up a threaded interrupt handler for a device that has a dedicated
 * wake-up interrupt in addition to the device IO interrupt. It sets
 * the status of WAKE_IRQ_DEDICATED_REVERSE to tell rpm_suspend()
 * to enable dedicated wake-up interrupt after running the runtime suspend
 * callback for @dev.
 */
int dev_pm_set_dedicated_wake_irq_reverse(struct device *dev, int irq)
{
	return __dev_pm_set_dedicated_wake_irq(dev, irq, WAKE_IRQ_DEDICATED_REVERSE);
}
EXPORT_SYMBOL_GPL(dev_pm_set_dedicated_wake_irq_reverse);

/**
 * dev_pm_enable_wake_irq_check - Checks and enables wake-up interrupt
 * @dev: Device
 * @can_change_status: Can change wake-up interrupt status
 *
 * Enables wakeirq conditionally. We need to enable wake-up interrupt
 * lazily on the first rpm_suspend(). This is needed as the consumer device
 * starts in RPM_SUSPENDED state, and the first pm_runtime_get() would
 * otherwise try to disable already disabled wakeirq. The wake-up interrupt
 * starts disabled with IRQ_NOAUTOEN set.
 *
 * Should be only called from rpm_suspend() and rpm_resume() path.
 * Caller must hold &dev->power.lock to change wirq->status
 */
void dev_pm_enable_wake_irq_check(struct device *dev,
				  bool can_change_status)
{
	struct wake_irq *wirq = dev->power.wakeirq;

	if (!wirq || !(wirq->status & WAKE_IRQ_DEDICATED_MASK))
		return;

	if (likely(wirq->status & WAKE_IRQ_DEDICATED_MANAGED)) {
		goto enable;
	} else if (can_change_status) {
		wirq->status |= WAKE_IRQ_DEDICATED_MANAGED;
		goto enable;
	}

	return;

enable:
	if (!can_change_status || !(wirq->status & WAKE_IRQ_DEDICATED_REVERSE)) {
		enable_irq(wirq->irq);
		wirq->status |= WAKE_IRQ_DEDICATED_ENABLED;
	}
}

/**
 * dev_pm_disable_wake_irq_check - Checks and disables wake-up interrupt
 * @dev: Device
 * @cond_disable: if set, also check WAKE_IRQ_DEDICATED_REVERSE
 *
 * Disables wake-up interrupt conditionally based on status.
 * Should be only called from rpm_suspend() and rpm_resume() path.
 */
void dev_pm_disable_wake_irq_check(struct device *dev, bool cond_disable)
{
	struct wake_irq *wirq = dev->power.wakeirq;

	if (!wirq || !(wirq->status & WAKE_IRQ_DEDICATED_MASK))
		return;

	if (cond_disable && (wirq->status & WAKE_IRQ_DEDICATED_REVERSE))
		return;

	if (wirq->status & WAKE_IRQ_DEDICATED_MANAGED) {
		wirq->status &= ~WAKE_IRQ_DEDICATED_ENABLED;
		disable_irq_nosync(wirq->irq);
	}
}

/**
 * dev_pm_enable_wake_irq_complete - enable wake IRQ not enabled before
 * @dev: Device using the wake IRQ
 *
 * Enable wake IRQ conditionally based on status, mainly used if want to
 * enable wake IRQ after running ->runtime_suspend() which depends on
 * WAKE_IRQ_DEDICATED_REVERSE.
 *
 * Should be only called from rpm_suspend() path.
 */
void dev_pm_enable_wake_irq_complete(struct device *dev)
{
	struct wake_irq *wirq = dev->power.wakeirq;

	if (!wirq || !(wirq->status & WAKE_IRQ_DEDICATED_MASK))
		return;

	if (wirq->status & WAKE_IRQ_DEDICATED_MANAGED &&
	    wirq->status & WAKE_IRQ_DEDICATED_REVERSE) {
		enable_irq(wirq->irq);
		wirq->status |= WAKE_IRQ_DEDICATED_ENABLED;
	}
}

/**
 * dev_pm_arm_wake_irq - Arm device wake-up
 * @wirq: Device wake-up interrupt
 *
 * Sets up the wake-up event conditionally based on the
 * device_may_wake().
 */
void dev_pm_arm_wake_irq(struct wake_irq *wirq)
{
	if (!wirq)
		return;

	if (device_may_wakeup(wirq->dev)) {
		if (wirq->status & WAKE_IRQ_DEDICATED_ALLOCATED &&
		    !(wirq->status & WAKE_IRQ_DEDICATED_ENABLED))
			enable_irq(wirq->irq);

		enable_irq_wake(wirq->irq);
	}
}

/**
 * dev_pm_disarm_wake_irq - Disarm device wake-up
 * @wirq: Device wake-up interrupt
 *
 * Clears up the wake-up event conditionally based on the
 * device_may_wake().
 */
void dev_pm_disarm_wake_irq(struct wake_irq *wirq)
{
	if (!wirq)
		return;

	if (device_may_wakeup(wirq->dev)) {
		disable_irq_wake(wirq->irq);

		if (wirq->status & WAKE_IRQ_DEDICATED_ALLOCATED &&
		    !(wirq->status & WAKE_IRQ_DEDICATED_ENABLED))
			disable_irq_nosync(wirq->irq);
	}
}
