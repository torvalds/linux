/*
 * Intel BayTrail PMIC I2C bus semaphore implementaion
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <asm/iosf_mbi.h>
#include "i2c-designware-core.h"

#define SEMAPHORE_TIMEOUT	100
#define PUNIT_SEMAPHORE		0x7

static unsigned long acquired;

static int get_sem(struct device *dev, u32 *sem)
{
	u32 reg_val;
	int ret;

	ret = iosf_mbi_read(BT_MBI_UNIT_PMC, BT_MBI_BUNIT_READ, PUNIT_SEMAPHORE,
			    &reg_val);
	if (ret) {
		dev_err(dev, "iosf failed to read punit semaphore\n");
		return ret;
	}

	*sem = reg_val & 0x1;

	return 0;
}

static void reset_semaphore(struct device *dev)
{
	u32 data;

	if (iosf_mbi_read(BT_MBI_UNIT_PMC, BT_MBI_BUNIT_READ,
				PUNIT_SEMAPHORE, &data)) {
		dev_err(dev, "iosf failed to reset punit semaphore during read\n");
		return;
	}

	data = data & 0xfffffffe;
	if (iosf_mbi_write(BT_MBI_UNIT_PMC, BT_MBI_BUNIT_WRITE,
				 PUNIT_SEMAPHORE, data))
		dev_err(dev, "iosf failed to reset punit semaphore during write\n");
}

int baytrail_i2c_acquire(struct dw_i2c_dev *dev)
{
	u32 sem = 0;
	int ret;
	unsigned long start, end;

	if (!dev || !dev->dev)
		return -ENODEV;

	if (!dev->acquire_lock)
		return 0;

	/* host driver writes 0x2 to side band semaphore register */
	ret = iosf_mbi_write(BT_MBI_UNIT_PMC, BT_MBI_BUNIT_WRITE,
				 PUNIT_SEMAPHORE, 0x2);
	if (ret) {
		dev_err(dev->dev, "iosf punit semaphore request failed\n");
		return ret;
	}

	/* host driver waits for bit 0 to be set in semaphore register */
	start = jiffies;
	end = start + msecs_to_jiffies(SEMAPHORE_TIMEOUT);
	while (!time_after(jiffies, end)) {
		ret = get_sem(dev->dev, &sem);
		if (!ret && sem) {
			acquired = jiffies;
			dev_dbg(dev->dev, "punit semaphore acquired after %ums\n",
				jiffies_to_msecs(jiffies - start));
			return 0;
		}

		usleep_range(1000, 2000);
	}

	dev_err(dev->dev, "punit semaphore timed out, resetting\n");
	reset_semaphore(dev->dev);

	ret = iosf_mbi_read(BT_MBI_UNIT_PMC, BT_MBI_BUNIT_READ,
		PUNIT_SEMAPHORE, &sem);
	if (!ret)
		dev_err(dev->dev, "iosf failed to read punit semaphore\n");
	else
		dev_err(dev->dev, "PUNIT SEM: %d\n", sem);

	WARN_ON(1);

	return -ETIMEDOUT;
}
EXPORT_SYMBOL(baytrail_i2c_acquire);

void baytrail_i2c_release(struct dw_i2c_dev *dev)
{
	if (!dev || !dev->dev)
		return;

	if (!dev->acquire_lock)
		return;

	reset_semaphore(dev->dev);
	dev_dbg(dev->dev, "punit semaphore held for %ums\n",
		jiffies_to_msecs(jiffies - acquired));
}
EXPORT_SYMBOL(baytrail_i2c_release);

int i2c_dw_eval_lock_support(struct dw_i2c_dev *dev)
{
	acpi_status status;
	unsigned long long shared_host = 0;
	acpi_handle handle;

	if (!dev || !dev->dev)
		return 0;

	handle = ACPI_HANDLE(dev->dev);
	if (!handle)
		return 0;

	status = acpi_evaluate_integer(handle, "_SEM", NULL, &shared_host);

	if (ACPI_FAILURE(status))
		return 0;

	if (shared_host) {
		dev_info(dev->dev, "I2C bus managed by PUNIT\n");
		dev->acquire_lock = baytrail_i2c_acquire;
		dev->release_lock = baytrail_i2c_release;
		dev->pm_runtime_disabled = true;
	}

	if (!iosf_mbi_available())
		return -EPROBE_DEFER;

	return 0;
}
EXPORT_SYMBOL(i2c_dw_eval_lock_support);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Baytrail I2C Semaphore driver");
MODULE_LICENSE("GPL v2");
