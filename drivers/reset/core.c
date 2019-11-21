// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Reset Controller framework
 *
 * Copyright 2013 Philipp Zabel, Pengutronix
 */
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/reset.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>

static DEFINE_MUTEX(reset_list_mutex);
static LIST_HEAD(reset_controller_list);

static DEFINE_MUTEX(reset_lookup_mutex);
static LIST_HEAD(reset_lookup_list);

/**
 * struct reset_control - a reset control
 * @rcdev: a pointer to the reset controller device
 *         this reset control belongs to
 * @list: list entry for the rcdev's reset controller list
 * @id: ID of the reset controller in the reset
 *      controller device
 * @refcnt: Number of gets of this reset_control
 * @acquired: Only one reset_control may be acquired for a given rcdev and id.
 * @shared: Is this a shared (1), or an exclusive (0) reset_control?
 * @deassert_cnt: Number of times this reset line has been deasserted
 * @triggered_count: Number of times this reset line has been reset. Currently
 *                   only used for shared resets, which means that the value
 *                   will be either 0 or 1.
 */
struct reset_control {
	struct reset_controller_dev *rcdev;
	struct list_head list;
	unsigned int id;
	struct kref refcnt;
	bool acquired;
	bool shared;
	bool array;
	atomic_t deassert_count;
	atomic_t triggered_count;
};

/**
 * struct reset_control_array - an array of reset controls
 * @base: reset control for compatibility with reset control API functions
 * @num_rstcs: number of reset controls
 * @rstc: array of reset controls
 */
struct reset_control_array {
	struct reset_control base;
	unsigned int num_rstcs;
	struct reset_control *rstc[];
};

static const char *rcdev_name(struct reset_controller_dev *rcdev)
{
	if (rcdev->dev)
		return dev_name(rcdev->dev);

	if (rcdev->of_node)
		return rcdev->of_node->full_name;

	return NULL;
}

/**
 * of_reset_simple_xlate - translate reset_spec to the reset line number
 * @rcdev: a pointer to the reset controller device
 * @reset_spec: reset line specifier as found in the device tree
 *
 * This simple translation function should be used for reset controllers
 * with 1:1 mapping, where reset lines can be indexed by number without gaps.
 */
static int of_reset_simple_xlate(struct reset_controller_dev *rcdev,
			  const struct of_phandle_args *reset_spec)
{
	if (reset_spec->args[0] >= rcdev->nr_resets)
		return -EINVAL;

	return reset_spec->args[0];
}

/**
 * reset_controller_register - register a reset controller device
 * @rcdev: a pointer to the initialized reset controller device
 */
int reset_controller_register(struct reset_controller_dev *rcdev)
{
	if (!rcdev->of_xlate) {
		rcdev->of_reset_n_cells = 1;
		rcdev->of_xlate = of_reset_simple_xlate;
	}

	INIT_LIST_HEAD(&rcdev->reset_control_head);

	mutex_lock(&reset_list_mutex);
	list_add(&rcdev->list, &reset_controller_list);
	mutex_unlock(&reset_list_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(reset_controller_register);

/**
 * reset_controller_unregister - unregister a reset controller device
 * @rcdev: a pointer to the reset controller device
 */
void reset_controller_unregister(struct reset_controller_dev *rcdev)
{
	mutex_lock(&reset_list_mutex);
	list_del(&rcdev->list);
	mutex_unlock(&reset_list_mutex);
}
EXPORT_SYMBOL_GPL(reset_controller_unregister);

static void devm_reset_controller_release(struct device *dev, void *res)
{
	reset_controller_unregister(*(struct reset_controller_dev **)res);
}

/**
 * devm_reset_controller_register - resource managed reset_controller_register()
 * @dev: device that is registering this reset controller
 * @rcdev: a pointer to the initialized reset controller device
 *
 * Managed reset_controller_register(). For reset controllers registered by
 * this function, reset_controller_unregister() is automatically called on
 * driver detach. See reset_controller_register() for more information.
 */
int devm_reset_controller_register(struct device *dev,
				   struct reset_controller_dev *rcdev)
{
	struct reset_controller_dev **rcdevp;
	int ret;

	rcdevp = devres_alloc(devm_reset_controller_release, sizeof(*rcdevp),
			      GFP_KERNEL);
	if (!rcdevp)
		return -ENOMEM;

	ret = reset_controller_register(rcdev);
	if (!ret) {
		*rcdevp = rcdev;
		devres_add(dev, rcdevp);
	} else {
		devres_free(rcdevp);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_reset_controller_register);

/**
 * reset_controller_add_lookup - register a set of lookup entries
 * @lookup: array of reset lookup entries
 * @num_entries: number of entries in the lookup array
 */
void reset_controller_add_lookup(struct reset_control_lookup *lookup,
				 unsigned int num_entries)
{
	struct reset_control_lookup *entry;
	unsigned int i;

	mutex_lock(&reset_lookup_mutex);
	for (i = 0; i < num_entries; i++) {
		entry = &lookup[i];

		if (!entry->dev_id || !entry->provider) {
			pr_warn("%s(): reset lookup entry badly specified, skipping\n",
				__func__);
			continue;
		}

		list_add_tail(&entry->list, &reset_lookup_list);
	}
	mutex_unlock(&reset_lookup_mutex);
}
EXPORT_SYMBOL_GPL(reset_controller_add_lookup);

static inline struct reset_control_array *
rstc_to_array(struct reset_control *rstc) {
	return container_of(rstc, struct reset_control_array, base);
}

static int reset_control_array_reset(struct reset_control_array *resets)
{
	int ret, i;

	for (i = 0; i < resets->num_rstcs; i++) {
		ret = reset_control_reset(resets->rstc[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int reset_control_array_assert(struct reset_control_array *resets)
{
	int ret, i;

	for (i = 0; i < resets->num_rstcs; i++) {
		ret = reset_control_assert(resets->rstc[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	while (i--)
		reset_control_deassert(resets->rstc[i]);
	return ret;
}

static int reset_control_array_deassert(struct reset_control_array *resets)
{
	int ret, i;

	for (i = 0; i < resets->num_rstcs; i++) {
		ret = reset_control_deassert(resets->rstc[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	while (i--)
		reset_control_assert(resets->rstc[i]);
	return ret;
}

static int reset_control_array_acquire(struct reset_control_array *resets)
{
	unsigned int i;
	int err;

	for (i = 0; i < resets->num_rstcs; i++) {
		err = reset_control_acquire(resets->rstc[i]);
		if (err < 0)
			goto release;
	}

	return 0;

release:
	while (i--)
		reset_control_release(resets->rstc[i]);

	return err;
}

static void reset_control_array_release(struct reset_control_array *resets)
{
	unsigned int i;

	for (i = 0; i < resets->num_rstcs; i++)
		reset_control_release(resets->rstc[i]);
}

static inline bool reset_control_is_array(struct reset_control *rstc)
{
	return rstc->array;
}

/**
 * reset_control_reset - reset the controlled device
 * @rstc: reset controller
 *
 * On a shared reset line the actual reset pulse is only triggered once for the
 * lifetime of the reset_control instance: for all but the first caller this is
 * a no-op.
 * Consumers must not use reset_control_(de)assert on shared reset lines when
 * reset_control_reset has been used.
 *
 * If rstc is NULL it is an optional reset and the function will just
 * return 0.
 */
int reset_control_reset(struct reset_control *rstc)
{
	int ret;

	if (!rstc)
		return 0;

	if (WARN_ON(IS_ERR(rstc)))
		return -EINVAL;

	if (reset_control_is_array(rstc))
		return reset_control_array_reset(rstc_to_array(rstc));

	if (!rstc->rcdev->ops->reset)
		return -ENOTSUPP;

	if (rstc->shared) {
		if (WARN_ON(atomic_read(&rstc->deassert_count) != 0))
			return -EINVAL;

		if (atomic_inc_return(&rstc->triggered_count) != 1)
			return 0;
	} else {
		if (!rstc->acquired)
			return -EPERM;
	}

	ret = rstc->rcdev->ops->reset(rstc->rcdev, rstc->id);
	if (rstc->shared && ret)
		atomic_dec(&rstc->triggered_count);

	return ret;
}
EXPORT_SYMBOL_GPL(reset_control_reset);

/**
 * reset_control_assert - asserts the reset line
 * @rstc: reset controller
 *
 * Calling this on an exclusive reset controller guarantees that the reset
 * will be asserted. When called on a shared reset controller the line may
 * still be deasserted, as long as other users keep it so.
 *
 * For shared reset controls a driver cannot expect the hw's registers and
 * internal state to be reset, but must be prepared for this to happen.
 * Consumers must not use reset_control_reset on shared reset lines when
 * reset_control_(de)assert has been used.
 * return 0.
 *
 * If rstc is NULL it is an optional reset and the function will just
 * return 0.
 */
int reset_control_assert(struct reset_control *rstc)
{
	if (!rstc)
		return 0;

	if (WARN_ON(IS_ERR(rstc)))
		return -EINVAL;

	if (reset_control_is_array(rstc))
		return reset_control_array_assert(rstc_to_array(rstc));

	if (rstc->shared) {
		if (WARN_ON(atomic_read(&rstc->triggered_count) != 0))
			return -EINVAL;

		if (WARN_ON(atomic_read(&rstc->deassert_count) == 0))
			return -EINVAL;

		if (atomic_dec_return(&rstc->deassert_count) != 0)
			return 0;

		/*
		 * Shared reset controls allow the reset line to be in any state
		 * after this call, so doing nothing is a valid option.
		 */
		if (!rstc->rcdev->ops->assert)
			return 0;
	} else {
		/*
		 * If the reset controller does not implement .assert(), there
		 * is no way to guarantee that the reset line is asserted after
		 * this call.
		 */
		if (!rstc->rcdev->ops->assert)
			return -ENOTSUPP;

		if (!rstc->acquired) {
			WARN(1, "reset %s (ID: %u) is not acquired\n",
			     rcdev_name(rstc->rcdev), rstc->id);
			return -EPERM;
		}
	}

	return rstc->rcdev->ops->assert(rstc->rcdev, rstc->id);
}
EXPORT_SYMBOL_GPL(reset_control_assert);

/**
 * reset_control_deassert - deasserts the reset line
 * @rstc: reset controller
 *
 * After calling this function, the reset is guaranteed to be deasserted.
 * Consumers must not use reset_control_reset on shared reset lines when
 * reset_control_(de)assert has been used.
 * return 0.
 *
 * If rstc is NULL it is an optional reset and the function will just
 * return 0.
 */
int reset_control_deassert(struct reset_control *rstc)
{
	if (!rstc)
		return 0;

	if (WARN_ON(IS_ERR(rstc)))
		return -EINVAL;

	if (reset_control_is_array(rstc))
		return reset_control_array_deassert(rstc_to_array(rstc));

	if (rstc->shared) {
		if (WARN_ON(atomic_read(&rstc->triggered_count) != 0))
			return -EINVAL;

		if (atomic_inc_return(&rstc->deassert_count) != 1)
			return 0;
	} else {
		if (!rstc->acquired) {
			WARN(1, "reset %s (ID: %u) is not acquired\n",
			     rcdev_name(rstc->rcdev), rstc->id);
			return -EPERM;
		}
	}

	/*
	 * If the reset controller does not implement .deassert(), we assume
	 * that it handles self-deasserting reset lines via .reset(). In that
	 * case, the reset lines are deasserted by default. If that is not the
	 * case, the reset controller driver should implement .deassert() and
	 * return -ENOTSUPP.
	 */
	if (!rstc->rcdev->ops->deassert)
		return 0;

	return rstc->rcdev->ops->deassert(rstc->rcdev, rstc->id);
}
EXPORT_SYMBOL_GPL(reset_control_deassert);

/**
 * reset_control_status - returns a negative errno if not supported, a
 * positive value if the reset line is asserted, or zero if the reset
 * line is not asserted or if the desc is NULL (optional reset).
 * @rstc: reset controller
 */
int reset_control_status(struct reset_control *rstc)
{
	if (!rstc)
		return 0;

	if (WARN_ON(IS_ERR(rstc)) || reset_control_is_array(rstc))
		return -EINVAL;

	if (rstc->rcdev->ops->status)
		return rstc->rcdev->ops->status(rstc->rcdev, rstc->id);

	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(reset_control_status);

/**
 * reset_control_acquire() - acquires a reset control for exclusive use
 * @rstc: reset control
 *
 * This is used to explicitly acquire a reset control for exclusive use. Note
 * that exclusive resets are requested as acquired by default. In order for a
 * second consumer to be able to control the reset, the first consumer has to
 * release it first. Typically the easiest way to achieve this is to call the
 * reset_control_get_exclusive_released() to obtain an instance of the reset
 * control. Such reset controls are not acquired by default.
 *
 * Consumers implementing shared access to an exclusive reset need to follow
 * a specific protocol in order to work together. Before consumers can change
 * a reset they must acquire exclusive access using reset_control_acquire().
 * After they are done operating the reset, they must release exclusive access
 * with a call to reset_control_release(). Consumers are not granted exclusive
 * access to the reset as long as another consumer hasn't released a reset.
 *
 * See also: reset_control_release()
 */
int reset_control_acquire(struct reset_control *rstc)
{
	struct reset_control *rc;

	if (!rstc)
		return 0;

	if (WARN_ON(IS_ERR(rstc)))
		return -EINVAL;

	if (reset_control_is_array(rstc))
		return reset_control_array_acquire(rstc_to_array(rstc));

	mutex_lock(&reset_list_mutex);

	if (rstc->acquired) {
		mutex_unlock(&reset_list_mutex);
		return 0;
	}

	list_for_each_entry(rc, &rstc->rcdev->reset_control_head, list) {
		if (rstc != rc && rstc->id == rc->id) {
			if (rc->acquired) {
				mutex_unlock(&reset_list_mutex);
				return -EBUSY;
			}
		}
	}

	rstc->acquired = true;

	mutex_unlock(&reset_list_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(reset_control_acquire);

/**
 * reset_control_release() - releases exclusive access to a reset control
 * @rstc: reset control
 *
 * Releases exclusive access right to a reset control previously obtained by a
 * call to reset_control_acquire(). Until a consumer calls this function, no
 * other consumers will be granted exclusive access.
 *
 * See also: reset_control_acquire()
 */
void reset_control_release(struct reset_control *rstc)
{
	if (!rstc || WARN_ON(IS_ERR(rstc)))
		return;

	if (reset_control_is_array(rstc))
		reset_control_array_release(rstc_to_array(rstc));
	else
		rstc->acquired = false;
}
EXPORT_SYMBOL_GPL(reset_control_release);

static struct reset_control *__reset_control_get_internal(
				struct reset_controller_dev *rcdev,
				unsigned int index, bool shared, bool acquired)
{
	struct reset_control *rstc;

	lockdep_assert_held(&reset_list_mutex);

	list_for_each_entry(rstc, &rcdev->reset_control_head, list) {
		if (rstc->id == index) {
			/*
			 * Allow creating a secondary exclusive reset_control
			 * that is initially not acquired for an already
			 * controlled reset line.
			 */
			if (!rstc->shared && !shared && !acquired)
				break;

			if (WARN_ON(!rstc->shared || !shared))
				return ERR_PTR(-EBUSY);

			kref_get(&rstc->refcnt);
			return rstc;
		}
	}

	rstc = kzalloc(sizeof(*rstc), GFP_KERNEL);
	if (!rstc)
		return ERR_PTR(-ENOMEM);

	try_module_get(rcdev->owner);

	rstc->rcdev = rcdev;
	list_add(&rstc->list, &rcdev->reset_control_head);
	rstc->id = index;
	kref_init(&rstc->refcnt);
	rstc->acquired = acquired;
	rstc->shared = shared;

	return rstc;
}

static void __reset_control_release(struct kref *kref)
{
	struct reset_control *rstc = container_of(kref, struct reset_control,
						  refcnt);

	lockdep_assert_held(&reset_list_mutex);

	module_put(rstc->rcdev->owner);

	list_del(&rstc->list);
	kfree(rstc);
}

static void __reset_control_put_internal(struct reset_control *rstc)
{
	lockdep_assert_held(&reset_list_mutex);

	kref_put(&rstc->refcnt, __reset_control_release);
}

struct reset_control *__of_reset_control_get(struct device_node *node,
				     const char *id, int index, bool shared,
				     bool optional, bool acquired)
{
	struct reset_control *rstc;
	struct reset_controller_dev *r, *rcdev;
	struct of_phandle_args args;
	int rstc_id;
	int ret;

	if (!node)
		return ERR_PTR(-EINVAL);

	if (id) {
		index = of_property_match_string(node,
						 "reset-names", id);
		if (index == -EILSEQ)
			return ERR_PTR(index);
		if (index < 0)
			return optional ? NULL : ERR_PTR(-ENOENT);
	}

	ret = of_parse_phandle_with_args(node, "resets", "#reset-cells",
					 index, &args);
	if (ret == -EINVAL)
		return ERR_PTR(ret);
	if (ret)
		return optional ? NULL : ERR_PTR(ret);

	mutex_lock(&reset_list_mutex);
	rcdev = NULL;
	list_for_each_entry(r, &reset_controller_list, list) {
		if (args.np == r->of_node) {
			rcdev = r;
			break;
		}
	}

	if (!rcdev) {
		rstc = ERR_PTR(-EPROBE_DEFER);
		goto out;
	}

	if (WARN_ON(args.args_count != rcdev->of_reset_n_cells)) {
		rstc = ERR_PTR(-EINVAL);
		goto out;
	}

	rstc_id = rcdev->of_xlate(rcdev, &args);
	if (rstc_id < 0) {
		rstc = ERR_PTR(rstc_id);
		goto out;
	}

	/* reset_list_mutex also protects the rcdev's reset_control list */
	rstc = __reset_control_get_internal(rcdev, rstc_id, shared, acquired);

out:
	mutex_unlock(&reset_list_mutex);
	of_node_put(args.np);

	return rstc;
}
EXPORT_SYMBOL_GPL(__of_reset_control_get);

static struct reset_controller_dev *
__reset_controller_by_name(const char *name)
{
	struct reset_controller_dev *rcdev;

	lockdep_assert_held(&reset_list_mutex);

	list_for_each_entry(rcdev, &reset_controller_list, list) {
		if (!rcdev->dev)
			continue;

		if (!strcmp(name, dev_name(rcdev->dev)))
			return rcdev;
	}

	return NULL;
}

static struct reset_control *
__reset_control_get_from_lookup(struct device *dev, const char *con_id,
				bool shared, bool optional, bool acquired)
{
	const struct reset_control_lookup *lookup;
	struct reset_controller_dev *rcdev;
	const char *dev_id = dev_name(dev);
	struct reset_control *rstc = NULL;

	mutex_lock(&reset_lookup_mutex);

	list_for_each_entry(lookup, &reset_lookup_list, list) {
		if (strcmp(lookup->dev_id, dev_id))
			continue;

		if ((!con_id && !lookup->con_id) ||
		    ((con_id && lookup->con_id) &&
		     !strcmp(con_id, lookup->con_id))) {
			mutex_lock(&reset_list_mutex);
			rcdev = __reset_controller_by_name(lookup->provider);
			if (!rcdev) {
				mutex_unlock(&reset_list_mutex);
				mutex_unlock(&reset_lookup_mutex);
				/* Reset provider may not be ready yet. */
				return ERR_PTR(-EPROBE_DEFER);
			}

			rstc = __reset_control_get_internal(rcdev,
							    lookup->index,
							    shared, acquired);
			mutex_unlock(&reset_list_mutex);
			break;
		}
	}

	mutex_unlock(&reset_lookup_mutex);

	if (!rstc)
		return optional ? NULL : ERR_PTR(-ENOENT);

	return rstc;
}

struct reset_control *__reset_control_get(struct device *dev, const char *id,
					  int index, bool shared, bool optional,
					  bool acquired)
{
	if (WARN_ON(shared && acquired))
		return ERR_PTR(-EINVAL);

	if (dev->of_node)
		return __of_reset_control_get(dev->of_node, id, index, shared,
					      optional, acquired);

	return __reset_control_get_from_lookup(dev, id, shared, optional,
					       acquired);
}
EXPORT_SYMBOL_GPL(__reset_control_get);

static void reset_control_array_put(struct reset_control_array *resets)
{
	int i;

	mutex_lock(&reset_list_mutex);
	for (i = 0; i < resets->num_rstcs; i++)
		__reset_control_put_internal(resets->rstc[i]);
	mutex_unlock(&reset_list_mutex);
	kfree(resets);
}

/**
 * reset_control_put - free the reset controller
 * @rstc: reset controller
 */
void reset_control_put(struct reset_control *rstc)
{
	if (IS_ERR_OR_NULL(rstc))
		return;

	if (reset_control_is_array(rstc)) {
		reset_control_array_put(rstc_to_array(rstc));
		return;
	}

	mutex_lock(&reset_list_mutex);
	__reset_control_put_internal(rstc);
	mutex_unlock(&reset_list_mutex);
}
EXPORT_SYMBOL_GPL(reset_control_put);

static void devm_reset_control_release(struct device *dev, void *res)
{
	reset_control_put(*(struct reset_control **)res);
}

struct reset_control *__devm_reset_control_get(struct device *dev,
				     const char *id, int index, bool shared,
				     bool optional, bool acquired)
{
	struct reset_control **ptr, *rstc;

	ptr = devres_alloc(devm_reset_control_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	rstc = __reset_control_get(dev, id, index, shared, optional, acquired);
	if (!IS_ERR(rstc)) {
		*ptr = rstc;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return rstc;
}
EXPORT_SYMBOL_GPL(__devm_reset_control_get);

/**
 * device_reset - find reset controller associated with the device
 *                and perform reset
 * @dev: device to be reset by the controller
 * @optional: whether it is optional to reset the device
 *
 * Convenience wrapper for __reset_control_get() and reset_control_reset().
 * This is useful for the common case of devices with single, dedicated reset
 * lines.
 */
int __device_reset(struct device *dev, bool optional)
{
	struct reset_control *rstc;
	int ret;

	rstc = __reset_control_get(dev, NULL, 0, 0, optional, true);
	if (IS_ERR(rstc))
		return PTR_ERR(rstc);

	ret = reset_control_reset(rstc);

	reset_control_put(rstc);

	return ret;
}
EXPORT_SYMBOL_GPL(__device_reset);

/*
 * APIs to manage an array of reset controls.
 */

/**
 * of_reset_control_get_count - Count number of resets available with a device
 *
 * @node: device node that contains 'resets'.
 *
 * Returns positive reset count on success, or error number on failure and
 * on count being zero.
 */
static int of_reset_control_get_count(struct device_node *node)
{
	int count;

	if (!node)
		return -EINVAL;

	count = of_count_phandle_with_args(node, "resets", "#reset-cells");
	if (count == 0)
		count = -ENOENT;

	return count;
}

/**
 * of_reset_control_array_get - Get a list of reset controls using
 *				device node.
 *
 * @np: device node for the device that requests the reset controls array
 * @shared: whether reset controls are shared or not
 * @optional: whether it is optional to get the reset controls
 * @acquired: only one reset control may be acquired for a given controller
 *            and ID
 *
 * Returns pointer to allocated reset_control_array on success or
 * error on failure
 */
struct reset_control *
of_reset_control_array_get(struct device_node *np, bool shared, bool optional,
			   bool acquired)
{
	struct reset_control_array *resets;
	struct reset_control *rstc;
	int num, i;

	num = of_reset_control_get_count(np);
	if (num < 0)
		return optional ? NULL : ERR_PTR(num);

	resets = kzalloc(struct_size(resets, rstc, num), GFP_KERNEL);
	if (!resets)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < num; i++) {
		rstc = __of_reset_control_get(np, NULL, i, shared, optional,
					      acquired);
		if (IS_ERR(rstc))
			goto err_rst;
		resets->rstc[i] = rstc;
	}
	resets->num_rstcs = num;
	resets->base.array = true;

	return &resets->base;

err_rst:
	mutex_lock(&reset_list_mutex);
	while (--i >= 0)
		__reset_control_put_internal(resets->rstc[i]);
	mutex_unlock(&reset_list_mutex);

	kfree(resets);

	return rstc;
}
EXPORT_SYMBOL_GPL(of_reset_control_array_get);

/**
 * devm_reset_control_array_get - Resource managed reset control array get
 *
 * @dev: device that requests the list of reset controls
 * @shared: whether reset controls are shared or not
 * @optional: whether it is optional to get the reset controls
 *
 * The reset control array APIs are intended for a list of resets
 * that just have to be asserted or deasserted, without any
 * requirements on the order.
 *
 * Returns pointer to allocated reset_control_array on success or
 * error on failure
 */
struct reset_control *
devm_reset_control_array_get(struct device *dev, bool shared, bool optional)
{
	struct reset_control **devres;
	struct reset_control *rstc;

	devres = devres_alloc(devm_reset_control_release, sizeof(*devres),
			      GFP_KERNEL);
	if (!devres)
		return ERR_PTR(-ENOMEM);

	rstc = of_reset_control_array_get(dev->of_node, shared, optional, true);
	if (IS_ERR(rstc)) {
		devres_free(devres);
		return rstc;
	}

	*devres = rstc;
	devres_add(dev, devres);

	return rstc;
}
EXPORT_SYMBOL_GPL(devm_reset_control_array_get);

static int reset_control_get_count_from_lookup(struct device *dev)
{
	const struct reset_control_lookup *lookup;
	const char *dev_id;
	int count = 0;

	if (!dev)
		return -EINVAL;

	dev_id = dev_name(dev);
	mutex_lock(&reset_lookup_mutex);

	list_for_each_entry(lookup, &reset_lookup_list, list) {
		if (!strcmp(lookup->dev_id, dev_id))
			count++;
	}

	mutex_unlock(&reset_lookup_mutex);

	if (count == 0)
		count = -ENOENT;

	return count;
}

/**
 * reset_control_get_count - Count number of resets available with a device
 *
 * @dev: device for which to return the number of resets
 *
 * Returns positive reset count on success, or error number on failure and
 * on count being zero.
 */
int reset_control_get_count(struct device *dev)
{
	if (dev->of_node)
		return of_reset_control_get_count(dev->of_node);

	return reset_control_get_count_from_lookup(dev);
}
EXPORT_SYMBOL_GPL(reset_control_get_count);
