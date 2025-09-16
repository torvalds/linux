// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013-2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifdef CONFIG_DEBUG_FS

#include <linux/debugfs.h>
#include <linux/fault-inject.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_framebuffer.h>

#include "msm_drv.h"
#include "msm_gpu.h"
#include "msm_kms.h"
#include "msm_debugfs.h"
#include "disp/msm_disp_snapshot.h"

/*
 * GPU Snapshot:
 */

struct msm_gpu_show_priv {
	struct msm_gpu_state *state;
	struct drm_device *dev;
};

static int msm_gpu_show(struct seq_file *m, void *arg)
{
	struct drm_printer p = drm_seq_file_printer(m);
	struct msm_gpu_show_priv *show_priv = m->private;
	struct msm_drm_private *priv = show_priv->dev->dev_private;
	struct msm_gpu *gpu = priv->gpu;
	int ret;

	ret = mutex_lock_interruptible(&gpu->lock);
	if (ret)
		return ret;

	drm_printf(&p, "%s Status:\n", gpu->name);
	gpu->funcs->show(gpu, show_priv->state, &p);

	mutex_unlock(&gpu->lock);

	return 0;
}

static int msm_gpu_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct msm_gpu_show_priv *show_priv = m->private;
	struct msm_drm_private *priv = show_priv->dev->dev_private;
	struct msm_gpu *gpu = priv->gpu;

	mutex_lock(&gpu->lock);
	gpu->funcs->gpu_state_put(show_priv->state);
	mutex_unlock(&gpu->lock);

	kfree(show_priv);

	return single_release(inode, file);
}

static int msm_gpu_open(struct inode *inode, struct file *file)
{
	struct drm_device *dev = inode->i_private;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gpu *gpu = priv->gpu;
	struct msm_gpu_show_priv *show_priv;
	int ret;

	if (!gpu || !gpu->funcs->gpu_state_get)
		return -ENODEV;

	show_priv = kmalloc(sizeof(*show_priv), GFP_KERNEL);
	if (!show_priv)
		return -ENOMEM;

	ret = mutex_lock_interruptible(&gpu->lock);
	if (ret)
		goto free_priv;

	pm_runtime_get_sync(&gpu->pdev->dev);
	msm_gpu_hw_init(gpu);
	show_priv->state = gpu->funcs->gpu_state_get(gpu);
	pm_runtime_put_sync(&gpu->pdev->dev);

	mutex_unlock(&gpu->lock);

	if (IS_ERR(show_priv->state)) {
		ret = PTR_ERR(show_priv->state);
		goto free_priv;
	}

	show_priv->dev = dev;

	ret = single_open(file, msm_gpu_show, show_priv);
	if (ret)
		goto free_priv;

	return 0;

free_priv:
	kfree(show_priv);
	return ret;
}

static const struct file_operations msm_gpu_fops = {
	.owner = THIS_MODULE,
	.open = msm_gpu_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = msm_gpu_release,
};

#ifdef CONFIG_DRM_MSM_KMS
static int msm_fb_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_framebuffer *fb, *fbdev_fb = NULL;

	if (dev->fb_helper && dev->fb_helper->fb) {
		seq_puts(m, "fbcon ");
		fbdev_fb = dev->fb_helper->fb;
		msm_framebuffer_describe(fbdev_fb, m);
	}

	mutex_lock(&dev->mode_config.fb_lock);
	list_for_each_entry(fb, &dev->mode_config.fb_list, head) {
		if (fb == fbdev_fb)
			continue;

		seq_puts(m, "user ");
		msm_framebuffer_describe(fb, m);
	}
	mutex_unlock(&dev->mode_config.fb_lock);

	return 0;
}

static struct drm_info_list msm_kms_debugfs_list[] = {
		{ "fb", msm_fb_show },
};

/*
 * Display Snapshot:
 */

static int msm_kms_show(struct seq_file *m, void *arg)
{
	struct drm_printer p = drm_seq_file_printer(m);
	struct msm_disp_state *state = m->private;

	msm_disp_state_print(state, &p);

	return 0;
}

static int msm_kms_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct msm_disp_state *state = m->private;

	msm_disp_state_free(state);

	return single_release(inode, file);
}

static int msm_kms_open(struct inode *inode, struct file *file)
{
	struct drm_device *dev = inode->i_private;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_disp_state *state;
	int ret;

	if (!priv->kms)
		return -ENODEV;

	ret = mutex_lock_interruptible(&priv->kms->dump_mutex);
	if (ret)
		return ret;

	state = msm_disp_snapshot_state_sync(priv->kms);

	mutex_unlock(&priv->kms->dump_mutex);

	if (IS_ERR(state)) {
		return PTR_ERR(state);
	}

	ret = single_open(file, msm_kms_show, state);
	if (ret) {
		msm_disp_state_free(state);
		return ret;
	}

	return 0;
}

static const struct file_operations msm_kms_fops = {
	.owner = THIS_MODULE,
	.open = msm_kms_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = msm_kms_release,
};

static void msm_debugfs_kms_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct msm_drm_private *priv = dev->dev_private;

	drm_debugfs_create_files(msm_kms_debugfs_list,
				 ARRAY_SIZE(msm_kms_debugfs_list),
				 minor->debugfs_root, minor);
	debugfs_create_file("kms", 0400, minor->debugfs_root,
			    dev, &msm_kms_fops);

	if (priv->kms->funcs->debugfs_init)
		priv->kms->funcs->debugfs_init(priv->kms, minor);

}
#else /* ! CONFIG_DRM_MSM_KMS */
static void msm_debugfs_kms_init(struct drm_minor *minor)
{
}
#endif

/*
 * Other debugfs:
 */

static unsigned long last_shrink_freed;

static int
shrink_get(void *data, u64 *val)
{
	*val = last_shrink_freed;

	return 0;
}

static int
shrink_set(void *data, u64 val)
{
	struct drm_device *dev = data;

	last_shrink_freed = msm_gem_shrinker_shrink(dev, val);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(shrink_fops,
			 shrink_get, shrink_set,
			 "0x%08llx\n");

/*
 * Return the number of microseconds to wait until stall-on-fault is
 * re-enabled. If 0 then it is already enabled or will be re-enabled on the
 * next submit (unless there's a leftover devcoredump). This is useful for
 * kernel tests that intentionally produce a fault and check the devcoredump to
 * wait until the cooldown period is over.
 */

static int
stall_reenable_time_get(void *data, u64 *val)
{
	struct msm_drm_private *priv = data;
	unsigned long irq_flags;

	spin_lock_irqsave(&priv->fault_stall_lock, irq_flags);

	if (priv->stall_enabled)
		*val = 0;
	else
		*val = max(ktime_us_delta(priv->stall_reenable_time, ktime_get()), 0);

	spin_unlock_irqrestore(&priv->fault_stall_lock, irq_flags);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(stall_reenable_time_fops,
			 stall_reenable_time_get, NULL,
			 "%lld\n");

static int msm_gem_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = m->private;
	struct drm_device *dev = node->minor->dev;
	struct msm_drm_private *priv = dev->dev_private;
	int ret;

	ret = mutex_lock_interruptible(&priv->obj_lock);
	if (ret)
		return ret;

	msm_gem_describe_objects(&priv->objects, m);

	mutex_unlock(&priv->obj_lock);

	return 0;
}

static int msm_mm_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_printer p = drm_seq_file_printer(m);

	drm_mm_print(&dev->vma_offset_manager->vm_addr_space_mm, &p);

	return 0;
}

static struct drm_info_list msm_debugfs_list[] = {
		{"gem", msm_gem_show},
		{ "mm", msm_mm_show },
};

static int late_init_minor(struct drm_minor *minor)
{
	struct drm_device *dev;
	struct msm_drm_private *priv;
	int ret;

	if (!minor)
		return 0;

	dev = minor->dev;
	priv = dev->dev_private;

	if (!priv->gpu_pdev)
		return 0;

	ret = msm_rd_debugfs_init(minor);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "could not install rd debugfs\n");
		return ret;
	}

	ret = msm_perf_debugfs_init(minor);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "could not install perf debugfs\n");
		return ret;
	}

	return 0;
}

int msm_debugfs_late_init(struct drm_device *dev)
{
	int ret;
	ret = late_init_minor(dev->primary);
	if (ret)
		return ret;
	ret = late_init_minor(dev->render);
	return ret;
}

static void msm_debugfs_gpu_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct dentry *gpu_devfreq;

	debugfs_create_file("gpu", S_IRUSR, minor->debugfs_root,
		dev, &msm_gpu_fops);

	debugfs_create_u32("hangcheck_period_ms", 0600, minor->debugfs_root,
		&priv->hangcheck_period);

	debugfs_create_bool("disable_err_irq", 0600, minor->debugfs_root,
		&priv->disable_err_irq);

	debugfs_create_file("stall_reenable_time_us", 0400, minor->debugfs_root,
		priv, &stall_reenable_time_fops);

	gpu_devfreq = debugfs_create_dir("devfreq", minor->debugfs_root);

	debugfs_create_bool("idle_clamp",0600, gpu_devfreq,
			    &priv->gpu_clamp_to_idle);

	debugfs_create_u32("upthreshold",0600, gpu_devfreq,
			   &priv->gpu_devfreq_config.upthreshold);

	debugfs_create_u32("downdifferential",0600, gpu_devfreq,
			   &priv->gpu_devfreq_config.downdifferential);
}

void msm_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct msm_drm_private *priv = dev->dev_private;

	drm_debugfs_create_files(msm_debugfs_list,
				 ARRAY_SIZE(msm_debugfs_list),
				 minor->debugfs_root, minor);

	if (priv->gpu_pdev)
		msm_debugfs_gpu_init(minor);

	if (priv->kms)
		msm_debugfs_kms_init(minor);

	debugfs_create_file("shrink", S_IRWXU, minor->debugfs_root,
		dev, &shrink_fops);

	fault_create_debugfs_attr("fail_gem_alloc", minor->debugfs_root,
				  &fail_gem_alloc);
	fault_create_debugfs_attr("fail_gem_iova", minor->debugfs_root,
				  &fail_gem_iova);
}
#endif

