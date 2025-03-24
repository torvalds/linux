// SPDX-License-Identifier: GPL-2.0 or MIT
/*
 * Copyright 2018 Noralf Trønnes
 */

#include <linux/iosys-map.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <drm/drm_client.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_mode.h>
#include <drm/drm_print.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

/**
 * DOC: overview
 *
 * This library provides support for clients running in the kernel like fbdev and bootsplash.
 *
 * GEM drivers which provide a GEM based dumb buffer with a virtual address are supported.
 */

static int drm_client_open(struct drm_client_dev *client)
{
	struct drm_device *dev = client->dev;
	struct drm_file *file;

	file = drm_file_alloc(dev->primary);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&dev->filelist_mutex);
	list_add(&file->lhead, &dev->filelist_internal);
	mutex_unlock(&dev->filelist_mutex);

	client->file = file;

	return 0;
}

static void drm_client_close(struct drm_client_dev *client)
{
	struct drm_device *dev = client->dev;

	mutex_lock(&dev->filelist_mutex);
	list_del(&client->file->lhead);
	mutex_unlock(&dev->filelist_mutex);

	drm_file_free(client->file);
}

/**
 * drm_client_init - Initialise a DRM client
 * @dev: DRM device
 * @client: DRM client
 * @name: Client name
 * @funcs: DRM client functions (optional)
 *
 * This initialises the client and opens a &drm_file.
 * Use drm_client_register() to complete the process.
 * The caller needs to hold a reference on @dev before calling this function.
 * The client is freed when the &drm_device is unregistered. See drm_client_release().
 *
 * Returns:
 * Zero on success or negative error code on failure.
 */
int drm_client_init(struct drm_device *dev, struct drm_client_dev *client,
		    const char *name, const struct drm_client_funcs *funcs)
{
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET) || !dev->driver->dumb_create)
		return -EOPNOTSUPP;

	client->dev = dev;
	client->name = name;
	client->funcs = funcs;

	ret = drm_client_modeset_create(client);
	if (ret)
		return ret;

	ret = drm_client_open(client);
	if (ret)
		goto err_free;

	drm_dev_get(dev);

	return 0;

err_free:
	drm_client_modeset_free(client);
	return ret;
}
EXPORT_SYMBOL(drm_client_init);

/**
 * drm_client_register - Register client
 * @client: DRM client
 *
 * Add the client to the &drm_device client list to activate its callbacks.
 * @client must be initialized by a call to drm_client_init(). After
 * drm_client_register() it is no longer permissible to call drm_client_release()
 * directly (outside the unregister callback), instead cleanup will happen
 * automatically on driver unload.
 *
 * Registering a client generates a hotplug event that allows the client
 * to set up its display from pre-existing outputs. The client must have
 * initialized its state to able to handle the hotplug event successfully.
 */
void drm_client_register(struct drm_client_dev *client)
{
	struct drm_device *dev = client->dev;
	int ret;

	mutex_lock(&dev->clientlist_mutex);
	list_add(&client->list, &dev->clientlist);

	if (client->funcs && client->funcs->hotplug) {
		/*
		 * Perform an initial hotplug event to pick up the
		 * display configuration for the client. This step
		 * has to be performed *after* registering the client
		 * in the list of clients, or a concurrent hotplug
		 * event might be lost; leaving the display off.
		 *
		 * Hold the clientlist_mutex as for a regular hotplug
		 * event.
		 */
		ret = client->funcs->hotplug(client);
		if (ret)
			drm_dbg_kms(dev, "client hotplug ret=%d\n", ret);
	}
	mutex_unlock(&dev->clientlist_mutex);
}
EXPORT_SYMBOL(drm_client_register);

/**
 * drm_client_release - Release DRM client resources
 * @client: DRM client
 *
 * Releases resources by closing the &drm_file that was opened by drm_client_init().
 * It is called automatically if the &drm_client_funcs.unregister callback is _not_ set.
 *
 * This function should only be called from the unregister callback. An exception
 * is fbdev which cannot free the buffer if userspace has open file descriptors.
 *
 * Note:
 * Clients cannot initiate a release by themselves. This is done to keep the code simple.
 * The driver has to be unloaded before the client can be unloaded.
 */
void drm_client_release(struct drm_client_dev *client)
{
	struct drm_device *dev = client->dev;

	drm_dbg_kms(dev, "%s\n", client->name);

	drm_client_modeset_free(client);
	drm_client_close(client);
	drm_dev_put(dev);
}
EXPORT_SYMBOL(drm_client_release);

static void drm_client_buffer_delete(struct drm_client_buffer *buffer)
{
	if (buffer->gem) {
		drm_gem_vunmap_unlocked(buffer->gem, &buffer->map);
		drm_gem_object_put(buffer->gem);
	}

	kfree(buffer);
}

static struct drm_client_buffer *
drm_client_buffer_create(struct drm_client_dev *client, u32 width, u32 height,
			 u32 format, u32 *handle)
{
	const struct drm_format_info *info = drm_format_info(format);
	struct drm_mode_create_dumb dumb_args = { };
	struct drm_device *dev = client->dev;
	struct drm_client_buffer *buffer;
	struct drm_gem_object *obj;
	int ret;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	buffer->client = client;

	dumb_args.width = width;
	dumb_args.height = height;
	dumb_args.bpp = drm_format_info_bpp(info, 0);
	ret = drm_mode_create_dumb(dev, &dumb_args, client->file);
	if (ret)
		goto err_delete;

	obj = drm_gem_object_lookup(client->file, dumb_args.handle);
	if (!obj)  {
		ret = -ENOENT;
		goto err_delete;
	}

	buffer->pitch = dumb_args.pitch;
	buffer->gem = obj;
	*handle = dumb_args.handle;

	return buffer;

err_delete:
	drm_client_buffer_delete(buffer);

	return ERR_PTR(ret);
}

/**
 * drm_client_buffer_vmap_local - Map DRM client buffer into address space
 * @buffer: DRM client buffer
 * @map_copy: Returns the mapped memory's address
 *
 * This function maps a client buffer into kernel address space. If the
 * buffer is already mapped, it returns the existing mapping's address.
 *
 * Client buffer mappings are not ref'counted. Each call to
 * drm_client_buffer_vmap_local() should be closely followed by a call to
 * drm_client_buffer_vunmap_local(). See drm_client_buffer_vmap() for
 * long-term mappings.
 *
 * The returned address is a copy of the internal value. In contrast to
 * other vmap interfaces, you don't need it for the client's vunmap
 * function. So you can modify it at will during blit and draw operations.
 *
 * Returns:
 *	0 on success, or a negative errno code otherwise.
 */
int drm_client_buffer_vmap_local(struct drm_client_buffer *buffer,
				 struct iosys_map *map_copy)
{
	struct drm_gem_object *gem = buffer->gem;
	struct iosys_map *map = &buffer->map;
	int ret;

	drm_gem_lock(gem);

	ret = drm_gem_vmap(gem, map);
	if (ret)
		goto err_drm_gem_vmap_unlocked;
	*map_copy = *map;

	return 0;

err_drm_gem_vmap_unlocked:
	drm_gem_unlock(gem);
	return ret;
}
EXPORT_SYMBOL(drm_client_buffer_vmap_local);

/**
 * drm_client_buffer_vunmap_local - Unmap DRM client buffer
 * @buffer: DRM client buffer
 *
 * This function removes a client buffer's memory mapping established
 * with drm_client_buffer_vunmap_local(). Calling this function is only
 * required by clients that manage their buffer mappings by themselves.
 */
void drm_client_buffer_vunmap_local(struct drm_client_buffer *buffer)
{
	struct drm_gem_object *gem = buffer->gem;
	struct iosys_map *map = &buffer->map;

	drm_gem_vunmap(gem, map);
	drm_gem_unlock(gem);
}
EXPORT_SYMBOL(drm_client_buffer_vunmap_local);

/**
 * drm_client_buffer_vmap - Map DRM client buffer into address space
 * @buffer: DRM client buffer
 * @map_copy: Returns the mapped memory's address
 *
 * This function maps a client buffer into kernel address space. If the
 * buffer is already mapped, it returns the existing mapping's address.
 *
 * Client buffer mappings are not ref'counted. Each call to
 * drm_client_buffer_vmap() should be followed by a call to
 * drm_client_buffer_vunmap(); or the client buffer should be mapped
 * throughout its lifetime.
 *
 * The returned address is a copy of the internal value. In contrast to
 * other vmap interfaces, you don't need it for the client's vunmap
 * function. So you can modify it at will during blit and draw operations.
 *
 * Returns:
 *	0 on success, or a negative errno code otherwise.
 */
int
drm_client_buffer_vmap(struct drm_client_buffer *buffer,
		       struct iosys_map *map_copy)
{
	struct drm_gem_object *gem = buffer->gem;
	struct iosys_map *map = &buffer->map;
	int ret;

	drm_gem_lock(gem);

	ret = drm_gem_pin_locked(gem);
	if (ret)
		goto err_drm_gem_pin_locked;
	ret = drm_gem_vmap(gem, map);
	if (ret)
		goto err_drm_gem_vmap;

	drm_gem_unlock(gem);

	*map_copy = *map;

	return 0;

err_drm_gem_vmap:
	drm_gem_unpin_locked(buffer->gem);
err_drm_gem_pin_locked:
	drm_gem_unlock(gem);
	return ret;
}
EXPORT_SYMBOL(drm_client_buffer_vmap);

/**
 * drm_client_buffer_vunmap - Unmap DRM client buffer
 * @buffer: DRM client buffer
 *
 * This function removes a client buffer's memory mapping. Calling this
 * function is only required by clients that manage their buffer mappings
 * by themselves.
 */
void drm_client_buffer_vunmap(struct drm_client_buffer *buffer)
{
	struct drm_gem_object *gem = buffer->gem;
	struct iosys_map *map = &buffer->map;

	drm_gem_lock(gem);
	drm_gem_vunmap(gem, map);
	drm_gem_unpin_locked(gem);
	drm_gem_unlock(gem);
}
EXPORT_SYMBOL(drm_client_buffer_vunmap);

static void drm_client_buffer_rmfb(struct drm_client_buffer *buffer)
{
	int ret;

	if (!buffer->fb)
		return;

	ret = drm_mode_rmfb(buffer->client->dev, buffer->fb->base.id, buffer->client->file);
	if (ret)
		drm_err(buffer->client->dev,
			"Error removing FB:%u (%d)\n", buffer->fb->base.id, ret);

	buffer->fb = NULL;
}

static int drm_client_buffer_addfb(struct drm_client_buffer *buffer,
				   u32 width, u32 height, u32 format,
				   u32 handle)
{
	struct drm_client_dev *client = buffer->client;
	struct drm_mode_fb_cmd2 fb_req = { };
	int ret;

	fb_req.width = width;
	fb_req.height = height;
	fb_req.pixel_format = format;
	fb_req.handles[0] = handle;
	fb_req.pitches[0] = buffer->pitch;

	ret = drm_mode_addfb2(client->dev, &fb_req, client->file);
	if (ret)
		return ret;

	buffer->fb = drm_framebuffer_lookup(client->dev, buffer->client->file, fb_req.fb_id);
	if (WARN_ON(!buffer->fb))
		return -ENOENT;

	/* drop the reference we picked up in framebuffer lookup */
	drm_framebuffer_put(buffer->fb);

	strscpy(buffer->fb->comm, client->name, TASK_COMM_LEN);

	return 0;
}

/**
 * drm_client_framebuffer_create - Create a client framebuffer
 * @client: DRM client
 * @width: Framebuffer width
 * @height: Framebuffer height
 * @format: Buffer format
 *
 * This function creates a &drm_client_buffer which consists of a
 * &drm_framebuffer backed by a dumb buffer.
 * Call drm_client_framebuffer_delete() to free the buffer.
 *
 * Returns:
 * Pointer to a client buffer or an error pointer on failure.
 */
struct drm_client_buffer *
drm_client_framebuffer_create(struct drm_client_dev *client, u32 width, u32 height, u32 format)
{
	struct drm_client_buffer *buffer;
	u32 handle;
	int ret;

	buffer = drm_client_buffer_create(client, width, height, format,
					  &handle);
	if (IS_ERR(buffer))
		return buffer;

	ret = drm_client_buffer_addfb(buffer, width, height, format, handle);

	/*
	 * The handle is only needed for creating the framebuffer, destroy it
	 * again to solve a circular dependency should anybody export the GEM
	 * object as DMA-buf. The framebuffer and our buffer structure are still
	 * holding references to the GEM object to prevent its destruction.
	 */
	drm_mode_destroy_dumb(client->dev, handle, client->file);

	if (ret) {
		drm_client_buffer_delete(buffer);
		return ERR_PTR(ret);
	}

	return buffer;
}
EXPORT_SYMBOL(drm_client_framebuffer_create);

/**
 * drm_client_framebuffer_delete - Delete a client framebuffer
 * @buffer: DRM client buffer (can be NULL)
 */
void drm_client_framebuffer_delete(struct drm_client_buffer *buffer)
{
	if (!buffer)
		return;

	drm_client_buffer_rmfb(buffer);
	drm_client_buffer_delete(buffer);
}
EXPORT_SYMBOL(drm_client_framebuffer_delete);

/**
 * drm_client_framebuffer_flush - Manually flush client framebuffer
 * @buffer: DRM client buffer (can be NULL)
 * @rect: Damage rectangle (if NULL flushes all)
 *
 * This calls &drm_framebuffer_funcs->dirty (if present) to flush buffer changes
 * for drivers that need it.
 *
 * Returns:
 * Zero on success or negative error code on failure.
 */
int drm_client_framebuffer_flush(struct drm_client_buffer *buffer, struct drm_rect *rect)
{
	if (!buffer || !buffer->fb || !buffer->fb->funcs->dirty)
		return 0;

	if (rect) {
		struct drm_clip_rect clip = {
			.x1 = rect->x1,
			.y1 = rect->y1,
			.x2 = rect->x2,
			.y2 = rect->y2,
		};

		return buffer->fb->funcs->dirty(buffer->fb, buffer->client->file,
						0, 0, &clip, 1);
	}

	return buffer->fb->funcs->dirty(buffer->fb, buffer->client->file,
					0, 0, NULL, 0);
}
EXPORT_SYMBOL(drm_client_framebuffer_flush);
