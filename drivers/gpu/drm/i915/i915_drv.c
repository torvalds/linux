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

#include <linux/device.h>
#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_drv.h"

#include <linux/console.h>
#include <linux/module.h>
#include <drm/drm_crtc_helper.h>

static struct drm_driver driver;

#define GEN_DEFAULT_PIPEOFFSETS \
	.pipe_offsets = { PIPE_A_OFFSET, PIPE_B_OFFSET, \
			  PIPE_C_OFFSET, PIPE_EDP_OFFSET }, \
	.trans_offsets = { TRANSCODER_A_OFFSET, TRANSCODER_B_OFFSET, \
			   TRANSCODER_C_OFFSET, TRANSCODER_EDP_OFFSET }, \
	.dpll_offsets = { DPLL_A_OFFSET, DPLL_B_OFFSET }, \
	.dpll_md_offsets = { DPLL_A_MD_OFFSET, DPLL_B_MD_OFFSET }, \
	.palette_offsets = { PALETTE_A_OFFSET, PALETTE_B_OFFSET }


static const struct intel_device_info intel_i830_info = {
	.gen = 2, .is_mobile = 1, .cursor_needs_physical = 1, .num_pipes = 2,
	.has_overlay = 1, .overlay_needs_physical = 1,
	.ring_mask = RENDER_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_845g_info = {
	.gen = 2, .num_pipes = 1,
	.has_overlay = 1, .overlay_needs_physical = 1,
	.ring_mask = RENDER_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_i85x_info = {
	.gen = 2, .is_i85x = 1, .is_mobile = 1, .num_pipes = 2,
	.cursor_needs_physical = 1,
	.has_overlay = 1, .overlay_needs_physical = 1,
	.has_fbc = 1,
	.ring_mask = RENDER_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_i865g_info = {
	.gen = 2, .num_pipes = 1,
	.has_overlay = 1, .overlay_needs_physical = 1,
	.ring_mask = RENDER_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_i915g_info = {
	.gen = 3, .is_i915g = 1, .cursor_needs_physical = 1, .num_pipes = 2,
	.has_overlay = 1, .overlay_needs_physical = 1,
	.ring_mask = RENDER_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};
static const struct intel_device_info intel_i915gm_info = {
	.gen = 3, .is_mobile = 1, .num_pipes = 2,
	.cursor_needs_physical = 1,
	.has_overlay = 1, .overlay_needs_physical = 1,
	.supports_tv = 1,
	.has_fbc = 1,
	.ring_mask = RENDER_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};
static const struct intel_device_info intel_i945g_info = {
	.gen = 3, .has_hotplug = 1, .cursor_needs_physical = 1, .num_pipes = 2,
	.has_overlay = 1, .overlay_needs_physical = 1,
	.ring_mask = RENDER_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};
static const struct intel_device_info intel_i945gm_info = {
	.gen = 3, .is_i945gm = 1, .is_mobile = 1, .num_pipes = 2,
	.has_hotplug = 1, .cursor_needs_physical = 1,
	.has_overlay = 1, .overlay_needs_physical = 1,
	.supports_tv = 1,
	.has_fbc = 1,
	.ring_mask = RENDER_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_i965g_info = {
	.gen = 4, .is_broadwater = 1, .num_pipes = 2,
	.has_hotplug = 1,
	.has_overlay = 1,
	.ring_mask = RENDER_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_i965gm_info = {
	.gen = 4, .is_crestline = 1, .num_pipes = 2,
	.is_mobile = 1, .has_fbc = 1, .has_hotplug = 1,
	.has_overlay = 1,
	.supports_tv = 1,
	.ring_mask = RENDER_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_g33_info = {
	.gen = 3, .is_g33 = 1, .num_pipes = 2,
	.need_gfx_hws = 1, .has_hotplug = 1,
	.has_overlay = 1,
	.ring_mask = RENDER_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_g45_info = {
	.gen = 4, .is_g4x = 1, .need_gfx_hws = 1, .num_pipes = 2,
	.has_pipe_cxsr = 1, .has_hotplug = 1,
	.ring_mask = RENDER_RING | BSD_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_gm45_info = {
	.gen = 4, .is_g4x = 1, .num_pipes = 2,
	.is_mobile = 1, .need_gfx_hws = 1, .has_fbc = 1,
	.has_pipe_cxsr = 1, .has_hotplug = 1,
	.supports_tv = 1,
	.ring_mask = RENDER_RING | BSD_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_pineview_info = {
	.gen = 3, .is_g33 = 1, .is_pineview = 1, .is_mobile = 1, .num_pipes = 2,
	.need_gfx_hws = 1, .has_hotplug = 1,
	.has_overlay = 1,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_ironlake_d_info = {
	.gen = 5, .num_pipes = 2,
	.need_gfx_hws = 1, .has_hotplug = 1,
	.ring_mask = RENDER_RING | BSD_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_ironlake_m_info = {
	.gen = 5, .is_mobile = 1, .num_pipes = 2,
	.need_gfx_hws = 1, .has_hotplug = 1,
	.has_fbc = 1,
	.ring_mask = RENDER_RING | BSD_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_sandybridge_d_info = {
	.gen = 6, .num_pipes = 2,
	.need_gfx_hws = 1, .has_hotplug = 1,
	.has_fbc = 1,
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING,
	.has_llc = 1,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_sandybridge_m_info = {
	.gen = 6, .is_mobile = 1, .num_pipes = 2,
	.need_gfx_hws = 1, .has_hotplug = 1,
	.has_fbc = 1,
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING,
	.has_llc = 1,
	GEN_DEFAULT_PIPEOFFSETS,
};

#define GEN7_FEATURES  \
	.gen = 7, .num_pipes = 3, \
	.need_gfx_hws = 1, .has_hotplug = 1, \
	.has_fbc = 1, \
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING, \
	.has_llc = 1

static const struct intel_device_info intel_ivybridge_d_info = {
	GEN7_FEATURES,
	.is_ivybridge = 1,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_ivybridge_m_info = {
	GEN7_FEATURES,
	.is_ivybridge = 1,
	.is_mobile = 1,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_ivybridge_q_info = {
	GEN7_FEATURES,
	.is_ivybridge = 1,
	.num_pipes = 0, /* legal, last one wins */
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_valleyview_m_info = {
	GEN7_FEATURES,
	.is_mobile = 1,
	.num_pipes = 2,
	.is_valleyview = 1,
	.display_mmio_offset = VLV_DISPLAY_BASE,
	.has_fbc = 0, /* legal, last one wins */
	.has_llc = 0, /* legal, last one wins */
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_valleyview_d_info = {
	GEN7_FEATURES,
	.num_pipes = 2,
	.is_valleyview = 1,
	.display_mmio_offset = VLV_DISPLAY_BASE,
	.has_fbc = 0, /* legal, last one wins */
	.has_llc = 0, /* legal, last one wins */
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_haswell_d_info = {
	GEN7_FEATURES,
	.is_haswell = 1,
	.has_ddi = 1,
	.has_fpga_dbg = 1,
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING | VEBOX_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_haswell_m_info = {
	GEN7_FEATURES,
	.is_haswell = 1,
	.is_mobile = 1,
	.has_ddi = 1,
	.has_fpga_dbg = 1,
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING | VEBOX_RING,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_broadwell_d_info = {
	.gen = 8, .num_pipes = 3,
	.need_gfx_hws = 1, .has_hotplug = 1,
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING | VEBOX_RING,
	.has_llc = 1,
	.has_ddi = 1,
	.has_fbc = 1,
	GEN_DEFAULT_PIPEOFFSETS,
};

static const struct intel_device_info intel_broadwell_m_info = {
	.gen = 8, .is_mobile = 1, .num_pipes = 3,
	.need_gfx_hws = 1, .has_hotplug = 1,
	.ring_mask = RENDER_RING | BSD_RING | BLT_RING | VEBOX_RING,
	.has_llc = 1,
	.has_ddi = 1,
	.has_fbc = 1,
	GEN_DEFAULT_PIPEOFFSETS,
};

/*
 * Make sure any device matches here are from most specific to most
 * general.  For example, since the Quanta match is based on the subsystem
 * and subvendor IDs, we need it to come before the more general IVB
 * PCI ID matches, otherwise we'll use the wrong info struct above.
 */
#define INTEL_PCI_IDS \
	INTEL_I830_IDS(&intel_i830_info),	\
	INTEL_I845G_IDS(&intel_845g_info),	\
	INTEL_I85X_IDS(&intel_i85x_info),	\
	INTEL_I865G_IDS(&intel_i865g_info),	\
	INTEL_I915G_IDS(&intel_i915g_info),	\
	INTEL_I915GM_IDS(&intel_i915gm_info),	\
	INTEL_I945G_IDS(&intel_i945g_info),	\
	INTEL_I945GM_IDS(&intel_i945gm_info),	\
	INTEL_I965G_IDS(&intel_i965g_info),	\
	INTEL_G33_IDS(&intel_g33_info),		\
	INTEL_I965GM_IDS(&intel_i965gm_info),	\
	INTEL_GM45_IDS(&intel_gm45_info), 	\
	INTEL_G45_IDS(&intel_g45_info), 	\
	INTEL_PINEVIEW_IDS(&intel_pineview_info),	\
	INTEL_IRONLAKE_D_IDS(&intel_ironlake_d_info),	\
	INTEL_IRONLAKE_M_IDS(&intel_ironlake_m_info),	\
	INTEL_SNB_D_IDS(&intel_sandybridge_d_info),	\
	INTEL_SNB_M_IDS(&intel_sandybridge_m_info),	\
	INTEL_IVB_Q_IDS(&intel_ivybridge_q_info), /* must be first IVB */ \
	INTEL_IVB_M_IDS(&intel_ivybridge_m_info),	\
	INTEL_IVB_D_IDS(&intel_ivybridge_d_info),	\
	INTEL_HSW_D_IDS(&intel_haswell_d_info), \
	INTEL_HSW_M_IDS(&intel_haswell_m_info), \
	INTEL_VLV_M_IDS(&intel_valleyview_m_info),	\
	INTEL_VLV_D_IDS(&intel_valleyview_d_info),	\
	INTEL_BDW_M_IDS(&intel_broadwell_m_info),	\
	INTEL_BDW_D_IDS(&intel_broadwell_d_info)

static const struct pci_device_id pciidlist[] = {		/* aka */
	INTEL_PCI_IDS,
	{0, 0, 0}
};

#if defined(CONFIG_DRM_I915_KMS)
MODULE_DEVICE_TABLE(pci, pciidlist);
#endif

void intel_detect_pch(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct pci_dev *pch = NULL;

	/* In all current cases, num_pipes is equivalent to the PCH_NOP setting
	 * (which really amounts to a PCH but no South Display).
	 */
	if (INTEL_INFO(dev)->num_pipes == 0) {
		dev_priv->pch_type = PCH_NOP;
		return;
	}

	/*
	 * The reason to probe ISA bridge instead of Dev31:Fun0 is to
	 * make graphics device passthrough work easy for VMM, that only
	 * need to expose ISA bridge to let driver know the real hardware
	 * underneath. This is a requirement from virtualization team.
	 *
	 * In some virtualized environments (e.g. XEN), there is irrelevant
	 * ISA bridge in the system. To work reliably, we should scan trhough
	 * all the ISA bridge devices and check for the first match, instead
	 * of only checking the first one.
	 */
	while ((pch = pci_get_class(PCI_CLASS_BRIDGE_ISA << 8, pch))) {
		if (pch->vendor == PCI_VENDOR_ID_INTEL) {
			unsigned short id = pch->device & INTEL_PCH_DEVICE_ID_MASK;
			dev_priv->pch_id = id;

			if (id == INTEL_PCH_IBX_DEVICE_ID_TYPE) {
				dev_priv->pch_type = PCH_IBX;
				DRM_DEBUG_KMS("Found Ibex Peak PCH\n");
				WARN_ON(!IS_GEN5(dev));
			} else if (id == INTEL_PCH_CPT_DEVICE_ID_TYPE) {
				dev_priv->pch_type = PCH_CPT;
				DRM_DEBUG_KMS("Found CougarPoint PCH\n");
				WARN_ON(!(IS_GEN6(dev) || IS_IVYBRIDGE(dev)));
			} else if (id == INTEL_PCH_PPT_DEVICE_ID_TYPE) {
				/* PantherPoint is CPT compatible */
				dev_priv->pch_type = PCH_CPT;
				DRM_DEBUG_KMS("Found PantherPoint PCH\n");
				WARN_ON(!(IS_GEN6(dev) || IS_IVYBRIDGE(dev)));
			} else if (id == INTEL_PCH_LPT_DEVICE_ID_TYPE) {
				dev_priv->pch_type = PCH_LPT;
				DRM_DEBUG_KMS("Found LynxPoint PCH\n");
				WARN_ON(!IS_HASWELL(dev));
				WARN_ON(IS_ULT(dev));
			} else if (IS_BROADWELL(dev)) {
				dev_priv->pch_type = PCH_LPT;
				dev_priv->pch_id =
					INTEL_PCH_LPT_LP_DEVICE_ID_TYPE;
				DRM_DEBUG_KMS("This is Broadwell, assuming "
					      "LynxPoint LP PCH\n");
			} else if (id == INTEL_PCH_LPT_LP_DEVICE_ID_TYPE) {
				dev_priv->pch_type = PCH_LPT;
				DRM_DEBUG_KMS("Found LynxPoint LP PCH\n");
				WARN_ON(!IS_HASWELL(dev));
				WARN_ON(!IS_ULT(dev));
			} else
				continue;

			break;
		}
	}
	if (!pch)
		DRM_DEBUG_KMS("No PCH found.\n");

	pci_dev_put(pch);
}

bool i915_semaphore_is_enabled(struct drm_device *dev)
{
	if (INTEL_INFO(dev)->gen < 6)
		return false;

	if (i915.semaphores >= 0)
		return i915.semaphores;

	/* Until we get further testing... */
	if (IS_GEN8(dev))
		return false;

#ifdef CONFIG_INTEL_IOMMU
	/* Enable semaphores on SNB when IO remapping is off */
	if (INTEL_INFO(dev)->gen == 6 && intel_iommu_gfx_mapped)
		return false;
#endif

	return true;
}

static int i915_drm_freeze(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;

	intel_runtime_pm_get(dev_priv);

	/* ignore lid events during suspend */
	mutex_lock(&dev_priv->modeset_restore_lock);
	dev_priv->modeset_restore = MODESET_SUSPENDED;
	mutex_unlock(&dev_priv->modeset_restore_lock);

	/* We do a lot of poking in a lot of registers, make sure they work
	 * properly. */
	intel_display_set_init_power(dev_priv, true);

	drm_kms_helper_poll_disable(dev);

	pci_save_state(dev->pdev);

	/* If KMS is active, we do the leavevt stuff here */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		int error;

		error = i915_gem_suspend(dev);
		if (error) {
			dev_err(&dev->pdev->dev,
				"GEM idle failed, resume might fail\n");
			return error;
		}

		cancel_delayed_work_sync(&dev_priv->rps.delayed_resume_work);

		drm_irq_uninstall(dev);
		dev_priv->enable_hotplug_processing = false;
		/*
		 * Disable CRTCs directly since we want to preserve sw state
		 * for _thaw.
		 */
		mutex_lock(&dev->mode_config.mutex);
		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
			dev_priv->display.crtc_disable(crtc);
		mutex_unlock(&dev->mode_config.mutex);

		intel_modeset_suspend_hw(dev);
	}

	i915_gem_suspend_gtt_mappings(dev);

	i915_save_state(dev);

	intel_opregion_fini(dev);
	intel_uncore_fini(dev);

	console_lock();
	intel_fbdev_set_suspend(dev, FBINFO_STATE_SUSPENDED);
	console_unlock();

	dev_priv->suspend_count++;

	return 0;
}

int i915_suspend(struct drm_device *dev, pm_message_t state)
{
	int error;

	if (!dev || !dev->dev_private) {
		DRM_ERROR("dev: %p\n", dev);
		DRM_ERROR("DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	if (state.event == PM_EVENT_PRETHAW)
		return 0;


	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	error = i915_drm_freeze(dev);
	if (error)
		return error;

	if (state.event == PM_EVENT_SUSPEND) {
		/* Shut down the device */
		pci_disable_device(dev->pdev);
		pci_set_power_state(dev->pdev, PCI_D3hot);
	}

	return 0;
}

void intel_console_resume(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private,
			     console_resume_work);
	struct drm_device *dev = dev_priv->dev;

	console_lock();
	intel_fbdev_set_suspend(dev, FBINFO_STATE_RUNNING);
	console_unlock();
}

static void intel_resume_hotplug(struct drm_device *dev)
{
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct intel_encoder *encoder;

	mutex_lock(&mode_config->mutex);
	DRM_DEBUG_KMS("running encoder hotplug functions\n");

	list_for_each_entry(encoder, &mode_config->encoder_list, base.head)
		if (encoder->hot_plug)
			encoder->hot_plug(encoder);

	mutex_unlock(&mode_config->mutex);

	/* Just fire off a uevent and let userspace tell us what to do */
	drm_helper_hpd_irq_event(dev);
}

static int i915_drm_thaw_early(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	intel_uncore_early_sanitize(dev);
	intel_uncore_sanitize(dev);
	intel_power_domains_init_hw(dev_priv);

	return 0;
}

static int __i915_drm_thaw(struct drm_device *dev, bool restore_gtt_mappings)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int error = 0;

	if (drm_core_check_feature(dev, DRIVER_MODESET) &&
	    restore_gtt_mappings) {
		mutex_lock(&dev->struct_mutex);
		i915_gem_restore_gtt_mappings(dev);
		mutex_unlock(&dev->struct_mutex);
	}

	i915_restore_state(dev);
	intel_opregion_setup(dev);

	/* KMS EnterVT equivalent */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		intel_init_pch_refclk(dev);
		drm_mode_config_reset(dev);

		mutex_lock(&dev->struct_mutex);

		error = i915_gem_init_hw(dev);
		mutex_unlock(&dev->struct_mutex);

		/* We need working interrupts for modeset enabling ... */
		drm_irq_install(dev);

		intel_modeset_init_hw(dev);

		drm_modeset_lock_all(dev);
		intel_modeset_setup_hw_state(dev, true);
		drm_modeset_unlock_all(dev);

		/*
		 * ... but also need to make sure that hotplug processing
		 * doesn't cause havoc. Like in the driver load code we don't
		 * bother with the tiny race here where we might loose hotplug
		 * notifications.
		 * */
		intel_hpd_init(dev);
		dev_priv->enable_hotplug_processing = true;
		/* Config may have changed between suspend and resume */
		intel_resume_hotplug(dev);
	}

	intel_opregion_init(dev);

	/*
	 * The console lock can be pretty contented on resume due
	 * to all the printk activity.  Try to keep it out of the hot
	 * path of resume if possible.
	 */
	if (console_trylock()) {
		intel_fbdev_set_suspend(dev, FBINFO_STATE_RUNNING);
		console_unlock();
	} else {
		schedule_work(&dev_priv->console_resume_work);
	}

	mutex_lock(&dev_priv->modeset_restore_lock);
	dev_priv->modeset_restore = MODESET_DONE;
	mutex_unlock(&dev_priv->modeset_restore_lock);

	intel_runtime_pm_put(dev_priv);
	return error;
}

static int i915_drm_thaw(struct drm_device *dev)
{
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		i915_check_and_clear_faults(dev);

	return __i915_drm_thaw(dev, true);
}

static int i915_resume_early(struct drm_device *dev)
{
	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	/*
	 * We have a resume ordering issue with the snd-hda driver also
	 * requiring our device to be power up. Due to the lack of a
	 * parent/child relationship we currently solve this with an early
	 * resume hook.
	 *
	 * FIXME: This should be solved with a special hdmi sink device or
	 * similar so that power domains can be employed.
	 */
	if (pci_enable_device(dev->pdev))
		return -EIO;

	pci_set_master(dev->pdev);

	return i915_drm_thaw_early(dev);
}

int i915_resume(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	/*
	 * Platforms with opregion should have sane BIOS, older ones (gen3 and
	 * earlier) need to restore the GTT mappings since the BIOS might clear
	 * all our scratch PTEs.
	 */
	ret = __i915_drm_thaw(dev, !dev_priv->opregion.header);
	if (ret)
		return ret;

	drm_kms_helper_poll_enable(dev);
	return 0;
}

static int i915_resume_legacy(struct drm_device *dev)
{
	i915_resume_early(dev);
	i915_resume(dev);

	return 0;
}

/**
 * i915_reset - reset chip after a hang
 * @dev: drm device to reset
 *
 * Reset the chip.  Useful if a hang is detected. Returns zero on successful
 * reset or otherwise an error code.
 *
 * Procedure is fairly simple:
 *   - reset the chip using the reset reg
 *   - re-init context state
 *   - re-init hardware status page
 *   - re-init ring buffer
 *   - re-init interrupt state
 *   - re-init display
 */
int i915_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool simulated;
	int ret;

	if (!i915.reset)
		return 0;

	mutex_lock(&dev->struct_mutex);

	i915_gem_reset(dev);

	simulated = dev_priv->gpu_error.stop_rings != 0;

	ret = intel_gpu_reset(dev);

	/* Also reset the gpu hangman. */
	if (simulated) {
		DRM_INFO("Simulated gpu hang, resetting stop_rings\n");
		dev_priv->gpu_error.stop_rings = 0;
		if (ret == -ENODEV) {
			DRM_INFO("Reset not implemented, but ignoring "
				 "error for simulated gpu hangs\n");
			ret = 0;
		}
	}

	if (ret) {
		DRM_ERROR("Failed to reset chip: %i\n", ret);
		mutex_unlock(&dev->struct_mutex);
		return ret;
	}

	/* Ok, now get things going again... */

	/*
	 * Everything depends on having the GTT running, so we need to start
	 * there.  Fortunately we don't need to do this unless we reset the
	 * chip at a PCI level.
	 *
	 * Next we need to restore the context, but we don't use those
	 * yet either...
	 *
	 * Ring buffer needs to be re-initialized in the KMS case, or if X
	 * was running at the time of the reset (i.e. we weren't VT
	 * switched away).
	 */
	if (drm_core_check_feature(dev, DRIVER_MODESET) ||
			!dev_priv->ums.mm_suspended) {
		dev_priv->ums.mm_suspended = 0;

		ret = i915_gem_init_hw(dev);
		mutex_unlock(&dev->struct_mutex);
		if (ret) {
			DRM_ERROR("Failed hw init on reset %d\n", ret);
			return ret;
		}

		drm_irq_uninstall(dev);
		drm_irq_install(dev);

		/* rps/rc6 re-init is necessary to restore state lost after the
		 * reset and the re-install of drm irq. Skip for ironlake per
		 * previous concerns that it doesn't respond well to some forms
		 * of re-init after reset. */
		if (INTEL_INFO(dev)->gen > 5) {
			mutex_lock(&dev->struct_mutex);
			intel_enable_gt_powersave(dev);
			mutex_unlock(&dev->struct_mutex);
		}

		intel_hpd_init(dev);
	} else {
		mutex_unlock(&dev->struct_mutex);
	}

	return 0;
}

static int i915_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct intel_device_info *intel_info =
		(struct intel_device_info *) ent->driver_data;

	if (IS_PRELIMINARY_HW(intel_info) && !i915.preliminary_hw_support) {
		DRM_INFO("This hardware requires preliminary hardware support.\n"
			 "See CONFIG_DRM_I915_PRELIMINARY_HW_SUPPORT, and/or modparam preliminary_hw_support\n");
		return -ENODEV;
	}

	/* Only bind to function 0 of the device. Early generations
	 * used function 1 as a placeholder for multi-head. This causes
	 * us confusion instead, especially on the systems where both
	 * functions have the same PCI-ID!
	 */
	if (PCI_FUNC(pdev->devfn))
		return -ENODEV;

	driver.driver_features &= ~(DRIVER_USE_AGP);

	return drm_get_pci_dev(pdev, ent, &driver);
}

static void
i915_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static int i915_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	if (!drm_dev || !drm_dev->dev_private) {
		dev_err(dev, "DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	if (drm_dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_freeze(drm_dev);
}

static int i915_pm_suspend_late(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	/*
	 * We have a suspedn ordering issue with the snd-hda driver also
	 * requiring our device to be power up. Due to the lack of a
	 * parent/child relationship we currently solve this with an late
	 * suspend hook.
	 *
	 * FIXME: This should be solved with a special hdmi sink device or
	 * similar so that power domains can be employed.
	 */
	if (drm_dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int i915_pm_resume_early(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	return i915_resume_early(drm_dev);
}

static int i915_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	return i915_resume(drm_dev);
}

static int i915_pm_freeze(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	if (!drm_dev || !drm_dev->dev_private) {
		dev_err(dev, "DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	return i915_drm_freeze(drm_dev);
}

static int i915_pm_thaw_early(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	return i915_drm_thaw_early(drm_dev);
}

static int i915_pm_thaw(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	return i915_drm_thaw(drm_dev);
}

static int i915_pm_poweroff(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);

	return i915_drm_freeze(drm_dev);
}

static int i915_runtime_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_i915_private *dev_priv = dev->dev_private;

	WARN_ON(!HAS_RUNTIME_PM(dev));
	assert_force_wake_inactive(dev_priv);

	DRM_DEBUG_KMS("Suspending device\n");

	if (HAS_PC8(dev))
		hsw_enable_pc8(dev_priv);

	i915_gem_release_all_mmaps(dev_priv);

	del_timer_sync(&dev_priv->gpu_error.hangcheck_timer);
	dev_priv->pm.suspended = true;

	/*
	 * current versions of firmware which depend on this opregion
	 * notification have repurposed the D1 definition to mean
	 * "runtime suspended" vs. what you would normally expect (D3)
	 * to distinguish it from notifications that might be sent
	 * via the suspend path.
	 */
	intel_opregion_notify_adapter(dev, PCI_D1);

	DRM_DEBUG_KMS("Device suspended\n");
	return 0;
}

static int i915_runtime_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_i915_private *dev_priv = dev->dev_private;

	WARN_ON(!HAS_RUNTIME_PM(dev));

	DRM_DEBUG_KMS("Resuming device\n");

	intel_opregion_notify_adapter(dev, PCI_D0);
	dev_priv->pm.suspended = false;

	if (HAS_PC8(dev))
		hsw_disable_pc8(dev_priv);

	DRM_DEBUG_KMS("Device resumed\n");
	return 0;
}

static const struct dev_pm_ops i915_pm_ops = {
	.suspend = i915_pm_suspend,
	.suspend_late = i915_pm_suspend_late,
	.resume_early = i915_pm_resume_early,
	.resume = i915_pm_resume,
	.freeze = i915_pm_freeze,
	.thaw_early = i915_pm_thaw_early,
	.thaw = i915_pm_thaw,
	.poweroff = i915_pm_poweroff,
	.restore_early = i915_pm_resume_early,
	.restore = i915_pm_resume,
	.runtime_suspend = i915_runtime_suspend,
	.runtime_resume = i915_runtime_resume,
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
#ifdef CONFIG_COMPAT
	.compat_ioctl = i915_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct drm_driver driver = {
	/* Don't use MTRRs here; the Xserver or userspace app should
	 * deal with them for Intel hardware.
	 */
	.driver_features =
	    DRIVER_USE_AGP |
	    DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | DRIVER_GEM | DRIVER_PRIME |
	    DRIVER_RENDER,
	.load = i915_driver_load,
	.unload = i915_driver_unload,
	.open = i915_driver_open,
	.lastclose = i915_driver_lastclose,
	.preclose = i915_driver_preclose,
	.postclose = i915_driver_postclose,

	/* Used in place of i915_pm_ops for non-DRIVER_MODESET */
	.suspend = i915_suspend,
	.resume = i915_resume_legacy,

	.device_is_agp = i915_driver_device_is_agp,
	.master_create = i915_master_create,
	.master_destroy = i915_master_destroy,
#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = i915_debugfs_init,
	.debugfs_cleanup = i915_debugfs_cleanup,
#endif
	.gem_free_object = i915_gem_free_object,
	.gem_vm_ops = &i915_gem_vm_ops,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = i915_gem_prime_export,
	.gem_prime_import = i915_gem_prime_import,

	.dumb_create = i915_gem_dumb_create,
	.dumb_map_offset = i915_gem_mmap_gtt,
	.dumb_destroy = drm_gem_dumb_destroy,
	.ioctls = i915_ioctls,
	.fops = &i915_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static struct pci_driver i915_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = i915_pci_probe,
	.remove = i915_pci_remove,
	.driver.pm = &i915_pm_ops,
};

static int __init i915_init(void)
{
	driver.num_ioctls = i915_max_ioctl;

	/*
	 * If CONFIG_DRM_I915_KMS is set, default to KMS unless
	 * explicitly disabled with the module pararmeter.
	 *
	 * Otherwise, just follow the parameter (defaulting to off).
	 *
	 * Allow optional vga_text_mode_force boot option to override
	 * the default behavior.
	 */
#if defined(CONFIG_DRM_I915_KMS)
	if (i915.modeset != 0)
		driver.driver_features |= DRIVER_MODESET;
#endif
	if (i915.modeset == 1)
		driver.driver_features |= DRIVER_MODESET;

#ifdef CONFIG_VGA_CONSOLE
	if (vgacon_text_force() && i915.modeset == -1)
		driver.driver_features &= ~DRIVER_MODESET;
#endif

	if (!(driver.driver_features & DRIVER_MODESET)) {
		driver.get_vblank_timestamp = NULL;
#ifndef CONFIG_DRM_I915_UMS
		/* Silently fail loading to not upset userspace. */
		return 0;
#endif
	}

	return drm_pci_init(&driver, &i915_pci_driver);
}

static void __exit i915_exit(void)
{
#ifndef CONFIG_DRM_I915_UMS
	if (!(driver.driver_features & DRIVER_MODESET))
		return; /* Never loaded a driver. */
#endif

	drm_pci_exit(&driver, &i915_pci_driver);
}

module_init(i915_init);
module_exit(i915_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
