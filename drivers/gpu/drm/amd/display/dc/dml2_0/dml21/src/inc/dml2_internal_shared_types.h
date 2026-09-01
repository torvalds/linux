// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_INTERNAL_SHARED_TYPES_H__
#define __DML2_INTERNAL_SHARED_TYPES_H__

#include "dml2_external_lib_deps.h"
#include "dml_top_types.h"
#include "dml2_core_shared_types.h"
#include "bounding_boxes/utm_qos_model_dchub_v1.h"
#include "bounding_boxes/utm_qos_model_dchub_v2.h"
#include "bounding_boxes/utm_qos_model_dchub_v3.h"
/*
* DML2 MCG Types and Interfaces
*/

#define DML_MCG_MAX_CLK_TABLE_SIZE 20

struct dram_bw_to_min_clk_table_entry {
	unsigned long long pre_derate_dram_bw_kbps;
	unsigned long min_uclk_khz;
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
		unsigned int dispclk;
		unsigned int dppclk;
		unsigned int dtbclk;
	} max_ss_clocks_khz;

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

struct dml2_soc_operating_point {
	unsigned int uclk_khz;
	unsigned int fclk_khz;
	unsigned int dcfclk_khz;
	unsigned int socclk_khz;
};

struct dml2_sop_constraint {
	union {
		struct {
			unsigned int min_sop_index;
			struct dml2_memory_path_latency latency;
			struct dml2_soc_operating_point clocks;
			double min_available_urgent_bandwidth_KBps; // minimum guaranteed urgent bandwidth at active
		} dcn5;
	};
};

struct dml2_sop_table {
	bool is_initialized;
	const struct utm_qos_model *model;
	uint32_t sop_min_available_urgent_bandwidths_KBps[MAX_UTM_SOP_COUNT];
	uint32_t sop_optimal_dcfclks_khz[MAX_UTM_SOP_COUNT];
	unsigned int (*get_highest_sop_index)(const struct dml2_sop_table *sop_table);
	void (*get_sop_constraint_at_index)(const struct dml2_sop_table *sop_table, unsigned int index, struct dml2_sop_constraint *constraint);
	bool (*is_bw_supported_at_index)(const struct dml2_sop_table *sop_table, const struct dml2_memory_path_bandwidth *bw, unsigned int index);
	void (*get_max_sop)(const struct dml2_sop_table *sop_table, struct dml2_soc_operating_point *sop);
	void (*get_min_sop)(const struct dml2_sop_table *sop_table, struct dml2_soc_operating_point *sop);
};

struct dml2_utm_soc_bb {
	struct dml2_sop_table sop_table;
	struct dml2_soc_power_management_parameters power_management_parameters;
	struct dml2_soc_vmin_clock_limits vmin_limit;
	struct dml2_dram_params dram_config;
	struct utm_qos_model qos_model;
	union {
		struct utm_qos_model_dchub_v1 qos_model_dchub_v1;
		struct utm_qos_model_dchub_v2 qos_model_dchub_v2;
		struct utm_qos_model_dchub_v3 qos_model_dchub_v3;
	};
	double lower_bound_bandwidth_dchub;
	double fraction_of_urgent_bandwidth_nominal_target;
	double fraction_of_urgent_bandwidth_flip_target;
	unsigned int dchub_refclk_mhz;
	unsigned int max_outstanding_reqs;
	unsigned long return_bus_width_bytes;
	double phy_downspread_percent;
	double dcn_downspread_percent;
	double nominal_sdp_derate_percent;
	double urgent_sdp_derate_percent;
	double dispclk_dppclk_vco_speed_mhz;
	bool no_dfs;
	unsigned int mem_word_bytes;
	unsigned int num_dcc_mcaches;
	unsigned int mcache_size_bytes;
	unsigned int mcache_line_size_bytes;
	unsigned int writeback_base_latency_us;
	unsigned int max_dispclk_khz;
	unsigned int max_dppclk_khz;
	unsigned int max_dscclk_khz;
	unsigned int max_dtbclk_khz;
	unsigned int max_phyclk_khz;
	unsigned int max_phyclk_d18_khz;
	unsigned int max_phyclk_d32_khz;
	unsigned int min_socclk_khz;
	unsigned int max_dcfclk_khz;
	unsigned int min_dcfclk_khz;
	/* TODO: remove once DML Core no longer depends on max SOP clocks */
	unsigned int max_uclk_khz;
	unsigned int max_fclk_khz;
};

struct dml2_clock_granularity_adjuster;
struct dml2_cga_initialize_in_out {
	struct dml2_clock_granularity_adjuster *adjuster;
	const struct dml2_soc_bb *soc_bb;
	const struct dml2_core_ip_params *ip;
};

struct dml2_clock_granularity_adjuster {
	double dcn_downspread_percent;
	double dispclk_dppclk_vco_speed_mhz;
	double dispclk_ramp_margin_percent;
	double max_dispclk_mhz;
	void (*initialize)(const struct dml2_cga_initialize_in_out *in_out);
	double (*adjust_dispclk_mhz)(const struct dml2_clock_granularity_adjuster *adjuster, double dispclk_mhz);
	void (*adjust_dppclks_mhz)(const struct dml2_clock_granularity_adjuster *adjuster, unsigned int count,
			const double *dppclks_mhz, double *adjusted_dppclks_mhz, double *adjusted_dpprefclk_mhz);
	void (*adjust_dtbclks_mhz)(const struct dml2_clock_granularity_adjuster *adjuster, unsigned int count,
			const double *dtbclks_mhz, double *adjusted_dtbclks_mhz, double *adjusted_dtbrefclk_mhz);
	double (*adjust_dcfclk_deepsleep_mhz)(const struct dml2_clock_granularity_adjuster *adjuster,
			double dcfclk_deepsleep_mhz);
};

#define DML2_STATUS_LIST(FORMAT) \
	/* Generic */ \
	FORMAT(DML2_STATUS_OK) \
	FORMAT(DML2_STATUS_UNKNOWN) \
	/* Validate */ \
	FORMAT(DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT) \
	FORMAT(DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT_PREFETCH) \
	FORMAT(DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT_PREFETCH_URGENT) \
	FORMAT(DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT_QOS_BANDWIDTH) \
	FORMAT(DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT_DCFCLK) \
	FORMAT(DML2_STATUS_VALIDATE_FAIL_PREFETCH) \
	FORMAT(DML2_STATUS_VALIDATE_FAIL_MCACHE) \
	FORMAT(DML2_STATUS_VALIDATE_FAIL_PMO_SANITY_TOTAL_PIPE_USAGE) \
	FORMAT(DML2_STATUS_VALIDATE_FAIL_PMO_SANITY_ODM_DIVISIBILITY) \
	FORMAT(DML2_STATUS_VALIDATE_FAIL_PSTATE_SCHEDULE) \
	FORMAT(DML2_STATUS_PSTATE_UNEXPECTED_PSTATE) \
	FORMAT(DML2_STATUS_PSTATE_NOT_ADMISSIBLE) \
	/* Optimize */ \
	FORMAT(DML2_STATUS_OPTIMIZE_FAIL_MCACHE) \
	FORMAT(DML2_STATUS_OPTIMIZE_FAIL_UCLK_PSTATE) \
	FORMAT(DML2_STATUS_OPTIMIZE_FAIL_QOS) \
	FORMAT(DML2_STATUS_OPTIMIZE_FAIL_VMIN) \
	FORMAT(DML2_STATUS_OPTIMIZE_FAIL_STUTTER) \
	FORMAT(DML2_STATUS_OPTIMIZE_FAIL_FCLK_PSTATE_UNSYNCHRONIZABLE_TIMINGS) \
	FORMAT(DML2_STATUS_OPTIMIZE_FAIL_FCLK_PSTATE_INSUFFICENT_HIDING) \
	FORMAT(DML2_STATUS_OPTIMIZE_FAIL_VMIN_DCFCLK) \
	FORMAT(DML2_STATUS_OPTIMIZE_FAIL_EXCEED_MAX_ITERATION) \
	/* Populate */ \
	FORMAT(DML2_STATUS_POPULATE_FAIL_MIN_CLOCK_STATE) \
	FORMAT(DML2_STATUS_POPULATE_FAIL_PROGRAMMING) \
	FORMAT(DML2_STATUS_POPULATE_FAIL_PROGRAMMING_PREFETCH) \
	FORMAT(DML2_STATUS_POPULATE_FAIL_PROGRAMMING_PREFETCH_URGENT) \
	FORMAT(DML2_STATUS_POPULATE_FAIL_PROGRAMMING_FLIP_BANDWIDTH) \
	FORMAT(DML2_STATUS_POPULATE_FAIL_PROGRAMMING_DCFCLK)

#define ENUM_FORMAT(entry) entry,
#define CASE_FORMAT(entry) case entry: return #entry;

enum dml2_status {
	DML2_STATUS_LIST(ENUM_FORMAT)
};

static inline const char *dml2_status_str(enum dml2_status status)
{
	switch (status) {
		DML2_STATUS_LIST(CASE_FORMAT)
	}

	return "";
}

struct dml2_mcg_instance {
	bool (*build_min_clock_table)(struct dml2_mcg_build_min_clock_table_params_in_out *in_out);
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
	const struct dml2_utm_soc_bb *utm_soc_bb;
	const struct dml2_display_solution *solution;
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
	const struct dml2_display_solution *solution;
	const struct dml2_core_instance *core;

	/*
	* Output
	*/
	struct dml2_display_cfg_programming *programming;
};

struct dml2_dpmm_scratch {
	struct dml2_display_cfg_programming programming;
};

struct dml2_dpmm_instance {
	bool (*map_mode_to_soc_dpm)(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out);
	bool (*map_watermarks)(struct dml2_dpmm_map_watermarks_params_in_out *in_out);

	struct dml2_dpmm_scratch dpmm_scratch;
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
	const struct dml2_utm_soc_bb *utm_soc_bb;
	const struct dml2_clock_granularity_adjuster *clock_adjuster;

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
	int vactive_det_fill_delay_us[dml2_pstate_type_count];
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
	unsigned int alternate_svp0_dst_lines;
	unsigned int alternate_svp1_dst_lines;
	unsigned int max_vstartup_lines;
	unsigned int max_dst_y_after_scaler;
	unsigned int max_dst_y_prefetch;
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
		unsigned int dpprefclk_khz;
		unsigned int dtbrefclk_khz;
		unsigned int dcfclk_deepsleep_khz;
		unsigned int socclk_khz;

		unsigned int uclk_pstate_supported;
		unsigned int fclk_pstate_supported;
		unsigned int alternate_total_bytes_copy_svp0;
		unsigned int alternate_total_bytes_copy_svp1;
		unsigned int lsdma_bw_req_for_alt_kbps;
		struct dml2_core_internal_watermarks watermarks;
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
	struct dml2_memory_path_bandwidth bandwidth_upper_bound;
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

struct dml2_pstate_per_method_common_meta {
	/* generic params */
	int allow_start_otg_vline;
	int allow_end_otg_vline;
	/* scheduling params */
	double allow_time_us;
	double disallow_time_us;
	double period_us;
};

struct dml2_pstate_meta {
	bool valid;
	double otg_vline_time_us;
	int scheduling_delay_otg_vlines;
	int vertical_interrupt_ack_delay_otg_vlines;
	int allow_to_target_delay_otg_vlines;
	int contention_delay_otg_vlines;
	int min_allow_width_otg_vlines;
	int nom_vtotal;
	int vblank_start;
	double nom_refresh_rate_hz;
	double nom_frame_time_us;
	int max_vtotal;
	double min_refresh_rate_hz;
	double max_frame_time_us;
	int blackout_otg_vlines;
	int max_allow_delay_otg_vlines;
	double nom_vblank_time_us;
	struct {
		double max_vactive_det_fill_delay_us;
		double vactive_latency_hiding_us;
		double reserved_vblank_required_us;
		int max_vactive_det_fill_delay_otg_vlines;
		int reserved_blank_required_vlines;
		struct dml2_pstate_per_method_common_meta common;
	} method_vactive;
	struct {
		struct dml2_pstate_per_method_common_meta common;
	} method_vblank;
	struct {
		int programming_delay_otg_vlines;
		int df_throttle_delay_otg_vlines;
		int prefetch_to_mall_delay_otg_vlines;
		unsigned long phantom_vactive;
		unsigned long phantom_vfp;
		unsigned long phantom_vtotal;
		struct dml2_pstate_per_method_common_meta common;
	} method_subvp;
	struct {
		int programming_delay_otg_vlines; // DMCUB <-> PMFW delays + any DMCUB/PMFW programming delays required
		int pmfw_throttle_delay_otg_vlines; // PMFW time it takes to throttle other clients + assert DF P-State allow
		struct dml2_pstate_per_method_common_meta common;
	} method_alternate;
	struct {
		int programming_delay_otg_vlines;
		int stretched_vtotal;
		struct dml2_pstate_per_method_common_meta common;
	} method_drr;
};

/* mask of synchronized timings by stream index */
struct dml2_pmo_synchronized_timing_groups {
	unsigned int num_timing_groups;
	unsigned int synchronized_timing_group_masks[DML2_MAX_PLANES];
	bool group_is_drr_enabled[DML2_MAX_PLANES];
	bool group_is_drr_active[DML2_MAX_PLANES];
	double group_line_time_us[DML2_MAX_PLANES];
};

struct dml2_optimization_stage3_state {
	bool performed;
	bool success;

	// The pstate support mode for each plane
	// The number of valid elements == display_cfg.num_planes
	// The indexing of pstate_switch_modes matches plane_descriptors[]
	enum dml2_pstate_method pstate_switch_modes[DML2_MAX_PLANES];

	// Meta-data for implicit SVP generation, indexed by stream index
	struct dml2_implicit_svp_meta stream_svp_meta[DML2_MAX_PLANES];

	// Meta-data for FAMS2
	bool fams2_required;
	struct dml2_pstate_meta stream_pstate_meta[DML2_MAX_PLANES];

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

struct dml2_pmo_pstate_strategy {
	enum dml2_pstate_method per_stream_pstate_method[DML2_MAX_PLANES];
	bool allow_state_increase;
};

struct dml2_validation_result {
	bool is_mode_support_valid;
	bool is_prefetch_valid;
	struct dml2_core_mode_support_result mode_support;
	bool is_mcache_allocation_valid;
	struct dml2_mcache_surface_allocation mcache_allocations[DML2_MAX_PLANES];
};

struct dml2_optimization_change {
	union {
		struct {
			bool sop_index				: 1; // bit 0
			bool mpc_combine_overrides		: 1;
			bool odm_combine_overrides		: 1;
			bool reserved_vblank_time		: 1;
			bool mcache_allocation			: 1;
			bool uclk_pstate_method			: 1;
			bool fclk_pstate_support		: 1;
			bool stutter_support			: 1;
			bool dcfclk_override			: 1; // bit 8
			bool ppt_temp_read_pstate_support	: 1;
			unsigned char reserved			: 6;
		} bits;
		unsigned short raw;
	};
};

struct dml2_optimization_worksheet {
	const struct dml2_display_cfg *orig_dispcfg;

	unsigned int timing_group_ids[DML2_MAX_PLANES]; // per plane
	unsigned int timing_group_count;

	struct {
		bool is_default_pipe_usage_attempted;
		bool is_single_stream_odm_case;
		bool per_plane_status[DML2_MAX_PLANES];
		struct {
			int pipe_vp_startx[DML2_MAX_DCN_PIPES];
			int pipe_vp_endx[DML2_MAX_DCN_PIPES];
		} plane0;
		struct {
			int pipe_vp_startx[DML2_MAX_DCN_PIPES];
			int pipe_vp_endx[DML2_MAX_DCN_PIPES];
		} plane1;
	} mcache;

	struct {
		unsigned int stream_plane_mask[DML2_MAX_PLANES];

		struct dml2_pstate_meta stream_pstate_meta[DML2_MAX_PLANES]; // per stream

		// Meta-data for implicit SVP generation, indexed by stream index
		struct dml2_implicit_svp_meta stream_svp_meta[DML2_MAX_PLANES];

		struct dml2_pmo_pstate_strategy pstate_strategy_candidates[DML2_PMO_PSTATE_CANDIDATE_LIST_SIZE];
		int num_pstate_candidates;
		int cur_pstate_candidate;

		// Initial value of reserved vblank time as pstate optimize may overwrite and clear current
		long init_reserved_vblank_time_ns[DML2_MAX_PLANES];
		unsigned int init_max_vactive_det_fill_delay_us[DML2_MAX_PLANES]; // per plane
	} uclk_pstate;

	struct {
		bool is_initialized;
		bool unoptimizable_streams[DML2_MAX_DCN_PIPES];
		unsigned int init_odms_used[DML2_MAX_DCN_PIPES];
	} vmin;

	struct {
		long init_reserved_vblank_time_ns[DML2_MAX_PLANES];
		bool should_optimize_z8_stutter;
		bool is_z8_stutter_attempted;
		bool should_optimize_stutter;
		bool is_stutter_attempted;
	} stutter;

	struct {
		unsigned int passing_index;
		unsigned int failing_index;
		bool is_index0_tested;
	} qos;

	struct {
		double max_available_bandwidth_kbps;
	} dcfclk_vmin;

	struct {
		bool is_attempted;
	} fclk_ppt_temp_read_pstate;

	/*
	 * unified structure for storing current optimization tuning variables. Please only store the final tuning knobs
	 * we should see exactly what will be optimized in display solution at a quick glance.
	 */
	struct dml2_optimization_config {
		struct  {
			unsigned int min_sop_index;
			unsigned int mpc_combine_overrides[DML2_MAX_PLANES]; // per plane
			unsigned int odm_combine_overrides[DML2_MAX_PLANES]; // per stream
			long reserved_vblank_time_ns[DML2_MAX_PLANES];
			struct dml2_mcache_surface_allocation mcache_allocations[DML2_MAX_PLANES];

			bool uclk_pstate_support;
			enum dml2_pstate_method uclk_pstate_switch_modes[DML2_MAX_PLANES]; // per plane
			int max_vactive_det_fill_delay_us[DML2_MAX_PLANES][dml2_pstate_type_count]; // per plane
			// Meta-data for FAMS2
			bool fams2_required;
			bool legacy_pstate_info_for_dmu;
			struct dml2_pstate_meta stream_pstate_meta[DML2_MAX_PLANES]; // per stream
			bool fclk_pstate_support;
			bool ppt_temp_read_support;
			bool stutter_support_in_vblank;
			bool z8_stutter_support_in_vblank;
			bool enable_vmin_dcfclk;
		} config;
		/* changes that have not yet been validated */
		struct dml2_optimization_change unvalidated_change;
	} cur;

	/* post validation */
	struct dml2_validation_result validation_result;
};

struct dml2_display_solution {
	const struct dml2_display_cfg *orig_dispcfg;

	/* current display configuration */
	struct dml2_display_cfg dispcfg;
	unsigned int timing_group_ids[DML2_MAX_PLANES]; // per plane
	unsigned int timing_group_count;

	/* additional DML internally decided configurations */
	struct dml2_sop_constraint sop_constraint;
	struct dml2_mcache_surface_allocation mcache_allocations[DML2_MAX_PLANES];
	struct dml2_uclk_pstate_params {
		bool support;
		/* Uclk pstate related*/
		enum dml2_pstate_method pstate_switch_modes[DML2_MAX_PLANES];
		// Meta-data for FAMS2
		bool fams2_required;
		bool legacy_pstate_info_for_dmu;
		struct dml2_pstate_meta stream_pstate_meta[DML2_MAX_PLANES]; // per stream
	} uclk_pstate_params;
	bool fclk_pstate_support;
	bool ppt_temp_read_support;
	bool stutter_support_in_vblank;
	bool z8_stutter_support_in_vblank;

	/* pre-validation states */
	struct dml2_optimization_change unvalidated_change;

	/* post validation */
	struct dml2_validation_result validation_result;
};

struct dml2_pmo_stage_optimizer {
	const struct dml2_pmo_instance *pmo;
	/*
	 * to optimize stack memory usage, large local variables are pre-allocated in this heap memory. The scope of
	 * func_locals is bounded by each optimizer's function defined in the union. To share states across an
	 * optimizer's functions, define it in the dedicated optimizer state in optimization worksheet.
	 */
	union dml2_stage_optimizer_function_locals *func_locals;
	/*
	 * init interface builds the initial states associated with the current stage optimizer into the worksheet. It
	 * is for state initialization only. It should not apply new optimization or cause changes to current validation
	 * result. It is safe to assume that the worksheet passed in or exited from this interface is always validated.
	 */
	void (*init)(struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet);
	/*
	 * optimize_next interface controls current optimization's stop conditions. When the interface returns false, it
	 * means the stage optimizer no longer needs to attempt further optimization. The current worksheet should be
	 * left unmodified. When the interface returns true, it means the stage optimizer applied new optimization to
	 * the worksheet. DML top will need to validate and test permissibility again. The worksheet passed in is based
	 * off the optimization decision from last attempt. It may or may not be validated or permissible. It is upto
	 * DML top to keep track of the last valid permissible worksheet. This interface is also responsible to clear
	 * corresponding valid bits in worksheet's validation result based on what optimization it gets applied. When
	 * the valid bits are cleared, it will be revalidated by top. Otherwise, DML top will assume it is safe to skip
	 * certain re-validations based on the remaining valid bits. Stage optimizers should clear only the necessary
	 * valid bits based on the optimization applied to speed up the process.
	 */
	bool (*optimize_next)(struct dml2_pmo_stage_optimizer *stage, struct dml2_optimization_worksheet *worksheet);
	/*
	 * test_permissibility interface should only check against current optimizer policy specific
	 * minimum requirements. Test permissibility result is orthogonal to validation result. It is
	 * safe to assume the worksheet constant passed in is always validated. The interface checks if
	 * the validated result fulfills the minimum requirements additionally imposed by current stage
	 * optimizer in order to consider the current optimization as a potential candidate. Stage
	 * optimizer may still attempt further optimization even if the current one is permissible.
	 */
	enum dml2_status (*test_permissibility)(struct dml2_pmo_stage_optimizer *stage,
			const struct dml2_optimization_worksheet *worksheet);
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

struct dml2_core_calculate_mp_context {
	const struct dml2_display_cfg *display_cfg;
	const struct dml2_core_ip_params *ip;
	const struct dml2_utm_soc_bb *soc_bb;
	const struct dml2_core_internal_mode_support *ms;
	struct dml2_core_calcs_mode_programming_locals *dummies;
	struct dml2_core_internal_scratch *func_params;
};
struct dml2_core_calculate_ms_context {
	const struct dml2_display_cfg *display_cfg;
	const struct dml2_core_ip_params *ip;
	const struct dml2_utm_soc_bb *soc_bb;
	const struct dml2_clock_granularity_adjuster *clock_adjuster;
	struct dml2_core_calcs_mode_support_locals *dummies;
	struct dml2_core_internal_scratch *func_params;
};

struct dml2_core_mode_support_locals {
	union {
		struct dml2_core_calcs_mode_support_ex mode_support_ex_params;
		struct dml2_core_calculate_ms_context calc_ms_ctx;
	};
	struct dml2_display_cfg svp_expanded_display_cfg;
	struct dml2_calculate_mcache_allocation_in_out calc_mcache_allocation_params;
};

struct dml2_core_mode_programming_locals {
	union {
		struct dml2_core_calcs_mode_programming_ex mode_programming_ex_params;
		struct dml2_core_calculate_mp_context calc_mp_ctx;
	};
	struct dml2_display_cfg svp_expanded_display_cfg;
	struct dml2_validation_result temp_result;
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
	const struct dml2_utm_soc_bb *utm_soc_bb;
	const struct dml2_clock_granularity_adjuster *clock_adjuster;
	struct dml2_core_internal_state_inputs inputs;
	struct dml2_core_internal_state_intermediates intermediates;

	struct dml2_core_scratch scratch;

	bool (*initialize)(struct dml2_core_initialize_in_out *in_out);
	bool (*mode_support)(struct dml2_core_mode_support_in_out *in_out);
	enum dml2_status (*validate_solution)(struct dml2_core_instance *core,
			const struct dml2_display_solution *solution,
			struct dml2_validation_result *validation_result);
	enum dml2_status (*populate_programming)(struct dml2_core_instance *core,
			const struct dml2_display_solution *solution,
			struct dml2_display_cfg_programming *programming);
	bool (*mode_programming)(struct dml2_core_mode_programming_in_out *in_out);
	bool (*populate_informative)(struct dml2_core_populate_informative_in_out *in_out);
	bool (*calculate_mcache_allocation)(struct dml2_calculate_mcache_allocation_in_out *in_out);


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
	const struct dml2_utm_soc_bb *utm_soc_bb;
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

#define PMO_NO_DRR_STRATEGY_MASK (((1 << (dml2_pstate_method_reserved_fw - dml2_pstate_method_na + 1)) - 1) << dml2_pstate_method_na)
#define PMO_DRR_STRATEGY_MASK (((1 << (dml2_pstate_method_reserved_fw_drr_var - dml2_pstate_method_fw_vactive_drr + 1)) - 1) << dml2_pstate_method_fw_vactive_drr)
#define PMO_DRR_CLAMPED_STRATEGY_MASK (((1 << (dml2_pstate_method_reserved_fw_drr_clamped - dml2_pstate_method_fw_vactive_drr + 1)) - 1) << dml2_pstate_method_fw_vactive_drr)
#define PMO_DRR_VAR_STRATEGY_MASK (((1 << (dml2_pstate_method_reserved_fw_drr_var - dml2_pstate_method_fw_drr + 1)) - 1) << dml2_pstate_method_fw_drr)
#define PMO_FW_STRATEGY_MASK (((1 << (dml2_pstate_method_reserved_fw_drr_var - dml2_pstate_method_fw_svp + 1)) - 1) << dml2_pstate_method_fw_svp)

#define PMO_DCN4_MAX_DISPLAYS 4
#define PMO_DCN4_MAX_NUM_VARIANTS 2
#define PMO_DCN4_MAX_BASE_STRATEGIES 10

struct dml2_scheduling_check_locals {
	struct dml2_pstate_per_method_common_meta group_common_pstate_meta[DML2_MAX_PLANES];
	unsigned int sorted_group_gtl_disallow_index[DML2_MAX_PLANES];
	unsigned int sorted_group_gtl_period_index[DML2_MAX_PLANES];
};

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
			struct dml2_pmo_pstate_strategy expanded_override_strategy_list[2 * 2 * 2 * 2];
			unsigned int num_expanded_override_strategies;
			struct dml2_pmo_pstate_strategy pstate_strategy_candidates[DML2_PMO_PSTATE_CANDIDATE_LIST_SIZE];
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
			struct dml2_pstate_meta stream_pstate_meta[DML2_MAX_PLANES];

			unsigned int optimal_vblank_reserved_time_for_stutter_us[DML2_PMO_STUTTER_CANDIDATE_LIST_SIZE];
			unsigned int num_stutter_candidates;
			unsigned int cur_stutter_candidate;
			bool z8_vblank_optimizable;

			/* mask of synchronized timings by stream index */
			unsigned int num_timing_groups;
			unsigned int synchronized_timing_group_masks[DML2_MAX_PLANES];
			bool group_is_drr_enabled[DML2_MAX_PLANES];
			bool group_is_drr_active[DML2_MAX_PLANES];
			double group_line_time_us[DML2_MAX_PLANES];

			/* scheduling check locals */
			struct dml2_pstate_per_method_common_meta group_common_pstate_meta[DML2_MAX_PLANES];
			unsigned int sorted_group_gtl_disallow_index[DML2_MAX_PLANES];
			unsigned int sorted_group_gtl_period_index[DML2_MAX_PLANES];
			double group_phase_offset[DML2_MAX_PLANES];
		} pmo_dcn4;
		struct {
			union dml2_stage_optimizer_function_locals {
				struct dml2_stage_optimizer_uclk_pstate_init_locals {
					double allow_delay_us;
					double blackout_us;
					double watermark_us;

					unsigned int stream_vactive_capability_mask;

					struct dml2_pmo_pstate_strategy expanded_override_strategy_list[2 * 2 * 2 * 2];
					unsigned int num_expanded_override_strategies;

					/* mask of synchronized timings by stream index */
					struct dml2_pmo_synchronized_timing_groups synchronized_timing_groups;

					/* scheduling check locals */
					struct dml2_scheduling_check_locals scheduling_check_locals;
				} uclk_pstate_init;
				struct dml2_stage_optimizer_fclk_ppt_temp_read_pstate_optimize_locals {
					double pstate_blackout_us;
					double pstate_watermark_us;
					double pstate_allow_delay_us;

					struct dml2_pstate_meta per_stream_pstate_meta[DML2_MAX_PLANES];
					enum dml2_pstate_method per_stream_pstate_method[DML2_MAX_PLANES];
					struct dml2_scheduling_check_locals scheduling_check_locals;
				} fclk_ppt_temp_read_pstate_optimize;
			} func_locals;
		} pmo_dcn5;
	};
};

struct dml2_pmo_init_data {
	union {
		struct {
			/* populated once during initialization */
			struct dml2_pmo_pstate_strategy expanded_strategy_list_1_display[PMO_DCN4_MAX_BASE_STRATEGIES * 2];
			struct dml2_pmo_pstate_strategy expanded_strategy_list_2_display[PMO_DCN4_MAX_BASE_STRATEGIES * 4 * 4];
			struct dml2_pmo_pstate_strategy expanded_strategy_list_3_display[PMO_DCN4_MAX_BASE_STRATEGIES * 6 * 6 * 6];
			struct dml2_pmo_pstate_strategy expanded_strategy_list_4_display[PMO_DCN4_MAX_BASE_STRATEGIES * 8 * 8 * 8 * 8];
			unsigned int num_expanded_strategies_per_list[PMO_DCN4_MAX_DISPLAYS];
		} pmo_dcn4;
	};
};

enum dml2_pmo_stage_index {
	dml2_pmo_stage_index_start = 0,
	dml2_pmo_stage_index_mcache = dml2_pmo_stage_index_start,
	dml2_pmo_stage_index_uclk_pstate,
	dml2_pmo_stage_index_qos,
	dml2_pmo_stage_index_vmin,
	dml2_pmo_stage_index_stutter,
	dml2_pmo_stage_index_vmin_dcfclk,
	dml2_pmo_stage_index_fclk_ppt_temp_read_pstate,
	dml2_pmo_stage_index_max,
};

struct dml2_pmo_instance {
	struct dml2_soc_bb *soc_bb;
	struct dml2_ip_capabilities *ip_caps;

	struct dml2_pmo_options *options;

	int disp_clk_vmin_threshold;
	int mpc_combine_limit;
	int odm_combine_limit;
	int mcg_clock_table_size;
	const struct dml2_utm_soc_bb *utm_soc_bb;
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

	/*
	 * obtain a list of mandatory stage optimizers ordered by priority. Caller must perform stage optimization in
	 * the same order.
	 * return - stage optimizer count
	 */
	int (*get_ordered_mandatory_stage_optimizers)(struct dml2_pmo_instance *pmo,
			struct dml2_pmo_stage_optimizer **optimers);
	/*
	 * obtain a list of option stage optimizers ordered by priority, Caller must perform stage optimization in the
	 * same order. Caller must complete stage optimization for all mandatory stage optimizers before performing
	 * optional stage optimization.
	 * return - stage optimizer count
	 */
	int (*get_ordered_optional_stage_optimizers)(struct dml2_pmo_instance *pmo,
			struct dml2_pmo_stage_optimizer **optimers);
	/*
	 * initialize an optimization worksheet based on the display config passed in.
	 */
	void (*initialize_worksheet)(struct dml2_pmo_instance *pmo,
			const struct dml2_display_cfg *dispcfg,
			struct dml2_optimization_worksheet *worksheet);
	/*
	 * convert an optimization worksheet to a display solution.
	 */
	void (*convert_worksheet_to_solution)(struct dml2_pmo_instance *pmo,
			const struct dml2_optimization_worksheet *worksheet,
			struct dml2_display_solution *solution);
	/*
	 * when validation is completed with an updated worksheet's validation result, PMO needs to reset pre validation
	 * states stored in worksheet.
	 */
	void (*clear_pre_validation_states)(struct dml2_pmo_instance *pmo,
			struct dml2_optimization_worksheet *worksheet);
	/*
	 * perform a mini validate solution to rule out common optimization config problems after optimize_next is
	 * called. This interface is a performance optimization to avoid of performing expensive full validate solution
	 * for common optimization problems. It also generalizes sanity check for all stage optimizers. So the concern
	 * of sanity check optimization config is isolated out of each stage optimizer.
	 */
	enum dml2_status (*optional_sanity_check)(struct dml2_pmo_instance *pmo,
			const struct dml2_optimization_worksheet *worksheet);


	struct dml2_pmo_init_data init_data;
	struct dml2_pmo_scratch scratch;
	struct dml2_pmo_stage_optimizer stage_optimizers[dml2_pmo_stage_index_max];
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
};

struct dml2_top_funcs {
	bool (*check_mode_supported)(struct dml2_check_mode_supported_in_out *in_out);
	bool (*build_mode_programming)(struct dml2_build_mode_programming_in_out *in_out);
	bool (*build_mcache_programming)(struct dml2_build_mcache_programming_in_out *in_out);
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
	struct dml2_utm_soc_bb utm_soc_bb;
	struct dml2_clock_granularity_adjuster clock_adjuster;
	struct dml2_pmo_options pmo_options;
	struct dml2_top_funcs funcs;

	struct {
		struct dml2_initialize_instance_locals initialize_instance_locals;
		struct dml2_top_mcache_verify_mcache_size_locals mcache_verify_mcache_size_locals;
		struct dml2_top_mcache_validate_admissability_locals mcache_validate_admissability_locals;
		struct dml2_check_mode_supported_locals check_mode_supported_locals;
		struct dml2_build_mode_programming_locals build_mode_programming_locals;
		struct dml2_optimization_worksheet worksheet;
		struct dml2_optimization_worksheet worksheet_backup;
		struct dml2_display_solution solution;
	} scratch;

	struct {
		struct {
			struct dml2_legacy_core_build_mode_programming_wrapper_locals legacy_core_build_mode_programming_wrapper_locals;
		} scratch;
	} legacy;
};
#endif
