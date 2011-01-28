/*
 * Generic pwmlib implementation
 *
 * Copyright (C) 2011 Sascha Hauer <s.hauer@pengutronix.de>
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
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/device.h>

struct pwm_device {
	struct			pwm_chip *chip;
	const char		*label;
	unsigned long		flags;
#define FLAG_REQUESTED	0
#define FLAG_ENABLED	1
	struct list_head	node;
};

static LIST_HEAD(pwm_list);

static DEFINE_MUTEX(pwm_lock);

static struct pwm_device *_find_pwm(int pwm_id)
{
	struct pwm_device *pwm;

	list_for_each_entry(pwm, &pwm_list, node) {
		if (pwm->chip->pwm_id == pwm_id)
			return pwm;
	}

	return NULL;
}

/**
 * pwmchip_add() - register a new PWM chip
 * @chip: the PWM chip to add
 */
int pwmchip_add(struct pwm_chip *chip)
{
	struct pwm_device *pwm;
	int ret = 0;

	pwm = kzalloc(sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	pwm->chip = chip;

	mutex_lock(&pwm_lock);

	if (chip->pwm_id >= 0 && _find_pwm(chip->pwm_id)) {
		ret = -EBUSY;
		goto out;
	}

	list_add_tail(&pwm->node, &pwm_list);
out:
	mutex_unlock(&pwm_lock);

	if (ret)
		kfree(pwm);

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
	struct pwm_device *pwm;
	int ret = 0;

	mutex_lock(&pwm_lock);

	pwm = _find_pwm(chip->pwm_id);
	if (!pwm) {
		ret = -ENOENT;
		goto out;
	}

	if (test_bit(FLAG_REQUESTED, &pwm->flags)) {
		ret = -EBUSY;
		goto out;
	}

	list_del(&pwm->node);

	kfree(pwm);
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
struct pwm_device *pwm_request(int pwm_id, const char *label)
{
	struct pwm_device *pwm;
	int ret;

	mutex_lock(&pwm_lock);

	pwm = _find_pwm(pwm_id);
	if (!pwm) {
		pwm = ERR_PTR(-ENOENT);
		goto out;
	}

	if (test_bit(FLAG_REQUESTED, &pwm->flags)) {
		pwm = ERR_PTR(-EBUSY);
		goto out;
	}

	if (!try_module_get(pwm->chip->ops->owner)) {
		pwm = ERR_PTR(-ENODEV);
		goto out;
	}

	if (pwm->chip->ops->request) {
		ret = pwm->chip->ops->request(pwm->chip);
		if (ret) {
			pwm = ERR_PTR(ret);
			goto out_put;
		}
	}

	pwm->label = label;
	set_bit(FLAG_REQUESTED, &pwm->flags);

	goto out;

out_put:
	module_put(pwm->chip->ops->owner);
out:
	mutex_unlock(&pwm_lock);

	return pwm;
}
EXPORT_SYMBOL_GPL(pwm_request);

/**
 * pwm_free() - free a PWM device
 * @pwm: PWM device
 */
void pwm_free(struct pwm_device *pwm)
{
	mutex_lock(&pwm_lock);

	if (!test_and_clear_bit(FLAG_REQUESTED, &pwm->flags)) {
		pr_warning("PWM device already freed\n");
		goto out;
	}

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
	return pwm->chip->ops->config(pwm->chip, duty_ns, period_ns);
}
EXPORT_SYMBOL_GPL(pwm_config);

/**
 * pwm_enable() - start a PWM output toggling
 * @pwm: PWM device
 */
int pwm_enable(struct pwm_device *pwm)
{
	if (!test_and_set_bit(FLAG_ENABLED, &pwm->flags))
		return pwm->chip->ops->enable(pwm->chip);

	return 0;
}
EXPORT_SYMBOL_GPL(pwm_enable);

/**
 * pwm_disable() - stop a PWM output toggling
 * @pwm: PWM device
 */
void pwm_disable(struct pwm_device *pwm)
{
	if (test_and_clear_bit(FLAG_ENABLED, &pwm->flags))
		pwm->chip->ops->disable(pwm->chip);
}
EXPORT_SYMBOL_GPL(pwm_disable);
