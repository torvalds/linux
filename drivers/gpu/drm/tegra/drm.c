/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012-2016 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/host1x.h>
#include <linux/idr.h>
#include <linux/iommu.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>

#include "drm.h"
#include "gem.h"

#define DRIVER_NAME "tegra"
#define DRIVER_DESC "NVIDIA Tegra graphics"
#define DRIVER_DATE "20120330"
#define DRIVER_MAJOR 0
#define DRIVER_MINOR 0
#define DRIVER_PATCHLEVEL 0

#define CARVEOUT_SZ SZ_64M
#define CDMA_GATHER_FETCHES_MAX_NB 16383

struct tegra_drm_file {
	struct idr contexts;
	struct mutex lock;
};

static void tegra_atomic_schedule(struct tegra_drm *tegra,
				  struct drm_atomic_state *state)
{
	tegra->commit.state = state;
	schedule_work(&tegra->commit.work);
}

static void tegra_atomic_complete(struct tegra_drm *tegra,
				  struct drm_atomic_state *state)
{
	struct drm_device *drm = tegra->drm;

	/*
	 * Everything below can be run asynchronously without the need to grab
	 * any modeset locks at all under one condition: It must be guaranteed
	 * that the asynchronous work has either been cancelled (if the driver
	 * supports it, which at least requires that the framebuffers get
	 * cleaned up with drm_atomic_helper_cleanup_planes()) or completed
	 * before the new state gets committed on the software side with
	 * drm_atomic_helper_swap_state().
	 *
	 * This scheme allows new atomic state updates to be prepared and
	 * checked in parallel to the asynchronous completion of the previous
	 * update. Which is important since compositors need to figure out the
	 * composition of the next frame right after having submitted the
	 * current layout.
	 */

	drm_atomic_helper_commit_modeset_disables(drm, state);
	drm_atomic_helper_commit_modeset_enables(drm, state);
	drm_atomic_helper_commit_planes(drm, state,
					DRM_PLANE_COMMIT_ACTIVE_ONLY);

	drm_atomic_helper_wait_for_vblanks(drm, state);

	drm_atomic_helper_cleanup_planes(drm, state);
	drm_atomic_state_put(state);
}

static void tegra_atomic_work(struct work_struct *work)
{
	struct tegra_drm *tegra = container_of(work, struct tegra_drm,
					       commit.work);

	tegra_atomic_complete(tegra, tegra->commit.state);
}

static int tegra_atomic_commit(struct drm_device *drm,
			       struct drm_atomic_state *state, bool nonblock)
{
	struct tegra_drm *tegra = drm->dev_private;
	int err;

	err = drm_atomic_helper_prepare_planes(drm, state);
	if (err)
		return err;

	/* serialize outstanding nonblocking commits */
	mutex_lock(&tegra->commit.lock);
	flush_work(&tegra->commit.work);

	/*
	 * This is the point of no return - everything below never fails except
	 * when the hw goes bonghits. Which means we can commit the new state on
	 * the software side now.
	 */

	err = drm_atomic_helper_swap_state(state, true);
	if (err) {
		mutex_unlock(&tegra->commit.lock);
		drm_atomic_helper_cleanup_planes(drm, state);
		return err;
	}

	drm_atomic_state_get(state);
	if (nonblock)
		tegra_atomic_schedule(tegra, state);
	else
		tegra_atomic_complete(tegra, state);

	mutex_unlock(&tegra->commit.lock);
	return 0;
}

static const struct drm_mode_config_funcs tegra_drm_mode_funcs = {
	.fb_create = tegra_fb_create,
#ifdef CONFIG_DRM_FBDEV_EMULATION
	.output_poll_changed = tegra_fb_output_poll_changed,
#endif
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = tegra_atomic_commit,
};

static int tegra_drm_load(struct drm_device *drm, unsigned long flags)
{
	struct host1x_device *device = to_host1x_device(drm->dev);
	struct tegra_drm *tegra;
	int err;

	tegra = kzalloc(sizeof(*tegra), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	if (iommu_present(&platform_bus_type)) {
		u64 carveout_start, carveout_end, gem_start, gem_end;
		struct iommu_domain_geometry *geometry;
		unsigned long order;

		tegra->domain = iommu_domain_alloc(&platform_bus_type);
		if (!tegra->domain) {
			err = -ENOMEM;
			goto free;
		}

		geometry = &tegra->domain->geometry;
		gem_start = geometry->aperture_start;
		gem_end = geometry->aperture_end - CARVEOUT_SZ;
		carveout_start = gem_end + 1;
		carveout_end = geometry->aperture_end;

		order = __ffs(tegra->domain->pgsize_bitmap);
		init_iova_domain(&tegra->carveout.domain, 1UL << order,
				 carveout_start >> order,
				 carveout_end >> order);

		tegra->carveout.shift = iova_shift(&tegra->carveout.domain);
		tegra->carveout.limit = carveout_end >> tegra->carveout.shift;

		drm_mm_init(&tegra->mm, gem_start, gem_end - gem_start + 1);
		mutex_init(&tegra->mm_lock);

		DRM_DEBUG("IOMMU apertures:\n");
		DRM_DEBUG("  GEM: %#llx-%#llx\n", gem_start, gem_end);
		DRM_DEBUG("  Carveout: %#llx-%#llx\n", carveout_start,
			  carveout_end);
	}

	mutex_init(&tegra->clients_lock);
	INIT_LIST_HEAD(&tegra->clients);

	mutex_init(&tegra->commit.lock);
	INIT_WORK(&tegra->commit.work, tegra_atomic_work);

	drm->dev_private = tegra;
	tegra->drm = drm;

	drm_mode_config_init(drm);

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;

	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;

	drm->mode_config.allow_fb_modifiers = true;

	drm->mode_config.funcs = &tegra_drm_mode_funcs;

	err = tegra_drm_fb_prepare(drm);
	if (err < 0)
		goto config;

	drm_kms_helper_poll_init(drm);

	err = host1x_device_init(device);
	if (err < 0)
		goto fbdev;

	/*
	 * We don't use the drm_irq_install() helpers provided by the DRM
	 * core, so we need to set this manually in order to allow the
	 * DRM_IOCTL_WAIT_VBLANK to operate correctly.
	 */
	drm->irq_enabled = true;

	/* syncpoints are used for full 32-bit hardware VBLANK counters */
	drm->max_vblank_count = 0xffffffff;

	err = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (err < 0)
		goto device;

	drm_mode_config_reset(drm);

	err = tegra_drm_fb_init(drm);
	if (err < 0)
		goto device;

	return 0;

device:
	host1x_device_exit(device);
fbdev:
	drm_kms_helper_poll_fini(drm);
	tegra_drm_fb_free(drm);
config:
	drm_mode_config_cleanup(drm);

	if (tegra->domain) {
		iommu_domain_free(tegra->domain);
		drm_mm_takedown(&tegra->mm);
		mutex_destroy(&tegra->mm_lock);
		put_iova_domain(&tegra->carveout.domain);
	}
free:
	kfree(tegra);
	return err;
}

static void tegra_drm_unload(struct drm_device *drm)
{
	struct host1x_device *device = to_host1x_device(drm->dev);
	struct tegra_drm *tegra = drm->dev_private;
	int err;

	drm_kms_helper_poll_fini(drm);
	tegra_drm_fb_exit(drm);
	drm_mode_config_cleanup(drm);

	err = host1x_device_exit(device);
	if (err < 0)
		return;

	if (tegra->domain) {
		iommu_domain_free(tegra->domain);
		drm_mm_takedown(&tegra->mm);
		mutex_destroy(&tegra->mm_lock);
		put_iova_domain(&tegra->carveout.domain);
	}

	kfree(tegra);
}

static int tegra_drm_open(struct drm_device *drm, struct drm_file *filp)
{
	struct tegra_drm_file *fpriv;

	fpriv = kzalloc(sizeof(*fpriv), GFP_KERNEL);
	if (!fpriv)
		return -ENOMEM;

	idr_init(&fpriv->contexts);
	mutex_init(&fpriv->lock);
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
#ifdef CONFIG_DRM_FBDEV_EMULATION
	struct tegra_drm *tegra = drm->dev_private;

	tegra_fbdev_restore_mode(tegra->fbdev);
#endif
}

static struct host1x_bo *
host1x_bo_lookup(struct drm_file *file, u32 handle)
{
	struct drm_gem_object *gem;
	struct tegra_bo *bo;

	gem = drm_gem_object_lookup(file, handle);
	if (!gem)
		return NULL;

	bo = to_tegra_bo(gem);
	return &bo->base;
}

static int host1x_reloc_copy_from_user(struct host1x_reloc *dest,
				       struct drm_tegra_reloc __user *src,
				       struct drm_device *drm,
				       struct drm_file *file)
{
	u32 cmdbuf, target;
	int err;

	err = get_user(cmdbuf, &src->cmdbuf.handle);
	if (err < 0)
		return err;

	err = get_user(dest->cmdbuf.offset, &src->cmdbuf.offset);
	if (err < 0)
		return err;

	err = get_user(target, &src->target.handle);
	if (err < 0)
		return err;

	err = get_user(dest->target.offset, &src->target.offset);
	if (err < 0)
		return err;

	err = get_user(dest->shift, &src->shift);
	if (err < 0)
		return err;

	dest->cmdbuf.bo = host1x_bo_lookup(file, cmdbuf);
	if (!dest->cmdbuf.bo)
		return -ENOENT;

	dest->target.bo = host1x_bo_lookup(file, target);
	if (!dest->target.bo)
		return -ENOENT;

	return 0;
}

static int host1x_waitchk_copy_from_user(struct host1x_waitchk *dest,
					 struct drm_tegra_waitchk __user *src,
					 struct drm_file *file)
{
	u32 cmdbuf;
	int err;

	err = get_user(cmdbuf, &src->handle);
	if (err < 0)
		return err;

	err = get_user(dest->offset, &src->offset);
	if (err < 0)
		return err;

	err = get_user(dest->syncpt_id, &src->syncpt);
	if (err < 0)
		return err;

	err = get_user(dest->thresh, &src->thresh);
	if (err < 0)
		return err;

	dest->bo = host1x_bo_lookup(file, cmdbuf);
	if (!dest->bo)
		return -ENOENT;

	return 0;
}

int tegra_drm_submit(struct tegra_drm_context *context,
		     struct drm_tegra_submit *args, struct drm_device *drm,
		     struct drm_file *file)
{
	unsigned int num_cmdbufs = args->num_cmdbufs;
	unsigned int num_relocs = args->num_relocs;
	unsigned int num_waitchks = args->num_waitchks;
	struct drm_tegra_cmdbuf __user *cmdbufs =
		(void __user *)(uintptr_t)args->cmdbufs;
	struct drm_tegra_reloc __user *relocs =
		(void __user *)(uintptr_t)args->relocs;
	struct drm_tegra_waitchk __user *waitchks =
		(void __user *)(uintptr_t)args->waitchks;
	struct drm_tegra_syncpt syncpt;
	struct host1x *host1x = dev_get_drvdata(drm->dev->parent);
	struct drm_gem_object **refs;
	struct host1x_syncpt *sp;
	struct host1x_job *job;
	unsigned int num_refs;
	int err;

	/* We don't yet support other than one syncpt_incr struct per submit */
	if (args->num_syncpts != 1)
		return -EINVAL;

	/* We don't yet support waitchks */
	if (args->num_waitchks != 0)
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

	/*
	 * Track referenced BOs so that they can be unreferenced after the
	 * submission is complete.
	 */
	num_refs = num_cmdbufs + num_relocs * 2 + num_waitchks;

	refs = kmalloc_array(num_refs, sizeof(*refs), GFP_KERNEL);
	if (!refs) {
		err = -ENOMEM;
		goto put;
	}

	/* reuse as an iterator later */
	num_refs = 0;

	while (num_cmdbufs) {
		struct drm_tegra_cmdbuf cmdbuf;
		struct host1x_bo *bo;
		struct tegra_bo *obj;
		u64 offset;

		if (copy_from_user(&cmdbuf, cmdbufs, sizeof(cmdbuf))) {
			err = -EFAULT;
			goto fail;
		}

		/*
		 * The maximum number of CDMA gather fetches is 16383, a higher
		 * value means the words count is malformed.
		 */
		if (cmdbuf.words > CDMA_GATHER_FETCHES_MAX_NB) {
			err = -EINVAL;
			goto fail;
		}

		bo = host1x_bo_lookup(file, cmdbuf.handle);
		if (!bo) {
			err = -ENOENT;
			goto fail;
		}

		offset = (u64)cmdbuf.offset + (u64)cmdbuf.words * sizeof(u32);
		obj = host1x_to_tegra_bo(bo);
		refs[num_refs++] = &obj->gem;

		/*
		 * Gather buffer base address must be 4-bytes aligned,
		 * unaligned offset is malformed and cause commands stream
		 * corruption on the buffer address relocation.
		 */
		if (offset & 3 || offset >= obj->gem.size) {
			err = -EINVAL;
			goto fail;
		}

		host1x_job_add_gather(job, bo, cmdbuf.words, cmdbuf.offset);
		num_cmdbufs--;
		cmdbufs++;
	}

	/* copy and resolve relocations from submit */
	while (num_relocs--) {
		struct host1x_reloc *reloc;
		struct tegra_bo *obj;

		err = host1x_reloc_copy_from_user(&job->relocarray[num_relocs],
						  &relocs[num_relocs], drm,
						  file);
		if (err < 0)
			goto fail;

		reloc = &job->relocarray[num_relocs];
		obj = host1x_to_tegra_bo(reloc->cmdbuf.bo);
		refs[num_refs++] = &obj->gem;

		/*
		 * The unaligned cmdbuf offset will cause an unaligned write
		 * during of the relocations patching, corrupting the commands
		 * stream.
		 */
		if (reloc->cmdbuf.offset & 3 ||
		    reloc->cmdbuf.offset >= obj->gem.size) {
			err = -EINVAL;
			goto fail;
		}

		obj = host1x_to_tegra_bo(reloc->target.bo);
		refs[num_refs++] = &obj->gem;

		if (reloc->target.offset >= obj->gem.size) {
			err = -EINVAL;
			goto fail;
		}
	}

	/* copy and resolve waitchks from submit */
	while (num_waitchks--) {
		struct host1x_waitchk *wait = &job->waitchk[num_waitchks];
		struct tegra_bo *obj;

		err = host1x_waitchk_copy_from_user(wait,
						    &waitchks[num_waitchks],
						    file);
		if (err < 0)
			goto fail;

		obj = host1x_to_tegra_bo(wait->bo);
		refs[num_refs++] = &obj->gem;

		/*
		 * The unaligned offset will cause an unaligned write during
		 * of the waitchks patching, corrupting the commands stream.
		 */
		if (wait->offset & 3 ||
		    wait->offset >= obj->gem.size) {
			err = -EINVAL;
			goto fail;
		}
	}

	if (copy_from_user(&syncpt, (void __user *)(uintptr_t)args->syncpts,
			   sizeof(syncpt))) {
		err = -EFAULT;
		goto fail;
	}

	/* check whether syncpoint ID is valid */
	sp = host1x_syncpt_get(host1x, syncpt.id);
	if (!sp) {
		err = -ENOENT;
		goto fail;
	}

	job->is_addr_reg = context->client->ops->is_addr_reg;
	job->is_valid_class = context->client->ops->is_valid_class;
	job->syncpt_incrs = syncpt.incrs;
	job->syncpt_id = syncpt.id;
	job->timeout = 10000;

	if (args->timeout && args->timeout < 10000)
		job->timeout = args->timeout;

	err = host1x_job_pin(job, context->client->base.dev);
	if (err)
		goto fail;

	err = host1x_job_submit(job);
	if (err) {
		host1x_job_unpin(job);
		goto fail;
	}

	args->fence = job->syncpt_end;

fail:
	while (num_refs--)
		drm_gem_object_put_unlocked(refs[num_refs]);

	kfree(refs);

put:
	host1x_job_put(job);
	return err;
}


#ifdef CONFIG_DRM_TEGRA_STAGING
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

	gem = drm_gem_object_lookup(file, args->handle);
	if (!gem)
		return -EINVAL;

	bo = to_tegra_bo(gem);

	args->offset = drm_vma_node_offset_addr(&bo->gem.vma_node);

	drm_gem_object_put_unlocked(gem);

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

static int tegra_client_open(struct tegra_drm_file *fpriv,
			     struct tegra_drm_client *client,
			     struct tegra_drm_context *context)
{
	int err;

	err = client->ops->open_channel(client, context);
	if (err < 0)
		return err;

	err = idr_alloc(&fpriv->contexts, context, 1, 0, GFP_KERNEL);
	if (err < 0) {
		client->ops->close_channel(context);
		return err;
	}

	context->client = client;
	context->id = err;

	return 0;
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

	mutex_lock(&fpriv->lock);

	list_for_each_entry(client, &tegra->clients, list)
		if (client->base.class == args->client) {
			err = tegra_client_open(fpriv, client, context);
			if (err < 0)
				break;

			args->context = context->id;
			break;
		}

	if (err < 0)
		kfree(context);

	mutex_unlock(&fpriv->lock);
	return err;
}

static int tegra_close_channel(struct drm_device *drm, void *data,
			       struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_close_channel *args = data;
	struct tegra_drm_context *context;
	int err = 0;

	mutex_lock(&fpriv->lock);

	context = idr_find(&fpriv->contexts, args->context);
	if (!context) {
		err = -EINVAL;
		goto unlock;
	}

	idr_remove(&fpriv->contexts, context->id);
	tegra_drm_context_free(context);

unlock:
	mutex_unlock(&fpriv->lock);
	return err;
}

static int tegra_get_syncpt(struct drm_device *drm, void *data,
			    struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_get_syncpt *args = data;
	struct tegra_drm_context *context;
	struct host1x_syncpt *syncpt;
	int err = 0;

	mutex_lock(&fpriv->lock);

	context = idr_find(&fpriv->contexts, args->context);
	if (!context) {
		err = -ENODEV;
		goto unlock;
	}

	if (args->index >= context->client->base.num_syncpts) {
		err = -EINVAL;
		goto unlock;
	}

	syncpt = context->client->base.syncpts[args->index];
	args->id = host1x_syncpt_id(syncpt);

unlock:
	mutex_unlock(&fpriv->lock);
	return err;
}

static int tegra_submit(struct drm_device *drm, void *data,
			struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_submit *args = data;
	struct tegra_drm_context *context;
	int err;

	mutex_lock(&fpriv->lock);

	context = idr_find(&fpriv->contexts, args->context);
	if (!context) {
		err = -ENODEV;
		goto unlock;
	}

	err = context->client->ops->submit(context, args, drm, file);

unlock:
	mutex_unlock(&fpriv->lock);
	return err;
}

static int tegra_get_syncpt_base(struct drm_device *drm, void *data,
				 struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_get_syncpt_base *args = data;
	struct tegra_drm_context *context;
	struct host1x_syncpt_base *base;
	struct host1x_syncpt *syncpt;
	int err = 0;

	mutex_lock(&fpriv->lock);

	context = idr_find(&fpriv->contexts, args->context);
	if (!context) {
		err = -ENODEV;
		goto unlock;
	}

	if (args->syncpt >= context->client->base.num_syncpts) {
		err = -EINVAL;
		goto unlock;
	}

	syncpt = context->client->base.syncpts[args->syncpt];

	base = host1x_syncpt_get_base(syncpt);
	if (!base) {
		err = -ENXIO;
		goto unlock;
	}

	args->id = host1x_syncpt_base_id(base);

unlock:
	mutex_unlock(&fpriv->lock);
	return err;
}

static int tegra_gem_set_tiling(struct drm_device *drm, void *data,
				struct drm_file *file)
{
	struct drm_tegra_gem_set_tiling *args = data;
	enum tegra_bo_tiling_mode mode;
	struct drm_gem_object *gem;
	unsigned long value = 0;
	struct tegra_bo *bo;

	switch (args->mode) {
	case DRM_TEGRA_GEM_TILING_MODE_PITCH:
		mode = TEGRA_BO_TILING_MODE_PITCH;

		if (args->value != 0)
			return -EINVAL;

		break;

	case DRM_TEGRA_GEM_TILING_MODE_TILED:
		mode = TEGRA_BO_TILING_MODE_TILED;

		if (args->value != 0)
			return -EINVAL;

		break;

	case DRM_TEGRA_GEM_TILING_MODE_BLOCK:
		mode = TEGRA_BO_TILING_MODE_BLOCK;

		if (args->value > 5)
			return -EINVAL;

		value = args->value;
		break;

	default:
		return -EINVAL;
	}

	gem = drm_gem_object_lookup(file, args->handle);
	if (!gem)
		return -ENOENT;

	bo = to_tegra_bo(gem);

	bo->tiling.mode = mode;
	bo->tiling.value = value;

	drm_gem_object_put_unlocked(gem);

	return 0;
}

static int tegra_gem_get_tiling(struct drm_device *drm, void *data,
				struct drm_file *file)
{
	struct drm_tegra_gem_get_tiling *args = data;
	struct drm_gem_object *gem;
	struct tegra_bo *bo;
	int err = 0;

	gem = drm_gem_object_lookup(file, args->handle);
	if (!gem)
		return -ENOENT;

	bo = to_tegra_bo(gem);

	switch (bo->tiling.mode) {
	case TEGRA_BO_TILING_MODE_PITCH:
		args->mode = DRM_TEGRA_GEM_TILING_MODE_PITCH;
		args->value = 0;
		break;

	case TEGRA_BO_TILING_MODE_TILED:
		args->mode = DRM_TEGRA_GEM_TILING_MODE_TILED;
		args->value = 0;
		break;

	case TEGRA_BO_TILING_MODE_BLOCK:
		args->mode = DRM_TEGRA_GEM_TILING_MODE_BLOCK;
		args->value = bo->tiling.value;
		break;

	default:
		err = -EINVAL;
		break;
	}

	drm_gem_object_put_unlocked(gem);

	return err;
}

static int tegra_gem_set_flags(struct drm_device *drm, void *data,
			       struct drm_file *file)
{
	struct drm_tegra_gem_set_flags *args = data;
	struct drm_gem_object *gem;
	struct tegra_bo *bo;

	if (args->flags & ~DRM_TEGRA_GEM_FLAGS)
		return -EINVAL;

	gem = drm_gem_object_lookup(file, args->handle);
	if (!gem)
		return -ENOENT;

	bo = to_tegra_bo(gem);
	bo->flags = 0;

	if (args->flags & DRM_TEGRA_GEM_BOTTOM_UP)
		bo->flags |= TEGRA_BO_BOTTOM_UP;

	drm_gem_object_put_unlocked(gem);

	return 0;
}

static int tegra_gem_get_flags(struct drm_device *drm, void *data,
			       struct drm_file *file)
{
	struct drm_tegra_gem_get_flags *args = data;
	struct drm_gem_object *gem;
	struct tegra_bo *bo;

	gem = drm_gem_object_lookup(file, args->handle);
	if (!gem)
		return -ENOENT;

	bo = to_tegra_bo(gem);
	args->flags = 0;

	if (bo->flags & TEGRA_BO_BOTTOM_UP)
		args->flags |= DRM_TEGRA_GEM_BOTTOM_UP;

	drm_gem_object_put_unlocked(gem);

	return 0;
}
#endif

static const struct drm_ioctl_desc tegra_drm_ioctls[] = {
#ifdef CONFIG_DRM_TEGRA_STAGING
	DRM_IOCTL_DEF_DRV(TEGRA_GEM_CREATE, tegra_gem_create,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TEGRA_GEM_MMAP, tegra_gem_mmap,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TEGRA_SYNCPT_READ, tegra_syncpt_read,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TEGRA_SYNCPT_INCR, tegra_syncpt_incr,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TEGRA_SYNCPT_WAIT, tegra_syncpt_wait,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TEGRA_OPEN_CHANNEL, tegra_open_channel,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TEGRA_CLOSE_CHANNEL, tegra_close_channel,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TEGRA_GET_SYNCPT, tegra_get_syncpt,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TEGRA_SUBMIT, tegra_submit,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TEGRA_GET_SYNCPT_BASE, tegra_get_syncpt_base,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TEGRA_GEM_SET_TILING, tegra_gem_set_tiling,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TEGRA_GEM_GET_TILING, tegra_gem_get_tiling,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TEGRA_GEM_SET_FLAGS, tegra_gem_set_flags,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(TEGRA_GEM_GET_FLAGS, tegra_gem_get_flags,
			  DRM_UNLOCKED | DRM_RENDER_ALLOW),
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
	.compat_ioctl = drm_compat_ioctl,
	.llseek = noop_llseek,
};

static int tegra_drm_context_cleanup(int id, void *p, void *data)
{
	struct tegra_drm_context *context = p;

	tegra_drm_context_free(context);

	return 0;
}

static void tegra_drm_postclose(struct drm_device *drm, struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;

	mutex_lock(&fpriv->lock);
	idr_for_each(&fpriv->contexts, tegra_drm_context_cleanup, NULL);
	mutex_unlock(&fpriv->lock);

	idr_destroy(&fpriv->contexts);
	mutex_destroy(&fpriv->lock);
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
			   fb->base.id, fb->width, fb->height,
			   fb->format->depth,
			   fb->format->cpp[0] * 8,
			   drm_framebuffer_read_refcount(fb));
	}

	mutex_unlock(&drm->mode_config.fb_lock);

	return 0;
}

static int tegra_debugfs_iova(struct seq_file *s, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *)s->private;
	struct drm_device *drm = node->minor->dev;
	struct tegra_drm *tegra = drm->dev_private;
	struct drm_printer p = drm_seq_file_printer(s);

	if (tegra->domain) {
		mutex_lock(&tegra->mm_lock);
		drm_mm_print(&tegra->mm, &p);
		mutex_unlock(&tegra->mm_lock);
	}

	return 0;
}

static struct drm_info_list tegra_debugfs_list[] = {
	{ "framebuffers", tegra_debugfs_framebuffers, 0 },
	{ "iova", tegra_debugfs_iova, 0 },
};

static int tegra_debugfs_init(struct drm_minor *minor)
{
	return drm_debugfs_create_files(tegra_debugfs_list,
					ARRAY_SIZE(tegra_debugfs_list),
					minor->debugfs_root, minor);
}
#endif

static struct drm_driver tegra_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME |
			   DRIVER_ATOMIC | DRIVER_RENDER,
	.load = tegra_drm_load,
	.unload = tegra_drm_unload,
	.open = tegra_drm_open,
	.postclose = tegra_drm_postclose,
	.lastclose = tegra_drm_lastclose,

#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = tegra_debugfs_init,
#endif

	.gem_free_object_unlocked = tegra_bo_free_object,
	.gem_vm_ops = &tegra_bo_vm_ops,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = tegra_gem_prime_export,
	.gem_prime_import = tegra_gem_prime_import,

	.dumb_create = tegra_bo_dumb_create,

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

void *tegra_drm_alloc(struct tegra_drm *tegra, size_t size,
			      dma_addr_t *dma)
{
	struct iova *alloc;
	void *virt;
	gfp_t gfp;
	int err;

	if (tegra->domain)
		size = iova_align(&tegra->carveout.domain, size);
	else
		size = PAGE_ALIGN(size);

	gfp = GFP_KERNEL | __GFP_ZERO;
	if (!tegra->domain) {
		/*
		 * Many units only support 32-bit addresses, even on 64-bit
		 * SoCs. If there is no IOMMU to translate into a 32-bit IO
		 * virtual address space, force allocations to be in the
		 * lower 32-bit range.
		 */
		gfp |= GFP_DMA;
	}

	virt = (void *)__get_free_pages(gfp, get_order(size));
	if (!virt)
		return ERR_PTR(-ENOMEM);

	if (!tegra->domain) {
		/*
		 * If IOMMU is disabled, devices address physical memory
		 * directly.
		 */
		*dma = virt_to_phys(virt);
		return virt;
	}

	alloc = alloc_iova(&tegra->carveout.domain,
			   size >> tegra->carveout.shift,
			   tegra->carveout.limit, true);
	if (!alloc) {
		err = -EBUSY;
		goto free_pages;
	}

	*dma = iova_dma_addr(&tegra->carveout.domain, alloc);
	err = iommu_map(tegra->domain, *dma, virt_to_phys(virt),
			size, IOMMU_READ | IOMMU_WRITE);
	if (err < 0)
		goto free_iova;

	return virt;

free_iova:
	__free_iova(&tegra->carveout.domain, alloc);
free_pages:
	free_pages((unsigned long)virt, get_order(size));

	return ERR_PTR(err);
}

void tegra_drm_free(struct tegra_drm *tegra, size_t size, void *virt,
		    dma_addr_t dma)
{
	if (tegra->domain)
		size = iova_align(&tegra->carveout.domain, size);
	else
		size = PAGE_ALIGN(size);

	if (tegra->domain) {
		iommu_unmap(tegra->domain, dma, size);
		free_iova(&tegra->carveout.domain,
			  iova_pfn(&tegra->carveout.domain, dma));
	}

	free_pages((unsigned long)virt, get_order(size));
}

static int host1x_drm_probe(struct host1x_device *dev)
{
	struct drm_driver *driver = &tegra_drm_driver;
	struct drm_device *drm;
	int err;

	drm = drm_dev_alloc(driver, &dev->dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	dev_set_drvdata(&dev->dev, drm);

	err = drm_dev_register(drm, 0);
	if (err < 0)
		goto unref;

	return 0;

unref:
	drm_dev_unref(drm);
	return err;
}

static int host1x_drm_remove(struct host1x_device *dev)
{
	struct drm_device *drm = dev_get_drvdata(&dev->dev);

	drm_dev_unregister(drm);
	drm_dev_unref(drm);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int host1x_drm_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct tegra_drm *tegra = drm->dev_private;

	drm_kms_helper_poll_disable(drm);
	tegra_drm_fb_suspend(drm);

	tegra->state = drm_atomic_helper_suspend(drm);
	if (IS_ERR(tegra->state)) {
		tegra_drm_fb_resume(drm);
		drm_kms_helper_poll_enable(drm);
		return PTR_ERR(tegra->state);
	}

	return 0;
}

static int host1x_drm_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct tegra_drm *tegra = drm->dev_private;

	drm_atomic_helper_resume(drm, tegra->state);
	tegra_drm_fb_resume(drm);
	drm_kms_helper_poll_enable(drm);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(host1x_drm_pm_ops, host1x_drm_suspend,
			 host1x_drm_resume);

static const struct of_device_id host1x_drm_subdevs[] = {
	{ .compatible = "nvidia,tegra20-dc", },
	{ .compatible = "nvidia,tegra20-hdmi", },
	{ .compatible = "nvidia,tegra20-gr2d", },
	{ .compatible = "nvidia,tegra20-gr3d", },
	{ .compatible = "nvidia,tegra30-dc", },
	{ .compatible = "nvidia,tegra30-hdmi", },
	{ .compatible = "nvidia,tegra30-gr2d", },
	{ .compatible = "nvidia,tegra30-gr3d", },
	{ .compatible = "nvidia,tegra114-dsi", },
	{ .compatible = "nvidia,tegra114-hdmi", },
	{ .compatible = "nvidia,tegra114-gr3d", },
	{ .compatible = "nvidia,tegra124-dc", },
	{ .compatible = "nvidia,tegra124-sor", },
	{ .compatible = "nvidia,tegra124-hdmi", },
	{ .compatible = "nvidia,tegra124-dsi", },
	{ .compatible = "nvidia,tegra124-vic", },
	{ .compatible = "nvidia,tegra132-dsi", },
	{ .compatible = "nvidia,tegra210-dc", },
	{ .compatible = "nvidia,tegra210-dsi", },
	{ .compatible = "nvidia,tegra210-sor", },
	{ .compatible = "nvidia,tegra210-sor1", },
	{ .compatible = "nvidia,tegra210-vic", },
	{ /* sentinel */ }
};

static struct host1x_driver host1x_drm_driver = {
	.driver = {
		.name = "drm",
		.pm = &host1x_drm_pm_ops,
	},
	.probe = host1x_drm_probe,
	.remove = host1x_drm_remove,
	.subdevs = host1x_drm_subdevs,
};

static struct platform_driver * const drivers[] = {
	&tegra_dc_driver,
	&tegra_hdmi_driver,
	&tegra_dsi_driver,
	&tegra_dpaux_driver,
	&tegra_sor_driver,
	&tegra_gr2d_driver,
	&tegra_gr3d_driver,
	&tegra_vic_driver,
};

static int __init host1x_drm_init(void)
{
	int err;

	err = host1x_driver_register(&host1x_drm_driver);
	if (err < 0)
		return err;

	err = platform_register_drivers(drivers, ARRAY_SIZE(drivers));
	if (err < 0)
		goto unregister_host1x;

	return 0;

unregister_host1x:
	host1x_driver_unregister(&host1x_drm_driver);
	return err;
}
module_init(host1x_drm_init);

static void __exit host1x_drm_exit(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
	host1x_driver_unregister(&host1x_drm_driver);
}
module_exit(host1x_drm_exit);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_DESCRIPTION("NVIDIA Tegra DRM driver");
MODULE_LICENSE("GPL v2");
