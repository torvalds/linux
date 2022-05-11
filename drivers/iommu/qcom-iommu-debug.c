// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 *
 */

#define pr_fmt(fmt) "iommu-debug: %s: " fmt, __func__

#include <linux/debugfs.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/qcom-iommu-util.h>
#include "qcom-iommu-debug.h"

#define USECASE_SWITCH_TIMEOUT_MSECS (500)

static int iommu_debug_nr_iters_set(void *data, u64 val)
{
	struct iommu_debug_device *ddev = data;

	if (!val)
		val = 1;

	if (val > 10000)
		val = 10000;

	ddev->nr_iters = (u32)val;

	return 0;
}

static int iommu_debug_nr_iters_get(void *data, u64 *val)
{
	struct iommu_debug_device *ddev = data;

	*val = ddev->nr_iters;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(iommu_debug_nr_iters_fops,
			 iommu_debug_nr_iters_get,
			 iommu_debug_nr_iters_set,
			 "%llu\n");

int iommu_debug_check_mapping_flags(struct device *dev, dma_addr_t iova, size_t size,
				    phys_addr_t expected_pa, u32 flags)
{
	struct qcom_iommu_atos_txn txn;
	struct iommu_fwspec *fwspec;
	struct iommu_domain *domain;

	domain = iommu_get_domain_for_dev(dev);
	if (!domain) {
		dev_err(dev, "iommu_get_domain_for_dev() failed\n");
		return -EINVAL;
	}

	fwspec = dev_iommu_fwspec_get(dev);
	if (!fwspec) {
		dev_err(dev, "dev_iommu_fwspec_get() failed\n");
		return -EINVAL;
	}

	txn.addr = iova;
	txn.id = FIELD_GET(ARM_SMMU_SMR_ID, (fwspec->ids[0]));
	txn.flags = flags;

	size = PAGE_ALIGN(size);
	while (size) {
		phys_addr_t walk_pa, atos_pa;

		atos_pa = qcom_iommu_iova_to_phys_hard(domain, &txn);
		walk_pa = iommu_iova_to_phys(domain, iova);

		if (expected_pa != atos_pa || expected_pa != walk_pa) {
			dev_err_ratelimited(dev,
				"Bad translation for %pad! Expected: %pa Got: %pa (ATOS) %pa (Table Walk) sid=%08x\n",
				&iova, &expected_pa, &atos_pa, &walk_pa, txn.id);
			return -EINVAL;
		}

		size -= PAGE_SIZE;
		iova += PAGE_SIZE;
		expected_pa += PAGE_SIZE;
	}

	return 0;
}

int iommu_debug_check_mapping_sg_flags(struct device *dev, struct scatterlist *sgl,
				       unsigned int pgoffset, unsigned int dma_nents,
				       unsigned int nents, u32 flags)
{
	int ret;
	struct sg_page_iter piter;
	struct sg_dma_page_iter diter;

	for (__sg_page_iter_start(&piter, sgl, nents, pgoffset),
	     __sg_page_iter_start(&diter.base, sgl, dma_nents, pgoffset);
	     __sg_page_iter_next(&piter) && __sg_page_iter_dma_next(&diter);) {

		struct page *page = sg_page_iter_page(&piter);
		dma_addr_t dma_addr = sg_page_iter_dma_address(&diter);

		ret = iommu_debug_check_mapping_flags(dev, dma_addr, PAGE_SIZE,
						      page_to_phys(page), flags);
		if (ret)
			return ret;
	}

	return 0;
}

static void iommu_debug_destroy_test_dev(struct iommu_debug_device *ddev)
{
	if (ddev->test_dev) {
		of_platform_device_destroy(ddev->test_dev, NULL);
		ddev->test_dev = NULL;
		ddev->domain = NULL;
	}
}

/*
 * Returns struct device corresponding to the new usecase.
 * ddev->test_dev will change - caller must not use old value!
 * Caller must hold ddev->state_lock
 */
struct device *
iommu_debug_switch_usecase(struct iommu_debug_device *ddev, u32 usecase_nr)
{
	struct platform_device *test_pdev;
	struct device_node *child;
	int child_nr = 0;
	int ret;

	if (ddev->test_dev)
		iommu_debug_destroy_test_dev(ddev);

	if (usecase_nr >= of_get_child_count(ddev->self->of_node)) {
		dev_err(ddev->self, "Invalid usecase nr requested: %u\n",
			usecase_nr);
		return NULL;
	}

	reinit_completion(&ddev->probe_wait);
	for_each_child_of_node(ddev->self->of_node, child) {
		if (child_nr == usecase_nr)
			break;
		child_nr++;
	}

	test_pdev = of_platform_device_create(child, NULL, ddev->self);
	if (!test_pdev) {
		dev_err(ddev->self, "Creating platform device failed\n");
		return NULL;
	}

	/*
	 * Wait for child device's probe function to be called.
	 * Its very unlikely to be asynchonrous...
	 */
	ret = wait_for_completion_interruptible_timeout(&ddev->probe_wait,
						msecs_to_jiffies(USECASE_SWITCH_TIMEOUT_MSECS));
	if (ret <= 0) {
		dev_err(ddev->self, "Timed out waiting for usecase to register\n");
		goto out;
	}

	ddev->usecase_nr = usecase_nr;
	ddev->test_dev = &test_pdev->dev;
	ddev->domain = iommu_get_domain_for_dev(ddev->test_dev);
	if (!ddev->domain) {
		dev_err(ddev->self, "Oops, usecase not associated with iommu\n");
		goto out;
	}

	return ddev->test_dev;
out:
	iommu_debug_destroy_test_dev(ddev);
	return NULL;
}

/*
 * Caller must hold ddev->state_lock
 */
struct device *iommu_debug_usecase_reset(struct iommu_debug_device *ddev)
{
	return iommu_debug_switch_usecase(ddev, ddev->usecase_nr);
}

static int iommu_debug_usecase_register(struct device *dev)
{
	struct iommu_debug_device *ddev = dev_get_drvdata(dev->parent);

	complete(&ddev->probe_wait);
	return 0;
}

static ssize_t iommu_debug_usecase_read(struct file *file, char __user *ubuf,
					size_t count, loff_t *offset)
{
	struct iommu_debug_device *ddev = file->private_data;

	return simple_read_from_buffer(ubuf, count, offset, ddev->buffer,
				       strnlen(ddev->buffer, PAGE_SIZE));
}

static ssize_t iommu_debug_usecase_write(struct file *file, const char __user *ubuf,
					 size_t count, loff_t *offset)
{
	struct iommu_debug_device *ddev = file->private_data;
	unsigned int usecase_nr;
	int ret;

	ret = kstrtouint_from_user(ubuf, count, 0, &usecase_nr);
	if (ret || usecase_nr >= ddev->nr_children)
		return -EINVAL;

	mutex_lock(&ddev->state_lock);
	if (!iommu_debug_switch_usecase(ddev, usecase_nr)) {
		mutex_unlock(&ddev->state_lock);
		return -EINVAL;
	}
	mutex_unlock(&ddev->state_lock);

	return count;
}

static const struct file_operations iommu_debug_usecase_fops = {
	.open	 = simple_open,
	.read	 = iommu_debug_usecase_read,
	.write   = iommu_debug_usecase_write,
	.llseek	 = no_llseek,
};

static int iommu_debug_debugfs_setup(struct iommu_debug_device *ddev)
{
	struct dentry *dir;

	dir = debugfs_create_dir("iommu-test", NULL);
	if (IS_ERR(dir))
		return -EINVAL;

	ddev->root_dir = dir;

	debugfs_create_file("usecase", 0600, dir, ddev, &iommu_debug_usecase_fops);
	debugfs_create_file("functional_arm_dma_api", 0400, dir, ddev,
			    &iommu_debug_functional_arm_dma_api_fops);
	debugfs_create_file("functional_fast_dma_api", 0400, dir, ddev,
			    &iommu_debug_functional_fast_dma_api_fops);
	debugfs_create_file("atos", 0600, dir, ddev, &iommu_debug_atos_fops);
	debugfs_create_file("map", 0200, dir, ddev, &iommu_debug_map_fops);
	debugfs_create_file("unmap", 0200, dir, ddev, &iommu_debug_unmap_fops);
	debugfs_create_file("dma_map", 0200, dir, ddev, &iommu_debug_dma_map_fops);
	debugfs_create_file("dma_unmap", 0200, dir, ddev, &iommu_debug_dma_unmap_fops);
	debugfs_create_file("nr_iters", 0600, dir, ddev, &iommu_debug_nr_iters_fops);
	debugfs_create_file("test_virt_addr", 0400, dir, ddev, &iommu_debug_test_virt_addr_fops);
	debugfs_create_file("profiling", 0400, dir, ddev, &iommu_debug_profiling_fops);

	return 0;
}

static int iommu_debug_probe(struct platform_device *pdev)
{
	struct iommu_debug_device *ddev;
	struct device *dev = &pdev->dev;
	struct device_node *child;
	int ret;
	int offset = 0;

	ddev = devm_kzalloc(dev, sizeof(*ddev), GFP_KERNEL);
	if (!ddev)
		return -ENOMEM;

	ddev->self = dev;
	ddev->usecase_nr = U32_MAX;
	ddev->nr_iters = 1;
	mutex_init(&ddev->state_lock);
	init_completion(&ddev->probe_wait);

	ddev->buffer = devm_kzalloc(dev, PAGE_SIZE, GFP_KERNEL);
	if (!ddev->buffer) {
		ret = -ENOMEM;
		goto out;
	}

	ddev->nr_children = 0;
	for_each_child_of_node(dev->of_node, child) {
		offset += scnprintf(ddev->buffer + offset, PAGE_SIZE - offset,
				"%d: %s\n", ddev->nr_children, child->name);
		if (offset + 1 == PAGE_SIZE) {
			dev_err(dev, "Too many testcases?\n");
			break;
		}
		ddev->nr_children++;
	}
	dev_set_drvdata(dev, ddev);

	ret = iommu_debug_debugfs_setup(ddev);
	if (ret)
		goto out;

	return 0;

out:
	mutex_destroy(&ddev->state_lock);
	return ret;
}

static int iommu_debug_remove(struct platform_device *pdev)
{
	struct iommu_debug_device *ddev = platform_get_drvdata(pdev);

	debugfs_remove_recursive(ddev->root_dir);
	if (ddev->test_dev)
		of_platform_device_destroy(ddev->test_dev, NULL);

	mutex_destroy(&ddev->state_lock);
	return 0;
}

static const struct of_device_id iommu_debug_of_match[] = {
	{ .compatible = "qcom,iommu-debug-test" },
	{ },
};

static struct platform_driver iommu_debug_driver = {
	.probe = iommu_debug_probe,
	.remove = iommu_debug_remove,
	.driver = {
		.name = "qcom-iommu-debug",
		.of_match_table = iommu_debug_of_match,
	},
};

/*
 * This isn't really a "driver", we just need something in the device tree
 * to hook up to the `iommus' property.
 */
static int iommu_debug_usecase_probe(struct platform_device *pdev)
{
	return iommu_debug_usecase_register(&pdev->dev);
}

static const struct of_device_id iommu_debug_usecase_of_match[] = {
	{ .compatible = "qcom,iommu-debug-usecase" },
	{ },
};

static struct platform_driver iommu_debug_usecase_driver = {
	.probe = iommu_debug_usecase_probe,
	.driver = {
		.name = "qcom-iommu-debug-usecase",
		.of_match_table = iommu_debug_usecase_of_match,
	},
};

static int iommu_debug_init(void)
{
	int ret;

	ret = platform_driver_register(&iommu_debug_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&iommu_debug_usecase_driver);
	if (ret)
		platform_driver_unregister(&iommu_debug_driver);
	return ret;
}

static void iommu_debug_exit(void)
{
	platform_driver_unregister(&iommu_debug_usecase_driver);
	platform_driver_unregister(&iommu_debug_driver);
}

module_init(iommu_debug_init);
module_exit(iommu_debug_exit);

MODULE_LICENSE("GPL v2");
