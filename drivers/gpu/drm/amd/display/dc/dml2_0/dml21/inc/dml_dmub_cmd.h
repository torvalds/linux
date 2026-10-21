// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef DML_DMUB_CMD_H
#define DML_DMUB_CMD_H

 /* always include for now */
#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/string.h>

#define DMUB_MAX(X, Y) ((X) > (Y) ? (X) : (Y))

#define DMUB_MIN(X, Y) ((X) < (Y) ? (X) : (Y))

/* Define to ensure that the "common" members always appear in the same
 * order in different structs for back compat purposes
 */
#define COMMON_STREAM_STATIC_SUB_STATE \
    struct dmub_fams2_cmd_legacy_stream_static_state legacy; \
    struct dmub_fams2_cmd_subvp_stream_static_state subvp; \
    struct dmub_fams2_cmd_drr_stream_static_state drr;

/* Maximum number of streams on any ASIC. */
#define DMUB_MAX_STREAMS 6

/* Maximum number of planes on any ASIC. */
#define DMUB_MAX_PLANES 6

/* Maximum number of phantom planes on any ASIC */
#define DMUB_MAX_PHANTOM_PLANES (DMUB_MAX_PLANES) / 2

/* Flattened structure containing SOC BB parameters stored in the VBIOS
 * It is not practical to store the entire bounding box in VBIOS since the bounding box struct can gain new parameters.
 * This also prevents alighment issues when new parameters are added to the SoC BB.
 * The following parameters should be added since these values can't be obtained elsewhere:
 * -dml2_soc_power_management_parameters
 * -dml2_soc_vmin_clock_limits
 */
struct dmub_soc_bb_params {
	uint32_t dram_clk_change_blackout_ns;
	uint32_t dram_clk_change_read_only_ns;
	uint32_t dram_clk_change_write_only_ns;
	uint32_t fclk_change_blackout_ns;
	uint32_t g7_ppt_blackout_ns;
	uint32_t stutter_enter_plus_exit_latency_ns;
	uint32_t stutter_exit_latency_ns;
	uint32_t z8_stutter_enter_plus_exit_latency_ns;
	uint32_t z8_stutter_exit_latency_ns;
	uint32_t z8_min_idle_time_ns;
	uint32_t type_b_dram_clk_change_blackout_ns;
	uint32_t type_b_ppt_blackout_ns;
	uint32_t vmin_limit_dispclk_khz;
	uint32_t vmin_limit_dcfclk_khz;
	uint32_t g7_temperature_read_blackout_ns;
};

struct dmub_rect16 {
	/**
	 * Dirty rect x offset.
	 */
	uint16_t x; // src_x

	/**
	 * Dirty rect y offset.
	 */
	uint16_t y; // src_y

	/**
	 * Dirty rect width.
	 */
	uint16_t width; // dest_width, rect_x

	/**
	 * Dirty rect height.
	 */
	uint16_t height; // dest_height, rect_y
};

union fw_assisted_mclk_switch_version {
	struct {
		uint8_t minor : 5;
		uint8_t major : 3;
	};
	uint8_t ver;
};

/* generic structures and enums */
struct dmub_optc_position {
	uint32_t vpos;
	uint32_t hpos;
	uint32_t frame;
};

/* HW and FW global configuration data for FAMS2 */
/* FAMS2 types and structs */
enum fams2_stream_type {
	FAMS2_STREAM_TYPE_NONE = 0,
	FAMS2_STREAM_TYPE_VBLANK = 1,
	FAMS2_STREAM_TYPE_VACTIVE = 2,
	FAMS2_STREAM_TYPE_DRR = 3,
	FAMS2_STREAM_TYPE_SUBVP = 4,
	FAMS2_STREAM_TYPE_ALTERNATE = 5,
};

struct plane_pipe_rect {
	struct dmub_rect16 luma;
	struct dmub_rect16 chroma;
};

/**
 * Structure to hold the LSDMA source / dest copy parameters.
 * Each field is an array of [2][4]:
 * [2] - Instance 0 is the copy for current frame, instance 1 is the copy for next frame (instance 1 potentially unused if no next)
 * [4] - One instance per pipe
 */
struct lsdma_outputs {
	uint16_t src_x[2][4]; // src x position for the copy. Array of [2][4] for curr vs. next and each pipe
	uint16_t src_y[2][4]; // src y position for the copy
	uint16_t dst_x[2][4]; // dst x position for the copy (can change for curr vs. next)
	uint16_t dst_y[2][4]; // dst y position for the copy (can change for curr vs. next)
	uint16_t width[2][4]; // src and dst width for the copy (src and dst must match)
	uint16_t height[2][4]; // src and dst height for the copy (src and dst must match)
	uint16_t dst_pitch[4]; // dst pitch for the copy (same for curr and next)
};

struct dmub_fams2_alternate_stream_dynamic_state {
	uint64_t earliest_init_tick; // track earliest possible init tick for calculating is_tick_in_allow
	uint32_t otg_frame_pending_clear[3]; // In this context pending means prefetch has never been completed for this frame yet
	uint8_t flip_pending_clear_order[3];
	uint8_t num_pending_flips;
	uint32_t prefetch_start_line_x1000[3]; // can compute from existing params, but store because we use this multiple times
	uint16_t prefetch_end_line[3]; // can compute from existing params, but store because we use this multiple times
	uint16_t recout_y[3];
	uint8_t flip_pending[3];
	uint8_t copy_from_earliest[3];
	uint16_t lsdma_bandwidth_mbps;
	uint16_t vstartup_line;
	uint16_t vready_line;
	uint8_t cursor_size[3]; //  Cursor array per plane for now - if we assume a single cursor, then we don't need an array
	uint8_t pad; // to maintain alignment for below fields - re-arrange structure once all fields are finalized
	/* outputs: */
	uint16_t subvp_start_line_a[3];
	uint16_t subvp_height_a[3];
	uint16_t subvp_next_start_line_a[3];
	uint16_t subvp_next_height_a[3];
	uint16_t subvp_start_line_b[3];
	uint16_t subvp_height_b[3];
	uint16_t subvp_next_start_line_b[3];
	uint16_t subvp_next_height_b[3];
	uint16_t subvp_c_start_line_a[3];
	uint16_t subvp_c_height_a[3];
	uint16_t subvp_c_next_start_line_a[3];
	uint16_t subvp_c_next_height_a[3];
	uint16_t subvp_c_start_line_b[3];
	uint16_t subvp_c_height_b[3];
	uint16_t subvp_c_next_start_line_b[3];
	uint16_t subvp_c_next_height_b[3];
	uint8_t subvp_position[3];
	uint8_t copy_from_primary[3];
	uint8_t pad1[2]; // to maintain alignment for below fields - re-arrange structure once all fields are finalized
	uint32_t program_go_line;
	uint32_t program_go_frame_count;
	uint16_t svp0_start_dst_line;
	uint16_t svp0_end_dst_line;
	uint16_t svp1_start_dst_line;
	uint16_t svp1_end_dst_line;
	struct lsdma_outputs lsdma[2]; // [2] - instance per SVP0 and SVP1
	struct lsdma_outputs lsdma_c[2]; // [2] - instance per SVP0 and SVP1
};

/* dynamic stream state */
struct dmub_fams2_legacy_stream_dynamic_state {
	uint8_t force_allow_at_vblank;
	uint8_t pad[3];
};

struct dmub_fams2_subvp_stream_dynamic_state {
	uint16_t viewport_start_hubp_vline;
	uint16_t viewport_height_hubp_vlines;
	uint16_t viewport_start_c_hubp_vline;
	uint16_t viewport_height_c_hubp_vlines;
	uint16_t phantom_viewport_height_hubp_vlines;
	uint16_t phantom_viewport_height_c_hubp_vlines;
	uint16_t microschedule_start_otg_vline;
	uint16_t mall_start_otg_vline;
	uint16_t mall_start_hubp_vline;
	uint16_t mall_start_c_hubp_vline;
	uint8_t force_allow_at_vblank_only;
	uint8_t swath_height;
	uint8_t swath_height_c;
	uint8_t pad;
};

struct dmub_fams2_drr_stream_dynamic_state {
	uint16_t stretched_vtotal;
	uint8_t use_cur_vtotal;
	uint8_t pad;
};

struct dmub_fams2_cmd_alternate_stream_static_state {
	uint32_t total_bytes_to_copy;
	uint16_t svp0_dst_lines; // per stream
	uint16_t svp1_dst_lines; // per stream
	uint16_t min_lead_dst_lines; // per stream, should be max(nominal_req_limit, vstartup_to_vactive). Does not have to be maxed over all planes
	uint16_t svp_req_limit; // per stream, should be the same value in time between all streams max(2 swaths, dst_y_pre) over all planes
	uint16_t fw_delays;
	uint16_t vstartup_start;
	uint16_t rec_height[3];
	uint16_t viewport_start[3];
	uint16_t viewport_size[3]; // for now size will be the number of lines perpendicular to scan direction (height for 0 / 180, width for 90 and 270)
	uint16_t viewport_start_c[3];
	uint16_t viewport_size_c[3];
	uint16_t surface_pitch[3];
	uint16_t surface_pitch_c[3];
	uint16_t surface_height[3];
	uint16_t surface_height_c[3];
	uint8_t element_size[3];
	uint8_t element_size_c[3];
	uint8_t swizzle_mode[3]; // TODO: Add mapping, should be value used in LSDMA command
	uint8_t vready_offset_lines; // vready offset from vstartup in lines (rounded up, as the actual offset may be a fraction of a line)
	uint16_t dst_y_prefetch_x1000[3];
	uint16_t total_swaths[3];
	uint16_t total_swaths_c[3];
	uint8_t prefetch_swaths[3];
	uint8_t prefetch_swaths_c[3];
	uint8_t swath_height[3];
	uint8_t swath_height_c[3];
	uint16_t block_256b_width[3];
	uint16_t block_256b_height[3];
	uint16_t block_256b_width_c[3];
	uint16_t block_256b_height_c[3];
	uint16_t macro_tile_width[3];
	uint16_t macro_tile_width_c[3];
	union {
		struct {
			uint8_t is_multi_planar : 1;
			uint8_t is_yuv420 : 1;
			uint8_t prefetch_relative_vblank : 1;
			uint8_t vertical_access : 1; // vertical_access = 1 means 90 or 270 rotation
			uint8_t access_direction : 1; // access_direction = 1 means bigger to smaller coordinations (e.g., scan from 2160 to 0 as opposed to regular 0 to 2160)
			uint8_t dcc : 1;
			uint8_t tmz : 1; // TODO: Need to assign outside of DML (DML not aware of TMZ)
		} bits;
		uint8_t all;
	} config[3];
	uint8_t max_cursor_size;
	uint16_t pre_hdl_delta_x1000[3];
	uint16_t pre_hdl_delta_c_x1000[3];
	uint16_t rec_hdl_delta_x1000[3];
	uint16_t rec_hdl_delta_c_x1000[3];
	uint16_t dst_y_per_vm_vblank_x1000[3];
	uint16_t dst_y_per_row_vblank_x1000[3];
	uint16_t dst_y_after_scaler[3];
	uint16_t vinit_prefill[3];
	uint16_t vinit_prefill_c[3];
	uint16_t vratio_x1000[3];
	uint16_t vratio_c_x1000[3];
	struct plane_pipe_rect pipe_viewports[4];
	/* TODO - remove these deprecated vars */
	uint32_t pipe_copy_offset[2][4]; // [2] - SVP0/1, [4] - 4 pipes
	uint32_t pipe_copy_offset_c[2][4];
	/* bits 47:16 of the surface address */
	uint32_t pipe_copy_addr_47_16[2][4]; // [2] - SVP0/1, [4] - 4 pipes
	uint32_t pipe_copy_addr_47_16_c[2][4];
	uint32_t pipe_copy_max_size[2][4];
	uint32_t pipe_copy_max_size_c[2][4];
};

struct dmub_fams2_stream_dynamic_state {
	uint64_t ref_tick;
	uint32_t cur_vtotal;
	uint16_t adjusted_allow_end_otg_vline;
	uint8_t pad[2];
	struct dmub_optc_position ref_otg_pos;
	struct dmub_optc_position target_otg_pos;
	union {
		struct dmub_fams2_legacy_stream_dynamic_state legacy;
		struct dmub_fams2_subvp_stream_dynamic_state subvp;
		struct dmub_fams2_drr_stream_dynamic_state drr;
		struct dmub_fams2_alternate_stream_dynamic_state alternate;
	} sub_state;
};

/* static stream state */
struct dmub_fams2_legacy_stream_static_state {
	uint8_t vactive_det_fill_delay_otg_vlines;
	uint8_t programming_delay_otg_vlines;
}; //v0

struct dmub_fams2_subvp_stream_static_state {
	uint16_t vratio_numerator;
	uint16_t vratio_denominator;
	uint16_t phantom_vtotal;
	uint16_t phantom_vactive;
	union {
		struct {
			uint8_t is_multi_planar : 1;
			uint8_t is_yuv420 : 1;
		} bits;
		uint8_t all;
	} config;
	uint8_t programming_delay_otg_vlines;
	uint8_t prefetch_to_mall_otg_vlines;
	uint8_t phantom_otg_inst;
	uint8_t phantom_pipe_mask;
	uint8_t phantom_plane_pipe_masks[DMUB_MAX_PHANTOM_PLANES]; // phantom pipe mask per plane (for flip passthrough)
}; //v0

struct dmub_fams2_drr_stream_static_state {
	uint16_t nom_stretched_vtotal;
	uint8_t programming_delay_otg_vlines;
	uint8_t only_stretch_if_required;
	uint8_t pad[2];
}; //v0

struct dmub_fams2_cmd_legacy_stream_static_state {
	uint16_t vactive_det_fill_delay_otg_vlines;
	uint16_t programming_delay_otg_vlines;
	uint32_t disallow_time_us;
}; //v1

struct dmub_fams2_cmd_subvp_stream_static_state {
	uint16_t vratio_numerator;
	uint16_t vratio_denominator;
	uint16_t phantom_vtotal;
	uint16_t phantom_vactive;
	uint16_t programming_delay_otg_vlines;
	uint16_t prefetch_to_mall_otg_vlines;
	union {
		struct {
			uint8_t is_multi_planar : 1;
			uint8_t is_yuv420 : 1;
		} bits;
		uint8_t all;
	} config;
	uint8_t phantom_otg_inst;
	uint8_t phantom_pipe_mask;
	uint8_t pad0;
	uint8_t phantom_plane_pipe_masks[DMUB_MAX_PHANTOM_PLANES]; // phantom pipe mask per plane (for flip passthrough)
	uint8_t pad1[4 - (DMUB_MAX_PHANTOM_PLANES % 4)];
}; //v1

struct dmub_fams2_cmd_drr_stream_static_state {
	uint16_t nom_stretched_vtotal;
	uint16_t programming_delay_otg_vlines;
	uint8_t only_stretch_if_required;
	uint8_t pad[3];
}; //v1

union dmub_fams2_stream_static_sub_state {
	struct dmub_fams2_legacy_stream_static_state legacy;
	struct dmub_fams2_subvp_stream_static_state subvp;
	struct dmub_fams2_drr_stream_static_state drr;
}; //v0

union dmub_fams2_cmd_stream_static_sub_state {
	COMMON_STREAM_STATIC_SUB_STATE
}; //v1

union dmub_fams2_stream_static_sub_state_v2 {
	COMMON_STREAM_STATIC_SUB_STATE
	struct dmub_fams2_cmd_alternate_stream_static_state alternate;
}; //v2

struct dmub_fams2_stream_static_state {
	enum fams2_stream_type type;
	uint32_t otg_vline_time_ns;
	uint32_t otg_vline_time_ticks;
	uint16_t htotal;
	uint16_t vtotal; // nominal vtotal
	uint16_t vblank_start;
	uint16_t vblank_end;
	uint16_t max_vtotal;
	uint16_t allow_start_otg_vline;
	uint16_t allow_end_otg_vline;
	uint16_t drr_keepout_otg_vline; // after this vline, vtotal cannot be changed
	uint8_t scheduling_delay_otg_vlines; // min time to budget for ready to microschedule start
	uint8_t contention_delay_otg_vlines; // time to budget for contention on execution
	uint8_t vline_int_ack_delay_otg_vlines; // min time to budget for vertical interrupt firing
	uint8_t allow_to_target_delay_otg_vlines; // time from allow vline to target vline
	union {
		struct {
			uint8_t is_drr : 1; // stream is DRR enabled
			uint8_t clamp_vtotal_min : 1; // clamp vtotal to min instead of nominal
			uint8_t min_ttu_vblank_usable : 1; // if min ttu vblank is above wm, no force pstate is needed in blank
		} bits;
		uint8_t all;
	} config;
	uint8_t otg_inst;
	uint8_t pipe_mask; // pipe mask for the whole config
	uint8_t num_planes;
	uint8_t plane_pipe_masks[DMUB_MAX_PLANES]; // pipe mask per plane (for flip passthrough)
	uint8_t pad[DMUB_MAX_PLANES % 4];
	union dmub_fams2_stream_static_sub_state sub_state;
}; //v0

struct dmub_fams2_cmd_stream_static_base_state {
	enum fams2_stream_type type;
	uint32_t otg_vline_time_ns;
	uint32_t otg_vline_time_ticks;
	uint16_t htotal;
	uint16_t vtotal; // nominal vtotal
	uint16_t vblank_start;
	uint16_t vblank_end;
	uint16_t max_vtotal;
	uint16_t allow_start_otg_vline;
	uint16_t allow_end_otg_vline;
	uint16_t drr_keepout_otg_vline; // after this vline, vtotal cannot be changed
	uint16_t scheduling_delay_otg_vlines; // min time to budget for ready to microschedule start
	uint16_t contention_delay_otg_vlines; // time to budget for contention on execution
	uint16_t vline_int_ack_delay_otg_vlines; // min time to budget for vertical interrupt firing
	uint16_t allow_to_target_delay_otg_vlines; // time from allow vline to target vline
	union {
		struct {
			uint8_t is_drr : 1; // stream is DRR enabled
			uint8_t clamp_vtotal_min : 1; // clamp vtotal to min instead of nominal
			uint8_t min_ttu_vblank_usable : 1; // if min ttu vblank is above wm, no force pstate is needed in blank
		} bits;
		uint8_t all;
	} config;
	uint8_t otg_inst;
	uint8_t pipe_mask; // pipe mask for the whole config
	uint8_t num_planes;
	uint8_t plane_pipe_masks[DMUB_MAX_PLANES]; // pipe mask per plane (for flip passthrough)
	uint8_t pad[DMUB_MAX_PLANES % 4];
}; //v1

struct dmub_fams2_stream_static_state_v1 {
	struct dmub_fams2_cmd_stream_static_base_state base;
	union dmub_fams2_stream_static_sub_state_v2 sub_state;
}; //v1

/**
 * enum dmub_fams2_allow_delay_check_mode - macroscheduler mode for breaking on excessive
 * p-state request to allow latency
 */
enum dmub_fams2_allow_delay_check_mode {
	/* No check for request to allow delay */
	FAMS2_ALLOW_DELAY_CHECK_NONE = 0,
	/* Check for request to allow delay */
	FAMS2_ALLOW_DELAY_CHECK_FROM_START = 1,
	/* Check for prepare to allow delay */
	FAMS2_ALLOW_DELAY_CHECK_FROM_PREPARE = 2,
};

union dmub_fams2_global_feature_config {
	struct {
		uint32_t enable : 1;
		uint32_t enable_ppt_check : 1;
		uint32_t enable_stall_recovery : 1;
		uint32_t enable_debug : 1;
		uint32_t enable_offload_flip : 1;
		uint32_t enable_visual_confirm : 1;
		uint32_t allow_delay_check_mode : 2;
		uint32_t legacy_method_no_fams2 : 1;
		uint32_t reserved : 23;
	} bits;
	uint32_t all;
};

struct dmub_cmd_fams2_global_config {
	uint32_t max_allow_delay_us; // max delay to assert allow from uclk change begin
	uint32_t lock_wait_time_us; // time to forecast acquisition of lock
	uint32_t num_streams;
	union dmub_fams2_global_feature_config features;
	uint32_t recovery_timeout_us;
	uint32_t hwfq_flip_programming_delay_us;
	uint32_t max_allow_to_target_delta_us; // how early DCN could assert P-State allow compared to the P-State target
};

union dmub_cmd_fams2_config {
	struct dmub_cmd_fams2_global_config global;
	struct dmub_fams2_stream_static_state stream; //v0
	union {
		struct dmub_fams2_cmd_stream_static_base_state base;
		union dmub_fams2_cmd_stream_static_sub_state sub_state;
	} stream_v1; //v1
};

struct dmub_fams2_config_v2 {
	struct dmub_cmd_fams2_global_config global;
	struct dmub_fams2_stream_static_state_v1 stream_v1[DMUB_MAX_STREAMS]; //v1
};

/**
 * OS/FW agnostic memcpy
 */
#ifndef dmub_memcpy
#define dmub_memcpy(dest, source, bytes) memcpy((dest), (source), (bytes))
#endif

 /**
  * OS/FW agnostic memset
  */
#ifndef dmub_memset
#define dmub_memset(dest, val, bytes) memset((dest), (val), (bytes))
#endif

  //#endif
#endif /* _DML_DMUB_CMD_H_ */
