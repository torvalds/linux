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
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/pm_qos.h>

#include <asm/iosf_mbi.h>

#include "i2c-designware-core.h"

#define SEMAPHORE_TIMEOUT	100
#define PUNIT_SEMAPHORE		0x7
#define PUNIT_SEMAPHORE_CHT	0x10e
#define PUNIT_SEMAPHORE_BIT	BIT(0)
#define PUNIT_SEMAPHORE_ACQUIRE	BIT(1)

static unsigned long acquired;

static u32 get_sem_addr(struct dw_i2c_dev *dev)
{
	if (dev->flags & MODEL_CHERRYTRAIL)
		return PUNIT_SEMAPHORE_CHT;
	else
		return PUNIT_SEMAPHORE;
}

static int get_sem(struct dw_i2c_dev *dev, u32 *sem)
{
	u32 addr = get_sem_addr(dev);
	u32 data;
	int ret;

	ret = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ, addr, &data);
	if (ret) {
		dev_err(dev->dev, "iosf failed to read punit semaphore\n");
		return ret;
	}

	*sem = data & PUNIT_SEMAPHORE_BIT;

	return 0;
}

static void reset_semaphore(struct dw_i2c_dev *dev)
{
	if (iosf_mbi_modify(BT_MBI_UNIT_PMC, MBI_REG_READ, get_sem_addr(dev),
			    0, PUNIT_SEMAPHORE_BIT))
		dev_err(dev->dev, "iosf failed to reset punit semaphore during write\n");

	pm_qos_update_request(&dev->pm_qos, PM_QOS_DEFAULT_VALUE);

	iosf_mbi_call_pmic_bus_access_notifier_chain(MBI_PMIC_BUS_ACCESS_END,
						     NULL);
	iosf_mbi_punit_release();
}

static int baytrail_i2c_acquire(struct dw_i2c_dev *dev)
{
	u32 addr = get_sem_addr(dev);
	u32 sem = PUNIT_SEMAPHORE_ACQUIRE;
	int ret;
	unsigned long start, end;

	might_sleep();

	if (!dev || !dev->dev)
		return -ENODEV;

	if (!dev->release_lock)
		return 0;

	iosf_mbi_punit_acquire();
	iosf_mbi_call_pmic_bus_access_notifier_chain(MBI_PMIC_BUS_ACCESS_BEGIN,
						     NULL);

	/*
	 * Disallow the CPU to enter C6 or C7 state, entering these states
	 * requires the punit to talk to the pmic and if this happens while
	 * we're holding the semaphore, the SoC hangs.
	 */
	pm_qos_update_request(&dev->pm_qos, 0);

	/* host driver writes to side band semaphore register */
	ret = iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE, addr, sem);
	if (ret) {
		dev_err(dev->dev, "iosf punit semaphore request failed\n");
		goto out;
	}

	/* host driver waits for bit 0 to be set in semaphore register */
	start = jiffies;
	end = start + msecs_to_jiffies(SEMAPHORE_TIMEOUT);
	do {
		ret = get_sem(dev, &sem);
		if (!ret && sem) {
			acquired = jiffies;
			dev_dbg(dev->dev, "punit semaphore acquired after %ums\n",
				jiffies_to_msecs(jiffies - start));
			return 0;
		}

		usleep_range(1000, 2000);
	} while (time_before(jiffies, end));

	dev_err(dev->dev, "punit semaphore timed out, resetting\n");
out:
	reset_semaphore(dev);

	ret = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ, addr, &sem);
	if (ret)
		dev_err(dev->dev, "iosf failed to read punit semaphore\n");
	else
		dev_err(dev->dev, "PUNIT SEM: %d\n", sem);

	WARN_ON(1);

	return -ETIMEDOUT;
}

static void baytrail_i2c_release(struct dw_i2c_dev *dev)
{
	if (!dev || !dev->dev)
		return;

	if (!dev->acquire_lock)
		return;

	reset_semaphore(dev);
	dev_dbg(dev->dev, "punit semaphore held for %ums\n",
		jiffies_to_msecs(jiffies - acquired));
}

int i2c_dw_probe_lock_support(struct dw_i2c_dev *dev)
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

	if (!shared_host)
		return 0;

	if (!iosf_mbi_available())
		return -EPROBE_DEFER;

	dev_info(dev->dev, "I2C bus managed by PUNIT\n");
	dev->acquire_lock = baytrail_i2c_acquire;
	dev->release_lock = baytrail_i2c_release;
	dev->pm_runtime_disabled = true;

	pm_qos_add_request(&dev->pm_qos, PM_QOS_CPU_DMA_LATENCY,
			   PM_QOS_DEFAULT_VALUE);

	return 0;
}

void i2c_dw_remove_lock_support(struct dw_i2c_dev *dev)
{
	if (dev->acquire_lock)
		pm_qos_remove_request(&dev->pm_qos);
}
