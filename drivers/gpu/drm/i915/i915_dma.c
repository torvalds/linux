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

#include <linux/async.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_legacy.h>
#include "intel_drv.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"
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
	case I915_PARAM_HAS_GEM:
		value = 1;
		break;
	case I915_PARAM_NUM_FENCES_AVAIL:
		value = dev_priv->num_fence_regs - dev_priv->fence_reg_start;
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
		value = intel_ring_initialized(&dev_priv->ring[VCS]);
		break;
	case I915_PARAM_HAS_BLT:
		value = intel_ring_initialized(&dev_priv->ring[BCS]);
		break;
	case I915_PARAM_HAS_VEBOX:
		value = intel_ring_initialized(&dev_priv->ring[VECS]);
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
		value = i915_semaphore_is_enabled(dev);
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
		value = i915_cmd_parser_get_version();
		break;
	case I915_PARAM_HAS_COHERENT_PHYS_GTT:
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

static int i915_setparam(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	drm_i915_setparam_t *param = data;

	switch (param->param) {
	case I915_SETPARAM_USE_MI_BATCHBUFFER_START:
	case I915_SETPARAM_TEX_LRU_LOG_GRANULARITY:
	case I915_SETPARAM_ALLOW_BATCHBUFFER:
		/* Reject all old ums/dri params. */
		return -ENODEV;

	case I915_SETPARAM_NUM_USED_FENCES:
		if (param->value > dev_priv->num_fence_regs ||
		    param->value < 0)
			return -EINVAL;
		/* Userspace can use first N regs */
		dev_priv->fence_reg_start = param->value;
		break;
	default:
		DRM_DEBUG_DRIVER("unknown parameter %d\n",
					param->param);
		return -EINVAL;
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

#define MCHBAR_I915 0x44
#define MCHBAR_I965 0x48
#define MCHBAR_SIZE (4*4096)

#define DEVEN_REG 0x54
#define   DEVEN_MCHBAR_EN (1 << 28)

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

	if (IS_VALLEYVIEW(dev))
		return;

	dev_priv->mchbar_need_disable = false;

	if (IS_I915G(dev) || IS_I915GM(dev)) {
		pci_read_config_dword(dev_priv->bridge_dev, DEVEN_REG, &temp);
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
		pci_write_config_dword(dev_priv->bridge_dev, DEVEN_REG,
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
	u32 temp;

	if (dev_priv->mchbar_need_disable) {
		if (IS_I915G(dev) || IS_I915GM(dev)) {
			pci_read_config_dword(dev_priv->bridge_dev, DEVEN_REG, &temp);
			temp &= ~DEVEN_MCHBAR_EN;
			pci_write_config_dword(dev_priv->bridge_dev, DEVEN_REG, temp);
		} else {
			pci_read_config_dword(dev_priv->bridge_dev, mchbar_reg, &temp);
			temp &= ~1;
			pci_write_config_dword(dev_priv->bridge_dev, mchbar_reg, temp);
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
		i915_resume_legacy(dev);
		dev->switch_power_state = DRM_SWITCH_POWER_ON;
	} else {
		pr_err("switched off\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		i915_suspend_legacy(dev, pmm);
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

static int i915_load_modeset_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = intel_parse_bios(dev);
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

	/* Initialise stolen first so that we may reserve preallocated
	 * objects for the BIOS to KMS transition.
	 */
	ret = i915_gem_init_stolen(dev);
	if (ret)
		goto cleanup_vga_switcheroo;

	intel_power_domains_init_hw(dev_priv);

	ret = intel_irq_install(dev_priv);
	if (ret)
		goto cleanup_gem_stolen;

	/* Important: The output setup functions called by modeset_init need
	 * working irqs for e.g. gmbus and dp aux transfers. */
	intel_modeset_init(dev);

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
	async_schedule(intel_fbdev_initial_config, dev_priv);

	drm_kms_helper_poll_init(dev);

	return 0;

cleanup_gem:
	mutex_lock(&dev->struct_mutex);
	i915_gem_cleanup_ringbuffer(dev);
	i915_gem_context_fini(dev);
	mutex_unlock(&dev->struct_mutex);
cleanup_irq:
	drm_irq_uninstall(dev);
cleanup_gem_stolen:
	i915_gem_cleanup_stolen(dev);
cleanup_vga_switcheroo:
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
	bool primary;
	int ret;

	ap = alloc_apertures(1);
	if (!ap)
		return -ENOMEM;

	ap->ranges[0].base = dev_priv->gtt.mappable_base;
	ap->ranges[0].size = dev_priv->gtt.mappable_end;

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

	if (IS_VALLEYVIEW(dev) || INTEL_INFO(dev)->gen == 9)
		for_each_pipe(dev_priv, pipe)
			info->num_sprites[pipe] = 2;
	else
		for_each_pipe(dev_priv, pipe)
			info->num_sprites[pipe] = 1;

	if (i915.disable_display) {
		DRM_INFO("Display disabled (module parameter)\n");
		info->num_pipes = 0;
	} else if (info->num_pipes > 0 &&
		   (INTEL_INFO(dev)->gen == 7 || INTEL_INFO(dev)->gen == 8) &&
		   !IS_VALLEYVIEW(dev)) {
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
		}
	}
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
	struct intel_device_info *info, *device_info;
	int ret = 0, mmio_bar, mmio_size;
	uint32_t aperture_size;

	info = (struct intel_device_info *) flags;

	/* Refuse to load on gen6+ without kms enabled. */
	if (info->gen >= 6 && !drm_core_check_feature(dev, DRIVER_MODESET)) {
		DRM_INFO("Your hardware requires kernel modesetting (KMS)\n");
		DRM_INFO("See CONFIG_DRM_I915_KMS, nomodeset, and i915.modeset parameters\n");
		return -ENODEV;
	}

	/* UMS needs agp support. */
	if (!drm_core_check_feature(dev, DRIVER_MODESET) && !dev->agp)
		return -EINVAL;

	dev_priv = kzalloc(sizeof(*dev_priv), GFP_KERNEL);
	if (dev_priv == NULL)
		return -ENOMEM;

	dev->dev_private = dev_priv;
	dev_priv->dev = dev;

	/* Setup the write-once "constant" device info */
	device_info = (struct intel_device_info *)&dev_priv->info;
	memcpy(device_info, info, sizeof(dev_priv->info));
	device_info->device_id = dev->pdev->device;

	spin_lock_init(&dev_priv->irq_lock);
	spin_lock_init(&dev_priv->gpu_error.lock);
	mutex_init(&dev_priv->backlight_lock);
	spin_lock_init(&dev_priv->uncore.lock);
	spin_lock_init(&dev_priv->mm.object_stat_lock);
	spin_lock_init(&dev_priv->mmio_flip_lock);
	mutex_init(&dev_priv->dpio_lock);
	mutex_init(&dev_priv->modeset_restore_lock);

	intel_pm_setup(dev);

	intel_display_crc_init(dev);

	i915_dump_device_info(dev_priv);

	/* Not all pre-production machines fall into this category, only the
	 * very first ones. Almost everything should work, except for maybe
	 * suspend/resume. And we don't implement workarounds that affect only
	 * pre-production machines. */
	if (IS_HSW_EARLY_SDV(dev))
		DRM_INFO("This is an early pre-production Haswell machine. "
			 "It may not be fully functional.\n");

	if (i915_get_bridge_dev(dev)) {
		ret = -EIO;
		goto free_priv;
	}

	mmio_bar = IS_GEN2(dev) ? 1 : 0;
	/* Before gen4, the registers and the GTT are behind different BARs.
	 * However, from gen4 onwards, the registers and the GTT are shared
	 * in the same BAR, so we want to restrict this ioremap from
	 * clobbering the GTT which we want ioremap_wc instead. Fortunately,
	 * the register BAR remains the same size for all the earlier
	 * generations up to Ironlake.
	 */
	if (info->gen < 5)
		mmio_size = 512*1024;
	else
		mmio_size = 2*1024*1024;

	dev_priv->regs = pci_iomap(dev->pdev, mmio_bar, mmio_size);
	if (!dev_priv->regs) {
		DRM_ERROR("failed to map registers\n");
		ret = -EIO;
		goto put_bridge;
	}

	/* This must be called before any calls to HAS_PCH_* */
	intel_detect_pch(dev);

	intel_uncore_init(dev);

	ret = i915_gem_gtt_init(dev);
	if (ret)
		goto out_regs;

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		/* WARNING: Apparently we must kick fbdev drivers before vgacon,
		 * otherwise the vga fbdev driver falls over. */
		ret = i915_kick_out_firmware_fb(dev_priv);
		if (ret) {
			DRM_ERROR("failed to remove conflicting framebuffer drivers\n");
			goto out_gtt;
		}

		ret = i915_kick_out_vgacon(dev_priv);
		if (ret) {
			DRM_ERROR("failed to remove conflicting VGA console\n");
			goto out_gtt;
		}
	}

	pci_set_master(dev->pdev);

	/* overlay on gen2 is broken and can't address above 1G */
	if (IS_GEN2(dev))
		dma_set_coherent_mask(&dev->pdev->dev, DMA_BIT_MASK(30));

	/* 965GM sometimes incorrectly writes to hardware status page (HWS)
	 * using 32bit addressing, overwriting memory if HWS is located
	 * above 4GB.
	 *
	 * The documentation also mentions an issue with undefined
	 * behaviour if any general state is accessed within a page above 4GB,
	 * which also needs to be handled carefully.
	 */
	if (IS_BROADWATER(dev) || IS_CRESTLINE(dev))
		dma_set_coherent_mask(&dev->pdev->dev, DMA_BIT_MASK(32));

	aperture_size = dev_priv->gtt.mappable_end;

	dev_priv->gtt.mappable =
		io_mapping_create_wc(dev_priv->gtt.mappable_base,
				     aperture_size);
	if (dev_priv->gtt.mappable == NULL) {
		ret = -EIO;
		goto out_gtt;
	}

	dev_priv->gtt.mtrr = arch_phys_wc_add(dev_priv->gtt.mappable_base,
					      aperture_size);

	/* The i915 workqueue is primarily used for batched retirement of
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
	if (dev_priv->wq == NULL) {
		DRM_ERROR("Failed to create our workqueue.\n");
		ret = -ENOMEM;
		goto out_mtrrfree;
	}

	dev_priv->dp_wq = alloc_ordered_workqueue("i915-dp", 0);
	if (dev_priv->dp_wq == NULL) {
		DRM_ERROR("Failed to create our dp workqueue.\n");
		ret = -ENOMEM;
		goto out_freewq;
	}

	intel_irq_init(dev_priv);
	intel_uncore_sanitize(dev);

	/* Try to make sure MCHBAR is enabled before poking at it */
	intel_setup_mchbar(dev);
	intel_setup_gmbus(dev);
	intel_opregion_setup(dev);

	intel_setup_bios(dev);

	i915_gem_load(dev);

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
	if (!IS_I945G(dev) && !IS_I945GM(dev))
		pci_enable_msi(dev->pdev);

	intel_device_info_runtime_init(dev);

	if (INTEL_INFO(dev)->num_pipes) {
		ret = drm_vblank_init(dev, INTEL_INFO(dev)->num_pipes);
		if (ret)
			goto out_gem_unload;
	}

	intel_power_domains_init(dev_priv);

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = i915_load_modeset_init(dev);
		if (ret < 0) {
			DRM_ERROR("failed to init modeset\n");
			goto out_power_well;
		}
	}

	i915_setup_sysfs(dev);

	if (INTEL_INFO(dev)->num_pipes) {
		/* Must be done after probing outputs */
		intel_opregion_init(dev);
		acpi_video_register();
	}

	if (IS_GEN5(dev))
		intel_gpu_ips_init(dev_priv);

	intel_runtime_pm_enable(dev_priv);

	return 0;

out_power_well:
	intel_power_domains_fini(dev_priv);
	drm_vblank_cleanup(dev);
out_gem_unload:
	WARN_ON(unregister_oom_notifier(&dev_priv->mm.oom_notifier));
	unregister_shrinker(&dev_priv->mm.shrinker);

	if (dev->pdev->msi_enabled)
		pci_disable_msi(dev->pdev);

	intel_teardown_gmbus(dev);
	intel_teardown_mchbar(dev);
	pm_qos_remove_request(&dev_priv->pm_qos);
	destroy_workqueue(dev_priv->dp_wq);
out_freewq:
	destroy_workqueue(dev_priv->wq);
out_mtrrfree:
	arch_phys_wc_del(dev_priv->gtt.mtrr);
	io_mapping_free(dev_priv->gtt.mappable);
out_gtt:
	i915_global_gtt_cleanup(dev);
out_regs:
	intel_uncore_fini(dev);
	pci_iounmap(dev->pdev, dev_priv->regs);
put_bridge:
	pci_dev_put(dev_priv->bridge_dev);
free_priv:
	if (dev_priv->slab)
		kmem_cache_destroy(dev_priv->slab);
	kfree(dev_priv);
	return ret;
}

int i915_driver_unload(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = i915_gem_suspend(dev);
	if (ret) {
		DRM_ERROR("failed to idle hardware: %d\n", ret);
		return ret;
	}

	intel_power_domains_fini(dev_priv);

	intel_gpu_ips_teardown();

	i915_teardown_sysfs(dev);

	WARN_ON(unregister_oom_notifier(&dev_priv->mm.oom_notifier));
	unregister_shrinker(&dev_priv->mm.shrinker);

	io_mapping_free(dev_priv->gtt.mappable);
	arch_phys_wc_del(dev_priv->gtt.mtrr);

	acpi_video_unregister();

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		intel_fbdev_fini(dev);

	drm_vblank_cleanup(dev);

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
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

		vga_switcheroo_unregister_client(dev->pdev);
		vga_client_register(dev->pdev, NULL, NULL, NULL);
	}

	/* Free error state after interrupts are fully disabled. */
	del_timer_sync(&dev_priv->gpu_error.hangcheck_timer);
	cancel_work_sync(&dev_priv->gpu_error.work);
	i915_destroy_error_state(dev);

	if (dev->pdev->msi_enabled)
		pci_disable_msi(dev->pdev);

	intel_opregion_fini(dev);

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		/* Flush any outstanding unpin_work. */
		flush_workqueue(dev_priv->wq);

		mutex_lock(&dev->struct_mutex);
		i915_gem_cleanup_ringbuffer(dev);
		i915_gem_context_fini(dev);
		mutex_unlock(&dev->struct_mutex);
		i915_gem_cleanup_stolen(dev);
	}

	intel_teardown_gmbus(dev);
	intel_teardown_mchbar(dev);

	destroy_workqueue(dev_priv->dp_wq);
	destroy_workqueue(dev_priv->wq);
	pm_qos_remove_request(&dev_priv->pm_qos);

	i915_global_gtt_cleanup(dev);

	intel_uncore_fini(dev);
	if (dev_priv->regs != NULL)
		pci_iounmap(dev->pdev, dev_priv->regs);

	if (dev_priv->slab)
		kmem_cache_destroy(dev_priv->slab);

	pci_dev_put(dev_priv->bridge_dev);
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

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		intel_modeset_preclose(dev, file);
}

void i915_driver_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	if (file_priv && file_priv->bsd_ring)
		file_priv->bsd_ring = NULL;
	kfree(file_priv);
}

const struct drm_ioctl_desc i915_ioctls[] = {
	DRM_IOCTL_DEF_DRV(I915_INIT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_FLUSH, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_FLIP, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_BATCHBUFFER, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_IRQ_EMIT, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_IRQ_WAIT, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_GETPARAM, i915_getparam, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_SETPARAM, i915_setparam, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_ALLOC, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_FREE, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_INIT_HEAP, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_CMDBUFFER, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_DESTROY_HEAP,  drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_SET_VBLANK_PIPE,  drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GET_VBLANK_PIPE,  drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_VBLANK_SWAP, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_HWS_ADDR, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_INIT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER, i915_gem_execbuffer, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER2, i915_gem_execbuffer2, DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PIN, i915_gem_pin_ioctl, DRM_AUTH|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_UNPIN, i915_gem_unpin_ioctl, DRM_AUTH|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_BUSY, i915_gem_busy_ioctl, DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_CACHING, i915_gem_set_caching_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_CACHING, i915_gem_get_caching_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_THROTTLE, i915_gem_throttle_ioctl, DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_ENTERVT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_LEAVEVT, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_CREATE, i915_gem_create_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PREAD, i915_gem_pread_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_PWRITE, i915_gem_pwrite_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_MMAP, i915_gem_mmap_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_MMAP_GTT, i915_gem_mmap_gtt_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_DOMAIN, i915_gem_set_domain_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SW_FINISH, i915_gem_sw_finish_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_TILING, i915_gem_set_tiling, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_TILING, i915_gem_get_tiling, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_APERTURE, i915_gem_get_aperture_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GET_PIPE_FROM_CRTC_ID, intel_get_pipe_from_crtc_id, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_MADVISE, i915_gem_madvise_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_OVERLAY_PUT_IMAGE, intel_overlay_put_image, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_OVERLAY_ATTRS, intel_overlay_attrs, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_SET_SPRITE_COLORKEY, intel_sprite_set_colorkey, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GET_SPRITE_COLORKEY, intel_sprite_get_colorkey, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_WAIT, i915_gem_wait_ioctl, DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_CREATE, i915_gem_context_create_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_DESTROY, i915_gem_context_destroy_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_REG_READ, i915_reg_read_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GET_RESET_STATS, i915_get_reset_stats_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(I915_GEM_USERPTR, i915_gem_userptr_ioctl, DRM_UNLOCKED|DRM_RENDER_ALLOW),
};

int i915_max_ioctl = ARRAY_SIZE(i915_ioctls);

/*
 * This is really ugly: Because old userspace abused the linux agp interface to
 * manage the gtt, we need to claim that all intel devices are agp.  For
 * otherwise the drm core refuses to initialize the agp support code.
 */
int i915_driver_device_is_agp(struct drm_device *dev)
{
	return 1;
}
