// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#include <linux/firmware.h>
#include <linux/highmem.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>

#include "vpu_boot_api.h"
#include "ivpu_drv.h"
#include "ivpu_fw.h"
#include "ivpu_fw_log.h"
#include "ivpu_gem.h"
#include "ivpu_hw.h"
#include "ivpu_ipc.h"
#include "ivpu_pm.h"

#define FW_GLOBAL_MEM_START	(2ull * SZ_1G)
#define FW_GLOBAL_MEM_END	(3ull * SZ_1G)
#define FW_SHARED_MEM_SIZE	SZ_256M /* Must be aligned to FW_SHARED_MEM_ALIGNMENT */
#define FW_SHARED_MEM_ALIGNMENT	SZ_128K /* VPU MTRR limitation */
#define FW_RUNTIME_MAX_SIZE	SZ_512M
#define FW_SHAVE_NN_MAX_SIZE	SZ_2M
#define FW_RUNTIME_MIN_ADDR	(FW_GLOBAL_MEM_START)
#define FW_RUNTIME_MAX_ADDR	(FW_GLOBAL_MEM_END - FW_SHARED_MEM_SIZE)
#define FW_VERSION_HEADER_SIZE	SZ_4K
#define FW_FILE_IMAGE_OFFSET	(VPU_FW_HEADER_SIZE + FW_VERSION_HEADER_SIZE)

#define WATCHDOG_MSS_REDIRECT	32
#define WATCHDOG_NCE_REDIRECT	33

#define ADDR_TO_L2_CACHE_CFG(addr) ((addr) >> 31)

/* Check if FW API is compatible with the driver */
#define IVPU_FW_CHECK_API_COMPAT(vdev, fw_hdr, name, min_major) \
	ivpu_fw_check_api(vdev, fw_hdr, #name, \
			  VPU_##name##_API_VER_INDEX, \
			  VPU_##name##_API_VER_MAJOR, \
			  VPU_##name##_API_VER_MINOR, min_major)

/* Check if API version is lower that the given version */
#define IVPU_FW_CHECK_API_VER_LT(vdev, fw_hdr, name, major, minor) \
	ivpu_fw_check_api_ver_lt(vdev, fw_hdr, #name, VPU_##name##_API_VER_INDEX, major, minor)

#define IVPU_FOCUS_PRESENT_TIMER_MS 1000

static char *ivpu_firmware;
module_param_named_unsafe(firmware, ivpu_firmware, charp, 0644);
MODULE_PARM_DESC(firmware, "NPU firmware binary in /lib/firmware/..");

static struct {
	int gen;
	const char *name;
} fw_names[] = {
	{ IVPU_HW_IP_37XX, "vpu_37xx.bin" },
	{ IVPU_HW_IP_37XX, "intel/vpu/vpu_37xx_v0.0.bin" },
	{ IVPU_HW_IP_40XX, "vpu_40xx.bin" },
	{ IVPU_HW_IP_40XX, "intel/vpu/vpu_40xx_v0.0.bin" },
};

static int ivpu_fw_request(struct ivpu_device *vdev)
{
	int ret = -ENOENT;
	int i;

	if (ivpu_firmware) {
		ret = request_firmware(&vdev->fw->file, ivpu_firmware, vdev->drm.dev);
		if (!ret)
			vdev->fw->name = ivpu_firmware;
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(fw_names); i++) {
		if (fw_names[i].gen != ivpu_hw_ip_gen(vdev))
			continue;

		ret = firmware_request_nowarn(&vdev->fw->file, fw_names[i].name, vdev->drm.dev);
		if (!ret) {
			vdev->fw->name = fw_names[i].name;
			return 0;
		}
	}

	ivpu_err(vdev, "Failed to request firmware: %d\n", ret);
	return ret;
}

static int
ivpu_fw_check_api(struct ivpu_device *vdev, const struct vpu_firmware_header *fw_hdr,
		  const char *str, int index, u16 expected_major, u16 expected_minor,
		  u16 min_major)
{
	u16 major = (u16)(fw_hdr->api_version[index] >> 16);
	u16 minor = (u16)(fw_hdr->api_version[index]);

	if (major < min_major) {
		ivpu_err(vdev, "Incompatible FW %s API version: %d.%d, required %d.0 or later\n",
			 str, major, minor, min_major);
		return -EINVAL;
	}
	if (major != expected_major) {
		ivpu_warn(vdev, "Major FW %s API version different: %d.%d (expected %d.%d)\n",
			  str, major, minor, expected_major, expected_minor);
	}
	ivpu_dbg(vdev, FW_BOOT, "FW %s API version: %d.%d (expected %d.%d)\n",
		 str, major, minor, expected_major, expected_minor);

	return 0;
}

static bool
ivpu_fw_check_api_ver_lt(struct ivpu_device *vdev, const struct vpu_firmware_header *fw_hdr,
			 const char *str, int index, u16 major, u16 minor)
{
	u16 fw_major = (u16)(fw_hdr->api_version[index] >> 16);
	u16 fw_minor = (u16)(fw_hdr->api_version[index]);

	if (fw_major < major || (fw_major == major && fw_minor < minor))
		return true;

	return false;
}

static bool is_within_range(u64 addr, size_t size, u64 range_start, size_t range_size)
{
	if (addr < range_start || addr + size > range_start + range_size)
		return false;

	return true;
}

static int ivpu_fw_parse(struct ivpu_device *vdev)
{
	struct ivpu_fw_info *fw = vdev->fw;
	const struct vpu_firmware_header *fw_hdr = (const void *)fw->file->data;
	u64 runtime_addr, image_load_addr, runtime_size, image_size;

	if (fw->file->size <= FW_FILE_IMAGE_OFFSET) {
		ivpu_err(vdev, "Firmware file is too small: %zu\n", fw->file->size);
		return -EINVAL;
	}

	if (fw_hdr->header_version != VPU_FW_HEADER_VERSION) {
		ivpu_err(vdev, "Invalid firmware header version: %u\n", fw_hdr->header_version);
		return -EINVAL;
	}

	runtime_addr = fw_hdr->boot_params_load_address;
	runtime_size = fw_hdr->runtime_size;
	image_load_addr = fw_hdr->image_load_address;
	image_size = fw_hdr->image_size;

	if (runtime_addr < FW_RUNTIME_MIN_ADDR || runtime_addr > FW_RUNTIME_MAX_ADDR) {
		ivpu_err(vdev, "Invalid firmware runtime address: 0x%llx\n", runtime_addr);
		return -EINVAL;
	}

	if (runtime_size < fw->file->size || runtime_size > FW_RUNTIME_MAX_SIZE) {
		ivpu_err(vdev, "Invalid firmware runtime size: %llu\n", runtime_size);
		return -EINVAL;
	}

	if (FW_FILE_IMAGE_OFFSET + image_size > fw->file->size) {
		ivpu_err(vdev, "Invalid image size: %llu\n", image_size);
		return -EINVAL;
	}

	if (image_load_addr < runtime_addr ||
	    image_load_addr + image_size > runtime_addr + runtime_size) {
		ivpu_err(vdev, "Invalid firmware load address size: 0x%llx and size %llu\n",
			 image_load_addr, image_size);
		return -EINVAL;
	}

	if (fw_hdr->shave_nn_fw_size > FW_SHAVE_NN_MAX_SIZE) {
		ivpu_err(vdev, "SHAVE NN firmware is too big: %u\n", fw_hdr->shave_nn_fw_size);
		return -EINVAL;
	}

	if (fw_hdr->entry_point < image_load_addr ||
	    fw_hdr->entry_point >= image_load_addr + image_size) {
		ivpu_err(vdev, "Invalid entry point: 0x%llx\n", fw_hdr->entry_point);
		return -EINVAL;
	}
	ivpu_dbg(vdev, FW_BOOT, "Header version: 0x%x, format 0x%x\n",
		 fw_hdr->header_version, fw_hdr->image_format);

	ivpu_info(vdev, "Firmware: %s, version: %s", fw->name,
		  (const char *)fw_hdr + VPU_FW_HEADER_SIZE);

	if (IVPU_FW_CHECK_API_COMPAT(vdev, fw_hdr, BOOT, 3))
		return -EINVAL;
	if (IVPU_FW_CHECK_API_COMPAT(vdev, fw_hdr, JSM, 3))
		return -EINVAL;

	fw->runtime_addr = runtime_addr;
	fw->runtime_size = runtime_size;
	fw->image_load_offset = image_load_addr - runtime_addr;
	fw->image_size = image_size;
	fw->shave_nn_size = PAGE_ALIGN(fw_hdr->shave_nn_fw_size);

	fw->cold_boot_entry_point = fw_hdr->entry_point;
	fw->entry_point = fw->cold_boot_entry_point;

	fw->trace_level = min_t(u32, ivpu_log_level, IVPU_FW_LOG_FATAL);
	fw->trace_destination_mask = VPU_TRACE_DESTINATION_VERBOSE_TRACING;
	fw->trace_hw_component_mask = -1;

	fw->dvfs_mode = 0;

	fw->primary_preempt_buf_size = fw_hdr->preemption_buffer_1_size;
	fw->secondary_preempt_buf_size = fw_hdr->preemption_buffer_2_size;

	if (fw_hdr->ro_section_start_address && !is_within_range(fw_hdr->ro_section_start_address,
								 fw_hdr->ro_section_size,
								 fw_hdr->image_load_address,
								 fw_hdr->image_size)) {
		ivpu_err(vdev, "Invalid read-only section: start address 0x%llx, size %u\n",
			 fw_hdr->ro_section_start_address, fw_hdr->ro_section_size);
		return -EINVAL;
	}

	fw->read_only_addr = fw_hdr->ro_section_start_address;
	fw->read_only_size = fw_hdr->ro_section_size;

	ivpu_dbg(vdev, FW_BOOT, "Size: file %lu image %u runtime %u shavenn %u\n",
		 fw->file->size, fw->image_size, fw->runtime_size, fw->shave_nn_size);
	ivpu_dbg(vdev, FW_BOOT, "Address: runtime 0x%llx, load 0x%llx, entry point 0x%llx\n",
		 fw->runtime_addr, image_load_addr, fw->entry_point);
	ivpu_dbg(vdev, FW_BOOT, "Read-only section: address 0x%llx, size %u\n",
		 fw->read_only_addr, fw->read_only_size);

	return 0;
}

static void ivpu_fw_release(struct ivpu_device *vdev)
{
	release_firmware(vdev->fw->file);
}

/* Initialize workarounds that depend on FW version */
static void
ivpu_fw_init_wa(struct ivpu_device *vdev)
{
	const struct vpu_firmware_header *fw_hdr = (const void *)vdev->fw->file->data;

	if (IVPU_FW_CHECK_API_VER_LT(vdev, fw_hdr, BOOT, 3, 17) ||
	    (ivpu_test_mode & IVPU_TEST_MODE_D0I3_MSG_DISABLE))
		vdev->wa.disable_d0i3_msg = true;

	/* Force enable the feature for testing purposes */
	if (ivpu_test_mode & IVPU_TEST_MODE_D0I3_MSG_ENABLE)
		vdev->wa.disable_d0i3_msg = false;

	IVPU_PRINT_WA(disable_d0i3_msg);
}

static int ivpu_fw_update_global_range(struct ivpu_device *vdev)
{
	struct ivpu_fw_info *fw = vdev->fw;
	u64 start = ALIGN(fw->runtime_addr + fw->runtime_size, FW_SHARED_MEM_ALIGNMENT);
	u64 size = FW_SHARED_MEM_SIZE;

	if (start + size > FW_GLOBAL_MEM_END) {
		ivpu_err(vdev, "No space for shared region, start %lld, size %lld\n", start, size);
		return -EINVAL;
	}

	ivpu_hw_range_init(&vdev->hw->ranges.global, start, size);
	return 0;
}

static int ivpu_fw_mem_init(struct ivpu_device *vdev)
{
	struct ivpu_fw_info *fw = vdev->fw;
	struct ivpu_addr_range fw_range;
	int log_verb_size;
	int ret;

	ret = ivpu_fw_update_global_range(vdev);
	if (ret)
		return ret;

	fw_range.start = fw->runtime_addr;
	fw_range.end = fw->runtime_addr + fw->runtime_size;
	fw->mem = ivpu_bo_create(vdev, &vdev->gctx, &fw_range, fw->runtime_size,
				 DRM_IVPU_BO_WC | DRM_IVPU_BO_MAPPABLE);
	if (!fw->mem) {
		ivpu_err(vdev, "Failed to create firmware runtime memory buffer\n");
		return -ENOMEM;
	}

	ret = ivpu_mmu_context_set_pages_ro(vdev, &vdev->gctx, fw->read_only_addr,
					    fw->read_only_size);
	if (ret) {
		ivpu_err(vdev, "Failed to set firmware image read-only\n");
		goto err_free_fw_mem;
	}

	fw->mem_log_crit = ivpu_bo_create_global(vdev, IVPU_FW_CRITICAL_BUFFER_SIZE,
						 DRM_IVPU_BO_CACHED | DRM_IVPU_BO_MAPPABLE);
	if (!fw->mem_log_crit) {
		ivpu_err(vdev, "Failed to create critical log buffer\n");
		ret = -ENOMEM;
		goto err_free_fw_mem;
	}

	if (ivpu_log_level <= IVPU_FW_LOG_INFO)
		log_verb_size = IVPU_FW_VERBOSE_BUFFER_LARGE_SIZE;
	else
		log_verb_size = IVPU_FW_VERBOSE_BUFFER_SMALL_SIZE;

	fw->mem_log_verb = ivpu_bo_create_global(vdev, log_verb_size,
						 DRM_IVPU_BO_CACHED | DRM_IVPU_BO_MAPPABLE);
	if (!fw->mem_log_verb) {
		ivpu_err(vdev, "Failed to create verbose log buffer\n");
		ret = -ENOMEM;
		goto err_free_log_crit;
	}

	if (fw->shave_nn_size) {
		fw->mem_shave_nn = ivpu_bo_create(vdev, &vdev->gctx, &vdev->hw->ranges.shave,
						  fw->shave_nn_size, DRM_IVPU_BO_WC);
		if (!fw->mem_shave_nn) {
			ivpu_err(vdev, "Failed to create shavenn buffer\n");
			ret = -ENOMEM;
			goto err_free_log_verb;
		}
	}

	return 0;

err_free_log_verb:
	ivpu_bo_free(fw->mem_log_verb);
err_free_log_crit:
	ivpu_bo_free(fw->mem_log_crit);
err_free_fw_mem:
	ivpu_bo_free(fw->mem);
	return ret;
}

static void ivpu_fw_mem_fini(struct ivpu_device *vdev)
{
	struct ivpu_fw_info *fw = vdev->fw;

	if (fw->mem_shave_nn) {
		ivpu_bo_free(fw->mem_shave_nn);
		fw->mem_shave_nn = NULL;
	}

	ivpu_bo_free(fw->mem_log_verb);
	ivpu_bo_free(fw->mem_log_crit);
	ivpu_bo_free(fw->mem);

	fw->mem_log_verb = NULL;
	fw->mem_log_crit = NULL;
	fw->mem = NULL;
}

int ivpu_fw_init(struct ivpu_device *vdev)
{
	int ret;

	ret = ivpu_fw_request(vdev);
	if (ret)
		return ret;

	ret = ivpu_fw_parse(vdev);
	if (ret)
		goto err_fw_release;

	ivpu_fw_init_wa(vdev);

	ret = ivpu_fw_mem_init(vdev);
	if (ret)
		goto err_fw_release;

	ivpu_fw_load(vdev);

	return 0;

err_fw_release:
	ivpu_fw_release(vdev);
	return ret;
}

void ivpu_fw_fini(struct ivpu_device *vdev)
{
	ivpu_fw_mem_fini(vdev);
	ivpu_fw_release(vdev);
}

void ivpu_fw_load(struct ivpu_device *vdev)
{
	struct ivpu_fw_info *fw = vdev->fw;
	u64 image_end_offset = fw->image_load_offset + fw->image_size;

	memset(ivpu_bo_vaddr(fw->mem), 0, fw->image_load_offset);
	memcpy(ivpu_bo_vaddr(fw->mem) + fw->image_load_offset,
	       fw->file->data + FW_FILE_IMAGE_OFFSET, fw->image_size);

	if (IVPU_WA(clear_runtime_mem)) {
		u8 *start = ivpu_bo_vaddr(fw->mem) + image_end_offset;
		u64 size = ivpu_bo_size(fw->mem) - image_end_offset;

		memset(start, 0, size);
	}

	wmb(); /* Flush WC buffers after writing fw->mem */
}

static void ivpu_fw_boot_params_print(struct ivpu_device *vdev, struct vpu_boot_params *boot_params)
{
	ivpu_dbg(vdev, FW_BOOT, "boot_params.magic = 0x%x\n",
		 boot_params->magic);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.vpu_id = 0x%x\n",
		 boot_params->vpu_id);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.vpu_count = 0x%x\n",
		 boot_params->vpu_count);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.frequency = %u\n",
		 boot_params->frequency);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.perf_clk_frequency = %u\n",
		 boot_params->perf_clk_frequency);

	ivpu_dbg(vdev, FW_BOOT, "boot_params.ipc_header_area_start = 0x%llx\n",
		 boot_params->ipc_header_area_start);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.ipc_header_area_size = 0x%x\n",
		 boot_params->ipc_header_area_size);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.shared_region_base = 0x%llx\n",
		 boot_params->shared_region_base);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.shared_region_size = 0x%x\n",
		 boot_params->shared_region_size);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.ipc_payload_area_start = 0x%llx\n",
		 boot_params->ipc_payload_area_start);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.ipc_payload_area_size = 0x%x\n",
		 boot_params->ipc_payload_area_size);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.global_aliased_pio_base = 0x%llx\n",
		 boot_params->global_aliased_pio_base);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.global_aliased_pio_size = 0x%x\n",
		 boot_params->global_aliased_pio_size);

	ivpu_dbg(vdev, FW_BOOT, "boot_params.autoconfig = 0x%x\n",
		 boot_params->autoconfig);

	ivpu_dbg(vdev, FW_BOOT, "boot_params.cache_defaults[VPU_BOOT_L2_CACHE_CFG_NN].use = 0x%x\n",
		 boot_params->cache_defaults[VPU_BOOT_L2_CACHE_CFG_NN].use);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.cache_defaults[VPU_BOOT_L2_CACHE_CFG_NN].cfg = 0x%x\n",
		 boot_params->cache_defaults[VPU_BOOT_L2_CACHE_CFG_NN].cfg);

	ivpu_dbg(vdev, FW_BOOT, "boot_params.global_memory_allocator_base = 0x%llx\n",
		 boot_params->global_memory_allocator_base);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.global_memory_allocator_size = 0x%x\n",
		 boot_params->global_memory_allocator_size);

	ivpu_dbg(vdev, FW_BOOT, "boot_params.shave_nn_fw_base = 0x%llx\n",
		 boot_params->shave_nn_fw_base);

	ivpu_dbg(vdev, FW_BOOT, "boot_params.watchdog_irq_mss = 0x%x\n",
		 boot_params->watchdog_irq_mss);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.watchdog_irq_nce = 0x%x\n",
		 boot_params->watchdog_irq_nce);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.host_to_vpu_irq = 0x%x\n",
		 boot_params->host_to_vpu_irq);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.job_done_irq = 0x%x\n",
		 boot_params->job_done_irq);

	ivpu_dbg(vdev, FW_BOOT, "boot_params.host_version_id = 0x%x\n",
		 boot_params->host_version_id);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.si_stepping = 0x%x\n",
		 boot_params->si_stepping);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.device_id = 0x%llx\n",
		 boot_params->device_id);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.feature_exclusion = 0x%llx\n",
		 boot_params->feature_exclusion);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.sku = 0x%llx\n",
		 boot_params->sku);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.min_freq_pll_ratio = 0x%x\n",
		 boot_params->min_freq_pll_ratio);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.pn_freq_pll_ratio = 0x%x\n",
		 boot_params->pn_freq_pll_ratio);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.max_freq_pll_ratio = 0x%x\n",
		 boot_params->max_freq_pll_ratio);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.default_trace_level = 0x%x\n",
		 boot_params->default_trace_level);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.tracing_buff_message_format_mask = 0x%llx\n",
		 boot_params->tracing_buff_message_format_mask);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.trace_destination_mask = 0x%x\n",
		 boot_params->trace_destination_mask);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.trace_hw_component_mask = 0x%llx\n",
		 boot_params->trace_hw_component_mask);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.boot_type = 0x%x\n",
		 boot_params->boot_type);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.punit_telemetry_sram_base = 0x%llx\n",
		 boot_params->punit_telemetry_sram_base);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.punit_telemetry_sram_size = 0x%llx\n",
		 boot_params->punit_telemetry_sram_size);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.vpu_telemetry_enable = 0x%x\n",
		 boot_params->vpu_telemetry_enable);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.vpu_scheduling_mode = 0x%x\n",
		 boot_params->vpu_scheduling_mode);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.dvfs_mode = %u\n",
		 boot_params->dvfs_mode);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.d0i3_delayed_entry = %d\n",
		 boot_params->d0i3_delayed_entry);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.d0i3_residency_time_us = %lld\n",
		 boot_params->d0i3_residency_time_us);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.d0i3_entry_vpu_ts = %llu\n",
		 boot_params->d0i3_entry_vpu_ts);
	ivpu_dbg(vdev, FW_BOOT, "boot_params.system_time_us = %llu\n",
		 boot_params->system_time_us);
}

void ivpu_fw_boot_params_setup(struct ivpu_device *vdev, struct vpu_boot_params *boot_params)
{
	struct ivpu_bo *ipc_mem_rx = vdev->ipc->mem_rx;

	/* In case of warm boot only update variable params */
	if (!ivpu_fw_is_cold_boot(vdev)) {
		boot_params->d0i3_residency_time_us =
			ktime_us_delta(ktime_get_boottime(), vdev->hw->d0i3_entry_host_ts);
		boot_params->d0i3_entry_vpu_ts = vdev->hw->d0i3_entry_vpu_ts;
		boot_params->system_time_us = ktime_to_us(ktime_get_real());

		ivpu_dbg(vdev, FW_BOOT, "boot_params.d0i3_residency_time_us = %lld\n",
			 boot_params->d0i3_residency_time_us);
		ivpu_dbg(vdev, FW_BOOT, "boot_params.d0i3_entry_vpu_ts = %llu\n",
			 boot_params->d0i3_entry_vpu_ts);
		ivpu_dbg(vdev, FW_BOOT, "boot_params.system_time_us = %llu\n",
			 boot_params->system_time_us);

		boot_params->save_restore_ret_address = 0;
		vdev->pm->is_warmboot = true;
		wmb(); /* Flush WC buffers after writing save_restore_ret_address */
		return;
	}

	vdev->pm->is_warmboot = false;

	boot_params->magic = VPU_BOOT_PARAMS_MAGIC;
	boot_params->vpu_id = to_pci_dev(vdev->drm.dev)->bus->number;
	boot_params->frequency = ivpu_hw_pll_freq_get(vdev);

	/*
	 * This param is a debug firmware feature.  It switches default clock
	 * to higher resolution one for fine-grained and more accurate firmware
	 * task profiling.
	 */
	boot_params->perf_clk_frequency = ivpu_hw_profiling_freq_get(vdev);

	/*
	 * Uncached region of VPU address space, covers IPC buffers, job queues
	 * and log buffers, programmable to L2$ Uncached by VPU MTRR
	 */
	boot_params->shared_region_base = vdev->hw->ranges.global.start;
	boot_params->shared_region_size = vdev->hw->ranges.global.end -
					  vdev->hw->ranges.global.start;

	boot_params->ipc_header_area_start = ipc_mem_rx->vpu_addr;
	boot_params->ipc_header_area_size = ivpu_bo_size(ipc_mem_rx) / 2;

	boot_params->ipc_payload_area_start = ipc_mem_rx->vpu_addr + ivpu_bo_size(ipc_mem_rx) / 2;
	boot_params->ipc_payload_area_size = ivpu_bo_size(ipc_mem_rx) / 2;

	boot_params->global_aliased_pio_base = vdev->hw->ranges.user.start;
	boot_params->global_aliased_pio_size = ivpu_hw_range_size(&vdev->hw->ranges.user);

	/* Allow configuration for L2C_PAGE_TABLE with boot param value */
	boot_params->autoconfig = 1;

	/* Enable L2 cache for first 2GB of high memory */
	boot_params->cache_defaults[VPU_BOOT_L2_CACHE_CFG_NN].use = 1;
	boot_params->cache_defaults[VPU_BOOT_L2_CACHE_CFG_NN].cfg =
		ADDR_TO_L2_CACHE_CFG(vdev->hw->ranges.shave.start);

	if (vdev->fw->mem_shave_nn)
		boot_params->shave_nn_fw_base = vdev->fw->mem_shave_nn->vpu_addr;

	boot_params->watchdog_irq_mss = WATCHDOG_MSS_REDIRECT;
	boot_params->watchdog_irq_nce = WATCHDOG_NCE_REDIRECT;
	boot_params->si_stepping = ivpu_revision(vdev);
	boot_params->device_id = ivpu_device_id(vdev);
	boot_params->feature_exclusion = vdev->hw->tile_fuse;
	boot_params->sku = vdev->hw->sku;

	boot_params->min_freq_pll_ratio = vdev->hw->pll.min_ratio;
	boot_params->pn_freq_pll_ratio = vdev->hw->pll.pn_ratio;
	boot_params->max_freq_pll_ratio = vdev->hw->pll.max_ratio;

	boot_params->default_trace_level = vdev->fw->trace_level;
	boot_params->tracing_buff_message_format_mask = BIT(VPU_TRACING_FORMAT_STRING);
	boot_params->trace_destination_mask = vdev->fw->trace_destination_mask;
	boot_params->trace_hw_component_mask = vdev->fw->trace_hw_component_mask;
	boot_params->crit_tracing_buff_addr = vdev->fw->mem_log_crit->vpu_addr;
	boot_params->crit_tracing_buff_size = ivpu_bo_size(vdev->fw->mem_log_crit);
	boot_params->verbose_tracing_buff_addr = vdev->fw->mem_log_verb->vpu_addr;
	boot_params->verbose_tracing_buff_size = ivpu_bo_size(vdev->fw->mem_log_verb);

	boot_params->punit_telemetry_sram_base = ivpu_hw_telemetry_offset_get(vdev);
	boot_params->punit_telemetry_sram_size = ivpu_hw_telemetry_size_get(vdev);
	boot_params->vpu_telemetry_enable = ivpu_hw_telemetry_enable_get(vdev);
	boot_params->vpu_scheduling_mode = vdev->hw->sched_mode;
	if (vdev->hw->sched_mode == VPU_SCHEDULING_MODE_HW)
		boot_params->vpu_focus_present_timer_ms = IVPU_FOCUS_PRESENT_TIMER_MS;
	boot_params->dvfs_mode = vdev->fw->dvfs_mode;
	if (!IVPU_WA(disable_d0i3_msg))
		boot_params->d0i3_delayed_entry = 1;
	boot_params->d0i3_residency_time_us = 0;
	boot_params->d0i3_entry_vpu_ts = 0;

	boot_params->system_time_us = ktime_to_us(ktime_get_real());
	wmb(); /* Flush WC buffers after writing bootparams */

	ivpu_fw_boot_params_print(vdev, boot_params);
}
