// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/qcom-dma-mapping.h>
#include <linux/qcom-iommu-util.h>
#include "qcom-iommu-debug.h"

#ifdef CONFIG_64BIT
#define kstrtoux kstrtou64
#define kstrtox_from_user kstrtoull_from_user
#define kstrtosize_t kstrtoul
#else
#define kstrtoux kstrtou32
#define kstrtox_from_user kstrtouint_from_user
#define kstrtosize_t kstrtouint
#endif

static void *test_virt_addr;
static DEFINE_MUTEX(test_virt_addr_lock);

static ssize_t iommu_debug_dma_atos_read(struct file *file, char __user *ubuf,
					 size_t count, loff_t *offset)
{
	struct iommu_debug_device *ddev = file->private_data;
	struct iommu_fwspec *fwspec;
	phys_addr_t phys;
	char buf[100] = {0};
	struct qcom_iommu_atos_txn txn;
	int len;

	if (*offset)
		return 0;

	mutex_lock(&ddev->state_lock);
	if (!ddev->domain) {
		pr_err("%s: No domain. Have you selected a usecase?\n", __func__);
		mutex_unlock(&ddev->state_lock);
		return -EINVAL;
	}

	fwspec = dev_iommu_fwspec_get(ddev->test_dev);
	if (!fwspec) {
		pr_err("%s: No fwspec.\n", __func__);
		mutex_unlock(&ddev->state_lock);
		return 0;
	}

	txn.addr = ddev->iova;
	txn.flags = IOMMU_TRANS_DEFAULT;
	txn.id = FIELD_GET(ARM_SMMU_SMR_ID, fwspec->ids[0]);
	phys = qcom_iommu_iova_to_phys_hard(ddev->domain, &txn);
	if (!phys)
		len = strscpy(buf, "FAIL\n", sizeof(buf));
	else
		len = scnprintf(buf, sizeof(buf), "%pa\n", &phys);
	mutex_unlock(&ddev->state_lock);
	return simple_read_from_buffer(ubuf, count, offset, buf, len);
}

static ssize_t iommu_debug_atos_write(struct file *file,
				      const char __user *ubuf,
				      size_t count, loff_t *offset)
{
	struct iommu_debug_device *ddev = file->private_data;
	dma_addr_t iova;
	phys_addr_t phys;
	unsigned long pfn;

	mutex_lock(&ddev->state_lock);
	if (!ddev->domain) {
		pr_err("%s: No domain. Have you selected a usecase?\n", __func__);
		mutex_unlock(&ddev->state_lock);
		return -EINVAL;
	}

	if (kstrtox_from_user(ubuf, count, 0, &iova)) {
		dev_err(ddev->test_dev, "Invalid format for iova\n");
		ddev->iova = 0;
		mutex_unlock(&ddev->state_lock);
		return -EINVAL;
	}

	phys = iommu_iova_to_phys(ddev->domain, iova);
	pfn = __phys_to_pfn(phys);
	if (!pfn_valid(pfn)) {
		dev_err(ddev->test_dev, "Invalid ATOS operation page %pa\n", &phys);
		mutex_unlock(&ddev->state_lock);
		return -EINVAL;
	}
	ddev->iova = iova;

	mutex_unlock(&ddev->state_lock);
	pr_info("Saved iova=%pa for future ATOS commands\n", &iova);
	return count;
}

const struct file_operations iommu_debug_atos_fops = {
	.open	= simple_open,
	.write	= iommu_debug_atos_write,
	.read	= iommu_debug_dma_atos_read,
};

static ssize_t iommu_debug_map_write(struct file *file, const char __user *ubuf,
				     size_t count, loff_t *offset)
{
	ssize_t retval = -EINVAL;
	int ret;
	char *comma1, *comma2, *comma3;
	char buf[100] = {0};
	dma_addr_t iova;
	phys_addr_t phys;
	size_t size;
	int prot;
	struct iommu_debug_device *ddev = file->private_data;

	if (count >= 100) {
		pr_err_ratelimited("Value too large\n");
		return -EINVAL;
	}

	if (copy_from_user(buf, ubuf, count)) {
		pr_err_ratelimited("Couldn't copy from user\n");
		retval = -EFAULT;
	}

	comma1 = strnchr(buf, count, ',');
	if (!comma1)
		goto invalid_format;

	comma2 = strnchr(comma1 + 1, count, ',');
	if (!comma2)
		goto invalid_format;

	comma3 = strnchr(comma2 + 1, count, ',');
	if (!comma3)
		goto invalid_format;

	/* split up the words */
	*comma1 = *comma2 = *comma3 = '\0';

	if (kstrtoux(buf, 0, &iova))
		goto invalid_format;

	if (kstrtoux(comma1 + 1, 0, &phys))
		goto invalid_format;

	if (kstrtosize_t(comma2 + 1, 0, &size))
		goto invalid_format;

	if (kstrtoint(comma3 + 1, 0, &prot))
		goto invalid_format;

	mutex_lock(&ddev->state_lock);
	if (!ddev->domain) {
		pr_err_ratelimited("%s: No domain. Have you selected a usecase?\n", __func__);
		mutex_unlock(&ddev->state_lock);
		return -EINVAL;
	}

	ret = iommu_map(ddev->domain, iova, phys, size, prot);
	if (ret) {
		pr_err_ratelimited("iommu_map failed with %d\n", ret);
		retval = -EIO;
		goto out;
	}

	retval = count;
	pr_info("Mapped %pa to %pa (len=0x%zx, prot=0x%x)\n", &iova, &phys, size, prot);
out:
	mutex_unlock(&ddev->state_lock);
	return retval;

invalid_format:
	pr_err_ratelimited("Invalid format. Expected: iova,phys,len,prot where `prot' is the bitwise OR of IOMMU_READ, IOMMU_WRITE, etc.\n");
	return -EINVAL;
}

const struct file_operations iommu_debug_map_fops = {
	.open	= simple_open,
	.write	= iommu_debug_map_write,
};

static ssize_t iommu_debug_unmap_write(struct file *file,
				       const char __user *ubuf,
				       size_t count, loff_t *offset)
{
	ssize_t retval = 0;
	char *comma1;
	char buf[100] = {0};
	dma_addr_t iova;
	size_t size;
	size_t unmapped;
	struct iommu_debug_device *ddev = file->private_data;

	if (count >= 100) {
		pr_err_ratelimited("Value too large\n");
		return -EINVAL;
	}

	if (!ddev->domain) {
		pr_err_ratelimited("%s: No domain. Have you selected a usecase?\n", __func__);
		return -EINVAL;
	}

	if (copy_from_user(buf, ubuf, count)) {
		pr_err_ratelimited("Couldn't copy from user\n");
		retval = -EFAULT;
		goto out;
	}

	comma1 = strnchr(buf, count, ',');
	if (!comma1)
		goto invalid_format;

	/* split up the words */
	*comma1 = '\0';

	if (kstrtoux(buf, 0, &iova))
		goto invalid_format;

	if (kstrtosize_t(comma1 + 1, 0, &size))
		goto invalid_format;

	mutex_lock(&ddev->state_lock);
	if (!ddev->domain) {
		pr_err_ratelimited("No domain. Did you already attach?\n");
		mutex_unlock(&ddev->state_lock);
		return -EINVAL;
	}

	unmapped = iommu_unmap(ddev->domain, iova, size);
	if (unmapped != size) {
		pr_err_ratelimited("iommu_unmap failed. Expected to unmap: 0x%zx, unmapped: 0x%zx",
				   size, unmapped);
		retval = -EIO;
		goto out;
	}

	retval = count;
	pr_info("Unmapped %pa (len=0x%zx)\n", &iova, size);
out:
	mutex_unlock(&ddev->state_lock);
	return retval;

invalid_format:
	pr_err_ratelimited("Invalid format. Expected: iova,len\n");
	return -EINVAL;
}

const struct file_operations iommu_debug_unmap_fops = {
	.open	= simple_open,
	.write	= iommu_debug_unmap_write,
};

/*
 * Performs DMA mapping of a given virtual address and size to an iova address.
 * User input format: (addr,len,dma attr) where dma attr is:
 *				0: normal mapping
 *				1: force coherent mapping
 *				2: force non-cohernet mapping
 *				3: use system cache
 */
static ssize_t iommu_debug_dma_map_write(struct file *file,
					 const char __user *ubuf, size_t count, loff_t *offset)
{
	ssize_t retval = -EINVAL;
	int ret;
	char *comma1, *comma2;
	char buf[100] = {0};
	unsigned long addr;
	void *v_addr;
	dma_addr_t iova;
	size_t size;
	unsigned int attr;
	unsigned long dma_attrs;
	struct iommu_debug_device *ddev = file->private_data;
	struct device *dev = ddev->test_dev;

	if (count >= sizeof(buf)) {
		pr_err_ratelimited("Value too large\n");
		return -EINVAL;
	}

	if (copy_from_user(buf, ubuf, count)) {
		pr_err_ratelimited("Couldn't copy from user\n");
		return -EFAULT;
	}

	comma1 = strnchr(buf, count, ',');
	if (!comma1)
		goto invalid_format;

	comma2 = strnchr(comma1 + 1, count, ',');
	if (!comma2)
		goto invalid_format;

	*comma1 = *comma2 = '\0';

	if (kstrtoul(buf, 0, &addr))
		goto invalid_format;

	v_addr = (void *)addr;

	if (kstrtosize_t(comma1 + 1, 0, &size))
		goto invalid_format;

	if (kstrtouint(comma2 + 1, 0, &attr))
		goto invalid_format;

	mutex_lock(&test_virt_addr_lock);
	if (IS_ERR(test_virt_addr)) {
		mutex_unlock(&test_virt_addr_lock);
		goto allocation_failure;
	}

	if (!test_virt_addr) {
		mutex_unlock(&test_virt_addr_lock);
		goto missing_allocation;
	}
	mutex_unlock(&test_virt_addr_lock);

	if (v_addr < test_virt_addr || v_addr + size > test_virt_addr + SZ_1M)
		goto invalid_addr;

	if (attr == 0)
		dma_attrs = 0;
	else if (attr == 1)
		dma_attrs = DMA_ATTR_FORCE_COHERENT;
	else if (attr == 2)
		dma_attrs = DMA_ATTR_FORCE_NON_COHERENT;
	else if (attr == 3)
		dma_attrs = DMA_ATTR_SYS_CACHE_ONLY;
	else
		goto invalid_format;

	mutex_lock(&ddev->state_lock);
	if (!ddev->domain) {
		pr_err_ratelimited("%s: No domain. Have you selected a usecase?\n", __func__);
		mutex_unlock(&ddev->state_lock);
		return -EINVAL;
	}

	iova = dma_map_single_attrs(dev, v_addr, size, DMA_TO_DEVICE, dma_attrs);

	if (dma_mapping_error(dev, iova)) {
		pr_err_ratelimited("Failed to perform dma_map_single\n");
		ret = -EINVAL;
		goto out;
	}

	retval = count;
	pr_err_ratelimited("Mapped 0x%p to %pa (len=0x%zx)\n", v_addr, &iova, size);
	ddev->iova = iova;
	pr_err_ratelimited("Saved iova=%pa for future PTE commands\n", &iova);

out:
	mutex_unlock(&ddev->state_lock);
	return retval;

invalid_format:
	pr_err_ratelimited("Invalid format. Expected: addr,len,dma attr where 'dma attr' is\n0: normal mapping\n1: force coherent\n2: force non-cohernet\n3: use system cache\n");
	return retval;

invalid_addr:
	pr_err_ratelimited("Invalid addr given (0x%p)! Address should be within 1MB size from start addr returned by doing 'cat test_virt_addr'.\n",
			   v_addr);
	return retval;

allocation_failure:
	pr_err_ratelimited("Allocation of test_virt_addr failed.\n");
	return -ENOMEM;

missing_allocation:
	pr_err_ratelimited("Please attempt to do 'cat test_virt_addr'.\n");
	return retval;
}

static ssize_t iommu_debug_dma_map_read(struct file *file, char __user *ubuf,
					size_t count, loff_t *offset)
{
	struct iommu_debug_device *ddev = file->private_data;
	char buf[100] = {};
	dma_addr_t iova;
	int len;

	if (*offset)
		return 0;

	iova = ddev->iova;
	len = scnprintf(buf, sizeof(buf), "%pa\n", &iova);
	return simple_read_from_buffer(ubuf, count, offset, buf, len);
}

const struct file_operations iommu_debug_dma_map_fops = {
	.open	= simple_open,
	.read	= iommu_debug_dma_map_read,
	.write	= iommu_debug_dma_map_write,
};

static ssize_t iommu_debug_dma_unmap_write(struct file *file, const char __user *ubuf,
					   size_t count, loff_t *offset)
{
	ssize_t retval = 0;
	char *comma1, *comma2;
	char buf[100] = {};
	size_t size;
	unsigned int attr;
	dma_addr_t iova;
	unsigned long dma_attrs;
	struct iommu_debug_device *ddev = file->private_data;
	struct device *dev = ddev->test_dev;

	if (count >= sizeof(buf)) {
		pr_err_ratelimited("Value too large\n");
		return -EINVAL;
	}

	if (copy_from_user(buf, ubuf, count)) {
		pr_err_ratelimited("Couldn't copy from user\n");
		retval = -EFAULT;
		goto out;
	}

	comma1 = strnchr(buf, count, ',');
	if (!comma1)
		goto invalid_format;

	comma2 = strnchr(comma1 + 1, count, ',');
	if (!comma2)
		goto invalid_format;

	*comma1 = *comma2 = '\0';

	if (kstrtoux(buf, 0, &iova))
		goto invalid_format;

	if (kstrtosize_t(comma1 + 1, 0, &size))
		goto invalid_format;

	if (kstrtouint(comma2 + 1, 0, &attr))
		goto invalid_format;

	if (attr == 0)
		dma_attrs = 0;
	else if (attr == 1)
		dma_attrs = DMA_ATTR_FORCE_COHERENT;
	else if (attr == 2)
		dma_attrs = DMA_ATTR_FORCE_NON_COHERENT;
	else if (attr == 3)
		dma_attrs = DMA_ATTR_SYS_CACHE_ONLY;
	else
		goto invalid_format;

	mutex_lock(&ddev->state_lock);
	if (!ddev->domain) {
		pr_err_ratelimited("%s: No domain. Have you selected a usecase?\n", __func__);
		mutex_unlock(&ddev->state_lock);
		return -EINVAL;
	}
	dma_unmap_single_attrs(dev, iova, size, DMA_TO_DEVICE, dma_attrs);

	retval = count;
	pr_err_ratelimited("Unmapped %pa (len=0x%zx)\n", &iova, size);
out:
	mutex_unlock(&ddev->state_lock);
	return retval;

invalid_format:
	pr_err_ratelimited("Invalid format. Expected: iova,len, dma attr\n");
	return -EINVAL;
}

const struct file_operations iommu_debug_dma_unmap_fops = {
	.open	= simple_open,
	.write	= iommu_debug_dma_unmap_write,
};

static int iommu_debug_build_phoney_sg_table(struct device *dev,
					     struct sg_table *table,
					     unsigned long total_size,
					     unsigned long chunk_size)
{
	unsigned long nents = total_size / chunk_size;
	struct scatterlist *sg;
	int i, j;
	struct page *page;

	if (!IS_ALIGNED(total_size, PAGE_SIZE))
		return -EINVAL;
	if (!IS_ALIGNED(total_size, chunk_size))
		return -EINVAL;
	if (sg_alloc_table(table, nents, GFP_KERNEL))
		return -EINVAL;

	for_each_sg(table->sgl, sg, table->nents, i) {
		page = alloc_pages(GFP_KERNEL, get_order(chunk_size));
		if (!page)
			goto free_pages;
		sg_set_page(sg, page, chunk_size, 0);
	}

	return 0;
free_pages:
	for_each_sg(table->sgl, sg, i--, j)
		__free_pages(sg_page(sg), get_order(chunk_size));
	sg_free_table(table);
	return -ENOMEM;
}

static void iommu_debug_destroy_phoney_sg_table(struct device *dev,
						struct sg_table *table,
						unsigned long chunk_size)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(table->sgl, sg, table->nents, i)
		__free_pages(sg_page(sg), get_order(chunk_size));
	sg_free_table(table);
}

#define ps_printf(name, s, fmt, ...) ({						\
			pr_err("%s: " fmt, name, ##__VA_ARGS__);		\
			seq_printf(s, fmt, ##__VA_ARGS__);			\
		})

static int __functional_dma_api_alloc_test(struct device *dev,
					   struct seq_file *s,
					   struct iommu_domain *domain,
					   void *ignored)
{
	size_t size = SZ_1K * 742;
	int ret = 0;
	u8 *data;
	dma_addr_t iova;

	/* Make sure we can allocate and use a buffer */
	ps_printf(dev_name(dev), s, "Allocating coherent buffer");
	data = dma_alloc_coherent(dev, size, &iova, GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
	} else {
		int i;

		ps_printf(dev_name(dev), s, "  -> SUCCEEDED\n");
		ps_printf(dev_name(dev), s, "Using coherent buffer");
		for (i = 0; i < 742; ++i) {
			int ind = SZ_1K * i;
			u8 *p = data + ind;
			u8 val = i % 255;

			memset(data, 0xa5, size);
			*p = val;
			(*p)++;
			if ((*p) != val + 1) {
				ps_printf(dev_name(dev), s,
					  "  -> FAILED on iter %d since %d != %d\n",
					  i, *p, val + 1);
				ret = -EINVAL;
			}
		}
		if (!ret)
			ps_printf(dev_name(dev), s, "  -> SUCCEEDED\n");
		dma_free_coherent(dev, size, data, iova);
	}

	return ret;
}

static int __functional_dma_api_basic_test(struct device *dev,
					   struct seq_file *s,
					   struct iommu_domain *domain,
					   void *ignored)
{
	size_t size = 1518;
	int i, j, ret = 0;
	u8 *data;
	dma_addr_t iova;

	ps_printf(dev_name(dev), s, "Basic DMA API test");
	/* Make sure we can allocate and use a buffer */
	for (i = 0; i < 1000; ++i) {
		data = kmalloc(size, GFP_KERNEL);
		if (!data) {
			ret = -ENOMEM;
			goto out;
		}
		memset(data, 0xa5, size);
		iova = dma_map_single(dev, data, size, DMA_TO_DEVICE);
		ret = iommu_debug_check_mapping_fast(dev, iova, size, virt_to_phys(data));
		if (ret)
			goto out;

		dma_unmap_single(dev, iova, size, DMA_TO_DEVICE);
		for (j = 0; j < size; ++j) {
			if (data[j] != 0xa5) {
				dev_err_ratelimited(dev, "data[%d] != 0xa5\n", data[j]);
				ret = -EINVAL;
				goto out;
			}
		}
		kfree(data);
	}

out:
	if (ret)
		ps_printf(dev_name(dev), s, "  -> FAILED\n");
	else
		ps_printf(dev_name(dev), s, "  -> SUCCEEDED\n");

	return ret;
}

static int __functional_dma_api_map_sg_test(struct device *dev, struct seq_file *s,
					    struct iommu_domain *domain, size_t sizes[])
{
	const size_t *sz;
	int ret = 0, count = 0;

	ps_printf(dev_name(dev), s, "Map SG DMA API test");

	for (sz = sizes; *sz; ++sz) {
		size_t size = *sz;
		struct sg_table table;
		unsigned long chunk_size = SZ_4K;

		/* Build us a table */
		ret = iommu_debug_build_phoney_sg_table(dev, &table, size, chunk_size);
		if (ret) {
			seq_puts(s, "couldn't build phoney sg table! bailing...\n");
			goto out;
		}

		count = dma_map_sg(dev, table.sgl, table.nents, DMA_BIDIRECTIONAL);
		if (!count) {
			ret = -EINVAL;
			goto destroy_table;
		}

		/* Check mappings... */
		ret = iommu_debug_check_mapping_sg_fast(dev, table.sgl, 0, table.nents, count);

		dma_unmap_sg(dev, table.sgl, table.nents, DMA_BIDIRECTIONAL);
destroy_table:
		iommu_debug_destroy_phoney_sg_table(dev, &table, chunk_size);
	}
out:
	if (ret)
		ps_printf(dev_name(dev), s, "  -> FAILED\n");
	else
		ps_printf(dev_name(dev), s, "  -> SUCCEEDED\n");

	return ret;
}

static int iommu_debug_functional_arm_dma_api_show(struct seq_file *s,
						   void *ignored)
{
	struct iommu_debug_device *ddev = s->private;
	struct device *dev;
	size_t sizes[] = {SZ_4K, SZ_64K, SZ_2M, SZ_1M * 12, 0};
	int ret = -EINVAL;

	mutex_lock(&ddev->state_lock);
	if (!iommu_debug_usecase_reset(ddev))
		goto out;
	dev = ddev->test_dev;

	ret = __functional_dma_api_alloc_test(dev, s, ddev->domain, sizes);
	ret |= __functional_dma_api_basic_test(dev, s, ddev->domain, sizes);
	ret |= __functional_dma_api_map_sg_test(dev, s, ddev->domain, sizes);

out:
	mutex_unlock(&ddev->state_lock);
	if (ret)
		seq_printf(s, "FAIL %d\n", ret);
	else
		seq_puts(s, "SUCCESS\n");

	return 0;
}

static int iommu_debug_functional_arm_dma_api_open(struct inode *inode,
						   struct file *file)
{
	return single_open(file, iommu_debug_functional_arm_dma_api_show,
			   inode->i_private);
}

const struct file_operations iommu_debug_functional_arm_dma_api_fops = {
	.open	 = iommu_debug_functional_arm_dma_api_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

/* Creates a fresh fast mapping and applies @fn to it */
static int __apply_to_new_mapping(struct seq_file *s,
				  int (*fn)(struct device *dev,
					    struct seq_file *s,
					    struct iommu_domain *domain,
					    void *priv),
				  void *priv)
{
	struct iommu_domain *domain;
	struct iommu_debug_device *ddev = s->private;
	struct device *dev;
	int ret = -EINVAL;

	mutex_lock(&ddev->state_lock);
	if (!iommu_debug_usecase_reset(ddev))
		goto out;

	domain = ddev->domain;
	dev = ddev->test_dev;

	ret = fn(dev, s, domain, priv);

out:
	mutex_unlock(&ddev->state_lock);
	seq_printf(s, "%s\n", ret ? "FAIL" : "SUCCESS");
	return ret;
}

static const char * const _size_to_string(unsigned long size)
{
	switch (size) {
	case SZ_4K:
		return "4K";
	case SZ_8K:
		return "8K";
	case SZ_16K:
		return "16K";
	case SZ_64K:
		return "64K";
	case SZ_1M:
		return "1M";
	case SZ_2M:
		return "2M";
	case SZ_1M * 12:
		return "12M";
	case SZ_1M * 20:
		return "20M";
	case SZ_1M * 24:
		return "24M";
	case SZ_1M * 32:
		return "32M";
	}

	pr_err("unknown size, please add to %s\n", __func__);
	return "unknown size";
}

static int __check_mapping(struct device *dev, struct iommu_domain *domain,
			   dma_addr_t iova, phys_addr_t expected)
{
	struct iommu_fwspec *fwspec;
	phys_addr_t res, res2;
	struct qcom_iommu_atos_txn txn;

	fwspec = dev_iommu_fwspec_get(dev);
	if (!fwspec) {
		dev_err_ratelimited(dev, "No fwspec.\n");
		return -EINVAL;
	}

	txn.addr = iova;
	txn.flags = IOMMU_TRANS_DEFAULT;
	txn.id = FIELD_GET(ARM_SMMU_SMR_ID, fwspec->ids[0]);

	res = qcom_iommu_iova_to_phys_hard(domain, &txn);
	res2 = iommu_iova_to_phys(domain, iova);

	WARN(res != res2, "hard/soft iova_to_phys fns don't agree...");

	if (res != expected) {
		dev_err_ratelimited(dev, "Bad translation for %pa! Expected: %pa Got: %pa\n",
				    &iova, &expected, &res);
		return -EINVAL;
	}

	return 0;
}

static int __full_va_sweep(struct device *dev, struct seq_file *s,
			   struct iommu_domain *domain, void *priv)
{
	u64 iova;
	int nr_maps = 0;
	dma_addr_t dma_addr;
	void *virt;
	phys_addr_t phys;
	const u64 max = SZ_1G * 4ULL - 1;
	int ret = 0, i;
	const size_t size = (size_t)priv;
	unsigned long theiova;

	virt = (void *)__get_free_pages(GFP_KERNEL, get_order(size));
	if (!virt) {
		if (size > SZ_8K) {
			dev_err_ratelimited(dev, "Failed to allocate %s of memory, which is a lot. Skipping test for this size\n",
					    _size_to_string(size));
			return 0;
		}
		return -ENOMEM;
	}
	phys = virt_to_phys(virt);

	for (iova = 0, i = 0; iova < max; iova += size, ++i) {
		unsigned long expected = iova;

		if (iova == MSI_IOVA_BASE) {
			iova = MSI_IOVA_BASE + MSI_IOVA_LENGTH - size;
			continue;
		}

		dma_addr = dma_map_single(dev, virt, size, DMA_TO_DEVICE);
		if (dma_addr != expected) {
			dev_err_ratelimited(dev, "Unexpected iova on iter %d (expected: 0x%lx got: 0x%lx)\n",
					    i, expected, (unsigned long)dma_addr);
			ret = -EINVAL;
			if (!dma_mapping_error(dev, dma_addr))
				dma_unmap_single(dev, dma_addr, size,
					DMA_TO_DEVICE);
			goto out;
		}
		nr_maps++;
	}

	if (domain) {
		/* check every mapping from 0..6M */
		for (iova = 0, i = 0; iova < SZ_2M * 3; iova += size, ++i) {
			phys_addr_t expected = phys;

			if (__check_mapping(dev, domain, iova, expected)) {
				dev_err_ratelimited(dev, "iter: %d\n", i);
				ret = -EINVAL;
				goto out;
			}
		}
		/* and from 4G..4G-6M */
		for (iova = 0, i = 0; iova < SZ_2M * 3; iova += size, ++i) {
			phys_addr_t expected = phys;

			if (iova == MSI_IOVA_BASE) {
				iova = MSI_IOVA_BASE + MSI_IOVA_LENGTH - size;
				continue;
			}
			theiova = ((SZ_1G * 4ULL) - size) - iova;

			if (__check_mapping(dev, domain, theiova, expected)) {
				dev_err_ratelimited(dev, "iter: %d\n", i);
				ret = -EINVAL;
				goto out;
			}
		}
	}

	/* at this point, our VA space should be full */
	dma_addr = dma_map_single(dev, virt, size, DMA_TO_DEVICE);
	if (dma_addr != DMA_MAPPING_ERROR) {
		dev_err_ratelimited(dev, "dma_map_single succeeded when it should have failed. Got iova: 0x%lx\n",
				    (unsigned long)dma_addr);
		ret = -EINVAL;
	}

out:
	for (iova = 0; iova < max && nr_maps--; iova += size) {
		if (iova == MSI_IOVA_BASE) {
			iova = MSI_IOVA_BASE + MSI_IOVA_LENGTH - size;
			continue;
		}
		dma_unmap_single(dev, (dma_addr_t)iova, size, DMA_TO_DEVICE);
	}

	free_pages((unsigned long)virt, get_order(size));
	return ret;
}

static int __tlb_stress_sweep(struct device *dev, struct seq_file *s,
			      struct iommu_domain *domain, void *unused)
{
	int i, ret = 0;
	int nr_maps = 0;
	u64 iova;
	u64 first_iova = 0;
	const u64  max = SZ_1G * 4ULL - 1;
	void *virt;
	phys_addr_t phys;
	dma_addr_t dma_addr;

	/*
	 * we'll be doing 4K and 8K mappings.  Need to own an entire 8K
	 * chunk that we can work with.
	 */
	virt = (void *)__get_free_pages(GFP_KERNEL, get_order(SZ_8K));
	phys = virt_to_phys(virt);

	/* fill the whole 4GB space */
	for (iova = 0, i = 0; iova < max; iova += SZ_8K, ++i) {
		if (iova == MSI_IOVA_BASE) {
			iova = MSI_IOVA_BASE + MSI_IOVA_LENGTH - SZ_8K;
			continue;
		}

		dma_addr = dma_map_single(dev, virt, SZ_8K, DMA_TO_DEVICE);
		if (dma_addr == DMA_MAPPING_ERROR) {
			dev_err_ratelimited(dev, "Failed map on iter %d\n", i);
			ret = -EINVAL;
			goto out;
		} else if (dma_addr != iova) {
			dma_unmap_single(dev, dma_addr, SZ_8K, DMA_TO_DEVICE);
			dev_err_ratelimited(dev, "Failed map on iter %d\n", i);
			ret = -EINVAL;
			goto out;
		}
		nr_maps++;
	}

	if (dma_map_single(dev, virt, SZ_4K, DMA_TO_DEVICE) != DMA_MAPPING_ERROR) {
		dev_err_ratelimited(dev, "dma_map_single unexpectedly (VA should have been exhausted)\n");
		ret = -EINVAL;
		goto out;
	}

	/*
	 * free up 4K at the very beginning, then leave one 4K mapping,
	 * then free up 8K.  This will result in the next 8K map to skip
	 * over the 4K hole and take the 8K one.
	 * i.e
	 *	 0K..4K	  Hole
	 *	 4K..8K	  Map R1
	 *	 8K..12K  Hole
	 *	12K..4G   Map R2
	 */
	dma_unmap_single(dev, 0, SZ_4K, DMA_TO_DEVICE);
	dma_unmap_single(dev, SZ_8K, SZ_4K, DMA_TO_DEVICE);
	dma_unmap_single(dev, SZ_8K + SZ_4K, SZ_4K, DMA_TO_DEVICE);

	/* remap 8K */
	dma_addr = dma_map_single(dev, virt, SZ_8K, DMA_TO_DEVICE);
	if (dma_addr != SZ_8K) {
		dma_addr_t expected = SZ_8K;

		dev_err_ratelimited(dev, "Unexpected dma_addr. got: %pa expected: %pa\n",
				    &dma_addr, &expected);

		/* To simplify error handling, unmap the 4K regions (4K..8K
		 * and 12K..16K) here and the rest (16K..4G) in 8K increments
		 * in the for loop.
		 */
		dma_unmap_single(dev, SZ_4K, SZ_4K, DMA_TO_DEVICE);
		dma_unmap_single(dev, SZ_8K+SZ_4K, SZ_4K, DMA_TO_DEVICE);
		nr_maps -= 2;
		first_iova = SZ_8K + SZ_8K;

		ret = -EINVAL;
		goto out;
	}

	/*
	 * Now we have 0..4K hole and 4K..4G mapped.
	 * Remap 4K.  We should get the first 4K chunk that was skipped
	 * over during the previous 8K map.  If we missed a TLB invalidate
	 * at that point this should explode.
	 */
	dma_addr = dma_map_single(dev, virt, SZ_4K, DMA_TO_DEVICE);
	if (dma_addr != 0) {
		dma_addr_t expected = 0;

		dev_err_ratelimited(dev, "Unexpected dma_addr. got: %pa expected: %pa\n",
				    &dma_addr, &expected);
		/* To simplify error handling, unmap the 4K region (4K..8K)
		 * here and rest (8K..4G) in 8K increments in the for loop.
		 */
		dma_unmap_single(dev, SZ_4K, SZ_4K, DMA_TO_DEVICE);
		first_iova = SZ_8K;
		nr_maps -= 1;
		ret = -EINVAL;
		goto out;
	}

	first_iova = 0;

	if (dma_map_single(dev, virt, SZ_4K, DMA_TO_DEVICE) != DMA_MAPPING_ERROR) {
		dev_err_ratelimited(dev, "dma_map_single unexpectedly after remaps (VA should have been exhausted)\n");
		ret = -EINVAL;
		goto out;
	}

out:
	/* we're all full again. unmap everything. */
	for (iova = first_iova; iova < max && nr_maps--; iova += SZ_8K) {
		if (iova == MSI_IOVA_BASE) {
			iova = MSI_IOVA_BASE + MSI_IOVA_LENGTH - SZ_8K;
			continue;
		}
		dma_unmap_single(dev, (dma_addr_t)iova, SZ_8K, DMA_TO_DEVICE);
	}

	free_pages((unsigned long)virt, get_order(SZ_8K));
	return ret;
}

struct fib_state {
	unsigned long cur;
	unsigned long prev;
};

static void fib_init(struct fib_state *f)
{
	f->cur = f->prev = 1;
}

static unsigned long get_next_fib(struct fib_state *f)
{
	int next = f->cur + f->prev;

	f->prev = f->cur;
	f->cur = next;
	return next;
}

/*
 * Not actually random.  Just testing the fibs (and max - the fibs).
 */
static int __rand_va_sweep(struct device *dev, struct seq_file *s,
			   struct iommu_domain *domain, void *priv)
{
	u64 iova;
	const u64 max = SZ_1G * 4ULL - 1;
	int i, remapped, unmapped, ret = 0;
	int nr_maps = 0;
	void *virt;
	dma_addr_t dma_addr, dma_addr2;
	struct fib_state fib;
	const size_t size = (size_t)priv;

	virt = (void *)__get_free_pages(GFP_KERNEL, get_order(size));
	if (!virt) {
		if (size > SZ_8K) {
			dev_err_ratelimited(dev, "Failed to allocate %s of memory, which is a lot. Skipping test for this size\n",
					    _size_to_string(size));
			return 0;
		}
		return -ENOMEM;
	}

	/* fill the whole 4GB space */
	for (iova = 0, i = 0; iova < max; iova += size, ++i) {
		if (iova == MSI_IOVA_BASE) {
			iova = MSI_IOVA_BASE + MSI_IOVA_LENGTH - size;
			continue;
		}

		dma_addr = dma_map_single(dev, virt, size, DMA_TO_DEVICE);
		if (dma_addr == DMA_MAPPING_ERROR) {
			dev_err_ratelimited(dev, "Failed map on iter %d\n", i);
			ret = -EINVAL;
			goto out;
		} else if (dma_addr != iova) {
			dma_unmap_single(dev, dma_addr, size, DMA_TO_DEVICE);
			dev_err_ratelimited(dev, "Unexpected dma_addr. got: %lx, expected: %lx\n",
				(unsigned long)dma_addr, (unsigned long)iova);
			ret = -EINVAL;
			goto out;
		}
		nr_maps++;
	}

	/* now unmap "random" iovas */
	unmapped = 0;
	fib_init(&fib);
	for (iova = get_next_fib(&fib) * size;
	     iova < max - size;
	     iova = (u64)get_next_fib(&fib) * size) {
		dma_addr = (dma_addr_t)(iova);
		dma_addr2 = (dma_addr_t)((max + 1) - size - iova);
		if (dma_addr == dma_addr2) {
			WARN(1, "%s test needs update! The random number sequence is folding in on itself and should be changed.\n",
			     __func__);
			return -EINVAL;
		}

		if (!(MSI_IOVA_BASE <= dma_addr && MSI_IOVA_BASE + MSI_IOVA_LENGTH > dma_addr))
			dma_unmap_single(dev, dma_addr, size, DMA_TO_DEVICE);

		if (!(MSI_IOVA_BASE <= dma_addr2 && MSI_IOVA_BASE + MSI_IOVA_LENGTH > dma_addr2))
			dma_unmap_single(dev, dma_addr2, size, DMA_TO_DEVICE);

		unmapped += 2;
	}

	/* and map until everything fills back up */
	for (remapped = 0; ; ++remapped) {
		dma_addr = dma_map_single(dev, virt, size, DMA_TO_DEVICE);
		if (dma_addr == DMA_MAPPING_ERROR)
			break;
	}

	if (unmapped != remapped) {
		dev_err_ratelimited(dev, "Unexpected random remap count! Unmapped %d but remapped %d\n",
				    unmapped, remapped);
		ret = -EINVAL;
	}

out:
	for (iova = 0; iova < max && nr_maps--; iova += size) {
		if (iova == MSI_IOVA_BASE) {
			iova = MSI_IOVA_BASE + MSI_IOVA_LENGTH - size;
			continue;
		}
		dma_unmap_single(dev, (dma_addr_t)iova, size, DMA_TO_DEVICE);
	}

	free_pages((unsigned long)virt, get_order(size));
	return ret;
}

static int __functional_dma_api_va_test(struct seq_file *s)
{
	int ret = 0;
	size_t *sz;
	size_t sizes[] = {SZ_4K, SZ_8K, SZ_16K, SZ_64K, 0};
	struct iommu_debug_device *ddev = s->private;
	char *usecase_name;

	/*
	 * dev_name() cannot be used to get the usecase name as ddev->test_dev
	 * will be NULL in case __apply_to_new_mapping() fails. Since
	 * ddev->test_dev changes across calls to __apply_to_new_mapping(), we
	 * also can't hold a reference to its name by caching the result of
	 * dev_name() initially.
	 */
	mutex_lock(&ddev->state_lock);
	if (!ddev->test_dev) {
		mutex_unlock(&ddev->state_lock);
		return -ENODEV;
	}

	usecase_name = kstrdup(dev_name(ddev->test_dev), GFP_KERNEL);
	mutex_unlock(&ddev->state_lock);
	if (!usecase_name)
		return -ENOMEM;

	for (sz = sizes; *sz; ++sz) {
		ps_printf(usecase_name, s, "Full VA sweep @%s:", _size_to_string(*sz));
		if (__apply_to_new_mapping(s, __full_va_sweep, (void *)*sz)) {
			ps_printf(usecase_name, s, "  -> FAILED\n");
			ret = -EINVAL;
		} else {
			ps_printf(usecase_name, s, "  -> SUCCEEDED\n");
		}
	}

	ps_printf(usecase_name, s, "bonus map:");
	if (__apply_to_new_mapping(s, __full_va_sweep, (void *)SZ_4K)) {
		ps_printf(usecase_name, s, "  -> FAILED\n");
		ret = -EINVAL;
	} else {
		ps_printf(usecase_name, s, "  -> SUCCEEDED\n");
	}

	for (sz = sizes; *sz; ++sz) {
		ps_printf(usecase_name, s, "Rand VA sweep @%s:", _size_to_string(*sz));
		if (__apply_to_new_mapping(s, __rand_va_sweep, (void *)*sz)) {
			ps_printf(usecase_name, s, "  -> FAILED\n");
			ret = -EINVAL;
		} else {
			ps_printf(usecase_name, s, "  -> SUCCEEDED\n");
		}
	}

	ps_printf(usecase_name, s, "TLB stress sweep:");
	if (__apply_to_new_mapping(s, __tlb_stress_sweep, NULL)) {
		ps_printf(usecase_name, s, "  -> FAILED\n");
		ret = -EINVAL;
	} else {
		ps_printf(usecase_name, s, "  -> SUCCEEDED\n");
	}

	ps_printf(usecase_name, s, "second bonus map:");
	if (__apply_to_new_mapping(s, __full_va_sweep, (void *)SZ_4K)) {
		ps_printf(usecase_name, s, "  -> FAILED\n");
		ret = -EINVAL;
	} else {
		ps_printf(usecase_name, s, "  -> SUCCEEDED\n");
	}

	kfree(usecase_name);
	return ret;
}

static int iommu_debug_functional_fast_dma_api_show(struct seq_file *s,
						    void *ignored)
{
	int ret = 0;
	struct iommu_debug_device *ddev = s->private;

	if (!ddev->test_dev) {
		pr_err("%s:Have you selected a uscase?\n", __func__);
		return -EINVAL;
	}

	if (!ddev->fastmap_usecase) {
		ps_printf(dev_name(ddev->test_dev), s,
			"Not a fastmap usecase\n");
		return 0;
	} else if (!IS_ENABLED(CONFIG_IOMMU_IO_PGTABLE_FAST)) {
		ps_printf(dev_name(ddev->test_dev), s,
			"CONFIG_IOMMU_IO_PGTABLE_FAST not enabled\n");
		return 0;
	}

	ret |= __apply_to_new_mapping(s, __functional_dma_api_alloc_test, NULL);
	ret |= __apply_to_new_mapping(s, __functional_dma_api_basic_test, NULL);
	ret |=  __functional_dma_api_va_test(s);
	return ret;
}

static int iommu_debug_functional_fast_dma_api_open(struct inode *inode,
						    struct file *file)
{
	return single_open(file, iommu_debug_functional_fast_dma_api_show,
			   inode->i_private);
}

const struct file_operations iommu_debug_functional_fast_dma_api_fops = {
	.open	 = iommu_debug_functional_fast_dma_api_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

static ssize_t iommu_debug_test_virt_addr_read(struct file *file,
					       char __user *ubuf,
					       size_t count, loff_t *offset)
{
	char buf[100];
	int len;

	if (*offset)
		return 0;

	mutex_lock(&test_virt_addr_lock);
	if (IS_ERR_OR_NULL(test_virt_addr))
		test_virt_addr = kzalloc(SZ_1M, GFP_KERNEL);

	if (!test_virt_addr) {
		test_virt_addr = ERR_PTR(-ENOMEM);
		len = strscpy(buf, "FAIL\n", sizeof(buf));
	} else {
		len = scnprintf(buf, sizeof(buf), "0x%p\n", test_virt_addr);
	}
	mutex_unlock(&test_virt_addr_lock);

	return simple_read_from_buffer(ubuf, count, offset, buf, len);
}

const struct file_operations iommu_debug_test_virt_addr_fops = {
	.open	= simple_open,
	.read	= iommu_debug_test_virt_addr_read,
};

#ifdef CONFIG_IOMMU_IOVA_ALIGNMENT
static unsigned long iommu_debug_get_align_mask(size_t size)
{
	unsigned long align_mask = ~0UL;

	align_mask <<= min_t(unsigned long, CONFIG_IOMMU_IOVA_ALIGNMENT + PAGE_SHIFT,
			     fls_long(size - 1));
	return ~align_mask;
}
#else
static unsigned long iommu_debug_get_align_mask(size_t size)
{
	unsigned long align_mask = ~0UL;

	align_mask <<= fls_long(size - 1);
	return ~align_mask;
}
#endif

static void iommu_debug_device_profiling(struct seq_file *s, struct iommu_debug_device *ddev,
					 const size_t sizes[])
{
	const size_t *sz;
	struct iommu_domain *domain;
	struct device *dev;
	unsigned long iova = 0x10000;
	phys_addr_t paddr = 0x80000000;

	mutex_lock(&ddev->state_lock);
	if (!iommu_debug_usecase_reset(ddev))
		goto out;

	domain = ddev->domain;
	dev = ddev->test_dev;

	seq_printf(s, "(average over %d iterations)\n", ddev->nr_iters);
	seq_printf(s, "%8s %19s %16s\n", "size", "iommu_map", "iommu_unmap");
	for (sz = sizes; *sz; ++sz) {
		size_t size = *sz;
		size_t unmapped;
		u64 map_elapsed_ns = 0, unmap_elapsed_ns = 0;
		u64 map_elapsed_us = 0, unmap_elapsed_us = 0;
		u32 map_elapsed_rem = 0, unmap_elapsed_rem = 0;
		ktime_t tbefore, tafter, diff;
		int i;
		unsigned long align_mask = iommu_debug_get_align_mask(size);

		for (i = 0; i < ddev->nr_iters; ++i) {
			tbefore = ktime_get();
			if (iommu_map(domain, __ALIGN_MASK(iova, align_mask),
				      ALIGN(paddr, size), size,
				      IOMMU_READ | IOMMU_WRITE)) {
				seq_puts(s, "Failed to map\n");
				continue;
			}
			tafter = ktime_get();
			diff = ktime_sub(tafter, tbefore);
			map_elapsed_ns += ktime_to_ns(diff);

			tbefore = ktime_get();
			unmapped = iommu_unmap(domain,
					       __ALIGN_MASK(iova, align_mask),
					       size);
			if (unmapped != size) {
				seq_printf(s,
					   "Only unmapped %zx instead of %zx\n",
					   unmapped, size);
				continue;
			}
			tafter = ktime_get();
			diff = ktime_sub(tafter, tbefore);
			unmap_elapsed_ns += ktime_to_ns(diff);
		}

		map_elapsed_ns = div_u64_rem(map_elapsed_ns, ddev->nr_iters, &map_elapsed_rem);
		unmap_elapsed_ns = div_u64_rem(unmap_elapsed_ns, ddev->nr_iters,
					       &unmap_elapsed_rem);

		map_elapsed_us = div_u64_rem(map_elapsed_ns, 1000, &map_elapsed_rem);
		unmap_elapsed_us = div_u64_rem(unmap_elapsed_ns, 1000, &unmap_elapsed_rem);

		seq_printf(s, "%8s %12lld.%03d us %9lld.%03d us\n",
			   _size_to_string(size), map_elapsed_us, map_elapsed_rem,
			   unmap_elapsed_us, unmap_elapsed_rem);
	}

	seq_putc(s, '\n');
	seq_printf(s, "%8s %19s %16s\n", "size", "iommu_map_sg", "iommu_unmap");
	for (sz = sizes; *sz; ++sz) {
		size_t size = *sz;
		size_t unmapped;
		u64 map_elapsed_ns = 0, unmap_elapsed_ns = 0;
		u64 map_elapsed_us = 0, unmap_elapsed_us = 0;
		u32 map_elapsed_rem = 0, unmap_elapsed_rem = 0;
		ktime_t tbefore, tafter, diff;
		struct sg_table table;
		unsigned long chunk_size = SZ_4K;
		int i;
		unsigned long align_mask = iommu_debug_get_align_mask(size);

		if (iommu_debug_build_phoney_sg_table(dev, &table, size,
						      chunk_size)) {
			seq_puts(s, "couldn't build phoney sg table! bailing...\n");
			goto out;
		}

		for (i = 0; i < ddev->nr_iters; ++i) {
			tbefore = ktime_get();
			if (iommu_map_sgtable(domain, __ALIGN_MASK(iova, align_mask),
					      &table, IOMMU_READ | IOMMU_WRITE) != size) {
				seq_puts(s, "Failed to map_sg\n");
				goto next;
			}
			tafter = ktime_get();
			diff = ktime_sub(tafter, tbefore);
			map_elapsed_ns += ktime_to_ns(diff);

			tbefore = ktime_get();
			unmapped = iommu_unmap(domain,
					       __ALIGN_MASK(iova, align_mask),
					       size);
			if (unmapped != size) {
				seq_printf(s, "Only unmapped %zx instead of %zx\n",
					   unmapped, size);
				goto next;
			}
			tafter = ktime_get();
			diff = ktime_sub(tafter, tbefore);
			unmap_elapsed_ns += ktime_to_ns(diff);
		}

		map_elapsed_ns = div_u64_rem(map_elapsed_ns, ddev->nr_iters, &map_elapsed_rem);
		unmap_elapsed_ns = div_u64_rem(unmap_elapsed_ns, ddev->nr_iters,
					       &unmap_elapsed_rem);

		map_elapsed_us = div_u64_rem(map_elapsed_ns, 1000, &map_elapsed_rem);
		unmap_elapsed_us = div_u64_rem(unmap_elapsed_ns, 1000, &unmap_elapsed_rem);

		seq_printf(s, "%8s %12lld.%03d us %9lld.%03d us\n", _size_to_string(size),
			   map_elapsed_us, map_elapsed_rem, unmap_elapsed_us, unmap_elapsed_rem);

next:
		iommu_debug_destroy_phoney_sg_table(dev, &table, chunk_size);
	}

out:
	mutex_unlock(&ddev->state_lock);
}

static int iommu_debug_profiling_show(struct seq_file *s, void *ignored)
{
	struct iommu_debug_device *ddev = s->private;
	const size_t sizes[] = { SZ_4K, SZ_64K, SZ_1M, SZ_2M, SZ_1M * 12,
					SZ_1M * 24, SZ_1M * 32, 0 };

	iommu_debug_device_profiling(s, ddev, sizes);
	return 0;
}

static int iommu_debug_profiling_open(struct inode *inode, struct file *file)
{
	return single_open(file, iommu_debug_profiling_show, inode->i_private);
}

const struct file_operations iommu_debug_profiling_fops = {
	.open	 = iommu_debug_profiling_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};
