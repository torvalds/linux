// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_ccb.h"
#include "pvr_device.h"
#include "pvr_device_info.h"
#include "pvr_fw.h"
#include "pvr_fw_info.h"
#include "pvr_fw_startstop.h"
#include "pvr_fw_trace.h"
#include "pvr_gem.h"
#include "pvr_power.h"
#include "pvr_rogue_fwif_dev_info.h"
#include "pvr_rogue_heap_config.h"
#include "pvr_vm.h"

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <drm/drm_mm.h>
#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/sizes.h>

#define FW_MAX_SUPPORTED_MAJOR_VERSION 1

#define FW_BOOT_TIMEOUT_USEC 5000000

/* Config heap occupies top 192k of the firmware heap. */
#define PVR_ROGUE_FW_CONFIG_HEAP_GRANULARITY SZ_64K
#define PVR_ROGUE_FW_CONFIG_HEAP_SIZE (3 * PVR_ROGUE_FW_CONFIG_HEAP_GRANULARITY)

/* Main firmware allocations should come from the remainder of the heap. */
#define PVR_ROGUE_FW_MAIN_HEAP_BASE ROGUE_FW_HEAP_BASE

/* Offsets from start of configuration area of FW heap. */
#define PVR_ROGUE_FWIF_CONNECTION_CTL_OFFSET 0
#define PVR_ROGUE_FWIF_OSINIT_OFFSET \
	(PVR_ROGUE_FWIF_CONNECTION_CTL_OFFSET + PVR_ROGUE_FW_CONFIG_HEAP_GRANULARITY)
#define PVR_ROGUE_FWIF_SYSINIT_OFFSET \
	(PVR_ROGUE_FWIF_OSINIT_OFFSET + PVR_ROGUE_FW_CONFIG_HEAP_GRANULARITY)

#define PVR_ROGUE_FAULT_PAGE_SIZE SZ_4K

#define PVR_SYNC_OBJ_SIZE sizeof(u32)

const struct pvr_fw_layout_entry *
pvr_fw_find_layout_entry(struct pvr_device *pvr_dev, enum pvr_fw_section_id id)
{
	const struct pvr_fw_layout_entry *layout_entries = pvr_dev->fw_dev.layout_entries;
	u32 num_layout_entries = pvr_dev->fw_dev.header->layout_entry_num;
	u32 entry;

	for (entry = 0; entry < num_layout_entries; entry++) {
		if (layout_entries[entry].id == id)
			return &layout_entries[entry];
	}

	return NULL;
}

static const struct pvr_fw_layout_entry *
pvr_fw_find_private_data(struct pvr_device *pvr_dev)
{
	const struct pvr_fw_layout_entry *layout_entries = pvr_dev->fw_dev.layout_entries;
	u32 num_layout_entries = pvr_dev->fw_dev.header->layout_entry_num;
	u32 entry;

	for (entry = 0; entry < num_layout_entries; entry++) {
		if (layout_entries[entry].id == META_PRIVATE_DATA ||
		    layout_entries[entry].id == MIPS_PRIVATE_DATA ||
		    layout_entries[entry].id == RISCV_PRIVATE_DATA)
			return &layout_entries[entry];
	}

	return NULL;
}

#define DEV_INFO_MASK_SIZE(x) DIV_ROUND_UP(x, 64)

/**
 * pvr_fw_validate() - Parse firmware header and check compatibility
 * @pvr_dev: Device pointer.
 *
 * Returns:
 *  * 0 on success, or
 *  * -EINVAL if firmware is incompatible.
 */
static int
pvr_fw_validate(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	const struct firmware *firmware = pvr_dev->fw_dev.firmware;
	const struct pvr_fw_layout_entry *layout_entries;
	const struct pvr_fw_info_header *header;
	const u8 *fw = firmware->data;
	u32 fw_offset = firmware->size - SZ_4K;
	u32 layout_table_size;
	u32 entry;

	if (firmware->size < SZ_4K || (firmware->size % FW_BLOCK_SIZE))
		return -EINVAL;

	header = (const struct pvr_fw_info_header *)&fw[fw_offset];

	if (header->info_version != PVR_FW_INFO_VERSION) {
		drm_err(drm_dev, "Unsupported fw info version %u\n",
			header->info_version);
		return -EINVAL;
	}

	if (header->header_len != sizeof(struct pvr_fw_info_header) ||
	    header->layout_entry_size != sizeof(struct pvr_fw_layout_entry) ||
	    header->layout_entry_num > PVR_FW_INFO_MAX_NUM_ENTRIES) {
		drm_err(drm_dev, "FW info format mismatch\n");
		return -EINVAL;
	}

	if (!(header->flags & PVR_FW_FLAGS_OPEN_SOURCE) ||
	    header->fw_version_major > FW_MAX_SUPPORTED_MAJOR_VERSION ||
	    header->fw_version_major == 0) {
		drm_err(drm_dev, "Unsupported FW version %u.%u (build: %u%s)\n",
			header->fw_version_major, header->fw_version_minor,
			header->fw_version_build,
			(header->flags & PVR_FW_FLAGS_OPEN_SOURCE) ? " OS" : "");
		return -EINVAL;
	}

	if (pvr_gpu_id_to_packed_bvnc(&pvr_dev->gpu_id) != header->bvnc) {
		struct pvr_gpu_id fw_gpu_id;

		packed_bvnc_to_pvr_gpu_id(header->bvnc, &fw_gpu_id);
		drm_err(drm_dev, "FW built for incorrect GPU ID %i.%i.%i.%i (expected %i.%i.%i.%i)\n",
			fw_gpu_id.b, fw_gpu_id.v, fw_gpu_id.n, fw_gpu_id.c,
			pvr_dev->gpu_id.b, pvr_dev->gpu_id.v, pvr_dev->gpu_id.n, pvr_dev->gpu_id.c);
		return -EINVAL;
	}

	fw_offset += header->header_len;
	layout_table_size =
		header->layout_entry_size * header->layout_entry_num;
	if ((fw_offset + layout_table_size) > firmware->size)
		return -EINVAL;

	layout_entries = (const struct pvr_fw_layout_entry *)&fw[fw_offset];
	for (entry = 0; entry < header->layout_entry_num; entry++) {
		u32 start_addr = layout_entries[entry].base_addr;
		u32 end_addr = start_addr + layout_entries[entry].alloc_size;

		if (start_addr >= end_addr)
			return -EINVAL;
	}

	fw_offset = (firmware->size - SZ_4K) - header->device_info_size;

	drm_info(drm_dev, "FW version v%u.%u (build %u OS)\n", header->fw_version_major,
		 header->fw_version_minor, header->fw_version_build);

	pvr_dev->fw_version.major = header->fw_version_major;
	pvr_dev->fw_version.minor = header->fw_version_minor;

	pvr_dev->fw_dev.header = header;
	pvr_dev->fw_dev.layout_entries = layout_entries;

	return 0;
}

static int
pvr_fw_get_device_info(struct pvr_device *pvr_dev)
{
	const struct firmware *firmware = pvr_dev->fw_dev.firmware;
	struct pvr_fw_device_info_header *header;
	const u8 *fw = firmware->data;
	const u64 *dev_info;
	u32 fw_offset;

	fw_offset = (firmware->size - SZ_4K) - pvr_dev->fw_dev.header->device_info_size;

	header = (struct pvr_fw_device_info_header *)&fw[fw_offset];
	dev_info = (u64 *)(header + 1);

	pvr_device_info_set_quirks(pvr_dev, dev_info, header->brn_mask_size);
	dev_info += header->brn_mask_size;

	pvr_device_info_set_enhancements(pvr_dev, dev_info, header->ern_mask_size);
	dev_info += header->ern_mask_size;

	return pvr_device_info_set_features(pvr_dev, dev_info, header->feature_mask_size,
					    header->feature_param_size);
}

static void
layout_get_sizes(struct pvr_device *pvr_dev)
{
	const struct pvr_fw_layout_entry *layout_entries = pvr_dev->fw_dev.layout_entries;
	u32 num_layout_entries = pvr_dev->fw_dev.header->layout_entry_num;
	struct pvr_fw_mem *fw_mem = &pvr_dev->fw_dev.mem;

	fw_mem->code_alloc_size = 0;
	fw_mem->data_alloc_size = 0;
	fw_mem->core_code_alloc_size = 0;
	fw_mem->core_data_alloc_size = 0;

	/* Extract section sizes from FW layout table. */
	for (u32 entry = 0; entry < num_layout_entries; entry++) {
		switch (layout_entries[entry].type) {
		case FW_CODE:
			fw_mem->code_alloc_size += layout_entries[entry].alloc_size;
			break;
		case FW_DATA:
			fw_mem->data_alloc_size += layout_entries[entry].alloc_size;
			break;
		case FW_COREMEM_CODE:
			fw_mem->core_code_alloc_size +=
				layout_entries[entry].alloc_size;
			break;
		case FW_COREMEM_DATA:
			fw_mem->core_data_alloc_size +=
				layout_entries[entry].alloc_size;
			break;
		case NONE:
			break;
		}
	}
}

int
pvr_fw_find_mmu_segment(struct pvr_device *pvr_dev, u32 addr, u32 size, void *fw_code_ptr,
			void *fw_data_ptr, void *fw_core_code_ptr, void *fw_core_data_ptr,
			void **host_addr_out)
{
	const struct pvr_fw_layout_entry *layout_entries = pvr_dev->fw_dev.layout_entries;
	u32 num_layout_entries = pvr_dev->fw_dev.header->layout_entry_num;
	u32 end_addr = addr + size;
	int entry = 0;

	/* Ensure requested range is not zero, and size is not causing addr to overflow. */
	if (end_addr <= addr)
		return -EINVAL;

	for (entry = 0; entry < num_layout_entries; entry++) {
		u32 entry_start_addr = layout_entries[entry].base_addr;
		u32 entry_end_addr = entry_start_addr + layout_entries[entry].alloc_size;

		if (addr >= entry_start_addr && addr < entry_end_addr &&
		    end_addr > entry_start_addr && end_addr <= entry_end_addr) {
			switch (layout_entries[entry].type) {
			case FW_CODE:
				*host_addr_out = fw_code_ptr;
				break;

			case FW_DATA:
				*host_addr_out = fw_data_ptr;
				break;

			case FW_COREMEM_CODE:
				*host_addr_out = fw_core_code_ptr;
				break;

			case FW_COREMEM_DATA:
				*host_addr_out = fw_core_data_ptr;
				break;

			default:
				return -EINVAL;
			}
			/* Direct Mem write to mapped memory */
			addr -= layout_entries[entry].base_addr;
			addr += layout_entries[entry].alloc_offset;

			/*
			 * Add offset to pointer to FW allocation only if that
			 * allocation is available
			 */
			*(u8 **)host_addr_out += addr;
			return 0;
		}
	}

	return -EINVAL;
}

static int
pvr_fw_create_fwif_connection_ctl(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;

	fw_dev->fwif_connection_ctl =
		pvr_fw_object_create_and_map_offset(pvr_dev,
						    fw_dev->fw_heap_info.config_offset +
						    PVR_ROGUE_FWIF_CONNECTION_CTL_OFFSET,
						    sizeof(*fw_dev->fwif_connection_ctl),
						    PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
						    NULL, NULL,
						    &fw_dev->mem.fwif_connection_ctl_obj);
	if (IS_ERR(fw_dev->fwif_connection_ctl)) {
		drm_err(drm_dev,
			"Unable to allocate FWIF connection control memory\n");
		return PTR_ERR(fw_dev->fwif_connection_ctl);
	}

	return 0;
}

static void
pvr_fw_fini_fwif_connection_ctl(struct pvr_device *pvr_dev)
{
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;

	pvr_fw_object_unmap_and_destroy(fw_dev->mem.fwif_connection_ctl_obj);
}

static void
fw_osinit_init(void *cpu_ptr, void *priv)
{
	struct rogue_fwif_osinit *fwif_osinit = cpu_ptr;
	struct pvr_device *pvr_dev = priv;
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;
	struct pvr_fw_mem *fw_mem = &fw_dev->mem;

	fwif_osinit->kernel_ccbctl_fw_addr = pvr_dev->kccb.ccb.ctrl_fw_addr;
	fwif_osinit->kernel_ccb_fw_addr = pvr_dev->kccb.ccb.ccb_fw_addr;
	pvr_fw_object_get_fw_addr(pvr_dev->kccb.rtn_obj,
				  &fwif_osinit->kernel_ccb_rtn_slots_fw_addr);

	fwif_osinit->firmware_ccbctl_fw_addr = pvr_dev->fwccb.ctrl_fw_addr;
	fwif_osinit->firmware_ccb_fw_addr = pvr_dev->fwccb.ccb_fw_addr;

	fwif_osinit->work_est_firmware_ccbctl_fw_addr = 0;
	fwif_osinit->work_est_firmware_ccb_fw_addr = 0;

	pvr_fw_object_get_fw_addr(fw_mem->hwrinfobuf_obj,
				  &fwif_osinit->rogue_fwif_hwr_info_buf_ctl_fw_addr);
	pvr_fw_object_get_fw_addr(fw_mem->osdata_obj, &fwif_osinit->fw_os_data_fw_addr);

	fwif_osinit->hwr_debug_dump_limit = 0;

	rogue_fwif_compchecks_bvnc_init(&fwif_osinit->rogue_comp_checks.hw_bvnc);
	rogue_fwif_compchecks_bvnc_init(&fwif_osinit->rogue_comp_checks.fw_bvnc);
}

static void
fw_osdata_init(void *cpu_ptr, void *priv)
{
	struct rogue_fwif_osdata *fwif_osdata = cpu_ptr;
	struct pvr_device *pvr_dev = priv;
	struct pvr_fw_mem *fw_mem = &pvr_dev->fw_dev.mem;

	pvr_fw_object_get_fw_addr(fw_mem->power_sync_obj, &fwif_osdata->power_sync_fw_addr);
}

static void
fw_fault_page_init(void *cpu_ptr, void *priv)
{
	u32 *fault_page = cpu_ptr;

	for (int i = 0; i < PVR_ROGUE_FAULT_PAGE_SIZE / sizeof(*fault_page); i++)
		fault_page[i] = 0xdeadbee0;
}

static void
fw_sysinit_init(void *cpu_ptr, void *priv)
{
	struct rogue_fwif_sysinit *fwif_sysinit = cpu_ptr;
	struct pvr_device *pvr_dev = priv;
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;
	struct pvr_fw_mem *fw_mem = &fw_dev->mem;
	dma_addr_t fault_dma_addr = 0;
	u32 clock_speed_hz = clk_get_rate(pvr_dev->core_clk);

	WARN_ON(!clock_speed_hz);

	WARN_ON(pvr_fw_object_get_dma_addr(fw_mem->fault_page_obj, 0, &fault_dma_addr));
	fwif_sysinit->fault_phys_addr = (u64)fault_dma_addr;

	fwif_sysinit->pds_exec_base = ROGUE_PDSCODEDATA_HEAP_BASE;
	fwif_sysinit->usc_exec_base = ROGUE_USCCODE_HEAP_BASE;

	pvr_fw_object_get_fw_addr(fw_mem->runtime_cfg_obj, &fwif_sysinit->runtime_cfg_fw_addr);
	pvr_fw_object_get_fw_addr(fw_dev->fw_trace.tracebuf_ctrl_obj,
				  &fwif_sysinit->trace_buf_ctl_fw_addr);
	pvr_fw_object_get_fw_addr(fw_mem->sysdata_obj, &fwif_sysinit->fw_sys_data_fw_addr);
	pvr_fw_object_get_fw_addr(fw_mem->gpu_util_fwcb_obj,
				  &fwif_sysinit->gpu_util_fw_cb_ctl_fw_addr);
	if (fw_mem->core_data_obj) {
		pvr_fw_object_get_fw_addr(fw_mem->core_data_obj,
					  &fwif_sysinit->coremem_data_store.fw_addr);
	}

	/* Currently unsupported. */
	fwif_sysinit->counter_dump_ctl.buffer_fw_addr = 0;
	fwif_sysinit->counter_dump_ctl.size_in_dwords = 0;

	/* Skip alignment checks. */
	fwif_sysinit->align_checks = 0;

	fwif_sysinit->filter_flags = 0;
	fwif_sysinit->hw_perf_filter = 0;
	fwif_sysinit->firmware_perf = FW_PERF_CONF_NONE;
	fwif_sysinit->initial_core_clock_speed = clock_speed_hz;
	fwif_sysinit->active_pm_latency_ms = 0;
	fwif_sysinit->gpio_validation_mode = ROGUE_FWIF_GPIO_VAL_OFF;
	fwif_sysinit->firmware_started = false;
	fwif_sysinit->marker_val = 1;

	memset(&fwif_sysinit->bvnc_km_feature_flags, 0,
	       sizeof(fwif_sysinit->bvnc_km_feature_flags));
}

#define ROGUE_FWIF_SLC_MIN_SIZE_FOR_DM_OVERLAP_KB 4

static void
fw_sysdata_init(void *cpu_ptr, void *priv)
{
	struct rogue_fwif_sysdata *fwif_sysdata = cpu_ptr;
	struct pvr_device *pvr_dev = priv;
	u32 slc_size_in_kilobytes = 0;
	u32 config_flags = 0;

	WARN_ON(PVR_FEATURE_VALUE(pvr_dev, slc_size_in_kilobytes, &slc_size_in_kilobytes));

	if (slc_size_in_kilobytes < ROGUE_FWIF_SLC_MIN_SIZE_FOR_DM_OVERLAP_KB)
		config_flags |= ROGUE_FWIF_INICFG_DISABLE_DM_OVERLAP;

	fwif_sysdata->config_flags = config_flags;
}

static void
fw_runtime_cfg_init(void *cpu_ptr, void *priv)
{
	struct rogue_fwif_runtime_cfg *runtime_cfg = cpu_ptr;
	struct pvr_device *pvr_dev = priv;
	u32 clock_speed_hz = clk_get_rate(pvr_dev->core_clk);

	WARN_ON(!clock_speed_hz);

	runtime_cfg->core_clock_speed = clock_speed_hz;
	runtime_cfg->active_pm_latency_ms = 0;
	runtime_cfg->active_pm_latency_persistant = true;
	WARN_ON(PVR_FEATURE_VALUE(pvr_dev, num_clusters,
				  &runtime_cfg->default_dusts_num_init) != 0);
}

static void
fw_gpu_util_fwcb_init(void *cpu_ptr, void *priv)
{
	struct rogue_fwif_gpu_util_fwcb *gpu_util_fwcb = cpu_ptr;

	gpu_util_fwcb->last_word = PVR_FWIF_GPU_UTIL_STATE_IDLE;
}

static int
pvr_fw_create_structures(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;
	struct pvr_fw_mem *fw_mem = &fw_dev->mem;
	int err;

	fw_dev->power_sync = pvr_fw_object_create_and_map(pvr_dev, sizeof(*fw_dev->power_sync),
							  PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
							  NULL, NULL, &fw_mem->power_sync_obj);
	if (IS_ERR(fw_dev->power_sync)) {
		drm_err(drm_dev, "Unable to allocate FW power_sync structure\n");
		return PTR_ERR(fw_dev->power_sync);
	}

	fw_dev->hwrinfobuf = pvr_fw_object_create_and_map(pvr_dev, sizeof(*fw_dev->hwrinfobuf),
							  PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
							  NULL, NULL, &fw_mem->hwrinfobuf_obj);
	if (IS_ERR(fw_dev->hwrinfobuf)) {
		drm_err(drm_dev,
			"Unable to allocate FW hwrinfobuf structure\n");
		err = PTR_ERR(fw_dev->hwrinfobuf);
		goto err_release_power_sync;
	}

	err = pvr_fw_object_create(pvr_dev, PVR_SYNC_OBJ_SIZE,
				   PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
				   NULL, NULL, &fw_mem->mmucache_sync_obj);
	if (err) {
		drm_err(drm_dev,
			"Unable to allocate MMU cache sync object\n");
		goto err_release_hwrinfobuf;
	}

	fw_dev->fwif_sysdata = pvr_fw_object_create_and_map(pvr_dev,
							    sizeof(*fw_dev->fwif_sysdata),
							    PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
							    fw_sysdata_init, pvr_dev,
							    &fw_mem->sysdata_obj);
	if (IS_ERR(fw_dev->fwif_sysdata)) {
		drm_err(drm_dev, "Unable to allocate FW SYSDATA structure\n");
		err = PTR_ERR(fw_dev->fwif_sysdata);
		goto err_release_mmucache_sync_obj;
	}

	err = pvr_fw_object_create(pvr_dev, PVR_ROGUE_FAULT_PAGE_SIZE,
				   PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
				   fw_fault_page_init, NULL, &fw_mem->fault_page_obj);
	if (err) {
		drm_err(drm_dev, "Unable to allocate FW fault page\n");
		goto err_release_sysdata;
	}

	err = pvr_fw_object_create(pvr_dev, sizeof(struct rogue_fwif_gpu_util_fwcb),
				   PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
				   fw_gpu_util_fwcb_init, pvr_dev, &fw_mem->gpu_util_fwcb_obj);
	if (err) {
		drm_err(drm_dev, "Unable to allocate GPU util FWCB\n");
		goto err_release_fault_page;
	}

	err = pvr_fw_object_create(pvr_dev, sizeof(struct rogue_fwif_runtime_cfg),
				   PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
				   fw_runtime_cfg_init, pvr_dev, &fw_mem->runtime_cfg_obj);
	if (err) {
		drm_err(drm_dev, "Unable to allocate FW runtime config\n");
		goto err_release_gpu_util_fwcb;
	}

	err = pvr_fw_trace_init(pvr_dev);
	if (err)
		goto err_release_runtime_cfg;

	fw_dev->fwif_osdata = pvr_fw_object_create_and_map(pvr_dev,
							   sizeof(*fw_dev->fwif_osdata),
							   PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
							   fw_osdata_init, pvr_dev,
							   &fw_mem->osdata_obj);
	if (IS_ERR(fw_dev->fwif_osdata)) {
		drm_err(drm_dev, "Unable to allocate FW OSDATA structure\n");
		err = PTR_ERR(fw_dev->fwif_osdata);
		goto err_fw_trace_fini;
	}

	fw_dev->fwif_osinit =
		pvr_fw_object_create_and_map_offset(pvr_dev,
						    fw_dev->fw_heap_info.config_offset +
						    PVR_ROGUE_FWIF_OSINIT_OFFSET,
						    sizeof(*fw_dev->fwif_osinit),
						    PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
						    fw_osinit_init, pvr_dev, &fw_mem->osinit_obj);
	if (IS_ERR(fw_dev->fwif_osinit)) {
		drm_err(drm_dev, "Unable to allocate FW OSINIT structure\n");
		err = PTR_ERR(fw_dev->fwif_osinit);
		goto err_release_osdata;
	}

	fw_dev->fwif_sysinit =
		pvr_fw_object_create_and_map_offset(pvr_dev,
						    fw_dev->fw_heap_info.config_offset +
						    PVR_ROGUE_FWIF_SYSINIT_OFFSET,
						    sizeof(*fw_dev->fwif_sysinit),
						    PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
						    fw_sysinit_init, pvr_dev, &fw_mem->sysinit_obj);
	if (IS_ERR(fw_dev->fwif_sysinit)) {
		drm_err(drm_dev, "Unable to allocate FW SYSINIT structure\n");
		err = PTR_ERR(fw_dev->fwif_sysinit);
		goto err_release_osinit;
	}

	return 0;

err_release_osinit:
	pvr_fw_object_unmap_and_destroy(fw_mem->osinit_obj);

err_release_osdata:
	pvr_fw_object_unmap_and_destroy(fw_mem->osdata_obj);

err_fw_trace_fini:
	pvr_fw_trace_fini(pvr_dev);

err_release_runtime_cfg:
	pvr_fw_object_destroy(fw_mem->runtime_cfg_obj);

err_release_gpu_util_fwcb:
	pvr_fw_object_destroy(fw_mem->gpu_util_fwcb_obj);

err_release_fault_page:
	pvr_fw_object_destroy(fw_mem->fault_page_obj);

err_release_sysdata:
	pvr_fw_object_unmap_and_destroy(fw_mem->sysdata_obj);

err_release_mmucache_sync_obj:
	pvr_fw_object_destroy(fw_mem->mmucache_sync_obj);

err_release_hwrinfobuf:
	pvr_fw_object_unmap_and_destroy(fw_mem->hwrinfobuf_obj);

err_release_power_sync:
	pvr_fw_object_unmap_and_destroy(fw_mem->power_sync_obj);

	return err;
}

static void
pvr_fw_destroy_structures(struct pvr_device *pvr_dev)
{
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;
	struct pvr_fw_mem *fw_mem = &fw_dev->mem;

	pvr_fw_trace_fini(pvr_dev);
	pvr_fw_object_destroy(fw_mem->runtime_cfg_obj);
	pvr_fw_object_destroy(fw_mem->gpu_util_fwcb_obj);
	pvr_fw_object_destroy(fw_mem->fault_page_obj);
	pvr_fw_object_unmap_and_destroy(fw_mem->sysdata_obj);
	pvr_fw_object_unmap_and_destroy(fw_mem->sysinit_obj);

	pvr_fw_object_destroy(fw_mem->mmucache_sync_obj);
	pvr_fw_object_unmap_and_destroy(fw_mem->hwrinfobuf_obj);
	pvr_fw_object_unmap_and_destroy(fw_mem->power_sync_obj);
	pvr_fw_object_unmap_and_destroy(fw_mem->osdata_obj);
	pvr_fw_object_unmap_and_destroy(fw_mem->osinit_obj);
}

/**
 * pvr_fw_process() - Process firmware image, allocate FW memory and create boot
 *                    arguments
 * @pvr_dev: Device pointer.
 *
 * Returns:
 *  * 0 on success, or
 *  * Any error returned by pvr_fw_object_create_and_map_offset(), or
 *  * Any error returned by pvr_fw_object_create_and_map().
 */
static int
pvr_fw_process(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	struct pvr_fw_mem *fw_mem = &pvr_dev->fw_dev.mem;
	const u8 *fw = pvr_dev->fw_dev.firmware->data;
	const struct pvr_fw_layout_entry *private_data;
	u8 *fw_code_ptr;
	u8 *fw_data_ptr;
	u8 *fw_core_code_ptr;
	u8 *fw_core_data_ptr;
	int err;

	layout_get_sizes(pvr_dev);

	private_data = pvr_fw_find_private_data(pvr_dev);
	if (!private_data)
		return -EINVAL;

	/* Allocate and map memory for firmware sections. */

	/*
	 * Code allocation must be at the start of the firmware heap, otherwise
	 * firmware processor will be unable to boot.
	 *
	 * This has the useful side-effect that for every other object in the
	 * driver, a firmware address of 0 is invalid.
	 */
	fw_code_ptr = pvr_fw_object_create_and_map_offset(pvr_dev, 0, fw_mem->code_alloc_size,
							  PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
							  NULL, NULL, &fw_mem->code_obj);
	if (IS_ERR(fw_code_ptr)) {
		drm_err(drm_dev, "Unable to allocate FW code memory\n");
		return PTR_ERR(fw_code_ptr);
	}

	if (pvr_dev->fw_dev.defs->has_fixed_data_addr()) {
		u32 base_addr = private_data->base_addr & pvr_dev->fw_dev.fw_heap_info.offset_mask;

		fw_data_ptr =
			pvr_fw_object_create_and_map_offset(pvr_dev, base_addr,
							    fw_mem->data_alloc_size,
							    PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
							    NULL, NULL, &fw_mem->data_obj);
	} else {
		fw_data_ptr = pvr_fw_object_create_and_map(pvr_dev, fw_mem->data_alloc_size,
							   PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
							   NULL, NULL, &fw_mem->data_obj);
	}
	if (IS_ERR(fw_data_ptr)) {
		drm_err(drm_dev, "Unable to allocate FW data memory\n");
		err = PTR_ERR(fw_data_ptr);
		goto err_free_fw_code_obj;
	}

	/* Core code and data sections are optional. */
	if (fw_mem->core_code_alloc_size) {
		fw_core_code_ptr =
			pvr_fw_object_create_and_map(pvr_dev, fw_mem->core_code_alloc_size,
						     PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
						     NULL, NULL, &fw_mem->core_code_obj);
		if (IS_ERR(fw_core_code_ptr)) {
			drm_err(drm_dev,
				"Unable to allocate FW core code memory\n");
			err = PTR_ERR(fw_core_code_ptr);
			goto err_free_fw_data_obj;
		}
	} else {
		fw_core_code_ptr = NULL;
	}

	if (fw_mem->core_data_alloc_size) {
		fw_core_data_ptr =
			pvr_fw_object_create_and_map(pvr_dev, fw_mem->core_data_alloc_size,
						     PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
						     NULL, NULL, &fw_mem->core_data_obj);
		if (IS_ERR(fw_core_data_ptr)) {
			drm_err(drm_dev,
				"Unable to allocate FW core data memory\n");
			err = PTR_ERR(fw_core_data_ptr);
			goto err_free_fw_core_code_obj;
		}
	} else {
		fw_core_data_ptr = NULL;
	}

	fw_mem->code = kzalloc(fw_mem->code_alloc_size, GFP_KERNEL);
	fw_mem->data = kzalloc(fw_mem->data_alloc_size, GFP_KERNEL);
	if (fw_mem->core_code_alloc_size)
		fw_mem->core_code = kzalloc(fw_mem->core_code_alloc_size, GFP_KERNEL);
	if (fw_mem->core_data_alloc_size)
		fw_mem->core_data = kzalloc(fw_mem->core_data_alloc_size, GFP_KERNEL);

	if (!fw_mem->code || !fw_mem->data ||
	    (!fw_mem->core_code && fw_mem->core_code_alloc_size) ||
	    (!fw_mem->core_data && fw_mem->core_data_alloc_size)) {
		err = -ENOMEM;
		goto err_free_kdata;
	}

	err = pvr_dev->fw_dev.defs->fw_process(pvr_dev, fw,
					       fw_mem->code, fw_mem->data, fw_mem->core_code,
					       fw_mem->core_data, fw_mem->core_code_alloc_size);

	if (err)
		goto err_free_kdata;

	memcpy(fw_code_ptr, fw_mem->code, fw_mem->code_alloc_size);
	memcpy(fw_data_ptr, fw_mem->data, fw_mem->data_alloc_size);
	if (fw_mem->core_code)
		memcpy(fw_core_code_ptr, fw_mem->core_code, fw_mem->core_code_alloc_size);
	if (fw_mem->core_data)
		memcpy(fw_core_data_ptr, fw_mem->core_data, fw_mem->core_data_alloc_size);

	/* We're finished with the firmware section memory on the CPU, unmap. */
	if (fw_core_data_ptr) {
		pvr_fw_object_vunmap(fw_mem->core_data_obj);
		fw_core_data_ptr = NULL;
	}
	if (fw_core_code_ptr) {
		pvr_fw_object_vunmap(fw_mem->core_code_obj);
		fw_core_code_ptr = NULL;
	}
	pvr_fw_object_vunmap(fw_mem->data_obj);
	fw_data_ptr = NULL;
	pvr_fw_object_vunmap(fw_mem->code_obj);
	fw_code_ptr = NULL;

	err = pvr_fw_create_fwif_connection_ctl(pvr_dev);
	if (err)
		goto err_free_kdata;

	return 0;

err_free_kdata:
	kfree(fw_mem->core_data);
	kfree(fw_mem->core_code);
	kfree(fw_mem->data);
	kfree(fw_mem->code);

	if (fw_core_data_ptr)
		pvr_fw_object_vunmap(fw_mem->core_data_obj);
	if (fw_mem->core_data_obj)
		pvr_fw_object_destroy(fw_mem->core_data_obj);

err_free_fw_core_code_obj:
	if (fw_core_code_ptr)
		pvr_fw_object_vunmap(fw_mem->core_code_obj);
	if (fw_mem->core_code_obj)
		pvr_fw_object_destroy(fw_mem->core_code_obj);

err_free_fw_data_obj:
	if (fw_data_ptr)
		pvr_fw_object_vunmap(fw_mem->data_obj);
	pvr_fw_object_destroy(fw_mem->data_obj);

err_free_fw_code_obj:
	if (fw_code_ptr)
		pvr_fw_object_vunmap(fw_mem->code_obj);
	pvr_fw_object_destroy(fw_mem->code_obj);

	return err;
}

static int
pvr_copy_to_fw(struct pvr_fw_object *dest_obj, u8 *src_ptr, u32 size)
{
	u8 *dest_ptr = pvr_fw_object_vmap(dest_obj);

	if (IS_ERR(dest_ptr))
		return PTR_ERR(dest_ptr);

	memcpy(dest_ptr, src_ptr, size);

	pvr_fw_object_vunmap(dest_obj);

	return 0;
}

static int
pvr_fw_reinit_code_data(struct pvr_device *pvr_dev)
{
	struct pvr_fw_mem *fw_mem = &pvr_dev->fw_dev.mem;
	int err;

	err = pvr_copy_to_fw(fw_mem->code_obj, fw_mem->code, fw_mem->code_alloc_size);
	if (err)
		return err;

	err = pvr_copy_to_fw(fw_mem->data_obj, fw_mem->data, fw_mem->data_alloc_size);
	if (err)
		return err;

	if (fw_mem->core_code) {
		err = pvr_copy_to_fw(fw_mem->core_code_obj, fw_mem->core_code,
				     fw_mem->core_code_alloc_size);
		if (err)
			return err;
	}

	if (fw_mem->core_data) {
		err = pvr_copy_to_fw(fw_mem->core_data_obj, fw_mem->core_data,
				     fw_mem->core_data_alloc_size);
		if (err)
			return err;
	}

	return 0;
}

static void
pvr_fw_cleanup(struct pvr_device *pvr_dev)
{
	struct pvr_fw_mem *fw_mem = &pvr_dev->fw_dev.mem;

	pvr_fw_fini_fwif_connection_ctl(pvr_dev);

	kfree(fw_mem->core_data);
	kfree(fw_mem->core_code);
	kfree(fw_mem->data);
	kfree(fw_mem->code);

	if (fw_mem->core_code_obj)
		pvr_fw_object_destroy(fw_mem->core_code_obj);
	if (fw_mem->core_data_obj)
		pvr_fw_object_destroy(fw_mem->core_data_obj);
	pvr_fw_object_destroy(fw_mem->code_obj);
	pvr_fw_object_destroy(fw_mem->data_obj);
}

/**
 * pvr_wait_for_fw_boot() - Wait for firmware to finish booting
 * @pvr_dev: Target PowerVR device.
 *
 * Returns:
 *  * 0 on success, or
 *  * -%ETIMEDOUT if firmware fails to boot within timeout.
 */
int
pvr_wait_for_fw_boot(struct pvr_device *pvr_dev)
{
	ktime_t deadline = ktime_add_us(ktime_get(), FW_BOOT_TIMEOUT_USEC);
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;

	while (ktime_to_ns(ktime_sub(deadline, ktime_get())) > 0) {
		if (READ_ONCE(fw_dev->fwif_sysinit->firmware_started))
			return 0;
	}

	return -ETIMEDOUT;
}

/*
 * pvr_fw_heap_info_init() - Calculate size and masks for FW heap
 * @pvr_dev: Target PowerVR device.
 * @log2_size: Log2 of raw heap size.
 * @reserved_size: Size of reserved area of heap, in bytes. May be zero.
 */
void
pvr_fw_heap_info_init(struct pvr_device *pvr_dev, u32 log2_size, u32 reserved_size)
{
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;

	fw_dev->fw_heap_info.gpu_addr = PVR_ROGUE_FW_MAIN_HEAP_BASE;
	fw_dev->fw_heap_info.log2_size = log2_size;
	fw_dev->fw_heap_info.reserved_size = reserved_size;
	fw_dev->fw_heap_info.raw_size = 1 << fw_dev->fw_heap_info.log2_size;
	fw_dev->fw_heap_info.offset_mask = fw_dev->fw_heap_info.raw_size - 1;
	fw_dev->fw_heap_info.config_offset = fw_dev->fw_heap_info.raw_size -
					     PVR_ROGUE_FW_CONFIG_HEAP_SIZE;
	fw_dev->fw_heap_info.size = fw_dev->fw_heap_info.raw_size -
				    (PVR_ROGUE_FW_CONFIG_HEAP_SIZE + reserved_size);
}

/**
 * pvr_fw_validate_init_device_info() - Validate firmware and initialise device information
 * @pvr_dev: Target PowerVR device.
 *
 * This function must be called before querying device information.
 *
 * Returns:
 *  * 0 on success, or
 *  * -%EINVAL if firmware validation fails.
 */
int
pvr_fw_validate_init_device_info(struct pvr_device *pvr_dev)
{
	int err;

	err = pvr_fw_validate(pvr_dev);
	if (err)
		return err;

	return pvr_fw_get_device_info(pvr_dev);
}

/**
 * pvr_fw_init() - Initialise and boot firmware
 * @pvr_dev: Target PowerVR device
 *
 * On successful completion of the function the PowerVR device will be
 * initialised and ready to use.
 *
 * Returns:
 *  * 0 on success,
 *  * -%EINVAL on invalid firmware image,
 *  * -%ENOMEM on out of memory, or
 *  * -%ETIMEDOUT if firmware processor fails to boot or on register poll timeout.
 */
int
pvr_fw_init(struct pvr_device *pvr_dev)
{
	u32 kccb_size_log2 = ROGUE_FWIF_KCCB_NUMCMDS_LOG2_DEFAULT;
	u32 kccb_rtn_size = (1 << kccb_size_log2) * sizeof(*pvr_dev->kccb.rtn);
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;
	int err;

	if (fw_dev->processor_type == PVR_FW_PROCESSOR_TYPE_META)
		fw_dev->defs = &pvr_fw_defs_meta;
	else if (fw_dev->processor_type == PVR_FW_PROCESSOR_TYPE_MIPS)
		fw_dev->defs = &pvr_fw_defs_mips;
	else
		return -EINVAL;

	err = fw_dev->defs->init(pvr_dev);
	if (err)
		return err;

	drm_mm_init(&fw_dev->fw_mm, ROGUE_FW_HEAP_BASE, fw_dev->fw_heap_info.raw_size);
	fw_dev->fw_mm_base = ROGUE_FW_HEAP_BASE;
	spin_lock_init(&fw_dev->fw_mm_lock);

	INIT_LIST_HEAD(&fw_dev->fw_objs.list);
	err = drmm_mutex_init(from_pvr_device(pvr_dev), &fw_dev->fw_objs.lock);
	if (err)
		goto err_mm_takedown;

	err = pvr_fw_process(pvr_dev);
	if (err)
		goto err_mm_takedown;

	/* Initialise KCCB and FWCCB. */
	err = pvr_kccb_init(pvr_dev);
	if (err)
		goto err_fw_cleanup;

	err = pvr_fwccb_init(pvr_dev);
	if (err)
		goto err_kccb_fini;

	/* Allocate memory for KCCB return slots. */
	pvr_dev->kccb.rtn = pvr_fw_object_create_and_map(pvr_dev, kccb_rtn_size,
							 PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
							 NULL, NULL, &pvr_dev->kccb.rtn_obj);
	if (IS_ERR(pvr_dev->kccb.rtn)) {
		err = PTR_ERR(pvr_dev->kccb.rtn);
		goto err_fwccb_fini;
	}

	err = pvr_fw_create_structures(pvr_dev);
	if (err)
		goto err_kccb_rtn_release;

	err = pvr_fw_start(pvr_dev);
	if (err)
		goto err_destroy_structures;

	err = pvr_wait_for_fw_boot(pvr_dev);
	if (err) {
		drm_err(from_pvr_device(pvr_dev), "Firmware failed to boot\n");
		goto err_fw_stop;
	}

	fw_dev->booted = true;

	return 0;

err_fw_stop:
	pvr_fw_stop(pvr_dev);

err_destroy_structures:
	pvr_fw_destroy_structures(pvr_dev);

err_kccb_rtn_release:
	pvr_fw_object_unmap_and_destroy(pvr_dev->kccb.rtn_obj);

err_fwccb_fini:
	pvr_ccb_fini(&pvr_dev->fwccb);

err_kccb_fini:
	pvr_kccb_fini(pvr_dev);

err_fw_cleanup:
	pvr_fw_cleanup(pvr_dev);

err_mm_takedown:
	drm_mm_takedown(&fw_dev->fw_mm);

	if (fw_dev->defs->fini)
		fw_dev->defs->fini(pvr_dev);

	return err;
}

/**
 * pvr_fw_fini() - Shutdown firmware processor and free associated memory
 * @pvr_dev: Target PowerVR device
 */
void
pvr_fw_fini(struct pvr_device *pvr_dev)
{
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;

	fw_dev->booted = false;

	pvr_fw_destroy_structures(pvr_dev);
	pvr_fw_object_unmap_and_destroy(pvr_dev->kccb.rtn_obj);

	/*
	 * Ensure FWCCB worker has finished executing before destroying FWCCB. The IRQ handler has
	 * been unregistered at this point so no new work should be being submitted.
	 */
	pvr_ccb_fini(&pvr_dev->fwccb);
	pvr_kccb_fini(pvr_dev);
	pvr_fw_cleanup(pvr_dev);

	mutex_lock(&pvr_dev->fw_dev.fw_objs.lock);
	WARN_ON(!list_empty(&pvr_dev->fw_dev.fw_objs.list));
	mutex_unlock(&pvr_dev->fw_dev.fw_objs.lock);

	drm_mm_takedown(&fw_dev->fw_mm);

	if (fw_dev->defs->fini)
		fw_dev->defs->fini(pvr_dev);
}

/**
 * pvr_fw_mts_schedule() - Schedule work via an MTS kick
 * @pvr_dev: Target PowerVR device
 * @val: Kick mask. Should be a combination of %ROGUE_CR_MTS_SCHEDULE_*
 */
void
pvr_fw_mts_schedule(struct pvr_device *pvr_dev, u32 val)
{
	/* Ensure memory is flushed before kicking MTS. */
	wmb();

	pvr_cr_write32(pvr_dev, ROGUE_CR_MTS_SCHEDULE, val);

	/* Ensure the MTS kick goes through before continuing. */
	mb();
}

/**
 * pvr_fw_structure_cleanup() - Send FW cleanup request for an object
 * @pvr_dev: Target PowerVR device.
 * @type: Type of object to cleanup. Must be one of &enum rogue_fwif_cleanup_type.
 * @fw_obj: Pointer to FW object containing object to cleanup.
 * @offset: Offset within FW object of object to cleanup.
 *
 * Returns:
 *  * 0 on success,
 *  * -EBUSY if object is busy,
 *  * -ETIMEDOUT on timeout, or
 *  * -EIO if device is lost.
 */
int
pvr_fw_structure_cleanup(struct pvr_device *pvr_dev, u32 type, struct pvr_fw_object *fw_obj,
			 u32 offset)
{
	struct rogue_fwif_kccb_cmd cmd;
	int slot_nr;
	int idx;
	int err;
	u32 rtn;

	struct rogue_fwif_cleanup_request *cleanup_req = &cmd.cmd_data.cleanup_data;

	down_read(&pvr_dev->reset_sem);

	if (!drm_dev_enter(from_pvr_device(pvr_dev), &idx)) {
		err = -EIO;
		goto err_up_read;
	}

	cmd.cmd_type = ROGUE_FWIF_KCCB_CMD_CLEANUP;
	cmd.kccb_flags = 0;
	cleanup_req->cleanup_type = type;

	switch (type) {
	case ROGUE_FWIF_CLEANUP_FWCOMMONCONTEXT:
		pvr_fw_object_get_fw_addr_offset(fw_obj, offset,
						 &cleanup_req->cleanup_data.context_fw_addr);
		break;
	case ROGUE_FWIF_CLEANUP_HWRTDATA:
		pvr_fw_object_get_fw_addr_offset(fw_obj, offset,
						 &cleanup_req->cleanup_data.hwrt_data_fw_addr);
		break;
	case ROGUE_FWIF_CLEANUP_FREELIST:
		pvr_fw_object_get_fw_addr_offset(fw_obj, offset,
						 &cleanup_req->cleanup_data.freelist_fw_addr);
		break;
	default:
		err = -EINVAL;
		goto err_drm_dev_exit;
	}

	err = pvr_kccb_send_cmd(pvr_dev, &cmd, &slot_nr);
	if (err)
		goto err_drm_dev_exit;

	err = pvr_kccb_wait_for_completion(pvr_dev, slot_nr, HZ, &rtn);
	if (err)
		goto err_drm_dev_exit;

	if (rtn & ROGUE_FWIF_KCCB_RTN_SLOT_CLEANUP_BUSY)
		err = -EBUSY;

err_drm_dev_exit:
	drm_dev_exit(idx);

err_up_read:
	up_read(&pvr_dev->reset_sem);

	return err;
}

/**
 * pvr_fw_object_fw_map() - Map a FW object in firmware address space
 * @pvr_dev: Device pointer.
 * @fw_obj: FW object to map.
 * @dev_addr: Desired address in device space, if a specific address is
 *            required. 0 otherwise.
 *
 * Returns:
 *  * 0 on success, or
 *  * -%EINVAL if @fw_obj is already mapped but has no references, or
 *  * Any error returned by DRM.
 */
static int
pvr_fw_object_fw_map(struct pvr_device *pvr_dev, struct pvr_fw_object *fw_obj, u64 dev_addr)
{
	struct pvr_gem_object *pvr_obj = fw_obj->gem;
	struct drm_gem_object *gem_obj = gem_from_pvr_gem(pvr_obj);
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;

	int err;

	spin_lock(&fw_dev->fw_mm_lock);

	if (drm_mm_node_allocated(&fw_obj->fw_mm_node)) {
		err = -EINVAL;
		goto err_unlock;
	}

	if (!dev_addr) {
		/*
		 * Allocate from the main heap only (firmware heap minus
		 * config space).
		 */
		err = drm_mm_insert_node_in_range(&fw_dev->fw_mm, &fw_obj->fw_mm_node,
						  gem_obj->size, 0, 0,
						  fw_dev->fw_heap_info.gpu_addr,
						  fw_dev->fw_heap_info.gpu_addr +
						  fw_dev->fw_heap_info.size, 0);
		if (err)
			goto err_unlock;
	} else {
		fw_obj->fw_mm_node.start = dev_addr;
		fw_obj->fw_mm_node.size = gem_obj->size;
		err = drm_mm_reserve_node(&fw_dev->fw_mm, &fw_obj->fw_mm_node);
		if (err)
			goto err_unlock;
	}

	spin_unlock(&fw_dev->fw_mm_lock);

	/* Map object on GPU. */
	err = fw_dev->defs->vm_map(pvr_dev, fw_obj);
	if (err)
		goto err_remove_node;

	fw_obj->fw_addr_offset = (u32)(fw_obj->fw_mm_node.start - fw_dev->fw_mm_base);

	return 0;

err_remove_node:
	spin_lock(&fw_dev->fw_mm_lock);
	drm_mm_remove_node(&fw_obj->fw_mm_node);

err_unlock:
	spin_unlock(&fw_dev->fw_mm_lock);

	return err;
}

/**
 * pvr_fw_object_fw_unmap() - Unmap a previously mapped FW object
 * @fw_obj: FW object to unmap.
 *
 * Returns:
 *  * 0 on success, or
 *  * -%EINVAL if object is not currently mapped.
 */
static int
pvr_fw_object_fw_unmap(struct pvr_fw_object *fw_obj)
{
	struct pvr_gem_object *pvr_obj = fw_obj->gem;
	struct drm_gem_object *gem_obj = gem_from_pvr_gem(pvr_obj);
	struct pvr_device *pvr_dev = to_pvr_device(gem_obj->dev);
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;

	fw_dev->defs->vm_unmap(pvr_dev, fw_obj);

	spin_lock(&fw_dev->fw_mm_lock);

	if (!drm_mm_node_allocated(&fw_obj->fw_mm_node)) {
		spin_unlock(&fw_dev->fw_mm_lock);
		return -EINVAL;
	}

	drm_mm_remove_node(&fw_obj->fw_mm_node);

	spin_unlock(&fw_dev->fw_mm_lock);

	return 0;
}

static void *
pvr_fw_object_create_and_map_common(struct pvr_device *pvr_dev, size_t size,
				    u64 flags, u64 dev_addr,
				    void (*init)(void *cpu_ptr, void *priv),
				    void *init_priv, struct pvr_fw_object **fw_obj_out)
{
	struct pvr_fw_object *fw_obj;
	void *cpu_ptr;
	int err;

	/* %DRM_PVR_BO_PM_FW_PROTECT is implicit for FW objects. */
	flags |= DRM_PVR_BO_PM_FW_PROTECT;

	fw_obj = kzalloc(sizeof(*fw_obj), GFP_KERNEL);
	if (!fw_obj)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&fw_obj->node);
	fw_obj->init = init;
	fw_obj->init_priv = init_priv;

	fw_obj->gem = pvr_gem_object_create(pvr_dev, size, flags);
	if (IS_ERR(fw_obj->gem)) {
		err = PTR_ERR(fw_obj->gem);
		fw_obj->gem = NULL;
		goto err_put_object;
	}

	err = pvr_fw_object_fw_map(pvr_dev, fw_obj, dev_addr);
	if (err)
		goto err_put_object;

	cpu_ptr = pvr_fw_object_vmap(fw_obj);
	if (IS_ERR(cpu_ptr)) {
		err = PTR_ERR(cpu_ptr);
		goto err_put_object;
	}

	*fw_obj_out = fw_obj;

	if (fw_obj->init)
		fw_obj->init(cpu_ptr, fw_obj->init_priv);

	mutex_lock(&pvr_dev->fw_dev.fw_objs.lock);
	list_add_tail(&fw_obj->node, &pvr_dev->fw_dev.fw_objs.list);
	mutex_unlock(&pvr_dev->fw_dev.fw_objs.lock);

	return cpu_ptr;

err_put_object:
	pvr_fw_object_destroy(fw_obj);

	return ERR_PTR(err);
}

/**
 * pvr_fw_object_create() - Create a FW object and map to firmware
 * @pvr_dev: PowerVR device pointer.
 * @size: Size of object, in bytes.
 * @flags: Options which affect both this operation and future mapping
 * operations performed on the returned object. Must be a combination of
 * DRM_PVR_BO_* and/or PVR_BO_* flags.
 * @init: Initialisation callback.
 * @init_priv: Private pointer to pass to initialisation callback.
 * @fw_obj_out: Pointer to location to store created object pointer.
 *
 * %DRM_PVR_BO_DEVICE_PM_FW_PROTECT is implied for all FW objects. Consequently,
 * this function will fail if @flags has %DRM_PVR_BO_CPU_ALLOW_USERSPACE_ACCESS
 * set.
 *
 * Returns:
 *  * 0 on success, or
 *  * Any error returned by pvr_fw_object_create_common().
 */
int
pvr_fw_object_create(struct pvr_device *pvr_dev, size_t size, u64 flags,
		     void (*init)(void *cpu_ptr, void *priv), void *init_priv,
		     struct pvr_fw_object **fw_obj_out)
{
	void *cpu_ptr;

	cpu_ptr = pvr_fw_object_create_and_map_common(pvr_dev, size, flags, 0, init, init_priv,
						      fw_obj_out);
	if (IS_ERR(cpu_ptr))
		return PTR_ERR(cpu_ptr);

	pvr_fw_object_vunmap(*fw_obj_out);

	return 0;
}

/**
 * pvr_fw_object_create_and_map() - Create a FW object and map to firmware and CPU
 * @pvr_dev: PowerVR device pointer.
 * @size: Size of object, in bytes.
 * @flags: Options which affect both this operation and future mapping
 * operations performed on the returned object. Must be a combination of
 * DRM_PVR_BO_* and/or PVR_BO_* flags.
 * @init: Initialisation callback.
 * @init_priv: Private pointer to pass to initialisation callback.
 * @fw_obj_out: Pointer to location to store created object pointer.
 *
 * %DRM_PVR_BO_DEVICE_PM_FW_PROTECT is implied for all FW objects. Consequently,
 * this function will fail if @flags has %DRM_PVR_BO_CPU_ALLOW_USERSPACE_ACCESS
 * set.
 *
 * Caller is responsible for calling pvr_fw_object_vunmap() to release the CPU
 * mapping.
 *
 * Returns:
 *  * Pointer to CPU mapping of newly created object, or
 *  * Any error returned by pvr_fw_object_create(), or
 *  * Any error returned by pvr_fw_object_vmap().
 */
void *
pvr_fw_object_create_and_map(struct pvr_device *pvr_dev, size_t size, u64 flags,
			     void (*init)(void *cpu_ptr, void *priv),
			     void *init_priv, struct pvr_fw_object **fw_obj_out)
{
	return pvr_fw_object_create_and_map_common(pvr_dev, size, flags, 0, init, init_priv,
						   fw_obj_out);
}

/**
 * pvr_fw_object_create_and_map_offset() - Create a FW object and map to
 * firmware at the provided offset and to the CPU.
 * @pvr_dev: PowerVR device pointer.
 * @dev_offset: Base address of desired FW mapping, offset from start of FW heap.
 * @size: Size of object, in bytes.
 * @flags: Options which affect both this operation and future mapping
 * operations performed on the returned object. Must be a combination of
 * DRM_PVR_BO_* and/or PVR_BO_* flags.
 * @init: Initialisation callback.
 * @init_priv: Private pointer to pass to initialisation callback.
 * @fw_obj_out: Pointer to location to store created object pointer.
 *
 * %DRM_PVR_BO_DEVICE_PM_FW_PROTECT is implied for all FW objects. Consequently,
 * this function will fail if @flags has %DRM_PVR_BO_CPU_ALLOW_USERSPACE_ACCESS
 * set.
 *
 * Caller is responsible for calling pvr_fw_object_vunmap() to release the CPU
 * mapping.
 *
 * Returns:
 *  * Pointer to CPU mapping of newly created object, or
 *  * Any error returned by pvr_fw_object_create(), or
 *  * Any error returned by pvr_fw_object_vmap().
 */
void *
pvr_fw_object_create_and_map_offset(struct pvr_device *pvr_dev,
				    u32 dev_offset, size_t size, u64 flags,
				    void (*init)(void *cpu_ptr, void *priv),
				    void *init_priv, struct pvr_fw_object **fw_obj_out)
{
	u64 dev_addr = pvr_dev->fw_dev.fw_mm_base + dev_offset;

	return pvr_fw_object_create_and_map_common(pvr_dev, size, flags, dev_addr, init, init_priv,
						   fw_obj_out);
}

/**
 * pvr_fw_object_destroy() - Destroy a pvr_fw_object
 * @fw_obj: Pointer to object to destroy.
 */
void pvr_fw_object_destroy(struct pvr_fw_object *fw_obj)
{
	struct pvr_gem_object *pvr_obj = fw_obj->gem;
	struct drm_gem_object *gem_obj = gem_from_pvr_gem(pvr_obj);
	struct pvr_device *pvr_dev = to_pvr_device(gem_obj->dev);

	mutex_lock(&pvr_dev->fw_dev.fw_objs.lock);
	list_del(&fw_obj->node);
	mutex_unlock(&pvr_dev->fw_dev.fw_objs.lock);

	if (drm_mm_node_allocated(&fw_obj->fw_mm_node)) {
		/* If we can't unmap, leak the memory. */
		if (WARN_ON(pvr_fw_object_fw_unmap(fw_obj)))
			return;
	}

	if (fw_obj->gem)
		pvr_gem_object_put(fw_obj->gem);

	kfree(fw_obj);
}

/**
 * pvr_fw_object_get_fw_addr_offset() - Return address of object in firmware address space, with
 * given offset.
 * @fw_obj: Pointer to object.
 * @offset: Desired offset from start of object.
 * @fw_addr_out: Location to store address to.
 */
void pvr_fw_object_get_fw_addr_offset(struct pvr_fw_object *fw_obj, u32 offset, u32 *fw_addr_out)
{
	struct pvr_gem_object *pvr_obj = fw_obj->gem;
	struct pvr_device *pvr_dev = to_pvr_device(gem_from_pvr_gem(pvr_obj)->dev);

	*fw_addr_out = pvr_dev->fw_dev.defs->get_fw_addr_with_offset(fw_obj, offset);
}

/*
 * pvr_fw_hard_reset() - Re-initialise the FW code and data segments, and reset all global FW
 *                       structures
 * @pvr_dev: Device pointer
 *
 * If this function returns an error then the caller must regard the device as lost.
 *
 * Returns:
 *  * 0 on success, or
 *  * Any error returned by pvr_fw_init_dev_structures() or pvr_fw_reset_all().
 */
int
pvr_fw_hard_reset(struct pvr_device *pvr_dev)
{
	struct list_head *pos;
	int err;

	/* Reset all FW objects */
	mutex_lock(&pvr_dev->fw_dev.fw_objs.lock);

	list_for_each(pos, &pvr_dev->fw_dev.fw_objs.list) {
		struct pvr_fw_object *fw_obj = container_of(pos, struct pvr_fw_object, node);
		void *cpu_ptr = pvr_fw_object_vmap(fw_obj);

		WARN_ON(IS_ERR(cpu_ptr));

		if (!(fw_obj->gem->flags & PVR_BO_FW_NO_CLEAR_ON_RESET)) {
			memset(cpu_ptr, 0, pvr_gem_object_size(fw_obj->gem));

			if (fw_obj->init)
				fw_obj->init(cpu_ptr, fw_obj->init_priv);
		}

		pvr_fw_object_vunmap(fw_obj);
	}

	mutex_unlock(&pvr_dev->fw_dev.fw_objs.lock);

	err = pvr_fw_reinit_code_data(pvr_dev);
	if (err)
		return err;

	return 0;
}
