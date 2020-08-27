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
};

struct idxd_user_context {
	struct idxd_wq *wq;
	struct task_struct *task;
	unsigned int flags;
};

enum idxd_cdev_cleanup {
	CDEV_NORMAL = 0,
	CDEV_FAILED,
};

static void idxd_cdev_dev_release(struct device *dev)
{
	dev_dbg(dev, "releasing cdev device\n");
	kfree(dev);
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

static inline struct idxd_wq *idxd_cdev_wq(struct idxd_cdev *idxd_cdev)
{
	return container_of(idxd_cdev, struct idxd_wq, idxd_cdev);
}

static inline struct idxd_wq *inode_wq(struct inode *inode)
{
	return idxd_cdev_wq(inode_idxd_cdev(inode));
}

static int idxd_cdev_open(struct inode *inode, struct file *filp)
{
	struct idxd_user_context *ctx;
	struct idxd_device *idxd;
	struct idxd_wq *wq;
	struct device *dev;
	int rc = 0;

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

	dev_dbg(dev, "%s called\n", __func__);
	filep->private_data = NULL;

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
	struct idxd_cdev *idxd_cdev = &wq->idxd_cdev;
	unsigned long flags;
	__poll_t out = 0;

	poll_wait(filp, &idxd_cdev->err_queue, wait);
	spin_lock_irqsave(&idxd->dev_lock, flags);
	if (idxd->sw_err.valid)
		out = EPOLLIN | EPOLLRDNORM;
	spin_unlock_irqrestore(&idxd->dev_lock, flags);

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
	return MAJOR(ictx[idxd->type].devt);
}

static int idxd_wq_cdev_dev_setup(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct idxd_cdev *idxd_cdev = &wq->idxd_cdev;
	struct idxd_cdev_context *cdev_ctx;
	struct device *dev;
	int minor, rc;

	idxd_cdev->dev = kzalloc(sizeof(*idxd_cdev->dev), GFP_KERNEL);
	if (!idxd_cdev->dev)
		return -ENOMEM;

	dev = idxd_cdev->dev;
	dev->parent = &idxd->pdev->dev;
	dev_set_name(dev, "%s/wq%u.%u", idxd_get_dev_name(idxd),
		     idxd->id, wq->id);
	dev->bus = idxd_get_bus_type(idxd);

	cdev_ctx = &ictx[wq->idxd->type];
	minor = ida_simple_get(&cdev_ctx->minor_ida, 0, MINORMASK, GFP_KERNEL);
	if (minor < 0) {
		rc = minor;
		kfree(dev);
		goto ida_err;
	}

	dev->devt = MKDEV(MAJOR(cdev_ctx->devt), minor);
	dev->type = &idxd_cdev_device_type;
	rc = device_register(dev);
	if (rc < 0) {
		dev_err(&idxd->pdev->dev, "device register failed\n");
		goto dev_reg_err;
	}
	idxd_cdev->minor = minor;

	return 0;

 dev_reg_err:
	ida_simple_remove(&cdev_ctx->minor_ida, MINOR(dev->devt));
	put_device(dev);
 ida_err:
	idxd_cdev->dev = NULL;
	return rc;
}

static void idxd_wq_cdev_cleanup(struct idxd_wq *wq,
				 enum idxd_cdev_cleanup cdev_state)
{
	struct idxd_cdev *idxd_cdev = &wq->idxd_cdev;
	struct idxd_cdev_context *cdev_ctx;

	cdev_ctx = &ictx[wq->idxd->type];
	if (cdev_state == CDEV_NORMAL)
		cdev_del(&idxd_cdev->cdev);
	device_unregister(idxd_cdev->dev);
	/*
	 * The device_type->release() will be called on the device and free
	 * the allocated struct device. We can just forget it.
	 */
	ida_simple_remove(&cdev_ctx->minor_ida, idxd_cdev->minor);
	idxd_cdev->dev = NULL;
	idxd_cdev->minor = -1;
}

int idxd_wq_add_cdev(struct idxd_wq *wq)
{
	struct idxd_cdev *idxd_cdev = &wq->idxd_cdev;
	struct cdev *cdev = &idxd_cdev->cdev;
	struct device *dev;
	int rc;

	rc = idxd_wq_cdev_dev_setup(wq);
	if (rc < 0)
		return rc;

	dev = idxd_cdev->dev;
	cdev_init(cdev, &idxd_cdev_fops);
	cdev_set_parent(cdev, &dev->kobj);
	rc = cdev_add(cdev, dev->devt, 1);
	if (rc) {
		dev_dbg(&wq->idxd->pdev->dev, "cdev_add failed: %d\n", rc);
		idxd_wq_cdev_cleanup(wq, CDEV_FAILED);
		return rc;
	}

	init_waitqueue_head(&idxd_cdev->err_queue);
	return 0;
}

void idxd_wq_del_cdev(struct idxd_wq *wq)
{
	idxd_wq_cdev_cleanup(wq, CDEV_NORMAL);
}

int idxd_cdev_register(void)
{
	int rc, i;

	for (i = 0; i < IDXD_TYPE_MAX; i++) {
		ida_init(&ictx[i].minor_ida);
		rc = alloc_chrdev_region(&ictx[i].devt, 0, MINORMASK,
					 ictx[i].name);
		if (rc)
			return rc;
	}

	return 0;
}

void idxd_cdev_remove(void)
{
	int i;

	for (i = 0; i < IDXD_TYPE_MAX; i++) {
		unregister_chrdev_region(ictx[i].devt, MINORMASK);
		ida_destroy(&ictx[i].minor_ida);
	}
}
