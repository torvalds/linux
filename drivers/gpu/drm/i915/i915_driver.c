/* i915_drv.c -- i830,i845,i855,i865,i915 driver -*- linux-c -*-
 */
/*
 *
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/oom.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/pnp.h>
#include <linux/slab.h>
#include <linux/vga_switcheroo.h>
#include <linux/vt.h>

#include <drm/drm_aperture.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>

#include "display/intel_acpi.h"
#include "display/intel_bw.h"
#include "display/intel_cdclk.h"
#include "display/intel_display_types.h"
#include "display/intel_dmc.h"
#include "display/intel_dp.h"
#include "display/intel_dpt.h"
#include "display/intel_fbdev.h"
#include "display/intel_hotplug.h"
#include "display/intel_overlay.h"
#include "display/intel_pch_refclk.h"
#include "display/intel_pipe_crc.h"
#include "display/intel_pps.h"
#include "display/intel_sprite.h"
#include "display/intel_vga.h"

#include "gem/i915_gem_context.h"
#include "gem/i915_gem_create.h"
#include "gem/i915_gem_dmabuf.h"
#include "gem/i915_gem_ioctls.h"
#include "gem/i915_gem_mman.h"
#include "gem/i915_gem_pm.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_rc6.h"

#include "pxp/intel_pxp_pm.h"

#include "i915_debugfs.h"
#include "i915_driver.h"
#include "i915_drv.h"
#include "i915_getparam.h"
#include "i915_ioc32.h"
#include "i915_ioctl.h"
#include "i915_irq.h"
#include "i915_memcpy.h"
#include "i915_perf.h"
#include "i915_query.h"
#include "i915_suspend.h"
#include "i915_switcheroo.h"
#include "i915_sysfs.h"
#include "i915_vgpu.h"
#include "intel_dram.h"
#include "intel_gvt.h"
#include "intel_memory_region.h"
#include "intel_pci_config.h"
#include "intel_pcode.h"
#include "intel_pm.h"
#include "intel_region_ttm.h"
#include "vlv_suspend.h"

static const struct drm_driver i915_drm_driver;

static int i915_get_bridge_dev(struct drm_i915_private *dev_priv)
{
	int domain = pci_domain_nr(to_pci_dev(dev_priv->drm.dev)->bus);

	dev_priv->bridge_dev =
		pci_get_domain_bus_and_slot(domain, 0, PCI_DEVFN(0, 0));
	if (!dev_priv->bridge_dev) {
		drm_err(&dev_priv->drm, "bridge device not found\n");
		return -EIO;
	}
	return 0;
}

/* Allocate space for the MCH regs if needed, return nonzero on error */
static int
intel_alloc_mchbar_resource(struct drm_i915_private *dev_priv)
{
	int reg = GRAPHICS_VER(dev_priv) >= 4 ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp_lo, temp_hi = 0;
	u64 mchbar_addr;
	int ret;

	if (GRAPHICS_VER(dev_priv) >= 4)
		pci_read_config_dword(dev_priv->bridge_dev, reg + 4, &temp_hi);
	pci_read_config_dword(dev_priv->bridge_dev, reg, &temp_lo);
	mchbar_addr = ((u64)temp_hi << 32) | temp_lo;

	/* If ACPI doesn't have it, assume we need to allocate it ourselves */
#ifdef CONFIG_PNP
	if (mchbar_addr &&
	    pnp_range_reserved(mchbar_addr, mchbar_addr + MCHBAR_SIZE))
		return 0;
#endif

	/* Get some space for it */
	dev_priv->mch_res.name = "i915 MCHBAR";
	dev_priv->mch_res.flags = IORESOURCE_MEM;
	ret = pci_bus_alloc_resource(dev_priv->bridge_dev->bus,
				     &dev_priv->mch_res,
				     MCHBAR_SIZE, MCHBAR_SIZE,
				     PCIBIOS_MIN_MEM,
				     0, pcibios_align_resource,
				     dev_priv->bridge_dev);
	if (ret) {
		drm_dbg(&dev_priv->drm, "failed bus alloc: %d\n", ret);
		dev_priv->mch_res.start = 0;
		return ret;
	}

	if (GRAPHICS_VER(dev_priv) >= 4)
		pci_write_config_dword(dev_priv->bridge_dev, reg + 4,
				       upper_32_bits(dev_priv->mch_res.start));

	pci_write_config_dword(dev_priv->bridge_dev, reg,
			       lower_32_bits(dev_priv->mch_res.start));
	return 0;
}

/* Setup MCHBAR if possible, return true if we should disable it again */
static void
intel_setup_mchbar(struct drm_i915_private *dev_priv)
{
	int mchbar_reg = GRAPHICS_VER(dev_priv) >= 4 ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp;
	bool enabled;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		return;

	dev_priv->mchbar_need_disable = false;

	if (IS_I915G(dev_priv) || IS_I915GM(dev_priv)) {
		pci_read_config_dword(dev_priv->bridge_dev, DEVEN, &temp);
		enabled = !!(temp & DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(dev_priv->bridge_dev, mchbar_reg, &temp);
		enabled = temp & 1;
	}

	/* If it's already enabled, don't have to do anything */
	if (enabled)
		return;

	if (intel_alloc_mchbar_resource(dev_priv))
		return;

	dev_priv->mchbar_need_disable = true;

	/* Space is allocated or reserved, so enable it. */
	if (IS_I915G(dev_priv) || IS_I915GM(dev_priv)) {
		pci_write_config_dword(dev_priv->bridge_dev, DEVEN,
				       temp | DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(dev_priv->bridge_dev, mchbar_reg, &temp);
		pci_write_config_dword(dev_priv->bridge_dev, mchbar_reg, temp | 1);
	}
}

static void
intel_teardown_mchbar(struct drm_i915_private *dev_priv)
{
	int mchbar_reg = GRAPHICS_VER(dev_priv) >= 4 ? MCHBAR_I965 : MCHBAR_I915;

	if (dev_priv->mchbar_need_disable) {
		if (IS_I915G(dev_priv) || IS_I915GM(dev_priv)) {
			u32 deven_val;

			pci_read_config_dword(dev_priv->bridge_dev, DEVEN,
					      &deven_val);
			deven_val &= ~DEVEN_MCHBAR_EN;
			pci_write_config_dword(dev_priv->bridge_dev, DEVEN,
					       deven_val);
		} else {
			u32 mchbar_val;

			pci_read_config_dword(dev_priv->bridge_dev, mchbar_reg,
					      &mchbar_val);
			mchbar_val &= ~1;
			pci_write_config_dword(dev_priv->bridge_dev, mchbar_reg,
					       mchbar_val);
		}
	}

	if (dev_priv->mch_res.start)
		release_resource(&dev_priv->mch_res);
}

static int i915_workqueues_init(struct drm_i915_private *dev_priv)
{
	/*
	 * The i915 workqueue is primarily used for batched retirement of
	 * requests (and thus managing bo) once the task has been completed
	 * by the GPU. i915_retire_requests() is called directly when we
	 * need high-priority retirement, such as waiting for an explicit
	 * bo.
	 *
	 * It is also used for periodic low-priority events, such as
	 * idle-timers and recording error state.
	 *
	 * All tasks on the workqueue are expected to acquire the dev mutex
	 * so there is no point in running more than one instance of the
	 * workqueue at any time.  Use an ordered one.
	 */
	dev_priv->wq = alloc_ordered_workqueue("i915", 0);
	if (dev_priv->wq == NULL)
		goto out_err;

	dev_priv->hotplug.dp_wq = alloc_ordered_workqueue("i915-dp", 0);
	if (dev_priv->hotplug.dp_wq == NULL)
		goto out_free_wq;

	return 0;

out_free_wq:
	destroy_workqueue(dev_priv->wq);
out_err:
	drm_err(&dev_priv->drm, "Failed to allocate workqueues.\n");

	return -ENOMEM;
}

static void i915_workqueues_cleanup(struct drm_i915_private *dev_priv)
{
	destroy_workqueue(dev_priv->hotplug.dp_wq);
	destroy_workqueue(dev_priv->wq);
}

/*
 * We don't keep the workarounds for pre-production hardware, so we expect our
 * driver to fail on these machines in one way or another. A little warning on
 * dmesg may help both the user and the bug triagers.
 *
 * Our policy for removing pre-production workarounds is to keep the
 * current gen workarounds as a guide to the bring-up of the next gen
 * (workarounds have a habit of persisting!). Anything older than that
 * should be removed along with the complications they introduce.
 */
static void intel_detect_preproduction_hw(struct drm_i915_private *dev_priv)
{
	bool pre = false;

	pre |= IS_HSW_EARLY_SDV(dev_priv);
	pre |= IS_SKYLAKE(dev_priv) && INTEL_REVID(dev_priv) < 0x6;
	pre |= IS_BROXTON(dev_priv) && INTEL_REVID(dev_priv) < 0xA;
	pre |= IS_KABYLAKE(dev_priv) && INTEL_REVID(dev_priv) < 0x1;
	pre |= IS_GEMINILAKE(dev_priv) && INTEL_REVID(dev_priv) < 0x3;
	pre |= IS_ICELAKE(dev_priv) && INTEL_REVID(dev_priv) < 0x7;

	if (pre) {
		drm_err(&dev_priv->drm, "This is a pre-production stepping. "
			  "It may not be fully functional.\n");
		add_taint(TAINT_MACHINE_CHECK, LOCKDEP_STILL_OK);
	}
}

static void sanitize_gpu(struct drm_i915_private *i915)
{
	if (!INTEL_INFO(i915)->gpu_reset_clobbers_display)
		__intel_gt_reset(to_gt(i915), ALL_ENGINES);
}

/**
 * i915_driver_early_probe - setup state not requiring device access
 * @dev_priv: device private
 *
 * Initialize everything that is a "SW-only" state, that is state not
 * requiring accessing the device or exposing the driver via kernel internal
 * or userspace interfaces. Example steps belonging here: lock initialization,
 * system memory allocation, setting up device specific attributes and
 * function hooks not requiring accessing the device.
 */
static int i915_driver_early_probe(struct drm_i915_private *dev_priv)
{
	int ret = 0;

	if (i915_inject_probe_failure(dev_priv))
		return -ENODEV;

	intel_device_info_subplatform_init(dev_priv);
	intel_step_init(dev_priv);

	intel_gt_init_early(to_gt(dev_priv), dev_priv);
	intel_uncore_mmio_debug_init_early(&dev_priv->mmio_debug);
	intel_uncore_init_early(&dev_priv->uncore, to_gt(dev_priv));

	spin_lock_init(&dev_priv->irq_lock);
	spin_lock_init(&dev_priv->gpu_error.lock);
	mutex_init(&dev_priv->backlight_lock);

	mutex_init(&dev_priv->sb_lock);
	cpu_latency_qos_add_request(&dev_priv->sb_qos, PM_QOS_DEFAULT_VALUE);

	mutex_init(&dev_priv->audio.mutex);
	mutex_init(&dev_priv->wm.wm_mutex);
	mutex_init(&dev_priv->pps_mutex);
	mutex_init(&dev_priv->hdcp_comp_mutex);

	i915_memcpy_init_early(dev_priv);
	intel_runtime_pm_init_early(&dev_priv->runtime_pm);

	ret = i915_workqueues_init(dev_priv);
	if (ret < 0)
		return ret;

	ret = vlv_suspend_init(dev_priv);
	if (ret < 0)
		goto err_workqueues;

	ret = intel_region_ttm_device_init(dev_priv);
	if (ret)
		goto err_ttm;

	intel_wopcm_init_early(&dev_priv->wopcm);

	__intel_gt_init_early(to_gt(dev_priv), dev_priv);

	i915_gem_init_early(dev_priv);

	/* This must be called before any calls to HAS_PCH_* */
	intel_detect_pch(dev_priv);

	intel_pm_setup(dev_priv);
	ret = intel_power_domains_init(dev_priv);
	if (ret < 0)
		goto err_gem;
	intel_irq_init(dev_priv);
	intel_init_display_hooks(dev_priv);
	intel_init_clock_gating_hooks(dev_priv);

	intel_detect_preproduction_hw(dev_priv);

	return 0;

err_gem:
	i915_gem_cleanup_early(dev_priv);
	intel_gt_driver_late_release(to_gt(dev_priv));
	intel_region_ttm_device_fini(dev_priv);
err_ttm:
	vlv_suspend_cleanup(dev_priv);
err_workqueues:
	i915_workqueues_cleanup(dev_priv);
	return ret;
}

/**
 * i915_driver_late_release - cleanup the setup done in
 *			       i915_driver_early_probe()
 * @dev_priv: device private
 */
static void i915_driver_late_release(struct drm_i915_private *dev_priv)
{
	intel_irq_fini(dev_priv);
	intel_power_domains_cleanup(dev_priv);
	i915_gem_cleanup_early(dev_priv);
	intel_gt_driver_late_release(to_gt(dev_priv));
	intel_region_ttm_device_fini(dev_priv);
	vlv_suspend_cleanup(dev_priv);
	i915_workqueues_cleanup(dev_priv);

	cpu_latency_qos_remove_request(&dev_priv->sb_qos);
	mutex_destroy(&dev_priv->sb_lock);

	i915_params_free(&dev_priv->params);
}

/**
 * i915_driver_mmio_probe - setup device MMIO
 * @dev_priv: device private
 *
 * Setup minimal device state necessary for MMIO accesses later in the
 * initialization sequence. The setup here should avoid any other device-wide
 * side effects or exposing the driver via kernel internal or user space
 * interfaces.
 */
static int i915_driver_mmio_probe(struct drm_i915_private *dev_priv)
{
	int ret;

	if (i915_inject_probe_failure(dev_priv))
		return -ENODEV;

	ret = i915_get_bridge_dev(dev_priv);
	if (ret < 0)
		return ret;

	ret = intel_uncore_setup_mmio(&dev_priv->uncore);
	if (ret < 0)
		goto err_bridge;

	ret = intel_uncore_init_mmio(&dev_priv->uncore);
	if (ret)
		goto err_mmio;

	/* Try to make sure MCHBAR is enabled before poking at it */
	intel_setup_mchbar(dev_priv);
	intel_device_info_runtime_init(dev_priv);

	ret = intel_gt_init_mmio(to_gt(dev_priv));
	if (ret)
		goto err_uncore;

	/* As early as possible, scrub existing GPU state before clobbering */
	sanitize_gpu(dev_priv);

	return 0;

err_uncore:
	intel_teardown_mchbar(dev_priv);
	intel_uncore_fini_mmio(&dev_priv->uncore);
err_mmio:
	intel_uncore_cleanup_mmio(&dev_priv->uncore);
err_bridge:
	pci_dev_put(dev_priv->bridge_dev);

	return ret;
}

/**
 * i915_driver_mmio_release - cleanup the setup done in i915_driver_mmio_probe()
 * @dev_priv: device private
 */
static void i915_driver_mmio_release(struct drm_i915_private *dev_priv)
{
	intel_teardown_mchbar(dev_priv);
	intel_uncore_fini_mmio(&dev_priv->uncore);
	intel_uncore_cleanup_mmio(&dev_priv->uncore);
	pci_dev_put(dev_priv->bridge_dev);
}

static void intel_sanitize_options(struct drm_i915_private *dev_priv)
{
	intel_gvt_sanitize_options(dev_priv);
}

/**
 * i915_set_dma_info - set all relevant PCI dma info as configured for the
 * platform
 * @i915: valid i915 instance
 *
 * Set the dma max segment size, device and coherent masks.  The dma mask set
 * needs to occur before i915_ggtt_probe_hw.
 *
 * A couple of platforms have special needs.  Address them as well.
 *
 */
static int i915_set_dma_info(struct drm_i915_private *i915)
{
	unsigned int mask_size = INTEL_INFO(i915)->dma_mask_size;
	int ret;

	GEM_BUG_ON(!mask_size);

	/*
	 * We don't have a max segment size, so set it to the max so sg's
	 * debugging layer doesn't complain
	 */
	dma_set_max_seg_size(i915->drm.dev, UINT_MAX);

	ret = dma_set_mask(i915->drm.dev, DMA_BIT_MASK(mask_size));
	if (ret)
		goto mask_err;

	/* overlay on gen2 is broken and can't address above 1G */
	if (GRAPHICS_VER(i915) == 2)
		mask_size = 30;

	/*
	 * 965GM sometimes incorrectly writes to hardware status page (HWS)
	 * using 32bit addressing, overwriting memory if HWS is located
	 * above 4GB.
	 *
	 * The documentation also mentions an issue with undefined
	 * behaviour if any general state is accessed within a page above 4GB,
	 * which also needs to be handled carefully.
	 */
	if (IS_I965G(i915) || IS_I965GM(i915))
		mask_size = 32;

	ret = dma_set_coherent_mask(i915->drm.dev, DMA_BIT_MASK(mask_size));
	if (ret)
		goto mask_err;

	return 0;

mask_err:
	drm_err(&i915->drm, "Can't set DMA mask/consistent mask (%d)\n", ret);
	return ret;
}

/**
 * i915_driver_hw_probe - setup state requiring device access
 * @dev_priv: device private
 *
 * Setup state that requires accessing the device, but doesn't require
 * exposing the driver via kernel internal or userspace interfaces.
 */
static int i915_driver_hw_probe(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	int ret;

	if (i915_inject_probe_failure(dev_priv))
		return -ENODEV;

	if (HAS_PPGTT(dev_priv)) {
		if (intel_vgpu_active(dev_priv) &&
		    !intel_vgpu_has_full_ppgtt(dev_priv)) {
			i915_report_error(dev_priv,
					  "incompatible vGPU found, support for isolated ppGTT required\n");
			return -ENXIO;
		}
	}

	if (HAS_EXECLISTS(dev_priv)) {
		/*
		 * Older GVT emulation depends upon intercepting CSB mmio,
		 * which we no longer use, preferring to use the HWSP cache
		 * instead.
		 */
		if (intel_vgpu_active(dev_priv) &&
		    !intel_vgpu_has_hwsp_emulation(dev_priv)) {
			i915_report_error(dev_priv,
					  "old vGPU host found, support for HWSP emulation required\n");
			return -ENXIO;
		}
	}

	intel_sanitize_options(dev_priv);

	/* needs to be done before ggtt probe */
	intel_dram_edram_detect(dev_priv);

	ret = i915_set_dma_info(dev_priv);
	if (ret)
		return ret;

	i915_perf_init(dev_priv);

	ret = i915_ggtt_probe_hw(dev_priv);
	if (ret)
		goto err_perf;

	ret = drm_aperture_remove_conflicting_pci_framebuffers(pdev, dev_priv->drm.driver);
	if (ret)
		goto err_ggtt;

	ret = i915_ggtt_init_hw(dev_priv);
	if (ret)
		goto err_ggtt;

	ret = intel_memory_regions_hw_probe(dev_priv);
	if (ret)
		goto err_ggtt;

	intel_gt_init_hw_early(to_gt(dev_priv), &dev_priv->ggtt);

	ret = intel_gt_probe_lmem(to_gt(dev_priv));
	if (ret)
		goto err_mem_regions;

	ret = i915_ggtt_enable_hw(dev_priv);
	if (ret) {
		drm_err(&dev_priv->drm, "failed to enable GGTT\n");
		goto err_mem_regions;
	}

	pci_set_master(pdev);

	/* On the 945G/GM, the chipset reports the MSI capability on the
	 * integrated graphics even though the support isn't actually there
	 * according to the published specs.  It doesn't appear to function
	 * correctly in testing on 945G.
	 * This may be a side effect of MSI having been made available for PEG
	 * and the registers being closely associated.
	 *
	 * According to chipset errata, on the 965GM, MSI interrupts may
	 * be lost or delayed, and was defeatured. MSI interrupts seem to
	 * get lost on g4x as well, and interrupt delivery seems to stay
	 * properly dead afterwards. So we'll just disable them for all
	 * pre-gen5 chipsets.
	 *
	 * dp aux and gmbus irq on gen4 seems to be able to generate legacy
	 * interrupts even when in MSI mode. This results in spurious
	 * interrupt warnings if the legacy irq no. is shared with another
	 * device. The kernel then disables that interrupt source and so
	 * prevents the other device from working properly.
	 */
	if (GRAPHICS_VER(dev_priv) >= 5) {
		if (pci_enable_msi(pdev) < 0)
			drm_dbg(&dev_priv->drm, "can't enable MSI");
	}

	ret = intel_gvt_init(dev_priv);
	if (ret)
		goto err_msi;

	intel_opregion_setup(dev_priv);

	ret = intel_pcode_init(dev_priv);
	if (ret)
		goto err_msi;

	/*
	 * Fill the dram structure to get the system dram info. This will be
	 * used for memory latency calculation.
	 */
	intel_dram_detect(dev_priv);

	intel_bw_init_hw(dev_priv);

	return 0;

err_msi:
	if (pdev->msi_enabled)
		pci_disable_msi(pdev);
err_mem_regions:
	intel_memory_regions_driver_release(dev_priv);
err_ggtt:
	i915_ggtt_driver_release(dev_priv);
	i915_gem_drain_freed_objects(dev_priv);
	i915_ggtt_driver_late_release(dev_priv);
err_perf:
	i915_perf_fini(dev_priv);
	return ret;
}

/**
 * i915_driver_hw_remove - cleanup the setup done in i915_driver_hw_probe()
 * @dev_priv: device private
 */
static void i915_driver_hw_remove(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);

	i915_perf_fini(dev_priv);

	if (pdev->msi_enabled)
		pci_disable_msi(pdev);
}

/**
 * i915_driver_register - register the driver with the rest of the system
 * @dev_priv: device private
 *
 * Perform any steps necessary to make the driver available via kernel
 * internal or userspace interfaces.
 */
static void i915_driver_register(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;

	i915_gem_driver_register(dev_priv);
	i915_pmu_register(dev_priv);

	intel_vgpu_register(dev_priv);

	/* Reveal our presence to userspace */
	if (drm_dev_register(dev, 0)) {
		drm_err(&dev_priv->drm,
			"Failed to register driver for userspace access!\n");
		return;
	}

	i915_debugfs_register(dev_priv);
	i915_setup_sysfs(dev_priv);

	/* Depends on sysfs having been initialized */
	i915_perf_register(dev_priv);

	intel_gt_driver_register(to_gt(dev_priv));

	intel_display_driver_register(dev_priv);

	intel_power_domains_enable(dev_priv);
	intel_runtime_pm_enable(&dev_priv->runtime_pm);

	intel_register_dsm_handler();

	if (i915_switcheroo_register(dev_priv))
		drm_err(&dev_priv->drm, "Failed to register vga switcheroo!\n");
}

/**
 * i915_driver_unregister - cleanup the registration done in i915_driver_regiser()
 * @dev_priv: device private
 */
static void i915_driver_unregister(struct drm_i915_private *dev_priv)
{
	i915_switcheroo_unregister(dev_priv);

	intel_unregister_dsm_handler();

	intel_runtime_pm_disable(&dev_priv->runtime_pm);
	intel_power_domains_disable(dev_priv);

	intel_display_driver_unregister(dev_priv);

	intel_gt_driver_unregister(to_gt(dev_priv));

	i915_perf_unregister(dev_priv);
	i915_pmu_unregister(dev_priv);

	i915_teardown_sysfs(dev_priv);
	drm_dev_unplug(&dev_priv->drm);

	i915_gem_driver_unregister(dev_priv);
}

void
i915_print_iommu_status(struct drm_i915_private *i915, struct drm_printer *p)
{
	drm_printf(p, "iommu: %s\n", enableddisabled(intel_vtd_active(i915)));
}

static void i915_welcome_messages(struct drm_i915_private *dev_priv)
{
	if (drm_debug_enabled(DRM_UT_DRIVER)) {
		struct drm_printer p = drm_debug_printer("i915 device info:");

		drm_printf(&p, "pciid=0x%04x rev=0x%02x platform=%s (subplatform=0x%x) gen=%i\n",
			   INTEL_DEVID(dev_priv),
			   INTEL_REVID(dev_priv),
			   intel_platform_name(INTEL_INFO(dev_priv)->platform),
			   intel_subplatform(RUNTIME_INFO(dev_priv),
					     INTEL_INFO(dev_priv)->platform),
			   GRAPHICS_VER(dev_priv));

		intel_device_info_print_static(INTEL_INFO(dev_priv), &p);
		intel_device_info_print_runtime(RUNTIME_INFO(dev_priv), &p);
		i915_print_iommu_status(dev_priv, &p);
		intel_gt_info_print(&to_gt(dev_priv)->info, &p);
	}

	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG))
		drm_info(&dev_priv->drm, "DRM_I915_DEBUG enabled\n");
	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		drm_info(&dev_priv->drm, "DRM_I915_DEBUG_GEM enabled\n");
	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM))
		drm_info(&dev_priv->drm,
			 "DRM_I915_DEBUG_RUNTIME_PM enabled\n");
}

static struct drm_i915_private *
i915_driver_create(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct intel_device_info *match_info =
		(struct intel_device_info *)ent->driver_data;
	struct intel_device_info *device_info;
	struct drm_i915_private *i915;

	i915 = devm_drm_dev_alloc(&pdev->dev, &i915_drm_driver,
				  struct drm_i915_private, drm);
	if (IS_ERR(i915))
		return i915;

	pci_set_drvdata(pdev, i915);

	/* Device parameters start as a copy of module parameters. */
	i915_params_copy(&i915->params, &i915_modparams);

	/* Setup the write-once "constant" device info */
	device_info = mkwrite_device_info(i915);
	memcpy(device_info, match_info, sizeof(*device_info));
	RUNTIME_INFO(i915)->device_id = pdev->device;

	return i915;
}

/**
 * i915_driver_probe - setup chip and create an initial config
 * @pdev: PCI device
 * @ent: matching PCI ID entry
 *
 * The driver probe routine has to do several things:
 *   - drive output discovery via intel_modeset_init()
 *   - initialize the memory manager
 *   - allocate initial config memory
 *   - setup the DRM framebuffer with the allocated memory
 */
int i915_driver_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct intel_device_info *match_info =
		(struct intel_device_info *)ent->driver_data;
	struct drm_i915_private *i915;
	int ret;

	i915 = i915_driver_create(pdev, ent);
	if (IS_ERR(i915))
		return PTR_ERR(i915);

	/* Disable nuclear pageflip by default on pre-ILK */
	if (!i915->params.nuclear_pageflip && match_info->graphics.ver < 5)
		i915->drm.driver_features &= ~DRIVER_ATOMIC;

	/*
	 * Check if we support fake LMEM -- for now we only unleash this for
	 * the live selftests(test-and-exit).
	 */
#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
	if (IS_ENABLED(CONFIG_DRM_I915_UNSTABLE_FAKE_LMEM)) {
		if (GRAPHICS_VER(i915) >= 9 && i915_selftest.live < 0 &&
		    i915->params.fake_lmem_start) {
			mkwrite_device_info(i915)->memory_regions =
				REGION_SMEM | REGION_LMEM | REGION_STOLEN_SMEM;
			GEM_BUG_ON(!HAS_LMEM(i915));
		}
	}
#endif

	ret = pci_enable_device(pdev);
	if (ret)
		goto out_fini;

	ret = i915_driver_early_probe(i915);
	if (ret < 0)
		goto out_pci_disable;

	disable_rpm_wakeref_asserts(&i915->runtime_pm);

	intel_vgpu_detect(i915);

	ret = i915_driver_mmio_probe(i915);
	if (ret < 0)
		goto out_runtime_pm_put;

	ret = i915_driver_hw_probe(i915);
	if (ret < 0)
		goto out_cleanup_mmio;

	ret = intel_modeset_init_noirq(i915);
	if (ret < 0)
		goto out_cleanup_hw;

	ret = intel_irq_install(i915);
	if (ret)
		goto out_cleanup_modeset;

	ret = intel_modeset_init_nogem(i915);
	if (ret)
		goto out_cleanup_irq;

	ret = i915_gem_init(i915);
	if (ret)
		goto out_cleanup_modeset2;

	ret = intel_modeset_init(i915);
	if (ret)
		goto out_cleanup_gem;

	i915_driver_register(i915);

	enable_rpm_wakeref_asserts(&i915->runtime_pm);

	i915_welcome_messages(i915);

	i915->do_release = true;

	return 0;

out_cleanup_gem:
	i915_gem_suspend(i915);
	i915_gem_driver_remove(i915);
	i915_gem_driver_release(i915);
out_cleanup_modeset2:
	/* FIXME clean up the error path */
	intel_modeset_driver_remove(i915);
	intel_irq_uninstall(i915);
	intel_modeset_driver_remove_noirq(i915);
	goto out_cleanup_modeset;
out_cleanup_irq:
	intel_irq_uninstall(i915);
out_cleanup_modeset:
	intel_modeset_driver_remove_nogem(i915);
out_cleanup_hw:
	i915_driver_hw_remove(i915);
	intel_memory_regions_driver_release(i915);
	i915_ggtt_driver_release(i915);
	i915_gem_drain_freed_objects(i915);
	i915_ggtt_driver_late_release(i915);
out_cleanup_mmio:
	i915_driver_mmio_release(i915);
out_runtime_pm_put:
	enable_rpm_wakeref_asserts(&i915->runtime_pm);
	i915_driver_late_release(i915);
out_pci_disable:
	pci_disable_device(pdev);
out_fini:
	i915_probe_error(i915, "Device initialization failed (%d)\n", ret);
	return ret;
}

void i915_driver_remove(struct drm_i915_private *i915)
{
	disable_rpm_wakeref_asserts(&i915->runtime_pm);

	i915_driver_unregister(i915);

	/* Flush any external code that still may be under the RCU lock */
	synchronize_rcu();

	i915_gem_suspend(i915);

	intel_gvt_driver_remove(i915);

	intel_modeset_driver_remove(i915);

	intel_irq_uninstall(i915);

	intel_modeset_driver_remove_noirq(i915);

	i915_reset_error_state(i915);
	i915_gem_driver_remove(i915);

	intel_modeset_driver_remove_nogem(i915);

	i915_driver_hw_remove(i915);

	enable_rpm_wakeref_asserts(&i915->runtime_pm);
}

static void i915_driver_release(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;

	if (!dev_priv->do_release)
		return;

	disable_rpm_wakeref_asserts(rpm);

	i915_gem_driver_release(dev_priv);

	intel_memory_regions_driver_release(dev_priv);
	i915_ggtt_driver_release(dev_priv);
	i915_gem_drain_freed_objects(dev_priv);
	i915_ggtt_driver_late_release(dev_priv);

	i915_driver_mmio_release(dev_priv);

	enable_rpm_wakeref_asserts(rpm);
	intel_runtime_pm_driver_release(rpm);

	i915_driver_late_release(dev_priv);
}

static int i915_driver_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	int ret;

	ret = i915_gem_open(i915, file);
	if (ret)
		return ret;

	return 0;
}

/**
 * i915_driver_lastclose - clean up after all DRM clients have exited
 * @dev: DRM device
 *
 * Take care of cleaning up after all DRM clients have exited.  In the
 * mode setting case, we want to restore the kernel's initial mode (just
 * in case the last client left us in a bad state).
 *
 * Additionally, in the non-mode setting case, we'll tear down the GTT
 * and DMA structures, since the kernel won't be using them, and clea
 * up any GEM state.
 */
static void i915_driver_lastclose(struct drm_device *dev)
{
	struct drm_i915_private *i915 = to_i915(dev);

	intel_fbdev_restore_mode(dev);

	if (HAS_DISPLAY(i915))
		vga_switcheroo_process_delayed_switch();
}

static void i915_driver_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	i915_gem_context_close(file);

	kfree_rcu(file_priv, rcu);

	/* Catch up with all the deferred frees from "this" client */
	i915_gem_flush_free_objects(to_i915(dev));
}

static void intel_suspend_encoders(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_encoder *encoder;

	if (!HAS_DISPLAY(dev_priv))
		return;

	drm_modeset_lock_all(dev);
	for_each_intel_encoder(dev, encoder)
		if (encoder->suspend)
			encoder->suspend(encoder);
	drm_modeset_unlock_all(dev);
}

static void intel_shutdown_encoders(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_encoder *encoder;

	if (!HAS_DISPLAY(dev_priv))
		return;

	drm_modeset_lock_all(dev);
	for_each_intel_encoder(dev, encoder)
		if (encoder->shutdown)
			encoder->shutdown(encoder);
	drm_modeset_unlock_all(dev);
}

void i915_driver_shutdown(struct drm_i915_private *i915)
{
	disable_rpm_wakeref_asserts(&i915->runtime_pm);
	intel_runtime_pm_disable(&i915->runtime_pm);
	intel_power_domains_disable(i915);

	i915_gem_suspend(i915);

	if (HAS_DISPLAY(i915)) {
		drm_kms_helper_poll_disable(&i915->drm);

		drm_atomic_helper_shutdown(&i915->drm);
	}

	intel_dp_mst_suspend(i915);

	intel_runtime_pm_disable_interrupts(i915);
	intel_hpd_cancel_work(i915);

	intel_suspend_encoders(i915);
	intel_shutdown_encoders(i915);

	intel_dmc_ucode_suspend(i915);

	/*
	 * The only requirement is to reboot with display DC states disabled,
	 * for now leaving all display power wells in the INIT power domain
	 * enabled.
	 *
	 * TODO:
	 * - unify the pci_driver::shutdown sequence here with the
	 *   pci_driver.driver.pm.poweroff,poweroff_late sequence.
	 * - unify the driver remove and system/runtime suspend sequences with
	 *   the above unified shutdown/poweroff sequence.
	 */
	intel_power_domains_driver_remove(i915);
	enable_rpm_wakeref_asserts(&i915->runtime_pm);

	intel_runtime_pm_driver_release(&i915->runtime_pm);
}

static bool suspend_to_idle(struct drm_i915_private *dev_priv)
{
#if IS_ENABLED(CONFIG_ACPI_SLEEP)
	if (acpi_target_system_state() < ACPI_STATE_S3)
		return true;
#endif
	return false;
}

static int i915_drm_prepare(struct drm_device *dev)
{
	struct drm_i915_private *i915 = to_i915(dev);

	/*
	 * NB intel_display_suspend() may issue new requests after we've
	 * ostensibly marked the GPU as ready-to-sleep here. We need to
	 * split out that work and pull it forward so that after point,
	 * the GPU is not woken again.
	 */
	return i915_gem_backup_suspend(i915);
}

static int i915_drm_suspend(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	pci_power_t opregion_target_state;

	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	/* We do a lot of poking in a lot of registers, make sure they work
	 * properly. */
	intel_power_domains_disable(dev_priv);
	if (HAS_DISPLAY(dev_priv))
		drm_kms_helper_poll_disable(dev);

	pci_save_state(pdev);

	intel_display_suspend(dev);

	intel_dp_mst_suspend(dev_priv);

	intel_runtime_pm_disable_interrupts(dev_priv);
	intel_hpd_cancel_work(dev_priv);

	intel_suspend_encoders(dev_priv);

	intel_suspend_hw(dev_priv);

	/* Must be called before GGTT is suspended. */
	intel_dpt_suspend(dev_priv);
	i915_ggtt_suspend(&dev_priv->ggtt);

	i915_save_display(dev_priv);

	opregion_target_state = suspend_to_idle(dev_priv) ? PCI_D1 : PCI_D3cold;
	intel_opregion_suspend(dev_priv, opregion_target_state);

	intel_fbdev_set_suspend(dev, FBINFO_STATE_SUSPENDED, true);

	dev_priv->suspend_count++;

	intel_dmc_ucode_suspend(dev_priv);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return 0;
}

static enum i915_drm_suspend_mode
get_suspend_mode(struct drm_i915_private *dev_priv, bool hibernate)
{
	if (hibernate)
		return I915_DRM_SUSPEND_HIBERNATE;

	if (suspend_to_idle(dev_priv))
		return I915_DRM_SUSPEND_IDLE;

	return I915_DRM_SUSPEND_MEM;
}

static int i915_drm_suspend_late(struct drm_device *dev, bool hibernation)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;
	int ret;

	disable_rpm_wakeref_asserts(rpm);

	i915_gem_suspend_late(dev_priv);

	intel_uncore_suspend(&dev_priv->uncore);

	intel_power_domains_suspend(dev_priv,
				    get_suspend_mode(dev_priv, hibernation));

	intel_display_power_suspend_late(dev_priv);

	ret = vlv_suspend_complete(dev_priv);
	if (ret) {
		drm_err(&dev_priv->drm, "Suspend complete failed: %d\n", ret);
		intel_power_domains_resume(dev_priv);

		goto out;
	}

	/*
	 * FIXME: Temporary hammer to avoid freezing the machine on our DGFX
	 * This should be totally removed when we handle the pci states properly
	 * on runtime PM and on s2idle cases.
	 */
	if (suspend_to_idle(dev_priv))
		pci_d3cold_disable(pdev);

	pci_disable_device(pdev);
	/*
	 * During hibernation on some platforms the BIOS may try to access
	 * the device even though it's already in D3 and hang the machine. So
	 * leave the device in D0 on those platforms and hope the BIOS will
	 * power down the device properly. The issue was seen on multiple old
	 * GENs with different BIOS vendors, so having an explicit blacklist
	 * is inpractical; apply the workaround on everything pre GEN6. The
	 * platforms where the issue was seen:
	 * Lenovo Thinkpad X301, X61s, X60, T60, X41
	 * Fujitsu FSC S7110
	 * Acer Aspire 1830T
	 */
	if (!(hibernation && GRAPHICS_VER(dev_priv) < 6))
		pci_set_power_state(pdev, PCI_D3hot);

out:
	enable_rpm_wakeref_asserts(rpm);
	if (!dev_priv->uncore.user_forcewake_count)
		intel_runtime_pm_driver_release(rpm);

	return ret;
}

int i915_driver_suspend_switcheroo(struct drm_i915_private *i915,
				   pm_message_t state)
{
	int error;

	if (drm_WARN_ON_ONCE(&i915->drm, state.event != PM_EVENT_SUSPEND &&
			     state.event != PM_EVENT_FREEZE))
		return -EINVAL;

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	error = i915_drm_suspend(&i915->drm);
	if (error)
		return error;

	return i915_drm_suspend_late(&i915->drm, false);
}

static int i915_drm_resume(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int ret;

	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	ret = intel_pcode_init(dev_priv);
	if (ret)
		return ret;

	sanitize_gpu(dev_priv);

	ret = i915_ggtt_enable_hw(dev_priv);
	if (ret)
		drm_err(&dev_priv->drm, "failed to re-enable GGTT\n");

	i915_ggtt_resume(&dev_priv->ggtt);
	/* Must be called after GGTT is resumed. */
	intel_dpt_resume(dev_priv);

	intel_dmc_ucode_resume(dev_priv);

	i915_restore_display(dev_priv);
	intel_pps_unlock_regs_wa(dev_priv);

	intel_init_pch_refclk(dev_priv);

	/*
	 * Interrupts have to be enabled before any batches are run. If not the
	 * GPU will hang. i915_gem_init_hw() will initiate batches to
	 * update/restore the context.
	 *
	 * drm_mode_config_reset() needs AUX interrupts.
	 *
	 * Modeset enabling in intel_modeset_init_hw() also needs working
	 * interrupts.
	 */
	intel_runtime_pm_enable_interrupts(dev_priv);

	if (HAS_DISPLAY(dev_priv))
		drm_mode_config_reset(dev);

	i915_gem_resume(dev_priv);

	intel_modeset_init_hw(dev_priv);
	intel_init_clock_gating(dev_priv);
	intel_hpd_init(dev_priv);

	/* MST sideband requires HPD interrupts enabled */
	intel_dp_mst_resume(dev_priv);
	intel_display_resume(dev);

	intel_hpd_poll_disable(dev_priv);
	if (HAS_DISPLAY(dev_priv))
		drm_kms_helper_poll_enable(dev);

	intel_opregion_resume(dev_priv);

	intel_fbdev_set_suspend(dev, FBINFO_STATE_RUNNING, false);

	intel_power_domains_enable(dev_priv);

	intel_gvt_resume(dev_priv);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return 0;
}

static int i915_drm_resume_early(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	int ret;

	/*
	 * We have a resume ordering issue with the snd-hda driver also
	 * requiring our device to be power up. Due to the lack of a
	 * parent/child relationship we currently solve this with an early
	 * resume hook.
	 *
	 * FIXME: This should be solved with a special hdmi sink device or
	 * similar so that power domains can be employed.
	 */

	/*
	 * Note that we need to set the power state explicitly, since we
	 * powered off the device during freeze and the PCI core won't power
	 * it back up for us during thaw. Powering off the device during
	 * freeze is not a hard requirement though, and during the
	 * suspend/resume phases the PCI core makes sure we get here with the
	 * device powered on. So in case we change our freeze logic and keep
	 * the device powered we can also remove the following set power state
	 * call.
	 */
	ret = pci_set_power_state(pdev, PCI_D0);
	if (ret) {
		drm_err(&dev_priv->drm,
			"failed to set PCI D0 power state (%d)\n", ret);
		return ret;
	}

	/*
	 * Note that pci_enable_device() first enables any parent bridge
	 * device and only then sets the power state for this device. The
	 * bridge enabling is a nop though, since bridge devices are resumed
	 * first. The order of enabling power and enabling the device is
	 * imposed by the PCI core as described above, so here we preserve the
	 * same order for the freeze/thaw phases.
	 *
	 * TODO: eventually we should remove pci_disable_device() /
	 * pci_enable_enable_device() from suspend/resume. Due to how they
	 * depend on the device enable refcount we can't anyway depend on them
	 * disabling/enabling the device.
	 */
	if (pci_enable_device(pdev))
		return -EIO;

	pci_set_master(pdev);

	pci_d3cold_enable(pdev);

	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	ret = vlv_resume_prepare(dev_priv, false);
	if (ret)
		drm_err(&dev_priv->drm,
			"Resume prepare failed: %d, continuing anyway\n", ret);

	intel_uncore_resume_early(&dev_priv->uncore);

	intel_gt_check_and_clear_faults(to_gt(dev_priv));

	intel_display_power_resume_early(dev_priv);

	intel_power_domains_resume(dev_priv);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

int i915_driver_resume_switcheroo(struct drm_i915_private *i915)
{
	int ret;

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	ret = i915_drm_resume_early(&i915->drm);
	if (ret)
		return ret;

	return i915_drm_resume(&i915->drm);
}

static int i915_pm_prepare(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	if (!i915) {
		dev_err(kdev, "DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_prepare(&i915->drm);
}

static int i915_pm_suspend(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	if (!i915) {
		dev_err(kdev, "DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_suspend(&i915->drm);
}

static int i915_pm_suspend_late(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	/*
	 * We have a suspend ordering issue with the snd-hda driver also
	 * requiring our device to be power up. Due to the lack of a
	 * parent/child relationship we currently solve this with an late
	 * suspend hook.
	 *
	 * FIXME: This should be solved with a special hdmi sink device or
	 * similar so that power domains can be employed.
	 */
	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_suspend_late(&i915->drm, false);
}

static int i915_pm_poweroff_late(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_suspend_late(&i915->drm, true);
}

static int i915_pm_resume_early(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_resume_early(&i915->drm);
}

static int i915_pm_resume(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);

	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_resume(&i915->drm);
}

/* freeze: before creating the hibernation_image */
static int i915_pm_freeze(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);
	int ret;

	if (i915->drm.switch_power_state != DRM_SWITCH_POWER_OFF) {
		ret = i915_drm_suspend(&i915->drm);
		if (ret)
			return ret;
	}

	ret = i915_gem_freeze(i915);
	if (ret)
		return ret;

	return 0;
}

static int i915_pm_freeze_late(struct device *kdev)
{
	struct drm_i915_private *i915 = kdev_to_i915(kdev);
	int ret;

	if (i915->drm.switch_power_state != DRM_SWITCH_POWER_OFF) {
		ret = i915_drm_suspend_late(&i915->drm, true);
		if (ret)
			return ret;
	}

	ret = i915_gem_freeze_late(i915);
	if (ret)
		return ret;

	return 0;
}

/* thaw: called after creating the hibernation image, but before turning off. */
static int i915_pm_thaw_early(struct device *kdev)
{
	return i915_pm_resume_early(kdev);
}

static int i915_pm_thaw(struct device *kdev)
{
	return i915_pm_resume(kdev);
}

/* restore: called after loading the hibernation image. */
static int i915_pm_restore_early(struct device *kdev)
{
	return i915_pm_resume_early(kdev);
}

static int i915_pm_restore(struct device *kdev)
{
	return i915_pm_resume(kdev);
}

static int intel_runtime_suspend(struct device *kdev)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(kdev);
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	int ret;

	if (drm_WARN_ON_ONCE(&dev_priv->drm, !HAS_RUNTIME_PM(dev_priv)))
		return -ENODEV;

	drm_dbg_kms(&dev_priv->drm, "Suspending device\n");

	disable_rpm_wakeref_asserts(rpm);

	/*
	 * We are safe here against re-faults, since the fault handler takes
	 * an RPM reference.
	 */
	i915_gem_runtime_suspend(dev_priv);

	intel_gt_runtime_suspend(to_gt(dev_priv));

	intel_runtime_pm_disable_interrupts(dev_priv);

	intel_uncore_suspend(&dev_priv->uncore);

	intel_display_power_suspend(dev_priv);

	ret = vlv_suspend_complete(dev_priv);
	if (ret) {
		drm_err(&dev_priv->drm,
			"Runtime suspend failed, disabling it (%d)\n", ret);
		intel_uncore_runtime_resume(&dev_priv->uncore);

		intel_runtime_pm_enable_interrupts(dev_priv);

		intel_gt_runtime_resume(to_gt(dev_priv));

		enable_rpm_wakeref_asserts(rpm);

		return ret;
	}

	enable_rpm_wakeref_asserts(rpm);
	intel_runtime_pm_driver_release(rpm);

	if (intel_uncore_arm_unclaimed_mmio_detection(&dev_priv->uncore))
		drm_err(&dev_priv->drm,
			"Unclaimed access detected prior to suspending\n");

	/*
	 * FIXME: Temporary hammer to avoid freezing the machine on our DGFX
	 * This should be totally removed when we handle the pci states properly
	 * on runtime PM and on s2idle cases.
	 */
	pci_d3cold_disable(pdev);
	rpm->suspended = true;

	/*
	 * FIXME: We really should find a document that references the arguments
	 * used below!
	 */
	if (IS_BROADWELL(dev_priv)) {
		/*
		 * On Broadwell, if we use PCI_D1 the PCH DDI ports will stop
		 * being detected, and the call we do at intel_runtime_resume()
		 * won't be able to restore them. Since PCI_D3hot matches the
		 * actual specification and appears to be working, use it.
		 */
		intel_opregion_notify_adapter(dev_priv, PCI_D3hot);
	} else {
		/*
		 * current versions of firmware which depend on this opregion
		 * notification have repurposed the D1 definition to mean
		 * "runtime suspended" vs. what you would normally expect (D3)
		 * to distinguish it from notifications that might be sent via
		 * the suspend path.
		 */
		intel_opregion_notify_adapter(dev_priv, PCI_D1);
	}

	assert_forcewakes_inactive(&dev_priv->uncore);

	if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv))
		intel_hpd_poll_enable(dev_priv);

	drm_dbg_kms(&dev_priv->drm, "Device suspended\n");
	return 0;
}

static int intel_runtime_resume(struct device *kdev)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(kdev);
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	int ret;

	if (drm_WARN_ON_ONCE(&dev_priv->drm, !HAS_RUNTIME_PM(dev_priv)))
		return -ENODEV;

	drm_dbg_kms(&dev_priv->drm, "Resuming device\n");

	drm_WARN_ON_ONCE(&dev_priv->drm, atomic_read(&rpm->wakeref_count));
	disable_rpm_wakeref_asserts(rpm);

	intel_opregion_notify_adapter(dev_priv, PCI_D0);
	rpm->suspended = false;
	pci_d3cold_enable(pdev);
	if (intel_uncore_unclaimed_mmio(&dev_priv->uncore))
		drm_dbg(&dev_priv->drm,
			"Unclaimed access during suspend, bios?\n");

	intel_display_power_resume(dev_priv);

	ret = vlv_resume_prepare(dev_priv, true);

	intel_uncore_runtime_resume(&dev_priv->uncore);

	intel_runtime_pm_enable_interrupts(dev_priv);

	/*
	 * No point of rolling back things in case of an error, as the best
	 * we can do is to hope that things will still work (and disable RPM).
	 */
	intel_gt_runtime_resume(to_gt(dev_priv));

	/*
	 * On VLV/CHV display interrupts are part of the display
	 * power well, so hpd is reinitialized from there. For
	 * everyone else do it here.
	 */
	if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv)) {
		intel_hpd_init(dev_priv);
		intel_hpd_poll_disable(dev_priv);
	}

	intel_enable_ipc(dev_priv);

	enable_rpm_wakeref_asserts(rpm);

	if (ret)
		drm_err(&dev_priv->drm,
			"Runtime resume failed, disabling it (%d)\n", ret);
	else
		drm_dbg_kms(&dev_priv->drm, "Device resumed\n");

	return ret;
}

const struct dev_pm_ops i915_pm_ops = {
	/*
	 * S0ix (via system suspend) and S3 event handlers [PMSG_SUSPEND,
	 * PMSG_RESUME]
	 */
	.prepare = i915_pm_prepare,
	.suspend = i915_pm_suspend,
	.suspend_late = i915_pm_suspend_late,
	.resume_early = i915_pm_resume_early,
	.resume = i915_pm_resume,

	/*
	 * S4 event handlers
	 * @freeze, @freeze_late    : called (1) before creating the
	 *                            hibernation image [PMSG_FREEZE] and
	 *                            (2) after rebooting, before restoring
	 *                            the image [PMSG_QUIESCE]
	 * @thaw, @thaw_early       : called (1) after creating the hibernation
	 *                            image, before writing it [PMSG_THAW]
	 *                            and (2) after failing to create or
	 *                            restore the image [PMSG_RECOVER]
	 * @poweroff, @poweroff_late: called after writing the hibernation
	 *                            image, before rebooting [PMSG_HIBERNATE]
	 * @restore, @restore_early : called after rebooting and restoring the
	 *                            hibernation image [PMSG_RESTORE]
	 */
	.freeze = i915_pm_freeze,
	.freeze_late = i915_pm_freeze_late,
	.thaw_early = i915_pm_thaw_early,
	.thaw = i915_pm_thaw,
	.poweroff = i915_pm_suspend,
	.poweroff_late = i915_pm_poweroff_late,
	.restore_early = i915_pm_restore_early,
	.restore = i915_pm_restore,

	/* S0ix (via runtime suspend) event handlers */
	.runtime_suspend = intel_runtime_suspend,
	.runtime_resume = intel_runtime_resume,
};

static const struct file_operations i915_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release_noglobal,
	.unlocked_ioctl = drm_ioctl,
	.mmap = i915_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.compat_ioctl = i915_ioc32_compat_ioctl,
	.llseek = noop_llseek,
};

static int
i915_gem_reject_pin_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	return -ENODEV;
}

static const struct drm_ioctl_desc i915_ioctls[] = {
	DRM_IOCTL_DEF_DRV(I915_INIT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_FLUSH, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_FLIP, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_BATCHBUFFER, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_IRQ_EMIT, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_IRQ_WAIT, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_GETPARAM, i915_getparam_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_SETPARAM, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_ALLOC, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_FREE, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_INIT_HEAP, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_CMDBUFFER, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_DESTROY_HEAP,  drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_SET_VBLANK_PIPE,  drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GET_VBLANK_PIPE,  drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_VBLANK_SWAP, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_HWS_ADDR, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_INIT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER2_WR, i915_gem_execbuffer2_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PIN, i915_gem_reject_pin_ioctl, DRM_AUTH|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_UNPIN, i915_gem_reject_pin_ioctl, DRM_AUTH|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_BUSY, i915_gem_busy_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_CACHING, i915_gem_set_caching_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_CACHING, i915_gem_get_caching_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_THROTTLE, i915_gem_throttle_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_ENTERVT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_LEAVEVT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_CREATE, i915_gem_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CREATE_EXT, i915_gem_create_ext_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PREAD, i915_gem_pread_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PWRITE, i915_gem_pwrite_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_MMAP, i915_gem_mmap_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_MMAP_OFFSET, i915_gem_mmap_offset_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_DOMAIN, i915_gem_set_domain_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SW_FINISH, i915_gem_sw_finish_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_TILING, i915_gem_set_tiling_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_TILING, i915_gem_get_tiling_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_APERTURE, i915_gem_get_aperture_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GET_PIPE_FROM_CRTC_ID, intel_get_pipe_from_crtc_id_ioctl, 0),
	DRM_IOCTL_DEF_DRV(I915_GEM_MADVISE, i915_gem_madvise_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_OVERLAY_PUT_IMAGE, intel_overlay_put_image_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(I915_OVERLAY_ATTRS, intel_overlay_attrs_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(I915_SET_SPRITE_COLORKEY, intel_sprite_set_colorkey_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(I915_GET_SPRITE_COLORKEY, drm_noop, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(I915_GEM_WAIT, i915_gem_wait_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_CREATE_EXT, i915_gem_context_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_DESTROY, i915_gem_context_destroy_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_REG_READ, i915_reg_read_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GET_RESET_STATS, i915_gem_context_reset_stats_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_USERPTR, i915_gem_userptr_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_GETPARAM, i915_gem_context_getparam_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_SETPARAM, i915_gem_context_setparam_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_PERF_OPEN, i915_perf_open_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_PERF_ADD_CONFIG, i915_perf_add_config_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_PERF_REMOVE_CONFIG, i915_perf_remove_config_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_QUERY, i915_query_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_VM_CREATE, i915_gem_vm_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_VM_DESTROY, i915_gem_vm_destroy_ioctl, DRM_RENDER_ALLOW),
};

/*
 * Interface history:
 *
 * 1.1: Original.
 * 1.2: Add Power Management
 * 1.3: Add vblank support
 * 1.4: Fix cmdbuffer path, add heap destroy
 * 1.5: Add vblank pipe configuration
 * 1.6: - New ioctl for scheduling buffer swaps on vertical blank
 *      - Support vertical blank on secondary display pipe
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		6
#define DRIVER_PATCHLEVEL	0

static const struct drm_driver i915_drm_driver = {
	/* Don't use MTRRs here; the Xserver or userspace app should
	 * deal with them for Intel hardware.
	 */
	.driver_features =
	    DRIVER_GEM |
	    DRIVER_RENDER | DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_SYNCOBJ |
	    DRIVER_SYNCOBJ_TIMELINE,
	.release = i915_driver_release,
	.open = i915_driver_open,
	.lastclose = i915_driver_lastclose,
	.postclose = i915_driver_postclose,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_import = i915_gem_prime_import,

	.dumb_create = i915_gem_dumb_create,
	.dumb_map_offset = i915_gem_dumb_mmap_offset,

	.ioctls = i915_ioctls,
	.num_ioctls = ARRAY_SIZE(i915_ioctls),
	.fops = &i915_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};
