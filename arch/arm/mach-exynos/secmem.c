/*
 * arch/arm/mach-exynos/secmem.c
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
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/pm_qos.h>

#include <asm/memory.h>
#include <asm/cacheflush.h>

#include <plat/devs.h>

#include <mach/secmem.h>

#define SECMEM_DEV_NAME	"s5p-smem"
struct miscdevice secmem;
struct secmem_crypto_driver_ftn *crypto_driver;
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
static struct pm_qos_request exynos5_secmem_mif_qos;
#endif

static char *secmem_regions[] = {
	"mfc_sh",	/* 0 */
	"g2d_wfd",	/* 1 */
	"fimd_video",	/* 2 */
	"mfc_output",	/* 3 */
	"mfc_input",	/* 4 */
	"mfc_fw",	/* 5 */
	"sectbl",	/* 6 */
	NULL
};

static bool		drm_onoff;
static DEFINE_MUTEX(drm_lock);

struct secmem_info {
	struct device	*dev;
	bool		drm_enabled;
};

#define SECMEM_IS_PAGE_ALIGNED(addr) (!((addr) & (~PAGE_MASK)))

static int secmem_open(struct inode *inode, struct file *file)
{
	struct miscdevice *miscdev = file->private_data;
	struct device *dev = miscdev->this_device;
	struct secmem_info *info;

	info = kzalloc(sizeof(struct secmem_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;
	file->private_data = info;
	return 0;
}

static void drm_enable_locked(struct secmem_info *info, bool enable)
{
	if (drm_onoff != enable) {
#ifdef CONFIG_EXYNOS5_DEV_GSC
		if (enable)
			pm_runtime_forbid(info->dev->parent);
		else
			pm_runtime_allow(info->dev->parent);
#endif
		drm_onoff = enable;
		/*
		 * this will only allow this instance to turn drm_off either by
		 * calling the ioctl or by closing the fd
		 */
		info->drm_enabled = enable;
	} else {
		pr_err("%s: DRM is already %s\n", __func__,
		       drm_onoff ? "on" : "off");
	}
}

static int secmem_release(struct inode *inode, struct file *file)
{
	struct secmem_info *info = file->private_data;

	/* disable drm if we were the one to turn it on */
	mutex_lock(&drm_lock);
	if (info->drm_enabled)
		drm_enable_locked(info, false);
	mutex_unlock(&drm_lock);

	kfree(info);
	return 0;
}

static long secmem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct secmem_info *info = filp->private_data;

	static int nbufs = 0;

	switch (cmd) {
	case SECMEM_IOC_GET_CHUNK_NUM:
	{
		char **mname;

		nbufs = 0;
		for (mname = secmem_regions; *mname != NULL; mname++)
			nbufs++;

		if (nbufs == 0)
			return -ENOMEM;

		if (copy_to_user((void __user *)arg, &nbufs, sizeof(int)))
			return -EFAULT;
		break;
	}
	case SECMEM_IOC_CHUNKINFO:
	{
		struct cma_info cinfo;
		struct secchunk_info minfo;

		if (copy_from_user(&minfo, (void __user *)arg, sizeof(minfo)))
			return -EFAULT;

		memset(&minfo.name, 0, MAX_NAME_LEN);

		if (minfo.index < 0)
			return -EINVAL;

		if (minfo.index >= nbufs) {
			minfo.index = -1; /* No more memory region */
		} else {

			if (cma_info(&cinfo, info->dev,
					secmem_regions[minfo.index]))
				return -EINVAL;

			minfo.base = cinfo.lower_bound;
			minfo.size = cinfo.total_size;
			memcpy(minfo.name, secmem_regions[minfo.index], MAX_NAME_LEN);
		}

		if (copy_to_user((void __user *)arg, &minfo, sizeof(minfo)))
			return -EFAULT;
		break;
	}
#if defined(CONFIG_ION)
	case SECMEM_IOC_GET_FD_PHYS_ADDR:
	{
		struct ion_client *client;
		struct secfd_info fd_info;
		struct ion_fd_data data;
		size_t len;

		if (copy_from_user(&fd_info, (int __user *)arg,
					sizeof(fd_info)))
			return -EFAULT;

		client = ion_client_create(ion_exynos, -1, "DRM");
		if (IS_ERR(client))
			pr_err("%s: Failed to get ion_client of DRM\n",
				__func__);

		data.fd = fd_info.fd;
		data.handle = ion_import_dma_buf(client, data.fd);
		pr_debug("%s: fd from user space = %d\n",
				__func__, fd_info.fd);
		if (IS_ERR(data.handle))
			pr_err("%s: Failed to get ion_handle of DRM\n",
				__func__);

		if (ion_phys(client, data.handle, &fd_info.phys, &len))
			pr_err("%s: Failed to get phys. addr of DRM\n",
				__func__);

		pr_debug("%s: physical addr from kernel space = 0x%08x\n",
				__func__, (unsigned int)fd_info.phys);

		ion_free(client, data.handle);
		ion_client_destroy(client);

		if (copy_to_user((void __user *)arg, &fd_info, sizeof(fd_info)))
			return -EFAULT;
		break;
	}
#endif
	case SECMEM_IOC_GET_DRM_ONOFF:
		smp_rmb();
		if (copy_to_user((void __user *)arg, &drm_onoff, sizeof(int)))
			return -EFAULT;
		break;
	case SECMEM_IOC_SET_DRM_ONOFF:
	{
		int val = 0;

		if (copy_from_user(&val, (int __user *)arg, sizeof(int)))
			return -EFAULT;

		mutex_lock(&drm_lock);
		if ((info->drm_enabled && !val) ||
		    (!info->drm_enabled && val)) {
			/*
			 * 1. if we enabled drm, then disable it
			 * 2. if we don't already hdrm enabled,
			 *    try to enable it.
			 */
			drm_enable_locked(info, val);
		}
		mutex_unlock(&drm_lock);
		break;
	}
	case SECMEM_IOC_GET_CRYPTO_LOCK:
	{
		int i;
		int ret;

		if (crypto_driver) {
			for (i = 0; i < 100; i++) {
				ret = crypto_driver->lock();
				if (ret == 0)
					break;
				pr_err("%s : Retry to get sync lock.\n",
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
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	case SECMEM_IOC_REQ_MIF_LOCK:
	{
		int req_mif_lock;

		if (copy_from_user(&req_mif_lock, (void __user *)arg, sizeof(int)))
			return -EFAULT;

		if (req_mif_lock) {
			pm_qos_update_request(&exynos5_secmem_mif_qos, 800000);
			pr_debug("%s: Get MIF lock successfully\n", __func__);
		} else {
			pm_qos_update_request(&exynos5_secmem_mif_qos, 0);
			pr_debug("%s: Release MIF lock successfully\n", __func__);
		}
		break;
	}
#endif
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

static const struct file_operations secmem_fops = {
	.open		= secmem_open,
	.release	= secmem_release,
	.unlocked_ioctl = secmem_ioctl,
};

struct miscdevice secmem = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= SECMEM_DEV_NAME,
	.fops	= &secmem_fops,
#ifdef CONFIG_EXYNOS5_DEV_GSC
	.parent	= &exynos5_device_gsc0.dev,
#endif
};

static int __init secmem_init(void)
{
	int ret;

	ret = misc_register(&secmem);
	if (ret) {
		pr_err("%s: SECMEM can't register misc on minor=%d\n",
			__func__, MISC_DYNAMIC_MINOR);
		return ret;
	}

	crypto_driver = NULL;

	pm_runtime_enable(secmem.this_device);
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	pm_qos_add_request(&exynos5_secmem_mif_qos, PM_QOS_BUS_THROUGHPUT, 0);
#endif

	return 0;
}

static void __exit secmem_exit(void)
{
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	pm_qos_remove_request(&exynos5_secmem_mif_qos);
#endif

	__pm_runtime_disable(secmem.this_device, false);
	misc_deregister(&secmem);
}

module_init(secmem_init);
module_exit(secmem_exit);
