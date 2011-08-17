/*
 * OMAP hardware spinlock driver
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com
 *
 * Contact: Simon Que <sque@ti.com>
 *          Hari Kanigeri <h-kanigeri2@ti.com>
 *          Ohad Ben-Cohen <ohad@wizery.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/hwspinlock.h>
#include <linux/platform_device.h>

#include "hwspinlock_internal.h"

/* Spinlock register offsets */
#define SYSSTATUS_OFFSET		0x0014
#define LOCK_BASE_OFFSET		0x0800

#define SPINLOCK_NUMLOCKS_BIT_OFFSET	(24)

/* Possible values of SPINLOCK_LOCK_REG */
#define SPINLOCK_NOTTAKEN		(0)	/* free */
#define SPINLOCK_TAKEN			(1)	/* locked */

#define to_omap_hwspinlock(lock)	\
	container_of(lock, struct omap_hwspinlock, lock)

struct omap_hwspinlock {
	struct hwspinlock lock;
	void __iomem *addr;
};

struct omap_hwspinlock_state {
	int num_locks;			/* Total number of locks in system */
	void __iomem *io_base;		/* Mapped base address */
};

static int omap_hwspinlock_trylock(struct hwspinlock *lock)
{
	struct omap_hwspinlock *omap_lock = to_omap_hwspinlock(lock);

	/* attempt to acquire the lock by reading its value */
	return (SPINLOCK_NOTTAKEN == readl(omap_lock->addr));
}

static void omap_hwspinlock_unlock(struct hwspinlock *lock)
{
	struct omap_hwspinlock *omap_lock = to_omap_hwspinlock(lock);

	/* release the lock by writing 0 to it */
	writel(SPINLOCK_NOTTAKEN, omap_lock->addr);
}

/*
 * relax the OMAP interconnect while spinning on it.
 *
 * The specs recommended that the retry delay time will be
 * just over half of the time that a requester would be
 * expected to hold the lock.
 *
 * The number below is taken from an hardware specs example,
 * obviously it is somewhat arbitrary.
 */
static void omap_hwspinlock_relax(struct hwspinlock *lock)
{
	ndelay(50);
}

static const struct hwspinlock_ops omap_hwspinlock_ops = {
	.trylock = omap_hwspinlock_trylock,
	.unlock = omap_hwspinlock_unlock,
	.relax = omap_hwspinlock_relax,
};

static int __devinit omap_hwspinlock_probe(struct platform_device *pdev)
{
	struct omap_hwspinlock *omap_lock;
	struct omap_hwspinlock_state *state;
	struct hwspinlock *lock;
	struct resource *res;
	void __iomem *io_base;
	int i, ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	io_base = ioremap(res->start, resource_size(res));
	if (!io_base) {
		ret = -ENOMEM;
		goto free_state;
	}

	/* Determine number of locks */
	i = readl(io_base + SYSSTATUS_OFFSET);
	i >>= SPINLOCK_NUMLOCKS_BIT_OFFSET;

	/* one of the four lsb's must be set, and nothing else */
	if (hweight_long(i & 0xf) != 1 || i > 8) {
		ret = -EINVAL;
		goto iounmap_base;
	}

	state->num_locks = i * 32;
	state->io_base = io_base;

	platform_set_drvdata(pdev, state);

	/*
	 * runtime PM will make sure the clock of this module is
	 * enabled iff at least one lock is requested
	 */
	pm_runtime_enable(&pdev->dev);

	for (i = 0; i < state->num_locks; i++) {
		omap_lock = kzalloc(sizeof(*omap_lock), GFP_KERNEL);
		if (!omap_lock) {
			ret = -ENOMEM;
			goto free_locks;
		}

		omap_lock->lock.dev = &pdev->dev;
		omap_lock->lock.owner = THIS_MODULE;
		omap_lock->lock.id = i;
		omap_lock->lock.ops = &omap_hwspinlock_ops;
		omap_lock->addr = io_base + LOCK_BASE_OFFSET + sizeof(u32) * i;

		ret = hwspin_lock_register(&omap_lock->lock);
		if (ret) {
			kfree(omap_lock);
			goto free_locks;
		}
	}

	return 0;

free_locks:
	while (--i >= 0) {
		lock = hwspin_lock_unregister(i);
		/* this should't happen, but let's give our best effort */
		if (!lock) {
			dev_err(&pdev->dev, "%s: cleanups failed\n", __func__);
			continue;
		}
		omap_lock = to_omap_hwspinlock(lock);
		kfree(omap_lock);
	}
	pm_runtime_disable(&pdev->dev);
iounmap_base:
	iounmap(io_base);
free_state:
	kfree(state);
	return ret;
}

static int omap_hwspinlock_remove(struct platform_device *pdev)
{
	struct omap_hwspinlock_state *state = platform_get_drvdata(pdev);
	struct hwspinlock *lock;
	struct omap_hwspinlock *omap_lock;
	int i;

	for (i = 0; i < state->num_locks; i++) {
		lock = hwspin_lock_unregister(i);
		/* this shouldn't happen at this point. if it does, at least
		 * don't continue with the remove */
		if (!lock) {
			dev_err(&pdev->dev, "%s: failed on %d\n", __func__, i);
			return -EBUSY;
		}

		omap_lock = to_omap_hwspinlock(lock);
		kfree(omap_lock);
	}

	pm_runtime_disable(&pdev->dev);
	iounmap(state->io_base);
	kfree(state);

	return 0;
}

static struct platform_driver omap_hwspinlock_driver = {
	.probe		= omap_hwspinlock_probe,
	.remove		= omap_hwspinlock_remove,
	.driver		= {
		.name	= "omap_hwspinlock",
	},
};

static int __init omap_hwspinlock_init(void)
{
	return platform_driver_register(&omap_hwspinlock_driver);
}
/* board init code might need to reserve hwspinlocks for predefined purposes */
postcore_initcall(omap_hwspinlock_init);

static void __exit omap_hwspinlock_exit(void)
{
	platform_driver_unregister(&omap_hwspinlock_driver);
}
module_exit(omap_hwspinlock_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Hardware spinlock driver for OMAP");
MODULE_AUTHOR("Simon Que <sque@ti.com>");
MODULE_AUTHOR("Hari Kanigeri <h-kanigeri2@ti.com>");
MODULE_AUTHOR("Ohad Ben-Cohen <ohad@wizery.com>");
