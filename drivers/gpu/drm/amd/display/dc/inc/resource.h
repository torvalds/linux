/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 */

#ifndef DRIVERS_GPU_DRM_AMD_DC_DEV_DC_INC_RESOURCE_H_
#define DRIVERS_GPU_DRM_AMD_DC_DEV_DC_INC_RESOURCE_H_

#include "core_types.h"
#include "core_status.h"
#include "dal_asic_id.h"
#include "dm_pp_smu.h"

#define MEMORY_TYPE_MULTIPLIER_CZ 4
#define MEMORY_TYPE_HBM 2


#define IS_PIPE_SYNCD_VALID(pipe) ((((pipe)->pipe_idx_syncd) & 0x80)?1:0)
#define GET_PIPE_SYNCD_FROM_PIPE(pipe) ((pipe)->pipe_idx_syncd & 0x7F)
#define SET_PIPE_SYNCD_TO_PIPE(pipe, pipe_syncd) ((pipe)->pipe_idx_syncd = (0x80 | pipe_syncd))

enum dce_version resource_parse_asic_id(
		struct hw_asic_id asic_id);

struct resource_caps {
	int num_timing_generator;
	int num_opp;
	int num_video_plane;
	int num_audio;
	int num_stream_encoder;
	int num_pll;
	int num_dwb;
	int num_ddc;
	int num_vmid;
	int num_dsc;
	unsigned int num_dig_link_enc; // Total number of DIGs (digital encoders) in DIO (Display Input/Output).
	unsigned int num_usb4_dpia; // Total number of USB4 DPIA (DisplayPort Input Adapters).
	int num_hpo_dp_stream_encoder;
	int num_hpo_dp_link_encoder;
	int num_mpc_3dlut;
};

struct resource_straps {
	uint32_t hdmi_disable;
	uint32_t dc_pinstraps_audio;
	uint32_t audio_stream_number;
};

struct resource_create_funcs {
	void (*read_dce_straps)(
			struct dc_context *ctx, struct resource_straps *straps);

	struct audio *(*create_audio)(
			struct dc_context *ctx, unsigned int inst);

	struct stream_encoder *(*create_stream_encoder)(
			enum engine_id eng_id, struct dc_context *ctx);

	struct hpo_dp_stream_encoder *(*create_hpo_dp_stream_encoder)(
			enum engine_id eng_id, struct dc_context *ctx);
	struct hpo_dp_link_encoder *(*create_hpo_dp_link_encoder)(
			uint8_t inst,
			struct dc_context *ctx);
	struct dce_hwseq *(*create_hwseq)(
			struct dc_context *ctx);
};

bool resource_construct(
	unsigned int num_virtual_links,
	struct dc *dc,
	struct resource_pool *pool,
	const struct resource_create_funcs *create_funcs);

struct resource_pool *dc_create_resource_pool(struct dc  *dc,
					      const struct dc_init_data *init_data,
					      enum dce_version dc_version);

void dc_destroy_resource_pool(struct dc *dc);

enum dc_status resource_map_pool_resources(
		const struct dc *dc,
		struct dc_state *context,
		struct dc_stream_state *stream);

void resource_build_test_pattern_params(
		struct resource_context *res_ctx,
		struct pipe_ctx *pipe_ctx);

bool resource_build_scaling_params(struct pipe_ctx *pipe_ctx);

enum dc_status resource_build_scaling_params_for_context(
		const struct dc *dc,
		struct dc_state *context);

void resource_build_info_frame(struct pipe_ctx *pipe_ctx);

void resource_unreference_clock_source(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct clock_source *clock_source);

void resource_reference_clock_source(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct clock_source *clock_source);

int resource_get_clock_source_reference(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct clock_source *clock_source);

bool resource_are_streams_timing_synchronizable(
		struct dc_stream_state *stream1,
		struct dc_stream_state *stream2);

bool resource_are_vblanks_synchronizable(
		struct dc_stream_state *stream1,
		struct dc_stream_state *stream2);

struct clock_source *resource_find_used_clk_src_for_sharing(
		struct resource_context *res_ctx,
		struct pipe_ctx *pipe_ctx);

struct clock_source *dc_resource_find_first_free_pll(
		struct resource_context *res_ctx,
		const struct resource_pool *pool);

bool resource_attach_surfaces_to_context(
		struct dc_plane_state *const *plane_state,
		int surface_count,
		struct dc_stream_state *dc_stream,
		struct dc_state *context,
		const struct resource_pool *pool);

bool resource_can_pipe_disable_cursor(struct pipe_ctx *pipe_ctx);

#define FREE_PIPE_INDEX_NOT_FOUND -1

/*
 * pipe types are identified based on MUXes in DCN front end that are capable
 * of taking input from one DCN pipeline to another DCN pipeline. The name is
 * in a form of XXXX_YYYY, where XXXX is the DCN front end hardware block the
 * pipeline ends with and YYYY is the rendering role that the pipe is in.
 *
 * For instance OTG_MASTER is a pipe ending with OTG hardware block in its
 * pipeline and it is in a role of a master pipe for timing generation.
 *
 * For quick reference a diagram of each pipe type's areas of responsibility
 * for outputting timings on the screen is shown below:
 *
 *       Timing Active for Stream 0
 *        __________________________________________________
 *       |OTG master 0 (OPP head 0)|OPP head 2 (DPP pipe 2) |
 *       |             (DPP pipe 0)|                        |
 *       | Top Plane 0             |                        |
 *       |           ______________|____                    |
 *       |          |DPP pipe 1    |DPP |                   |
 *       |          |              |pipe|                   |
 *       |          |  Bottom      |3   |                   |
 *       |          |  Plane 1     |    |                   |
 *       |          |              |    |                   |
 *       |          |______________|____|                   |
 *       |                         |                        |
 *       |                         |                        |
 *       | ODM slice 0             | ODM slice 1            |
 *       |_________________________|________________________|
 *
 *       Timing Active for Stream 1
 *        __________________________________________________
 *       |OTG master 4 (OPP head 4)                         |
 *       |                                                  |
 *       |                                                  |
 *       |                                                  |
 *       |                                                  |
 *       |                                                  |
 *       |               Blank Pixel Data                   |
 *       |              (generated by DPG4)                 |
 *       |                                                  |
 *       |                                                  |
 *       |                                                  |
 *       |                                                  |
 *       |                                                  |
 *       |__________________________________________________|
 *
 *       Inter-pipe Relation
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |  plane 0      | slice 0   |             |
 *       |   0    | -------------MPC---------ODM----------- |
 *       |        |  plane 1    | |         | |             |
 *       |   1    | ------------- |         | |             |
 *       |        |  plane 0      | slice 1 | |             |
 *       |   2    | -------------MPC--------- |             |
 *       |        |  plane 1    | |           |             |
 *       |   3    | ------------- |           |             |
 *       |        |               | blank     |             |
 *       |   4    |               | ----------------------- |
 *       |        |               |           |             |
 *       |   5    |  (FREE)       |           |             |
 *       |________|_______________|___________|_____________|
 *
 * The following is a quick reference of the class relation:
 *
 *	DC state            ---1--------0..N---           streams
 *
 *	stream              ---1-----------1---           OTG Master pipe
 *
 *	OTG Master pipe     ---1--------1..N---           OPP Head pipes
 *
 *	OPP Head pipe       ---1--------0..N---           DPP pipes
 *
 *	stream              ---1--------0..N---           Planes
 *
 *	Plane               ---1--------1..N---           DPP pipes
 *
 */
enum pipe_type {
	/* free pipe - free pipe is an uninitialized pipe without a stream
	 * associated with it. It is a free DCN pipe resource. It can be
	 * acquired as any type of pipe.
	 */
	FREE_PIPE,

	/* OTG master pipe - the master pipe of its OPP head pipes with a
	 * functional OTG. It merges all its OPP head pipes pixel data in ODM
	 * block and output to back end DIG. OTG master pipe is responsible for
	 * generating entire CRTC timing to back end DIG. An OTG master pipe may
	 * or may not have a plane. If it has a plane it blends it as the left
	 * most MPC slice of the top most layer. If it doesn't have a plane it
	 * can output pixel data from its OPP head pipes' test pattern
	 * generators (DPG) such as solid black pixel data to blank the screen.
	 */
	OTG_MASTER,

	/* OPP head pipe - the head pipe of an MPC blending tree with a
	 * functional OPP outputting to an OTG. OPP head pipe is responsible for
	 * processing output pixels in its own ODM slice. It may or may not have
	 * a plane. If it has a plane it blends it as the top most layer within
	 * its own ODM slice. If it doesn't have a plane it can output pixel
	 * data from its DPG such as solid black pixel data to blank the pixel
	 * data in its own ODM slice. OTG master pipe is also an OPP head pipe
	 * but with more responsibility.
	 */
	OPP_HEAD,

	/* DPP pipe - the pipe with a functional DPP outputting to an OPP head
	 * pipe's MPC. DPP pipe is responsible for processing pixel data from
	 * its own MPC slice of a plane. It must be connected to an OPP head
	 * pipe and it must have a plane associated with it.
	 */
	DPP_PIPE,
};

/*
 * Determine if the input pipe_ctx is of a pipe type.
 * return - true if pipe_ctx is of the input type.
 */
bool resource_is_pipe_type(const struct pipe_ctx *pipe_ctx, enum pipe_type type);

/*
 * Acquire a pipe as OTG master pipe and allocate pipe resources required to
 * enable stream output.
 */
enum dc_status resource_add_otg_master_for_stream_output(struct dc_state *new_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream);

/*
 * Release pipe resources and the OTG master pipe associated with the stream
 * The stream must have all planes removed and ODM/MPC slice counts are reset
 * to 1 before invoking this interface.
 */
void resource_remove_otg_master_for_stream_output(struct dc_state *new_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream);

/*
 * Add plane to the bottom most layer in plane composition and allocate DPP pipe
 * resources as needed.
 * return - true if plane is added in plane composition, false otherwise.
 */
bool resource_append_dpp_pipes_for_plane_composition(
		struct dc_state *new_ctx,
		struct dc_state *cur_ctx,
		struct resource_pool *pool,
		struct pipe_ctx *otg_master_pipe,
		struct dc_plane_state *plane_state);

/*
 * Add plane to the bottom most layer in plane composition and allocate DPP pipe
 * resources as needed.
 * return - true if plane is added in plane composition, false otherwise.
 */
void resource_remove_dpp_pipes_for_plane_composition(
		struct dc_state *context,
		const struct resource_pool *pool,
		const struct dc_plane_state *plane_state);

/*
 * Update ODM slice count by acquiring or releasing pipes. If new slices need
 * to be added, it is going to add them to the last ODM index. If existing
 * slices need to be removed, it is going to remove them from the last ODM
 * index.
 *
 * return - true if ODM slices are updated and required pipes are acquired. All
 * affected pipe parameters are updated.
 *
 * false if resource fails to complete this update. The function is not designed
 * to recover the creation of invalid topologies. Returning false is typically
 * an indication of insufficient validation in caller's stack. new_ctx will be
 * invalid. Caller may attempt to restore new_ctx by calling this function
 * again with original slice count.
 */
bool resource_update_pipes_for_stream_with_slice_count(
		struct dc_state *new_ctx,
		const struct dc_state *cur_ctx,
		const struct resource_pool *pool,
		const struct dc_stream_state *stream,
		int new_slice_count);

/*
 * Update MPC slice count by acquiring or releasing DPP pipes. If new slices
 * need to be added it is going to add to the last MPC index. If existing
 * slices need to be removed, it is going to remove them from the last MPC
 * index.
 *
 * @dpp_pipe - top most dpp pipe for MPCC combine.
 *
 * return - true if MPC slices are updated and required pipes are acquired. All
 * affected pipe parameters are updated.
 *
 * false if resource fails to complete this update. The function is not designed
 * to recover the creation of invalid topologies. Returning false is typically
 * an indication of insufficient validation in caller's stack. new_ctx will be
 * invalid. Caller may attempt to restore new_ctx by calling this function
 * again with original slice count.
 */
bool resource_update_pipes_for_plane_with_slice_count(
		struct dc_state *new_ctx,
		const struct dc_state *cur_ctx,
		const struct resource_pool *pool,
		const struct dc_plane_state *plane,
		int slice_count);

/*
 * Get the OTG master pipe in resource context associated with the stream.
 * return - NULL if not found. Otherwise the OTG master pipe associated with the
 * stream.
 */
struct pipe_ctx *resource_get_otg_master_for_stream(
		struct resource_context *res_ctx,
		const struct dc_stream_state *stream);

/*
 * Get an array of OPP heads in opp_heads ordered with index low to high for OTG
 * master pipe in res_ctx.
 * return - number of OPP heads in the array. If otg_master passed in is not
 * an OTG master, the function returns 0.
 */
int resource_get_opp_heads_for_otg_master(const struct pipe_ctx *otg_master,
		struct resource_context *res_ctx,
		struct pipe_ctx *opp_heads[MAX_PIPES]);

/*
 * Get an array of DPP pipes in dpp_pipes ordered with index low to high for OPP
 * head pipe in res_ctx.
 * return - number of DPP pipes in the array. If opp_head passed in is not
 * an OPP pipe, the function returns 0.
 */
int resource_get_dpp_pipes_for_opp_head(const struct pipe_ctx *opp_head,
		struct resource_context *res_ctx,
		struct pipe_ctx *dpp_pipes[MAX_PIPES]);

/*
 * Get an array of DPP pipes in dpp_pipes ordered with index low to high for
 * plane in res_ctx.
 * return - number of DPP pipes in the array.
 */
int resource_get_dpp_pipes_for_plane(const struct dc_plane_state *plane,
		struct resource_context *res_ctx,
		struct pipe_ctx *dpp_pipes[MAX_PIPES]);

/*
 * Get the OTG master pipe for the input pipe context.
 * return - the OTG master pipe for the input pipe
 * context.
 */
struct pipe_ctx *resource_get_otg_master(const struct pipe_ctx *pipe_ctx);

/*
 * Get the OPP head pipe for the input pipe context.
 * return - the OPP head pipe for the input pipe
 * context.
 */
struct pipe_ctx *resource_get_opp_head(const struct pipe_ctx *pipe_ctx);

/*
 * Get the DPP pipe allocated for MPC slice 0 and ODM slice 0 of the plane
 * associated with dpp_pipe.
 */
struct pipe_ctx *resource_get_primary_dpp_pipe(const struct pipe_ctx *dpp_pipe);

/*
 * Get the MPC slice index counting from 0 from left most slice
 * For example, if a DPP pipe is used as a secondary pipe in MPCC combine, MPC
 * split index is greater than 0.
 */
int resource_get_mpc_slice_index(const struct pipe_ctx *dpp_pipe);

/*
 * Get the number of MPC slices associated with the pipe.
 * The function returns 0 if the pipe is not associated with an MPC combine
 * pipe topology.
 */
int resource_get_mpc_slice_count(const struct pipe_ctx *pipe);

/*
 * Get the number of ODM slices associated with the pipe.
 * The function returns 0 if the pipe is not associated with an ODM combine
 * pipe topology.
 */
int resource_get_odm_slice_count(const struct pipe_ctx *pipe);

/* Get the ODM slice index counting from 0 from left most slice */
int resource_get_odm_slice_index(const struct pipe_ctx *opp_head);

/* Get ODM slice source rect in timing active as input to OPP block */
struct rect resource_get_odm_slice_src_rect(struct pipe_ctx *pipe_ctx);

/* Get ODM slice destination rect in timing active as output from OPP block */
struct rect resource_get_odm_slice_dst_rect(struct pipe_ctx *pipe_ctx);

/* Get ODM slice destination width in timing active as output from OPP block */
int resource_get_odm_slice_dst_width(struct pipe_ctx *otg_master,
		bool is_last_segment);

/* determine if pipe topology is changed between state a and state b */
bool resource_is_pipe_topology_changed(const struct dc_state *state_a,
		const struct dc_state *state_b);

/*
 * determine if the two OTG master pipes have the same ODM topology
 * return
 * false - if pipes passed in are not OTG masters or ODM topology is
 * changed.
 * true - otherwise
 */
bool resource_is_odm_topology_changed(const struct pipe_ctx *otg_master_a,
		const struct pipe_ctx *otg_master_b);

/* log the pipe topology update in state */
void resource_log_pipe_topology_update(struct dc *dc, struct dc_state *state);

/*
 * Look for a free pipe in new resource context that is used as a secondary OPP
 * head by cur_otg_master.
 *
 * return - FREE_PIPE_INDEX_NOT_FOUND if free pipe is not found, otherwise
 * pipe idx of the free pipe
 */
int resource_find_free_pipe_used_as_sec_opp_head_by_cur_otg_master(
		const struct resource_context *cur_res_ctx,
		struct resource_context *new_res_ctx,
		const struct pipe_ctx *cur_otg_master);

/*
 * Look for a free pipe in new resource context that is used as a secondary DPP
 * pipe in MPC blending tree associated with input OPP head pipe.
 *
 * return - FREE_PIPE_INDEX_NOT_FOUND if free pipe is not found, otherwise
 * pipe idx of the free pipe
 */
int resource_find_free_pipe_used_in_cur_mpc_blending_tree(
		const struct resource_context *cur_res_ctx,
		struct resource_context *new_res_ctx,
		const struct pipe_ctx *cur_opp_head);

/*
 * Look for a free pipe in new resource context that is not used in current
 * resource context.
 *
 * return - FREE_PIPE_INDEX_NOT_FOUND if free pipe is not found, otherwise
 * pipe idx of the free pipe
 */
int recource_find_free_pipe_not_used_in_cur_res_ctx(
		const struct resource_context *cur_res_ctx,
		struct resource_context *new_res_ctx,
		const struct resource_pool *pool);

/*
 * Look for a free pipe in new resource context that is used in current resource
 * context as an OTG master pipe.
 *
 * return - FREE_PIPE_INDEX_NOT_FOUND if free pipe is not found, otherwise
 * pipe idx of the free pipe
 */
int recource_find_free_pipe_used_as_otg_master_in_cur_res_ctx(
		const struct resource_context *cur_res_ctx,
		struct resource_context *new_res_ctx,
		const struct resource_pool *pool);

/*
 * Look for a free pipe in new resource context that is used as a secondary DPP
 * pipe in current resource context.
 * return - FREE_PIPE_INDEX_NOT_FOUND if free pipe is not found, otherwise
 * pipe idx of the free pipe
 */
int resource_find_free_pipe_used_as_cur_sec_dpp(
		const struct resource_context *cur_res_ctx,
		struct resource_context *new_res_ctx,
		const struct resource_pool *pool);

/*
 * Look for a free pipe in new resource context that is used as a secondary DPP
 * pipe in any MPCC combine in current resource context.
 * return - FREE_PIPE_INDEX_NOT_FOUND if free pipe is not found, otherwise
 * pipe idx of the free pipe
 */
int resource_find_free_pipe_used_as_cur_sec_dpp_in_mpcc_combine(
		const struct resource_context *cur_res_ctx,
		struct resource_context *new_res_ctx,
		const struct resource_pool *pool);

/*
 * Look for any free pipe in new resource context.
 * return - FREE_PIPE_INDEX_NOT_FOUND if free pipe is not found, otherwise
 * pipe idx of the free pipe
 */
int resource_find_any_free_pipe(struct resource_context *new_res_ctx,
		const struct resource_pool *pool);

/*
 * Legacy find free secondary pipe logic deprecated for newer DCNs as it doesn't
 * find the most optimal free pipe to prevent from time consuming hardware state
 * transitions.
 */
struct pipe_ctx *resource_find_free_secondary_pipe_legacy(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		const struct pipe_ctx *primary_pipe);

bool resource_validate_attach_surfaces(
		const struct dc_validation_set set[],
		int set_count,
		const struct dc_state *old_context,
		struct dc_state *context,
		const struct resource_pool *pool);

enum dc_status resource_map_clock_resources(
		const struct dc *dc,
		struct dc_state *context,
		struct dc_stream_state *stream);

enum dc_status resource_map_phy_clock_resources(
		const struct dc *dc,
		struct dc_state *context,
		struct dc_stream_state *stream);

bool pipe_need_reprogram(
		struct pipe_ctx *pipe_ctx_old,
		struct pipe_ctx *pipe_ctx);

void resource_build_bit_depth_reduction_params(struct dc_stream_state *stream,
		struct bit_depth_reduction_params *fmt_bit_depth);

void update_audio_usage(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct audio *audio,
		bool acquired);

unsigned int resource_pixel_format_to_bpp(enum surface_pixel_format format);

bool get_temp_dp_link_res(struct dc_link *link,
		struct link_resource *link_res,
		struct dc_link_settings *link_settings);

void reset_syncd_pipes_from_disabled_pipes(struct dc *dc,
	struct dc_state *context);

void check_syncd_pipes_for_disabled_master_pipe(struct dc *dc,
	struct dc_state *context,
	uint8_t disabled_master_pipe_idx);

void reset_sync_context_for_pipe(const struct dc *dc,
	struct dc_state *context,
	uint8_t pipe_idx);

uint8_t resource_transmitter_to_phy_idx(const struct dc *dc, enum transmitter transmitter);

const struct link_hwss *get_link_hwss(const struct dc_link *link,
		const struct link_resource *link_res);

bool is_h_timing_divisible_by_2(struct dc_stream_state *stream);

bool dc_resource_acquire_secondary_pipe_for_mpc_odm_legacy(
		const struct dc *dc,
		struct dc_state *state,
		struct pipe_ctx *pri_pipe,
		struct pipe_ctx *sec_pipe,
		bool odm);

/* A test harness interface that modifies dp encoder resources in the given dc
 * state and bypasses the need to revalidate. The interface assumes that the
 * test harness interface is called with pre-validated link config stored in the
 * pipe_ctx and updates dp encoder resources according to the link config.
 */
enum dc_status update_dp_encoder_resources_for_test_harness(const struct dc *dc,
		struct dc_state *context,
		struct pipe_ctx *pipe_ctx);

bool check_subvp_sw_cursor_fallback_req(const struct dc *dc, struct dc_stream_state *stream);

/* Get hw programming parameters container from pipe context
 * @pipe_ctx: pipe context
 * @dscl_prog_data: struct to hold programmable hw reg values
 */
struct dscl_prog_data *resource_get_dscl_prog_data(struct pipe_ctx *pipe_ctx);
/* Setup dc callbacks for dml2
 * @dc: the display core structure
 * @dml2_options: struct to hold callbacks
 */
void resource_init_common_dml2_callbacks(struct dc *dc, struct dml2_configuration_options *dml2_options);

/*
 *Calculate total DET allocated for all pipes for a given OTG_MASTER pipe
 */
int resource_calculate_det_for_stream(struct dc_state *state, struct pipe_ctx *otg_master);

bool resource_is_hpo_acquired(struct dc_state *context);

struct link_encoder *get_temp_dio_link_enc(
		const struct resource_context *res_ctx,
		const struct resource_pool *const pool,
		const struct dc_link *link);
#endif /* DRIVERS_GPU_DRM_AMD_DC_DEV_DC_INC_RESOURCE_H_ */
