/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */

#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vga_switcheroo.h>

#include <drm/drm_agpsupport.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/radeon_drm.h>

#include "radeon.h"
#include "radeon_asic.h"
#include "radeon_drv.h"
#include "radeon_kms.h"

#if defined(CONFIG_VGA_SWITCHEROO)
bool radeon_has_atpx(void);
#else
static inline bool radeon_has_atpx(void) { return false; }
#endif

/**
 * radeon_driver_unload_kms - Main unload function for KMS.
 *
 * @dev: drm dev pointer
 *
 * This is the main unload function for KMS (all asics).
 * It calls radeon_modeset_fini() to tear down the
 * displays, and radeon_device_fini() to tear down
 * the rest of the device (CP, writeback, etc.).
 * Returns 0 on success.
 */
void radeon_driver_unload_kms(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;

	if (rdev == NULL)
		return;

	if (rdev->rmmio == NULL)
		goto done_free;

	if (radeon_is_px(dev)) {
		pm_runtime_get_sync(dev->dev);
		pm_runtime_forbid(dev->dev);
	}

	radeon_acpi_fini(rdev);

	radeon_modeset_fini(rdev);
	radeon_device_fini(rdev);

	if (dev->agp)
		arch_phys_wc_del(dev->agp->agp_mtrr);
	kfree(dev->agp);
	dev->agp = NULL;

done_free:
	kfree(rdev);
	dev->dev_private = NULL;
}

/**
 * radeon_driver_load_kms - Main load function for KMS.
 *
 * @dev: drm dev pointer
 * @flags: device flags
 *
 * This is the main load function for KMS (all asics).
 * It calls radeon_device_init() to set up the non-display
 * parts of the chip (asic init, CP, writeback, etc.), and
 * radeon_modeset_init() to set up the display parts
 * (crtcs, encoders, hotplug detect, etc.).
 * Returns 0 on success, error on failure.
 */
int radeon_driver_load_kms(struct drm_device *dev, unsigned long flags)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct radeon_device *rdev;
	int r, acpi_status;

	rdev = kzalloc(sizeof(struct radeon_device), GFP_KERNEL);
	if (rdev == NULL) {
		return -ENOMEM;
	}
	dev->dev_private = (void *)rdev;

#ifdef __alpha__
	rdev->hose = pdev->sysdata;
#endif

	/* update BUS flag */
	if (pci_find_capability(pdev, PCI_CAP_ID_AGP)) {
		flags |= RADEON_IS_AGP;
	} else if (pci_is_pcie(pdev)) {
		flags |= RADEON_IS_PCIE;
	} else {
		flags |= RADEON_IS_PCI;
	}

	if ((radeon_runtime_pm != 0) &&
	    radeon_has_atpx() &&
	    ((flags & RADEON_IS_IGP) == 0) &&
	    !pci_is_thunderbolt_attached(pdev))
		flags |= RADEON_IS_PX;

	/* radeon_device_init should report only fatal error
	 * like memory allocation failure or iomapping failure,
	 * or memory manager initialization failure, it must
	 * properly initialize the GPU MC controller and permit
	 * VRAM allocation
	 */
	r = radeon_device_init(rdev, dev, pdev, flags);
	if (r) {
		dev_err(dev->dev, "Fatal error during GPU init\n");
		goto out;
	}

	/* Again modeset_init should fail only on fatal error
	 * otherwise it should provide enough functionalities
	 * for shadowfb to run
	 */
	r = radeon_modeset_init(rdev);
	if (r)
		dev_err(dev->dev, "Fatal error during modeset init\n");

	/* Call ACPI methods: require modeset init
	 * but failure is not fatal
	 */
	if (!r) {
		acpi_status = radeon_acpi_init(rdev);
		if (acpi_status)
		dev_dbg(dev->dev, "Error during ACPI methods call\n");
	}

	if (radeon_is_px(dev)) {
		dev_pm_set_driver_flags(dev->dev, DPM_FLAG_NO_DIRECT_COMPLETE);
		pm_runtime_use_autosuspend(dev->dev);
		pm_runtime_set_autosuspend_delay(dev->dev, 5000);
		pm_runtime_set_active(dev->dev);
		pm_runtime_allow(dev->dev);
		pm_runtime_mark_last_busy(dev->dev);
		pm_runtime_put_autosuspend(dev->dev);
	}

out:
	if (r)
		radeon_driver_unload_kms(dev);


	return r;
}

/**
 * radeon_set_filp_rights - Set filp right.
 *
 * @dev: drm dev pointer
 * @owner: drm file
 * @applier: drm file
 * @value: value
 *
 * Sets the filp rights for the device (all asics).
 */
static void radeon_set_filp_rights(struct drm_device *dev,
				   struct drm_file **owner,
				   struct drm_file *applier,
				   uint32_t *value)
{
	struct radeon_device *rdev = dev->dev_private;

	mutex_lock(&rdev->gem.mutex);
	if (*value == 1) {
		/* wants rights */
		if (!*owner)
			*owner = applier;
	} else if (*value == 0) {
		/* revokes rights */
		if (*owner == applier)
			*owner = NULL;
	}
	*value = *owner == applier ? 1 : 0;
	mutex_unlock(&rdev->gem.mutex);
}

/*
 * Userspace get information ioctl
 */
/**
 * radeon_info_ioctl - answer a device specific request.
 *
 * @dev: drm device pointer
 * @data: request object
 * @filp: drm filp
 *
 * This function is used to pass device specific parameters to the userspace
 * drivers.  Examples include: pci device id, pipeline parms, tiling params,
 * etc. (all asics).
 * Returns 0 on success, -EINVAL on failure.
 */
int radeon_info_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct radeon_device *rdev = dev->dev_private;
	struct drm_radeon_info *info = data;
	struct radeon_mode_info *minfo = &rdev->mode_info;
	uint32_t *value, value_tmp, *value_ptr, value_size;
	uint64_t value64;
	struct drm_crtc *crtc;
	int i, found;

	value_ptr = (uint32_t *)((unsigned long)info->value);
	value = &value_tmp;
	value_size = sizeof(uint32_t);

	switch (info->request) {
	case RADEON_INFO_DEVICE_ID:
		*value = to_pci_dev(dev->dev)->device;
		break;
	case RADEON_INFO_NUM_GB_PIPES:
		*value = rdev->num_gb_pipes;
		break;
	case RADEON_INFO_NUM_Z_PIPES:
		*value = rdev->num_z_pipes;
		break;
	case RADEON_INFO_ACCEL_WORKING:
		/* xf86-video-ati 6.13.0 relies on this being false for evergreen */
		if ((rdev->family >= CHIP_CEDAR) && (rdev->family <= CHIP_HEMLOCK))
			*value = false;
		else
			*value = rdev->accel_working;
		break;
	case RADEON_INFO_CRTC_FROM_ID:
		if (copy_from_user(value, value_ptr, sizeof(uint32_t))) {
			DRM_ERROR("copy_from_user %s:%u\n", __func__, __LINE__);
			return -EFAULT;
		}
		for (i = 0, found = 0; i < rdev->num_crtc; i++) {
			crtc = (struct drm_crtc *)minfo->crtcs[i];
			if (crtc && crtc->base.id == *value) {
				struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
				*value = radeon_crtc->crtc_id;
				found = 1;
				break;
			}
		}
		if (!found) {
			DRM_DEBUG_KMS("unknown crtc id %d\n", *value);
			return -EINVAL;
		}
		break;
	case RADEON_INFO_ACCEL_WORKING2:
		if (rdev->family == CHIP_HAWAII) {
			if (rdev->accel_working) {
				if (rdev->new_fw)
					*value = 3;
				else
					*value = 2;
			} else {
				*value = 0;
			}
		} else {
			*value = rdev->accel_working;
		}
		break;
	case RADEON_INFO_TILING_CONFIG:
		if (rdev->family >= CHIP_BONAIRE)
			*value = rdev->config.cik.tile_config;
		else if (rdev->family >= CHIP_TAHITI)
			*value = rdev->config.si.tile_config;
		else if (rdev->family >= CHIP_CAYMAN)
			*value = rdev->config.cayman.tile_config;
		else if (rdev->family >= CHIP_CEDAR)
			*value = rdev->config.evergreen.tile_config;
		else if (rdev->family >= CHIP_RV770)
			*value = rdev->config.rv770.tile_config;
		else if (rdev->family >= CHIP_R600)
			*value = rdev->config.r600.tile_config;
		else {
			DRM_DEBUG_KMS("tiling config is r6xx+ only!\n");
			return -EINVAL;
		}
		break;
	case RADEON_INFO_WANT_HYPERZ:
		/* The "value" here is both an input and output parameter.
		 * If the input value is 1, filp requests hyper-z access.
		 * If the input value is 0, filp revokes its hyper-z access.
		 *
		 * When returning, the value is 1 if filp owns hyper-z access,
		 * 0 otherwise. */
		if (copy_from_user(value, value_ptr, sizeof(uint32_t))) {
			DRM_ERROR("copy_from_user %s:%u\n", __func__, __LINE__);
			return -EFAULT;
		}
		if (*value >= 2) {
			DRM_DEBUG_KMS("WANT_HYPERZ: invalid value %d\n", *value);
			return -EINVAL;
		}
		radeon_set_filp_rights(dev, &rdev->hyperz_filp, filp, value);
		break;
	case RADEON_INFO_WANT_CMASK:
		/* The same logic as Hyper-Z. */
		if (copy_from_user(value, value_ptr, sizeof(uint32_t))) {
			DRM_ERROR("copy_from_user %s:%u\n", __func__, __LINE__);
			return -EFAULT;
		}
		if (*value >= 2) {
			DRM_DEBUG_KMS("WANT_CMASK: invalid value %d\n", *value);
			return -EINVAL;
		}
		radeon_set_filp_rights(dev, &rdev->cmask_filp, filp, value);
		break;
	case RADEON_INFO_CLOCK_CRYSTAL_FREQ:
		/* return clock value in KHz */
		if (rdev->asic->get_xclk)
			*value = radeon_get_xclk(rdev) * 10;
		else
			*value = rdev->clock.spll.reference_freq * 10;
		break;
	case RADEON_INFO_NUM_BACKENDS:
		if (rdev->family >= CHIP_BONAIRE)
			*value = rdev->config.cik.max_backends_per_se *
				rdev->config.cik.max_shader_engines;
		else if (rdev->family >= CHIP_TAHITI)
			*value = rdev->config.si.max_backends_per_se *
				rdev->config.si.max_shader_engines;
		else if (rdev->family >= CHIP_CAYMAN)
			*value = rdev->config.cayman.max_backends_per_se *
				rdev->config.cayman.max_shader_engines;
		else if (rdev->family >= CHIP_CEDAR)
			*value = rdev->config.evergreen.max_backends;
		else if (rdev->family >= CHIP_RV770)
			*value = rdev->config.rv770.max_backends;
		else if (rdev->family >= CHIP_R600)
			*value = rdev->config.r600.max_backends;
		else {
			return -EINVAL;
		}
		break;
	case RADEON_INFO_NUM_TILE_PIPES:
		if (rdev->family >= CHIP_BONAIRE)
			*value = rdev->config.cik.max_tile_pipes;
		else if (rdev->family >= CHIP_TAHITI)
			*value = rdev->config.si.max_tile_pipes;
		else if (rdev->family >= CHIP_CAYMAN)
			*value = rdev->config.cayman.max_tile_pipes;
		else if (rdev->family >= CHIP_CEDAR)
			*value = rdev->config.evergreen.max_tile_pipes;
		else if (rdev->family >= CHIP_RV770)
			*value = rdev->config.rv770.max_tile_pipes;
		else if (rdev->family >= CHIP_R600)
			*value = rdev->config.r600.max_tile_pipes;
		else {
			return -EINVAL;
		}
		break;
	case RADEON_INFO_FUSION_GART_WORKING:
		*value = 1;
		break;
	case RADEON_INFO_BACKEND_MAP:
		if (rdev->family >= CHIP_BONAIRE)
			*value = rdev->config.cik.backend_map;
		else if (rdev->family >= CHIP_TAHITI)
			*value = rdev->config.si.backend_map;
		else if (rdev->family >= CHIP_CAYMAN)
			*value = rdev->config.cayman.backend_map;
		else if (rdev->family >= CHIP_CEDAR)
			*value = rdev->config.evergreen.backend_map;
		else if (rdev->family >= CHIP_RV770)
			*value = rdev->config.rv770.backend_map;
		else if (rdev->family >= CHIP_R600)
			*value = rdev->config.r600.backend_map;
		else {
			return -EINVAL;
		}
		break;
	case RADEON_INFO_VA_START:
		/* this is where we report if vm is supported or not */
		if (rdev->family < CHIP_CAYMAN)
			return -EINVAL;
		*value = RADEON_VA_RESERVED_SIZE;
		break;
	case RADEON_INFO_IB_VM_MAX_SIZE:
		/* this is where we report if vm is supported or not */
		if (rdev->family < CHIP_CAYMAN)
			return -EINVAL;
		*value = RADEON_IB_VM_MAX_SIZE;
		break;
	case RADEON_INFO_MAX_PIPES:
		if (rdev->family >= CHIP_BONAIRE)
			*value = rdev->config.cik.max_cu_per_sh;
		else if (rdev->family >= CHIP_TAHITI)
			*value = rdev->config.si.max_cu_per_sh;
		else if (rdev->family >= CHIP_CAYMAN)
			*value = rdev->config.cayman.max_pipes_per_simd;
		else if (rdev->family >= CHIP_CEDAR)
			*value = rdev->config.evergreen.max_pipes;
		else if (rdev->family >= CHIP_RV770)
			*value = rdev->config.rv770.max_pipes;
		else if (rdev->family >= CHIP_R600)
			*value = rdev->config.r600.max_pipes;
		else {
			return -EINVAL;
		}
		break;
	case RADEON_INFO_TIMESTAMP:
		if (rdev->family < CHIP_R600) {
			DRM_DEBUG_KMS("timestamp is r6xx+ only!\n");
			return -EINVAL;
		}
		value = (uint32_t*)&value64;
		value_size = sizeof(uint64_t);
		value64 = radeon_get_gpu_clock_counter(rdev);
		break;
	case RADEON_INFO_MAX_SE:
		if (rdev->family >= CHIP_BONAIRE)
			*value = rdev->config.cik.max_shader_engines;
		else if (rdev->family >= CHIP_TAHITI)
			*value = rdev->config.si.max_shader_engines;
		else if (rdev->family >= CHIP_CAYMAN)
			*value = rdev->config.cayman.max_shader_engines;
		else if (rdev->family >= CHIP_CEDAR)
			*value = rdev->config.evergreen.num_ses;
		else
			*value = 1;
		break;
	case RADEON_INFO_MAX_SH_PER_SE:
		if (rdev->family >= CHIP_BONAIRE)
			*value = rdev->config.cik.max_sh_per_se;
		else if (rdev->family >= CHIP_TAHITI)
			*value = rdev->config.si.max_sh_per_se;
		else
			return -EINVAL;
		break;
	case RADEON_INFO_FASTFB_WORKING:
		*value = rdev->fastfb_working;
		break;
	case RADEON_INFO_RING_WORKING:
		if (copy_from_user(value, value_ptr, sizeof(uint32_t))) {
			DRM_ERROR("copy_from_user %s:%u\n", __func__, __LINE__);
			return -EFAULT;
		}
		switch (*value) {
		case RADEON_CS_RING_GFX:
		case RADEON_CS_RING_COMPUTE:
			*value = rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready;
			break;
		case RADEON_CS_RING_DMA:
			*value = rdev->ring[R600_RING_TYPE_DMA_INDEX].ready;
			*value |= rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX].ready;
			break;
		case RADEON_CS_RING_UVD:
			*value = rdev->ring[R600_RING_TYPE_UVD_INDEX].ready;
			break;
		case RADEON_CS_RING_VCE:
			*value = rdev->ring[TN_RING_TYPE_VCE1_INDEX].ready;
			break;
		default:
			return -EINVAL;
		}
		break;
	case RADEON_INFO_SI_TILE_MODE_ARRAY:
		if (rdev->family >= CHIP_BONAIRE) {
			value = rdev->config.cik.tile_mode_array;
			value_size = sizeof(uint32_t)*32;
		} else if (rdev->family >= CHIP_TAHITI) {
			value = rdev->config.si.tile_mode_array;
			value_size = sizeof(uint32_t)*32;
		} else {
			DRM_DEBUG_KMS("tile mode array is si+ only!\n");
			return -EINVAL;
		}
		break;
	case RADEON_INFO_CIK_MACROTILE_MODE_ARRAY:
		if (rdev->family >= CHIP_BONAIRE) {
			value = rdev->config.cik.macrotile_mode_array;
			value_size = sizeof(uint32_t)*16;
		} else {
			DRM_DEBUG_KMS("macrotile mode array is cik+ only!\n");
			return -EINVAL;
		}
		break;
	case RADEON_INFO_SI_CP_DMA_COMPUTE:
		*value = 1;
		break;
	case RADEON_INFO_SI_BACKEND_ENABLED_MASK:
		if (rdev->family >= CHIP_BONAIRE) {
			*value = rdev->config.cik.backend_enable_mask;
		} else if (rdev->family >= CHIP_TAHITI) {
			*value = rdev->config.si.backend_enable_mask;
		} else {
			DRM_DEBUG_KMS("BACKEND_ENABLED_MASK is si+ only!\n");
		}
		break;
	case RADEON_INFO_MAX_SCLK:
		if ((rdev->pm.pm_method == PM_METHOD_DPM) &&
		    rdev->pm.dpm_enabled)
			*value = rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.sclk * 10;
		else
			*value = rdev->pm.default_sclk * 10;
		break;
	case RADEON_INFO_VCE_FW_VERSION:
		*value = rdev->vce.fw_version;
		break;
	case RADEON_INFO_VCE_FB_VERSION:
		*value = rdev->vce.fb_version;
		break;
	case RADEON_INFO_NUM_BYTES_MOVED:
		value = (uint32_t*)&value64;
		value_size = sizeof(uint64_t);
		value64 = atomic64_read(&rdev->num_bytes_moved);
		break;
	case RADEON_INFO_VRAM_USAGE:
		value = (uint32_t*)&value64;
		value_size = sizeof(uint64_t);
		value64 = atomic64_read(&rdev->vram_usage);
		break;
	case RADEON_INFO_GTT_USAGE:
		value = (uint32_t*)&value64;
		value_size = sizeof(uint64_t);
		value64 = atomic64_read(&rdev->gtt_usage);
		break;
	case RADEON_INFO_ACTIVE_CU_COUNT:
		if (rdev->family >= CHIP_BONAIRE)
			*value = rdev->config.cik.active_cus;
		else if (rdev->family >= CHIP_TAHITI)
			*value = rdev->config.si.active_cus;
		else if (rdev->family >= CHIP_CAYMAN)
			*value = rdev->config.cayman.active_simds;
		else if (rdev->family >= CHIP_CEDAR)
			*value = rdev->config.evergreen.active_simds;
		else if (rdev->family >= CHIP_RV770)
			*value = rdev->config.rv770.active_simds;
		else if (rdev->family >= CHIP_R600)
			*value = rdev->config.r600.active_simds;
		else
			*value = 1;
		break;
	case RADEON_INFO_CURRENT_GPU_TEMP:
		/* get temperature in millidegrees C */
		if (rdev->asic->pm.get_temperature)
			*value = radeon_get_temperature(rdev);
		else
			*value = 0;
		break;
	case RADEON_INFO_CURRENT_GPU_SCLK:
		/* get sclk in Mhz */
		if (rdev->pm.dpm_enabled)
			*value = radeon_dpm_get_current_sclk(rdev) / 100;
		else
			*value = rdev->pm.current_sclk / 100;
		break;
	case RADEON_INFO_CURRENT_GPU_MCLK:
		/* get mclk in Mhz */
		if (rdev->pm.dpm_enabled)
			*value = radeon_dpm_get_current_mclk(rdev) / 100;
		else
			*value = rdev->pm.current_mclk / 100;
		break;
	case RADEON_INFO_READ_REG:
		if (copy_from_user(value, value_ptr, sizeof(uint32_t))) {
			DRM_ERROR("copy_from_user %s:%u\n", __func__, __LINE__);
			return -EFAULT;
		}
		if (radeon_get_allowed_info_register(rdev, *value, value))
			return -EINVAL;
		break;
	case RADEON_INFO_VA_UNMAP_WORKING:
		*value = true;
		break;
	case RADEON_INFO_GPU_RESET_COUNTER:
		*value = atomic_read(&rdev->gpu_reset_counter);
		break;
	default:
		DRM_DEBUG_KMS("Invalid request %d\n", info->request);
		return -EINVAL;
	}
	if (copy_to_user(value_ptr, (char*)value, value_size)) {
		DRM_ERROR("copy_to_user %s:%u\n", __func__, __LINE__);
		return -EFAULT;
	}
	return 0;
}


/*
 * Outdated mess for old drm with Xorg being in charge (void function now).
 */
/**
 * radeon_driver_lastclose_kms - drm callback for last close
 *
 * @dev: drm dev pointer
 *
 * Switch vga_switcheroo state after last close (all asics).
 */
void radeon_driver_lastclose_kms(struct drm_device *dev)
{
	drm_fb_helper_lastclose(dev);
	vga_switcheroo_process_delayed_switch();
}

/**
 * radeon_driver_open_kms - drm callback for open
 *
 * @dev: drm dev pointer
 * @file_priv: drm file
 *
 * On device open, init vm on cayman+ (all asics).
 * Returns 0 on success, error on failure.
 */
int radeon_driver_open_kms(struct drm_device *dev, struct drm_file *file_priv)
{
	struct radeon_device *rdev = dev->dev_private;
	int r;

	file_priv->driver_priv = NULL;

	r = pm_runtime_get_sync(dev->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(dev->dev);
		return r;
	}

	/* new gpu have virtual address space support */
	if (rdev->family >= CHIP_CAYMAN) {
		struct radeon_fpriv *fpriv;
		struct radeon_vm *vm;

		fpriv = kzalloc(sizeof(*fpriv), GFP_KERNEL);
		if (unlikely(!fpriv)) {
			r = -ENOMEM;
			goto out_suspend;
		}

		if (rdev->accel_working) {
			vm = &fpriv->vm;
			r = radeon_vm_init(rdev, vm);
			if (r) {
				kfree(fpriv);
				goto out_suspend;
			}

			r = radeon_bo_reserve(rdev->ring_tmp_bo.bo, false);
			if (r) {
				radeon_vm_fini(rdev, vm);
				kfree(fpriv);
				goto out_suspend;
			}

			/* map the ib pool buffer read only into
			 * virtual address space */
			vm->ib_bo_va = radeon_vm_bo_add(rdev, vm,
							rdev->ring_tmp_bo.bo);
			r = radeon_vm_bo_set_addr(rdev, vm->ib_bo_va,
						  RADEON_VA_IB_OFFSET,
						  RADEON_VM_PAGE_READABLE |
						  RADEON_VM_PAGE_SNOOPED);
			if (r) {
				radeon_vm_fini(rdev, vm);
				kfree(fpriv);
				goto out_suspend;
			}
		}
		file_priv->driver_priv = fpriv;
	}

out_suspend:
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
	return r;
}

/**
 * radeon_driver_postclose_kms - drm callback for post close
 *
 * @dev: drm dev pointer
 * @file_priv: drm file
 *
 * On device close, tear down hyperz and cmask filps on r1xx-r5xx
 * (all asics).  And tear down vm on cayman+ (all asics).
 */
void radeon_driver_postclose_kms(struct drm_device *dev,
				 struct drm_file *file_priv)
{
	struct radeon_device *rdev = dev->dev_private;

	pm_runtime_get_sync(dev->dev);

	mutex_lock(&rdev->gem.mutex);
	if (rdev->hyperz_filp == file_priv)
		rdev->hyperz_filp = NULL;
	if (rdev->cmask_filp == file_priv)
		rdev->cmask_filp = NULL;
	mutex_unlock(&rdev->gem.mutex);

	radeon_uvd_free_handles(rdev, file_priv);
	radeon_vce_free_handles(rdev, file_priv);

	/* new gpu have virtual address space support */
	if (rdev->family >= CHIP_CAYMAN && file_priv->driver_priv) {
		struct radeon_fpriv *fpriv = file_priv->driver_priv;
		struct radeon_vm *vm = &fpriv->vm;
		int r;

		if (rdev->accel_working) {
			r = radeon_bo_reserve(rdev->ring_tmp_bo.bo, false);
			if (!r) {
				if (vm->ib_bo_va)
					radeon_vm_bo_rmv(rdev, vm->ib_bo_va);
				radeon_bo_unreserve(rdev->ring_tmp_bo.bo);
			}
			radeon_vm_fini(rdev, vm);
		}

		kfree(fpriv);
		file_priv->driver_priv = NULL;
	}
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
}

/*
 * VBlank related functions.
 */
/**
 * radeon_get_vblank_counter_kms - get frame count
 *
 * @crtc: crtc to get the frame count from
 *
 * Gets the frame count on the requested crtc (all asics).
 * Returns frame count on success, -EINVAL on failure.
 */
u32 radeon_get_vblank_counter_kms(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = crtc->index;
	int vpos, hpos, stat;
	u32 count;
	struct radeon_device *rdev = dev->dev_private;

	if (pipe >= rdev->num_crtc) {
		DRM_ERROR("Invalid crtc %u\n", pipe);
		return -EINVAL;
	}

	/* The hw increments its frame counter at start of vsync, not at start
	 * of vblank, as is required by DRM core vblank counter handling.
	 * Cook the hw count here to make it appear to the caller as if it
	 * incremented at start of vblank. We measure distance to start of
	 * vblank in vpos. vpos therefore will be >= 0 between start of vblank
	 * and start of vsync, so vpos >= 0 means to bump the hw frame counter
	 * result by 1 to give the proper appearance to caller.
	 */
	if (rdev->mode_info.crtcs[pipe]) {
		/* Repeat readout if needed to provide stable result if
		 * we cross start of vsync during the queries.
		 */
		do {
			count = radeon_get_vblank_counter(rdev, pipe);
			/* Ask radeon_get_crtc_scanoutpos to return vpos as
			 * distance to start of vblank, instead of regular
			 * vertical scanout pos.
			 */
			stat = radeon_get_crtc_scanoutpos(
				dev, pipe, GET_DISTANCE_TO_VBLANKSTART,
				&vpos, &hpos, NULL, NULL,
				&rdev->mode_info.crtcs[pipe]->base.hwmode);
		} while (count != radeon_get_vblank_counter(rdev, pipe));

		if (((stat & (DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_ACCURATE)) !=
		    (DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_ACCURATE))) {
			DRM_DEBUG_VBL("Query failed! stat %d\n", stat);
		}
		else {
			DRM_DEBUG_VBL("crtc %u: dist from vblank start %d\n",
				      pipe, vpos);

			/* Bump counter if we are at >= leading edge of vblank,
			 * but before vsync where vpos would turn negative and
			 * the hw counter really increments.
			 */
			if (vpos >= 0)
				count++;
		}
	}
	else {
	    /* Fallback to use value as is. */
	    count = radeon_get_vblank_counter(rdev, pipe);
	    DRM_DEBUG_VBL("NULL mode info! Returned count may be wrong.\n");
	}

	return count;
}

/**
 * radeon_enable_vblank_kms - enable vblank interrupt
 *
 * @crtc: crtc to enable vblank interrupt for
 *
 * Enable the interrupt on the requested crtc (all asics).
 * Returns 0 on success, -EINVAL on failure.
 */
int radeon_enable_vblank_kms(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = crtc->index;
	struct radeon_device *rdev = dev->dev_private;
	unsigned long irqflags;
	int r;

	if (pipe >= rdev->num_crtc) {
		DRM_ERROR("Invalid crtc %d\n", pipe);
		return -EINVAL;
	}

	spin_lock_irqsave(&rdev->irq.lock, irqflags);
	rdev->irq.crtc_vblank_int[pipe] = true;
	r = radeon_irq_set(rdev);
	spin_unlock_irqrestore(&rdev->irq.lock, irqflags);
	return r;
}

/**
 * radeon_disable_vblank_kms - disable vblank interrupt
 *
 * @crtc: crtc to disable vblank interrupt for
 *
 * Disable the interrupt on the requested crtc (all asics).
 */
void radeon_disable_vblank_kms(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = crtc->index;
	struct radeon_device *rdev = dev->dev_private;
	unsigned long irqflags;

	if (pipe >= rdev->num_crtc) {
		DRM_ERROR("Invalid crtc %d\n", pipe);
		return;
	}

	spin_lock_irqsave(&rdev->irq.lock, irqflags);
	rdev->irq.crtc_vblank_int[pipe] = false;
	radeon_irq_set(rdev);
	spin_unlock_irqrestore(&rdev->irq.lock, irqflags);
}
