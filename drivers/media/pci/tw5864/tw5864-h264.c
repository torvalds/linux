/*
 *  TW5864 driver - H.264 headers generation functions
 *
 *  Copyright (C) 2016 Bluecherry, LLC <maintainers@bluecherrydvr.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/log2.h>

#include "tw5864.h"

static u8 marker[] = { 0x00, 0x00, 0x00, 0x01 };

/*
 * Exponential-Golomb coding functions
 *
 * These functions are used for generation of H.264 bitstream headers.
 *
 * This code is derived from tw5864 reference driver by manufacturers, which
 * itself apparently was derived from x264 project.
 */

/* Bitstream writing context */
struct bs {
	u8 *buf; /* pointer to buffer beginning */
	u8 *buf_end; /* pointer to buffer end */
	u8 *ptr; /* pointer to current byte in buffer */
	unsigned int bits_left; /* number of available bits in current byte */
};

static void bs_init(struct bs *s, void *buf, int size)
{
	s->buf = buf;
	s->ptr = buf;
	s->buf_end = s->ptr + size;
	s->bits_left = 8;
}

static int bs_len(struct bs *s)
{
	return s->ptr - s->buf;
}

static void bs_write(struct bs *s, int count, u32 bits)
{
	if (s->ptr >= s->buf_end - 4)
		return;
	while (count > 0) {
		if (count < 32)
			bits &= (1 << count) - 1;
		if (count < s->bits_left) {
			*s->ptr = (*s->ptr << count) | bits;
			s->bits_left -= count;
			break;
		}
		*s->ptr = (*s->ptr << s->bits_left) |
			(bits >> (count - s->bits_left));
		count -= s->bits_left;
		s->ptr++;
		s->bits_left = 8;
	}
}

static void bs_write1(struct bs *s, u32 bit)
{
	if (s->ptr < s->buf_end) {
		*s->ptr <<= 1;
		*s->ptr |= bit;
		s->bits_left--;
		if (s->bits_left == 0) {
			s->ptr++;
			s->bits_left = 8;
		}
	}
}

static void bs_write_ue(struct bs *s, u32 val)
{
	if (val == 0) {
		bs_write1(s, 1);
	} else {
		val++;
		bs_write(s, 2 * fls(val) - 1, val);
	}
}

static void bs_write_se(struct bs *s, int val)
{
	bs_write_ue(s, val <= 0 ? -val * 2 : val * 2 - 1);
}

static void bs_rbsp_trailing(struct bs *s)
{
	bs_write1(s, 1);
	if (s->bits_left != 8)
		bs_write(s, s->bits_left, 0x00);
}

/* H.264 headers generation functions */

static int tw5864_h264_gen_sps_rbsp(u8 *buf, size_t size, int width, int height)
{
	struct bs bs, *s;

	s = &bs;
	bs_init(s, buf, size);
	bs_write(s, 8, 0x42); /* profile_idc, baseline */
	bs_write(s, 1, 1); /* constraint_set0_flag */
	bs_write(s, 1, 1); /* constraint_set1_flag */
	bs_write(s, 1, 0); /* constraint_set2_flag */
	bs_write(s, 5, 0); /* reserved_zero_5bits */
	bs_write(s, 8, 0x1e); /* level_idc */
	bs_write_ue(s, 0); /* seq_parameter_set_id */
	bs_write_ue(s, ilog2(MAX_GOP_SIZE) - 4); /* log2_max_frame_num_minus4 */
	bs_write_ue(s, 0); /* pic_order_cnt_type */
	/* log2_max_pic_order_cnt_lsb_minus4 */
	bs_write_ue(s, ilog2(MAX_GOP_SIZE) - 4);
	bs_write_ue(s, 1); /* num_ref_frames */
	bs_write(s, 1, 0); /* gaps_in_frame_num_value_allowed_flag */
	bs_write_ue(s, width / 16 - 1); /* pic_width_in_mbs_minus1 */
	bs_write_ue(s, height / 16 - 1); /* pic_height_in_map_units_minus1 */
	bs_write(s, 1, 1); /* frame_mbs_only_flag */
	bs_write(s, 1, 0); /* direct_8x8_inference_flag */
	bs_write(s, 1, 0); /* frame_cropping_flag */
	bs_write(s, 1, 0); /* vui_parameters_present_flag */
	bs_rbsp_trailing(s);
	return bs_len(s);
}

static int tw5864_h264_gen_pps_rbsp(u8 *buf, size_t size, int qp)
{
	struct bs bs, *s;

	s = &bs;
	bs_init(s, buf, size);
	bs_write_ue(s, 0); /* pic_parameter_set_id */
	bs_write_ue(s, 0); /* seq_parameter_set_id */
	bs_write(s, 1, 0); /* entropy_coding_mode_flag */
	bs_write(s, 1, 0); /* pic_order_present_flag */
	bs_write_ue(s, 0); /* num_slice_groups_minus1 */
	bs_write_ue(s, 0); /* i_num_ref_idx_l0_active_minus1 */
	bs_write_ue(s, 0); /* i_num_ref_idx_l1_active_minus1 */
	bs_write(s, 1, 0); /* weighted_pred_flag */
	bs_write(s, 2, 0); /* weighted_bipred_idc */
	bs_write_se(s, qp - 26); /* pic_init_qp_minus26 */
	bs_write_se(s, qp - 26); /* pic_init_qs_minus26 */
	bs_write_se(s, 0); /* chroma_qp_index_offset */
	bs_write(s, 1, 0); /* deblocking_filter_control_present_flag */
	bs_write(s, 1, 0); /* constrained_intra_pred_flag */
	bs_write(s, 1, 0); /* redundant_pic_cnt_present_flag */
	bs_rbsp_trailing(s);
	return bs_len(s);
}

static int tw5864_h264_gen_slice_head(u8 *buf, size_t size,
				      unsigned int idr_pic_id,
				      unsigned int frame_gop_seqno,
				      int *tail_nb_bits, u8 *tail)
{
	struct bs bs, *s;
	int is_i_frame = frame_gop_seqno == 0;

	s = &bs;
	bs_init(s, buf, size);
	bs_write_ue(s, 0); /* first_mb_in_slice */
	bs_write_ue(s, is_i_frame ? 2 : 5); /* slice_type - I or P */
	bs_write_ue(s, 0); /* pic_parameter_set_id */
	bs_write(s, ilog2(MAX_GOP_SIZE), frame_gop_seqno); /* frame_num */
	if (is_i_frame)
		bs_write_ue(s, idr_pic_id);

	/* pic_order_cnt_lsb */
	bs_write(s, ilog2(MAX_GOP_SIZE), frame_gop_seqno);

	if (is_i_frame) {
		bs_write1(s, 0); /* no_output_of_prior_pics_flag */
		bs_write1(s, 0); /* long_term_reference_flag */
	} else {
		bs_write1(s, 0); /* num_ref_idx_active_override_flag */
		bs_write1(s, 0); /* ref_pic_list_reordering_flag_l0 */
		bs_write1(s, 0); /* adaptive_ref_pic_marking_mode_flag */
	}

	bs_write_se(s, 0); /* slice_qp_delta */

	if (s->bits_left != 8) {
		*tail = ((s->ptr[0]) << s->bits_left);
		*tail_nb_bits = 8 - s->bits_left;
	} else {
		*tail = 0;
		*tail_nb_bits = 0;
	}

	return bs_len(s);
}

void tw5864_h264_put_stream_header(u8 **buf, size_t *space_left, int qp,
				   int width, int height)
{
	int nal_len;

	/* SPS */
	memcpy(*buf, marker, sizeof(marker));
	*buf += 4;
	*space_left -= 4;

	**buf = 0x67; /* SPS NAL header */
	*buf += 1;
	*space_left -= 1;

	nal_len = tw5864_h264_gen_sps_rbsp(*buf, *space_left, width, height);
	*buf += nal_len;
	*space_left -= nal_len;

	/* PPS */
	memcpy(*buf, marker, sizeof(marker));
	*buf += 4;
	*space_left -= 4;

	**buf = 0x68; /* PPS NAL header */
	*buf += 1;
	*space_left -= 1;

	nal_len = tw5864_h264_gen_pps_rbsp(*buf, *space_left, qp);
	*buf += nal_len;
	*space_left -= nal_len;
}

void tw5864_h264_put_slice_header(u8 **buf, size_t *space_left,
				  unsigned int idr_pic_id,
				  unsigned int frame_gop_seqno,
				  int *tail_nb_bits, u8 *tail)
{
	int nal_len;

	memcpy(*buf, marker, sizeof(marker));
	*buf += 4;
	*space_left -= 4;

	/* Frame NAL header */
	**buf = (frame_gop_seqno == 0) ? 0x25 : 0x21;
	*buf += 1;
	*space_left -= 1;

	nal_len = tw5864_h264_gen_slice_head(*buf, *space_left, idr_pic_id,
					     frame_gop_seqno, tail_nb_bits,
					     tail);
	*buf += nal_len;
	*space_left -= nal_len;
}
