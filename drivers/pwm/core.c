/*
 * Generic pwmlib implementation
 *
 * Copyright (C) 2011 Sascha Hauer <s.hauer@pengutronix.de>
 * Copyright (C) 2011-2012 Avionic Design GmbH
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/pwm.h>
#include <linux/radix-tree.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <dt-bindings/pwm/pwm.h>

#define MAX_PWMS 1024

static DEFINE_MUTEX(pwm_lookup_lock);
static LIST_HEAD(pwm_lookup_list);
static DEFINE_MUTEX(pwm_lock);
static LIST_HEAD(pwm_chips);
static DECLARE_BITMAP(allocated_pwms, MAX_PWMS);
static RADIX_TREE(pwm_tree, GFP_KERNEL);

static struct pwm_device *pwm_to_device(unsigned int pwm)
{
	return radix_tree_lookup(&pwm_tree, pwm);
}

static int alloc_pwms(int pwm, unsigned int count)
{
	unsigned int from = 0;
	unsigned int start;

	if (pwm >= MAX_PWMS)
		return -EINVAL;

	if (pwm >= 0)
		from = pwm;

	start = bitmap_find_next_zero_area(allocated_pwms, MAX_PWMS, from,
					   count, 0);

	if (pwm >= 0 && start != pwm)
		return -EEXIST;

	if (start + count > MAX_PWMS)
		return -ENOSPC;

	return start;
}

static void free_pwms(struct pwm_chip *chip)
{
	unsigned int i;

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];
		radix_tree_delete(&pwm_tree, pwm->pwm);
	}

	bitmap_clear(allocated_pwms, chip->base, chip->npwm);

	kfree(chip->pwms);
	chip->pwms = NULL;
}

static struct pwm_chip *pwmchip_find_by_name(const char *name)
{
	struct pwm_chip *chip;

	if (!name)
		return NULL;

	mutex_lock(&pwm_lock);

	list_for_each_entry(chip, &pwm_chips, list) {
		const char *chip_name = dev_name(chip->dev);

		if (chip_name && strcmp(chip_name, name) == 0) {
			mutex_unlock(&pwm_lock);
			return chip;
		}
	}

	mutex_unlock(&pwm_lock);

	return NULL;
}

static int pwm_device_request(struct pwm_device *pwm, const char *label)
{
	int err;

	if (test_bit(PWMF_REQUESTED, &pwm->flags))
		return -EBUSY;

	if (!try_module_get(pwm->chip->ops->owner))
		return -ENODEV;

	if (pwm->chip->ops->request) {
		err = pwm->chip->ops->request(pwm->chip, pwm);
		if (err) {
			module_put(pwm->chip->ops->owner);
			return err;
		}
	}

	set_bit(PWMF_REQUESTED, &pwm->flags);
	pwm->label = label;

	return 0;
}

struct pwm_device *
of_pwm_xlate_with_flags(struct pwm_chip *pc, const struct of_phandle_args *args)
{
	struct pwm_device *pwm;

	if (pc->of_pwm_n_cells < 3)
		return ERR_PTR(-EINVAL);

	if (args->args[0] >= pc->npwm)
		return ERR_PTR(-EINVAL);

	pwm = pwm_request_from_chip(pc, args->args[0], NULL);
	if (IS_ERR(pwm))
		return pwm;

	pwm->args.period = args->args[1];

	if (args->args[2] & PWM_POLARITY_INVERTED)
		pwm->args.polarity = PWM_POLARITY_INVERSED;
	else
		pwm->args.polarity = PWM_POLARITY_NORMAL;

	return pwm;
}
EXPORT_SYMBOL_GPL(of_pwm_xlate_with_flags);

static struct pwm_device *
of_pwm_simple_xlate(struct pwm_chip *pc, const struct of_phandle_args *args)
{
	struct pwm_device *pwm;

	if (pc->of_pwm_n_cells < 2)
		return ERR_PTR(-EINVAL);

	if (args->args[0] >= pc->npwm)
		return ERR_PTR(-EINVAL);

	pwm = pwm_request_from_chip(pc, args->args[0], NULL);
	if (IS_ERR(pwm))
		return pwm;

	pwm->args.period = args->args[1];

	return pwm;
}

static void of_pwmchip_add(struct pwm_chip *chip)
{
	if (!chip->dev || !chip->dev->of_node)
		return;

	if (!chip->of_xlate) {
		chip->of_xlate = of_pwm_simple_xlate;
		chip->of_pwm_n_cells = 2;
	}

	of_node_get(chip->dev->of_node);
}

static void of_pwmchip_remove(struct pwm_chip *chip)
{
	if (chip->dev)
		of_node_put(chip->dev->of_node);
}

/**
 * pwm_set_chip_data() - set private chip data for a PWM
 * @pwm: PWM device
 * @data: pointer to chip-specific data
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int pwm_set_chip_data(struct pwm_device *pwm, void *data)
{
	if (!pwm)
		return -EINVAL;

	pwm->chip_data = data;

	return 0;
}
EXPORT_SYMBOL_GPL(pwm_set_chip_data);

/**
 * pwm_get_chip_data() - get private chip data for a PWM
 * @pwm: PWM device
 *
 * Returns: A pointer to the chip-private data for the PWM device.
 */
void *pwm_get_chip_data(struct pwm_device *pwm)
{
	return pwm ? pwm->chip_data : NULL;
}
EXPORT_SYMBOL_GPL(pwm_get_chip_data);

/**
 * pwmchip_add_with_polarity() - register a new PWM chip
 * @chip: the PWM chip to add
 * @polarity: initial polarity of PWM channels
 *
 * Register a new PWM chip. If chip->base < 0 then a dynamically assigned base
 * will be used. The initial polarity for all channels is specified by the
 * @polarity parameter.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int pwmchip_add_with_polarity(struct pwm_chip *chip,
			      enum pwm_polarity polarity)
{
	struct pwm_device *pwm;
	unsigned int i;
	int ret;

	if (!chip || !chip->dev || !chip->ops || !chip->ops->config ||
	    !chip->ops->enable || !chip->ops->disable || !chip->npwm)
		return -EINVAL;

	mutex_lock(&pwm_lock);

	ret = alloc_pwms(chip->base, chip->npwm);
	if (ret < 0)
		goto out;

	chip->pwms = kzalloc(chip->npwm * sizeof(*pwm), GFP_KERNEL);
	if (!chip->pwms) {
		ret = -ENOMEM;
		goto out;
	}

	chip->base = ret;

	for (i = 0; i < chip->npwm; i++) {
		pwm = &chip->pwms[i];

		pwm->chip = chip;
		pwm->pwm = chip->base + i;
		pwm->hwpwm = i;
		pwm->polarity = polarity;

		radix_tree_insert(&pwm_tree, pwm->pwm, pwm);
	}

	bitmap_set(allocated_pwms, chip->base, chip->npwm);

	INIT_LIST_HEAD(&chip->list);
	list_add(&chip->list, &pwm_chips);

	ret = 0;

	if (IS_ENABLED(CONFIG_OF))
		of_pwmchip_add(chip);

	pwmchip_sysfs_export(chip);

out:
	mutex_unlock(&pwm_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(pwmchip_add_with_polarity);

/**
 * pwmchip_add() - register a new PWM chip
 * @chip: the PWM chip to add
 *
 * Register a new PWM chip. If chip->base < 0 then a dynamically assigned base
 * will be used. The initial polarity for all channels is normal.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int pwmchip_add(struct pwm_chip *chip)
{
	return pwmchip_add_with_polarity(chip, PWM_POLARITY_NORMAL);
}
EXPORT_SYMBOL_GPL(pwmchip_add);

/**
 * pwmchip_remove() - remove a PWM chip
 * @chip: the PWM chip to remove
 *
 * Removes a PWM chip. This function may return busy if the PWM chip provides
 * a PWM device that is still requested.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int pwmchip_remove(struct pwm_chip *chip)
{
	unsigned int i;
	int ret = 0;

	mutex_lock(&pwm_lock);

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];

		if (test_bit(PWMF_REQUESTED, &pwm->flags)) {
			ret = -EBUSY;
			goto out;
		}
	}

	list_del_init(&chip->list);

	if (IS_ENABLED(CONFIG_OF))
		of_pwmchip_remove(chip);

	free_pwms(chip);

	pwmchip_sysfs_unexport(chip);

out:
	mutex_unlock(&pwm_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(pwmchip_remove);

/**
 * pwm_request() - request a PWM device
 * @pwm: global PWM device index
 * @label: PWM device label
 *
 * This function is deprecated, use pwm_get() instead.
 *
 * Returns: A pointer to a PWM device or an ERR_PTR()-encoded error code on
 * failure.
 */
struct pwm_device *pwm_request(int pwm, const char *label)
{
	struct pwm_device *dev;
	int err;

	if (pwm < 0 || pwm >= MAX_PWMS)
		return ERR_PTR(-EINVAL);

	mutex_lock(&pwm_lock);

	dev = pwm_to_device(pwm);
	if (!dev) {
		dev = ERR_PTR(-EPROBE_DEFER);
		goto out;
	}

	err = pwm_device_request(dev, label);
	if (err < 0)
		dev = ERR_PTR(err);

out:
	mutex_unlock(&pwm_lock);

	return dev;
}
EXPORT_SYMBOL_GPL(pwm_request);

/**
 * pwm_request_from_chip() - request a PWM device relative to a PWM chip
 * @chip: PWM chip
 * @index: per-chip index of the PWM to request
 * @label: a literal description string of this PWM
 *
 * Returns: A pointer to the PWM device at the given index of the given PWM
 * chip. A negative error code is returned if the index is not valid for the
 * specified PWM chip or if the PWM device cannot be requested.
 */
struct pwm_device *pwm_request_from_chip(struct pwm_chip *chip,
					 unsigned int index,
					 const char *label)
{
	struct pwm_device *pwm;
	int err;

	if (!chip || index >= chip->npwm)
		return ERR_PTR(-EINVAL);

	mutex_lock(&pwm_lock);
	pwm = &chip->pwms[index];

	err = pwm_device_request(pwm, label);
	if (err < 0)
		pwm = ERR_PTR(err);

	mutex_unlock(&pwm_lock);
	return pwm;
}
EXPORT_SYMBOL_GPL(pwm_request_from_chip);

/**
 * pwm_free() - free a PWM device
 * @pwm: PWM device
 *
 * This function is deprecated, use pwm_put() instead.
 */
void pwm_free(struct pwm_device *pwm)
{
	pwm_put(pwm);
}
EXPORT_SYMBOL_GPL(pwm_free);

/**
 * pwm_config() - change a PWM device configuration
 * @pwm: PWM device
 * @duty_ns: "on" time (in nanoseconds)
 * @period_ns: duration (in nanoseconds) of one cycle
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	int err;

	if (!pwm || duty_ns < 0 || period_ns <= 0 || duty_ns > period_ns)
		return -EINVAL;

	err = pwm->chip->ops->config(pwm->chip, pwm, duty_ns, period_ns);
	if (err)
		return err;

	pwm->duty_cycle = duty_ns;
	pwm->period = period_ns;

	return 0;
}
EXPORT_SYMBOL_GPL(pwm_config);

/**
 * pwm_set_polarity() - configure the polarity of a PWM signal
 * @pwm: PWM device
 * @polarity: new polarity of the PWM signal
 *
 * Note that the polarity cannot be configured while the PWM device is
 * enabled.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int pwm_set_polarity(struct pwm_device *pwm, enum pwm_polarity polarity)
{
	int err;

	if (!pwm || !pwm->chip->ops)
		return -EINVAL;

	if (!pwm->chip->ops->set_polarity)
		return -ENOSYS;

	if (pwm_is_enabled(pwm))
		return -EBUSY;

	err = pwm->chip->ops->set_polarity(pwm->chip, pwm, polarity);
	if (err)
		return err;

	pwm->polarity = polarity;

	return 0;
}
EXPORT_SYMBOL_GPL(pwm_set_polarity);

/**
 * pwm_enable() - start a PWM output toggling
 * @pwm: PWM device
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int pwm_enable(struct pwm_device *pwm)
{
	int err = 0;

	if (!pwm)
		return -EINVAL;

	if (!test_and_set_bit(PWMF_ENABLED, &pwm->flags)) {
		err = pwm->chip->ops->enable(pwm->chip, pwm);
		if (err)
			clear_bit(PWMF_ENABLED, &pwm->flags);
	}

	return err;
}
EXPORT_SYMBOL_GPL(pwm_enable);

/**
 * pwm_disable() - stop a PWM output toggling
 * @pwm: PWM device
 */
void pwm_disable(struct pwm_device *pwm)
{
	if (pwm && test_and_clear_bit(PWMF_ENABLED, &pwm->flags))
		pwm->chip->ops->disable(pwm->chip, pwm);
}
EXPORT_SYMBOL_GPL(pwm_disable);

static struct pwm_chip *of_node_to_pwmchip(struct device_node *np)
{
	struct pwm_chip *chip;

	mutex_lock(&pwm_lock);

	list_for_each_entry(chip, &pwm_chips, list)
		if (chip->dev && chip->dev->of_node == np) {
			mutex_unlock(&pwm_lock);
			return chip;
		}

	mutex_unlock(&pwm_lock);

	return ERR_PTR(-EPROBE_DEFER);
}

/**
 * of_pwm_get() - request a PWM via the PWM framework
 * @np: device node to get the PWM from
 * @con_id: consumer name
 *
 * Returns the PWM device parsed from the phandle and index specified in the
 * "pwms" property of a device tree node or a negative error-code on failure.
 * Values parsed from the device tree are stored in the returned PWM device
 * object.
 *
 * If con_id is NULL, the first PWM device listed in the "pwms" property will
 * be requested. Otherwise the "pwm-names" property is used to do a reverse
 * lookup of the PWM index. This also means that the "pwm-names" property
 * becomes mandatory for devices that look up the PWM device via the con_id
 * parameter.
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
struct pwm_device *of_pwm_get(struct device_node *np, const char *con_id)
{
	struct pwm_device *pwm = NULL;
	struct of_phandle_args args;
	struct pwm_chip *pc;
	int index = 0;
	int err;

	if (con_id) {
		index = of_property_match_string(np, "pwm-names", con_id);
		if (index < 0)
			return ERR_PTR(index);
	}

	err = of_parse_phandle_with_args(np, "pwms", "#pwm-cells", index,
					 &args);
	if (err) {
		pr_debug("%s(): can't parse \"pwms\" property\n", __func__);
		return ERR_PTR(err);
	}

	pc = of_node_to_pwmchip(args.np);
	if (IS_ERR(pc)) {
		pr_debug("%s(): PWM chip not found\n", __func__);
		pwm = ERR_CAST(pc);
		goto put;
	}

	if (args.args_count != pc->of_pwm_n_cells) {
		pr_debug("%s: wrong #pwm-cells for %s\n", np->full_name,
			 args.np->full_name);
		pwm = ERR_PTR(-EINVAL);
		goto put;
	}

	pwm = pc->of_xlate(pc, &args);
	if (IS_ERR(pwm))
		goto put;

	/*
	 * If a consumer name was not given, try to look it up from the
	 * "pwm-names" property if it exists. Otherwise use the name of
	 * the user device node.
	 */
	if (!con_id) {
		err = of_property_read_string_index(np, "pwm-names", index,
						    &con_id);
		if (err < 0)
			con_id = np->name;
	}

	pwm->label = con_id;

	/*
	 * FIXME: This should be removed once all PWM users properly make use
	 * of struct pwm_args to initialize the PWM device. As long as this is
	 * here, the PWM state and hardware state can get out of sync.
	 */
	pwm_apply_args(pwm);

put:
	of_node_put(args.np);

	return pwm;
}
EXPORT_SYMBOL_GPL(of_pwm_get);

/**
 * pwm_add_table() - register PWM device consumers
 * @table: array of consumers to register
 * @num: number of consumers in table
 */
void pwm_add_table(struct pwm_lookup *table, size_t num)
{
	mutex_lock(&pwm_lookup_lock);

	while (num--) {
		list_add_tail(&table->list, &pwm_lookup_list);
		table++;
	}

	mutex_unlock(&pwm_lookup_lock);
}

/**
 * pwm_remove_table() - unregister PWM device consumers
 * @table: array of consumers to unregister
 * @num: number of consumers in table
 */
void pwm_remove_table(struct pwm_lookup *table, size_t num)
{
	mutex_lock(&pwm_lookup_lock);

	while (num--) {
		list_del(&table->list);
		table++;
	}

	mutex_unlock(&pwm_lookup_lock);
}

/**
 * pwm_get() - look up and request a PWM device
 * @dev: device for PWM consumer
 * @con_id: consumer name
 *
 * Lookup is first attempted using DT. If the device was not instantiated from
 * a device tree, a PWM chip and a relative index is looked up via a table
 * supplied by board setup code (see pwm_add_table()).
 *
 * Once a PWM chip has been found the specified PWM device will be requested
 * and is ready to be used.
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
struct pwm_device *pwm_get(struct device *dev, const char *con_id)
{
	struct pwm_device *pwm = ERR_PTR(-EPROBE_DEFER);
	const char *dev_id = dev ? dev_name(dev) : NULL;
	struct pwm_chip *chip = NULL;
	unsigned int best = 0;
	struct pwm_lookup *p, *chosen = NULL;
	unsigned int match;

	/* look up via DT first */
	if (IS_ENABLED(CONFIG_OF) && dev && dev->of_node)
		return of_pwm_get(dev->of_node, con_id);

	/*
	 * We look up the provider in the static table typically provided by
	 * board setup code. We first try to lookup the consumer device by
	 * name. If the consumer device was passed in as NULL or if no match
	 * was found, we try to find the consumer by directly looking it up
	 * by name.
	 *
	 * If a match is found, the provider PWM chip is looked up by name
	 * and a PWM device is requested using the PWM device per-chip index.
	 *
	 * The lookup algorithm was shamelessly taken from the clock
	 * framework:
	 *
	 * We do slightly fuzzy matching here:
	 *  An entry with a NULL ID is assumed to be a wildcard.
	 *  If an entry has a device ID, it must match
	 *  If an entry has a connection ID, it must match
	 * Then we take the most specific entry - with the following order
	 * of precedence: dev+con > dev only > con only.
	 */
	mutex_lock(&pwm_lookup_lock);

	list_for_each_entry(p, &pwm_lookup_list, list) {
		match = 0;

		if (p->dev_id) {
			if (!dev_id || strcmp(p->dev_id, dev_id))
				continue;

			match += 2;
		}

		if (p->con_id) {
			if (!con_id || strcmp(p->con_id, con_id))
				continue;

			match += 1;
		}

		if (match > best) {
			chosen = p;

			if (match != 3)
				best = match;
			else
				break;
		}
	}

	if (!chosen) {
		pwm = ERR_PTR(-ENODEV);
		goto out;
	}

	chip = pwmchip_find_by_name(chosen->provider);
	if (!chip)
		goto out;

	pwm = pwm_request_from_chip(chip, chosen->index, con_id ?: dev_id);
	if (IS_ERR(pwm))
		goto out;

	pwm->args.period = chosen->period;
	pwm->args.polarity = chosen->polarity;

	/*
	 * FIXME: This should be removed once all PWM users properly make use
	 * of struct pwm_args to initialize the PWM device. As long as this is
	 * here, the PWM state and hardware state can get out of sync.
	 */
	pwm_apply_args(pwm);

out:
	mutex_unlock(&pwm_lookup_lock);
	return pwm;
}
EXPORT_SYMBOL_GPL(pwm_get);

/**
 * pwm_put() - release a PWM device
 * @pwm: PWM device
 */
void pwm_put(struct pwm_device *pwm)
{
	if (!pwm)
		return;

	mutex_lock(&pwm_lock);

	if (!test_and_clear_bit(PWMF_REQUESTED, &pwm->flags)) {
		pr_warn("PWM device already freed\n");
		goto out;
	}

	if (pwm->chip->ops->free)
		pwm->chip->ops->free(pwm->chip, pwm);

	pwm->label = NULL;

	module_put(pwm->chip->ops->owner);
out:
	mutex_unlock(&pwm_lock);
}
EXPORT_SYMBOL_GPL(pwm_put);

static void devm_pwm_release(struct device *dev, void *res)
{
	pwm_put(*(struct pwm_device **)res);
}

/**
 * devm_pwm_get() - resource managed pwm_get()
 * @dev: device for PWM consumer
 * @con_id: consumer name
 *
 * This function performs like pwm_get() but the acquired PWM device will
 * automatically be released on driver detach.
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
struct pwm_device *devm_pwm_get(struct device *dev, const char *con_id)
{
	struct pwm_device **ptr, *pwm;

	ptr = devres_alloc(devm_pwm_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	pwm = pwm_get(dev, con_id);
	if (!IS_ERR(pwm)) {
		*ptr = pwm;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return pwm;
}
EXPORT_SYMBOL_GPL(devm_pwm_get);

/**
 * devm_of_pwm_get() - resource managed of_pwm_get()
 * @dev: device for PWM consumer
 * @np: device node to get the PWM from
 * @con_id: consumer name
 *
 * This function performs like of_pwm_get() but the acquired PWM device will
 * automatically be released on driver detach.
 *
 * Returns: A pointer to the requested PWM device or an ERR_PTR()-encoded
 * error code on failure.
 */
struct pwm_device *devm_of_pwm_get(struct device *dev, struct device_node *np,
				   const char *con_id)
{
	struct pwm_device **ptr, *pwm;

	ptr = devres_alloc(devm_pwm_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	pwm = of_pwm_get(np, con_id);
	if (!IS_ERR(pwm)) {
		*ptr = pwm;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return pwm;
}
EXPORT_SYMBOL_GPL(devm_of_pwm_get);

static int devm_pwm_match(struct device *dev, void *res, void *data)
{
	struct pwm_device **p = res;

	if (WARN_ON(!p || !*p))
		return 0;

	return *p == data;
}

/**
 * devm_pwm_put() - resource managed pwm_put()
 * @dev: device for PWM consumer
 * @pwm: PWM device
 *
 * Release a PWM previously allocated using devm_pwm_get(). Calling this
 * function is usually not needed because devm-allocated resources are
 * automatically released on driver detach.
 */
void devm_pwm_put(struct device *dev, struct pwm_device *pwm)
{
	WARN_ON(devres_release(dev, devm_pwm_release, devm_pwm_match, pwm));
}
EXPORT_SYMBOL_GPL(devm_pwm_put);

/**
  * pwm_can_sleep() - report whether PWM access will sleep
  * @pwm: PWM device
  *
  * Returns: True if accessing the PWM can sleep, false otherwise.
  */
bool pwm_can_sleep(struct pwm_device *pwm)
{
	return true;
}
EXPORT_SYMBOL_GPL(pwm_can_sleep);

#ifdef CONFIG_DEBUG_FS
static void pwm_dbg_show(struct pwm_chip *chip, struct seq_file *s)
{
	unsigned int i;

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];

		seq_printf(s, " pwm-%-3d (%-20.20s):", i, pwm->label);

		if (test_bit(PWMF_REQUESTED, &pwm->flags))
			seq_puts(s, " requested");

		if (pwm_is_enabled(pwm))
			seq_puts(s, " enabled");

		seq_puts(s, "\n");
	}
}

static void *pwm_seq_start(struct seq_file *s, loff_t *pos)
{
	mutex_lock(&pwm_lock);
	s->private = "";

	return seq_list_start(&pwm_chips, *pos);
}

static void *pwm_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	s->private = "\n";

	return seq_list_next(v, &pwm_chips, pos);
}

static void pwm_seq_stop(struct seq_file *s, void *v)
{
	mutex_unlock(&pwm_lock);
}

static int pwm_seq_show(struct seq_file *s, void *v)
{
	struct pwm_chip *chip = list_entry(v, struct pwm_chip, list);

	seq_printf(s, "%s%s/%s, %d PWM device%s\n", (char *)s->private,
		   chip->dev->bus ? chip->dev->bus->name : "no-bus",
		   dev_name(chip->dev), chip->npwm,
		   (chip->npwm != 1) ? "s" : "");

	if (chip->ops->dbg_show)
		chip->ops->dbg_show(chip, s);
	else
		pwm_dbg_show(chip, s);

	return 0;
}

static const struct seq_operations pwm_seq_ops = {
	.start = pwm_seq_start,
	.next = pwm_seq_next,
	.stop = pwm_seq_stop,
	.show = pwm_seq_show,
};

static int pwm_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &pwm_seq_ops);
}

static const struct file_operations pwm_debugfs_ops = {
	.owner = THIS_MODULE,
	.open = pwm_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int __init pwm_debugfs_init(void)
{
	debugfs_create_file("pwm", S_IFREG | S_IRUGO, NULL, NULL,
			    &pwm_debugfs_ops);

	return 0;
}
subsys_initcall(pwm_debugfs_init);
#endif /* CONFIG_DEBUG_FS */
