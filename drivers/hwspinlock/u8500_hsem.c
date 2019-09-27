// SPDX-License-Identifier: GPL-2.0
/*
 * u8500 HWSEM driver
 *
 * Copyright (C) 2010-2011 ST-Ericsson
 *
 * Implements u8500 semaphore handling for protocol 1, no interrupts.
 *
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 * Heavily borrowed from the work of :
 *   Simon Que <sque@ti.com>
 *   Hari Kanigeri <h-kanigeri2@ti.com>
 *   Ohad Ben-Cohen <ohad@wizery.com>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/hwspinlock.h>
#include <linux/platform_device.h>

#include "hwspinlock_internal.h"

/*
 * Implementation of STE's HSem protocol 1 without interrutps.
 * The only masterID we allow is '0x01' to force people to use
 * HSems for synchronisation between processors rather than processes
 * on the ARM core.
 */

#define U8500_MAX_SEMAPHORE		32	/* a total of 32 semaphore */
#define RESET_SEMAPHORE			(0)	/* free */

/*
 * CPU ID for master running u8500 kernel.
 * Hswpinlocks should only be used to synchonise operations
 * between the Cortex A9 core and the other CPUs.  Hence
 * forcing the masterID to a preset value.
 */
#define HSEM_MASTER_ID			0x01

#define HSEM_REGISTER_OFFSET		0x08

#define HSEM_CTRL_REG			0x00
#define HSEM_ICRALL			0x90
#define HSEM_PROTOCOL_1			0x01

static int u8500_hsem_trylock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	writel(HSEM_MASTER_ID, lock_addr);

	/* get only first 4 bit and compare to masterID.
	 * if equal, we have the semaphore, otherwise
	 * someone else has it.
	 */
	return (HSEM_MASTER_ID == (0x0F & readl(lock_addr)));
}

static void u8500_hsem_unlock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	/* release the lock by writing 0 to it */
	writel(RESET_SEMAPHORE, lock_addr);
}

/*
 * u8500: what value is recommended here ?
 */
static void u8500_hsem_relax(struct hwspinlock *lock)
{
	ndelay(50);
}

static const struct hwspinlock_ops u8500_hwspinlock_ops = {
	.trylock	= u8500_hsem_trylock,
	.unlock		= u8500_hsem_unlock,
	.relax		= u8500_hsem_relax,
};

static int u8500_hsem_probe(struct platform_device *pdev)
{
	struct hwspinlock_pdata *pdata = pdev->dev.platform_data;
	struct hwspinlock_device *bank;
	struct hwspinlock *hwlock;
	void __iomem *io_base;
	int i, ret, num_locks = U8500_MAX_SEMAPHORE;
	ulong val;

	if (!pdata)
		return -ENODEV;

	io_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	/* make sure protocol 1 is selected */
	val = readl(io_base + HSEM_CTRL_REG);
	writel((val & ~HSEM_PROTOCOL_1), io_base + HSEM_CTRL_REG);

	/* clear all interrupts */
	writel(0xFFFF, io_base + HSEM_ICRALL);

	bank = devm_kzalloc(&pdev->dev, struct_size(bank, lock, num_locks),
			    GFP_KERNEL);
	if (!bank)
		return -ENOMEM;

	platform_set_drvdata(pdev, bank);

	for (i = 0, hwlock = &bank->lock[0]; i < num_locks; i++, hwlock++)
		hwlock->priv = io_base + HSEM_REGISTER_OFFSET + sizeof(u32) * i;

	/* no pm needed for HSem but required to comply with hwspilock core */
	pm_runtime_enable(&pdev->dev);

	ret = devm_hwspin_lock_register(&pdev->dev, bank, &u8500_hwspinlock_ops,
					pdata->base_id, num_locks);
	if (ret) {
		pm_runtime_disable(&pdev->dev);
		return ret;
	}

	return 0;
}

static int u8500_hsem_remove(struct platform_device *pdev)
{
	struct hwspinlock_device *bank = platform_get_drvdata(pdev);
	void __iomem *io_base = bank->lock[0].priv - HSEM_REGISTER_OFFSET;

	/* clear all interrupts */
	writel(0xFFFF, io_base + HSEM_ICRALL);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver u8500_hsem_driver = {
	.probe		= u8500_hsem_probe,
	.remove		= u8500_hsem_remove,
	.driver		= {
		.name	= "u8500_hsem",
	},
};

static int __init u8500_hsem_init(void)
{
	return platform_driver_register(&u8500_hsem_driver);
}
/* board init code might need to reserve hwspinlocks for predefined purposes */
postcore_initcall(u8500_hsem_init);

static void __exit u8500_hsem_exit(void)
{
	platform_driver_unregister(&u8500_hsem_driver);
}
module_exit(u8500_hsem_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Hardware Spinlock driver for u8500");
MODULE_AUTHOR("Mathieu Poirier <mathieu.poirier@linaro.org>");
