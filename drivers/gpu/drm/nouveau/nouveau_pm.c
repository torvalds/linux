/*
 * Copyright 2010 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#ifdef CONFIG_ACPI
#include <linux/acpi.h>
#endif
#include <linux/power_supply.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#include <drm/drmP.h>

#include "nouveau_drm.h"
#include "nouveau_pm.h"

#include <subdev/gpio.h>
#include <subdev/timer.h>
#include <subdev/therm.h>

MODULE_PARM_DESC(perflvl, "Performance level (default: boot)");
static char *nouveau_perflvl;
module_param_named(perflvl, nouveau_perflvl, charp, 0400);

MODULE_PARM_DESC(perflvl_wr, "Allow perflvl changes (warning: dangerous!)");
static int nouveau_perflvl_wr;
module_param_named(perflvl_wr, nouveau_perflvl_wr, int, 0400);

static int
nouveau_pm_perflvl_aux(struct drm_device *dev, struct nouveau_pm_level *perflvl,
		       struct nouveau_pm_level *a, struct nouveau_pm_level *b)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);
	int ret;

	/*XXX: not on all boards, we should control based on temperature
	 *     on recent boards..  or maybe on some other factor we don't
	 *     know about?
	 */
	if (therm && therm->fan_set &&
		a->fanspeed && b->fanspeed && b->fanspeed > a->fanspeed) {
		ret = therm->fan_set(therm, perflvl->fanspeed);
		if (ret && ret != -ENODEV) {
			NV_ERROR(drm, "fanspeed set failed: %d\n", ret);
		}
	}

	if (pm->voltage.supported && pm->voltage_set) {
		if (perflvl->volt_min && b->volt_min > a->volt_min) {
			ret = pm->voltage_set(dev, perflvl->volt_min);
			if (ret) {
				NV_ERROR(drm, "voltage set failed: %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

static int
nouveau_pm_perflvl_set(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct nouveau_pm *pm = nouveau_pm(dev);
	void *state;
	int ret;

	if (perflvl == pm->cur)
		return 0;

	ret = nouveau_pm_perflvl_aux(dev, perflvl, pm->cur, perflvl);
	if (ret)
		return ret;

	state = pm->clocks_pre(dev, perflvl);
	if (IS_ERR(state)) {
		ret = PTR_ERR(state);
		goto error;
	}
	ret = pm->clocks_set(dev, state);
	if (ret)
		goto error;

	ret = nouveau_pm_perflvl_aux(dev, perflvl, perflvl, pm->cur);
	if (ret)
		return ret;

	pm->cur = perflvl;
	return 0;

error:
	/* restore the fan speed and voltage before leaving */
	nouveau_pm_perflvl_aux(dev, perflvl, perflvl, pm->cur);
	return ret;
}

void
nouveau_pm_trigger(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_timer *ptimer = nouveau_timer(drm->device);
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct nouveau_pm_profile *profile = NULL;
	struct nouveau_pm_level *perflvl = NULL;
	int ret;

	/* select power profile based on current power source */
	if (power_supply_is_system_supplied())
		profile = pm->profile_ac;
	else
		profile = pm->profile_dc;

	if (profile != pm->profile) {
		pm->profile->func->fini(pm->profile);
		pm->profile = profile;
		pm->profile->func->init(pm->profile);
	}

	/* select performance level based on profile */
	perflvl = profile->func->select(profile);

	/* change perflvl, if necessary */
	if (perflvl != pm->cur) {
		u64 time0 = ptimer->read(ptimer);

		NV_INFO(drm, "setting performance level: %d", perflvl->id);
		ret = nouveau_pm_perflvl_set(dev, perflvl);
		if (ret)
			NV_INFO(drm, "> reclocking failed: %d\n\n", ret);

		NV_INFO(drm, "> reclocking took %lluns\n\n",
			     ptimer->read(ptimer) - time0);
	}
}

static struct nouveau_pm_profile *
profile_find(struct drm_device *dev, const char *string)
{
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct nouveau_pm_profile *profile;

	list_for_each_entry(profile, &pm->profiles, head) {
		if (!strncmp(profile->name, string, sizeof(profile->name)))
			return profile;
	}

	return NULL;
}

static int
nouveau_pm_profile_set(struct drm_device *dev, const char *profile)
{
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct nouveau_pm_profile *ac = NULL, *dc = NULL;
	char string[16], *cur = string, *ptr;

	/* safety precaution, for now */
	if (nouveau_perflvl_wr != 7777)
		return -EPERM;

	strncpy(string, profile, sizeof(string));
	string[sizeof(string) - 1] = 0;
	if ((ptr = strchr(string, '\n')))
		*ptr = '\0';

	ptr = strsep(&cur, ",");
	if (ptr)
		ac = profile_find(dev, ptr);

	ptr = strsep(&cur, ",");
	if (ptr)
		dc = profile_find(dev, ptr);
	else
		dc = ac;

	if (ac == NULL || dc == NULL)
		return -EINVAL;

	pm->profile_ac = ac;
	pm->profile_dc = dc;
	nouveau_pm_trigger(dev);
	return 0;
}

static void
nouveau_pm_static_dummy(struct nouveau_pm_profile *profile)
{
}

static struct nouveau_pm_level *
nouveau_pm_static_select(struct nouveau_pm_profile *profile)
{
	return container_of(profile, struct nouveau_pm_level, profile);
}

const struct nouveau_pm_profile_func nouveau_pm_static_profile_func = {
	.destroy = nouveau_pm_static_dummy,
	.init = nouveau_pm_static_dummy,
	.fini = nouveau_pm_static_dummy,
	.select = nouveau_pm_static_select,
};

static int
nouveau_pm_perflvl_get(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);
	int ret;

	memset(perflvl, 0, sizeof(*perflvl));

	if (pm->clocks_get) {
		ret = pm->clocks_get(dev, perflvl);
		if (ret)
			return ret;
	}

	if (pm->voltage.supported && pm->voltage_get) {
		ret = pm->voltage_get(dev);
		if (ret > 0) {
			perflvl->volt_min = ret;
			perflvl->volt_max = ret;
		}
	}

	if (therm && therm->fan_get) {
		ret = therm->fan_get(therm);
		if (ret >= 0)
			perflvl->fanspeed = ret;
	}

	nouveau_mem_timing_read(dev, &perflvl->timing);
	return 0;
}

static void
nouveau_pm_perflvl_info(struct nouveau_pm_level *perflvl, char *ptr, int len)
{
	char c[16], s[16], v[32], f[16], m[16];

	c[0] = '\0';
	if (perflvl->core)
		snprintf(c, sizeof(c), " core %dMHz", perflvl->core / 1000);

	s[0] = '\0';
	if (perflvl->shader)
		snprintf(s, sizeof(s), " shader %dMHz", perflvl->shader / 1000);

	m[0] = '\0';
	if (perflvl->memory)
		snprintf(m, sizeof(m), " memory %dMHz", perflvl->memory / 1000);

	v[0] = '\0';
	if (perflvl->volt_min && perflvl->volt_min != perflvl->volt_max) {
		snprintf(v, sizeof(v), " voltage %dmV-%dmV",
			 perflvl->volt_min / 1000, perflvl->volt_max / 1000);
	} else
	if (perflvl->volt_min) {
		snprintf(v, sizeof(v), " voltage %dmV",
			 perflvl->volt_min / 1000);
	}

	f[0] = '\0';
	if (perflvl->fanspeed)
		snprintf(f, sizeof(f), " fanspeed %d%%", perflvl->fanspeed);

	snprintf(ptr, len, "%s%s%s%s%s\n", c, s, m, v, f);
}

static ssize_t
nouveau_pm_get_perflvl_info(struct device *d,
			    struct device_attribute *a, char *buf)
{
	struct nouveau_pm_level *perflvl =
		container_of(a, struct nouveau_pm_level, dev_attr);
	char *ptr = buf;
	int len = PAGE_SIZE;

	snprintf(ptr, len, "%d:", perflvl->id);
	ptr += strlen(buf);
	len -= strlen(buf);

	nouveau_pm_perflvl_info(perflvl, ptr, len);
	return strlen(buf);
}

static ssize_t
nouveau_pm_get_perflvl(struct device *d, struct device_attribute *a, char *buf)
{
	struct drm_device *dev = pci_get_drvdata(to_pci_dev(d));
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct nouveau_pm_level cur;
	int len = PAGE_SIZE, ret;
	char *ptr = buf;

	snprintf(ptr, len, "profile: %s, %s\nc:",
		 pm->profile_ac->name, pm->profile_dc->name);
	ptr += strlen(buf);
	len -= strlen(buf);

	ret = nouveau_pm_perflvl_get(dev, &cur);
	if (ret == 0)
		nouveau_pm_perflvl_info(&cur, ptr, len);
	return strlen(buf);
}

static ssize_t
nouveau_pm_set_perflvl(struct device *d, struct device_attribute *a,
		       const char *buf, size_t count)
{
	struct drm_device *dev = pci_get_drvdata(to_pci_dev(d));
	int ret;

	ret = nouveau_pm_profile_set(dev, buf);
	if (ret)
		return ret;
	return strlen(buf);
}

static DEVICE_ATTR(performance_level, S_IRUGO | S_IWUSR,
		   nouveau_pm_get_perflvl, nouveau_pm_set_perflvl);

static int
nouveau_sysfs_init(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct device *d = &dev->pdev->dev;
	int ret, i;

	ret = device_create_file(d, &dev_attr_performance_level);
	if (ret)
		return ret;

	for (i = 0; i < pm->nr_perflvl; i++) {
		struct nouveau_pm_level *perflvl = &pm->perflvl[i];

		perflvl->dev_attr.attr.name = perflvl->name;
		perflvl->dev_attr.attr.mode = S_IRUGO;
		perflvl->dev_attr.show = nouveau_pm_get_perflvl_info;
		perflvl->dev_attr.store = NULL;
		sysfs_attr_init(&perflvl->dev_attr.attr);

		ret = device_create_file(d, &perflvl->dev_attr);
		if (ret) {
			NV_ERROR(drm, "failed pervlvl %d sysfs: %d\n",
				 perflvl->id, i);
			perflvl->dev_attr.attr.name = NULL;
			nouveau_pm_fini(dev);
			return ret;
		}
	}

	return 0;
}

static void
nouveau_sysfs_fini(struct drm_device *dev)
{
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct device *d = &dev->pdev->dev;
	int i;

	device_remove_file(d, &dev_attr_performance_level);
	for (i = 0; i < pm->nr_perflvl; i++) {
		struct nouveau_pm_level *pl = &pm->perflvl[i];

		if (!pl->dev_attr.attr.name)
			break;

		device_remove_file(d, &pl->dev_attr);
	}
}

#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))
static ssize_t
nouveau_hwmon_show_temp(struct device *d, struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);

	return snprintf(buf, PAGE_SIZE, "%d\n", therm->temp_get(therm) * 1000);
}
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, nouveau_hwmon_show_temp,
						  NULL, 0);

static ssize_t
nouveau_hwmon_max_temp(struct device *d, struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);

	return snprintf(buf, PAGE_SIZE, "%d\n",
	       therm->attr_get(therm, NOUVEAU_THERM_ATTR_THRS_DOWN_CLK) * 1000);
}
static ssize_t
nouveau_hwmon_set_max_temp(struct device *d, struct device_attribute *a,
						const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	therm->attr_set(therm, NOUVEAU_THERM_ATTR_THRS_DOWN_CLK, value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO | S_IWUSR, nouveau_hwmon_max_temp,
						  nouveau_hwmon_set_max_temp,
						  0);

static ssize_t
nouveau_hwmon_critical_temp(struct device *d, struct device_attribute *a,
							char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);

	return snprintf(buf, PAGE_SIZE, "%d\n",
	       therm->attr_get(therm, NOUVEAU_THERM_ATTR_THRS_CRITICAL) * 1000);
}
static ssize_t
nouveau_hwmon_set_critical_temp(struct device *d, struct device_attribute *a,
							    const char *buf,
								size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	therm->attr_set(therm, NOUVEAU_THERM_ATTR_THRS_CRITICAL, value / 1000);

	return count;
}
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO | S_IWUSR,
						nouveau_hwmon_critical_temp,
						nouveau_hwmon_set_critical_temp,
						0);

static ssize_t nouveau_hwmon_show_name(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return sprintf(buf, "nouveau\n");
}
static SENSOR_DEVICE_ATTR(name, S_IRUGO, nouveau_hwmon_show_name, NULL, 0);

static ssize_t nouveau_hwmon_show_update_rate(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return sprintf(buf, "1000\n");
}
static SENSOR_DEVICE_ATTR(update_rate, S_IRUGO,
						nouveau_hwmon_show_update_rate,
						NULL, 0);

static ssize_t
nouveau_hwmon_show_fan0_input(struct device *d, struct device_attribute *attr,
			      char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);

	return snprintf(buf, PAGE_SIZE, "%d\n", therm->fan_sense(therm));
}
static SENSOR_DEVICE_ATTR(fan0_input, S_IRUGO, nouveau_hwmon_show_fan0_input,
			  NULL, 0);

 static ssize_t
nouveau_hwmon_get_pwm1_enable(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);
	int ret;

	ret = therm->attr_get(therm, NOUVEAU_THERM_ATTR_FAN_MODE);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
nouveau_hwmon_set_pwm1_enable(struct device *d, struct device_attribute *a,
			   const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);
	long value;
	int ret;

	if (strict_strtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;

	ret = therm->attr_set(therm, NOUVEAU_THERM_ATTR_FAN_MODE, value);
	if (ret)
		return ret;
	else
		return count;
}
static SENSOR_DEVICE_ATTR(pwm1_enable, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm1_enable,
			  nouveau_hwmon_set_pwm1_enable, 0);

static ssize_t
nouveau_hwmon_get_pwm1(struct device *d, struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);
	int ret;

	ret = therm->fan_get(therm);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
nouveau_hwmon_set_pwm1(struct device *d, struct device_attribute *a,
		       const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);
	int ret = -ENODEV;
	long value;

	if (nouveau_perflvl_wr != 7777)
		return -EPERM;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;

	ret = therm->fan_set(therm, value);
	if (ret)
		return ret;

	return count;
}

static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm1,
			  nouveau_hwmon_set_pwm1, 0);

static ssize_t
nouveau_hwmon_get_pwm1_min(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);
	int ret;

	ret = therm->attr_get(therm, NOUVEAU_THERM_ATTR_FAN_MIN_DUTY);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
nouveau_hwmon_set_pwm1_min(struct device *d, struct device_attribute *a,
			   const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);
	long value;
	int ret;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;

	ret = therm->attr_set(therm, NOUVEAU_THERM_ATTR_FAN_MIN_DUTY, value);
	if (ret < 0)
		return ret;

	return count;
}

static SENSOR_DEVICE_ATTR(pwm1_min, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm1_min,
			  nouveau_hwmon_set_pwm1_min, 0);

static ssize_t
nouveau_hwmon_get_pwm1_max(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);
	int ret;

	ret = therm->attr_get(therm, NOUVEAU_THERM_ATTR_FAN_MAX_DUTY);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
nouveau_hwmon_set_pwm1_max(struct device *d, struct device_attribute *a,
			   const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);
	long value;
	int ret;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;

	ret = therm->attr_set(therm, NOUVEAU_THERM_ATTR_FAN_MAX_DUTY, value);
	if (ret < 0)
		return ret;

	return count;
}

static SENSOR_DEVICE_ATTR(pwm1_max, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm1_max,
			  nouveau_hwmon_set_pwm1_max, 0);

static struct attribute *hwmon_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_name.dev_attr.attr,
	&sensor_dev_attr_update_rate.dev_attr.attr,
	NULL
};
static struct attribute *hwmon_fan_rpm_attributes[] = {
	&sensor_dev_attr_fan0_input.dev_attr.attr,
	NULL
};
static struct attribute *hwmon_pwm_fan_attributes[] = {
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_min.dev_attr.attr,
	&sensor_dev_attr_pwm1_max.dev_attr.attr,
	NULL
};

static const struct attribute_group hwmon_attrgroup = {
	.attrs = hwmon_attributes,
};
static const struct attribute_group hwmon_fan_rpm_attrgroup = {
	.attrs = hwmon_fan_rpm_attributes,
};
static const struct attribute_group hwmon_pwm_fan_attrgroup = {
	.attrs = hwmon_pwm_fan_attributes,
};
#endif

static int
nouveau_hwmon_init(struct drm_device *dev)
{
	struct nouveau_pm *pm = nouveau_pm(dev);

#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_therm *therm = nouveau_therm(drm->device);
	struct device *hwmon_dev;
	int ret = 0;

	if (!therm || !therm->temp_get || !therm->attr_get || !therm->attr_set)
		return -ENODEV;

	hwmon_dev = hwmon_device_register(&dev->pdev->dev);
	if (IS_ERR(hwmon_dev)) {
		ret = PTR_ERR(hwmon_dev);
		NV_ERROR(drm, "Unable to register hwmon device: %d\n", ret);
		return ret;
	}
	dev_set_drvdata(hwmon_dev, dev);

	/* default sysfs entries */
	ret = sysfs_create_group(&dev->pdev->dev.kobj, &hwmon_attrgroup);
	if (ret) {
		if (ret)
			goto error;
	}

	/* if the card has a pwm fan */
	/*XXX: incorrect, need better detection for this, some boards have
	 *     the gpio entries for pwm fan control even when there's no
	 *     actual fan connected to it... therm table? */
	if (therm->fan_get && therm->fan_get(therm) >= 0) {
		ret = sysfs_create_group(&dev->pdev->dev.kobj,
					 &hwmon_pwm_fan_attrgroup);
		if (ret)
			goto error;
	}

	/* if the card can read the fan rpm */
	if (therm->fan_sense(therm) >= 0) {
		ret = sysfs_create_group(&dev->pdev->dev.kobj,
					 &hwmon_fan_rpm_attrgroup);
		if (ret)
			goto error;
	}

	pm->hwmon = hwmon_dev;

	return 0;

error:
	NV_ERROR(drm, "Unable to create some hwmon sysfs files: %d\n", ret);
	hwmon_device_unregister(hwmon_dev);
	pm->hwmon = NULL;
	return ret;
#else
	pm->hwmon = NULL;
	return 0;
#endif
}

static void
nouveau_hwmon_fini(struct drm_device *dev)
{
#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))
	struct nouveau_pm *pm = nouveau_pm(dev);

	if (pm->hwmon) {
		sysfs_remove_group(&dev->pdev->dev.kobj, &hwmon_attrgroup);
		sysfs_remove_group(&dev->pdev->dev.kobj,
				   &hwmon_pwm_fan_attrgroup);
		sysfs_remove_group(&dev->pdev->dev.kobj,
				   &hwmon_fan_rpm_attrgroup);

		hwmon_device_unregister(pm->hwmon);
	}
#endif
}

#if defined(CONFIG_ACPI) && defined(CONFIG_POWER_SUPPLY)
static int
nouveau_pm_acpi_event(struct notifier_block *nb, unsigned long val, void *data)
{
	struct nouveau_pm *pm = container_of(nb, struct nouveau_pm, acpi_nb);
	struct nouveau_drm *drm = nouveau_drm(pm->dev);
	struct acpi_bus_event *entry = (struct acpi_bus_event *)data;

	if (strcmp(entry->device_class, "ac_adapter") == 0) {
		bool ac = power_supply_is_system_supplied();

		NV_DEBUG(drm, "power supply changed: %s\n", ac ? "AC" : "DC");
		nouveau_pm_trigger(pm->dev);
	}

	return NOTIFY_OK;
}
#endif

int
nouveau_pm_init(struct drm_device *dev)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_pm *pm;
	char info[256];
	int ret, i;

	pm = drm->pm = kzalloc(sizeof(*pm), GFP_KERNEL);
	if (!pm)
		return -ENOMEM;

	pm->dev = dev;

	if (device->card_type < NV_40) {
		pm->clocks_get = nv04_pm_clocks_get;
		pm->clocks_pre = nv04_pm_clocks_pre;
		pm->clocks_set = nv04_pm_clocks_set;
		if (nouveau_gpio(drm->device)) {
			pm->voltage_get = nouveau_voltage_gpio_get;
			pm->voltage_set = nouveau_voltage_gpio_set;
		}
	} else
	if (device->card_type < NV_50) {
		pm->clocks_get = nv40_pm_clocks_get;
		pm->clocks_pre = nv40_pm_clocks_pre;
		pm->clocks_set = nv40_pm_clocks_set;
		pm->voltage_get = nouveau_voltage_gpio_get;
		pm->voltage_set = nouveau_voltage_gpio_set;
	} else
	if (device->card_type < NV_C0) {
		if (device->chipset <  0xa3 ||
		    device->chipset == 0xaa ||
		    device->chipset == 0xac) {
			pm->clocks_get = nv50_pm_clocks_get;
			pm->clocks_pre = nv50_pm_clocks_pre;
			pm->clocks_set = nv50_pm_clocks_set;
		} else {
			pm->clocks_get = nva3_pm_clocks_get;
			pm->clocks_pre = nva3_pm_clocks_pre;
			pm->clocks_set = nva3_pm_clocks_set;
		}
		pm->voltage_get = nouveau_voltage_gpio_get;
		pm->voltage_set = nouveau_voltage_gpio_set;
	} else
	if (device->card_type < NV_E0) {
		pm->clocks_get = nvc0_pm_clocks_get;
		pm->clocks_pre = nvc0_pm_clocks_pre;
		pm->clocks_set = nvc0_pm_clocks_set;
		pm->voltage_get = nouveau_voltage_gpio_get;
		pm->voltage_set = nouveau_voltage_gpio_set;
	}


	/* parse aux tables from vbios */
	nouveau_volt_init(dev);

	INIT_LIST_HEAD(&pm->profiles);

	/* determine current ("boot") performance level */
	ret = nouveau_pm_perflvl_get(dev, &pm->boot);
	if (ret) {
		NV_ERROR(drm, "failed to determine boot perflvl\n");
		return ret;
	}

	strncpy(pm->boot.name, "boot", 4);
	strncpy(pm->boot.profile.name, "boot", 4);
	pm->boot.profile.func = &nouveau_pm_static_profile_func;

	list_add(&pm->boot.profile.head, &pm->profiles);

	pm->profile_ac = &pm->boot.profile;
	pm->profile_dc = &pm->boot.profile;
	pm->profile = &pm->boot.profile;
	pm->cur = &pm->boot;

	/* add performance levels from vbios */
	nouveau_perf_init(dev);

	/* display available performance levels */
	NV_INFO(drm, "%d available performance level(s)\n", pm->nr_perflvl);
	for (i = 0; i < pm->nr_perflvl; i++) {
		nouveau_pm_perflvl_info(&pm->perflvl[i], info, sizeof(info));
		NV_INFO(drm, "%d:%s", pm->perflvl[i].id, info);
	}

	nouveau_pm_perflvl_info(&pm->boot, info, sizeof(info));
	NV_INFO(drm, "c:%s", info);

	/* switch performance levels now if requested */
	if (nouveau_perflvl != NULL)
		nouveau_pm_profile_set(dev, nouveau_perflvl);

	nouveau_sysfs_init(dev);
	nouveau_hwmon_init(dev);
#if defined(CONFIG_ACPI) && defined(CONFIG_POWER_SUPPLY)
	pm->acpi_nb.notifier_call = nouveau_pm_acpi_event;
	register_acpi_notifier(&pm->acpi_nb);
#endif

	return 0;
}

void
nouveau_pm_fini(struct drm_device *dev)
{
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct nouveau_pm_profile *profile, *tmp;

	list_for_each_entry_safe(profile, tmp, &pm->profiles, head) {
		list_del(&profile->head);
		profile->func->destroy(profile);
	}

	if (pm->cur != &pm->boot)
		nouveau_pm_perflvl_set(dev, &pm->boot);

	nouveau_perf_fini(dev);
	nouveau_volt_fini(dev);

#if defined(CONFIG_ACPI) && defined(CONFIG_POWER_SUPPLY)
	unregister_acpi_notifier(&pm->acpi_nb);
#endif
	nouveau_hwmon_fini(dev);
	nouveau_sysfs_fini(dev);

	nouveau_drm(dev)->pm = NULL;
	kfree(pm);
}

void
nouveau_pm_resume(struct drm_device *dev)
{
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct nouveau_pm_level *perflvl;

	if (!pm->cur || pm->cur == &pm->boot)
		return;

	perflvl = pm->cur;
	pm->cur = &pm->boot;
	nouveau_pm_perflvl_set(dev, perflvl);
}
