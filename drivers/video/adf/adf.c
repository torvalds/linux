/*
 * Copyright (C) 2013 Google, Inc.
 * adf_modeinfo_{set_name,set_vrefresh} modified from
 * drivers/gpu/drm/drm_modes.c
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

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/highmem.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <video/adf_format.h>

#include "sw_sync.h"
#include "sync.h"

#include "adf.h"
#include "adf_fops.h"
#include "adf_sysfs.h"

#define CREATE_TRACE_POINTS
#include "adf_trace.h"

#define ADF_SHORT_FENCE_TIMEOUT (1 * MSEC_PER_SEC)
#define ADF_LONG_FENCE_TIMEOUT (10 * MSEC_PER_SEC)

static void adf_fence_wait(struct adf_device *dev, struct sync_fence *fence)
{
	/* sync_fence_wait() dumps debug information on timeout.  Experience
	   has shown that if the pipeline gets stuck, a short timeout followed
	   by a longer one provides useful information for debugging. */
	int err = sync_fence_wait(fence, ADF_SHORT_FENCE_TIMEOUT);
	if (err >= 0)
		return;

	if (err == -ETIME)
		err = sync_fence_wait(fence, ADF_LONG_FENCE_TIMEOUT);

	if (err < 0)
		dev_warn(&dev->base.dev, "error waiting on fence: %d\n", err);
}

void adf_buffer_cleanup(struct adf_buffer *buf)
{
	size_t i;
	for (i = 0; i < ARRAY_SIZE(buf->dma_bufs); i++)
		if (buf->dma_bufs[i])
			dma_buf_put(buf->dma_bufs[i]);

	if (buf->acquire_fence)
		sync_fence_put(buf->acquire_fence);
}

void adf_buffer_mapping_cleanup(struct adf_buffer_mapping *mapping,
		struct adf_buffer *buf)
{
	/* calling adf_buffer_mapping_cleanup() is safe even if mapping is
	   uninitialized or partially-initialized, as long as it was
	   zeroed on allocation */
	size_t i;
	for (i = 0; i < ARRAY_SIZE(mapping->sg_tables); i++) {
		if (mapping->sg_tables[i])
			dma_buf_unmap_attachment(mapping->attachments[i],
					mapping->sg_tables[i], DMA_TO_DEVICE);
		if (mapping->attachments[i])
			dma_buf_detach(buf->dma_bufs[i],
					mapping->attachments[i]);
	}
}

void adf_post_cleanup(struct adf_device *dev, struct adf_pending_post *post)
{
	size_t i;

	if (post->state)
		dev->ops->state_free(dev, post->state);

	for (i = 0; i < post->config.n_bufs; i++) {
		adf_buffer_mapping_cleanup(&post->config.mappings[i],
				&post->config.bufs[i]);
		adf_buffer_cleanup(&post->config.bufs[i]);
	}

	kfree(post->config.custom_data);
	kfree(post->config.mappings);
	kfree(post->config.bufs);
	kfree(post);
}

static void adf_sw_advance_timeline(struct adf_device *dev)
{
#ifdef CONFIG_SW_SYNC
	sw_sync_timeline_inc(dev->timeline, 1);
#else
	BUG();
#endif
}

static void adf_post_work_func(struct kthread_work *work)
{
	struct adf_device *dev =
			container_of(work, struct adf_device, post_work);
	struct adf_pending_post *post, *next;
	struct list_head saved_list;

	mutex_lock(&dev->post_lock);
	memcpy(&saved_list, &dev->post_list, sizeof(saved_list));
	list_replace_init(&dev->post_list, &saved_list);
	mutex_unlock(&dev->post_lock);

	list_for_each_entry_safe(post, next, &saved_list, head) {
		int i;

		for (i = 0; i < post->config.n_bufs; i++) {
			struct sync_fence *fence =
					post->config.bufs[i].acquire_fence;
			if (fence)
				adf_fence_wait(dev, fence);
		}

		dev->ops->post(dev, &post->config, post->state);

		if (dev->ops->advance_timeline)
			dev->ops->advance_timeline(dev, &post->config,
					post->state);
		else
			adf_sw_advance_timeline(dev);

		list_del(&post->head);
		if (dev->onscreen)
			adf_post_cleanup(dev, dev->onscreen);
		dev->onscreen = post;
	}
}

void adf_attachment_free(struct adf_attachment_list *attachment)
{
	list_del(&attachment->head);
	kfree(attachment);
}

struct adf_event_refcount *adf_obj_find_event_refcount(struct adf_obj *obj,
		enum adf_event_type type)
{
	struct rb_root *root = &obj->event_refcount;
	struct rb_node **new = &(root->rb_node);
	struct rb_node *parent = NULL;
	struct adf_event_refcount *refcount;

	while (*new) {
		refcount = container_of(*new, struct adf_event_refcount, node);
		parent = *new;

		if (refcount->type > type)
			new = &(*new)->rb_left;
		else if (refcount->type < type)
			new = &(*new)->rb_right;
		else
			return refcount;
	}

	refcount = kzalloc(sizeof(*refcount), GFP_KERNEL);
	if (!refcount)
		return NULL;
	refcount->type = type;

	rb_link_node(&refcount->node, parent, new);
	rb_insert_color(&refcount->node, root);
	return refcount;
}

/**
 * adf_event_get - increase the refcount for an event
 *
 * @obj: the object that produces the event
 * @type: the event type
 *
 * ADF will call the object's set_event() op if needed.  ops are allowed
 * to sleep, so adf_event_get() must NOT be called from an atomic context.
 *
 * Returns 0 if successful, or -%EINVAL if the object does not support the
 * requested event type.
 */
int adf_event_get(struct adf_obj *obj, enum adf_event_type type)
{
	struct adf_event_refcount *refcount;
	int old_refcount;
	int ret;

	ret = adf_obj_check_supports_event(obj, type);
	if (ret < 0)
		return ret;

	mutex_lock(&obj->event_lock);

	refcount = adf_obj_find_event_refcount(obj, type);
	if (!refcount) {
		ret = -ENOMEM;
		goto done;
	}

	old_refcount = refcount->refcount++;

	if (old_refcount == 0) {
		obj->ops->set_event(obj, type, true);
		trace_adf_event_enable(obj, type);
	}

done:
	mutex_unlock(&obj->event_lock);
	return ret;
}
EXPORT_SYMBOL(adf_event_get);

/**
 * adf_event_put - decrease the refcount for an event
 *
 * @obj: the object that produces the event
 * @type: the event type
 *
 * ADF will call the object's set_event() op if needed.  ops are allowed
 * to sleep, so adf_event_put() must NOT be called from an atomic context.
 *
 * Returns 0 if successful, -%EINVAL if the object does not support the
 * requested event type, or -%EALREADY if the refcount is already 0.
 */
int adf_event_put(struct adf_obj *obj, enum adf_event_type type)
{
	struct adf_event_refcount *refcount;
	int old_refcount;
	int ret;

	ret = adf_obj_check_supports_event(obj, type);
	if (ret < 0)
		return ret;


	mutex_lock(&obj->event_lock);

	refcount = adf_obj_find_event_refcount(obj, type);
	if (!refcount) {
		ret = -ENOMEM;
		goto done;
	}

	old_refcount = refcount->refcount--;

	if (WARN_ON(old_refcount == 0)) {
		refcount->refcount++;
		ret = -EALREADY;
	} else if (old_refcount == 1) {
		obj->ops->set_event(obj, type, false);
		trace_adf_event_disable(obj, type);
	}

done:
	mutex_unlock(&obj->event_lock);
	return ret;
}
EXPORT_SYMBOL(adf_event_put);

/**
 * adf_vsync_wait - wait for a vsync event on a display interface
 *
 * @intf: the display interface
 * @timeout: timeout in jiffies (0 = wait indefinitely)
 *
 * adf_vsync_wait() may sleep, so it must NOT be called from an atomic context.
 *
 * This function returns -%ERESTARTSYS if it is interrupted by a signal.
 * If @timeout == 0 then this function returns 0 on vsync. If @timeout > 0 then
 * this function returns the number of remaining jiffies or -%ETIMEDOUT on
 * timeout.
 */
int adf_vsync_wait(struct adf_interface *intf, long timeout)
{
	ktime_t timestamp;
	int ret;
	unsigned long flags;

	read_lock_irqsave(&intf->vsync_lock, flags);
	timestamp = intf->vsync_timestamp;
	read_unlock_irqrestore(&intf->vsync_lock, flags);

	adf_vsync_get(intf);
	if (timeout) {
		ret = wait_event_interruptible_timeout(intf->vsync_wait,
				!ktime_equal(timestamp,
						intf->vsync_timestamp),
				msecs_to_jiffies(timeout));
		if (ret == 0 && ktime_equal(timestamp, intf->vsync_timestamp))
			ret = -ETIMEDOUT;
	} else {
		ret = wait_event_interruptible(intf->vsync_wait,
				!ktime_equal(timestamp,
						intf->vsync_timestamp));
	}
	adf_vsync_put(intf);

	return ret;
}
EXPORT_SYMBOL(adf_vsync_wait);

static void adf_event_queue(struct adf_obj *obj, struct adf_event *event)
{
	struct adf_file *file;
	unsigned long flags;

	trace_adf_event(obj, event->type);

	spin_lock_irqsave(&obj->file_lock, flags);

	list_for_each_entry(file, &obj->file_list, head)
		if (test_bit(event->type, file->event_subscriptions))
			adf_file_queue_event(file, event);

	spin_unlock_irqrestore(&obj->file_lock, flags);
}

/**
 * adf_event_notify - notify userspace of a driver-private event
 *
 * @obj: the ADF object that produced the event
 * @event: the event
 *
 * adf_event_notify() may be called safely from an atomic context.  It will
 * copy @event if needed, so @event may point to a variable on the stack.
 *
 * Drivers must NOT call adf_event_notify() for vsync and hotplug events.
 * ADF provides adf_vsync_notify() and
 * adf_hotplug_notify_{connected,disconnected}() for these events.
 */
int adf_event_notify(struct adf_obj *obj, struct adf_event *event)
{
	if (WARN_ON(event->type == ADF_EVENT_VSYNC ||
			event->type == ADF_EVENT_HOTPLUG))
		return -EINVAL;

	adf_event_queue(obj, event);
	return 0;
}
EXPORT_SYMBOL(adf_event_notify);

/**
 * adf_vsync_notify - notify ADF of a display interface's vsync event
 *
 * @intf: the display interface
 * @timestamp: the time the vsync occurred
 *
 * adf_vsync_notify() may be called safely from an atomic context.
 */
void adf_vsync_notify(struct adf_interface *intf, ktime_t timestamp)
{
	unsigned long flags;
	struct adf_vsync_event event;

	write_lock_irqsave(&intf->vsync_lock, flags);
	intf->vsync_timestamp = timestamp;
	write_unlock_irqrestore(&intf->vsync_lock, flags);

	wake_up_interruptible_all(&intf->vsync_wait);

	event.base.type = ADF_EVENT_VSYNC;
	event.base.length = sizeof(event);
	event.timestamp = ktime_to_ns(timestamp);
	adf_event_queue(&intf->base, &event.base);
}
EXPORT_SYMBOL(adf_vsync_notify);

void adf_hotplug_notify(struct adf_interface *intf, bool connected,
		struct drm_mode_modeinfo *modelist, size_t n_modes)
{
	unsigned long flags;
	struct adf_hotplug_event event;
	struct drm_mode_modeinfo *old_modelist;

	write_lock_irqsave(&intf->hotplug_modelist_lock, flags);
	old_modelist = intf->modelist;
	intf->hotplug_detect = connected;
	intf->modelist = modelist;
	intf->n_modes = n_modes;
	write_unlock_irqrestore(&intf->hotplug_modelist_lock, flags);

	kfree(old_modelist);

	event.base.length = sizeof(event);
	event.base.type = ADF_EVENT_HOTPLUG;
	event.connected = connected;
	adf_event_queue(&intf->base, &event.base);
}

/**
 * adf_hotplug_notify_connected - notify ADF of a display interface being
 * connected to a display
 *
 * @intf: the display interface
 * @modelist: hardware modes supported by display
 * @n_modes: length of modelist
 *
 * @modelist is copied as needed, so it may point to a variable on the stack.
 *
 * adf_hotplug_notify_connected() may NOT be called safely from an atomic
 * context.
 *
 * Returns 0 on success or error code (<0) on error.
 */
int adf_hotplug_notify_connected(struct adf_interface *intf,
		struct drm_mode_modeinfo *modelist, size_t n_modes)
{
	struct drm_mode_modeinfo *modelist_copy;

	if (n_modes > ADF_MAX_MODES)
		return -ENOMEM;

	modelist_copy = kzalloc(sizeof(modelist_copy[0]) * n_modes,
			GFP_KERNEL);
	if (!modelist_copy)
		return -ENOMEM;
	memcpy(modelist_copy, modelist, sizeof(modelist_copy[0]) * n_modes);

	adf_hotplug_notify(intf, true, modelist_copy, n_modes);
	return 0;
}
EXPORT_SYMBOL(adf_hotplug_notify_connected);

/**
 * adf_hotplug_notify_disconnected - notify ADF of a display interface being
 * disconnected from a display
 *
 * @intf: the display interface
 *
 * adf_hotplug_notify_disconnected() may be called safely from an atomic
 * context.
 */
void adf_hotplug_notify_disconnected(struct adf_interface *intf)
{
	adf_hotplug_notify(intf, false, NULL, 0);
}
EXPORT_SYMBOL(adf_hotplug_notify_disconnected);

static int adf_obj_init(struct adf_obj *obj, enum adf_obj_type type,
		struct idr *idr, struct adf_device *parent,
		const struct adf_obj_ops *ops, const char *fmt, va_list args)
{
	if (ops && ops->supports_event && !ops->set_event) {
		pr_err("%s: %s implements supports_event but not set_event\n",
				__func__, adf_obj_type_str(type));
		return -EINVAL;
	}

	if (idr) {
		int ret = idr_alloc(idr, obj, 0, 0, GFP_KERNEL);
		if (ret < 0) {
			pr_err("%s: allocating object id failed: %d\n",
					__func__, ret);
			return ret;
		}
		obj->id = ret;
	} else {
		obj->id = -1;
	}

	vscnprintf(obj->name, sizeof(obj->name), fmt, args);

	obj->type = type;
	obj->ops = ops;
	obj->parent = parent;
	mutex_init(&obj->event_lock);
	obj->event_refcount = RB_ROOT;
	spin_lock_init(&obj->file_lock);
	INIT_LIST_HEAD(&obj->file_list);
	return 0;
}

static void adf_obj_destroy(struct adf_obj *obj, struct idr *idr)
{
	struct rb_node *node = rb_first(&obj->event_refcount);

	while (node) {
		struct adf_event_refcount *refcount =
				container_of(node, struct adf_event_refcount,
						node);
		kfree(refcount);
		node = rb_first(&obj->event_refcount);
	}

	mutex_destroy(&obj->event_lock);
	if (idr)
		idr_remove(idr, obj->id);
}

/**
 * adf_device_init - initialize ADF-internal data for a display device
 * and create sysfs entries
 *
 * @dev: the display device
 * @parent: the device's parent device
 * @ops: the device's associated ops
 * @fmt: formatting string for the display device's name
 *
 * @fmt specifies the device's sysfs filename and the name returned to
 * userspace through the %ADF_GET_DEVICE_DATA ioctl.
 *
 * Returns 0 on success or error code (<0) on failure.
 */
int adf_device_init(struct adf_device *dev, struct device *parent,
		const struct adf_device_ops *ops, const char *fmt, ...)
{
	int ret;
	va_list args;

	if (!ops->validate || !ops->post) {
		pr_err("%s: device must implement validate and post\n",
				__func__);
		return -EINVAL;
	}

	if (!ops->complete_fence && !ops->advance_timeline) {
		if (!IS_ENABLED(CONFIG_SW_SYNC)) {
			pr_err("%s: device requires sw_sync but it is not enabled in the kernel\n",
					__func__);
			return -EINVAL;
		}
	} else if (!(ops->complete_fence && ops->advance_timeline)) {
		pr_err("%s: device must implement both complete_fence and advance_timeline, or implement neither\n",
				__func__);
		return -EINVAL;
	}

	memset(dev, 0, sizeof(*dev));

	va_start(args, fmt);
	ret = adf_obj_init(&dev->base, ADF_OBJ_DEVICE, NULL, dev, &ops->base,
			fmt, args);
	va_end(args);
	if (ret < 0)
		return ret;

	dev->dev = parent;
	dev->ops = ops;
	idr_init(&dev->overlay_engines);
	idr_init(&dev->interfaces);
	mutex_init(&dev->client_lock);
	INIT_LIST_HEAD(&dev->post_list);
	mutex_init(&dev->post_lock);
	init_kthread_worker(&dev->post_worker);
	INIT_LIST_HEAD(&dev->attached);
	INIT_LIST_HEAD(&dev->attach_allowed);

	dev->post_thread = kthread_run(kthread_worker_fn,
			&dev->post_worker, dev->base.name);
	if (IS_ERR(dev->post_thread)) {
		ret = PTR_ERR(dev->post_thread);
		dev->post_thread = NULL;

		pr_err("%s: failed to run config posting thread: %d\n",
				__func__, ret);
		goto err;
	}
	init_kthread_work(&dev->post_work, adf_post_work_func);

	ret = adf_device_sysfs_init(dev);
	if (ret < 0)
		goto err;

	return 0;

err:
	adf_device_destroy(dev);
	return ret;
}
EXPORT_SYMBOL(adf_device_init);

/**
 * adf_device_destroy - clean up ADF-internal data for a display device
 *
 * @dev: the display device
 */
void adf_device_destroy(struct adf_device *dev)
{
	struct adf_attachment_list *entry, *next;

	idr_destroy(&dev->interfaces);
	idr_destroy(&dev->overlay_engines);

	if (dev->post_thread) {
		flush_kthread_worker(&dev->post_worker);
		kthread_stop(dev->post_thread);
	}

	if (dev->onscreen)
		adf_post_cleanup(dev, dev->onscreen);
	adf_device_sysfs_destroy(dev);
	list_for_each_entry_safe(entry, next, &dev->attach_allowed, head) {
		adf_attachment_free(entry);
	}
	list_for_each_entry_safe(entry, next, &dev->attached, head) {
		adf_attachment_free(entry);
	}
	mutex_destroy(&dev->post_lock);
	mutex_destroy(&dev->client_lock);
	adf_obj_destroy(&dev->base, NULL);
}
EXPORT_SYMBOL(adf_device_destroy);

/**
 * adf_interface_init - initialize ADF-internal data for a display interface
 * and create sysfs entries
 *
 * @intf: the display interface
 * @dev: the interface's "parent" display device
 * @type: interface type (see enum @adf_interface_type)
 * @idx: which interface of type @type;
 *	e.g. interface DSI.1 -> @type=%ADF_INTF_TYPE_DSI, @idx=1
 * @flags: informational flags (bitmask of %ADF_INTF_FLAG_* values)
 * @ops: the interface's associated ops
 * @fmt: formatting string for the display interface's name
 *
 * @dev must have previously been initialized with adf_device_init().
 *
 * @fmt affects the name returned to userspace through the
 * %ADF_GET_INTERFACE_DATA ioctl.  It does not affect the sysfs filename,
 * which is derived from @dev's name.
 *
 * Returns 0 on success or error code (<0) on failure.
 */
int adf_interface_init(struct adf_interface *intf, struct adf_device *dev,
		enum adf_interface_type type, u32 idx, u32 flags,
		const struct adf_interface_ops *ops, const char *fmt, ...)
{
	int ret;
	va_list args;
	const u32 allowed_flags = ADF_INTF_FLAG_PRIMARY |
			ADF_INTF_FLAG_EXTERNAL;

	if (dev->n_interfaces == ADF_MAX_INTERFACES) {
		pr_err("%s: parent device %s has too many interfaces\n",
				__func__, dev->base.name);
		return -ENOMEM;
	}

	if (type >= ADF_INTF_MEMORY && type <= ADF_INTF_TYPE_DEVICE_CUSTOM) {
		pr_err("%s: invalid interface type %u\n", __func__, type);
		return -EINVAL;
	}

	if (flags & ~allowed_flags) {
		pr_err("%s: invalid interface flags 0x%X\n", __func__,
				flags & ~allowed_flags);
		return -EINVAL;
	}

	memset(intf, 0, sizeof(*intf));

	va_start(args, fmt);
	ret = adf_obj_init(&intf->base, ADF_OBJ_INTERFACE, &dev->interfaces,
			dev, ops ? &ops->base : NULL, fmt, args);
	va_end(args);
	if (ret < 0)
		return ret;

	intf->type = type;
	intf->idx = idx;
	intf->flags = flags;
	intf->ops = ops;
	init_waitqueue_head(&intf->vsync_wait);
	rwlock_init(&intf->vsync_lock);
	rwlock_init(&intf->hotplug_modelist_lock);

	ret = adf_interface_sysfs_init(intf);
	if (ret < 0)
		goto err;
	dev->n_interfaces++;

	return 0;

err:
	adf_obj_destroy(&intf->base, &dev->interfaces);
	return ret;
}
EXPORT_SYMBOL(adf_interface_init);

/**
 * adf_interface_destroy - clean up ADF-internal data for a display interface
 *
 * @intf: the display interface
 */
void adf_interface_destroy(struct adf_interface *intf)
{
	struct adf_device *dev = adf_interface_parent(intf);
	struct adf_attachment_list *entry, *next;

	mutex_lock(&dev->client_lock);
	list_for_each_entry_safe(entry, next, &dev->attach_allowed, head) {
		if (entry->attachment.interface == intf) {
			adf_attachment_free(entry);
			dev->n_attach_allowed--;
		}
	}
	list_for_each_entry_safe(entry, next, &dev->attached, head) {
		if (entry->attachment.interface == intf) {
			adf_device_detach_op(dev,
					entry->attachment.overlay_engine, intf);
			adf_attachment_free(entry);
			dev->n_attached--;
		}
	}
	kfree(intf->modelist);
	adf_interface_sysfs_destroy(intf);
	adf_obj_destroy(&intf->base, &dev->interfaces);
	dev->n_interfaces--;
	mutex_unlock(&dev->client_lock);
}
EXPORT_SYMBOL(adf_interface_destroy);

static bool adf_overlay_engine_has_custom_formats(
		const struct adf_overlay_engine_ops *ops)
{
	size_t i;
	for (i = 0; i < ops->n_supported_formats; i++)
		if (!adf_format_is_standard(ops->supported_formats[i]))
			return true;
	return false;
}

/**
 * adf_overlay_engine_init - initialize ADF-internal data for an
 * overlay engine and create sysfs entries
 *
 * @eng: the overlay engine
 * @dev: the overlay engine's "parent" display device
 * @ops: the overlay engine's associated ops
 * @fmt: formatting string for the overlay engine's name
 *
 * @dev must have previously been initialized with adf_device_init().
 *
 * @fmt affects the name returned to userspace through the
 * %ADF_GET_OVERLAY_ENGINE_DATA ioctl.  It does not affect the sysfs filename,
 * which is derived from @dev's name.
 *
 * Returns 0 on success or error code (<0) on failure.
 */
int adf_overlay_engine_init(struct adf_overlay_engine *eng,
		struct adf_device *dev,
		const struct adf_overlay_engine_ops *ops, const char *fmt, ...)
{
	int ret;
	va_list args;

	if (!ops->supported_formats) {
		pr_err("%s: overlay engine must support at least one format\n",
				__func__);
		return -EINVAL;
	}

	if (ops->n_supported_formats > ADF_MAX_SUPPORTED_FORMATS) {
		pr_err("%s: overlay engine supports too many formats\n",
				__func__);
		return -EINVAL;
	}

	if (adf_overlay_engine_has_custom_formats(ops) &&
			!dev->ops->validate_custom_format) {
		pr_err("%s: overlay engine has custom formats but parent device %s does not implement validate_custom_format\n",
				__func__, dev->base.name);
		return -EINVAL;
	}

	memset(eng, 0, sizeof(*eng));

	va_start(args, fmt);
	ret = adf_obj_init(&eng->base, ADF_OBJ_OVERLAY_ENGINE,
			&dev->overlay_engines, dev, &ops->base, fmt, args);
	va_end(args);
	if (ret < 0)
		return ret;

	eng->ops = ops;

	ret = adf_overlay_engine_sysfs_init(eng);
	if (ret < 0)
		goto err;

	return 0;

err:
	adf_obj_destroy(&eng->base, &dev->overlay_engines);
	return ret;
}
EXPORT_SYMBOL(adf_overlay_engine_init);

/**
 * adf_interface_destroy - clean up ADF-internal data for an overlay engine
 *
 * @eng: the overlay engine
 */
void adf_overlay_engine_destroy(struct adf_overlay_engine *eng)
{
	struct adf_device *dev = adf_overlay_engine_parent(eng);
	struct adf_attachment_list *entry, *next;

	mutex_lock(&dev->client_lock);
	list_for_each_entry_safe(entry, next, &dev->attach_allowed, head) {
		if (entry->attachment.overlay_engine == eng) {
			adf_attachment_free(entry);
			dev->n_attach_allowed--;
		}
	}
	list_for_each_entry_safe(entry, next, &dev->attached, head) {
		if (entry->attachment.overlay_engine == eng) {
			adf_device_detach_op(dev, eng,
					entry->attachment.interface);
			adf_attachment_free(entry);
			dev->n_attached--;
		}
	}
	adf_overlay_engine_sysfs_destroy(eng);
	adf_obj_destroy(&eng->base, &dev->overlay_engines);
	mutex_unlock(&dev->client_lock);
}
EXPORT_SYMBOL(adf_overlay_engine_destroy);

struct adf_attachment_list *adf_attachment_find(struct list_head *list,
		struct adf_overlay_engine *eng, struct adf_interface *intf)
{
	struct adf_attachment_list *entry;
	list_for_each_entry(entry, list, head) {
		if (entry->attachment.interface == intf &&
				entry->attachment.overlay_engine == eng)
			return entry;
	}
	return NULL;
}

int adf_attachment_validate(struct adf_device *dev,
		struct adf_overlay_engine *eng, struct adf_interface *intf)
{
	struct adf_device *intf_dev = adf_interface_parent(intf);
	struct adf_device *eng_dev = adf_overlay_engine_parent(eng);

	if (intf_dev != dev) {
		dev_err(&dev->base.dev, "can't attach interface %s belonging to device %s\n",
				intf->base.name, intf_dev->base.name);
		return -EINVAL;
	}

	if (eng_dev != dev) {
		dev_err(&dev->base.dev, "can't attach overlay engine %s belonging to device %s\n",
				eng->base.name, eng_dev->base.name);
		return -EINVAL;
	}

	return 0;
}

/**
 * adf_attachment_allow - add a new entry to the list of allowed
 * attachments
 *
 * @dev: the parent device
 * @eng: the overlay engine
 * @intf: the interface
 *
 * adf_attachment_allow() indicates that the underlying display hardware allows
 * @intf to scan out @eng's output.  It is intended to be called at
 * driver initialization for each supported overlay engine + interface pair.
 *
 * Returns 0 on success, -%EALREADY if the entry already exists, or -errno on
 * any other failure.
 */
int adf_attachment_allow(struct adf_device *dev,
		struct adf_overlay_engine *eng, struct adf_interface *intf)
{
	int ret;
	struct adf_attachment_list *entry = NULL;

	ret = adf_attachment_validate(dev, eng, intf);
	if (ret < 0)
		return ret;

	mutex_lock(&dev->client_lock);

	if (dev->n_attach_allowed == ADF_MAX_ATTACHMENTS) {
		ret = -ENOMEM;
		goto done;
	}

	if (adf_attachment_find(&dev->attach_allowed, eng, intf)) {
		ret = -EALREADY;
		goto done;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		ret = -ENOMEM;
		goto done;
	}

	entry->attachment.interface = intf;
	entry->attachment.overlay_engine = eng;
	list_add_tail(&entry->head, &dev->attach_allowed);
	dev->n_attach_allowed++;

done:
	mutex_unlock(&dev->client_lock);
	if (ret < 0)
		kfree(entry);

	return ret;
}

/**
 * adf_obj_type_str - string representation of an adf_obj_type
 *
 * @type: the object type
 */
const char *adf_obj_type_str(enum adf_obj_type type)
{
	switch (type) {
	case ADF_OBJ_OVERLAY_ENGINE:
		return "overlay engine";

	case ADF_OBJ_INTERFACE:
		return "interface";

	case ADF_OBJ_DEVICE:
		return "device";

	default:
		return "unknown";
	}
}
EXPORT_SYMBOL(adf_obj_type_str);

/**
 * adf_interface_type_str - string representation of an adf_interface's type
 *
 * @intf: the interface
 */
const char *adf_interface_type_str(struct adf_interface *intf)
{
	switch (intf->type) {
	case ADF_INTF_DSI:
		return "DSI";

	case ADF_INTF_eDP:
		return "eDP";

	case ADF_INTF_DPI:
		return "DPI";

	case ADF_INTF_VGA:
		return "VGA";

	case ADF_INTF_DVI:
		return "DVI";

	case ADF_INTF_HDMI:
		return "HDMI";

	case ADF_INTF_MEMORY:
		return "memory";

	default:
		if (intf->type >= ADF_INTF_TYPE_DEVICE_CUSTOM) {
			if (intf->ops && intf->ops->type_str)
				return intf->ops->type_str(intf);
			return "custom";
		}
		return "unknown";
	}
}
EXPORT_SYMBOL(adf_interface_type_str);

/**
 * adf_event_type_str - string representation of an adf_event_type
 *
 * @obj: ADF object that produced the event
 * @type: event type
 */
const char *adf_event_type_str(struct adf_obj *obj, enum adf_event_type type)
{
	switch (type) {
	case ADF_EVENT_VSYNC:
		return "vsync";

	case ADF_EVENT_HOTPLUG:
		return "hotplug";

	default:
		if (type >= ADF_EVENT_DEVICE_CUSTOM) {
			if (obj->ops && obj->ops->event_type_str)
				return obj->ops->event_type_str(obj, type);
			return "custom";
		}
		return "unknown";
	}
}
EXPORT_SYMBOL(adf_event_type_str);

/**
 * adf_format_str - string representation of an ADF/DRM fourcc format
 *
 * @format: format fourcc
 * @buf: target buffer for the format's string representation
 */
void adf_format_str(u32 format, char buf[ADF_FORMAT_STR_SIZE])
{
	buf[0] = format & 0xFF;
	buf[1] = (format >> 8) & 0xFF;
	buf[2] = (format >> 16) & 0xFF;
	buf[3] = (format >> 24) & 0xFF;
	buf[4] = '\0';
}
EXPORT_SYMBOL(adf_format_str);

void adf_modeinfo_set_name(struct drm_mode_modeinfo *mode)
{
	bool interlaced = mode->flags & DRM_MODE_FLAG_INTERLACE;

	snprintf(mode->name, DRM_DISPLAY_MODE_LEN, "%dx%d%s",
		 mode->hdisplay, mode->vdisplay,
		 interlaced ? "i" : "");
}

void adf_modeinfo_set_vrefresh(struct drm_mode_modeinfo *mode)
{
	int refresh = 0;
	unsigned int calc_val;

	if (mode->vrefresh > 0)
		return;

	if (mode->htotal <= 0 || mode->vtotal <= 0)
		return;

	/* work out vrefresh the value will be x1000 */
	calc_val = (mode->clock * 1000);
	calc_val /= mode->htotal;
	refresh = (calc_val + mode->vtotal / 2) / mode->vtotal;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		refresh *= 2;
	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		refresh /= 2;
	if (mode->vscan > 1)
		refresh /= mode->vscan;

	mode->vrefresh = refresh;
}

static int __init adf_init(void)
{
	int err;

	err = adf_sysfs_init();
	if (err < 0)
		return err;

	return 0;
}

static void __exit adf_exit(void)
{
	adf_sysfs_destroy();
}

module_init(adf_init);
module_exit(adf_exit);
