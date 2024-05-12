// SPDX-License-Identifier: GPL-2.0

#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "sysfs.h"

/*
 * sysfs support for firmware loader
 */

void __fw_load_abort(struct fw_priv *fw_priv)
{
	/*
	 * There is a small window in which user can write to 'loading'
	 * between loading done/aborted and disappearance of 'loading'
	 */
	if (fw_state_is_aborted(fw_priv) || fw_state_is_done(fw_priv))
		return;

	fw_state_aborted(fw_priv);
}

#ifdef CONFIG_FW_LOADER_USER_HELPER
static ssize_t timeout_show(const struct class *class, const struct class_attribute *attr,
			    char *buf)
{
	return sysfs_emit(buf, "%d\n", __firmware_loading_timeout());
}

/**
 * timeout_store() - set number of seconds to wait for firmware
 * @class: device class pointer
 * @attr: device attribute pointer
 * @buf: buffer to scan for timeout value
 * @count: number of bytes in @buf
 *
 *	Sets the number of seconds to wait for the firmware.  Once
 *	this expires an error will be returned to the driver and no
 *	firmware will be provided.
 *
 *	Note: zero means 'wait forever'.
 **/
static ssize_t timeout_store(const struct class *class, const struct class_attribute *attr,
			     const char *buf, size_t count)
{
	int tmp_loading_timeout = simple_strtol(buf, NULL, 10);

	if (tmp_loading_timeout < 0)
		tmp_loading_timeout = 0;

	__fw_fallback_set_timeout(tmp_loading_timeout);

	return count;
}
static CLASS_ATTR_RW(timeout);

static struct attribute *firmware_class_attrs[] = {
	&class_attr_timeout.attr,
	NULL,
};
ATTRIBUTE_GROUPS(firmware_class);

static int do_firmware_uevent(const struct fw_sysfs *fw_sysfs, struct kobj_uevent_env *env)
{
	if (add_uevent_var(env, "FIRMWARE=%s", fw_sysfs->fw_priv->fw_name))
		return -ENOMEM;
	if (add_uevent_var(env, "TIMEOUT=%i", __firmware_loading_timeout()))
		return -ENOMEM;
	if (add_uevent_var(env, "ASYNC=%d", fw_sysfs->nowait))
		return -ENOMEM;

	return 0;
}

static int firmware_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct fw_sysfs *fw_sysfs = to_fw_sysfs(dev);
	int err = 0;

	mutex_lock(&fw_lock);
	if (fw_sysfs->fw_priv)
		err = do_firmware_uevent(fw_sysfs, env);
	mutex_unlock(&fw_lock);
	return err;
}
#endif /* CONFIG_FW_LOADER_USER_HELPER */

static void fw_dev_release(struct device *dev)
{
	struct fw_sysfs *fw_sysfs = to_fw_sysfs(dev);

	if (fw_sysfs->fw_upload_priv)
		fw_upload_free(fw_sysfs);

	kfree(fw_sysfs);
}

static struct class firmware_class = {
	.name		= "firmware",
#ifdef CONFIG_FW_LOADER_USER_HELPER
	.class_groups	= firmware_class_groups,
	.dev_uevent	= firmware_uevent,
#endif
	.dev_release	= fw_dev_release,
};

int register_sysfs_loader(void)
{
	int ret = class_register(&firmware_class);

	if (ret != 0)
		return ret;
	return register_firmware_config_sysctl();
}

void unregister_sysfs_loader(void)
{
	unregister_firmware_config_sysctl();
	class_unregister(&firmware_class);
}

static ssize_t firmware_loading_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct fw_sysfs *fw_sysfs = to_fw_sysfs(dev);
	int loading = 0;

	mutex_lock(&fw_lock);
	if (fw_sysfs->fw_priv)
		loading = fw_state_is_loading(fw_sysfs->fw_priv);
	mutex_unlock(&fw_lock);

	return sysfs_emit(buf, "%d\n", loading);
}

/**
 * firmware_loading_store() - set value in the 'loading' control file
 * @dev: device pointer
 * @attr: device attribute pointer
 * @buf: buffer to scan for loading control value
 * @count: number of bytes in @buf
 *
 *	The relevant values are:
 *
 *	 1: Start a load, discarding any previous partial load.
 *	 0: Conclude the load and hand the data to the driver code.
 *	-1: Conclude the load with an error and discard any written data.
 **/
static ssize_t firmware_loading_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct fw_sysfs *fw_sysfs = to_fw_sysfs(dev);
	struct fw_priv *fw_priv;
	ssize_t written = count;
	int loading = simple_strtol(buf, NULL, 10);

	mutex_lock(&fw_lock);
	fw_priv = fw_sysfs->fw_priv;
	if (fw_state_is_aborted(fw_priv) || fw_state_is_done(fw_priv))
		goto out;

	switch (loading) {
	case 1:
		/* discarding any previous partial load */
		fw_free_paged_buf(fw_priv);
		fw_state_start(fw_priv);
		break;
	case 0:
		if (fw_state_is_loading(fw_priv)) {
			int rc;

			/*
			 * Several loading requests may be pending on
			 * one same firmware buf, so let all requests
			 * see the mapped 'buf->data' once the loading
			 * is completed.
			 */
			rc = fw_map_paged_buf(fw_priv);
			if (rc)
				dev_err(dev, "%s: map pages failed\n",
					__func__);
			else
				rc = security_kernel_post_load_data(fw_priv->data,
								    fw_priv->size,
								    LOADING_FIRMWARE,
								    "blob");

			/*
			 * Same logic as fw_load_abort, only the DONE bit
			 * is ignored and we set ABORT only on failure.
			 */
			if (rc) {
				fw_state_aborted(fw_priv);
				written = rc;
			} else {
				fw_state_done(fw_priv);

				/*
				 * If this is a user-initiated firmware upload
				 * then start the upload in a worker thread now.
				 */
				rc = fw_upload_start(fw_sysfs);
				if (rc)
					written = rc;
			}
			break;
		}
		fallthrough;
	default:
		dev_err(dev, "%s: unexpected value (%d)\n", __func__, loading);
		fallthrough;
	case -1:
		fw_load_abort(fw_sysfs);
		if (fw_sysfs->fw_upload_priv)
			fw_state_init(fw_sysfs->fw_priv);

		break;
	}
out:
	mutex_unlock(&fw_lock);
	return written;
}

DEVICE_ATTR(loading, 0644, firmware_loading_show, firmware_loading_store);

static void firmware_rw_data(struct fw_priv *fw_priv, char *buffer,
			     loff_t offset, size_t count, bool read)
{
	if (read)
		memcpy(buffer, fw_priv->data + offset, count);
	else
		memcpy(fw_priv->data + offset, buffer, count);
}

static void firmware_rw(struct fw_priv *fw_priv, char *buffer,
			loff_t offset, size_t count, bool read)
{
	while (count) {
		int page_nr = offset >> PAGE_SHIFT;
		int page_ofs = offset & (PAGE_SIZE - 1);
		int page_cnt = min_t(size_t, PAGE_SIZE - page_ofs, count);

		if (read)
			memcpy_from_page(buffer, fw_priv->pages[page_nr],
					 page_ofs, page_cnt);
		else
			memcpy_to_page(fw_priv->pages[page_nr], page_ofs,
				       buffer, page_cnt);

		buffer += page_cnt;
		offset += page_cnt;
		count -= page_cnt;
	}
}

static ssize_t firmware_data_read(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buffer, loff_t offset, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct fw_sysfs *fw_sysfs = to_fw_sysfs(dev);
	struct fw_priv *fw_priv;
	ssize_t ret_count;

	mutex_lock(&fw_lock);
	fw_priv = fw_sysfs->fw_priv;
	if (!fw_priv || fw_state_is_done(fw_priv)) {
		ret_count = -ENODEV;
		goto out;
	}
	if (offset > fw_priv->size) {
		ret_count = 0;
		goto out;
	}
	if (count > fw_priv->size - offset)
		count = fw_priv->size - offset;

	ret_count = count;

	if (fw_priv->data)
		firmware_rw_data(fw_priv, buffer, offset, count, true);
	else
		firmware_rw(fw_priv, buffer, offset, count, true);

out:
	mutex_unlock(&fw_lock);
	return ret_count;
}

static int fw_realloc_pages(struct fw_sysfs *fw_sysfs, int min_size)
{
	int err;

	err = fw_grow_paged_buf(fw_sysfs->fw_priv,
				PAGE_ALIGN(min_size) >> PAGE_SHIFT);
	if (err)
		fw_load_abort(fw_sysfs);
	return err;
}

/**
 * firmware_data_write() - write method for firmware
 * @filp: open sysfs file
 * @kobj: kobject for the device
 * @bin_attr: bin_attr structure
 * @buffer: buffer being written
 * @offset: buffer offset for write in total data store area
 * @count: buffer size
 *
 *	Data written to the 'data' attribute will be later handed to
 *	the driver as a firmware image.
 **/
static ssize_t firmware_data_write(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *bin_attr,
				   char *buffer, loff_t offset, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct fw_sysfs *fw_sysfs = to_fw_sysfs(dev);
	struct fw_priv *fw_priv;
	ssize_t retval;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	mutex_lock(&fw_lock);
	fw_priv = fw_sysfs->fw_priv;
	if (!fw_priv || fw_state_is_done(fw_priv)) {
		retval = -ENODEV;
		goto out;
	}

	if (fw_priv->data) {
		if (offset + count > fw_priv->allocated_size) {
			retval = -ENOMEM;
			goto out;
		}
		firmware_rw_data(fw_priv, buffer, offset, count, false);
		retval = count;
	} else {
		retval = fw_realloc_pages(fw_sysfs, offset + count);
		if (retval)
			goto out;

		retval = count;
		firmware_rw(fw_priv, buffer, offset, count, false);
	}

	fw_priv->size = max_t(size_t, offset + count, fw_priv->size);
out:
	mutex_unlock(&fw_lock);
	return retval;
}

static struct bin_attribute firmware_attr_data = {
	.attr = { .name = "data", .mode = 0644 },
	.size = 0,
	.read = firmware_data_read,
	.write = firmware_data_write,
};

static struct attribute *fw_dev_attrs[] = {
	&dev_attr_loading.attr,
#ifdef CONFIG_FW_UPLOAD
	&dev_attr_cancel.attr,
	&dev_attr_status.attr,
	&dev_attr_error.attr,
	&dev_attr_remaining_size.attr,
#endif
	NULL
};

static struct bin_attribute *fw_dev_bin_attrs[] = {
	&firmware_attr_data,
	NULL
};

static const struct attribute_group fw_dev_attr_group = {
	.attrs = fw_dev_attrs,
	.bin_attrs = fw_dev_bin_attrs,
#ifdef CONFIG_FW_UPLOAD
	.is_visible = fw_upload_is_visible,
#endif
};

static const struct attribute_group *fw_dev_attr_groups[] = {
	&fw_dev_attr_group,
	NULL
};

struct fw_sysfs *
fw_create_instance(struct firmware *firmware, const char *fw_name,
		   struct device *device, u32 opt_flags)
{
	struct fw_sysfs *fw_sysfs;
	struct device *f_dev;

	fw_sysfs = kzalloc(sizeof(*fw_sysfs), GFP_KERNEL);
	if (!fw_sysfs) {
		fw_sysfs = ERR_PTR(-ENOMEM);
		goto exit;
	}

	fw_sysfs->nowait = !!(opt_flags & FW_OPT_NOWAIT);
	fw_sysfs->fw = firmware;
	f_dev = &fw_sysfs->dev;

	device_initialize(f_dev);
	dev_set_name(f_dev, "%s", fw_name);
	f_dev->parent = device;
	f_dev->class = &firmware_class;
	f_dev->groups = fw_dev_attr_groups;
exit:
	return fw_sysfs;
}
