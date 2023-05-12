// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A simple sysfs interface for the generic PWM framework
 *
 * Copyright (C) 2013 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Based on previous work by Lars Poeschel <poeschel@lemonage.de>
 */

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/pwm.h>

struct pwm_export {
	struct device child;
	struct pwm_device *pwm;
	struct mutex lock;
	struct pwm_state suspend;
};

static struct pwm_export *child_to_pwm_export(struct device *child)
{
	return container_of(child, struct pwm_export, child);
}

static struct pwm_device *child_to_pwm_device(struct device *child)
{
	struct pwm_export *export = child_to_pwm_export(child);

	return export->pwm;
}

static ssize_t period_show(struct device *child,
			   struct device_attribute *attr,
			   char *buf)
{
	const struct pwm_device *pwm = child_to_pwm_device(child);
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return sprintf(buf, "%llu\n", state.period);
}

static ssize_t period_store(struct device *child,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct pwm_export *export = child_to_pwm_export(child);
	struct pwm_device *pwm = export->pwm;
	struct pwm_state state;
	u64 val;
	int ret;

	ret = kstrtou64(buf, 0, &val);
	if (ret)
		return ret;

	mutex_lock(&export->lock);
	pwm_get_state(pwm, &state);
	state.period = val;
	ret = pwm_apply_state(pwm, &state);
	mutex_unlock(&export->lock);

	return ret ? : size;
}

static ssize_t duty_cycle_show(struct device *child,
			       struct device_attribute *attr,
			       char *buf)
{
	const struct pwm_device *pwm = child_to_pwm_device(child);
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return sprintf(buf, "%llu\n", state.duty_cycle);
}

static ssize_t duty_cycle_store(struct device *child,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct pwm_export *export = child_to_pwm_export(child);
	struct pwm_device *pwm = export->pwm;
	struct pwm_state state;
	u64 val;
	int ret;

	ret = kstrtou64(buf, 0, &val);
	if (ret)
		return ret;

	mutex_lock(&export->lock);
	pwm_get_state(pwm, &state);
	state.duty_cycle = val;
	ret = pwm_apply_state(pwm, &state);
	mutex_unlock(&export->lock);

	return ret ? : size;
}

static ssize_t enable_show(struct device *child,
			   struct device_attribute *attr,
			   char *buf)
{
	const struct pwm_device *pwm = child_to_pwm_device(child);
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return sprintf(buf, "%d\n", state.enabled);
}

static ssize_t enable_store(struct device *child,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct pwm_export *export = child_to_pwm_export(child);
	struct pwm_device *pwm = export->pwm;
	struct pwm_state state;
	int val, ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;

	mutex_lock(&export->lock);

	pwm_get_state(pwm, &state);

	switch (val) {
	case 0:
		state.enabled = false;
		break;
	case 1:
		state.enabled = true;
		break;
	default:
		ret = -EINVAL;
		goto unlock;
	}

	ret = pwm_apply_state(pwm, &state);

unlock:
	mutex_unlock(&export->lock);
	return ret ? : size;
}

static ssize_t polarity_show(struct device *child,
			     struct device_attribute *attr,
			     char *buf)
{
	const struct pwm_device *pwm = child_to_pwm_device(child);
	const char *polarity = "unknown";
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	switch (state.polarity) {
	case PWM_POLARITY_NORMAL:
		polarity = "normal";
		break;

	case PWM_POLARITY_INVERSED:
		polarity = "inversed";
		break;
	}

	return sprintf(buf, "%s\n", polarity);
}

static ssize_t polarity_store(struct device *child,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	struct pwm_export *export = child_to_pwm_export(child);
	struct pwm_device *pwm = export->pwm;
	enum pwm_polarity polarity;
	struct pwm_state state;
	int ret;

	if (sysfs_streq(buf, "normal"))
		polarity = PWM_POLARITY_NORMAL;
	else if (sysfs_streq(buf, "inversed"))
		polarity = PWM_POLARITY_INVERSED;
	else
		return -EINVAL;

	mutex_lock(&export->lock);
	pwm_get_state(pwm, &state);
	state.polarity = polarity;
	ret = pwm_apply_state(pwm, &state);
	mutex_unlock(&export->lock);

	return ret ? : size;
}

static ssize_t capture_show(struct device *child,
			    struct device_attribute *attr,
			    char *buf)
{
	struct pwm_device *pwm = child_to_pwm_device(child);
	struct pwm_capture result;
	int ret;

	ret = pwm_capture(pwm, &result, jiffies_to_msecs(HZ));
	if (ret)
		return ret;

	return sprintf(buf, "%u %u\n", result.period, result.duty_cycle);
}

static DEVICE_ATTR_RW(period);
static DEVICE_ATTR_RW(duty_cycle);
static DEVICE_ATTR_RW(enable);
static DEVICE_ATTR_RW(polarity);
static DEVICE_ATTR_RO(capture);

static struct attribute *pwm_attrs[] = {
	&dev_attr_period.attr,
	&dev_attr_duty_cycle.attr,
	&dev_attr_enable.attr,
	&dev_attr_polarity.attr,
	&dev_attr_capture.attr,
	NULL
};
ATTRIBUTE_GROUPS(pwm);

static void pwm_export_release(struct device *child)
{
	struct pwm_export *export = child_to_pwm_export(child);

	kfree(export);
}

static int pwm_export_child(struct device *parent, struct pwm_device *pwm)
{
	struct pwm_export *export;
	char *pwm_prop[2];
	int ret;

	if (test_and_set_bit(PWMF_EXPORTED, &pwm->flags))
		return -EBUSY;

	export = kzalloc(sizeof(*export), GFP_KERNEL);
	if (!export) {
		clear_bit(PWMF_EXPORTED, &pwm->flags);
		return -ENOMEM;
	}

	export->pwm = pwm;
	mutex_init(&export->lock);

	export->child.release = pwm_export_release;
	export->child.parent = parent;
	export->child.devt = MKDEV(0, 0);
	export->child.groups = pwm_groups;
	dev_set_name(&export->child, "pwm%u", pwm->hwpwm);

	ret = device_register(&export->child);
	if (ret) {
		clear_bit(PWMF_EXPORTED, &pwm->flags);
		put_device(&export->child);
		export = NULL;
		return ret;
	}
	pwm_prop[0] = kasprintf(GFP_KERNEL, "EXPORT=pwm%u", pwm->hwpwm);
	pwm_prop[1] = NULL;
	kobject_uevent_env(&parent->kobj, KOBJ_CHANGE, pwm_prop);
	kfree(pwm_prop[0]);

	return 0;
}

static int pwm_unexport_match(struct device *child, void *data)
{
	return child_to_pwm_device(child) == data;
}

static int pwm_unexport_child(struct device *parent, struct pwm_device *pwm)
{
	struct device *child;
	char *pwm_prop[2];

	if (!test_and_clear_bit(PWMF_EXPORTED, &pwm->flags))
		return -ENODEV;

	child = device_find_child(parent, pwm, pwm_unexport_match);
	if (!child)
		return -ENODEV;

	pwm_prop[0] = kasprintf(GFP_KERNEL, "UNEXPORT=pwm%u", pwm->hwpwm);
	pwm_prop[1] = NULL;
	kobject_uevent_env(&parent->kobj, KOBJ_CHANGE, pwm_prop);
	kfree(pwm_prop[0]);

	/* for device_find_child() */
	put_device(child);
	device_unregister(child);
	pwm_put(pwm);

	return 0;
}

static ssize_t export_store(struct device *parent,
			    struct device_attribute *attr,
			    const char *buf, size_t len)
{
	struct pwm_chip *chip = dev_get_drvdata(parent);
	struct pwm_device *pwm;
	unsigned int hwpwm;
	int ret;

	ret = kstrtouint(buf, 0, &hwpwm);
	if (ret < 0)
		return ret;

	if (hwpwm >= chip->npwm)
		return -ENODEV;

	pwm = pwm_request_from_chip(chip, hwpwm, "sysfs");
	if (IS_ERR(pwm))
		return PTR_ERR(pwm);

	ret = pwm_export_child(parent, pwm);
	if (ret < 0)
		pwm_put(pwm);

	return ret ? : len;
}
static DEVICE_ATTR_WO(export);

static ssize_t unexport_store(struct device *parent,
			      struct device_attribute *attr,
			      const char *buf, size_t len)
{
	struct pwm_chip *chip = dev_get_drvdata(parent);
	unsigned int hwpwm;
	int ret;

	ret = kstrtouint(buf, 0, &hwpwm);
	if (ret < 0)
		return ret;

	if (hwpwm >= chip->npwm)
		return -ENODEV;

	ret = pwm_unexport_child(parent, &chip->pwms[hwpwm]);

	return ret ? : len;
}
static DEVICE_ATTR_WO(unexport);

static ssize_t npwm_show(struct device *parent, struct device_attribute *attr,
			 char *buf)
{
	const struct pwm_chip *chip = dev_get_drvdata(parent);

	return sprintf(buf, "%u\n", chip->npwm);
}
static DEVICE_ATTR_RO(npwm);

static struct attribute *pwm_chip_attrs[] = {
	&dev_attr_export.attr,
	&dev_attr_unexport.attr,
	&dev_attr_npwm.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pwm_chip);

/* takes export->lock on success */
static struct pwm_export *pwm_class_get_state(struct device *parent,
					      struct pwm_device *pwm,
					      struct pwm_state *state)
{
	struct device *child;
	struct pwm_export *export;

	if (!test_bit(PWMF_EXPORTED, &pwm->flags))
		return NULL;

	child = device_find_child(parent, pwm, pwm_unexport_match);
	if (!child)
		return NULL;

	export = child_to_pwm_export(child);
	put_device(child);	/* for device_find_child() */

	mutex_lock(&export->lock);
	pwm_get_state(pwm, state);

	return export;
}

static int pwm_class_apply_state(struct pwm_export *export,
				 struct pwm_device *pwm,
				 struct pwm_state *state)
{
	int ret = pwm_apply_state(pwm, state);

	/* release lock taken in pwm_class_get_state */
	mutex_unlock(&export->lock);

	return ret;
}

static int pwm_class_resume_npwm(struct device *parent, unsigned int npwm)
{
	struct pwm_chip *chip = dev_get_drvdata(parent);
	unsigned int i;
	int ret = 0;

	for (i = 0; i < npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];
		struct pwm_state state;
		struct pwm_export *export;

		export = pwm_class_get_state(parent, pwm, &state);
		if (!export)
			continue;

		/* If pwmchip was not enabled before suspend, do nothing. */
		if (!export->suspend.enabled) {
			/* release lock taken in pwm_class_get_state */
			mutex_unlock(&export->lock);
			continue;
		}

		state.enabled = export->suspend.enabled;
		ret = pwm_class_apply_state(export, pwm, &state);
		if (ret < 0)
			break;
	}

	return ret;
}

static int __maybe_unused pwm_class_suspend(struct device *parent)
{
	struct pwm_chip *chip = dev_get_drvdata(parent);
	unsigned int i;
	int ret = 0;

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];
		struct pwm_state state;
		struct pwm_export *export;

		export = pwm_class_get_state(parent, pwm, &state);
		if (!export)
			continue;

		/*
		 * If pwmchip was not enabled before suspend, save
		 * state for resume time and do nothing else.
		 */
		export->suspend = state;
		if (!state.enabled) {
			/* release lock taken in pwm_class_get_state */
			mutex_unlock(&export->lock);
			continue;
		}

		state.enabled = false;
		ret = pwm_class_apply_state(export, pwm, &state);
		if (ret < 0) {
			/*
			 * roll back the PWM devices that were disabled by
			 * this suspend function.
			 */
			pwm_class_resume_npwm(parent, i);
			break;
		}
	}

	return ret;
}

static int __maybe_unused pwm_class_resume(struct device *parent)
{
	struct pwm_chip *chip = dev_get_drvdata(parent);

	return pwm_class_resume_npwm(parent, chip->npwm);
}

static SIMPLE_DEV_PM_OPS(pwm_class_pm_ops, pwm_class_suspend, pwm_class_resume);

static struct class pwm_class = {
	.name = "pwm",
	.owner = THIS_MODULE,
	.dev_groups = pwm_chip_groups,
	.pm = &pwm_class_pm_ops,
};

static int pwmchip_sysfs_match(struct device *parent, const void *data)
{
	return dev_get_drvdata(parent) == data;
}

void pwmchip_sysfs_export(struct pwm_chip *chip)
{
	struct device *parent;

	/*
	 * If device_create() fails the pwm_chip is still usable by
	 * the kernel it's just not exported.
	 */
	parent = device_create(&pwm_class, chip->dev, MKDEV(0, 0), chip,
			       "pwmchip%d", chip->base);
	if (IS_ERR(parent)) {
		dev_warn(chip->dev,
			 "device_create failed for pwm_chip sysfs export\n");
	}
}

void pwmchip_sysfs_unexport(struct pwm_chip *chip)
{
	struct device *parent;
	unsigned int i;

	parent = class_find_device(&pwm_class, NULL, chip,
				   pwmchip_sysfs_match);
	if (!parent)
		return;

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];

		if (test_bit(PWMF_EXPORTED, &pwm->flags))
			pwm_unexport_child(parent, pwm);
	}

	put_device(parent);
	device_unregister(parent);
}

static int __init pwm_sysfs_init(void)
{
	return class_register(&pwm_class);
}
subsys_initcall(pwm_sysfs_init);
