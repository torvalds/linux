// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <uapi/misc/snps_accel.h>
#include "snps_accel_drv.h"

#define MAX_DEVS		32
#define DRIVER_NAME		"snps_accel_app"
#define DEV_NAME_FORMAT		"snps!arcnet%d!app%d"

static struct class *snps_accel_class;
static unsigned int snps_accel_major;


static int
snps_accel_info_shmem(struct snps_accel_app *accel_app, char __user *argp)
{
	struct snps_accel_shmem data;

	data.offset = accel_app->shmem_base;
	data.size = accel_app->shmem_size;
	if (copy_to_user((void __user *)argp, &data,
			 sizeof(struct snps_accel_shmem)))
		return -EFAULT;

	return 0;
}

static int
snps_accel_info_notify(struct snps_accel_app *accel_app, char __user *argp)
{
	struct snps_accel_notify data;

	data.offset = accel_app->ctrl_base;
	data.size = accel_app->ctrl_size;
	if (copy_to_user((void __user *)argp, &data,
			 sizeof(struct snps_accel_notify)))
		return -EFAULT;

	return 0;
}

static int
snps_accel_wait_irq(struct snps_accel_file_priv *fpriv, char __user *argp)
{
	struct snps_accel_app *accel_app = fpriv->app;
	struct snps_accel_wait_irq data;
	int ret = 0;
	u32 event_count = 0;
	DECLARE_WAITQUEUE(wait, current);

	if (!accel_app || !accel_app->ctrl.dev || accel_app->irq_num < 0)
		return -EIO;

	if (copy_from_user(&data, (void __user *)argp,
			  sizeof(struct snps_accel_wait_irq)))
		return -EFAULT;

	add_wait_queue(&accel_app->wait, &wait);
	event_count = atomic_read(&accel_app->irq_event);
	if (data.timeout == 0)
		goto done_wirq;

	if (fpriv->handled_irq_event != event_count)
		goto done_wirq;

	set_current_state(TASK_INTERRUPTIBLE);
	if (schedule_timeout(msecs_to_jiffies(data.timeout)) == 0)
		ret = -ETIMEDOUT;

	__set_current_state(TASK_RUNNING);
	event_count = atomic_read(&accel_app->irq_event);

done_wirq:
	remove_wait_queue(&accel_app->wait, &wait);
	fpriv->handled_irq_event = data.count = event_count;
	if (copy_to_user((void __user *)argp, &data, sizeof(struct snps_accel_wait_irq)))
		return -EFAULT;

	return ret;
}

static int
snps_accel_do_dmabuf_alloc(struct snps_accel_file_priv *fpriv, char __user *argp)
{
	struct snps_accel_dmabuf_alloc data;
	struct snps_accel_mem_buffer *mbuf = NULL;

	if (copy_from_user(&data, (void __user *)argp,
			  sizeof(struct snps_accel_dmabuf_alloc)))
		return -EFAULT;

	mbuf = snps_accel_app_dmabuf_create(&fpriv->mem, data.size, data.flags);
	if (!mbuf)
		return -ENOMEM;

	data.fd = mbuf->fd;
	if (copy_to_user((void __user *)argp, &data, sizeof(data))) {
		snps_accel_app_dmabuf_release(mbuf);
		return -EFAULT;
	}

	return 0;
}

static int
snps_accel_do_dmabuf_info(char __user *argp)
{
	struct snps_accel_dmabuf_info data;
	int ret;

	if (copy_from_user(&data, (void __user *)argp,
			  sizeof(struct snps_accel_dmabuf_info)))
		return -EFAULT;

	ret = snps_accel_app_dmabuf_info(&data);
	if (ret)
		return ret;

	if (copy_to_user((void __user *)argp, &data, sizeof(data)))
		return -EFAULT;

	return 0;
}

static int
snps_accel_do_dmabuf_import(struct snps_accel_file_priv *fpriv, char __user *argp)
{
	struct snps_accel_dmabuf_import data;
	int ret;

	if (copy_from_user(&data, (void __user *)argp,
			  sizeof(struct snps_accel_dmabuf_import)))
		return -EFAULT;

	ret = snps_accel_app_dmabuf_import(&fpriv->mem, data.fd);
	if (ret)
		return ret;

	return 0;
}

static int
snps_accel_do_dmabuf_detach(struct snps_accel_file_priv *fpriv, char __user *argp)
{
	struct snps_accel_dmabuf_detach data;
	int ret;

	if (copy_from_user(&data, (void __user *)argp,
			  sizeof(struct snps_accel_dmabuf_detach)))
		return -EFAULT;

	ret = snps_accel_app_dmabuf_detach(&fpriv->mem, data.fd);
	if (ret)
		return ret;

	return 0;
}

static void file_priv_release(struct kref *ref)
{
	struct snps_accel_file_priv *fpriv = container_of(ref, struct snps_accel_file_priv, ref);

	kfree(fpriv);
}

void snps_accel_file_priv_get(struct snps_accel_file_priv *fpriv)
{
	kref_get(&fpriv->ref);
}

void snps_accel_file_priv_put(struct snps_accel_file_priv *fpriv)
{
	kref_put(&fpriv->ref, file_priv_release);
}

static int snps_accel_open(struct inode *inode, struct file *filp)
{
	struct cdev *cdev = inode->i_cdev;
	struct snps_accel_file_priv *fpriv;
	struct snps_accel_app *accel_app =
		container_of(cdev, struct snps_accel_app, cdev);

	fpriv = kzalloc(sizeof(*fpriv), GFP_KERNEL);
	if (!fpriv)
		return -ENOMEM;

	kref_init(&fpriv->ref);
	fpriv->app = accel_app;
	snps_accel_app_mem_init(accel_app->device, &fpriv->mem);

	filp->private_data = fpriv;

	return 0;
}

static int snps_accel_close(struct inode *inode, struct file *filp)
{
	struct snps_accel_file_priv *fpriv = (struct snps_accel_file_priv *)filp->private_data;

	flush_delayed_fput();
	snps_accel_app_release_import(&fpriv->mem);
	snps_accel_file_priv_put(fpriv);
	return 0;
}

static long
snps_accel_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct snps_accel_file_priv *fpriv = (struct snps_accel_file_priv *)filp->private_data;
	struct snps_accel_app *accel_app = fpriv->app;
	char __user *argp = (char __user *)arg;
	int err;

	switch (cmd) {
	case SNPS_ACCEL_IOCTL_INFO_SHMEM:
		err = snps_accel_info_shmem(accel_app, argp);
		break;
	case SNPS_ACCEL_IOCTL_INFO_NOTIFY:
		err = snps_accel_info_notify(accel_app, argp);
		break;
	case SNPS_ACCEL_IOCTL_WAIT_IRQ:
		err = snps_accel_wait_irq(fpriv, argp);
		break;
	case SNPS_ACCEL_IOCTL_DMABUF_ALLOC:
		err = snps_accel_do_dmabuf_alloc(fpriv, argp);
		break;
	case SNPS_ACCEL_IOCTL_DMABUF_INFO:
		err = snps_accel_do_dmabuf_info(argp);
		break;
	case SNPS_ACCEL_IOCTL_DMABUF_IMPORT:
		err = snps_accel_do_dmabuf_import(fpriv, argp);
		break;
	case SNPS_ACCEL_IOCTL_DMABUF_DETACH:
		err = snps_accel_do_dmabuf_detach(fpriv, argp);
		break;
	default:
		err = -ENOTTY;
		break;
	}

	return err;
}

static int snps_accel_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct snps_accel_file_priv *fpriv = (struct snps_accel_file_priv *)filp->private_data;
	struct snps_accel_app *accel_app = fpriv->app;
	int ret;
	u64 addr = vma->vm_pgoff << PAGE_SHIFT;
	size_t size = vma->vm_end - vma->vm_start;

	dev_dbg(accel_app->device, "mmap: start %lx end %lx pgoff %lx (%pap)\n",
		vma->vm_start, vma->vm_end, vma->vm_pgoff, &addr);

	if (addr == accel_app->shmem_base) {
		if (size != accel_app->shmem_size && size != PAGE_SIZE) {
			dev_dbg(accel_app->device, "Shared memory size mismatch\n");
			return -EINVAL;
		}
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		ret = remap_pfn_range(vma, vma->vm_start,
				      vma->vm_pgoff,
				      size,
				      vma->vm_page_prot);
	} else if (addr == accel_app->ctrl_base) {
		if (size != accel_app->ctrl_size && size != PAGE_SIZE) {
			dev_dbg(accel_app->device, "Notify memory size mismatch\n");
			return -EINVAL;
		}
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		ret = io_remap_pfn_range(vma, vma->vm_start,
					 vma->vm_pgoff,
					 size,
					 vma->vm_page_prot);
	} else {
		dev_dbg(accel_app->device, "Unsupported address to mmap %pap\n",
			&addr);
		return -EINVAL;
	}

	return ret;
}

static const struct file_operations snps_accel_app_fops = {
	.owner		= THIS_MODULE,
	.open		= snps_accel_open,
	.release	= snps_accel_close,
	.unlocked_ioctl	= snps_accel_ioctl,
	.compat_ioctl	= snps_accel_ioctl,
	.mmap		= snps_accel_mmap,
};

static int
snps_accel_get_ctrl_mem(struct device_node *node, struct resource *ctrl)
{
	int ret;
	struct device_node *np;

	/* Get control unit reference */
	np = of_parse_phandle(node, "snps,arcsync-ctrl", 0);
	if (!np)
		return -EINVAL;

	/* Get control unit registers base address */
	ret = of_address_to_resource(np, 0, ctrl);
	if (ret < 0)
		return ret;

	return 0;
}

static irqreturn_t snps_accel_app_irq_callback(int irq, void *dev)
{
	struct snps_accel_app *accel_app = dev;

	atomic_inc(&accel_app->irq_event);
	wake_up_interruptible(&accel_app->wait);

	return IRQ_HANDLED;
}

static int
snps_accel_init_ctrl_with_arcsync_fn(struct snps_accel_app *accel_app, struct device *arcsync_dev)
{
	const struct arcsync_funcs *arcsync_fn;
	struct snps_accel_ctrl_fn *ctrl_fn = &accel_app->ctrl.fn;

	arcsync_fn = arcsync_get_ctrl_fn(arcsync_dev);
	if (IS_ERR(arcsync_fn))
		return PTR_ERR(arcsync_fn);

	ctrl_fn->set_interrupt_callback = arcsync_fn->set_interrupt_callback;
	ctrl_fn->remove_interrupt_callback = arcsync_fn->remove_interrupt_callback;

	accel_app->ctrl.arcnet_id = arcsync_fn->get_arcnet_id(arcsync_dev);

	return 0;
}

static int
snps_accel_add_app(struct platform_device *pdev, struct device_node *node)
{
	int ret;
	struct snps_accel_app *accel_app;
	struct snps_accel_device *accel_dev = dev_get_drvdata(&pdev->dev);
	struct resource ctrl;
	struct resource shmem;

	ret = snps_accel_get_ctrl_mem(node, &ctrl);
	if (ret < 0) {
		dev_err(&pdev->dev, "ARCsync control unit MMIO is not found\n");
		/* Return 0 to skip this app */
		return 0;
	}

	ret = of_address_to_resource(node, 0, &shmem);
	if (ret < 0) {
		dev_err(&pdev->dev, "Shared memory is not found\n");
		/* Return 0 to skip this app */
		return 0;
	}

	accel_app = kzalloc(sizeof(*accel_app), GFP_KERNEL);
	if (!accel_app)
		return -ENOMEM;

	/* Get ARCsync device reference and init ctrl func struct with arcsync funcs */
	accel_app->ctrl.dev = arcsync_get_device_by_phandle(node, "snps,arcsync-ctrl");
	if (IS_ERR(accel_app->ctrl.dev)) {
		dev_err(&pdev->dev, "Failed to get ARCSync ref: %ld\n",
			PTR_ERR(accel_app->ctrl.dev));

		ret = PTR_ERR(accel_app->ctrl.dev);
		goto err_get_arcsync_dev;
	}
	ret = snps_accel_init_ctrl_with_arcsync_fn(accel_app, accel_app->ctrl.dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get ARCSync funcs\n");
		goto err_get_arcsync_dev;
	}

	cdev_init(&accel_app->cdev, &snps_accel_app_fops);
	accel_app->cdev.owner = THIS_MODULE;
	ret = cdev_add(&accel_app->cdev,
		       MKDEV(snps_accel_major, accel_dev->minor_count), MAX_DEVS);
	if (ret)
		goto err_cdev_add;

	accel_app->devt = MKDEV(snps_accel_major, accel_dev->minor_count);
	accel_app->device = device_create(snps_accel_class, &pdev->dev,
					  accel_app->devt,
					  accel_app,
					  DEV_NAME_FORMAT,
					  accel_app->ctrl.arcnet_id,
					  accel_dev->minor_count);
	if (IS_ERR(accel_app->device)) {
		dev_err(&pdev->dev, "Failed to create device /dev/snps/arcnet%d/hw%d\n",
			accel_app->ctrl.arcnet_id, accel_dev->minor_count);
		ret = PTR_ERR(accel_app->device);
		goto err_dev_create;
	}

	accel_app->device->dma_mask = pdev->dev.dma_mask;
	ret = dma_set_coherent_mask(accel_app->device, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(accel_app->device, "No suitable coherent DMA available\n");
		goto err_app_dev_init;
	}

	/* Add interrupt callback for ARCSync interrupt */
	accel_app->irq_num = of_irq_get(node, 0);
	if (accel_app->irq_num >= 0) {
		ret = accel_app->ctrl.fn.set_interrupt_callback(accel_app->ctrl.dev,
					accel_app->irq_num,
					snps_accel_app_irq_callback, accel_app);
		if (!ret) {
			init_waitqueue_head(&accel_app->wait);
			dev_dbg(accel_app->device, "App IRQ: %d\n", accel_app->irq_num);
		} else {
			dev_warn(accel_app->device, "Not ARCSync IRQ %d\n", accel_app->irq_num);
			accel_app->irq_num = -EINVAL;
		}
	} else {
		dev_warn(accel_app->device, "Notification IRQ not specified\n");
	}

	accel_app->ctrl_base = ctrl.start;
	accel_app->ctrl_size = resource_size(&ctrl);
	accel_app->shmem_base = shmem.start;
	accel_app->shmem_size = resource_size(&shmem);

	dev_dbg(accel_app->device, "Control region: start %pap size %pap\n",
		&accel_app->ctrl_base, &accel_app->ctrl_size);
	dev_dbg(accel_app->device, "Shared region: start %pap size %pap\n",
		&accel_app->shmem_base, &accel_app->shmem_size);

	accel_dev->minor_count++;
	list_add_tail(&accel_app->link, &accel_dev->devs_list);

	return 0;

err_app_dev_init:
	device_destroy(snps_accel_class, accel_app->devt);
err_dev_create:
	cdev_del(&accel_app->cdev);
err_cdev_add:
err_get_arcsync_dev:
	kfree(accel_app);
	return ret;
}

static int snps_accel_create_devs(struct platform_device *pdev)
{
	int ret;
	struct device_node *node = pdev->dev.of_node;

	do {
		node = of_find_compatible_node(node, NULL, "snps,accel-app");
		if (node) {
			ret = snps_accel_add_app(pdev, node);
			if (ret) {
				of_node_put(node);
				return ret;
			}
		}
	} while (node);

	return 0;
}

static void snps_accel_release_app(struct snps_accel_app *accel_app)
{
	const struct snps_accel_ctrl_fn *fn = &accel_app->ctrl.fn;

	if (accel_app->irq_num >= 0)
		fn->remove_interrupt_callback(accel_app->ctrl.dev,
					      accel_app->irq_num, accel_app);
	device_destroy(snps_accel_class, accel_app->devt);
	cdev_del(&accel_app->cdev);
}

static void snps_accel_release_devs(struct platform_device *pdev)
{
	struct snps_accel_device *accel_dev = dev_get_drvdata(&pdev->dev);
	struct snps_accel_app *cur, *n;

	list_for_each_entry_safe(cur, n, &accel_dev->devs_list, link) {
		if (cur->device)
			snps_accel_release_app(cur);

		list_del(&cur->link);
		kfree(cur);
	}
}

static int snps_accel_probe(struct platform_device *pdev)
{
	struct snps_accel_device *accel_dev;
	struct resource *res;
	int ret;

	accel_dev = devm_kzalloc(&pdev->dev, sizeof(*accel_dev), GFP_KERNEL);
	if (!accel_dev)
		return -ENOMEM;

	INIT_LIST_HEAD(&accel_dev->devs_list);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Shared memory is not defined\n");
		return -EINVAL;
	}
	accel_dev->shared_base = res->start;
	accel_dev->shared_size = resource_size(res);

	dev_set_drvdata(&pdev->dev, accel_dev);
	ret = snps_accel_create_devs(pdev);
	if (ret != 0) {
		snps_accel_release_devs(pdev);
		return ret;
	}

	return ret;
}

static int snps_accel_remove(struct platform_device *pdev)
{
	snps_accel_release_devs(pdev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id snps_accel_match[] = {
	{ .compatible = "snps,accel" },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, snps_accel_match);
#endif

static struct platform_driver snps_accel_platform_driver = {
	.probe = snps_accel_probe,
	.remove = snps_accel_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(snps_accel_match),
	},
};

static int __init snps_accel_init(void)
{
	int ret;
	dev_t dev;

	snps_accel_class = class_create(THIS_MODULE, "snps-accel");
	if (IS_ERR(snps_accel_class)) {
		ret = PTR_ERR(snps_accel_class);
		goto err_class;
	}

	ret = alloc_chrdev_region(&dev, 0, MAX_DEVS, DRIVER_NAME);
	if (ret)
		goto err_chr;

	snps_accel_major = MAJOR(dev);

	ret = platform_driver_register(&snps_accel_platform_driver);
	if (ret < 0)
		goto err_reg;

	return 0;

err_reg:
	unregister_chrdev_region(dev, MAX_DEVS);
err_chr:
	class_destroy(snps_accel_class);
err_class:
	return ret;
}
module_init(snps_accel_init);

static void __exit snps_accel_exit(void)
{
	platform_driver_unregister(&snps_accel_platform_driver);
	unregister_chrdev_region(MKDEV(snps_accel_major, 0), MAX_DEVS);
	class_destroy(snps_accel_class);
}
module_exit(snps_accel_exit);

MODULE_AUTHOR("Synopsys Inc.");
MODULE_DESCRIPTION("NPX/VPX driver");
MODULE_LICENSE("GPL v2");
