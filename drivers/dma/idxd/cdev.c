// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/sched/task.h>
#include <linux/intel-svm.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/iommu.h>
#include <uapi/linux/idxd.h>
#include "registers.h"
#include "idxd.h"

struct idxd_cdev_context {
	const char *name;
	dev_t devt;
	struct ida minor_ida;
};

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
	unsigned int flags;
	struct iommu_sva *sva;
};

static void idxd_cdev_dev_release(struct device *dev)
{
	struct idxd_cdev *idxd_cdev = dev_to_cdev(dev);
	struct idxd_cdev_context *cdev_ctx;
	struct idxd_wq *wq = idxd_cdev->wq;

	cdev_ctx = &ictx[wq->idxd->data->type];
	ida_simple_remove(&cdev_ctx->minor_ida, idxd_cdev->minor);
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

static int idxd_cdev_open(struct inode *inode, struct file *filp)
{
	struct idxd_user_context *ctx;
	struct idxd_device *idxd;
	struct idxd_wq *wq;
	struct device *dev;
	int rc = 0;
	struct iommu_sva *sva;
	unsigned int pasid;

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

	if (device_pasid_enabled(idxd)) {
		sva = iommu_sva_bind_device(dev, current->mm, NULL);
		if (IS_ERR(sva)) {
			rc = PTR_ERR(sva);
			dev_err(dev, "pasid allocation failed: %d\n", rc);
			goto failed;
		}

		pasid = iommu_sva_get_pasid(sva);
		if (pasid == IOMMU_PASID_INVALID) {
			iommu_sva_unbind_device(sva);
			rc = -EINVAL;
			goto failed;
		}

		ctx->sva = sva;
		ctx->pasid = pasid;

		if (wq_dedicated(wq)) {
			rc = idxd_wq_set_pasid(wq, pasid);
			if (rc < 0) {
				iommu_sva_unbind_device(sva);
				dev_err(dev, "wq set pasid failed: %d\n", rc);
				goto failed;
			}
		}
	}

	idxd_wq_get(wq);
	mutex_unlock(&wq->wq_lock);
	return 0;

 failed:
	mutex_unlock(&wq->wq_lock);
	kfree(ctx);
	return rc;
}

static int idxd_cdev_release(struct inode *node, struct file *filep)
{
	struct idxd_user_context *ctx = filep->private_data;
	struct idxd_wq *wq = ctx->wq;
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	int rc;

	dev_dbg(dev, "%s called\n", __func__);
	filep->private_data = NULL;

	/* Wait for in-flight operations to complete. */
	if (wq_shared(wq)) {
		idxd_device_drain_pasid(idxd, ctx->pasid);
	} else {
		if (device_pasid_enabled(idxd)) {
			/* The wq disable in the disable pasid function will drain the wq */
			rc = idxd_wq_disable_pasid(wq);
			if (rc < 0)
				dev_err(dev, "wq disable pasid failed.\n");
		} else {
			idxd_wq_drain(wq);
		}
	}

	if (ctx->sva)
		iommu_sva_unbind_device(ctx->sva);
	kfree(ctx);
	mutex_lock(&wq->wq_lock);
	idxd_wq_put(wq);
	mutex_unlock(&wq->wq_lock);
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

	vma->vm_flags |= VM_DONTCOPY;
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
	minor = ida_simple_get(&cdev_ctx->minor_ida, 0, MINORMASK, GFP_KERNEL);
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
	wq->idxd_cdev = NULL;
	cdev_device_del(&idxd_cdev->cdev, cdev_dev(idxd_cdev));
	put_device(cdev_dev(idxd_cdev));
}

static int idxd_user_drv_probe(struct idxd_dev *idxd_dev)
{
	struct idxd_wq *wq = idxd_dev_to_wq(idxd_dev);
	struct idxd_device *idxd = wq->idxd;
	int rc;

	if (idxd->state != IDXD_DEV_ENABLED)
		return -ENXIO;

	mutex_lock(&wq->wq_lock);
	wq->type = IDXD_WQT_USER;
	rc = drv_enable_wq(wq);
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
	drv_disable_wq(wq);
err:
	wq->type = IDXD_WQT_NONE;
	mutex_unlock(&wq->wq_lock);
	return rc;
}

static void idxd_user_drv_remove(struct idxd_dev *idxd_dev)
{
	struct idxd_wq *wq = idxd_dev_to_wq(idxd_dev);

	mutex_lock(&wq->wq_lock);
	idxd_wq_del_cdev(wq);
	drv_disable_wq(wq);
	wq->type = IDXD_WQT_NONE;
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
