/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_DEVICE_INFO_H
#define PVR_DEVICE_INFO_H

#include <linux/types.h>

struct pvr_device;

/*
 * struct pvr_device_features - Hardware feature information
 */
struct pvr_device_features {
	bool has_axi_acelite;
	bool has_cdm_control_stream_format;
	bool has_cluster_grouping;
	bool has_common_store_size_in_dwords;
	bool has_compute;
	bool has_compute_morton_capable;
	bool has_compute_overlap;
	bool has_coreid_per_os;
	bool has_dynamic_dust_power;
	bool has_ecc_rams;
	bool has_fb_cdc_v4;
	bool has_fbc_max_default_descriptors;
	bool has_fbc_max_large_descriptors;
	bool has_fbcdc;
	bool has_fbcdc_algorithm;
	bool has_fbcdc_architecture;
	bool has_gpu_multicore_support;
	bool has_gpu_virtualisation;
	bool has_gs_rta_support;
	bool has_irq_per_os;
	bool has_isp_max_tiles_in_flight;
	bool has_isp_samples_per_pixel;
	bool has_isp_zls_d24_s8_packing_ogl_mode;
	bool has_layout_mars;
	bool has_max_partitions;
	bool has_meta;
	bool has_meta_coremem_size;
	bool has_mips;
	bool has_num_clusters;
	bool has_num_isp_ipp_pipes;
	bool has_num_osids;
	bool has_num_raster_pipes;
	bool has_pbe2_in_xe;
	bool has_pbvnc_coreid_reg;
	bool has_perfbus;
	bool has_perf_counter_batch;
	bool has_phys_bus_width;
	bool has_riscv_fw_processor;
	bool has_roguexe;
	bool has_s7_top_infrastructure;
	bool has_simple_internal_parameter_format;
	bool has_simple_internal_parameter_format_v2;
	bool has_simple_parameter_format_version;
	bool has_slc_banks;
	bool has_slc_cache_line_size_bits;
	bool has_slc_size_configurable;
	bool has_slc_size_in_kilobytes;
	bool has_soc_timer;
	bool has_sys_bus_secure_reset;
	bool has_tessellation;
	bool has_tile_region_protection;
	bool has_tile_size_x;
	bool has_tile_size_y;
	bool has_tla;
	bool has_tpu_cem_datamaster_global_registers;
	bool has_tpu_dm_global_registers;
	bool has_tpu_filtering_mode_control;
	bool has_usc_min_output_registers_per_pix;
	bool has_vdm_drawindirect;
	bool has_vdm_object_level_lls;
	bool has_virtual_address_space_bits;
	bool has_watchdog_timer;
	bool has_workgroup_protection;
	bool has_xe_architecture;
	bool has_xe_memory_hierarchy;
	bool has_xe_tpu2;
	bool has_xpu_max_regbanks_addr_width;
	bool has_xpu_max_slaves;
	bool has_xpu_register_broadcast;
	bool has_xt_top_infrastructure;
	bool has_zls_subtile;

	u64 cdm_control_stream_format;
	u64 common_store_size_in_dwords;
	u64 ecc_rams;
	u64 fbc_max_default_descriptors;
	u64 fbc_max_large_descriptors;
	u64 fbcdc;
	u64 fbcdc_algorithm;
	u64 fbcdc_architecture;
	u64 isp_max_tiles_in_flight;
	u64 isp_samples_per_pixel;
	u64 layout_mars;
	u64 max_partitions;
	u64 meta;
	u64 meta_coremem_size;
	u64 num_clusters;
	u64 num_isp_ipp_pipes;
	u64 num_osids;
	u64 num_raster_pipes;
	u64 phys_bus_width;
	u64 simple_parameter_format_version;
	u64 slc_banks;
	u64 slc_cache_line_size_bits;
	u64 slc_size_in_kilobytes;
	u64 tile_size_x;
	u64 tile_size_y;
	u64 usc_min_output_registers_per_pix;
	u64 virtual_address_space_bits;
	u64 xe_architecture;
	u64 xpu_max_regbanks_addr_width;
	u64 xpu_max_slaves;
	u64 xpu_register_broadcast;
};

/*
 * struct pvr_device_quirks - Hardware quirk information
 */
struct pvr_device_quirks {
	bool has_brn44079;
	bool has_brn47217;
	bool has_brn48492;
	bool has_brn48545;
	bool has_brn49927;
	bool has_brn50767;
	bool has_brn51764;
	bool has_brn62269;
	bool has_brn63142;
	bool has_brn63553;
	bool has_brn66011;
	bool has_brn71242;
};

/*
 * struct pvr_device_enhancements - Hardware enhancement information
 */
struct pvr_device_enhancements {
	bool has_ern35421;
	bool has_ern38020;
	bool has_ern38748;
	bool has_ern42064;
	bool has_ern42290;
	bool has_ern42606;
	bool has_ern47025;
	bool has_ern57596;
};

void pvr_device_info_set_quirks(struct pvr_device *pvr_dev, const u64 *bitmask,
				u32 bitmask_len);
void pvr_device_info_set_enhancements(struct pvr_device *pvr_dev, const u64 *bitmask,
				      u32 bitmask_len);
int pvr_device_info_set_features(struct pvr_device *pvr_dev, const u64 *features, u32 features_size,
				 u32 feature_param_size);

/*
 * Meta cores
 *
 * These are the values for the 'meta' feature when the feature is present
 * (as per &struct pvr_device_features)/
 */
#define PVR_META_MTP218 (1)
#define PVR_META_MTP219 (2)
#define PVR_META_LTP218 (3)
#define PVR_META_LTP217 (4)

enum {
	PVR_FEATURE_CDM_USER_MODE_QUEUE,
	PVR_FEATURE_CLUSTER_GROUPING,
	PVR_FEATURE_COMPUTE_MORTON_CAPABLE,
	PVR_FEATURE_FB_CDC_V4,
	PVR_FEATURE_GPU_MULTICORE_SUPPORT,
	PVR_FEATURE_ISP_ZLS_D24_S8_PACKING_OGL_MODE,
	PVR_FEATURE_REQUIRES_FB_CDC_ZLS_SETUP,
	PVR_FEATURE_S7_TOP_INFRASTRUCTURE,
	PVR_FEATURE_TESSELLATION,
	PVR_FEATURE_TPU_DM_GLOBAL_REGISTERS,
	PVR_FEATURE_VDM_DRAWINDIRECT,
	PVR_FEATURE_VDM_OBJECT_LEVEL_LLS,
	PVR_FEATURE_ZLS_SUBTILE,
};

#endif /* PVR_DEVICE_INFO_H */
