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

#define MAX_PWMS 1024

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

/**
 * pwm_set_chip_data() - set private chip data for a PWM
 * @pwm: PWM device
 * @data: pointer to chip-specific data
 */
int pwm_set_chip_data(struct pwm_device *pwm, void *data)
{
	if (!pwm)
		return -EINVAL;

	pwm->chip_data = data;

	return 0;
}

/**
 * pwm_get_chip_data() - get private chip data for a PWM
 * @pwm: PWM device
 */
void *pwm_get_chip_data(struct pwm_device *pwm)
{
	return pwm ? pwm->chip_data : NULL;
}

/**
 * pwmchip_add() - register a new PWM chip
 * @chip: the PWM chip to add
 *
 * Register a new PWM chip. If chip->base < 0 then a dynamically assigned base
 * will be used.
 */
int pwmchip_add(struct pwm_chip *chip)
{
	struct pwm_device *pwm;
	unsigned int i;
	int ret;

	if (!chip || !chip->dev || !chip->ops || !chip->ops->config ||
	    !chip->ops->enable || !chip->ops->disable)
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

		radix_tree_insert(&pwm_tree, pwm->pwm, pwm);
	}

	bitmap_set(allocated_pwms, chip->base, chip->npwm);

	INIT_LIST_HEAD(&chip->list);
	list_add(&chip->list, &pwm_chips);

	ret = 0;

out:
	mutex_unlock(&pwm_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(pwmchip_add);

/**
 * pwmchip_remove() - remove a PWM chip
 * @chip: the PWM chip to remove
 *
 * Removes a PWM chip. This function may return busy if the PWM chip provides
 * a PWM device that is still requested.
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
	free_pwms(chip);

out:
	mutex_unlock(&pwm_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(pwmchip_remove);

/**
 * pwm_request() - request a PWM device
 * @pwm_id: global PWM device index
 * @label: PWM device label
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
 * Returns the PWM at the given index of the given PWM chip. A negative error
 * code is returned if the index is not valid for the specified PWM chip or
 * if the PWM device cannot be requested.
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
 */
void pwm_free(struct pwm_device *pwm)
{
	mutex_lock(&pwm_lock);

	if (!test_and_clear_bit(PWMF_REQUESTED, &pwm->flags)) {
		pr_warning("PWM device already freed\n");
		goto out;
	}

	if (pwm->chip->ops->free)
		pwm->chip->ops->free(pwm->chip, pwm);

	pwm->label = NULL;

	module_put(pwm->chip->ops->owner);
out:
	mutex_unlock(&pwm_lock);
}
EXPORT_SYMBOL_GPL(pwm_free);

/**
 * pwm_config() - change a PWM device configuration
 * @pwm: PWM device
 * @duty_ns: "on" time (in nanoseconds)
 * @period_ns: duration (in nanoseconds) of one cycle
 */
int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
	if (!pwm || period_ns == 0 || duty_ns > period_ns)
		return -EINVAL;

	return pwm->chip->ops->config(pwm->chip, pwm, duty_ns, period_ns);
}
EXPORT_SYMBOL_GPL(pwm_config);

/**
 * pwm_enable() - start a PWM output toggling
 * @pwm: PWM device
 */
int pwm_enable(struct pwm_device *pwm)
{
	if (pwm && !test_and_set_bit(PWMF_ENABLED, &pwm->flags))
		return pwm->chip->ops->enable(pwm->chip, pwm);

	return pwm ? 0 : -EINVAL;
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

#ifdef CONFIG_DEBUG_FS
static void pwm_dbg_show(struct pwm_chip *chip, struct seq_file *s)
{
	unsigned int i;

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];

		seq_printf(s, " pwm-%-3d (%-20.20s):", i, pwm->label);

		if (test_bit(PWMF_REQUESTED, &pwm->flags))
			seq_printf(s, " requested");

		if (test_bit(PWMF_ENABLED, &pwm->flags))
			seq_printf(s, " enabled");

		seq_printf(s, "\n");
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
