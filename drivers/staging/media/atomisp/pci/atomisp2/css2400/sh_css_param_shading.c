/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/slab.h>

#include <math_support.h>
#include "sh_css_param_shading.h"
#include "ia_css_shading.h"
#include "assert_support.h"
#include "sh_css_defs.h"
#include "sh_css_internal.h"
#include "ia_css_debug.h"
#include "ia_css_pipe_binarydesc.h"

#include "sh_css_hrt.h"

#include "platform_support.h"

/* Bilinear interpolation on shading tables:
 * For each target point T, we calculate the 4 surrounding source points:
 * ul (upper left), ur (upper right), ll (lower left) and lr (lower right).
 * We then calculate the distances from the T to the source points: x0, x1,
 * y0 and y1.
 * We then calculate the value of T:
 *   dx0*dy0*Slr + dx0*dy1*Sur + dx1*dy0*Sll + dx1*dy1*Sul.
 * We choose a grid size of 1x1 which means:
 *   dx1 = 1-dx0
 *   dy1 = 1-dy0
 *
 *   Sul dx0         dx1      Sur
 *    .<----->|<------------->.
 *    ^
 * dy0|
 *    v        T
 *    -        .
 *    ^
 *    |
 * dy1|
 *    v
 *    .                        .
 *   Sll                      Slr
 *
 * Padding:
 * The area that the ISP operates on can include padding both on the left
 * and the right. We need to padd the shading table such that the shading
 * values end up on the correct pixel values. This means we must padd the
 * shading table to match the ISP padding.
 * We can have 5 cases:
 * 1. All 4 points fall in the left padding.
 * 2. The left 2 points fall in the left padding.
 * 3. All 4 points fall in the cropped (target) region.
 * 4. The right 2 points fall in the right padding.
 * 5. All 4 points fall in the right padding.
 * Cases 1 and 5 are easy to handle: we simply use the
 * value 1 in the shading table.
 * Cases 2 and 4 require interpolation that takes into
 * account how far into the padding area the pixels
 * fall. We extrapolate the shading table into the
 * padded area and then interpolate.
 */
static void
crop_and_interpolate(unsigned int cropped_width,
		     unsigned int cropped_height,
		     unsigned int left_padding,
		     int right_padding,
		     int top_padding,
		     const struct ia_css_shading_table *in_table,
		     struct ia_css_shading_table *out_table,
		     enum ia_css_sc_color color)
{
	unsigned int i, j,
		     sensor_width,
		     sensor_height,
		     table_width,
		     table_height,
		     table_cell_h,
		     out_cell_size,
		     in_cell_size,
		     out_start_row,
		     padded_width;
	int out_start_col, /* can be negative to indicate padded space */
	    table_cell_w;
	unsigned short *in_ptr,
		       *out_ptr;

	assert(in_table != NULL);
	assert(out_table != NULL);

	sensor_width  = in_table->sensor_width;
	sensor_height = in_table->sensor_height;
	table_width   = in_table->width;
	table_height  = in_table->height;
	in_ptr = in_table->data[color];
	out_ptr = out_table->data[color];

	padded_width = cropped_width + left_padding + right_padding;
	out_cell_size = CEIL_DIV(padded_width, out_table->width - 1);
	in_cell_size  = CEIL_DIV(sensor_width, table_width - 1);

	out_start_col = ((int)sensor_width - (int)cropped_width)/2 - left_padding;
	out_start_row = ((int)sensor_height - (int)cropped_height)/2 - top_padding;
	table_cell_w = (int)((table_width-1) * in_cell_size);
	table_cell_h = (table_height-1) * in_cell_size;

	for (i = 0; i < out_table->height; i++) {
		int ty, src_y0, src_y1;
		unsigned int sy0, sy1, dy0, dy1, divy;

		/* calculate target point and make sure it falls within
		   the table */
		ty = out_start_row + i * out_cell_size;

		/* calculate closest source points in shading table and
		   make sure they fall within the table */
		src_y0 = ty / (int)in_cell_size;
		if (in_cell_size < out_cell_size)
			src_y1 = (ty + out_cell_size) / in_cell_size;
		else
			src_y1 = src_y0 + 1;
		src_y0 = clamp(src_y0, 0, (int)table_height-1);
		src_y1 = clamp(src_y1, 0, (int)table_height-1);
		ty = min(clamp(ty, 0, (int)sensor_height-1),
				 (int)table_cell_h);

		/* calculate closest source points for distance computation */
		sy0 = min(src_y0 * in_cell_size, sensor_height-1);
		sy1 = min(src_y1 * in_cell_size, sensor_height-1);
		/* calculate distance between source and target pixels */
		dy0 = ty - sy0;
		dy1 = sy1 - ty;
		divy = sy1 - sy0;
		if (divy == 0) {
			dy0 = 1;
			divy = 1;
		}

		for (j = 0; j < out_table->width; j++, out_ptr++) {
			int tx, src_x0, src_x1;
			unsigned int sx0, sx1, dx0, dx1, divx;
			unsigned short s_ul, s_ur, s_ll, s_lr;

			/* calculate target point */
			tx = out_start_col + j * out_cell_size;
			/* calculate closest source points. */
			src_x0 = tx / (int)in_cell_size;
			if (in_cell_size < out_cell_size) {
				src_x1 = (tx + out_cell_size) /
					 (int)in_cell_size;
			} else {
				src_x1 = src_x0 + 1;
			}
			/* if src points fall in padding, select closest ones.*/
			src_x0 = clamp(src_x0, 0, (int)table_width-1);
			src_x1 = clamp(src_x1, 0, (int)table_width-1);
			tx = min(clamp(tx, 0, (int)sensor_width-1),
				 (int)table_cell_w);
			/* calculate closest source points for distance
			   computation */
			sx0 = min(src_x0 * in_cell_size, sensor_width-1);
			sx1 = min(src_x1 * in_cell_size, sensor_width-1);
			/* calculate distances between source and target
			   pixels */
			dx0 = tx - sx0;
			dx1 = sx1 - tx;
			divx = sx1 - sx0;
			/* if we're at the edge, we just use the closest
			   point still in the grid. We make up for the divider
			   in this case by setting the distance to
			   out_cell_size, since it's actually 0. */
			if (divx == 0) {
				dx0 = 1;
				divx = 1;
			}

			/* get source pixel values */
			s_ul = in_ptr[(table_width*src_y0)+src_x0];
			s_ur = in_ptr[(table_width*src_y0)+src_x1];
			s_ll = in_ptr[(table_width*src_y1)+src_x0];
			s_lr = in_ptr[(table_width*src_y1)+src_x1];

			*out_ptr = (unsigned short) ((dx0*dy0*s_lr + dx0*dy1*s_ur + dx1*dy0*s_ll + dx1*dy1*s_ul) /
					(divx*divy));
		}
	}
}

void
sh_css_params_shading_id_table_generate(
	struct ia_css_shading_table **target_table,
#ifndef ISP2401
	const struct ia_css_binary *binary)
#else
	unsigned int table_width,
	unsigned int table_height)
#endif
{
	/* initialize table with ones, shift becomes zero */
#ifndef ISP2401
	unsigned int i, j, table_width, table_height;
#else
	unsigned int i, j;
#endif
	struct ia_css_shading_table *result;

	assert(target_table != NULL);
#ifndef ISP2401
	assert(binary != NULL);
#endif

#ifndef ISP2401
	table_width  = binary->sctbl_width_per_color;
	table_height = binary->sctbl_height;
#endif
	result = ia_css_shading_table_alloc(table_width, table_height);
	if (result == NULL) {
		*target_table = NULL;
		return;
	}

	for (i = 0; i < IA_CSS_SC_NUM_COLORS; i++) {
		for (j = 0; j < table_height * table_width; j++)
			result->data[i][j] = 1;
	}
	result->fraction_bits = 0;
	*target_table = result;
}

void
prepare_shading_table(const struct ia_css_shading_table *in_table,
		      unsigned int sensor_binning,
		      struct ia_css_shading_table **target_table,
		      const struct ia_css_binary *binary,
		      unsigned int bds_factor)
{
	unsigned int input_width,
		     input_height,
		     table_width,
		     table_height,
		     left_padding,
		     top_padding,
		     padded_width,
		     left_cropping,
		     i;
	unsigned int bds_numerator, bds_denominator;
	int right_padding;

	struct ia_css_shading_table *result;

	assert(target_table != NULL);
	assert(binary != NULL);

	if (!in_table) {
#ifndef ISP2401
		sh_css_params_shading_id_table_generate(target_table, binary);
#else
		sh_css_params_shading_id_table_generate(target_table,
			binary->sctbl_legacy_width_per_color, binary->sctbl_legacy_height);
#endif
		return;
	}

	padded_width = binary->in_frame_info.padded_width;
	/* We use the ISP input resolution for the shading table because
	   shading correction is performed in the bayer domain (before bayer
	   down scaling). */
#if defined(USE_INPUT_SYSTEM_VERSION_2401)
	padded_width = CEIL_MUL(binary->effective_in_frame_res.width + 2*ISP_VEC_NELEMS,
					2*ISP_VEC_NELEMS);
#endif
	input_height  = binary->in_frame_info.res.height;
	input_width   = binary->in_frame_info.res.width;
	left_padding  = binary->left_padding;
	left_cropping = (binary->info->sp.pipeline.left_cropping == 0) ?
			binary->dvs_envelope.width : 2*ISP_VEC_NELEMS;

	sh_css_bds_factor_get_numerator_denominator
		(bds_factor, &bds_numerator, &bds_denominator);

	left_padding  = (left_padding + binary->info->sp.pipeline.left_cropping) * bds_numerator / bds_denominator - binary->info->sp.pipeline.left_cropping;
	right_padding = (binary->internal_frame_info.res.width - binary->effective_in_frame_res.width * bds_denominator / bds_numerator - left_cropping) * bds_numerator / bds_denominator;
	top_padding = binary->info->sp.pipeline.top_cropping * bds_numerator / bds_denominator - binary->info->sp.pipeline.top_cropping;

#if !defined(USE_WINDOWS_BINNING_FACTOR)
	/* @deprecated{This part of the code will be replaced by the code
	 * in the #else section below to make the calculation same across
	 * all platforms.
	 * Android and Windows platforms interpret the binning_factor parameter
	 * differently. In Android, the binning factor is expressed in the form
	 * 2^N * 2^N, whereas in Windows platform, the binning factor is N*N}
	 */

	/* We take into account the binning done by the sensor. We do this
	   by cropping the non-binned part of the shading table and then
	   increasing the size of a grid cell with this same binning factor. */
	input_width  <<= sensor_binning;
	input_height <<= sensor_binning;
	/* We also scale the padding by the same binning factor. This will
	   make it much easier later on to calculate the padding of the
	   shading table. */
	left_padding  <<= sensor_binning;
	right_padding <<= sensor_binning;
	top_padding   <<= sensor_binning;
#else
	input_width   *= sensor_binning;
	input_height  *= sensor_binning;
	left_padding  *= sensor_binning;
	right_padding *= sensor_binning;
	top_padding   *= sensor_binning;
#endif /*USE_WINDOWS_BINNING_FACTOR*/

	/* during simulation, the used resolution can exceed the sensor
	   resolution, so we clip it. */
	input_width  = min(input_width,  in_table->sensor_width);
	input_height = min(input_height, in_table->sensor_height);

#ifndef ISP2401
	table_width  = binary->sctbl_width_per_color;
	table_height = binary->sctbl_height;
#else
	/* This prepare_shading_table() function is called only in legacy API (not in new API).
	   Then, the legacy shading table width and height should be used. */
	table_width  = binary->sctbl_legacy_width_per_color;
	table_height = binary->sctbl_legacy_height;
#endif

	result = ia_css_shading_table_alloc(table_width, table_height);
	if (result == NULL) {
		*target_table = NULL;
		return;
	}
	result->sensor_width  = in_table->sensor_width;
	result->sensor_height = in_table->sensor_height;
	result->fraction_bits = in_table->fraction_bits;

	/* now we crop the original shading table and then interpolate to the
	   requested resolution and decimation factor. */
	for (i = 0; i < IA_CSS_SC_NUM_COLORS; i++) {
		crop_and_interpolate(input_width, input_height,
				     left_padding, right_padding, top_padding,
				     in_table,
				     result, i);
	}
	*target_table = result;
}

struct ia_css_shading_table *
ia_css_shading_table_alloc(
	unsigned int width,
	unsigned int height)
{
	unsigned int i;
	struct ia_css_shading_table *me;

	IA_CSS_ENTER("");

	me = kmalloc(sizeof(*me), GFP_KERNEL);
	if (!me)
		return me;

	me->width         = width;
	me->height        = height;
	me->sensor_width  = 0;
	me->sensor_height = 0;
	me->fraction_bits = 0;
	for (i = 0; i < IA_CSS_SC_NUM_COLORS; i++) {
		me->data[i] =
		    sh_css_malloc(width * height * sizeof(*me->data[0]));
		if (me->data[i] == NULL) {
			unsigned int j;
			for (j = 0; j < i; j++) {
				sh_css_free(me->data[j]);
				me->data[j] = NULL;
			}
			kfree(me);
			return NULL;
		}
	}

	IA_CSS_LEAVE("");
	return me;
}

void
ia_css_shading_table_free(struct ia_css_shading_table *table)
{
	unsigned int i;

	if (table == NULL)
		return;

	/* We only output logging when the table is not NULL, otherwise
	 * logs will give the impression that a table was freed.
	 * */
	IA_CSS_ENTER("");

	for (i = 0; i < IA_CSS_SC_NUM_COLORS; i++) {
		if (table->data[i]) {
			sh_css_free(table->data[i]);
			table->data[i] = NULL;
		}
	}
	kfree(table);

	IA_CSS_LEAVE("");
}

