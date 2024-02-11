// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/sched/task.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/iommu.h>
#include <linux/highmem.h>
#include <uapi/linux/idxd.h>
#include <linux/xarray.h>
#include "registers.h"
#include "idxd.h"

struct idxd_cdev_context {
	const char *name;
	dev_t devt;
	struct ida minor_ida;
};

/*
 * Since user file names are global in DSA devices, define their ida's as
 * global to avoid conflict file names.
 */
static DEFINE_IDA(file_ida);
static DEFINE_MUTEX(ida_lock);

/*
 * ictx is an array based off of accelerator types. enum idxd_type
 * is used as index
 */
static struct idxd_cdev_context ictx[IDXD_TYPE_MAX] = {
	{ .name = "dsa" },
	{ .name = "iax" }
};

struct idxd_user_context {
	struct idxd_wq *wq;
	struct task_struct *task;
	unsigned int pasid;
	struct mm_struct *mm;
	unsigned int flags;
	struct iommu_sva *sva;
	struct idxd_dev idxd_dev;
	u64 counters[COUNTER_MAX];
	int id;
	pid_t pid;
};

static void idxd_cdev_evl_drain_pasid(struct idxd_wq *wq, u32 pasid);
static void idxd_xa_pasid_remove(struct idxd_user_context *ctx);

static inline struct idxd_user_context *dev_to_uctx(struct device *dev)
{
	struct idxd_dev *idxd_dev = confdev_to_idxd_dev(dev);

	return container_of(idxd_dev, struct idxd_user_context, idxd_dev);
}

static ssize_t cr_faults_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct idxd_user_context *ctx = dev_to_uctx(dev);

	return sysfs_emit(buf, "%llu\n", ctx->counters[COUNTER_FAULTS]);
}
static DEVICE_ATTR_RO(cr_faults);

static ssize_t cr_fault_failures_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct idxd_user_context *ctx = dev_to_uctx(dev);

	return sysfs_emit(buf, "%llu\n", ctx->counters[COUNTER_FAULT_FAILS]);
}
static DEVICE_ATTR_RO(cr_fault_failures);

static ssize_t pid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct idxd_user_context *ctx = dev_to_uctx(dev);

	return sysfs_emit(buf, "%u\n", ctx->pid);
}
static DEVICE_ATTR_RO(pid);

static struct attribute *cdev_file_attributes[] = {
	&dev_attr_cr_faults.attr,
	&dev_attr_cr_fault_failures.attr,
	&dev_attr_pid.attr,
	NULL
};

static umode_t cdev_file_attr_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, typeof(*dev), kobj);
	struct idxd_user_context *ctx = dev_to_uctx(dev);
	struct idxd_wq *wq = ctx->wq;

	if (!wq_pasid_enabled(wq))
		return 0;

	return a->mode;
}

static const struct attribute_group cdev_file_attribute_group = {
	.attrs = cdev_file_attributes,
	.is_visible = cdev_file_attr_visible,
};

static const struct attribute_group *cdev_file_attribute_groups[] = {
	&cdev_file_attribute_group,
	NULL
};

static void idxd_file_dev_release(struct device *dev)
{
	struct idxd_user_context *ctx = dev_to_uctx(dev);
	struct idxd_wq *wq = ctx->wq;
	struct idxd_device *idxd = wq->idxd;
	int rc;

	mutex_lock(&ida_lock);
	ida_free(&file_ida, ctx->id);
	mutex_unlock(&ida_lock);

	/* Wait for in-flight operations to complete. */
	if (wq_shared(wq)) {
		idxd_device_drain_pasid(idxd, ctx->pasid);
	} else {
		if (device_user_pasid_enabled(idxd)) {
			/* The wq disable in the disable pasid function will drain the wq */
			rc = idxd_wq_disable_pasid(wq);
			if (rc < 0)
				dev_err(dev, "wq disable pasid failed.\n");
		} else {
			idxd_wq_drain(wq);
		}
	}

	if (ctx->sva) {
		idxd_cdev_evl_drain_pasid(wq, ctx->pasid);
		iommu_sva_unbind_device(ctx->sva);
		idxd_xa_pasid_remove(ctx);
	}
	kfree(ctx);
	mutex_lock(&wq->wq_lock);
	idxd_wq_put(wq);
	mutex_unlock(&wq->wq_lock);
}

static struct device_type idxd_cdev_file_type = {
	.name = "idxd_file",
	.release = idxd_file_dev_release,
	.groups = cdev_file_attribute_groups,
};

static void idxd_cdev_dev_release(struct device *dev)
{
	struct idxd_cdev *idxd_cdev = dev_to_cdev(dev);
	struct idxd_cdev_context *cdev_ctx;
	struct idxd_wq *wq = idxd_cdev->wq;

	cdev_ctx = &ictx[wq->idxd->data->type];
	ida_free(&cdev_ctx->minor_ida, idxd_cdev->minor);
	kfree(idxd_cdev);
}

static struct device_type idxd_cdev_device_type = {
	.name = "idxd_cdev",
	.release = idxd_cdev_dev_release,
};

static inline struct idxd_cdev *inode_idxd_cdev(struct inode *inode)
{
	struct cdev *cdev = inode->i_cdev;

	return container_of(cdev, struct idxd_cdev, cdev);
}

static inline struct idxd_wq *inode_wq(struct inode *inode)
{
	struct idxd_cdev *idxd_cdev = inode_idxd_cdev(inode);

	return idxd_cdev->wq;
}

static void idxd_xa_pasid_remove(struct idxd_user_context *ctx)
{
	struct idxd_wq *wq = ctx->wq;
	void *ptr;

	mutex_lock(&wq->uc_lock);
	ptr = xa_cmpxchg(&wq->upasid_xa, ctx->pasid, ctx, NULL, GFP_KERNEL);
	if (ptr != (void *)ctx)
		dev_warn(&wq->idxd->pdev->dev, "xarray cmpxchg failed for pasid %u\n",
			 ctx->pasid);
	mutex_unlock(&wq->uc_lock);
}

void idxd_user_counter_increment(struct idxd_wq *wq, u32 pasid, int index)
{
	struct idxd_user_context *ctx;

	if (index >= COUNTER_MAX)
		return;

	mutex_lock(&wq->uc_lock);
	ctx = xa_load(&wq->upasid_xa, pasid);
	if (!ctx) {
		mutex_unlock(&wq->uc_lock);
		return;
	}
	ctx->counters[index]++;
	mutex_unlock(&wq->uc_lock);
}

static int idxd_cdev_open(struct inode *inode, struct file *filp)
{
	struct idxd_user_context *ctx;
	struct idxd_device *idxd;
	struct idxd_wq *wq;
	struct device *dev, *fdev;
	int rc = 0;
	struct iommu_sva *sva;
	unsigned int pasid;
	struct idxd_cdev *idxd_cdev;

	wq = inode_wq(inode);
	idxd = wq->idxd;
	dev = &idxd->pdev->dev;

	dev_dbg(dev, "%s called: %d\n", __func__, idxd_wq_refcount(wq));

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_lock(&wq->wq_lock);

	if (idxd_wq_refcount(wq) > 0 && wq_dedicated(wq)) {
		rc = -EBUSY;
		goto failed;
	}

	ctx->wq = wq;
	filp->private_data = ctx;
	ctx->pid = current->pid;

	if (device_user_pasid_enabled(idxd)) {
		sva = iommu_sva_bind_device(dev, current->mm);
		if (IS_ERR(sva)) {
			rc = PTR_ERR(sva);
			dev_err(dev, "pasid allocation failed: %d\n", rc);
			goto failed;
		}

		pasid = iommu_sva_get_pasid(sva);
		if (pasid == IOMMU_PASID_INVALID) {
			rc = -EINVAL;
			goto failed_get_pasid;
		}

		ctx->sva = sva;
		ctx->pasid = pasid;
		ctx->mm = current->mm;

		mutex_lock(&wq->uc_lock);
		rc = xa_insert(&wq->upasid_xa, pasid, ctx, GFP_KERNEL);
		mutex_unlock(&wq->uc_lock);
		if (rc < 0)
			dev_warn(dev, "PASID entry already exist in xarray.\n");

		if (wq_dedicated(wq)) {
			rc = idxd_wq_set_pasid(wq, pasid);
			if (rc < 0) {
				dev_err(dev, "wq set pasid failed: %d\n", rc);
				goto failed_set_pasid;
			}
		}
	}

	idxd_cdev = wq->idxd_cdev;
	mutex_lock(&ida_lock);
	ctx->id = ida_alloc(&file_ida, GFP_KERNEL);
	mutex_unlock(&ida_lock);
	if (ctx->id < 0) {
		dev_warn(dev, "ida alloc failure\n");
		goto failed_ida;
	}
	ctx->idxd_dev.type  = IDXD_DEV_CDEV_FILE;
	fdev = user_ctx_dev(ctx);
	device_initialize(fdev);
	fdev->parent = cdev_dev(idxd_cdev);
	fdev->bus = &dsa_bus_type;
	fdev->type = &idxd_cdev_file_type;

	rc = dev_set_name(fdev, "file%d", ctx->id);
	if (rc < 0) {
		dev_warn(dev, "set name failure\n");
		goto failed_dev_name;
	}

	rc = device_add(fdev);
	if (rc < 0) {
		dev_warn(dev, "file device add failure\n");
		goto failed_dev_add;
	}

	idxd_wq_get(wq);
	mutex_unlock(&wq->wq_lock);
	return 0;

failed_dev_add:
failed_dev_name:
	put_device(fdev);
failed_ida:
failed_set_pasid:
	if (device_user_pasid_enabled(idxd))
		idxd_xa_pasid_remove(ctx);
failed_get_pasid:
	if (device_user_pasid_enabled(idxd))
		iommu_sva_unbind_device(sva);
failed:
	mutex_unlock(&wq->wq_lock);
	kfree(ctx);
	return rc;
}

static void idxd_cdev_evl_drain_pasid(struct idxd_wq *wq, u32 pasid)
{
	struct idxd_device *idxd = wq->idxd;
	struct idxd_evl *evl = idxd->evl;
	union evl_status_reg status;
	u16 h, t, size;
	int ent_size = evl_ent_size(idxd);
	struct __evl_entry *entry_head;

	if (!evl)
		return;

	spin_lock(&evl->lock);
	status.bits = ioread64(idxd->reg_base + IDXD_EVLSTATUS_OFFSET);
	t = status.tail;
	h = evl->head;
	size = evl->size;

	while (h != t) {
		entry_head = (struct __evl_entry *)(evl->log + (h * ent_size));
		if (entry_head->pasid == pasid && entry_head->wq_idx == wq->id)
			set_bit(h, evl->bmap);
		h = (h + 1) % size;
	}
	spin_unlock(&evl->lock);

	drain_workqueue(wq->wq);
}

static int idxd_cdev_release(struct inode *node, struct file *filep)
{
	struct idxd_user_context *ctx = filep->private_data;
	struct idxd_wq *wq = ctx->wq;
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;

	dev_dbg(dev, "%s called\n", __func__);
	filep->private_data = NULL;

	device_unregister(user_ctx_dev(ctx));

	return 0;
}

static int check_vma(struct idxd_wq *wq, struct vm_area_struct *vma,
		     const char *func)
{
	struct device *dev = &wq->idxd->pdev->dev;

	if ((vma->vm_end - vma->vm_start) > PAGE_SIZE) {
		dev_info_ratelimited(dev,
				     "%s: %s: mapping too large: %lu\n",
				     current->comm, func,
				     vma->vm_end - vma->vm_start);
		return -EINVAL;
	}

	return 0;
}

static int idxd_cdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct idxd_user_context *ctx = filp->private_data;
	struct idxd_wq *wq = ctx->wq;
	struct idxd_device *idxd = wq->idxd;
	struct pci_dev *pdev = idxd->pdev;
	phys_addr_t base = pci_resource_start(pdev, IDXD_WQ_BAR);
	unsigned long pfn;
	int rc;

	dev_dbg(&pdev->dev, "%s called\n", __func__);
	rc = check_vma(wq, vma, __func__);
	if (rc < 0)
		return rc;

	vm_flags_set(vma, VM_DONTCOPY);
	pfn = (base + idxd_get_wq_portal_full_offset(wq->id,
				IDXD_PORTAL_LIMITED)) >> PAGE_SHIFT;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_private_data = ctx;

	return io_remap_pfn_range(vma, vma->vm_start, pfn, PAGE_SIZE,
			vma->vm_page_prot);
}

static __poll_t idxd_cdev_poll(struct file *filp,
			       struct poll_table_struct *wait)
{
	struct idxd_user_context *ctx = filp->private_data;
	struct idxd_wq *wq = ctx->wq;
	struct idxd_device *idxd = wq->idxd;
	__poll_t out = 0;

	poll_wait(filp, &wq->err_queue, wait);
	spin_lock(&idxd->dev_lock);
	if (idxd->sw_err.valid)
		out = EPOLLIN | EPOLLRDNORM;
	spin_unlock(&idxd->dev_lock);

	return out;
}

static const struct file_operations idxd_cdev_fops = {
	.owner = THIS_MODULE,
	.open = idxd_cdev_open,
	.release = idxd_cdev_release,
	.mmap = idxd_cdev_mmap,
	.poll = idxd_cdev_poll,
};

int idxd_cdev_get_major(struct idxd_device *idxd)
{
	return MAJOR(ictx[idxd->data->type].devt);
}

int idxd_wq_add_cdev(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct idxd_cdev *idxd_cdev;
	struct cdev *cdev;
	struct device *dev;
	struct idxd_cdev_context *cdev_ctx;
	int rc, minor;

	idxd_cdev = kzalloc(sizeof(*idxd_cdev), GFP_KERNEL);
	if (!idxd_cdev)
		return -ENOMEM;

	idxd_cdev->idxd_dev.type = IDXD_DEV_CDEV;
	idxd_cdev->wq = wq;
	cdev = &idxd_cdev->cdev;
	dev = cdev_dev(idxd_cdev);
	cdev_ctx = &ictx[wq->idxd->data->type];
	minor = ida_alloc_max(&cdev_ctx->minor_ida, MINORMASK, GFP_KERNEL);
	if (minor < 0) {
		kfree(idxd_cdev);
		return minor;
	}
	idxd_cdev->minor = minor;

	device_initialize(dev);
	dev->parent = wq_confdev(wq);
	dev->bus = &dsa_bus_type;
	dev->type = &idxd_cdev_device_type;
	dev->devt = MKDEV(MAJOR(cdev_ctx->devt), minor);

	rc = dev_set_name(dev, "%s/wq%u.%u", idxd->data->name_prefix, idxd->id, wq->id);
	if (rc < 0)
		goto err;

	wq->idxd_cdev = idxd_cdev;
	cdev_init(cdev, &idxd_cdev_fops);
	rc = cdev_device_add(cdev, dev);
	if (rc) {
		dev_dbg(&wq->idxd->pdev->dev, "cdev_add failed: %d\n", rc);
		goto err;
	}

	return 0;

 err:
	put_device(dev);
	wq->idxd_cdev = NULL;
	return rc;
}

void idxd_wq_del_cdev(struct idxd_wq *wq)
{
	struct idxd_cdev *idxd_cdev;

	idxd_cdev = wq->idxd_cdev;
	ida_destroy(&file_ida);
	wq->idxd_cdev = NULL;
	cdev_device_del(&idxd_cdev->cdev, cdev_dev(idxd_cdev));
	put_device(cdev_dev(idxd_cdev));
}

static int idxd_user_drv_probe(struct idxd_dev *idxd_dev)
{
	struct device *dev = &idxd_dev->conf_dev;
	struct idxd_wq *wq = idxd_dev_to_wq(idxd_dev);
	struct idxd_device *idxd = wq->idxd;
	int rc;

	if (idxd->state != IDXD_DEV_ENABLED)
		return -ENXIO;

	/*
	 * User type WQ is enabled only when SVA is enabled for two reasons:
	 *   - If no IOMMU or IOMMU Passthrough without SVA, userspace
	 *     can directly access physical address through the WQ.
	 *   - The IDXD cdev driver does not provide any ways to pin
	 *     user pages and translate the address from user VA to IOVA or
	 *     PA without IOMMU SVA. Therefore the application has no way
	 *     to instruct the device to perform DMA function. This makes
	 *     the cdev not usable for normal application usage.
	 */
	if (!device_user_pasid_enabled(idxd)) {
		idxd->cmd_status = IDXD_SCMD_WQ_USER_NO_IOMMU;
		dev_dbg(&idxd->pdev->dev,
			"User type WQ cannot be enabled without SVA.\n");

		return -EOPNOTSUPP;
	}

	mutex_lock(&wq->wq_lock);

	if (!idxd_wq_driver_name_match(wq, dev)) {
		idxd->cmd_status = IDXD_SCMD_WQ_NO_DRV_NAME;
		rc = -ENODEV;
		goto wq_err;
	}

	wq->wq = create_workqueue(dev_name(wq_confdev(wq)));
	if (!wq->wq) {
		rc = -ENOMEM;
		goto wq_err;
	}

	wq->type = IDXD_WQT_USER;
	rc = idxd_drv_enable_wq(wq);
	if (rc < 0)
		goto err;

	rc = idxd_wq_add_cdev(wq);
	if (rc < 0) {
		idxd->cmd_status = IDXD_SCMD_CDEV_ERR;
		goto err_cdev;
	}

	idxd->cmd_status = 0;
	mutex_unlock(&wq->wq_lock);
	return 0;

err_cdev:
	idxd_drv_disable_wq(wq);
err:
	destroy_workqueue(wq->wq);
	wq->type = IDXD_WQT_NONE;
wq_err:
	mutex_unlock(&wq->wq_lock);
	return rc;
}

static void idxd_user_drv_remove(struct idxd_dev *idxd_dev)
{
	struct idxd_wq *wq = idxd_dev_to_wq(idxd_dev);

	mutex_lock(&wq->wq_lock);
	idxd_wq_del_cdev(wq);
	idxd_drv_disable_wq(wq);
	wq->type = IDXD_WQT_NONE;
	destroy_workqueue(wq->wq);
	wq->wq = NULL;
	mutex_unlock(&wq->wq_lock);
}

static enum idxd_dev_type dev_types[] = {
	IDXD_DEV_WQ,
	IDXD_DEV_NONE,
};

struct idxd_device_driver idxd_user_drv = {
	.probe = idxd_user_drv_probe,
	.remove = idxd_user_drv_remove,
	.name = "user",
	.type = dev_types,
};
EXPORT_SYMBOL_GPL(idxd_user_drv);

int idxd_cdev_register(void)
{
	int rc, i;

	for (i = 0; i < IDXD_TYPE_MAX; i++) {
		ida_init(&ictx[i].minor_ida);
		rc = alloc_chrdev_region(&ictx[i].devt, 0, MINORMASK,
					 ictx[i].name);
		if (rc)
			goto err_free_chrdev_region;
	}

	return 0;

err_free_chrdev_region:
	for (i--; i >= 0; i--)
		unregister_chrdev_region(ictx[i].devt, MINORMASK);

	return rc;
}

void idxd_cdev_remove(void)
{
	int i;

	for (i = 0; i < IDXD_TYPE_MAX; i++) {
		unregister_chrdev_region(ictx[i].devt, MINORMASK);
		ida_destroy(&ictx[i].minor_ida);
	}
}

/**
 * idxd_copy_cr - copy completion record to user address space found by wq and
 *		  PASID
 * @wq:		work queue
 * @pasid:	PASID
 * @addr:	user fault address to write
 * @cr:		completion record
 * @len:	number of bytes to copy
 *
 * This is called by a work that handles completion record fault.
 *
 * Return: number of bytes copied.
 */
int idxd_copy_cr(struct idxd_wq *wq, ioasid_t pasid, unsigned long addr,
		 void *cr, int len)
{
	struct device *dev = &wq->idxd->pdev->dev;
	int left = len, status_size = 1;
	struct idxd_user_context *ctx;
	struct mm_struct *mm;

	mutex_lock(&wq->uc_lock);

	ctx = xa_load(&wq->upasid_xa, pasid);
	if (!ctx) {
		dev_warn(dev, "No user context\n");
		goto out;
	}

	mm = ctx->mm;
	/*
	 * The completion record fault handling work is running in kernel
	 * thread context. It temporarily switches to the mm to copy cr
	 * to addr in the mm.
	 */
	kthread_use_mm(mm);
	left = copy_to_user((void __user *)addr + status_size, cr + status_size,
			    len - status_size);
	/*
	 * Copy status only after the rest of completion record is copied
	 * successfully so that the user gets the complete completion record
	 * when a non-zero status is polled.
	 */
	if (!left) {
		u8 status;

		/*
		 * Ensure that the completion record's status field is written
		 * after the rest of the completion record has been written.
		 * This ensures that the user receives the correct completion
		 * record information once polling for a non-zero status.
		 */
		wmb();
		status = *(u8 *)cr;
		if (put_user(status, (u8 __user *)addr))
			left += status_size;
	} else {
		left += status_size;
	}
	kthread_unuse_mm(mm);

out:
	mutex_unlock(&wq->uc_lock);

	return len - left;
}
