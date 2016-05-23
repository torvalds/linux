/* i915_dma.c -- DMA support for the I915 -*- linux-c -*-
 */
/*
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_legacy.h>
#include "intel_drv.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_vgpu.h"
#include "i915_trace.h"
#include <linux/pci.h>
#include <linux/console.h>
#include <linux/vt.h>
#include <linux/vgaarb.h>
#include <linux/acpi.h>
#include <linux/pnp.h>
#include <linux/vga_switcheroo.h>
#include <linux/slab.h>
#include <acpi/video.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/oom.h>

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
	struct device *dev = dev_priv->dev->dev;
	bool is_error = level[1] <= KERN_ERR[1];
	bool is_debug = level[1] == KERN_DEBUG[1];
	struct va_format vaf;
	va_list args;

	if (is_debug && !(drm_debug & DRM_UT_DRIVER))
		return;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	dev_printk(level, dev, "[" DRM_NAME ":%ps] %pV",
		   __builtin_return_address(0), &vaf);

	if (is_error && !shown_bug_once) {
		dev_notice(dev, "%s", FDO_BUG_MSG);
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

static int i915_getparam(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	drm_i915_getparam_t *param = data;
	int value;

	switch (param->param) {
	case I915_PARAM_IRQ_ACTIVE:
	case I915_PARAM_ALLOW_BATCHBUFFER:
	case I915_PARAM_LAST_DISPATCH:
		/* Reject all old ums/dri params. */
		return -ENODEV;
	case I915_PARAM_CHIPSET_ID:
		value = dev->pdev->device;
		break;
	case I915_PARAM_REVISION:
		value = dev->pdev->revision;
		break;
	case I915_PARAM_HAS_GEM:
		value = 1;
		break;
	case I915_PARAM_NUM_FENCES_AVAIL:
		value = dev_priv->num_fence_regs;
		break;
	case I915_PARAM_HAS_OVERLAY:
		value = dev_priv->overlay ? 1 : 0;
		break;
	case I915_PARAM_HAS_PAGEFLIPPING:
		value = 1;
		break;
	case I915_PARAM_HAS_EXECBUF2:
		/* depends on GEM */
		value = 1;
		break;
	case I915_PARAM_HAS_BSD:
		value = intel_engine_initialized(&dev_priv->engine[VCS]);
		break;
	case I915_PARAM_HAS_BLT:
		value = intel_engine_initialized(&dev_priv->engine[BCS]);
		break;
	case I915_PARAM_HAS_VEBOX:
		value = intel_engine_initialized(&dev_priv->engine[VECS]);
		break;
	case I915_PARAM_HAS_BSD2:
		value = intel_engine_initialized(&dev_priv->engine[VCS2]);
		break;
	case I915_PARAM_HAS_RELAXED_FENCING:
		value = 1;
		break;
	case I915_PARAM_HAS_COHERENT_RINGS:
		value = 1;
		break;
	case I915_PARAM_HAS_EXEC_CONSTANTS:
		value = INTEL_INFO(dev)->gen >= 4;
		break;
	case I915_PARAM_HAS_RELAXED_DELTA:
		value = 1;
		break;
	case I915_PARAM_HAS_GEN7_SOL_RESET:
		value = 1;
		break;
	case I915_PARAM_HAS_LLC:
		value = HAS_LLC(dev);
		break;
	case I915_PARAM_HAS_WT:
		value = HAS_WT(dev);
		break;
	case I915_PARAM_HAS_ALIASING_PPGTT:
		value = USES_PPGTT(dev);
		break;
	case I915_PARAM_HAS_WAIT_TIMEOUT:
		value = 1;
		break;
	case I915_PARAM_HAS_SEMAPHORES:
		value = i915_semaphore_is_enabled(dev_priv);
		break;
	case I915_PARAM_HAS_PRIME_VMAP_FLUSH:
		value = 1;
		break;
	case I915_PARAM_HAS_SECURE_BATCHES:
		value = capable(CAP_SYS_ADMIN);
		break;
	case I915_PARAM_HAS_PINNED_BATCHES:
		value = 1;
		break;
	case I915_PARAM_HAS_EXEC_NO_RELOC:
		value = 1;
		break;
	case I915_PARAM_HAS_EXEC_HANDLE_LUT:
		value = 1;
		break;
	case I915_PARAM_CMD_PARSER_VERSION:
		value = i915_cmd_parser_get_version(dev_priv);
		break;
	case I915_PARAM_HAS_COHERENT_PHYS_GTT:
		value = 1;
		break;
	case I915_PARAM_MMAP_VERSION:
		value = 1;
		break;
	case I915_PARAM_SUBSLICE_TOTAL:
		value = INTEL_INFO(dev)->subslice_total;
		if (!value)
			return -ENODEV;
		break;
	case I915_PARAM_EU_TOTAL:
		value = INTEL_INFO(dev)->eu_total;
		if (!value)
			return -ENODEV;
		break;
	case I915_PARAM_HAS_GPU_RESET:
		value = i915.enable_hangcheck && intel_has_gpu_reset(dev_priv);
		break;
	case I915_PARAM_HAS_RESOURCE_STREAMER:
		value = HAS_RESOURCE_STREAMER(dev);
		break;
	case I915_PARAM_HAS_EXEC_SOFTPIN:
		value = 1;
		break;
	default:
		DRM_DEBUG("Unknown parameter %d\n", param->param);
		return -EINVAL;
	}

	if (copy_to_user(param->value, &value, sizeof(int))) {
		DRM_ERROR("copy_to_user failed\n");
		return -EFAULT;
	}

	return 0;
}

static int i915_get_bridge_dev(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->bridge_dev = pci_get_bus_and_slot(0, PCI_DEVFN(0, 0));
	if (!dev_priv->bridge_dev) {
		DRM_ERROR("bridge device not found\n");
		return -1;
	}
	return 0;
}

/* Allocate space for the MCH regs if needed, return nonzero on error */
static int
intel_alloc_mchbar_resource(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int reg = INTEL_INFO(dev)->gen >= 4 ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp_lo, temp_hi = 0;
	u64 mchbar_addr;
	int ret;

	if (INTEL_INFO(dev)->gen >= 4)
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

	if (INTEL_INFO(dev)->gen >= 4)
		pci_write_config_dword(dev_priv->bridge_dev, reg + 4,
				       upper_32_bits(dev_priv->mch_res.start));

	pci_write_config_dword(dev_priv->bridge_dev, reg,
			       lower_32_bits(dev_priv->mch_res.start));
	return 0;
}

/* Setup MCHBAR if possible, return true if we should disable it again */
static void
intel_setup_mchbar(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int mchbar_reg = INTEL_INFO(dev)->gen >= 4 ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp;
	bool enabled;

	if (IS_VALLEYVIEW(dev) || IS_CHERRYVIEW(dev))
		return;

	dev_priv->mchbar_need_disable = false;

	if (IS_I915G(dev) || IS_I915GM(dev)) {
		pci_read_config_dword(dev_priv->bridge_dev, DEVEN, &temp);
		enabled = !!(temp & DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(dev_priv->bridge_dev, mchbar_reg, &temp);
		enabled = temp & 1;
	}

	/* If it's already enabled, don't have to do anything */
	if (enabled)
		return;

	if (intel_alloc_mchbar_resource(dev))
		return;

	dev_priv->mchbar_need_disable = true;

	/* Space is allocated or reserved, so enable it. */
	if (IS_I915G(dev) || IS_I915GM(dev)) {
		pci_write_config_dword(dev_priv->bridge_dev, DEVEN,
				       temp | DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(dev_priv->bridge_dev, mchbar_reg, &temp);
		pci_write_config_dword(dev_priv->bridge_dev, mchbar_reg, temp | 1);
	}
}

static void
intel_teardown_mchbar(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int mchbar_reg = INTEL_INFO(dev)->gen >= 4 ? MCHBAR_I965 : MCHBAR_I915;

	if (dev_priv->mchbar_need_disable) {
		if (IS_I915G(dev) || IS_I915GM(dev)) {
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
	struct drm_device *dev = cookie;

	intel_modeset_vga_set_state(dev, state);
	if (state)
		return VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
		       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
	else
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
}

static void i915_switcheroo_set_state(struct pci_dev *pdev, enum vga_switcheroo_state state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	pm_message_t pmm = { .event = PM_EVENT_SUSPEND };

	if (state == VGA_SWITCHEROO_ON) {
		pr_info("switched on\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		/* i915 resume handler doesn't set to D0 */
		pci_set_power_state(dev->pdev, PCI_D0);
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

static void i915_gem_fini(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	/*
	 * Neither the BIOS, ourselves or any other kernel
	 * expects the system to be in execlists mode on startup,
	 * so we need to reset the GPU back to legacy mode. And the only
	 * known way to disable logical contexts is through a GPU reset.
	 *
	 * So in order to leave the system in a known default configuration,
	 * always reset the GPU upon unload. Afterwards we then clean up the
	 * GEM state tracking, flushing off the requests and leaving the
	 * system in a known idle state.
	 *
	 * Note that is of the upmost importance that the GPU is idle and
	 * all stray writes are flushed *before* we dismantle the backing
	 * storage for the pinned objects.
	 *
	 * However, since we are uncertain that reseting the GPU on older
	 * machines is a good idea, we don't - just in case it leaves the
	 * machine in an unusable condition.
	 */
	if (HAS_HW_CONTEXTS(dev)) {
		int reset = intel_gpu_reset(dev_priv, ALL_ENGINES);
		WARN_ON(reset && reset != -ENODEV);
	}

	mutex_lock(&dev->struct_mutex);
	i915_gem_reset(dev);
	i915_gem_cleanup_engines(dev);
	i915_gem_context_fini(dev);
	mutex_unlock(&dev->struct_mutex);

	WARN_ON(!list_empty(&to_i915(dev)->context_list));
}

static int i915_load_modeset_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	if (i915_inject_load_failure())
		return -ENODEV;

	ret = intel_bios_init(dev_priv);
	if (ret)
		DRM_INFO("failed to find VBIOS tables\n");

	/* If we have > 1 VGA cards, then we need to arbitrate access
	 * to the common VGA resources.
	 *
	 * If we are a secondary display controller (!PCI_DISPLAY_CLASS_VGA),
	 * then we do not take part in VGA arbitration and the
	 * vga_client_register() fails with -ENODEV.
	 */
	ret = vga_client_register(dev->pdev, dev, NULL, i915_vga_set_decode);
	if (ret && ret != -ENODEV)
		goto out;

	intel_register_dsm_handler();

	ret = vga_switcheroo_register_client(dev->pdev, &i915_switcheroo_ops, false);
	if (ret)
		goto cleanup_vga_client;

	/* must happen before intel_power_domains_init_hw() on VLV/CHV */
	intel_update_rawclk(dev_priv);

	intel_power_domains_init_hw(dev_priv, false);

	intel_csr_ucode_init(dev_priv);

	ret = intel_irq_install(dev_priv);
	if (ret)
		goto cleanup_csr;

	intel_setup_gmbus(dev);

	/* Important: The output setup functions called by modeset_init need
	 * working irqs for e.g. gmbus and dp aux transfers. */
	intel_modeset_init(dev);

	intel_guc_init(dev);

	ret = i915_gem_init(dev);
	if (ret)
		goto cleanup_irq;

	intel_modeset_gem_init(dev);

	/* Always safe in the mode setting case. */
	/* FIXME: do pre/post-mode set stuff in core KMS code */
	dev->vblank_disable_allowed = true;
	if (INTEL_INFO(dev)->num_pipes == 0)
		return 0;

	ret = intel_fbdev_init(dev);
	if (ret)
		goto cleanup_gem;

	/* Only enable hotplug handling once the fbdev is fully set up. */
	intel_hpd_init(dev_priv);

	/*
	 * Some ports require correctly set-up hpd registers for detection to
	 * work properly (leading to ghost connected connector status), e.g. VGA
	 * on gm45.  Hence we can only set up the initial fbdev config after hpd
	 * irqs are fully enabled. Now we should scan for the initial config
	 * only once hotplug handling is enabled, but due to screwed-up locking
	 * around kms/fbdev init we can't protect the fdbev initial config
	 * scanning against hotplug events. Hence do this first and ignore the
	 * tiny window where we will loose hotplug notifactions.
	 */
	intel_fbdev_initial_config_async(dev);

	drm_kms_helper_poll_init(dev);

	return 0;

cleanup_gem:
	i915_gem_fini(dev);
cleanup_irq:
	intel_guc_fini(dev);
	drm_irq_uninstall(dev);
	intel_teardown_gmbus(dev);
cleanup_csr:
	intel_csr_ucode_fini(dev_priv);
	intel_power_domains_fini(dev_priv);
	vga_switcheroo_unregister_client(dev->pdev);
cleanup_vga_client:
	vga_client_register(dev->pdev, NULL, NULL, NULL);
out:
	return ret;
}

#if IS_ENABLED(CONFIG_FB)
static int i915_kick_out_firmware_fb(struct drm_i915_private *dev_priv)
{
	struct apertures_struct *ap;
	struct pci_dev *pdev = dev_priv->dev->pdev;
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

	ret = remove_conflicting_framebuffers(ap, "inteldrmfb", primary);

	kfree(ap);

	return ret;
}
#else
static int i915_kick_out_firmware_fb(struct drm_i915_private *dev_priv)
{
	return 0;
}
#endif

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

static void i915_dump_device_info(struct drm_i915_private *dev_priv)
{
	const struct intel_device_info *info = &dev_priv->info;

#define PRINT_S(name) "%s"
#define SEP_EMPTY
#define PRINT_FLAG(name) info->name ? #name "," : ""
#define SEP_COMMA ,
	DRM_DEBUG_DRIVER("i915 device info: gen=%i, pciid=0x%04x rev=0x%02x flags="
			 DEV_INFO_FOR_EACH_FLAG(PRINT_S, SEP_EMPTY),
			 info->gen,
			 dev_priv->dev->pdev->device,
			 dev_priv->dev->pdev->revision,
			 DEV_INFO_FOR_EACH_FLAG(PRINT_FLAG, SEP_COMMA));
#undef PRINT_S
#undef SEP_EMPTY
#undef PRINT_FLAG
#undef SEP_COMMA
}

static void cherryview_sseu_info_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_device_info *info;
	u32 fuse, eu_dis;

	info = (struct intel_device_info *)&dev_priv->info;
	fuse = I915_READ(CHV_FUSE_GT);

	info->slice_total = 1;

	if (!(fuse & CHV_FGT_DISABLE_SS0)) {
		info->subslice_per_slice++;
		eu_dis = fuse & (CHV_FGT_EU_DIS_SS0_R0_MASK |
				 CHV_FGT_EU_DIS_SS0_R1_MASK);
		info->eu_total += 8 - hweight32(eu_dis);
	}

	if (!(fuse & CHV_FGT_DISABLE_SS1)) {
		info->subslice_per_slice++;
		eu_dis = fuse & (CHV_FGT_EU_DIS_SS1_R0_MASK |
				 CHV_FGT_EU_DIS_SS1_R1_MASK);
		info->eu_total += 8 - hweight32(eu_dis);
	}

	info->subslice_total = info->subslice_per_slice;
	/*
	 * CHV expected to always have a uniform distribution of EU
	 * across subslices.
	*/
	info->eu_per_subslice = info->subslice_total ?
				info->eu_total / info->subslice_total :
				0;
	/*
	 * CHV supports subslice power gating on devices with more than
	 * one subslice, and supports EU power gating on devices with
	 * more than one EU pair per subslice.
	*/
	info->has_slice_pg = 0;
	info->has_subslice_pg = (info->subslice_total > 1);
	info->has_eu_pg = (info->eu_per_subslice > 2);
}

static void gen9_sseu_info_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_device_info *info;
	int s_max = 3, ss_max = 4, eu_max = 8;
	int s, ss;
	u32 fuse2, s_enable, ss_disable, eu_disable;
	u8 eu_mask = 0xff;

	info = (struct intel_device_info *)&dev_priv->info;
	fuse2 = I915_READ(GEN8_FUSE2);
	s_enable = (fuse2 & GEN8_F2_S_ENA_MASK) >>
		   GEN8_F2_S_ENA_SHIFT;
	ss_disable = (fuse2 & GEN9_F2_SS_DIS_MASK) >>
		     GEN9_F2_SS_DIS_SHIFT;

	info->slice_total = hweight32(s_enable);
	/*
	 * The subslice disable field is global, i.e. it applies
	 * to each of the enabled slices.
	*/
	info->subslice_per_slice = ss_max - hweight32(ss_disable);
	info->subslice_total = info->slice_total *
			       info->subslice_per_slice;

	/*
	 * Iterate through enabled slices and subslices to
	 * count the total enabled EU.
	*/
	for (s = 0; s < s_max; s++) {
		if (!(s_enable & (0x1 << s)))
			/* skip disabled slice */
			continue;

		eu_disable = I915_READ(GEN9_EU_DISABLE(s));
		for (ss = 0; ss < ss_max; ss++) {
			int eu_per_ss;

			if (ss_disable & (0x1 << ss))
				/* skip disabled subslice */
				continue;

			eu_per_ss = eu_max - hweight8((eu_disable >> (ss*8)) &
						      eu_mask);

			/*
			 * Record which subslice(s) has(have) 7 EUs. we
			 * can tune the hash used to spread work among
			 * subslices if they are unbalanced.
			 */
			if (eu_per_ss == 7)
				info->subslice_7eu[s] |= 1 << ss;

			info->eu_total += eu_per_ss;
		}
	}

	/*
	 * SKL is expected to always have a uniform distribution
	 * of EU across subslices with the exception that any one
	 * EU in any one subslice may be fused off for die
	 * recovery. BXT is expected to be perfectly uniform in EU
	 * distribution.
	*/
	info->eu_per_subslice = info->subslice_total ?
				DIV_ROUND_UP(info->eu_total,
					     info->subslice_total) : 0;
	/*
	 * SKL supports slice power gating on devices with more than
	 * one slice, and supports EU power gating on devices with
	 * more than one EU pair per subslice. BXT supports subslice
	 * power gating on devices with more than one subslice, and
	 * supports EU power gating on devices with more than one EU
	 * pair per subslice.
	*/
	info->has_slice_pg = ((IS_SKYLAKE(dev) || IS_KABYLAKE(dev)) &&
			       (info->slice_total > 1));
	info->has_subslice_pg = (IS_BROXTON(dev) && (info->subslice_total > 1));
	info->has_eu_pg = (info->eu_per_subslice > 2);
}

static void broadwell_sseu_info_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_device_info *info;
	const int s_max = 3, ss_max = 3, eu_max = 8;
	int s, ss;
	u32 fuse2, eu_disable[s_max], s_enable, ss_disable;

	fuse2 = I915_READ(GEN8_FUSE2);
	s_enable = (fuse2 & GEN8_F2_S_ENA_MASK) >> GEN8_F2_S_ENA_SHIFT;
	ss_disable = (fuse2 & GEN8_F2_SS_DIS_MASK) >> GEN8_F2_SS_DIS_SHIFT;

	eu_disable[0] = I915_READ(GEN8_EU_DISABLE0) & GEN8_EU_DIS0_S0_MASK;
	eu_disable[1] = (I915_READ(GEN8_EU_DISABLE0) >> GEN8_EU_DIS0_S1_SHIFT) |
			((I915_READ(GEN8_EU_DISABLE1) & GEN8_EU_DIS1_S1_MASK) <<
			 (32 - GEN8_EU_DIS0_S1_SHIFT));
	eu_disable[2] = (I915_READ(GEN8_EU_DISABLE1) >> GEN8_EU_DIS1_S2_SHIFT) |
			((I915_READ(GEN8_EU_DISABLE2) & GEN8_EU_DIS2_S2_MASK) <<
			 (32 - GEN8_EU_DIS1_S2_SHIFT));


	info = (struct intel_device_info *)&dev_priv->info;
	info->slice_total = hweight32(s_enable);

	/*
	 * The subslice disable field is global, i.e. it applies
	 * to each of the enabled slices.
	 */
	info->subslice_per_slice = ss_max - hweight32(ss_disable);
	info->subslice_total = info->slice_total * info->subslice_per_slice;

	/*
	 * Iterate through enabled slices and subslices to
	 * count the total enabled EU.
	 */
	for (s = 0; s < s_max; s++) {
		if (!(s_enable & (0x1 << s)))
			/* skip disabled slice */
			continue;

		for (ss = 0; ss < ss_max; ss++) {
			u32 n_disabled;

			if (ss_disable & (0x1 << ss))
				/* skip disabled subslice */
				continue;

			n_disabled = hweight8(eu_disable[s] >> (ss * eu_max));

			/*
			 * Record which subslices have 7 EUs.
			 */
			if (eu_max - n_disabled == 7)
				info->subslice_7eu[s] |= 1 << ss;

			info->eu_total += eu_max - n_disabled;
		}
	}

	/*
	 * BDW is expected to always have a uniform distribution of EU across
	 * subslices with the exception that any one EU in any one subslice may
	 * be fused off for die recovery.
	 */
	info->eu_per_subslice = info->subslice_total ?
		DIV_ROUND_UP(info->eu_total, info->subslice_total) : 0;

	/*
	 * BDW supports slice power gating on devices with more than
	 * one slice.
	 */
	info->has_slice_pg = (info->slice_total > 1);
	info->has_subslice_pg = 0;
	info->has_eu_pg = 0;
}

/*
 * Determine various intel_device_info fields at runtime.
 *
 * Use it when either:
 *   - it's judged too laborious to fill n static structures with the limit
 *     when a simple if statement does the job,
 *   - run-time checks (eg read fuse/strap registers) are needed.
 *
 * This function needs to be called:
 *   - after the MMIO has been setup as we are reading registers,
 *   - after the PCH has been detected,
 *   - before the first usage of the fields it can tweak.
 */
static void intel_device_info_runtime_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_device_info *info;
	enum pipe pipe;

	info = (struct intel_device_info *)&dev_priv->info;

	/*
	 * Skylake and Broxton currently don't expose the topmost plane as its
	 * use is exclusive with the legacy cursor and we only want to expose
	 * one of those, not both. Until we can safely expose the topmost plane
	 * as a DRM_PLANE_TYPE_CURSOR with all the features exposed/supported,
	 * we don't expose the topmost plane at all to prevent ABI breakage
	 * down the line.
	 */
	if (IS_BROXTON(dev)) {
		info->num_sprites[PIPE_A] = 2;
		info->num_sprites[PIPE_B] = 2;
		info->num_sprites[PIPE_C] = 1;
	} else if (IS_VALLEYVIEW(dev) || IS_CHERRYVIEW(dev))
		for_each_pipe(dev_priv, pipe)
			info->num_sprites[pipe] = 2;
	else
		for_each_pipe(dev_priv, pipe)
			info->num_sprites[pipe] = 1;

	if (i915.disable_display) {
		DRM_INFO("Display disabled (module parameter)\n");
		info->num_pipes = 0;
	} else if (info->num_pipes > 0 &&
		   (IS_GEN7(dev_priv) || IS_GEN8(dev_priv)) &&
		   HAS_PCH_SPLIT(dev)) {
		u32 fuse_strap = I915_READ(FUSE_STRAP);
		u32 sfuse_strap = I915_READ(SFUSE_STRAP);

		/*
		 * SFUSE_STRAP is supposed to have a bit signalling the display
		 * is fused off. Unfortunately it seems that, at least in
		 * certain cases, fused off display means that PCH display
		 * reads don't land anywhere. In that case, we read 0s.
		 *
		 * On CPT/PPT, we can detect this case as SFUSE_STRAP_FUSE_LOCK
		 * should be set when taking over after the firmware.
		 */
		if (fuse_strap & ILK_INTERNAL_DISPLAY_DISABLE ||
		    sfuse_strap & SFUSE_STRAP_DISPLAY_DISABLED ||
		    (dev_priv->pch_type == PCH_CPT &&
		     !(sfuse_strap & SFUSE_STRAP_FUSE_LOCK))) {
			DRM_INFO("Display fused off, disabling\n");
			info->num_pipes = 0;
		} else if (fuse_strap & IVB_PIPE_C_DISABLE) {
			DRM_INFO("PipeC fused off\n");
			info->num_pipes -= 1;
		}
	} else if (info->num_pipes > 0 && IS_GEN9(dev_priv)) {
		u32 dfsm = I915_READ(SKL_DFSM);
		u8 disabled_mask = 0;
		bool invalid;
		int num_bits;

		if (dfsm & SKL_DFSM_PIPE_A_DISABLE)
			disabled_mask |= BIT(PIPE_A);
		if (dfsm & SKL_DFSM_PIPE_B_DISABLE)
			disabled_mask |= BIT(PIPE_B);
		if (dfsm & SKL_DFSM_PIPE_C_DISABLE)
			disabled_mask |= BIT(PIPE_C);

		num_bits = hweight8(disabled_mask);

		switch (disabled_mask) {
		case BIT(PIPE_A):
		case BIT(PIPE_B):
		case BIT(PIPE_A) | BIT(PIPE_B):
		case BIT(PIPE_A) | BIT(PIPE_C):
			invalid = true;
			break;
		default:
			invalid = false;
		}

		if (num_bits > info->num_pipes || invalid)
			DRM_ERROR("invalid pipe fuse configuration: 0x%x\n",
				  disabled_mask);
		else
			info->num_pipes -= num_bits;
	}

	/* Initialize slice/subslice/EU info */
	if (IS_CHERRYVIEW(dev))
		cherryview_sseu_info_init(dev);
	else if (IS_BROADWELL(dev))
		broadwell_sseu_info_init(dev);
	else if (INTEL_INFO(dev)->gen >= 9)
		gen9_sseu_info_init(dev);

	info->has_snoop = !info->has_llc;

	/* Snooping is broken on BXT A stepping. */
	if (IS_BXT_REVID(dev, 0, BXT_REVID_A1))
		info->has_snoop = false;

	DRM_DEBUG_DRIVER("slice total: %u\n", info->slice_total);
	DRM_DEBUG_DRIVER("subslice total: %u\n", info->subslice_total);
	DRM_DEBUG_DRIVER("subslice per slice: %u\n", info->subslice_per_slice);
	DRM_DEBUG_DRIVER("EU total: %u\n", info->eu_total);
	DRM_DEBUG_DRIVER("EU per subslice: %u\n", info->eu_per_subslice);
	DRM_DEBUG_DRIVER("has slice power gating: %s\n",
			 info->has_slice_pg ? "y" : "n");
	DRM_DEBUG_DRIVER("has subslice power gating: %s\n",
			 info->has_subslice_pg ? "y" : "n");
	DRM_DEBUG_DRIVER("has EU power gating: %s\n",
			 info->has_eu_pg ? "y" : "n");

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

	dev_priv->gpu_error.hangcheck_wq =
		alloc_ordered_workqueue("i915-hangcheck", 0);
	if (dev_priv->gpu_error.hangcheck_wq == NULL)
		goto out_free_dp_wq;

	return 0;

out_free_dp_wq:
	destroy_workqueue(dev_priv->hotplug.dp_wq);
out_free_wq:
	destroy_workqueue(dev_priv->wq);
out_err:
	DRM_ERROR("Failed to allocate workqueues.\n");

	return -ENOMEM;
}

static void i915_workqueues_cleanup(struct drm_i915_private *dev_priv)
{
	destroy_workqueue(dev_priv->gpu_error.hangcheck_wq);
	destroy_workqueue(dev_priv->hotplug.dp_wq);
	destroy_workqueue(dev_priv->wq);
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
				  struct drm_device *dev,
				  struct intel_device_info *info)
{
	struct intel_device_info *device_info;
	int ret = 0;

	if (i915_inject_load_failure())
		return -ENODEV;

	/* Setup the write-once "constant" device info */
	device_info = (struct intel_device_info *)&dev_priv->info;
	memcpy(device_info, info, sizeof(dev_priv->info));
	device_info->device_id = dev->pdev->device;

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

	ret = i915_workqueues_init(dev_priv);
	if (ret < 0)
		return ret;

	/* This must be called before any calls to HAS_PCH_* */
	intel_detect_pch(dev);

	intel_pm_setup(dev);
	intel_init_dpio(dev_priv);
	intel_power_domains_init(dev_priv);
	intel_irq_init(dev_priv);
	intel_init_display_hooks(dev_priv);
	intel_init_clock_gating_hooks(dev_priv);
	intel_init_audio_hooks(dev_priv);
	i915_gem_load_init(dev);

	intel_display_crc_init(dev);

	i915_dump_device_info(dev_priv);

	/* Not all pre-production machines fall into this category, only the
	 * very first ones. Almost everything should work, except for maybe
	 * suspend/resume. And we don't implement workarounds that affect only
	 * pre-production machines. */
	if (IS_HSW_EARLY_SDV(dev))
		DRM_INFO("This is an early pre-production Haswell machine. "
			 "It may not be fully functional.\n");

	return 0;
}

/**
 * i915_driver_cleanup_early - cleanup the setup done in i915_driver_init_early()
 * @dev_priv: device private
 */
static void i915_driver_cleanup_early(struct drm_i915_private *dev_priv)
{
	i915_gem_load_cleanup(dev_priv->dev);
	i915_workqueues_cleanup(dev_priv);
}

static int i915_mmio_setup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int mmio_bar;
	int mmio_size;

	mmio_bar = IS_GEN2(dev) ? 1 : 0;
	/*
	 * Before gen4, the registers and the GTT are behind different BARs.
	 * However, from gen4 onwards, the registers and the GTT are shared
	 * in the same BAR, so we want to restrict this ioremap from
	 * clobbering the GTT which we want ioremap_wc instead. Fortunately,
	 * the register BAR remains the same size for all the earlier
	 * generations up to Ironlake.
	 */
	if (INTEL_INFO(dev)->gen < 5)
		mmio_size = 512 * 1024;
	else
		mmio_size = 2 * 1024 * 1024;
	dev_priv->regs = pci_iomap(dev->pdev, mmio_bar, mmio_size);
	if (dev_priv->regs == NULL) {
		DRM_ERROR("failed to map registers\n");

		return -EIO;
	}

	/* Try to make sure MCHBAR is enabled before poking at it */
	intel_setup_mchbar(dev);

	return 0;
}

static void i915_mmio_cleanup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	intel_teardown_mchbar(dev);
	pci_iounmap(dev->pdev, dev_priv->regs);
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
	struct drm_device *dev = dev_priv->dev;
	int ret;

	if (i915_inject_load_failure())
		return -ENODEV;

	if (i915_get_bridge_dev(dev))
		return -EIO;

	ret = i915_mmio_setup(dev);
	if (ret < 0)
		goto put_bridge;

	intel_uncore_init(dev_priv);

	return 0;

put_bridge:
	pci_dev_put(dev_priv->bridge_dev);

	return ret;
}

/**
 * i915_driver_cleanup_mmio - cleanup the setup done in i915_driver_init_mmio()
 * @dev_priv: device private
 */
static void i915_driver_cleanup_mmio(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;

	intel_uncore_fini(dev_priv);
	i915_mmio_cleanup(dev);
	pci_dev_put(dev_priv->bridge_dev);
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
	struct drm_device *dev = dev_priv->dev;
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	uint32_t aperture_size;
	int ret;

	if (i915_inject_load_failure())
		return -ENODEV;

	intel_device_info_runtime_init(dev);

	ret = i915_ggtt_init_hw(dev);
	if (ret)
		return ret;

	ret = i915_ggtt_enable_hw(dev);
	if (ret) {
		DRM_ERROR("failed to enable GGTT\n");
		goto out_ggtt;
	}

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

	pci_set_master(dev->pdev);

	/* overlay on gen2 is broken and can't address above 1G */
	if (IS_GEN2(dev)) {
		ret = dma_set_coherent_mask(&dev->pdev->dev, DMA_BIT_MASK(30));
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
	if (IS_BROADWATER(dev) || IS_CRESTLINE(dev)) {
		ret = dma_set_coherent_mask(&dev->pdev->dev, DMA_BIT_MASK(32));

		if (ret) {
			DRM_ERROR("failed to set DMA mask\n");

			goto out_ggtt;
		}
	}

	aperture_size = ggtt->mappable_end;

	ggtt->mappable =
		io_mapping_create_wc(ggtt->mappable_base,
				     aperture_size);
	if (!ggtt->mappable) {
		ret = -EIO;
		goto out_ggtt;
	}

	ggtt->mtrr = arch_phys_wc_add(ggtt->mappable_base,
					      aperture_size);

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
	if (!IS_I945G(dev) && !IS_I945GM(dev)) {
		if (pci_enable_msi(dev->pdev) < 0)
			DRM_DEBUG_DRIVER("can't enable MSI");
	}

	return 0;

out_ggtt:
	i915_ggtt_cleanup_hw(dev);

	return ret;
}

/**
 * i915_driver_cleanup_hw - cleanup the setup done in i915_driver_init_hw()
 * @dev_priv: device private
 */
static void i915_driver_cleanup_hw(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct i915_ggtt *ggtt = &dev_priv->ggtt;

	if (dev->pdev->msi_enabled)
		pci_disable_msi(dev->pdev);

	pm_qos_remove_request(&dev_priv->pm_qos);
	arch_phys_wc_del(ggtt->mtrr);
	io_mapping_free(ggtt->mappable);
	i915_ggtt_cleanup_hw(dev);
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
	struct drm_device *dev = dev_priv->dev;

	i915_gem_shrinker_init(dev_priv);
	/*
	 * Notify a valid surface after modesetting,
	 * when running inside a VM.
	 */
	if (intel_vgpu_active(dev_priv))
		I915_WRITE(vgtif_reg(display_ready), VGT_DRV_DISPLAY_READY);

	i915_setup_sysfs(dev);

	if (INTEL_INFO(dev_priv)->num_pipes) {
		/* Must be done after probing outputs */
		intel_opregion_init(dev_priv);
		acpi_video_register();
	}

	if (IS_GEN5(dev_priv))
		intel_gpu_ips_init(dev_priv);

	i915_audio_component_init(dev_priv);
}

/**
 * i915_driver_unregister - cleanup the registration done in i915_driver_regiser()
 * @dev_priv: device private
 */
static void i915_driver_unregister(struct drm_i915_private *dev_priv)
{
	i915_audio_component_cleanup(dev_priv);
	intel_gpu_ips_teardown();
	acpi_video_unregister();
	intel_opregion_fini(dev_priv);
	i915_teardown_sysfs(dev_priv->dev);
	i915_gem_shrinker_cleanup(dev_priv);
}

/**
 * i915_driver_load - setup chip and create an initial config
 * @dev: DRM device
 * @flags: startup flags
 *
 * The driver load routine has to do several things:
 *   - drive output discovery via intel_modeset_init()
 *   - initialize the memory manager
 *   - allocate initial config memory
 *   - setup the DRM framebuffer with the allocated memory
 */
int i915_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct drm_i915_private *dev_priv;
	int ret = 0;

	dev_priv = kzalloc(sizeof(*dev_priv), GFP_KERNEL);
	if (dev_priv == NULL)
		return -ENOMEM;

	dev->dev_private = dev_priv;
	/* Must be set before calling __i915_printk */
	dev_priv->dev = dev;

	ret = i915_driver_init_early(dev_priv, dev,
				     (struct intel_device_info *)flags);

	if (ret < 0)
		goto out_free_priv;

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
	if (INTEL_INFO(dev)->num_pipes) {
		ret = drm_vblank_init(dev, INTEL_INFO(dev)->num_pipes);
		if (ret)
			goto out_cleanup_hw;
	}

	ret = i915_load_modeset_init(dev);
	if (ret < 0)
		goto out_cleanup_vblank;

	i915_driver_register(dev_priv);

	intel_runtime_pm_enable(dev_priv);

	intel_runtime_pm_put(dev_priv);

	return 0;

out_cleanup_vblank:
	drm_vblank_cleanup(dev);
out_cleanup_hw:
	i915_driver_cleanup_hw(dev_priv);
out_cleanup_mmio:
	i915_driver_cleanup_mmio(dev_priv);
out_runtime_pm_put:
	intel_runtime_pm_put(dev_priv);
	i915_driver_cleanup_early(dev_priv);
out_free_priv:
	i915_load_error(dev_priv, "Device initialization failed (%d)\n", ret);

	kfree(dev_priv);

	return ret;
}

int i915_driver_unload(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	intel_fbdev_fini(dev);

	ret = i915_gem_suspend(dev);
	if (ret) {
		DRM_ERROR("failed to idle hardware: %d\n", ret);
		return ret;
	}

	intel_display_power_get(dev_priv, POWER_DOMAIN_INIT);

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

	vga_switcheroo_unregister_client(dev->pdev);
	vga_client_register(dev->pdev, NULL, NULL, NULL);

	intel_csr_ucode_fini(dev_priv);

	/* Free error state after interrupts are fully disabled. */
	cancel_delayed_work_sync(&dev_priv->gpu_error.hangcheck_work);
	i915_destroy_error_state(dev);

	/* Flush any outstanding unpin_work. */
	flush_workqueue(dev_priv->wq);

	intel_guc_fini(dev);
	i915_gem_fini(dev);
	intel_fbc_cleanup_cfb(dev_priv);

	intel_power_domains_fini(dev_priv);

	i915_driver_cleanup_hw(dev_priv);
	i915_driver_cleanup_mmio(dev_priv);

	intel_display_power_put(dev_priv, POWER_DOMAIN_INIT);

	i915_driver_cleanup_early(dev_priv);
	kfree(dev_priv);

	return 0;
}

int i915_driver_open(struct drm_device *dev, struct drm_file *file)
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
void i915_driver_lastclose(struct drm_device *dev)
{
	intel_fbdev_restore_mode(dev);
	vga_switcheroo_process_delayed_switch();
}

void i915_driver_preclose(struct drm_device *dev, struct drm_file *file)
{
	mutex_lock(&dev->struct_mutex);
	i915_gem_context_close(dev, file);
	i915_gem_release(dev, file);
	mutex_unlock(&dev->struct_mutex);
}

void i915_driver_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	kfree(file_priv);
}

static int
i915_gem_reject_pin_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	return -ENODEV;
}

const struct drm_ioctl_desc i915_ioctls[] = {
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
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER2, i915_gem_execbuffer2, DRM_AUTH|DRM_RENDER_ALLOW),
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
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_TILING, i915_gem_set_tiling, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_TILING, i915_gem_get_tiling, DRM_RENDER_ALLOW),
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
};

int i915_max_ioctl = ARRAY_SIZE(i915_ioctls);
