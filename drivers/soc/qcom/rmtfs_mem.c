// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 Linaro Ltd.
 */

#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/qcom_scm.h>

#define QCOM_RMTFS_MEM_DEV_MAX	(MINORMASK + 1)

static dev_t qcom_rmtfs_mem_major;

struct qcom_rmtfs_mem {
	struct device dev;
	struct cdev cdev;

	void *base;
	phys_addr_t addr;
	phys_addr_t size;

	unsigned int client_id;

	unsigned int perms;
};

static ssize_t qcom_rmtfs_mem_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf);

static DEVICE_ATTR(phys_addr, 0444, qcom_rmtfs_mem_show, NULL);
static DEVICE_ATTR(size, 0444, qcom_rmtfs_mem_show, NULL);
static DEVICE_ATTR(client_id, 0444, qcom_rmtfs_mem_show, NULL);

static ssize_t qcom_rmtfs_mem_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct qcom_rmtfs_mem *rmtfs_mem = container_of(dev,
							struct qcom_rmtfs_mem,
							dev);

	if (attr == &dev_attr_phys_addr)
		return sprintf(buf, "%pa\n", &rmtfs_mem->addr);
	if (attr == &dev_attr_size)
		return sprintf(buf, "%pa\n", &rmtfs_mem->size);
	if (attr == &dev_attr_client_id)
		return sprintf(buf, "%d\n", rmtfs_mem->client_id);

	return -EINVAL;
}

static struct attribute *qcom_rmtfs_mem_attrs[] = {
	&dev_attr_phys_addr.attr,
	&dev_attr_size.attr,
	&dev_attr_client_id.attr,
	NULL
};
ATTRIBUTE_GROUPS(qcom_rmtfs_mem);

static int qcom_rmtfs_mem_open(struct inode *inode, struct file *filp)
{
	struct qcom_rmtfs_mem *rmtfs_mem = container_of(inode->i_cdev,
							struct qcom_rmtfs_mem,
							cdev);

	get_device(&rmtfs_mem->dev);
	filp->private_data = rmtfs_mem;

	return 0;
}
static ssize_t qcom_rmtfs_mem_read(struct file *filp,
			      char __user *buf, size_t count, loff_t *f_pos)
{
	struct qcom_rmtfs_mem *rmtfs_mem = filp->private_data;

	if (*f_pos >= rmtfs_mem->size)
		return 0;

	if (*f_pos + count >= rmtfs_mem->size)
		count = rmtfs_mem->size - *f_pos;

	if (copy_to_user(buf, rmtfs_mem->base + *f_pos, count))
		return -EFAULT;

	*f_pos += count;
	return count;
}

static ssize_t qcom_rmtfs_mem_write(struct file *filp,
			       const char __user *buf, size_t count,
			       loff_t *f_pos)
{
	struct qcom_rmtfs_mem *rmtfs_mem = filp->private_data;

	if (*f_pos >= rmtfs_mem->size)
		return 0;

	if (*f_pos + count >= rmtfs_mem->size)
		count = rmtfs_mem->size - *f_pos;

	if (copy_from_user(rmtfs_mem->base + *f_pos, buf, count))
		return -EFAULT;

	*f_pos += count;
	return count;
}

static int qcom_rmtfs_mem_release(struct inode *inode, struct file *filp)
{
	struct qcom_rmtfs_mem *rmtfs_mem = filp->private_data;

	put_device(&rmtfs_mem->dev);

	return 0;
}

static struct class rmtfs_class = {
	.owner          = THIS_MODULE,
	.name           = "rmtfs",
};

static int qcom_rmtfs_mem_mmap(struct file *filep, struct vm_area_struct *vma)
{
	struct qcom_rmtfs_mem *rmtfs_mem = filep->private_data;

	if (vma->vm_end - vma->vm_start > rmtfs_mem->size) {
		dev_dbg(&rmtfs_mem->dev,
			"vm_end[%lu] - vm_start[%lu] [%lu] > mem->size[%pa]\n",
			vma->vm_end, vma->vm_start,
			(vma->vm_end - vma->vm_start), &rmtfs_mem->size);
		return -EINVAL;
	}

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	return remap_pfn_range(vma,
			       vma->vm_start,
			       rmtfs_mem->addr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

static const struct file_operations qcom_rmtfs_mem_fops = {
	.owner = THIS_MODULE,
	.open = qcom_rmtfs_mem_open,
	.read = qcom_rmtfs_mem_read,
	.write = qcom_rmtfs_mem_write,
	.release = qcom_rmtfs_mem_release,
	.llseek = default_llseek,
	.mmap = qcom_rmtfs_mem_mmap,
};

static void qcom_rmtfs_mem_release_device(struct device *dev)
{
	struct qcom_rmtfs_mem *rmtfs_mem = container_of(dev,
							struct qcom_rmtfs_mem,
							dev);

	kfree(rmtfs_mem);
}

static int qcom_rmtfs_mem_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct qcom_scm_vmperm perms[2];
	struct reserved_mem *rmem;
	struct qcom_rmtfs_mem *rmtfs_mem;
	u32 client_id;
	u32 vmid;
	int ret;

	rmem = of_reserved_mem_lookup(node);
	if (!rmem) {
		dev_err(&pdev->dev, "failed to acquire memory region\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "qcom,client-id", &client_id);
	if (ret) {
		dev_err(&pdev->dev, "failed to parse \"qcom,client-id\"\n");
		return ret;

	}

	rmtfs_mem = kzalloc(sizeof(*rmtfs_mem), GFP_KERNEL);
	if (!rmtfs_mem)
		return -ENOMEM;

	rmtfs_mem->addr = rmem->base;
	rmtfs_mem->client_id = client_id;
	rmtfs_mem->size = rmem->size;

	device_initialize(&rmtfs_mem->dev);
	rmtfs_mem->dev.parent = &pdev->dev;
	rmtfs_mem->dev.groups = qcom_rmtfs_mem_groups;
	rmtfs_mem->dev.release = qcom_rmtfs_mem_release_device;

	rmtfs_mem->base = devm_memremap(&rmtfs_mem->dev, rmtfs_mem->addr,
					rmtfs_mem->size, MEMREMAP_WC);
	if (IS_ERR(rmtfs_mem->base)) {
		dev_err(&pdev->dev, "failed to remap rmtfs_mem region\n");
		ret = PTR_ERR(rmtfs_mem->base);
		goto put_device;
	}

	cdev_init(&rmtfs_mem->cdev, &qcom_rmtfs_mem_fops);
	rmtfs_mem->cdev.owner = THIS_MODULE;

	dev_set_name(&rmtfs_mem->dev, "qcom_rmtfs_mem%d", client_id);
	rmtfs_mem->dev.id = client_id;
	rmtfs_mem->dev.class = &rmtfs_class;
	rmtfs_mem->dev.devt = MKDEV(MAJOR(qcom_rmtfs_mem_major), client_id);

	ret = cdev_device_add(&rmtfs_mem->cdev, &rmtfs_mem->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to add cdev: %d\n", ret);
		goto put_device;
	}

	ret = of_property_read_u32(node, "qcom,vmid", &vmid);
	if (ret < 0 && ret != -EINVAL) {
		dev_err(&pdev->dev, "failed to parse qcom,vmid\n");
		goto remove_cdev;
	} else if (!ret) {
		if (!qcom_scm_is_available()) {
			ret = -EPROBE_DEFER;
			goto remove_cdev;
		}

		perms[0].vmid = QCOM_SCM_VMID_HLOS;
		perms[0].perm = QCOM_SCM_PERM_RW;
		perms[1].vmid = vmid;
		perms[1].perm = QCOM_SCM_PERM_RW;

		rmtfs_mem->perms = BIT(QCOM_SCM_VMID_HLOS);
		ret = qcom_scm_assign_mem(rmtfs_mem->addr, rmtfs_mem->size,
					  &rmtfs_mem->perms, perms, 2);
		if (ret < 0) {
			dev_err(&pdev->dev, "assign memory failed\n");
			goto remove_cdev;
		}
	}

	dev_set_drvdata(&pdev->dev, rmtfs_mem);

	return 0;

remove_cdev:
	cdev_device_del(&rmtfs_mem->cdev, &rmtfs_mem->dev);
put_device:
	put_device(&rmtfs_mem->dev);

	return ret;
}

static int qcom_rmtfs_mem_remove(struct platform_device *pdev)
{
	struct qcom_rmtfs_mem *rmtfs_mem = dev_get_drvdata(&pdev->dev);
	struct qcom_scm_vmperm perm;

	if (rmtfs_mem->perms) {
		perm.vmid = QCOM_SCM_VMID_HLOS;
		perm.perm = QCOM_SCM_PERM_RW;

		qcom_scm_assign_mem(rmtfs_mem->addr, rmtfs_mem->size,
				    &rmtfs_mem->perms, &perm, 1);
	}

	cdev_device_del(&rmtfs_mem->cdev, &rmtfs_mem->dev);
	put_device(&rmtfs_mem->dev);

	return 0;
}

static const struct of_device_id qcom_rmtfs_mem_of_match[] = {
	{ .compatible = "qcom,rmtfs-mem" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_rmtfs_mem_of_match);

static struct platform_driver qcom_rmtfs_mem_driver = {
	.probe = qcom_rmtfs_mem_probe,
	.remove = qcom_rmtfs_mem_remove,
	.driver  = {
		.name  = "qcom_rmtfs_mem",
		.of_match_table = qcom_rmtfs_mem_of_match,
	},
};

static int __init qcom_rmtfs_mem_init(void)
{
	int ret;

	ret = class_register(&rmtfs_class);
	if (ret)
		return ret;

	ret = alloc_chrdev_region(&qcom_rmtfs_mem_major, 0,
				  QCOM_RMTFS_MEM_DEV_MAX, "qcom_rmtfs_mem");
	if (ret < 0) {
		pr_err("qcom_rmtfs_mem: failed to allocate char dev region\n");
		goto unregister_class;
	}

	ret = platform_driver_register(&qcom_rmtfs_mem_driver);
	if (ret < 0) {
		pr_err("qcom_rmtfs_mem: failed to register rmtfs_mem driver\n");
		goto unregister_chrdev;
	}

	return 0;

unregister_chrdev:
	unregister_chrdev_region(qcom_rmtfs_mem_major, QCOM_RMTFS_MEM_DEV_MAX);
unregister_class:
	class_unregister(&rmtfs_class);
	return ret;
}
module_init(qcom_rmtfs_mem_init);

static void __exit qcom_rmtfs_mem_exit(void)
{
	platform_driver_unregister(&qcom_rmtfs_mem_driver);
	unregister_chrdev_region(qcom_rmtfs_mem_major, QCOM_RMTFS_MEM_DEV_MAX);
	class_unregister(&rmtfs_class);
}
module_exit(qcom_rmtfs_mem_exit);

MODULE_AUTHOR("Linaro Ltd");
MODULE_DESCRIPTION("Qualcomm Remote Filesystem memory driver");
MODULE_LICENSE("GPL v2");
