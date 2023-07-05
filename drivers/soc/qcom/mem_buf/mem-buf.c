// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cdev.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mem-buf.h>
#include <linux/module.h>
#include <linux/of.h>

#include "mem-buf-gh.h"
#include "mem-buf-ids.h"

#define MEM_BUF_MAX_DEVS 1
static dev_t mem_buf_dev_no;
static struct class *mem_buf_class;
static struct cdev mem_buf_char_dev;

union mem_buf_ioctl_arg {
	struct mem_buf_alloc_ioctl_arg allocation;
	struct mem_buf_lend_ioctl_arg lend;
	struct mem_buf_retrieve_ioctl_arg retrieve;
	struct mem_buf_reclaim_ioctl_arg reclaim;
	struct mem_buf_share_ioctl_arg share;
	struct mem_buf_exclusive_owner_ioctl_arg get_ownership;
	struct mem_buf_get_memparcel_hdl_ioctl_arg get_memparcel_hdl;
};

static bool is_valid_mem_buf_perms(u32 mem_buf_perms)
{
	if (mem_buf_perms & ~MEM_BUF_PERM_VALID_FLAGS) {
		pr_err_ratelimited("%s: Invalid mem-buf permissions detected\n",
				   __func__);
		return false;
	}
	return true;
}

static int mem_buf_perms_to_perms(u32 mem_buf_perms)
{
	int perms = 0;

	if (!is_valid_mem_buf_perms(mem_buf_perms))
		return -EINVAL;

	if (mem_buf_perms & MEM_BUF_PERM_FLAG_READ)
		perms |= PERM_READ;
	if (mem_buf_perms & MEM_BUF_PERM_FLAG_WRITE)
		perms |= PERM_WRITE;
	if (mem_buf_perms & MEM_BUF_PERM_FLAG_EXEC)
		perms |= PERM_EXEC;

	return perms;
}

int mem_buf_acl_to_vmid_perms_list(unsigned int nr_acl_entries, const void __user *acl_entries,
				   int **dst_vmids, int **dst_perms)
{
	int ret, i, *vmids, *perms;
	struct acl_entry entry;

	if (!nr_acl_entries || !acl_entries)
		return -EINVAL;

	vmids = kmalloc_array(nr_acl_entries, sizeof(*vmids), GFP_KERNEL);
	if (!vmids)
		return -ENOMEM;

	perms = kmalloc_array(nr_acl_entries, sizeof(*perms), GFP_KERNEL);
	if (!perms) {
		kfree(vmids);
		return -ENOMEM;
	}

	for (i = 0; i < nr_acl_entries; i++) {
		ret = copy_struct_from_user(&entry, sizeof(entry),
					    acl_entries + (sizeof(entry) * i),
					    sizeof(entry));
		if (ret < 0)
			goto out;

		vmids[i] = mem_buf_fd_to_vmid(entry.vmid);
		perms[i] = mem_buf_perms_to_perms(entry.perms);
		if (vmids[i] < 0 || perms[i] < 0) {
			ret = -EINVAL;
			goto out;
		}
	}

	*dst_vmids = vmids;
	*dst_perms = perms;
	return ret;

out:
	kfree(perms);
	kfree(vmids);
	return ret;
}

static int mem_buf_lend_user(struct mem_buf_lend_ioctl_arg *uarg, bool is_lend)
{
	int *vmids, *perms;
	int ret;
	struct dma_buf *dmabuf;
	struct mem_buf_lend_kernel_arg karg = {0};

	if (!uarg->nr_acl_entries || !uarg->acl_list ||
	    uarg->nr_acl_entries > MEM_BUF_MAX_NR_ACL_ENTS ||
	    uarg->reserved0 || uarg->reserved1 || uarg->reserved2)
		return -EINVAL;

	dmabuf = dma_buf_get(uarg->dma_buf_fd);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	ret = mem_buf_acl_to_vmid_perms_list(uarg->nr_acl_entries,
			(void *)uarg->acl_list, &vmids, &perms);
	if (ret)
		goto err_acl;

	karg.nr_acl_entries = uarg->nr_acl_entries;
	karg.vmids = vmids;
	karg.perms = perms;

	if (is_lend) {
		ret = mem_buf_lend(dmabuf, &karg);
		if (ret)
			goto err_lend;
	} else {
		ret = mem_buf_share(dmabuf, &karg);
		if (ret)
			goto err_lend;
	}

	uarg->memparcel_hdl = karg.memparcel_hdl;
err_lend:
	kfree(perms);
	kfree(vmids);
err_acl:
	dma_buf_put(dmabuf);
	return ret;
}

static int mem_buf_reclaim_user(struct mem_buf_reclaim_ioctl_arg *uarg)
{
	struct dma_buf *dmabuf;
	int ret;

	if (uarg->reserved0 || uarg->reserved1 || uarg->reserved2)
		return -EINVAL;

	dmabuf = dma_buf_get(uarg->dma_buf_fd);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	ret = mem_buf_reclaim(dmabuf);
	dma_buf_put(dmabuf);
	return ret;
}

static int mem_buf_get_exclusive_ownership(struct mem_buf_exclusive_owner_ioctl_arg *uarg)
{
	struct dma_buf *dmabuf;
	int ret = 0;

	dmabuf = dma_buf_get(uarg->dma_buf_fd);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	if (IS_ERR(to_mem_buf_vmperm(dmabuf))) {
		ret = -EINVAL;
		goto put_dma_buf;
	}

	uarg->is_exclusive_owner = mem_buf_dma_buf_exclusive_owner(dmabuf);

put_dma_buf:
	dma_buf_put(dmabuf);

	return ret;
}

static int mem_buf_get_memparcel_hdl(struct mem_buf_get_memparcel_hdl_ioctl_arg *uarg)
{
	struct dma_buf *dmabuf;
	int ret = 0;
	gh_memparcel_handle_t memparcel_hdl;

	dmabuf = dma_buf_get(uarg->dma_buf_fd);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	ret = mem_buf_dma_buf_get_memparcel_hdl(dmabuf, &memparcel_hdl);
	if (ret) {
		ret = -EINVAL;
		goto put_dma_buf;
	}

	uarg->memparcel_hdl = memparcel_hdl;

put_dma_buf:
	dma_buf_put(dmabuf);

	return ret;
}

static long mem_buf_dev_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg)
{
	int fd;
	unsigned int dir = _IOC_DIR(cmd);
	union mem_buf_ioctl_arg ioctl_arg;

	if (_IOC_SIZE(cmd) > sizeof(ioctl_arg))
		return -EINVAL;

	if (copy_from_user(&ioctl_arg, (void __user *)arg, _IOC_SIZE(cmd)))
		return -EFAULT;

	if (!(dir & _IOC_WRITE))
		memset(&ioctl_arg, 0, sizeof(ioctl_arg));

	switch (cmd) {
	case MEM_BUF_IOC_ALLOC:
	{
		struct mem_buf_alloc_ioctl_arg *allocation =
			&ioctl_arg.allocation;

		if (!(mem_buf_capability & MEM_BUF_CAP_CONSUMER))
			return -EOPNOTSUPP;

		fd = mem_buf_alloc_fd(allocation);

		if (fd < 0)
			return fd;

		allocation->mem_buf_fd = fd;
		break;
	}
	case MEM_BUF_IOC_LEND:
	{
		struct mem_buf_lend_ioctl_arg *lend = &ioctl_arg.lend;
		int ret;

		ret = mem_buf_lend_user(lend, true);
		if (ret)
			return ret;

		break;
	}
	case MEM_BUF_IOC_RETRIEVE:
	{
		struct mem_buf_retrieve_ioctl_arg *retrieve =
			&ioctl_arg.retrieve;
		int ret;

		ret = mem_buf_retrieve_user(retrieve);
		if (ret)
			return ret;
		break;
	}
	case MEM_BUF_IOC_RECLAIM:
	{
		struct mem_buf_reclaim_ioctl_arg *reclaim =
			&ioctl_arg.reclaim;
		int ret;

		ret = mem_buf_reclaim_user(reclaim);
		if (ret)
			return ret;
		break;
	}
	case MEM_BUF_IOC_SHARE:
	{
		struct mem_buf_share_ioctl_arg *share = &ioctl_arg.share;
		int ret;

		/* The two formats are currently identical */
		ret = mem_buf_lend_user((struct mem_buf_lend_ioctl_arg *)share,
					 false);
		if (ret)
			return ret;

		break;
	}
	case MEM_BUF_IOC_EXCLUSIVE_OWNER:
	{
		struct mem_buf_exclusive_owner_ioctl_arg *get_ownership = &ioctl_arg.get_ownership;
		int ret;

		ret = mem_buf_get_exclusive_ownership(get_ownership);
		if (ret)
			return ret;

		break;
	}
	case MEM_BUF_IOC_GET_MEMPARCEL_HDL:
	{
		struct mem_buf_get_memparcel_hdl_ioctl_arg *get_memparcel_hdl;
		int ret;

		get_memparcel_hdl = &ioctl_arg.get_memparcel_hdl;
		ret = mem_buf_get_memparcel_hdl(get_memparcel_hdl);
		if (ret)
			return ret;

		break;
	}

	default:
		return -ENOTTY;
	}

	if (dir & _IOC_READ) {
		if (copy_to_user((void __user *)arg, &ioctl_arg,
				 _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	return 0;
}

static const struct file_operations mem_buf_dev_fops = {
	.unlocked_ioctl = mem_buf_dev_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static int mem_buf_msgq_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct device *class_dev;

	if (!mem_buf_dev)
		return -EPROBE_DEFER;

	ret = mem_buf_msgq_alloc(dev);
	if (ret)
		return ret;

	cdev_init(&mem_buf_char_dev, &mem_buf_dev_fops);
	ret = cdev_add(&mem_buf_char_dev, mem_buf_dev_no, MEM_BUF_MAX_DEVS);
	if (ret < 0)
		goto err_cdev_add;

	class_dev = device_create(mem_buf_class, NULL, mem_buf_dev_no, NULL,
				  "membuf");
	if (IS_ERR(class_dev)) {
		ret = PTR_ERR(class_dev);
		goto err_dev_create;
	}

	return 0;

err_dev_create:
	cdev_del(&mem_buf_char_dev);
err_cdev_add:
	mem_buf_msgq_free(dev);
	return ret;
}

static int mem_buf_msgq_remove(struct platform_device *pdev)
{
	device_destroy(mem_buf_class, mem_buf_dev_no);
	cdev_del(&mem_buf_char_dev);
	mem_buf_msgq_free(&pdev->dev);
	return 0;
}

static const struct of_device_id mem_buf_msgq_match_tbl[] = {
	 {.compatible = "qcom,mem-buf-msgq"},
	 {},
};

static struct platform_driver mem_buf_msgq_driver = {
	.probe = mem_buf_msgq_probe,
	.remove = mem_buf_msgq_remove,
	.driver = {
		.name = "mem-buf-msgq",
		.of_match_table = of_match_ptr(mem_buf_msgq_match_tbl),
	},
};

static int __init mem_buf_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&mem_buf_dev_no, 0, MEM_BUF_MAX_DEVS,
				  "membuf");
	if (ret < 0)
		goto err_chrdev_region;

	mem_buf_class = class_create(THIS_MODULE, "membuf");
	if (IS_ERR(mem_buf_class)) {
		ret = PTR_ERR(mem_buf_class);
		goto err_class_create;
	}

	ret = platform_driver_register(&mem_buf_msgq_driver);
	if (ret < 0)
		goto err_platform_drvr_register;

	return 0;

err_platform_drvr_register:
	class_destroy(mem_buf_class);
err_class_create:
	unregister_chrdev_region(mem_buf_dev_no, MEM_BUF_MAX_DEVS);
err_chrdev_region:
	return ret;
}
module_init(mem_buf_init);

static void __exit mem_buf_exit(void)
{
	platform_driver_unregister(&mem_buf_msgq_driver);
	class_destroy(mem_buf_class);
	unregister_chrdev_region(mem_buf_dev_no, MEM_BUF_MAX_DEVS);
}
module_exit(mem_buf_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Memory Buffer Sharing driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(DMA_BUF);
