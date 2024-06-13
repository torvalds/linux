// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/base/power/clock_ops.c - Generic clock manipulation PM callbacks
 *
 * Copyright (c) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Renesas Electronics Corp.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/pm.h>
#include <linux/pm_clock.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/of_clk.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

#ifdef CONFIG_PM_CLK

enum pce_status {
	PCE_STATUS_NONE = 0,
	PCE_STATUS_ACQUIRED,
	PCE_STATUS_PREPARED,
	PCE_STATUS_ENABLED,
	PCE_STATUS_ERROR,
};

struct pm_clock_entry {
	struct list_head node;
	char *con_id;
	struct clk *clk;
	enum pce_status status;
	bool enabled_when_prepared;
};

/**
 * pm_clk_list_lock - ensure exclusive access for modifying the PM clock
 *		      entry list.
 * @psd: pm_subsys_data instance corresponding to the PM clock entry list
 *	 and clk_op_might_sleep count to be modified.
 *
 * Get exclusive access before modifying the PM clock entry list and the
 * clock_op_might_sleep count to guard against concurrent modifications.
 * This also protects against a concurrent clock_op_might_sleep and PM clock
 * entry list usage in pm_clk_suspend()/pm_clk_resume() that may or may not
 * happen in atomic context, hence both the mutex and the spinlock must be
 * taken here.
 */
static void pm_clk_list_lock(struct pm_subsys_data *psd)
	__acquires(&psd->lock)
{
	mutex_lock(&psd->clock_mutex);
	spin_lock_irq(&psd->lock);
}

/**
 * pm_clk_list_unlock - counterpart to pm_clk_list_lock().
 * @psd: the same pm_subsys_data instance previously passed to
 *	 pm_clk_list_lock().
 */
static void pm_clk_list_unlock(struct pm_subsys_data *psd)
	__releases(&psd->lock)
{
	spin_unlock_irq(&psd->lock);
	mutex_unlock(&psd->clock_mutex);
}

/**
 * pm_clk_op_lock - ensure exclusive access for performing clock operations.
 * @psd: pm_subsys_data instance corresponding to the PM clock entry list
 *	 and clk_op_might_sleep count being used.
 * @flags: stored irq flags.
 * @fn: string for the caller function's name.
 *
 * This is used by pm_clk_suspend() and pm_clk_resume() to guard
 * against concurrent modifications to the clock entry list and the
 * clock_op_might_sleep count. If clock_op_might_sleep is != 0 then
 * only the mutex can be locked and those functions can only be used in
 * non atomic context. If clock_op_might_sleep == 0 then these functions
 * may be used in any context and only the spinlock can be locked.
 * Returns -EINVAL if called in atomic context when clock ops might sleep.
 */
static int pm_clk_op_lock(struct pm_subsys_data *psd, unsigned long *flags,
			  const char *fn)
	/* sparse annotations don't work here as exit state isn't static */
{
	bool atomic_context = in_atomic() || irqs_disabled();

try_again:
	spin_lock_irqsave(&psd->lock, *flags);
	if (!psd->clock_op_might_sleep) {
		/* the __release is there to work around sparse limitations */
		__release(&psd->lock);
		return 0;
	}

	/* bail out if in atomic context */
	if (atomic_context) {
		pr_err("%s: atomic context with clock_ops_might_sleep = %d",
		       fn, psd->clock_op_might_sleep);
		spin_unlock_irqrestore(&psd->lock, *flags);
		might_sleep();
		return -EPERM;
	}

	/* we must switch to the mutex */
	spin_unlock_irqrestore(&psd->lock, *flags);
	mutex_lock(&psd->clock_mutex);

	/*
	 * There was a possibility for psd->clock_op_might_sleep
	 * to become 0 above. Keep the mutex only if not the case.
	 */
	if (likely(psd->clock_op_might_sleep))
		return 0;

	mutex_unlock(&psd->clock_mutex);
	goto try_again;
}

/**
 * pm_clk_op_unlock - counterpart to pm_clk_op_lock().
 * @psd: the same pm_subsys_data instance previously passed to
 *	 pm_clk_op_lock().
 * @flags: irq flags provided by pm_clk_op_lock().
 */
static void pm_clk_op_unlock(struct pm_subsys_data *psd, unsigned long *flags)
	/* sparse annotations don't work here as entry state isn't static */
{
	if (psd->clock_op_might_sleep) {
		mutex_unlock(&psd->clock_mutex);
	} else {
		/* the __acquire is there to work around sparse limitations */
		__acquire(&psd->lock);
		spin_unlock_irqrestore(&psd->lock, *flags);
	}
}

/**
 * __pm_clk_enable - Enable a clock, reporting any errors
 * @dev: The device for the given clock
 * @ce: PM clock entry corresponding to the clock.
 */
static inline void __pm_clk_enable(struct device *dev, struct pm_clock_entry *ce)
{
	int ret;

	switch (ce->status) {
	case PCE_STATUS_ACQUIRED:
		ret = clk_prepare_enable(ce->clk);
		break;
	case PCE_STATUS_PREPARED:
		ret = clk_enable(ce->clk);
		break;
	default:
		return;
	}
	if (!ret)
		ce->status = PCE_STATUS_ENABLED;
	else
		dev_err(dev, "%s: failed to enable clk %p, error %d\n",
			__func__, ce->clk, ret);
}

/**
 * pm_clk_acquire - Acquire a device clock.
 * @dev: Device whose clock is to be acquired.
 * @ce: PM clock entry corresponding to the clock.
 */
static void pm_clk_acquire(struct device *dev, struct pm_clock_entry *ce)
{
	if (!ce->clk)
		ce->clk = clk_get(dev, ce->con_id);
	if (IS_ERR(ce->clk)) {
		ce->status = PCE_STATUS_ERROR;
		return;
	} else if (clk_is_enabled_when_prepared(ce->clk)) {
		/* we defer preparing the clock in that case */
		ce->status = PCE_STATUS_ACQUIRED;
		ce->enabled_when_prepared = true;
	} else if (clk_prepare(ce->clk)) {
		ce->status = PCE_STATUS_ERROR;
		dev_err(dev, "clk_prepare() failed\n");
		return;
	} else {
		ce->status = PCE_STATUS_PREPARED;
	}
	dev_dbg(dev, "Clock %pC con_id %s managed by runtime PM.\n",
		ce->clk, ce->con_id);
}

static int __pm_clk_add(struct device *dev, const char *con_id,
			struct clk *clk)
{
	struct pm_subsys_data *psd = dev_to_psd(dev);
	struct pm_clock_entry *ce;

	if (!psd)
		return -EINVAL;

	ce = kzalloc(sizeof(*ce), GFP_KERNEL);
	if (!ce)
		return -ENOMEM;

	if (con_id) {
		ce->con_id = kstrdup(con_id, GFP_KERNEL);
		if (!ce->con_id) {
			kfree(ce);
			return -ENOMEM;
		}
	} else {
		if (IS_ERR(clk)) {
			kfree(ce);
			return -ENOENT;
		}
		ce->clk = clk;
	}

	pm_clk_acquire(dev, ce);

	pm_clk_list_lock(psd);
	list_add_tail(&ce->node, &psd->clock_list);
	if (ce->enabled_when_prepared)
		psd->clock_op_might_sleep++;
	pm_clk_list_unlock(psd);
	return 0;
}

/**
 * pm_clk_add - Start using a device clock for power management.
 * @dev: Device whose clock is going to be used for power management.
 * @con_id: Connection ID of the clock.
 *
 * Add the clock represented by @con_id to the list of clocks used for
 * the power management of @dev.
 */
int pm_clk_add(struct device *dev, const char *con_id)
{
	return __pm_clk_add(dev, con_id, NULL);
}
EXPORT_SYMBOL_GPL(pm_clk_add);

/**
 * pm_clk_add_clk - Start using a device clock for power management.
 * @dev: Device whose clock is going to be used for power management.
 * @clk: Clock pointer
 *
 * Add the clock to the list of clocks used for the power management of @dev.
 * The power-management code will take control of the clock reference, so
 * callers should not call clk_put() on @clk after this function sucessfully
 * returned.
 */
int pm_clk_add_clk(struct device *dev, struct clk *clk)
{
	return __pm_clk_add(dev, NULL, clk);
}
EXPORT_SYMBOL_GPL(pm_clk_add_clk);


/**
 * of_pm_clk_add_clk - Start using a device clock for power management.
 * @dev: Device whose clock is going to be used for power management.
 * @name: Name of clock that is going to be used for power management.
 *
 * Add the clock described in the 'clocks' device-tree node that matches
 * with the 'name' provided, to the list of clocks used for the power
 * management of @dev. On success, returns 0. Returns a negative error
 * code if the clock is not found or cannot be added.
 */
int of_pm_clk_add_clk(struct device *dev, const char *name)
{
	struct clk *clk;
	int ret;

	if (!dev || !dev->of_node || !name)
		return -EINVAL;

	clk = of_clk_get_by_name(dev->of_node, name);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = pm_clk_add_clk(dev, clk);
	if (ret) {
		clk_put(clk);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(of_pm_clk_add_clk);

/**
 * of_pm_clk_add_clks - Start using device clock(s) for power management.
 * @dev: Device whose clock(s) is going to be used for power management.
 *
 * Add a series of clocks described in the 'clocks' device-tree node for
 * a device to the list of clocks used for the power management of @dev.
 * On success, returns the number of clocks added. Returns a negative
 * error code if there are no clocks in the device node for the device
 * or if adding a clock fails.
 */
int of_pm_clk_add_clks(struct device *dev)
{
	struct clk **clks;
	int i, count;
	int ret;

	if (!dev || !dev->of_node)
		return -EINVAL;

	count = of_clk_get_parent_count(dev->of_node);
	if (count <= 0)
		return -ENODEV;

	clks = kcalloc(count, sizeof(*clks), GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		clks[i] = of_clk_get(dev->of_node, i);
		if (IS_ERR(clks[i])) {
			ret = PTR_ERR(clks[i]);
			goto error;
		}

		ret = pm_clk_add_clk(dev, clks[i]);
		if (ret) {
			clk_put(clks[i]);
			goto error;
		}
	}

	kfree(clks);

	return i;

error:
	while (i--)
		pm_clk_remove_clk(dev, clks[i]);

	kfree(clks);

	return ret;
}
EXPORT_SYMBOL_GPL(of_pm_clk_add_clks);

/**
 * __pm_clk_remove - Destroy PM clock entry.
 * @ce: PM clock entry to destroy.
 */
static void __pm_clk_remove(struct pm_clock_entry *ce)
{
	if (!ce)
		return;

	switch (ce->status) {
	case PCE_STATUS_ENABLED:
		clk_disable(ce->clk);
		fallthrough;
	case PCE_STATUS_PREPARED:
		clk_unprepare(ce->clk);
		fallthrough;
	case PCE_STATUS_ACQUIRED:
	case PCE_STATUS_ERROR:
		if (!IS_ERR(ce->clk))
			clk_put(ce->clk);
		break;
	default:
		break;
	}

	kfree(ce->con_id);
	kfree(ce);
}

/**
 * pm_clk_remove - Stop using a device clock for power management.
 * @dev: Device whose clock should not be used for PM any more.
 * @con_id: Connection ID of the clock.
 *
 * Remove the clock represented by @con_id from the list of clocks used for
 * the power management of @dev.
 */
void pm_clk_remove(struct device *dev, const char *con_id)
{
	struct pm_subsys_data *psd = dev_to_psd(dev);
	struct pm_clock_entry *ce;

	if (!psd)
		return;

	pm_clk_list_lock(psd);

	list_for_each_entry(ce, &psd->clock_list, node) {
		if (!con_id && !ce->con_id)
			goto remove;
		else if (!con_id || !ce->con_id)
			continue;
		else if (!strcmp(con_id, ce->con_id))
			goto remove;
	}

	pm_clk_list_unlock(psd);
	return;

 remove:
	list_del(&ce->node);
	if (ce->enabled_when_prepared)
		psd->clock_op_might_sleep--;
	pm_clk_list_unlock(psd);

	__pm_clk_remove(ce);
}
EXPORT_SYMBOL_GPL(pm_clk_remove);

/**
 * pm_clk_remove_clk - Stop using a device clock for power management.
 * @dev: Device whose clock should not be used for PM any more.
 * @clk: Clock pointer
 *
 * Remove the clock pointed to by @clk from the list of clocks used for
 * the power management of @dev.
 */
void pm_clk_remove_clk(struct device *dev, struct clk *clk)
{
	struct pm_subsys_data *psd = dev_to_psd(dev);
	struct pm_clock_entry *ce;

	if (!psd || !clk)
		return;

	pm_clk_list_lock(psd);

	list_for_each_entry(ce, &psd->clock_list, node) {
		if (clk == ce->clk)
			goto remove;
	}

	pm_clk_list_unlock(psd);
	return;

 remove:
	list_del(&ce->node);
	if (ce->enabled_when_prepared)
		psd->clock_op_might_sleep--;
	pm_clk_list_unlock(psd);

	__pm_clk_remove(ce);
}
EXPORT_SYMBOL_GPL(pm_clk_remove_clk);

/**
 * pm_clk_init - Initialize a device's list of power management clocks.
 * @dev: Device to initialize the list of PM clocks for.
 *
 * Initialize the lock and clock_list members of the device's pm_subsys_data
 * object, set the count of clocks that might sleep to 0.
 */
void pm_clk_init(struct device *dev)
{
	struct pm_subsys_data *psd = dev_to_psd(dev);
	if (psd) {
		INIT_LIST_HEAD(&psd->clock_list);
		mutex_init(&psd->clock_mutex);
		psd->clock_op_might_sleep = 0;
	}
}
EXPORT_SYMBOL_GPL(pm_clk_init);

/**
 * pm_clk_create - Create and initialize a device's list of PM clocks.
 * @dev: Device to create and initialize the list of PM clocks for.
 *
 * Allocate a struct pm_subsys_data object, initialize its lock and clock_list
 * members and make the @dev's power.subsys_data field point to it.
 */
int pm_clk_create(struct device *dev)
{
	return dev_pm_get_subsys_data(dev);
}
EXPORT_SYMBOL_GPL(pm_clk_create);

/**
 * pm_clk_destroy - Destroy a device's list of power management clocks.
 * @dev: Device to destroy the list of PM clocks for.
 *
 * Clear the @dev's power.subsys_data field, remove the list of clock entries
 * from the struct pm_subsys_data object pointed to by it before and free
 * that object.
 */
void pm_clk_destroy(struct device *dev)
{
	struct pm_subsys_data *psd = dev_to_psd(dev);
	struct pm_clock_entry *ce, *c;
	struct list_head list;

	if (!psd)
		return;

	INIT_LIST_HEAD(&list);

	pm_clk_list_lock(psd);

	list_for_each_entry_safe_reverse(ce, c, &psd->clock_list, node)
		list_move(&ce->node, &list);
	psd->clock_op_might_sleep = 0;

	pm_clk_list_unlock(psd);

	dev_pm_put_subsys_data(dev);

	list_for_each_entry_safe_reverse(ce, c, &list, node) {
		list_del(&ce->node);
		__pm_clk_remove(ce);
	}
}
EXPORT_SYMBOL_GPL(pm_clk_destroy);

static void pm_clk_destroy_action(void *data)
{
	pm_clk_destroy(data);
}

int devm_pm_clk_create(struct device *dev)
{
	int ret;

	ret = pm_clk_create(dev);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, pm_clk_destroy_action, dev);
}
EXPORT_SYMBOL_GPL(devm_pm_clk_create);

/**
 * pm_clk_suspend - Disable clocks in a device's PM clock list.
 * @dev: Device to disable the clocks for.
 */
int pm_clk_suspend(struct device *dev)
{
	struct pm_subsys_data *psd = dev_to_psd(dev);
	struct pm_clock_entry *ce;
	unsigned long flags;
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	if (!psd)
		return 0;

	ret = pm_clk_op_lock(psd, &flags, __func__);
	if (ret)
		return ret;

	list_for_each_entry_reverse(ce, &psd->clock_list, node) {
		if (ce->status == PCE_STATUS_ENABLED) {
			if (ce->enabled_when_prepared) {
				clk_disable_unprepare(ce->clk);
				ce->status = PCE_STATUS_ACQUIRED;
			} else {
				clk_disable(ce->clk);
				ce->status = PCE_STATUS_PREPARED;
			}
		}
	}

	pm_clk_op_unlock(psd, &flags);

	return 0;
}
EXPORT_SYMBOL_GPL(pm_clk_suspend);

/**
 * pm_clk_resume - Enable clocks in a device's PM clock list.
 * @dev: Device to enable the clocks for.
 */
int pm_clk_resume(struct device *dev)
{
	struct pm_subsys_data *psd = dev_to_psd(dev);
	struct pm_clock_entry *ce;
	unsigned long flags;
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	if (!psd)
		return 0;

	ret = pm_clk_op_lock(psd, &flags, __func__);
	if (ret)
		return ret;

	list_for_each_entry(ce, &psd->clock_list, node)
		__pm_clk_enable(dev, ce);

	pm_clk_op_unlock(psd, &flags);

	return 0;
}
EXPORT_SYMBOL_GPL(pm_clk_resume);

/**
 * pm_clk_notify - Notify routine for device addition and removal.
 * @nb: Notifier block object this function is a member of.
 * @action: Operation being carried out by the caller.
 * @data: Device the routine is being run for.
 *
 * For this function to work, @nb must be a member of an object of type
 * struct pm_clk_notifier_block containing all of the requisite data.
 * Specifically, the pm_domain member of that object is copied to the device's
 * pm_domain field and its con_ids member is used to populate the device's list
 * of PM clocks, depending on @action.
 *
 * If the device's pm_domain field is already populated with a value different
 * from the one stored in the struct pm_clk_notifier_block object, the function
 * does nothing.
 */
static int pm_clk_notify(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct pm_clk_notifier_block *clknb;
	struct device *dev = data;
	char **con_id;
	int error;

	dev_dbg(dev, "%s() %ld\n", __func__, action);

	clknb = container_of(nb, struct pm_clk_notifier_block, nb);

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		if (dev->pm_domain)
			break;

		error = pm_clk_create(dev);
		if (error)
			break;

		dev_pm_domain_set(dev, clknb->pm_domain);
		if (clknb->con_ids[0]) {
			for (con_id = clknb->con_ids; *con_id; con_id++)
				pm_clk_add(dev, *con_id);
		} else {
			pm_clk_add(dev, NULL);
		}

		break;
	case BUS_NOTIFY_DEL_DEVICE:
		if (dev->pm_domain != clknb->pm_domain)
			break;

		dev_pm_domain_set(dev, NULL);
		pm_clk_destroy(dev);
		break;
	}

	return 0;
}

int pm_clk_runtime_suspend(struct device *dev)
{
	int ret;

	dev_dbg(dev, "%s\n", __func__);

	ret = pm_generic_runtime_suspend(dev);
	if (ret) {
		dev_err(dev, "failed to suspend device\n");
		return ret;
	}

	ret = pm_clk_suspend(dev);
	if (ret) {
		dev_err(dev, "failed to suspend clock\n");
		pm_generic_runtime_resume(dev);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pm_clk_runtime_suspend);

int pm_clk_runtime_resume(struct device *dev)
{
	int ret;

	dev_dbg(dev, "%s\n", __func__);

	ret = pm_clk_resume(dev);
	if (ret) {
		dev_err(dev, "failed to resume clock\n");
		return ret;
	}

	return pm_generic_runtime_resume(dev);
}
EXPORT_SYMBOL_GPL(pm_clk_runtime_resume);

#else /* !CONFIG_PM_CLK */

/**
 * enable_clock - Enable a device clock.
 * @dev: Device whose clock is to be enabled.
 * @con_id: Connection ID of the clock.
 */
static void enable_clock(struct device *dev, const char *con_id)
{
	struct clk *clk;

	clk = clk_get(dev, con_id);
	if (!IS_ERR(clk)) {
		clk_prepare_enable(clk);
		clk_put(clk);
		dev_info(dev, "Runtime PM disabled, clock forced on.\n");
	}
}

/**
 * disable_clock - Disable a device clock.
 * @dev: Device whose clock is to be disabled.
 * @con_id: Connection ID of the clock.
 */
static void disable_clock(struct device *dev, const char *con_id)
{
	struct clk *clk;

	clk = clk_get(dev, con_id);
	if (!IS_ERR(clk)) {
		clk_disable_unprepare(clk);
		clk_put(clk);
		dev_info(dev, "Runtime PM disabled, clock forced off.\n");
	}
}

/**
 * pm_clk_notify - Notify routine for device addition and removal.
 * @nb: Notifier block object this function is a member of.
 * @action: Operation being carried out by the caller.
 * @data: Device the routine is being run for.
 *
 * For this function to work, @nb must be a member of an object of type
 * struct pm_clk_notifier_block containing all of the requisite data.
 * Specifically, the con_ids member of that object is used to enable or disable
 * the device's clocks, depending on @action.
 */
static int pm_clk_notify(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct pm_clk_notifier_block *clknb;
	struct device *dev = data;
	char **con_id;

	dev_dbg(dev, "%s() %ld\n", __func__, action);

	clknb = container_of(nb, struct pm_clk_notifier_block, nb);

	switch (action) {
	case BUS_NOTIFY_BIND_DRIVER:
		if (clknb->con_ids[0]) {
			for (con_id = clknb->con_ids; *con_id; con_id++)
				enable_clock(dev, *con_id);
		} else {
			enable_clock(dev, NULL);
		}
		break;
	case BUS_NOTIFY_DRIVER_NOT_BOUND:
	case BUS_NOTIFY_UNBOUND_DRIVER:
		if (clknb->con_ids[0]) {
			for (con_id = clknb->con_ids; *con_id; con_id++)
				disable_clock(dev, *con_id);
		} else {
			disable_clock(dev, NULL);
		}
		break;
	}

	return 0;
}

#endif /* !CONFIG_PM_CLK */

/**
 * pm_clk_add_notifier - Add bus type notifier for power management clocks.
 * @bus: Bus type to add the notifier to.
 * @clknb: Notifier to be added to the given bus type.
 *
 * The nb member of @clknb is not expected to be initialized and its
 * notifier_call member will be replaced with pm_clk_notify().  However,
 * the remaining members of @clknb should be populated prior to calling this
 * routine.
 */
void pm_clk_add_notifier(struct bus_type *bus,
				 struct pm_clk_notifier_block *clknb)
{
	if (!bus || !clknb)
		return;

	clknb->nb.notifier_call = pm_clk_notify;
	bus_register_notifier(bus, &clknb->nb);
}
EXPORT_SYMBOL_GPL(pm_clk_add_notifier);
