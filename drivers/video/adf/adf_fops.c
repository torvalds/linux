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

#include <linux/bitops.h>
#include <linux/circ_buf.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <video/adf_client.h>
#include <video/adf_format.h>

#include "sw_sync.h"
#include "sync.h"

#include "adf.h"
#include "adf_fops.h"
#include "adf_sysfs.h"

#ifdef CONFIG_COMPAT
#include "adf_fops32.h"
#endif

static int adf_obj_set_event(struct adf_obj *obj, struct adf_file *file,
		struct adf_set_event __user *arg)
{
	struct adf_set_event data;
	bool enabled;
	unsigned long flags;
	int err;

	if (copy_from_user(&data, arg, sizeof(data)))
		return -EFAULT;

	err = adf_obj_check_supports_event(obj, data.type);
	if (err < 0)
		return err;

	spin_lock_irqsave(&obj->file_lock, flags);
	if (data.enabled)
		enabled = test_and_set_bit(data.type,
				file->event_subscriptions);
	else
		enabled = test_and_clear_bit(data.type,
				file->event_subscriptions);
	spin_unlock_irqrestore(&obj->file_lock, flags);

	if (data.enabled == enabled)
		return -EALREADY;

	if (data.enabled)
		adf_event_get(obj, data.type);
	else
		adf_event_put(obj, data.type);

	return 0;
}

static int adf_obj_copy_custom_data_to_user(struct adf_obj *obj,
		void __user *dst, size_t *dst_size)
{
	void *custom_data;
	size_t custom_data_size;
	int ret;

	if (!obj->ops || !obj->ops->custom_data) {
		dev_dbg(&obj->dev, "%s: no custom_data op\n", __func__);
		return 0;
	}

	custom_data = kzalloc(ADF_MAX_CUSTOM_DATA_SIZE, GFP_KERNEL);
	if (!custom_data)
		return -ENOMEM;

	ret = obj->ops->custom_data(obj, custom_data, &custom_data_size);
	if (ret < 0)
		goto done;

	if (copy_to_user(dst, custom_data, min(*dst_size, custom_data_size))) {
		ret = -EFAULT;
		goto done;
	}
	*dst_size = custom_data_size;

done:
	kfree(custom_data);
	return ret;
}

static int adf_eng_get_data(struct adf_overlay_engine *eng,
		struct adf_overlay_engine_data __user *arg)
{
	struct adf_device *dev = adf_overlay_engine_parent(eng);
	struct adf_overlay_engine_data data;
	int ret = 0;

	if (copy_from_user(&data, arg, sizeof(data)))
		return -EFAULT;

	strlcpy(data.name, eng->base.name, sizeof(data.name));

	mutex_lock(&dev->client_lock);
	ret = adf_obj_copy_custom_data_to_user(&eng->base, arg->custom_data,
			&data.custom_data_size);
	mutex_unlock(&dev->client_lock);

	if (ret < 0)
		return ret;

	if (copy_to_user(arg, &data, sizeof(data)))
		return -EFAULT;

	return 0;
}

static int adf_buffer_import(struct adf_device *dev,
		struct adf_buffer_config __user *cfg, struct adf_buffer *buf)
{
	struct adf_buffer_config user_buf;
	size_t i;
	int ret = 0;

	if (copy_from_user(&user_buf, cfg, sizeof(user_buf)))
		return -EFAULT;

	memset(buf, 0, sizeof(*buf));

	if (user_buf.n_planes > ADF_MAX_PLANES) {
		dev_err(&dev->base.dev, "invalid plane count %u\n",
				user_buf.n_planes);
		return -EINVAL;
	}

	buf->overlay_engine = idr_find(&dev->overlay_engines,
			user_buf.overlay_engine);
	if (!buf->overlay_engine) {
		dev_err(&dev->base.dev, "invalid overlay engine id %u\n",
				user_buf.overlay_engine);
		return -ENOENT;
	}

	buf->w = user_buf.w;
	buf->h = user_buf.h;
	buf->format = user_buf.format;
	for (i = 0; i < user_buf.n_planes; i++) {
		buf->dma_bufs[i] = dma_buf_get(user_buf.fd[i]);
		if (IS_ERR(buf->dma_bufs[i])) {
			ret = PTR_ERR(buf->dma_bufs[i]);
			dev_err(&dev->base.dev, "importing dma_buf fd %llu failed: %d\n",
					user_buf.fd[i], ret);
			buf->dma_bufs[i] = NULL;
			goto done;
		}
		buf->offset[i] = user_buf.offset[i];
		buf->pitch[i] = user_buf.pitch[i];
	}
	buf->n_planes = user_buf.n_planes;

	if (user_buf.acquire_fence >= 0) {
		buf->acquire_fence = sync_fence_fdget(user_buf.acquire_fence);
		if (!buf->acquire_fence) {
			dev_err(&dev->base.dev, "getting fence fd %lld failed\n",
					user_buf.acquire_fence);
			ret = -EINVAL;
			goto done;
		}
	}

done:
	if (ret < 0)
		adf_buffer_cleanup(buf);
	return ret;
}

static int adf_device_post_config(struct adf_device *dev,
		struct adf_post_config __user *arg)
{
	struct sync_fence *complete_fence;
	int complete_fence_fd;
	struct adf_buffer *bufs = NULL;
	struct adf_interface **intfs = NULL;
	size_t n_intfs, n_bufs, i;
	void *custom_data = NULL;
	size_t custom_data_size;
	int ret = 0;

	complete_fence_fd = get_unused_fd();
	if (complete_fence_fd < 0)
		return complete_fence_fd;

	if (get_user(n_intfs, &arg->n_interfaces)) {
		ret = -EFAULT;
		goto err_get_user;
	}

	if (n_intfs > ADF_MAX_INTERFACES) {
		ret = -EINVAL;
		goto err_get_user;
	}

	if (get_user(n_bufs, &arg->n_bufs)) {
		ret = -EFAULT;
		goto err_get_user;
	}

	if (n_bufs > ADF_MAX_BUFFERS) {
		ret = -EINVAL;
		goto err_get_user;
	}

	if (get_user(custom_data_size, &arg->custom_data_size)) {
		ret = -EFAULT;
		goto err_get_user;
	}

	if (custom_data_size > ADF_MAX_CUSTOM_DATA_SIZE) {
		ret = -EINVAL;
		goto err_get_user;
	}

	if (n_intfs) {
		intfs = kmalloc(sizeof(intfs[0]) * n_intfs, GFP_KERNEL);
		if (!intfs) {
			ret = -ENOMEM;
			goto err_get_user;
		}
	}

	for (i = 0; i < n_intfs; i++) {
		u32 intf_id;
		if (get_user(intf_id, &arg->interfaces[i])) {
			ret = -EFAULT;
			goto err_get_user;
		}

		intfs[i] = idr_find(&dev->interfaces, intf_id);
		if (!intfs[i]) {
			ret = -EINVAL;
			goto err_get_user;
		}
	}

	if (n_bufs) {
		bufs = kzalloc(sizeof(bufs[0]) * n_bufs, GFP_KERNEL);
		if (!bufs) {
			ret = -ENOMEM;
			goto err_get_user;
		}
	}

	for (i = 0; i < n_bufs; i++) {
		ret = adf_buffer_import(dev, &arg->bufs[i], &bufs[i]);
		if (ret < 0) {
			memset(&bufs[i], 0, sizeof(bufs[i]));
			goto err_import;
		}
	}

	if (custom_data_size) {
		custom_data = kzalloc(custom_data_size, GFP_KERNEL);
		if (!custom_data) {
			ret = -ENOMEM;
			goto err_import;
		}

		if (copy_from_user(custom_data, arg->custom_data,
				custom_data_size)) {
			ret = -EFAULT;
			goto err_import;
		}
	}

	if (put_user(complete_fence_fd, &arg->complete_fence)) {
		ret = -EFAULT;
		goto err_import;
	}

	complete_fence = adf_device_post_nocopy(dev, intfs, n_intfs, bufs,
			n_bufs, custom_data, custom_data_size);
	if (IS_ERR(complete_fence)) {
		ret = PTR_ERR(complete_fence);
		goto err_import;
	}

	sync_fence_install(complete_fence, complete_fence_fd);
	return 0;

err_import:
	for (i = 0; i < n_bufs; i++)
		adf_buffer_cleanup(&bufs[i]);

err_get_user:
	kfree(custom_data);
	kfree(bufs);
	kfree(intfs);
	put_unused_fd(complete_fence_fd);
	return ret;
}

static int adf_copy_attachment_list_to_user(
		struct adf_attachment_config __user *to, size_t n_to,
		struct adf_attachment *from, size_t n_from)
{
	struct adf_attachment_config *temp;
	size_t n = min(n_to, n_from);
	size_t i;
	int ret = 0;

	if (!n)
		return 0;

	temp = kzalloc(n * sizeof(temp[0]), GFP_KERNEL);
	if (!temp)
		return -ENOMEM;

	for (i = 0; i < n; i++) {
		temp[i].interface = from[i].interface->base.id;
		temp[i].overlay_engine = from[i].overlay_engine->base.id;
	}

	if (copy_to_user(to, temp, n * sizeof(to[0]))) {
		ret = -EFAULT;
		goto done;
	}

done:
	kfree(temp);
	return ret;
}

static int adf_device_get_data(struct adf_device *dev,
		struct adf_device_data __user *arg)
{
	struct adf_device_data data;
	size_t n_attach;
	struct adf_attachment *attach = NULL;
	size_t n_allowed_attach;
	struct adf_attachment *allowed_attach = NULL;
	int ret = 0;

	if (copy_from_user(&data, arg, sizeof(data)))
		return -EFAULT;

	if (data.n_attachments > ADF_MAX_ATTACHMENTS ||
			data.n_allowed_attachments > ADF_MAX_ATTACHMENTS)
		return -EINVAL;

	strlcpy(data.name, dev->base.name, sizeof(data.name));

	if (data.n_attachments) {
		attach = kzalloc(data.n_attachments * sizeof(attach[0]),
				GFP_KERNEL);
		if (!attach)
			return -ENOMEM;
	}
	n_attach = adf_device_attachments(dev, attach, data.n_attachments);

	if (data.n_allowed_attachments) {
		allowed_attach = kzalloc(data.n_allowed_attachments *
				sizeof(allowed_attach[0]), GFP_KERNEL);
		if (!allowed_attach) {
			ret = -ENOMEM;
			goto done;
		}
	}
	n_allowed_attach = adf_device_attachments_allowed(dev, allowed_attach,
			data.n_allowed_attachments);

	mutex_lock(&dev->client_lock);
	ret = adf_obj_copy_custom_data_to_user(&dev->base, arg->custom_data,
			&data.custom_data_size);
	mutex_unlock(&dev->client_lock);

	if (ret < 0)
		goto done;

	ret = adf_copy_attachment_list_to_user(arg->attachments,
			data.n_attachments, attach, n_attach);
	if (ret < 0)
		goto done;

	ret = adf_copy_attachment_list_to_user(arg->allowed_attachments,
			data.n_allowed_attachments, allowed_attach,
			n_allowed_attach);
	if (ret < 0)
		goto done;

	data.n_attachments = n_attach;
	data.n_allowed_attachments = n_allowed_attach;

	if (copy_to_user(arg, &data, sizeof(data)))
		ret = -EFAULT;

done:
	kfree(allowed_attach);
	kfree(attach);
	return ret;
}

static int adf_device_handle_attachment(struct adf_device *dev,
		struct adf_attachment_config __user *arg, bool attach)
{
	struct adf_attachment_config data;
	struct adf_overlay_engine *eng;
	struct adf_interface *intf;

	if (copy_from_user(&data, arg, sizeof(data)))
		return -EFAULT;

	eng = idr_find(&dev->overlay_engines, data.overlay_engine);
	if (!eng) {
		dev_err(&dev->base.dev, "invalid overlay engine id %u\n",
				data.overlay_engine);
		return -EINVAL;
	}

	intf = idr_find(&dev->interfaces, data.interface);
	if (!intf) {
		dev_err(&dev->base.dev, "invalid interface id %u\n",
				data.interface);
		return -EINVAL;
	}

	if (attach)
		return adf_device_attach(dev, eng, intf);
	else
		return adf_device_detach(dev, eng, intf);
}

static int adf_intf_set_mode(struct adf_interface *intf,
		struct drm_mode_modeinfo __user *arg)
{
	struct drm_mode_modeinfo mode;

	if (copy_from_user(&mode, arg, sizeof(mode)))
		return -EFAULT;

	return adf_interface_set_mode(intf, &mode);
}

static int adf_intf_get_data(struct adf_interface *intf,
		struct adf_interface_data __user *arg)
{
	struct adf_device *dev = adf_interface_parent(intf);
	struct adf_interface_data data;
	struct drm_mode_modeinfo *modelist;
	size_t modelist_size;
	int err;
	int ret = 0;
	unsigned long flags;

	if (copy_from_user(&data, arg, sizeof(data)))
		return -EFAULT;

	strlcpy(data.name, intf->base.name, sizeof(data.name));

	data.type = intf->type;
	data.id = intf->idx;

	err = adf_interface_get_screen_size(intf, &data.width_mm,
			&data.height_mm);
	if (err < 0) {
		data.width_mm = 0;
		data.height_mm = 0;
	}

	modelist = kmalloc(sizeof(modelist[0]) * ADF_MAX_MODES, GFP_KERNEL);
	if (!modelist)
		return -ENOMEM;

	mutex_lock(&dev->client_lock);
	read_lock_irqsave(&intf->hotplug_modelist_lock, flags);
	data.hotplug_detect = intf->hotplug_detect;
	modelist_size = min(data.n_available_modes, intf->n_modes) *
			sizeof(intf->modelist[0]);
	memcpy(modelist, intf->modelist, modelist_size);
	data.n_available_modes = intf->n_modes;
	read_unlock_irqrestore(&intf->hotplug_modelist_lock, flags);

	if (copy_to_user(arg->available_modes, modelist, modelist_size)) {
		ret = -EFAULT;
		goto done;
	}

	data.dpms_state = intf->dpms_state;
	memcpy(&data.current_mode, &intf->current_mode,
			sizeof(intf->current_mode));

	ret = adf_obj_copy_custom_data_to_user(&intf->base, arg->custom_data,
			&data.custom_data_size);
done:
	mutex_unlock(&dev->client_lock);
	kfree(modelist);

	if (ret < 0)
		return ret;

	if (copy_to_user(arg, &data, sizeof(data)))
		ret = -EFAULT;

	return ret;
}

static inline long adf_obj_custom_ioctl(struct adf_obj *obj, unsigned int cmd,
		unsigned long arg)
{
	if (obj->ops && obj->ops->ioctl)
		return obj->ops->ioctl(obj, cmd, arg);
	return -ENOTTY;
}

static long adf_overlay_engine_ioctl(struct adf_overlay_engine *eng,
		struct adf_file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case ADF_SET_EVENT:
		return adf_obj_set_event(&eng->base, file,
				(struct adf_set_event __user *)arg);

	case ADF_GET_OVERLAY_ENGINE_DATA:
		return adf_eng_get_data(eng,
			(struct adf_overlay_engine_data __user *)arg);

	case ADF_BLANK:
	case ADF_POST_CONFIG:
	case ADF_SET_MODE:
	case ADF_GET_DEVICE_DATA:
	case ADF_GET_INTERFACE_DATA:
	case ADF_ATTACH:
	case ADF_DETACH:
		return -EINVAL;

	default:
		return adf_obj_custom_ioctl(&eng->base, cmd, arg);
	}
}

static long adf_interface_ioctl(struct adf_interface *intf,
		struct adf_file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case ADF_SET_EVENT:
		return adf_obj_set_event(&intf->base, file,
				(struct adf_set_event __user *)arg);

	case ADF_BLANK:
		return adf_interface_blank(intf, arg);

	case ADF_SET_MODE:
		return adf_intf_set_mode(intf,
				(struct drm_mode_modeinfo __user *)arg);

	case ADF_GET_INTERFACE_DATA:
		return adf_intf_get_data(intf,
				(struct adf_interface_data __user *)arg);

	case ADF_POST_CONFIG:
	case ADF_GET_DEVICE_DATA:
	case ADF_GET_OVERLAY_ENGINE_DATA:
	case ADF_ATTACH:
	case ADF_DETACH:
		return -EINVAL;

	default:
		return adf_obj_custom_ioctl(&intf->base, cmd, arg);
	}
}

static long adf_device_ioctl(struct adf_device *dev, struct adf_file *file,
		unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case ADF_SET_EVENT:
		return adf_obj_set_event(&dev->base, file,
				(struct adf_set_event __user *)arg);

	case ADF_POST_CONFIG:
		return adf_device_post_config(dev,
				(struct adf_post_config __user *)arg);

	case ADF_GET_DEVICE_DATA:
		return adf_device_get_data(dev,
				(struct adf_device_data __user *)arg);

	case ADF_ATTACH:
		return adf_device_handle_attachment(dev,
				(struct adf_attachment_config __user *)arg,
				true);

	case ADF_DETACH:
		return adf_device_handle_attachment(dev,
				(struct adf_attachment_config __user *)arg,
				false);

	case ADF_BLANK:
	case ADF_SET_MODE:
	case ADF_GET_INTERFACE_DATA:
	case ADF_GET_OVERLAY_ENGINE_DATA:
		return -EINVAL;

	default:
		return adf_obj_custom_ioctl(&dev->base, cmd, arg);
	}
}

static int adf_file_open(struct inode *inode, struct file *file)
{
	struct adf_obj *obj;
	struct adf_file *fpriv = NULL;
	unsigned long flags;
	int ret = 0;

	obj = adf_obj_sysfs_find(iminor(inode));
	if (!obj)
		return -ENODEV;

	dev_dbg(&obj->dev, "opening %s\n", dev_name(&obj->dev));

	if (!try_module_get(obj->parent->ops->owner)) {
		dev_err(&obj->dev, "getting owner module failed\n");
		return -ENODEV;
	}

	fpriv = kzalloc(sizeof(*fpriv), GFP_KERNEL);
	if (!fpriv) {
		ret = -ENOMEM;
		goto done;
	}

	INIT_LIST_HEAD(&fpriv->head);
	fpriv->obj = obj;
	init_waitqueue_head(&fpriv->event_wait);

	file->private_data = fpriv;

	if (obj->ops && obj->ops->open) {
		ret = obj->ops->open(obj, inode, file);
		if (ret < 0)
			goto done;
	}

	spin_lock_irqsave(&obj->file_lock, flags);
	list_add_tail(&fpriv->head, &obj->file_list);
	spin_unlock_irqrestore(&obj->file_lock, flags);

done:
	if (ret < 0) {
		kfree(fpriv);
		module_put(obj->parent->ops->owner);
	}
	return ret;
}

static int adf_file_release(struct inode *inode, struct file *file)
{
	struct adf_file *fpriv = file->private_data;
	struct adf_obj *obj = fpriv->obj;
	enum adf_event_type event_type;
	unsigned long flags;

	if (obj->ops && obj->ops->release)
		obj->ops->release(obj, inode, file);

	spin_lock_irqsave(&obj->file_lock, flags);
	list_del(&fpriv->head);
	spin_unlock_irqrestore(&obj->file_lock, flags);

	for_each_set_bit(event_type, fpriv->event_subscriptions,
			ADF_EVENT_TYPE_MAX) {
		adf_event_put(obj, event_type);
	}

	kfree(fpriv);
	module_put(obj->parent->ops->owner);

	dev_dbg(&obj->dev, "released %s\n", dev_name(&obj->dev));
	return 0;
}

long adf_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct adf_file *fpriv = file->private_data;
	struct adf_obj *obj = fpriv->obj;
	long ret = -EINVAL;

	dev_dbg(&obj->dev, "%s ioctl %u\n", dev_name(&obj->dev), _IOC_NR(cmd));

	switch (obj->type) {
	case ADF_OBJ_OVERLAY_ENGINE:
		ret = adf_overlay_engine_ioctl(adf_obj_to_overlay_engine(obj),
				fpriv, cmd, arg);
		break;

	case ADF_OBJ_INTERFACE:
		ret = adf_interface_ioctl(adf_obj_to_interface(obj), fpriv, cmd,
				arg);
		break;

	case ADF_OBJ_DEVICE:
		ret = adf_device_ioctl(adf_obj_to_device(obj), fpriv, cmd, arg);
		break;
	}

	return ret;
}

static inline bool adf_file_event_available(struct adf_file *fpriv)
{
	int head = fpriv->event_head;
	int tail = fpriv->event_tail;
	return CIRC_CNT(head, tail, sizeof(fpriv->event_buf)) != 0;
}

void adf_file_queue_event(struct adf_file *fpriv, struct adf_event *event)
{
	int head = fpriv->event_head;
	int tail = fpriv->event_tail;
	size_t space = CIRC_SPACE(head, tail, sizeof(fpriv->event_buf));
	size_t space_to_end =
			CIRC_SPACE_TO_END(head, tail, sizeof(fpriv->event_buf));

	if (space < event->length) {
		dev_dbg(&fpriv->obj->dev,
				"insufficient buffer space for event %u\n",
				event->type);
		return;
	}

	if (space_to_end >= event->length) {
		memcpy(fpriv->event_buf + head, event, event->length);
	} else {
		memcpy(fpriv->event_buf + head, event, space_to_end);
		memcpy(fpriv->event_buf, (u8 *)event + space_to_end,
				event->length - space_to_end);
	}

	smp_wmb();
	fpriv->event_head = (fpriv->event_head + event->length) &
			(sizeof(fpriv->event_buf) - 1);
	wake_up_interruptible_all(&fpriv->event_wait);
}

static ssize_t adf_file_copy_to_user(struct adf_file *fpriv,
		char __user *buffer, size_t buffer_size)
{
	int head, tail;
	u8 *event_buf;
	size_t cnt, cnt_to_end, copy_size = 0;
	ssize_t ret = 0;
	unsigned long flags;

	event_buf = kmalloc(min(buffer_size, sizeof(fpriv->event_buf)),
			GFP_KERNEL);
	if (!event_buf)
		return -ENOMEM;

	spin_lock_irqsave(&fpriv->obj->file_lock, flags);

	if (!adf_file_event_available(fpriv))
		goto out;

	head = fpriv->event_head;
	tail = fpriv->event_tail;

	cnt = CIRC_CNT(head, tail, sizeof(fpriv->event_buf));
	cnt_to_end = CIRC_CNT_TO_END(head, tail, sizeof(fpriv->event_buf));
	copy_size = min(buffer_size, cnt);

	if (cnt_to_end >= copy_size) {
		memcpy(event_buf, fpriv->event_buf + tail, copy_size);
	} else {
		memcpy(event_buf, fpriv->event_buf + tail, cnt_to_end);
		memcpy(event_buf + cnt_to_end, fpriv->event_buf,
				copy_size - cnt_to_end);
	}

	fpriv->event_tail = (fpriv->event_tail + copy_size) &
			(sizeof(fpriv->event_buf) - 1);

out:
	spin_unlock_irqrestore(&fpriv->obj->file_lock, flags);
	if (copy_size) {
		if (copy_to_user(buffer, event_buf, copy_size))
			ret = -EFAULT;
		else
			ret = copy_size;
	}
	kfree(event_buf);
	return ret;
}

ssize_t adf_file_read(struct file *filp, char __user *buffer,
		 size_t count, loff_t *offset)
{
	struct adf_file *fpriv = filp->private_data;
	int err;

	err = wait_event_interruptible(fpriv->event_wait,
			adf_file_event_available(fpriv));
	if (err < 0)
		return err;

	return adf_file_copy_to_user(fpriv, buffer, count);
}

unsigned int adf_file_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct adf_file *fpriv = filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &fpriv->event_wait, wait);

	if (adf_file_event_available(fpriv))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

const struct file_operations adf_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = adf_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = adf_file_compat_ioctl,
#endif
	.open = adf_file_open,
	.release = adf_file_release,
	.llseek = default_llseek,
	.read = adf_file_read,
	.poll = adf_file_poll,
};
