// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dc_spl.h"
#include "dc_spl_scl_easf_filters.h"
#include "dc_spl_isharp_filters.h"
#include "spl_debug.h"

#define IDENTITY_RATIO(ratio) (spl_fixpt_u3d19(ratio) == (1 << 19))
#define MIN_VIEWPORT_SIZE 12

static bool spl_is_yuv420(enum spl_pixel_format format)
{
	if ((format >= SPL_PIXEL_FORMAT_420BPP8) &&
		(format <= SPL_PIXEL_FORMAT_420BPP10))
		return true;

	return false;
}

static bool spl_is_rgb8(enum spl_pixel_format format)
{
	if (format == SPL_PIXEL_FORMAT_ARGB8888)
		return true;

	return false;
}

static bool spl_is_video_format(enum spl_pixel_format format)
{
	if (format >= SPL_PIXEL_FORMAT_VIDEO_BEGIN
		&& format <= SPL_PIXEL_FORMAT_VIDEO_END)
		return true;
	else
		return false;
}

static bool spl_is_subsampled_format(enum spl_pixel_format format)
{
	if (format >= SPL_PIXEL_FORMAT_SUBSAMPLED_BEGIN
		&& format <= SPL_PIXEL_FORMAT_SUBSAMPLED_END)
		return true;
	else
		return false;
}

static struct spl_rect intersect_rec(const struct spl_rect *r0, const struct spl_rect *r1)
{
	struct spl_rect rec;
	int r0_x_end = r0->x + r0->width;
	int r1_x_end = r1->x + r1->width;
	int r0_y_end = r0->y + r0->height;
	int r1_y_end = r1->y + r1->height;

	rec.x = r0->x > r1->x ? r0->x : r1->x;
	rec.width = r0_x_end > r1_x_end ? r1_x_end - rec.x : r0_x_end - rec.x;
	rec.y = r0->y > r1->y ? r0->y : r1->y;
	rec.height = r0_y_end > r1_y_end ? r1_y_end - rec.y : r0_y_end - rec.y;

	/* in case that there is no intersection */
	if (rec.width < 0 || rec.height < 0)
		memset(&rec, 0, sizeof(rec));

	return rec;
}

static struct spl_rect shift_rec(const struct spl_rect *rec_in, int x, int y)
{
	struct spl_rect rec_out = *rec_in;

	rec_out.x += x;
	rec_out.y += y;

	return rec_out;
}

static void spl_opp_adjust_rect(struct spl_rect *rec, const struct spl_opp_adjust *adjust)
{
	if ((rec->x + adjust->x) >= 0)
		rec->x += adjust->x;

	if ((rec->y + adjust->y) >= 0)
		rec->y += adjust->y;

	if ((rec->width + adjust->width) >= 1)
		rec->width += adjust->width;

	if ((rec->height + adjust->height) >= 1)
		rec->height += adjust->height;
}

static struct spl_rect calculate_plane_rec_in_timing_active(
		struct spl_in *spl_in,
		const struct spl_rect *rec_in)
{
	/*
	 * The following diagram shows an example where we map a 1920x1200
	 * desktop to a 2560x1440 timing with a plane rect in the middle
	 * of the screen. To map a plane rect from Stream Source to Timing
	 * Active space, we first multiply stream scaling ratios (i.e 2304/1920
	 * horizontal and 1440/1200 vertical) to the plane's x and y, then
	 * we add stream destination offsets (i.e 128 horizontal, 0 vertical).
	 * This will give us a plane rect's position in Timing Active. However
	 * we have to remove the fractional. The rule is that we find left/right
	 * and top/bottom positions and round the value to the adjacent integer.
	 *
	 * Stream Source Space
	 * ------------
	 *        __________________________________________________
	 *       |Stream Source (1920 x 1200) ^                     |
	 *       |                            y                     |
	 *       |         <------- w --------|>                    |
	 *       |          __________________V                     |
	 *       |<-- x -->|Plane//////////////| ^                  |
	 *       |         |(pre scale)////////| |                  |
	 *       |         |///////////////////| |                  |
	 *       |         |///////////////////| h                  |
	 *       |         |///////////////////| |                  |
	 *       |         |///////////////////| |                  |
	 *       |         |///////////////////| V                  |
	 *       |                                                  |
	 *       |                                                  |
	 *       |__________________________________________________|
	 *
	 *
	 * Timing Active Space
	 * ---------------------------------
	 *
	 *       Timing Active (2560 x 1440)
	 *        __________________________________________________
	 *       |*****|  Stteam Destination (2304 x 1440)    |*****|
	 *       |*****|                                      |*****|
	 *       |<128>|                                      |*****|
	 *       |*****|     __________________               |*****|
	 *       |*****|    |Plane/////////////|              |*****|
	 *       |*****|    |(post scale)//////|              |*****|
	 *       |*****|    |//////////////////|              |*****|
	 *       |*****|    |//////////////////|              |*****|
	 *       |*****|    |//////////////////|              |*****|
	 *       |*****|    |//////////////////|              |*****|
	 *       |*****|                                      |*****|
	 *       |*****|                                      |*****|
	 *       |*****|                                      |*****|
	 *       |*****|______________________________________|*****|
	 *
	 * So the resulting formulas are shown below:
	 *
	 * recout_x = 128 + round(plane_x * 2304 / 1920)
	 * recout_w = 128 + round((plane_x + plane_w) * 2304 / 1920) - recout_x
	 * recout_y = 0 + round(plane_y * 1440 / 1200)
	 * recout_h = 0 + round((plane_y + plane_h) * 1440 / 1200) - recout_y
	 *
	 * NOTE: fixed point division is not error free. To reduce errors
	 * introduced by fixed point division, we divide only after
	 * multiplication is complete.
	 */
	const struct spl_rect *stream_src = &spl_in->basic_out.src_rect;
	const struct spl_rect *stream_dst = &spl_in->basic_out.dst_rect;
	struct spl_rect rec_out = {0};
	struct spl_fixed31_32 temp;


	temp = spl_fixpt_from_fraction(rec_in->x * (long long)stream_dst->width,
			stream_src->width);
	rec_out.x = stream_dst->x + spl_fixpt_round(temp);

	temp = spl_fixpt_from_fraction(
			(rec_in->x + rec_in->width) * (long long)stream_dst->width,
			stream_src->width);
	rec_out.width = stream_dst->x + spl_fixpt_round(temp) - rec_out.x;

	temp = spl_fixpt_from_fraction(rec_in->y * (long long)stream_dst->height,
			stream_src->height);
	rec_out.y = stream_dst->y + spl_fixpt_round(temp);

	temp = spl_fixpt_from_fraction(
			(rec_in->y + rec_in->height) * (long long)stream_dst->height,
			stream_src->height);
	rec_out.height = stream_dst->y + spl_fixpt_round(temp) - rec_out.y;

	return rec_out;
}

static struct spl_rect calculate_mpc_slice_in_timing_active(
		struct spl_in *spl_in,
		struct spl_rect *plane_clip_rec)
{
	bool use_recout_width_aligned =
		spl_in->basic_in.num_h_slices_recout_width_align.use_recout_width_aligned;
	int mpc_slice_count =
		spl_in->basic_in.num_h_slices_recout_width_align.num_slices_recout_width.mpc_num_h_slices;
	int recout_width_align =
		spl_in->basic_in.num_h_slices_recout_width_align.num_slices_recout_width.mpc_recout_width_align;
	int mpc_slice_idx = spl_in->basic_in.mpc_h_slice_index;
	int epimo = mpc_slice_count - plane_clip_rec->width % mpc_slice_count - 1;
	struct spl_rect mpc_rec;

	if (use_recout_width_aligned) {
		mpc_rec.width = recout_width_align;
		if ((mpc_rec.width * (mpc_slice_idx + 1)) > plane_clip_rec->width) {
			mpc_rec.width = plane_clip_rec->width % recout_width_align;
			mpc_rec.x = plane_clip_rec->x + recout_width_align * mpc_slice_idx;
		} else
			mpc_rec.x = plane_clip_rec->x + mpc_rec.width * mpc_slice_idx;
		mpc_rec.height = plane_clip_rec->height;
		mpc_rec.y = plane_clip_rec->y;

	} else {
		mpc_rec.width = plane_clip_rec->width / mpc_slice_count;
		mpc_rec.x = plane_clip_rec->x + mpc_rec.width * mpc_slice_idx;
		mpc_rec.height = plane_clip_rec->height;
		mpc_rec.y = plane_clip_rec->y;
	}
	SPL_ASSERT(mpc_slice_count == 1 ||
			spl_in->basic_out.view_format != SPL_VIEW_3D_SIDE_BY_SIDE ||
			mpc_rec.width % 2 == 0);

	/* extra pixels in the division remainder need to go to pipes after
	 * the extra pixel index minus one(epimo) defined here as:
	 */
	if (mpc_slice_idx > epimo) {
		mpc_rec.x += mpc_slice_idx - epimo - 1;
		mpc_rec.width += 1;
	}

	if (spl_in->basic_out.view_format == SPL_VIEW_3D_TOP_AND_BOTTOM) {
		SPL_ASSERT(mpc_rec.height % 2 == 0);
		mpc_rec.height /= 2;
	}
	return mpc_rec;
}

static struct spl_rect calculate_odm_slice_in_timing_active(struct spl_in *spl_in)
{
	int odm_slice_count = spl_in->basic_out.odm_combine_factor;
	int odm_slice_idx = spl_in->odm_slice_index;
	bool is_last_odm_slice = (odm_slice_idx + 1) == odm_slice_count;
	int h_active = spl_in->basic_out.output_size.width;
	int v_active = spl_in->basic_out.output_size.height;
	int odm_slice_width;
	struct spl_rect odm_rec;

	if (spl_in->basic_out.odm_combine_factor > 0) {
		odm_slice_width = h_active / odm_slice_count;
		/*
		 * deprecated, caller must pass in odm slice rect i.e OPP input
		 * rect in timing active for the new interface.
		 */
		if (spl_in->basic_out.use_two_pixels_per_container && (odm_slice_width % 2))
			odm_slice_width++;

		odm_rec.x = odm_slice_width * odm_slice_idx;
		odm_rec.width = is_last_odm_slice ?
				/* last slice width is the reminder of h_active */
				h_active - odm_slice_width * (odm_slice_count - 1) :
				/* odm slice width is the floor of h_active / count */
				odm_slice_width;
		odm_rec.y = 0;
		odm_rec.height = v_active;

		return odm_rec;
	}

	return spl_in->basic_out.odm_slice_rect;
}

static void spl_calculate_recout(struct spl_in *spl_in, struct spl_scratch *spl_scratch, struct spl_out *spl_out)
{
	/*
	 * A plane clip represents the desired plane size and position in Stream
	 * Source Space. Stream Source is the destination where all planes are
	 * blended (i.e. positioned, scaled and overlaid). It is a canvas where
	 * all planes associated with the current stream are drawn together.
	 * After Stream Source is completed, we will further scale and
	 * reposition the entire canvas of the stream source to Stream
	 * Destination in Timing Active Space. This could be due to display
	 * overscan adjustment where we will need to rescale and reposition all
	 * the planes so they can fit into a TV with overscan or downscale
	 * upscale features such as GPU scaling or VSR.
	 *
	 * This two step blending is a virtual procedure in software. In
	 * hardware there is no such thing as Stream Source. all planes are
	 * blended once in Timing Active Space. Software virtualizes a Stream
	 * Source space to decouple the math complicity so scaling param
	 * calculation focuses on one step at a time.
	 *
	 * In the following two diagrams, user applied 10% overscan adjustment
	 * so the Stream Source needs to be scaled down a little before mapping
	 * to Timing Active Space. As a result the Plane Clip is also scaled
	 * down by the same ratio, Plane Clip position (i.e. x and y) with
	 * respect to Stream Source is also scaled down. To map it in Timing
	 * Active Space additional x and y offsets from Stream Destination are
	 * added to Plane Clip as well.
	 *
	 * Stream Source Space
	 * ------------
	 *        __________________________________________________
	 *       |Stream Source (3840 x 2160) ^                     |
	 *       |                            y                     |
	 *       |                            |                     |
	 *       |          __________________V                     |
	 *       |<-- x -->|Plane Clip/////////|                    |
	 *       |         |(pre scale)////////|                    |
	 *       |         |///////////////////|                    |
	 *       |         |///////////////////|                    |
	 *       |         |///////////////////|                    |
	 *       |         |///////////////////|                    |
	 *       |         |///////////////////|                    |
	 *       |                                                  |
	 *       |                                                  |
	 *       |__________________________________________________|
	 *
	 *
	 * Timing Active Space (3840 x 2160)
	 * ---------------------------------
	 *
	 *       Timing Active
	 *        __________________________________________________
	 *       | y_____________________________________________   |
	 *       |x |Stream Destination (3456 x 1944)            |  |
	 *       |  |                                            |  |
	 *       |  |        __________________                  |  |
	 *       |  |       |Plane Clip////////|                 |  |
	 *       |  |       |(post scale)//////|                 |  |
	 *       |  |       |//////////////////|                 |  |
	 *       |  |       |//////////////////|                 |  |
	 *       |  |       |//////////////////|                 |  |
	 *       |  |       |//////////////////|                 |  |
	 *       |  |                                            |  |
	 *       |  |                                            |  |
	 *       |  |____________________________________________|  |
	 *       |__________________________________________________|
	 *
	 *
	 * In Timing Active Space a plane clip could be further sliced into
	 * pieces called MPC slices. Each Pipe Context is responsible for
	 * processing only one MPC slice so the plane processing workload can be
	 * distributed to multiple DPP Pipes. MPC slices could be blended
	 * together to a single ODM slice. Each ODM slice is responsible for
	 * processing a portion of Timing Active divided horizontally so the
	 * output pixel processing workload can be distributed to multiple OPP
	 * pipes. All ODM slices are mapped together in ODM block so all MPC
	 * slices belong to different ODM slices could be pieced together to
	 * form a single image in Timing Active. MPC slices must belong to
	 * single ODM slice. If an MPC slice goes across ODM slice boundary, it
	 * needs to be divided into two MPC slices one for each ODM slice.
	 *
	 * In the following diagram the output pixel processing workload is
	 * divided horizontally into two ODM slices one for each OPP blend tree.
	 * OPP0 blend tree is responsible for processing left half of Timing
	 * Active, while OPP2 blend tree is responsible for processing right
	 * half.
	 *
	 * The plane has two MPC slices. However since the right MPC slice goes
	 * across ODM boundary, two DPP pipes are needed one for each OPP blend
	 * tree. (i.e. DPP1 for OPP0 blend tree and DPP2 for OPP2 blend tree).
	 *
	 * Assuming that we have a Pipe Context associated with OPP0 and DPP1
	 * working on processing the plane in the diagram. We want to know the
	 * width and height of the shaded rectangle and its relative position
	 * with respect to the ODM slice0. This is called the recout of the pipe
	 * context.
	 *
	 * Planes can be at arbitrary size and position and there could be an
	 * arbitrary number of MPC and ODM slices. The algorithm needs to take
	 * all scenarios into account.
	 *
	 * Timing Active Space (3840 x 2160)
	 * ---------------------------------
	 *
	 *       Timing Active
	 *        __________________________________________________
	 *       |OPP0(ODM slice0)^        |OPP2(ODM slice1)        |
	 *       |                y        |                        |
	 *       |                |  <- w ->                        |
	 *       |           _____V________|____                    |
	 *       |          |DPP0 ^  |DPP1 |DPP2|                   |
	 *       |<------ x |-----|->|/////|    |                   |
	 *       |          |     |  |/////|    |                   |
	 *       |          |     h  |/////|    |                   |
	 *       |          |     |  |/////|    |                   |
	 *       |          |_____V__|/////|____|                   |
	 *       |                         |                        |
	 *       |                         |                        |
	 *       |                         |                        |
	 *       |_________________________|________________________|
	 *
	 *
	 */
	struct spl_rect plane_clip;
	struct spl_rect mpc_slice_of_plane_clip;
	struct spl_rect odm_slice;
	struct spl_rect overlapping_area;

	plane_clip = calculate_plane_rec_in_timing_active(spl_in,
			&spl_in->basic_in.clip_rect);
	/* guard plane clip from drawing beyond stream dst here */
	plane_clip = intersect_rec(&plane_clip,
				&spl_in->basic_out.dst_rect);
	mpc_slice_of_plane_clip = calculate_mpc_slice_in_timing_active(
			spl_in, &plane_clip);
	odm_slice = calculate_odm_slice_in_timing_active(spl_in);
	overlapping_area = intersect_rec(&mpc_slice_of_plane_clip, &odm_slice);

	if (overlapping_area.height > 0 &&
			overlapping_area.width > 0) {
		/* shift the overlapping area so it is with respect to current
		 * ODM slice's position
		 */
		spl_scratch->scl_data.recout = shift_rec(
				&overlapping_area,
				-odm_slice.x, -odm_slice.y);
		spl_scratch->scl_data.recout.height -=
			spl_in->debug.visual_confirm_base_offset;
		spl_scratch->scl_data.recout.height -=
			spl_in->debug.visual_confirm_dpp_offset;
	} else
		/* if there is no overlap, zero recout */
		memset(&spl_scratch->scl_data.recout, 0,
				sizeof(struct spl_rect));
}

/* Calculate scaling ratios */
static void spl_calculate_scaling_ratios(struct spl_in *spl_in,
		struct spl_scratch *spl_scratch,
		struct spl_out *spl_out)
{
	const int in_w = spl_in->basic_out.src_rect.width;
	const int in_h = spl_in->basic_out.src_rect.height;
	const int out_w = spl_in->basic_out.dst_rect.width;
	const int out_h = spl_in->basic_out.dst_rect.height;
	struct spl_rect surf_src = spl_in->basic_in.src_rect;

	/*Swap surf_src height and width since scaling ratios are in recout rotation*/
	if (spl_in->basic_in.rotation == SPL_ROTATION_ANGLE_90 ||
		spl_in->basic_in.rotation == SPL_ROTATION_ANGLE_270)
		spl_swap(surf_src.height, surf_src.width);

	spl_scratch->scl_data.ratios.horz = spl_fixpt_from_fraction(
					surf_src.width,
					spl_in->basic_in.dst_rect.width);
	spl_scratch->scl_data.ratios.vert = spl_fixpt_from_fraction(
					surf_src.height,
					spl_in->basic_in.dst_rect.height);

	if (spl_in->basic_out.view_format == SPL_VIEW_3D_SIDE_BY_SIDE)
		spl_scratch->scl_data.ratios.horz.value *= 2;
	else if (spl_in->basic_out.view_format == SPL_VIEW_3D_TOP_AND_BOTTOM)
		spl_scratch->scl_data.ratios.vert.value *= 2;

	spl_scratch->scl_data.ratios.vert.value = spl_div64_s64(
		spl_scratch->scl_data.ratios.vert.value * in_h, out_h);
	spl_scratch->scl_data.ratios.horz.value = spl_div64_s64(
		spl_scratch->scl_data.ratios.horz.value * in_w, out_w);

	spl_scratch->scl_data.ratios.horz_c = spl_scratch->scl_data.ratios.horz;
	spl_scratch->scl_data.ratios.vert_c = spl_scratch->scl_data.ratios.vert;

	if (spl_is_yuv420(spl_in->basic_in.format)) {
		spl_scratch->scl_data.ratios.horz_c.value /= 2;
		spl_scratch->scl_data.ratios.vert_c.value /= 2;
	}
	spl_scratch->scl_data.ratios.horz = spl_fixpt_truncate(
			spl_scratch->scl_data.ratios.horz, 19);
	spl_scratch->scl_data.ratios.vert = spl_fixpt_truncate(
			spl_scratch->scl_data.ratios.vert, 19);
	spl_scratch->scl_data.ratios.horz_c = spl_fixpt_truncate(
			spl_scratch->scl_data.ratios.horz_c, 19);
	spl_scratch->scl_data.ratios.vert_c = spl_fixpt_truncate(
			spl_scratch->scl_data.ratios.vert_c, 19);

	/*
	 * Coefficient table and some registers are different based on ratio
	 * that is output/input.  Currently we calculate input/output
	 * Store 1/ratio in recip_ratio for those lookups
	 */
	spl_scratch->scl_data.recip_ratios.horz = spl_fixpt_recip(
			spl_scratch->scl_data.ratios.horz);
	spl_scratch->scl_data.recip_ratios.vert = spl_fixpt_recip(
			spl_scratch->scl_data.ratios.vert);
	spl_scratch->scl_data.recip_ratios.horz_c = spl_fixpt_recip(
			spl_scratch->scl_data.ratios.horz_c);
	spl_scratch->scl_data.recip_ratios.vert_c = spl_fixpt_recip(
			spl_scratch->scl_data.ratios.vert_c);
}

/* Calculate Viewport size */
static void spl_calculate_viewport_size(struct spl_in *spl_in, struct spl_scratch *spl_scratch)
{
	spl_scratch->scl_data.viewport.width = spl_fixpt_ceil(spl_fixpt_mul_int(spl_scratch->scl_data.ratios.horz,
							spl_scratch->scl_data.recout.width));
	spl_scratch->scl_data.viewport.height = spl_fixpt_ceil(spl_fixpt_mul_int(spl_scratch->scl_data.ratios.vert,
							spl_scratch->scl_data.recout.height));
	spl_scratch->scl_data.viewport_c.width = spl_fixpt_ceil(spl_fixpt_mul_int(spl_scratch->scl_data.ratios.horz_c,
						spl_scratch->scl_data.recout.width));
	spl_scratch->scl_data.viewport_c.height = spl_fixpt_ceil(spl_fixpt_mul_int(spl_scratch->scl_data.ratios.vert_c,
						spl_scratch->scl_data.recout.height));
	if (spl_in->basic_in.rotation == SPL_ROTATION_ANGLE_90 ||
			spl_in->basic_in.rotation == SPL_ROTATION_ANGLE_270) {
		spl_swap(spl_scratch->scl_data.viewport.width, spl_scratch->scl_data.viewport.height);
		spl_swap(spl_scratch->scl_data.viewport_c.width, spl_scratch->scl_data.viewport_c.height);
	}
}

static void spl_get_vp_scan_direction(enum spl_rotation_angle rotation,
			   bool horizontal_mirror,
			   bool *orthogonal_rotation,
			   bool *flip_vert_scan_dir,
			   bool *flip_horz_scan_dir)
{
	*orthogonal_rotation = false;
	*flip_vert_scan_dir = false;
	*flip_horz_scan_dir = false;
	if (rotation == SPL_ROTATION_ANGLE_180) {
		*flip_vert_scan_dir = true;
		*flip_horz_scan_dir = true;
	} else if (rotation == SPL_ROTATION_ANGLE_90) {
		*orthogonal_rotation = true;
		*flip_horz_scan_dir = true;
	} else if (rotation == SPL_ROTATION_ANGLE_270) {
		*orthogonal_rotation = true;
		*flip_vert_scan_dir = true;
	}

	if (horizontal_mirror)
		*flip_horz_scan_dir = !*flip_horz_scan_dir;
}

/*
 * We completely calculate vp offset, size and inits here based entirely on scaling
 * ratios and recout for pixel perfect pipe combine.
 */
static void spl_calculate_init_and_vp(bool flip_scan_dir,
				int recout_offset_within_recout_full,
				int recout_size,
				int src_size,
				int taps,
				struct spl_fixed31_32 ratio,
				struct spl_fixed31_32 init_adj,
				struct spl_fixed31_32 *init,
				int *vp_offset,
				int *vp_size)
{
	struct spl_fixed31_32 temp;
	int int_part;

	/*
	 * First of the taps starts sampling pixel number <init_int_part> corresponding to recout
	 * pixel 1. Next recout pixel samples int part of <init + scaling ratio> and so on.
	 * All following calculations are based on this logic.
	 *
	 * Init calculated according to formula:
	 * init = (scaling_ratio + number_of_taps + 1) / 2
	 * init_bot = init + scaling_ratio
	 * to get pixel perfect combine add the fraction from calculating vp offset
	 */
	temp = spl_fixpt_mul_int(ratio, recout_offset_within_recout_full);
	*vp_offset = spl_fixpt_floor(temp);
	temp.value &= 0xffffffff;
	*init = spl_fixpt_add(spl_fixpt_div_int(spl_fixpt_add_int(ratio, taps + 1), 2), temp);
	*init = spl_fixpt_add(*init, init_adj);
	*init = spl_fixpt_truncate(*init, 19);

	/*
	 * If viewport has non 0 offset and there are more taps than covered by init then
	 * we should decrease the offset and increase init so we are never sampling
	 * outside of viewport.
	 */
	int_part = spl_fixpt_floor(*init);
	if (int_part < taps) {
		int_part = taps - int_part;
		if (int_part > *vp_offset)
			int_part = *vp_offset;
		*vp_offset -= int_part;
		*init = spl_fixpt_add_int(*init, int_part);
	}
	/*
	 * If taps are sampling outside of viewport at end of recout and there are more pixels
	 * available in the surface we should increase the viewport size, regardless set vp to
	 * only what is used.
	 */
	temp = spl_fixpt_add(*init, spl_fixpt_mul_int(ratio, recout_size - 1));
	*vp_size = spl_fixpt_floor(temp);
	if (*vp_size + *vp_offset > src_size)
		*vp_size = src_size - *vp_offset;

	/* We did all the math assuming we are scanning same direction as display does,
	 * however mirror/rotation changes how vp scans vs how it is offset. If scan direction
	 * is flipped we simply need to calculate offset from the other side of plane.
	 * Note that outside of viewport all scaling hardware works in recout space.
	 */
	if (flip_scan_dir)
		*vp_offset = src_size - *vp_offset - *vp_size;
}

/*Calculate inits and viewport */
static void spl_calculate_inits_and_viewports(struct spl_in *spl_in,
		struct spl_scratch *spl_scratch)
{
	struct spl_rect src = spl_in->basic_in.src_rect;
	struct spl_rect recout_dst_in_active_timing;
	struct spl_rect recout_clip_in_active_timing;
	struct spl_rect recout_clip_in_recout_dst;
	struct spl_rect overlap_in_active_timing;
	struct spl_rect odm_slice = calculate_odm_slice_in_timing_active(spl_in);
	int vpc_div = spl_is_subsampled_format(spl_in->basic_in.format) ? 2 : 1;
	bool orthogonal_rotation, flip_vert_scan_dir, flip_horz_scan_dir;
	struct spl_fixed31_32 init_adj_h = spl_fixpt_zero;
	struct spl_fixed31_32 init_adj_v = spl_fixpt_zero;

	recout_clip_in_active_timing = shift_rec(
			&spl_scratch->scl_data.recout, odm_slice.x, odm_slice.y);
	recout_dst_in_active_timing = calculate_plane_rec_in_timing_active(
			spl_in, &spl_in->basic_in.dst_rect);
	overlap_in_active_timing = intersect_rec(&recout_clip_in_active_timing,
			&recout_dst_in_active_timing);
	if (overlap_in_active_timing.width > 0 &&
			overlap_in_active_timing.height > 0)
		recout_clip_in_recout_dst = shift_rec(&overlap_in_active_timing,
				-recout_dst_in_active_timing.x,
				-recout_dst_in_active_timing.y);
	else
		memset(&recout_clip_in_recout_dst, 0, sizeof(struct spl_rect));
	/*
	 * Work in recout rotation since that requires less transformations
	 */
	spl_get_vp_scan_direction(
			spl_in->basic_in.rotation,
			spl_in->basic_in.horizontal_mirror,
			&orthogonal_rotation,
			&flip_vert_scan_dir,
			&flip_horz_scan_dir);

	if (spl_is_subsampled_format(spl_in->basic_in.format)) {
		/* this gives the direction of the cositing (negative will move
		 * left, right otherwise)
		 */
		int sign = 1;

		switch (spl_in->basic_in.cositing) {

		case CHROMA_COSITING_TOPLEFT:
			init_adj_h = spl_fixpt_from_fraction(sign, 4);
			init_adj_v = spl_fixpt_from_fraction(sign, 4);
			break;
		case CHROMA_COSITING_LEFT:
			init_adj_h = spl_fixpt_from_fraction(sign, 4);
			init_adj_v = spl_fixpt_zero;
			break;
		case CHROMA_COSITING_NONE:
		default:
			init_adj_h = spl_fixpt_zero;
			init_adj_v = spl_fixpt_zero;
			break;
		}
	}

	if (orthogonal_rotation) {
		spl_swap(src.width, src.height);
		spl_swap(flip_vert_scan_dir, flip_horz_scan_dir);
		spl_swap(init_adj_h, init_adj_v);
	}

	spl_calculate_init_and_vp(
			flip_horz_scan_dir,
			recout_clip_in_recout_dst.x,
			spl_scratch->scl_data.recout.width,
			src.width,
			spl_scratch->scl_data.taps.h_taps,
			spl_scratch->scl_data.ratios.horz,
			spl_fixpt_zero,
			&spl_scratch->scl_data.inits.h,
			&spl_scratch->scl_data.viewport.x,
			&spl_scratch->scl_data.viewport.width);
	spl_calculate_init_and_vp(
			flip_horz_scan_dir,
			recout_clip_in_recout_dst.x,
			spl_scratch->scl_data.recout.width,
			src.width / vpc_div,
			spl_scratch->scl_data.taps.h_taps_c,
			spl_scratch->scl_data.ratios.horz_c,
			init_adj_h,
			&spl_scratch->scl_data.inits.h_c,
			&spl_scratch->scl_data.viewport_c.x,
			&spl_scratch->scl_data.viewport_c.width);
	spl_calculate_init_and_vp(
			flip_vert_scan_dir,
			recout_clip_in_recout_dst.y,
			spl_scratch->scl_data.recout.height,
			src.height,
			spl_scratch->scl_data.taps.v_taps,
			spl_scratch->scl_data.ratios.vert,
			spl_fixpt_zero,
			&spl_scratch->scl_data.inits.v,
			&spl_scratch->scl_data.viewport.y,
			&spl_scratch->scl_data.viewport.height);
	spl_calculate_init_and_vp(
			flip_vert_scan_dir,
			recout_clip_in_recout_dst.y,
			spl_scratch->scl_data.recout.height,
			src.height / vpc_div,
			spl_scratch->scl_data.taps.v_taps_c,
			spl_scratch->scl_data.ratios.vert_c,
			init_adj_v,
			&spl_scratch->scl_data.inits.v_c,
			&spl_scratch->scl_data.viewport_c.y,
			&spl_scratch->scl_data.viewport_c.height);
	if (orthogonal_rotation) {
		spl_swap(spl_scratch->scl_data.viewport.x, spl_scratch->scl_data.viewport.y);
		spl_swap(spl_scratch->scl_data.viewport.width, spl_scratch->scl_data.viewport.height);
		spl_swap(spl_scratch->scl_data.viewport_c.x, spl_scratch->scl_data.viewport_c.y);
		spl_swap(spl_scratch->scl_data.viewport_c.width, spl_scratch->scl_data.viewport_c.height);
	}
	spl_scratch->scl_data.viewport.x += src.x;
	spl_scratch->scl_data.viewport.y += src.y;
	SPL_ASSERT(src.x % vpc_div == 0 && src.y % vpc_div == 0);
	spl_scratch->scl_data.viewport_c.x += src.x / vpc_div;
	spl_scratch->scl_data.viewport_c.y += src.y / vpc_div;
}

static void spl_handle_3d_recout(struct spl_in *spl_in, struct spl_rect *recout)
{
	/*
	 * Handle side by side and top bottom 3d recout offsets after vp calculation
	 * since 3d is special and needs to calculate vp as if there is no recout offset
	 * This may break with rotation, good thing we aren't mixing hw rotation and 3d
	 */
	if (spl_in->basic_in.mpc_h_slice_index) {
		SPL_ASSERT(spl_in->basic_in.rotation == SPL_ROTATION_ANGLE_0 ||
			(spl_in->basic_out.view_format != SPL_VIEW_3D_TOP_AND_BOTTOM &&
					spl_in->basic_out.view_format != SPL_VIEW_3D_SIDE_BY_SIDE));
		if (spl_in->basic_out.view_format == SPL_VIEW_3D_TOP_AND_BOTTOM)
			recout->y += recout->height;
		else if (spl_in->basic_out.view_format == SPL_VIEW_3D_SIDE_BY_SIDE)
			recout->x += recout->width;
	}
}

static void spl_clamp_viewport(struct spl_rect *viewport, int min_viewport_size)
{
	if (min_viewport_size == 0)
		min_viewport_size = MIN_VIEWPORT_SIZE;
	/* Clamp minimum viewport size */
	if (viewport->height < min_viewport_size)
		viewport->height = min_viewport_size;
	if (viewport->width < min_viewport_size)
		viewport->width = min_viewport_size;
}

static enum scl_mode spl_get_dscl_mode(const struct spl_in *spl_in,
				const struct spl_scaler_data *data,
				bool enable_isharp, bool enable_easf)
{
	const long long one = spl_fixpt_one.value;
	enum spl_pixel_format pixel_format = spl_in->basic_in.format;

	/* Bypass if ratio is 1:1 with no ISHARP or force scale on */
	if (data->ratios.horz.value == one
			&& data->ratios.vert.value == one
			&& data->ratios.horz_c.value == one
			&& data->ratios.vert_c.value == one
			&& !spl_in->basic_out.always_scale
			&& !enable_isharp)
		return SCL_MODE_SCALING_444_BYPASS;

	if (!spl_is_subsampled_format(pixel_format)) {
		if (spl_is_video_format(pixel_format))
			return SCL_MODE_SCALING_444_YCBCR_ENABLE;
		else
			return SCL_MODE_SCALING_444_RGB_ENABLE;
	}

	/*
	 * Bypass YUV if Y is 1:1 with no ISHARP
	 * Do not bypass UV at 1:1 for cositing to be applied
	 */
	if (!enable_isharp) {
		if (data->ratios.horz.value == one && data->ratios.vert.value == one)
			return SCL_MODE_SCALING_420_LUMA_BYPASS;
	}

	return SCL_MODE_SCALING_420_YCBCR_ENABLE;
}

static void spl_choose_lls_policy(enum spl_pixel_format format,
	enum linear_light_scaling *lls_pref)
{
	if (spl_is_subsampled_format(format))
		*lls_pref = LLS_PREF_NO;
	else /* RGB or YUV444 */
		*lls_pref = LLS_PREF_YES;
}

/* Enable EASF ?*/
static bool enable_easf(struct spl_in *spl_in, struct spl_scratch *spl_scratch)
{
	int vratio = 0;
	int hratio = 0;
	bool skip_easf = false;

	if (spl_in->disable_easf)
		skip_easf = true;

	vratio = spl_fixpt_ceil(spl_scratch->scl_data.ratios.vert);
	hratio = spl_fixpt_ceil(spl_scratch->scl_data.ratios.horz);

	/*
	 * No EASF support for downscaling > 2:1
	 * EASF support for upscaling or downscaling up to 2:1
	 */
	if ((vratio > 2) || (hratio > 2))
		skip_easf = true;

	/*
	 * If lls_pref is LLS_PREF_DONT_CARE, then use pixel format
	 *  to determine whether to use LINEAR or NONLINEAR scaling
	 */
	if (spl_in->lls_pref == LLS_PREF_DONT_CARE)
		spl_choose_lls_policy(spl_in->basic_in.format,
			&spl_in->lls_pref);

	/* Check for linear scaling or EASF preferred */
	if (spl_in->lls_pref != LLS_PREF_YES && !spl_in->prefer_easf)
		skip_easf = true;

	return skip_easf;
}

/* Check if video is in fullscreen mode */
static bool spl_is_video_fullscreen(struct spl_in *spl_in)
{
	if (spl_is_video_format(spl_in->basic_in.format) && spl_in->is_fullscreen)
		return true;
	return false;
}

static bool spl_get_isharp_en(struct spl_in *spl_in,
	struct spl_scratch *spl_scratch)
{
	bool enable_isharp = false;
	int vratio = 0;
	int hratio = 0;
	struct spl_taps taps = spl_scratch->scl_data.taps;
	bool fullscreen = spl_is_video_fullscreen(spl_in);

	/* Return if adaptive sharpness is disabled */
	if (spl_in->adaptive_sharpness.enable == false)
		return enable_isharp;

	vratio = spl_fixpt_ceil(spl_scratch->scl_data.ratios.vert);
	hratio = spl_fixpt_ceil(spl_scratch->scl_data.ratios.horz);

	/* No iSHARP support for downscaling */
	if (vratio > 1 || hratio > 1)
		return enable_isharp;

	// Scaling is up to 1:1 (no scaling) or upscaling

	/*
	 * Apply sharpness to RGB and YUV (NV12/P010)
	 *  surfaces based on policy setting
	 */
	if (!spl_is_video_format(spl_in->basic_in.format) &&
		(spl_in->sharpen_policy == SHARPEN_YUV))
		return enable_isharp;
	else if ((spl_is_video_format(spl_in->basic_in.format) && !fullscreen) &&
		(spl_in->sharpen_policy == SHARPEN_RGB_FULLSCREEN_YUV))
		return enable_isharp;
	else if (!spl_in->is_fullscreen &&
			spl_in->sharpen_policy == SHARPEN_FULLSCREEN_ALL)
		return enable_isharp;

	/*
	 * Apply sharpness if supports horizontal taps 4,6 AND
	 *  vertical taps 3, 4, 6
	 */
	if ((taps.h_taps == 4 || taps.h_taps == 6) &&
		(taps.v_taps == 3 || taps.v_taps == 4 || taps.v_taps == 6))
		enable_isharp = true;

	return enable_isharp;
}

/* Calculate number of tap with adaptive scaling off */
static void spl_get_taps_non_adaptive_scaler(
	  struct spl_scratch *spl_scratch, const struct spl_taps *in_taps)
{
	bool check_max_downscale = false;

	if (in_taps->h_taps == 0) {
		if (spl_fixpt_ceil(spl_scratch->scl_data.ratios.horz) > 1)
			spl_scratch->scl_data.taps.h_taps = spl_min(2 * spl_fixpt_ceil(
				spl_scratch->scl_data.ratios.horz), 8);
		else
			spl_scratch->scl_data.taps.h_taps = 4;
	} else
		spl_scratch->scl_data.taps.h_taps = in_taps->h_taps;

	if (in_taps->v_taps == 0) {
		if (spl_fixpt_ceil(spl_scratch->scl_data.ratios.vert) > 1)
			spl_scratch->scl_data.taps.v_taps = spl_min(2 * spl_fixpt_ceil(
				spl_scratch->scl_data.ratios.vert), 8);
		else
			spl_scratch->scl_data.taps.v_taps = 4;
	} else
		spl_scratch->scl_data.taps.v_taps = in_taps->v_taps;

	if (in_taps->v_taps_c == 0) {
		if (spl_fixpt_ceil(spl_scratch->scl_data.ratios.vert_c) > 1)
			spl_scratch->scl_data.taps.v_taps_c = spl_min(2 * spl_fixpt_ceil(
				spl_scratch->scl_data.ratios.vert_c), 8);
		else
			spl_scratch->scl_data.taps.v_taps_c = 4;
	} else
		spl_scratch->scl_data.taps.v_taps_c = in_taps->v_taps_c;

	if (in_taps->h_taps_c == 0) {
		if (spl_fixpt_ceil(spl_scratch->scl_data.ratios.horz_c) > 1)
			spl_scratch->scl_data.taps.h_taps_c = spl_min(2 * spl_fixpt_ceil(
				spl_scratch->scl_data.ratios.horz_c), 8);
		else
			spl_scratch->scl_data.taps.h_taps_c = 4;
	} else if ((in_taps->h_taps_c % 2) != 0 && in_taps->h_taps_c != 1)
		/* Only 1 and even h_taps_c are supported by hw */
		spl_scratch->scl_data.taps.h_taps_c = in_taps->h_taps_c - 1;
	else
		spl_scratch->scl_data.taps.h_taps_c = in_taps->h_taps_c;


	/*
	 * Max downscale supported is 6.0x.  Add ASSERT to catch if go beyond that
	 */
	check_max_downscale = spl_fixpt_le(spl_scratch->scl_data.ratios.horz,
		spl_fixpt_from_fraction(6, 1));
	SPL_ASSERT(check_max_downscale);
	check_max_downscale = spl_fixpt_le(spl_scratch->scl_data.ratios.vert,
		spl_fixpt_from_fraction(6, 1));
	SPL_ASSERT(check_max_downscale);
	check_max_downscale = spl_fixpt_le(spl_scratch->scl_data.ratios.horz_c,
		spl_fixpt_from_fraction(6, 1));
	SPL_ASSERT(check_max_downscale);
	check_max_downscale = spl_fixpt_le(spl_scratch->scl_data.ratios.vert_c,
		spl_fixpt_from_fraction(6, 1));
	SPL_ASSERT(check_max_downscale);

	if (IDENTITY_RATIO(spl_scratch->scl_data.ratios.horz))
		spl_scratch->scl_data.taps.h_taps = 1;
	if (IDENTITY_RATIO(spl_scratch->scl_data.ratios.vert))
		spl_scratch->scl_data.taps.v_taps = 1;
	if (IDENTITY_RATIO(spl_scratch->scl_data.ratios.horz_c))
		spl_scratch->scl_data.taps.h_taps_c = 1;
	if (IDENTITY_RATIO(spl_scratch->scl_data.ratios.vert_c))
		spl_scratch->scl_data.taps.v_taps_c = 1;

}

/* Calculate optimal number of taps */
static bool spl_get_optimal_number_of_taps(
	  int max_downscale_src_width, struct spl_in *spl_in, struct spl_scratch *spl_scratch,
	  const struct spl_taps *in_taps, bool *enable_easf_v, bool *enable_easf_h,
	  bool *enable_isharp)
{
	int num_part_y, num_part_c;
	unsigned int max_taps_y, max_taps_c;
	unsigned int min_taps_y, min_taps_c;
	enum lb_memory_config lb_config;
	bool skip_easf = false;
	bool is_subsampled = spl_is_subsampled_format(spl_in->basic_in.format);

	if (spl_scratch->scl_data.viewport.width > spl_scratch->scl_data.h_active &&
		max_downscale_src_width != 0 &&
		spl_scratch->scl_data.viewport.width > max_downscale_src_width) {
		spl_get_taps_non_adaptive_scaler(spl_scratch, in_taps);
		*enable_easf_v = false;
		*enable_easf_h = false;
		*enable_isharp = false;
		return false;
	}

	/* Disable adaptive scaler and sharpener when integer scaling is enabled */
	if (spl_in->scaling_quality.integer_scaling) {
		spl_get_taps_non_adaptive_scaler(spl_scratch, in_taps);
		*enable_easf_v = false;
		*enable_easf_h = false;
		*enable_isharp = false;
		return true;
	}

	/* Check if we are using EASF or not */
	skip_easf = enable_easf(spl_in, spl_scratch);

	/*
	 * Set default taps if none are provided
	 * From programming guide: taps = min{ ceil(2*H_RATIO,1), 8} for downscaling
	 * taps = 4 for upscaling
	 */
	if (skip_easf)
		spl_get_taps_non_adaptive_scaler(spl_scratch, in_taps);
	else {
		if (spl_is_video_format(spl_in->basic_in.format)) {
			spl_scratch->scl_data.taps.h_taps = 6;
			spl_scratch->scl_data.taps.v_taps = 6;
			spl_scratch->scl_data.taps.h_taps_c = 4;
			spl_scratch->scl_data.taps.v_taps_c = 4;
		} else { /* RGB */
			spl_scratch->scl_data.taps.h_taps = 6;
			spl_scratch->scl_data.taps.v_taps = 6;
			spl_scratch->scl_data.taps.h_taps_c = 6;
			spl_scratch->scl_data.taps.v_taps_c = 6;
		}
	}

	/*Ensure we can support the requested number of vtaps*/
	min_taps_y = spl_fixpt_ceil(spl_scratch->scl_data.ratios.vert);
	min_taps_c = spl_fixpt_ceil(spl_scratch->scl_data.ratios.vert_c);

	/* Use LB_MEMORY_CONFIG_3 for 4:2:0 */
	if (spl_is_yuv420(spl_in->basic_in.format))
		lb_config = LB_MEMORY_CONFIG_3;
	else
		lb_config = LB_MEMORY_CONFIG_0;
	// Determine max vtap support by calculating how much line buffer can fit
	spl_in->callbacks.spl_calc_lb_num_partitions(spl_in->basic_out.alpha_en, &spl_scratch->scl_data,
			lb_config, &num_part_y, &num_part_c);
	/* MAX_V_TAPS = MIN (NUM_LINES - MAX(CEILING(V_RATIO,1)-2, 0), 8) */
	if (spl_fixpt_ceil(spl_scratch->scl_data.ratios.vert) > 2)
		if ((spl_fixpt_ceil(spl_scratch->scl_data.ratios.vert) - 2) > num_part_y)
			max_taps_y = 0;
		else
			max_taps_y = num_part_y - (spl_fixpt_ceil(spl_scratch->scl_data.ratios.vert) - 2);
	else
		max_taps_y = num_part_y;

	if (spl_fixpt_ceil(spl_scratch->scl_data.ratios.vert_c) > 2)
		if ((spl_fixpt_ceil(spl_scratch->scl_data.ratios.vert_c) - 2) > num_part_c)
			max_taps_c = 0;
		else
			max_taps_c = num_part_c - (spl_fixpt_ceil(spl_scratch->scl_data.ratios.vert_c) - 2);
	else
		max_taps_c = num_part_c;

	if (max_taps_y < min_taps_y)
		return false;
	else if (max_taps_c < min_taps_c)
		return false;

	if (spl_scratch->scl_data.taps.v_taps > max_taps_y)
		spl_scratch->scl_data.taps.v_taps = max_taps_y;

	if (spl_scratch->scl_data.taps.v_taps_c > max_taps_c)
		spl_scratch->scl_data.taps.v_taps_c = max_taps_c;

	if (!skip_easf) {
		/*
		 * RGB ( L + NL ) and Linear HDR support 6x6, 6x4, 6x3, 4x4, 4x3
		 * NL YUV420 only supports 6x6, 6x4 for Y and 4x4 for UV
		 *
		 * If LB does not support 3, 4, or 6 taps, then disable EASF_V
		 *  and only enable EASF_H.  So for RGB, support 6x2, 4x2
		 *  and for NL YUV420, support 6x2 for Y and 4x2 for UV
		 *
		 * All other cases, have to disable EASF_V and EASF_H
		 *
		 * If optimal no of taps is 5, then set it to 4
		 * If optimal no of taps is 7 or 8, then fine since max tap is 6
		 *
		 */
		if (spl_scratch->scl_data.taps.v_taps == 5)
			spl_scratch->scl_data.taps.v_taps = 4;

		if (spl_scratch->scl_data.taps.v_taps_c == 5)
			spl_scratch->scl_data.taps.v_taps_c = 4;

		if (spl_scratch->scl_data.taps.h_taps == 5)
			spl_scratch->scl_data.taps.h_taps = 4;

		if (spl_scratch->scl_data.taps.h_taps_c == 5)
			spl_scratch->scl_data.taps.h_taps_c = 4;

		if (spl_is_video_format(spl_in->basic_in.format)) {
			if (spl_scratch->scl_data.taps.h_taps <= 4) {
				*enable_easf_v = false;
				*enable_easf_h = false;
			} else if (spl_scratch->scl_data.taps.v_taps <= 3) {
				*enable_easf_v = false;
				*enable_easf_h = true;
			} else {
				*enable_easf_v = true;
				*enable_easf_h = true;
			}
			SPL_ASSERT((spl_scratch->scl_data.taps.v_taps > 1) &&
				(spl_scratch->scl_data.taps.v_taps_c > 1));
		} else { /* RGB */
			if (spl_scratch->scl_data.taps.h_taps <= 3) {
				*enable_easf_v = false;
				*enable_easf_h = false;
			} else if (spl_scratch->scl_data.taps.v_taps < 3) {
				*enable_easf_v = false;
				*enable_easf_h = true;
			} else {
				*enable_easf_v = true;
				*enable_easf_h = true;
			}
			SPL_ASSERT(spl_scratch->scl_data.taps.v_taps > 1);
		}
	} else {
		*enable_easf_v = false;
		*enable_easf_h = false;
	} // end of if prefer_easf

	/* Sharpener requires scaler to be enabled, including for 1:1
	 * Check if ISHARP can be enabled
	 * If ISHARP is not enabled, set taps to 1 if ratio is 1:1
	 *  except for chroma taps.  Keep previous taps so it can
	 *  handle cositing
	 */

	*enable_isharp = spl_get_isharp_en(spl_in, spl_scratch);
	if (!*enable_isharp && !spl_in->basic_out.always_scale)	{
		if ((IDENTITY_RATIO(spl_scratch->scl_data.ratios.horz)) &&
			(IDENTITY_RATIO(spl_scratch->scl_data.ratios.vert))) {
			spl_scratch->scl_data.taps.h_taps = 1;
			spl_scratch->scl_data.taps.v_taps = 1;

			if (IDENTITY_RATIO(spl_scratch->scl_data.ratios.horz_c) && !is_subsampled)
				spl_scratch->scl_data.taps.h_taps_c = 1;

			if (IDENTITY_RATIO(spl_scratch->scl_data.ratios.vert_c) && !is_subsampled)
				spl_scratch->scl_data.taps.v_taps_c = 1;

			*enable_easf_v = false;
			*enable_easf_h = false;
		} else {
			if ((!*enable_easf_h) &&
				(IDENTITY_RATIO(spl_scratch->scl_data.ratios.horz)))
				spl_scratch->scl_data.taps.h_taps = 1;

			if ((!*enable_easf_v) &&
				(IDENTITY_RATIO(spl_scratch->scl_data.ratios.vert)))
				spl_scratch->scl_data.taps.v_taps = 1;

			if ((!*enable_easf_h) && !is_subsampled &&
				(IDENTITY_RATIO(spl_scratch->scl_data.ratios.horz_c)))
				spl_scratch->scl_data.taps.h_taps_c = 1;

			if ((!*enable_easf_v) && !is_subsampled &&
				(IDENTITY_RATIO(spl_scratch->scl_data.ratios.vert_c)))
				spl_scratch->scl_data.taps.v_taps_c = 1;
		}
	}
	return true;
}

static void spl_set_black_color_data(enum spl_pixel_format format,
			struct scl_black_color *scl_black_color)
{
	bool ycbcr = spl_is_video_format(format);
	if (ycbcr)	{
		scl_black_color->offset_rgb_y = BLACK_OFFSET_RGB_Y;
		scl_black_color->offset_rgb_cbcr = BLACK_OFFSET_CBCR;
	}	else {
		scl_black_color->offset_rgb_y = 0x0;
		scl_black_color->offset_rgb_cbcr = 0x0;
	}
}

static void spl_set_manual_ratio_init_data(struct dscl_prog_data *dscl_prog_data,
		const struct spl_scaler_data *scl_data)
{
	struct spl_fixed31_32 bot;

	dscl_prog_data->ratios.h_scale_ratio = spl_fixpt_u3d19(scl_data->ratios.horz) << 5;
	dscl_prog_data->ratios.v_scale_ratio = spl_fixpt_u3d19(scl_data->ratios.vert) << 5;
	dscl_prog_data->ratios.h_scale_ratio_c = spl_fixpt_u3d19(scl_data->ratios.horz_c) << 5;
	dscl_prog_data->ratios.v_scale_ratio_c = spl_fixpt_u3d19(scl_data->ratios.vert_c) << 5;
	/*
	 * 0.24 format for fraction, first five bits zeroed
	 */
	dscl_prog_data->init.h_filter_init_frac =
			spl_fixpt_u0d19(scl_data->inits.h) << 5;
	dscl_prog_data->init.h_filter_init_int =
			spl_fixpt_floor(scl_data->inits.h);
	dscl_prog_data->init.h_filter_init_frac_c =
			spl_fixpt_u0d19(scl_data->inits.h_c) << 5;
	dscl_prog_data->init.h_filter_init_int_c =
			spl_fixpt_floor(scl_data->inits.h_c);
	dscl_prog_data->init.v_filter_init_frac =
			spl_fixpt_u0d19(scl_data->inits.v) << 5;
	dscl_prog_data->init.v_filter_init_int =
			spl_fixpt_floor(scl_data->inits.v);
	dscl_prog_data->init.v_filter_init_frac_c =
			spl_fixpt_u0d19(scl_data->inits.v_c) << 5;
	dscl_prog_data->init.v_filter_init_int_c =
			spl_fixpt_floor(scl_data->inits.v_c);

	bot = spl_fixpt_add(scl_data->inits.v, scl_data->ratios.vert);
	dscl_prog_data->init.v_filter_init_bot_frac = spl_fixpt_u0d19(bot) << 5;
	dscl_prog_data->init.v_filter_init_bot_int = spl_fixpt_floor(bot);
	bot = spl_fixpt_add(scl_data->inits.v_c, scl_data->ratios.vert_c);
	dscl_prog_data->init.v_filter_init_bot_frac_c = spl_fixpt_u0d19(bot) << 5;
	dscl_prog_data->init.v_filter_init_bot_int_c = spl_fixpt_floor(bot);
}

static void spl_set_taps_data(struct dscl_prog_data *dscl_prog_data,
		const struct spl_scaler_data *scl_data)
{
	dscl_prog_data->taps.v_taps = scl_data->taps.v_taps - 1;
	dscl_prog_data->taps.h_taps = scl_data->taps.h_taps - 1;
	dscl_prog_data->taps.v_taps_c = scl_data->taps.v_taps_c - 1;
	dscl_prog_data->taps.h_taps_c = scl_data->taps.h_taps_c - 1;
}

/* Populate dscl prog data structure from scaler data calculated by SPL */
static void spl_set_dscl_prog_data(struct spl_in *spl_in, struct spl_scratch *spl_scratch,
	struct spl_out *spl_out, bool enable_easf_v, bool enable_easf_h, bool enable_isharp)
{
	struct dscl_prog_data *dscl_prog_data = spl_out->dscl_prog_data;

	const struct spl_scaler_data *data = &spl_scratch->scl_data;

	struct scl_black_color *scl_black_color = &dscl_prog_data->scl_black_color;

	bool enable_easf = enable_easf_v || enable_easf_h;

	// Set values for recout
	dscl_prog_data->recout = spl_scratch->scl_data.recout;
	// Set values for MPC Size
	dscl_prog_data->mpc_size.width = spl_scratch->scl_data.h_active;
	dscl_prog_data->mpc_size.height = spl_scratch->scl_data.v_active;

	// SCL_MODE - Set SCL_MODE data
	dscl_prog_data->dscl_mode = spl_get_dscl_mode(spl_in, data, enable_isharp,
		enable_easf);

	// SCL_BLACK_COLOR
	spl_set_black_color_data(spl_in->basic_in.format, scl_black_color);

	/* Manually calculate scale ratio and init values */
	spl_set_manual_ratio_init_data(dscl_prog_data, data);

	// Set HTaps/VTaps
	spl_set_taps_data(dscl_prog_data, data);
	// Set viewport
	dscl_prog_data->viewport = spl_scratch->scl_data.viewport;
	// Set viewport_c
	dscl_prog_data->viewport_c = spl_scratch->scl_data.viewport_c;
	// Set filters data
	spl_set_filters_data(dscl_prog_data, data, enable_easf_v, enable_easf_h);
}

/* Calculate C0-C3 coefficients based on HDR_mult */
static void spl_calculate_c0_c3_hdr(struct dscl_prog_data *dscl_prog_data, uint32_t sdr_white_level_nits)
{
	struct spl_fixed31_32 hdr_mult, c0_mult, c1_mult, c2_mult;
	struct spl_fixed31_32 c0_calc, c1_calc, c2_calc;
	struct spl_custom_float_format fmt;
	uint32_t hdr_multx100_int;

	if ((sdr_white_level_nits >= 80) && (sdr_white_level_nits <= 480))
		hdr_multx100_int = sdr_white_level_nits * 100 / 80;
	else
		hdr_multx100_int = 100; /* default for 80 nits otherwise */

	hdr_mult = spl_fixpt_from_fraction((long long)hdr_multx100_int, 100LL);
	c0_mult = spl_fixpt_from_fraction(2126LL, 10000LL);
	c1_mult = spl_fixpt_from_fraction(7152LL, 10000LL);
	c2_mult = spl_fixpt_from_fraction(722LL, 10000LL);

	c0_calc = spl_fixpt_mul(hdr_mult, spl_fixpt_mul(c0_mult, spl_fixpt_from_fraction(
		16384LL, 125LL)));
	c1_calc = spl_fixpt_mul(hdr_mult, spl_fixpt_mul(c1_mult, spl_fixpt_from_fraction(
		16384LL, 125LL)));
	c2_calc = spl_fixpt_mul(hdr_mult, spl_fixpt_mul(c2_mult, spl_fixpt_from_fraction(
		16384LL, 125LL)));

	fmt.exponenta_bits = 5;
	fmt.mantissa_bits = 10;
	fmt.sign = true;

	// fp1.5.10, C0 coefficient (LN_rec709:  HDR_MULT * 0.212600 * 2^14/125)
	spl_convert_to_custom_float_format(c0_calc, &fmt, &dscl_prog_data->easf_matrix_c0);
	// fp1.5.10, C1 coefficient (LN_rec709:  HDR_MULT * 0.715200 * 2^14/125)
	spl_convert_to_custom_float_format(c1_calc, &fmt, &dscl_prog_data->easf_matrix_c1);
	// fp1.5.10, C2 coefficient (LN_rec709:  HDR_MULT * 0.072200 * 2^14/125)
	spl_convert_to_custom_float_format(c2_calc, &fmt, &dscl_prog_data->easf_matrix_c2);
	dscl_prog_data->easf_matrix_c3 = 0x0; // fp1.5.10, C3 coefficient
}

/* Set EASF data */
static void spl_set_easf_data(struct spl_scratch *spl_scratch, struct spl_out *spl_out, bool enable_easf_v,
	bool enable_easf_h, enum linear_light_scaling lls_pref,
	enum spl_pixel_format format, enum system_setup setup,
	uint32_t sdr_white_level_nits)
{
	struct dscl_prog_data *dscl_prog_data = spl_out->dscl_prog_data;
	if (enable_easf_v) {
		dscl_prog_data->easf_v_en = true;
		dscl_prog_data->easf_v_ring = 0;
		dscl_prog_data->easf_v_sharp_factor = 0;
		dscl_prog_data->easf_v_bf1_en = 1;	// 1-bit, BF1 calculation enable, 0=disable, 1=enable
		dscl_prog_data->easf_v_bf2_mode = 0xF;	// 4-bit, BF2 calculation mode
		/* 2-bit, BF3 chroma mode correction calculation mode */
		dscl_prog_data->easf_v_bf3_mode = spl_get_v_bf3_mode(
			spl_scratch->scl_data.recip_ratios.vert);
		/* FP1.5.10 [ minCoef ]*/
		dscl_prog_data->easf_v_ringest_3tap_dntilt_uptilt =
			spl_get_3tap_dntilt_uptilt_offset(spl_scratch->scl_data.taps.v_taps,
				spl_scratch->scl_data.recip_ratios.vert);
		/* FP1.5.10 [ upTiltMaxVal ]*/
		dscl_prog_data->easf_v_ringest_3tap_uptilt_max =
			spl_get_3tap_uptilt_maxval(spl_scratch->scl_data.taps.v_taps,
				spl_scratch->scl_data.recip_ratios.vert);
		/* FP1.5.10 [ dnTiltSlope ]*/
		dscl_prog_data->easf_v_ringest_3tap_dntilt_slope =
			spl_get_3tap_dntilt_slope(spl_scratch->scl_data.taps.v_taps,
				spl_scratch->scl_data.recip_ratios.vert);
		/* FP1.5.10 [ upTilt1Slope ]*/
		dscl_prog_data->easf_v_ringest_3tap_uptilt1_slope =
			spl_get_3tap_uptilt1_slope(spl_scratch->scl_data.taps.v_taps,
				spl_scratch->scl_data.recip_ratios.vert);
		/* FP1.5.10 [ upTilt2Slope ]*/
		dscl_prog_data->easf_v_ringest_3tap_uptilt2_slope =
			spl_get_3tap_uptilt2_slope(spl_scratch->scl_data.taps.v_taps,
				spl_scratch->scl_data.recip_ratios.vert);
		/* FP1.5.10 [ upTilt2Offset ]*/
		dscl_prog_data->easf_v_ringest_3tap_uptilt2_offset =
			spl_get_3tap_uptilt2_offset(spl_scratch->scl_data.taps.v_taps,
				spl_scratch->scl_data.recip_ratios.vert);
		/* FP1.5.10; (2.0) Ring reducer gain for 4 or 6-tap mode [H_REDUCER_GAIN4] */
		dscl_prog_data->easf_v_ringest_eventap_reduceg1 =
			spl_get_reducer_gain4(spl_scratch->scl_data.taps.v_taps,
				spl_scratch->scl_data.recip_ratios.vert);
		/* FP1.5.10; (2.5) Ring reducer gain for 6-tap mode [V_REDUCER_GAIN6] */
		dscl_prog_data->easf_v_ringest_eventap_reduceg2 =
			spl_get_reducer_gain6(spl_scratch->scl_data.taps.v_taps,
				spl_scratch->scl_data.recip_ratios.vert);
		/* FP1.5.10; (-0.135742) Ring gain for 6-tap set to -139/1024 */
		dscl_prog_data->easf_v_ringest_eventap_gain1 =
			spl_get_gainRing4(spl_scratch->scl_data.taps.v_taps,
				spl_scratch->scl_data.recip_ratios.vert);
		/* FP1.5.10; (-0.024414) Ring gain for 6-tap set to -25/1024 */
		dscl_prog_data->easf_v_ringest_eventap_gain2 =
			spl_get_gainRing6(spl_scratch->scl_data.taps.v_taps,
				spl_scratch->scl_data.recip_ratios.vert);
		dscl_prog_data->easf_v_bf_maxa = 63; //Vertical Max BF value A in U0.6 format.Selected if V_FCNTL == 0
		dscl_prog_data->easf_v_bf_maxb = 63; //Vertical Max BF value A in U0.6 format.Selected if V_FCNTL == 1
		dscl_prog_data->easf_v_bf_mina = 0;	//Vertical Min BF value A in U0.6 format.Selected if V_FCNTL == 0
		dscl_prog_data->easf_v_bf_minb = 0;	//Vertical Min BF value A in U0.6 format.Selected if V_FCNTL == 1
		if (lls_pref == LLS_PREF_YES)	{
			dscl_prog_data->easf_v_bf2_flat1_gain = 4;	// U1.3, BF2 Flat1 Gain control
			dscl_prog_data->easf_v_bf2_flat2_gain = 8;	// U4.0, BF2 Flat2 Gain control
			dscl_prog_data->easf_v_bf2_roc_gain = 4;	// U2.2, Rate Of Change control

			dscl_prog_data->easf_v_bf1_pwl_in_seg0 = 0x600;	// S0.10, BF1 PWL Segment 0 = -512
			dscl_prog_data->easf_v_bf1_pwl_base_seg0 = 0;	// U0.6, BF1 Base PWL Segment 0
			dscl_prog_data->easf_v_bf1_pwl_slope_seg0 = 3;	// S7.3, BF1 Slope PWL Segment 0
			dscl_prog_data->easf_v_bf1_pwl_in_seg1 = 0x7EC;	// S0.10, BF1 PWL Segment 1 = -20
			dscl_prog_data->easf_v_bf1_pwl_base_seg1 = 12;	// U0.6, BF1 Base PWL Segment 1
			dscl_prog_data->easf_v_bf1_pwl_slope_seg1 = 326;	// S7.3, BF1 Slope PWL Segment 1
			dscl_prog_data->easf_v_bf1_pwl_in_seg2 = 0;	// S0.10, BF1 PWL Segment 2
			dscl_prog_data->easf_v_bf1_pwl_base_seg2 = 63;	// U0.6, BF1 Base PWL Segment 2
			dscl_prog_data->easf_v_bf1_pwl_slope_seg2 = 0;	// S7.3, BF1 Slope PWL Segment 2
			dscl_prog_data->easf_v_bf1_pwl_in_seg3 = 16;	// S0.10, BF1 PWL Segment 3
			dscl_prog_data->easf_v_bf1_pwl_base_seg3 = 63;	// U0.6, BF1 Base PWL Segment 3
			dscl_prog_data->easf_v_bf1_pwl_slope_seg3 = 0x7C8;	// S7.3, BF1 Slope PWL Segment 3 = -56
			dscl_prog_data->easf_v_bf1_pwl_in_seg4 = 32;	// S0.10, BF1 PWL Segment 4
			dscl_prog_data->easf_v_bf1_pwl_base_seg4 = 56;	// U0.6, BF1 Base PWL Segment 4
			dscl_prog_data->easf_v_bf1_pwl_slope_seg4 = 0x7D0;	// S7.3, BF1 Slope PWL Segment 4 = -48
			dscl_prog_data->easf_v_bf1_pwl_in_seg5 = 48;	// S0.10, BF1 PWL Segment 5
			dscl_prog_data->easf_v_bf1_pwl_base_seg5 = 50;	// U0.6, BF1 Base PWL Segment 5
			dscl_prog_data->easf_v_bf1_pwl_slope_seg5 = 0x710;	// S7.3, BF1 Slope PWL Segment 5 = -240
			dscl_prog_data->easf_v_bf1_pwl_in_seg6 = 64;	// S0.10, BF1 PWL Segment 6
			dscl_prog_data->easf_v_bf1_pwl_base_seg6 = 20;	// U0.6, BF1 Base PWL Segment 6
			dscl_prog_data->easf_v_bf1_pwl_slope_seg6 = 0x760;	// S7.3, BF1 Slope PWL Segment 6 = -160
			dscl_prog_data->easf_v_bf1_pwl_in_seg7 = 80;	// S0.10, BF1 PWL Segment 7
			dscl_prog_data->easf_v_bf1_pwl_base_seg7 = 0;	// U0.6, BF1 Base PWL Segment 7

			dscl_prog_data->easf_v_bf3_pwl_in_set0 = 0x000;	// FP0.6.6, BF3 Input value PWL Segment 0
			dscl_prog_data->easf_v_bf3_pwl_base_set0 = 63;	// S0.6, BF3 Base PWL Segment 0
			dscl_prog_data->easf_v_bf3_pwl_slope_set0 = 0x12C5;	// FP1.6.6, BF3 Slope PWL Segment 0
			dscl_prog_data->easf_v_bf3_pwl_in_set1 =
				0x0B37; // FP0.6.6, BF3 Input value PWL Segment 1 (0.0078125 * 125^3)
			dscl_prog_data->easf_v_bf3_pwl_base_set1 = 62;	// S0.6, BF3 Base PWL Segment 1
			dscl_prog_data->easf_v_bf3_pwl_slope_set1 =
				0x13B8;	// FP1.6.6, BF3 Slope PWL Segment 1
			dscl_prog_data->easf_v_bf3_pwl_in_set2 =
				0x0BB7;	// FP0.6.6, BF3 Input value PWL Segment 2 (0.03125 * 125^3)
			dscl_prog_data->easf_v_bf3_pwl_base_set2 = 20;	// S0.6, BF3 Base PWL Segment 2
			dscl_prog_data->easf_v_bf3_pwl_slope_set2 =
				0x1356;	// FP1.6.6, BF3 Slope PWL Segment 2
			dscl_prog_data->easf_v_bf3_pwl_in_set3 =
				0x0BF7;	// FP0.6.6, BF3 Input value PWL Segment 3 (0.0625 * 125^3)
			dscl_prog_data->easf_v_bf3_pwl_base_set3 = 0;	// S0.6, BF3 Base PWL Segment 3
			dscl_prog_data->easf_v_bf3_pwl_slope_set3 =
				0x136B;	// FP1.6.6, BF3 Slope PWL Segment 3
			dscl_prog_data->easf_v_bf3_pwl_in_set4 =
				0x0C37;	// FP0.6.6, BF3 Input value PWL Segment 4 (0.125 * 125^3)
			dscl_prog_data->easf_v_bf3_pwl_base_set4 = 0x4E;	// S0.6, BF3 Base PWL Segment 4 = -50
			dscl_prog_data->easf_v_bf3_pwl_slope_set4 =
				0x1200;	// FP1.6.6, BF3 Slope PWL Segment 4
			dscl_prog_data->easf_v_bf3_pwl_in_set5 =
				0x0CF7;	// FP0.6.6, BF3 Input value PWL Segment 5 (1.0 * 125^3)
			dscl_prog_data->easf_v_bf3_pwl_base_set5 = 0x41;	// S0.6, BF3 Base PWL Segment 5 = -63
		}	else	{
			dscl_prog_data->easf_v_bf2_flat1_gain = 13;	// U1.3, BF2 Flat1 Gain control
			dscl_prog_data->easf_v_bf2_flat2_gain = 15;	// U4.0, BF2 Flat2 Gain control
			dscl_prog_data->easf_v_bf2_roc_gain = 14;	// U2.2, Rate Of Change control

			dscl_prog_data->easf_v_bf1_pwl_in_seg0 = 0x440;	// S0.10, BF1 PWL Segment 0 = -960
			dscl_prog_data->easf_v_bf1_pwl_base_seg0 = 0;	// U0.6, BF1 Base PWL Segment 0
			dscl_prog_data->easf_v_bf1_pwl_slope_seg0 = 2;	// S7.3, BF1 Slope PWL Segment 0
			dscl_prog_data->easf_v_bf1_pwl_in_seg1 = 0x7C4;	// S0.10, BF1 PWL Segment 1 = -60
			dscl_prog_data->easf_v_bf1_pwl_base_seg1 = 12;	// U0.6, BF1 Base PWL Segment 1
			dscl_prog_data->easf_v_bf1_pwl_slope_seg1 = 109;	// S7.3, BF1 Slope PWL Segment 1
			dscl_prog_data->easf_v_bf1_pwl_in_seg2 = 0;	// S0.10, BF1 PWL Segment 2
			dscl_prog_data->easf_v_bf1_pwl_base_seg2 = 63;	// U0.6, BF1 Base PWL Segment 2
			dscl_prog_data->easf_v_bf1_pwl_slope_seg2 = 0;	// S7.3, BF1 Slope PWL Segment 2
			dscl_prog_data->easf_v_bf1_pwl_in_seg3 = 48;	// S0.10, BF1 PWL Segment 3
			dscl_prog_data->easf_v_bf1_pwl_base_seg3 = 63;	// U0.6, BF1 Base PWL Segment 3
			dscl_prog_data->easf_v_bf1_pwl_slope_seg3 = 0x7ED;	// S7.3, BF1 Slope PWL Segment 3 = -19
			dscl_prog_data->easf_v_bf1_pwl_in_seg4 = 96;	// S0.10, BF1 PWL Segment 4
			dscl_prog_data->easf_v_bf1_pwl_base_seg4 = 56;	// U0.6, BF1 Base PWL Segment 4
			dscl_prog_data->easf_v_bf1_pwl_slope_seg4 = 0x7F0;	// S7.3, BF1 Slope PWL Segment 4 = -16
			dscl_prog_data->easf_v_bf1_pwl_in_seg5 = 144;	// S0.10, BF1 PWL Segment 5
			dscl_prog_data->easf_v_bf1_pwl_base_seg5 = 50;	// U0.6, BF1 Base PWL Segment 5
			dscl_prog_data->easf_v_bf1_pwl_slope_seg5 = 0x7B0;	// S7.3, BF1 Slope PWL Segment 5 = -80
			dscl_prog_data->easf_v_bf1_pwl_in_seg6 = 192;	// S0.10, BF1 PWL Segment 6
			dscl_prog_data->easf_v_bf1_pwl_base_seg6 = 20;	// U0.6, BF1 Base PWL Segment 6
			dscl_prog_data->easf_v_bf1_pwl_slope_seg6 = 0x7CB;	// S7.3, BF1 Slope PWL Segment 6 = -53
			dscl_prog_data->easf_v_bf1_pwl_in_seg7 = 240;	// S0.10, BF1 PWL Segment 7
			dscl_prog_data->easf_v_bf1_pwl_base_seg7 = 0;	// U0.6, BF1 Base PWL Segment 7

			dscl_prog_data->easf_v_bf3_pwl_in_set0 = 0x000;	// FP0.6.6, BF3 Input value PWL Segment 0
			dscl_prog_data->easf_v_bf3_pwl_base_set0 = 63;	// S0.6, BF3 Base PWL Segment 0
			dscl_prog_data->easf_v_bf3_pwl_slope_set0 = 0x0000;	// FP1.6.6, BF3 Slope PWL Segment 0
			dscl_prog_data->easf_v_bf3_pwl_in_set1 =
				0x06C0; // FP0.6.6, BF3 Input value PWL Segment 1 (0.0625)
			dscl_prog_data->easf_v_bf3_pwl_base_set1 = 63;	// S0.6, BF3 Base PWL Segment 1
			dscl_prog_data->easf_v_bf3_pwl_slope_set1 = 0x1896;	// FP1.6.6, BF3 Slope PWL Segment 1
			dscl_prog_data->easf_v_bf3_pwl_in_set2 =
				0x0700;	// FP0.6.6, BF3 Input value PWL Segment 2 (0.125)
			dscl_prog_data->easf_v_bf3_pwl_base_set2 = 20;	// S0.6, BF3 Base PWL Segment 2
			dscl_prog_data->easf_v_bf3_pwl_slope_set2 = 0x1810;	// FP1.6.6, BF3 Slope PWL Segment 2
			dscl_prog_data->easf_v_bf3_pwl_in_set3 =
				0x0740;	// FP0.6.6, BF3 Input value PWL Segment 3 (0.25)
			dscl_prog_data->easf_v_bf3_pwl_base_set3 = 0;	// S0.6, BF3 Base PWL Segment 3
			dscl_prog_data->easf_v_bf3_pwl_slope_set3 =
				0x1878;	// FP1.6.6, BF3 Slope PWL Segment 3
			dscl_prog_data->easf_v_bf3_pwl_in_set4 =
				0x0761;	// FP0.6.6, BF3 Input value PWL Segment 4 (0.375)
			dscl_prog_data->easf_v_bf3_pwl_base_set4 = 0x44;	// S0.6, BF3 Base PWL Segment 4 = -60
			dscl_prog_data->easf_v_bf3_pwl_slope_set4 = 0x1760;	// FP1.6.6, BF3 Slope PWL Segment 4
			dscl_prog_data->easf_v_bf3_pwl_in_set5 =
				0x0780;	// FP0.6.6, BF3 Input value PWL Segment 5 (0.5)
			dscl_prog_data->easf_v_bf3_pwl_base_set5 = 0x41;	// S0.6, BF3 Base PWL Segment 5 = -63
		}
	} else
		dscl_prog_data->easf_v_en = false;

	if (enable_easf_h) {
		dscl_prog_data->easf_h_en = true;
		dscl_prog_data->easf_h_ring = 0;
		dscl_prog_data->easf_h_sharp_factor = 0;
		dscl_prog_data->easf_h_bf1_en =
			1;	// 1-bit, BF1 calculation enable, 0=disable, 1=enable
		dscl_prog_data->easf_h_bf2_mode =
			0xF;	// 4-bit, BF2 calculation mode
		/* 2-bit, BF3 chroma mode correction calculation mode */
		dscl_prog_data->easf_h_bf3_mode = spl_get_h_bf3_mode(
			spl_scratch->scl_data.recip_ratios.horz);
		/* FP1.5.10; (2.0) Ring reducer gain for 4 or 6-tap mode [H_REDUCER_GAIN4] */
		dscl_prog_data->easf_h_ringest_eventap_reduceg1 =
			spl_get_reducer_gain4(spl_scratch->scl_data.taps.h_taps,
				spl_scratch->scl_data.recip_ratios.horz);
		/* FP1.5.10; (2.5) Ring reducer gain for 6-tap mode [V_REDUCER_GAIN6] */
		dscl_prog_data->easf_h_ringest_eventap_reduceg2 =
			spl_get_reducer_gain6(spl_scratch->scl_data.taps.h_taps,
				spl_scratch->scl_data.recip_ratios.horz);
		/* FP1.5.10; (-0.135742) Ring gain for 6-tap set to -139/1024 */
		dscl_prog_data->easf_h_ringest_eventap_gain1 =
			spl_get_gainRing4(spl_scratch->scl_data.taps.h_taps,
				spl_scratch->scl_data.recip_ratios.horz);
		/* FP1.5.10; (-0.024414) Ring gain for 6-tap set to -25/1024 */
		dscl_prog_data->easf_h_ringest_eventap_gain2 =
			spl_get_gainRing6(spl_scratch->scl_data.taps.h_taps,
				spl_scratch->scl_data.recip_ratios.horz);
		dscl_prog_data->easf_h_bf_maxa = 63; //Horz Max BF value A in U0.6 format.Selected if H_FCNTL==0
		dscl_prog_data->easf_h_bf_maxb = 63; //Horz Max BF value B in U0.6 format.Selected if H_FCNTL==1
		dscl_prog_data->easf_h_bf_mina = 0;	//Horz Min BF value B in U0.6 format.Selected if H_FCNTL==0
		dscl_prog_data->easf_h_bf_minb = 0;	//Horz Min BF value B in U0.6 format.Selected if H_FCNTL==1
		if (lls_pref == LLS_PREF_YES)	{
			dscl_prog_data->easf_h_bf2_flat1_gain = 4;	// U1.3, BF2 Flat1 Gain control
			dscl_prog_data->easf_h_bf2_flat2_gain = 8;	// U4.0, BF2 Flat2 Gain control
			dscl_prog_data->easf_h_bf2_roc_gain = 4;	// U2.2, Rate Of Change control

			dscl_prog_data->easf_h_bf1_pwl_in_seg0 = 0x600;	// S0.10, BF1 PWL Segment 0 = -512
			dscl_prog_data->easf_h_bf1_pwl_base_seg0 = 0;	// U0.6, BF1 Base PWL Segment 0
			dscl_prog_data->easf_h_bf1_pwl_slope_seg0 = 3;	// S7.3, BF1 Slope PWL Segment 0
			dscl_prog_data->easf_h_bf1_pwl_in_seg1 = 0x7EC;	// S0.10, BF1 PWL Segment 1 = -20
			dscl_prog_data->easf_h_bf1_pwl_base_seg1 = 12;	// U0.6, BF1 Base PWL Segment 1
			dscl_prog_data->easf_h_bf1_pwl_slope_seg1 = 326;	// S7.3, BF1 Slope PWL Segment 1
			dscl_prog_data->easf_h_bf1_pwl_in_seg2 = 0;	// S0.10, BF1 PWL Segment 2
			dscl_prog_data->easf_h_bf1_pwl_base_seg2 = 63;	// U0.6, BF1 Base PWL Segment 2
			dscl_prog_data->easf_h_bf1_pwl_slope_seg2 = 0;	// S7.3, BF1 Slope PWL Segment 2
			dscl_prog_data->easf_h_bf1_pwl_in_seg3 = 16;	// S0.10, BF1 PWL Segment 3
			dscl_prog_data->easf_h_bf1_pwl_base_seg3 = 63;	// U0.6, BF1 Base PWL Segment 3
			dscl_prog_data->easf_h_bf1_pwl_slope_seg3 = 0x7C8;	// S7.3, BF1 Slope PWL Segment 3 = -56
			dscl_prog_data->easf_h_bf1_pwl_in_seg4 = 32;	// S0.10, BF1 PWL Segment 4
			dscl_prog_data->easf_h_bf1_pwl_base_seg4 = 56;	// U0.6, BF1 Base PWL Segment 4
			dscl_prog_data->easf_h_bf1_pwl_slope_seg4 = 0x7D0;	// S7.3, BF1 Slope PWL Segment 4 = -48
			dscl_prog_data->easf_h_bf1_pwl_in_seg5 = 48;	// S0.10, BF1 PWL Segment 5
			dscl_prog_data->easf_h_bf1_pwl_base_seg5 = 50;	// U0.6, BF1 Base PWL Segment 5
			dscl_prog_data->easf_h_bf1_pwl_slope_seg5 = 0x710;	// S7.3, BF1 Slope PWL Segment 5 = -240
			dscl_prog_data->easf_h_bf1_pwl_in_seg6 = 64;	// S0.10, BF1 PWL Segment 6
			dscl_prog_data->easf_h_bf1_pwl_base_seg6 = 20;	// U0.6, BF1 Base PWL Segment 6
			dscl_prog_data->easf_h_bf1_pwl_slope_seg6 = 0x760;	// S7.3, BF1 Slope PWL Segment 6 = -160
			dscl_prog_data->easf_h_bf1_pwl_in_seg7 = 80;	// S0.10, BF1 PWL Segment 7
			dscl_prog_data->easf_h_bf1_pwl_base_seg7 = 0;	// U0.6, BF1 Base PWL Segment 7

			dscl_prog_data->easf_h_bf3_pwl_in_set0 = 0x000;	// FP0.6.6, BF3 Input value PWL Segment 0
			dscl_prog_data->easf_h_bf3_pwl_base_set0 = 63;	// S0.6, BF3 Base PWL Segment 0
			dscl_prog_data->easf_h_bf3_pwl_slope_set0 = 0x12C5;	// FP1.6.6, BF3 Slope PWL Segment 0
			dscl_prog_data->easf_h_bf3_pwl_in_set1 =
				0x0B37;	// FP0.6.6, BF3 Input value PWL Segment 1 (0.0078125 * 125^3)
			dscl_prog_data->easf_h_bf3_pwl_base_set1 = 62;	// S0.6, BF3 Base PWL Segment 1
			dscl_prog_data->easf_h_bf3_pwl_slope_set1 =	0x13B8;	// FP1.6.6, BF3 Slope PWL Segment 1
			dscl_prog_data->easf_h_bf3_pwl_in_set2 =
				0x0BB7;	// FP0.6.6, BF3 Input value PWL Segment 2 (0.03125 * 125^3)
			dscl_prog_data->easf_h_bf3_pwl_base_set2 = 20;	// S0.6, BF3 Base PWL Segment 2
			dscl_prog_data->easf_h_bf3_pwl_slope_set2 =	0x1356;	// FP1.6.6, BF3 Slope PWL Segment 2
			dscl_prog_data->easf_h_bf3_pwl_in_set3 =
				0x0BF7;	// FP0.6.6, BF3 Input value PWL Segment 3 (0.0625 * 125^3)
			dscl_prog_data->easf_h_bf3_pwl_base_set3 = 0;	// S0.6, BF3 Base PWL Segment 3
			dscl_prog_data->easf_h_bf3_pwl_slope_set3 =	0x136B;	// FP1.6.6, BF3 Slope PWL Segment 3
			dscl_prog_data->easf_h_bf3_pwl_in_set4 =
				0x0C37;	// FP0.6.6, BF3 Input value PWL Segment 4 (0.125 * 125^3)
			dscl_prog_data->easf_h_bf3_pwl_base_set4 = 0x4E;	// S0.6, BF3 Base PWL Segment 4 = -50
			dscl_prog_data->easf_h_bf3_pwl_slope_set4 = 0x1200;	// FP1.6.6, BF3 Slope PWL Segment 4
			dscl_prog_data->easf_h_bf3_pwl_in_set5 =
				0x0CF7;	// FP0.6.6, BF3 Input value PWL Segment 5 (1.0 * 125^3)
			dscl_prog_data->easf_h_bf3_pwl_base_set5 = 0x41;	// S0.6, BF3 Base PWL Segment 5 = -63
		} else {
			dscl_prog_data->easf_h_bf2_flat1_gain = 13;	// U1.3, BF2 Flat1 Gain control
			dscl_prog_data->easf_h_bf2_flat2_gain = 15;	// U4.0, BF2 Flat2 Gain control
			dscl_prog_data->easf_h_bf2_roc_gain = 14;	// U2.2, Rate Of Change control

			dscl_prog_data->easf_h_bf1_pwl_in_seg0 = 0x440;	// S0.10, BF1 PWL Segment 0 = -960
			dscl_prog_data->easf_h_bf1_pwl_base_seg0 = 0;	// U0.6, BF1 Base PWL Segment 0
			dscl_prog_data->easf_h_bf1_pwl_slope_seg0 = 2;	// S7.3, BF1 Slope PWL Segment 0
			dscl_prog_data->easf_h_bf1_pwl_in_seg1 = 0x7C4;	// S0.10, BF1 PWL Segment 1 = -60
			dscl_prog_data->easf_h_bf1_pwl_base_seg1 = 12;	// U0.6, BF1 Base PWL Segment 1
			dscl_prog_data->easf_h_bf1_pwl_slope_seg1 = 109;	// S7.3, BF1 Slope PWL Segment 1
			dscl_prog_data->easf_h_bf1_pwl_in_seg2 = 0;	// S0.10, BF1 PWL Segment 2
			dscl_prog_data->easf_h_bf1_pwl_base_seg2 = 63;	// U0.6, BF1 Base PWL Segment 2
			dscl_prog_data->easf_h_bf1_pwl_slope_seg2 = 0;	// S7.3, BF1 Slope PWL Segment 2
			dscl_prog_data->easf_h_bf1_pwl_in_seg3 = 48;	// S0.10, BF1 PWL Segment 3
			dscl_prog_data->easf_h_bf1_pwl_base_seg3 = 63;	// U0.6, BF1 Base PWL Segment 3
			dscl_prog_data->easf_h_bf1_pwl_slope_seg3 = 0x7ED;	// S7.3, BF1 Slope PWL Segment 3 = -19
			dscl_prog_data->easf_h_bf1_pwl_in_seg4 = 96;	// S0.10, BF1 PWL Segment 4
			dscl_prog_data->easf_h_bf1_pwl_base_seg4 = 56;	// U0.6, BF1 Base PWL Segment 4
			dscl_prog_data->easf_h_bf1_pwl_slope_seg4 = 0x7F0;	// S7.3, BF1 Slope PWL Segment 4 = -16
			dscl_prog_data->easf_h_bf1_pwl_in_seg5 = 144;	// S0.10, BF1 PWL Segment 5
			dscl_prog_data->easf_h_bf1_pwl_base_seg5 = 50;	// U0.6, BF1 Base PWL Segment 5
			dscl_prog_data->easf_h_bf1_pwl_slope_seg5 = 0x7B0;	// S7.3, BF1 Slope PWL Segment 5 = -80
			dscl_prog_data->easf_h_bf1_pwl_in_seg6 = 192;	// S0.10, BF1 PWL Segment 6
			dscl_prog_data->easf_h_bf1_pwl_base_seg6 = 20;	// U0.6, BF1 Base PWL Segment 6
			dscl_prog_data->easf_h_bf1_pwl_slope_seg6 = 0x7CB;	// S7.3, BF1 Slope PWL Segment 6 = -53
			dscl_prog_data->easf_h_bf1_pwl_in_seg7 = 240;	// S0.10, BF1 PWL Segment 7
			dscl_prog_data->easf_h_bf1_pwl_base_seg7 = 0;	// U0.6, BF1 Base PWL Segment 7

			dscl_prog_data->easf_h_bf3_pwl_in_set0 = 0x000;	// FP0.6.6, BF3 Input value PWL Segment 0
			dscl_prog_data->easf_h_bf3_pwl_base_set0 = 63;	// S0.6, BF3 Base PWL Segment 0
			dscl_prog_data->easf_h_bf3_pwl_slope_set0 = 0x0000;	// FP1.6.6, BF3 Slope PWL Segment 0
			dscl_prog_data->easf_h_bf3_pwl_in_set1 =
				0x06C0;	// FP0.6.6, BF3 Input value PWL Segment 1 (0.0625)
			dscl_prog_data->easf_h_bf3_pwl_base_set1 = 63;	// S0.6, BF3 Base PWL Segment 1
			dscl_prog_data->easf_h_bf3_pwl_slope_set1 = 0x1896;	// FP1.6.6, BF3 Slope PWL Segment 1
			dscl_prog_data->easf_h_bf3_pwl_in_set2 =
				0x0700;	// FP0.6.6, BF3 Input value PWL Segment 2 (0.125)
			dscl_prog_data->easf_h_bf3_pwl_base_set2 = 20;	// S0.6, BF3 Base PWL Segment 2
			dscl_prog_data->easf_h_bf3_pwl_slope_set2 = 0x1810;	// FP1.6.6, BF3 Slope PWL Segment 2
			dscl_prog_data->easf_h_bf3_pwl_in_set3 =
				0x0740;	// FP0.6.6, BF3 Input value PWL Segment 3 (0.25)
			dscl_prog_data->easf_h_bf3_pwl_base_set3 = 0;	// S0.6, BF3 Base PWL Segment 3
			dscl_prog_data->easf_h_bf3_pwl_slope_set3 = 0x1878;	// FP1.6.6, BF3 Slope PWL Segment 3
			dscl_prog_data->easf_h_bf3_pwl_in_set4 =
				0x0761;	// FP0.6.6, BF3 Input value PWL Segment 4 (0.375)
			dscl_prog_data->easf_h_bf3_pwl_base_set4 = 0x44;	// S0.6, BF3 Base PWL Segment 4 = -60
			dscl_prog_data->easf_h_bf3_pwl_slope_set4 = 0x1760;	// FP1.6.6, BF3 Slope PWL Segment 4
			dscl_prog_data->easf_h_bf3_pwl_in_set5 =
				0x0780;	// FP0.6.6, BF3 Input value PWL Segment 5 (0.5)
			dscl_prog_data->easf_h_bf3_pwl_base_set5 = 0x41;	// S0.6, BF3 Base PWL Segment 5 = -63
		} // if (lls_pref == LLS_PREF_YES)
	} else
		dscl_prog_data->easf_h_en = false;

	if (lls_pref == LLS_PREF_YES)	{
		dscl_prog_data->easf_ltonl_en = 1;	// Linear input
		if ((setup == HDR_L) && (spl_is_rgb8(format))) {
			/* Calculate C0-C3 coefficients based on HDR multiplier */
			spl_calculate_c0_c3_hdr(dscl_prog_data, sdr_white_level_nits);
		} else { // HDR_L ( DWM ) and SDR_L
			dscl_prog_data->easf_matrix_c0 =
				0x4EF7;	// fp1.5.10, C0 coefficient (LN_rec709:  0.2126 * (2^14)/125 = 27.86590720)
			dscl_prog_data->easf_matrix_c1 =
				0x55DC;	// fp1.5.10, C1 coefficient (LN_rec709:  0.7152 * (2^14)/125 = 93.74269440)
			dscl_prog_data->easf_matrix_c2 =
				0x48BB;	// fp1.5.10, C2 coefficient (LN_rec709:  0.0722 * (2^14)/125 = 9.46339840)
			dscl_prog_data->easf_matrix_c3 =
				0x0;	// fp1.5.10, C3 coefficient
		}
	}	else	{
		dscl_prog_data->easf_ltonl_en = 0;	// Non-Linear input
		dscl_prog_data->easf_matrix_c0 =
			0x3434;	// fp1.5.10, C0 coefficient (LN_BT2020:  0.262695312500000)
		dscl_prog_data->easf_matrix_c1 =
			0x396D;	// fp1.5.10, C1 coefficient (LN_BT2020:  0.678222656250000)
		dscl_prog_data->easf_matrix_c2 =
			0x2B97;	// fp1.5.10, C2 coefficient (LN_BT2020:  0.059295654296875)
		dscl_prog_data->easf_matrix_c3 =
			0x0;	// fp1.5.10, C3 coefficient
	}

	if (spl_is_subsampled_format(format)) { /* TODO: 0 = RGB, 1 = YUV */
		dscl_prog_data->easf_matrix_mode = 1;
		/*
		 * 2-bit, BF3 chroma mode correction calculation mode
		 * Needs to be disabled for YUV420 mode
		 * Override lookup value
		 */
		dscl_prog_data->easf_v_bf3_mode = 0;
		dscl_prog_data->easf_h_bf3_mode = 0;
	} else
		dscl_prog_data->easf_matrix_mode = 0;

}

/*Set isharp noise detection */
static void spl_set_isharp_noise_det_mode(struct dscl_prog_data *dscl_prog_data,
	const struct spl_scaler_data *data)
{
	// ISHARP_NOISEDET_MODE
	// 0: 3x5 as VxH
	// 1: 4x5 as VxH
	// 2:
	// 3: 5x5 as VxH
	if (data->taps.v_taps == 6)
		dscl_prog_data->isharp_noise_det.mode = 3;
	else if (data->taps.v_taps == 4)
		dscl_prog_data->isharp_noise_det.mode = 1;
	else if (data->taps.v_taps == 3)
		dscl_prog_data->isharp_noise_det.mode = 0;
};
/* Set Sharpener data */
static void spl_set_isharp_data(struct dscl_prog_data *dscl_prog_data,
		struct adaptive_sharpness adp_sharpness, bool enable_isharp,
		enum linear_light_scaling lls_pref, enum spl_pixel_format format,
		const struct spl_scaler_data *data, struct spl_fixed31_32 ratio,
		enum system_setup setup, enum scale_to_sharpness_policy scale_to_sharpness_policy)
{
	/* Turn off sharpener if not required */
	if (!enable_isharp) {
		dscl_prog_data->isharp_en = 0;
		return;
	}

	spl_build_isharp_1dlut_from_reference_curve(ratio, setup, adp_sharpness,
		scale_to_sharpness_policy);
	memcpy(dscl_prog_data->isharp_delta, spl_get_pregen_filter_isharp_1D_lut(setup),
		sizeof(uint32_t) * ISHARP_LUT_TABLE_SIZE);
	dscl_prog_data->sharpness_level = adp_sharpness.sharpness_level;

	dscl_prog_data->isharp_en = 1;	// ISHARP_EN
	// Set ISHARP_NOISEDET_MODE if htaps = 6-tap
	if (data->taps.h_taps == 6) {
		dscl_prog_data->isharp_noise_det.enable = 1;	/* ISHARP_NOISEDET_EN */
		spl_set_isharp_noise_det_mode(dscl_prog_data, data);	/* ISHARP_NOISEDET_MODE */
	} else
		dscl_prog_data->isharp_noise_det.enable = 0;	// ISHARP_NOISEDET_EN
	// Program noise detection threshold
	dscl_prog_data->isharp_noise_det.uthreshold = 24;	// ISHARP_NOISEDET_UTHRE
	dscl_prog_data->isharp_noise_det.dthreshold = 4;	// ISHARP_NOISEDET_DTHRE
	// Program noise detection gain
	dscl_prog_data->isharp_noise_det.pwl_start_in = 3;	// ISHARP_NOISEDET_PWL_START_IN
	dscl_prog_data->isharp_noise_det.pwl_end_in = 13;	// ISHARP_NOISEDET_PWL_END_IN
	dscl_prog_data->isharp_noise_det.pwl_slope = 1623;	// ISHARP_NOISEDET_PWL_SLOPE

	if (lls_pref == LLS_PREF_NO) /* ISHARP_FMT_MODE */
		dscl_prog_data->isharp_fmt.mode = 1;
	else
		dscl_prog_data->isharp_fmt.mode = 0;

	dscl_prog_data->isharp_fmt.norm = 0x3C00;	// ISHARP_FMT_NORM
	dscl_prog_data->isharp_lba.mode = 0;	// ISHARP_LBA_MODE

	if (setup == SDR_L) {
		// ISHARP_LBA_PWL_SEG0: ISHARP Local Brightness Adjustment PWL Segment 0
		dscl_prog_data->isharp_lba.in_seg[0] = 0;	// ISHARP LBA PWL for Seg 0. INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[0] = 0;	// ISHARP LBA PWL for Seg 0. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[0] = 62;	// ISHARP LBA for Seg 0. SLOPE value in S5.3 format
		// ISHARP_LBA_PWL_SEG1: ISHARP LBA PWL Segment 1
		dscl_prog_data->isharp_lba.in_seg[1] = 130;	// ISHARP LBA PWL for Seg 1. INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[1] = 63; // ISHARP LBA PWL for Seg 1. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[1] = 0; // ISHARP LBA for Seg 1. SLOPE value in S5.3 format
		// ISHARP_LBA_PWL_SEG2: ISHARP LBA PWL Segment 2
		dscl_prog_data->isharp_lba.in_seg[2] = 450; // ISHARP LBA PWL for Seg 2. INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[2] = 63; // ISHARP LBA PWL for Seg 2. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[2] = 0x18D; // ISHARP LBA for Seg 2. SLOPE value in S5.3 format = -115
		// ISHARP_LBA_PWL_SEG3: ISHARP LBA PWL Segment 3
		dscl_prog_data->isharp_lba.in_seg[3] = 520; // ISHARP LBA PWL for Seg 3.INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[3] = 0; // ISHARP LBA PWL for Seg 3. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[3] = 0; // ISHARP LBA for Seg 3. SLOPE value in S5.3 format
		// ISHARP_LBA_PWL_SEG4: ISHARP LBA PWL Segment 4
		dscl_prog_data->isharp_lba.in_seg[4] = 520; // ISHARP LBA PWL for Seg 4.INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[4] = 0; // ISHARP LBA PWL for Seg 4. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[4] = 0; // ISHARP LBA for Seg 4. SLOPE value in S5.3 format
		// ISHARP_LBA_PWL_SEG5: ISHARP LBA PWL Segment 5
		dscl_prog_data->isharp_lba.in_seg[5] = 520; // ISHARP LBA PWL for Seg 5.INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[5] = 0;	// ISHARP LBA PWL for Seg 5. BASE value in U0.6 format
	} else if (setup == HDR_L) {
		// ISHARP_LBA_PWL_SEG0: ISHARP Local Brightness Adjustment PWL Segment 0
		dscl_prog_data->isharp_lba.in_seg[0] = 0;	// ISHARP LBA PWL for Seg 0. INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[0] = 0;	// ISHARP LBA PWL for Seg 0. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[0] = 32;	// ISHARP LBA for Seg 0. SLOPE value in S5.3 format
		// ISHARP_LBA_PWL_SEG1: ISHARP LBA PWL Segment 1
		dscl_prog_data->isharp_lba.in_seg[1] = 254;	// ISHARP LBA PWL for Seg 1. INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[1] = 63; // ISHARP LBA PWL for Seg 1. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[1] = 0; // ISHARP LBA for Seg 1. SLOPE value in S5.3 format
		// ISHARP_LBA_PWL_SEG2: ISHARP LBA PWL Segment 2
		dscl_prog_data->isharp_lba.in_seg[2] = 559; // ISHARP LBA PWL for Seg 2. INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[2] = 63; // ISHARP LBA PWL for Seg 2. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[2] = 0x10C; // ISHARP LBA for Seg 2. SLOPE value in S5.3 format = -244
		// ISHARP_LBA_PWL_SEG3: ISHARP LBA PWL Segment 3
		dscl_prog_data->isharp_lba.in_seg[3] = 592; // ISHARP LBA PWL for Seg 3.INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[3] = 0; // ISHARP LBA PWL for Seg 3. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[3] = 0; // ISHARP LBA for Seg 3. SLOPE value in S5.3 format
		// ISHARP_LBA_PWL_SEG4: ISHARP LBA PWL Segment 4
		dscl_prog_data->isharp_lba.in_seg[4] = 1023; // ISHARP LBA PWL for Seg 4.INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[4] = 0; // ISHARP LBA PWL for Seg 4. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[4] = 0; // ISHARP LBA for Seg 4. SLOPE value in S5.3 format
		// ISHARP_LBA_PWL_SEG5: ISHARP LBA PWL Segment 5
		dscl_prog_data->isharp_lba.in_seg[5] = 1023; // ISHARP LBA PWL for Seg 5.INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[5] = 0;	// ISHARP LBA PWL for Seg 5. BASE value in U0.6 format
	} else {
		// ISHARP_LBA_PWL_SEG0: ISHARP Local Brightness Adjustment PWL Segment 0
		dscl_prog_data->isharp_lba.in_seg[0] = 0;	// ISHARP LBA PWL for Seg 0. INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[0] = 0;	// ISHARP LBA PWL for Seg 0. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[0] = 40;	// ISHARP LBA for Seg 0. SLOPE value in S5.3 format
		// ISHARP_LBA_PWL_SEG1: ISHARP LBA PWL Segment 1
		dscl_prog_data->isharp_lba.in_seg[1] = 204;	// ISHARP LBA PWL for Seg 1. INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[1] = 63; // ISHARP LBA PWL for Seg 1. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[1] = 0; // ISHARP LBA for Seg 1. SLOPE value in S5.3 format
		// ISHARP_LBA_PWL_SEG2: ISHARP LBA PWL Segment 2
		dscl_prog_data->isharp_lba.in_seg[2] = 818; // ISHARP LBA PWL for Seg 2. INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[2] = 63; // ISHARP LBA PWL for Seg 2. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[2] = 0x1D9; // ISHARP LBA for Seg 2. SLOPE value in S5.3 format = -39
		// ISHARP_LBA_PWL_SEG3: ISHARP LBA PWL Segment 3
		dscl_prog_data->isharp_lba.in_seg[3] = 1023; // ISHARP LBA PWL for Seg 3.INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[3] = 0; // ISHARP LBA PWL for Seg 3. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[3] = 0; // ISHARP LBA for Seg 3. SLOPE value in S5.3 format
		// ISHARP_LBA_PWL_SEG4: ISHARP LBA PWL Segment 4
		dscl_prog_data->isharp_lba.in_seg[4] = 1023; // ISHARP LBA PWL for Seg 4.INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[4] = 0; // ISHARP LBA PWL for Seg 4. BASE value in U0.6 format
		dscl_prog_data->isharp_lba.slope_seg[4] = 0; // ISHARP LBA for Seg 4. SLOPE value in S5.3 format
		// ISHARP_LBA_PWL_SEG5: ISHARP LBA PWL Segment 5
		dscl_prog_data->isharp_lba.in_seg[5] = 1023; // ISHARP LBA PWL for Seg 5.INPUT value in U0.10 format
		dscl_prog_data->isharp_lba.base_seg[5] = 0;	// ISHARP LBA PWL for Seg 5. BASE value in U0.6 format
	}

	// Program the nldelta soft clip values
	if (lls_pref == LLS_PREF_YES) {
		dscl_prog_data->isharp_nldelta_sclip.enable_p = 0;	/* ISHARP_NLDELTA_SCLIP_EN_P */
		dscl_prog_data->isharp_nldelta_sclip.pivot_p = 0;	/* ISHARP_NLDELTA_SCLIP_PIVOT_P */
		dscl_prog_data->isharp_nldelta_sclip.slope_p = 0;	/* ISHARP_NLDELTA_SCLIP_SLOPE_P */
		dscl_prog_data->isharp_nldelta_sclip.enable_n = 1;	/* ISHARP_NLDELTA_SCLIP_EN_N */
		dscl_prog_data->isharp_nldelta_sclip.pivot_n = 71;	/* ISHARP_NLDELTA_SCLIP_PIVOT_N */
		dscl_prog_data->isharp_nldelta_sclip.slope_n = 16;	/* ISHARP_NLDELTA_SCLIP_SLOPE_N */
	} else {
		dscl_prog_data->isharp_nldelta_sclip.enable_p = 1;	/* ISHARP_NLDELTA_SCLIP_EN_P */
		dscl_prog_data->isharp_nldelta_sclip.pivot_p = 70;	/* ISHARP_NLDELTA_SCLIP_PIVOT_P */
		dscl_prog_data->isharp_nldelta_sclip.slope_p = 24;	/* ISHARP_NLDELTA_SCLIP_SLOPE_P */
		dscl_prog_data->isharp_nldelta_sclip.enable_n = 1;	/* ISHARP_NLDELTA_SCLIP_EN_N */
		dscl_prog_data->isharp_nldelta_sclip.pivot_n = 70;	/* ISHARP_NLDELTA_SCLIP_PIVOT_N */
		dscl_prog_data->isharp_nldelta_sclip.slope_n = 24;	/* ISHARP_NLDELTA_SCLIP_SLOPE_N */
	}

	// Set the values as per lookup table
	spl_set_blur_scale_data(dscl_prog_data, data);
}

/* Calculate recout, scaling ratio, and viewport, then get optimal number of taps */
static bool spl_calculate_number_of_taps(struct spl_in *spl_in, struct spl_scratch *spl_scratch, struct spl_out *spl_out,
	bool *enable_easf_v, bool *enable_easf_h, bool *enable_isharp)
{
	bool res = false;

	memset(spl_scratch, 0, sizeof(struct spl_scratch));
	spl_scratch->scl_data.h_active = spl_in->h_active;
	spl_scratch->scl_data.v_active = spl_in->v_active;

	// All SPL calls
	/* recout calculation */
	/* depends on h_active */
	spl_calculate_recout(spl_in, spl_scratch, spl_out);
	/* depends on pixel format */
	spl_calculate_scaling_ratios(spl_in, spl_scratch, spl_out);
	/* Adjust recout for opp if needed */
	spl_opp_adjust_rect(&spl_scratch->scl_data.recout, &spl_in->basic_in.opp_recout_adjust);
	/* depends on scaling ratios and recout, does not calculate offset yet */
	spl_calculate_viewport_size(spl_in, spl_scratch);

	res = spl_get_optimal_number_of_taps(
			  spl_in->basic_out.max_downscale_src_width, spl_in,
			  spl_scratch, &spl_in->scaling_quality, enable_easf_v,
			  enable_easf_h, enable_isharp);
	return res;
}

/* Calculate scaler parameters */
bool SPL_NAMESPACE(spl_calculate_scaler_params(struct spl_in *spl_in, struct spl_out *spl_out))
{
	bool res = false;
	bool enable_easf_v = false;
	bool enable_easf_h = false;
	int vratio = 0;
	int hratio = 0;
	struct spl_scratch spl_scratch;
	struct spl_fixed31_32 isharp_scale_ratio;
	enum system_setup setup;
	bool enable_isharp = false;
	const struct spl_scaler_data *data = &spl_scratch.scl_data;

	res = spl_calculate_number_of_taps(spl_in, &spl_scratch, spl_out,
		&enable_easf_v, &enable_easf_h, &enable_isharp);

	/*
	 * Depends on recout, scaling ratios, h_active and taps
	 * May need to re-check lb size after this in some obscure scenario
	 */
	if (res)
		spl_calculate_inits_and_viewports(spl_in, &spl_scratch);
	// Handle 3d recout
	spl_handle_3d_recout(spl_in, &spl_scratch.scl_data.recout);
	// Clamp
	spl_clamp_viewport(&spl_scratch.scl_data.viewport, spl_in->min_viewport_size);

	// Save all calculated parameters in dscl_prog_data structure to program hw registers
	spl_set_dscl_prog_data(spl_in, &spl_scratch, spl_out, enable_easf_v, enable_easf_h, enable_isharp);

	if (!res)
		return res;

	if (spl_in->lls_pref == LLS_PREF_YES) {
		if (spl_in->is_hdr_on)
			setup = HDR_L;
		else
			setup = SDR_L;
	} else {
		if (spl_in->is_hdr_on)
			setup = HDR_NL;
		else
			setup = SDR_NL;
	}

	// Set EASF
	spl_set_easf_data(&spl_scratch, spl_out, enable_easf_v, enable_easf_h, spl_in->lls_pref,
		spl_in->basic_in.format, setup, spl_in->sdr_white_level_nits);

	// Set iSHARP
	vratio = spl_fixpt_ceil(spl_scratch.scl_data.ratios.vert);
	hratio = spl_fixpt_ceil(spl_scratch.scl_data.ratios.horz);
	if (vratio <= hratio)
		isharp_scale_ratio = spl_scratch.scl_data.recip_ratios.vert;
	else
		isharp_scale_ratio = spl_scratch.scl_data.recip_ratios.horz;

	spl_set_isharp_data(spl_out->dscl_prog_data, spl_in->adaptive_sharpness, enable_isharp,
		spl_in->lls_pref, spl_in->basic_in.format, data, isharp_scale_ratio, setup,
		spl_in->debug.scale_to_sharpness_policy);

	return res;
}

/* External interface to get number of taps only */
bool SPL_NAMESPACE(spl_get_number_of_taps(struct spl_in *spl_in, struct spl_out *spl_out))
{
	bool res = false;
	bool enable_easf_v = false;
	bool enable_easf_h = false;
	bool enable_isharp = false;
	struct spl_scratch spl_scratch;
	struct dscl_prog_data *dscl_prog_data = spl_out->dscl_prog_data;
	const struct spl_scaler_data *data = &spl_scratch.scl_data;

	res = spl_calculate_number_of_taps(spl_in, &spl_scratch, spl_out,
		&enable_easf_v, &enable_easf_h, &enable_isharp);
	spl_set_taps_data(dscl_prog_data, data);
	return res;
}
