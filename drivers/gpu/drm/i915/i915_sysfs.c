/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Ben Widawsky <ben@bwidawsk.net>
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include "i915_drv.h"

static u32 calc_residency(struct drm_device *dev, const u32 reg)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u64 raw_time; /* 32b value may overflow during fixed point math */

	if (!intel_enable_rc6(dev))
		return 0;

	raw_time = I915_READ(reg) * 128ULL;
	return DIV_ROUND_UP_ULL(raw_time, 100000);
}

static ssize_t
show_rc6_mask(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_minor *dminor = container_of(dev, struct drm_minor, kdev);
	return snprintf(buf, PAGE_SIZE, "%x", intel_enable_rc6(dminor->dev));
}

static ssize_t
show_rc6_ms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_minor *dminor = container_of(dev, struct drm_minor, kdev);
	u32 rc6_residency = calc_residency(dminor->dev, GEN6_GT_GFX_RC6);
	return snprintf(buf, PAGE_SIZE, "%u", rc6_residency);
}

static ssize_t
show_rc6p_ms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_minor *dminor = container_of(dev, struct drm_minor, kdev);
	u32 rc6p_residency = calc_residency(dminor->dev, GEN6_GT_GFX_RC6p);
	return snprintf(buf, PAGE_SIZE, "%u", rc6p_residency);
}

static ssize_t
show_rc6pp_ms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct drm_minor *dminor = container_of(dev, struct drm_minor, kdev);
	u32 rc6pp_residency = calc_residency(dminor->dev, GEN6_GT_GFX_RC6pp);
	return snprintf(buf, PAGE_SIZE, "%u", rc6pp_residency);
}

static DEVICE_ATTR(rc6_enable, S_IRUGO, show_rc6_mask, NULL);
static DEVICE_ATTR(rc6_residency_ms, S_IRUGO, show_rc6_ms, NULL);
static DEVICE_ATTR(rc6p_residency_ms, S_IRUGO, show_rc6p_ms, NULL);
static DEVICE_ATTR(rc6pp_residency_ms, S_IRUGO, show_rc6pp_ms, NULL);

static struct attribute *rc6_attrs[] = {
	&dev_attr_rc6_enable.attr,
	&dev_attr_rc6_residency_ms.attr,
	&dev_attr_rc6p_residency_ms.attr,
	&dev_attr_rc6pp_residency_ms.attr,
	NULL
};

static struct attribute_group rc6_attr_group = {
	.name = power_group_name,
	.attrs =  rc6_attrs
};

void i915_setup_sysfs(struct drm_device *dev)
{
	int ret;

	/* ILK doesn't have any residency information */
	if (INTEL_INFO(dev)->gen < 6)
		return;

	ret = sysfs_merge_group(&dev->primary->kdev.kobj, &rc6_attr_group);
	if (ret)
		DRM_ERROR("sysfs setup failed\n");
}

void i915_teardown_sysfs(struct drm_device *dev)
{
	sysfs_unmerge_group(&dev->primary->kdev.kobj, &rc6_attr_group);
}
