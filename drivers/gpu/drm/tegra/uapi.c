// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 NVIDIA Corporation */

#include <linux/host1x.h>
#include <linux/iommu.h>
#include <linux/list.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_utils.h>

#include "drm.h"
#include "uapi.h"

static void tegra_drm_mapping_release(struct kref *ref)
{
	struct tegra_drm_mapping *mapping =
		container_of(ref, struct tegra_drm_mapping, ref);

	host1x_bo_unpin(mapping->map);
	host1x_bo_put(mapping->bo);

	kfree(mapping);
}

void tegra_drm_mapping_put(struct tegra_drm_mapping *mapping)
{
	kref_put(&mapping->ref, tegra_drm_mapping_release);
}

static void tegra_drm_channel_context_close(struct tegra_drm_context *context)
{
	struct tegra_drm_mapping *mapping;
	unsigned long id;

	xa_for_each(&context->mappings, id, mapping)
		tegra_drm_mapping_put(mapping);

	xa_destroy(&context->mappings);

	host1x_channel_put(context->channel);

	kfree(context);
}

void tegra_drm_uapi_close_file(struct tegra_drm_file *file)
{
	struct tegra_drm_context *context;
	struct host1x_syncpt *sp;
	unsigned long id;

	xa_for_each(&file->contexts, id, context)
		tegra_drm_channel_context_close(context);

	xa_for_each(&file->syncpoints, id, sp)
		host1x_syncpt_put(sp);

	xa_destroy(&file->contexts);
	xa_destroy(&file->syncpoints);
}

static struct tegra_drm_client *tegra_drm_find_client(struct tegra_drm *tegra, u32 class)
{
	struct tegra_drm_client *client;

	list_for_each_entry(client, &tegra->clients, list)
		if (client->base.class == class)
			return client;

	return NULL;
}

int tegra_drm_ioctl_channel_open(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct tegra_drm *tegra = drm->dev_private;
	struct drm_tegra_channel_open *args = data;
	struct tegra_drm_client *client = NULL;
	struct tegra_drm_context *context;
	int err;

	if (args->flags)
		return -EINVAL;

	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	client = tegra_drm_find_client(tegra, args->host1x_class);
	if (!client) {
		err = -ENODEV;
		goto free;
	}

	if (client->shared_channel) {
		context->channel = host1x_channel_get(client->shared_channel);
	} else {
		context->channel = host1x_channel_request(&client->base);
		if (!context->channel) {
			err = -EBUSY;
			goto free;
		}
	}

	err = xa_alloc(&fpriv->contexts, &args->context, context, XA_LIMIT(1, U32_MAX),
		       GFP_KERNEL);
	if (err < 0)
		goto put_channel;

	context->client = client;
	xa_init_flags(&context->mappings, XA_FLAGS_ALLOC1);

	args->version = client->version;
	args->capabilities = 0;

	if (device_get_dma_attr(client->base.dev) == DEV_DMA_COHERENT)
		args->capabilities |= DRM_TEGRA_CHANNEL_CAP_CACHE_COHERENT;

	return 0;

put_channel:
	host1x_channel_put(context->channel);
free:
	kfree(context);

	return err;
}

int tegra_drm_ioctl_channel_close(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_channel_close *args = data;
	struct tegra_drm_context *context;

	mutex_lock(&fpriv->lock);

	context = xa_load(&fpriv->contexts, args->context);
	if (!context) {
		mutex_unlock(&fpriv->lock);
		return -EINVAL;
	}

	xa_erase(&fpriv->contexts, args->context);

	mutex_unlock(&fpriv->lock);

	tegra_drm_channel_context_close(context);

	return 0;
}

int tegra_drm_ioctl_channel_map(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_channel_map *args = data;
	struct tegra_drm_mapping *mapping;
	struct tegra_drm_context *context;
	enum dma_data_direction direction;
	int err = 0;

	if (args->flags & ~DRM_TEGRA_CHANNEL_MAP_READ_WRITE)
		return -EINVAL;

	mutex_lock(&fpriv->lock);

	context = xa_load(&fpriv->contexts, args->context);
	if (!context) {
		mutex_unlock(&fpriv->lock);
		return -EINVAL;
	}

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping) {
		err = -ENOMEM;
		goto unlock;
	}

	kref_init(&mapping->ref);

	mapping->bo = tegra_gem_lookup(file, args->handle);
	if (!mapping->bo) {
		err = -EINVAL;
		goto free;
	}

	switch (args->flags & DRM_TEGRA_CHANNEL_MAP_READ_WRITE) {
	case DRM_TEGRA_CHANNEL_MAP_READ_WRITE:
		direction = DMA_BIDIRECTIONAL;
		break;

	case DRM_TEGRA_CHANNEL_MAP_WRITE:
		direction = DMA_FROM_DEVICE;
		break;

	case DRM_TEGRA_CHANNEL_MAP_READ:
		direction = DMA_TO_DEVICE;
		break;

	default:
		err = -EINVAL;
		goto put_gem;
	}

	mapping->map = host1x_bo_pin(context->client->base.dev, mapping->bo, direction, NULL);
	if (IS_ERR(mapping->map)) {
		err = PTR_ERR(mapping->map);
		goto put_gem;
	}

	mapping->iova = mapping->map->phys;
	mapping->iova_end = mapping->iova + host1x_to_tegra_bo(mapping->bo)->gem.size;

	err = xa_alloc(&context->mappings, &args->mapping, mapping, XA_LIMIT(1, U32_MAX),
		       GFP_KERNEL);
	if (err < 0)
		goto unpin;

	mutex_unlock(&fpriv->lock);

	return 0;

unpin:
	host1x_bo_unpin(mapping->map);
put_gem:
	host1x_bo_put(mapping->bo);
free:
	kfree(mapping);
unlock:
	mutex_unlock(&fpriv->lock);
	return err;
}

int tegra_drm_ioctl_channel_unmap(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_channel_unmap *args = data;
	struct tegra_drm_mapping *mapping;
	struct tegra_drm_context *context;

	mutex_lock(&fpriv->lock);

	context = xa_load(&fpriv->contexts, args->context);
	if (!context) {
		mutex_unlock(&fpriv->lock);
		return -EINVAL;
	}

	mapping = xa_erase(&context->mappings, args->mapping);

	mutex_unlock(&fpriv->lock);

	if (!mapping)
		return -EINVAL;

	tegra_drm_mapping_put(mapping);
	return 0;
}

int tegra_drm_ioctl_syncpoint_allocate(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct host1x *host1x = tegra_drm_to_host1x(drm->dev_private);
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_syncpoint_allocate *args = data;
	struct host1x_syncpt *sp;
	int err;

	if (args->id)
		return -EINVAL;

	sp = host1x_syncpt_alloc(host1x, HOST1X_SYNCPT_CLIENT_MANAGED, current->comm);
	if (!sp)
		return -EBUSY;

	args->id = host1x_syncpt_id(sp);

	err = xa_insert(&fpriv->syncpoints, args->id, sp, GFP_KERNEL);
	if (err) {
		host1x_syncpt_put(sp);
		return err;
	}

	return 0;
}

int tegra_drm_ioctl_syncpoint_free(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct tegra_drm_file *fpriv = file->driver_priv;
	struct drm_tegra_syncpoint_allocate *args = data;
	struct host1x_syncpt *sp;

	mutex_lock(&fpriv->lock);
	sp = xa_erase(&fpriv->syncpoints, args->id);
	mutex_unlock(&fpriv->lock);

	if (!sp)
		return -EINVAL;

	host1x_syncpt_put(sp);

	return 0;
}

int tegra_drm_ioctl_syncpoint_wait(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct host1x *host1x = tegra_drm_to_host1x(drm->dev_private);
	struct drm_tegra_syncpoint_wait *args = data;
	signed long timeout_jiffies;
	struct host1x_syncpt *sp;

	if (args->padding != 0)
		return -EINVAL;

	sp = host1x_syncpt_get_by_id_noref(host1x, args->id);
	if (!sp)
		return -EINVAL;

	timeout_jiffies = drm_timeout_abs_to_jiffies(args->timeout_ns);

	return host1x_syncpt_wait(sp, args->threshold, timeout_jiffies, &args->value);
}
