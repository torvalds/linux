/*
 * Copyright Altera Corporation (C) 2013. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/altera_hwmutex.h>

#define DRV_NAME	"altera_hwmutex"


static DEFINE_SPINLOCK(list_lock);	/* protect mutex_list */
static LIST_HEAD(mutex_list);

/* Mutex Registers */
#define MUTEX_REG			0x0

#define MUTEX_REG_VALUE_MASK		0xFFFF
#define MUTEX_REG_OWNER_OFFSET		16
#define MUTEX_REG_OWNER_MASK		0xFFFF
#define MUTEX_GET_OWNER(reg)			\
	((reg >> MUTEX_REG_OWNER_OFFSET) & MUTEX_REG_OWNER_MASK)

/**
 *	altera_mutex_request - Retrieves a pointer to an acquired mutex device
 *			       structure
 *	@mutex_np:	The pointer to mutex device node
 *
 *	Returns a pointer to the mutex device structure associated with the
 *	supplied device node, or NULL if no corresponding mutex device was
 *	found.
 */
struct altera_mutex *altera_mutex_request(struct device_node *mutex_np)
{
	struct altera_mutex *mutex;

	spin_lock(&list_lock);
	list_for_each_entry(mutex, &mutex_list, list) {
		if (mutex_np == mutex->pdev->dev.of_node) {
			if (!mutex->requested) {
				mutex->requested = true;
				spin_unlock(&list_lock);
				return mutex;
			} else {
				pr_info("Mutex device is in use.\n");
				spin_unlock(&list_lock);
				return NULL;
			}
		}
	}
	spin_unlock(&list_lock);
	pr_info("Mutex device not found!\n");
	return NULL;
}
EXPORT_SYMBOL(altera_mutex_request);

/**
 *	altera_mutex_free - Free the mutex
 *	@mutex:	the mutex
 *
 *	Return 0 if success. Otherwise, returns non-zero.
 */
int altera_mutex_free(struct altera_mutex *mutex)
{
	if (!mutex || !mutex->requested)
		return -EINVAL;

	spin_lock(&list_lock);
	mutex->requested = false;
	spin_unlock(&list_lock);

	return 0;
}
EXPORT_SYMBOL(altera_mutex_free);

static int __mutex_trylock(struct altera_mutex *mutex, u16 owner, u16 value)
{
	u32 read;
	int ret = 0;
	u32 data = (owner << MUTEX_REG_OWNER_OFFSET) | value;

	mutex_lock(&mutex->lock);
	__raw_writel(data, mutex->regs + MUTEX_REG);
	read = __raw_readl(mutex->regs + MUTEX_REG);
	if (read != data)
		ret = -1;

	mutex_unlock(&mutex->lock);
	return ret;
}

/**
 *	altera_mutex_lock - Acquires a hardware mutex, wait until it can get it.
 *	@mutex:	the mutex to be acquired
 *	@owner:	owner ID
 *	@value:	the new non-zero value to write to mutex
 *
 *	Returns 0 if mutex was successfully locked. Otherwise, returns non-zero.
 *
 *	The mutex must later on be released by the same owner that acquired it.
 *	This function is not ISR callable.
 */
int altera_mutex_lock(struct altera_mutex *mutex, u16 owner, u16 value)
{
	if (!mutex || !mutex->requested)
		return -EINVAL;

	while (__mutex_trylock(mutex, owner, value) != 0)
		;

	return 0;
}
EXPORT_SYMBOL(altera_mutex_lock);

/**
 *	altera_mutex_trylock - Tries once to lock the hardware mutex and returns
 *				immediately
 *	@mutex:	the mutex to be acquired
 *	@owner:	owner ID
 *	@value:	the new non-zero value to write to mutex
 *
 *	Returns 0 if mutex was successfully locked. Otherwise, returns non-zero.
 *
 *	The mutex must later on be released by the same owner that acquired it.
 *	This function is not ISR callable.
 */
int altera_mutex_trylock(struct altera_mutex *mutex, u16 owner, u16 value)
{
	if (!mutex || !mutex->requested)
		return -EINVAL;

	return __mutex_trylock(mutex, owner, value);
}
EXPORT_SYMBOL(altera_mutex_trylock);

/**
 *	altera_mutex_unlock - Unlock a mutex that has been locked by this owner
 *			      previously that was locked on the
 *			      altera_mutex_lock. Upon release, the value stored
 *			      in the mutex is set to zero.
 *	@mutex:	the mutex to be released
 *	@owner:	Owner ID
 *
 *	Returns 0 if mutex was successfully unlocked. Otherwise, returns
 *	non-zero.
 *
 *	This function is not ISR callable.
 */
int altera_mutex_unlock(struct altera_mutex *mutex, u16 owner)
{
	u32 reg;

	if (!mutex || !mutex->requested)
		return -EINVAL;

	mutex_lock(&mutex->lock);

	__raw_writel(owner << MUTEX_REG_OWNER_OFFSET,
		mutex->regs + MUTEX_REG);

	reg = __raw_readl(mutex->regs + MUTEX_REG);
	if (reg & MUTEX_REG_VALUE_MASK) {
		/* Unlock failed */
		dev_dbg(&mutex->pdev->dev,
			"Unlock mutex failed, owner %d and expected owner %d\n",
			owner, MUTEX_GET_OWNER(reg));
		mutex_unlock(&mutex->lock);
		return -EINVAL;
	}

	mutex_unlock(&mutex->lock);
	return 0;
}
EXPORT_SYMBOL(altera_mutex_unlock);

/**
 *	altera_mutex_owned - Determines if this owner owns the mutex
 *	@mutex:	the mutex to be queried
 *	@owner:	Owner ID
 *
 *	Returns 1 if the owner owns the mutex. Otherwise, returns zero.
 */
int altera_mutex_owned(struct altera_mutex *mutex, u16 owner)
{
	u32 reg;
	u16 actual_owner;
	int ret = 0;

	if (!mutex || !mutex->requested)
		return ret;

	mutex_lock(&mutex->lock);
	reg = __raw_readl(mutex->regs + MUTEX_REG);
	actual_owner = MUTEX_GET_OWNER(reg);
	if (actual_owner == owner)
		ret = 1;

	mutex_unlock(&mutex->lock);
	return ret;
}
EXPORT_SYMBOL(altera_mutex_owned);

/**
 *	altera_mutex_is_locked - Determines if the mutex is locked
 *	@mutex:	the mutex to be queried
 *
 *	Returns 1 if the mutex is locked, 0 if unlocked.
 */
int altera_mutex_is_locked(struct altera_mutex *mutex)
{
	u32 reg;
	int ret = 0;

	if (!mutex || !mutex->requested)
		return ret;

	mutex_lock(&mutex->lock);
	reg = __raw_readl(mutex->regs + MUTEX_REG);
	reg &= MUTEX_REG_VALUE_MASK;
	if (reg)
		ret = 1;

	mutex_unlock(&mutex->lock);
	return ret;
}
EXPORT_SYMBOL(altera_mutex_is_locked);

static int altera_mutex_probe(struct platform_device *pdev)
{
	struct altera_mutex *mutex;
	struct resource	*regs;

	mutex = devm_kzalloc(&pdev->dev, sizeof(struct altera_mutex),
		GFP_KERNEL);
	if (!mutex)
		return -ENOMEM;

	mutex->pdev = pdev;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;

	mutex->regs = devm_request_and_ioremap(&pdev->dev, regs);
	if (!mutex->regs)
		return -EADDRNOTAVAIL;

	mutex_init(&mutex->lock);

	spin_lock(&list_lock);
	list_add_tail(&mutex->list, &mutex_list);
	spin_unlock(&list_lock);

	platform_set_drvdata(pdev, mutex);

	return 0;
}

static int altera_mutex_remove(struct platform_device *pdev)
{
	struct altera_mutex *mutex = platform_get_drvdata(pdev);

	spin_lock(&list_lock);
	if (mutex)
		list_del(&mutex->list);
	spin_unlock(&list_lock);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id altera_mutex_match[] = {
	{ .compatible = "altr,hwmutex-1.0" },
	{ /* Sentinel */ }
};

MODULE_DEVICE_TABLE(of, altera_mutex_match);

static struct platform_driver altera_mutex_platform_driver = {
	.driver = {
		.name		= DRV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(altera_mutex_match),
	},
	.remove			= altera_mutex_remove,
};

static int __init altera_mutex_init(void)
{
	return platform_driver_probe(&altera_mutex_platform_driver,
		altera_mutex_probe);
}

static void __exit altera_mutex_exit(void)
{
	platform_driver_unregister(&altera_mutex_platform_driver);
}

module_init(altera_mutex_init);
module_exit(altera_mutex_exit);

MODULE_AUTHOR("Ley Foon Tan <lftan@altera.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Altera Hardware Mutex driver");
MODULE_ALIAS("platform:" DRV_NAME);
