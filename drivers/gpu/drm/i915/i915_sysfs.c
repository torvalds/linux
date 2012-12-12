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
#include "intel_drv.h"
#include "i915_drv.h"

#ifdef CONFIG_PM
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
show_rc6_mask(struct device *kdev, struct device_attribute *attr, char *buf)
{
	struct drm_minor *dminor = container_of(kdev, struct drm_minor, kdev);
	return snprintf(buf, PAGE_SIZE, "%x", intel_enable_rc6(dminor->dev));
}

static ssize_t
show_rc6_ms(struct device *kdev, struct device_attribute *attr, char *buf)
{
	struct drm_minor *dminor = container_of(kdev, struct drm_minor, kdev);
	u32 rc6_residency = calc_residency(dminor->dev, GEN6_GT_GFX_RC6);
	return snprintf(buf, PAGE_SIZE, "%u", rc6_residency);
}

static ssize_t
show_rc6p_ms(struct device *kdev, struct device_attribute *attr, char *buf)
{
	struct drm_minor *dminor = container_of(kdev, struct drm_minor, kdev);
	u32 rc6p_residency = calc_residency(dminor->dev, GEN6_GT_GFX_RC6p);
	return snprintf(buf, PAGE_SIZE, "%u", rc6p_residency);
}

static ssize_t
show_rc6pp_ms(struct device *kdev, struct device_attribute *attr, char *buf)
{
	struct drm_minor *dminor = container_of(kdev, struct drm_minor, kdev);
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
#endif

static int l3_access_valid(struct drm_device *dev, loff_t offset)
{
	if (!IS_IVYBRIDGE(dev))
		return -EPERM;

	if (offset % 4 != 0)
		return -EINVAL;

	if (offset >= GEN7_L3LOG_SIZE)
		return -ENXIO;

	return 0;
}

static ssize_t
i915_l3_read(struct file *filp, struct kobject *kobj,
	     struct bin_attribute *attr, char *buf,
	     loff_t offset, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct drm_minor *dminor = container_of(dev, struct drm_minor, kdev);
	struct drm_device *drm_dev = dminor->dev;
	struct drm_i915_private *dev_priv = drm_dev->dev_private;
	uint32_t misccpctl;
	int i, ret;

	ret = l3_access_valid(drm_dev, offset);
	if (ret)
		return ret;

	ret = i915_mutex_lock_interruptible(drm_dev);
	if (ret)
		return ret;

	misccpctl = I915_READ(GEN7_MISCCPCTL);
	I915_WRITE(GEN7_MISCCPCTL, misccpctl & ~GEN7_DOP_CLOCK_GATE_ENABLE);

	for (i = offset; count >= 4 && i < GEN7_L3LOG_SIZE; i += 4, count -= 4)
		*((uint32_t *)(&buf[i])) = I915_READ(GEN7_L3LOG_BASE + i);

	I915_WRITE(GEN7_MISCCPCTL, misccpctl);

	mutex_unlock(&drm_dev->struct_mutex);

	return i - offset;
}

static ssize_t
i915_l3_write(struct file *filp, struct kobject *kobj,
	      struct bin_attribute *attr, char *buf,
	      loff_t offset, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct drm_minor *dminor = container_of(dev, struct drm_minor, kdev);
	struct drm_device *drm_dev = dminor->dev;
	struct drm_i915_private *dev_priv = drm_dev->dev_private;
	u32 *temp = NULL; /* Just here to make handling failures easy */
	int ret;

	ret = l3_access_valid(drm_dev, offset);
	if (ret)
		return ret;

	ret = i915_mutex_lock_interruptible(drm_dev);
	if (ret)
		return ret;

	if (!dev_priv->mm.l3_remap_info) {
		temp = kzalloc(GEN7_L3LOG_SIZE, GFP_KERNEL);
		if (!temp) {
			mutex_unlock(&drm_dev->struct_mutex);
			return -ENOMEM;
		}
	}

	ret = i915_gpu_idle(drm_dev);
	if (ret) {
		kfree(temp);
		mutex_unlock(&drm_dev->struct_mutex);
		return ret;
	}

	/* TODO: Ideally we really want a GPU reset here to make sure errors
	 * aren't propagated. Since I cannot find a stable way to reset the GPU
	 * at this point it is left as a TODO.
	*/
	if (temp)
		dev_priv->mm.l3_remap_info = temp;

	memcpy(dev_priv->mm.l3_remap_info + (offset/4),
	       buf + (offset/4),
	       count);

	i915_gem_l3_remap(drm_dev);

	mutex_unlock(&drm_dev->struct_mutex);

	return count;
}

static struct bin_attribute dpf_attrs = {
	.attr = {.name = "l3_parity", .mode = (S_IRUSR | S_IWUSR)},
	.size = GEN7_L3LOG_SIZE,
	.read = i915_l3_read,
	.write = i915_l3_write,
	.mmap = NULL
};

static ssize_t gt_cur_freq_mhz_show(struct device *kdev,
				    struct device_attribute *attr, char *buf)
{
	struct drm_minor *minor = container_of(kdev, struct drm_minor, kdev);
	struct drm_device *dev = minor->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ret = dev_priv->rps.cur_delay * GT_FREQUENCY_MULTIPLIER;
	mutex_unlock(&dev->struct_mutex);

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t gt_max_freq_mhz_show(struct device *kdev, struct device_attribute *attr, char *buf)
{
	struct drm_minor *minor = container_of(kdev, struct drm_minor, kdev);
	struct drm_device *dev = minor->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ret = dev_priv->rps.max_delay * GT_FREQUENCY_MULTIPLIER;
	mutex_unlock(&dev->struct_mutex);

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t gt_max_freq_mhz_store(struct device *kdev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct drm_minor *minor = container_of(kdev, struct drm_minor, kdev);
	struct drm_device *dev = minor->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val, rp_state_cap, hw_max, hw_min;
	ssize_t ret;

	ret = kstrtou32(buf, 0, &val);
	if (ret)
		return ret;

	val /= GT_FREQUENCY_MULTIPLIER;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	rp_state_cap = I915_READ(GEN6_RP_STATE_CAP);
	hw_max = (rp_state_cap & 0xff);
	hw_min = ((rp_state_cap & 0xff0000) >> 16);

	if (val < hw_min || val > hw_max || val < dev_priv->rps.min_delay) {
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	if (dev_priv->rps.cur_delay > val)
		gen6_set_rps(dev_priv->dev, val);

	dev_priv->rps.max_delay = val;

	mutex_unlock(&dev->struct_mutex);

	return count;
}

static ssize_t gt_min_freq_mhz_show(struct device *kdev, struct device_attribute *attr, char *buf)
{
	struct drm_minor *minor = container_of(kdev, struct drm_minor, kdev);
	struct drm_device *dev = minor->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ret = dev_priv->rps.min_delay * GT_FREQUENCY_MULTIPLIER;
	mutex_unlock(&dev->struct_mutex);

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t gt_min_freq_mhz_store(struct device *kdev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct drm_minor *minor = container_of(kdev, struct drm_minor, kdev);
	struct drm_device *dev = minor->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val, rp_state_cap, hw_max, hw_min;
	ssize_t ret;

	ret = kstrtou32(buf, 0, &val);
	if (ret)
		return ret;

	val /= GT_FREQUENCY_MULTIPLIER;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	rp_state_cap = I915_READ(GEN6_RP_STATE_CAP);
	hw_max = (rp_state_cap & 0xff);
	hw_min = ((rp_state_cap & 0xff0000) >> 16);

	if (val < hw_min || val > hw_max || val > dev_priv->rps.max_delay) {
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	if (dev_priv->rps.cur_delay < val)
		gen6_set_rps(dev_priv->dev, val);

	dev_priv->rps.min_delay = val;

	mutex_unlock(&dev->struct_mutex);

	return count;

}

static DEVICE_ATTR(gt_cur_freq_mhz, S_IRUGO, gt_cur_freq_mhz_show, NULL);
static DEVICE_ATTR(gt_max_freq_mhz, S_IRUGO | S_IWUSR, gt_max_freq_mhz_show, gt_max_freq_mhz_store);
static DEVICE_ATTR(gt_min_freq_mhz, S_IRUGO | S_IWUSR, gt_min_freq_mhz_show, gt_min_freq_mhz_store);


static ssize_t gt_rp_mhz_show(struct device *kdev, struct device_attribute *attr, char *buf);
static DEVICE_ATTR(gt_RP0_freq_mhz, S_IRUGO, gt_rp_mhz_show, NULL);
static DEVICE_ATTR(gt_RP1_freq_mhz, S_IRUGO, gt_rp_mhz_show, NULL);
static DEVICE_ATTR(gt_RPn_freq_mhz, S_IRUGO, gt_rp_mhz_show, NULL);

/* For now we have a static number of RP states */
static ssize_t gt_rp_mhz_show(struct device *kdev, struct device_attribute *attr, char *buf)
{
	struct drm_minor *minor = container_of(kdev, struct drm_minor, kdev);
	struct drm_device *dev = minor->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val, rp_state_cap;
	ssize_t ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;
	rp_state_cap = I915_READ(GEN6_RP_STATE_CAP);
	mutex_unlock(&dev->struct_mutex);

	if (attr == &dev_attr_gt_RP0_freq_mhz) {
		val = ((rp_state_cap & 0x0000ff) >> 0) * GT_FREQUENCY_MULTIPLIER;
	} else if (attr == &dev_attr_gt_RP1_freq_mhz) {
		val = ((rp_state_cap & 0x00ff00) >> 8) * GT_FREQUENCY_MULTIPLIER;
	} else if (attr == &dev_attr_gt_RPn_freq_mhz) {
		val = ((rp_state_cap & 0xff0000) >> 16) * GT_FREQUENCY_MULTIPLIER;
	} else {
		BUG();
	}
	return snprintf(buf, PAGE_SIZE, "%d", val);
}

static const struct attribute *gen6_attrs[] = {
	&dev_attr_gt_cur_freq_mhz.attr,
	&dev_attr_gt_max_freq_mhz.attr,
	&dev_attr_gt_min_freq_mhz.attr,
	&dev_attr_gt_RP0_freq_mhz.attr,
	&dev_attr_gt_RP1_freq_mhz.attr,
	&dev_attr_gt_RPn_freq_mhz.attr,
	NULL,
};

void i915_setup_sysfs(struct drm_device *dev)
{
	int ret;

#ifdef CONFIG_PM
	if (INTEL_INFO(dev)->gen >= 6) {
		ret = sysfs_merge_group(&dev->primary->kdev.kobj,
					&rc6_attr_group);
		if (ret)
			DRM_ERROR("RC6 residency sysfs setup failed\n");
	}
#endif
	if (HAS_L3_GPU_CACHE(dev)) {
		ret = device_create_bin_file(&dev->primary->kdev, &dpf_attrs);
		if (ret)
			DRM_ERROR("l3 parity sysfs setup failed\n");
	}

	if (INTEL_INFO(dev)->gen >= 6) {
		ret = sysfs_create_files(&dev->primary->kdev.kobj, gen6_attrs);
		if (ret)
			DRM_ERROR("gen6 sysfs setup failed\n");
	}
}

void i915_teardown_sysfs(struct drm_device *dev)
{
	sysfs_remove_files(&dev->primary->kdev.kobj, gen6_attrs);
	device_remove_bin_file(&dev->primary->kdev,  &dpf_attrs);
#ifdef CONFIG_PM
	sysfs_unmerge_group(&dev->primary->kdev.kobj, &rc6_attr_group);
#endif
}
