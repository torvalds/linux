// SPDX-License-Identifier: GPL-2.0

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "sysfs_upload.h"

/*
 * Support for user-space to initiate a firmware upload to a device.
 */

static const char * const fw_upload_prog_str[] = {
	[FW_UPLOAD_PROG_IDLE]	      = "idle",
	[FW_UPLOAD_PROG_RECEIVING]    = "receiving",
	[FW_UPLOAD_PROG_PREPARING]    = "preparing",
	[FW_UPLOAD_PROG_TRANSFERRING] = "transferring",
	[FW_UPLOAD_PROG_PROGRAMMING]  = "programming"
};

static const char * const fw_upload_err_str[] = {
	[FW_UPLOAD_ERR_NONE]	     = "none",
	[FW_UPLOAD_ERR_HW_ERROR]     = "hw-error",
	[FW_UPLOAD_ERR_TIMEOUT]	     = "timeout",
	[FW_UPLOAD_ERR_CANCELED]     = "user-abort",
	[FW_UPLOAD_ERR_BUSY]	     = "device-busy",
	[FW_UPLOAD_ERR_INVALID_SIZE] = "invalid-file-size",
	[FW_UPLOAD_ERR_RW_ERROR]     = "read-write-error",
	[FW_UPLOAD_ERR_WEAROUT]	     = "flash-wearout",
};

static const char *fw_upload_progress(struct device *dev,
				      enum fw_upload_prog prog)
{
	const char *status = "unknown-status";

	if (prog < FW_UPLOAD_PROG_MAX)
		status = fw_upload_prog_str[prog];
	else
		dev_err(dev, "Invalid status during secure update: %d\n", prog);

	return status;
}

static const char *fw_upload_error(struct device *dev,
				   enum fw_upload_err err_code)
{
	const char *error = "unknown-error";

	if (err_code < FW_UPLOAD_ERR_MAX)
		error = fw_upload_err_str[err_code];
	else
		dev_err(dev, "Invalid error code during secure update: %d\n",
			err_code);

	return error;
}

static ssize_t
status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fw_upload_priv *fwlp = to_fw_sysfs(dev)->fw_upload_priv;

	return sysfs_emit(buf, "%s\n", fw_upload_progress(dev, fwlp->progress));
}
DEVICE_ATTR_RO(status);

static ssize_t
error_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fw_upload_priv *fwlp = to_fw_sysfs(dev)->fw_upload_priv;
	int ret;

	mutex_lock(&fwlp->lock);

	if (fwlp->progress != FW_UPLOAD_PROG_IDLE)
		ret = -EBUSY;
	else if (!fwlp->err_code)
		ret = 0;
	else
		ret = sysfs_emit(buf, "%s:%s\n",
				 fw_upload_progress(dev, fwlp->err_progress),
				 fw_upload_error(dev, fwlp->err_code));

	mutex_unlock(&fwlp->lock);

	return ret;
}
DEVICE_ATTR_RO(error);

static ssize_t cancel_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct fw_upload_priv *fwlp = to_fw_sysfs(dev)->fw_upload_priv;
	int ret = count;
	bool cancel;

	if (kstrtobool(buf, &cancel) || !cancel)
		return -EINVAL;

	mutex_lock(&fwlp->lock);
	if (fwlp->progress == FW_UPLOAD_PROG_IDLE)
		ret = -ENODEV;

	fwlp->ops->cancel(fwlp->fw_upload);
	mutex_unlock(&fwlp->lock);

	return ret;
}
DEVICE_ATTR_WO(cancel);

static ssize_t remaining_size_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct fw_upload_priv *fwlp = to_fw_sysfs(dev)->fw_upload_priv;

	return sysfs_emit(buf, "%u\n", fwlp->remaining_size);
}
DEVICE_ATTR_RO(remaining_size);

umode_t
fw_upload_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	static struct fw_sysfs *fw_sysfs;

	fw_sysfs = to_fw_sysfs(kobj_to_dev(kobj));

	if (fw_sysfs->fw_upload_priv || attr == &dev_attr_loading.attr)
		return attr->mode;

	return 0;
}

static void fw_upload_update_progress(struct fw_upload_priv *fwlp,
				      enum fw_upload_prog new_progress)
{
	mutex_lock(&fwlp->lock);
	fwlp->progress = new_progress;
	mutex_unlock(&fwlp->lock);
}

static void fw_upload_set_error(struct fw_upload_priv *fwlp,
				enum fw_upload_err err_code)
{
	mutex_lock(&fwlp->lock);
	fwlp->err_progress = fwlp->progress;
	fwlp->err_code = err_code;
	mutex_unlock(&fwlp->lock);
}

static void fw_upload_prog_complete(struct fw_upload_priv *fwlp)
{
	mutex_lock(&fwlp->lock);
	fwlp->progress = FW_UPLOAD_PROG_IDLE;
	mutex_unlock(&fwlp->lock);
}

static void fw_upload_main(struct work_struct *work)
{
	struct fw_upload_priv *fwlp;
	struct fw_sysfs *fw_sysfs;
	u32 written = 0, offset = 0;
	enum fw_upload_err ret;
	struct device *fw_dev;
	struct fw_upload *fwl;

	fwlp = container_of(work, struct fw_upload_priv, work);
	fwl = fwlp->fw_upload;
	fw_sysfs = (struct fw_sysfs *)fwl->priv;
	fw_dev = &fw_sysfs->dev;

	fw_upload_update_progress(fwlp, FW_UPLOAD_PROG_PREPARING);
	ret = fwlp->ops->prepare(fwl, fwlp->data, fwlp->remaining_size);
	if (ret != FW_UPLOAD_ERR_NONE) {
		fw_upload_set_error(fwlp, ret);
		goto putdev_exit;
	}

	fw_upload_update_progress(fwlp, FW_UPLOAD_PROG_TRANSFERRING);
	while (fwlp->remaining_size) {
		ret = fwlp->ops->write(fwl, fwlp->data, offset,
					fwlp->remaining_size, &written);
		if (ret != FW_UPLOAD_ERR_NONE || !written) {
			if (ret == FW_UPLOAD_ERR_NONE) {
				dev_warn(fw_dev, "write-op wrote zero data\n");
				ret = FW_UPLOAD_ERR_RW_ERROR;
			}
			fw_upload_set_error(fwlp, ret);
			goto done;
		}

		fwlp->remaining_size -= written;
		offset += written;
	}

	fw_upload_update_progress(fwlp, FW_UPLOAD_PROG_PROGRAMMING);
	ret = fwlp->ops->poll_complete(fwl);
	if (ret != FW_UPLOAD_ERR_NONE)
		fw_upload_set_error(fwlp, ret);

done:
	if (fwlp->ops->cleanup)
		fwlp->ops->cleanup(fwl);

putdev_exit:
	put_device(fw_dev->parent);

	/*
	 * Note: fwlp->remaining_size is left unmodified here to provide
	 * additional information on errors. It will be reinitialized when
	 * the next firmeware upload begins.
	 */
	mutex_lock(&fw_lock);
	fw_free_paged_buf(fw_sysfs->fw_priv);
	fw_state_init(fw_sysfs->fw_priv);
	mutex_unlock(&fw_lock);
	fwlp->data = NULL;
	fw_upload_prog_complete(fwlp);
}

/*
 * Start a worker thread to upload data to the parent driver.
 * Must be called with fw_lock held.
 */
int fw_upload_start(struct fw_sysfs *fw_sysfs)
{
	struct fw_priv *fw_priv = fw_sysfs->fw_priv;
	struct device *fw_dev = &fw_sysfs->dev;
	struct fw_upload_priv *fwlp;

	if (!fw_sysfs->fw_upload_priv)
		return 0;

	if (!fw_priv->size) {
		fw_free_paged_buf(fw_priv);
		fw_state_init(fw_sysfs->fw_priv);
		return 0;
	}

	fwlp = fw_sysfs->fw_upload_priv;
	mutex_lock(&fwlp->lock);

	/* Do not interfere with an on-going fw_upload */
	if (fwlp->progress != FW_UPLOAD_PROG_IDLE) {
		mutex_unlock(&fwlp->lock);
		return -EBUSY;
	}

	get_device(fw_dev->parent); /* released in fw_upload_main */

	fwlp->progress = FW_UPLOAD_PROG_RECEIVING;
	fwlp->err_code = 0;
	fwlp->remaining_size = fw_priv->size;
	fwlp->data = fw_priv->data;

	pr_debug("%s: fw-%s fw_priv=%p data=%p size=%u\n",
		 __func__, fw_priv->fw_name,
		 fw_priv, fw_priv->data,
		 (unsigned int)fw_priv->size);

	queue_work(system_long_wq, &fwlp->work);
	mutex_unlock(&fwlp->lock);

	return 0;
}

void fw_upload_free(struct fw_sysfs *fw_sysfs)
{
	struct fw_upload_priv *fw_upload_priv = fw_sysfs->fw_upload_priv;

	free_fw_priv(fw_sysfs->fw_priv);
	kfree(fw_upload_priv->fw_upload);
	kfree(fw_upload_priv);
}

/**
 * firmware_upload_register() - register for the firmware upload sysfs API
 * @module: kernel module of this device
 * @parent: parent device instantiating firmware upload
 * @name: firmware name to be associated with this device
 * @ops: pointer to structure of firmware upload ops
 * @dd_handle: pointer to parent driver private data
 *
 *	@name must be unique among all users of firmware upload. The firmware
 *	sysfs files for this device will be found at /sys/class/firmware/@name.
 *
 *	Return: struct fw_upload pointer or ERR_PTR()
 *
 **/
struct fw_upload *
firmware_upload_register(struct module *module, struct device *parent,
			 const char *name, const struct fw_upload_ops *ops,
			 void *dd_handle)
{
	u32 opt_flags = FW_OPT_NOCACHE;
	struct fw_upload *fw_upload;
	struct fw_upload_priv *fw_upload_priv;
	struct fw_sysfs *fw_sysfs;
	struct fw_priv *fw_priv;
	struct device *fw_dev;
	int ret;

	if (!name || name[0] == '\0')
		return ERR_PTR(-EINVAL);

	if (!ops || !ops->cancel || !ops->prepare ||
	    !ops->write || !ops->poll_complete) {
		dev_err(parent, "Attempt to register without all required ops\n");
		return ERR_PTR(-EINVAL);
	}

	if (!try_module_get(module))
		return ERR_PTR(-EFAULT);

	fw_upload = kzalloc(sizeof(*fw_upload), GFP_KERNEL);
	if (!fw_upload) {
		ret = -ENOMEM;
		goto exit_module_put;
	}

	fw_upload_priv = kzalloc(sizeof(*fw_upload_priv), GFP_KERNEL);
	if (!fw_upload_priv) {
		ret = -ENOMEM;
		goto free_fw_upload;
	}

	fw_upload_priv->fw_upload = fw_upload;
	fw_upload_priv->ops = ops;
	mutex_init(&fw_upload_priv->lock);
	fw_upload_priv->module = module;
	fw_upload_priv->name = name;
	fw_upload_priv->err_code = 0;
	fw_upload_priv->progress = FW_UPLOAD_PROG_IDLE;
	INIT_WORK(&fw_upload_priv->work, fw_upload_main);
	fw_upload->dd_handle = dd_handle;

	fw_sysfs = fw_create_instance(NULL, name, parent, opt_flags);
	if (IS_ERR(fw_sysfs)) {
		ret = PTR_ERR(fw_sysfs);
		goto free_fw_upload_priv;
	}
	fw_upload->priv = fw_sysfs;
	fw_sysfs->fw_upload_priv = fw_upload_priv;
	fw_dev = &fw_sysfs->dev;

	ret = alloc_lookup_fw_priv(name, &fw_cache, &fw_priv,  NULL, 0, 0,
				   FW_OPT_NOCACHE);
	if (ret != 0) {
		if (ret > 0)
			ret = -EINVAL;
		goto free_fw_sysfs;
	}
	fw_priv->is_paged_buf = true;
	fw_sysfs->fw_priv = fw_priv;

	ret = device_add(fw_dev);
	if (ret) {
		dev_err(fw_dev, "%s: device_register failed\n", __func__);
		put_device(fw_dev);
		goto exit_module_put;
	}

	return fw_upload;

free_fw_sysfs:
	kfree(fw_sysfs);

free_fw_upload_priv:
	kfree(fw_upload_priv);

free_fw_upload:
	kfree(fw_upload);

exit_module_put:
	module_put(module);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(firmware_upload_register);

/**
 * firmware_upload_unregister() - Unregister firmware upload interface
 * @fw_upload: pointer to struct fw_upload
 **/
void firmware_upload_unregister(struct fw_upload *fw_upload)
{
	struct fw_sysfs *fw_sysfs = fw_upload->priv;
	struct fw_upload_priv *fw_upload_priv = fw_sysfs->fw_upload_priv;
	struct module *module = fw_upload_priv->module;

	mutex_lock(&fw_upload_priv->lock);
	if (fw_upload_priv->progress == FW_UPLOAD_PROG_IDLE) {
		mutex_unlock(&fw_upload_priv->lock);
		goto unregister;
	}

	fw_upload_priv->ops->cancel(fw_upload);
	mutex_unlock(&fw_upload_priv->lock);

	/* Ensure lower-level device-driver is finished */
	flush_work(&fw_upload_priv->work);

unregister:
	device_unregister(&fw_sysfs->dev);
	module_put(module);
}
EXPORT_SYMBOL_GPL(firmware_upload_unregister);
