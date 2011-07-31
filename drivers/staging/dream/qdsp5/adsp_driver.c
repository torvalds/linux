/* arch/arm/mach-msm/qdsp5/adsp_driver.c
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Iliyan Malchev <ibm@android.com>
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

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "adsp.h"

#include <linux/msm_adsp.h>
#include <linux/android_pmem.h>

struct adsp_pmem_region {
	struct hlist_node list;
	void *vaddr;
	unsigned long paddr;
	unsigned long kvaddr;
	unsigned long len;
	struct file *file;
};

struct adsp_device {
	struct msm_adsp_module *module;

	spinlock_t event_queue_lock;
	wait_queue_head_t event_wait;
	struct list_head event_queue;
	int abort;

	const char *name;
	struct device *device;
	struct cdev cdev;
};

static struct adsp_device *inode_to_device(struct inode *inode);

#define __CONTAINS(r, v, l) ({					\
	typeof(r) __r = r;					\
	typeof(v) __v = v;					\
	typeof(v) __e = __v + l;				\
	int res = __v >= __r->vaddr && 				\
		__e <= __r->vaddr + __r->len;			\
	res;							\
})

#define CONTAINS(r1, r2) ({					\
	typeof(r2) __r2 = r2;					\
	__CONTAINS(r1, __r2->vaddr, __r2->len);			\
})

#define IN_RANGE(r, v) ({					\
	typeof(r) __r = r;					\
	typeof(v) __vv = v;					\
	int res = ((__vv >= __r->vaddr) &&			\
		(__vv < (__r->vaddr + __r->len)));		\
	res;							\
})

#define OVERLAPS(r1, r2) ({					\
	typeof(r1) __r1 = r1;					\
	typeof(r2) __r2 = r2;					\
	typeof(__r2->vaddr) __v = __r2->vaddr;			\
	typeof(__v) __e = __v + __r2->len - 1;			\
	int res = (IN_RANGE(__r1, __v) || IN_RANGE(__r1, __e));	\
	res;							\
})

static int adsp_pmem_check(struct msm_adsp_module *module,
		void *vaddr, unsigned long len)
{
	struct adsp_pmem_region *region_elt;
	struct hlist_node *node;
	struct adsp_pmem_region t = { .vaddr = vaddr, .len = len };

	hlist_for_each_entry(region_elt, node, &module->pmem_regions, list) {
		if (CONTAINS(region_elt, &t) || CONTAINS(&t, region_elt) ||
		    OVERLAPS(region_elt, &t)) {
			printk(KERN_ERR "adsp: module %s:"
				" region (vaddr %p len %ld)"
				" clashes with registered region"
				" (vaddr %p paddr %p len %ld)\n",
				module->name,
				vaddr, len,
				region_elt->vaddr,
				(void *)region_elt->paddr,
				region_elt->len);
			return -EINVAL;
		}
	}

	return 0;
}

static int adsp_pmem_add(struct msm_adsp_module *module,
			 struct adsp_pmem_info *info)
{
	unsigned long paddr, kvaddr, len;
	struct file *file;
	struct adsp_pmem_region *region;
	int rc = -EINVAL;

	mutex_lock(&module->pmem_regions_lock);
	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (!region) {
		rc = -ENOMEM;
		goto end;
	}
	INIT_HLIST_NODE(&region->list);
	if (get_pmem_file(info->fd, &paddr, &kvaddr, &len, &file)) {
		kfree(region);
		goto end;
	}

	rc = adsp_pmem_check(module, info->vaddr, len);
	if (rc < 0) {
		put_pmem_file(file);
		kfree(region);
		goto end;
	}

	region->vaddr = info->vaddr;
	region->paddr = paddr;
	region->kvaddr = kvaddr;
	region->len = len;
	region->file = file;

	hlist_add_head(&region->list, &module->pmem_regions);
end:
	mutex_unlock(&module->pmem_regions_lock);
	return rc;
}

static int adsp_pmem_lookup_vaddr(struct msm_adsp_module *module, void **addr,
		     unsigned long len, struct adsp_pmem_region **region)
{
	struct hlist_node *node;
	void *vaddr = *addr;
	struct adsp_pmem_region *region_elt;

	int match_count = 0;

	*region = NULL;

	/* returns physical address or zero */
	hlist_for_each_entry(region_elt, node, &module->pmem_regions, list) {
		if (vaddr >= region_elt->vaddr &&
		    vaddr < region_elt->vaddr + region_elt->len &&
		    vaddr + len <= region_elt->vaddr + region_elt->len) {
			/* offset since we could pass vaddr inside a registerd
			 * pmem buffer
			 */

			match_count++;
			if (!*region)
				*region = region_elt;
		}
	}

	if (match_count > 1) {
		printk(KERN_ERR "adsp: module %s: "
			"multiple hits for vaddr %p, len %ld\n",
			module->name, vaddr, len);
		hlist_for_each_entry(region_elt, node,
				&module->pmem_regions, list) {
			if (vaddr >= region_elt->vaddr &&
			    vaddr < region_elt->vaddr + region_elt->len &&
			    vaddr + len <= region_elt->vaddr + region_elt->len)
				printk(KERN_ERR "\t%p, %ld --> %p\n",
					region_elt->vaddr,
					region_elt->len,
					(void *)region_elt->paddr);
		}
	}

	return *region ? 0 : -1;
}

int adsp_pmem_fixup_kvaddr(struct msm_adsp_module *module, void **addr,
			   unsigned long *kvaddr, unsigned long len)
{
	struct adsp_pmem_region *region;
	void *vaddr = *addr;
	unsigned long *paddr = (unsigned long *)addr;
	int ret;

	ret = adsp_pmem_lookup_vaddr(module, addr, len, &region);
	if (ret) {
		printk(KERN_ERR "adsp: not patching %s (paddr & kvaddr),"
			" lookup (%p, %ld) failed\n",
			module->name, vaddr, len);
		return ret;
	}
	*paddr = region->paddr + (vaddr - region->vaddr);
	*kvaddr = region->kvaddr + (vaddr - region->vaddr);
	return 0;
}

int adsp_pmem_fixup(struct msm_adsp_module *module, void **addr,
		    unsigned long len)
{
	struct adsp_pmem_region *region;
	void *vaddr = *addr;
	unsigned long *paddr = (unsigned long *)addr;
	int ret;

	ret = adsp_pmem_lookup_vaddr(module, addr, len, &region);
	if (ret) {
		printk(KERN_ERR "adsp: not patching %s, lookup (%p, %ld) failed\n",
			module->name, vaddr, len);
		return ret;
	}

	*paddr = region->paddr + (vaddr - region->vaddr);
	return 0;
}

static int adsp_verify_cmd(struct msm_adsp_module *module,
			   unsigned int queue_id, void *cmd_data,
			   size_t cmd_size)
{
	/* call the per module verifier */
	if (module->verify_cmd)
		return module->verify_cmd(module, queue_id, cmd_data,
					     cmd_size);
	else
		printk(KERN_INFO "adsp: no packet verifying function "
				 "for task %s\n", module->name);
	return 0;
}

static long adsp_write_cmd(struct adsp_device *adev, void __user *arg)
{
	struct adsp_command_t cmd;
	unsigned char buf[256];
	void *cmd_data;
	long rc;

	if (copy_from_user(&cmd, (void __user *)arg, sizeof(cmd)))
		return -EFAULT;

	if (cmd.len > 256) {
		cmd_data = kmalloc(cmd.len, GFP_USER);
		if (!cmd_data)
			return -ENOMEM;
	} else {
		cmd_data = buf;
	}

	if (copy_from_user(cmd_data, (void __user *)(cmd.data), cmd.len)) {
		rc = -EFAULT;
		goto end;
	}

	mutex_lock(&adev->module->pmem_regions_lock);
	if (adsp_verify_cmd(adev->module, cmd.queue, cmd_data, cmd.len)) {
		printk(KERN_ERR "module %s: verify failed.\n",
			adev->module->name);
		rc = -EINVAL;
		goto end;
	}
	rc = msm_adsp_write(adev->module, cmd.queue, cmd_data, cmd.len);
end:
	mutex_unlock(&adev->module->pmem_regions_lock);

	if (cmd.len > 256)
		kfree(cmd_data);

	return rc;
}

static int adsp_events_pending(struct adsp_device *adev)
{
	unsigned long flags;
	int yes;
	spin_lock_irqsave(&adev->event_queue_lock, flags);
	yes = !list_empty(&adev->event_queue);
	spin_unlock_irqrestore(&adev->event_queue_lock, flags);
	return yes || adev->abort;
}

static int adsp_pmem_lookup_paddr(struct msm_adsp_module *module, void **addr,
		     struct adsp_pmem_region **region)
{
	struct hlist_node *node;
	unsigned long paddr = (unsigned long)(*addr);
	struct adsp_pmem_region *region_elt;

	hlist_for_each_entry(region_elt, node, &module->pmem_regions, list) {
		if (paddr >= region_elt->paddr &&
		    paddr < region_elt->paddr + region_elt->len) {
			*region = region_elt;
			return 0;
		}
	}
	return -1;
}

int adsp_pmem_paddr_fixup(struct msm_adsp_module *module, void **addr)
{
	struct adsp_pmem_region *region;
	unsigned long paddr = (unsigned long)(*addr);
	unsigned long *vaddr = (unsigned long *)addr;
	int ret;

	ret = adsp_pmem_lookup_paddr(module, addr, &region);
	if (ret) {
		printk(KERN_ERR "adsp: not patching %s, paddr %p lookup failed\n",
			module->name, vaddr);
		return ret;
	}

	*vaddr = (unsigned long)region->vaddr + (paddr - region->paddr);
	return 0;
}

static int adsp_patch_event(struct msm_adsp_module *module,
				struct adsp_event *event)
{
	/* call the per-module msg verifier */
	if (module->patch_event)
		return module->patch_event(module, event);
	return 0;
}

static long adsp_get_event(struct adsp_device *adev, void __user *arg)
{
	unsigned long flags;
	struct adsp_event *data = NULL;
	struct adsp_event_t evt;
	int timeout;
	long rc = 0;

	if (copy_from_user(&evt, arg, sizeof(struct adsp_event_t)))
		return -EFAULT;

	timeout = (int)evt.timeout_ms;

	if (timeout > 0) {
		rc = wait_event_interruptible_timeout(
			adev->event_wait, adsp_events_pending(adev),
			msecs_to_jiffies(timeout));
		if (rc == 0)
			return -ETIMEDOUT;
	} else {
		rc = wait_event_interruptible(
			adev->event_wait, adsp_events_pending(adev));
	}
	if (rc < 0)
		return rc;

	if (adev->abort)
		return -ENODEV;

	spin_lock_irqsave(&adev->event_queue_lock, flags);
	if (!list_empty(&adev->event_queue)) {
		data = list_first_entry(&adev->event_queue,
					struct adsp_event, list);
		list_del(&data->list);
	}
	spin_unlock_irqrestore(&adev->event_queue_lock, flags);

	if (!data)
		return -EAGAIN;

	/* DSP messages are type 0; they may contain physical addresses */
	if (data->type == 0)
		adsp_patch_event(adev->module, data);

	/* map adsp_event --> adsp_event_t */
	if (evt.len < data->size) {
		rc = -ETOOSMALL;
		goto end;
	}
	if (data->msg_id != EVENT_MSG_ID) {
		if (copy_to_user((void *)(evt.data), data->data.msg16,
					data->size)) {
			rc = -EFAULT;
			goto end;
	}
	} else {
		if (copy_to_user((void *)(evt.data), data->data.msg32,
					data->size)) {
			rc = -EFAULT;
			goto end;
		}
	}

	evt.type = data->type; /* 0 --> from aDSP, 1 --> from ARM9 */
	evt.msg_id = data->msg_id;
	evt.flags = data->is16;
	evt.len = data->size;
	if (copy_to_user(arg, &evt, sizeof(evt)))
		rc = -EFAULT;
end:
	kfree(data);
	return rc;
}

static long adsp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct adsp_device *adev = filp->private_data;

	switch (cmd) {
	case ADSP_IOCTL_ENABLE:
		return msm_adsp_enable(adev->module);

	case ADSP_IOCTL_DISABLE:
		return msm_adsp_disable(adev->module);

	case ADSP_IOCTL_DISABLE_EVENT_RSP:
		return 0;

	case ADSP_IOCTL_DISABLE_ACK:
		pr_err("adsp: ADSP_IOCTL_DISABLE_ACK is not implemented.\n");
		break;

	case ADSP_IOCTL_WRITE_COMMAND:
		return adsp_write_cmd(adev, (void __user *) arg);

	case ADSP_IOCTL_GET_EVENT:
		return adsp_get_event(adev, (void __user *) arg);

	case ADSP_IOCTL_SET_CLKRATE: {
#if CONFIG_MSM_AMSS_VERSION==6350
		unsigned long clk_rate;
		if (copy_from_user(&clk_rate, (void *) arg, sizeof(clk_rate)))
			return -EFAULT;
		return adsp_set_clkrate(adev->module, clk_rate);
#endif
	}

	case ADSP_IOCTL_REGISTER_PMEM: {
		struct adsp_pmem_info info;
		if (copy_from_user(&info, (void *) arg, sizeof(info)))
			return -EFAULT;
		return adsp_pmem_add(adev->module, &info);
	}

	case ADSP_IOCTL_ABORT_EVENT_READ:
		adev->abort = 1;
		wake_up(&adev->event_wait);
		break;

	default:
		break;
	}
	return -EINVAL;
}

static int adsp_release(struct inode *inode, struct file *filp)
{
	struct adsp_device *adev = filp->private_data;
	struct msm_adsp_module *module = adev->module;
	struct hlist_node *node, *tmp;
	struct adsp_pmem_region *region;

	pr_info("adsp_release() '%s'\n", adev->name);

	/* clear module before putting it to avoid race with open() */
	adev->module = NULL;

	mutex_lock(&module->pmem_regions_lock);
	hlist_for_each_safe(node, tmp, &module->pmem_regions) {
		region = hlist_entry(node, struct adsp_pmem_region, list);
		hlist_del(node);
		put_pmem_file(region->file);
		kfree(region);
	}
	mutex_unlock(&module->pmem_regions_lock);
	BUG_ON(!hlist_empty(&module->pmem_regions));

	msm_adsp_put(module);
	return 0;
}

static void adsp_event(void *driver_data, unsigned id, size_t len,
		       void (*getevent)(void *ptr, size_t len))
{
	struct adsp_device *adev = driver_data;
	struct adsp_event *event;
	unsigned long flags;

	if (len > ADSP_EVENT_MAX_SIZE) {
		pr_err("adsp_event: event too large (%d bytes)\n", len);
		return;
	}

	event = kmalloc(sizeof(*event), GFP_ATOMIC);
	if (!event) {
		pr_err("adsp_event: cannot allocate buffer\n");
		return;
	}

	if (id != EVENT_MSG_ID) {
		event->type = 0;
		event->is16 = 0;
		event->msg_id = id;
		event->size = len;

		getevent(event->data.msg16, len);
	} else {
		event->type = 1;
		event->is16 = 1;
		event->msg_id = id;
		event->size = len;
		getevent(event->data.msg32, len);
	}

	spin_lock_irqsave(&adev->event_queue_lock, flags);
	list_add_tail(&event->list, &adev->event_queue);
	spin_unlock_irqrestore(&adev->event_queue_lock, flags);
	wake_up(&adev->event_wait);
}

static struct msm_adsp_ops adsp_ops = {
	.event = adsp_event,
};

static int adsp_open(struct inode *inode, struct file *filp)
{
	struct adsp_device *adev;
	int rc;

	rc = nonseekable_open(inode, filp);
	if (rc < 0)
		return rc;

	adev = inode_to_device(inode);
	if (!adev)
		return -ENODEV;

	pr_info("adsp_open() name = '%s'\n", adev->name);

	rc = msm_adsp_get(adev->name, &adev->module, &adsp_ops, adev);
	if (rc)
		return rc;

	pr_info("adsp_open() module '%s' adev %p\n", adev->name, adev);
	filp->private_data = adev;
	adev->abort = 0;
	INIT_HLIST_HEAD(&adev->module->pmem_regions);
	mutex_init(&adev->module->pmem_regions_lock);

	return 0;
}

static unsigned adsp_device_count;
static struct adsp_device *adsp_devices;

static struct adsp_device *inode_to_device(struct inode *inode)
{
	unsigned n = MINOR(inode->i_rdev);
	if (n < adsp_device_count) {
		if (adsp_devices[n].device)
			return adsp_devices + n;
	}
	return NULL;
}

static dev_t adsp_devno;
static struct class *adsp_class;

static struct file_operations adsp_fops = {
	.owner = THIS_MODULE,
	.open = adsp_open,
	.unlocked_ioctl = adsp_ioctl,
	.release = adsp_release,
};

static void adsp_create(struct adsp_device *adev, const char *name,
			struct device *parent, dev_t devt)
{
	struct device *dev;
	int rc;

	dev = device_create(adsp_class, parent, devt, "%s", name);
	if (IS_ERR(dev))
		return;

	init_waitqueue_head(&adev->event_wait);
	INIT_LIST_HEAD(&adev->event_queue);
	spin_lock_init(&adev->event_queue_lock);

	cdev_init(&adev->cdev, &adsp_fops);
	adev->cdev.owner = THIS_MODULE;

	rc = cdev_add(&adev->cdev, devt, 1);
	if (rc < 0) {
		device_destroy(adsp_class, devt);
	} else {
		adev->device = dev;
		adev->name = name;
	}
}

void msm_adsp_publish_cdevs(struct msm_adsp_module *modules, unsigned n)
{
	int rc;

	adsp_devices = kzalloc(sizeof(struct adsp_device) * n, GFP_KERNEL);
	if (!adsp_devices)
		return;

	adsp_class = class_create(THIS_MODULE, "adsp");
	if (IS_ERR(adsp_class))
		goto fail_create_class;

	rc = alloc_chrdev_region(&adsp_devno, 0, n, "adsp");
	if (rc < 0)
		goto fail_alloc_region;

	adsp_device_count = n;
	for (n = 0; n < adsp_device_count; n++) {
		adsp_create(adsp_devices + n,
			    modules[n].name, &modules[n].pdev.dev,
			    MKDEV(MAJOR(adsp_devno), n));
	}

	return;

fail_alloc_region:
	class_unregister(adsp_class);
fail_create_class:
	kfree(adsp_devices);
}
