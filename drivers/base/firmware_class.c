/*
 * firmware_class.c - Multi purpose firmware loading support
 *
 * Copyright (c) 2003 Manuel Estrada Sainz
 *
 * Please see Documentation/firmware_class/ for more information.
 *
 */

#include <linux/capability.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/highmem.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/sched.h>

MODULE_AUTHOR("Manuel Estrada Sainz");
MODULE_DESCRIPTION("Multi purpose firmware loading support");
MODULE_LICENSE("GPL");

/* Builtin firmware support */

#ifdef CONFIG_FW_LOADER

extern struct builtin_fw __start_builtin_fw[];
extern struct builtin_fw __end_builtin_fw[];

static bool fw_get_builtin_firmware(struct firmware *fw, const char *name)
{
	struct builtin_fw *b_fw;

	for (b_fw = __start_builtin_fw; b_fw != __end_builtin_fw; b_fw++) {
		if (strcmp(name, b_fw->name) == 0) {
			fw->size = b_fw->size;
			fw->data = b_fw->data;
			return true;
		}
	}

	return false;
}

static bool fw_is_builtin_firmware(const struct firmware *fw)
{
	struct builtin_fw *b_fw;

	for (b_fw = __start_builtin_fw; b_fw != __end_builtin_fw; b_fw++)
		if (fw->data == b_fw->data)
			return true;

	return false;
}

#else /* Module case - no builtin firmware support */

static inline bool fw_get_builtin_firmware(struct firmware *fw, const char *name)
{
	return false;
}

static inline bool fw_is_builtin_firmware(const struct firmware *fw)
{
	return false;
}
#endif

enum {
	FW_STATUS_LOADING,
	FW_STATUS_DONE,
	FW_STATUS_ABORT,
};

static int loading_timeout = 60;	/* In seconds */

static inline long firmware_loading_timeout(void)
{
	return loading_timeout > 0 ? loading_timeout * HZ : MAX_SCHEDULE_TIMEOUT;
}

/* fw_lock could be moved to 'struct firmware_priv' but since it is just
 * guarding for corner cases a global lock should be OK */
static DEFINE_MUTEX(fw_lock);

struct firmware_priv {
	struct completion completion;
	struct firmware *fw;
	unsigned long status;
	struct page **pages;
	int nr_pages;
	int page_array_size;
	struct timer_list timeout;
	struct device dev;
	bool nowait;
	char fw_id[];
};

static struct firmware_priv *to_firmware_priv(struct device *dev)
{
	return container_of(dev, struct firmware_priv, dev);
}

static void fw_load_abort(struct firmware_priv *fw_priv)
{
	set_bit(FW_STATUS_ABORT, &fw_priv->status);
	wmb();
	complete(&fw_priv->completion);
}

static ssize_t firmware_timeout_show(struct class *class,
				     struct class_attribute *attr,
				     char *buf)
{
	return sprintf(buf, "%d\n", loading_timeout);
}

/**
 * firmware_timeout_store - set number of seconds to wait for firmware
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
static ssize_t firmware_timeout_store(struct class *class,
				      struct class_attribute *attr,
				      const char *buf, size_t count)
{
	loading_timeout = simple_strtol(buf, NULL, 10);
	if (loading_timeout < 0)
		loading_timeout = 0;

	return count;
}

static struct class_attribute firmware_class_attrs[] = {
	__ATTR(timeout, S_IWUSR | S_IRUGO,
		firmware_timeout_show, firmware_timeout_store),
	__ATTR_NULL
};

static void fw_dev_release(struct device *dev)
{
	struct firmware_priv *fw_priv = to_firmware_priv(dev);
	int i;

	for (i = 0; i < fw_priv->nr_pages; i++)
		__free_page(fw_priv->pages[i]);
	kfree(fw_priv->pages);
	kfree(fw_priv);

	module_put(THIS_MODULE);
}

static int firmware_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct firmware_priv *fw_priv = to_firmware_priv(dev);

	if (add_uevent_var(env, "FIRMWARE=%s", fw_priv->fw_id))
		return -ENOMEM;
	if (add_uevent_var(env, "TIMEOUT=%i", loading_timeout))
		return -ENOMEM;
	if (add_uevent_var(env, "ASYNC=%d", fw_priv->nowait))
		return -ENOMEM;

	return 0;
}

static struct class firmware_class = {
	.name		= "firmware",
	.class_attrs	= firmware_class_attrs,
	.dev_uevent	= firmware_uevent,
	.dev_release	= fw_dev_release,
};

static ssize_t firmware_loading_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct firmware_priv *fw_priv = to_firmware_priv(dev);
	int loading = test_bit(FW_STATUS_LOADING, &fw_priv->status);

	return sprintf(buf, "%d\n", loading);
}

static void firmware_free_data(const struct firmware *fw)
{
	int i;
	vunmap(fw->data);
	if (fw->pages) {
		for (i = 0; i < PFN_UP(fw->size); i++)
			__free_page(fw->pages[i]);
		kfree(fw->pages);
	}
}

/* Some architectures don't have PAGE_KERNEL_RO */
#ifndef PAGE_KERNEL_RO
#define PAGE_KERNEL_RO PAGE_KERNEL
#endif
/**
 * firmware_loading_store - set value in the 'loading' control file
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
	struct firmware_priv *fw_priv = to_firmware_priv(dev);
	int loading = simple_strtol(buf, NULL, 10);
	int i;

	mutex_lock(&fw_lock);

	if (!fw_priv->fw)
		goto out;

	switch (loading) {
	case 1:
		firmware_free_data(fw_priv->fw);
		memset(fw_priv->fw, 0, sizeof(struct firmware));
		/* If the pages are not owned by 'struct firmware' */
		for (i = 0; i < fw_priv->nr_pages; i++)
			__free_page(fw_priv->pages[i]);
		kfree(fw_priv->pages);
		fw_priv->pages = NULL;
		fw_priv->page_array_size = 0;
		fw_priv->nr_pages = 0;
		set_bit(FW_STATUS_LOADING, &fw_priv->status);
		break;
	case 0:
		if (test_bit(FW_STATUS_LOADING, &fw_priv->status)) {
			vunmap(fw_priv->fw->data);
			fw_priv->fw->data = vmap(fw_priv->pages,
						 fw_priv->nr_pages,
						 0, PAGE_KERNEL_RO);
			if (!fw_priv->fw->data) {
				dev_err(dev, "%s: vmap() failed\n", __func__);
				goto err;
			}
			/* Pages are now owned by 'struct firmware' */
			fw_priv->fw->pages = fw_priv->pages;
			fw_priv->pages = NULL;

			fw_priv->page_array_size = 0;
			fw_priv->nr_pages = 0;
			complete(&fw_priv->completion);
			clear_bit(FW_STATUS_LOADING, &fw_priv->status);
			break;
		}
		/* fallthrough */
	default:
		dev_err(dev, "%s: unexpected value (%d)\n", __func__, loading);
		/* fallthrough */
	case -1:
	err:
		fw_load_abort(fw_priv);
		break;
	}
out:
	mutex_unlock(&fw_lock);
	return count;
}

static DEVICE_ATTR(loading, 0644, firmware_loading_show, firmware_loading_store);

static ssize_t firmware_data_read(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buffer, loff_t offset, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct firmware_priv *fw_priv = to_firmware_priv(dev);
	struct firmware *fw;
	ssize_t ret_count;

	mutex_lock(&fw_lock);
	fw = fw_priv->fw;
	if (!fw || test_bit(FW_STATUS_DONE, &fw_priv->status)) {
		ret_count = -ENODEV;
		goto out;
	}
	if (offset > fw->size) {
		ret_count = 0;
		goto out;
	}
	if (count > fw->size - offset)
		count = fw->size - offset;

	ret_count = count;

	while (count) {
		void *page_data;
		int page_nr = offset >> PAGE_SHIFT;
		int page_ofs = offset & (PAGE_SIZE-1);
		int page_cnt = min_t(size_t, PAGE_SIZE - page_ofs, count);

		page_data = kmap(fw_priv->pages[page_nr]);

		memcpy(buffer, page_data + page_ofs, page_cnt);

		kunmap(fw_priv->pages[page_nr]);
		buffer += page_cnt;
		offset += page_cnt;
		count -= page_cnt;
	}
out:
	mutex_unlock(&fw_lock);
	return ret_count;
}

static int fw_realloc_buffer(struct firmware_priv *fw_priv, int min_size)
{
	int pages_needed = ALIGN(min_size, PAGE_SIZE) >> PAGE_SHIFT;

	/* If the array of pages is too small, grow it... */
	if (fw_priv->page_array_size < pages_needed) {
		int new_array_size = max(pages_needed,
					 fw_priv->page_array_size * 2);
		struct page **new_pages;

		new_pages = kmalloc(new_array_size * sizeof(void *),
				    GFP_KERNEL);
		if (!new_pages) {
			fw_load_abort(fw_priv);
			return -ENOMEM;
		}
		memcpy(new_pages, fw_priv->pages,
		       fw_priv->page_array_size * sizeof(void *));
		memset(&new_pages[fw_priv->page_array_size], 0, sizeof(void *) *
		       (new_array_size - fw_priv->page_array_size));
		kfree(fw_priv->pages);
		fw_priv->pages = new_pages;
		fw_priv->page_array_size = new_array_size;
	}

	while (fw_priv->nr_pages < pages_needed) {
		fw_priv->pages[fw_priv->nr_pages] =
			alloc_page(GFP_KERNEL | __GFP_HIGHMEM);

		if (!fw_priv->pages[fw_priv->nr_pages]) {
			fw_load_abort(fw_priv);
			return -ENOMEM;
		}
		fw_priv->nr_pages++;
	}
	return 0;
}

/**
 * firmware_data_write - write method for firmware
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
	struct firmware_priv *fw_priv = to_firmware_priv(dev);
	struct firmware *fw;
	ssize_t retval;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	mutex_lock(&fw_lock);
	fw = fw_priv->fw;
	if (!fw || test_bit(FW_STATUS_DONE, &fw_priv->status)) {
		retval = -ENODEV;
		goto out;
	}
	retval = fw_realloc_buffer(fw_priv, offset + count);
	if (retval)
		goto out;

	retval = count;

	while (count) {
		void *page_data;
		int page_nr = offset >> PAGE_SHIFT;
		int page_ofs = offset & (PAGE_SIZE - 1);
		int page_cnt = min_t(size_t, PAGE_SIZE - page_ofs, count);

		page_data = kmap(fw_priv->pages[page_nr]);

		memcpy(page_data + page_ofs, buffer, page_cnt);

		kunmap(fw_priv->pages[page_nr]);
		buffer += page_cnt;
		offset += page_cnt;
		count -= page_cnt;
	}

	fw->size = max_t(size_t, offset, fw->size);
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

static void firmware_class_timeout(u_long data)
{
	struct firmware_priv *fw_priv = (struct firmware_priv *) data;

	fw_load_abort(fw_priv);
}

static struct firmware_priv *
fw_create_instance(struct firmware *firmware, const char *fw_name,
		   struct device *device, bool uevent, bool nowait)
{
	struct firmware_priv *fw_priv;
	struct device *f_dev;

	fw_priv = kzalloc(sizeof(*fw_priv) + strlen(fw_name) + 1 , GFP_KERNEL);
	if (!fw_priv) {
		dev_err(device, "%s: kmalloc failed\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	fw_priv->fw = firmware;
	fw_priv->nowait = nowait;
	strcpy(fw_priv->fw_id, fw_name);
	init_completion(&fw_priv->completion);
	setup_timer(&fw_priv->timeout,
		    firmware_class_timeout, (u_long) fw_priv);

	f_dev = &fw_priv->dev;

	device_initialize(f_dev);
	dev_set_name(f_dev, "%s", dev_name(device));
	f_dev->parent = device;
	f_dev->class = &firmware_class;

	return fw_priv;
}

static struct firmware_priv *
_request_firmware_prepare(const struct firmware **firmware_p, const char *name,
			  struct device *device, bool uevent, bool nowait)
{
	struct firmware *firmware;
	struct firmware_priv *fw_priv;

	if (!firmware_p)
		return ERR_PTR(-EINVAL);

	*firmware_p = firmware = kzalloc(sizeof(*firmware), GFP_KERNEL);
	if (!firmware) {
		dev_err(device, "%s: kmalloc(struct firmware) failed\n",
			__func__);
		return ERR_PTR(-ENOMEM);
	}

	if (fw_get_builtin_firmware(firmware, name)) {
		dev_dbg(device, "firmware: using built-in firmware %s\n", name);
		return NULL;
	}

	fw_priv = fw_create_instance(firmware, name, device, uevent, nowait);
	if (IS_ERR(fw_priv)) {
		release_firmware(firmware);
		*firmware_p = NULL;
	}
	return fw_priv;
}

static void _request_firmware_cleanup(const struct firmware **firmware_p)
{
	release_firmware(*firmware_p);
	*firmware_p = NULL;
}

static int _request_firmware_load(struct firmware_priv *fw_priv, bool uevent,
				  long timeout)
{
	int retval = 0;
	struct device *f_dev = &fw_priv->dev;

	dev_set_uevent_suppress(f_dev, true);

	/* Need to pin this module until class device is destroyed */
	__module_get(THIS_MODULE);

	retval = device_add(f_dev);
	if (retval) {
		dev_err(f_dev, "%s: device_register failed\n", __func__);
		goto err_put_dev;
	}

	retval = device_create_bin_file(f_dev, &firmware_attr_data);
	if (retval) {
		dev_err(f_dev, "%s: sysfs_create_bin_file failed\n", __func__);
		goto err_del_dev;
	}

	retval = device_create_file(f_dev, &dev_attr_loading);
	if (retval) {
		dev_err(f_dev, "%s: device_create_file failed\n", __func__);
		goto err_del_bin_attr;
	}

	if (uevent) {
		dev_set_uevent_suppress(f_dev, false);
		dev_dbg(f_dev, "firmware: requesting %s\n", fw_priv->fw_id);
		if (timeout != MAX_SCHEDULE_TIMEOUT)
			mod_timer(&fw_priv->timeout,
				  round_jiffies_up(jiffies + timeout));

		kobject_uevent(&fw_priv->dev.kobj, KOBJ_ADD);
	}

	wait_for_completion(&fw_priv->completion);

	set_bit(FW_STATUS_DONE, &fw_priv->status);
	del_timer_sync(&fw_priv->timeout);

	mutex_lock(&fw_lock);
	if (!fw_priv->fw->size || test_bit(FW_STATUS_ABORT, &fw_priv->status))
		retval = -ENOENT;
	fw_priv->fw = NULL;
	mutex_unlock(&fw_lock);

	device_remove_file(f_dev, &dev_attr_loading);
err_del_bin_attr:
	device_remove_bin_file(f_dev, &firmware_attr_data);
err_del_dev:
	device_del(f_dev);
err_put_dev:
	put_device(f_dev);
	return retval;
}

/**
 * request_firmware: - send firmware request and wait for it
 * @firmware_p: pointer to firmware image
 * @name: name of firmware file
 * @device: device for which firmware is being loaded
 *
 *      @firmware_p will be used to return a firmware image by the name
 *      of @name for device @device.
 *
 *      Should be called from user context where sleeping is allowed.
 *
 *      @name will be used as $FIRMWARE in the uevent environment and
 *      should be distinctive enough not to be confused with any other
 *      firmware image for this or any other device.
 **/
int
request_firmware(const struct firmware **firmware_p, const char *name,
                 struct device *device)
{
	struct firmware_priv *fw_priv;
	int ret;

	fw_priv = _request_firmware_prepare(firmware_p, name, device, true,
					    false);
	if (IS_ERR_OR_NULL(fw_priv))
		return PTR_RET(fw_priv);

	ret = usermodehelper_read_trylock();
	if (WARN_ON(ret)) {
		dev_err(device, "firmware: %s will not be loaded\n", name);
	} else {
		ret = _request_firmware_load(fw_priv, true,
					firmware_loading_timeout());
		usermodehelper_read_unlock();
	}
	if (ret)
		_request_firmware_cleanup(firmware_p);

	return ret;
}

/**
 * release_firmware: - release the resource associated with a firmware image
 * @fw: firmware resource to release
 **/
void release_firmware(const struct firmware *fw)
{
	if (fw) {
		if (!fw_is_builtin_firmware(fw))
			firmware_free_data(fw);
		kfree(fw);
	}
}

/* Async support */
struct firmware_work {
	struct work_struct work;
	struct module *module;
	const char *name;
	struct device *device;
	void *context;
	void (*cont)(const struct firmware *fw, void *context);
	bool uevent;
};

static void request_firmware_work_func(struct work_struct *work)
{
	struct firmware_work *fw_work;
	const struct firmware *fw;
	struct firmware_priv *fw_priv;
	long timeout;
	int ret;

	fw_work = container_of(work, struct firmware_work, work);
	fw_priv = _request_firmware_prepare(&fw, fw_work->name, fw_work->device,
			fw_work->uevent, true);
	if (IS_ERR_OR_NULL(fw_priv)) {
		ret = PTR_RET(fw_priv);
		goto out;
	}

	timeout = usermodehelper_read_lock_wait(firmware_loading_timeout());
	if (timeout) {
		ret = _request_firmware_load(fw_priv, fw_work->uevent, timeout);
		usermodehelper_read_unlock();
	} else {
		dev_dbg(fw_work->device, "firmware: %s loading timed out\n",
			fw_work->name);
		ret = -EAGAIN;
	}
	if (ret)
		_request_firmware_cleanup(&fw);

 out:
	fw_work->cont(fw, fw_work->context);

	module_put(fw_work->module);
	kfree(fw_work);
}

/**
 * request_firmware_nowait - asynchronous version of request_firmware
 * @module: module requesting the firmware
 * @uevent: sends uevent to copy the firmware image if this flag
 *	is non-zero else the firmware copy must be done manually.
 * @name: name of firmware file
 * @device: device for which firmware is being loaded
 * @gfp: allocation flags
 * @context: will be passed over to @cont, and
 *	@fw may be %NULL if firmware request fails.
 * @cont: function will be called asynchronously when the firmware
 *	request is over.
 *
 *	Asynchronous variant of request_firmware() for user contexts where
 *	it is not possible to sleep for long time. It can't be called
 *	in atomic contexts.
 **/
int
request_firmware_nowait(
	struct module *module, bool uevent,
	const char *name, struct device *device, gfp_t gfp, void *context,
	void (*cont)(const struct firmware *fw, void *context))
{
	struct firmware_work *fw_work;

	fw_work = kzalloc(sizeof (struct firmware_work), gfp);
	if (!fw_work)
		return -ENOMEM;

	fw_work->module = module;
	fw_work->name = name;
	fw_work->device = device;
	fw_work->context = context;
	fw_work->cont = cont;
	fw_work->uevent = uevent;

	if (!try_module_get(module)) {
		kfree(fw_work);
		return -EFAULT;
	}

	INIT_WORK(&fw_work->work, request_firmware_work_func);
	schedule_work(&fw_work->work);
	return 0;
}

static int __init firmware_class_init(void)
{
	return class_register(&firmware_class);
}

static void __exit firmware_class_exit(void)
{
	class_unregister(&firmware_class);
}

fs_initcall(firmware_class_init);
module_exit(firmware_class_exit);

EXPORT_SYMBOL(release_firmware);
EXPORT_SYMBOL(request_firmware);
EXPORT_SYMBOL(request_firmware_nowait);
