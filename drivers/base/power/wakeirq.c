/*
 * wakeirq.c - Device wakeirq helper functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

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
 * @irq: Device wake-up capable interrupt
 * @wirq: Wake irq specific data
 *
 * Internal function to attach either a device IO interrupt or a
 * dedicated wake-up interrupt as a wake IRQ.
 */
static int dev_pm_attach_wake_irq(struct device *dev, int irq,
				  struct wake_irq *wirq)
{
	unsigned long flags;
	int err;

	if (!dev || !wirq)
		return -EINVAL;

	spin_lock_irqsave(&dev->power.lock, flags);
	if (dev_WARN_ONCE(dev, dev->power.wakeirq,
			  "wake irq already initialized\n")) {
		spin_unlock_irqrestore(&dev->power.lock, flags);
		return -EEXIST;
	}

	err = device_wakeup_attach_irq(dev, wirq);
	if (!err)
		dev->power.wakeirq = wirq;

	spin_unlock_irqrestore(&dev->power.lock, flags);
	return err;
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

	wirq = kzalloc(sizeof(*wirq), GFP_KERNEL);
	if (!wirq)
		return -ENOMEM;

	wirq->dev = dev;
	wirq->irq = irq;

	err = dev_pm_attach_wake_irq(dev, irq, wirq);
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

	if (wirq->dedicated_irq)
		free_irq(wirq->irq, wirq);
	kfree(wirq);
}
EXPORT_SYMBOL_GPL(dev_pm_clear_wake_irq);

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

	/* We don't want RPM_ASYNC or RPM_NOWAIT here */
	res = pm_runtime_resume(wirq->dev);
	if (res < 0)
		dev_warn(wirq->dev,
			 "wake IRQ with no resume: %i\n", res);

	return IRQ_HANDLED;
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
 *
 * The interrupt starts disabled, and needs to be managed for
 * the device by the bus code or the device driver using
 * dev_pm_enable_wake_irq() and dev_pm_disable_wake_irq()
 * functions.
 */
int dev_pm_set_dedicated_wake_irq(struct device *dev, int irq)
{
	struct wake_irq *wirq;
	int err;

	wirq = kzalloc(sizeof(*wirq), GFP_KERNEL);
	if (!wirq)
		return -ENOMEM;

	wirq->dev = dev;
	wirq->irq = irq;
	wirq->dedicated_irq = true;
	irq_set_status_flags(irq, IRQ_NOAUTOEN);

	/*
	 * Consumer device may need to power up and restore state
	 * so we use a threaded irq.
	 */
	err = request_threaded_irq(irq, NULL, handle_threaded_wake_irq,
				   IRQF_ONESHOT, dev_name(dev), wirq);
	if (err)
		goto err_free;

	err = dev_pm_attach_wake_irq(dev, irq, wirq);
	if (err)
		goto err_free_irq;

	return err;

err_free_irq:
	free_irq(irq, wirq);
err_free:
	kfree(wirq);

	return err;
}
EXPORT_SYMBOL_GPL(dev_pm_set_dedicated_wake_irq);

/**
 * dev_pm_enable_wake_irq - Enable device wake-up interrupt
 * @dev: Device
 *
 * Called from the bus code or the device driver for
 * runtime_suspend() to enable the wake-up interrupt while
 * the device is running.
 *
 * Note that for runtime_suspend()) the wake-up interrupts
 * should be unconditionally enabled unlike for suspend()
 * that is conditional.
 */
void dev_pm_enable_wake_irq(struct device *dev)
{
	struct wake_irq *wirq = dev->power.wakeirq;

	if (wirq && wirq->dedicated_irq)
		enable_irq(wirq->irq);
}
EXPORT_SYMBOL_GPL(dev_pm_enable_wake_irq);

/**
 * dev_pm_disable_wake_irq - Disable device wake-up interrupt
 * @dev: Device
 *
 * Called from the bus code or the device driver for
 * runtime_resume() to disable the wake-up interrupt while
 * the device is running.
 */
void dev_pm_disable_wake_irq(struct device *dev)
{
	struct wake_irq *wirq = dev->power.wakeirq;

	if (wirq && wirq->dedicated_irq)
		disable_irq_nosync(wirq->irq);
}
EXPORT_SYMBOL_GPL(dev_pm_disable_wake_irq);

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

	if (device_may_wakeup(wirq->dev))
		enable_irq_wake(wirq->irq);
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

	if (device_may_wakeup(wirq->dev))
		disable_irq_wake(wirq->irq);
}
