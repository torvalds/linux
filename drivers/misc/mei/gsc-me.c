// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2019-2022, Intel Corporation. All rights reserved.
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */

#include <linux/module.h>
#include <linux/mei_aux.h>
#include <linux/device.h>
#include <linux/irqreturn.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>

#include "mei_dev.h"
#include "hw-me.h"
#include "hw-me-regs.h"

#include "mei-trace.h"

#define MEI_GSC_RPM_TIMEOUT 500

static int mei_gsc_read_hfs(const struct mei_device *dev, int where, u32 *val)
{
	struct mei_me_hw *hw = to_me_hw(dev);

	*val = ioread32(hw->mem_addr + where + 0xC00);

	return 0;
}

static int mei_gsc_probe(struct auxiliary_device *aux_dev,
			 const struct auxiliary_device_id *aux_dev_id)
{
	struct mei_aux_device *adev = auxiliary_dev_to_mei_aux_dev(aux_dev);
	struct mei_device *dev;
	struct mei_me_hw *hw;
	struct device *device;
	const struct mei_cfg *cfg;
	int ret;

	cfg = mei_me_get_cfg(aux_dev_id->driver_data);
	if (!cfg)
		return -ENODEV;

	device = &aux_dev->dev;

	dev = mei_me_dev_init(device, cfg);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	hw = to_me_hw(dev);
	hw->mem_addr = devm_ioremap_resource(device, &adev->bar);
	if (IS_ERR(hw->mem_addr)) {
		dev_err(device, "mmio not mapped\n");
		ret = PTR_ERR(hw->mem_addr);
		goto err;
	}

	hw->irq = adev->irq;
	hw->read_fws = mei_gsc_read_hfs;

	dev_set_drvdata(device, dev);

	ret = devm_request_threaded_irq(device, hw->irq,
					mei_me_irq_quick_handler,
					mei_me_irq_thread_handler,
					IRQF_ONESHOT, KBUILD_MODNAME, dev);
	if (ret) {
		dev_err(device, "irq register failed %d\n", ret);
		goto err;
	}

	pm_runtime_get_noresume(device);
	pm_runtime_set_active(device);
	pm_runtime_enable(device);

	/* Continue to char device setup in spite of firmware handshake failure.
	 * In order to provide access to the firmware status registers to the user
	 * space via sysfs.
	 */
	if (mei_start(dev))
		dev_warn(device, "init hw failure.\n");

	pm_runtime_set_autosuspend_delay(device, MEI_GSC_RPM_TIMEOUT);
	pm_runtime_use_autosuspend(device);

	ret = mei_register(dev, device);
	if (ret)
		goto register_err;

	pm_runtime_put_noidle(device);
	return 0;

register_err:
	mei_stop(dev);
	devm_free_irq(device, hw->irq, dev);

err:
	dev_err(device, "probe failed: %d\n", ret);
	dev_set_drvdata(device, NULL);
	return ret;
}

static void mei_gsc_remove(struct auxiliary_device *aux_dev)
{
	struct mei_device *dev;
	struct mei_me_hw *hw;

	dev = dev_get_drvdata(&aux_dev->dev);
	if (!dev)
		return;

	hw = to_me_hw(dev);

	mei_stop(dev);

	mei_deregister(dev);

	pm_runtime_disable(&aux_dev->dev);

	mei_disable_interrupts(dev);
	devm_free_irq(&aux_dev->dev, hw->irq, dev);
}

static int __maybe_unused mei_gsc_pm_suspend(struct device *device)
{
	struct mei_device *dev = dev_get_drvdata(device);

	if (!dev)
		return -ENODEV;

	mei_stop(dev);

	mei_disable_interrupts(dev);

	return 0;
}

static int __maybe_unused mei_gsc_pm_resume(struct device *device)
{
	struct mei_device *dev = dev_get_drvdata(device);
	int err;

	if (!dev)
		return -ENODEV;

	err = mei_restart(dev);
	if (err)
		return err;

	/* Start timer if stopped in suspend */
	schedule_delayed_work(&dev->timer_work, HZ);

	return 0;
}

static int __maybe_unused mei_gsc_pm_runtime_idle(struct device *device)
{
	struct mei_device *dev = dev_get_drvdata(device);

	if (!dev)
		return -ENODEV;
	if (mei_write_is_idle(dev))
		pm_runtime_autosuspend(device);

	return -EBUSY;
}

static int  __maybe_unused mei_gsc_pm_runtime_suspend(struct device *device)
{
	struct mei_device *dev = dev_get_drvdata(device);
	struct mei_me_hw *hw;
	int ret;

	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	if (mei_write_is_idle(dev)) {
		hw = to_me_hw(dev);
		hw->pg_state = MEI_PG_ON;
		ret = 0;
	} else {
		ret = -EAGAIN;
	}

	mutex_unlock(&dev->device_lock);

	return ret;
}

static int __maybe_unused mei_gsc_pm_runtime_resume(struct device *device)
{
	struct mei_device *dev = dev_get_drvdata(device);
	struct mei_me_hw *hw;
	irqreturn_t irq_ret;

	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	hw = to_me_hw(dev);
	hw->pg_state = MEI_PG_OFF;

	mutex_unlock(&dev->device_lock);

	irq_ret = mei_me_irq_thread_handler(1, dev);
	if (irq_ret != IRQ_HANDLED)
		dev_err(dev->dev, "thread handler fail %d\n", irq_ret);

	return 0;
}

static const struct dev_pm_ops mei_gsc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mei_gsc_pm_suspend,
				mei_gsc_pm_resume)
	SET_RUNTIME_PM_OPS(mei_gsc_pm_runtime_suspend,
			   mei_gsc_pm_runtime_resume,
			   mei_gsc_pm_runtime_idle)
};

static const struct auxiliary_device_id mei_gsc_id_table[] = {
	{
		.name = "i915.mei-gsc",
		.driver_data = MEI_ME_GSC_CFG,

	},
	{
		.name = "i915.mei-gscfi",
		.driver_data = MEI_ME_GSCFI_CFG,
	},
	{
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(auxiliary, mei_gsc_id_table);

static struct auxiliary_driver mei_gsc_driver = {
	.probe	= mei_gsc_probe,
	.remove = mei_gsc_remove,
	.driver = {
		/* auxiliary_driver_register() sets .name to be the modname */
		.pm = &mei_gsc_pm_ops,
	},
	.id_table = mei_gsc_id_table
};
module_auxiliary_driver(mei_gsc_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_ALIAS("auxiliary:i915.mei-gsc");
MODULE_ALIAS("auxiliary:i915.mei-gscfi");
MODULE_LICENSE("GPL");
