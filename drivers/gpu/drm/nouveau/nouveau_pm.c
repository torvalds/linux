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

#ifdef CONFIG_ACPI
#include <linux/acpi.h>
#endif
#include <linux/power_supply.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

static int
nouveau_pm_clock_set(struct drm_device *dev, struct nouveau_pm_level *perflvl,
		     u8 id, u32 khz)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	void *pre_state;

	if (khz == 0)
		return 0;

	pre_state = pm->clock_pre(dev, perflvl, id, khz);
	if (IS_ERR(pre_state))
		return PTR_ERR(pre_state);

	if (pre_state)
		pm->clock_set(dev, pre_state);
	return 0;
}

static int
nouveau_pm_perflvl_set(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	int ret;

	if (perflvl == pm->cur)
		return 0;

	if (pm->voltage.supported && pm->voltage_set && perflvl->voltage) {
		ret = pm->voltage_set(dev, perflvl->voltage);
		if (ret) {
			NV_ERROR(dev, "voltage_set %d failed: %d\n",
				 perflvl->voltage, ret);
		}
	}

	nouveau_pm_clock_set(dev, perflvl, PLL_CORE, perflvl->core);
	nouveau_pm_clock_set(dev, perflvl, PLL_SHADER, perflvl->shader);
	nouveau_pm_clock_set(dev, perflvl, PLL_MEMORY, perflvl->memory);
	nouveau_pm_clock_set(dev, perflvl, PLL_UNK05, perflvl->unk05);

	pm->cur = perflvl;
	return 0;
}

static int
nouveau_pm_profile_set(struct drm_device *dev, const char *profile)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct nouveau_pm_level *perflvl = NULL;

	/* safety precaution, for now */
	if (nouveau_perflvl_wr != 7777)
		return -EPERM;

	if (!pm->clock_set)
		return -EINVAL;

	if (!strncmp(profile, "boot", 4))
		perflvl = &pm->boot;
	else {
		int pl = simple_strtol(profile, NULL, 10);
		int i;

		for (i = 0; i < pm->nr_perflvl; i++) {
			if (pm->perflvl[i].id == pl) {
				perflvl = &pm->perflvl[i];
				break;
			}
		}

		if (!perflvl)
			return -EINVAL;
	}

	NV_INFO(dev, "setting performance level: %s\n", profile);
	return nouveau_pm_perflvl_set(dev, perflvl);
}

static int
nouveau_pm_perflvl_get(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	int ret;

	if (!pm->clock_get)
		return -EINVAL;

	memset(perflvl, 0, sizeof(*perflvl));

	ret = pm->clock_get(dev, PLL_CORE);
	if (ret > 0)
		perflvl->core = ret;

	ret = pm->clock_get(dev, PLL_MEMORY);
	if (ret > 0)
		perflvl->memory = ret;

	ret = pm->clock_get(dev, PLL_SHADER);
	if (ret > 0)
		perflvl->shader = ret;

	ret = pm->clock_get(dev, PLL_UNK05);
	if (ret > 0)
		perflvl->unk05 = ret;

	if (pm->voltage.supported && pm->voltage_get) {
		ret = pm->voltage_get(dev);
		if (ret > 0)
			perflvl->voltage = ret;
	}

	return 0;
}

static void
nouveau_pm_perflvl_info(struct nouveau_pm_level *perflvl, char *ptr, int len)
{
	char c[16], s[16], v[16], f[16];

	c[0] = '\0';
	if (perflvl->core)
		snprintf(c, sizeof(c), " core %dMHz", perflvl->core / 1000);

	s[0] = '\0';
	if (perflvl->shader)
		snprintf(s, sizeof(s), " shader %dMHz", perflvl->shader / 1000);

	v[0] = '\0';
	if (perflvl->voltage)
		snprintf(v, sizeof(v), " voltage %dmV", perflvl->voltage * 10);

	f[0] = '\0';
	if (perflvl->fanspeed)
		snprintf(f, sizeof(f), " fanspeed %d%%", perflvl->fanspeed);

	snprintf(ptr, len, "memory %dMHz%s%s%s%s\n", perflvl->memory / 1000,
		 c, s, v, f);
}

static ssize_t
nouveau_pm_get_perflvl_info(struct device *d,
			    struct device_attribute *a, char *buf)
{
	struct nouveau_pm_level *perflvl = (struct nouveau_pm_level *)a;
	char *ptr = buf;
	int len = PAGE_SIZE;

	snprintf(ptr, len, "%d: ", perflvl->id);
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

	if (!pm->cur)
		snprintf(ptr, len, "setting: boot\n");
	else if (pm->cur == &pm->boot)
		snprintf(ptr, len, "setting: boot\nc: ");
	else
		snprintf(ptr, len, "setting: static %d\nc: ", pm->cur->id);
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

#ifdef CONFIG_HWMON
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

	if (strict_strtol(buf, 10, &value) == -EINVAL)
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

	if (strict_strtol(buf, 10, &value) == -EINVAL)
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

static struct attribute *hwmon_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_name.dev_attr.attr,
	&sensor_dev_attr_update_rate.dev_attr.attr,
	NULL
};

static const struct attribute_group hwmon_attrgroup = {
	.attrs = hwmon_attributes,
};
#endif

static int
nouveau_hwmon_init(struct drm_device *dev)
{
#ifdef CONFIG_HWMON
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct device *hwmon_dev;
	int ret;

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
	ret = sysfs_create_group(&dev->pdev->dev.kobj, &hwmon_attrgroup);
	if (ret) {
		NV_ERROR(dev,
			"Unable to create hwmon sysfs file: %d\n", ret);
		hwmon_device_unregister(hwmon_dev);
		return ret;
	}

	pm->hwmon = hwmon_dev;
#endif
	return 0;
}

static void
nouveau_hwmon_fini(struct drm_device *dev)
{
#ifdef CONFIG_HWMON
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;

	if (pm->hwmon) {
		sysfs_remove_group(&dev->pdev->dev.kobj, &hwmon_attrgroup);
		hwmon_device_unregister(pm->hwmon);
	}
#endif
}

#ifdef CONFIG_ACPI
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

	nouveau_volt_init(dev);
	nouveau_perf_init(dev);
	nouveau_temp_init(dev);
	nouveau_mem_timing_init(dev);

	NV_INFO(dev, "%d available performance level(s)\n", pm->nr_perflvl);
	for (i = 0; i < pm->nr_perflvl; i++) {
		nouveau_pm_perflvl_info(&pm->perflvl[i], info, sizeof(info));
		NV_INFO(dev, "%d: %s", pm->perflvl[i].id, info);
	}

	/* determine current ("boot") performance level */
	ret = nouveau_pm_perflvl_get(dev, &pm->boot);
	if (ret == 0) {
		pm->cur = &pm->boot;

		nouveau_pm_perflvl_info(&pm->boot, info, sizeof(info));
		NV_INFO(dev, "c: %s", info);
	}

	/* switch performance levels now if requested */
	if (nouveau_perflvl != NULL) {
		ret = nouveau_pm_profile_set(dev, nouveau_perflvl);
		if (ret) {
			NV_ERROR(dev, "error setting perflvl \"%s\": %d\n",
				 nouveau_perflvl, ret);
		}
	}

	nouveau_sysfs_init(dev);
	nouveau_hwmon_init(dev);
#ifdef CONFIG_ACPI
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

	if (pm->cur != &pm->boot)
		nouveau_pm_perflvl_set(dev, &pm->boot);

	nouveau_mem_timing_fini(dev);
	nouveau_temp_fini(dev);
	nouveau_perf_fini(dev);
	nouveau_volt_fini(dev);

#ifdef CONFIG_ACPI
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
}
