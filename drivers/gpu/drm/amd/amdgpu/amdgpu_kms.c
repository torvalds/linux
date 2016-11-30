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
#include <drm/drmP.h>
#include "amdgpu.h"
#include <drm/amdgpu_drm.h>
#include "amdgpu_uvd.h"
#include "amdgpu_vce.h"

#include <linux/vga_switcheroo.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include "amdgpu_amdkfd.h"

#if defined(CONFIG_VGA_SWITCHEROO)
bool amdgpu_has_atpx(void);
#else
static inline bool amdgpu_has_atpx(void) { return false; }
#endif

/**
 * amdgpu_driver_unload_kms - Main unload function for KMS.
 *
 * @dev: drm dev pointer
 *
 * This is the main unload function for KMS (all asics).
 * Returns 0 on success.
 */
int amdgpu_driver_unload_kms(struct drm_device *dev)
{
	struct amdgpu_device *adev = dev->dev_private;

	if (adev == NULL)
		return 0;

	if (adev->rmmio == NULL)
		goto done_free;

	if (amdgpu_device_is_px(dev)) {
		pm_runtime_get_sync(dev->dev);
		pm_runtime_forbid(dev->dev);
	}

	amdgpu_amdkfd_device_fini(adev);

	amdgpu_acpi_fini(adev);

	amdgpu_device_fini(adev);

done_free:
	kfree(adev);
	dev->dev_private = NULL;
	return 0;
}

/**
 * amdgpu_driver_load_kms - Main load function for KMS.
 *
 * @dev: drm dev pointer
 * @flags: device flags
 *
 * This is the main load function for KMS (all asics).
 * Returns 0 on success, error on failure.
 */
int amdgpu_driver_load_kms(struct drm_device *dev, unsigned long flags)
{
	struct amdgpu_device *adev;
	int r, acpi_status;

	adev = kzalloc(sizeof(struct amdgpu_device), GFP_KERNEL);
	if (adev == NULL) {
		return -ENOMEM;
	}
	dev->dev_private = (void *)adev;

	if ((amdgpu_runtime_pm != 0) &&
	    amdgpu_has_atpx() &&
	    (amdgpu_is_atpx_hybrid() ||
	     amdgpu_has_atpx_dgpu_power_cntl()) &&
	    ((flags & AMD_IS_APU) == 0))
		flags |= AMD_IS_PX;

	/* amdgpu_device_init should report only fatal error
	 * like memory allocation failure or iomapping failure,
	 * or memory manager initialization failure, it must
	 * properly initialize the GPU MC controller and permit
	 * VRAM allocation
	 */
	r = amdgpu_device_init(adev, dev, dev->pdev, flags);
	if (r) {
		dev_err(&dev->pdev->dev, "Fatal error during GPU init\n");
		goto out;
	}

	/* Call ACPI methods: require modeset init
	 * but failure is not fatal
	 */
	if (!r) {
		acpi_status = amdgpu_acpi_init(adev);
		if (acpi_status)
		dev_dbg(&dev->pdev->dev,
				"Error during ACPI methods call\n");
	}

	amdgpu_amdkfd_load_interface(adev);
	amdgpu_amdkfd_device_probe(adev);
	amdgpu_amdkfd_device_init(adev);

	if (amdgpu_device_is_px(dev)) {
		pm_runtime_use_autosuspend(dev->dev);
		pm_runtime_set_autosuspend_delay(dev->dev, 5000);
		pm_runtime_set_active(dev->dev);
		pm_runtime_allow(dev->dev);
		pm_runtime_mark_last_busy(dev->dev);
		pm_runtime_put_autosuspend(dev->dev);
	}

out:
	if (r) {
		/* balance pm_runtime_get_sync in amdgpu_driver_unload_kms */
		if (adev->rmmio && amdgpu_device_is_px(dev))
			pm_runtime_put_noidle(dev->dev);
		amdgpu_driver_unload_kms(dev);
	}

	return r;
}

static int amdgpu_firmware_info(struct drm_amdgpu_info_firmware *fw_info,
				struct drm_amdgpu_query_fw *query_fw,
				struct amdgpu_device *adev)
{
	switch (query_fw->fw_type) {
	case AMDGPU_INFO_FW_VCE:
		fw_info->ver = adev->vce.fw_version;
		fw_info->feature = adev->vce.fb_version;
		break;
	case AMDGPU_INFO_FW_UVD:
		fw_info->ver = adev->uvd.fw_version;
		fw_info->feature = 0;
		break;
	case AMDGPU_INFO_FW_GMC:
		fw_info->ver = adev->mc.fw_version;
		fw_info->feature = 0;
		break;
	case AMDGPU_INFO_FW_GFX_ME:
		fw_info->ver = adev->gfx.me_fw_version;
		fw_info->feature = adev->gfx.me_feature_version;
		break;
	case AMDGPU_INFO_FW_GFX_PFP:
		fw_info->ver = adev->gfx.pfp_fw_version;
		fw_info->feature = adev->gfx.pfp_feature_version;
		break;
	case AMDGPU_INFO_FW_GFX_CE:
		fw_info->ver = adev->gfx.ce_fw_version;
		fw_info->feature = adev->gfx.ce_feature_version;
		break;
	case AMDGPU_INFO_FW_GFX_RLC:
		fw_info->ver = adev->gfx.rlc_fw_version;
		fw_info->feature = adev->gfx.rlc_feature_version;
		break;
	case AMDGPU_INFO_FW_GFX_MEC:
		if (query_fw->index == 0) {
			fw_info->ver = adev->gfx.mec_fw_version;
			fw_info->feature = adev->gfx.mec_feature_version;
		} else if (query_fw->index == 1) {
			fw_info->ver = adev->gfx.mec2_fw_version;
			fw_info->feature = adev->gfx.mec2_feature_version;
		} else
			return -EINVAL;
		break;
	case AMDGPU_INFO_FW_SMC:
		fw_info->ver = adev->pm.fw_version;
		fw_info->feature = 0;
		break;
	case AMDGPU_INFO_FW_SDMA:
		if (query_fw->index >= adev->sdma.num_instances)
			return -EINVAL;
		fw_info->ver = adev->sdma.instance[query_fw->index].fw_version;
		fw_info->feature = adev->sdma.instance[query_fw->index].feature_version;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * Userspace get information ioctl
 */
/**
 * amdgpu_info_ioctl - answer a device specific request.
 *
 * @adev: amdgpu device pointer
 * @data: request object
 * @filp: drm filp
 *
 * This function is used to pass device specific parameters to the userspace
 * drivers.  Examples include: pci device id, pipeline parms, tiling params,
 * etc. (all asics).
 * Returns 0 on success, -EINVAL on failure.
 */
static int amdgpu_info_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct drm_amdgpu_info *info = data;
	struct amdgpu_mode_info *minfo = &adev->mode_info;
	void __user *out = (void __user *)(long)info->return_pointer;
	uint32_t size = info->return_size;
	struct drm_crtc *crtc;
	uint32_t ui32 = 0;
	uint64_t ui64 = 0;
	int i, found;

	if (!info->return_size || !info->return_pointer)
		return -EINVAL;

	switch (info->query) {
	case AMDGPU_INFO_ACCEL_WORKING:
		ui32 = adev->accel_working;
		return copy_to_user(out, &ui32, min(size, 4u)) ? -EFAULT : 0;
	case AMDGPU_INFO_CRTC_FROM_ID:
		for (i = 0, found = 0; i < adev->mode_info.num_crtc; i++) {
			crtc = (struct drm_crtc *)minfo->crtcs[i];
			if (crtc && crtc->base.id == info->mode_crtc.id) {
				struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
				ui32 = amdgpu_crtc->crtc_id;
				found = 1;
				break;
			}
		}
		if (!found) {
			DRM_DEBUG_KMS("unknown crtc id %d\n", info->mode_crtc.id);
			return -EINVAL;
		}
		return copy_to_user(out, &ui32, min(size, 4u)) ? -EFAULT : 0;
	case AMDGPU_INFO_HW_IP_INFO: {
		struct drm_amdgpu_info_hw_ip ip = {};
		enum amd_ip_block_type type;
		uint32_t ring_mask = 0;
		uint32_t ib_start_alignment = 0;
		uint32_t ib_size_alignment = 0;

		if (info->query_hw_ip.ip_instance >= AMDGPU_HW_IP_INSTANCE_MAX_COUNT)
			return -EINVAL;

		switch (info->query_hw_ip.type) {
		case AMDGPU_HW_IP_GFX:
			type = AMD_IP_BLOCK_TYPE_GFX;
			for (i = 0; i < adev->gfx.num_gfx_rings; i++)
				ring_mask |= ((adev->gfx.gfx_ring[i].ready ? 1 : 0) << i);
			ib_start_alignment = AMDGPU_GPU_PAGE_SIZE;
			ib_size_alignment = 8;
			break;
		case AMDGPU_HW_IP_COMPUTE:
			type = AMD_IP_BLOCK_TYPE_GFX;
			for (i = 0; i < adev->gfx.num_compute_rings; i++)
				ring_mask |= ((adev->gfx.compute_ring[i].ready ? 1 : 0) << i);
			ib_start_alignment = AMDGPU_GPU_PAGE_SIZE;
			ib_size_alignment = 8;
			break;
		case AMDGPU_HW_IP_DMA:
			type = AMD_IP_BLOCK_TYPE_SDMA;
			for (i = 0; i < adev->sdma.num_instances; i++)
				ring_mask |= ((adev->sdma.instance[i].ring.ready ? 1 : 0) << i);
			ib_start_alignment = AMDGPU_GPU_PAGE_SIZE;
			ib_size_alignment = 1;
			break;
		case AMDGPU_HW_IP_UVD:
			type = AMD_IP_BLOCK_TYPE_UVD;
			ring_mask = adev->uvd.ring.ready ? 1 : 0;
			ib_start_alignment = AMDGPU_GPU_PAGE_SIZE;
			ib_size_alignment = 16;
			break;
		case AMDGPU_HW_IP_VCE:
			type = AMD_IP_BLOCK_TYPE_VCE;
			for (i = 0; i < adev->vce.num_rings; i++)
				ring_mask |= ((adev->vce.ring[i].ready ? 1 : 0) << i);
			ib_start_alignment = AMDGPU_GPU_PAGE_SIZE;
			ib_size_alignment = 1;
			break;
		default:
			return -EINVAL;
		}

		for (i = 0; i < adev->num_ip_blocks; i++) {
			if (adev->ip_blocks[i].version->type == type &&
			    adev->ip_blocks[i].status.valid) {
				ip.hw_ip_version_major = adev->ip_blocks[i].version->major;
				ip.hw_ip_version_minor = adev->ip_blocks[i].version->minor;
				ip.capabilities_flags = 0;
				ip.available_rings = ring_mask;
				ip.ib_start_alignment = ib_start_alignment;
				ip.ib_size_alignment = ib_size_alignment;
				break;
			}
		}
		return copy_to_user(out, &ip,
				    min((size_t)size, sizeof(ip))) ? -EFAULT : 0;
	}
	case AMDGPU_INFO_HW_IP_COUNT: {
		enum amd_ip_block_type type;
		uint32_t count = 0;

		switch (info->query_hw_ip.type) {
		case AMDGPU_HW_IP_GFX:
			type = AMD_IP_BLOCK_TYPE_GFX;
			break;
		case AMDGPU_HW_IP_COMPUTE:
			type = AMD_IP_BLOCK_TYPE_GFX;
			break;
		case AMDGPU_HW_IP_DMA:
			type = AMD_IP_BLOCK_TYPE_SDMA;
			break;
		case AMDGPU_HW_IP_UVD:
			type = AMD_IP_BLOCK_TYPE_UVD;
			break;
		case AMDGPU_HW_IP_VCE:
			type = AMD_IP_BLOCK_TYPE_VCE;
			break;
		default:
			return -EINVAL;
		}

		for (i = 0; i < adev->num_ip_blocks; i++)
			if (adev->ip_blocks[i].version->type == type &&
			    adev->ip_blocks[i].status.valid &&
			    count < AMDGPU_HW_IP_INSTANCE_MAX_COUNT)
				count++;

		return copy_to_user(out, &count, min(size, 4u)) ? -EFAULT : 0;
	}
	case AMDGPU_INFO_TIMESTAMP:
		ui64 = amdgpu_gfx_get_gpu_clock_counter(adev);
		return copy_to_user(out, &ui64, min(size, 8u)) ? -EFAULT : 0;
	case AMDGPU_INFO_FW_VERSION: {
		struct drm_amdgpu_info_firmware fw_info;
		int ret;

		/* We only support one instance of each IP block right now. */
		if (info->query_fw.ip_instance != 0)
			return -EINVAL;

		ret = amdgpu_firmware_info(&fw_info, &info->query_fw, adev);
		if (ret)
			return ret;

		return copy_to_user(out, &fw_info,
				    min((size_t)size, sizeof(fw_info))) ? -EFAULT : 0;
	}
	case AMDGPU_INFO_NUM_BYTES_MOVED:
		ui64 = atomic64_read(&adev->num_bytes_moved);
		return copy_to_user(out, &ui64, min(size, 8u)) ? -EFAULT : 0;
	case AMDGPU_INFO_NUM_EVICTIONS:
		ui64 = atomic64_read(&adev->num_evictions);
		return copy_to_user(out, &ui64, min(size, 8u)) ? -EFAULT : 0;
	case AMDGPU_INFO_VRAM_USAGE:
		ui64 = atomic64_read(&adev->vram_usage);
		return copy_to_user(out, &ui64, min(size, 8u)) ? -EFAULT : 0;
	case AMDGPU_INFO_VIS_VRAM_USAGE:
		ui64 = atomic64_read(&adev->vram_vis_usage);
		return copy_to_user(out, &ui64, min(size, 8u)) ? -EFAULT : 0;
	case AMDGPU_INFO_GTT_USAGE:
		ui64 = atomic64_read(&adev->gtt_usage);
		return copy_to_user(out, &ui64, min(size, 8u)) ? -EFAULT : 0;
	case AMDGPU_INFO_GDS_CONFIG: {
		struct drm_amdgpu_info_gds gds_info;

		memset(&gds_info, 0, sizeof(gds_info));
		gds_info.gds_gfx_partition_size = adev->gds.mem.gfx_partition_size >> AMDGPU_GDS_SHIFT;
		gds_info.compute_partition_size = adev->gds.mem.cs_partition_size >> AMDGPU_GDS_SHIFT;
		gds_info.gds_total_size = adev->gds.mem.total_size >> AMDGPU_GDS_SHIFT;
		gds_info.gws_per_gfx_partition = adev->gds.gws.gfx_partition_size >> AMDGPU_GWS_SHIFT;
		gds_info.gws_per_compute_partition = adev->gds.gws.cs_partition_size >> AMDGPU_GWS_SHIFT;
		gds_info.oa_per_gfx_partition = adev->gds.oa.gfx_partition_size >> AMDGPU_OA_SHIFT;
		gds_info.oa_per_compute_partition = adev->gds.oa.cs_partition_size >> AMDGPU_OA_SHIFT;
		return copy_to_user(out, &gds_info,
				    min((size_t)size, sizeof(gds_info))) ? -EFAULT : 0;
	}
	case AMDGPU_INFO_VRAM_GTT: {
		struct drm_amdgpu_info_vram_gtt vram_gtt;

		vram_gtt.vram_size = adev->mc.real_vram_size;
		vram_gtt.vram_size -= adev->vram_pin_size;
		vram_gtt.vram_cpu_accessible_size = adev->mc.visible_vram_size;
		vram_gtt.vram_cpu_accessible_size -= (adev->vram_pin_size - adev->invisible_pin_size);
		vram_gtt.gtt_size  = adev->mc.gtt_size;
		vram_gtt.gtt_size -= adev->gart_pin_size;
		return copy_to_user(out, &vram_gtt,
				    min((size_t)size, sizeof(vram_gtt))) ? -EFAULT : 0;
	}
	case AMDGPU_INFO_MEMORY: {
		struct drm_amdgpu_memory_info mem;

		memset(&mem, 0, sizeof(mem));
		mem.vram.total_heap_size = adev->mc.real_vram_size;
		mem.vram.usable_heap_size =
			adev->mc.real_vram_size - adev->vram_pin_size;
		mem.vram.heap_usage = atomic64_read(&adev->vram_usage);
		mem.vram.max_allocation = mem.vram.usable_heap_size * 3 / 4;

		mem.cpu_accessible_vram.total_heap_size =
			adev->mc.visible_vram_size;
		mem.cpu_accessible_vram.usable_heap_size =
			adev->mc.visible_vram_size -
			(adev->vram_pin_size - adev->invisible_pin_size);
		mem.cpu_accessible_vram.heap_usage =
			atomic64_read(&adev->vram_vis_usage);
		mem.cpu_accessible_vram.max_allocation =
			mem.cpu_accessible_vram.usable_heap_size * 3 / 4;

		mem.gtt.total_heap_size = adev->mc.gtt_size;
		mem.gtt.usable_heap_size =
			adev->mc.gtt_size - adev->gart_pin_size;
		mem.gtt.heap_usage = atomic64_read(&adev->gtt_usage);
		mem.gtt.max_allocation = mem.gtt.usable_heap_size * 3 / 4;

		return copy_to_user(out, &mem,
				    min((size_t)size, sizeof(mem)))
				    ? -EFAULT : 0;
	}
	case AMDGPU_INFO_READ_MMR_REG: {
		unsigned n, alloc_size;
		uint32_t *regs;
		unsigned se_num = (info->read_mmr_reg.instance >>
				   AMDGPU_INFO_MMR_SE_INDEX_SHIFT) &
				  AMDGPU_INFO_MMR_SE_INDEX_MASK;
		unsigned sh_num = (info->read_mmr_reg.instance >>
				   AMDGPU_INFO_MMR_SH_INDEX_SHIFT) &
				  AMDGPU_INFO_MMR_SH_INDEX_MASK;

		/* set full masks if the userspace set all bits
		 * in the bitfields */
		if (se_num == AMDGPU_INFO_MMR_SE_INDEX_MASK)
			se_num = 0xffffffff;
		if (sh_num == AMDGPU_INFO_MMR_SH_INDEX_MASK)
			sh_num = 0xffffffff;

		regs = kmalloc_array(info->read_mmr_reg.count, sizeof(*regs), GFP_KERNEL);
		if (!regs)
			return -ENOMEM;
		alloc_size = info->read_mmr_reg.count * sizeof(*regs);

		for (i = 0; i < info->read_mmr_reg.count; i++)
			if (amdgpu_asic_read_register(adev, se_num, sh_num,
						      info->read_mmr_reg.dword_offset + i,
						      &regs[i])) {
				DRM_DEBUG_KMS("unallowed offset %#x\n",
					      info->read_mmr_reg.dword_offset + i);
				kfree(regs);
				return -EFAULT;
			}
		n = copy_to_user(out, regs, min(size, alloc_size));
		kfree(regs);
		return n ? -EFAULT : 0;
	}
	case AMDGPU_INFO_DEV_INFO: {
		struct drm_amdgpu_info_device dev_info = {};

		dev_info.device_id = dev->pdev->device;
		dev_info.chip_rev = adev->rev_id;
		dev_info.external_rev = adev->external_rev_id;
		dev_info.pci_rev = dev->pdev->revision;
		dev_info.family = adev->family;
		dev_info.num_shader_engines = adev->gfx.config.max_shader_engines;
		dev_info.num_shader_arrays_per_engine = adev->gfx.config.max_sh_per_se;
		/* return all clocks in KHz */
		dev_info.gpu_counter_freq = amdgpu_asic_get_xclk(adev) * 10;
		if (adev->pm.dpm_enabled) {
			dev_info.max_engine_clock = amdgpu_dpm_get_sclk(adev, false) * 10;
			dev_info.max_memory_clock = amdgpu_dpm_get_mclk(adev, false) * 10;
		} else {
			dev_info.max_engine_clock = adev->pm.default_sclk * 10;
			dev_info.max_memory_clock = adev->pm.default_mclk * 10;
		}
		dev_info.enabled_rb_pipes_mask = adev->gfx.config.backend_enable_mask;
		dev_info.num_rb_pipes = adev->gfx.config.max_backends_per_se *
			adev->gfx.config.max_shader_engines;
		dev_info.num_hw_gfx_contexts = adev->gfx.config.max_hw_contexts;
		dev_info._pad = 0;
		dev_info.ids_flags = 0;
		if (adev->flags & AMD_IS_APU)
			dev_info.ids_flags |= AMDGPU_IDS_FLAGS_FUSION;
		if (amdgpu_sriov_vf(adev))
			dev_info.ids_flags |= AMDGPU_IDS_FLAGS_PREEMPTION;
		dev_info.virtual_address_offset = AMDGPU_VA_RESERVED_SIZE;
		dev_info.virtual_address_max = (uint64_t)adev->vm_manager.max_pfn * AMDGPU_GPU_PAGE_SIZE;
		dev_info.virtual_address_alignment = max((int)PAGE_SIZE, AMDGPU_GPU_PAGE_SIZE);
		dev_info.pte_fragment_size = (1 << AMDGPU_LOG2_PAGES_PER_FRAG) *
					     AMDGPU_GPU_PAGE_SIZE;
		dev_info.gart_page_size = AMDGPU_GPU_PAGE_SIZE;

		dev_info.cu_active_number = adev->gfx.cu_info.number;
		dev_info.cu_ao_mask = adev->gfx.cu_info.ao_cu_mask;
		dev_info.ce_ram_size = adev->gfx.ce_ram_size;
		memcpy(&dev_info.cu_bitmap[0], &adev->gfx.cu_info.bitmap[0],
		       sizeof(adev->gfx.cu_info.bitmap));
		dev_info.vram_type = adev->mc.vram_type;
		dev_info.vram_bit_width = adev->mc.vram_width;
		dev_info.vce_harvest_config = adev->vce.harvest_config;

		return copy_to_user(out, &dev_info,
				    min((size_t)size, sizeof(dev_info))) ? -EFAULT : 0;
	}
	case AMDGPU_INFO_VCE_CLOCK_TABLE: {
		unsigned i;
		struct drm_amdgpu_info_vce_clock_table vce_clk_table = {};
		struct amd_vce_state *vce_state;

		for (i = 0; i < AMDGPU_VCE_CLOCK_TABLE_ENTRIES; i++) {
			vce_state = amdgpu_dpm_get_vce_clock_state(adev, i);
			if (vce_state) {
				vce_clk_table.entries[i].sclk = vce_state->sclk;
				vce_clk_table.entries[i].mclk = vce_state->mclk;
				vce_clk_table.entries[i].eclk = vce_state->evclk;
				vce_clk_table.num_valid_entries++;
			}
		}

		return copy_to_user(out, &vce_clk_table,
				    min((size_t)size, sizeof(vce_clk_table))) ? -EFAULT : 0;
	}
	default:
		DRM_DEBUG_KMS("Invalid request %d\n", info->query);
		return -EINVAL;
	}
	return 0;
}


/*
 * Outdated mess for old drm with Xorg being in charge (void function now).
 */
/**
 * amdgpu_driver_lastclose_kms - drm callback for last close
 *
 * @dev: drm dev pointer
 *
 * Switch vga_switcheroo state after last close (all asics).
 */
void amdgpu_driver_lastclose_kms(struct drm_device *dev)
{
	struct amdgpu_device *adev = dev->dev_private;

	amdgpu_fbdev_restore_mode(adev);
	vga_switcheroo_process_delayed_switch();
}

/**
 * amdgpu_driver_open_kms - drm callback for open
 *
 * @dev: drm dev pointer
 * @file_priv: drm file
 *
 * On device open, init vm on cayman+ (all asics).
 * Returns 0 on success, error on failure.
 */
int amdgpu_driver_open_kms(struct drm_device *dev, struct drm_file *file_priv)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_fpriv *fpriv;
	int r;

	file_priv->driver_priv = NULL;

	r = pm_runtime_get_sync(dev->dev);
	if (r < 0)
		return r;

	fpriv = kzalloc(sizeof(*fpriv), GFP_KERNEL);
	if (unlikely(!fpriv)) {
		r = -ENOMEM;
		goto out_suspend;
	}

	r = amdgpu_vm_init(adev, &fpriv->vm);
	if (r) {
		kfree(fpriv);
		goto out_suspend;
	}

	mutex_init(&fpriv->bo_list_lock);
	idr_init(&fpriv->bo_list_handles);

	amdgpu_ctx_mgr_init(&fpriv->ctx_mgr);

	file_priv->driver_priv = fpriv;

out_suspend:
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return r;
}

/**
 * amdgpu_driver_postclose_kms - drm callback for post close
 *
 * @dev: drm dev pointer
 * @file_priv: drm file
 *
 * On device post close, tear down vm on cayman+ (all asics).
 */
void amdgpu_driver_postclose_kms(struct drm_device *dev,
				 struct drm_file *file_priv)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_fpriv *fpriv = file_priv->driver_priv;
	struct amdgpu_bo_list *list;
	int handle;

	if (!fpriv)
		return;

	amdgpu_ctx_mgr_fini(&fpriv->ctx_mgr);

	amdgpu_uvd_free_handles(adev, file_priv);
	amdgpu_vce_free_handles(adev, file_priv);

	amdgpu_vm_fini(adev, &fpriv->vm);

	idr_for_each_entry(&fpriv->bo_list_handles, list, handle)
		amdgpu_bo_list_free(list);

	idr_destroy(&fpriv->bo_list_handles);
	mutex_destroy(&fpriv->bo_list_lock);

	kfree(fpriv);
	file_priv->driver_priv = NULL;

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
}

/**
 * amdgpu_driver_preclose_kms - drm callback for pre close
 *
 * @dev: drm dev pointer
 * @file_priv: drm file
 *
 * On device pre close, tear down hyperz and cmask filps on r1xx-r5xx
 * (all asics).
 */
void amdgpu_driver_preclose_kms(struct drm_device *dev,
				struct drm_file *file_priv)
{
	pm_runtime_get_sync(dev->dev);
}

/*
 * VBlank related functions.
 */
/**
 * amdgpu_get_vblank_counter_kms - get frame count
 *
 * @dev: drm dev pointer
 * @pipe: crtc to get the frame count from
 *
 * Gets the frame count on the requested crtc (all asics).
 * Returns frame count on success, -EINVAL on failure.
 */
u32 amdgpu_get_vblank_counter_kms(struct drm_device *dev, unsigned int pipe)
{
	struct amdgpu_device *adev = dev->dev_private;
	int vpos, hpos, stat;
	u32 count;

	if (pipe >= adev->mode_info.num_crtc) {
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
	if (adev->mode_info.crtcs[pipe]) {
		/* Repeat readout if needed to provide stable result if
		 * we cross start of vsync during the queries.
		 */
		do {
			count = amdgpu_display_vblank_get_counter(adev, pipe);
			/* Ask amdgpu_get_crtc_scanoutpos to return vpos as
			 * distance to start of vblank, instead of regular
			 * vertical scanout pos.
			 */
			stat = amdgpu_get_crtc_scanoutpos(
				dev, pipe, GET_DISTANCE_TO_VBLANKSTART,
				&vpos, &hpos, NULL, NULL,
				&adev->mode_info.crtcs[pipe]->base.hwmode);
		} while (count != amdgpu_display_vblank_get_counter(adev, pipe));

		if (((stat & (DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_ACCURATE)) !=
		    (DRM_SCANOUTPOS_VALID | DRM_SCANOUTPOS_ACCURATE))) {
			DRM_DEBUG_VBL("Query failed! stat %d\n", stat);
		} else {
			DRM_DEBUG_VBL("crtc %d: dist from vblank start %d\n",
				      pipe, vpos);

			/* Bump counter if we are at >= leading edge of vblank,
			 * but before vsync where vpos would turn negative and
			 * the hw counter really increments.
			 */
			if (vpos >= 0)
				count++;
		}
	} else {
		/* Fallback to use value as is. */
		count = amdgpu_display_vblank_get_counter(adev, pipe);
		DRM_DEBUG_VBL("NULL mode info! Returned count may be wrong.\n");
	}

	return count;
}

/**
 * amdgpu_enable_vblank_kms - enable vblank interrupt
 *
 * @dev: drm dev pointer
 * @pipe: crtc to enable vblank interrupt for
 *
 * Enable the interrupt on the requested crtc (all asics).
 * Returns 0 on success, -EINVAL on failure.
 */
int amdgpu_enable_vblank_kms(struct drm_device *dev, unsigned int pipe)
{
	struct amdgpu_device *adev = dev->dev_private;
	int idx = amdgpu_crtc_idx_to_irq_type(adev, pipe);

	return amdgpu_irq_get(adev, &adev->crtc_irq, idx);
}

/**
 * amdgpu_disable_vblank_kms - disable vblank interrupt
 *
 * @dev: drm dev pointer
 * @pipe: crtc to disable vblank interrupt for
 *
 * Disable the interrupt on the requested crtc (all asics).
 */
void amdgpu_disable_vblank_kms(struct drm_device *dev, unsigned int pipe)
{
	struct amdgpu_device *adev = dev->dev_private;
	int idx = amdgpu_crtc_idx_to_irq_type(adev, pipe);

	amdgpu_irq_put(adev, &adev->crtc_irq, idx);
}

/**
 * amdgpu_get_vblank_timestamp_kms - get vblank timestamp
 *
 * @dev: drm dev pointer
 * @crtc: crtc to get the timestamp for
 * @max_error: max error
 * @vblank_time: time value
 * @flags: flags passed to the driver
 *
 * Gets the timestamp on the requested crtc based on the
 * scanout position.  (all asics).
 * Returns postive status flags on success, negative error on failure.
 */
int amdgpu_get_vblank_timestamp_kms(struct drm_device *dev, unsigned int pipe,
				    int *max_error,
				    struct timeval *vblank_time,
				    unsigned flags)
{
	struct drm_crtc *crtc;
	struct amdgpu_device *adev = dev->dev_private;

	if (pipe >= dev->num_crtcs) {
		DRM_ERROR("Invalid crtc %u\n", pipe);
		return -EINVAL;
	}

	/* Get associated drm_crtc: */
	crtc = &adev->mode_info.crtcs[pipe]->base;
	if (!crtc) {
		/* This can occur on driver load if some component fails to
		 * initialize completely and driver is unloaded */
		DRM_ERROR("Uninitialized crtc %d\n", pipe);
		return -EINVAL;
	}

	/* Helper routine in DRM core does all the work: */
	return drm_calc_vbltimestamp_from_scanoutpos(dev, pipe, max_error,
						     vblank_time, flags,
						     &crtc->hwmode);
}

const struct drm_ioctl_desc amdgpu_ioctls_kms[] = {
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_CREATE, amdgpu_gem_create_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_CTX, amdgpu_ctx_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_BO_LIST, amdgpu_bo_list_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	/* KMS */
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_MMAP, amdgpu_gem_mmap_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_WAIT_IDLE, amdgpu_gem_wait_idle_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_CS, amdgpu_cs_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_INFO, amdgpu_info_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_WAIT_CS, amdgpu_cs_wait_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_WAIT_FENCES, amdgpu_cs_wait_fences_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_METADATA, amdgpu_gem_metadata_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_VA, amdgpu_gem_va_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_OP, amdgpu_gem_op_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_USERPTR, amdgpu_gem_userptr_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
};
const int amdgpu_max_kms_ioctl = ARRAY_SIZE(amdgpu_ioctls_kms);

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

static int amdgpu_debugfs_firmware_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct drm_amdgpu_info_firmware fw_info;
	struct drm_amdgpu_query_fw query_fw;
	int ret, i;

	/* VCE */
	query_fw.fw_type = AMDGPU_INFO_FW_VCE;
	ret = amdgpu_firmware_info(&fw_info, &query_fw, adev);
	if (ret)
		return ret;
	seq_printf(m, "VCE feature version: %u, firmware version: 0x%08x\n",
		   fw_info.feature, fw_info.ver);

	/* UVD */
	query_fw.fw_type = AMDGPU_INFO_FW_UVD;
	ret = amdgpu_firmware_info(&fw_info, &query_fw, adev);
	if (ret)
		return ret;
	seq_printf(m, "UVD feature version: %u, firmware version: 0x%08x\n",
		   fw_info.feature, fw_info.ver);

	/* GMC */
	query_fw.fw_type = AMDGPU_INFO_FW_GMC;
	ret = amdgpu_firmware_info(&fw_info, &query_fw, adev);
	if (ret)
		return ret;
	seq_printf(m, "MC feature version: %u, firmware version: 0x%08x\n",
		   fw_info.feature, fw_info.ver);

	/* ME */
	query_fw.fw_type = AMDGPU_INFO_FW_GFX_ME;
	ret = amdgpu_firmware_info(&fw_info, &query_fw, adev);
	if (ret)
		return ret;
	seq_printf(m, "ME feature version: %u, firmware version: 0x%08x\n",
		   fw_info.feature, fw_info.ver);

	/* PFP */
	query_fw.fw_type = AMDGPU_INFO_FW_GFX_PFP;
	ret = amdgpu_firmware_info(&fw_info, &query_fw, adev);
	if (ret)
		return ret;
	seq_printf(m, "PFP feature version: %u, firmware version: 0x%08x\n",
		   fw_info.feature, fw_info.ver);

	/* CE */
	query_fw.fw_type = AMDGPU_INFO_FW_GFX_CE;
	ret = amdgpu_firmware_info(&fw_info, &query_fw, adev);
	if (ret)
		return ret;
	seq_printf(m, "CE feature version: %u, firmware version: 0x%08x\n",
		   fw_info.feature, fw_info.ver);

	/* RLC */
	query_fw.fw_type = AMDGPU_INFO_FW_GFX_RLC;
	ret = amdgpu_firmware_info(&fw_info, &query_fw, adev);
	if (ret)
		return ret;
	seq_printf(m, "RLC feature version: %u, firmware version: 0x%08x\n",
		   fw_info.feature, fw_info.ver);

	/* MEC */
	query_fw.fw_type = AMDGPU_INFO_FW_GFX_MEC;
	query_fw.index = 0;
	ret = amdgpu_firmware_info(&fw_info, &query_fw, adev);
	if (ret)
		return ret;
	seq_printf(m, "MEC feature version: %u, firmware version: 0x%08x\n",
		   fw_info.feature, fw_info.ver);

	/* MEC2 */
	if (adev->asic_type == CHIP_KAVERI ||
	    (adev->asic_type > CHIP_TOPAZ && adev->asic_type != CHIP_STONEY)) {
		query_fw.index = 1;
		ret = amdgpu_firmware_info(&fw_info, &query_fw, adev);
		if (ret)
			return ret;
		seq_printf(m, "MEC2 feature version: %u, firmware version: 0x%08x\n",
			   fw_info.feature, fw_info.ver);
	}

	/* SMC */
	query_fw.fw_type = AMDGPU_INFO_FW_SMC;
	ret = amdgpu_firmware_info(&fw_info, &query_fw, adev);
	if (ret)
		return ret;
	seq_printf(m, "SMC feature version: %u, firmware version: 0x%08x\n",
		   fw_info.feature, fw_info.ver);

	/* SDMA */
	query_fw.fw_type = AMDGPU_INFO_FW_SDMA;
	for (i = 0; i < adev->sdma.num_instances; i++) {
		query_fw.index = i;
		ret = amdgpu_firmware_info(&fw_info, &query_fw, adev);
		if (ret)
			return ret;
		seq_printf(m, "SDMA%d feature version: %u, firmware version: 0x%08x\n",
			   i, fw_info.feature, fw_info.ver);
	}

	return 0;
}

static const struct drm_info_list amdgpu_firmware_info_list[] = {
	{"amdgpu_firmware_info", amdgpu_debugfs_firmware_info, 0, NULL},
};
#endif

int amdgpu_debugfs_firmware_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	return amdgpu_debugfs_add_files(adev, amdgpu_firmware_info_list,
					ARRAY_SIZE(amdgpu_firmware_info_list));
#else
	return 0;
#endif
}
