/*
 * Collaborative memory management interface.
 *
 * Copyright (C) 2008 IBM Corporation
 * Author(s): Brian King (brking@linux.vnet.ibm.com),
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/oom.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/stringify.h>
#include <linux/swap.h>
#include <linux/sysdev.h>
#include <asm/firmware.h>
#include <asm/hvcall.h>
#include <asm/mmu.h>
#include <asm/pgalloc.h>
#include <asm/uaccess.h>

#include "plpar_wrappers.h"

#define CMM_DRIVER_VERSION	"1.0.0"
#define CMM_DEFAULT_DELAY	1
#define CMM_DEBUG			0
#define CMM_DISABLE		0
#define CMM_OOM_KB		1024
#define CMM_MIN_MEM_MB		256
#define KB2PAGES(_p)		((_p)>>(PAGE_SHIFT-10))
#define PAGES2KB(_p)		((_p)<<(PAGE_SHIFT-10))

static unsigned int delay = CMM_DEFAULT_DELAY;
static unsigned int oom_kb = CMM_OOM_KB;
static unsigned int cmm_debug = CMM_DEBUG;
static unsigned int cmm_disabled = CMM_DISABLE;
static unsigned long min_mem_mb = CMM_MIN_MEM_MB;
static struct sys_device cmm_sysdev;

MODULE_AUTHOR("Brian King <brking@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("IBM System p Collaborative Memory Manager");
MODULE_LICENSE("GPL");
MODULE_VERSION(CMM_DRIVER_VERSION);

module_param_named(delay, delay, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(delay, "Delay (in seconds) between polls to query hypervisor paging requests. "
		 "[Default=" __stringify(CMM_DEFAULT_DELAY) "]");
module_param_named(oom_kb, oom_kb, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(oom_kb, "Amount of memory in kb to free on OOM. "
		 "[Default=" __stringify(CMM_OOM_KB) "]");
module_param_named(min_mem_mb, min_mem_mb, ulong, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(min_mem_mb, "Minimum amount of memory (in MB) to not balloon. "
		 "[Default=" __stringify(CMM_MIN_MEM_MB) "]");
module_param_named(debug, cmm_debug, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable module debugging logging. Set to 1 to enable. "
		 "[Default=" __stringify(CMM_DEBUG) "]");

#define CMM_NR_PAGES ((PAGE_SIZE - sizeof(void *) - sizeof(unsigned long)) / sizeof(unsigned long))

#define cmm_dbg(...) if (cmm_debug) { printk(KERN_INFO "cmm: "__VA_ARGS__); }

struct cmm_page_array {
	struct cmm_page_array *next;
	unsigned long index;
	unsigned long page[CMM_NR_PAGES];
};

static unsigned long loaned_pages;
static unsigned long loaned_pages_target;
static unsigned long oom_freed_pages;

static struct cmm_page_array *cmm_page_list;
static DEFINE_SPINLOCK(cmm_lock);

static struct task_struct *cmm_thread_ptr;

/**
 * cmm_alloc_pages - Allocate pages and mark them as loaned
 * @nr:	number of pages to allocate
 *
 * Return value:
 * 	number of pages requested to be allocated which were not
 **/
static long cmm_alloc_pages(long nr)
{
	struct cmm_page_array *pa, *npa;
	unsigned long addr;
	long rc;

	cmm_dbg("Begin request for %ld pages\n", nr);

	while (nr) {
		addr = __get_free_page(GFP_NOIO | __GFP_NOWARN |
				       __GFP_NORETRY | __GFP_NOMEMALLOC);
		if (!addr)
			break;
		spin_lock(&cmm_lock);
		pa = cmm_page_list;
		if (!pa || pa->index >= CMM_NR_PAGES) {
			/* Need a new page for the page list. */
			spin_unlock(&cmm_lock);
			npa = (struct cmm_page_array *)__get_free_page(GFP_NOIO | __GFP_NOWARN |
								       __GFP_NORETRY | __GFP_NOMEMALLOC);
			if (!npa) {
				pr_info("%s: Can not allocate new page list\n", __func__);
				free_page(addr);
				break;
			}
			spin_lock(&cmm_lock);
			pa = cmm_page_list;

			if (!pa || pa->index >= CMM_NR_PAGES) {
				npa->next = pa;
				npa->index = 0;
				pa = npa;
				cmm_page_list = pa;
			} else
				free_page((unsigned long) npa);
		}

		if ((rc = plpar_page_set_loaned(__pa(addr)))) {
			pr_err("%s: Can not set page to loaned. rc=%ld\n", __func__, rc);
			spin_unlock(&cmm_lock);
			free_page(addr);
			break;
		}

		pa->page[pa->index++] = addr;
		loaned_pages++;
		totalram_pages--;
		spin_unlock(&cmm_lock);
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
	struct cmm_page_array *pa;
	unsigned long addr;

	cmm_dbg("Begin free of %ld pages.\n", nr);
	spin_lock(&cmm_lock);
	pa = cmm_page_list;
	while (nr) {
		if (!pa || pa->index <= 0)
			break;
		addr = pa->page[--pa->index];

		if (pa->index == 0) {
			pa = pa->next;
			free_page((unsigned long) cmm_page_list);
			cmm_page_list = pa;
		}

		plpar_page_set_active(__pa(addr));
		free_page(addr);
		loaned_pages--;
		nr--;
		totalram_pages++;
	}
	spin_unlock(&cmm_lock);
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
	loaned_pages_target = loaned_pages;
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
	int rc;
	struct hvcall_mpp_data mpp_data;
	signed long active_pages_target, page_loan_request, target;
	signed long total_pages = totalram_pages + loaned_pages;
	signed long min_mem_pages = (min_mem_mb * 1024 * 1024) / PAGE_SIZE;

	rc = h_get_mpp(&mpp_data);

	if (rc != H_SUCCESS)
		return;

	page_loan_request = div_s64((s64)mpp_data.loan_request, PAGE_SIZE);
	target = page_loan_request + (signed long)loaned_pages;

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
		page_loan_request, loaned_pages, loaned_pages_target,
		oom_freed_pages, totalram_pages);
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

	while (1) {
		timeleft = msleep_interruptible(delay * 1000);

		if (kthread_should_stop() || timeleft) {
			loaned_pages_target = loaned_pages;
			break;
		}

		cmm_get_mpp();

		if (loaned_pages_target > loaned_pages) {
			if (cmm_alloc_pages(loaned_pages_target - loaned_pages))
				loaned_pages_target = loaned_pages;
		} else if (loaned_pages_target < loaned_pages)
			cmm_free_pages(loaned_pages - loaned_pages_target);
	}
	return 0;
}

#define CMM_SHOW(name, format, args...)			\
	static ssize_t show_##name(struct sys_device *dev,	\
				   struct sysdev_attribute *attr,	\
				   char *buf)			\
	{							\
		return sprintf(buf, format, ##args);		\
	}							\
	static SYSDEV_ATTR(name, S_IRUGO, show_##name, NULL)

CMM_SHOW(loaned_kb, "%lu\n", PAGES2KB(loaned_pages));
CMM_SHOW(loaned_target_kb, "%lu\n", PAGES2KB(loaned_pages_target));

static ssize_t show_oom_pages(struct sys_device *dev,
			      struct sysdev_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", PAGES2KB(oom_freed_pages));
}

static ssize_t store_oom_pages(struct sys_device *dev,
			       struct sysdev_attribute *attr,
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

static SYSDEV_ATTR(oom_freed_kb, S_IWUSR| S_IRUGO,
		   show_oom_pages, store_oom_pages);

static struct sysdev_attribute *cmm_attrs[] = {
	&attr_loaned_kb,
	&attr_loaned_target_kb,
	&attr_oom_freed_kb,
};

static struct sysdev_class cmm_sysdev_class = {
	.name = "cmm",
};

/**
 * cmm_sysfs_register - Register with sysfs
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int cmm_sysfs_register(struct sys_device *sysdev)
{
	int i, rc;

	if ((rc = sysdev_class_register(&cmm_sysdev_class)))
		return rc;

	sysdev->id = 0;
	sysdev->cls = &cmm_sysdev_class;

	if ((rc = sysdev_register(sysdev)))
		goto class_unregister;

	for (i = 0; i < ARRAY_SIZE(cmm_attrs); i++) {
		if ((rc = sysdev_create_file(sysdev, cmm_attrs[i])))
			goto fail;
	}

	return 0;

fail:
	while (--i >= 0)
		sysdev_remove_file(sysdev, cmm_attrs[i]);
	sysdev_unregister(sysdev);
class_unregister:
	sysdev_class_unregister(&cmm_sysdev_class);
	return rc;
}

/**
 * cmm_unregister_sysfs - Unregister from sysfs
 *
 **/
static void cmm_unregister_sysfs(struct sys_device *sysdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cmm_attrs); i++)
		sysdev_remove_file(sysdev, cmm_attrs[i]);
	sysdev_unregister(sysdev);
	sysdev_class_unregister(&cmm_sysdev_class);
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
		cmm_free_pages(loaned_pages);
	}
	return NOTIFY_DONE;
}

static struct notifier_block cmm_reboot_nb = {
	.notifier_call = cmm_reboot_notifier,
};

/**
 * cmm_init - Module initialization
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int cmm_init(void)
{
	int rc = -ENOMEM;

	if (!firmware_has_feature(FW_FEATURE_CMO))
		return -EOPNOTSUPP;

	if ((rc = register_oom_notifier(&cmm_oom_nb)) < 0)
		return rc;

	if ((rc = register_reboot_notifier(&cmm_reboot_nb)))
		goto out_oom_notifier;

	if ((rc = cmm_sysfs_register(&cmm_sysdev)))
		goto out_reboot_notifier;

	if (cmm_disabled)
		return rc;

	cmm_thread_ptr = kthread_run(cmm_thread, NULL, "cmmthread");
	if (IS_ERR(cmm_thread_ptr)) {
		rc = PTR_ERR(cmm_thread_ptr);
		goto out_unregister_sysfs;
	}

	return rc;

out_unregister_sysfs:
	cmm_unregister_sysfs(&cmm_sysdev);
out_reboot_notifier:
	unregister_reboot_notifier(&cmm_reboot_nb);
out_oom_notifier:
	unregister_oom_notifier(&cmm_oom_nb);
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
	cmm_free_pages(loaned_pages);
	cmm_unregister_sysfs(&cmm_sysdev);
}

/**
 * cmm_set_disable - Disable/Enable CMM
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int cmm_set_disable(const char *val, struct kernel_param *kp)
{
	int disable = simple_strtoul(val, NULL, 10);

	if (disable != 0 && disable != 1)
		return -EINVAL;

	if (disable && !cmm_disabled) {
		if (cmm_thread_ptr)
			kthread_stop(cmm_thread_ptr);
		cmm_thread_ptr = NULL;
		cmm_free_pages(loaned_pages);
	} else if (!disable && cmm_disabled) {
		cmm_thread_ptr = kthread_run(cmm_thread, NULL, "cmmthread");
		if (IS_ERR(cmm_thread_ptr))
			return PTR_ERR(cmm_thread_ptr);
	}

	cmm_disabled = disable;
	return 0;
}

module_param_call(disable, cmm_set_disable, param_get_uint,
		  &cmm_disabled, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(disable, "Disable CMM. Set to 1 to disable. "
		 "[Default=" __stringify(CMM_DISABLE) "]");

module_init(cmm_init);
module_exit(cmm_exit);
