/*
 * @File
 * @Title       PowerVR DRM platform driver
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
#include <drm/drm_drv.h>
#include <drm/drm_print.h>
#include <linux/mod_devicetable.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/slab.h>
#else
#include <drm/drmP.h>
#endif

#include <linux/module.h>
#include <linux/platform_device.h>
#include <asm/page.h>

#include "module_common.h"
#include "pvr_drv.h"
#include "pvrmodule.h"
#include "sysinfo.h"
#include "pvr_debug.h"



/* This header must always be included last */
#include "kernel_compatibility.h"

static struct drm_driver pvr_drm_platform_driver;

#if defined(MODULE) && !defined(PVR_LDM_PLATFORM_PRE_REGISTERED)
/*
 * This is an arbitrary value. If it's changed then the 'num_devices' module
 * parameter description should also be updated to match.
 */
#define MAX_DEVICES 16

static unsigned int pvr_num_devices = 1;
static struct platform_device **pvr_devices;

#if defined(NO_HARDWARE)
static int pvr_num_devices_set(const char *val,
			       const struct kernel_param *param)
{
	int err;

	err = param_set_uint(val, param);
	if (err)
		return err;

	if (pvr_num_devices == 0 || pvr_num_devices > MAX_DEVICES)
		return -EINVAL;

	return 0;
}

static const struct kernel_param_ops pvr_num_devices_ops = {
	.set = pvr_num_devices_set,
	.get = param_get_uint,
};

module_param_cb(num_devices, &pvr_num_devices_ops, &pvr_num_devices, 0444);
MODULE_PARM_DESC(num_devices,
		 "Number of platform devices to register (default: 1 - max: 16)");
#endif /* defined(NO_HARDWARE) */
#endif /* defined(MODULE) && !defined(PVR_LDM_PLATFORM_PRE_REGISTERED) */

#if 0
static int pvr_devices_register(void)
{
#if defined(MODULE) && !defined(PVR_LDM_PLATFORM_PRE_REGISTERED)
	struct platform_device_info pvr_dev_info = {
		.name = SYS_RGX_DEV_NAME,
		.id = -2,
#if defined(NO_HARDWARE)
		/* Not all cores have 40 bit physical support, but this
		 * will work unless > 32 bit address is returned on those cores.
		 * In the future this will be fixed more correctly.
		 */
		.dma_mask = DMA_BIT_MASK(40),
#else
		.dma_mask = DMA_BIT_MASK(32),
#endif
	};
	unsigned int i;

	BUG_ON(pvr_num_devices == 0 || pvr_num_devices > MAX_DEVICES);

	pvr_devices = kmalloc_array(pvr_num_devices, sizeof(*pvr_devices),
				    GFP_KERNEL);
	if (!pvr_devices)
		return -ENOMEM;

	for (i = 0; i < pvr_num_devices; i++) {
		pvr_devices[i] = platform_device_register_full(&pvr_dev_info);
		if (IS_ERR(pvr_devices[i])) {
			DRM_ERROR("unable to register device %u (err=%ld)\n",
				  i, PTR_ERR(pvr_devices[i]));
			pvr_devices[i] = NULL;
			return -ENODEV;
		}
	}
#endif /* defined(MODULE) && !defined(PVR_LDM_PLATFORM_PRE_REGISTERED) */

	return 0;
}
#endif
static void pvr_devices_unregister(void)
{
#if defined(MODULE) && !defined(PVR_LDM_PLATFORM_PRE_REGISTERED)
	unsigned int i;

	BUG_ON(!pvr_devices);

	for (i = 0; i < pvr_num_devices && pvr_devices[i]; i++)
		platform_device_unregister(pvr_devices[i]);

	kfree(pvr_devices);
	pvr_devices = NULL;
#endif /* defined(MODULE) && !defined(PVR_LDM_PLATFORM_PRE_REGISTERED) */
}

#ifdef IMG_GPU_DEBUG
unsigned long va2pa(const void *vaddr_in);
unsigned long va2pa(const void *vaddr_in)
{
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    unsigned long paddr = 0;
    unsigned long page_addr = 0;
    unsigned long page_offset = 0;
    unsigned long vaddr = (unsigned long)vaddr_in;

    printk("CSR_SATP = %lx\n", csr_read(CSR_SATP));

    pgd = pgd_offset_k(vaddr);
    printk("pgd_val = 0x%lx\n", pgd_val(*pgd));
    printk("pgd_index = %lu\n", pgd_index(vaddr));
    if (pgd_none(*pgd)) {
        printk("not mapped in pgd\n");
        return -1;
    }

    p4d = p4d_offset(pgd, vaddr);
    printk("p4d_val = 0x%lx\n", p4d_val(*p4d));
    if (p4d_none(*p4d)) {
        printk("not mapped in p4d\n");
        return -1;
    }

    pud = pud_offset(p4d, vaddr);
    printk("pud_val = 0x%lx\n", pud_val(*pud));
    if (pud_none(*pud)) {
        printk("not mapped in pud\n");
        return -1;
    }

    pmd = pmd_offset(pud, vaddr);
    printk("pmd_val = 0x%lx\n", pmd_val(*pmd));
    printk("pmd_index = %lu\n", pmd_index(vaddr));
    if (pmd_none(*pmd)) {
        printk("not mapped in pmd\n");
        return -1;
    }


    pte = pte_offset_kernel(pmd, vaddr);
    printk("pte_val = 0x%lx\n", pte_val(*pte));
    printk("pte_index = %lu\n", pte_index(vaddr));
    if (pte_none(*pte)) {
        printk("not mapped in pte\n");
        return -1;
    }

    /*Risc-v PTE format: PFN << 10 | prot_val*/
    /* If PMD entry's bits 1-3 is non-zeor, then this is the leaf PTE */
    if ((pmd_val(*pmd) & 0xE)) {
        page_addr = pfn_to_phys(pmd_val(*pmd) >> _PAGE_PFN_SHIFT);
        page_offset = vaddr & ~PMD_MASK;
    } else {
        page_addr = pfn_to_phys(pte_pfn(*pte));
        page_offset = vaddr & ~PAGE_MASK;
    }

    paddr = page_addr | page_offset;
    printk("page_addr = %lx, page_offset = %lx\n", page_addr, page_offset);
    printk("vaddr = %lx, paddr = %lx\n", vaddr, paddr);

    printk("!Only valid for linear space! - virt_to_phys(vaddr) = %lx\n", virt_to_phys((unsigned long *)vaddr_in));

    return paddr;
}

void va2pa_test(void)
{
	void *tmp = kmalloc(256, GFP_KERNEL);

	memset(tmp, 0x55, 256);
	va2pa(tmp);
	kfree(tmp);
}
#endif /* IMG_GPU_DEBUG */

static int pvr_probe(struct platform_device *pdev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
	struct drm_device *ddev;
	int ret;

	DRM_DEBUG_DRIVER("device %p\n", &pdev->dev);
	printk("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
	
	ddev = drm_dev_alloc(&pvr_drm_platform_driver, &pdev->dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0))
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);
#else
	if (!ddev)
		return -ENOMEM;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
	/* Needed by drm_platform_set_busid */
	ddev->platformdev = pdev;
#endif

	/*
	 * The load callback, called from drm_dev_register, is deprecated,
	 * because of potential race conditions. Calling the function here,
	 * before calling drm_dev_register, avoids those potential races.
	 */
	BUG_ON(pvr_drm_platform_driver.load != NULL);
	ret = pvr_drm_load(ddev, 0);
	if (ret)
		goto err_drm_dev_put;

	ret = drm_dev_register(ddev, 0);
	if (ret)
		goto err_drm_dev_unload;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
	DRM_INFO("Initialized %s %d.%d.%d %s on minor %d\n",
		pvr_drm_platform_driver.name,
		pvr_drm_platform_driver.major,
		pvr_drm_platform_driver.minor,
		pvr_drm_platform_driver.patchlevel,
		pvr_drm_platform_driver.date,
		ddev->primary->index);
#endif
	return 0;

err_drm_dev_unload:
	pvr_drm_unload(ddev);
err_drm_dev_put:
	drm_dev_put(ddev);
	return	ret;
#else
	DRM_DEBUG_DRIVER("device %p\n", &pdev->dev);

	return drm_platform_init(&pvr_drm_platform_driver, pdev);
#endif
}

static int pvr_remove(struct platform_device *pdev)
{
	struct drm_device *ddev = platform_get_drvdata(pdev);

	DRM_DEBUG_DRIVER("device %p\n", &pdev->dev);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
	drm_dev_unregister(ddev);

	/* The unload callback, called from drm_dev_unregister, is
	 * deprecated. Call the unload function directly.
	 */
	BUG_ON(pvr_drm_platform_driver.unload != NULL);
	pvr_drm_unload(ddev);

	drm_dev_put(ddev);
#else
	drm_put_dev(ddev);
#endif
	return 0;
}

static void pvr_shutdown(struct platform_device *pdev)
{
	struct drm_device *ddev = platform_get_drvdata(pdev);
	struct pvr_drm_private *priv = ddev->dev_private;

	DRM_DEBUG_DRIVER("device %p\n", &pdev->dev);

	PVRSRVDeviceShutdown(priv->dev_node);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
static const struct of_device_id pvr_of_ids[] = {
#if 1// defined(SYS_RGX_OF_COMPATIBLE)
	{ .compatible = SYS_RGX_OF_COMPATIBLE, },
#endif
	{},
};

#if !defined(CHROMIUMOS_KERNEL) || !defined(MODULE)
MODULE_DEVICE_TABLE(of, pvr_of_ids);
#endif
#endif

static struct platform_device_id pvr_platform_ids[] = {
#if defined(SYS_RGX_DEV_NAME)
	{ SYS_RGX_DEV_NAME, 0 },
#endif
	{ }
};

#if !defined(CHROMIUMOS_KERNEL) || !defined(MODULE)
MODULE_DEVICE_TABLE(platform, pvr_platform_ids);
#endif

static struct platform_driver pvr_platform_driver = {
	.driver = {
		.name		= DRVNAME,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
		.of_match_table	= of_match_ptr(pvr_of_ids),
#endif
		.pm		= &pvr_pm_ops,
	},
	.id_table		= pvr_platform_ids,
	.probe			= pvr_probe,
	.remove			= pvr_remove,
	.shutdown		= pvr_shutdown,
};

static int __init pvr_init(void)
{
	int err;

	DRM_DEBUG_DRIVER("\n");

	pvr_drm_platform_driver = pvr_drm_generic_driver;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)) && \
	(LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
	pvr_drm_platform_driver.set_busid = drm_platform_set_busid;
#endif
	printk("@@@@@#################################################\n");
	err = PVRSRVDriverInit();
	if (err)
		return err;

	printk("%s...%d.. compatable:%s\n", __func__, __LINE__, pvr_platform_driver.driver.of_match_table->compatible);
	err = platform_driver_register(&pvr_platform_driver);
	if (err)
		return err;

	return 0;
}

static void __exit pvr_exit(void)
{
	DRM_DEBUG_DRIVER("\n");

	pvr_devices_unregister();
	platform_driver_unregister(&pvr_platform_driver);
	PVRSRVDriverDeinit();

	DRM_DEBUG_DRIVER("done\n");
}

module_init(pvr_init);
//late_initcall(pvr_init);
module_exit(pvr_exit);
