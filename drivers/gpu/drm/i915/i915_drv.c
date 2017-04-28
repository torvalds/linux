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

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/i915_drm.h>

#include "i915_drv.h"
#include "i915_trace.h"
#include "i915_vgpu.h"
#include "intel_drv.h"
#include "intel_uc.h"

static struct drm_driver driver;

static unsigned int i915_load_fail_count;

bool __i915_inject_load_failure(const char *func, int line)
{
	if (i915_load_fail_count >= i915.inject_load_failure)
		return false;

	if (++i915_load_fail_count == i915.inject_load_failure) {
		DRM_INFO("Injecting failure at checkpoint %u [%s:%d]\n",
			 i915.inject_load_failure, func, line);
		return true;
	}

	return false;
}

#define FDO_BUG_URL "https://bugs.freedesktop.org/enter_bug.cgi?product=DRI"
#define FDO_BUG_MSG "Please file a bug at " FDO_BUG_URL " against DRM/Intel " \
		    "providing the dmesg log by booting with drm.debug=0xf"

void
__i915_printk(struct drm_i915_private *dev_priv, const char *level,
	      const char *fmt, ...)
{
	static bool shown_bug_once;
	struct device *kdev = dev_priv->drm.dev;
	bool is_error = level[1] <= KERN_ERR[1];
	bool is_debug = level[1] == KERN_DEBUG[1];
	struct va_format vaf;
	va_list args;

	if (is_debug && !(drm_debug & DRM_UT_DRIVER))
		return;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	dev_printk(level, kdev, "[" DRM_NAME ":%ps] %pV",
		   __builtin_return_address(0), &vaf);

	if (is_error && !shown_bug_once) {
		dev_notice(kdev, "%s", FDO_BUG_MSG);
		shown_bug_once = true;
	}

	va_end(args);
}

static bool i915_error_injected(struct drm_i915_private *dev_priv)
{
	return i915.inject_load_failure &&
	       i915_load_fail_count == i915.inject_load_failure;
}

#define i915_load_error(dev_priv, fmt, ...)				     \
	__i915_printk(dev_priv,						     \
		      i915_error_injected(dev_priv) ? KERN_DEBUG : KERN_ERR, \
		      fmt, ##__VA_ARGS__)


static enum intel_pch intel_virt_detect_pch(struct drm_i915_private *dev_priv)
{
	enum intel_pch ret = PCH_NOP;

	/*
	 * In a virtualized passthrough environment we can be in a
	 * setup where the ISA bridge is not able to be passed through.
	 * In this case, a south bridge can be emulated and we have to
	 * make an educated guess as to which PCH is really there.
	 */

	if (IS_GEN5(dev_priv)) {
		ret = PCH_IBX;
		DRM_DEBUG_KMS("Assuming Ibex Peak PCH\n");
	} else if (IS_GEN6(dev_priv) || IS_IVYBRIDGE(dev_priv)) {
		ret = PCH_CPT;
		DRM_DEBUG_KMS("Assuming CouarPoint PCH\n");
	} else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		ret = PCH_LPT;
		DRM_DEBUG_KMS("Assuming LynxPoint PCH\n");
	} else if (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv)) {
		ret = PCH_SPT;
		DRM_DEBUG_KMS("Assuming SunrisePoint PCH\n");
	}

	return ret;
}

static void intel_detect_pch(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pch = NULL;

	/* In all current cases, num_pipes is equivalent to the PCH_NOP setting
	 * (which really amounts to a PCH but no South Display).
	 */
	if (INTEL_INFO(dev_priv)->num_pipes == 0) {
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
				WARN_ON(!IS_GEN5(dev_priv));
			} else if (id == INTEL_PCH_CPT_DEVICE_ID_TYPE) {
				dev_priv->pch_type = PCH_CPT;
				DRM_DEBUG_KMS("Found CougarPoint PCH\n");
				WARN_ON(!(IS_GEN6(dev_priv) ||
					IS_IVYBRIDGE(dev_priv)));
			} else if (id == INTEL_PCH_PPT_DEVICE_ID_TYPE) {
				/* PantherPoint is CPT compatible */
				dev_priv->pch_type = PCH_CPT;
				DRM_DEBUG_KMS("Found PantherPoint PCH\n");
				WARN_ON(!(IS_GEN6(dev_priv) ||
					IS_IVYBRIDGE(dev_priv)));
			} else if (id == INTEL_PCH_LPT_DEVICE_ID_TYPE) {
				dev_priv->pch_type = PCH_LPT;
				DRM_DEBUG_KMS("Found LynxPoint PCH\n");
				WARN_ON(!IS_HASWELL(dev_priv) &&
					!IS_BROADWELL(dev_priv));
				WARN_ON(IS_HSW_ULT(dev_priv) ||
					IS_BDW_ULT(dev_priv));
			} else if (id == INTEL_PCH_LPT_LP_DEVICE_ID_TYPE) {
				dev_priv->pch_type = PCH_LPT;
				DRM_DEBUG_KMS("Found LynxPoint LP PCH\n");
				WARN_ON(!IS_HASWELL(dev_priv) &&
					!IS_BROADWELL(dev_priv));
				WARN_ON(!IS_HSW_ULT(dev_priv) &&
					!IS_BDW_ULT(dev_priv));
			} else if (id == INTEL_PCH_SPT_DEVICE_ID_TYPE) {
				dev_priv->pch_type = PCH_SPT;
				DRM_DEBUG_KMS("Found SunrisePoint PCH\n");
				WARN_ON(!IS_SKYLAKE(dev_priv) &&
					!IS_KABYLAKE(dev_priv));
			} else if (id == INTEL_PCH_SPT_LP_DEVICE_ID_TYPE) {
				dev_priv->pch_type = PCH_SPT;
				DRM_DEBUG_KMS("Found SunrisePoint LP PCH\n");
				WARN_ON(!IS_SKYLAKE(dev_priv) &&
					!IS_KABYLAKE(dev_priv));
			} else if (id == INTEL_PCH_KBP_DEVICE_ID_TYPE) {
				dev_priv->pch_type = PCH_KBP;
				DRM_DEBUG_KMS("Found KabyPoint PCH\n");
				WARN_ON(!IS_SKYLAKE(dev_priv) &&
					!IS_KABYLAKE(dev_priv));
			} else if ((id == INTEL_PCH_P2X_DEVICE_ID_TYPE) ||
				   (id == INTEL_PCH_P3X_DEVICE_ID_TYPE) ||
				   ((id == INTEL_PCH_QEMU_DEVICE_ID_TYPE) &&
				    pch->subsystem_vendor ==
					    PCI_SUBVENDOR_ID_REDHAT_QUMRANET &&
				    pch->subsystem_device ==
					    PCI_SUBDEVICE_ID_QEMU)) {
				dev_priv->pch_type =
					intel_virt_detect_pch(dev_priv);
			} else
				continue;

			break;
		}
	}
	if (!pch)
		DRM_DEBUG_KMS("No PCH found.\n");

	pci_dev_put(pch);
}

static int i915_getparam(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = dev_priv->drm.pdev;
	drm_i915_getparam_t *param = data;
	int value;

	switch (param->param) {
	case I915_PARAM_IRQ_ACTIVE:
	case I915_PARAM_ALLOW_BATCHBUFFER:
	case I915_PARAM_LAST_DISPATCH:
	case I915_PARAM_HAS_EXEC_CONSTANTS:
		/* Reject all old ums/dri params. */
		return -ENODEV;
	case I915_PARAM_CHIPSET_ID:
		value = pdev->device;
		break;
	case I915_PARAM_REVISION:
		value = pdev->revision;
		break;
	case I915_PARAM_NUM_FENCES_AVAIL:
		value = dev_priv->num_fence_regs;
		break;
	case I915_PARAM_HAS_OVERLAY:
		value = dev_priv->overlay ? 1 : 0;
		break;
	case I915_PARAM_HAS_BSD:
		value = !!dev_priv->engine[VCS];
		break;
	case I915_PARAM_HAS_BLT:
		value = !!dev_priv->engine[BCS];
		break;
	case I915_PARAM_HAS_VEBOX:
		value = !!dev_priv->engine[VECS];
		break;
	case I915_PARAM_HAS_BSD2:
		value = !!dev_priv->engine[VCS2];
		break;
	case I915_PARAM_HAS_LLC:
		value = HAS_LLC(dev_priv);
		break;
	case I915_PARAM_HAS_WT:
		value = HAS_WT(dev_priv);
		break;
	case I915_PARAM_HAS_ALIASING_PPGTT:
		value = USES_PPGTT(dev_priv);
		break;
	case I915_PARAM_HAS_SEMAPHORES:
		value = i915.semaphores;
		break;
	case I915_PARAM_HAS_SECURE_BATCHES:
		value = capable(CAP_SYS_ADMIN);
		break;
	case I915_PARAM_CMD_PARSER_VERSION:
		value = i915_cmd_parser_get_version(dev_priv);
		break;
	case I915_PARAM_SUBSLICE_TOTAL:
		value = sseu_subslice_total(&INTEL_INFO(dev_priv)->sseu);
		if (!value)
			return -ENODEV;
		break;
	case I915_PARAM_EU_TOTAL:
		value = INTEL_INFO(dev_priv)->sseu.eu_total;
		if (!value)
			return -ENODEV;
		break;
	case I915_PARAM_HAS_GPU_RESET:
		value = i915.enable_hangcheck && intel_has_gpu_reset(dev_priv);
		break;
	case I915_PARAM_HAS_RESOURCE_STREAMER:
		value = HAS_RESOURCE_STREAMER(dev_priv);
		break;
	case I915_PARAM_HAS_POOLED_EU:
		value = HAS_POOLED_EU(dev_priv);
		break;
	case I915_PARAM_MIN_EU_IN_POOL:
		value = INTEL_INFO(dev_priv)->sseu.min_eu_in_pool;
		break;
	case I915_PARAM_HUC_STATUS:
		intel_runtime_pm_get(dev_priv);
		value = I915_READ(HUC_STATUS2) & HUC_FW_VERIFIED;
		intel_runtime_pm_put(dev_priv);
		break;
	case I915_PARAM_MMAP_GTT_VERSION:
		/* Though we've started our numbering from 1, and so class all
		 * earlier versions as 0, in effect their value is undefined as
		 * the ioctl will report EINVAL for the unknown param!
		 */
		value = i915_gem_mmap_gtt_version();
		break;
	case I915_PARAM_HAS_SCHEDULER:
		value = dev_priv->engine[RCS] &&
			dev_priv->engine[RCS]->schedule;
		break;
	case I915_PARAM_MMAP_VERSION:
		/* Remember to bump this if the version changes! */
	case I915_PARAM_HAS_GEM:
	case I915_PARAM_HAS_PAGEFLIPPING:
	case I915_PARAM_HAS_EXECBUF2: /* depends on GEM */
	case I915_PARAM_HAS_RELAXED_FENCING:
	case I915_PARAM_HAS_COHERENT_RINGS:
	case I915_PARAM_HAS_RELAXED_DELTA:
	case I915_PARAM_HAS_GEN7_SOL_RESET:
	case I915_PARAM_HAS_WAIT_TIMEOUT:
	case I915_PARAM_HAS_PRIME_VMAP_FLUSH:
	case I915_PARAM_HAS_PINNED_BATCHES:
	case I915_PARAM_HAS_EXEC_NO_RELOC:
	case I915_PARAM_HAS_EXEC_HANDLE_LUT:
	case I915_PARAM_HAS_COHERENT_PHYS_GTT:
	case I915_PARAM_HAS_EXEC_SOFTPIN:
	case I915_PARAM_HAS_EXEC_ASYNC:
	case I915_PARAM_HAS_EXEC_FENCE:
	case I915_PARAM_HAS_EXEC_CAPTURE:
		/* For the time being all of these are always true;
		 * if some supported hardware does not have one of these
		 * features this value needs to be provided from
		 * INTEL_INFO(), a feature macro, or similar.
		 */
		value = 1;
		break;
	default:
		DRM_DEBUG("Unknown parameter %d\n", param->param);
		return -EINVAL;
	}

	if (put_user(value, param->value))
		return -EFAULT;

	return 0;
}

static int i915_get_bridge_dev(struct drm_i915_private *dev_priv)
{
	dev_priv->bridge_dev = pci_get_bus_and_slot(0, PCI_DEVFN(0, 0));
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

static int i915_resume_switcheroo(struct drm_device *dev);
static int i915_suspend_switcheroo(struct drm_device *dev, pm_message_t state);

static void i915_switcheroo_set_state(struct pci_dev *pdev, enum vga_switcheroo_state state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	pm_message_t pmm = { .event = PM_EVENT_SUSPEND };

	if (state == VGA_SWITCHEROO_ON) {
		pr_info("switched on\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		/* i915 resume handler doesn't set to D0 */
		pci_set_power_state(pdev, PCI_D0);
		i915_resume_switcheroo(dev);
		dev->switch_power_state = DRM_SWITCH_POWER_ON;
	} else {
		pr_info("switched off\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		i915_suspend_switcheroo(dev, pmm);
		dev->switch_power_state = DRM_SWITCH_POWER_OFF;
	}
}

static bool i915_switcheroo_can_switch(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	/*
	 * FIXME: open_count is protected by drm_global_mutex but that would lead to
	 * locking inversion with the driver load path. And the access here is
	 * completely racy anyway. So don't bother with locking for now.
	 */
	return dev->open_count == 0;
}

static const struct vga_switcheroo_client_ops i915_switcheroo_ops = {
	.set_gpu_state = i915_switcheroo_set_state,
	.reprobe = NULL,
	.can_switch = i915_switcheroo_can_switch,
};

static void i915_gem_fini(struct drm_i915_private *dev_priv)
{
	mutex_lock(&dev_priv->drm.struct_mutex);
	intel_uc_fini_hw(dev_priv);
	i915_gem_cleanup_engines(dev_priv);
	i915_gem_context_fini(dev_priv);
	mutex_unlock(&dev_priv->drm.struct_mutex);

	i915_gem_drain_freed_objects(dev_priv);

	WARN_ON(!list_empty(&dev_priv->context_list));
}

static int i915_load_modeset_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = dev_priv->drm.pdev;
	int ret;

	if (i915_inject_load_failure())
		return -ENODEV;

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

	/* must happen before intel_power_domains_init_hw() on VLV/CHV */
	intel_update_rawclk(dev_priv);

	intel_power_domains_init_hw(dev_priv, false);

	intel_csr_ucode_init(dev_priv);

	ret = intel_irq_install(dev_priv);
	if (ret)
		goto cleanup_csr;

	intel_setup_gmbus(dev_priv);

	/* Important: The output setup functions called by modeset_init need
	 * working irqs for e.g. gmbus and dp aux transfers. */
	ret = intel_modeset_init(dev);
	if (ret)
		goto cleanup_irq;

	intel_uc_init_fw(dev_priv);

	ret = i915_gem_init(dev_priv);
	if (ret)
		goto cleanup_uc;

	intel_modeset_gem_init(dev);

	if (INTEL_INFO(dev_priv)->num_pipes == 0)
		return 0;

	ret = intel_fbdev_init(dev);
	if (ret)
		goto cleanup_gem;

	/* Only enable hotplug handling once the fbdev is fully set up. */
	intel_hpd_init(dev_priv);

	drm_kms_helper_poll_init(dev);

	return 0;

cleanup_gem:
	if (i915_gem_suspend(dev_priv))
		DRM_ERROR("failed to idle hardware; continuing to unload!\n");
	i915_gem_fini(dev_priv);
cleanup_uc:
	intel_uc_fini_fw(dev_priv);
cleanup_irq:
	drm_irq_uninstall(dev);
	intel_teardown_gmbus(dev_priv);
cleanup_csr:
	intel_csr_ucode_fini(dev_priv);
	intel_power_domains_fini(dev_priv);
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

	ap->ranges[0].base = ggtt->mappable_base;
	ap->ranges[0].size = ggtt->mappable_end;

	primary =
		pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW;

	ret = drm_fb_helper_remove_conflicting_framebuffers(ap, "inteldrmfb", primary);

	kfree(ap);

	return ret;
}

#if !defined(CONFIG_VGA_CONSOLE)
static int i915_kick_out_vgacon(struct drm_i915_private *dev_priv)
{
	return 0;
}
#elif !defined(CONFIG_DUMMY_CONSOLE)
static int i915_kick_out_vgacon(struct drm_i915_private *dev_priv)
{
	return -ENODEV;
}
#else
static int i915_kick_out_vgacon(struct drm_i915_private *dev_priv)
{
	int ret = 0;

	DRM_INFO("Replacing VGA console driver\n");

	console_lock();
	if (con_is_bound(&vga_con))
		ret = do_take_over_console(&dummy_con, 0, MAX_NR_CONSOLES - 1, 1);
	if (ret == 0) {
		ret = do_unregister_con_driver(&vga_con);

		/* Ignore "already unregistered". */
		if (ret == -ENODEV)
			ret = 0;
	}
	console_unlock();

	return ret;
}
#endif

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
	 * by the GPU. i915_gem_retire_requests() is called directly when we
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

static void i915_engines_cleanup(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, i915, id)
		kfree(engine);
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
 */
static void intel_detect_preproduction_hw(struct drm_i915_private *dev_priv)
{
	bool pre = false;

	pre |= IS_HSW_EARLY_SDV(dev_priv);
	pre |= IS_SKL_REVID(dev_priv, 0, SKL_REVID_F0);
	pre |= IS_BXT_REVID(dev_priv, 0, BXT_REVID_B_LAST);

	if (pre) {
		DRM_ERROR("This is a pre-production stepping. "
			  "It may not be fully functional.\n");
		add_taint(TAINT_MACHINE_CHECK, LOCKDEP_STILL_OK);
	}
}

/**
 * i915_driver_init_early - setup state not requiring device access
 * @dev_priv: device private
 *
 * Initialize everything that is a "SW-only" state, that is state not
 * requiring accessing the device or exposing the driver via kernel internal
 * or userspace interfaces. Example steps belonging here: lock initialization,
 * system memory allocation, setting up device specific attributes and
 * function hooks not requiring accessing the device.
 */
static int i915_driver_init_early(struct drm_i915_private *dev_priv,
				  const struct pci_device_id *ent)
{
	const struct intel_device_info *match_info =
		(struct intel_device_info *)ent->driver_data;
	struct intel_device_info *device_info;
	int ret = 0;

	if (i915_inject_load_failure())
		return -ENODEV;

	/* Setup the write-once "constant" device info */
	device_info = mkwrite_device_info(dev_priv);
	memcpy(device_info, match_info, sizeof(*device_info));
	device_info->device_id = dev_priv->drm.pdev->device;

	BUG_ON(device_info->gen > sizeof(device_info->gen_mask) * BITS_PER_BYTE);
	device_info->gen_mask = BIT(device_info->gen - 1);

	spin_lock_init(&dev_priv->irq_lock);
	spin_lock_init(&dev_priv->gpu_error.lock);
	mutex_init(&dev_priv->backlight_lock);
	spin_lock_init(&dev_priv->uncore.lock);

	spin_lock_init(&dev_priv->mm.object_stat_lock);
	spin_lock_init(&dev_priv->mmio_flip_lock);
	mutex_init(&dev_priv->sb_lock);
	mutex_init(&dev_priv->modeset_restore_lock);
	mutex_init(&dev_priv->av_mutex);
	mutex_init(&dev_priv->wm.wm_mutex);
	mutex_init(&dev_priv->pps_mutex);

	intel_uc_init_early(dev_priv);
	i915_memcpy_init_early(dev_priv);

	ret = i915_workqueues_init(dev_priv);
	if (ret < 0)
		goto err_engines;

	/* This must be called before any calls to HAS_PCH_* */
	intel_detect_pch(dev_priv);

	intel_pm_setup(dev_priv);
	intel_init_dpio(dev_priv);
	intel_power_domains_init(dev_priv);
	intel_irq_init(dev_priv);
	intel_hangcheck_init(dev_priv);
	intel_init_display_hooks(dev_priv);
	intel_init_clock_gating_hooks(dev_priv);
	intel_init_audio_hooks(dev_priv);
	ret = i915_gem_load_init(dev_priv);
	if (ret < 0)
		goto err_irq;

	intel_display_crc_init(dev_priv);

	intel_device_info_dump(dev_priv);

	intel_detect_preproduction_hw(dev_priv);

	i915_perf_init(dev_priv);

	return 0;

err_irq:
	intel_irq_fini(dev_priv);
	i915_workqueues_cleanup(dev_priv);
err_engines:
	i915_engines_cleanup(dev_priv);
	return ret;
}

/**
 * i915_driver_cleanup_early - cleanup the setup done in i915_driver_init_early()
 * @dev_priv: device private
 */
static void i915_driver_cleanup_early(struct drm_i915_private *dev_priv)
{
	i915_perf_fini(dev_priv);
	i915_gem_load_cleanup(dev_priv);
	intel_irq_fini(dev_priv);
	i915_workqueues_cleanup(dev_priv);
	i915_engines_cleanup(dev_priv);
}

static int i915_mmio_setup(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	int mmio_bar;
	int mmio_size;

	mmio_bar = IS_GEN2(dev_priv) ? 1 : 0;
	/*
	 * Before gen4, the registers and the GTT are behind different BARs.
	 * However, from gen4 onwards, the registers and the GTT are shared
	 * in the same BAR, so we want to restrict this ioremap from
	 * clobbering the GTT which we want ioremap_wc instead. Fortunately,
	 * the register BAR remains the same size for all the earlier
	 * generations up to Ironlake.
	 */
	if (INTEL_GEN(dev_priv) < 5)
		mmio_size = 512 * 1024;
	else
		mmio_size = 2 * 1024 * 1024;
	dev_priv->regs = pci_iomap(pdev, mmio_bar, mmio_size);
	if (dev_priv->regs == NULL) {
		DRM_ERROR("failed to map registers\n");

		return -EIO;
	}

	/* Try to make sure MCHBAR is enabled before poking at it */
	intel_setup_mchbar(dev_priv);

	return 0;
}

static void i915_mmio_cleanup(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;

	intel_teardown_mchbar(dev_priv);
	pci_iounmap(pdev, dev_priv->regs);
}

/**
 * i915_driver_init_mmio - setup device MMIO
 * @dev_priv: device private
 *
 * Setup minimal device state necessary for MMIO accesses later in the
 * initialization sequence. The setup here should avoid any other device-wide
 * side effects or exposing the driver via kernel internal or user space
 * interfaces.
 */
static int i915_driver_init_mmio(struct drm_i915_private *dev_priv)
{
	int ret;

	if (i915_inject_load_failure())
		return -ENODEV;

	if (i915_get_bridge_dev(dev_priv))
		return -EIO;

	ret = i915_mmio_setup(dev_priv);
	if (ret < 0)
		goto err_bridge;

	intel_uncore_init(dev_priv);

	ret = intel_engines_init_mmio(dev_priv);
	if (ret)
		goto err_uncore;

	i915_gem_init_mmio(dev_priv);

	return 0;

err_uncore:
	intel_uncore_fini(dev_priv);
err_bridge:
	pci_dev_put(dev_priv->bridge_dev);

	return ret;
}

/**
 * i915_driver_cleanup_mmio - cleanup the setup done in i915_driver_init_mmio()
 * @dev_priv: device private
 */
static void i915_driver_cleanup_mmio(struct drm_i915_private *dev_priv)
{
	intel_uncore_fini(dev_priv);
	i915_mmio_cleanup(dev_priv);
	pci_dev_put(dev_priv->bridge_dev);
}

static void intel_sanitize_options(struct drm_i915_private *dev_priv)
{
	i915.enable_execlists =
		intel_sanitize_enable_execlists(dev_priv,
						i915.enable_execlists);

	/*
	 * i915.enable_ppgtt is read-only, so do an early pass to validate the
	 * user's requested state against the hardware/driver capabilities.  We
	 * do this now so that we can print out any log messages once rather
	 * than every time we check intel_enable_ppgtt().
	 */
	i915.enable_ppgtt =
		intel_sanitize_enable_ppgtt(dev_priv, i915.enable_ppgtt);
	DRM_DEBUG_DRIVER("ppgtt mode: %i\n", i915.enable_ppgtt);

	i915.semaphores = intel_sanitize_semaphores(dev_priv, i915.semaphores);
	DRM_DEBUG_DRIVER("use GPU semaphores? %s\n", yesno(i915.semaphores));

	intel_uc_sanitize_options(dev_priv);
}

/**
 * i915_driver_init_hw - setup state requiring device access
 * @dev_priv: device private
 *
 * Setup state that requires accessing the device, but doesn't require
 * exposing the driver via kernel internal or userspace interfaces.
 */
static int i915_driver_init_hw(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	int ret;

	if (i915_inject_load_failure())
		return -ENODEV;

	intel_device_info_runtime_init(dev_priv);

	intel_sanitize_options(dev_priv);

	ret = i915_ggtt_probe_hw(dev_priv);
	if (ret)
		return ret;

	/* WARNING: Apparently we must kick fbdev drivers before vgacon,
	 * otherwise the vga fbdev driver falls over. */
	ret = i915_kick_out_firmware_fb(dev_priv);
	if (ret) {
		DRM_ERROR("failed to remove conflicting framebuffer drivers\n");
		goto out_ggtt;
	}

	ret = i915_kick_out_vgacon(dev_priv);
	if (ret) {
		DRM_ERROR("failed to remove conflicting VGA console\n");
		goto out_ggtt;
	}

	ret = i915_ggtt_init_hw(dev_priv);
	if (ret)
		return ret;

	ret = i915_ggtt_enable_hw(dev_priv);
	if (ret) {
		DRM_ERROR("failed to enable GGTT\n");
		goto out_ggtt;
	}

	pci_set_master(pdev);

	/* overlay on gen2 is broken and can't address above 1G */
	if (IS_GEN2(dev_priv)) {
		ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(30));
		if (ret) {
			DRM_ERROR("failed to set DMA mask\n");

			goto out_ggtt;
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

			goto out_ggtt;
		}
	}

	pm_qos_add_request(&dev_priv->pm_qos, PM_QOS_CPU_DMA_LATENCY,
			   PM_QOS_DEFAULT_VALUE);

	intel_uncore_sanitize(dev_priv);

	intel_opregion_setup(dev_priv);

	i915_gem_load_init_fences(dev_priv);

	/* On the 945G/GM, the chipset reports the MSI capability on the
	 * integrated graphics even though the support isn't actually there
	 * according to the published specs.  It doesn't appear to function
	 * correctly in testing on 945G.
	 * This may be a side effect of MSI having been made available for PEG
	 * and the registers being closely associated.
	 *
	 * According to chipset errata, on the 965GM, MSI interrupts may
	 * be lost or delayed, but we use them anyways to avoid
	 * stuck interrupts on some machines.
	 */
	if (!IS_I945G(dev_priv) && !IS_I945GM(dev_priv)) {
		if (pci_enable_msi(pdev) < 0)
			DRM_DEBUG_DRIVER("can't enable MSI");
	}

	ret = intel_gvt_init(dev_priv);
	if (ret)
		goto out_ggtt;

	return 0;

out_ggtt:
	i915_ggtt_cleanup_hw(dev_priv);

	return ret;
}

/**
 * i915_driver_cleanup_hw - cleanup the setup done in i915_driver_init_hw()
 * @dev_priv: device private
 */
static void i915_driver_cleanup_hw(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;

	if (pdev->msi_enabled)
		pci_disable_msi(pdev);

	pm_qos_remove_request(&dev_priv->pm_qos);
	i915_ggtt_cleanup_hw(dev_priv);
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

	i915_gem_shrinker_init(dev_priv);

	/*
	 * Notify a valid surface after modesetting,
	 * when running inside a VM.
	 */
	if (intel_vgpu_active(dev_priv))
		I915_WRITE(vgtif_reg(display_ready), VGT_DRV_DISPLAY_READY);

	/* Reveal our presence to userspace */
	if (drm_dev_register(dev, 0) == 0) {
		i915_debugfs_register(dev_priv);
		i915_guc_log_register(dev_priv);
		i915_setup_sysfs(dev_priv);

		/* Depends on sysfs having been initialized */
		i915_perf_register(dev_priv);
	} else
		DRM_ERROR("Failed to register driver for userspace access!\n");

	if (INTEL_INFO(dev_priv)->num_pipes) {
		/* Must be done after probing outputs */
		intel_opregion_register(dev_priv);
		acpi_video_register();
	}

	if (IS_GEN5(dev_priv))
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
}

/**
 * i915_driver_unregister - cleanup the registration done in i915_driver_regiser()
 * @dev_priv: device private
 */
static void i915_driver_unregister(struct drm_i915_private *dev_priv)
{
	intel_audio_deinit(dev_priv);

	intel_gpu_ips_teardown();
	acpi_video_unregister();
	intel_opregion_unregister(dev_priv);

	i915_perf_unregister(dev_priv);

	i915_teardown_sysfs(dev_priv);
	i915_guc_log_unregister(dev_priv);
	drm_dev_unregister(&dev_priv->drm);

	i915_gem_shrinker_cleanup(dev_priv);
}

/**
 * i915_driver_load - setup chip and create an initial config
 * @pdev: PCI device
 * @ent: matching PCI ID entry
 *
 * The driver load routine has to do several things:
 *   - drive output discovery via intel_modeset_init()
 *   - initialize the memory manager
 *   - allocate initial config memory
 *   - setup the DRM framebuffer with the allocated memory
 */
int i915_driver_load(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct intel_device_info *match_info =
		(struct intel_device_info *)ent->driver_data;
	struct drm_i915_private *dev_priv;
	int ret;

	/* Enable nuclear pageflip on ILK+ */
	if (!i915.nuclear_pageflip && match_info->gen < 5)
		driver.driver_features &= ~DRIVER_ATOMIC;

	ret = -ENOMEM;
	dev_priv = kzalloc(sizeof(*dev_priv), GFP_KERNEL);
	if (dev_priv)
		ret = drm_dev_init(&dev_priv->drm, &driver, &pdev->dev);
	if (ret) {
		DRM_DEV_ERROR(&pdev->dev, "allocation failed\n");
		goto out_free;
	}

	dev_priv->drm.pdev = pdev;
	dev_priv->drm.dev_private = dev_priv;

	ret = pci_enable_device(pdev);
	if (ret)
		goto out_fini;

	pci_set_drvdata(pdev, &dev_priv->drm);

	ret = i915_driver_init_early(dev_priv, ent);
	if (ret < 0)
		goto out_pci_disable;

	intel_runtime_pm_get(dev_priv);

	ret = i915_driver_init_mmio(dev_priv);
	if (ret < 0)
		goto out_runtime_pm_put;

	ret = i915_driver_init_hw(dev_priv);
	if (ret < 0)
		goto out_cleanup_mmio;

	/*
	 * TODO: move the vblank init and parts of modeset init steps into one
	 * of the i915_driver_init_/i915_driver_register functions according
	 * to the role/effect of the given init step.
	 */
	if (INTEL_INFO(dev_priv)->num_pipes) {
		ret = drm_vblank_init(&dev_priv->drm,
				      INTEL_INFO(dev_priv)->num_pipes);
		if (ret)
			goto out_cleanup_hw;
	}

	ret = i915_load_modeset_init(&dev_priv->drm);
	if (ret < 0)
		goto out_cleanup_vblank;

	i915_driver_register(dev_priv);

	intel_runtime_pm_enable(dev_priv);

	dev_priv->ipc_enabled = false;

	/* Everything is in place, we can now relax! */
	DRM_INFO("Initialized %s %d.%d.%d %s for %s on minor %d\n",
		 driver.name, driver.major, driver.minor, driver.patchlevel,
		 driver.date, pci_name(pdev), dev_priv->drm.primary->index);
	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG))
		DRM_INFO("DRM_I915_DEBUG enabled\n");
	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		DRM_INFO("DRM_I915_DEBUG_GEM enabled\n");

	intel_runtime_pm_put(dev_priv);

	return 0;

out_cleanup_vblank:
	drm_vblank_cleanup(&dev_priv->drm);
out_cleanup_hw:
	i915_driver_cleanup_hw(dev_priv);
out_cleanup_mmio:
	i915_driver_cleanup_mmio(dev_priv);
out_runtime_pm_put:
	intel_runtime_pm_put(dev_priv);
	i915_driver_cleanup_early(dev_priv);
out_pci_disable:
	pci_disable_device(pdev);
out_fini:
	i915_load_error(dev_priv, "Device initialization failed (%d)\n", ret);
	drm_dev_fini(&dev_priv->drm);
out_free:
	kfree(dev_priv);
	return ret;
}

void i915_driver_unload(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = dev_priv->drm.pdev;

	intel_fbdev_fini(dev);

	if (i915_gem_suspend(dev_priv))
		DRM_ERROR("failed to idle hardware; continuing to unload!\n");

	intel_display_power_get(dev_priv, POWER_DOMAIN_INIT);

	drm_atomic_helper_shutdown(dev);

	intel_gvt_cleanup(dev_priv);

	i915_driver_unregister(dev_priv);

	drm_vblank_cleanup(dev);

	intel_modeset_cleanup(dev);

	/*
	 * free the memory space allocated for the child device
	 * config parsed from VBT
	 */
	if (dev_priv->vbt.child_dev && dev_priv->vbt.child_dev_num) {
		kfree(dev_priv->vbt.child_dev);
		dev_priv->vbt.child_dev = NULL;
		dev_priv->vbt.child_dev_num = 0;
	}
	kfree(dev_priv->vbt.sdvo_lvds_vbt_mode);
	dev_priv->vbt.sdvo_lvds_vbt_mode = NULL;
	kfree(dev_priv->vbt.lfp_lvds_vbt_mode);
	dev_priv->vbt.lfp_lvds_vbt_mode = NULL;

	vga_switcheroo_unregister_client(pdev);
	vga_client_register(pdev, NULL, NULL, NULL);

	intel_csr_ucode_fini(dev_priv);

	/* Free error state after interrupts are fully disabled. */
	cancel_delayed_work_sync(&dev_priv->gpu_error.hangcheck_work);
	i915_reset_error_state(dev_priv);

	/* Flush any outstanding unpin_work. */
	drain_workqueue(dev_priv->wq);

	i915_gem_fini(dev_priv);
	intel_uc_fini_fw(dev_priv);
	intel_fbc_cleanup_cfb(dev_priv);

	intel_power_domains_fini(dev_priv);

	i915_driver_cleanup_hw(dev_priv);
	i915_driver_cleanup_mmio(dev_priv);

	intel_display_power_put(dev_priv, POWER_DOMAIN_INIT);
}

static void i915_driver_release(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	i915_driver_cleanup_early(dev_priv);
	drm_dev_fini(&dev_priv->drm);

	kfree(dev_priv);
}

static int i915_driver_open(struct drm_device *dev, struct drm_file *file)
{
	int ret;

	ret = i915_gem_open(dev, file);
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
	i915_gem_context_close(dev, file);
	i915_gem_release(dev, file);
	mutex_unlock(&dev->struct_mutex);

	kfree(file_priv);
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

static int i915_drm_suspend(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = dev_priv->drm.pdev;
	pci_power_t opregion_target_state;
	int error;

	/* ignore lid events during suspend */
	mutex_lock(&dev_priv->modeset_restore_lock);
	dev_priv->modeset_restore = MODESET_SUSPENDED;
	mutex_unlock(&dev_priv->modeset_restore_lock);

	disable_rpm_wakeref_asserts(dev_priv);

	/* We do a lot of poking in a lot of registers, make sure they work
	 * properly. */
	intel_display_set_init_power(dev_priv, true);

	drm_kms_helper_poll_disable(dev);

	pci_save_state(pdev);

	error = i915_gem_suspend(dev_priv);
	if (error) {
		dev_err(&pdev->dev,
			"GEM idle failed, resume might fail\n");
		goto out;
	}

	intel_display_suspend(dev);

	intel_dp_mst_suspend(dev);

	intel_runtime_pm_disable_interrupts(dev_priv);
	intel_hpd_cancel_work(dev_priv);

	intel_suspend_encoders(dev_priv);

	intel_suspend_hw(dev_priv);

	i915_gem_suspend_gtt_mappings(dev_priv);

	i915_save_state(dev_priv);

	opregion_target_state = suspend_to_idle(dev_priv) ? PCI_D1 : PCI_D3cold;
	intel_opregion_notify_adapter(dev_priv, opregion_target_state);

	intel_uncore_suspend(dev_priv);
	intel_opregion_unregister(dev_priv);

	intel_fbdev_set_suspend(dev, FBINFO_STATE_SUSPENDED, true);

	dev_priv->suspend_count++;

	intel_csr_ucode_suspend(dev_priv);

out:
	enable_rpm_wakeref_asserts(dev_priv);

	return error;
}

static int i915_drm_suspend_late(struct drm_device *dev, bool hibernation)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct pci_dev *pdev = dev_priv->drm.pdev;
	bool fw_csr;
	int ret;

	disable_rpm_wakeref_asserts(dev_priv);

	intel_display_set_init_power(dev_priv, false);

	fw_csr = !IS_GEN9_LP(dev_priv) &&
		suspend_to_idle(dev_priv) && dev_priv->csr.dmc_payload;
	/*
	 * In case of firmware assisted context save/restore don't manually
	 * deinit the power domains. This also means the CSR/DMC firmware will
	 * stay active, it will power down any HW resources as required and
	 * also enable deeper system power states that would be blocked if the
	 * firmware was inactive.
	 */
	if (!fw_csr)
		intel_power_domains_suspend(dev_priv);

	ret = 0;
	if (IS_GEN9_LP(dev_priv))
		bxt_enable_dc9(dev_priv);
	else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		hsw_enable_pc8(dev_priv);
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		ret = vlv_suspend_complete(dev_priv);

	if (ret) {
		DRM_ERROR("Suspend complete failed: %d\n", ret);
		if (!fw_csr)
			intel_power_domains_init_hw(dev_priv, true);

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

	dev_priv->suspended_to_idle = suspend_to_idle(dev_priv);

out:
	enable_rpm_wakeref_asserts(dev_priv);

	return ret;
}

static int i915_suspend_switcheroo(struct drm_device *dev, pm_message_t state)
{
	int error;

	if (!dev) {
		DRM_ERROR("dev: %p\n", dev);
		DRM_ERROR("DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	if (WARN_ON_ONCE(state.event != PM_EVENT_SUSPEND &&
			 state.event != PM_EVENT_FREEZE))
		return -EINVAL;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	error = i915_drm_suspend(dev);
	if (error)
		return error;

	return i915_drm_suspend_late(dev, false);
}

static int i915_drm_resume(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int ret;

	disable_rpm_wakeref_asserts(dev_priv);
	intel_sanitize_gt_powersave(dev_priv);

	ret = i915_ggtt_enable_hw(dev_priv);
	if (ret)
		DRM_ERROR("failed to re-enable GGTT\n");

	intel_csr_ucode_resume(dev_priv);

	i915_gem_resume(dev_priv);

	i915_restore_state(dev_priv);
	intel_pps_unlock_regs_wa(dev_priv);
	intel_opregion_setup(dev_priv);

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

	mutex_lock(&dev->struct_mutex);
	if (i915_gem_init_hw(dev_priv)) {
		DRM_ERROR("failed to re-initialize GPU, declaring wedged!\n");
		i915_gem_set_wedged(dev_priv);
	}
	mutex_unlock(&dev->struct_mutex);

	intel_guc_resume(dev_priv);

	intel_modeset_init_hw(dev);

	spin_lock_irq(&dev_priv->irq_lock);
	if (dev_priv->display.hpd_irq_setup)
		dev_priv->display.hpd_irq_setup(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	intel_dp_mst_resume(dev);

	intel_display_resume(dev);

	drm_kms_helper_poll_enable(dev);

	/*
	 * ... but also need to make sure that hotplug processing
	 * doesn't cause havoc. Like in the driver load code we don't
	 * bother with the tiny race here where we might loose hotplug
	 * notifications.
	 * */
	intel_hpd_init(dev_priv);

	intel_opregion_register(dev_priv);

	intel_fbdev_set_suspend(dev, FBINFO_STATE_RUNNING, false);

	mutex_lock(&dev_priv->modeset_restore_lock);
	dev_priv->modeset_restore = MODESET_DONE;
	mutex_unlock(&dev_priv->modeset_restore_lock);

	intel_opregion_notify_adapter(dev_priv, PCI_D0);

	intel_autoenable_gt_powersave(dev_priv);

	enable_rpm_wakeref_asserts(dev_priv);

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
		goto out;
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
	if (pci_enable_device(pdev)) {
		ret = -EIO;
		goto out;
	}

	pci_set_master(pdev);

	disable_rpm_wakeref_asserts(dev_priv);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		ret = vlv_resume_prepare(dev_priv, false);
	if (ret)
		DRM_ERROR("Resume prepare failed: %d, continuing anyway\n",
			  ret);

	intel_uncore_resume_early(dev_priv);

	if (IS_GEN9_LP(dev_priv)) {
		if (!dev_priv->suspended_to_idle)
			gen9_sanitize_dc_state(dev_priv);
		bxt_disable_dc9(dev_priv);
	} else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		hsw_disable_pc8(dev_priv);
	}

	intel_uncore_sanitize(dev_priv);

	if (IS_GEN9_LP(dev_priv) ||
	    !(dev_priv->suspended_to_idle && dev_priv->csr.dmc_payload))
		intel_power_domains_init_hw(dev_priv, true);

	i915_gem_sanitize(dev_priv);

	enable_rpm_wakeref_asserts(dev_priv);

out:
	dev_priv->suspended_to_idle = false;

	return ret;
}

static int i915_resume_switcheroo(struct drm_device *dev)
{
	int ret;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	ret = i915_drm_resume_early(dev);
	if (ret)
		return ret;

	return i915_drm_resume(dev);
}

/**
 * i915_reset - reset chip after a hang
 * @dev_priv: device private to reset
 *
 * Reset the chip.  Useful if a hang is detected. Marks the device as wedged
 * on failure.
 *
 * Caller must hold the struct_mutex.
 *
 * Procedure is fairly simple:
 *   - reset the chip using the reset reg
 *   - re-init context state
 *   - re-init hardware status page
 *   - re-init ring buffer
 *   - re-init interrupt state
 *   - re-init display
 */
void i915_reset(struct drm_i915_private *dev_priv)
{
	struct i915_gpu_error *error = &dev_priv->gpu_error;
	int ret;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);
	GEM_BUG_ON(!test_bit(I915_RESET_BACKOFF, &error->flags));

	if (!test_bit(I915_RESET_HANDOFF, &error->flags))
		return;

	/* Clear any previous failed attempts at recovery. Time to try again. */
	if (!i915_gem_unset_wedged(dev_priv))
		goto wakeup;

	error->reset_count++;

	pr_notice("drm/i915: Resetting chip after gpu hang\n");
	disable_irq(dev_priv->drm.irq);
	ret = i915_gem_reset_prepare(dev_priv);
	if (ret) {
		DRM_ERROR("GPU recovery failed\n");
		intel_gpu_reset(dev_priv, ALL_ENGINES);
		goto error;
	}

	ret = intel_gpu_reset(dev_priv, ALL_ENGINES);
	if (ret) {
		if (ret != -ENODEV)
			DRM_ERROR("Failed to reset chip: %i\n", ret);
		else
			DRM_DEBUG_DRIVER("GPU reset disabled\n");
		goto error;
	}

	i915_gem_reset(dev_priv);
	intel_overlay_reset(dev_priv);

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
	ret = i915_gem_init_hw(dev_priv);
	if (ret) {
		DRM_ERROR("Failed hw init on reset %d\n", ret);
		goto error;
	}

	i915_queue_hangcheck(dev_priv);

finish:
	i915_gem_reset_finish(dev_priv);
	enable_irq(dev_priv->drm.irq);

wakeup:
	clear_bit(I915_RESET_HANDOFF, &error->flags);
	wake_up_bit(&error->flags, I915_RESET_HANDOFF);
	return;

error:
	i915_gem_set_wedged(dev_priv);
	goto finish;
}

static int i915_pm_suspend(struct device *kdev)
{
	struct pci_dev *pdev = to_pci_dev(kdev);
	struct drm_device *dev = pci_get_drvdata(pdev);

	if (!dev) {
		dev_err(kdev, "DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_suspend(dev);
}

static int i915_pm_suspend_late(struct device *kdev)
{
	struct drm_device *dev = &kdev_to_i915(kdev)->drm;

	/*
	 * We have a suspend ordering issue with the snd-hda driver also
	 * requiring our device to be power up. Due to the lack of a
	 * parent/child relationship we currently solve this with an late
	 * suspend hook.
	 *
	 * FIXME: This should be solved with a special hdmi sink device or
	 * similar so that power domains can be employed.
	 */
	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_suspend_late(dev, false);
}

static int i915_pm_poweroff_late(struct device *kdev)
{
	struct drm_device *dev = &kdev_to_i915(kdev)->drm;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_suspend_late(dev, true);
}

static int i915_pm_resume_early(struct device *kdev)
{
	struct drm_device *dev = &kdev_to_i915(kdev)->drm;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_resume_early(dev);
}

static int i915_pm_resume(struct device *kdev)
{
	struct drm_device *dev = &kdev_to_i915(kdev)->drm;

	if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
		return 0;

	return i915_drm_resume(dev);
}

/* freeze: before creating the hibernation_image */
static int i915_pm_freeze(struct device *kdev)
{
	int ret;

	ret = i915_pm_suspend(kdev);
	if (ret)
		return ret;

	ret = i915_gem_freeze(kdev_to_i915(kdev));
	if (ret)
		return ret;

	return 0;
}

static int i915_pm_freeze_late(struct device *kdev)
{
	int ret;

	ret = i915_pm_suspend_late(kdev);
	if (ret)
		return ret;

	ret = i915_gem_freeze_late(kdev_to_i915(kdev));
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
	struct vlv_s0ix_state *s = &dev_priv->vlv_s0ix_state;
	int i;

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
	struct vlv_s0ix_state *s = &dev_priv->vlv_s0ix_state;
	u32 val;
	int i;

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

static int vlv_wait_for_pw_status(struct drm_i915_private *dev_priv,
				  u32 mask, u32 val)
{
	/* The HW does not like us polling for PW_STATUS frequently, so
	 * use the sleeping loop rather than risk the busy spin within
	 * intel_wait_for_register().
	 *
	 * Transitioning between RC6 states should be at most 2ms (see
	 * valleyview_enable_rps) so use a 3ms timeout.
	 */
	return wait_for((I915_READ_NOTRACE(VLV_GTLC_PW_STATUS) & mask) == val,
			3);
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

	err = intel_wait_for_register(dev_priv,
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
	 */
	if (vlv_wait_for_pw_status(dev_priv, mask, val))
		DRM_ERROR("timeout waiting for GT wells to go %s\n",
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

	if (!IS_CHERRYVIEW(dev_priv))
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

	if (!IS_CHERRYVIEW(dev_priv))
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
	struct pci_dev *pdev = to_pci_dev(kdev);
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_i915_private *dev_priv = to_i915(dev);
	int ret;

	if (WARN_ON_ONCE(!(dev_priv->rps.enabled && intel_enable_rc6())))
		return -ENODEV;

	if (WARN_ON_ONCE(!HAS_RUNTIME_PM(dev_priv)))
		return -ENODEV;

	DRM_DEBUG_KMS("Suspending device\n");

	disable_rpm_wakeref_asserts(dev_priv);

	/*
	 * We are safe here against re-faults, since the fault handler takes
	 * an RPM reference.
	 */
	i915_gem_runtime_suspend(dev_priv);

	intel_guc_suspend(dev_priv);

	intel_runtime_pm_disable_interrupts(dev_priv);

	ret = 0;
	if (IS_GEN9_LP(dev_priv)) {
		bxt_display_core_uninit(dev_priv);
		bxt_enable_dc9(dev_priv);
	} else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		hsw_enable_pc8(dev_priv);
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		ret = vlv_suspend_complete(dev_priv);
	}

	if (ret) {
		DRM_ERROR("Runtime suspend failed, disabling it (%d)\n", ret);
		intel_runtime_pm_enable_interrupts(dev_priv);

		enable_rpm_wakeref_asserts(dev_priv);

		return ret;
	}

	intel_uncore_suspend(dev_priv);

	enable_rpm_wakeref_asserts(dev_priv);
	WARN_ON_ONCE(atomic_read(&dev_priv->pm.wakeref_count));

	if (intel_uncore_arm_unclaimed_mmio_detection(dev_priv))
		DRM_ERROR("Unclaimed access detected prior to suspending\n");

	dev_priv->pm.suspended = true;

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

	assert_forcewakes_inactive(dev_priv);

	if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv))
		intel_hpd_poll_init(dev_priv);

	DRM_DEBUG_KMS("Device suspended\n");
	return 0;
}

static int intel_runtime_resume(struct device *kdev)
{
	struct pci_dev *pdev = to_pci_dev(kdev);
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_i915_private *dev_priv = to_i915(dev);
	int ret = 0;

	if (WARN_ON_ONCE(!HAS_RUNTIME_PM(dev_priv)))
		return -ENODEV;

	DRM_DEBUG_KMS("Resuming device\n");

	WARN_ON_ONCE(atomic_read(&dev_priv->pm.wakeref_count));
	disable_rpm_wakeref_asserts(dev_priv);

	intel_opregion_notify_adapter(dev_priv, PCI_D0);
	dev_priv->pm.suspended = false;
	if (intel_uncore_unclaimed_mmio(dev_priv))
		DRM_DEBUG_DRIVER("Unclaimed access during suspend, bios?\n");

	intel_guc_resume(dev_priv);

	if (IS_GEN6(dev_priv))
		intel_init_pch_refclk(dev_priv);

	if (IS_GEN9_LP(dev_priv)) {
		bxt_disable_dc9(dev_priv);
		bxt_display_core_init(dev_priv, true);
		if (dev_priv->csr.dmc_payload &&
		    (dev_priv->csr.allowed_dc_mask & DC_STATE_EN_UPTO_DC5))
			gen9_enable_dc5(dev_priv);
	} else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		hsw_disable_pc8(dev_priv);
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		ret = vlv_resume_prepare(dev_priv, true);
	}

	/*
	 * No point of rolling back things in case of an error, as the best
	 * we can do is to hope that things will still work (and disable RPM).
	 */
	i915_gem_init_swizzling(dev_priv);
	i915_gem_restore_fences(dev_priv);

	intel_runtime_pm_enable_interrupts(dev_priv);

	/*
	 * On VLV/CHV display interrupts are part of the display
	 * power well, so hpd is reinitialized from there. For
	 * everyone else do it here.
	 */
	if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv))
		intel_hpd_init(dev_priv);

	enable_rpm_wakeref_asserts(dev_priv);

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
	DRM_IOCTL_DEF_DRV(I915_GETPARAM, i915_getparam, DRM_AUTH|DRM_RENDER_ALLOW),
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
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER, i915_gem_execbuffer, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER2_WR, i915_gem_execbuffer2, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PIN, i915_gem_reject_pin_ioctl, DRM_AUTH|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_UNPIN, i915_gem_reject_pin_ioctl, DRM_AUTH|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_BUSY, i915_gem_busy_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_CACHING, i915_gem_set_caching_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_CACHING, i915_gem_get_caching_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_THROTTLE, i915_gem_throttle_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
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
	DRM_IOCTL_DEF_DRV(I915_GET_PIPE_FROM_CRTC_ID, intel_get_pipe_from_crtc_id, 0),
	DRM_IOCTL_DEF_DRV(I915_GEM_MADVISE, i915_gem_madvise_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_OVERLAY_PUT_IMAGE, intel_overlay_put_image_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_OVERLAY_ATTRS, intel_overlay_attrs_ioctl, DRM_MASTER|DRM_CONTROL_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_SET_SPRITE_COLORKEY, intel_sprite_set_colorkey, DRM_MASTER|DRM_CONTROL_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GET_SPRITE_COLORKEY, drm_noop, DRM_MASTER|DRM_CONTROL_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_WAIT, i915_gem_wait_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_CREATE, i915_gem_context_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_DESTROY, i915_gem_context_destroy_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_REG_READ, i915_reg_read_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GET_RESET_STATS, i915_gem_context_reset_stats_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_USERPTR, i915_gem_userptr_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_GETPARAM, i915_gem_context_getparam_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_SETPARAM, i915_gem_context_setparam_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_PERF_OPEN, i915_perf_open_ioctl, DRM_RENDER_ALLOW),
};

static struct drm_driver driver = {
	/* Don't use MTRRs here; the Xserver or userspace app should
	 * deal with them for Intel hardware.
	 */
	.driver_features =
	    DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | DRIVER_GEM | DRIVER_PRIME |
	    DRIVER_RENDER | DRIVER_MODESET | DRIVER_ATOMIC,
	.release = i915_driver_release,
	.open = i915_driver_open,
	.lastclose = i915_driver_lastclose,
	.postclose = i915_driver_postclose,
	.set_busid = drm_pci_set_busid,

	.gem_close_object = i915_gem_close_object,
	.gem_free_object_unlocked = i915_gem_free_object,
	.gem_vm_ops = &i915_gem_vm_ops,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = i915_gem_prime_export,
	.gem_prime_import = i915_gem_prime_import,

	.dumb_create = i915_gem_dumb_create,
	.dumb_map_offset = i915_gem_mmap_gtt,
	.dumb_destroy = drm_gem_dumb_destroy,
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
