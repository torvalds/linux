// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#ifndef __DML2_INTERNAL_SHARED_TYPES_H__
#define __DML2_INTERNAL_SHARED_TYPES_H__

#include "dml2_external_lib_deps.h"
#include "dml_top_types.h"
#include "dml2_core_shared_types.h"

/*
* DML2 MCG Types and Interfaces
*/

#define DML_MCG_MAX_CLK_TABLE_SIZE 20

struct dram_bw_to_min_clk_table_entry {
	unsigned long long pre_derate_dram_bw_kbps;
	unsigned long min_fclk_khz;
	unsigned long min_dcfclk_khz;
};

struct dml2_mcg_dram_bw_to_min_clk_table {
	struct dram_bw_to_min_clk_table_entry entries[DML_MCG_MAX_CLK_TABLE_SIZE];

	unsigned int num_entries;
};

struct dml2_mcg_min_clock_table {
	struct {
		unsigned int dispclk;
		unsigned int dppclk;
		unsigned int dscclk;
		unsigned int dtbclk;
		unsigned int phyclk;
		unsigned int fclk;
		unsigned int dcfclk;
	} max_clocks_khz;

	struct {
		unsigned int dprefclk;
		unsigned int xtalclk;
		unsigned int pcierefclk;
		unsigned int dchubrefclk;
		unsigned int amclk;
	} fixed_clocks_khz;

	struct dml2_mcg_dram_bw_to_min_clk_table dram_bw_table;
};

struct dml2_mcg_build_min_clock_table_params_in_out {
	/*
	* Input
	*/
	struct dml2_soc_bb *soc_bb;
	struct {
		bool perform_pseudo_build;
	} clean_me_up;

	/*
	* Output
	*/
	struct dml2_mcg_min_clock_table *min_clk_table;
};

struct dml2_mcg_instance {
	bool (*build_min_clock_table)(struct dml2_mcg_build_min_clock_table_params_in_out *in_out);
	bool (*unit_test)(void);
};

/*
* DML2 DPMM Types and Interfaces
*/

struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out {
	/*
	* Input
	*/
	struct dml2_core_ip_params *ip;
	struct dml2_soc_bb *soc_bb;
	struct dml2_mcg_min_clock_table *min_clk_table;
	const struct display_configuation_with_meta *display_cfg;

	struct {
		bool perform_pseudo_map;
		struct dml2_core_internal_soc_bb *soc_bb;
	} clean_me_up;

	/*
	* Output
	*/
	struct dml2_display_cfg_programming *programming;
};

struct dml2_dpmm_map_watermarks_params_in_out {
	/*
	* Input
	*/
	const struct display_configuation_with_meta *display_cfg;
	const struct dml2_core_instance *core;

	/*
	* Output
	*/
	struct dml2_display_cfg_programming *programming;
};

struct dml2_dpmm_instance {
	bool (*map_mode_to_soc_dpm)(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out);
	bool (*map_watermarks)(struct dml2_dpmm_map_watermarks_params_in_out *in_out);
	bool (*unit_test)(void);
};

/*
* DML2 Core Types and Interfaces
*/

struct dml2_core_initialize_in_out {
	enum dml2_project_id project_id;
	struct dml2_core_instance *instance;
	struct dml2_soc_bb *soc_bb;
	struct dml2_ip_capabilities *ip_caps;

	struct dml2_mcg_min_clock_table *minimum_clock_table;

	void *explicit_ip_bb;
	unsigned int explicit_ip_bb_size;

	// FIXME_STAGE2 can remove but dcn3 version still need this
	struct {
		struct soc_bounding_box_st *soc_bb;
		struct soc_states_st *soc_states;
	} legacy;
};

struct core_bandwidth_requirements {
	int urgent_bandwidth_kbytes_per_sec;
	int average_bandwidth_kbytes_per_sec;
};

struct core_plane_support_info {
	int dpps_used;
	int dram_change_latency_hiding_margin_in_active;
	int active_latency_hiding_us;
	int mall_svp_size_requirement_ways;
	int nominal_vblank_pstate_latency_hiding_us;
	unsigned int dram_change_vactive_det_fill_delay_us;
};

struct core_stream_support_info {
	unsigned int odms_used;
	unsigned int num_odm_output_segments; // for odm split mode (e.g. a value of 2 for odm_mode_mso_1to2)

	/* FAMS2 SubVP support info */
	unsigned int phantom_min_v_active;
	unsigned int phantom_v_startup;

	unsigned int phantom_v_active;
	unsigned int phantom_v_total;
	int vblank_reserved_time_us;
	int num_dsc_slices;
	bool dsc_enable;
};

struct core_display_cfg_support_info {
	bool is_supported;

	struct core_stream_support_info stream_support_info[DML2_MAX_PLANES];
	struct core_plane_support_info plane_support_info[DML2_MAX_PLANES];

	struct {
		struct dml2_core_internal_mode_support_info support_info;
	} clean_me_up;
};

struct dml2_core_mode_support_result {
	struct {
		struct {
			unsigned long urgent_bw_sdp_kbps;
			unsigned long average_bw_sdp_kbps;
			unsigned long urgent_bw_dram_kbps;
			unsigned long average_bw_dram_kbps;
			unsigned long dcfclk_khz;
			unsigned long fclk_khz;
		} svp_prefetch;

		struct {
			unsigned long urgent_bw_sdp_kbps;
			unsigned long average_bw_sdp_kbps;
			unsigned long urgent_bw_dram_kbps;
			unsigned long average_bw_dram_kbps;
			unsigned long dcfclk_khz;
			unsigned long fclk_khz;
		} active;

		unsigned int dispclk_khz;
		unsigned int dcfclk_deepsleep_khz;
		unsigned int socclk_khz;

		unsigned int uclk_pstate_supported;
		unsigned int fclk_pstate_supported;
	} global;

	struct {
		unsigned int dscclk_khz;
		unsigned int dtbclk_khz;
		unsigned int phyclk_khz;
	} per_stream[DML2_MAX_PLANES];

	struct {
		unsigned int dppclk_khz;
		unsigned int mall_svp_allocation_mblks;
		unsigned int mall_full_frame_allocation_mblks;
	} per_plane[DML2_MAX_PLANES];

	struct core_display_cfg_support_info cfg_support_info;
};

struct dml2_optimization_stage1_state {
	bool performed;
	bool success;

	int min_clk_index_for_latency;
};

struct dml2_optimization_stage2_state {
	bool performed;
	bool success;

	// Whether or not each plane supports mcache
	// The number of valid elements == display_cfg.num_planes
	// The indexing of pstate_switch_modes matches plane_descriptors[]
	bool per_plane_mcache_support[DML2_MAX_PLANES];
	struct dml2_mcache_surface_allocation mcache_allocations[DML2_MAX_PLANES];
};

#define DML2_PMO_LEGACY_PREFETCH_MAX_TWAIT_OPTIONS 8
#define DML2_PMO_PSTATE_CANDIDATE_LIST_SIZE 10
#define DML2_PMO_STUTTER_CANDIDATE_LIST_SIZE 3

struct dml2_implicit_svp_meta {
	bool valid;
	unsigned long v_active;
	unsigned long v_total;
	unsigned long v_front_porch;
};

struct dml2_fams2_per_method_common_meta {
	/* generic params */
	unsigned int allow_start_otg_vline;
	unsigned int allow_end_otg_vline;
	/* scheduling params */
	double allow_time_us;
	double disallow_time_us;
	double period_us;
};

struct dml2_fams2_meta {
	bool valid;
	double otg_vline_time_us;
	unsigned int scheduling_delay_otg_vlines;
	unsigned int vertical_interrupt_ack_delay_otg_vlines;
	unsigned int allow_to_target_delay_otg_vlines;
	unsigned int contention_delay_otg_vlines;
	unsigned int min_allow_width_otg_vlines;
	unsigned int nom_vtotal;
	double nom_refresh_rate_hz;
	double nom_frame_time_us;
	unsigned int max_vtotal;
	double min_refresh_rate_hz;
	double max_frame_time_us;
	unsigned int dram_clk_change_blackout_otg_vlines;
	struct {
		double max_vactive_det_fill_delay_us;
		unsigned int max_vactive_det_fill_delay_otg_vlines;
		struct dml2_fams2_per_method_common_meta common;
	} method_vactive;
	struct {
		struct dml2_fams2_per_method_common_meta common;
	} method_vblank;
	struct {
		unsigned int programming_delay_otg_vlines;
		unsigned int df_throttle_delay_otg_vlines;
		unsigned int prefetch_to_mall_delay_otg_vlines;
		unsigned long phantom_vactive;
		unsigned long phantom_vfp;
		unsigned long phantom_vtotal;
		struct dml2_fams2_per_method_common_meta common;
	} method_subvp;
	struct {
		unsigned int programming_delay_otg_vlines;
		unsigned int stretched_vtotal;
		struct dml2_fams2_per_method_common_meta common;
	} method_drr;
};

struct dml2_optimization_stage3_state {
	bool performed;
	bool success;

	// The pstate support mode for each plane
	// The number of valid elements == display_cfg.num_planes
	// The indexing of pstate_switch_modes matches plane_descriptors[]
	enum dml2_uclk_pstate_support_method pstate_switch_modes[DML2_MAX_PLANES];

	// Meta-data for implicit SVP generation, indexed by stream index
	struct dml2_implicit_svp_meta stream_svp_meta[DML2_MAX_PLANES];

	// Meta-data for FAMS2
	bool fams2_required;
	struct dml2_fams2_meta stream_fams2_meta[DML2_MAX_PLANES];

	int min_clk_index_for_latency;
};

struct dml2_optimization_stage4_state {
	bool performed;
	bool success;
	bool unoptimizable_streams[DML2_MAX_DCN_PIPES];
};

struct dml2_optimization_stage5_state {
	bool performed;
	bool success;

	bool optimal_reserved_time_in_vblank_us;
	bool vblank_includes_z8_optimization;
};

struct display_configuation_with_meta {
	struct dml2_display_cfg display_config;

	struct dml2_core_mode_support_result mode_support_result;

	// Stage 1 = Min Clocks for Latency
	struct dml2_optimization_stage1_state stage1;

	// Stage 2 = MCache
	struct dml2_optimization_stage2_state stage2;

	// Stage 3 = UCLK PState
	struct dml2_optimization_stage3_state stage3;

	// Stage 4 = Vmin
	struct dml2_optimization_stage4_state stage4;

	// Stage 5 = Stutter
	struct dml2_optimization_stage5_state stage5;
};

struct dml2_core_mode_support_in_out {
	/*
	* Inputs
	*/
	struct dml2_core_instance *instance;
	const struct display_configuation_with_meta *display_cfg;

	struct dml2_mcg_min_clock_table *min_clk_table;
	int min_clk_index;

	/*
	* Outputs
	*/
	struct dml2_core_mode_support_result mode_support_result;

	struct {
		// Inputs
		struct dml_display_cfg_st *display_cfg;

		// Outputs
		struct dml_mode_support_info_st *support_info;
		unsigned int out_lowest_state_idx;
		unsigned int min_fclk_khz;
		unsigned int min_dcfclk_khz;
		unsigned int min_dram_speed_mts;
		unsigned int min_socclk_khz;
		unsigned int min_dscclk_khz;
		unsigned int min_dtbclk_khz;
		unsigned int min_phyclk_khz;
	} legacy;
};

struct dml2_core_mode_programming_in_out {
	/*
	* Inputs
	*/
	struct dml2_core_instance *instance;
	const struct display_configuation_with_meta *display_cfg;
	const struct core_display_cfg_support_info *cfg_support_info;

	/*
	* Outputs (also Input the clk freq are also from programming struct)
	*/
	struct dml2_display_cfg_programming *programming;

};

struct dml2_core_populate_informative_in_out {
	/*
	* Inputs
	*/
	struct dml2_core_instance *instance;

	// If this is set, then the mode was supported, and mode programming
	// was successfully run.
	// Otherwise, mode programming was not run, because mode support failed.
	bool mode_is_supported;

	/*
	* Outputs
	*/
	struct dml2_display_cfg_programming *programming;
};

struct dml2_calculate_mcache_allocation_in_out {
	/*
	* Inputs
	*/
	struct dml2_core_instance *instance;
	const struct dml2_plane_parameters *plane_descriptor;
	unsigned int plane_index;

	/*
	* Outputs
	*/
	struct dml2_mcache_surface_allocation *mcache_allocation;
};

struct dml2_core_internal_state_inputs {
	unsigned int dummy;
};

struct dml2_core_internal_state_intermediates {
	unsigned int dummy;
};

struct dml2_core_mode_support_locals {
	struct dml2_core_calcs_mode_support_ex mode_support_ex_params;
	struct dml2_display_cfg svp_expanded_display_cfg;
};

struct dml2_core_mode_programming_locals {
	struct dml2_core_calcs_mode_programming_ex mode_programming_ex_params;
	struct dml2_display_cfg svp_expanded_display_cfg;
};

struct dml2_core_scratch {
	struct dml2_core_mode_support_locals mode_support_locals;
	struct dml2_core_mode_programming_locals mode_programming_locals;
	int main_stream_index_from_svp_stream_index[DML2_MAX_PLANES];
	int svp_stream_index_from_main_stream_index[DML2_MAX_PLANES];
	int main_plane_index_to_phantom_plane_index[DML2_MAX_PLANES];
	int phantom_plane_index_to_main_plane_index[DML2_MAX_PLANES];
};

struct dml2_core_instance {
	struct dml2_mcg_min_clock_table *minimum_clock_table;
	struct dml2_core_internal_state_inputs inputs;
	struct dml2_core_internal_state_intermediates intermediates;

	struct dml2_core_scratch scratch;

	bool (*initialize)(struct dml2_core_initialize_in_out *in_out);
	bool (*mode_support)(struct dml2_core_mode_support_in_out *in_out);
	bool (*mode_programming)(struct dml2_core_mode_programming_in_out *in_out);
	bool (*populate_informative)(struct dml2_core_populate_informative_in_out *in_out);
	bool (*calculate_mcache_allocation)(struct dml2_calculate_mcache_allocation_in_out *in_out);
	bool (*unit_test)(void);

	struct {
		struct dml2_core_internal_display_mode_lib mode_lib;
	} clean_me_up;
};

/*
* DML2 PMO Types and Interfaces
*/

struct dml2_pmo_initialize_in_out {
	/*
	* Input
	*/
	struct dml2_pmo_instance *instance;
	struct dml2_soc_bb *soc_bb;
	struct dml2_ip_capabilities *ip_caps;
	struct dml2_pmo_options *options;
	int mcg_clock_table_size;
};

struct dml2_pmo_optimize_dcc_mcache_in_out {
	/*
	* Input
	*/
	struct dml2_pmo_instance *instance;
	const struct dml2_display_cfg *display_config;
	bool *dcc_mcache_supported;
	struct core_display_cfg_support_info *cfg_support_info;

	/*
	* Output
	*/
	struct dml2_display_cfg *optimized_display_cfg;
};

struct dml2_pmo_init_for_vmin_in_out {
	/*
	* Input
	*/
	struct dml2_pmo_instance *instance;
	struct display_configuation_with_meta *base_display_config;
};

struct dml2_pmo_test_for_vmin_in_out {
	/*
	* Input
	*/
	struct dml2_pmo_instance *instance;
	const struct display_configuation_with_meta *display_config;
	const struct dml2_soc_vmin_clock_limits *vmin_limits;
};

struct dml2_pmo_optimize_for_vmin_in_out {
	/*
	* Input
	*/
	struct dml2_pmo_instance *instance;
	struct display_configuation_with_meta *base_display_config;

	/*
	* Output
	*/
	struct display_configuation_with_meta *optimized_display_config;
};

struct dml2_pmo_init_for_pstate_support_in_out {
	/*
	* Input
	*/
	struct dml2_pmo_instance *instance;
	struct display_configuation_with_meta *base_display_config;
};

struct dml2_pmo_test_for_pstate_support_in_out {
	/*
	* Input
	*/
	struct dml2_pmo_instance *instance;
	struct display_configuation_with_meta *base_display_config;
};

struct dml2_pmo_optimize_for_pstate_support_in_out {
	/*
	* Input
	*/
	struct dml2_pmo_instance *instance;
	struct display_configuation_with_meta *base_display_config;
	bool last_candidate_failed;

	/*
	* Output
	*/
	struct display_configuation_with_meta *optimized_display_config;
};

struct dml2_pmo_init_for_stutter_in_out {
	/*
	* Input
	*/
	struct dml2_pmo_instance *instance;
	struct display_configuation_with_meta *base_display_config;
};

struct dml2_pmo_test_for_stutter_in_out {
	/*
	* Input
	*/
	struct dml2_pmo_instance *instance;
	struct display_configuation_with_meta *base_display_config;
};

struct dml2_pmo_optimize_for_stutter_in_out {
	/*
	* Input
	*/
	struct dml2_pmo_instance *instance;
	struct display_configuation_with_meta *base_display_config;
	bool last_candidate_failed;

	/*
	* Output
	*/
	struct display_configuation_with_meta *optimized_display_config;
};

enum dml2_pmo_pstate_strategy {
	dml2_pmo_pstate_strategy_na = 0,
	/* hw exclusive modes */
	dml2_pmo_pstate_strategy_vactive = 1,
	dml2_pmo_pstate_strategy_vblank = 2,
	dml2_pmo_pstate_strategy_reserved_hw = 5,
	/* fw assisted exclusive modes */
	dml2_pmo_pstate_strategy_fw_svp = 6,
	dml2_pmo_pstate_strategy_reserved_fw = 10,
	/* fw assisted modes requiring drr modulation */
	dml2_pmo_pstate_strategy_fw_vactive_drr = 11,
	dml2_pmo_pstate_strategy_fw_vblank_drr = 12,
	dml2_pmo_pstate_strategy_fw_svp_drr = 13,
	dml2_pmo_pstate_strategy_reserved_fw_drr_clamped = 20,
	dml2_pmo_pstate_strategy_fw_drr = 21,
	dml2_pmo_pstate_strategy_reserved_fw_drr_var = 22,
};

#define PMO_NO_DRR_STRATEGY_MASK (((1 << (dml2_pmo_pstate_strategy_reserved_fw - dml2_pmo_pstate_strategy_na + 1)) - 1) << dml2_pmo_pstate_strategy_na)
#define PMO_DRR_STRATEGY_MASK (((1 << (dml2_pmo_pstate_strategy_reserved_fw_drr_var - dml2_pmo_pstate_strategy_fw_vactive_drr + 1)) - 1) << dml2_pmo_pstate_strategy_fw_vactive_drr)
#define PMO_DRR_CLAMPED_STRATEGY_MASK (((1 << (dml2_pmo_pstate_strategy_reserved_fw_drr_clamped - dml2_pmo_pstate_strategy_fw_vactive_drr + 1)) - 1) << dml2_pmo_pstate_strategy_fw_vactive_drr)
#define PMO_DRR_VAR_STRATEGY_MASK (((1 << (dml2_pmo_pstate_strategy_reserved_fw_drr_var - dml2_pmo_pstate_strategy_fw_drr + 1)) - 1) << dml2_pmo_pstate_strategy_fw_drr)
#define PMO_FW_STRATEGY_MASK (((1 << (dml2_pmo_pstate_strategy_reserved_fw_drr_var - dml2_pmo_pstate_strategy_fw_svp + 1)) - 1) << dml2_pmo_pstate_strategy_fw_svp)

#define PMO_DCN4_MAX_DISPLAYS 4
#define PMO_DCN4_MAX_NUM_VARIANTS 2
#define PMO_DCN4_MAX_BASE_STRATEGIES 10

struct dml2_pmo_scratch {
	union {
		struct {
			double reserved_time_candidates[DML2_MAX_PLANES][DML2_PMO_LEGACY_PREFETCH_MAX_TWAIT_OPTIONS];
			int reserved_time_candidates_count[DML2_MAX_PLANES];
			int current_candidate[DML2_MAX_PLANES];
			int min_latency_index;
			int max_latency_index;
			int cur_latency_index;
			int stream_mask;
		} pmo_dcn3;
		struct {
			enum dml2_pmo_pstate_strategy per_stream_pstate_strategy[DML2_MAX_PLANES][DML2_PMO_PSTATE_CANDIDATE_LIST_SIZE];
			bool allow_state_increase_for_strategy[DML2_PMO_PSTATE_CANDIDATE_LIST_SIZE];
			int num_pstate_candidates;
			int cur_pstate_candidate;

			unsigned int stream_plane_mask[DML2_MAX_PLANES];

			unsigned int stream_vactive_capability_mask;

			int min_latency_index;
			int max_latency_index;
			int cur_latency_index;

			// Stores all the implicit SVP meta information indexed by stream index of the display
			// configuration under inspection, built at optimization stage init
			struct dml2_implicit_svp_meta stream_svp_meta[DML2_MAX_PLANES];
			struct dml2_fams2_meta stream_fams2_meta[DML2_MAX_PLANES];

			unsigned int optimal_vblank_reserved_time_for_stutter_us[DML2_PMO_STUTTER_CANDIDATE_LIST_SIZE];
			unsigned int num_stutter_candidates;
			unsigned int cur_stutter_candidate;
			bool z8_vblank_optimizable;

			/* mask of synchronized timings by stream index */
			unsigned int num_timing_groups;
			unsigned int synchronized_timing_group_masks[DML2_MAX_PLANES];
			bool group_is_drr_enabled[DML2_MAX_PLANES];
			double group_line_time_us[DML2_MAX_PLANES];

			/* scheduling check locals */
			struct dml2_fams2_per_method_common_meta group_common_fams2_meta[DML2_MAX_PLANES];
			unsigned int sorted_group_gtl_disallow_index[DML2_MAX_PLANES];
			unsigned int sorted_group_gtl_period_index[DML2_MAX_PLANES];
			double group_phase_offset[DML2_MAX_PLANES];
		} pmo_dcn4;
	};
};

struct dml2_pmo_init_data {
	union {
		struct {
			/* populated once during initialization */
			enum dml2_pmo_pstate_strategy expanded_strategy_list_1_display[PMO_DCN4_MAX_BASE_STRATEGIES * 2][PMO_DCN4_MAX_DISPLAYS];
			enum dml2_pmo_pstate_strategy expanded_strategy_list_2_display[PMO_DCN4_MAX_BASE_STRATEGIES * 2 * 2][PMO_DCN4_MAX_DISPLAYS];
			enum dml2_pmo_pstate_strategy expanded_strategy_list_3_display[PMO_DCN4_MAX_BASE_STRATEGIES * 6 * 2][PMO_DCN4_MAX_DISPLAYS];
			enum dml2_pmo_pstate_strategy expanded_strategy_list_4_display[PMO_DCN4_MAX_BASE_STRATEGIES * 24 * 2][PMO_DCN4_MAX_DISPLAYS];
			unsigned int num_expanded_strategies_per_list[PMO_DCN4_MAX_DISPLAYS];
		} pmo_dcn4;
	};
};

struct dml2_pmo_instance {
	struct dml2_soc_bb *soc_bb;
	struct dml2_ip_capabilities *ip_caps;

	struct dml2_pmo_options *options;

	int disp_clk_vmin_threshold;
	int mpc_combine_limit;
	int odm_combine_limit;
	int mcg_clock_table_size;

	union {
		struct {
			struct {
				int prefetch_end_to_mall_start_us;
				int fw_processing_delay_us;
				int refresh_rate_limit_min;
				int refresh_rate_limit_max;
			} subvp;
		} v1;
		struct {
			struct {
				int refresh_rate_limit_min;
				int refresh_rate_limit_max;
			} subvp;
			struct {
				int refresh_rate_limit_min;
				int refresh_rate_limit_max;
			} drr;
		} v2;
	} fams_params;

	bool (*initialize)(struct dml2_pmo_initialize_in_out *in_out);
	bool (*optimize_dcc_mcache)(struct dml2_pmo_optimize_dcc_mcache_in_out *in_out);

	bool (*init_for_vmin)(struct dml2_pmo_init_for_vmin_in_out *in_out);
	bool (*test_for_vmin)(struct dml2_pmo_test_for_vmin_in_out *in_out);
	bool (*optimize_for_vmin)(struct dml2_pmo_optimize_for_vmin_in_out *in_out);

	bool (*init_for_uclk_pstate)(struct dml2_pmo_init_for_pstate_support_in_out *in_out);
	bool (*test_for_uclk_pstate)(struct dml2_pmo_test_for_pstate_support_in_out *in_out);
	bool (*optimize_for_uclk_pstate)(struct dml2_pmo_optimize_for_pstate_support_in_out *in_out);

	bool (*init_for_stutter)(struct dml2_pmo_init_for_stutter_in_out *in_out);
	bool (*test_for_stutter)(struct dml2_pmo_test_for_stutter_in_out *in_out);
	bool (*optimize_for_stutter)(struct dml2_pmo_optimize_for_stutter_in_out *in_out);

	bool (*unit_test)(void);

	struct dml2_pmo_init_data init_data;
	struct dml2_pmo_scratch scratch;
};

/*
* DML2 MCache Types
*/

struct top_mcache_validate_admissability_in_out {
	struct dml2_instance *dml2_instance;

	const struct dml2_display_cfg *display_cfg;
	const struct core_display_cfg_support_info *cfg_support_info;
	struct dml2_mcache_surface_allocation *mcache_allocations;

	bool per_plane_status[DML2_MAX_PLANES];

	struct {
		const struct dml_mode_support_info_st *mode_support_info;
	} legacy;
};

struct top_mcache_assign_ids_in_out {
	/*
	* Input
	*/
	const struct dml2_mcache_surface_allocation *mcache_allocations;
	int plane_count;

	int per_pipe_viewport_x_start[DML2_MAX_PLANES][DML2_MAX_DCN_PIPES];
	int per_pipe_viewport_x_end[DML2_MAX_PLANES][DML2_MAX_DCN_PIPES];
	int pipe_count_per_plane[DML2_MAX_PLANES];

	struct dml2_display_mcache_regs *current_mcache_regs[DML2_MAX_PLANES][DML2_MAX_DCN_PIPES]; //One set per pipe/hubp

	/*
	* Output
	*/
	struct dml2_display_mcache_regs mcache_regs[DML2_MAX_PLANES][DML2_MAX_DCN_PIPES]; //One set per pipe/hubp
	struct dml2_build_mcache_programming_in_out *mcache_programming;
};

struct top_mcache_calc_mcache_count_and_offsets_in_out {
	/*
	* Inputs
	*/
	struct dml2_instance *dml2_instance;
	const struct dml2_display_cfg *display_config;

	/*
	* Outputs
	*/
	struct dml2_mcache_surface_allocation *mcache_allocations;
};

struct top_mcache_assign_global_mcache_ids_in_out {
	/*
	* Inputs/Outputs
	*/
	struct dml2_mcache_surface_allocation *allocations;
	int num_allocations;
};

/*
* DML2 Top Types
*/

struct dml2_initialize_instance_locals {
	int dummy;
};

struct dml2_optimization_init_function_locals {
	union {
		struct {
			struct dml2_pmo_init_for_pstate_support_in_out init_params;
		} uclk_pstate;
		struct {
			struct dml2_pmo_init_for_stutter_in_out stutter_params;
		} stutter;
		struct {
			struct dml2_pmo_init_for_vmin_in_out init_params;
		} vmin;
	};
};

struct dml2_optimization_test_function_locals {
	union {
		struct {
			struct top_mcache_calc_mcache_count_and_offsets_in_out calc_mcache_count_params;
			struct top_mcache_assign_global_mcache_ids_in_out assign_global_mcache_ids_params;
			struct top_mcache_validate_admissability_in_out validate_admissibility_params;
		} test_mcache;
		struct {
			struct dml2_pmo_test_for_vmin_in_out pmo_test_vmin_params;
		} test_vmin;
		struct {
			struct dml2_pmo_test_for_pstate_support_in_out test_params;
		} uclk_pstate;
		struct {
			struct dml2_pmo_test_for_stutter_in_out stutter_params;
		} stutter;
	};
};

struct dml2_optimization_optimize_function_locals {
	union {
		struct {
			struct dml2_pmo_optimize_dcc_mcache_in_out optimize_mcache_params;
		} optimize_mcache;
		struct {
			struct dml2_pmo_optimize_for_vmin_in_out pmo_optimize_vmin_params;
		} optimize_vmin;
		struct {
			struct dml2_pmo_optimize_for_pstate_support_in_out optimize_params;
		} uclk_pstate;
		struct {
			struct dml2_pmo_optimize_for_stutter_in_out stutter_params;
		} stutter;
	};
};

struct dml2_optimization_phase_locals {
	struct display_configuation_with_meta cur_candidate_display_cfg;
	struct display_configuation_with_meta next_candidate_display_cfg;
	struct dml2_core_mode_support_in_out mode_support_params;
	struct dml2_optimization_init_function_locals init_function_locals;
	struct dml2_optimization_test_function_locals test_function_locals;
	struct dml2_optimization_optimize_function_locals optimize_function_locals;
};

struct dml2_check_mode_supported_locals {
	struct dml2_display_cfg display_cfg_working_copy;
	struct dml2_core_mode_support_in_out mode_support_params;
	struct dml2_optimization_phase_locals optimization_phase_locals;
	struct display_configuation_with_meta base_display_config_with_meta;
	struct display_configuation_with_meta optimized_display_config_with_meta;
	struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out dppm_map_mode_params;
};

struct optimization_init_function_params {
	struct dml2_optimization_init_function_locals *locals;
	struct dml2_instance *dml;
	struct display_configuation_with_meta *display_config;
};

struct optimization_test_function_params {
	struct dml2_optimization_test_function_locals *locals;
	struct dml2_instance *dml;
	struct display_configuation_with_meta *display_config;
};

struct optimization_optimize_function_params {
	bool last_candidate_supported;
	struct dml2_optimization_optimize_function_locals *locals;
	struct dml2_instance *dml;
	struct display_configuation_with_meta *display_config;
	struct display_configuation_with_meta *optimized_display_config;
};

struct optimization_phase_params {
	struct dml2_instance *dml;
	const struct display_configuation_with_meta *display_config; // Initial Display Configuration
	bool (*init_function)(const struct optimization_init_function_params *params); // Test function to determine optimization is complete
	bool (*test_function)(const struct optimization_test_function_params *params); // Test function to determine optimization is complete
	bool (*optimize_function)(const struct optimization_optimize_function_params *params); // Function which produces a more optimized display configuration
	struct display_configuation_with_meta *optimized_display_config; // The optimized display configuration

	bool all_or_nothing;
};

struct dml2_build_mode_programming_locals {
	struct dml2_core_mode_support_in_out mode_support_params;
	struct dml2_core_mode_programming_in_out mode_programming_params;
	struct dml2_core_populate_informative_in_out informative_params;
	struct dml2_pmo_optimize_dcc_mcache_in_out optimize_mcache_params;
	struct display_configuation_with_meta base_display_config_with_meta;
	struct display_configuation_with_meta optimized_display_config_with_meta;
	struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out dppm_map_mode_params;
	struct dml2_dpmm_map_watermarks_params_in_out dppm_map_watermarks_params;
	struct dml2_optimization_phase_locals optimization_phase_locals;
	struct optimization_phase_params min_clock_for_latency_phase;
	struct optimization_phase_params mcache_phase;
	struct optimization_phase_params uclk_pstate_phase;
	struct optimization_phase_params vmin_phase;
	struct optimization_phase_params stutter_phase;
};

struct dml2_legacy_core_build_mode_programming_wrapper_locals {
	struct dml2_core_mode_support_in_out mode_support_params;
	struct dml2_core_mode_programming_in_out mode_programming_params;
	struct dml2_core_populate_informative_in_out informative_params;
	struct top_mcache_calc_mcache_count_and_offsets_in_out calc_mcache_count_params;
	struct top_mcache_validate_admissability_in_out validate_admissibility_params;
	struct dml2_mcache_surface_allocation mcache_allocations[DML2_MAX_PLANES];
	struct top_mcache_assign_global_mcache_ids_in_out assign_global_mcache_ids_params;
	struct dml2_pmo_optimize_dcc_mcache_in_out optimize_mcache_params;
	struct dml2_display_cfg optimized_display_cfg;
	struct core_display_cfg_support_info core_support_info;
};

struct dml2_top_mcache_verify_mcache_size_locals {
	struct dml2_calculate_mcache_allocation_in_out calc_mcache_params;
};

struct dml2_top_mcache_validate_admissability_locals {
	struct {
		int pipe_vp_startx[DML2_MAX_DCN_PIPES];
		int pipe_vp_endx[DML2_MAX_DCN_PIPES];
	} plane0;
	struct {
		int pipe_vp_startx[DML2_MAX_DCN_PIPES];
		int pipe_vp_endx[DML2_MAX_DCN_PIPES];
	} plane1;
};

struct dml2_top_display_cfg_support_info {
	const struct dml2_display_cfg *display_config;
	struct core_display_cfg_support_info core_info;
	enum dml2_pstate_support_method per_plane_pstate_method[DML2_MAX_PLANES];
};

struct dml2_instance {
	enum dml2_project_id project_id;

	struct dml2_core_instance core_instance;
	struct dml2_mcg_instance mcg_instance;
	struct dml2_dpmm_instance dpmm_instance;
	struct dml2_pmo_instance pmo_instance;

	struct dml2_soc_bb soc_bbox;
	struct dml2_ip_capabilities ip_caps;

	struct dml2_mcg_min_clock_table min_clk_table;

	struct dml2_pmo_options pmo_options;

	struct {
		struct dml2_initialize_instance_locals initialize_instance_locals;
		struct dml2_top_mcache_verify_mcache_size_locals mcache_verify_mcache_size_locals;
		struct dml2_top_mcache_validate_admissability_locals mcache_validate_admissability_locals;
		struct dml2_check_mode_supported_locals check_mode_supported_locals;
		struct dml2_build_mode_programming_locals build_mode_programming_locals;
	} scratch;

	struct {
		struct {
			struct dml2_legacy_core_build_mode_programming_wrapper_locals legacy_core_build_mode_programming_wrapper_locals;
		} scratch;
	} legacy;
};
#endif
