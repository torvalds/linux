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
#include <linux/file.h>
#include <linux/list.h>
#include <linux/async.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/reboot.h>
#include <linux/security.h>

#include <generated/utsrelease.h>

#include "base.h"

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
	return loading_timeout > 0 ? loading_timeout * HZ : MAX_JIFFY_OFFSET;
}

/* firmware behavior options */
#define FW_OPT_UEVENT	(1U << 0)
#define FW_OPT_NOWAIT	(1U << 1)
#ifdef CONFIG_FW_LOADER_USER_HELPER
#define FW_OPT_USERHELPER	(1U << 2)
#else
#define FW_OPT_USERHELPER	0
#endif
#ifdef CONFIG_FW_LOADER_USER_HELPER_FALLBACK
#define FW_OPT_FALLBACK		FW_OPT_USERHELPER
#else
#define FW_OPT_FALLBACK		0
#endif
#define FW_OPT_NO_WARN	(1U << 3)

struct firmware_cache {
	/* firmware_buf instance will be added into the below list */
	spinlock_t lock;
	struct list_head head;
	int state;

#ifdef CONFIG_PM_SLEEP
	/*
	 * Names of firmware images which have been cached successfully
	 * will be added into the below list so that device uncache
	 * helper can trace which firmware images have been cached
	 * before.
	 */
	spinlock_t name_lock;
	struct list_head fw_names;

	struct delayed_work work;

	struct notifier_block   pm_notify;
#endif
};

struct firmware_buf {
	struct kref ref;
	struct list_head list;
	struct completion completion;
	struct firmware_cache *fwc;
	unsigned long status;
	void *data;
	size_t size;
#ifdef CONFIG_FW_LOADER_USER_HELPER
	bool is_paged_buf;
	bool need_uevent;
	struct page **pages;
	int nr_pages;
	int page_array_size;
	struct list_head pending_list;
#endif
	const char *fw_id;
};

struct fw_cache_entry {
	struct list_head list;
	const char *name;
};

struct fw_name_devm {
	unsigned long magic;
	const char *name;
};

#define to_fwbuf(d) container_of(d, struct firmware_buf, ref)

#define	FW_LOADER_NO_CACHE	0
#define	FW_LOADER_START_CACHE	1

static int fw_cache_piggyback_on_request(const char *name);

/* fw_lock could be moved to 'struct firmware_priv' but since it is just
 * guarding for corner cases a global lock should be OK */
static DEFINE_MUTEX(fw_lock);

static struct firmware_cache fw_cache;

static struct firmware_buf *__allocate_fw_buf(const char *fw_name,
					      struct firmware_cache *fwc)
{
	struct firmware_buf *buf;

	buf = kzalloc(sizeof(*buf), GFP_ATOMIC);
	if (!buf)
		return NULL;

	buf->fw_id = kstrdup_const(fw_name, GFP_ATOMIC);
	if (!buf->fw_id) {
		kfree(buf);
		return NULL;
	}

	kref_init(&buf->ref);
	buf->fwc = fwc;
	init_completion(&buf->completion);
#ifdef CONFIG_FW_LOADER_USER_HELPER
	INIT_LIST_HEAD(&buf->pending_list);
#endif

	pr_debug("%s: fw-%s buf=%p\n", __func__, fw_name, buf);

	return buf;
}

static struct firmware_buf *__fw_lookup_buf(const char *fw_name)
{
	struct firmware_buf *tmp;
	struct firmware_cache *fwc = &fw_cache;

	list_for_each_entry(tmp, &fwc->head, list)
		if (!strcmp(tmp->fw_id, fw_name))
			return tmp;
	return NULL;
}

static int fw_lookup_and_allocate_buf(const char *fw_name,
				      struct firmware_cache *fwc,
				      struct firmware_buf **buf)
{
	struct firmware_buf *tmp;

	spin_lock(&fwc->lock);
	tmp = __fw_lookup_buf(fw_name);
	if (tmp) {
		kref_get(&tmp->ref);
		spin_unlock(&fwc->lock);
		*buf = tmp;
		return 1;
	}
	tmp = __allocate_fw_buf(fw_name, fwc);
	if (tmp)
		list_add(&tmp->list, &fwc->head);
	spin_unlock(&fwc->lock);

	*buf = tmp;

	return tmp ? 0 : -ENOMEM;
}

static void __fw_free_buf(struct kref *ref)
	__releases(&fwc->lock)
{
	struct firmware_buf *buf = to_fwbuf(ref);
	struct firmware_cache *fwc = buf->fwc;

	pr_debug("%s: fw-%s buf=%p data=%p size=%u\n",
		 __func__, buf->fw_id, buf, buf->data,
		 (unsigned int)buf->size);

	list_del(&buf->list);
	spin_unlock(&fwc->lock);

#ifdef CONFIG_FW_LOADER_USER_HELPER
	if (buf->is_paged_buf) {
		int i;
		vunmap(buf->data);
		for (i = 0; i < buf->nr_pages; i++)
			__free_page(buf->pages[i]);
		kfree(buf->pages);
	} else
#endif
		vfree(buf->data);
	kfree_const(buf->fw_id);
	kfree(buf);
}

static void fw_free_buf(struct firmware_buf *buf)
{
	struct firmware_cache *fwc = buf->fwc;
	spin_lock(&fwc->lock);
	if (!kref_put(&buf->ref, __fw_free_buf))
		spin_unlock(&fwc->lock);
}

/* direct firmware loading support */
static char fw_path_para[256];
static const char * const fw_path[] = {
	fw_path_para,
	"/lib/firmware/updates/" UTS_RELEASE,
	"/lib/firmware/updates",
	"/lib/firmware/" UTS_RELEASE,
	"/lib/firmware"
};

/*
 * Typical usage is that passing 'firmware_class.path=$CUSTOMIZED_PATH'
 * from kernel command line because firmware_class is generally built in
 * kernel instead of module.
 */
module_param_string(path, fw_path_para, sizeof(fw_path_para), 0644);
MODULE_PARM_DESC(path, "customized firmware image search path with a higher priority than default path");

static int fw_read_file_contents(struct file *file, struct firmware_buf *fw_buf)
{
	int size;
	char *buf;
	int rc;

	if (!S_ISREG(file_inode(file)->i_mode))
		return -EINVAL;
	size = i_size_read(file_inode(file));
	if (size <= 0)
		return -EINVAL;
	buf = vmalloc(size);
	if (!buf)
		return -ENOMEM;
	rc = kernel_read(file, 0, buf, size);
	if (rc != size) {
		if (rc > 0)
			rc = -EIO;
		goto fail;
	}
	rc = security_kernel_fw_from_file(file, buf, size);
	if (rc)
		goto fail;
	fw_buf->data = buf;
	fw_buf->size = size;
	return 0;
fail:
	vfree(buf);
	return rc;
}

static int fw_get_filesystem_firmware(struct device *device,
				       struct firmware_buf *buf)
{
	int i, len;
	int rc = -ENOENT;
	char *path;

	path = __getname();
	if (!path)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(fw_path); i++) {
		struct file *file;

		/* skip the unset customized path */
		if (!fw_path[i][0])
			continue;

		len = snprintf(path, PATH_MAX, "%s/%s",
			       fw_path[i], buf->fw_id);
		if (len >= PATH_MAX) {
			rc = -ENAMETOOLONG;
			break;
		}

		file = filp_open(path, O_RDONLY, 0);
		if (IS_ERR(file))
			continue;
		rc = fw_read_file_contents(file, buf);
		fput(file);
		if (rc)
			dev_warn(device, "firmware, attempted to load %s, but failed with error %d\n",
				path, rc);
		else
			break;
	}
	__putname(path);

	if (!rc) {
		dev_dbg(device, "firmware: direct-loading firmware %s\n",
			buf->fw_id);
		mutex_lock(&fw_lock);
		set_bit(FW_STATUS_DONE, &buf->status);
		complete_all(&buf->completion);
		mutex_unlock(&fw_lock);
	}

	return rc;
}

/* firmware holds the ownership of pages */
static void firmware_free_data(const struct firmware *fw)
{
	/* Loaded directly? */
	if (!fw->priv) {
		vfree(fw->data);
		return;
	}
	fw_free_buf(fw->priv);
}

/* store the pages buffer info firmware from buf */
static void fw_set_page_data(struct firmware_buf *buf, struct firmware *fw)
{
	fw->priv = buf;
#ifdef CONFIG_FW_LOADER_USER_HELPER
	fw->pages = buf->pages;
#endif
	fw->size = buf->size;
	fw->data = buf->data;

	pr_debug("%s: fw-%s buf=%p data=%p size=%u\n",
		 __func__, buf->fw_id, buf, buf->data,
		 (unsigned int)buf->size);
}

#ifdef CONFIG_PM_SLEEP
static void fw_name_devm_release(struct device *dev, void *res)
{
	struct fw_name_devm *fwn = res;

	if (fwn->magic == (unsigned long)&fw_cache)
		pr_debug("%s: fw_name-%s devm-%p released\n",
				__func__, fwn->name, res);
	kfree_const(fwn->name);
}

static int fw_devm_match(struct device *dev, void *res,
		void *match_data)
{
	struct fw_name_devm *fwn = res;

	return (fwn->magic == (unsigned long)&fw_cache) &&
		!strcmp(fwn->name, match_data);
}

static struct fw_name_devm *fw_find_devm_name(struct device *dev,
		const char *name)
{
	struct fw_name_devm *fwn;

	fwn = devres_find(dev, fw_name_devm_release,
			  fw_devm_match, (void *)name);
	return fwn;
}

/* add firmware name into devres list */
static int fw_add_devm_name(struct device *dev, const char *name)
{
	struct fw_name_devm *fwn;

	fwn = fw_find_devm_name(dev, name);
	if (fwn)
		return 1;

	fwn = devres_alloc(fw_name_devm_release, sizeof(struct fw_name_devm),
			   GFP_KERNEL);
	if (!fwn)
		return -ENOMEM;
	fwn->name = kstrdup_const(name, GFP_KERNEL);
	if (!fwn->name) {
		kfree(fwn);
		return -ENOMEM;
	}

	fwn->magic = (unsigned long)&fw_cache;
	devres_add(dev, fwn);

	return 0;
}
#else
static int fw_add_devm_name(struct device *dev, const char *name)
{
	return 0;
}
#endif


/*
 * user-mode helper code
 */
#ifdef CONFIG_FW_LOADER_USER_HELPER
struct firmware_priv {
	bool nowait;
	struct device dev;
	struct firmware_buf *buf;
	struct firmware *fw;
};

static struct firmware_priv *to_firmware_priv(struct device *dev)
{
	return container_of(dev, struct firmware_priv, dev);
}

static void __fw_load_abort(struct firmware_buf *buf)
{
	/*
	 * There is a small window in which user can write to 'loading'
	 * between loading done and disappearance of 'loading'
	 */
	if (test_bit(FW_STATUS_DONE, &buf->status))
		return;

	list_del_init(&buf->pending_list);
	set_bit(FW_STATUS_ABORT, &buf->status);
	complete_all(&buf->completion);
}

static void fw_load_abort(struct firmware_priv *fw_priv)
{
	struct firmware_buf *buf = fw_priv->buf;

	__fw_load_abort(buf);

	/* avoid user action after loading abort */
	fw_priv->buf = NULL;
}

#define is_fw_load_aborted(buf)	\
	test_bit(FW_STATUS_ABORT, &(buf)->status)

static LIST_HEAD(pending_fw_head);

/* reboot notifier for avoid deadlock with usermode_lock */
static int fw_shutdown_notify(struct notifier_block *unused1,
			      unsigned long unused2, void *unused3)
{
	mutex_lock(&fw_lock);
	while (!list_empty(&pending_fw_head))
		__fw_load_abort(list_first_entry(&pending_fw_head,
					       struct firmware_buf,
					       pending_list));
	mutex_unlock(&fw_lock);
	return NOTIFY_DONE;
}

static struct notifier_block fw_shutdown_nb = {
	.notifier_call = fw_shutdown_notify,
};

static ssize_t timeout_show(struct class *class, struct class_attribute *attr,
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
static ssize_t timeout_store(struct class *class, struct class_attribute *attr,
			     const char *buf, size_t count)
{
	loading_timeout = simple_strtol(buf, NULL, 10);
	if (loading_timeout < 0)
		loading_timeout = 0;

	return count;
}

static struct class_attribute firmware_class_attrs[] = {
	__ATTR_RW(timeout),
	__ATTR_NULL
};

static void fw_dev_release(struct device *dev)
{
	struct firmware_priv *fw_priv = to_firmware_priv(dev);

	kfree(fw_priv);
}

static int do_firmware_uevent(struct firmware_priv *fw_priv, struct kobj_uevent_env *env)
{
	if (add_uevent_var(env, "FIRMWARE=%s", fw_priv->buf->fw_id))
		return -ENOMEM;
	if (add_uevent_var(env, "TIMEOUT=%i", loading_timeout))
		return -ENOMEM;
	if (add_uevent_var(env, "ASYNC=%d", fw_priv->nowait))
		return -ENOMEM;

	return 0;
}

static int firmware_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct firmware_priv *fw_priv = to_firmware_priv(dev);
	int err = 0;

	mutex_lock(&fw_lock);
	if (fw_priv->buf)
		err = do_firmware_uevent(fw_priv, env);
	mutex_unlock(&fw_lock);
	return err;
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
	int loading = 0;

	mutex_lock(&fw_lock);
	if (fw_priv->buf)
		loading = test_bit(FW_STATUS_LOADING, &fw_priv->buf->status);
	mutex_unlock(&fw_lock);

	return sprintf(buf, "%d\n", loading);
}

/* Some architectures don't have PAGE_KERNEL_RO */
#ifndef PAGE_KERNEL_RO
#define PAGE_KERNEL_RO PAGE_KERNEL
#endif

/* one pages buffer should be mapped/unmapped only once */
static int fw_map_pages_buf(struct firmware_buf *buf)
{
	if (!buf->is_paged_buf)
		return 0;

	vunmap(buf->data);
	buf->data = vmap(buf->pages, buf->nr_pages, 0, PAGE_KERNEL_RO);
	if (!buf->data)
		return -ENOMEM;
	return 0;
}

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
	struct firmware_buf *fw_buf;
	ssize_t written = count;
	int loading = simple_strtol(buf, NULL, 10);
	int i;

	mutex_lock(&fw_lock);
	fw_buf = fw_priv->buf;
	if (!fw_buf)
		goto out;

	switch (loading) {
	case 1:
		/* discarding any previous partial load */
		if (!test_bit(FW_STATUS_DONE, &fw_buf->status)) {
			for (i = 0; i < fw_buf->nr_pages; i++)
				__free_page(fw_buf->pages[i]);
			kfree(fw_buf->pages);
			fw_buf->pages = NULL;
			fw_buf->page_array_size = 0;
			fw_buf->nr_pages = 0;
			set_bit(FW_STATUS_LOADING, &fw_buf->status);
		}
		break;
	case 0:
		if (test_bit(FW_STATUS_LOADING, &fw_buf->status)) {
			int rc;

			set_bit(FW_STATUS_DONE, &fw_buf->status);
			clear_bit(FW_STATUS_LOADING, &fw_buf->status);

			/*
			 * Several loading requests may be pending on
			 * one same firmware buf, so let all requests
			 * see the mapped 'buf->data' once the loading
			 * is completed.
			 * */
			rc = fw_map_pages_buf(fw_buf);
			if (rc)
				dev_err(dev, "%s: map pages failed\n",
					__func__);
			else
				rc = security_kernel_fw_from_file(NULL,
						fw_buf->data, fw_buf->size);

			/*
			 * Same logic as fw_load_abort, only the DONE bit
			 * is ignored and we set ABORT only on failure.
			 */
			list_del_init(&fw_buf->pending_list);
			if (rc) {
				set_bit(FW_STATUS_ABORT, &fw_buf->status);
				written = rc;
			}
			complete_all(&fw_buf->completion);
			break;
		}
		/* fallthrough */
	default:
		dev_err(dev, "%s: unexpected value (%d)\n", __func__, loading);
		/* fallthrough */
	case -1:
		fw_load_abort(fw_priv);
		break;
	}
out:
	mutex_unlock(&fw_lock);
	return written;
}

static DEVICE_ATTR(loading, 0644, firmware_loading_show, firmware_loading_store);

static ssize_t firmware_data_read(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buffer, loff_t offset, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct firmware_priv *fw_priv = to_firmware_priv(dev);
	struct firmware_buf *buf;
	ssize_t ret_count;

	mutex_lock(&fw_lock);
	buf = fw_priv->buf;
	if (!buf || test_bit(FW_STATUS_DONE, &buf->status)) {
		ret_count = -ENODEV;
		goto out;
	}
	if (offset > buf->size) {
		ret_count = 0;
		goto out;
	}
	if (count > buf->size - offset)
		count = buf->size - offset;

	ret_count = count;

	while (count) {
		void *page_data;
		int page_nr = offset >> PAGE_SHIFT;
		int page_ofs = offset & (PAGE_SIZE-1);
		int page_cnt = min_t(size_t, PAGE_SIZE - page_ofs, count);

		page_data = kmap(buf->pages[page_nr]);

		memcpy(buffer, page_data + page_ofs, page_cnt);

		kunmap(buf->pages[page_nr]);
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
	struct firmware_buf *buf = fw_priv->buf;
	int pages_needed = PAGE_ALIGN(min_size) >> PAGE_SHIFT;

	/* If the array of pages is too small, grow it... */
	if (buf->page_array_size < pages_needed) {
		int new_array_size = max(pages_needed,
					 buf->page_array_size * 2);
		struct page **new_pages;

		new_pages = kmalloc(new_array_size * sizeof(void *),
				    GFP_KERNEL);
		if (!new_pages) {
			fw_load_abort(fw_priv);
			return -ENOMEM;
		}
		memcpy(new_pages, buf->pages,
		       buf->page_array_size * sizeof(void *));
		memset(&new_pages[buf->page_array_size], 0, sizeof(void *) *
		       (new_array_size - buf->page_array_size));
		kfree(buf->pages);
		buf->pages = new_pages;
		buf->page_array_size = new_array_size;
	}

	while (buf->nr_pages < pages_needed) {
		buf->pages[buf->nr_pages] =
			alloc_page(GFP_KERNEL | __GFP_HIGHMEM);

		if (!buf->pages[buf->nr_pages]) {
			fw_load_abort(fw_priv);
			return -ENOMEM;
		}
		buf->nr_pages++;
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
	struct firmware_buf *buf;
	ssize_t retval;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	mutex_lock(&fw_lock);
	buf = fw_priv->buf;
	if (!buf || test_bit(FW_STATUS_DONE, &buf->status)) {
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

		page_data = kmap(buf->pages[page_nr]);

		memcpy(page_data + page_ofs, buffer, page_cnt);

		kunmap(buf->pages[page_nr]);
		buffer += page_cnt;
		offset += page_cnt;
		count -= page_cnt;
	}

	buf->size = max_t(size_t, offset, buf->size);
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

static struct firmware_priv *
fw_create_instance(struct firmware *firmware, const char *fw_name,
		   struct device *device, unsigned int opt_flags)
{
	struct firmware_priv *fw_priv;
	struct device *f_dev;

	fw_priv = kzalloc(sizeof(*fw_priv), GFP_KERNEL);
	if (!fw_priv) {
		fw_priv = ERR_PTR(-ENOMEM);
		goto exit;
	}

	fw_priv->nowait = !!(opt_flags & FW_OPT_NOWAIT);
	fw_priv->fw = firmware;
	f_dev = &fw_priv->dev;

	device_initialize(f_dev);
	dev_set_name(f_dev, "%s", fw_name);
	f_dev->parent = device;
	f_dev->class = &firmware_class;
	f_dev->groups = fw_dev_attr_groups;
exit:
	return fw_priv;
}

/* load a firmware via user helper */
static int _request_firmware_load(struct firmware_priv *fw_priv,
				  unsigned int opt_flags, long timeout)
{
	int retval = 0;
	struct device *f_dev = &fw_priv->dev;
	struct firmware_buf *buf = fw_priv->buf;

	/* fall back on userspace loading */
	buf->is_paged_buf = true;

	dev_set_uevent_suppress(f_dev, true);

	retval = device_add(f_dev);
	if (retval) {
		dev_err(f_dev, "%s: device_register failed\n", __func__);
		goto err_put_dev;
	}

	mutex_lock(&fw_lock);
	list_add(&buf->pending_list, &pending_fw_head);
	mutex_unlock(&fw_lock);

	if (opt_flags & FW_OPT_UEVENT) {
		buf->need_uevent = true;
		dev_set_uevent_suppress(f_dev, false);
		dev_dbg(f_dev, "firmware: requesting %s\n", buf->fw_id);
		kobject_uevent(&fw_priv->dev.kobj, KOBJ_ADD);
	} else {
		timeout = MAX_JIFFY_OFFSET;
	}

	retval = wait_for_completion_interruptible_timeout(&buf->completion,
			timeout);
	if (retval == -ERESTARTSYS || !retval) {
		mutex_lock(&fw_lock);
		fw_load_abort(fw_priv);
		mutex_unlock(&fw_lock);
	} else if (retval > 0) {
		retval = 0;
	}

	if (is_fw_load_aborted(buf))
		retval = -EAGAIN;
	else if (!buf->data)
		retval = -ENOMEM;

	device_del(f_dev);
err_put_dev:
	put_device(f_dev);
	return retval;
}

static int fw_load_from_user_helper(struct firmware *firmware,
				    const char *name, struct device *device,
				    unsigned int opt_flags, long timeout)
{
	struct firmware_priv *fw_priv;

	fw_priv = fw_create_instance(firmware, name, device, opt_flags);
	if (IS_ERR(fw_priv))
		return PTR_ERR(fw_priv);

	fw_priv->buf = firmware->priv;
	return _request_firmware_load(fw_priv, opt_flags, timeout);
}

#ifdef CONFIG_PM_SLEEP
/* kill pending requests without uevent to avoid blocking suspend */
static void kill_requests_without_uevent(void)
{
	struct firmware_buf *buf;
	struct firmware_buf *next;

	mutex_lock(&fw_lock);
	list_for_each_entry_safe(buf, next, &pending_fw_head, pending_list) {
		if (!buf->need_uevent)
			 __fw_load_abort(buf);
	}
	mutex_unlock(&fw_lock);
}
#endif

#else /* CONFIG_FW_LOADER_USER_HELPER */
static inline int
fw_load_from_user_helper(struct firmware *firmware, const char *name,
			 struct device *device, unsigned int opt_flags,
			 long timeout)
{
	return -ENOENT;
}

/* No abort during direct loading */
#define is_fw_load_aborted(buf) false

#ifdef CONFIG_PM_SLEEP
static inline void kill_requests_without_uevent(void) { }
#endif

#endif /* CONFIG_FW_LOADER_USER_HELPER */


/* wait until the shared firmware_buf becomes ready (or error) */
static int sync_cached_firmware_buf(struct firmware_buf *buf)
{
	int ret = 0;

	mutex_lock(&fw_lock);
	while (!test_bit(FW_STATUS_DONE, &buf->status)) {
		if (is_fw_load_aborted(buf)) {
			ret = -ENOENT;
			break;
		}
		mutex_unlock(&fw_lock);
		ret = wait_for_completion_interruptible(&buf->completion);
		mutex_lock(&fw_lock);
	}
	mutex_unlock(&fw_lock);
	return ret;
}

/* prepare firmware and firmware_buf structs;
 * return 0 if a firmware is already assigned, 1 if need to load one,
 * or a negative error code
 */
static int
_request_firmware_prepare(struct firmware **firmware_p, const char *name,
			  struct device *device)
{
	struct firmware *firmware;
	struct firmware_buf *buf;
	int ret;

	*firmware_p = firmware = kzalloc(sizeof(*firmware), GFP_KERNEL);
	if (!firmware) {
		dev_err(device, "%s: kmalloc(struct firmware) failed\n",
			__func__);
		return -ENOMEM;
	}

	if (fw_get_builtin_firmware(firmware, name)) {
		dev_dbg(device, "firmware: using built-in firmware %s\n", name);
		return 0; /* assigned */
	}

	ret = fw_lookup_and_allocate_buf(name, &fw_cache, &buf);

	/*
	 * bind with 'buf' now to avoid warning in failure path
	 * of requesting firmware.
	 */
	firmware->priv = buf;

	if (ret > 0) {
		ret = sync_cached_firmware_buf(buf);
		if (!ret) {
			fw_set_page_data(buf, firmware);
			return 0; /* assigned */
		}
	}

	if (ret < 0)
		return ret;
	return 1; /* need to load */
}

static int assign_firmware_buf(struct firmware *fw, struct device *device,
			       unsigned int opt_flags)
{
	struct firmware_buf *buf = fw->priv;

	mutex_lock(&fw_lock);
	if (!buf->size || is_fw_load_aborted(buf)) {
		mutex_unlock(&fw_lock);
		return -ENOENT;
	}

	/*
	 * add firmware name into devres list so that we can auto cache
	 * and uncache firmware for device.
	 *
	 * device may has been deleted already, but the problem
	 * should be fixed in devres or driver core.
	 */
	/* don't cache firmware handled without uevent */
	if (device && (opt_flags & FW_OPT_UEVENT))
		fw_add_devm_name(device, buf->fw_id);

	/*
	 * After caching firmware image is started, let it piggyback
	 * on request firmware.
	 */
	if (buf->fwc->state == FW_LOADER_START_CACHE) {
		if (fw_cache_piggyback_on_request(buf->fw_id))
			kref_get(&buf->ref);
	}

	/* pass the pages buffer to driver at the last minute */
	fw_set_page_data(buf, fw);
	mutex_unlock(&fw_lock);
	return 0;
}

/* called from request_firmware() and request_firmware_work_func() */
static int
_request_firmware(const struct firmware **firmware_p, const char *name,
		  struct device *device, unsigned int opt_flags)
{
	struct firmware *fw;
	long timeout;
	int ret;

	if (!firmware_p)
		return -EINVAL;

	if (!name || name[0] == '\0')
		return -EINVAL;

	ret = _request_firmware_prepare(&fw, name, device);
	if (ret <= 0) /* error or already assigned */
		goto out;

	ret = 0;
	timeout = firmware_loading_timeout();
	if (opt_flags & FW_OPT_NOWAIT) {
		timeout = usermodehelper_read_lock_wait(timeout);
		if (!timeout) {
			dev_dbg(device, "firmware: %s loading timed out\n",
				name);
			ret = -EBUSY;
			goto out;
		}
	} else {
		ret = usermodehelper_read_trylock();
		if (WARN_ON(ret)) {
			dev_err(device, "firmware: %s will not be loaded\n",
				name);
			goto out;
		}
	}

	ret = fw_get_filesystem_firmware(device, fw->priv);
	if (ret) {
		if (!(opt_flags & FW_OPT_NO_WARN))
			dev_warn(device,
				 "Direct firmware load for %s failed with error %d\n",
				 name, ret);
		if (opt_flags & FW_OPT_USERHELPER) {
			dev_warn(device, "Falling back to user helper\n");
			ret = fw_load_from_user_helper(fw, name, device,
						       opt_flags, timeout);
		}
	}

	if (!ret)
		ret = assign_firmware_buf(fw, device, opt_flags);

	usermodehelper_read_unlock();

 out:
	if (ret < 0) {
		release_firmware(fw);
		fw = NULL;
	}

	*firmware_p = fw;
	return ret;
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
 *
 *	Caller must hold the reference count of @device.
 *
 *	The function can be called safely inside device's suspend and
 *	resume callback.
 **/
int
request_firmware(const struct firmware **firmware_p, const char *name,
		 struct device *device)
{
	int ret;

	/* Need to pin this module until return */
	__module_get(THIS_MODULE);
	ret = _request_firmware(firmware_p, name, device,
				FW_OPT_UEVENT | FW_OPT_FALLBACK);
	module_put(THIS_MODULE);
	return ret;
}
EXPORT_SYMBOL(request_firmware);

/**
 * request_firmware_direct: - load firmware directly without usermode helper
 * @firmware_p: pointer to firmware image
 * @name: name of firmware file
 * @device: device for which firmware is being loaded
 *
 * This function works pretty much like request_firmware(), but this doesn't
 * fall back to usermode helper even if the firmware couldn't be loaded
 * directly from fs.  Hence it's useful for loading optional firmwares, which
 * aren't always present, without extra long timeouts of udev.
 **/
int request_firmware_direct(const struct firmware **firmware_p,
			    const char *name, struct device *device)
{
	int ret;

	__module_get(THIS_MODULE);
	ret = _request_firmware(firmware_p, name, device,
				FW_OPT_UEVENT | FW_OPT_NO_WARN);
	module_put(THIS_MODULE);
	return ret;
}
EXPORT_SYMBOL_GPL(request_firmware_direct);

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
EXPORT_SYMBOL(release_firmware);

/* Async support */
struct firmware_work {
	struct work_struct work;
	struct module *module;
	const char *name;
	struct device *device;
	void *context;
	void (*cont)(const struct firmware *fw, void *context);
	unsigned int opt_flags;
};

static void request_firmware_work_func(struct work_struct *work)
{
	struct firmware_work *fw_work;
	const struct firmware *fw;

	fw_work = container_of(work, struct firmware_work, work);

	_request_firmware(&fw, fw_work->name, fw_work->device,
			  fw_work->opt_flags);
	fw_work->cont(fw, fw_work->context);
	put_device(fw_work->device); /* taken in request_firmware_nowait() */

	module_put(fw_work->module);
	kfree_const(fw_work->name);
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
 *	Caller must hold the reference count of @device.
 *
 *	Asynchronous variant of request_firmware() for user contexts:
 *		- sleep for as small periods as possible since it may
 *		increase kernel boot time of built-in device drivers
 *		requesting firmware in their ->probe() methods, if
 *		@gfp is GFP_KERNEL.
 *
 *		- can't sleep at all if @gfp is GFP_ATOMIC.
 **/
int
request_firmware_nowait(
	struct module *module, bool uevent,
	const char *name, struct device *device, gfp_t gfp, void *context,
	void (*cont)(const struct firmware *fw, void *context))
{
	struct firmware_work *fw_work;

	fw_work = kzalloc(sizeof(struct firmware_work), gfp);
	if (!fw_work)
		return -ENOMEM;

	fw_work->module = module;
	fw_work->name = kstrdup_const(name, gfp);
	if (!fw_work->name) {
		kfree(fw_work);
		return -ENOMEM;
	}
	fw_work->device = device;
	fw_work->context = context;
	fw_work->cont = cont;
	fw_work->opt_flags = FW_OPT_NOWAIT | FW_OPT_FALLBACK |
		(uevent ? FW_OPT_UEVENT : FW_OPT_USERHELPER);

	if (!try_module_get(module)) {
		kfree_const(fw_work->name);
		kfree(fw_work);
		return -EFAULT;
	}

	get_device(fw_work->device);
	INIT_WORK(&fw_work->work, request_firmware_work_func);
	schedule_work(&fw_work->work);
	return 0;
}
EXPORT_SYMBOL(request_firmware_nowait);

#ifdef CONFIG_PM_SLEEP
static ASYNC_DOMAIN_EXCLUSIVE(fw_cache_domain);

/**
 * cache_firmware - cache one firmware image in kernel memory space
 * @fw_name: the firmware image name
 *
 * Cache firmware in kernel memory so that drivers can use it when
 * system isn't ready for them to request firmware image from userspace.
 * Once it returns successfully, driver can use request_firmware or its
 * nowait version to get the cached firmware without any interacting
 * with userspace
 *
 * Return 0 if the firmware image has been cached successfully
 * Return !0 otherwise
 *
 */
static int cache_firmware(const char *fw_name)
{
	int ret;
	const struct firmware *fw;

	pr_debug("%s: %s\n", __func__, fw_name);

	ret = request_firmware(&fw, fw_name, NULL);
	if (!ret)
		kfree(fw);

	pr_debug("%s: %s ret=%d\n", __func__, fw_name, ret);

	return ret;
}

static struct firmware_buf *fw_lookup_buf(const char *fw_name)
{
	struct firmware_buf *tmp;
	struct firmware_cache *fwc = &fw_cache;

	spin_lock(&fwc->lock);
	tmp = __fw_lookup_buf(fw_name);
	spin_unlock(&fwc->lock);

	return tmp;
}

/**
 * uncache_firmware - remove one cached firmware image
 * @fw_name: the firmware image name
 *
 * Uncache one firmware image which has been cached successfully
 * before.
 *
 * Return 0 if the firmware cache has been removed successfully
 * Return !0 otherwise
 *
 */
static int uncache_firmware(const char *fw_name)
{
	struct firmware_buf *buf;
	struct firmware fw;

	pr_debug("%s: %s\n", __func__, fw_name);

	if (fw_get_builtin_firmware(&fw, fw_name))
		return 0;

	buf = fw_lookup_buf(fw_name);
	if (buf) {
		fw_free_buf(buf);
		return 0;
	}

	return -EINVAL;
}

static struct fw_cache_entry *alloc_fw_cache_entry(const char *name)
{
	struct fw_cache_entry *fce;

	fce = kzalloc(sizeof(*fce), GFP_ATOMIC);
	if (!fce)
		goto exit;

	fce->name = kstrdup_const(name, GFP_ATOMIC);
	if (!fce->name) {
		kfree(fce);
		fce = NULL;
		goto exit;
	}
exit:
	return fce;
}

static int __fw_entry_found(const char *name)
{
	struct firmware_cache *fwc = &fw_cache;
	struct fw_cache_entry *fce;

	list_for_each_entry(fce, &fwc->fw_names, list) {
		if (!strcmp(fce->name, name))
			return 1;
	}
	return 0;
}

static int fw_cache_piggyback_on_request(const char *name)
{
	struct firmware_cache *fwc = &fw_cache;
	struct fw_cache_entry *fce;
	int ret = 0;

	spin_lock(&fwc->name_lock);
	if (__fw_entry_found(name))
		goto found;

	fce = alloc_fw_cache_entry(name);
	if (fce) {
		ret = 1;
		list_add(&fce->list, &fwc->fw_names);
		pr_debug("%s: fw: %s\n", __func__, name);
	}
found:
	spin_unlock(&fwc->name_lock);
	return ret;
}

static void free_fw_cache_entry(struct fw_cache_entry *fce)
{
	kfree_const(fce->name);
	kfree(fce);
}

static void __async_dev_cache_fw_image(void *fw_entry,
				       async_cookie_t cookie)
{
	struct fw_cache_entry *fce = fw_entry;
	struct firmware_cache *fwc = &fw_cache;
	int ret;

	ret = cache_firmware(fce->name);
	if (ret) {
		spin_lock(&fwc->name_lock);
		list_del(&fce->list);
		spin_unlock(&fwc->name_lock);

		free_fw_cache_entry(fce);
	}
}

/* called with dev->devres_lock held */
static void dev_create_fw_entry(struct device *dev, void *res,
				void *data)
{
	struct fw_name_devm *fwn = res;
	const char *fw_name = fwn->name;
	struct list_head *head = data;
	struct fw_cache_entry *fce;

	fce = alloc_fw_cache_entry(fw_name);
	if (fce)
		list_add(&fce->list, head);
}

static int devm_name_match(struct device *dev, void *res,
			   void *match_data)
{
	struct fw_name_devm *fwn = res;
	return (fwn->magic == (unsigned long)match_data);
}

static void dev_cache_fw_image(struct device *dev, void *data)
{
	LIST_HEAD(todo);
	struct fw_cache_entry *fce;
	struct fw_cache_entry *fce_next;
	struct firmware_cache *fwc = &fw_cache;

	devres_for_each_res(dev, fw_name_devm_release,
			    devm_name_match, &fw_cache,
			    dev_create_fw_entry, &todo);

	list_for_each_entry_safe(fce, fce_next, &todo, list) {
		list_del(&fce->list);

		spin_lock(&fwc->name_lock);
		/* only one cache entry for one firmware */
		if (!__fw_entry_found(fce->name)) {
			list_add(&fce->list, &fwc->fw_names);
		} else {
			free_fw_cache_entry(fce);
			fce = NULL;
		}
		spin_unlock(&fwc->name_lock);

		if (fce)
			async_schedule_domain(__async_dev_cache_fw_image,
					      (void *)fce,
					      &fw_cache_domain);
	}
}

static void __device_uncache_fw_images(void)
{
	struct firmware_cache *fwc = &fw_cache;
	struct fw_cache_entry *fce;

	spin_lock(&fwc->name_lock);
	while (!list_empty(&fwc->fw_names)) {
		fce = list_entry(fwc->fw_names.next,
				struct fw_cache_entry, list);
		list_del(&fce->list);
		spin_unlock(&fwc->name_lock);

		uncache_firmware(fce->name);
		free_fw_cache_entry(fce);

		spin_lock(&fwc->name_lock);
	}
	spin_unlock(&fwc->name_lock);
}

/**
 * device_cache_fw_images - cache devices' firmware
 *
 * If one device called request_firmware or its nowait version
 * successfully before, the firmware names are recored into the
 * device's devres link list, so device_cache_fw_images can call
 * cache_firmware() to cache these firmwares for the device,
 * then the device driver can load its firmwares easily at
 * time when system is not ready to complete loading firmware.
 */
static void device_cache_fw_images(void)
{
	struct firmware_cache *fwc = &fw_cache;
	int old_timeout;
	DEFINE_WAIT(wait);

	pr_debug("%s\n", __func__);

	/* cancel uncache work */
	cancel_delayed_work_sync(&fwc->work);

	/*
	 * use small loading timeout for caching devices' firmware
	 * because all these firmware images have been loaded
	 * successfully at lease once, also system is ready for
	 * completing firmware loading now. The maximum size of
	 * firmware in current distributions is about 2M bytes,
	 * so 10 secs should be enough.
	 */
	old_timeout = loading_timeout;
	loading_timeout = 10;

	mutex_lock(&fw_lock);
	fwc->state = FW_LOADER_START_CACHE;
	dpm_for_each_dev(NULL, dev_cache_fw_image);
	mutex_unlock(&fw_lock);

	/* wait for completion of caching firmware for all devices */
	async_synchronize_full_domain(&fw_cache_domain);

	loading_timeout = old_timeout;
}

/**
 * device_uncache_fw_images - uncache devices' firmware
 *
 * uncache all firmwares which have been cached successfully
 * by device_uncache_fw_images earlier
 */
static void device_uncache_fw_images(void)
{
	pr_debug("%s\n", __func__);
	__device_uncache_fw_images();
}

static void device_uncache_fw_images_work(struct work_struct *work)
{
	device_uncache_fw_images();
}

/**
 * device_uncache_fw_images_delay - uncache devices firmwares
 * @delay: number of milliseconds to delay uncache device firmwares
 *
 * uncache all devices's firmwares which has been cached successfully
 * by device_cache_fw_images after @delay milliseconds.
 */
static void device_uncache_fw_images_delay(unsigned long delay)
{
	queue_delayed_work(system_power_efficient_wq, &fw_cache.work,
			   msecs_to_jiffies(delay));
}

static int fw_pm_notify(struct notifier_block *notify_block,
			unsigned long mode, void *unused)
{
	switch (mode) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
	case PM_RESTORE_PREPARE:
		kill_requests_without_uevent();
		device_cache_fw_images();
		break;

	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
		/*
		 * In case that system sleep failed and syscore_suspend is
		 * not called.
		 */
		mutex_lock(&fw_lock);
		fw_cache.state = FW_LOADER_NO_CACHE;
		mutex_unlock(&fw_lock);

		device_uncache_fw_images_delay(10 * MSEC_PER_SEC);
		break;
	}

	return 0;
}

/* stop caching firmware once syscore_suspend is reached */
static int fw_suspend(void)
{
	fw_cache.state = FW_LOADER_NO_CACHE;
	return 0;
}

static struct syscore_ops fw_syscore_ops = {
	.suspend = fw_suspend,
};
#else
static int fw_cache_piggyback_on_request(const char *name)
{
	return 0;
}
#endif

static void __init fw_cache_init(void)
{
	spin_lock_init(&fw_cache.lock);
	INIT_LIST_HEAD(&fw_cache.head);
	fw_cache.state = FW_LOADER_NO_CACHE;

#ifdef CONFIG_PM_SLEEP
	spin_lock_init(&fw_cache.name_lock);
	INIT_LIST_HEAD(&fw_cache.fw_names);

	INIT_DELAYED_WORK(&fw_cache.work,
			  device_uncache_fw_images_work);

	fw_cache.pm_notify.notifier_call = fw_pm_notify;
	register_pm_notifier(&fw_cache.pm_notify);

	register_syscore_ops(&fw_syscore_ops);
#endif
}

static int __init firmware_class_init(void)
{
	fw_cache_init();
#ifdef CONFIG_FW_LOADER_USER_HELPER
	register_reboot_notifier(&fw_shutdown_nb);
	return class_register(&firmware_class);
#else
	return 0;
#endif
}

static void __exit firmware_class_exit(void)
{
#ifdef CONFIG_PM_SLEEP
	unregister_syscore_ops(&fw_syscore_ops);
	unregister_pm_notifier(&fw_cache.pm_notify);
#endif
#ifdef CONFIG_FW_LOADER_USER_HELPER
	unregister_reboot_notifier(&fw_shutdown_nb);
	class_unregister(&firmware_class);
#endif
}

fs_initcall(firmware_class_init);
module_exit(firmware_class_exit);
