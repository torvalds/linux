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
	char s[16], v[16], f[16];

	s[0] = '\0';
	if (perflvl->shader)
		snprintf(s, sizeof(s), " shader %dMHz", perflvl->shader / 1000);

	v[0] = '\0';
	if (perflvl->voltage)
		snprintf(v, sizeof(v), " voltage %dmV", perflvl->voltage * 10);

	f[0] = '\0';
	if (perflvl->fanspeed)
		snprintf(f, sizeof(f), " fanspeed %d%%", perflvl->fanspeed);

	snprintf(ptr, len, "core %dMHz memory %dMHz%s%s%s\n",
		 perflvl->core / 1000, perflvl->memory / 1000, s, v, f);
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
	return -EPERM;
}

DEVICE_ATTR(performance_level, S_IRUGO | S_IWUSR,
	    nouveau_pm_get_perflvl, nouveau_pm_set_perflvl);

int
nouveau_pm_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct device *d = &dev->pdev->dev;
	char info[256];
	int ret, i;

	nouveau_volt_init(dev);
	nouveau_perf_init(dev);

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

	/* initialise sysfs */
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

void
nouveau_pm_fini(struct drm_device *dev)
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

	nouveau_perf_fini(dev);
	nouveau_volt_fini(dev);
}

