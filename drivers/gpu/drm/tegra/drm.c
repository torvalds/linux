/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012-2013 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/host1x.h>

#include "drm.h"
#include "gem.h"

#define DRIVER_NAME "tegra"
#define DRIVER_DESC "NVIDIA Tegra graphics"
#define DRIVER_DATE "20120330"
#define DRIVER_MAJOR 0
#define DRIVER_MINOR 0
#define DRIVER_PATCHLEVEL 0

struct tegra_drm_file {
	struct list_head contexts;
};

static int tegra_drm_load(struct drm_device *drm, unsigned long flags)
{
	struct host1x_device *device = to_host1x_device(drm->dev);
	struct tegra_drm *tegra;
	int err;

	tegra = kzalloc(sizeof(*tegra), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	dev_set_drvdata(drm->dev, tegra);
	mutex_init(&tegra->clients_lock);
	INIT_LIST_HEAD(&tegra->clients);
	drm->dev_private = tegra;
	tegra->drm = drm;

	drm_mode_config_init(drm);

	err = host1x_device_init(device);
	if (err < 0)
		return err;

	/*
	 * We don't use the drm_irq_install() helpers provided by the DRM
	 * core, so we need to set this manually in order to allow the
	 * DRM_IOCTL_WAIT_VBLANK to operate correctly.
	 */
	drm->irq_enabled = true;

	err = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (err < 0)
		return err;

	err = tegra_drm_fb_init(drm);
	if (err < 0)
		return err;

	drm_kms_helper_poll_init(drm);

	return 0;
}

static int tegra_drm_unload(struct drm_device *drm)
{
	struct host1x_device *device = to_host1x_device(drm->dev);
	int err;

	drm_kms_helper_poll_fini(drm);
	tegra_drm_fb_exit(drm);
	drm_vblank_cleanup(drm);
	drm_mode_config_cleanup(drm);

	err = host1x_device_exit(device);
	if (err < 0)
		return err;

	return 0;
}

static int tegra_drm_open(struct drm_device *drm, struct drm_file *filp)
{
	struct tegra_drm_file *fpriv;

	fpriv = kzalloc(sizeof(*fpriv), GFP_KERNEL);
	if (!fpriv)
		return -ENOMEM;

	INIT_LIST_HEAD(&fpriv->contexts);
	filp->driver_priv = fpriv;

	return 0;
}

static void tegra_drm_context_free(struct tegra_drm_context *context)
{
	context->client->ops->close_channel(context);
	kfree(context);
}

static void tegra_drm_lastclose(struct drm_device *drm)
{
	struct tegra_drm *tegra = drm->dev_private;

	tegra_fbdev_restore_mode(tegra->fbdev);
}

static struct host1x_bo *
host1x_bo_lookup(struct drm_device *drm, struct drm_file *file, u32 handle)
{
	struct drm_gem_object *gem;
	struct tegra_bo *bo;

	gem = drm_gem_object_lookup(drm, file, handle);
	if (!gem)
		return NULL;

	mutex_lock(&drm->struct_mutex);
	drm_gem_object_unreference(gem);
	mutex_unlock(&drm->struct_mutex);

	bo = to_tegra_bo(gem);
	return &bo->base;
}

int tegra_drm_submit(struct tegra_drm_context *context,
		     struct drm_tegra_submit *args, struct drm_device *drm,
		     struct drm_file *file)
{
	unsigned int num_cmdbufs = args->num_cmdbufs;
	unsigned int num_relocs = args->num_relocs;
	unsigned int num_waitchks = args->num_waitchks;
	struct drm_tegra_cmdbuf __user *cmdbufs =
		(void * __user)(uintptr_t)args->cmdbufs;
	struct drm_tegra_reloc __user *relocs =
		(void * __user)(uintptr_t)args->relocs;
	struct drm_tegra_waitchk __user *waitchks =
		(void * __user)(uintptr_t)args->waitchks;
	struct drm_tegra_syncpt syncpt;
	struct host1x_job *job;
	int err;

	/* We don't yet support other than one syncpt_incr struct per submit */
	if (args->num_syncpts != 1)
		return -EINVAL;

	job = host1x_job_alloc(context->channel, args->num_cmdbufs,
			       args->num_relocs, args->num_waitchks);
	if (!job)
		return -ENOMEM;

	job->num_relocs = args->num_relocs;
	job->num_waitchk = args->num_waitchks;
	job->client = (u32)args->context;
	job->class = context->client->base.class;
	job->serialize = true;

	while (num_cmdbufs) {
		struct drm_tegra_cmdbuf cmdbuf;
		struct host1x_bo *bo;

		err = copy_from_user(&cmdbuf, cmdbufs, sizeof(cmdbuf));
		if (err)
			goto fail;

		bo = host1x_bo_lookup(drm, file, cmdbuf.handle);
		if (!bo) {
			err = -ENOENT;
			goto fail;
		}

		host1x_job_add_gather(job, bo, cmdbuf.words, cmdbuf.offset);
		num_cmdbufs--;
		cmdbufs++;
	}

	err = copy_from_user(job->relocarray, relocs,
			     sizeof(*relocs) * num_relocs);
	if (err)
		goto fail;

	while (num_relocs--) {
		struct host1x_reloc *reloc = &job->relocarray[num_relocs];
		struct host1x_bo *cmdbuf, *target;

		cmdbuf = host1x_bo_lookup(drm, file, (u32)reloc->cmdbuf);
		target = host1x_bo_lookup(drm, file, (u32)reloc->target);

		reloc->cmdbuf = cmdbuf;
		reloc->target = target;

		if (!reloc->target || !reloc->cmdbuf) {
			err = -ENOENT;
			goto fail;
		}
	}

	err = copy_from_user(job->waitchk, waitchks,
			     sizeof(*waitchks) * num_waitchks);
	if (err)
		goto fail;

	err = copy_from_user(&syncpt, (void * __user)(uintptr_t)args->syncpts,
			     sizeof(syncpt));
	if (err)
		goto fail;

	job->is_addr_reg = context->client->ops->is_addr_reg;
	job->syncpt_incrs = syncpt.incrs;
	job->syncpt_id = syncpt.id;
	job->timeout = 10000;

	if (args->timeout && args->timeout < 10000)
		job->timeout = args->timeout;

	err = host1x_job_pin(job, context->client->base.dev);
	if (err)
		goto fail;

	err = host1x_job_submit(job);
	if (err)
		goto fail_submit;

	args->fence = job->syncpt_end;

	host1x_job_put(job);
	return 0;

fail_submit:
	host1x_job_unpin(job);
fail:
	host1x_job_put(job);
	return err;
}


#ifdef CONFIG_DRM_TEGRA_STAGING
static struct tegra_drm_context *tegra_drm_get_context(__u64 context)
{
	return (struct tegra_drm_context *)(uintptr_t)context;
}

static bool tegra_drm_file_owns_context(struct tegra_drm_file *file,
					struct tegra_drm_context *context)
{
	struct tegra_drm_context *ctx;

	list_for_each_entry(ctx, &file->contexts, list)
		if (ctx == context)
			return true;

	return false;
}

static int tegra_gem_create(struct drm_device *drm, void *data,
			    struct drm_file *file)
{
	struct drm_tegra_gem_create *args = data;
	struct tegra_bo *bo;

	bo = tegra_bo_create_with_handle(file, drm, args->size, args->flags,
					 &args->handle);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	return 0;
}

static int tegra_gem_mmap(struct drm_device *drm, void *data,
			  struct drm_file *file)
{
	struct drm_tegra_gem_mmap *args = data;
	struct drm_gem_object *gem;
	struct tegra_bo *bo;

	gem = drm_gem_object_lookup(drm, file, args->handle);
	if (!gem)
		return -EINVAL;

	bo = to_tegra_bo(gem);

	args->offset = drm_vma_node_offset_addr(&bo->gem.vma_node);

	drm_gem_object_unreference(gem);

	return 0;
}

static int tegra_syncpt_read(struct drm_device *drm, void *data,
			     struct drm_file *file)
{
	struct host1x *host = dev_get_drvdata(drm->dev->parent);
	struct drm_tegra_syncpt_read *args = data;
	struct host1x_syncpt *sp;

	sp = host1x_syncpt_get(host, args->id);
	if (!sp)
		return -EINVAL;

	args->value = host1x_syncpt_read_min(sp);
	return 0;
}

static int tegra_syncpt_incr(struct drm_device *drm, void *data,
			     struct drm_file *file)
{
	struct host1x *host1x = dev_get_drvdata(drm->dev->parent);
	struct drm_tegra_syncpt_incr *args = data;
	struct host1x_syncpt *sp;

	sp = host1x_syncpt_get(host1x, args->id);
	if (!sp)
		return -EINVAL;

	return host1x_syncpt_incr(sp);
}

static int tegra_syncpt_wait(struct drm_device *drm, void *data,
			     struct drm_file *file)
{
	struct host1x *host1x = dev_get_drvdata(drm->dev->parent);
	struct drm_tegra_syncpt_wait *args = data;
	struct host1x_syncpt *sp;

	sp = host1x_syncpt_get(host1x, args->id);
	if (!sp)
		return -EINVAL;

	return host1x_syncpt_wait(sp, args->thresh, args->timeout,
				  &args->value);
}

static int tegra_open_channel(struct drm_device *drm, void *data,
			      struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct tegra_drm *tegra = drm->dev_private;
	struct drm_tegra_open_channel *args = data;
	struct tegra_drm_context *context;
	struct tegra_drm_client *client;
	int err = -ENODEV;

	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	list_for_each_entry(client, &tegra->clients, list)
		if (client->base.class == args->client) {
			err = client->ops->open_channel(client, context);
			if (err)
				break;

			list_add(&context->list, &fpriv->contexts);
			args->context = (uintptr_t)context;
			context->client = client;
			return 0;
		}

	kfree(context);
	return err;
}

static int tegra_close_channel(struct drm_device *drm, void *data,
			       struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_close_channel *args = data;
	struct tegra_drm_context *context;

	context = tegra_drm_get_context(args->context);

	if (!tegra_drm_file_owns_context(fpriv, context))
		return -EINVAL;

	list_del(&context->list);
	tegra_drm_context_free(context);

	return 0;
}

static int tegra_get_syncpt(struct drm_device *drm, void *data,
			    struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_get_syncpt *args = data;
	struct tegra_drm_context *context;
	struct host1x_syncpt *syncpt;

	context = tegra_drm_get_context(args->context);

	if (!tegra_drm_file_owns_context(fpriv, context))
		return -ENODEV;

	if (args->index >= context->client->base.num_syncpts)
		return -EINVAL;

	syncpt = context->client->base.syncpts[args->index];
	args->id = host1x_syncpt_id(syncpt);

	return 0;
}

static int tegra_submit(struct drm_device *drm, void *data,
			struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_submit *args = data;
	struct tegra_drm_context *context;

	context = tegra_drm_get_context(args->context);

	if (!tegra_drm_file_owns_context(fpriv, context))
		return -ENODEV;

	return context->client->ops->submit(context, args, drm, file);
}

static int tegra_get_syncpt_base(struct drm_device *drm, void *data,
				 struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_get_syncpt_base *args = data;
	struct tegra_drm_context *context;
	struct host1x_syncpt_base *base;
	struct host1x_syncpt *syncpt;

	context = tegra_drm_get_context(args->context);

	if (!tegra_drm_file_owns_context(fpriv, context))
		return -ENODEV;

	if (args->syncpt >= context->client->base.num_syncpts)
		return -EINVAL;

	syncpt = context->client->base.syncpts[args->syncpt];

	base = host1x_syncpt_get_base(syncpt);
	if (!base)
		return -ENXIO;

	args->id = host1x_syncpt_base_id(base);

	return 0;
}
#endif

static const struct drm_ioctl_desc tegra_drm_ioctls[] = {
#ifdef CONFIG_DRM_TEGRA_STAGING
	DRM_IOCTL_DEF_DRV(TEGRA_GEM_CREATE, tegra_gem_create, DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(TEGRA_GEM_MMAP, tegra_gem_mmap, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(TEGRA_SYNCPT_READ, tegra_syncpt_read, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(TEGRA_SYNCPT_INCR, tegra_syncpt_incr, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(TEGRA_SYNCPT_WAIT, tegra_syncpt_wait, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(TEGRA_OPEN_CHANNEL, tegra_open_channel, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(TEGRA_CLOSE_CHANNEL, tegra_close_channel, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(TEGRA_GET_SYNCPT, tegra_get_syncpt, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(TEGRA_SUBMIT, tegra_submit, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(TEGRA_GET_SYNCPT_BASE, tegra_get_syncpt_base, DRM_UNLOCKED),
#endif
};

static const struct file_operations tegra_drm_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = tegra_drm_mmap,
	.poll = drm_poll,
	.read = drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct drm_crtc *tegra_crtc_from_pipe(struct drm_device *drm, int pipe)
{
	struct drm_crtc *crtc;

	list_for_each_entry(crtc, &drm->mode_config.crtc_list, head) {
		struct tegra_dc *dc = to_tegra_dc(crtc);

		if (dc->pipe == pipe)
			return crtc;
	}

	return NULL;
}

static u32 tegra_drm_get_vblank_counter(struct drm_device *dev, int crtc)
{
	/* TODO: implement real hardware counter using syncpoints */
	return drm_vblank_count(dev, crtc);
}

static int tegra_drm_enable_vblank(struct drm_device *drm, int pipe)
{
	struct drm_crtc *crtc = tegra_crtc_from_pipe(drm, pipe);
	struct tegra_dc *dc = to_tegra_dc(crtc);

	if (!crtc)
		return -ENODEV;

	tegra_dc_enable_vblank(dc);

	return 0;
}

static void tegra_drm_disable_vblank(struct drm_device *drm, int pipe)
{
	struct drm_crtc *crtc = tegra_crtc_from_pipe(drm, pipe);
	struct tegra_dc *dc = to_tegra_dc(crtc);

	if (crtc)
		tegra_dc_disable_vblank(dc);
}

static void tegra_drm_preclose(struct drm_device *drm, struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct tegra_drm_context *context, *tmp;
	struct drm_crtc *crtc;

	list_for_each_entry(crtc, &drm->mode_config.crtc_list, head)
		tegra_dc_cancel_page_flip(crtc, file);

	list_for_each_entry_safe(context, tmp, &fpriv->contexts, list)
		tegra_drm_context_free(context);

	kfree(fpriv);
}

#ifdef CONFIG_DEBUG_FS
static int tegra_debugfs_framebuffers(struct seq_file *s, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *)s->private;
	struct drm_device *drm = node->minor->dev;
	struct drm_framebuffer *fb;

	mutex_lock(&drm->mode_config.fb_lock);

	list_for_each_entry(fb, &drm->mode_config.fb_list, head) {
		seq_printf(s, "%3d: user size: %d x %d, depth %d, %d bpp, refcount %d\n",
			   fb->base.id, fb->width, fb->height, fb->depth,
			   fb->bits_per_pixel,
			   atomic_read(&fb->refcount.refcount));
	}

	mutex_unlock(&drm->mode_config.fb_lock);

	return 0;
}

static struct drm_info_list tegra_debugfs_list[] = {
	{ "framebuffers", tegra_debugfs_framebuffers, 0 },
};

static int tegra_debugfs_init(struct drm_minor *minor)
{
	return drm_debugfs_create_files(tegra_debugfs_list,
					ARRAY_SIZE(tegra_debugfs_list),
					minor->debugfs_root, minor);
}

static void tegra_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(tegra_debugfs_list,
				 ARRAY_SIZE(tegra_debugfs_list), minor);
}
#endif

struct drm_driver tegra_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM,
	.load = tegra_drm_load,
	.unload = tegra_drm_unload,
	.open = tegra_drm_open,
	.preclose = tegra_drm_preclose,
	.lastclose = tegra_drm_lastclose,

	.get_vblank_counter = tegra_drm_get_vblank_counter,
	.enable_vblank = tegra_drm_enable_vblank,
	.disable_vblank = tegra_drm_disable_vblank,

#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = tegra_debugfs_init,
	.debugfs_cleanup = tegra_debugfs_cleanup,
#endif

	.gem_free_object = tegra_bo_free_object,
	.gem_vm_ops = &tegra_bo_vm_ops,
	.dumb_create = tegra_bo_dumb_create,
	.dumb_map_offset = tegra_bo_dumb_map_offset,
	.dumb_destroy = drm_gem_dumb_destroy,

	.ioctls = tegra_drm_ioctls,
	.num_ioctls = ARRAY_SIZE(tegra_drm_ioctls),
	.fops = &tegra_drm_fops,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

int tegra_drm_register_client(struct tegra_drm *tegra,
			      struct tegra_drm_client *client)
{
	mutex_lock(&tegra->clients_lock);
	list_add_tail(&client->list, &tegra->clients);
	mutex_unlock(&tegra->clients_lock);

	return 0;
}

int tegra_drm_unregister_client(struct tegra_drm *tegra,
				struct tegra_drm_client *client)
{
	mutex_lock(&tegra->clients_lock);
	list_del_init(&client->list);
	mutex_unlock(&tegra->clients_lock);

	return 0;
}

static int host1x_drm_probe(struct host1x_device *device)
{
	return drm_host1x_init(&tegra_drm_driver, device);
}

static int host1x_drm_remove(struct host1x_device *device)
{
	drm_host1x_exit(&tegra_drm_driver, device);

	return 0;
}

static const struct of_device_id host1x_drm_subdevs[] = {
	{ .compatible = "nvidia,tegra20-dc", },
	{ .compatible = "nvidia,tegra20-hdmi", },
	{ .compatible = "nvidia,tegra20-gr2d", },
	{ .compatible = "nvidia,tegra20-gr3d", },
	{ .compatible = "nvidia,tegra30-dc", },
	{ .compatible = "nvidia,tegra30-hdmi", },
	{ .compatible = "nvidia,tegra30-gr2d", },
	{ .compatible = "nvidia,tegra30-gr3d", },
	{ .compatible = "nvidia,tegra114-hdmi", },
	{ .compatible = "nvidia,tegra114-gr3d", },
	{ /* sentinel */ }
};

static struct host1x_driver host1x_drm_driver = {
	.name = "drm",
	.probe = host1x_drm_probe,
	.remove = host1x_drm_remove,
	.subdevs = host1x_drm_subdevs,
};

static int __init host1x_drm_init(void)
{
	int err;

	err = host1x_driver_register(&host1x_drm_driver);
	if (err < 0)
		return err;

	err = platform_driver_register(&tegra_dc_driver);
	if (err < 0)
		goto unregister_host1x;

	err = platform_driver_register(&tegra_hdmi_driver);
	if (err < 0)
		goto unregister_dc;

	err = platform_driver_register(&tegra_gr2d_driver);
	if (err < 0)
		goto unregister_hdmi;

	err = platform_driver_register(&tegra_gr3d_driver);
	if (err < 0)
		goto unregister_gr2d;

	return 0;

unregister_gr2d:
	platform_driver_unregister(&tegra_gr2d_driver);
unregister_hdmi:
	platform_driver_unregister(&tegra_hdmi_driver);
unregister_dc:
	platform_driver_unregister(&tegra_dc_driver);
unregister_host1x:
	host1x_driver_unregister(&host1x_drm_driver);
	return err;
}
module_init(host1x_drm_init);

static void __exit host1x_drm_exit(void)
{
	platform_driver_unregister(&tegra_gr3d_driver);
	platform_driver_unregister(&tegra_gr2d_driver);
	platform_driver_unregister(&tegra_hdmi_driver);
	platform_driver_unregister(&tegra_dc_driver);
	host1x_driver_unregister(&host1x_drm_driver);
}
module_exit(host1x_drm_exit);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_DESCRIPTION("NVIDIA Tegra DRM driver");
MODULE_LICENSE("GPL v2");
