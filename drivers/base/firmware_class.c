/*
 * firmware_class.c - Multi purpose firmware loading support
 *
 * Copyright (c) 2003 Manuel Estrada Sainz
 *
 * Please see Documentation/firmware_class/ for more information.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/fs.h>
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

enum fw_status {
	FW_STATUS_UNKNOWN,
	FW_STATUS_LOADING,
	FW_STATUS_DONE,
	FW_STATUS_ABORTED,
};

/*
 * Concurrent request_firmware() for the same firmware need to be
 * serialized.  struct fw_state is simple state machine which hold the
 * state of the firmware loading.
 */
struct fw_state {
	struct completion completion;
	enum fw_status status;
};

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
#define FW_OPT_NOCACHE	(1U << 4)

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

struct fw_priv {
	struct kref ref;
	struct list_head list;
	struct firmware_cache *fwc;
	struct fw_state fw_st;
	void *data;
	size_t size;
	size_t allocated_size;
#ifdef CONFIG_FW_LOADER_USER_HELPER
	bool is_paged_buf;
	bool need_uevent;
	struct page **pages;
	int nr_pages;
	int page_array_size;
	struct list_head pending_list;
#endif
	const char *fw_name;
};

struct fw_cache_entry {
	struct list_head list;
	const char *name;
};

struct fw_name_devm {
	unsigned long magic;
	const char *name;
};

#define to_fw_priv(d) container_of(d, struct fw_priv, ref)

#define	FW_LOADER_NO_CACHE	0
#define	FW_LOADER_START_CACHE	1

/* fw_lock could be moved to 'struct fw_sysfs' but since it is just
 * guarding for corner cases a global lock should be OK */
static DEFINE_MUTEX(fw_lock);

static struct firmware_cache fw_cache;

/* Builtin firmware support */

#ifdef CONFIG_FW_LOADER

extern struct builtin_fw __start_builtin_fw[];
extern struct builtin_fw __end_builtin_fw[];

static bool fw_get_builtin_firmware(struct firmware *fw, const char *name,
				    void *buf, size_t size)
{
	struct builtin_fw *b_fw;

	for (b_fw = __start_builtin_fw; b_fw != __end_builtin_fw; b_fw++) {
		if (strcmp(name, b_fw->name) == 0) {
			fw->size = b_fw->size;
			fw->data = b_fw->data;

			if (buf && fw->size <= size)
				memcpy(buf, fw->data, fw->size);
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

static inline bool fw_get_builtin_firmware(struct firmware *fw,
					   const char *name, void *buf,
					   size_t size)
{
	return false;
}

static inline bool fw_is_builtin_firmware(const struct firmware *fw)
{
	return false;
}
#endif

static int loading_timeout = 60;	/* In seconds */

static inline long firmware_loading_timeout(void)
{
	return loading_timeout > 0 ? loading_timeout * HZ : MAX_JIFFY_OFFSET;
}

static void fw_state_init(struct fw_priv *fw_priv)
{
	struct fw_state *fw_st = &fw_priv->fw_st;

	init_completion(&fw_st->completion);
	fw_st->status = FW_STATUS_UNKNOWN;
}

static int __fw_state_wait_common(struct fw_priv *fw_priv, long timeout)
{
	struct fw_state *fw_st = &fw_priv->fw_st;
	long ret;

	ret = wait_for_completion_killable_timeout(&fw_st->completion, timeout);
	if (ret != 0 && fw_st->status == FW_STATUS_ABORTED)
		return -ENOENT;
	if (!ret)
		return -ETIMEDOUT;

	return ret < 0 ? ret : 0;
}

static void __fw_state_set(struct fw_priv *fw_priv,
			   enum fw_status status)
{
	struct fw_state *fw_st = &fw_priv->fw_st;

	WRITE_ONCE(fw_st->status, status);

	if (status == FW_STATUS_DONE || status == FW_STATUS_ABORTED)
		complete_all(&fw_st->completion);
}

static inline void fw_state_start(struct fw_priv *fw_priv)
{
	__fw_state_set(fw_priv, FW_STATUS_LOADING);
}

static inline void fw_state_done(struct fw_priv *fw_priv)
{
	__fw_state_set(fw_priv, FW_STATUS_DONE);
}

static inline void fw_state_aborted(struct fw_priv *fw_priv)
{
	__fw_state_set(fw_priv, FW_STATUS_ABORTED);
}

static inline int fw_state_wait(struct fw_priv *fw_priv)
{
	return __fw_state_wait_common(fw_priv, MAX_SCHEDULE_TIMEOUT);
}

static bool __fw_state_check(struct fw_priv *fw_priv,
			     enum fw_status status)
{
	struct fw_state *fw_st = &fw_priv->fw_st;

	return fw_st->status == status;
}

static inline bool fw_state_is_aborted(struct fw_priv *fw_priv)
{
	return __fw_state_check(fw_priv, FW_STATUS_ABORTED);
}

#ifdef CONFIG_FW_LOADER_USER_HELPER

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

#endif /* CONFIG_FW_LOADER_USER_HELPER */

static int fw_cache_piggyback_on_request(const char *name);

static struct fw_priv *__allocate_fw_priv(const char *fw_name,
					  struct firmware_cache *fwc,
					  void *dbuf, size_t size)
{
	struct fw_priv *fw_priv;

	fw_priv = kzalloc(sizeof(*fw_priv), GFP_ATOMIC);
	if (!fw_priv)
		return NULL;

	fw_priv->fw_name = kstrdup_const(fw_name, GFP_ATOMIC);
	if (!fw_priv->fw_name) {
		kfree(fw_priv);
		return NULL;
	}

	kref_init(&fw_priv->ref);
	fw_priv->fwc = fwc;
	fw_priv->data = dbuf;
	fw_priv->allocated_size = size;
	fw_state_init(fw_priv);
#ifdef CONFIG_FW_LOADER_USER_HELPER
	INIT_LIST_HEAD(&fw_priv->pending_list);
#endif

	pr_debug("%s: fw-%s fw_priv=%p\n", __func__, fw_name, fw_priv);

	return fw_priv;
}

static struct fw_priv *__lookup_fw_priv(const char *fw_name)
{
	struct fw_priv *tmp;
	struct firmware_cache *fwc = &fw_cache;

	list_for_each_entry(tmp, &fwc->head, list)
		if (!strcmp(tmp->fw_name, fw_name))
			return tmp;
	return NULL;
}

/* Returns 1 for batching firmware requests with the same name */
static int alloc_lookup_fw_priv(const char *fw_name,
				struct firmware_cache *fwc,
				struct fw_priv **fw_priv, void *dbuf,
				size_t size)
{
	struct fw_priv *tmp;

	spin_lock(&fwc->lock);
	tmp = __lookup_fw_priv(fw_name);
	if (tmp) {
		kref_get(&tmp->ref);
		spin_unlock(&fwc->lock);
		*fw_priv = tmp;
		pr_debug("batched request - sharing the same struct fw_priv and lookup for multiple requests\n");
		return 1;
	}
	tmp = __allocate_fw_priv(fw_name, fwc, dbuf, size);
	if (tmp)
		list_add(&tmp->list, &fwc->head);
	spin_unlock(&fwc->lock);

	*fw_priv = tmp;

	return tmp ? 0 : -ENOMEM;
}

static void __free_fw_priv(struct kref *ref)
	__releases(&fwc->lock)
{
	struct fw_priv *fw_priv = to_fw_priv(ref);
	struct firmware_cache *fwc = fw_priv->fwc;

	pr_debug("%s: fw-%s fw_priv=%p data=%p size=%u\n",
		 __func__, fw_priv->fw_name, fw_priv, fw_priv->data,
		 (unsigned int)fw_priv->size);

	list_del(&fw_priv->list);
	spin_unlock(&fwc->lock);

#ifdef CONFIG_FW_LOADER_USER_HELPER
	if (fw_priv->is_paged_buf) {
		int i;
		vunmap(fw_priv->data);
		for (i = 0; i < fw_priv->nr_pages; i++)
			__free_page(fw_priv->pages[i]);
		vfree(fw_priv->pages);
	} else
#endif
	if (!fw_priv->allocated_size)
		vfree(fw_priv->data);
	kfree_const(fw_priv->fw_name);
	kfree(fw_priv);
}

static void free_fw_priv(struct fw_priv *fw_priv)
{
	struct firmware_cache *fwc = fw_priv->fwc;
	spin_lock(&fwc->lock);
	if (!kref_put(&fw_priv->ref, __free_fw_priv))
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

static int
fw_get_filesystem_firmware(struct device *device, struct fw_priv *fw_priv)
{
	loff_t size;
	int i, len;
	int rc = -ENOENT;
	char *path;
	enum kernel_read_file_id id = READING_FIRMWARE;
	size_t msize = INT_MAX;

	/* Already populated data member means we're loading into a buffer */
	if (fw_priv->data) {
		id = READING_FIRMWARE_PREALLOC_BUFFER;
		msize = fw_priv->allocated_size;
	}

	path = __getname();
	if (!path)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(fw_path); i++) {
		/* skip the unset customized path */
		if (!fw_path[i][0])
			continue;

		len = snprintf(path, PATH_MAX, "%s/%s",
			       fw_path[i], fw_priv->fw_name);
		if (len >= PATH_MAX) {
			rc = -ENAMETOOLONG;
			break;
		}

		fw_priv->size = 0;
		rc = kernel_read_file_from_path(path, &fw_priv->data, &size,
						msize, id);
		if (rc) {
			if (rc == -ENOENT)
				dev_dbg(device, "loading %s failed with error %d\n",
					 path, rc);
			else
				dev_warn(device, "loading %s failed with error %d\n",
					 path, rc);
			continue;
		}
		dev_dbg(device, "direct-loading %s\n", fw_priv->fw_name);
		fw_priv->size = size;
		fw_state_done(fw_priv);
		break;
	}
	__putname(path);

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
	free_fw_priv(fw->priv);
}

/* store the pages buffer info firmware from buf */
static void fw_set_page_data(struct fw_priv *fw_priv, struct firmware *fw)
{
	fw->priv = fw_priv;
#ifdef CONFIG_FW_LOADER_USER_HELPER
	fw->pages = fw_priv->pages;
#endif
	fw->size = fw_priv->size;
	fw->data = fw_priv->data;

	pr_debug("%s: fw-%s fw_priv=%p data=%p size=%u\n",
		 __func__, fw_priv->fw_name, fw_priv, fw_priv->data,
		 (unsigned int)fw_priv->size);
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
		devres_free(fwn);
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

static int assign_fw(struct firmware *fw, struct device *device,
		     unsigned int opt_flags)
{
	struct fw_priv *fw_priv = fw->priv;

	mutex_lock(&fw_lock);
	if (!fw_priv->size || fw_state_is_aborted(fw_priv)) {
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
	if (device && (opt_flags & FW_OPT_UEVENT) &&
	    !(opt_flags & FW_OPT_NOCACHE))
		fw_add_devm_name(device, fw_priv->fw_name);

	/*
	 * After caching firmware image is started, let it piggyback
	 * on request firmware.
	 */
	if (!(opt_flags & FW_OPT_NOCACHE) &&
	    fw_priv->fwc->state == FW_LOADER_START_CACHE) {
		if (fw_cache_piggyback_on_request(fw_priv->fw_name))
			kref_get(&fw_priv->ref);
	}

	/* pass the pages buffer to driver at the last minute */
	fw_set_page_data(fw_priv, fw);
	mutex_unlock(&fw_lock);
	return 0;
}

/*
 * user-mode helper code
 */
#ifdef CONFIG_FW_LOADER_USER_HELPER
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

static void kill_pending_fw_fallback_reqs(bool only_kill_custom)
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
	if (add_uevent_var(env, "TIMEOUT=%i", loading_timeout))
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

static inline int register_sysfs_loader(void)
{
	return class_register(&firmware_class);
}

static inline void unregister_sysfs_loader(void)
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

/* Some architectures don't have PAGE_KERNEL_RO */
#ifndef PAGE_KERNEL_RO
#define PAGE_KERNEL_RO PAGE_KERNEL
#endif

/* one pages buffer should be mapped/unmapped only once */
static int map_fw_priv_pages(struct fw_priv *fw_priv)
{
	if (!fw_priv->is_paged_buf)
		return 0;

	vunmap(fw_priv->data);
	fw_priv->data = vmap(fw_priv->pages, fw_priv->nr_pages, 0,
			     PAGE_KERNEL_RO);
	if (!fw_priv->data)
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
	struct fw_sysfs *fw_sysfs = to_fw_sysfs(dev);
	struct fw_priv *fw_priv;
	ssize_t written = count;
	int loading = simple_strtol(buf, NULL, 10);
	int i;

	mutex_lock(&fw_lock);
	fw_priv = fw_sysfs->fw_priv;
	if (fw_state_is_aborted(fw_priv))
		goto out;

	switch (loading) {
	case 1:
		/* discarding any previous partial load */
		if (!fw_sysfs_done(fw_priv)) {
			for (i = 0; i < fw_priv->nr_pages; i++)
				__free_page(fw_priv->pages[i]);
			vfree(fw_priv->pages);
			fw_priv->pages = NULL;
			fw_priv->page_array_size = 0;
			fw_priv->nr_pages = 0;
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
			rc = map_fw_priv_pages(fw_priv);
			if (rc)
				dev_err(dev, "%s: map pages failed\n",
					__func__);
			else
				rc = security_kernel_post_read_file(NULL,
						fw_priv->data, fw_priv->size,
						READING_FIRMWARE);

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
		/* fallthrough */
	default:
		dev_err(dev, "%s: unexpected value (%d)\n", __func__, loading);
		/* fallthrough */
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
	struct fw_priv *fw_priv= fw_sysfs->fw_priv;
	int pages_needed = PAGE_ALIGN(min_size) >> PAGE_SHIFT;

	/* If the array of pages is too small, grow it... */
	if (fw_priv->page_array_size < pages_needed) {
		int new_array_size = max(pages_needed,
					 fw_priv->page_array_size * 2);
		struct page **new_pages;

		new_pages = vmalloc(new_array_size * sizeof(void *));
		if (!new_pages) {
			fw_load_abort(fw_sysfs);
			return -ENOMEM;
		}
		memcpy(new_pages, fw_priv->pages,
		       fw_priv->page_array_size * sizeof(void *));
		memset(&new_pages[fw_priv->page_array_size], 0, sizeof(void *) *
		       (new_array_size - fw_priv->page_array_size));
		vfree(fw_priv->pages);
		fw_priv->pages = new_pages;
		fw_priv->page_array_size = new_array_size;
	}

	while (fw_priv->nr_pages < pages_needed) {
		fw_priv->pages[fw_priv->nr_pages] =
			alloc_page(GFP_KERNEL | __GFP_HIGHMEM);

		if (!fw_priv->pages[fw_priv->nr_pages]) {
			fw_load_abort(fw_sysfs);
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
		   struct device *device, unsigned int opt_flags)
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

/* load a firmware via user helper */
static int _request_firmware_load(struct fw_sysfs *fw_sysfs,
				  unsigned int opt_flags, long timeout)
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

	if (opt_flags & FW_OPT_UEVENT) {
		fw_priv->need_uevent = true;
		dev_set_uevent_suppress(f_dev, false);
		dev_dbg(f_dev, "firmware: requesting %s\n", fw_priv->fw_name);
		kobject_uevent(&fw_sysfs->dev.kobj, KOBJ_ADD);
	} else {
		timeout = MAX_JIFFY_OFFSET;
	}

	retval = fw_sysfs_wait_timeout(fw_priv, timeout);
	if (retval < 0) {
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
				    unsigned int opt_flags)
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
	ret = _request_firmware_load(fw_sysfs, opt_flags, timeout);

	if (!ret)
		ret = assign_fw(firmware, device, opt_flags);

out_unlock:
	usermodehelper_read_unlock();

	return ret;
}

#else /* CONFIG_FW_LOADER_USER_HELPER */
static inline int
fw_load_from_user_helper(struct firmware *firmware, const char *name,
			 struct device *device, unsigned int opt_flags)
{
	return -ENOENT;
}

static inline void kill_pending_fw_fallback_reqs(bool only_kill_custom) { }

static inline int register_sysfs_loader(void)
{
	return 0;
}

static inline void unregister_sysfs_loader(void)
{
}

#endif /* CONFIG_FW_LOADER_USER_HELPER */

/* prepare firmware and firmware_buf structs;
 * return 0 if a firmware is already assigned, 1 if need to load one,
 * or a negative error code
 */
static int
_request_firmware_prepare(struct firmware **firmware_p, const char *name,
			  struct device *device, void *dbuf, size_t size)
{
	struct firmware *firmware;
	struct fw_priv *fw_priv;
	int ret;

	*firmware_p = firmware = kzalloc(sizeof(*firmware), GFP_KERNEL);
	if (!firmware) {
		dev_err(device, "%s: kmalloc(struct firmware) failed\n",
			__func__);
		return -ENOMEM;
	}

	if (fw_get_builtin_firmware(firmware, name, dbuf, size)) {
		dev_dbg(device, "using built-in %s\n", name);
		return 0; /* assigned */
	}

	ret = alloc_lookup_fw_priv(name, &fw_cache, &fw_priv, dbuf, size);

	/*
	 * bind with 'priv' now to avoid warning in failure path
	 * of requesting firmware.
	 */
	firmware->priv = fw_priv;

	if (ret > 0) {
		ret = fw_state_wait(fw_priv);
		if (!ret) {
			fw_set_page_data(fw_priv, firmware);
			return 0; /* assigned */
		}
	}

	if (ret < 0)
		return ret;
	return 1; /* need to load */
}

/*
 * Batched requests need only one wake, we need to do this step last due to the
 * fallback mechanism. The buf is protected with kref_get(), and it won't be
 * released until the last user calls release_firmware().
 *
 * Failed batched requests are possible as well, in such cases we just share
 * the struct fw_priv and won't release it until all requests are woken
 * and have gone through this same path.
 */
static void fw_abort_batch_reqs(struct firmware *fw)
{
	struct fw_priv *fw_priv;

	/* Loaded directly? */
	if (!fw || !fw->priv)
		return;

	fw_priv = fw->priv;
	if (!fw_state_is_aborted(fw_priv))
		fw_state_aborted(fw_priv);
}

/* called from request_firmware() and request_firmware_work_func() */
static int
_request_firmware(const struct firmware **firmware_p, const char *name,
		  struct device *device, void *buf, size_t size,
		  unsigned int opt_flags)
{
	struct firmware *fw = NULL;
	int ret;

	if (!firmware_p)
		return -EINVAL;

	if (!name || name[0] == '\0') {
		ret = -EINVAL;
		goto out;
	}

	ret = _request_firmware_prepare(&fw, name, device, buf, size);
	if (ret <= 0) /* error or already assigned */
		goto out;

	ret = fw_get_filesystem_firmware(device, fw->priv);
	if (ret) {
		if (!(opt_flags & FW_OPT_NO_WARN))
			dev_warn(device,
				 "Direct firmware load for %s failed with error %d\n",
				 name, ret);
		if (opt_flags & FW_OPT_USERHELPER) {
			dev_warn(device, "Falling back to user helper\n");
			ret = fw_load_from_user_helper(fw, name, device,
						       opt_flags);
		}
	} else
		ret = assign_fw(fw, device, opt_flags);

 out:
	if (ret < 0) {
		fw_abort_batch_reqs(fw);
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
	ret = _request_firmware(firmware_p, name, device, NULL, 0,
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
	ret = _request_firmware(firmware_p, name, device, NULL, 0,
				FW_OPT_UEVENT | FW_OPT_NO_WARN);
	module_put(THIS_MODULE);
	return ret;
}
EXPORT_SYMBOL_GPL(request_firmware_direct);

/**
 * request_firmware_into_buf - load firmware into a previously allocated buffer
 * @firmware_p: pointer to firmware image
 * @name: name of firmware file
 * @device: device for which firmware is being loaded and DMA region allocated
 * @buf: address of buffer to load firmware into
 * @size: size of buffer
 *
 * This function works pretty much like request_firmware(), but it doesn't
 * allocate a buffer to hold the firmware data. Instead, the firmware
 * is loaded directly into the buffer pointed to by @buf and the @firmware_p
 * data member is pointed at @buf.
 *
 * This function doesn't cache firmware either.
 */
int
request_firmware_into_buf(const struct firmware **firmware_p, const char *name,
			  struct device *device, void *buf, size_t size)
{
	int ret;

	__module_get(THIS_MODULE);
	ret = _request_firmware(firmware_p, name, device, buf, size,
				FW_OPT_UEVENT | FW_OPT_FALLBACK |
				FW_OPT_NOCACHE);
	module_put(THIS_MODULE);
	return ret;
}
EXPORT_SYMBOL(request_firmware_into_buf);

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

	_request_firmware(&fw, fw_work->name, fw_work->device, NULL, 0,
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
 *		  increase kernel boot time of built-in device drivers
 *		  requesting firmware in their ->probe() methods, if
 *		  @gfp is GFP_KERNEL.
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

static struct fw_priv *lookup_fw_priv(const char *fw_name)
{
	struct fw_priv *tmp;
	struct firmware_cache *fwc = &fw_cache;

	spin_lock(&fwc->lock);
	tmp = __lookup_fw_priv(fw_name);
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
	struct fw_priv *fw_priv;
	struct firmware fw;

	pr_debug("%s: %s\n", __func__, fw_name);

	if (fw_get_builtin_firmware(&fw, fw_name, NULL, 0))
		return 0;

	fw_priv = lookup_fw_priv(fw_name);
	if (fw_priv) {
		free_fw_priv(fw_priv);
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
		/*
		 * kill pending fallback requests with a custom fallback
		 * to avoid stalling suspend.
		 */
		kill_pending_fw_fallback_reqs(true);
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

static int __init register_fw_pm_ops(void)
{
	int ret;

	spin_lock_init(&fw_cache.name_lock);
	INIT_LIST_HEAD(&fw_cache.fw_names);

	INIT_DELAYED_WORK(&fw_cache.work,
			  device_uncache_fw_images_work);

	fw_cache.pm_notify.notifier_call = fw_pm_notify;
	ret = register_pm_notifier(&fw_cache.pm_notify);
	if (ret)
		return ret;

	register_syscore_ops(&fw_syscore_ops);

	return ret;
}

static inline void unregister_fw_pm_ops(void)
{
	unregister_syscore_ops(&fw_syscore_ops);
	unregister_pm_notifier(&fw_cache.pm_notify);
}
#else
static int fw_cache_piggyback_on_request(const char *name)
{
	return 0;
}
static inline int register_fw_pm_ops(void)
{
	return 0;
}
static inline void unregister_fw_pm_ops(void)
{
}
#endif

static void __init fw_cache_init(void)
{
	spin_lock_init(&fw_cache.lock);
	INIT_LIST_HEAD(&fw_cache.head);
	fw_cache.state = FW_LOADER_NO_CACHE;
}

static int fw_shutdown_notify(struct notifier_block *unused1,
			      unsigned long unused2, void *unused3)
{
	/*
	 * Kill all pending fallback requests to avoid both stalling shutdown,
	 * and avoid a deadlock with the usermode_lock.
	 */
	kill_pending_fw_fallback_reqs(false);

	return NOTIFY_DONE;
}

static struct notifier_block fw_shutdown_nb = {
	.notifier_call = fw_shutdown_notify,
};

static int __init firmware_class_init(void)
{
	int ret;

	/* No need to unfold these on exit */
	fw_cache_init();

	ret = register_fw_pm_ops();
	if (ret)
		return ret;

	ret = register_reboot_notifier(&fw_shutdown_nb);
	if (ret)
		goto out;

	return register_sysfs_loader();

out:
	unregister_fw_pm_ops();
	return ret;
}

static void __exit firmware_class_exit(void)
{
	unregister_fw_pm_ops();
	unregister_reboot_notifier(&fw_shutdown_nb);
	unregister_sysfs_loader();
}

fs_initcall(firmware_class_init);
module_exit(firmware_class_exit);
