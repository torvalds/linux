/*
 * arch/arm/mach-exynos/secmem-allocdev.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/cma.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/memory.h>
#include <asm/cacheflush.h>

#include <plat/devs.h>
#include <plat/pd.h>

#include <mach/secmem.h>
#include <mach/cpufreq.h>
#include <mach/dev.h>

#define DRM_CPU_FREQ	400000
#define DRM_BUS_FREQ	267160

struct miscdevice secmem;
struct secmem_crypto_driver_ftn *crypto_driver;

static char *secmem_info[] = {
	"mfc",		/* 0 */
	"fimc",		/* 1 */
	"mfc-shm",	/* 2 */
	"sectbl",	/* 3 */
	"video",	/* 4 */
	"fimd",		/* 5 */
	NULL
};

static bool drm_onoff = false;

#define SECMEM_IS_PAGE_ALIGNED(addr) (!((addr) & (~PAGE_MASK)))

static int secmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;

	BUG_ON(!SECMEM_IS_PAGE_ALIGNED(vma->vm_start));
	BUG_ON(!SECMEM_IS_PAGE_ALIGNED(vma->vm_end));

	vma->vm_flags |= VM_RESERVED;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				size, vma->vm_page_prot)) {
		printk(KERN_ERR "%s : remap_pfn_range() failed!\n", __func__);
		return -EAGAIN;
	}

	return 0;
}

static long secmem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case SECMEM_IOC_CHUNKINFO:
	{
		struct cma_info info;
		struct secchunk_info minfo;
		char **mname;
		int nbufs = 0;

		for (mname = secmem_info; *mname != NULL; mname++)
			nbufs++;

		if (nbufs == 0)
			return -ENOMEM;

		if (copy_from_user(&minfo, (void __user *)arg, sizeof(minfo)))
			return -EFAULT;

		if (minfo.index < 0)
			return -EINVAL;

		if (minfo.index >= nbufs) {
			minfo.index = -1; /* No more memory region */
		} else {

			if (cma_info(&info, secmem.this_device,
					secmem_info[minfo.index]))
				return -EINVAL;

			minfo.base = info.lower_bound;
			minfo.size = info.total_size;
		}

		if (copy_to_user((void __user *)arg, &minfo, sizeof(minfo)))
			return -EFAULT;
	}
		break;
	case SECMEM_IOC_GET_DRM_ONOFF:
		if (copy_to_user((void __user *)arg, &drm_onoff, sizeof(int)))
			return -EFAULT;
		break;
	case SECMEM_IOC_SET_DRM_ONOFF:
	{
		int val = 0;
		unsigned int cpufreq;
#if defined(CONFIG_BUSFREQ_OPP)
		struct device *bus_dev=NULL;
		bus_dev = dev_get("exynos-busfreq");
#endif

		if (copy_from_user(&val, (int __user *)arg, sizeof(int)))
			return -EFAULT;

		if (val) {
			exynos_cpufreq_get_level(DRM_CPU_FREQ, &cpufreq);
			exynos_cpufreq_lock(DVFS_LOCK_ID_DRM, cpufreq);
#if defined(CONFIG_BUSFREQ_OPP)
			dev_lock(bus_dev, secmem.this_device, DRM_BUS_FREQ);
#endif
			if (drm_onoff == false) {
				drm_onoff = true;
				pm_runtime_forbid((*(secmem.this_device)).parent);
				exynos_pd_enable(&exynos4_device_pd[PD_MFC].dev);
			} else
				printk(KERN_ERR "%s: DRM is already on\n", __func__);
		} else {
			exynos_cpufreq_lock_free(DVFS_LOCK_ID_DRM);
#if defined(CONFIG_BUSFREQ_OPP)
			dev_unlock(bus_dev, secmem.this_device);
#endif
			if (drm_onoff == true) {
				drm_onoff = false;
				pm_runtime_allow((*(secmem.this_device)).parent);
			} else
				printk(KERN_ERR "%s: DRM is already off\n", __func__);
		}
	}
		break;
	case SECMEM_IOC_GET_CRYPTO_LOCK:
	{
		int i;
		int ret;

		if (crypto_driver) {
			for (i = 0; i < 100; i++) {
				ret = crypto_driver->lock();
				if (ret == 0)
					break;
				printk(KERN_ERR "%s : Retry to get sync lock.\n",
					__func__);
			}
			return ret;
		}
		break;
	}
	case SECMEM_IOC_RELEASE_CRYPTO_LOCK:
	{
		if (crypto_driver)
			return crypto_driver->release();
		break;
	}
	case SECMEM_IOC_GET_ADDR:
	{
		struct secmem_region region;

		if (copy_from_user(&region, (void __user *)arg,
					sizeof(struct secmem_region)))
			return -EFAULT;

		region.virt_addr = kmalloc(region.len, GFP_KERNEL | GFP_DMA);
		if (!region.virt_addr) {
			printk(KERN_ERR "%s: Get memory address failed. [size : %ld]\n", __func__, region.len);
			return -EFAULT;
		}
		region.phys_addr = virt_to_phys(region.virt_addr);

		dmac_map_area(region.virt_addr, region.len / sizeof(unsigned long), 2);

		if (copy_to_user((void __user *)arg, &region,
					sizeof(struct secmem_region)))
			return -EFAULT;
		break;
	}
	case SECMEM_IOC_RELEASE_ADDR:
	{
		struct secmem_region region;

		if (copy_from_user(&region, (void __user *)arg,
					sizeof(struct secmem_region)))
			return -EFAULT;

		dmac_unmap_area(region.virt_addr, region.len, 2);

		kfree(region.virt_addr);
		break;
	}
	default:
		return -ENOTTY;
	}

	return 0;
}

void secmem_crypto_register(struct secmem_crypto_driver_ftn *ftn)
{
	crypto_driver = ftn;
}
EXPORT_SYMBOL(secmem_crypto_register);

void secmem_crypto_deregister(void)
{
	crypto_driver = NULL;
}
EXPORT_SYMBOL(secmem_crypto_deregister);

static struct file_operations secmem_fops = {
	.unlocked_ioctl = &secmem_ioctl,
	.mmap		= secmem_mmap,
};

static int __init secmem_init(void)
{
	int ret;
	secmem.minor = MISC_DYNAMIC_MINOR;
	secmem.name = "s5p-smem";
	secmem.fops = &secmem_fops;

	ret = misc_register(&secmem);
	if (ret)
		return ret;

	crypto_driver = NULL;

	pm_runtime_enable(secmem.this_device);

	return 0;
}

static void __exit secmem_exit(void)
{
	__pm_runtime_disable(secmem.this_device, false);
	misc_deregister(&secmem);
}

module_init(secmem_init);
module_exit(secmem_exit);
