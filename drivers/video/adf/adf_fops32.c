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

#include <linux/uaccess.h>
#include <video/adf.h>

#include "adf_fops.h"
#include "adf_fops32.h"

long adf_compat_post_config(struct file *file,
		struct adf_post_config32 __user *arg)
{
	struct adf_post_config32 cfg32;
	struct adf_post_config __user *cfg;
	int ret;

	if (copy_from_user(&cfg32, arg, sizeof(cfg32)))
		return -EFAULT;

	cfg = compat_alloc_user_space(sizeof(*cfg));
	if (!access_ok(VERIFY_WRITE, cfg, sizeof(*cfg)))
		return -EFAULT;

	if (put_user(cfg32.n_interfaces, &cfg->n_interfaces) ||
			put_user(compat_ptr(cfg32.interfaces),
					&cfg->interfaces) ||
			put_user(cfg32.n_bufs, &cfg->n_bufs) ||
			put_user(compat_ptr(cfg32.bufs), &cfg->bufs) ||
			put_user(cfg32.custom_data_size,
					&cfg->custom_data_size) ||
			put_user(compat_ptr(cfg32.custom_data),
					&cfg->custom_data))
		return -EFAULT;

	ret = adf_file_ioctl(file, ADF_POST_CONFIG, (unsigned long)cfg);
	if (ret < 0)
		return ret;

	if (copy_in_user(&arg->complete_fence, &cfg->complete_fence,
			sizeof(cfg->complete_fence)))
		return -EFAULT;

	return 0;
}

long adf_compat_get_device_data(struct file *file,
		struct adf_device_data32 __user *arg)
{
	struct adf_device_data32 data32;
	struct adf_device_data __user *data;
	int ret;

	if (copy_from_user(&data32, arg, sizeof(data32)))
		return -EFAULT;

	data = compat_alloc_user_space(sizeof(*data));
	if (!access_ok(VERIFY_WRITE, data, sizeof(*data)))
		return -EFAULT;

	if (put_user(data32.n_attachments, &data->n_attachments) ||
			put_user(compat_ptr(data32.attachments),
					&data->attachments) ||
			put_user(data32.n_allowed_attachments,
					&data->n_allowed_attachments) ||
			put_user(compat_ptr(data32.allowed_attachments),
					&data->allowed_attachments) ||
			put_user(data32.custom_data_size,
					&data->custom_data_size) ||
			put_user(compat_ptr(data32.custom_data),
					&data->custom_data))
		return -EFAULT;

	ret = adf_file_ioctl(file, ADF_GET_DEVICE_DATA32, (unsigned long)data);
	if (ret < 0)
		return ret;

	if (copy_in_user(arg->name, data->name, sizeof(arg->name)) ||
			copy_in_user(&arg->n_attachments, &data->n_attachments,
					sizeof(arg->n_attachments)) ||
			copy_in_user(&arg->n_allowed_attachments,
					&data->n_allowed_attachments,
					sizeof(arg->n_allowed_attachments)) ||
			copy_in_user(&arg->custom_data_size,
					&data->custom_data_size,
					sizeof(arg->custom_data_size)))
		return -EFAULT;

	return 0;
}

long adf_compat_get_interface_data(struct file *file,
		struct adf_interface_data32 __user *arg)
{
	struct adf_interface_data32 data32;
	struct adf_interface_data __user *data;
	int ret;

	if (copy_from_user(&data32, arg, sizeof(data32)))
		return -EFAULT;

	data = compat_alloc_user_space(sizeof(*data));
	if (!access_ok(VERIFY_WRITE, data, sizeof(*data)))
		return -EFAULT;

	if (put_user(data32.n_available_modes, &data->n_available_modes) ||
			put_user(compat_ptr(data32.available_modes),
					&data->available_modes) ||
			put_user(data32.custom_data_size,
					&data->custom_data_size) ||
			put_user(compat_ptr(data32.custom_data),
					&data->custom_data))
		return -EFAULT;

	ret = adf_file_ioctl(file, ADF_GET_DEVICE_DATA32, (unsigned long)data);
	if (ret < 0)
		return ret;

	if (copy_in_user(arg->name, data->name, sizeof(arg->name)) ||
			copy_in_user(&arg->type, &data->type,
					sizeof(arg->type)) ||
			copy_in_user(&arg->id, &data->id, sizeof(arg->id)) ||
			copy_in_user(&arg->dpms_state, &data->dpms_state,
					sizeof(arg->dpms_state)) ||
			copy_in_user(&arg->hotplug_detect,
					&data->hotplug_detect,
					sizeof(arg->hotplug_detect)) ||
			copy_in_user(&arg->width_mm, &data->width_mm,
					sizeof(arg->width_mm)) ||
			copy_in_user(&arg->height_mm, &data->height_mm,
					sizeof(arg->height_mm)) ||
			copy_in_user(&arg->current_mode, &data->current_mode,
					sizeof(arg->current_mode)) ||
			copy_in_user(&arg->n_available_modes,
					&data->n_available_modes,
					sizeof(arg->n_available_modes)) ||
			copy_in_user(&arg->custom_data_size,
					&data->custom_data_size,
					sizeof(arg->custom_data_size)))
		return -EFAULT;

	return 0;
}

long adf_compat_get_overlay_engine_data(struct file *file,
		struct adf_overlay_engine_data32 __user *arg)
{
	struct adf_overlay_engine_data32 data32;
	struct adf_overlay_engine_data __user *data;
	int ret;

	if (copy_from_user(&data32, arg, sizeof(data32)))
		return -EFAULT;

	data = compat_alloc_user_space(sizeof(*data));
	if (!access_ok(VERIFY_WRITE, data, sizeof(*data)))
		return -EFAULT;

	if (put_user(data32.custom_data_size, &data->custom_data_size) ||
			put_user(compat_ptr(data32.custom_data),
					&data->custom_data))
		return -EFAULT;

	ret = adf_file_ioctl(file, ADF_GET_OVERLAY_ENGINE_DATA,
			(unsigned long)data);
	if (ret < 0)
		return ret;

	if (copy_in_user(arg->name, data->name, sizeof(arg->name)) ||
			copy_in_user(&arg->custom_data_size,
					&data->custom_data_size,
					sizeof(arg->custom_data_size)))
		return -EFAULT;

	return 0;
}

long adf_file_compat_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	switch (cmd) {
	case ADF_POST_CONFIG32:
		return adf_compat_post_config(file, compat_ptr(arg));

	case ADF_GET_DEVICE_DATA32:
		return adf_compat_get_device_data(file, compat_ptr(arg));

	case ADF_GET_INTERFACE_DATA32:
		return adf_compat_get_interface_data(file, compat_ptr(arg));

	case ADF_GET_OVERLAY_ENGINE_DATA32:
		return adf_compat_get_overlay_engine_data(file,
				compat_ptr(arg));

	default:
		return adf_file_ioctl(file, cmd, arg);
	}
}
