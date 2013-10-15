/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "sw_sync.h"

#include <video/adf.h>
#include <video/adf_client.h>
#include <video/adf_format.h>

#include "adf.h"

static inline bool vsync_active(u8 state)
{
	return state == DRM_MODE_DPMS_ON || state == DRM_MODE_DPMS_STANDBY;
}

/**
 * adf_interface_blank - set interface's DPMS state
 *
 * @intf: the interface
 * @state: one of %DRM_MODE_DPMS_*
 *
 * Returns 0 on success or -errno on failure.
 */
int adf_interface_blank(struct adf_interface *intf, u8 state)
{
	struct adf_device *dev = adf_interface_parent(intf);
	u8 prev_state;
	bool disable_vsync;
	bool enable_vsync;
	int ret = 0;
	struct adf_event_refcount *vsync_refcount;

	if (!intf->ops || !intf->ops->blank)
		return -EOPNOTSUPP;

	mutex_lock(&dev->client_lock);
	if (state != DRM_MODE_DPMS_ON)
		flush_kthread_worker(&dev->post_worker);
	mutex_lock(&intf->base.event_lock);

	vsync_refcount = adf_obj_find_event_refcount(&intf->base,
			ADF_EVENT_VSYNC);
	if (!vsync_refcount) {
		ret = -ENOMEM;
		goto done;
	}

	prev_state = intf->dpms_state;
	if (prev_state == state) {
		ret = -EBUSY;
		goto done;
	}

	disable_vsync = vsync_active(prev_state) &&
			!vsync_active(state) &&
			vsync_refcount->refcount;
	enable_vsync = !vsync_active(prev_state) &&
			vsync_active(state) &&
			vsync_refcount->refcount;

	if (disable_vsync)
		intf->base.ops->set_event(&intf->base, ADF_EVENT_VSYNC,
				false);

	ret = intf->ops->blank(intf, state);
	if (ret < 0) {
		if (disable_vsync)
			intf->base.ops->set_event(&intf->base, ADF_EVENT_VSYNC,
					true);
		goto done;
	}

	if (enable_vsync)
		intf->base.ops->set_event(&intf->base, ADF_EVENT_VSYNC,
				true);

	intf->dpms_state = state;
done:
	mutex_unlock(&intf->base.event_lock);
	mutex_unlock(&dev->client_lock);
	return ret;
}
EXPORT_SYMBOL(adf_interface_blank);

/**
 * adf_interface_blank - get interface's current DPMS state
 *
 * @intf: the interface
 *
 * Returns one of %DRM_MODE_DPMS_*.
 */
u8 adf_interface_dpms_state(struct adf_interface *intf)
{
	struct adf_device *dev = adf_interface_parent(intf);
	u8 dpms_state;

	mutex_lock(&dev->client_lock);
	dpms_state = intf->dpms_state;
	mutex_unlock(&dev->client_lock);

	return dpms_state;
}
EXPORT_SYMBOL(adf_interface_dpms_state);

/**
 * adf_interface_current_mode - get interface's current display mode
 *
 * @intf: the interface
 * @mode: returns the current mode
 */
void adf_interface_current_mode(struct adf_interface *intf,
		struct drm_mode_modeinfo *mode)
{
	struct adf_device *dev = adf_interface_parent(intf);

	mutex_lock(&dev->client_lock);
	memcpy(mode, &intf->current_mode, sizeof(*mode));
	mutex_unlock(&dev->client_lock);
}
EXPORT_SYMBOL(adf_interface_current_mode);

/**
 * adf_interface_modelist - get interface's modelist
 *
 * @intf: the interface
 * @modelist: storage for the modelist (optional)
 * @n_modes: length of @modelist
 *
 * If @modelist is not NULL, adf_interface_modelist() will copy up to @n_modes
 * modelist entries into @modelist.
 *
 * Returns the length of the modelist.
 */
size_t adf_interface_modelist(struct adf_interface *intf,
		struct drm_mode_modeinfo *modelist, size_t n_modes)
{
	unsigned long flags;
	size_t retval;

	read_lock_irqsave(&intf->hotplug_modelist_lock, flags);
	if (modelist)
		memcpy(modelist, intf->modelist, sizeof(modelist[0]) *
				min(n_modes, intf->n_modes));
	retval = intf->n_modes;
	read_unlock_irqrestore(&intf->hotplug_modelist_lock, flags);

	return retval;
}
EXPORT_SYMBOL(adf_interface_modelist);

/**
 * adf_interface_set_mode - set interface's display mode
 *
 * @intf: the interface
 * @mode: the new mode
 *
 * Returns 0 on success or -errno on failure.
 */
int adf_interface_set_mode(struct adf_interface *intf,
		struct drm_mode_modeinfo *mode)
{
	struct adf_device *dev = adf_interface_parent(intf);
	int ret = 0;

	if (!intf->ops || !intf->ops->modeset)
		return -EOPNOTSUPP;

	mutex_lock(&dev->client_lock);
	flush_kthread_worker(&dev->post_worker);

	ret = intf->ops->modeset(intf, mode);
	if (ret < 0)
		goto done;

	memcpy(&intf->current_mode, mode, sizeof(*mode));
done:
	mutex_unlock(&dev->client_lock);
	return ret;
}
EXPORT_SYMBOL(adf_interface_set_mode);

/**
 * adf_interface_screen_size - get size of screen connected to interface
 *
 * @intf: the interface
 * @width_mm: returns the screen width in mm
 * @height_mm: returns the screen width in mm
 *
 * Returns 0 on success or -errno on failure.
 */
int adf_interface_get_screen_size(struct adf_interface *intf, u16 *width_mm,
		u16 *height_mm)
{
	struct adf_device *dev = adf_interface_parent(intf);
	int ret;

	if (!intf->ops || !intf->ops->screen_size)
		return -EOPNOTSUPP;

	mutex_lock(&dev->client_lock);
	ret = intf->ops->screen_size(intf, width_mm, height_mm);
	mutex_unlock(&dev->client_lock);

	return ret;
}
EXPORT_SYMBOL(adf_interface_get_screen_size);

/**
 * adf_overlay_engine_supports_format - returns whether a format is in an
 * overlay engine's supported list
 *
 * @eng: the overlay engine
 * @format: format fourcc
 */
bool adf_overlay_engine_supports_format(struct adf_overlay_engine *eng,
		u32 format)
{
	size_t i;
	for (i = 0; i < eng->ops->n_supported_formats; i++)
		if (format == eng->ops->supported_formats[i])
			return true;

	return false;
}
EXPORT_SYMBOL(adf_overlay_engine_supports_format);

static int adf_buffer_validate(struct adf_buffer *buf)
{
	struct adf_overlay_engine *eng = buf->overlay_engine;
	struct device *dev = &eng->base.dev;
	struct adf_device *parent = adf_overlay_engine_parent(eng);
	u8 hsub, vsub, num_planes, cpp[ADF_MAX_PLANES], i;

	if (!adf_overlay_engine_supports_format(eng, buf->format)) {
		char format_str[ADF_FORMAT_STR_SIZE];
		adf_format_str(buf->format, format_str);
		dev_err(dev, "unsupported format %s\n", format_str);
		return -EINVAL;
	}

	if (!adf_format_is_standard(buf->format))
		return parent->ops->validate_custom_format(parent, buf);

	hsub = adf_format_horz_chroma_subsampling(buf->format);
	vsub = adf_format_vert_chroma_subsampling(buf->format);
	num_planes = adf_format_num_planes(buf->format);
	for (i = 0; i < num_planes; i++)
		cpp[i] = adf_format_plane_cpp(buf->format, i);

	return adf_format_validate_yuv(parent, buf, num_planes, hsub, vsub,
			cpp);
}

static int adf_buffer_map(struct adf_device *dev, struct adf_buffer *buf,
		struct adf_buffer_mapping *mapping)
{
	int ret = 0;
	size_t i;

	for (i = 0; i < buf->n_planes; i++) {
		struct dma_buf_attachment *attachment;
		struct sg_table *sg_table;

		attachment = dma_buf_attach(buf->dma_bufs[i], dev->dev);
		if (IS_ERR(attachment)) {
			ret = PTR_ERR(attachment);
			dev_err(&dev->base.dev, "attaching plane %u failed: %d\n",
					i, ret);
			goto done;
		}
		mapping->attachments[i] = attachment;

		sg_table = dma_buf_map_attachment(attachment, DMA_TO_DEVICE);
		if (IS_ERR(sg_table)) {
			ret = PTR_ERR(sg_table);
			dev_err(&dev->base.dev, "mapping plane %u failed: %d",
					i, ret);
			goto done;
		} else if (!sg_table) {
			ret = -ENOMEM;
			dev_err(&dev->base.dev, "mapping plane %u failed\n", i);
			goto done;
		}
		mapping->sg_tables[i] = sg_table;
	}

done:
	if (ret < 0)
		adf_buffer_mapping_cleanup(mapping, buf);

	return ret;
}

static struct sync_fence *adf_sw_complete_fence(struct adf_device *dev)
{
	struct sync_pt *pt;
	struct sync_fence *complete_fence;

	if (!dev->timeline) {
		dev->timeline = sw_sync_timeline_create(dev->base.name);
		if (!dev->timeline)
			return ERR_PTR(-ENOMEM);
		dev->timeline_max = 1;
	}

	dev->timeline_max++;
	pt = sw_sync_pt_create(dev->timeline, dev->timeline_max);
	if (!pt)
		goto err_pt_create;
	complete_fence = sync_fence_create(dev->base.name, pt);
	if (!complete_fence)
		goto err_fence_create;

	return complete_fence;

err_fence_create:
	sync_pt_free(pt);
err_pt_create:
	dev->timeline_max--;
	return ERR_PTR(-ENOSYS);
}

/**
 * adf_device_post - flip to a new set of buffers
 *
 * @dev: device targeted by the flip
 * @intfs: interfaces targeted by the flip
 * @n_intfs: number of targeted interfaces
 * @bufs: description of buffers displayed
 * @n_bufs: number of buffers displayed
 * @custom_data: driver-private data
 * @custom_data_size: size of driver-private data
 *
 * adf_device_post() will copy @intfs, @bufs, and @custom_data, so they may
 * point to variables on the stack.  adf_device_post() also takes its own
 * reference on each of the dma-bufs in @bufs.  The adf_device_post_nocopy()
 * variant transfers ownership of these resources to ADF instead.
 *
 * On success, returns a sync fence which signals when the buffers are removed
 * from the screen.  On failure, returns ERR_PTR(-errno).
 */
struct sync_fence *adf_device_post(struct adf_device *dev,
		struct adf_interface **intfs, size_t n_intfs,
		struct adf_buffer *bufs, size_t n_bufs, void *custom_data,
		size_t custom_data_size)
{
	struct adf_interface **intfs_copy = NULL;
	struct adf_buffer *bufs_copy = NULL;
	void *custom_data_copy = NULL;
	struct sync_fence *ret;
	size_t i;

	intfs_copy = kzalloc(sizeof(intfs_copy[0]) * n_intfs, GFP_KERNEL);
	if (!intfs_copy)
		return ERR_PTR(-ENOMEM);

	bufs_copy = kzalloc(sizeof(bufs_copy[0]) * n_bufs, GFP_KERNEL);
	if (!bufs_copy) {
		ret = ERR_PTR(-ENOMEM);
		goto err_alloc;
	}

	custom_data_copy = kzalloc(custom_data_size, GFP_KERNEL);
	if (!custom_data_copy) {
		ret = ERR_PTR(-ENOMEM);
		goto err_alloc;
	}

	for (i = 0; i < n_bufs; i++) {
		size_t j;
		for (j = 0; j < bufs[i].n_planes; j++)
			get_dma_buf(bufs[i].dma_bufs[j]);
	}

	memcpy(intfs_copy, intfs, sizeof(intfs_copy[0]) * n_intfs);
	memcpy(bufs_copy, bufs, sizeof(bufs_copy[0]) * n_bufs);
	memcpy(custom_data_copy, custom_data, custom_data_size);

	ret = adf_device_post_nocopy(dev, intfs_copy, n_intfs, bufs_copy,
			n_bufs, custom_data_copy, custom_data_size);
	if (IS_ERR(ret))
		goto err_post;

	return ret;

err_post:
	for (i = 0; i < n_bufs; i++) {
		size_t j;
		for (j = 0; j < bufs[i].n_planes; j++)
			dma_buf_put(bufs[i].dma_bufs[j]);
	}
err_alloc:
	kfree(custom_data_copy);
	kfree(bufs_copy);
	kfree(intfs_copy);
	return ret;
}
EXPORT_SYMBOL(adf_device_post);

/**
 * adf_device_post_nocopy - flip to a new set of buffers
 *
 * adf_device_post_nocopy() has the same behavior as adf_device_post(),
 * except ADF does not copy @intfs, @bufs, or @custom_data, and it does
 * not take an extra reference on the dma-bufs in @bufs.
 *
 * @intfs, @bufs, and @custom_data must point to buffers allocated by
 * kmalloc().  On success, ADF takes ownership of these buffers and the dma-bufs
 * in @bufs, and will kfree()/dma_buf_put() them when they are no longer needed.
 * On failure, adf_device_post_nocopy() does NOT take ownership of these
 * buffers or the dma-bufs, and the caller must clean them up.
 *
 * adf_device_post_nocopy() is mainly intended for implementing ADF's ioctls.
 * Clients may find the nocopy variant useful in limited cases, but most should
 * call adf_device_post() instead.
 */
struct sync_fence *adf_device_post_nocopy(struct adf_device *dev,
		struct adf_interface **intfs, size_t n_intfs,
		struct adf_buffer *bufs, size_t n_bufs,
		void *custom_data, size_t custom_data_size)
{
	struct adf_pending_post *cfg;
	struct adf_buffer_mapping *mappings;
	struct sync_fence *ret;
	size_t i;
	int err;

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return ERR_PTR(-ENOMEM);

	mappings = kzalloc(sizeof(mappings[0]) * n_bufs, GFP_KERNEL);
	if (!mappings) {
		ret = ERR_PTR(-ENOMEM);
		goto err_alloc;
	}

	mutex_lock(&dev->client_lock);

	for (i = 0; i < n_bufs; i++) {
		err = adf_buffer_validate(&bufs[i]);
		if (err < 0) {
			ret = ERR_PTR(err);
			goto err_buf;
		}

		err = adf_buffer_map(dev, &bufs[i], &mappings[i]);
		if (err < 0) {
			ret = ERR_PTR(err);
			goto err_buf;
		}
	}

	INIT_LIST_HEAD(&cfg->head);
	cfg->config.n_bufs = n_bufs;
	cfg->config.bufs = bufs;
	cfg->config.mappings = mappings;
	cfg->config.custom_data = custom_data;
	cfg->config.custom_data_size = custom_data_size;

	err = dev->ops->validate(dev, &cfg->config, &cfg->state);
	if (err < 0) {
		ret = ERR_PTR(err);
		goto err_buf;
	}

	mutex_lock(&dev->post_lock);

	if (dev->ops->complete_fence)
		ret = dev->ops->complete_fence(dev, &cfg->config,
				cfg->state);
	else
		ret = adf_sw_complete_fence(dev);

	if (IS_ERR(ret))
		goto err_fence;

	list_add_tail(&cfg->head, &dev->post_list);
	queue_kthread_work(&dev->post_worker, &dev->post_work);
	mutex_unlock(&dev->post_lock);
	mutex_unlock(&dev->client_lock);
	kfree(intfs);
	return ret;

err_fence:
	mutex_unlock(&dev->post_lock);

err_buf:
	for (i = 0; i < n_bufs; i++)
		adf_buffer_mapping_cleanup(&mappings[i], &bufs[i]);

	mutex_unlock(&dev->client_lock);
	kfree(mappings);

err_alloc:
	kfree(cfg);
	return ret;
}
EXPORT_SYMBOL(adf_device_post_nocopy);

static void adf_attachment_list_to_array(struct adf_device *dev,
		struct list_head *src, struct adf_attachment *dst, size_t size)
{
	struct adf_attachment_list *entry;
	size_t i = 0;

	if (!dst)
		return;

	list_for_each_entry(entry, src, head) {
		if (i == size)
			return;
		dst[i] = entry->attachment;
		i++;
	}
}

/**
 * adf_device_attachments - get device's list of active attachments
 *
 * @dev: the device
 * @attachments: storage for the attachment list (optional)
 * @n_attachments: length of @attachments
 *
 * If @attachments is not NULL, adf_device_attachments() will copy up to
 * @n_attachments entries into @attachments.
 *
 * Returns the length of the active attachment list.
 */
size_t adf_device_attachments(struct adf_device *dev,
		struct adf_attachment *attachments, size_t n_attachments)
{
	size_t retval;

	mutex_lock(&dev->client_lock);
	adf_attachment_list_to_array(dev, &dev->attached, attachments,
			n_attachments);
	retval = dev->n_attached;
	mutex_unlock(&dev->client_lock);

	return retval;
}
EXPORT_SYMBOL(adf_device_attachments);

/**
 * adf_device_attachments_allowed - get device's list of allowed attachments
 *
 * @dev: the device
 * @attachments: storage for the attachment list (optional)
 * @n_attachments: length of @attachments
 *
 * If @attachments is not NULL, adf_device_attachments_allowed() will copy up to
 * @n_attachments entries into @attachments.
 *
 * Returns the length of the allowed attachment list.
 */
size_t adf_device_attachments_allowed(struct adf_device *dev,
		struct adf_attachment *attachments, size_t n_attachments)
{
	size_t retval;

	mutex_lock(&dev->client_lock);
	adf_attachment_list_to_array(dev, &dev->attach_allowed, attachments,
			n_attachments);
	retval = dev->n_attach_allowed;
	mutex_unlock(&dev->client_lock);

	return retval;
}
EXPORT_SYMBOL(adf_device_attachments_allowed);

/**
 * adf_device_attached - return whether an overlay engine and interface are
 * attached
 *
 * @dev: the parent device
 * @eng: the overlay engine
 * @intf: the interface
 */
bool adf_device_attached(struct adf_device *dev, struct adf_overlay_engine *eng,
		struct adf_interface *intf)
{
	struct adf_attachment_list *attachment;

	mutex_lock(&dev->client_lock);
	attachment = adf_attachment_find(&dev->attached, eng, intf);
	mutex_unlock(&dev->client_lock);

	return attachment != NULL;
}
EXPORT_SYMBOL(adf_device_attached);

/**
 * adf_device_attach_allowed - return whether the ADF device supports attaching
 * an overlay engine and interface
 *
 * @dev: the parent device
 * @eng: the overlay engine
 * @intf: the interface
 */
bool adf_device_attach_allowed(struct adf_device *dev,
		struct adf_overlay_engine *eng, struct adf_interface *intf)
{
	struct adf_attachment_list *attachment;

	mutex_lock(&dev->client_lock);
	attachment = adf_attachment_find(&dev->attach_allowed, eng, intf);
	mutex_unlock(&dev->client_lock);

	return attachment != NULL;
}
EXPORT_SYMBOL(adf_device_attach_allowed);
/**
 * adf_device_attach - attach an overlay engine to an interface
 *
 * @dev: the parent device
 * @eng: the overlay engine
 * @intf: the interface
 *
 * Returns 0 on success, -%EINVAL if attaching @intf and @eng is not allowed,
 * -%EALREADY if @intf and @eng are already attached, or -errno on any other
 * failure.
 */
int adf_device_attach(struct adf_device *dev, struct adf_overlay_engine *eng,
		struct adf_interface *intf)
{
	int ret;
	struct adf_attachment_list *attachment = NULL;

	ret = adf_attachment_validate(dev, eng, intf);
	if (ret < 0)
		return ret;

	mutex_lock(&dev->client_lock);

	if (dev->n_attached == ADF_MAX_ATTACHMENTS) {
		ret = -ENOMEM;
		goto done;
	}

	if (!adf_attachment_find(&dev->attach_allowed, eng, intf)) {
		ret = -EINVAL;
		goto done;
	}

	if (adf_attachment_find(&dev->attached, eng, intf)) {
		ret = -EALREADY;
		goto done;
	}

	ret = adf_device_attach_op(dev, eng, intf);
	if (ret < 0)
		goto done;

	attachment = kzalloc(sizeof(*attachment), GFP_KERNEL);
	if (!attachment) {
		ret = -ENOMEM;
		goto done;
	}

	attachment->attachment.interface = intf;
	attachment->attachment.overlay_engine = eng;
	list_add_tail(&attachment->head, &dev->attached);
	dev->n_attached++;

done:
	mutex_unlock(&dev->client_lock);
	if (ret < 0)
		kfree(attachment);

	return ret;
}
EXPORT_SYMBOL(adf_device_attach);

/**
 * adf_device_detach - detach an overlay engine from an interface
 *
 * @dev: the parent device
 * @eng: the overlay engine
 * @intf: the interface
 *
 * Returns 0 on success, -%EINVAL if @intf and @eng are not attached,
 * or -errno on any other failure.
 */
int adf_device_detach(struct adf_device *dev, struct adf_overlay_engine *eng,
		struct adf_interface *intf)
{
	int ret;
	struct adf_attachment_list *attachment;

	ret = adf_attachment_validate(dev, eng, intf);
	if (ret < 0)
		return ret;

	mutex_lock(&dev->client_lock);

	attachment = adf_attachment_find(&dev->attached, eng, intf);
	if (!attachment) {
		ret = -EINVAL;
		goto done;
	}

	ret = adf_device_detach_op(dev, eng, intf);
	if (ret < 0)
		goto done;

	adf_attachment_free(attachment);
	dev->n_attached--;
done:
	mutex_unlock(&dev->client_lock);
	return ret;
}
EXPORT_SYMBOL(adf_device_detach);

/**
 * adf_interface_simple_buffer_alloc - allocate a simple buffer
 *
 * @intf: target interface
 * @w: width in pixels
 * @h: height in pixels
 * @format: format fourcc
 * @dma_buf: returns the allocated buffer
 * @offset: returns the byte offset of the allocated buffer's first pixel
 * @pitch: returns the allocated buffer's pitch
 *
 * See &struct adf_simple_buffer_alloc for a description of simple buffers and
 * their limitations.
 *
 * Returns 0 on success or -errno on failure.
 */
int adf_interface_simple_buffer_alloc(struct adf_interface *intf, u16 w, u16 h,
		u32 format, struct dma_buf **dma_buf, u32 *offset, u32 *pitch)
{
	if (!intf->ops || !intf->ops->alloc_simple_buffer)
		return -EOPNOTSUPP;

	if (!adf_format_is_rgb(format))
		return -EINVAL;

	return intf->ops->alloc_simple_buffer(intf, w, h, format, dma_buf,
			offset, pitch);
}
EXPORT_SYMBOL(adf_interface_simple_buffer_alloc);

/**
 * adf_interface_simple_post - flip to a single buffer
 *
 * @intf: interface targeted by the flip
 * @buf: buffer to display
 *
 * adf_interface_simple_post() can be used generically for simple display
 * configurations, since the client does not need to provide any driver-private
 * configuration data.
 *
 * adf_interface_simple_post() has the same copying semantics as
 * adf_device_post().
 *
 * On success, returns a sync fence which signals when the buffer is removed
 * from the screen.  On failure, returns ERR_PTR(-errno).
 */
struct sync_fence *adf_interface_simple_post(struct adf_interface *intf,
		struct adf_buffer *buf)
{
	size_t custom_data_size = 0;
	void *custom_data = NULL;
	struct sync_fence *ret;

	if (intf->ops && intf->ops->describe_simple_post) {
		int err;

		custom_data = kzalloc(ADF_MAX_CUSTOM_DATA_SIZE, GFP_KERNEL);
		if (!custom_data) {
			ret = ERR_PTR(-ENOMEM);
			goto done;
		}

		err = intf->ops->describe_simple_post(intf, buf, custom_data,
				&custom_data_size);
		if (err < 0) {
			ret = ERR_PTR(err);
			goto done;
		}
	}

	ret = adf_device_post(adf_interface_parent(intf), &intf, 1, buf, 1,
			custom_data, custom_data_size);
done:
	kfree(custom_data);
	return ret;
}
EXPORT_SYMBOL(adf_interface_simple_post);
