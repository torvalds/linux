// SPDX-License-Identifier: GPL-2.0

#include <linux/types.h>
#include <linux/kconfig.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/security.h>
#include <linux/highmem.h>
#include <linux/umh.h>
#include <linux/sysctl.h>
#include <linux/vmalloc.h>
#include <linux/module.h>

#include "fallback.h"
#include "firmware.h"

/*
 * firmware fallback mechanism
 */

MODULE_IMPORT_NS(FIRMWARE_LOADER_PRIVATE);

extern struct firmware_fallback_config fw_fallback_config;

/* These getters are vetted to use int properly */
static inline int __firmware_loading_timeout(void)
{
	return fw_fallback_config.loading_timeout;
}

/* These setters are vetted to use int properly */
static void __fw_fallback_set_timeout(int timeout)
{
	fw_fallback_config.loading_timeout = timeout;
}

/*
 * use small loading timeout for caching devices' firmware because all these
 * firmware images have been loaded successfully at lease once, also system is
 * ready for completing firmware loading now. The maximum size of firmware in
 * current distributions is about 2M bytes, so 10 secs should be enough.
 */
void fw_fallback_set_cache_timeout(void)
{
	fw_fallback_config.old_timeout = __firmware_loading_timeout();
	__fw_fallback_set_timeout(10);
}

/* Restores the timeout to the value last configured during normal operation */
void fw_fallback_set_default_timeout(void)
{
	__fw_fallback_set_timeout(fw_fallback_config.old_timeout);
}

static long firmware_loading_timeout(void)
{
	return __firmware_loading_timeout() > 0 ?
		__firmware_loading_timeout() * HZ : MAX_JIFFY_OFFSET;
}

static inline bool fw_sysfs_done(struct fw_priv *fw_priv)
{
	return __fw_state_check(fw_priv, FW_STATUS_DONE);
}

static inline bool fw_sysfs_loading(struct fw_priv *fw_priv)
{
	return __fw_state_check(fw_priv, FW_STATUS_LOADING);
}

static inline int fw_sysfs_wait_timeout(struct fw_priv *fw_priv,  long timeout)
{
	return __fw_state_wait_common(fw_priv, timeout);
}

struct fw_sysfs {
	bool nowait;
	struct device dev;
	struct fw_priv *fw_priv;
	struct firmware *fw;
};

static struct fw_sysfs *to_fw_sysfs(struct device *dev)
{
	return container_of(dev, struct fw_sysfs, dev);
}

static void __fw_load_abort(struct fw_priv *fw_priv)
{
	/*
	 * There is a small window in which user can write to 'loading'
	 * between loading done and disappearance of 'loading'
	 */
	if (fw_sysfs_done(fw_priv))
		return;

	list_del_init(&fw_priv->pending_list);
	fw_state_aborted(fw_priv);
}

static void fw_load_abort(struct fw_sysfs *fw_sysfs)
{
	struct fw_priv *fw_priv = fw_sysfs->fw_priv;

	__fw_load_abort(fw_priv);
}

static LIST_HEAD(pending_fw_head);

void kill_pending_fw_fallback_reqs(bool only_kill_custom)
{
	struct fw_priv *fw_priv;
	struct fw_priv *next;

	mutex_lock(&fw_lock);
	list_for_each_entry_safe(fw_priv, next, &pending_fw_head,
				 pending_list) {
		if (!fw_priv->need_uevent || !only_kill_custom)
			 __fw_load_abort(fw_priv);
	}
	mutex_unlock(&fw_lock);
}

static ssize_t timeout_show(struct class *class, struct class_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", __firmware_loading_timeout());
}

/**
 * firmware_timeout_store() - set number of seconds to wait for firmware
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
static ssize_t timeout_store(struct class *class, struct class_attribute *attr,
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

static void fw_dev_release(struct device *dev)
{
	struct fw_sysfs *fw_sysfs = to_fw_sysfs(dev);

	kfree(fw_sysfs);
}

static int do_firmware_uevent(struct fw_sysfs *fw_sysfs, struct kobj_uevent_env *env)
{
	if (add_uevent_var(env, "FIRMWARE=%s", fw_sysfs->fw_priv->fw_name))
		return -ENOMEM;
	if (add_uevent_var(env, "TIMEOUT=%i", __firmware_loading_timeout()))
		return -ENOMEM;
	if (add_uevent_var(env, "ASYNC=%d", fw_sysfs->nowait))
		return -ENOMEM;

	return 0;
}

static int firmware_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct fw_sysfs *fw_sysfs = to_fw_sysfs(dev);
	int err = 0;

	mutex_lock(&fw_lock);
	if (fw_sysfs->fw_priv)
		err = do_firmware_uevent(fw_sysfs, env);
	mutex_unlock(&fw_lock);
	return err;
}

static struct class firmware_class = {
	.name		= "firmware",
	.class_groups	= firmware_class_groups,
	.dev_uevent	= firmware_uevent,
	.dev_release	= fw_dev_release,
};

int register_sysfs_loader(void)
{
	return class_register(&firmware_class);
}

void unregister_sysfs_loader(void)
{
	class_unregister(&firmware_class);
}

static ssize_t firmware_loading_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct fw_sysfs *fw_sysfs = to_fw_sysfs(dev);
	int loading = 0;

	mutex_lock(&fw_lock);
	if (fw_sysfs->fw_priv)
		loading = fw_sysfs_loading(fw_sysfs->fw_priv);
	mutex_unlock(&fw_lock);

	return sprintf(buf, "%d\n", loading);
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
	if (fw_state_is_aborted(fw_priv))
		goto out;

	switch (loading) {
	case 1:
		/* discarding any previous partial load */
		if (!fw_sysfs_done(fw_priv)) {
			fw_free_paged_buf(fw_priv);
			fw_state_start(fw_priv);
		}
		break;
	case 0:
		if (fw_sysfs_loading(fw_priv)) {
			int rc;

			/*
			 * Several loading requests may be pending on
			 * one same firmware buf, so let all requests
			 * see the mapped 'buf->data' once the loading
			 * is completed.
			 * */
			rc = fw_map_paged_buf(fw_priv);
			if (rc)
				dev_err(dev, "%s: map pages failed\n",
					__func__);
			else
				rc = security_kernel_post_load_data(fw_priv->data,
						fw_priv->size,
						LOADING_FIRMWARE, "blob");

			/*
			 * Same logic as fw_load_abort, only the DONE bit
			 * is ignored and we set ABORT only on failure.
			 */
			list_del_init(&fw_priv->pending_list);
			if (rc) {
				fw_state_aborted(fw_priv);
				written = rc;
			} else {
				fw_state_done(fw_priv);
			}
			break;
		}
		fallthrough;
	default:
		dev_err(dev, "%s: unexpected value (%d)\n", __func__, loading);
		fallthrough;
	case -1:
		fw_load_abort(fw_sysfs);
		break;
	}
out:
	mutex_unlock(&fw_lock);
	return written;
}

static DEVICE_ATTR(loading, 0644, firmware_loading_show, firmware_loading_store);

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
		void *page_data;
		int page_nr = offset >> PAGE_SHIFT;
		int page_ofs = offset & (PAGE_SIZE-1);
		int page_cnt = min_t(size_t, PAGE_SIZE - page_ofs, count);

		page_data = kmap(fw_priv->pages[page_nr]);

		if (read)
			memcpy(buffer, page_data + page_ofs, page_cnt);
		else
			memcpy(page_data + page_ofs, buffer, page_cnt);

		kunmap(fw_priv->pages[page_nr]);
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
	if (!fw_priv || fw_sysfs_done(fw_priv)) {
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
	if (!fw_priv || fw_sysfs_done(fw_priv)) {
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
	NULL
};

static struct bin_attribute *fw_dev_bin_attrs[] = {
	&firmware_attr_data,
	NULL
};

static const struct attribute_group fw_dev_attr_group = {
	.attrs = fw_dev_attrs,
	.bin_attrs = fw_dev_bin_attrs,
};

static const struct attribute_group *fw_dev_attr_groups[] = {
	&fw_dev_attr_group,
	NULL
};

static struct fw_sysfs *
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

/**
 * fw_load_sysfs_fallback() - load a firmware via the sysfs fallback mechanism
 * @fw_sysfs: firmware sysfs information for the firmware to load
 * @timeout: timeout to wait for the load
 *
 * In charge of constructing a sysfs fallback interface for firmware loading.
 **/
static int fw_load_sysfs_fallback(struct fw_sysfs *fw_sysfs, long timeout)
{
	int retval = 0;
	struct device *f_dev = &fw_sysfs->dev;
	struct fw_priv *fw_priv = fw_sysfs->fw_priv;

	/* fall back on userspace loading */
	if (!fw_priv->data)
		fw_priv->is_paged_buf = true;

	dev_set_uevent_suppress(f_dev, true);

	retval = device_add(f_dev);
	if (retval) {
		dev_err(f_dev, "%s: device_register failed\n", __func__);
		goto err_put_dev;
	}

	mutex_lock(&fw_lock);
	list_add(&fw_priv->pending_list, &pending_fw_head);
	mutex_unlock(&fw_lock);

	if (fw_priv->opt_flags & FW_OPT_UEVENT) {
		fw_priv->need_uevent = true;
		dev_set_uevent_suppress(f_dev, false);
		dev_dbg(f_dev, "firmware: requesting %s\n", fw_priv->fw_name);
		kobject_uevent(&fw_sysfs->dev.kobj, KOBJ_ADD);
	} else {
		timeout = MAX_JIFFY_OFFSET;
	}

	retval = fw_sysfs_wait_timeout(fw_priv, timeout);
	if (retval < 0 && retval != -ENOENT) {
		mutex_lock(&fw_lock);
		fw_load_abort(fw_sysfs);
		mutex_unlock(&fw_lock);
	}

	if (fw_state_is_aborted(fw_priv)) {
		if (retval == -ERESTARTSYS)
			retval = -EINTR;
		else
			retval = -EAGAIN;
	} else if (fw_priv->is_paged_buf && !fw_priv->data)
		retval = -ENOMEM;

	device_del(f_dev);
err_put_dev:
	put_device(f_dev);
	return retval;
}

static int fw_load_from_user_helper(struct firmware *firmware,
				    const char *name, struct device *device,
				    u32 opt_flags)
{
	struct fw_sysfs *fw_sysfs;
	long timeout;
	int ret;

	timeout = firmware_loading_timeout();
	if (opt_flags & FW_OPT_NOWAIT) {
		timeout = usermodehelper_read_lock_wait(timeout);
		if (!timeout) {
			dev_dbg(device, "firmware: %s loading timed out\n",
				name);
			return -EBUSY;
		}
	} else {
		ret = usermodehelper_read_trylock();
		if (WARN_ON(ret)) {
			dev_err(device, "firmware: %s will not be loaded\n",
				name);
			return ret;
		}
	}

	fw_sysfs = fw_create_instance(firmware, name, device, opt_flags);
	if (IS_ERR(fw_sysfs)) {
		ret = PTR_ERR(fw_sysfs);
		goto out_unlock;
	}

	fw_sysfs->fw_priv = firmware->priv;
	ret = fw_load_sysfs_fallback(fw_sysfs, timeout);

	if (!ret)
		ret = assign_fw(firmware, device);

out_unlock:
	usermodehelper_read_unlock();

	return ret;
}

static bool fw_force_sysfs_fallback(u32 opt_flags)
{
	if (fw_fallback_config.force_sysfs_fallback)
		return true;
	if (!(opt_flags & FW_OPT_USERHELPER))
		return false;
	return true;
}

static bool fw_run_sysfs_fallback(u32 opt_flags)
{
	int ret;

	if (fw_fallback_config.ignore_sysfs_fallback) {
		pr_info_once("Ignoring firmware sysfs fallback due to sysctl knob\n");
		return false;
	}

	if ((opt_flags & FW_OPT_NOFALLBACK_SYSFS))
		return false;

	/* Also permit LSMs and IMA to fail firmware sysfs fallback */
	ret = security_kernel_load_data(LOADING_FIRMWARE, true);
	if (ret < 0)
		return false;

	return fw_force_sysfs_fallback(opt_flags);
}

/**
 * firmware_fallback_sysfs() - use the fallback mechanism to find firmware
 * @fw: pointer to firmware image
 * @name: name of firmware file to look for
 * @device: device for which firmware is being loaded
 * @ret: return value from direct lookup which triggered the fallback mechanism
 *
 * This function is called if direct lookup for the firmware failed, it enables
 * a fallback mechanism through userspace by exposing a sysfs loading
 * interface. Userspace is in charge of loading the firmware through the sysfs
 * loading interface. This sysfs fallback mechanism may be disabled completely
 * on a system by setting the proc sysctl value ignore_sysfs_fallback to true.
 * If this is false we check if the internal API caller set the
 * @FW_OPT_NOFALLBACK_SYSFS flag, if so it would also disable the fallback
 * mechanism. A system may want to enforce the sysfs fallback mechanism at all
 * times, it can do this by setting ignore_sysfs_fallback to false and
 * force_sysfs_fallback to true.
 * Enabling force_sysfs_fallback is functionally equivalent to build a kernel
 * with CONFIG_FW_LOADER_USER_HELPER_FALLBACK.
 **/
int firmware_fallback_sysfs(struct firmware *fw, const char *name,
			    struct device *device,
			    u32 opt_flags,
			    int ret)
{
	if (!fw_run_sysfs_fallback(opt_flags))
		return ret;

	if (!(opt_flags & FW_OPT_NO_WARN))
		dev_warn(device, "Falling back to sysfs fallback for: %s\n",
				 name);
	else
		dev_dbg(device, "Falling back to sysfs fallback for: %s\n",
				name);
	return fw_load_from_user_helper(fw, name, device, opt_flags);
}
