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
	struct device pwm_dev;
	struct pwm_device *pwm;
	struct mutex lock;
	struct pwm_state suspend;
};

static inline struct pwm_chip *pwmchip_from_dev(struct device *pwmchip_dev)
{
	return dev_get_drvdata(pwmchip_dev);
}

static inline struct pwm_export *pwmexport_from_dev(struct device *pwm_dev)
{
	return container_of(pwm_dev, struct pwm_export, pwm_dev);
}

static inline struct pwm_device *pwm_from_dev(struct device *pwm_dev)
{
	struct pwm_export *export = pwmexport_from_dev(pwm_dev);

	return export->pwm;
}

static ssize_t period_show(struct device *pwm_dev,
			   struct device_attribute *attr,
			   char *buf)
{
	const struct pwm_device *pwm = pwm_from_dev(pwm_dev);
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return sysfs_emit(buf, "%llu\n", state.period);
}

static ssize_t period_store(struct device *pwm_dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct pwm_export *export = pwmexport_from_dev(pwm_dev);
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
	ret = pwm_apply_might_sleep(pwm, &state);
	mutex_unlock(&export->lock);

	return ret ? : size;
}

static ssize_t duty_cycle_show(struct device *pwm_dev,
			       struct device_attribute *attr,
			       char *buf)
{
	const struct pwm_device *pwm = pwm_from_dev(pwm_dev);
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return sysfs_emit(buf, "%llu\n", state.duty_cycle);
}

static ssize_t duty_cycle_store(struct device *pwm_dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct pwm_export *export = pwmexport_from_dev(pwm_dev);
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
	ret = pwm_apply_might_sleep(pwm, &state);
	mutex_unlock(&export->lock);

	return ret ? : size;
}

static ssize_t enable_show(struct device *pwm_dev,
			   struct device_attribute *attr,
			   char *buf)
{
	const struct pwm_device *pwm = pwm_from_dev(pwm_dev);
	struct pwm_state state;

	pwm_get_state(pwm, &state);

	return sysfs_emit(buf, "%d\n", state.enabled);
}

static ssize_t enable_store(struct device *pwm_dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct pwm_export *export = pwmexport_from_dev(pwm_dev);
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

	ret = pwm_apply_might_sleep(pwm, &state);

unlock:
	mutex_unlock(&export->lock);
	return ret ? : size;
}

static ssize_t polarity_show(struct device *pwm_dev,
			     struct device_attribute *attr,
			     char *buf)
{
	const struct pwm_device *pwm = pwm_from_dev(pwm_dev);
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

	return sysfs_emit(buf, "%s\n", polarity);
}

static ssize_t polarity_store(struct device *pwm_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	struct pwm_export *export = pwmexport_from_dev(pwm_dev);
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
	ret = pwm_apply_might_sleep(pwm, &state);
	mutex_unlock(&export->lock);

	return ret ? : size;
}

static ssize_t capture_show(struct device *pwm_dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct pwm_device *pwm = pwm_from_dev(pwm_dev);
	struct pwm_capture result;
	int ret;

	ret = pwm_capture(pwm, &result, jiffies_to_msecs(HZ));
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u %u\n", result.period, result.duty_cycle);
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

static void pwm_export_release(struct device *pwm_dev)
{
	struct pwm_export *export = pwmexport_from_dev(pwm_dev);

	kfree(export);
}

static int pwm_export_child(struct device *pwmchip_dev, struct pwm_device *pwm)
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

	export->pwm_dev.release = pwm_export_release;
	export->pwm_dev.parent = pwmchip_dev;
	export->pwm_dev.devt = MKDEV(0, 0);
	export->pwm_dev.groups = pwm_groups;
	dev_set_name(&export->pwm_dev, "pwm%u", pwm->hwpwm);

	ret = device_register(&export->pwm_dev);
	if (ret) {
		clear_bit(PWMF_EXPORTED, &pwm->flags);
		put_device(&export->pwm_dev);
		export = NULL;
		return ret;
	}
	pwm_prop[0] = kasprintf(GFP_KERNEL, "EXPORT=pwm%u", pwm->hwpwm);
	pwm_prop[1] = NULL;
	kobject_uevent_env(&pwmchip_dev->kobj, KOBJ_CHANGE, pwm_prop);
	kfree(pwm_prop[0]);

	return 0;
}

static int pwm_unexport_match(struct device *pwm_dev, void *data)
{
	return pwm_from_dev(pwm_dev) == data;
}

static int pwm_unexport_child(struct device *pwmchip_dev, struct pwm_device *pwm)
{
	struct device *pwm_dev;
	char *pwm_prop[2];

	if (!test_and_clear_bit(PWMF_EXPORTED, &pwm->flags))
		return -ENODEV;

	pwm_dev = device_find_child(pwmchip_dev, pwm, pwm_unexport_match);
	if (!pwm_dev)
		return -ENODEV;

	pwm_prop[0] = kasprintf(GFP_KERNEL, "UNEXPORT=pwm%u", pwm->hwpwm);
	pwm_prop[1] = NULL;
	kobject_uevent_env(&pwmchip_dev->kobj, KOBJ_CHANGE, pwm_prop);
	kfree(pwm_prop[0]);

	/* for device_find_child() */
	put_device(pwm_dev);
	device_unregister(pwm_dev);
	pwm_put(pwm);

	return 0;
}

static ssize_t export_store(struct device *pwmchip_dev,
			    struct device_attribute *attr,
			    const char *buf, size_t len)
{
	struct pwm_chip *chip = pwmchip_from_dev(pwmchip_dev);
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

	ret = pwm_export_child(pwmchip_dev, pwm);
	if (ret < 0)
		pwm_put(pwm);

	return ret ? : len;
}
static DEVICE_ATTR_WO(export);

static ssize_t unexport_store(struct device *pwmchip_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t len)
{
	struct pwm_chip *chip = pwmchip_from_dev(pwmchip_dev);
	unsigned int hwpwm;
	int ret;

	ret = kstrtouint(buf, 0, &hwpwm);
	if (ret < 0)
		return ret;

	if (hwpwm >= chip->npwm)
		return -ENODEV;

	ret = pwm_unexport_child(pwmchip_dev, &chip->pwms[hwpwm]);

	return ret ? : len;
}
static DEVICE_ATTR_WO(unexport);

static ssize_t npwm_show(struct device *pwmchip_dev, struct device_attribute *attr,
			 char *buf)
{
	const struct pwm_chip *chip = pwmchip_from_dev(pwmchip_dev);

	return sysfs_emit(buf, "%u\n", chip->npwm);
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
static struct pwm_export *pwm_class_get_state(struct device *pwmchip_dev,
					      struct pwm_device *pwm,
					      struct pwm_state *state)
{
	struct device *pwm_dev;
	struct pwm_export *export;

	if (!test_bit(PWMF_EXPORTED, &pwm->flags))
		return NULL;

	pwm_dev = device_find_child(pwmchip_dev, pwm, pwm_unexport_match);
	if (!pwm_dev)
		return NULL;

	export = pwmexport_from_dev(pwm_dev);
	put_device(pwm_dev);	/* for device_find_child() */

	mutex_lock(&export->lock);
	pwm_get_state(pwm, state);

	return export;
}

static int pwm_class_apply_state(struct pwm_export *export,
				 struct pwm_device *pwm,
				 struct pwm_state *state)
{
	int ret = pwm_apply_might_sleep(pwm, state);

	/* release lock taken in pwm_class_get_state */
	mutex_unlock(&export->lock);

	return ret;
}

static int pwm_class_resume_npwm(struct device *pwmchip_dev, unsigned int npwm)
{
	struct pwm_chip *chip = pwmchip_from_dev(pwmchip_dev);
	unsigned int i;
	int ret = 0;

	for (i = 0; i < npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];
		struct pwm_state state;
		struct pwm_export *export;

		export = pwm_class_get_state(pwmchip_dev, pwm, &state);
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

static int pwm_class_suspend(struct device *pwmchip_dev)
{
	struct pwm_chip *chip = pwmchip_from_dev(pwmchip_dev);
	unsigned int i;
	int ret = 0;

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];
		struct pwm_state state;
		struct pwm_export *export;

		export = pwm_class_get_state(pwmchip_dev, pwm, &state);
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
			pwm_class_resume_npwm(pwmchip_dev, i);
			break;
		}
	}

	return ret;
}

static int pwm_class_resume(struct device *pwmchip_dev)
{
	struct pwm_chip *chip = pwmchip_from_dev(pwmchip_dev);

	return pwm_class_resume_npwm(pwmchip_dev, chip->npwm);
}

static DEFINE_SIMPLE_DEV_PM_OPS(pwm_class_pm_ops, pwm_class_suspend, pwm_class_resume);

static struct class pwm_class = {
	.name = "pwm",
	.dev_groups = pwm_chip_groups,
	.pm = pm_sleep_ptr(&pwm_class_pm_ops),
};

static int pwmchip_sysfs_match(struct device *pwmchip_dev, const void *data)
{
	return pwmchip_from_dev(pwmchip_dev) == data;
}

void pwmchip_sysfs_export(struct pwm_chip *chip)
{
	struct device *pwmchip_dev;

	/*
	 * If device_create() fails the pwm_chip is still usable by
	 * the kernel it's just not exported.
	 */
	pwmchip_dev = device_create(&pwm_class, pwmchip_parent(chip), MKDEV(0, 0), chip,
			       "pwmchip%d", chip->id);
	if (IS_ERR(pwmchip_dev)) {
		dev_warn(pwmchip_parent(chip),
			 "device_create failed for pwm_chip sysfs export\n");
	}
}

void pwmchip_sysfs_unexport(struct pwm_chip *chip)
{
	struct device *pwmchip_dev;
	unsigned int i;

	pwmchip_dev = class_find_device(&pwm_class, NULL, chip,
				   pwmchip_sysfs_match);
	if (!pwmchip_dev)
		return;

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];

		if (test_bit(PWMF_EXPORTED, &pwm->flags))
			pwm_unexport_child(pwmchip_dev, pwm);
	}

	put_device(pwmchip_dev);
	device_unregister(pwmchip_dev);
}

static int __init pwm_sysfs_init(void)
{
	return class_register(&pwm_class);
}
subsys_initcall(pwm_sysfs_init);
