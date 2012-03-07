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

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_pm.h"
#include "nouveau_gpio.h"

#ifdef CONFIG_ACPI
#include <linux/acpi.h>
#endif
#include <linux/power_supply.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

static int
nouveau_pwmfan_get(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct gpio_func gpio;
	u32 divs, duty;
	int ret;

	if (!pm->pwm_get)
		return -ENODEV;

	ret = nouveau_gpio_find(dev, 0, DCB_GPIO_PWM_FAN, 0xff, &gpio);
	if (ret == 0) {
		ret = pm->pwm_get(dev, gpio.line, &divs, &duty);
		if (ret == 0 && divs) {
			divs = max(divs, duty);
			if (dev_priv->card_type <= NV_40 || (gpio.log[0] & 1))
				duty = divs - duty;
			return (duty * 100) / divs;
		}

		return nouveau_gpio_func_get(dev, gpio.func) * 100;
	}

	return -ENODEV;
}

static int
nouveau_pwmfan_set(struct drm_device *dev, int percent)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct gpio_func gpio;
	u32 divs, duty;
	int ret;

	if (!pm->pwm_set)
		return -ENODEV;

	ret = nouveau_gpio_find(dev, 0, DCB_GPIO_PWM_FAN, 0xff, &gpio);
	if (ret == 0) {
		divs = pm->fan.pwm_divisor;
		if (pm->fan.pwm_freq) {
			/*XXX: PNVIO clock more than likely... */
			divs = 135000 / pm->fan.pwm_freq;
			if (dev_priv->chipset < 0xa3)
				divs /= 4;
		}

		duty = ((divs * percent) + 99) / 100;
		if (dev_priv->card_type <= NV_40 || (gpio.log[0] & 1))
			duty = divs - duty;

		ret = pm->pwm_set(dev, gpio.line, divs, duty);
		if (!ret)
			pm->fan.percent = percent;
		return ret;
	}

	return -ENODEV;
}

static int
nouveau_pm_perflvl_aux(struct drm_device *dev, struct nouveau_pm_level *perflvl,
		       struct nouveau_pm_level *a, struct nouveau_pm_level *b)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	int ret;

	/*XXX: not on all boards, we should control based on temperature
	 *     on recent boards..  or maybe on some other factor we don't
	 *     know about?
	 */
	if (a->fanspeed && b->fanspeed && b->fanspeed > a->fanspeed) {
		ret = nouveau_pwmfan_set(dev, perflvl->fanspeed);
		if (ret && ret != -ENODEV) {
			NV_ERROR(dev, "fanspeed set failed: %d\n", ret);
			return ret;
		}
	}

	if (pm->voltage.supported && pm->voltage_set) {
		if (perflvl->volt_min && b->volt_min > a->volt_min) {
			ret = pm->voltage_set(dev, perflvl->volt_min);
			if (ret) {
				NV_ERROR(dev, "voltage set failed: %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

static int
nouveau_pm_perflvl_set(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
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
		struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
		u64 time0 = ptimer->read(dev);

		NV_INFO(dev, "setting performance level: %d", perflvl->id);
		ret = nouveau_pm_perflvl_set(dev, perflvl);
		if (ret)
			NV_INFO(dev, "> reclocking failed: %d\n\n", ret);

		NV_INFO(dev, "> reclocking took %lluns\n\n",
			     ptimer->read(dev) - time0);
	}
}

static struct nouveau_pm_profile *
profile_find(struct drm_device *dev, const char *string)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct nouveau_pm_profile *ac = NULL, *dc = NULL;
	char string[16], *cur = string, *ptr;

	/* safety precaution, for now */
	if (nouveau_perflvl_wr != 7777)
		return -EPERM;

	strncpy(string, profile, sizeof(string));
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
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

	ret = nouveau_pwmfan_get(dev);
	if (ret > 0)
		perflvl->fanspeed = ret;

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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
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
			NV_ERROR(dev, "failed pervlvl %d sysfs: %d\n",
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;

	return snprintf(buf, PAGE_SIZE, "%d\n", pm->temp_get(dev)*1000);
}
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, nouveau_hwmon_show_temp,
						  NULL, 0);

static ssize_t
nouveau_hwmon_max_temp(struct device *d, struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct nouveau_pm_threshold_temp *temp = &pm->threshold_temp;

	return snprintf(buf, PAGE_SIZE, "%d\n", temp->down_clock*1000);
}
static ssize_t
nouveau_hwmon_set_max_temp(struct device *d, struct device_attribute *a,
						const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct nouveau_pm_threshold_temp *temp = &pm->threshold_temp;
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	temp->down_clock = value/1000;

	nouveau_temp_safety_checks(dev);

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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct nouveau_pm_threshold_temp *temp = &pm->threshold_temp;

	return snprintf(buf, PAGE_SIZE, "%d\n", temp->critical*1000);
}
static ssize_t
nouveau_hwmon_set_critical_temp(struct device *d, struct device_attribute *a,
							    const char *buf,
								size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct nouveau_pm_threshold_temp *temp = &pm->threshold_temp;
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	temp->critical = value/1000;

	nouveau_temp_safety_checks(dev);

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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
	struct gpio_func gpio;
	u32 cycles, cur, prev;
	u64 start;
	int ret;

	ret = nouveau_gpio_find(dev, 0, DCB_GPIO_FAN_SENSE, 0xff, &gpio);
	if (ret)
		return ret;

	/* Monitor the GPIO input 0x3b for 250ms.
	 * When the fan spins, it changes the value of GPIO FAN_SENSE.
	 * We get 4 changes (0 -> 1 -> 0 -> 1 -> [...]) per complete rotation.
	 */
	start = ptimer->read(dev);
	prev = nouveau_gpio_sense(dev, 0, gpio.line);
	cycles = 0;
	do {
		cur = nouveau_gpio_sense(dev, 0, gpio.line);
		if (prev != cur) {
			cycles++;
			prev = cur;
		}

		usleep_range(500, 1000); /* supports 0 < rpm < 7500 */
	} while (ptimer->read(dev) - start < 250000000);

	/* interpolate to get rpm */
	return sprintf(buf, "%i\n", cycles / 4 * 4 * 60);
}
static SENSOR_DEVICE_ATTR(fan0_input, S_IRUGO, nouveau_hwmon_show_fan0_input,
			  NULL, 0);

static ssize_t
nouveau_hwmon_get_pwm0(struct device *d, struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	int ret;

	ret = nouveau_pwmfan_get(dev);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
nouveau_hwmon_set_pwm0(struct device *d, struct device_attribute *a,
		       const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	int ret = -ENODEV;
	long value;

	if (nouveau_perflvl_wr != 7777)
		return -EPERM;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;

	if (value < pm->fan.min_duty)
		value = pm->fan.min_duty;
	if (value > pm->fan.max_duty)
		value = pm->fan.max_duty;

	ret = nouveau_pwmfan_set(dev, value);
	if (ret)
		return ret;

	return count;
}

static SENSOR_DEVICE_ATTR(pwm0, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm0,
			  nouveau_hwmon_set_pwm0, 0);

static ssize_t
nouveau_hwmon_get_pwm0_min(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;

	return sprintf(buf, "%i\n", pm->fan.min_duty);
}

static ssize_t
nouveau_hwmon_set_pwm0_min(struct device *d, struct device_attribute *a,
			   const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;

	if (value < 0)
		value = 0;

	if (pm->fan.max_duty - value < 10)
		value = pm->fan.max_duty - 10;

	if (value < 10)
		pm->fan.min_duty = 10;
	else
		pm->fan.min_duty = value;

	return count;
}

static SENSOR_DEVICE_ATTR(pwm0_min, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm0_min,
			  nouveau_hwmon_set_pwm0_min, 0);

static ssize_t
nouveau_hwmon_get_pwm0_max(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;

	return sprintf(buf, "%i\n", pm->fan.max_duty);
}

static ssize_t
nouveau_hwmon_set_pwm0_max(struct device *d, struct device_attribute *a,
			   const char *buf, size_t count)
{
	struct drm_device *dev = dev_get_drvdata(d);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;

	if (value < 0)
		value = 0;

	if (value - pm->fan.min_duty < 10)
		value = pm->fan.min_duty + 10;

	if (value > 100)
		pm->fan.max_duty = 100;
	else
		pm->fan.max_duty = value;

	return count;
}

static SENSOR_DEVICE_ATTR(pwm0_max, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm0_max,
			  nouveau_hwmon_set_pwm0_max, 0);

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
	&sensor_dev_attr_pwm0.dev_attr.attr,
	&sensor_dev_attr_pwm0_min.dev_attr.attr,
	&sensor_dev_attr_pwm0_max.dev_attr.attr,
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))
	struct device *hwmon_dev;
	int ret = 0;

	if (!pm->temp_get)
		return -ENODEV;

	hwmon_dev = hwmon_device_register(&dev->pdev->dev);
	if (IS_ERR(hwmon_dev)) {
		ret = PTR_ERR(hwmon_dev);
		NV_ERROR(dev,
			"Unable to register hwmon device: %d\n", ret);
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
	if (nouveau_pwmfan_get(dev) >= 0) {
		ret = sysfs_create_group(&dev->pdev->dev.kobj,
					 &hwmon_pwm_fan_attrgroup);
		if (ret)
			goto error;
	}

	/* if the card can read the fan rpm */
	if (nouveau_gpio_func_valid(dev, DCB_GPIO_FAN_SENSE)) {
		ret = sysfs_create_group(&dev->pdev->dev.kobj,
					 &hwmon_fan_rpm_attrgroup);
		if (ret)
			goto error;
	}

	pm->hwmon = hwmon_dev;

	return 0;

error:
	NV_ERROR(dev, "Unable to create some hwmon sysfs files: %d\n", ret);
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;

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
	struct drm_nouveau_private *dev_priv =
		container_of(nb, struct drm_nouveau_private, engine.pm.acpi_nb);
	struct drm_device *dev = dev_priv->dev;
	struct acpi_bus_event *entry = (struct acpi_bus_event *)data;

	if (strcmp(entry->device_class, "ac_adapter") == 0) {
		bool ac = power_supply_is_system_supplied();

		NV_DEBUG(dev, "power supply changed: %s\n", ac ? "AC" : "DC");
		nouveau_pm_trigger(dev);
	}

	return NOTIFY_OK;
}
#endif

int
nouveau_pm_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	char info[256];
	int ret, i;

	/* parse aux tables from vbios */
	nouveau_volt_init(dev);
	nouveau_temp_init(dev);

	/* determine current ("boot") performance level */
	ret = nouveau_pm_perflvl_get(dev, &pm->boot);
	if (ret) {
		NV_ERROR(dev, "failed to determine boot perflvl\n");
		return ret;
	}

	strncpy(pm->boot.name, "boot", 4);
	strncpy(pm->boot.profile.name, "boot", 4);
	pm->boot.profile.func = &nouveau_pm_static_profile_func;

	INIT_LIST_HEAD(&pm->profiles);
	list_add(&pm->boot.profile.head, &pm->profiles);

	pm->profile_ac = &pm->boot.profile;
	pm->profile_dc = &pm->boot.profile;
	pm->profile = &pm->boot.profile;
	pm->cur = &pm->boot;

	/* add performance levels from vbios */
	nouveau_perf_init(dev);

	/* display available performance levels */
	NV_INFO(dev, "%d available performance level(s)\n", pm->nr_perflvl);
	for (i = 0; i < pm->nr_perflvl; i++) {
		nouveau_pm_perflvl_info(&pm->perflvl[i], info, sizeof(info));
		NV_INFO(dev, "%d:%s", pm->perflvl[i].id, info);
	}

	nouveau_pm_perflvl_info(&pm->boot, info, sizeof(info));
	NV_INFO(dev, "c:%s", info);

	/* switch performance levels now if requested */
	if (nouveau_perflvl != NULL)
		nouveau_pm_profile_set(dev, nouveau_perflvl);

	/* determine the current fan speed */
	pm->fan.percent = nouveau_pwmfan_get(dev);

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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct nouveau_pm_profile *profile, *tmp;

	list_for_each_entry_safe(profile, tmp, &pm->profiles, head) {
		list_del(&profile->head);
		profile->func->destroy(profile);
	}

	if (pm->cur != &pm->boot)
		nouveau_pm_perflvl_set(dev, &pm->boot);

	nouveau_temp_fini(dev);
	nouveau_perf_fini(dev);
	nouveau_volt_fini(dev);

#if defined(CONFIG_ACPI) && defined(CONFIG_POWER_SUPPLY)
	unregister_acpi_notifier(&pm->acpi_nb);
#endif
	nouveau_hwmon_fini(dev);
	nouveau_sysfs_fini(dev);
}

void
nouveau_pm_resume(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct nouveau_pm_level *perflvl;

	if (!pm->cur || pm->cur == &pm->boot)
		return;

	perflvl = pm->cur;
	pm->cur = &pm->boot;
	nouveau_pm_perflvl_set(dev, perflvl);
	nouveau_pwmfan_set(dev, pm->fan.percent);
}
