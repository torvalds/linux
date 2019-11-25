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
#include <linux/oom.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/pnp.h>
#include <linux/slab.h>
#include <linux/vgaarb.h>
#include <linux/vga_switcheroo.h>
#include <linux/vt.h>
#include <acpi/video.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_irq.h>
#include <drm/drm_probe_helper.h>
#include <drm/i915_drm.h>

#include "display/intel_acpi.h"
#include "display/intel_audio.h"
#include "display/intel_bw.h"
#include "display/intel_cdclk.h"
#include "display/intel_display_types.h"
#include "display/intel_dp.h"
#include "display/intel_fbdev.h"
#include "display/intel_gmbus.h"
#include "display/intel_hotplug.h"
#include "display/intel_overlay.h"
#include "display/intel_pipe_crc.h"
#include "display/intel_sprite.h"

#include "gem/i915_gem_context.h"
#include "gem/i915_gem_ioctls.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_pm.h"

#include "i915_debugfs.h"
#include "i915_drv.h"
#include "i915_irq.h"
#include "i915_memcpy.h"
#include "i915_perf.h"
#include "i915_query.h"
#include "i915_suspend.h"
#include "i915_sysfs.h"
#include "i915_trace.h"
#include "i915_vgpu.h"
#include "intel_csr.h"
#include "intel_pm.h"

static struct drm_driver driver;

struct vlv_s0ix_state {
	/* GAM */
	u32 wr_watermark;
	u32 gfx_prio_ctrl;
	u32 arb_mode;
	u32 gfx_pend_tlb0;
	u32 gfx_pend_tlb1;
	u32 lra_limits[GEN7_LRA_LIMITS_REG_NUM];
	u32 media_max_req_count;
	u32 gfx_max_req_count;
	u32 render_hwsp;
	u32 ecochk;
	u32 bsd_hwsp;
	u32 blt_hwsp;
	u32 tlb_rd_addr;

	/* MBC */
	u32 g3dctl;
	u32 gsckgctl;
	u32 mbctl;

	/* GCP */
	u32 ucgctl1;
	u32 ucgctl3;
	u32 rcgctl1;
	u32 rcgctl2;
	u32 rstctl;
	u32 misccpctl;

	/* GPM */
	u32 gfxpause;
	u32 rpdeuhwtc;
	u32 rpdeuc;
	u32 ecobus;
	u32 pwrdwnupctl;
	u32 rp_down_timeout;
	u32 rp_deucsw;
	u32 rcubmabdtmr;
	u32 rcedata;
	u32 spare2gh;

	/* Display 1 CZ domain */
	u32 gt_imr;
	u32 gt_ier;
	u32 pm_imr;
	u32 pm_ier;
	u32 gt_scratch[GEN7_GT_SCRATCH_REG_NUM];

	/* GT SA CZ domain */
	u32 tilectl;
	u32 gt_fifoctl;
	u32 gtlc_wake_ctrl;
	u32 gtlc_survive;
	u32 pmwgicz;

	/* Display 2 CZ domain */
	u32 gu_ctl0;
	u32 gu_ctl1;
	u32 pcbr;
	u32 clock_gate_dis2;
};

static int i915_get_bridge_dev(struct drm_i915_private *dev_priv)
{
	int domain = pci_domain_nr(dev_priv->drm.pdev->bus);

	dev_priv->bridge_dev =
		pci_get_domain_bus_and_slot(domain, 0, PCI_DEVFN(0, 0));
	if (!dev_priv->bridge_dev) {
		DRM_ERROR("bridge device not found\n");
		return -1;
	}
	return 0;
}

/* Allocate space for the MCH regs if needed, return nonzero on error */
static int
intel_alloc_mchbar_resource(struct drm_i915_private *dev_priv)
{
	int reg = INTEL_GEN(dev_priv) >= 4 ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp_lo, temp_hi = 0;
	u64 mchbar_addr;
	int ret;

	if (INTEL_GEN(dev_priv) >= 4)
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
		DRM_DEBUG_DRIVER("failed bus alloc: %d\n", ret);
		dev_priv->mch_res.start = 0;
		return ret;
	}

	if (INTEL_GEN(dev_priv) >= 4)
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
	int mchbar_reg = INTEL_GEN(dev_priv) >= 4 ? MCHBAR_I965 : MCHBAR_I915;
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
	int mchbar_reg = INTEL_GEN(dev_priv) >= 4 ? MCHBAR_I965 : MCHBAR_I915;

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

/* true = enable decode, false = disable decoder */
static unsigned int i915_vga_set_decode(void *cookie, bool state)
{
	struct drm_i915_private *dev_priv = cookie;

	intel_modeset_vga_set_state(dev_priv, state);
	if (state)
		return VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
		       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
	else
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
}

static int i915_resume_switcheroo(struct drm_i915_private *i915);
static int i915_suspend_switcheroo(struct drm_i915_private *i915,
				   pm_message_t state);

static void i915_switcheroo_set_state(struct pci_dev *pdev, enum vga_switcheroo_state state)
{
	struct drm_i915_private *i915 = pdev_to_i915(pdev);
	pm_message_t pmm = { .event = PM_EVENT_SUSPEND };

	if (!i915) {
		dev_err(&pdev->dev, "DRM not initialized, aborting switch.\n");
		return;
	}

	if (state == VGA_SWITCHEROO_ON) {
		pr_info("switched on\n");
		i915->drm.switch_power_state = DRM_SWITCH_POWER_CHANGING;
		/* i915 resume handler doesn't set to D0 */
		pci_set_power_state(pdev, PCI_D0);
		i915_resume_switcheroo(i915);
		i915->drm.switch_power_state = DRM_SWITCH_POWER_ON;
	} else {
		pr_info("switched off\n");
		i915->drm.switch_power_state = DRM_SWITCH_POWER_CHANGING;
		i915_suspend_switcheroo(i915, pmm);
		i915->drm.switch_power_state = DRM_SWITCH_POWER_OFF;
	}
}

static bool i915_switcheroo_can_switch(struct pci_dev *pdev)
{
	struct drm_i915_private *i915 = pdev_to_i915(pdev);

	/*
	 * FIXME: open_count is protected by drm_global_mutex but that would lead to
	 * locking inversion with the driver load path. And the access here is
	 * completely racy anyway. So don't bother with locking for now.
	 */
	return i915 && i915->drm.open_count == 0;
}

static const struct vga_switcheroo_client_ops i915_switcheroo_ops = {
	.set_gpu_state = i915_switcheroo_set_state,
	.reprobe = NULL,
	.can_switch = i915_switcheroo_can_switch,
};

static int i915_driver_modeset_probe(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = dev_priv->drm.pdev;
	int ret;

	if (i915_inject_probe_failure(dev_priv))
		return -ENODEV;

	if (HAS_DISPLAY(dev_priv)) {
		ret = drm_vblank_init(&dev_priv->drm,
				      INTEL_INFO(dev_priv)->num_pipes);
		if (ret)
			goto out;
	}

	intel_bios_init(dev_priv);

	/* If we have > 1 VGA cards, then we need to arbitrate access
	 * to the common VGA resources.
	 *
	 * If we are a secondary display controller (!PCI_DISPLAY_CLASS_VGA),
	 * then we do not take part in VGA arbitration and the
	 * vga_client_register() fails with -ENODEV.
	 */
	ret = vga_client_register(pdev, dev_priv, NULL, i915_vga_set_decode);
	if (ret && ret != -ENODEV)
		goto out;

	intel_register_dsm_handler();

	ret = vga_switcheroo_register_client(pdev, &i915_switcheroo_ops, false);
	if (ret)
		goto cleanup_vga_client;

	intel_power_domains_init_hw(dev_priv, false);

	intel_csr_ucode_init(dev_priv);

	ret = intel_irq_install(dev_priv);
	if (ret)
		goto cleanup_csr;

	intel_gmbus_setup(dev_priv);

	/* Important: The output setup functions called by modeset_init need
	 * working irqs for e.g. gmbus and dp aux transfers. */
	ret = intel_modeset_init(dev);
	if (ret)
		goto cleanup_irq;

	ret = i915_gem_init(dev_priv);
	if (ret)
		goto cleanup_modeset;

	intel_overlay_setup(dev_priv);

	if (!HAS_DISPLAY(dev_priv))
		return 0;

	ret = intel_fbdev_init(dev);
	if (ret)
		goto cleanup_gem;

	/* Only enable hotplug handling once the fbdev is fully set up. */
	intel_hpd_init(dev_priv);

	intel_init_ipc(dev_priv);

	return 0;

cleanup_gem:
	i915_gem_suspend(dev_priv);
	i915_gem_driver_remove(dev_priv);
	i915_gem_driver_release(dev_priv);
cleanup_modeset:
	intel_modeset_driver_remove(dev);
cleanup_irq:
	intel_irq_uninstall(dev_priv);
	intel_gmbus_teardown(dev_priv);
cleanup_csr:
	intel_csr_ucode_fini(dev_priv);
	intel_power_domains_driver_remove(dev_priv);
	vga_switcheroo_unregister_client(pdev);
cleanup_vga_client:
	vga_client_register(pdev, NULL, NULL, NULL);
out:
	return ret;
}

static int i915_kick_out_firmware_fb(struct drm_i915_private *dev_priv)
{
	struct apertures_struct *ap;
	struct pci_dev *pdev = dev_priv->drm.pdev;
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	bool primary;
	int ret;

	ap = alloc_apertures(1);
	if (!ap)
		return -ENOMEM;

	ap->ranges[0].base = ggtt->gmadr.start;
	ap->ranges[0].size = ggtt->mappable_end;

	primary =
		pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW;

	ret = drm_fb_helper_remove_conflicting_framebuffers(ap, "inteldrmfb", primary);

	kfree(ap);

	return ret;
}

static void intel_init_dpio(struct drm_i915_private *dev_priv)
{
	/*
	 * IOSF_PORT_DPIO is used for VLV x2 PHY (DP/HDMI B and C),
	 * CHV x1 PHY (DP/HDMI D)
	 * IOSF_PORT_DPIO_2 is used for CHV x2 PHY (DP/HDMI B and C)
	 */
	if (IS_CHERRYVIEW(dev_priv)) {
		DPIO_PHY_IOSF_PORT(DPIO_PHY0) = IOSF_PORT_DPIO_2;
		DPIO_PHY_IOSF_PORT(DPIO_PHY1) = IOSF_PORT_DPIO;
	} else if (IS_VALLEYVIEW(dev_priv)) {
		DPIO_PHY_IOSF_PORT(DPIO_PHY0) = IOSF_PORT_DPIO;
	}
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
	DRM_ERROR("Failed to allocate workqueues.\n");

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
	pre |= IS_SKL_REVID(dev_priv, 0, SKL_REVID_F0);
	pre |= IS_BXT_REVID(dev_priv, 0, BXT_REVID_B_LAST);
	pre |= IS_KBL_REVID(dev_priv, 0, KBL_REVID_A0);

	if (pre) {
		DRM_ERROR("This is a pre-production stepping. "
			  "It may not be fully functional.\n");
		add_taint(TAINT_MACHINE_CHECK, LOCKDEP_STILL_OK);
	}
}

static int vlv_alloc_s0ix_state(struct drm_i915_private *i915)
{
	if (!IS_VALLEYVIEW(i915))
		return 0;

	/* we write all the values in the struct, so no need to zero it out */
	i915->vlv_s0ix_state = kmalloc(sizeof(*i915->vlv_s0ix_state),
				       GFP_KERNEL);
	if (!i915->vlv_s0ix_state)
		return -ENOMEM;

	return 0;
}

static void vlv_free_s0ix_state(struct drm_i915_private *i915)
{
	if (!i915->vlv_s0ix_state)
		return;

	kfree(i915->vlv_s0ix_state);
	i915->vlv_s0ix_state = NULL;
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

	intel_uncore_mmio_debug_init_early(&dev_priv->mmio_debug);
	intel_uncore_init_early(&dev_priv->uncore, dev_priv);

	spin_lock_init(&dev_priv->irq_lock);
	spin_lock_init(&dev_priv->gpu_error.lock);
	mutex_init(&dev_priv->backlight_lock);

	mutex_init(&dev_priv->sb_lock);
	pm_qos_add_request(&dev_priv->sb_qos,
			   PM_QOS_CPU_DMA_LATENCY, PM_QOS_DEFAULT_VALUE);

	mutex_init(&dev_priv->av_mutex);
	mutex_init(&dev_priv->wm.wm_mutex);
	mutex_init(&dev_priv->pps_mutex);
	mutex_init(&dev_priv->hdcp_comp_mutex);

	i915_memcpy_init_early(dev_priv);
	intel_runtime_pm_init_early(&dev_priv->runtime_pm);

	ret = i915_workqueues_init(dev_priv);
	if (ret < 0)
		return ret;

	ret = vlv_alloc_s0ix_state(dev_priv);
	if (ret < 0)
		goto err_workqueues;

	intel_wopcm_init_early(&dev_priv->wopcm);

	intel_gt_init_early(&dev_priv->gt, dev_priv);

	ret = i915_gem_init_early(dev_priv);
	if (ret < 0)
		goto err_gt;

	/* This must be called before any calls to HAS_PCH_* */
	intel_detect_pch(dev_priv);

	intel_pm_setup(dev_priv);
	intel_init_dpio(dev_priv);
	ret = intel_power_domains_init(dev_priv);
	if (ret < 0)
		goto err_gem;
	intel_irq_init(dev_priv);
	intel_init_display_hooks(dev_priv);
	intel_init_clock_gating_hooks(dev_priv);
	intel_init_audio_hooks(dev_priv);
	intel_display_crc_init(dev_priv);

	intel_detect_preproduction_hw(dev_priv);

	return 0;

err_gem:
	i915_gem_cleanup_early(dev_priv);
err_gt:
	intel_gt_driver_late_release(&dev_priv->gt);
	vlv_free_s0ix_state(dev_priv);
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
	intel_gt_driver_late_release(&dev_priv->gt);
	vlv_free_s0ix_state(dev_priv);
	i915_workqueues_cleanup(dev_priv);

	pm_qos_remove_request(&dev_priv->sb_qos);
	mutex_destroy(&dev_priv->sb_lock);
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

	if (i915_get_bridge_dev(dev_priv))
		return -EIO;

	ret = intel_uncore_init_mmio(&dev_priv->uncore);
	if (ret < 0)
		goto err_bridge;

	/* Try to make sure MCHBAR is enabled before poking at it */
	intel_setup_mchbar(dev_priv);

	intel_device_info_init_mmio(dev_priv);

	intel_uncore_prune_mmio_domains(&dev_priv->uncore);

	intel_uc_init_mmio(&dev_priv->gt.uc);

	ret = intel_engines_init_mmio(dev_priv);
	if (ret)
		goto err_uncore;

	i915_gem_init_mmio(dev_priv);

	return 0;

err_uncore:
	intel_teardown_mchbar(dev_priv);
	intel_uncore_fini_mmio(&dev_priv->uncore);
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
	intel_engines_cleanup(dev_priv);
	intel_teardown_mchbar(dev_priv);
	intel_uncore_fini_mmio(&dev_priv->uncore);
	pci_dev_put(dev_priv->bridge_dev);
}

static void intel_sanitize_options(struct drm_i915_private *dev_priv)
{
	intel_gvt_sanitize_options(dev_priv);
}

#define DRAM_TYPE_STR(type) [INTEL_DRAM_ ## type] = #type

static const char *intel_dram_type_str(enum intel_dram_type type)
{
	static const char * const str[] = {
		DRAM_TYPE_STR(UNKNOWN),
		DRAM_TYPE_STR(DDR3),
		DRAM_TYPE_STR(DDR4),
		DRAM_TYPE_STR(LPDDR3),
		DRAM_TYPE_STR(LPDDR4),
	};

	if (type >= ARRAY_SIZE(str))
		type = INTEL_DRAM_UNKNOWN;

	return str[type];
}

#undef DRAM_TYPE_STR

static int intel_dimm_num_devices(const struct dram_dimm_info *dimm)
{
	return dimm->ranks * 64 / (dimm->width ?: 1);
}

/* Returns total GB for the whole DIMM */
static int skl_get_dimm_size(u16 val)
{
	return val & SKL_DRAM_SIZE_MASK;
}

static int skl_get_dimm_width(u16 val)
{
	if (skl_get_dimm_size(val) == 0)
		return 0;

	switch (val & SKL_DRAM_WIDTH_MASK) {
	case SKL_DRAM_WIDTH_X8:
	case SKL_DRAM_WIDTH_X16:
	case SKL_DRAM_WIDTH_X32:
		val = (val & SKL_DRAM_WIDTH_MASK) >> SKL_DRAM_WIDTH_SHIFT;
		return 8 << val;
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static int skl_get_dimm_ranks(u16 val)
{
	if (skl_get_dimm_size(val) == 0)
		return 0;

	val = (val & SKL_DRAM_RANK_MASK) >> SKL_DRAM_RANK_SHIFT;

	return val + 1;
}

/* Returns total GB for the whole DIMM */
static int cnl_get_dimm_size(u16 val)
{
	return (val & CNL_DRAM_SIZE_MASK) / 2;
}

static int cnl_get_dimm_width(u16 val)
{
	if (cnl_get_dimm_size(val) == 0)
		return 0;

	switch (val & CNL_DRAM_WIDTH_MASK) {
	case CNL_DRAM_WIDTH_X8:
	case CNL_DRAM_WIDTH_X16:
	case CNL_DRAM_WIDTH_X32:
		val = (val & CNL_DRAM_WIDTH_MASK) >> CNL_DRAM_WIDTH_SHIFT;
		return 8 << val;
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static int cnl_get_dimm_ranks(u16 val)
{
	if (cnl_get_dimm_size(val) == 0)
		return 0;

	val = (val & CNL_DRAM_RANK_MASK) >> CNL_DRAM_RANK_SHIFT;

	return val + 1;
}

static bool
skl_is_16gb_dimm(const struct dram_dimm_info *dimm)
{
	/* Convert total GB to Gb per DRAM device */
	return 8 * dimm->size / (intel_dimm_num_devices(dimm) ?: 1) == 16;
}

static void
skl_dram_get_dimm_info(struct drm_i915_private *dev_priv,
		       struct dram_dimm_info *dimm,
		       int channel, char dimm_name, u16 val)
{
	if (INTEL_GEN(dev_priv) >= 10) {
		dimm->size = cnl_get_dimm_size(val);
		dimm->width = cnl_get_dimm_width(val);
		dimm->ranks = cnl_get_dimm_ranks(val);
	} else {
		dimm->size = skl_get_dimm_size(val);
		dimm->width = skl_get_dimm_width(val);
		dimm->ranks = skl_get_dimm_ranks(val);
	}

	DRM_DEBUG_KMS("CH%u DIMM %c size: %u GB, width: X%u, ranks: %u, 16Gb DIMMs: %s\n",
		      channel, dimm_name, dimm->size, dimm->width, dimm->ranks,
		      yesno(skl_is_16gb_dimm(dimm)));
}

static int
skl_dram_get_channel_info(struct drm_i915_private *dev_priv,
			  struct dram_channel_info *ch,
			  int channel, u32 val)
{
	skl_dram_get_dimm_info(dev_priv, &ch->dimm_l,
			       channel, 'L', val & 0xffff);
	skl_dram_get_dimm_info(dev_priv, &ch->dimm_s,
			       channel, 'S', val >> 16);

	if (ch->dimm_l.size == 0 && ch->dimm_s.size == 0) {
		DRM_DEBUG_KMS("CH%u not populated\n", channel);
		return -EINVAL;
	}

	if (ch->dimm_l.ranks == 2 || ch->dimm_s.ranks == 2)
		ch->ranks = 2;
	else if (ch->dimm_l.ranks == 1 && ch->dimm_s.ranks == 1)
		ch->ranks = 2;
	else
		ch->ranks = 1;

	ch->is_16gb_dimm =
		skl_is_16gb_dimm(&ch->dimm_l) ||
		skl_is_16gb_dimm(&ch->dimm_s);

	DRM_DEBUG_KMS("CH%u ranks: %u, 16Gb DIMMs: %s\n",
		      channel, ch->ranks, yesno(ch->is_16gb_dimm));

	return 0;
}

static bool
intel_is_dram_symmetric(const struct dram_channel_info *ch0,
			const struct dram_channel_info *ch1)
{
	return !memcmp(ch0, ch1, sizeof(*ch0)) &&
		(ch0->dimm_s.size == 0 ||
		 !memcmp(&ch0->dimm_l, &ch0->dimm_s, sizeof(ch0->dimm_l)));
}

static int
skl_dram_get_channels_info(struct drm_i915_private *dev_priv)
{
	struct dram_info *dram_info = &dev_priv->dram_info;
	struct dram_channel_info ch0 = {}, ch1 = {};
	u32 val;
	int ret;

	val = I915_READ(SKL_MAD_DIMM_CH0_0_0_0_MCHBAR_MCMAIN);
	ret = skl_dram_get_channel_info(dev_priv, &ch0, 0, val);
	if (ret == 0)
		dram_info->num_channels++;

	val = I915_READ(SKL_MAD_DIMM_CH1_0_0_0_MCHBAR_MCMAIN);
	ret = skl_dram_get_channel_info(dev_priv, &ch1, 1, val);
	if (ret == 0)
		dram_info->num_channels++;

	if (dram_info->num_channels == 0) {
		DRM_INFO("Number of memory channels is zero\n");
		return -EINVAL;
	}

	/*
	 * If any of the channel is single rank channel, worst case output
	 * will be same as if single rank memory, so consider single rank
	 * memory.
	 */
	if (ch0.ranks == 1 || ch1.ranks == 1)
		dram_info->ranks = 1;
	else
		dram_info->ranks = max(ch0.ranks, ch1.ranks);

	if (dram_info->ranks == 0) {
		DRM_INFO("couldn't get memory rank information\n");
		return -EINVAL;
	}

	dram_info->is_16gb_dimm = ch0.is_16gb_dimm || ch1.is_16gb_dimm;

	dram_info->symmetric_memory = intel_is_dram_symmetric(&ch0, &ch1);

	DRM_DEBUG_KMS("Memory configuration is symmetric? %s\n",
		      yesno(dram_info->symmetric_memory));
	return 0;
}

static enum intel_dram_type
skl_get_dram_type(struct drm_i915_private *dev_priv)
{
	u32 val;

	val = I915_READ(SKL_MAD_INTER_CHANNEL_0_0_0_MCHBAR_MCMAIN);

	switch (val & SKL_DRAM_DDR_TYPE_MASK) {
	case SKL_DRAM_DDR_TYPE_DDR3:
		return INTEL_DRAM_DDR3;
	case SKL_DRAM_DDR_TYPE_DDR4:
		return INTEL_DRAM_DDR4;
	case SKL_DRAM_DDR_TYPE_LPDDR3:
		return INTEL_DRAM_LPDDR3;
	case SKL_DRAM_DDR_TYPE_LPDDR4:
		return INTEL_DRAM_LPDDR4;
	default:
		MISSING_CASE(val);
		return INTEL_DRAM_UNKNOWN;
	}
}

static int
skl_get_dram_info(struct drm_i915_private *dev_priv)
{
	struct dram_info *dram_info = &dev_priv->dram_info;
	u32 mem_freq_khz, val;
	int ret;

	dram_info->type = skl_get_dram_type(dev_priv);
	DRM_DEBUG_KMS("DRAM type: %s\n", intel_dram_type_str(dram_info->type));

	ret = skl_dram_get_channels_info(dev_priv);
	if (ret)
		return ret;

	val = I915_READ(SKL_MC_BIOS_DATA_0_0_0_MCHBAR_PCU);
	mem_freq_khz = DIV_ROUND_UP((val & SKL_REQ_DATA_MASK) *
				    SKL_MEMORY_FREQ_MULTIPLIER_HZ, 1000);

	dram_info->bandwidth_kbps = dram_info->num_channels *
							mem_freq_khz * 8;

	if (dram_info->bandwidth_kbps == 0) {
		DRM_INFO("Couldn't get system memory bandwidth\n");
		return -EINVAL;
	}

	dram_info->valid = true;
	return 0;
}

/* Returns Gb per DRAM device */
static int bxt_get_dimm_size(u32 val)
{
	switch (val & BXT_DRAM_SIZE_MASK) {
	case BXT_DRAM_SIZE_4GBIT:
		return 4;
	case BXT_DRAM_SIZE_6GBIT:
		return 6;
	case BXT_DRAM_SIZE_8GBIT:
		return 8;
	case BXT_DRAM_SIZE_12GBIT:
		return 12;
	case BXT_DRAM_SIZE_16GBIT:
		return 16;
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static int bxt_get_dimm_width(u32 val)
{
	if (!bxt_get_dimm_size(val))
		return 0;

	val = (val & BXT_DRAM_WIDTH_MASK) >> BXT_DRAM_WIDTH_SHIFT;

	return 8 << val;
}

static int bxt_get_dimm_ranks(u32 val)
{
	if (!bxt_get_dimm_size(val))
		return 0;

	switch (val & BXT_DRAM_RANK_MASK) {
	case BXT_DRAM_RANK_SINGLE:
		return 1;
	case BXT_DRAM_RANK_DUAL:
		return 2;
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static enum intel_dram_type bxt_get_dimm_type(u32 val)
{
	if (!bxt_get_dimm_size(val))
		return INTEL_DRAM_UNKNOWN;

	switch (val & BXT_DRAM_TYPE_MASK) {
	case BXT_DRAM_TYPE_DDR3:
		return INTEL_DRAM_DDR3;
	case BXT_DRAM_TYPE_LPDDR3:
		return INTEL_DRAM_LPDDR3;
	case BXT_DRAM_TYPE_DDR4:
		return INTEL_DRAM_DDR4;
	case BXT_DRAM_TYPE_LPDDR4:
		return INTEL_DRAM_LPDDR4;
	default:
		MISSING_CASE(val);
		return INTEL_DRAM_UNKNOWN;
	}
}

static void bxt_get_dimm_info(struct dram_dimm_info *dimm,
			      u32 val)
{
	dimm->width = bxt_get_dimm_width(val);
	dimm->ranks = bxt_get_dimm_ranks(val);

	/*
	 * Size in register is Gb per DRAM device. Convert to total
	 * GB to match the way we report this for non-LP platforms.
	 */
	dimm->size = bxt_get_dimm_size(val) * intel_dimm_num_devices(dimm) / 8;
}

static int
bxt_get_dram_info(struct drm_i915_private *dev_priv)
{
	struct dram_info *dram_info = &dev_priv->dram_info;
	u32 dram_channels;
	u32 mem_freq_khz, val;
	u8 num_active_channels;
	int i;

	val = I915_READ(BXT_P_CR_MC_BIOS_REQ_0_0_0);
	mem_freq_khz = DIV_ROUND_UP((val & BXT_REQ_DATA_MASK) *
				    BXT_MEMORY_FREQ_MULTIPLIER_HZ, 1000);

	dram_channels = val & BXT_DRAM_CHANNEL_ACTIVE_MASK;
	num_active_channels = hweight32(dram_channels);

	/* Each active bit represents 4-byte channel */
	dram_info->bandwidth_kbps = (mem_freq_khz * num_active_channels * 4);

	if (dram_info->bandwidth_kbps == 0) {
		DRM_INFO("Couldn't get system memory bandwidth\n");
		return -EINVAL;
	}

	/*
	 * Now read each DUNIT8/9/10/11 to check the rank of each dimms.
	 */
	for (i = BXT_D_CR_DRP0_DUNIT_START; i <= BXT_D_CR_DRP0_DUNIT_END; i++) {
		struct dram_dimm_info dimm;
		enum intel_dram_type type;

		val = I915_READ(BXT_D_CR_DRP0_DUNIT(i));
		if (val == 0xFFFFFFFF)
			continue;

		dram_info->num_channels++;

		bxt_get_dimm_info(&dimm, val);
		type = bxt_get_dimm_type(val);

		WARN_ON(type != INTEL_DRAM_UNKNOWN &&
			dram_info->type != INTEL_DRAM_UNKNOWN &&
			dram_info->type != type);

		DRM_DEBUG_KMS("CH%u DIMM size: %u GB, width: X%u, ranks: %u, type: %s\n",
			      i - BXT_D_CR_DRP0_DUNIT_START,
			      dimm.size, dimm.width, dimm.ranks,
			      intel_dram_type_str(type));

		/*
		 * If any of the channel is single rank channel,
		 * worst case output will be same as if single rank
		 * memory, so consider single rank memory.
		 */
		if (dram_info->ranks == 0)
			dram_info->ranks = dimm.ranks;
		else if (dimm.ranks == 1)
			dram_info->ranks = 1;

		if (type != INTEL_DRAM_UNKNOWN)
			dram_info->type = type;
	}

	if (dram_info->type == INTEL_DRAM_UNKNOWN ||
	    dram_info->ranks == 0) {
		DRM_INFO("couldn't get memory information\n");
		return -EINVAL;
	}

	dram_info->valid = true;
	return 0;
}

static void
intel_get_dram_info(struct drm_i915_private *dev_priv)
{
	struct dram_info *dram_info = &dev_priv->dram_info;
	int ret;

	/*
	 * Assume 16Gb DIMMs are present until proven otherwise.
	 * This is only used for the level 0 watermark latency
	 * w/a which does not apply to bxt/glk.
	 */
	dram_info->is_16gb_dimm = !IS_GEN9_LP(dev_priv);

	if (INTEL_GEN(dev_priv) < 9)
		return;

	if (IS_GEN9_LP(dev_priv))
		ret = bxt_get_dram_info(dev_priv);
	else
		ret = skl_get_dram_info(dev_priv);
	if (ret)
		return;

	DRM_DEBUG_KMS("DRAM bandwidth: %u kBps, channels: %u\n",
		      dram_info->bandwidth_kbps,
		      dram_info->num_channels);

	DRM_DEBUG_KMS("DRAM ranks: %u, 16Gb DIMMs: %s\n",
		      dram_info->ranks, yesno(dram_info->is_16gb_dimm));
}

static u32 gen9_edram_size_mb(struct drm_i915_private *dev_priv, u32 cap)
{
	const unsigned int ways[8] = { 4, 8, 12, 16, 16, 16, 16, 16 };
	const unsigned int sets[4] = { 1, 1, 2, 2 };

	return EDRAM_NUM_BANKS(cap) *
		ways[EDRAM_WAYS_IDX(cap)] *
		sets[EDRAM_SETS_IDX(cap)];
}

static void edram_detect(struct drm_i915_private *dev_priv)
{
	u32 edram_cap = 0;

	if (!(IS_HASWELL(dev_priv) ||
	      IS_BROADWELL(dev_priv) ||
	      INTEL_GEN(dev_priv) >= 9))
		return;

	edram_cap = __raw_uncore_read32(&dev_priv->uncore, HSW_EDRAM_CAP);

	/* NB: We can't write IDICR yet because we don't have gt funcs set up */

	if (!(edram_cap & EDRAM_ENABLED))
		return;

	/*
	 * The needed capability bits for size calculation are not there with
	 * pre gen9 so return 128MB always.
	 */
	if (INTEL_GEN(dev_priv) < 9)
		dev_priv->edram_size_mb = 128;
	else
		dev_priv->edram_size_mb =
			gen9_edram_size_mb(dev_priv, edram_cap);

	dev_info(dev_priv->drm.dev,
		 "Found %uMB of eDRAM\n", dev_priv->edram_size_mb);
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
	struct pci_dev *pdev = dev_priv->drm.pdev;
	int ret;

	if (i915_inject_probe_failure(dev_priv))
		return -ENODEV;

	intel_device_info_runtime_init(dev_priv);

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
	edram_detect(dev_priv);

	i915_perf_init(dev_priv);

	ret = i915_ggtt_probe_hw(dev_priv);
	if (ret)
		goto err_perf;

	/*
	 * WARNING: Apparently we must kick fbdev drivers before vgacon,
	 * otherwise the vga fbdev driver falls over.
	 */
	ret = i915_kick_out_firmware_fb(dev_priv);
	if (ret) {
		DRM_ERROR("failed to remove conflicting framebuffer drivers\n");
		goto err_ggtt;
	}

	ret = vga_remove_vgacon(pdev);
	if (ret) {
		DRM_ERROR("failed to remove conflicting VGA console\n");
		goto err_ggtt;
	}

	ret = i915_ggtt_init_hw(dev_priv);
	if (ret)
		goto err_ggtt;

	intel_gt_init_hw(dev_priv);

	ret = i915_ggtt_enable_hw(dev_priv);
	if (ret) {
		DRM_ERROR("failed to enable GGTT\n");
		goto err_ggtt;
	}

	pci_set_master(pdev);

	/*
	 * We don't have a max segment size, so set it to the max so sg's
	 * debugging layer doesn't complain
	 */
	dma_set_max_seg_size(&pdev->dev, UINT_MAX);

	/* overlay on gen2 is broken and can't address above 1G */
	if (IS_GEN(dev_priv, 2)) {
		ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(30));
		if (ret) {
			DRM_ERROR("failed to set DMA mask\n");

			goto err_ggtt;
		}
	}

	/* 965GM sometimes incorrectly writes to hardware status page (HWS)
	 * using 32bit addressing, overwriting memory if HWS is located
	 * above 4GB.
	 *
	 * The documentation also mentions an issue with undefined
	 * behaviour if any general state is accessed within a page above 4GB,
	 * which also needs to be handled carefully.
	 */
	if (IS_I965G(dev_priv) || IS_I965GM(dev_priv)) {
		ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));

		if (ret) {
			DRM_ERROR("failed to set DMA mask\n");

			goto err_ggtt;
		}
	}

	pm_qos_add_request(&dev_priv->pm_qos, PM_QOS_CPU_DMA_LATENCY,
			   PM_QOS_DEFAULT_VALUE);

	/* BIOS often leaves RC6 enabled, but disable it for hw init */
	intel_sanitize_gt_powersave(dev_priv);

	intel_gt_init_workarounds(dev_priv);

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
	if (INTEL_GEN(dev_priv) >= 5) {
		if (pci_enable_msi(pdev) < 0)
			DRM_DEBUG_DRIVER("can't enable MSI");
	}

	ret = intel_gvt_init(dev_priv);
	if (ret)
		goto err_msi;

	intel_opregion_setup(dev_priv);
	/*
	 * Fill the dram structure to get the system raw bandwidth and
	 * dram info. This will be used for memory latency calculation.
	 */
	intel_get_dram_info(dev_priv);

	intel_bw_init_hw(dev_priv);

	return 0;

err_msi:
	if (pdev->msi_enabled)
		pci_disable_msi(pdev);
	pm_qos_remove_request(&dev_priv->pm_qos);
err_ggtt:
	i915_ggtt_driver_release(dev_priv);
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
	struct pci_dev *pdev = dev_priv->drm.pdev;

	i915_perf_fini(dev_priv);

	if (pdev->msi_enabled)
		pci_disable_msi(pdev);

	pm_qos_remove_request(&dev_priv->pm_qos);
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

	/*
	 * Notify a valid surface after modesetting,
	 * when running inside a VM.
	 */
	if (intel_vgpu_active(dev_priv))
		I915_WRITE(vgtif_reg(display_ready), VGT_DRV_DISPLAY_READY);

	/* Reveal our presence to userspace */
	if (drm_dev_register(dev, 0) == 0) {
		i915_debugfs_register(dev_priv);
		i915_setup_sysfs(dev_priv);

		/* Depends on sysfs having been initialized */
		i915_perf_register(dev_priv);
	} else
		DRM_ERROR("Failed to register driver for userspace access!\n");

	if (HAS_DISPLAY(dev_priv)) {
		/* Must be done after probing outputs */
		intel_opregion_register(dev_priv);
		acpi_video_register();
	}

	if (IS_GEN(dev_priv, 5))
		intel_gpu_ips_init(dev_priv);

	intel_audio_init(dev_priv);

	/*
	 * Some ports require correctly set-up hpd registers for detection to
	 * work properly (leading to ghost connected connector status), e.g. VGA
	 * on gm45.  Hence we can only set up the initial fbdev config after hpd
	 * irqs are fully enabled. We do it last so that the async config
	 * cannot run before the connectors are registered.
	 */
	intel_fbdev_initial_config_async(dev);

	/*
	 * We need to coordinate the hotplugs with the asynchronous fbdev
	 * configuration, for which we use the fbdev->async_cookie.
	 */
	if (HAS_DISPLAY(dev_priv))
		drm_kms_helper_poll_init(dev);

	intel_power_domains_enable(dev_priv);
	intel_runtime_pm_enable(&dev_priv->runtime_pm);
}

/**
 * i915_driver_unregister - cleanup the registration done in i915_driver_regiser()
 * @dev_priv: device private
 */
static void i915_driver_unregister(struct drm_i915_private *dev_priv)
{
	intel_runtime_pm_disable(&dev_priv->runtime_pm);
	intel_power_domains_disable(dev_priv);

	intel_fbdev_unregister(dev_priv);
	intel_audio_deinit(dev_priv);

	/*
	 * After flushing the fbdev (incl. a late async config which will
	 * have delayed queuing of a hotplug event), then flush the hotplug
	 * events.
	 */
	drm_kms_helper_poll_fini(&dev_priv->drm);

	intel_gpu_ips_teardown();
	acpi_video_unregister();
	intel_opregion_unregister(dev_priv);

	i915_perf_unregister(dev_priv);
	i915_pmu_unregister(dev_priv);

	i915_teardown_sysfs(dev_priv);
	drm_dev_unplug(&dev_priv->drm);

	i915_gem_driver_unregister(dev_priv);
}

static void i915_welcome_messages(struct drm_i915_private *dev_priv)
{
	if (drm_debug & DRM_UT_DRIVER) {
		struct drm_printer p = drm_debug_printer("i915 device info:");

		drm_printf(&p, "pciid=0x%04x rev=0x%02x platform=%s (subplatform=0x%x) gen=%i\n",
			   INTEL_DEVID(dev_priv),
			   INTEL_REVID(dev_priv),
			   intel_platform_name(INTEL_INFO(dev_priv)->platform),
			   intel_subplatform(RUNTIME_INFO(dev_priv),
					     INTEL_INFO(dev_priv)->platform),
			   INTEL_GEN(dev_priv));

		intel_device_info_dump_flags(INTEL_INFO(dev_priv), &p);
		intel_device_info_dump_runtime(RUNTIME_INFO(dev_priv), &p);
	}

	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG))
		DRM_INFO("DRM_I915_DEBUG enabled\n");
	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		DRM_INFO("DRM_I915_DEBUG_GEM enabled\n");
	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM))
		DRM_INFO("DRM_I915_DEBUG_RUNTIME_PM enabled\n");
}

static struct drm_i915_private *
i915_driver_create(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct intel_device_info *match_info =
		(struct intel_device_info *)ent->driver_data;
	struct intel_device_info *device_info;
	struct drm_i915_private *i915;
	int err;

	i915 = kzalloc(sizeof(*i915), GFP_KERNEL);
	if (!i915)
		return ERR_PTR(-ENOMEM);

	err = drm_dev_init(&i915->drm, &driver, &pdev->dev);
	if (err) {
		kfree(i915);
		return ERR_PTR(err);
	}

	i915->drm.dev_private = i915;

	i915->drm.pdev = pdev;
	pci_set_drvdata(pdev, i915);

	/* Setup the write-once "constant" device info */
	device_info = mkwrite_device_info(i915);
	memcpy(device_info, match_info, sizeof(*device_info));
	RUNTIME_INFO(i915)->device_id = pdev->device;

	BUG_ON(device_info->gen > BITS_PER_TYPE(device_info->gen_mask));

	return i915;
}

static void i915_driver_destroy(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = i915->drm.pdev;

	drm_dev_fini(&i915->drm);
	kfree(i915);

	/* And make sure we never chase our dangling pointer from pci_dev */
	pci_set_drvdata(pdev, NULL);
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
	struct drm_i915_private *dev_priv;
	int ret;

	dev_priv = i915_driver_create(pdev, ent);
	if (IS_ERR(dev_priv))
		return PTR_ERR(dev_priv);

	/* Disable nuclear pageflip by default on pre-ILK */
	if (!i915_modparams.nuclear_pageflip && match_info->gen < 5)
		dev_priv->drm.driver_features &= ~DRIVER_ATOMIC;

	ret = pci_enable_device(pdev);
	if (ret)
		goto out_fini;

	ret = i915_driver_early_probe(dev_priv);
	if (ret < 0)
		goto out_pci_disable;

	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	i915_detect_vgpu(dev_priv);

	ret = i915_driver_mmio_probe(dev_priv);
	if (ret < 0)
		goto out_runtime_pm_put;

	ret = i915_driver_hw_probe(dev_priv);
	if (ret < 0)
		goto out_cleanup_mmio;

	ret = i915_driver_modeset_probe(&dev_priv->drm);
	if (ret < 0)
		goto out_cleanup_hw;

	i915_driver_register(dev_priv);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	i915_welcome_messages(dev_priv);

	return 0;

out_cleanup_hw:
	i915_driver_hw_remove(dev_priv);
	i915_ggtt_driver_release(dev_priv);

	/* Paranoia: make sure we have disabled everything before we exit. */
	intel_sanitize_gt_powersave(dev_priv);
out_cleanup_mmio:
	i915_driver_mmio_release(dev_priv);
out_runtime_pm_put:
	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);
	i915_driver_late_release(dev_priv);
out_pci_disable:
	pci_disable_device(pdev);
out_fini:
	i915_probe_error(dev_priv, "Device initialization failed (%d)\n", ret);
	i915_driver_destroy(dev_priv);
	return ret;
}

void i915_driver_remove(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = i915->drm.pdev;

	disable_rpm_wakeref_asserts(&i915->runtime_pm);

	i915_driver_unregister(i915);

	/*
	 * After unregistering the device to prevent any new users, cancel
	 * all in-flight requests so that we can quickly unbind the active
	 * resources.
	 */
	intel_gt_set_wedged(&i915->gt);

	/* Flush any external code that still may be under the RCU lock */
	synchronize_rcu();

	i915_gem_suspend(i915);

	drm_atomic_helper_shutdown(&i915->drm);

	intel_gvt_driver_remove(i915);

	intel_modeset_driver_remove(&i915->drm);

	intel_bios_driver_remove(i915);

	vga_switcheroo_unregister_client(pdev);
	vga_client_register(pdev, NULL, NULL, NULL);

	intel_csr_ucode_fini(i915);

	/* Free error state after interrupts are fully disabled. */
	cancel_delayed_work_sync(&i915->gt.hangcheck.work);
	i915_reset_error_state(i915);

	i915_gem_driver_remove(i915);

	intel_power_domains_driver_remove(i915);

	i915_driver_hw_remove(i915);

	enable_rpm_wakeref_asserts(&i915->runtime_pm);
}

static void i915_driver_release(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;

	disable_rpm_wakeref_asserts(rpm);

	i915_gem_driver_release(dev_priv);

	i915_ggtt_driver_release(dev_priv);

	/* Paranoia: make sure we have disabled everything before we exit. */
	intel_sanitize_gt_powersave(dev_priv);

	i915_driver_mmio_release(dev_priv);

	enable_rpm_wakeref_asserts(rpm);
	intel_runtime_pm_driver_release(rpm);

	i915_driver_late_release(dev_priv);
	i915_driver_destroy(dev_priv);
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
	intel_fbdev_restore_mode(dev);
	vga_switcheroo_process_delayed_switch();
}

static void i915_driver_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	mutex_lock(&dev->struct_mutex);
	i915_gem_context_close(file);
	i915_gem_release(dev, file);
	mutex_unlock(&dev->struct_mutex);

	kfree(file_priv);

	/* Catch up with all the deferred frees from "this" client */
	i915_gem_flush_free_objects(to_i915(dev));
}

static void intel_suspend_encoders(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_encoder *encoder;

	drm_modeset_lock_all(dev);
	for_each_intel_encoder(dev, encoder)
		if (encoder->suspend)
			encoder->suspend(encoder);
	drm_modeset_unlock_all(dev);
}

static int vlv_resume_prepare(struct drm_i915_private *dev_priv,
			      bool rpm_resume);
static int vlv_suspend_complete(struct drm_i915_private *dev_priv);

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
	i915_gem_suspend(i915);

	return 0;
}

static int i915_drm_suspend(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = dev_priv->drm.pdev;
	pci_power_t opregion_target_state;

	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	/* We do a lot of poking in a lot of registers, make sure they work
	 * properly. */
	intel_power_domains_disable(dev_priv);

	drm_kms_helper_poll_disable(dev);

	pci_save_state(pdev);

	intel_display_suspend(dev);

	intel_dp_mst_suspend(dev_priv);

	intel_runtime_pm_disable_interrupts(dev_priv);
	intel_hpd_cancel_work(dev_priv);

	intel_suspend_encoders(dev_priv);

	intel_suspend_hw(dev_priv);

	i915_gem_suspend_gtt_mappings(dev_priv);

	i915_save_state(dev_priv);

	opregion_target_state = suspend_to_idle(dev_priv) ? PCI_D1 : PCI_D3cold;
	intel_opregion_suspend(dev_priv, opregion_target_state);

	intel_fbdev_set_suspend(dev, FBINFO_STATE_SUSPENDED, true);

	dev_priv->suspend_count++;

	intel_csr_ucode_suspend(dev_priv);

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
	struct pci_dev *pdev = dev_priv->drm.pdev;
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;
	int ret = 0;

	disable_rpm_wakeref_asserts(rpm);

	i915_gem_suspend_late(dev_priv);

	i915_rc6_ctx_wa_suspend(dev_priv);

	intel_uncore_suspend(&dev_priv->uncore);

	intel_power_domains_suspend(dev_priv,
				    get_suspend_mode(dev_priv, hibernation));

	intel_display_power_suspend_late(dev_priv);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		ret = vlv_suspend_complete(dev_priv);

	if (ret) {
		DRM_ERROR("Suspend complete failed: %d\n", ret);
		intel_power_domains_resume(dev_priv);

		goto out;
	}

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
	if (!(hibernation && INTEL_GEN(dev_priv) < 6))
		pci_set_power_state(pdev, PCI_D3hot);

out:
	enable_rpm_wakeref_asserts(rpm);
	if (!dev_priv->uncore.user_forcewake_count)
		intel_runtime_pm_driver_release(rpm);

	return ret;
}

static int
i915_suspend_switcheroo(struct drm_i915_private *i915, pm_message_t state)
{
	int error;

	if (WARN_ON_ONCE(state.event != PM_EVENT_SUSPEND &&
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
	intel_sanitize_gt_powersave(dev_priv);

	i915_gem_sanitize(dev_priv);

	ret = i915_ggtt_enable_hw(dev_priv);
	if (ret)
		DRM_ERROR("failed to re-enable GGTT\n");

	mutex_lock(&dev_priv->drm.struct_mutex);
	i915_gem_restore_gtt_mappings(dev_priv);
	i915_gem_restore_fences(dev_priv);
	mutex_unlock(&dev_priv->drm.struct_mutex);

	intel_csr_ucode_resume(dev_priv);

	i915_restore_state(dev_priv);
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

	drm_mode_config_reset(dev);

	i915_gem_resume(dev_priv);

	intel_modeset_init_hw(dev);
	intel_init_clock_gating(dev_priv);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display.hpd_irq_setup)
		dev_priv->display.hpd_irq_setup(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	intel_dp_mst_resume(dev_priv);

	intel_display_resume(dev);

	drm_kms_helper_poll_enable(dev);

	/*
	 * ... but also need to make sure that hotplug processing
	 * doesn't cause havoc. Like in the driver load code we don't
	 * bother with the tiny race here where we might lose hotplug
	 * notifications.
	 * */
	intel_hpd_init(dev_priv);

	intel_opregion_resume(dev_priv);

	intel_fbdev_set_suspend(dev, FBINFO_STATE_RUNNING, false);

	intel_power_domains_enable(dev_priv);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return 0;
}

static int i915_drm_resume_early(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = dev_priv->drm.pdev;
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
		DRM_ERROR("failed to set PCI D0 power state (%d)\n", ret);
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

	disable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		ret = vlv_resume_prepare(dev_priv, false);
	if (ret)
		DRM_ERROR("Resume prepare failed: %d, continuing anyway\n",
			  ret);

	intel_uncore_resume_early(&dev_priv->uncore);

	intel_gt_check_and_clear_faults(&dev_priv->gt);

	intel_display_power_resume_early(dev_priv);

	intel_sanitize_gt_powersave(dev_priv);

	intel_power_domains_resume(dev_priv);

	i915_rc6_ctx_wa_resume(dev_priv);

	intel_gt_sanitize(&dev_priv->gt, true);

	enable_rpm_wakeref_asserts(&dev_priv->runtime_pm);

	return ret;
}

static int i915_resume_switcheroo(struct drm_i915_private *i915)
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

/*
 * Save all Gunit registers that may be lost after a D3 and a subsequent
 * S0i[R123] transition. The list of registers needing a save/restore is
 * defined in the VLV2_S0IXRegs document. This documents marks all Gunit
 * registers in the following way:
 * - Driver: saved/restored by the driver
 * - Punit : saved/restored by the Punit firmware
 * - No, w/o marking: no need to save/restore, since the register is R/O or
 *                    used internally by the HW in a way that doesn't depend
 *                    keeping the content across a suspend/resume.
 * - Debug : used for debugging
 *
 * We save/restore all registers marked with 'Driver', with the following
 * exceptions:
 * - Registers out of use, including also registers marked with 'Debug'.
 *   These have no effect on the driver's operation, so we don't save/restore
 *   them to reduce the overhead.
 * - Registers that are fully setup by an initialization function called from
 *   the resume path. For example many clock gating and RPS/RC6 registers.
 * - Registers that provide the right functionality with their reset defaults.
 *
 * TODO: Except for registers that based on the above 3 criteria can be safely
 * ignored, we save/restore all others, practically treating the HW context as
 * a black-box for the driver. Further investigation is needed to reduce the
 * saved/restored registers even further, by following the same 3 criteria.
 */
static void vlv_save_gunit_s0ix_state(struct drm_i915_private *dev_priv)
{
	struct vlv_s0ix_state *s = dev_priv->vlv_s0ix_state;
	int i;

	if (!s)
		return;

	/* GAM 0x4000-0x4770 */
	s->wr_watermark		= I915_READ(GEN7_WR_WATERMARK);
	s->gfx_prio_ctrl	= I915_READ(GEN7_GFX_PRIO_CTRL);
	s->arb_mode		= I915_READ(ARB_MODE);
	s->gfx_pend_tlb0	= I915_READ(GEN7_GFX_PEND_TLB0);
	s->gfx_pend_tlb1	= I915_READ(GEN7_GFX_PEND_TLB1);

	for (i = 0; i < ARRAY_SIZE(s->lra_limits); i++)
		s->lra_limits[i] = I915_READ(GEN7_LRA_LIMITS(i));

	s->media_max_req_count	= I915_READ(GEN7_MEDIA_MAX_REQ_COUNT);
	s->gfx_max_req_count	= I915_READ(GEN7_GFX_MAX_REQ_COUNT);

	s->render_hwsp		= I915_READ(RENDER_HWS_PGA_GEN7);
	s->ecochk		= I915_READ(GAM_ECOCHK);
	s->bsd_hwsp		= I915_READ(BSD_HWS_PGA_GEN7);
	s->blt_hwsp		= I915_READ(BLT_HWS_PGA_GEN7);

	s->tlb_rd_addr		= I915_READ(GEN7_TLB_RD_ADDR);

	/* MBC 0x9024-0x91D0, 0x8500 */
	s->g3dctl		= I915_READ(VLV_G3DCTL);
	s->gsckgctl		= I915_READ(VLV_GSCKGCTL);
	s->mbctl		= I915_READ(GEN6_MBCTL);

	/* GCP 0x9400-0x9424, 0x8100-0x810C */
	s->ucgctl1		= I915_READ(GEN6_UCGCTL1);
	s->ucgctl3		= I915_READ(GEN6_UCGCTL3);
	s->rcgctl1		= I915_READ(GEN6_RCGCTL1);
	s->rcgctl2		= I915_READ(GEN6_RCGCTL2);
	s->rstctl		= I915_READ(GEN6_RSTCTL);
	s->misccpctl		= I915_READ(GEN7_MISCCPCTL);

	/* GPM 0xA000-0xAA84, 0x8000-0x80FC */
	s->gfxpause		= I915_READ(GEN6_GFXPAUSE);
	s->rpdeuhwtc		= I915_READ(GEN6_RPDEUHWTC);
	s->rpdeuc		= I915_READ(GEN6_RPDEUC);
	s->ecobus		= I915_READ(ECOBUS);
	s->pwrdwnupctl		= I915_READ(VLV_PWRDWNUPCTL);
	s->rp_down_timeout	= I915_READ(GEN6_RP_DOWN_TIMEOUT);
	s->rp_deucsw		= I915_READ(GEN6_RPDEUCSW);
	s->rcubmabdtmr		= I915_READ(GEN6_RCUBMABDTMR);
	s->rcedata		= I915_READ(VLV_RCEDATA);
	s->spare2gh		= I915_READ(VLV_SPAREG2H);

	/* Display CZ domain, 0x4400C-0x4402C, 0x4F000-0x4F11F */
	s->gt_imr		= I915_READ(GTIMR);
	s->gt_ier		= I915_READ(GTIER);
	s->pm_imr		= I915_READ(GEN6_PMIMR);
	s->pm_ier		= I915_READ(GEN6_PMIER);

	for (i = 0; i < ARRAY_SIZE(s->gt_scratch); i++)
		s->gt_scratch[i] = I915_READ(GEN7_GT_SCRATCH(i));

	/* GT SA CZ domain, 0x100000-0x138124 */
	s->tilectl		= I915_READ(TILECTL);
	s->gt_fifoctl		= I915_READ(GTFIFOCTL);
	s->gtlc_wake_ctrl	= I915_READ(VLV_GTLC_WAKE_CTRL);
	s->gtlc_survive		= I915_READ(VLV_GTLC_SURVIVABILITY_REG);
	s->pmwgicz		= I915_READ(VLV_PMWGICZ);

	/* Gunit-Display CZ domain, 0x182028-0x1821CF */
	s->gu_ctl0		= I915_READ(VLV_GU_CTL0);
	s->gu_ctl1		= I915_READ(VLV_GU_CTL1);
	s->pcbr			= I915_READ(VLV_PCBR);
	s->clock_gate_dis2	= I915_READ(VLV_GUNIT_CLOCK_GATE2);

	/*
	 * Not saving any of:
	 * DFT,		0x9800-0x9EC0
	 * SARB,	0xB000-0xB1FC
	 * GAC,		0x5208-0x524C, 0x14000-0x14C000
	 * PCI CFG
	 */
}

static void vlv_restore_gunit_s0ix_state(struct drm_i915_private *dev_priv)
{
	struct vlv_s0ix_state *s = dev_priv->vlv_s0ix_state;
	u32 val;
	int i;

	if (!s)
		return;

	/* GAM 0x4000-0x4770 */
	I915_WRITE(GEN7_WR_WATERMARK,	s->wr_watermark);
	I915_WRITE(GEN7_GFX_PRIO_CTRL,	s->gfx_prio_ctrl);
	I915_WRITE(ARB_MODE,		s->arb_mode | (0xffff << 16));
	I915_WRITE(GEN7_GFX_PEND_TLB0,	s->gfx_pend_tlb0);
	I915_WRITE(GEN7_GFX_PEND_TLB1,	s->gfx_pend_tlb1);

	for (i = 0; i < ARRAY_SIZE(s->lra_limits); i++)
		I915_WRITE(GEN7_LRA_LIMITS(i), s->lra_limits[i]);

	I915_WRITE(GEN7_MEDIA_MAX_REQ_COUNT, s->media_max_req_count);
	I915_WRITE(GEN7_GFX_MAX_REQ_COUNT, s->gfx_max_req_count);

	I915_WRITE(RENDER_HWS_PGA_GEN7,	s->render_hwsp);
	I915_WRITE(GAM_ECOCHK,		s->ecochk);
	I915_WRITE(BSD_HWS_PGA_GEN7,	s->bsd_hwsp);
	I915_WRITE(BLT_HWS_PGA_GEN7,	s->blt_hwsp);

	I915_WRITE(GEN7_TLB_RD_ADDR,	s->tlb_rd_addr);

	/* MBC 0x9024-0x91D0, 0x8500 */
	I915_WRITE(VLV_G3DCTL,		s->g3dctl);
	I915_WRITE(VLV_GSCKGCTL,	s->gsckgctl);
	I915_WRITE(GEN6_MBCTL,		s->mbctl);

	/* GCP 0x9400-0x9424, 0x8100-0x810C */
	I915_WRITE(GEN6_UCGCTL1,	s->ucgctl1);
	I915_WRITE(GEN6_UCGCTL3,	s->ucgctl3);
	I915_WRITE(GEN6_RCGCTL1,	s->rcgctl1);
	I915_WRITE(GEN6_RCGCTL2,	s->rcgctl2);
	I915_WRITE(GEN6_RSTCTL,		s->rstctl);
	I915_WRITE(GEN7_MISCCPCTL,	s->misccpctl);

	/* GPM 0xA000-0xAA84, 0x8000-0x80FC */
	I915_WRITE(GEN6_GFXPAUSE,	s->gfxpause);
	I915_WRITE(GEN6_RPDEUHWTC,	s->rpdeuhwtc);
	I915_WRITE(GEN6_RPDEUC,		s->rpdeuc);
	I915_WRITE(ECOBUS,		s->ecobus);
	I915_WRITE(VLV_PWRDWNUPCTL,	s->pwrdwnupctl);
	I915_WRITE(GEN6_RP_DOWN_TIMEOUT,s->rp_down_timeout);
	I915_WRITE(GEN6_RPDEUCSW,	s->rp_deucsw);
	I915_WRITE(GEN6_RCUBMABDTMR,	s->rcubmabdtmr);
	I915_WRITE(VLV_RCEDATA,		s->rcedata);
	I915_WRITE(VLV_SPAREG2H,	s->spare2gh);

	/* Display CZ domain, 0x4400C-0x4402C, 0x4F000-0x4F11F */
	I915_WRITE(GTIMR,		s->gt_imr);
	I915_WRITE(GTIER,		s->gt_ier);
	I915_WRITE(GEN6_PMIMR,		s->pm_imr);
	I915_WRITE(GEN6_PMIER,		s->pm_ier);

	for (i = 0; i < ARRAY_SIZE(s->gt_scratch); i++)
		I915_WRITE(GEN7_GT_SCRATCH(i), s->gt_scratch[i]);

	/* GT SA CZ domain, 0x100000-0x138124 */
	I915_WRITE(TILECTL,			s->tilectl);
	I915_WRITE(GTFIFOCTL,			s->gt_fifoctl);
	/*
	 * Preserve the GT allow wake and GFX force clock bit, they are not
	 * be restored, as they are used to control the s0ix suspend/resume
	 * sequence by the caller.
	 */
	val = I915_READ(VLV_GTLC_WAKE_CTRL);
	val &= VLV_GTLC_ALLOWWAKEREQ;
	val |= s->gtlc_wake_ctrl & ~VLV_GTLC_ALLOWWAKEREQ;
	I915_WRITE(VLV_GTLC_WAKE_CTRL, val);

	val = I915_READ(VLV_GTLC_SURVIVABILITY_REG);
	val &= VLV_GFX_CLK_FORCE_ON_BIT;
	val |= s->gtlc_survive & ~VLV_GFX_CLK_FORCE_ON_BIT;
	I915_WRITE(VLV_GTLC_SURVIVABILITY_REG, val);

	I915_WRITE(VLV_PMWGICZ,			s->pmwgicz);

	/* Gunit-Display CZ domain, 0x182028-0x1821CF */
	I915_WRITE(VLV_GU_CTL0,			s->gu_ctl0);
	I915_WRITE(VLV_GU_CTL1,			s->gu_ctl1);
	I915_WRITE(VLV_PCBR,			s->pcbr);
	I915_WRITE(VLV_GUNIT_CLOCK_GATE2,	s->clock_gate_dis2);
}

static int vlv_wait_for_pw_status(struct drm_i915_private *i915,
				  u32 mask, u32 val)
{
	i915_reg_t reg = VLV_GTLC_PW_STATUS;
	u32 reg_value;
	int ret;

	/* The HW does not like us polling for PW_STATUS frequently, so
	 * use the sleeping loop rather than risk the busy spin within
	 * intel_wait_for_register().
	 *
	 * Transitioning between RC6 states should be at most 2ms (see
	 * valleyview_enable_rps) so use a 3ms timeout.
	 */
	ret = wait_for(((reg_value =
			 intel_uncore_read_notrace(&i915->uncore, reg)) & mask)
		       == val, 3);

	/* just trace the final value */
	trace_i915_reg_rw(false, reg, reg_value, sizeof(reg_value), true);

	return ret;
}

int vlv_force_gfx_clock(struct drm_i915_private *dev_priv, bool force_on)
{
	u32 val;
	int err;

	val = I915_READ(VLV_GTLC_SURVIVABILITY_REG);
	val &= ~VLV_GFX_CLK_FORCE_ON_BIT;
	if (force_on)
		val |= VLV_GFX_CLK_FORCE_ON_BIT;
	I915_WRITE(VLV_GTLC_SURVIVABILITY_REG, val);

	if (!force_on)
		return 0;

	err = intel_wait_for_register(&dev_priv->uncore,
				      VLV_GTLC_SURVIVABILITY_REG,
				      VLV_GFX_CLK_STATUS_BIT,
				      VLV_GFX_CLK_STATUS_BIT,
				      20);
	if (err)
		DRM_ERROR("timeout waiting for GFX clock force-on (%08x)\n",
			  I915_READ(VLV_GTLC_SURVIVABILITY_REG));

	return err;
}

static int vlv_allow_gt_wake(struct drm_i915_private *dev_priv, bool allow)
{
	u32 mask;
	u32 val;
	int err;

	val = I915_READ(VLV_GTLC_WAKE_CTRL);
	val &= ~VLV_GTLC_ALLOWWAKEREQ;
	if (allow)
		val |= VLV_GTLC_ALLOWWAKEREQ;
	I915_WRITE(VLV_GTLC_WAKE_CTRL, val);
	POSTING_READ(VLV_GTLC_WAKE_CTRL);

	mask = VLV_GTLC_ALLOWWAKEACK;
	val = allow ? mask : 0;

	err = vlv_wait_for_pw_status(dev_priv, mask, val);
	if (err)
		DRM_ERROR("timeout disabling GT waking\n");

	return err;
}

static void vlv_wait_for_gt_wells(struct drm_i915_private *dev_priv,
				  bool wait_for_on)
{
	u32 mask;
	u32 val;

	mask = VLV_GTLC_PW_MEDIA_STATUS_MASK | VLV_GTLC_PW_RENDER_STATUS_MASK;
	val = wait_for_on ? mask : 0;

	/*
	 * RC6 transitioning can be delayed up to 2 msec (see
	 * valleyview_enable_rps), use 3 msec for safety.
	 *
	 * This can fail to turn off the rc6 if the GPU is stuck after a failed
	 * reset and we are trying to force the machine to sleep.
	 */
	if (vlv_wait_for_pw_status(dev_priv, mask, val))
		DRM_DEBUG_DRIVER("timeout waiting for GT wells to go %s\n",
				 onoff(wait_for_on));
}

static void vlv_check_no_gt_access(struct drm_i915_private *dev_priv)
{
	if (!(I915_READ(VLV_GTLC_PW_STATUS) & VLV_GTLC_ALLOWWAKEERR))
		return;

	DRM_DEBUG_DRIVER("GT register access while GT waking disabled\n");
	I915_WRITE(VLV_GTLC_PW_STATUS, VLV_GTLC_ALLOWWAKEERR);
}

static int vlv_suspend_complete(struct drm_i915_private *dev_priv)
{
	u32 mask;
	int err;

	/*
	 * Bspec defines the following GT well on flags as debug only, so
	 * don't treat them as hard failures.
	 */
	vlv_wait_for_gt_wells(dev_priv, false);

	mask = VLV_GTLC_RENDER_CTX_EXISTS | VLV_GTLC_MEDIA_CTX_EXISTS;
	WARN_ON((I915_READ(VLV_GTLC_WAKE_CTRL) & mask) != mask);

	vlv_check_no_gt_access(dev_priv);

	err = vlv_force_gfx_clock(dev_priv, true);
	if (err)
		goto err1;

	err = vlv_allow_gt_wake(dev_priv, false);
	if (err)
		goto err2;

	vlv_save_gunit_s0ix_state(dev_priv);

	err = vlv_force_gfx_clock(dev_priv, false);
	if (err)
		goto err2;

	return 0;

err2:
	/* For safety always re-enable waking and disable gfx clock forcing */
	vlv_allow_gt_wake(dev_priv, true);
err1:
	vlv_force_gfx_clock(dev_priv, false);

	return err;
}

static int vlv_resume_prepare(struct drm_i915_private *dev_priv,
				bool rpm_resume)
{
	int err;
	int ret;

	/*
	 * If any of the steps fail just try to continue, that's the best we
	 * can do at this point. Return the first error code (which will also
	 * leave RPM permanently disabled).
	 */
	ret = vlv_force_gfx_clock(dev_priv, true);

	vlv_restore_gunit_s0ix_state(dev_priv);

	err = vlv_allow_gt_wake(dev_priv, true);
	if (!ret)
		ret = err;

	err = vlv_force_gfx_clock(dev_priv, false);
	if (!ret)
		ret = err;

	vlv_check_no_gt_access(dev_priv);

	if (rpm_resume)
		intel_init_clock_gating(dev_priv);

	return ret;
}

static int intel_runtime_suspend(struct device *kdev)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(kdev);
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;
	int ret = 0;

	if (WARN_ON_ONCE(!(dev_priv->gt_pm.rc6.enabled && HAS_RC6(dev_priv))))
		return -ENODEV;

	if (WARN_ON_ONCE(!HAS_RUNTIME_PM(dev_priv)))
		return -ENODEV;

	DRM_DEBUG_KMS("Suspending device\n");

	disable_rpm_wakeref_asserts(rpm);

	/*
	 * We are safe here against re-faults, since the fault handler takes
	 * an RPM reference.
	 */
	i915_gem_runtime_suspend(dev_priv);

	intel_gt_runtime_suspend(&dev_priv->gt);

	intel_runtime_pm_disable_interrupts(dev_priv);

	intel_uncore_suspend(&dev_priv->uncore);

	intel_display_power_suspend(dev_priv);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		ret = vlv_suspend_complete(dev_priv);

	if (ret) {
		DRM_ERROR("Runtime suspend failed, disabling it (%d)\n", ret);
		intel_uncore_runtime_resume(&dev_priv->uncore);

		intel_runtime_pm_enable_interrupts(dev_priv);

		intel_gt_runtime_resume(&dev_priv->gt);

		i915_gem_restore_fences(dev_priv);

		enable_rpm_wakeref_asserts(rpm);

		return ret;
	}

	enable_rpm_wakeref_asserts(rpm);
	intel_runtime_pm_driver_release(rpm);

	if (intel_uncore_arm_unclaimed_mmio_detection(&dev_priv->uncore))
		DRM_ERROR("Unclaimed access detected prior to suspending\n");

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
		intel_hpd_poll_init(dev_priv);

	DRM_DEBUG_KMS("Device suspended\n");
	return 0;
}

static int intel_runtime_resume(struct device *kdev)
{
	struct drm_i915_private *dev_priv = kdev_to_i915(kdev);
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;
	int ret = 0;

	if (WARN_ON_ONCE(!HAS_RUNTIME_PM(dev_priv)))
		return -ENODEV;

	DRM_DEBUG_KMS("Resuming device\n");

	WARN_ON_ONCE(atomic_read(&rpm->wakeref_count));
	disable_rpm_wakeref_asserts(rpm);

	intel_opregion_notify_adapter(dev_priv, PCI_D0);
	rpm->suspended = false;
	if (intel_uncore_unclaimed_mmio(&dev_priv->uncore))
		DRM_DEBUG_DRIVER("Unclaimed access during suspend, bios?\n");

	intel_display_power_resume(dev_priv);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		ret = vlv_resume_prepare(dev_priv, true);

	intel_uncore_runtime_resume(&dev_priv->uncore);

	intel_runtime_pm_enable_interrupts(dev_priv);

	/*
	 * No point of rolling back things in case of an error, as the best
	 * we can do is to hope that things will still work (and disable RPM).
	 */
	intel_gt_runtime_resume(&dev_priv->gt);
	i915_gem_restore_fences(dev_priv);

	/*
	 * On VLV/CHV display interrupts are part of the display
	 * power well, so hpd is reinitialized from there. For
	 * everyone else do it here.
	 */
	if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv))
		intel_hpd_init(dev_priv);

	intel_enable_ipc(dev_priv);

	enable_rpm_wakeref_asserts(rpm);

	if (ret)
		DRM_ERROR("Runtime resume failed, disabling it (%d)\n", ret);
	else
		DRM_DEBUG_KMS("Device resumed\n");

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

static const struct vm_operations_struct i915_gem_vm_ops = {
	.fault = i915_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct file_operations i915_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.compat_ioctl = i915_compat_ioctl,
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
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER, i915_gem_execbuffer_ioctl, DRM_AUTH),
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
	DRM_IOCTL_DEF_DRV(I915_GEM_PREAD, i915_gem_pread_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PWRITE, i915_gem_pwrite_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_MMAP, i915_gem_mmap_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_MMAP_GTT, i915_gem_mmap_gtt_ioctl, DRM_RENDER_ALLOW),
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

static struct drm_driver driver = {
	/* Don't use MTRRs here; the Xserver or userspace app should
	 * deal with them for Intel hardware.
	 */
	.driver_features =
	    DRIVER_GEM |
	    DRIVER_RENDER | DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_SYNCOBJ,
	.release = i915_driver_release,
	.open = i915_driver_open,
	.lastclose = i915_driver_lastclose,
	.postclose = i915_driver_postclose,

	.gem_close_object = i915_gem_close_object,
	.gem_free_object_unlocked = i915_gem_free_object,
	.gem_vm_ops = &i915_gem_vm_ops,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = i915_gem_prime_export,
	.gem_prime_import = i915_gem_prime_import,

	.get_vblank_timestamp = drm_calc_vbltimestamp_from_scanoutpos,
	.get_scanout_position = i915_get_crtc_scanoutpos,

	.dumb_create = i915_gem_dumb_create,
	.dumb_map_offset = i915_gem_mmap_gtt,
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

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_drm.c"
#endif
