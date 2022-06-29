// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Collaborative memory management interface.
 *
 * Copyright (C) 2008 IBM Corporation
 * Author(s): Brian King (brking@linux.vnet.ibm.com),
 */

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/oom.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/stringify.h>
#include <linux/swap.h>
#include <linux/device.h>
#include <linux/mount.h>
#include <linux/pseudo_fs.h>
#include <linux/magic.h>
#include <linux/balloon_compaction.h>
#include <asm/firmware.h>
#include <asm/hvcall.h>
#include <asm/mmu.h>
#include <linux/uaccess.h>
#include <linux/memory.h>
#include <asm/plpar_wrappers.h>

#include "pseries.h"

#define CMM_DRIVER_VERSION	"1.0.0"
#define CMM_DEFAULT_DELAY	1
#define CMM_HOTPLUG_DELAY	5
#define CMM_DEBUG			0
#define CMM_DISABLE		0
#define CMM_OOM_KB		1024
#define CMM_MIN_MEM_MB		256
#define KB2PAGES(_p)		((_p)>>(PAGE_SHIFT-10))
#define PAGES2KB(_p)		((_p)<<(PAGE_SHIFT-10))

#define CMM_MEM_HOTPLUG_PRI	1

static unsigned int delay = CMM_DEFAULT_DELAY;
static unsigned int hotplug_delay = CMM_HOTPLUG_DELAY;
static unsigned int oom_kb = CMM_OOM_KB;
static unsigned int cmm_debug = CMM_DEBUG;
static unsigned int cmm_disabled = CMM_DISABLE;
static unsigned long min_mem_mb = CMM_MIN_MEM_MB;
static bool __read_mostly simulate;
static unsigned long simulate_loan_target_kb;
static struct device cmm_dev;

MODULE_AUTHOR("Brian King <brking@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("IBM System p Collaborative Memory Manager");
MODULE_LICENSE("GPL");
MODULE_VERSION(CMM_DRIVER_VERSION);

module_param_named(delay, delay, uint, 0644);
MODULE_PARM_DESC(delay, "Delay (in seconds) between polls to query hypervisor paging requests. "
		 "[Default=" __stringify(CMM_DEFAULT_DELAY) "]");
module_param_named(hotplug_delay, hotplug_delay, uint, 0644);
MODULE_PARM_DESC(hotplug_delay, "Delay (in seconds) after memory hotplug remove "
		 "before loaning resumes. "
		 "[Default=" __stringify(CMM_HOTPLUG_DELAY) "]");
module_param_named(oom_kb, oom_kb, uint, 0644);
MODULE_PARM_DESC(oom_kb, "Amount of memory in kb to free on OOM. "
		 "[Default=" __stringify(CMM_OOM_KB) "]");
module_param_named(min_mem_mb, min_mem_mb, ulong, 0644);
MODULE_PARM_DESC(min_mem_mb, "Minimum amount of memory (in MB) to not balloon. "
		 "[Default=" __stringify(CMM_MIN_MEM_MB) "]");
module_param_named(debug, cmm_debug, uint, 0644);
MODULE_PARM_DESC(debug, "Enable module debugging logging. Set to 1 to enable. "
		 "[Default=" __stringify(CMM_DEBUG) "]");
module_param_named(simulate, simulate, bool, 0444);
MODULE_PARM_DESC(simulate, "Enable simulation mode (no communication with hw).");

#define cmm_dbg(...) if (cmm_debug) { printk(KERN_INFO "cmm: "__VA_ARGS__); }

static atomic_long_t loaned_pages;
static unsigned long loaned_pages_target;
static unsigned long oom_freed_pages;

static DEFINE_MUTEX(hotplug_mutex);
static int hotplug_occurred; /* protected by the hotplug mutex */

static struct task_struct *cmm_thread_ptr;
static struct balloon_dev_info b_dev_info;

static long plpar_page_set_loaned(struct page *page)
{
	const unsigned long vpa = page_to_phys(page);
	unsigned long cmo_page_sz = cmo_get_page_size();
	long rc = 0;
	int i;

	if (unlikely(simulate))
		return 0;

	for (i = 0; !rc && i < PAGE_SIZE; i += cmo_page_sz)
		rc = plpar_hcall_norets(H_PAGE_INIT, H_PAGE_SET_LOANED, vpa + i, 0);

	for (i -= cmo_page_sz; rc && i != 0; i -= cmo_page_sz)
		plpar_hcall_norets(H_PAGE_INIT, H_PAGE_SET_ACTIVE,
				   vpa + i - cmo_page_sz, 0);

	return rc;
}

static long plpar_page_set_active(struct page *page)
{
	const unsigned long vpa = page_to_phys(page);
	unsigned long cmo_page_sz = cmo_get_page_size();
	long rc = 0;
	int i;

	if (unlikely(simulate))
		return 0;

	for (i = 0; !rc && i < PAGE_SIZE; i += cmo_page_sz)
		rc = plpar_hcall_norets(H_PAGE_INIT, H_PAGE_SET_ACTIVE, vpa + i, 0);

	for (i -= cmo_page_sz; rc && i != 0; i -= cmo_page_sz)
		plpar_hcall_norets(H_PAGE_INIT, H_PAGE_SET_LOANED,
				   vpa + i - cmo_page_sz, 0);

	return rc;
}

/**
 * cmm_alloc_pages - Allocate pages and mark them as loaned
 * @nr:	number of pages to allocate
 *
 * Return value:
 * 	number of pages requested to be allocated which were not
 **/
static long cmm_alloc_pages(long nr)
{
	struct page *page;
	long rc;

	cmm_dbg("Begin request for %ld pages\n", nr);

	while (nr) {
		/* Exit if a hotplug operation is in progress or occurred */
		if (mutex_trylock(&hotplug_mutex)) {
			if (hotplug_occurred) {
				mutex_unlock(&hotplug_mutex);
				break;
			}
			mutex_unlock(&hotplug_mutex);
		} else {
			break;
		}

		page = balloon_page_alloc();
		if (!page)
			break;
		rc = plpar_page_set_loaned(page);
		if (rc) {
			pr_err("%s: Can not set page to loaned. rc=%ld\n", __func__, rc);
			__free_page(page);
			break;
		}

		balloon_page_enqueue(&b_dev_info, page);
		atomic_long_inc(&loaned_pages);
		adjust_managed_page_count(page, -1);
		nr--;
	}

	cmm_dbg("End request with %ld pages unfulfilled\n", nr);
	return nr;
}

/**
 * cmm_free_pages - Free pages and mark them as active
 * @nr:	number of pages to free
 *
 * Return value:
 * 	number of pages requested to be freed which were not
 **/
static long cmm_free_pages(long nr)
{
	struct page *page;

	cmm_dbg("Begin free of %ld pages.\n", nr);
	while (nr) {
		page = balloon_page_dequeue(&b_dev_info);
		if (!page)
			break;
		plpar_page_set_active(page);
		adjust_managed_page_count(page, 1);
		__free_page(page);
		atomic_long_dec(&loaned_pages);
		nr--;
	}
	cmm_dbg("End request with %ld pages unfulfilled\n", nr);
	return nr;
}

/**
 * cmm_oom_notify - OOM notifier
 * @self:	notifier block struct
 * @dummy:	not used
 * @parm:	returned - number of pages freed
 *
 * Return value:
 * 	NOTIFY_OK
 **/
static int cmm_oom_notify(struct notifier_block *self,
			  unsigned long dummy, void *parm)
{
	unsigned long *freed = parm;
	long nr = KB2PAGES(oom_kb);

	cmm_dbg("OOM processing started\n");
	nr = cmm_free_pages(nr);
	loaned_pages_target = atomic_long_read(&loaned_pages);
	*freed += KB2PAGES(oom_kb) - nr;
	oom_freed_pages += KB2PAGES(oom_kb) - nr;
	cmm_dbg("OOM processing complete\n");
	return NOTIFY_OK;
}

/**
 * cmm_get_mpp - Read memory performance parameters
 *
 * Makes hcall to query the current page loan request from the hypervisor.
 *
 * Return value:
 * 	nothing
 **/
static void cmm_get_mpp(void)
{
	const long __loaned_pages = atomic_long_read(&loaned_pages);
	const long total_pages = totalram_pages() + __loaned_pages;
	int rc;
	struct hvcall_mpp_data mpp_data;
	signed long active_pages_target, page_loan_request, target;
	signed long min_mem_pages = (min_mem_mb * 1024 * 1024) / PAGE_SIZE;

	if (likely(!simulate)) {
		rc = h_get_mpp(&mpp_data);
		if (rc != H_SUCCESS)
			return;
		page_loan_request = div_s64((s64)mpp_data.loan_request,
					    PAGE_SIZE);
		target = page_loan_request + __loaned_pages;
	} else {
		target = KB2PAGES(simulate_loan_target_kb);
		page_loan_request = target - __loaned_pages;
	}

	if (target < 0 || total_pages < min_mem_pages)
		target = 0;

	if (target > oom_freed_pages)
		target -= oom_freed_pages;
	else
		target = 0;

	active_pages_target = total_pages - target;

	if (min_mem_pages > active_pages_target)
		target = total_pages - min_mem_pages;

	if (target < 0)
		target = 0;

	loaned_pages_target = target;

	cmm_dbg("delta = %ld, loaned = %lu, target = %lu, oom = %lu, totalram = %lu\n",
		page_loan_request, __loaned_pages, loaned_pages_target,
		oom_freed_pages, totalram_pages());
}

static struct notifier_block cmm_oom_nb = {
	.notifier_call = cmm_oom_notify
};

/**
 * cmm_thread - CMM task thread
 * @dummy:	not used
 *
 * Return value:
 * 	0
 **/
static int cmm_thread(void *dummy)
{
	unsigned long timeleft;
	long __loaned_pages;

	while (1) {
		timeleft = msleep_interruptible(delay * 1000);

		if (kthread_should_stop() || timeleft)
			break;

		if (mutex_trylock(&hotplug_mutex)) {
			if (hotplug_occurred) {
				hotplug_occurred = 0;
				mutex_unlock(&hotplug_mutex);
				cmm_dbg("Hotplug operation has occurred, "
						"loaning activity suspended "
						"for %d seconds.\n",
						hotplug_delay);
				timeleft = msleep_interruptible(hotplug_delay *
						1000);
				if (kthread_should_stop() || timeleft)
					break;
				continue;
			}
			mutex_unlock(&hotplug_mutex);
		} else {
			cmm_dbg("Hotplug operation in progress, activity "
					"suspended\n");
			continue;
		}

		cmm_get_mpp();

		__loaned_pages = atomic_long_read(&loaned_pages);
		if (loaned_pages_target > __loaned_pages) {
			if (cmm_alloc_pages(loaned_pages_target - __loaned_pages))
				loaned_pages_target = __loaned_pages;
		} else if (loaned_pages_target < __loaned_pages)
			cmm_free_pages(__loaned_pages - loaned_pages_target);
	}
	return 0;
}

#define CMM_SHOW(name, format, args...)			\
	static ssize_t show_##name(struct device *dev,	\
				   struct device_attribute *attr,	\
				   char *buf)			\
	{							\
		return sprintf(buf, format, ##args);		\
	}							\
	static DEVICE_ATTR(name, 0444, show_##name, NULL)

CMM_SHOW(loaned_kb, "%lu\n", PAGES2KB(atomic_long_read(&loaned_pages)));
CMM_SHOW(loaned_target_kb, "%lu\n", PAGES2KB(loaned_pages_target));

static ssize_t show_oom_pages(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", PAGES2KB(oom_freed_pages));
}

static ssize_t store_oom_pages(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	unsigned long val = simple_strtoul (buf, NULL, 10);

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (val != 0)
		return -EBADMSG;

	oom_freed_pages = 0;
	return count;
}

static DEVICE_ATTR(oom_freed_kb, 0644,
		   show_oom_pages, store_oom_pages);

static struct device_attribute *cmm_attrs[] = {
	&dev_attr_loaned_kb,
	&dev_attr_loaned_target_kb,
	&dev_attr_oom_freed_kb,
};

static DEVICE_ULONG_ATTR(simulate_loan_target_kb, 0644,
			 simulate_loan_target_kb);

static struct bus_type cmm_subsys = {
	.name = "cmm",
	.dev_name = "cmm",
};

static void cmm_release_device(struct device *dev)
{
}

/**
 * cmm_sysfs_register - Register with sysfs
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int cmm_sysfs_register(struct device *dev)
{
	int i, rc;

	if ((rc = subsys_system_register(&cmm_subsys, NULL)))
		return rc;

	dev->id = 0;
	dev->bus = &cmm_subsys;
	dev->release = cmm_release_device;

	if ((rc = device_register(dev)))
		goto subsys_unregister;

	for (i = 0; i < ARRAY_SIZE(cmm_attrs); i++) {
		if ((rc = device_create_file(dev, cmm_attrs[i])))
			goto fail;
	}

	if (!simulate)
		return 0;
	rc = device_create_file(dev, &dev_attr_simulate_loan_target_kb.attr);
	if (rc)
		goto fail;
	return 0;

fail:
	while (--i >= 0)
		device_remove_file(dev, cmm_attrs[i]);
	device_unregister(dev);
subsys_unregister:
	bus_unregister(&cmm_subsys);
	return rc;
}

/**
 * cmm_unregister_sysfs - Unregister from sysfs
 *
 **/
static void cmm_unregister_sysfs(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cmm_attrs); i++)
		device_remove_file(dev, cmm_attrs[i]);
	device_unregister(dev);
	bus_unregister(&cmm_subsys);
}

/**
 * cmm_reboot_notifier - Make sure pages are not still marked as "loaned"
 *
 **/
static int cmm_reboot_notifier(struct notifier_block *nb,
			       unsigned long action, void *unused)
{
	if (action == SYS_RESTART) {
		if (cmm_thread_ptr)
			kthread_stop(cmm_thread_ptr);
		cmm_thread_ptr = NULL;
		cmm_free_pages(atomic_long_read(&loaned_pages));
	}
	return NOTIFY_DONE;
}

static struct notifier_block cmm_reboot_nb = {
	.notifier_call = cmm_reboot_notifier,
};

/**
 * cmm_memory_cb - Handle memory hotplug notifier calls
 * @self:	notifier block struct
 * @action:	action to take
 * @arg:	struct memory_notify data for handler
 *
 * Return value:
 *	NOTIFY_OK or notifier error based on subfunction return value
 *
 **/
static int cmm_memory_cb(struct notifier_block *self,
			unsigned long action, void *arg)
{
	switch (action) {
	case MEM_GOING_OFFLINE:
		mutex_lock(&hotplug_mutex);
		hotplug_occurred = 1;
		break;
	case MEM_OFFLINE:
	case MEM_CANCEL_OFFLINE:
		mutex_unlock(&hotplug_mutex);
		cmm_dbg("Memory offline operation complete.\n");
		break;
	case MEM_GOING_ONLINE:
	case MEM_ONLINE:
	case MEM_CANCEL_ONLINE:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block cmm_mem_nb = {
	.notifier_call = cmm_memory_cb,
	.priority = CMM_MEM_HOTPLUG_PRI
};

#ifdef CONFIG_BALLOON_COMPACTION
static struct vfsmount *balloon_mnt;

static int cmm_init_fs_context(struct fs_context *fc)
{
	return init_pseudo(fc, PPC_CMM_MAGIC) ? 0 : -ENOMEM;
}

static struct file_system_type balloon_fs = {
	.name = "ppc-cmm",
	.init_fs_context = cmm_init_fs_context,
	.kill_sb = kill_anon_super,
};

static int cmm_migratepage(struct balloon_dev_info *b_dev_info,
			   struct page *newpage, struct page *page,
			   enum migrate_mode mode)
{
	unsigned long flags;

	/*
	 * loan/"inflate" the newpage first.
	 *
	 * We might race against the cmm_thread who might discover after our
	 * loan request that another page is to be unloaned. However, once
	 * the cmm_thread runs again later, this error will automatically
	 * be corrected.
	 */
	if (plpar_page_set_loaned(newpage)) {
		/* Unlikely, but possible. Tell the caller not to retry now. */
		pr_err_ratelimited("%s: Cannot set page to loaned.", __func__);
		return -EBUSY;
	}

	/* balloon page list reference */
	get_page(newpage);

	/*
	 * When we migrate a page to a different zone, we have to fixup the
	 * count of both involved zones as we adjusted the managed page count
	 * when inflating.
	 */
	if (page_zone(page) != page_zone(newpage)) {
		adjust_managed_page_count(page, 1);
		adjust_managed_page_count(newpage, -1);
	}

	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	balloon_page_insert(b_dev_info, newpage);
	balloon_page_delete(page);
	b_dev_info->isolated_pages--;
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);

	/*
	 * activate/"deflate" the old page. We ignore any errors just like the
	 * other callers.
	 */
	plpar_page_set_active(page);

	/* balloon page list reference */
	put_page(page);

	return MIGRATEPAGE_SUCCESS;
}

static int cmm_balloon_compaction_init(void)
{
	int rc;

	balloon_devinfo_init(&b_dev_info);
	b_dev_info.migratepage = cmm_migratepage;

	balloon_mnt = kern_mount(&balloon_fs);
	if (IS_ERR(balloon_mnt)) {
		rc = PTR_ERR(balloon_mnt);
		balloon_mnt = NULL;
		return rc;
	}

	b_dev_info.inode = alloc_anon_inode(balloon_mnt->mnt_sb);
	if (IS_ERR(b_dev_info.inode)) {
		rc = PTR_ERR(b_dev_info.inode);
		b_dev_info.inode = NULL;
		kern_unmount(balloon_mnt);
		balloon_mnt = NULL;
		return rc;
	}

	b_dev_info.inode->i_mapping->a_ops = &balloon_aops;
	return 0;
}
static void cmm_balloon_compaction_deinit(void)
{
	if (b_dev_info.inode)
		iput(b_dev_info.inode);
	b_dev_info.inode = NULL;
	kern_unmount(balloon_mnt);
	balloon_mnt = NULL;
}
#else /* CONFIG_BALLOON_COMPACTION */
static int cmm_balloon_compaction_init(void)
{
	return 0;
}

static void cmm_balloon_compaction_deinit(void)
{
}
#endif /* CONFIG_BALLOON_COMPACTION */

/**
 * cmm_init - Module initialization
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int cmm_init(void)
{
	int rc;

	if (!firmware_has_feature(FW_FEATURE_CMO) && !simulate)
		return -EOPNOTSUPP;

	rc = cmm_balloon_compaction_init();
	if (rc)
		return rc;

	rc = register_oom_notifier(&cmm_oom_nb);
	if (rc < 0)
		goto out_balloon_compaction;

	if ((rc = register_reboot_notifier(&cmm_reboot_nb)))
		goto out_oom_notifier;

	if ((rc = cmm_sysfs_register(&cmm_dev)))
		goto out_reboot_notifier;

	rc = register_memory_notifier(&cmm_mem_nb);
	if (rc)
		goto out_unregister_notifier;

	if (cmm_disabled)
		return 0;

	cmm_thread_ptr = kthread_run(cmm_thread, NULL, "cmmthread");
	if (IS_ERR(cmm_thread_ptr)) {
		rc = PTR_ERR(cmm_thread_ptr);
		goto out_unregister_notifier;
	}

	return 0;
out_unregister_notifier:
	unregister_memory_notifier(&cmm_mem_nb);
	cmm_unregister_sysfs(&cmm_dev);
out_reboot_notifier:
	unregister_reboot_notifier(&cmm_reboot_nb);
out_oom_notifier:
	unregister_oom_notifier(&cmm_oom_nb);
out_balloon_compaction:
	cmm_balloon_compaction_deinit();
	return rc;
}

/**
 * cmm_exit - Module exit
 *
 * Return value:
 * 	nothing
 **/
static void cmm_exit(void)
{
	if (cmm_thread_ptr)
		kthread_stop(cmm_thread_ptr);
	unregister_oom_notifier(&cmm_oom_nb);
	unregister_reboot_notifier(&cmm_reboot_nb);
	unregister_memory_notifier(&cmm_mem_nb);
	cmm_free_pages(atomic_long_read(&loaned_pages));
	cmm_unregister_sysfs(&cmm_dev);
	cmm_balloon_compaction_deinit();
}

/**
 * cmm_set_disable - Disable/Enable CMM
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int cmm_set_disable(const char *val, const struct kernel_param *kp)
{
	int disable = simple_strtoul(val, NULL, 10);

	if (disable != 0 && disable != 1)
		return -EINVAL;

	if (disable && !cmm_disabled) {
		if (cmm_thread_ptr)
			kthread_stop(cmm_thread_ptr);
		cmm_thread_ptr = NULL;
		cmm_free_pages(atomic_long_read(&loaned_pages));
	} else if (!disable && cmm_disabled) {
		cmm_thread_ptr = kthread_run(cmm_thread, NULL, "cmmthread");
		if (IS_ERR(cmm_thread_ptr))
			return PTR_ERR(cmm_thread_ptr);
	}

	cmm_disabled = disable;
	return 0;
}

module_param_call(disable, cmm_set_disable, param_get_uint,
		  &cmm_disabled, 0644);
MODULE_PARM_DESC(disable, "Disable CMM. Set to 1 to disable. "
		 "[Default=" __stringify(CMM_DISABLE) "]");

module_init(cmm_init);
module_exit(cmm_exit);
