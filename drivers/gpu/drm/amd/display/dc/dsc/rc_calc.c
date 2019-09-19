#if defined(CONFIG_DRM_AMD_DC_DSC_SUPPORT)

/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
 *
 */

#include "os_types.h"
#include "rc_calc.h"
#include "qp_tables.h"

#define table_hash(mode, bpc, max_min) ((mode << 16) | (bpc << 8) | max_min)

#define MODE_SELECT(val444, val422, val420) \
	(cm == CM_444 || cm == CM_RGB) ? (val444) : (cm == CM_422 ? (val422) : (val420))


#define TABLE_CASE(mode, bpc, max)   case (table_hash(mode, BPC_##bpc, max)): \
	table = qp_table_##mode##_##bpc##bpc_##max; \
	table_size = sizeof(qp_table_##mode##_##bpc##bpc_##max)/sizeof(*qp_table_##mode##_##bpc##bpc_##max); \
	break


void get_qp_set(qp_set qps, enum colour_mode cm, enum bits_per_comp bpc, enum max_min max_min, float bpp)
{
	int mode = MODE_SELECT(444, 422, 420);
	int sel = table_hash(mode, bpc, max_min);
	int table_size = 0;
	int index;
	const struct qp_entry *table = 0L;

	// alias enum
	enum { min = MM_MIN, max = MM_MAX };
	switch (sel) {
		TABLE_CASE(444,  8, max);
		TABLE_CASE(444,  8, min);
		TABLE_CASE(444, 10, max);
		TABLE_CASE(444, 10, min);
		TABLE_CASE(444, 12, max);
		TABLE_CASE(444, 12, min);
		TABLE_CASE(422,  8, max);
		TABLE_CASE(422,  8, min);
		TABLE_CASE(422, 10, max);
		TABLE_CASE(422, 10, min);
		TABLE_CASE(422, 12, max);
		TABLE_CASE(422, 12, min);
		TABLE_CASE(420,  8, max);
		TABLE_CASE(420,  8, min);
		TABLE_CASE(420, 10, max);
		TABLE_CASE(420, 10, min);
		TABLE_CASE(420, 12, max);
		TABLE_CASE(420, 12, min);
	}

	if (table == 0)
		return;

	index = (bpp - table[0].bpp) * 2;

	/* requested size is bigger than the table */
	if (index >= table_size) {
		dm_error("ERROR: Requested rc_calc to find a bpp entry that exceeds the table size\n");
		return;
	}

	memcpy(qps, table[index].qps, sizeof(qp_set));
}

double dsc_roundf(double num)
{
	if (num < 0.0)
		num = num - 0.5;
	else
		num = num + 0.5;

	return (int)(num);
}

double dsc_ceil(double num)
{
	double retval = (int)num;

	if (retval != num && num > 0)
		retval = num + 1;

	return (int)retval;
}

void get_ofs_set(qp_set ofs, enum colour_mode mode, float bpp)
{
	int   *p = ofs;

	if (mode == CM_444 || mode == CM_RGB) {
		*p++ = (bpp <=  6) ? (0) : ((((bpp >=  8) && (bpp <= 12))) ? (2) : ((bpp >= 15) ? (10) : ((((bpp > 6) && (bpp < 8))) ? (0 + dsc_roundf((bpp -  6) * (2 / 2.0))) : (2 + dsc_roundf((bpp - 12) * (8 / 3.0))))));
		*p++ = (bpp <=  6) ? (-2) : ((((bpp >=  8) && (bpp <= 12))) ? (0) : ((bpp >= 15) ? (8) : ((((bpp > 6) && (bpp < 8))) ? (-2 + dsc_roundf((bpp -  6) * (2 / 2.0))) : (0 + dsc_roundf((bpp - 12) * (8 / 3.0))))));
		*p++ = (bpp <=  6) ? (-2) : ((((bpp >=  8) && (bpp <= 12))) ? (0) : ((bpp >= 15) ? (6) : ((((bpp > 6) && (bpp < 8))) ? (-2 + dsc_roundf((bpp -  6) * (2 / 2.0))) : (0 + dsc_roundf((bpp - 12) * (6 / 3.0))))));
		*p++ = (bpp <=  6) ? (-4) : ((((bpp >=  8) && (bpp <= 12))) ? (-2) : ((bpp >= 15) ? (4) : ((((bpp > 6) && (bpp < 8))) ? (-4 + dsc_roundf((bpp -  6) * (2 / 2.0))) : (-2 + dsc_roundf((bpp - 12) * (6 / 3.0))))));
		*p++ = (bpp <=  6) ? (-6) : ((((bpp >=  8) && (bpp <= 12))) ? (-4) : ((bpp >= 15) ? (2) : ((((bpp > 6) && (bpp < 8))) ? (-6 + dsc_roundf((bpp -  6) * (2 / 2.0))) : (-4 + dsc_roundf((bpp - 12) * (6 / 3.0))))));
		*p++ = (bpp <= 12) ? (-6) : ((bpp >= 15) ? (0) : (-6 + dsc_roundf((bpp - 12) * (6 / 3.0))));
		*p++ = (bpp <= 12) ? (-8) : ((bpp >= 15) ? (-2) : (-8 + dsc_roundf((bpp - 12) * (6 / 3.0))));
		*p++ = (bpp <= 12) ? (-8) : ((bpp >= 15) ? (-4) : (-8 + dsc_roundf((bpp - 12) * (4 / 3.0))));
		*p++ = (bpp <= 12) ? (-8) : ((bpp >= 15) ? (-6) : (-8 + dsc_roundf((bpp - 12) * (2 / 3.0))));
		*p++ = (bpp <= 12) ? (-10) : ((bpp >= 15) ? (-8) : (-10 + dsc_roundf((bpp - 12) * (2 / 3.0))));
		*p++ = -10;
		*p++ = (bpp <=  6) ? (-12) : ((bpp >=  8) ? (-10) : (-12 + dsc_roundf((bpp -  6) * (2 / 2.0))));
		*p++ = -12;
		*p++ = -12;
		*p++ = -12;
	} else if (mode == CM_422) {
		*p++ = (bpp <=  8) ? (2) : ((bpp >= 10) ? (10) : (2 + dsc_roundf((bpp -  8) * (8 / 2.0))));
		*p++ = (bpp <=  8) ? (0) : ((bpp >= 10) ? (8) : (0 + dsc_roundf((bpp -  8) * (8 / 2.0))));
		*p++ = (bpp <=  8) ? (0) : ((bpp >= 10) ? (6) : (0 + dsc_roundf((bpp -  8) * (6 / 2.0))));
		*p++ = (bpp <=  8) ? (-2) : ((bpp >= 10) ? (4) : (-2 + dsc_roundf((bpp -  8) * (6 / 2.0))));
		*p++ = (bpp <=  8) ? (-4) : ((bpp >= 10) ? (2) : (-4 + dsc_roundf((bpp -  8) * (6 / 2.0))));
		*p++ = (bpp <=  8) ? (-6) : ((bpp >= 10) ? (0) : (-6 + dsc_roundf((bpp -  8) * (6 / 2.0))));
		*p++ = (bpp <=  8) ? (-8) : ((bpp >= 10) ? (-2) : (-8 + dsc_roundf((bpp -  8) * (6 / 2.0))));
		*p++ = (bpp <=  8) ? (-8) : ((bpp >= 10) ? (-4) : (-8 + dsc_roundf((bpp -  8) * (4 / 2.0))));
		*p++ = (bpp <=  8) ? (-8) : ((bpp >= 10) ? (-6) : (-8 + dsc_roundf((bpp -  8) * (2 / 2.0))));
		*p++ = (bpp <=  8) ? (-10) : ((bpp >= 10) ? (-8) : (-10 + dsc_roundf((bpp -  8) * (2 / 2.0))));
		*p++ = -10;
		*p++ = (bpp <=  6) ? (-12) : ((bpp >= 7) ? (-10) : (-12 + dsc_roundf((bpp -  6) * (2.0 / 1))));
		*p++ = -12;
		*p++ = -12;
		*p++ = -12;
	} else {
		*p++ = (bpp <=  6) ? (2) : ((bpp >=  8) ? (10) : (2 + dsc_roundf((bpp -  6) * (8 / 2.0))));
		*p++ = (bpp <=  6) ? (0) : ((bpp >=  8) ? (8) : (0 + dsc_roundf((bpp -  6) * (8 / 2.0))));
		*p++ = (bpp <=  6) ? (0) : ((bpp >=  8) ? (6) : (0 + dsc_roundf((bpp -  6) * (6 / 2.0))));
		*p++ = (bpp <=  6) ? (-2) : ((bpp >=  8) ? (4) : (-2 + dsc_roundf((bpp -  6) * (6 / 2.0))));
		*p++ = (bpp <=  6) ? (-4) : ((bpp >=  8) ? (2) : (-4 + dsc_roundf((bpp -  6) * (6 / 2.0))));
		*p++ = (bpp <=  6) ? (-6) : ((bpp >=  8) ? (0) : (-6 + dsc_roundf((bpp -  6) * (6 / 2.0))));
		*p++ = (bpp <=  6) ? (-8) : ((bpp >=  8) ? (-2) : (-8 + dsc_roundf((bpp -  6) * (6 / 2.0))));
		*p++ = (bpp <=  6) ? (-8) : ((bpp >=  8) ? (-4) : (-8 + dsc_roundf((bpp -  6) * (4 / 2.0))));
		*p++ = (bpp <=  6) ? (-8) : ((bpp >=  8) ? (-6) : (-8 + dsc_roundf((bpp -  6) * (2 / 2.0))));
		*p++ = (bpp <=  6) ? (-10) : ((bpp >=  8) ? (-8) : (-10 + dsc_roundf((bpp -  6) * (2 / 2.0))));
		*p++ = -10;
		*p++ = (bpp <=  4) ? (-12) : ((bpp >=  5) ? (-10) : (-12 + dsc_roundf((bpp -  4) * (2 / 1.0))));
		*p++ = -12;
		*p++ = -12;
		*p++ = -12;
	}
}

int median3(int a, int b, int c)
{
	if (a > b)
		swap(a, b);
	if (b > c)
		swap(b, c);
	if (a > b)
		swap(b, c);

	return b;
}

void calc_rc_params(struct rc_params *rc, enum colour_mode cm, enum bits_per_comp bpc, float bpp, int slice_width, int slice_height, int minor_version)
{
	float bpp_group;
	float initial_xmit_delay_factor;
	int source_bpp;
	int padding_pixels;
	int i;

	rc->rc_quant_incr_limit0 = ((bpc == BPC_8) ? 11 : (bpc == BPC_10 ? 15 : 19)) - ((minor_version == 1 && cm == CM_444) ? 1 : 0);
	rc->rc_quant_incr_limit1 = ((bpc == BPC_8) ? 11 : (bpc == BPC_10 ? 15 : 19)) - ((minor_version == 1 && cm == CM_444) ? 1 : 0);

	bpp_group = MODE_SELECT(bpp, bpp * 2.0, bpp * 2.0);

	switch (cm) {
	case CM_420:
		rc->initial_fullness_offset = (bpp >=  6) ? (2048) : ((bpp <=  4) ? (6144) : ((((bpp >  4) && (bpp <=  5))) ? (6144 - dsc_roundf((bpp - 4) * (512))) : (5632 - dsc_roundf((bpp -  5) * (3584)))));
		rc->first_line_bpg_offset   = median3(0, (12 + (int) (0.09 *  min(34, slice_height - 8))), (int)((3 * bpc * 3) - (3 * bpp_group)));
		rc->second_line_bpg_offset  = median3(0, 12, (int)((3 * bpc * 3) - (3 * bpp_group)));
		break;
	case CM_422:
		rc->initial_fullness_offset = (bpp >=  8) ? (2048) : ((bpp <=  7) ? (5632) : (5632 - dsc_roundf((bpp - 7) * (3584))));
		rc->first_line_bpg_offset   = median3(0, (12 + (int) (0.09 *  min(34, slice_height - 8))), (int)((3 * bpc * 4) - (3 * bpp_group)));
		rc->second_line_bpg_offset  = 0;
		break;
	case CM_444:
	case CM_RGB:
		rc->initial_fullness_offset = (bpp >= 12) ? (2048) : ((bpp <=  8) ? (6144) : ((((bpp >  8) && (bpp <= 10))) ? (6144 - dsc_roundf((bpp - 8) * (512 / 2))) : (5632 - dsc_roundf((bpp - 10) * (3584 / 2)))));
		rc->first_line_bpg_offset   = median3(0, (12 + (int) (0.09 *  min(34, slice_height - 8))), (int)(((3 * bpc + (cm == CM_444 ? 0 : 2)) * 3) - (3 * bpp_group)));
		rc->second_line_bpg_offset  = 0;
		break;
	}

	initial_xmit_delay_factor = (cm == CM_444 || cm == CM_RGB) ? 1.0 : 2.0;
	rc->initial_xmit_delay = dsc_roundf(8192.0/2.0/bpp/initial_xmit_delay_factor);

	if (cm == CM_422 || cm == CM_420)
		slice_width /= 2;

	padding_pixels = ((slice_width % 3) != 0) ? (3 - (slice_width % 3)) * (rc->initial_xmit_delay / slice_width) : 0;
	if (3 * bpp_group >= (((rc->initial_xmit_delay + 2) / 3) * (3 + (cm == CM_422)))) {
		if ((rc->initial_xmit_delay + padding_pixels) % 3 == 1)
			rc->initial_xmit_delay++;
	}

	source_bpp = MODE_SELECT(bpc * 3, bpc * 2, bpc * 1.5);

	rc->flatness_min_qp     = ((bpc == BPC_8) ?  (3) : ((bpc == BPC_10) ? (7)  : (11))) - ((minor_version == 1 && cm == CM_444) ? 1 : 0);
	rc->flatness_max_qp     = ((bpc == BPC_8) ? (12) : ((bpc == BPC_10) ? (16) : (20))) - ((minor_version == 1 && cm == CM_444) ? 1 : 0);
	rc->flatness_det_thresh = 2 << (bpc - 8);

	get_qp_set(rc->qp_min, cm, bpc, MM_MIN, bpp);
	get_qp_set(rc->qp_max, cm, bpc, MM_MAX, bpp);
	if (cm == CM_444 && minor_version == 1) {
		for (i = 0; i < QP_SET_SIZE; ++i) {
			rc->qp_min[i] = rc->qp_min[i] > 0 ? rc->qp_min[i] - 1 : 0;
			rc->qp_max[i] = rc->qp_max[i] > 0 ? rc->qp_max[i] - 1 : 0;
		}
	}
	get_ofs_set(rc->ofs, cm, bpp);

	/* fixed parameters */
	rc->rc_model_size    = 8192;
	rc->rc_edge_factor   = 6;
	rc->rc_tgt_offset_hi = 3;
	rc->rc_tgt_offset_lo = 3;

	rc->rc_buf_thresh[0] = 896;
	rc->rc_buf_thresh[1] = 1792;
	rc->rc_buf_thresh[2] = 2688;
	rc->rc_buf_thresh[3] = 3584;
	rc->rc_buf_thresh[4] = 4480;
	rc->rc_buf_thresh[5] = 5376;
	rc->rc_buf_thresh[6] = 6272;
	rc->rc_buf_thresh[7] = 6720;
	rc->rc_buf_thresh[8] = 7168;
	rc->rc_buf_thresh[9] = 7616;
	rc->rc_buf_thresh[10] = 7744;
	rc->rc_buf_thresh[11] = 7872;
	rc->rc_buf_thresh[12] = 8000;
	rc->rc_buf_thresh[13] = 8064;
}

#endif
