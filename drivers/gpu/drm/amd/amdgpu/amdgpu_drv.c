/**
 * \file amdgpu_drv.c
 * AMD Amdgpu driver
 *
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <drm/drmP.h>
#include <drm/amdgpu_drm.h>
#include <drm/drm_gem.h>
#include "amdgpu_drv.h"

#include <drm/drm_pciids.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/vga_switcheroo.h>
#include "drm_crtc_helper.h"

#include "amdgpu.h"
#include "amdgpu_irq.h"

#include "amdgpu_amdkfd.h"

/*
 * KMS wrapper.
 * - 3.0.0 - initial driver
 * - 3.1.0 - allow reading more status registers (GRBM, SRBM, SDMA, CP)
 */
#define KMS_DRIVER_MAJOR	3
#define KMS_DRIVER_MINOR	1
#define KMS_DRIVER_PATCHLEVEL	0

int amdgpu_vram_limit = 0;
int amdgpu_gart_size = -1; /* auto */
int amdgpu_benchmarking = 0;
int amdgpu_testing = 0;
int amdgpu_audio = -1;
int amdgpu_disp_priority = 0;
int amdgpu_hw_i2c = 0;
int amdgpu_pcie_gen2 = -1;
int amdgpu_msi = -1;
int amdgpu_lockup_timeout = 0;
int amdgpu_dpm = -1;
int amdgpu_smc_load_fw = 1;
int amdgpu_aspm = -1;
int amdgpu_runtime_pm = -1;
int amdgpu_hard_reset = 0;
unsigned amdgpu_ip_block_mask = 0xffffffff;
int amdgpu_bapm = -1;
int amdgpu_deep_color = 0;
int amdgpu_vm_size = 64;
int amdgpu_vm_block_size = -1;
int amdgpu_vm_fault_stop = 0;
int amdgpu_vm_debug = 0;
int amdgpu_exp_hw_support = 0;
int amdgpu_enable_scheduler = 1;
int amdgpu_sched_jobs = 16;
int amdgpu_sched_hw_submission = 2;
int amdgpu_enable_semaphores = 0;

MODULE_PARM_DESC(vramlimit, "Restrict VRAM for testing, in megabytes");
module_param_named(vramlimit, amdgpu_vram_limit, int, 0600);

MODULE_PARM_DESC(gartsize, "Size of PCIE/IGP gart to setup in megabytes (32, 64, etc., -1 = auto)");
module_param_named(gartsize, amdgpu_gart_size, int, 0600);

MODULE_PARM_DESC(benchmark, "Run benchmark");
module_param_named(benchmark, amdgpu_benchmarking, int, 0444);

MODULE_PARM_DESC(test, "Run tests");
module_param_named(test, amdgpu_testing, int, 0444);

MODULE_PARM_DESC(audio, "Audio enable (-1 = auto, 0 = disable, 1 = enable)");
module_param_named(audio, amdgpu_audio, int, 0444);

MODULE_PARM_DESC(disp_priority, "Display Priority (0 = auto, 1 = normal, 2 = high)");
module_param_named(disp_priority, amdgpu_disp_priority, int, 0444);

MODULE_PARM_DESC(hw_i2c, "hw i2c engine enable (0 = disable)");
module_param_named(hw_i2c, amdgpu_hw_i2c, int, 0444);

MODULE_PARM_DESC(pcie_gen2, "PCIE Gen2 mode (-1 = auto, 0 = disable, 1 = enable)");
module_param_named(pcie_gen2, amdgpu_pcie_gen2, int, 0444);

MODULE_PARM_DESC(msi, "MSI support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(msi, amdgpu_msi, int, 0444);

MODULE_PARM_DESC(lockup_timeout, "GPU lockup timeout in ms (default 0 = disable)");
module_param_named(lockup_timeout, amdgpu_lockup_timeout, int, 0444);

MODULE_PARM_DESC(dpm, "DPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(dpm, amdgpu_dpm, int, 0444);

MODULE_PARM_DESC(smc_load_fw, "SMC firmware loading(1 = enable, 0 = disable)");
module_param_named(smc_load_fw, amdgpu_smc_load_fw, int, 0444);

MODULE_PARM_DESC(aspm, "ASPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(aspm, amdgpu_aspm, int, 0444);

MODULE_PARM_DESC(runpm, "PX runtime pm (1 = force enable, 0 = disable, -1 = PX only default)");
module_param_named(runpm, amdgpu_runtime_pm, int, 0444);

MODULE_PARM_DESC(hard_reset, "PCI config reset (1 = force enable, 0 = disable (default))");
module_param_named(hard_reset, amdgpu_hard_reset, int, 0444);

MODULE_PARM_DESC(ip_block_mask, "IP Block Mask (all blocks enabled (default))");
module_param_named(ip_block_mask, amdgpu_ip_block_mask, uint, 0444);

MODULE_PARM_DESC(bapm, "BAPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(bapm, amdgpu_bapm, int, 0444);

MODULE_PARM_DESC(deep_color, "Deep Color support (1 = enable, 0 = disable (default))");
module_param_named(deep_color, amdgpu_deep_color, int, 0444);

MODULE_PARM_DESC(vm_size, "VM address space size in gigabytes (default 64GB)");
module_param_named(vm_size, amdgpu_vm_size, int, 0444);

MODULE_PARM_DESC(vm_block_size, "VM page table size in bits (default depending on vm_size)");
module_param_named(vm_block_size, amdgpu_vm_block_size, int, 0444);

MODULE_PARM_DESC(vm_fault_stop, "Stop on VM fault (0 = never (default), 1 = print first, 2 = always)");
module_param_named(vm_fault_stop, amdgpu_vm_fault_stop, int, 0444);

MODULE_PARM_DESC(vm_debug, "Debug VM handling (0 = disabled (default), 1 = enabled)");
module_param_named(vm_debug, amdgpu_vm_debug, int, 0644);

MODULE_PARM_DESC(exp_hw_support, "experimental hw support (1 = enable, 0 = disable (default))");
module_param_named(exp_hw_support, amdgpu_exp_hw_support, int, 0444);

MODULE_PARM_DESC(enable_scheduler, "enable SW GPU scheduler (1 = enable (default), 0 = disable)");
module_param_named(enable_scheduler, amdgpu_enable_scheduler, int, 0444);

MODULE_PARM_DESC(sched_jobs, "the max number of jobs supported in the sw queue (default 16)");
module_param_named(sched_jobs, amdgpu_sched_jobs, int, 0444);

MODULE_PARM_DESC(sched_hw_submission, "the max number of HW submissions (default 2)");
module_param_named(sched_hw_submission, amdgpu_sched_hw_submission, int, 0444);

MODULE_PARM_DESC(enable_semaphores, "Enable semaphores (1 = enable, 0 = disable (default))");
module_param_named(enable_semaphores, amdgpu_enable_semaphores, int, 0644);

static struct pci_device_id pciidlist[] = {
#ifdef CONFIG_DRM_AMDGPU_CIK
	/* Kaveri */
	{0x1002, 0x1304, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x1305, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1306, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x1307, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1309, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x130A, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x130B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x130C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x130D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x130E, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x130F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1310, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1311, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1312, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1313, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1315, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1316, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1317, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x1318, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x131B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x131C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x131D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	/* Bonaire */
	{0x1002, 0x6640, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE|AMD_IS_MOBILITY},
	{0x1002, 0x6641, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE|AMD_IS_MOBILITY},
	{0x1002, 0x6646, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE|AMD_IS_MOBILITY},
	{0x1002, 0x6647, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE|AMD_IS_MOBILITY},
	{0x1002, 0x6649, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	{0x1002, 0x6650, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	{0x1002, 0x6651, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	{0x1002, 0x6658, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	{0x1002, 0x665c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	{0x1002, 0x665d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	{0x1002, 0x665f, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	/* Hawaii */
	{0x1002, 0x67A0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67A1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67A2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67A8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67A9, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67AA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67B0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67B1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67B8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67B9, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67BA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67BE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	/* Kabini */
	{0x1002, 0x9830, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9831, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x9832, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9833, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x9834, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9835, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x9836, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9837, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x9838, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9839, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x983a, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x983b, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x983c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x983d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x983e, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x983f, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	/* mullins */
	{0x1002, 0x9850, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9851, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9852, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9853, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9854, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9855, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9856, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9857, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9858, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9859, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x985A, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x985B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x985C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x985D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x985E, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x985F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
#endif
	/* topaz */
	{0x1002, 0x6900, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TOPAZ},
	{0x1002, 0x6901, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TOPAZ},
	{0x1002, 0x6902, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TOPAZ},
	{0x1002, 0x6903, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TOPAZ},
	{0x1002, 0x6907, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TOPAZ},
	/* tonga */
	{0x1002, 0x6920, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x6921, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x6928, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x6929, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x692B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x692F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x6930, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x6938, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x6939, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	/* fiji */
	{0x1002, 0x7300, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_FIJI},
	/* carrizo */
	{0x1002, 0x9870, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_CARRIZO|AMD_IS_APU},
	{0x1002, 0x9874, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_CARRIZO|AMD_IS_APU},
	{0x1002, 0x9875, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_CARRIZO|AMD_IS_APU},
	{0x1002, 0x9876, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_CARRIZO|AMD_IS_APU},
	{0x1002, 0x9877, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_CARRIZO|AMD_IS_APU},
	/* stoney */
	{0x1002, 0x98E4, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_STONEY|AMD_IS_APU},

	{0, 0, 0}
};

MODULE_DEVICE_TABLE(pci, pciidlist);

static struct drm_driver kms_driver;

static int amdgpu_kick_out_firmware_fb(struct pci_dev *pdev)
{
	struct apertures_struct *ap;
	bool primary = false;

	ap = alloc_apertures(1);
	if (!ap)
		return -ENOMEM;

	ap->ranges[0].base = pci_resource_start(pdev, 0);
	ap->ranges[0].size = pci_resource_len(pdev, 0);

#ifdef CONFIG_X86
	primary = pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW;
#endif
	remove_conflicting_framebuffers(ap, "amdgpudrmfb", primary);
	kfree(ap);

	return 0;
}

static int amdgpu_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	unsigned long flags = ent->driver_data;
	int ret;

	if ((flags & AMD_EXP_HW_SUPPORT) && !amdgpu_exp_hw_support) {
		DRM_INFO("This hardware requires experimental hardware support.\n"
			 "See modparam exp_hw_support\n");
		return -ENODEV;
	}

	/* Get rid of things like offb */
	ret = amdgpu_kick_out_firmware_fb(pdev);
	if (ret)
		return ret;

	return drm_get_pci_dev(pdev, ent, &kms_driver);
}

static void
amdgpu_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static int amdgpu_pmops_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	return amdgpu_suspend_kms(drm_dev, true, true);
}

static int amdgpu_pmops_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	return amdgpu_resume_kms(drm_dev, true, true);
}

static int amdgpu_pmops_freeze(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	return amdgpu_suspend_kms(drm_dev, false, true);
}

static int amdgpu_pmops_thaw(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	return amdgpu_resume_kms(drm_dev, false, true);
}

static int amdgpu_pmops_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int ret;

	if (!amdgpu_device_is_px(drm_dev)) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	drm_dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
	drm_kms_helper_poll_disable(drm_dev);
	vga_switcheroo_set_dynamic_switch(pdev, VGA_SWITCHEROO_OFF);

	ret = amdgpu_suspend_kms(drm_dev, false, false);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_ignore_hotplug(pdev);
	pci_set_power_state(pdev, PCI_D3cold);
	drm_dev->switch_power_state = DRM_SWITCH_POWER_DYNAMIC_OFF;

	return 0;
}

static int amdgpu_pmops_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	int ret;

	if (!amdgpu_device_is_px(drm_dev))
		return -EINVAL;

	drm_dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	pci_set_master(pdev);

	ret = amdgpu_resume_kms(drm_dev, false, false);
	drm_kms_helper_poll_enable(drm_dev);
	vga_switcheroo_set_dynamic_switch(pdev, VGA_SWITCHEROO_ON);
	drm_dev->switch_power_state = DRM_SWITCH_POWER_ON;
	return 0;
}

static int amdgpu_pmops_runtime_idle(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct drm_crtc *crtc;

	if (!amdgpu_device_is_px(drm_dev)) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	list_for_each_entry(crtc, &drm_dev->mode_config.crtc_list, head) {
		if (crtc->enabled) {
			DRM_DEBUG_DRIVER("failing to power off - crtc active\n");
			return -EBUSY;
		}
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_autosuspend(dev);
	/* we don't want the main rpm_idle to call suspend - we want to autosuspend */
	return 1;
}

long amdgpu_drm_ioctl(struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev;
	long ret;
	dev = file_priv->minor->dev;
	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0)
		return ret;

	ret = drm_ioctl(filp, cmd, arg);

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
	return ret;
}

static const struct dev_pm_ops amdgpu_pm_ops = {
	.suspend = amdgpu_pmops_suspend,
	.resume = amdgpu_pmops_resume,
	.freeze = amdgpu_pmops_freeze,
	.thaw = amdgpu_pmops_thaw,
	.poweroff = amdgpu_pmops_freeze,
	.restore = amdgpu_pmops_resume,
	.runtime_suspend = amdgpu_pmops_runtime_suspend,
	.runtime_resume = amdgpu_pmops_runtime_resume,
	.runtime_idle = amdgpu_pmops_runtime_idle,
};

static const struct file_operations amdgpu_driver_kms_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = amdgpu_drm_ioctl,
	.mmap = amdgpu_mmap,
	.poll = drm_poll,
	.read = drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl = amdgpu_kms_compat_ioctl,
#endif
};

static struct drm_driver kms_driver = {
	.driver_features =
	    DRIVER_USE_AGP |
	    DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | DRIVER_GEM |
	    DRIVER_PRIME | DRIVER_RENDER,
	.dev_priv_size = 0,
	.load = amdgpu_driver_load_kms,
	.open = amdgpu_driver_open_kms,
	.preclose = amdgpu_driver_preclose_kms,
	.postclose = amdgpu_driver_postclose_kms,
	.lastclose = amdgpu_driver_lastclose_kms,
	.set_busid = drm_pci_set_busid,
	.unload = amdgpu_driver_unload_kms,
	.get_vblank_counter = amdgpu_get_vblank_counter_kms,
	.enable_vblank = amdgpu_enable_vblank_kms,
	.disable_vblank = amdgpu_disable_vblank_kms,
	.get_vblank_timestamp = amdgpu_get_vblank_timestamp_kms,
	.get_scanout_position = amdgpu_get_crtc_scanoutpos,
#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = amdgpu_debugfs_init,
	.debugfs_cleanup = amdgpu_debugfs_cleanup,
#endif
	.irq_preinstall = amdgpu_irq_preinstall,
	.irq_postinstall = amdgpu_irq_postinstall,
	.irq_uninstall = amdgpu_irq_uninstall,
	.irq_handler = amdgpu_irq_handler,
	.ioctls = amdgpu_ioctls_kms,
	.gem_free_object = amdgpu_gem_object_free,
	.gem_open_object = amdgpu_gem_object_open,
	.gem_close_object = amdgpu_gem_object_close,
	.dumb_create = amdgpu_mode_dumb_create,
	.dumb_map_offset = amdgpu_mode_dumb_mmap,
	.dumb_destroy = drm_gem_dumb_destroy,
	.fops = &amdgpu_driver_kms_fops,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = amdgpu_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_pin = amdgpu_gem_prime_pin,
	.gem_prime_unpin = amdgpu_gem_prime_unpin,
	.gem_prime_res_obj = amdgpu_gem_prime_res_obj,
	.gem_prime_get_sg_table = amdgpu_gem_prime_get_sg_table,
	.gem_prime_import_sg_table = amdgpu_gem_prime_import_sg_table,
	.gem_prime_vmap = amdgpu_gem_prime_vmap,
	.gem_prime_vunmap = amdgpu_gem_prime_vunmap,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = KMS_DRIVER_MAJOR,
	.minor = KMS_DRIVER_MINOR,
	.patchlevel = KMS_DRIVER_PATCHLEVEL,
};

static struct drm_driver *driver;
static struct pci_driver *pdriver;

static struct pci_driver amdgpu_kms_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = amdgpu_pci_probe,
	.remove = amdgpu_pci_remove,
	.driver.pm = &amdgpu_pm_ops,
};

static int __init amdgpu_init(void)
{
#ifdef CONFIG_VGA_CONSOLE
	if (vgacon_text_force()) {
		DRM_ERROR("VGACON disables amdgpu kernel modesetting.\n");
		return -EINVAL;
	}
#endif
	DRM_INFO("amdgpu kernel modesetting enabled.\n");
	driver = &kms_driver;
	pdriver = &amdgpu_kms_pci_driver;
	driver->driver_features |= DRIVER_MODESET;
	driver->num_ioctls = amdgpu_max_kms_ioctl;
	amdgpu_register_atpx_handler();

	amdgpu_amdkfd_init();

	/* let modprobe override vga console setting */
	return drm_pci_init(driver, pdriver);
}

static void __exit amdgpu_exit(void)
{
	amdgpu_amdkfd_fini();
	drm_pci_exit(driver, pdriver);
	amdgpu_unregister_atpx_handler();
}

module_init(amdgpu_init);
module_exit(amdgpu_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
