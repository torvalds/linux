/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012-2013 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "host1x_client.h"
#include "dev.h"
#include "drm.h"
#include "gem.h"
#include "syncpt.h"

#define DRIVER_NAME "tegra"
#define DRIVER_DESC "NVIDIA Tegra graphics"
#define DRIVER_DATE "20120330"
#define DRIVER_MAJOR 0
#define DRIVER_MINOR 0
#define DRIVER_PATCHLEVEL 0

struct tegra_drm_file {
	struct list_head contexts;
};

struct host1x_subdev {
	struct host1x_client *client;
	struct device_node *np;
	struct list_head list;
};

static int host1x_subdev_add(struct tegra_drm *tegra, struct device_node *np)
{
	struct host1x_subdev *subdev;

	subdev = kzalloc(sizeof(*subdev), GFP_KERNEL);
	if (!subdev)
		return -ENOMEM;

	INIT_LIST_HEAD(&subdev->list);
	subdev->np = of_node_get(np);

	list_add_tail(&subdev->list, &tegra->subdevs);

	return 0;
}

static int host1x_subdev_register(struct tegra_drm *tegra,
				  struct host1x_subdev *subdev,
				  struct host1x_client *client)
{
	mutex_lock(&tegra->subdevs_lock);
	list_del_init(&subdev->list);
	list_add_tail(&subdev->list, &tegra->active);
	subdev->client = client;
	mutex_unlock(&tegra->subdevs_lock);

	return 0;
}

static int host1x_subdev_unregister(struct tegra_drm *tegra,
				    struct host1x_subdev *subdev)
{
	mutex_lock(&tegra->subdevs_lock);
	list_del_init(&subdev->list);
	mutex_unlock(&tegra->subdevs_lock);

	of_node_put(subdev->np);
	kfree(subdev);

	return 0;
}

static int tegra_parse_dt(struct tegra_drm *tegra)
{
	static const char * const compat[] = {
		"nvidia,tegra20-dc",
		"nvidia,tegra20-hdmi",
		"nvidia,tegra20-gr2d",
		"nvidia,tegra30-dc",
		"nvidia,tegra30-hdmi",
		"nvidia,tegra30-gr2d",
	};
	unsigned int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(compat); i++) {
		struct device_node *np;

		for_each_child_of_node(tegra->dev->of_node, np) {
			if (of_device_is_compatible(np, compat[i]) &&
			    of_device_is_available(np)) {
				err = host1x_subdev_add(tegra, np);
				if (err < 0)
					return err;
			}
		}
	}

	return 0;
}

int tegra_drm_alloc(struct platform_device *pdev)
{
	struct tegra_drm *tegra;
	int err;

	tegra = devm_kzalloc(&pdev->dev, sizeof(*tegra), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	mutex_init(&tegra->subdevs_lock);
	INIT_LIST_HEAD(&tegra->subdevs);
	INIT_LIST_HEAD(&tegra->active);
	mutex_init(&tegra->clients_lock);
	INIT_LIST_HEAD(&tegra->clients);
	tegra->dev = &pdev->dev;

	err = tegra_parse_dt(tegra);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to parse DT: %d\n", err);
		return err;
	}

	host1x_set_drm_data(&pdev->dev, tegra);

	return 0;
}

int tegra_drm_init(struct tegra_drm *tegra, struct drm_device *drm)
{
	struct host1x_client *client;

	mutex_lock(&tegra->clients_lock);

	list_for_each_entry(client, &tegra->clients, list) {
		if (client->ops && client->ops->drm_init) {
			int err = client->ops->drm_init(client, drm);
			if (err < 0) {
				dev_err(tegra->dev,
					"DRM setup failed for %s: %d\n",
					dev_name(client->dev), err);
				mutex_unlock(&tegra->clients_lock);
				return err;
			}
		}
	}

	mutex_unlock(&tegra->clients_lock);

	return 0;
}

int tegra_drm_exit(struct tegra_drm *tegra)
{
	struct platform_device *pdev = to_platform_device(tegra->dev);
	struct host1x_client *client;

	if (!tegra->drm)
		return 0;

	mutex_lock(&tegra->clients_lock);

	list_for_each_entry_reverse(client, &tegra->clients, list) {
		if (client->ops && client->ops->drm_exit) {
			int err = client->ops->drm_exit(client);
			if (err < 0) {
				dev_err(tegra->dev,
					"DRM cleanup failed for %s: %d\n",
					dev_name(client->dev), err);
				mutex_unlock(&tegra->clients_lock);
				return err;
			}
		}
	}

	mutex_unlock(&tegra->clients_lock);

	drm_platform_exit(&tegra_drm_driver, pdev);
	tegra->drm = NULL;

	return 0;
}

int host1x_register_client(struct tegra_drm *tegra,
			   struct host1x_client *client)
{
	struct host1x_subdev *subdev, *tmp;
	int err;

	mutex_lock(&tegra->clients_lock);
	list_add_tail(&client->list, &tegra->clients);
	mutex_unlock(&tegra->clients_lock);

	list_for_each_entry_safe(subdev, tmp, &tegra->subdevs, list)
		if (subdev->np == client->dev->of_node)
			host1x_subdev_register(tegra, subdev, client);

	if (list_empty(&tegra->subdevs)) {
		struct platform_device *pdev = to_platform_device(tegra->dev);

		err = drm_platform_init(&tegra_drm_driver, pdev);
		if (err < 0) {
			dev_err(tegra->dev, "drm_platform_init(): %d\n", err);
			return err;
		}
	}

	return 0;
}

int host1x_unregister_client(struct tegra_drm *tegra,
			     struct host1x_client *client)
{
	struct host1x_subdev *subdev, *tmp;
	int err;

	list_for_each_entry_safe(subdev, tmp, &tegra->active, list) {
		if (subdev->client == client) {
			err = tegra_drm_exit(tegra);
			if (err < 0) {
				dev_err(tegra->dev, "tegra_drm_exit(): %d\n",
					err);
				return err;
			}

			host1x_subdev_unregister(tegra, subdev);
			break;
		}
	}

	mutex_lock(&tegra->clients_lock);
	list_del_init(&client->list);
	mutex_unlock(&tegra->clients_lock);

	return 0;
}

static int tegra_drm_load(struct drm_device *drm, unsigned long flags)
{
	struct tegra_drm *tegra;
	int err;

	tegra = host1x_get_drm_data(drm->dev);
	drm->dev_private = tegra;
	tegra->drm = drm;

	drm_mode_config_init(drm);

	err = tegra_drm_init(tegra, drm);
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
	drm_kms_helper_poll_fini(drm);
	tegra_drm_fb_exit(drm);

	drm_mode_config_cleanup(drm);

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

	bo = tegra_bo_create_with_handle(file, drm, args->size,
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
	struct drm_tegra_syncpt_read *args = data;
	struct host1x *host = dev_get_drvdata(drm->dev);
	struct host1x_syncpt *sp = host1x_syncpt_get(host, args->id);

	if (!sp)
		return -EINVAL;

	args->value = host1x_syncpt_read_min(sp);
	return 0;
}

static int tegra_syncpt_incr(struct drm_device *drm, void *data,
			     struct drm_file *file)
{
	struct drm_tegra_syncpt_incr *args = data;
	struct host1x *host = dev_get_drvdata(drm->dev);
	struct host1x_syncpt *sp = host1x_syncpt_get(host, args->id);

	if (!sp)
		return -EINVAL;

	return host1x_syncpt_incr(sp);
}

static int tegra_syncpt_wait(struct drm_device *drm, void *data,
			     struct drm_file *file)
{
	struct drm_tegra_syncpt_wait *args = data;
	struct host1x *host = dev_get_drvdata(drm->dev);
	struct host1x_syncpt *sp = host1x_syncpt_get(host, args->id);

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
	struct host1x_client *client;
	int err = -ENODEV;

	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	list_for_each_entry(client, &tegra->clients, list)
		if (client->class == args->client) {
			err = client->ops->open_channel(client, context);
			if (err)
				break;

			context->client = client;
			list_add(&context->list, &fpriv->contexts);
			args->context = (uintptr_t)context;
			return 0;
		}

	kfree(context);
	return err;
}

static int tegra_close_channel(struct drm_device *drm, void *data,
			       struct drm_file *file)
{
	struct drm_tegra_close_channel *args = data;
	struct tegra_drm_file *fpriv = file->driver_priv;
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

	if (args->index >= context->client->num_syncpts)
		return -EINVAL;

	syncpt = context->client->syncpts[args->index];
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
