/* linux/drivers/char/exynos_mem.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/errno.h>	/* error codes */
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/highmem.h>
#include <linux/dma-mapping.h>
#include <linux/cma.h>
#include <asm/cacheflush.h>

#include <plat/cpu.h>

#include <linux/exynos_mem.h>

#define L2_FLUSH_ALL	SZ_1M
#define L1_FLUSH_ALL	SZ_64K

struct exynos_mem {
	bool cacheable;
	unsigned int  phybase;
};

int exynos_mem_open(struct inode *inode, struct file *filp)
{
	struct exynos_mem *prv_data;

	prv_data = kzalloc(sizeof(struct exynos_mem), GFP_KERNEL);
	if (!prv_data) {
		pr_err("%s: not enough memory\n", __func__);
		return -ENOMEM;
	}

	prv_data->cacheable = true;	/* Default: cacheable */

	filp->private_data = prv_data;

	printk(KERN_DEBUG "[%s:%d] private_data(0x%08x)\n",
		__func__, __LINE__, (u32)prv_data);

	return 0;
}

int exynos_mem_release(struct inode *inode, struct file *filp)
{
	printk(KERN_DEBUG "[%s:%d] private_data(0x%08x)\n",
		__func__, __LINE__, (u32)filp->private_data);

	kfree(filp->private_data);

	return 0;
}

enum cacheop { EM_CLEAN, EM_INV, EM_FLUSH };

static void cache_maint_inner(void *vaddr, size_t size, enum cacheop op)
{
	switch (op) {
		case EM_CLEAN:
			dmac_map_area(vaddr, size, DMA_TO_DEVICE);
			break;
		case EM_INV:
			dmac_unmap_area(vaddr, size, DMA_TO_DEVICE);
			break;
		case EM_FLUSH:
			dmac_flush_range(vaddr, vaddr + size);
	}
}

static void cache_maint_phys(phys_addr_t start, size_t length, enum cacheop op)
{
	size_t left = length;
	phys_addr_t begin = start;

	if (!cma_is_registered_region(start, length)) {
		pr_err("[%s] handling non-cma region (%#x@%#x)is prohibited\n",
					__func__, length, start);
		return;
	}

	if (!soc_is_exynos5250() && !soc_is_exynos5210()) {
		if (length > (size_t) L1_FLUSH_ALL) {
			flush_cache_all();
			smp_call_function(
					(smp_call_func_t)__cpuc_flush_kern_all,
					NULL, 1);

			goto outer_cache_ops;
		}
	}

#ifdef CONFIG_HIGHMEM
	do {
		size_t len;
		struct page *page;
		void *vaddr;
		off_t offset;

		page = phys_to_page(start);
		offset = offset_in_page(start);
		len = PAGE_SIZE - offset;

		if (left < len)
			len = left;

		if (PageHighMem(page)) {
			vaddr = kmap(page);
			cache_maint_inner(vaddr + offset, len, op);
			kunmap(page);
		} else {
			vaddr = page_address(page) + offset;
			cache_maint_inner(vaddr, len, op);
		}
		left -= len;
		start += len;
	} while (left);
#else
	cache_maint_inner(phys_to_virt(begin), left, op);
#endif

outer_cache_ops:
	switch (op) {
	case EM_CLEAN:
		outer_clean_range(begin, begin + length);
		break;
	case EM_INV:
		if (length <= L2_FLUSH_ALL) {
			outer_inv_range(begin, begin + length);
			break;
		}
		/* else FALL THROUGH */
	case EM_FLUSH:
		outer_flush_range(begin, begin + length);
		break;
	}
}

static void exynos_mem_paddr_cache_clean(dma_addr_t start, size_t length)
{
	if (length > (size_t) L2_FLUSH_ALL) {
		flush_cache_all();		/* L1 */
		smp_call_function((smp_call_func_t)__cpuc_flush_kern_all, NULL, 1);
		outer_clean_all();		/* L2 */
	} else if (length > (size_t) L1_FLUSH_ALL) {
		dma_addr_t end = start + length - 1;

		flush_cache_all();		/* L1 */
		smp_call_function((smp_call_func_t)__cpuc_flush_kern_all, NULL, 1);
		outer_clean_range(start, end);  /* L2 */
	} else {
		dma_addr_t end = start + length - 1;

		dmac_flush_range(phys_to_virt(start), phys_to_virt(end));
		outer_clean_range(start, end);	/* L2 */
	}
}

long exynos_mem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case EXYNOS_MEM_SET_CACHEABLE:
	{
		struct exynos_mem *mem = filp->private_data;
		int cacheable;
		if (get_user(cacheable, (u32 __user *)arg)) {
			pr_err("[%s:%d] err: EXYNOS_MEM_SET_CACHEABLE\n",
				__func__, __LINE__);
			return -EFAULT;
		}
		mem->cacheable = cacheable;
		break;
	}

	case EXYNOS_MEM_PADDR_CACHE_FLUSH:
	{
		struct exynos_mem_flush_range range;
		if (copy_from_user(&range,
				   (struct exynos_mem_flush_range __user *)arg,
				   sizeof(range))) {
			pr_err("[%s:%d] err: EXYNOS_MEM_PADDR_CACHE_FLUSH\n",
				__func__, __LINE__);
			return -EFAULT;
		}

		cache_maint_phys(range.start, range.length, EM_FLUSH);
		break;
	}
	case EXYNOS_MEM_PADDR_CACHE_CLEAN:
	{
		struct exynos_mem_flush_range range;
		if (copy_from_user(&range,
				   (struct exynos_mem_flush_range __user *)arg,
				   sizeof(range))) {
			pr_err("[%s:%d] err: EXYNOS_MEM_PADDR_CACHE_FLUSH\n",
				__func__, __LINE__);
			return -EFAULT;
		}

		cache_maint_phys(range.start, range.length, EM_CLEAN);
		break;
	}
	case EXYNOS_MEM_SET_PHYADDR:
	{
		struct exynos_mem *mem = filp->private_data;
		int phyaddr;
		if (get_user(phyaddr, (u32 __user *)arg)) {
			pr_err("[%s:%d] err: EXYNOS_MEM_SET_PHYADDR\n",
				__func__, __LINE__);
			return -EFAULT;
		}
		mem->phybase = phyaddr >> PAGE_SHIFT;

		break;
	}

	default:
		pr_err("[%s:%d] error command\n", __func__, __LINE__);
		return -EINVAL;
	}

	return 0;
}

static void exynos_mem_mmap_open(struct vm_area_struct *vma)
{
	printk(KERN_DEBUG "[%s] addr(0x%08x)\n", __func__, (u32)vma->vm_start);
}

static void exynos_mem_mmap_close(struct vm_area_struct *vma)
{
	printk(KERN_DEBUG "[%s] addr(0x%08x)\n", __func__, (u32)vma->vm_start);
}

static struct vm_operations_struct exynos_mem_ops = {
	.open	= exynos_mem_mmap_open,
	.close	= exynos_mem_mmap_close,
};

int exynos_mem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct exynos_mem *mem = (struct exynos_mem *)filp->private_data;
	bool cacheable = mem->cacheable;
	dma_addr_t start = 0;
	u32 pfn = 0;
	u32 size = vma->vm_end - vma->vm_start;

	if (vma->vm_pgoff) {
		start = vma->vm_pgoff << PAGE_SHIFT;
		pfn = vma->vm_pgoff;
	} else {
		start = mem->phybase << PAGE_SHIFT;
		pfn = mem->phybase;
	}

	if (!cma_is_registered_region(start, size)) {
		pr_err("[%s] handling non-cma region (%#x@%#x)is prohibited\n",
						__func__, size, start);
		return -EINVAL;
	}

	if (!cacheable)
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	vma->vm_flags |= VM_RESERVED;
	vma->vm_ops = &exynos_mem_ops;

	if ((vma->vm_flags & VM_WRITE) && !(vma->vm_flags & VM_SHARED)) {
		pr_err("writable mapping must be shared\n");
		return -EINVAL;
	}

	if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
		pr_err("mmap fail\n");
		return -EINVAL;
	}

	vma->vm_ops->open(vma);

	return 0;
}
