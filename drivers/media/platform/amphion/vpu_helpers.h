/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _AMPHION_VPU_HELPERS_H
#define _AMPHION_VPU_HELPERS_H

struct vpu_pair {
	u32 src;
	u32 dst;
};

int vpu_helper_find_in_array_u8(const u8 *array, u32 size, u32 x);
bool vpu_helper_check_type(struct vpu_inst *inst, u32 type);
const struct vpu_format *vpu_helper_find_format(struct vpu_inst *inst, u32 type, u32 pixelfmt);
const struct vpu_format *vpu_helper_enum_format(struct vpu_inst *inst, u32 type, int index);
u32 vpu_helper_valid_frame_width(struct vpu_inst *inst, u32 width);
u32 vpu_helper_valid_frame_height(struct vpu_inst *inst, u32 height);
u32 vpu_helper_get_plane_size(u32 fmt, u32 width, u32 height, int plane_no,
			      u32 stride, u32 interlaced, u32 *pbl);
int vpu_helper_copy_from_stream_buffer(struct vpu_buffer *stream_buffer,
				       u32 *rptr, u32 size, void *dst);
int vpu_helper_copy_to_stream_buffer(struct vpu_buffer *stream_buffer,
				     u32 *wptr, u32 size, void *src);
int vpu_helper_memset_stream_buffer(struct vpu_buffer *stream_buffer,
				    u32 *wptr, u8 val, u32 size);
u32 vpu_helper_get_free_space(struct vpu_inst *inst);
u32 vpu_helper_get_used_space(struct vpu_inst *inst);
int vpu_helper_g_volatile_ctrl(struct v4l2_ctrl *ctrl);
void vpu_helper_get_kmp_next(const u8 *pattern, int *next, int size);
int vpu_helper_kmp_search(u8 *s, int s_len, const u8 *p, int p_len, int *next);
int vpu_helper_kmp_search_in_stream_buffer(struct vpu_buffer *stream_buffer,
					   u32 offset, int bytesused,
					   const u8 *p, int p_len, int *next);
int vpu_helper_find_startcode(struct vpu_buffer *stream_buffer,
			      u32 pixelformat, u32 offset, u32 bytesused);

static inline u32 vpu_helper_step_walk(struct vpu_buffer *stream_buffer, u32 pos, u32 step)
{
	pos += step;
	if (pos > stream_buffer->phys + stream_buffer->length)
		pos -= stream_buffer->length;

	return pos;
}

static inline u8 vpu_helper_read_byte(struct vpu_buffer *stream_buffer, u32 pos)
{
	u8 *pdata = (u8 *)stream_buffer->virt;

	return pdata[pos % stream_buffer->length];
}

int vpu_color_check_primaries(u32 primaries);
int vpu_color_check_transfers(u32 transfers);
int vpu_color_check_matrix(u32 matrix);
int vpu_color_check_full_range(u32 full_range);
u32 vpu_color_cvrt_primaries_v2i(u32 primaries);
u32 vpu_color_cvrt_primaries_i2v(u32 primaries);
u32 vpu_color_cvrt_transfers_v2i(u32 transfers);
u32 vpu_color_cvrt_transfers_i2v(u32 transfers);
u32 vpu_color_cvrt_matrix_v2i(u32 matrix);
u32 vpu_color_cvrt_matrix_i2v(u32 matrix);
u32 vpu_color_cvrt_full_range_v2i(u32 full_range);
u32 vpu_color_cvrt_full_range_i2v(u32 full_range);
int vpu_color_get_default(u32 primaries, u32 *ptransfers, u32 *pmatrix, u32 *pfull_range);

int vpu_find_dst_by_src(struct vpu_pair *pairs, u32 cnt, u32 src);
int vpu_find_src_by_dst(struct vpu_pair *pairs, u32 cnt, u32 dst);
#endif
