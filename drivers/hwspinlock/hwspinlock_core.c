// SPDX-License-Identifier: GPL-2.0
/*
 * Hardware spinlock framework
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com
 *
 * Contact: Ohad Ben-Cohen <ohad@wizery.com>
 */

#define pr_fmt(fmt)    "%s: " fmt, __func__

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/radix-tree.h>
#include <linux/hwspinlock.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/of.h>

#include "hwspinlock_internal.h"

/* retry delay used in atomic context */
#define HWSPINLOCK_RETRY_DELAY_US	100

/* radix tree tags */
#define HWSPINLOCK_UNUSED	(0) /* tags an hwspinlock as unused */

/*
 * A radix tree is used to maintain the available hwspinlock instances.
 * The tree associates hwspinlock pointers with their integer key id,
 * and provides easy-to-use API which makes the hwspinlock core code simple
 * and easy to read.
 *
 * Radix trees are quick on lookups, and reasonably efficient in terms of
 * storage, especially with high density usages such as this framework
 * requires (a continuous range of integer keys, beginning with zero, is
 * used as the ID's of the hwspinlock instances).
 *
 * The radix tree API supports tagging items in the tree, which this
 * framework uses to mark unused hwspinlock instances (see the
 * HWSPINLOCK_UNUSED tag above). As a result, the process of querying the
 * tree, looking for an unused hwspinlock instance, is now reduced to a
 * single radix tree API call.
 */
static RADIX_TREE(hwspinlock_tree, GFP_KERNEL);

/*
 * Synchronization of access to the tree is achieved using this mutex,
 * as the radix-tree API requires that users provide all synchronisation.
 * A mutex is needed because we're using non-atomic radix tree allocations.
 */
static DEFINE_MUTEX(hwspinlock_tree_lock);


/**
 * __hwspin_trylock() - attempt to lock a specific hwspinlock
 * @hwlock: an hwspinlock which we want to trylock
 * @mode: controls whether local interrupts are disabled or not
 * @flags: a pointer where the caller's interrupt state will be saved at (if
 *         requested)
 *
 * This function attempts to lock an hwspinlock, and will immediately
 * fail if the hwspinlock is already taken.
 *
 * Caution: If the mode is HWLOCK_RAW, that means user must protect the routine
 * of getting hardware lock with mutex or spinlock. Since in some scenarios,
 * user need some time-consuming or sleepable operations under the hardware
 * lock, they need one sleepable lock (like mutex) to protect the operations.
 *
 * If the mode is neither HWLOCK_IN_ATOMIC nor HWLOCK_RAW, upon a successful
 * return from this function, preemption (and possibly interrupts) is disabled,
 * so the caller must not sleep, and is advised to release the hwspinlock as
 * soon as possible. This is required in order to minimize remote cores polling
 * on the hardware interconnect.
 *
 * The user decides whether local interrupts are disabled or not, and if yes,
 * whether he wants their previous state to be saved. It is up to the user
 * to choose the appropriate @mode of operation, exactly the same way users
 * should decide between spin_trylock, spin_trylock_irq and
 * spin_trylock_irqsave.
 *
 * Returns 0 if we successfully locked the hwspinlock or -EBUSY if
 * the hwspinlock was already taken.
 * This function will never sleep.
 */
int __hwspin_trylock(struct hwspinlock *hwlock, int mode, unsigned long *flags)
{
	int ret;

	if (WARN_ON(!hwlock || (!flags && mode == HWLOCK_IRQSTATE)))
		return -EINVAL;

	/*
	 * This spin_lock{_irq, _irqsave} serves three purposes:
	 *
	 * 1. Disable preemption, in order to minimize the period of time
	 *    in which the hwspinlock is taken. This is important in order
	 *    to minimize the possible polling on the hardware interconnect
	 *    by a remote user of this lock.
	 * 2. Make the hwspinlock SMP-safe (so we can take it from
	 *    additional contexts on the local host).
	 * 3. Ensure that in_atomic/might_sleep checks catch potential
	 *    problems with hwspinlock usage (e.g. scheduler checks like
	 *    'scheduling while atomic' etc.)
	 */
	switch (mode) {
	case HWLOCK_IRQSTATE:
		ret = spin_trylock_irqsave(&hwlock->lock, *flags);
		break;
	case HWLOCK_IRQ:
		ret = spin_trylock_irq(&hwlock->lock);
		break;
	case HWLOCK_RAW:
	case HWLOCK_IN_ATOMIC:
		ret = 1;
		break;
	default:
		ret = spin_trylock(&hwlock->lock);
		break;
	}

	/* is lock already taken by another context on the local cpu ? */
	if (!ret)
		return -EBUSY;

	/* try to take the hwspinlock device */
	ret = hwlock->bank->ops->trylock(hwlock);

	/* if hwlock is already taken, undo spin_trylock_* and exit */
	if (!ret) {
		switch (mode) {
		case HWLOCK_IRQSTATE:
			spin_unlock_irqrestore(&hwlock->lock, *flags);
			break;
		case HWLOCK_IRQ:
			spin_unlock_irq(&hwlock->lock);
			break;
		case HWLOCK_RAW:
		case HWLOCK_IN_ATOMIC:
			/* Nothing to do */
			break;
		default:
			spin_unlock(&hwlock->lock);
			break;
		}

		return -EBUSY;
	}

	/*
	 * We can be sure the other core's memory operations
	 * are observable to us only _after_ we successfully take
	 * the hwspinlock, and we must make sure that subsequent memory
	 * operations (both reads and writes) will not be reordered before
	 * we actually took the hwspinlock.
	 *
	 * Note: the implicit memory barrier of the spinlock above is too
	 * early, so we need this additional explicit memory barrier.
	 */
	mb();

	return 0;
}
EXPORT_SYMBOL_GPL(__hwspin_trylock);

/**
 * __hwspin_lock_timeout() - lock an hwspinlock with timeout limit
 * @hwlock: the hwspinlock to be locked
 * @timeout: timeout value in msecs
 * @mode: mode which controls whether local interrupts are disabled or not
 * @flags: a pointer to where the caller's interrupt state will be saved at (if
 *         requested)
 *
 * This function locks the given @hwlock. If the @hwlock
 * is already taken, the function will busy loop waiting for it to
 * be released, but give up after @timeout msecs have elapsed.
 *
 * Caution: If the mode is HWLOCK_RAW, that means user must protect the routine
 * of getting hardware lock with mutex or spinlock. Since in some scenarios,
 * user need some time-consuming or sleepable operations under the hardware
 * lock, they need one sleepable lock (like mutex) to protect the operations.
 *
 * If the mode is HWLOCK_IN_ATOMIC (called from an atomic context) the timeout
 * is handled with busy-waiting delays, hence shall not exceed few msecs.
 *
 * If the mode is neither HWLOCK_IN_ATOMIC nor HWLOCK_RAW, upon a successful
 * return from this function, preemption (and possibly interrupts) is disabled,
 * so the caller must not sleep, and is advised to release the hwspinlock as
 * soon as possible. This is required in order to minimize remote cores polling
 * on the hardware interconnect.
 *
 * The user decides whether local interrupts are disabled or not, and if yes,
 * whether he wants their previous state to be saved. It is up to the user
 * to choose the appropriate @mode of operation, exactly the same way users
 * should decide between spin_lock, spin_lock_irq and spin_lock_irqsave.
 *
 * Returns 0 when the @hwlock was successfully taken, and an appropriate
 * error code otherwise (most notably -ETIMEDOUT if the @hwlock is still
 * busy after @timeout msecs). The function will never sleep.
 */
int __hwspin_lock_timeout(struct hwspinlock *hwlock, unsigned int to,
					int mode, unsigned long *flags)
{
	int ret;
	unsigned long expire, atomic_delay = 0;

	expire = msecs_to_jiffies(to) + jiffies;

	for (;;) {
		/* Try to take the hwspinlock */
		ret = __hwspin_trylock(hwlock, mode, flags);
		if (ret != -EBUSY)
			break;

		/*
		 * The lock is already taken, let's check if the user wants
		 * us to try again
		 */
		if (mode == HWLOCK_IN_ATOMIC) {
			udelay(HWSPINLOCK_RETRY_DELAY_US);
			atomic_delay += HWSPINLOCK_RETRY_DELAY_US;
			if (atomic_delay > to * 1000)
				return -ETIMEDOUT;
		} else {
			if (time_is_before_eq_jiffies(expire))
				return -ETIMEDOUT;
		}

		/*
		 * Allow platform-specific relax handlers to prevent
		 * hogging the interconnect (no sleeping, though)
		 */
		if (hwlock->bank->ops->relax)
			hwlock->bank->ops->relax(hwlock);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(__hwspin_lock_timeout);

/**
 * __hwspin_unlock() - unlock a specific hwspinlock
 * @hwlock: a previously-acquired hwspinlock which we want to unlock
 * @mode: controls whether local interrupts needs to be restored or not
 * @flags: previous caller's interrupt state to restore (if requested)
 *
 * This function will unlock a specific hwspinlock, enable preemption and
 * (possibly) enable interrupts or restore their previous state.
 * @hwlock must be already locked before calling this function: it is a bug
 * to call unlock on a @hwlock that is already unlocked.
 *
 * The user decides whether local interrupts should be enabled or not, and
 * if yes, whether he wants their previous state to be restored. It is up
 * to the user to choose the appropriate @mode of operation, exactly the
 * same way users decide between spin_unlock, spin_unlock_irq and
 * spin_unlock_irqrestore.
 *
 * The function will never sleep.
 */
void __hwspin_unlock(struct hwspinlock *hwlock, int mode, unsigned long *flags)
{
	if (WARN_ON(!hwlock || (!flags && mode == HWLOCK_IRQSTATE)))
		return;

	/*
	 * We must make sure that memory operations (both reads and writes),
	 * done before unlocking the hwspinlock, will not be reordered
	 * after the lock is released.
	 *
	 * That's the purpose of this explicit memory barrier.
	 *
	 * Note: the memory barrier induced by the spin_unlock below is too
	 * late; the other core is going to access memory soon after it will
	 * take the hwspinlock, and by then we want to be sure our memory
	 * operations are already observable.
	 */
	mb();

	hwlock->bank->ops->unlock(hwlock);

	/* Undo the spin_trylock{_irq, _irqsave} called while locking */
	switch (mode) {
	case HWLOCK_IRQSTATE:
		spin_unlock_irqrestore(&hwlock->lock, *flags);
		break;
	case HWLOCK_IRQ:
		spin_unlock_irq(&hwlock->lock);
		break;
	case HWLOCK_RAW:
	case HWLOCK_IN_ATOMIC:
		/* Nothing to do */
		break;
	default:
		spin_unlock(&hwlock->lock);
		break;
	}
}
EXPORT_SYMBOL_GPL(__hwspin_unlock);

/**
 * of_hwspin_lock_simple_xlate - translate hwlock_spec to return a lock id
 * @bank: the hwspinlock device bank
 * @hwlock_spec: hwlock specifier as found in the device tree
 *
 * This is a simple translation function, suitable for hwspinlock platform
 * drivers that only has a lock specifier length of 1.
 *
 * Returns a relative index of the lock within a specified bank on success,
 * or -EINVAL on invalid specifier cell count.
 */
static inline int
of_hwspin_lock_simple_xlate(const struct of_phandle_args *hwlock_spec)
{
	if (WARN_ON(hwlock_spec->args_count != 1))
		return -EINVAL;

	return hwlock_spec->args[0];
}

/**
 * of_hwspin_lock_get_id() - get lock id for an OF phandle-based specific lock
 * @np: device node from which to request the specific hwlock
 * @index: index of the hwlock in the list of values
 *
 * This function provides a means for DT users of the hwspinlock module to
 * get the global lock id of a specific hwspinlock using the phandle of the
 * hwspinlock device, so that it can be requested using the normal
 * hwspin_lock_request_specific() API.
 *
 * Returns the global lock id number on success, -EPROBE_DEFER if the hwspinlock
 * device is not yet registered, -EINVAL on invalid args specifier value or an
 * appropriate error as returned from the OF parsing of the DT client node.
 */
int of_hwspin_lock_get_id(struct device_node *np, int index)
{
	struct of_phandle_args args;
	struct hwspinlock *hwlock;
	struct radix_tree_iter iter;
	void **slot;
	int id;
	int ret;

	ret = of_parse_phandle_with_args(np, "hwlocks", "#hwlock-cells", index,
					 &args);
	if (ret)
		return ret;

	if (!of_device_is_available(args.np)) {
		ret = -ENOENT;
		goto out;
	}

	/* Find the hwspinlock device: we need its base_id */
	ret = -EPROBE_DEFER;
	rcu_read_lock();
	radix_tree_for_each_slot(slot, &hwspinlock_tree, &iter, 0) {
		hwlock = radix_tree_deref_slot(slot);
		if (unlikely(!hwlock))
			continue;
		if (radix_tree_deref_retry(hwlock)) {
			slot = radix_tree_iter_retry(&iter);
			continue;
		}

		if (hwlock->bank->dev->of_node == args.np) {
			ret = 0;
			break;
		}
	}
	rcu_read_unlock();
	if (ret < 0)
		goto out;

	id = of_hwspin_lock_simple_xlate(&args);
	if (id < 0 || id >= hwlock->bank->num_locks) {
		ret = -EINVAL;
		goto out;
	}
	id += hwlock->bank->base_id;

out:
	of_node_put(args.np);
	return ret ? ret : id;
}
EXPORT_SYMBOL_GPL(of_hwspin_lock_get_id);

/**
 * of_hwspin_lock_get_id_byname() - get lock id for an specified hwlock name
 * @np: device node from which to request the specific hwlock
 * @name: hwlock name
 *
 * This function provides a means for DT users of the hwspinlock module to
 * get the global lock id of a specific hwspinlock using the specified name of
 * the hwspinlock device, so that it can be requested using the normal
 * hwspin_lock_request_specific() API.
 *
 * Returns the global lock id number on success, -EPROBE_DEFER if the hwspinlock
 * device is not yet registered, -EINVAL on invalid args specifier value or an
 * appropriate error as returned from the OF parsing of the DT client node.
 */
int of_hwspin_lock_get_id_byname(struct device_node *np, const char *name)
{
	int index;

	if (!name)
		return -EINVAL;

	index = of_property_match_string(np, "hwlock-names", name);
	if (index < 0)
		return index;

	return of_hwspin_lock_get_id(np, index);
}
EXPORT_SYMBOL_GPL(of_hwspin_lock_get_id_byname);

static int hwspin_lock_register_single(struct hwspinlock *hwlock, int id)
{
	struct hwspinlock *tmp;
	int ret;

	mutex_lock(&hwspinlock_tree_lock);

	ret = radix_tree_insert(&hwspinlock_tree, id, hwlock);
	if (ret) {
		if (ret == -EEXIST)
			pr_err("hwspinlock id %d already exists!\n", id);
		goto out;
	}

	/* mark this hwspinlock as available */
	tmp = radix_tree_tag_set(&hwspinlock_tree, id, HWSPINLOCK_UNUSED);

	/* self-sanity check which should never fail */
	WARN_ON(tmp != hwlock);

out:
	mutex_unlock(&hwspinlock_tree_lock);
	return 0;
}

static struct hwspinlock *hwspin_lock_unregister_single(unsigned int id)
{
	struct hwspinlock *hwlock = NULL;
	int ret;

	mutex_lock(&hwspinlock_tree_lock);

	/* make sure the hwspinlock is not in use (tag is set) */
	ret = radix_tree_tag_get(&hwspinlock_tree, id, HWSPINLOCK_UNUSED);
	if (ret == 0) {
		pr_err("hwspinlock %d still in use (or not present)\n", id);
		goto out;
	}

	hwlock = radix_tree_delete(&hwspinlock_tree, id);
	if (!hwlock) {
		pr_err("failed to delete hwspinlock %d\n", id);
		goto out;
	}

out:
	mutex_unlock(&hwspinlock_tree_lock);
	return hwlock;
}

/**
 * hwspin_lock_register() - register a new hw spinlock device
 * @bank: the hwspinlock device, which usually provides numerous hw locks
 * @dev: the backing device
 * @ops: hwspinlock handlers for this device
 * @base_id: id of the first hardware spinlock in this bank
 * @num_locks: number of hwspinlocks provided by this device
 *
 * This function should be called from the underlying platform-specific
 * implementation, to register a new hwspinlock device instance.
 *
 * Should be called from a process context (might sleep)
 *
 * Returns 0 on success, or an appropriate error code on failure
 */
int hwspin_lock_register(struct hwspinlock_device *bank, struct device *dev,
		const struct hwspinlock_ops *ops, int base_id, int num_locks)
{
	struct hwspinlock *hwlock;
	int ret = 0, i;

	if (!bank || !ops || !dev || !num_locks || !ops->trylock ||
							!ops->unlock) {
		pr_err("invalid parameters\n");
		return -EINVAL;
	}

	bank->dev = dev;
	bank->ops = ops;
	bank->base_id = base_id;
	bank->num_locks = num_locks;

	for (i = 0; i < num_locks; i++) {
		hwlock = &bank->lock[i];

		spin_lock_init(&hwlock->lock);
		hwlock->bank = bank;

		ret = hwspin_lock_register_single(hwlock, base_id + i);
		if (ret)
			goto reg_failed;
	}

	return 0;

reg_failed:
	while (--i >= 0)
		hwspin_lock_unregister_single(base_id + i);
	return ret;
}
EXPORT_SYMBOL_GPL(hwspin_lock_register);

/**
 * hwspin_lock_unregister() - unregister an hw spinlock device
 * @bank: the hwspinlock device, which usually provides numerous hw locks
 *
 * This function should be called from the underlying platform-specific
 * implementation, to unregister an existing (and unused) hwspinlock.
 *
 * Should be called from a process context (might sleep)
 *
 * Returns 0 on success, or an appropriate error code on failure
 */
int hwspin_lock_unregister(struct hwspinlock_device *bank)
{
	struct hwspinlock *hwlock, *tmp;
	int i;

	for (i = 0; i < bank->num_locks; i++) {
		hwlock = &bank->lock[i];

		tmp = hwspin_lock_unregister_single(bank->base_id + i);
		if (!tmp)
			return -EBUSY;

		/* self-sanity check that should never fail */
		WARN_ON(tmp != hwlock);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(hwspin_lock_unregister);

static void devm_hwspin_lock_unreg(struct device *dev, void *res)
{
	hwspin_lock_unregister(*(struct hwspinlock_device **)res);
}

static int devm_hwspin_lock_device_match(struct device *dev, void *res,
					 void *data)
{
	struct hwspinlock_device **bank = res;

	if (WARN_ON(!bank || !*bank))
		return 0;

	return *bank == data;
}

/**
 * devm_hwspin_lock_unregister() - unregister an hw spinlock device for
 *				   a managed device
 * @dev: the backing device
 * @bank: the hwspinlock device, which usually provides numerous hw locks
 *
 * This function should be called from the underlying platform-specific
 * implementation, to unregister an existing (and unused) hwspinlock.
 *
 * Should be called from a process context (might sleep)
 *
 * Returns 0 on success, or an appropriate error code on failure
 */
int devm_hwspin_lock_unregister(struct device *dev,
				struct hwspinlock_device *bank)
{
	int ret;

	ret = devres_release(dev, devm_hwspin_lock_unreg,
			     devm_hwspin_lock_device_match, bank);
	WARN_ON(ret);

	return ret;
}
EXPORT_SYMBOL_GPL(devm_hwspin_lock_unregister);

/**
 * devm_hwspin_lock_register() - register a new hw spinlock device for
 *				 a managed device
 * @dev: the backing device
 * @bank: the hwspinlock device, which usually provides numerous hw locks
 * @ops: hwspinlock handlers for this device
 * @base_id: id of the first hardware spinlock in this bank
 * @num_locks: number of hwspinlocks provided by this device
 *
 * This function should be called from the underlying platform-specific
 * implementation, to register a new hwspinlock device instance.
 *
 * Should be called from a process context (might sleep)
 *
 * Returns 0 on success, or an appropriate error code on failure
 */
int devm_hwspin_lock_register(struct device *dev,
			      struct hwspinlock_device *bank,
			      const struct hwspinlock_ops *ops,
			      int base_id, int num_locks)
{
	struct hwspinlock_device **ptr;
	int ret;

	ptr = devres_alloc(devm_hwspin_lock_unreg, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = hwspin_lock_register(bank, dev, ops, base_id, num_locks);
	if (!ret) {
		*ptr = bank;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_hwspin_lock_register);

/**
 * __hwspin_lock_request() - tag an hwspinlock as used and power it up
 *
 * This is an internal function that prepares an hwspinlock instance
 * before it is given to the user. The function assumes that
 * hwspinlock_tree_lock is taken.
 *
 * Returns 0 or positive to indicate success, and a negative value to
 * indicate an error (with the appropriate error code)
 */
static int __hwspin_lock_request(struct hwspinlock *hwlock)
{
	struct device *dev = hwlock->bank->dev;
	struct hwspinlock *tmp;
	int ret;

	/* prevent underlying implementation from being removed */
	if (!try_module_get(dev->driver->owner)) {
		dev_err(dev, "%s: can't get owner\n", __func__);
		return -EINVAL;
	}

	/* notify PM core that power is now needed */
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "%s: can't power on device\n", __func__);
		pm_runtime_put_noidle(dev);
		module_put(dev->driver->owner);
		return ret;
	}

	/* mark hwspinlock as used, should not fail */
	tmp = radix_tree_tag_clear(&hwspinlock_tree, hwlock_to_id(hwlock),
							HWSPINLOCK_UNUSED);

	/* self-sanity check that should never fail */
	WARN_ON(tmp != hwlock);

	return ret;
}

/**
 * hwspin_lock_get_id() - retrieve id number of a given hwspinlock
 * @hwlock: a valid hwspinlock instance
 *
 * Returns the id number of a given @hwlock, or -EINVAL if @hwlock is invalid.
 */
int hwspin_lock_get_id(struct hwspinlock *hwlock)
{
	if (!hwlock) {
		pr_err("invalid hwlock\n");
		return -EINVAL;
	}

	return hwlock_to_id(hwlock);
}
EXPORT_SYMBOL_GPL(hwspin_lock_get_id);

/**
 * hwspin_lock_request() - request an hwspinlock
 *
 * This function should be called by users of the hwspinlock device,
 * in order to dynamically assign them an unused hwspinlock.
 * Usually the user of this lock will then have to communicate the lock's id
 * to the remote core before it can be used for synchronization (to get the
 * id of a given hwlock, use hwspin_lock_get_id()).
 *
 * Should be called from a process context (might sleep)
 *
 * Returns the address of the assigned hwspinlock, or NULL on error
 */
struct hwspinlock *hwspin_lock_request(void)
{
	struct hwspinlock *hwlock;
	int ret;

	mutex_lock(&hwspinlock_tree_lock);

	/* look for an unused lock */
	ret = radix_tree_gang_lookup_tag(&hwspinlock_tree, (void **)&hwlock,
						0, 1, HWSPINLOCK_UNUSED);
	if (ret == 0) {
		pr_warn("a free hwspinlock is not available\n");
		hwlock = NULL;
		goto out;
	}

	/* sanity check that should never fail */
	WARN_ON(ret > 1);

	/* mark as used and power up */
	ret = __hwspin_lock_request(hwlock);
	if (ret < 0)
		hwlock = NULL;

out:
	mutex_unlock(&hwspinlock_tree_lock);
	return hwlock;
}
EXPORT_SYMBOL_GPL(hwspin_lock_request);

/**
 * hwspin_lock_request_specific() - request for a specific hwspinlock
 * @id: index of the specific hwspinlock that is requested
 *
 * This function should be called by users of the hwspinlock module,
 * in order to assign them a specific hwspinlock.
 * Usually early board code will be calling this function in order to
 * reserve specific hwspinlock ids for predefined purposes.
 *
 * Should be called from a process context (might sleep)
 *
 * Returns the address of the assigned hwspinlock, or NULL on error
 */
struct hwspinlock *hwspin_lock_request_specific(unsigned int id)
{
	struct hwspinlock *hwlock;
	int ret;

	mutex_lock(&hwspinlock_tree_lock);

	/* make sure this hwspinlock exists */
	hwlock = radix_tree_lookup(&hwspinlock_tree, id);
	if (!hwlock) {
		pr_warn("hwspinlock %u does not exist\n", id);
		goto out;
	}

	/* sanity check (this shouldn't happen) */
	WARN_ON(hwlock_to_id(hwlock) != id);

	/* make sure this hwspinlock is unused */
	ret = radix_tree_tag_get(&hwspinlock_tree, id, HWSPINLOCK_UNUSED);
	if (ret == 0) {
		pr_warn("hwspinlock %u is already in use\n", id);
		hwlock = NULL;
		goto out;
	}

	/* mark as used and power up */
	ret = __hwspin_lock_request(hwlock);
	if (ret < 0)
		hwlock = NULL;

out:
	mutex_unlock(&hwspinlock_tree_lock);
	return hwlock;
}
EXPORT_SYMBOL_GPL(hwspin_lock_request_specific);

/**
 * hwspin_lock_free() - free a specific hwspinlock
 * @hwlock: the specific hwspinlock to free
 *
 * This function mark @hwlock as free again.
 * Should only be called with an @hwlock that was retrieved from
 * an earlier call to hwspin_lock_request{_specific}.
 *
 * Should be called from a process context (might sleep)
 *
 * Returns 0 on success, or an appropriate error code on failure
 */
int hwspin_lock_free(struct hwspinlock *hwlock)
{
	struct device *dev;
	struct hwspinlock *tmp;
	int ret;

	if (!hwlock) {
		pr_err("invalid hwlock\n");
		return -EINVAL;
	}

	dev = hwlock->bank->dev;
	mutex_lock(&hwspinlock_tree_lock);

	/* make sure the hwspinlock is used */
	ret = radix_tree_tag_get(&hwspinlock_tree, hwlock_to_id(hwlock),
							HWSPINLOCK_UNUSED);
	if (ret == 1) {
		dev_err(dev, "%s: hwlock is already free\n", __func__);
		dump_stack();
		ret = -EINVAL;
		goto out;
	}

	/* notify the underlying device that power is not needed */
	ret = pm_runtime_put(dev);
	if (ret < 0)
		goto out;

	/* mark this hwspinlock as available */
	tmp = radix_tree_tag_set(&hwspinlock_tree, hwlock_to_id(hwlock),
							HWSPINLOCK_UNUSED);

	/* sanity check (this shouldn't happen) */
	WARN_ON(tmp != hwlock);

	module_put(dev->driver->owner);

out:
	mutex_unlock(&hwspinlock_tree_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(hwspin_lock_free);

static int devm_hwspin_lock_match(struct device *dev, void *res, void *data)
{
	struct hwspinlock **hwlock = res;

	if (WARN_ON(!hwlock || !*hwlock))
		return 0;

	return *hwlock == data;
}

static void devm_hwspin_lock_release(struct device *dev, void *res)
{
	hwspin_lock_free(*(struct hwspinlock **)res);
}

/**
 * devm_hwspin_lock_free() - free a specific hwspinlock for a managed device
 * @dev: the device to free the specific hwspinlock
 * @hwlock: the specific hwspinlock to free
 *
 * This function mark @hwlock as free again.
 * Should only be called with an @hwlock that was retrieved from
 * an earlier call to hwspin_lock_request{_specific}.
 *
 * Should be called from a process context (might sleep)
 *
 * Returns 0 on success, or an appropriate error code on failure
 */
int devm_hwspin_lock_free(struct device *dev, struct hwspinlock *hwlock)
{
	int ret;

	ret = devres_release(dev, devm_hwspin_lock_release,
			     devm_hwspin_lock_match, hwlock);
	WARN_ON(ret);

	return ret;
}
EXPORT_SYMBOL_GPL(devm_hwspin_lock_free);

/**
 * devm_hwspin_lock_request() - request an hwspinlock for a managed device
 * @dev: the device to request an hwspinlock
 *
 * This function should be called by users of the hwspinlock device,
 * in order to dynamically assign them an unused hwspinlock.
 * Usually the user of this lock will then have to communicate the lock's id
 * to the remote core before it can be used for synchronization (to get the
 * id of a given hwlock, use hwspin_lock_get_id()).
 *
 * Should be called from a process context (might sleep)
 *
 * Returns the address of the assigned hwspinlock, or NULL on error
 */
struct hwspinlock *devm_hwspin_lock_request(struct device *dev)
{
	struct hwspinlock **ptr, *hwlock;

	ptr = devres_alloc(devm_hwspin_lock_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	hwlock = hwspin_lock_request();
	if (hwlock) {
		*ptr = hwlock;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return hwlock;
}
EXPORT_SYMBOL_GPL(devm_hwspin_lock_request);

/**
 * devm_hwspin_lock_request_specific() - request for a specific hwspinlock for
 *					 a managed device
 * @dev: the device to request the specific hwspinlock
 * @id: index of the specific hwspinlock that is requested
 *
 * This function should be called by users of the hwspinlock module,
 * in order to assign them a specific hwspinlock.
 * Usually early board code will be calling this function in order to
 * reserve specific hwspinlock ids for predefined purposes.
 *
 * Should be called from a process context (might sleep)
 *
 * Returns the address of the assigned hwspinlock, or NULL on error
 */
struct hwspinlock *devm_hwspin_lock_request_specific(struct device *dev,
						     unsigned int id)
{
	struct hwspinlock **ptr, *hwlock;

	ptr = devres_alloc(devm_hwspin_lock_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	hwlock = hwspin_lock_request_specific(id);
	if (hwlock) {
		*ptr = hwlock;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return hwlock;
}
EXPORT_SYMBOL_GPL(devm_hwspin_lock_request_specific);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Hardware spinlock interface");
MODULE_AUTHOR("Ohad Ben-Cohen <ohad@wizery.com>");
