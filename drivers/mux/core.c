// SPDX-License-Identifier: GPL-2.0
/*
 * Multiplexer subsystem
 *
 * Copyright (C) 2017 Axentia Technologies AB
 *
 * Author: Peter Rosin <peda@axentia.se>
 */

#define pr_fmt(fmt) "mux-core: " fmt

#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mux/consumer.h>
#include <linux/mux/driver.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

/*
 * The idle-as-is "state" is not an actual state that may be selected, it
 * only implies that the state should not be changed. So, use that state
 * as indication that the cached state of the multiplexer is unknown.
 */
#define MUX_CACHE_UNKNOWN MUX_IDLE_AS_IS

static struct class mux_class = {
	.name = "mux",
	.owner = THIS_MODULE,
};

static DEFINE_IDA(mux_ida);

static int __init mux_init(void)
{
	ida_init(&mux_ida);
	return class_register(&mux_class);
}

static void __exit mux_exit(void)
{
	class_unregister(&mux_class);
	ida_destroy(&mux_ida);
}

static void mux_chip_release(struct device *dev)
{
	struct mux_chip *mux_chip = to_mux_chip(dev);

	ida_simple_remove(&mux_ida, mux_chip->id);
	kfree(mux_chip);
}

static const struct device_type mux_type = {
	.name = "mux-chip",
	.release = mux_chip_release,
};

/**
 * mux_chip_alloc() - Allocate a mux-chip.
 * @dev: The parent device implementing the mux interface.
 * @controllers: The number of mux controllers to allocate for this chip.
 * @sizeof_priv: Size of extra memory area for private use by the caller.
 *
 * After allocating the mux-chip with the desired number of mux controllers
 * but before registering the chip, the mux driver is required to configure
 * the number of valid mux states in the mux_chip->mux[N].states members and
 * the desired idle state in the returned mux_chip->mux[N].idle_state members.
 * The default idle state is MUX_IDLE_AS_IS. The mux driver also needs to
 * provide a pointer to the operations struct in the mux_chip->ops member
 * before registering the mux-chip with mux_chip_register.
 *
 * Return: A pointer to the new mux-chip, or an ERR_PTR with a negative errno.
 */
struct mux_chip *mux_chip_alloc(struct device *dev,
				unsigned int controllers, size_t sizeof_priv)
{
	struct mux_chip *mux_chip;
	int i;

	if (WARN_ON(!dev || !controllers))
		return ERR_PTR(-EINVAL);

	mux_chip = kzalloc(sizeof(*mux_chip) +
			   controllers * sizeof(*mux_chip->mux) +
			   sizeof_priv, GFP_KERNEL);
	if (!mux_chip)
		return ERR_PTR(-ENOMEM);

	mux_chip->mux = (struct mux_control *)(mux_chip + 1);
	mux_chip->dev.class = &mux_class;
	mux_chip->dev.type = &mux_type;
	mux_chip->dev.parent = dev;
	mux_chip->dev.of_node = dev->of_node;
	dev_set_drvdata(&mux_chip->dev, mux_chip);

	mux_chip->id = ida_simple_get(&mux_ida, 0, 0, GFP_KERNEL);
	if (mux_chip->id < 0) {
		int err = mux_chip->id;

		pr_err("muxchipX failed to get a device id\n");
		kfree(mux_chip);
		return ERR_PTR(err);
	}
	dev_set_name(&mux_chip->dev, "muxchip%d", mux_chip->id);

	mux_chip->controllers = controllers;
	for (i = 0; i < controllers; ++i) {
		struct mux_control *mux = &mux_chip->mux[i];

		mux->chip = mux_chip;
		sema_init(&mux->lock, 1);
		mux->cached_state = MUX_CACHE_UNKNOWN;
		mux->idle_state = MUX_IDLE_AS_IS;
	}

	device_initialize(&mux_chip->dev);

	return mux_chip;
}
EXPORT_SYMBOL_GPL(mux_chip_alloc);

static int mux_control_set(struct mux_control *mux, int state)
{
	int ret = mux->chip->ops->set(mux, state);

	mux->cached_state = ret < 0 ? MUX_CACHE_UNKNOWN : state;

	return ret;
}

/**
 * mux_chip_register() - Register a mux-chip, thus readying the controllers
 *			 for use.
 * @mux_chip: The mux-chip to register.
 *
 * Do not retry registration of the same mux-chip on failure. You should
 * instead put it away with mux_chip_free() and allocate a new one, if you
 * for some reason would like to retry registration.
 *
 * Return: Zero on success or a negative errno on error.
 */
int mux_chip_register(struct mux_chip *mux_chip)
{
	int i;
	int ret;

	for (i = 0; i < mux_chip->controllers; ++i) {
		struct mux_control *mux = &mux_chip->mux[i];

		if (mux->idle_state == mux->cached_state)
			continue;

		ret = mux_control_set(mux, mux->idle_state);
		if (ret < 0) {
			dev_err(&mux_chip->dev, "unable to set idle state\n");
			return ret;
		}
	}

	ret = device_add(&mux_chip->dev);
	if (ret < 0)
		dev_err(&mux_chip->dev,
			"device_add failed in %s: %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(mux_chip_register);

/**
 * mux_chip_unregister() - Take the mux-chip off-line.
 * @mux_chip: The mux-chip to unregister.
 *
 * mux_chip_unregister() reverses the effects of mux_chip_register().
 * But not completely, you should not try to call mux_chip_register()
 * on a mux-chip that has been registered before.
 */
void mux_chip_unregister(struct mux_chip *mux_chip)
{
	device_del(&mux_chip->dev);
}
EXPORT_SYMBOL_GPL(mux_chip_unregister);

/**
 * mux_chip_free() - Free the mux-chip for good.
 * @mux_chip: The mux-chip to free.
 *
 * mux_chip_free() reverses the effects of mux_chip_alloc().
 */
void mux_chip_free(struct mux_chip *mux_chip)
{
	if (!mux_chip)
		return;

	put_device(&mux_chip->dev);
}
EXPORT_SYMBOL_GPL(mux_chip_free);

static void devm_mux_chip_release(struct device *dev, void *res)
{
	struct mux_chip *mux_chip = *(struct mux_chip **)res;

	mux_chip_free(mux_chip);
}

/**
 * devm_mux_chip_alloc() - Resource-managed version of mux_chip_alloc().
 * @dev: The parent device implementing the mux interface.
 * @controllers: The number of mux controllers to allocate for this chip.
 * @sizeof_priv: Size of extra memory area for private use by the caller.
 *
 * See mux_chip_alloc() for more details.
 *
 * Return: A pointer to the new mux-chip, or an ERR_PTR with a negative errno.
 */
struct mux_chip *devm_mux_chip_alloc(struct device *dev,
				     unsigned int controllers,
				     size_t sizeof_priv)
{
	struct mux_chip **ptr, *mux_chip;

	ptr = devres_alloc(devm_mux_chip_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	mux_chip = mux_chip_alloc(dev, controllers, sizeof_priv);
	if (IS_ERR(mux_chip)) {
		devres_free(ptr);
		return mux_chip;
	}

	*ptr = mux_chip;
	devres_add(dev, ptr);

	return mux_chip;
}
EXPORT_SYMBOL_GPL(devm_mux_chip_alloc);

static void devm_mux_chip_reg_release(struct device *dev, void *res)
{
	struct mux_chip *mux_chip = *(struct mux_chip **)res;

	mux_chip_unregister(mux_chip);
}

/**
 * devm_mux_chip_register() - Resource-managed version mux_chip_register().
 * @dev: The parent device implementing the mux interface.
 * @mux_chip: The mux-chip to register.
 *
 * See mux_chip_register() for more details.
 *
 * Return: Zero on success or a negative errno on error.
 */
int devm_mux_chip_register(struct device *dev,
			   struct mux_chip *mux_chip)
{
	struct mux_chip **ptr;
	int res;

	ptr = devres_alloc(devm_mux_chip_reg_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	res = mux_chip_register(mux_chip);
	if (res) {
		devres_free(ptr);
		return res;
	}

	*ptr = mux_chip;
	devres_add(dev, ptr);

	return res;
}
EXPORT_SYMBOL_GPL(devm_mux_chip_register);

/**
 * mux_control_states() - Query the number of multiplexer states.
 * @mux: The mux-control to query.
 *
 * Return: The number of multiplexer states.
 */
unsigned int mux_control_states(struct mux_control *mux)
{
	return mux->states;
}
EXPORT_SYMBOL_GPL(mux_control_states);

/*
 * The mux->lock must be down when calling this function.
 */
static int __mux_control_select(struct mux_control *mux, int state)
{
	int ret;

	if (WARN_ON(state < 0 || state >= mux->states))
		return -EINVAL;

	if (mux->cached_state == state)
		return 0;

	ret = mux_control_set(mux, state);
	if (ret >= 0)
		return 0;

	/* The mux update failed, try to revert if appropriate... */
	if (mux->idle_state != MUX_IDLE_AS_IS)
		mux_control_set(mux, mux->idle_state);

	return ret;
}

/**
 * mux_control_select() - Select the given multiplexer state.
 * @mux: The mux-control to request a change of state from.
 * @state: The new requested state.
 *
 * On successfully selecting the mux-control state, it will be locked until
 * there is a call to mux_control_deselect(). If the mux-control is already
 * selected when mux_control_select() is called, the caller will be blocked
 * until mux_control_deselect() is called (by someone else).
 *
 * Therefore, make sure to call mux_control_deselect() when the operation is
 * complete and the mux-control is free for others to use, but do not call
 * mux_control_deselect() if mux_control_select() fails.
 *
 * Return: 0 when the mux-control state has the requested state or a negative
 * errno on error.
 */
int mux_control_select(struct mux_control *mux, unsigned int state)
{
	int ret;

	ret = down_killable(&mux->lock);
	if (ret < 0)
		return ret;

	ret = __mux_control_select(mux, state);

	if (ret < 0)
		up(&mux->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(mux_control_select);

/**
 * mux_control_try_select() - Try to select the given multiplexer state.
 * @mux: The mux-control to request a change of state from.
 * @state: The new requested state.
 *
 * On successfully selecting the mux-control state, it will be locked until
 * mux_control_deselect() called.
 *
 * Therefore, make sure to call mux_control_deselect() when the operation is
 * complete and the mux-control is free for others to use, but do not call
 * mux_control_deselect() if mux_control_try_select() fails.
 *
 * Return: 0 when the mux-control state has the requested state or a negative
 * errno on error. Specifically -EBUSY if the mux-control is contended.
 */
int mux_control_try_select(struct mux_control *mux, unsigned int state)
{
	int ret;

	if (down_trylock(&mux->lock))
		return -EBUSY;

	ret = __mux_control_select(mux, state);

	if (ret < 0)
		up(&mux->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(mux_control_try_select);

/**
 * mux_control_deselect() - Deselect the previously selected multiplexer state.
 * @mux: The mux-control to deselect.
 *
 * It is required that a single call is made to mux_control_deselect() for
 * each and every successful call made to either of mux_control_select() or
 * mux_control_try_select().
 *
 * Return: 0 on success and a negative errno on error. An error can only
 * occur if the mux has an idle state. Note that even if an error occurs, the
 * mux-control is unlocked and is thus free for the next access.
 */
int mux_control_deselect(struct mux_control *mux)
{
	int ret = 0;

	if (mux->idle_state != MUX_IDLE_AS_IS &&
	    mux->idle_state != mux->cached_state)
		ret = mux_control_set(mux, mux->idle_state);

	up(&mux->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(mux_control_deselect);

static int of_dev_node_match(struct device *dev, const void *data)
{
	return dev->of_node == data;
}

/* Note this function returns a reference to the mux_chip dev. */
static struct mux_chip *of_find_mux_chip_by_node(struct device_node *np)
{
	struct device *dev;

	dev = class_find_device(&mux_class, NULL, np, of_dev_node_match);

	return dev ? to_mux_chip(dev) : NULL;
}

/**
 * mux_control_get() - Get the mux-control for a device.
 * @dev: The device that needs a mux-control.
 * @mux_name: The name identifying the mux-control.
 *
 * Return: A pointer to the mux-control, or an ERR_PTR with a negative errno.
 */
struct mux_control *mux_control_get(struct device *dev, const char *mux_name)
{
	struct device_node *np = dev->of_node;
	struct of_phandle_args args;
	struct mux_chip *mux_chip;
	unsigned int controller;
	int index = 0;
	int ret;

	if (mux_name) {
		index = of_property_match_string(np, "mux-control-names",
						 mux_name);
		if (index < 0) {
			dev_err(dev, "mux controller '%s' not found\n",
				mux_name);
			return ERR_PTR(index);
		}
	}

	ret = of_parse_phandle_with_args(np,
					 "mux-controls", "#mux-control-cells",
					 index, &args);
	if (ret) {
		dev_err(dev, "%pOF: failed to get mux-control %s(%i)\n",
			np, mux_name ?: "", index);
		return ERR_PTR(ret);
	}

	mux_chip = of_find_mux_chip_by_node(args.np);
	of_node_put(args.np);
	if (!mux_chip)
		return ERR_PTR(-EPROBE_DEFER);

	if (args.args_count > 1 ||
	    (!args.args_count && (mux_chip->controllers > 1))) {
		dev_err(dev, "%pOF: wrong #mux-control-cells for %pOF\n",
			np, args.np);
		put_device(&mux_chip->dev);
		return ERR_PTR(-EINVAL);
	}

	controller = 0;
	if (args.args_count)
		controller = args.args[0];

	if (controller >= mux_chip->controllers) {
		dev_err(dev, "%pOF: bad mux controller %u specified in %pOF\n",
			np, controller, args.np);
		put_device(&mux_chip->dev);
		return ERR_PTR(-EINVAL);
	}

	return &mux_chip->mux[controller];
}
EXPORT_SYMBOL_GPL(mux_control_get);

/**
 * mux_control_put() - Put away the mux-control for good.
 * @mux: The mux-control to put away.
 *
 * mux_control_put() reverses the effects of mux_control_get().
 */
void mux_control_put(struct mux_control *mux)
{
	put_device(&mux->chip->dev);
}
EXPORT_SYMBOL_GPL(mux_control_put);

static void devm_mux_control_release(struct device *dev, void *res)
{
	struct mux_control *mux = *(struct mux_control **)res;

	mux_control_put(mux);
}

/**
 * devm_mux_control_get() - Get the mux-control for a device, with resource
 *			    management.
 * @dev: The device that needs a mux-control.
 * @mux_name: The name identifying the mux-control.
 *
 * Return: Pointer to the mux-control, or an ERR_PTR with a negative errno.
 */
struct mux_control *devm_mux_control_get(struct device *dev,
					 const char *mux_name)
{
	struct mux_control **ptr, *mux;

	ptr = devres_alloc(devm_mux_control_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	mux = mux_control_get(dev, mux_name);
	if (IS_ERR(mux)) {
		devres_free(ptr);
		return mux;
	}

	*ptr = mux;
	devres_add(dev, ptr);

	return mux;
}
EXPORT_SYMBOL_GPL(devm_mux_control_get);

/*
 * Using subsys_initcall instead of module_init here to try to ensure - for
 * the non-modular case - that the subsystem is initialized when mux consumers
 * and mux controllers start to use it.
 * For the modular case, the ordering is ensured with module dependencies.
 */
subsys_initcall(mux_init);
module_exit(mux_exit);

MODULE_DESCRIPTION("Multiplexer subsystem");
MODULE_AUTHOR("Peter Rosin <peda@axentia.se>");
MODULE_LICENSE("GPL v2");
