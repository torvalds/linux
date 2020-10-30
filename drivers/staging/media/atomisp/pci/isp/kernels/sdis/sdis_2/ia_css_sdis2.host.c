// SPDX-License-Identifier: GPL-2.0
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

#include "hmm.h"

#include <assert_support.h>
#include "ia_css_debug.h"
#include "ia_css_sdis2.host.h"

const struct ia_css_dvs2_coefficients default_sdis2_config = {
	.grid = { 0, 0, 0, 0, 0, 0, 0, 0 },
	.hor_coefs = { NULL, NULL, NULL, NULL },
	.ver_coefs = { NULL, NULL, NULL, NULL },
};

static void
fill_row(short *private, const short *public, unsigned int width,
	 unsigned int padding)
{
	memcpy(private, public, width * sizeof(short));
	memset(&private[width], 0, padding * sizeof(short));
}

void ia_css_sdis2_horicoef_vmem_encode(
    struct sh_css_isp_sdis_hori_coef_tbl *to,
    const struct ia_css_dvs2_coefficients *from,
    unsigned int size)
{
	unsigned int aligned_width = from->grid.aligned_width *
				     from->grid.bqs_per_grid_cell;
	unsigned int width         = from->grid.num_hor_coefs;
	int      padding       = aligned_width - width;
	unsigned int stride        = size / IA_CSS_DVS2_NUM_COEF_TYPES / sizeof(short);
	unsigned int total_bytes   = aligned_width * IA_CSS_DVS2_NUM_COEF_TYPES *
				     sizeof(short);
	short   *private       = (short *)to;

	/* Copy the table, add padding */
	assert(padding >= 0);
	assert(total_bytes <= size);
	assert(size % (IA_CSS_DVS2_NUM_COEF_TYPES * ISP_VEC_NELEMS * sizeof(
			   short)) == 0);
	fill_row(&private[0 * stride], from->hor_coefs.odd_real,  width, padding);
	fill_row(&private[1 * stride], from->hor_coefs.odd_imag,  width, padding);
	fill_row(&private[2 * stride], from->hor_coefs.even_real, width, padding);
	fill_row(&private[3 * stride], from->hor_coefs.even_imag, width, padding);
}

void ia_css_sdis2_vertcoef_vmem_encode(
    struct sh_css_isp_sdis_vert_coef_tbl *to,
    const struct ia_css_dvs2_coefficients *from,
    unsigned int size)
{
	unsigned int aligned_height = from->grid.aligned_height *
				      from->grid.bqs_per_grid_cell;
	unsigned int height         = from->grid.num_ver_coefs;
	int      padding        = aligned_height - height;
	unsigned int stride         = size / IA_CSS_DVS2_NUM_COEF_TYPES / sizeof(short);
	unsigned int total_bytes    = aligned_height * IA_CSS_DVS2_NUM_COEF_TYPES *
				      sizeof(short);
	short   *private        = (short *)to;

	/* Copy the table, add padding */
	assert(padding >= 0);
	assert(total_bytes <= size);
	assert(size % (IA_CSS_DVS2_NUM_COEF_TYPES * ISP_VEC_NELEMS * sizeof(
			   short)) == 0);
	fill_row(&private[0 * stride], from->ver_coefs.odd_real,  height, padding);
	fill_row(&private[1 * stride], from->ver_coefs.odd_imag,  height, padding);
	fill_row(&private[2 * stride], from->ver_coefs.even_real, height, padding);
	fill_row(&private[3 * stride], from->ver_coefs.even_imag, height, padding);
}

void ia_css_sdis2_horiproj_encode(
    struct sh_css_isp_sdis_hori_proj_tbl *to,
    const struct ia_css_dvs2_coefficients *from,
    unsigned int size)
{
	(void)to;
	(void)from;
	(void)size;
}

void ia_css_sdis2_vertproj_encode(
    struct sh_css_isp_sdis_vert_proj_tbl *to,
    const struct ia_css_dvs2_coefficients *from,
    unsigned int size)
{
	(void)to;
	(void)from;
	(void)size;
}

void ia_css_get_isp_dvs2_coefficients(
    struct ia_css_stream *stream,
    short *hor_coefs_odd_real,
    short *hor_coefs_odd_imag,
    short *hor_coefs_even_real,
    short *hor_coefs_even_imag,
    short *ver_coefs_odd_real,
    short *ver_coefs_odd_imag,
    short *ver_coefs_even_real,
    short *ver_coefs_even_imag)
{
	struct ia_css_isp_parameters *params;
	unsigned int hor_num_3a, ver_num_3a;
	struct ia_css_binary *dvs_binary;

	IA_CSS_ENTER("void");

	assert(stream);
	assert(hor_coefs_odd_real);
	assert(hor_coefs_odd_imag);
	assert(hor_coefs_even_real);
	assert(hor_coefs_even_imag);
	assert(ver_coefs_odd_real);
	assert(ver_coefs_odd_imag);
	assert(ver_coefs_even_real);
	assert(ver_coefs_even_imag);

	params = stream->isp_params_configs;

	/* Only video pipe supports DVS */
	dvs_binary = ia_css_stream_get_dvs_binary(stream);
	if (!dvs_binary)
		return;

	hor_num_3a  = dvs_binary->dis.coef.dim.width;
	ver_num_3a  = dvs_binary->dis.coef.dim.height;

	memcpy(hor_coefs_odd_real,  params->dvs2_coefs.hor_coefs.odd_real,
	       hor_num_3a * sizeof(short));
	memcpy(hor_coefs_odd_imag,  params->dvs2_coefs.hor_coefs.odd_imag,
	       hor_num_3a * sizeof(short));
	memcpy(hor_coefs_even_real, params->dvs2_coefs.hor_coefs.even_real,
	       hor_num_3a * sizeof(short));
	memcpy(hor_coefs_even_imag, params->dvs2_coefs.hor_coefs.even_imag,
	       hor_num_3a * sizeof(short));
	memcpy(ver_coefs_odd_real,  params->dvs2_coefs.ver_coefs.odd_real,
	       ver_num_3a * sizeof(short));
	memcpy(ver_coefs_odd_imag,  params->dvs2_coefs.ver_coefs.odd_imag,
	       ver_num_3a * sizeof(short));
	memcpy(ver_coefs_even_real, params->dvs2_coefs.ver_coefs.even_real,
	       ver_num_3a * sizeof(short));
	memcpy(ver_coefs_even_imag, params->dvs2_coefs.ver_coefs.even_imag,
	       ver_num_3a * sizeof(short));

	IA_CSS_LEAVE("void");
}

void ia_css_sdis2_clear_coefficients(
    struct ia_css_dvs2_coefficients *dvs2_coefs)
{
	dvs2_coefs->hor_coefs.odd_real  = NULL;
	dvs2_coefs->hor_coefs.odd_imag  = NULL;
	dvs2_coefs->hor_coefs.even_real = NULL;
	dvs2_coefs->hor_coefs.even_imag = NULL;
	dvs2_coefs->ver_coefs.odd_real  = NULL;
	dvs2_coefs->ver_coefs.odd_imag  = NULL;
	dvs2_coefs->ver_coefs.even_real = NULL;
	dvs2_coefs->ver_coefs.even_imag = NULL;
}

int
ia_css_get_dvs2_statistics(
    struct ia_css_dvs2_statistics          *host_stats,
    const struct ia_css_isp_dvs_statistics *isp_stats) {
	struct ia_css_isp_dvs_statistics_map *map;
	int ret = 0;

	IA_CSS_ENTER("host_stats=%p, isp_stats=%p", host_stats, isp_stats);

	assert(host_stats);
	assert(isp_stats);

	map = ia_css_isp_dvs_statistics_map_allocate(isp_stats, NULL);
	if (map)
	{
		hmm_load(isp_stats->data_ptr, map->data_ptr, isp_stats->size);
		ia_css_translate_dvs2_statistics(host_stats, map);
		ia_css_isp_dvs_statistics_map_free(map);
	} else
	{
		IA_CSS_ERROR("out of memory");
		ret = -ENOMEM;
	}

	IA_CSS_LEAVE_ERR(ret);
	return ret;
}

void
ia_css_translate_dvs2_statistics(
    struct ia_css_dvs2_statistics		   *host_stats,
    const struct ia_css_isp_dvs_statistics_map *isp_stats)
{
	unsigned int size_bytes, table_width, table_size, height;
	unsigned int src_offset = 0, dst_offset = 0;
	s32 *htemp_ptr, *vtemp_ptr;

	assert(host_stats);
	assert(host_stats->hor_prod.odd_real);
	assert(host_stats->hor_prod.odd_imag);
	assert(host_stats->hor_prod.even_real);
	assert(host_stats->hor_prod.even_imag);
	assert(host_stats->ver_prod.odd_real);
	assert(host_stats->ver_prod.odd_imag);
	assert(host_stats->ver_prod.even_real);
	assert(host_stats->ver_prod.even_imag);
	assert(isp_stats);
	assert(isp_stats->hor_proj);
	assert(isp_stats->ver_proj);

	IA_CSS_ENTER("hor_coefs.odd_real=%p, hor_coefs.odd_imag=%p, hor_coefs.even_real=%p, hor_coefs.even_imag=%p, ver_coefs.odd_real=%p, ver_coefs.odd_imag=%p, ver_coefs.even_real=%p, ver_coefs.even_imag=%p, haddr=%p, vaddr=%p",
		     host_stats->hor_prod.odd_real, host_stats->hor_prod.odd_imag,
		     host_stats->hor_prod.even_real, host_stats->hor_prod.even_imag,
		     host_stats->ver_prod.odd_real, host_stats->ver_prod.odd_imag,
		     host_stats->ver_prod.even_real, host_stats->ver_prod.even_imag,
		     isp_stats->hor_proj, isp_stats->ver_proj);

	/* Host side: reflecting the true width in bytes */
	size_bytes = host_stats->grid.aligned_width * sizeof(*htemp_ptr);

	/* DDR side: need to be aligned to the system bus width */
	/* statistics table width in terms of 32-bit words*/
	table_width = CEIL_MUL(size_bytes,
			       HIVE_ISP_DDR_WORD_BYTES) / sizeof(*htemp_ptr);
	table_size = table_width * host_stats->grid.aligned_height;

	htemp_ptr = isp_stats->hor_proj; /* horizontal stats */
	vtemp_ptr = isp_stats->ver_proj; /* vertical stats */
	for (height = 0; height < host_stats->grid.aligned_height; height++) {
		/* hor stats */
		memcpy(host_stats->hor_prod.odd_real + dst_offset,
		       &htemp_ptr[0 * table_size + src_offset], size_bytes);
		memcpy(host_stats->hor_prod.odd_imag + dst_offset,
		       &htemp_ptr[1 * table_size + src_offset], size_bytes);
		memcpy(host_stats->hor_prod.even_real + dst_offset,
		       &htemp_ptr[2 * table_size + src_offset], size_bytes);
		memcpy(host_stats->hor_prod.even_imag + dst_offset,
		       &htemp_ptr[3 * table_size + src_offset], size_bytes);

		/* ver stats */
		memcpy(host_stats->ver_prod.odd_real + dst_offset,
		       &vtemp_ptr[0 * table_size + src_offset], size_bytes);
		memcpy(host_stats->ver_prod.odd_imag + dst_offset,
		       &vtemp_ptr[1 * table_size + src_offset], size_bytes);
		memcpy(host_stats->ver_prod.even_real + dst_offset,
		       &vtemp_ptr[2 * table_size + src_offset], size_bytes);
		memcpy(host_stats->ver_prod.even_imag + dst_offset,
		       &vtemp_ptr[3 * table_size + src_offset], size_bytes);

		src_offset += table_width; /* aligned table width */
		dst_offset += host_stats->grid.aligned_width;
	}

	IA_CSS_LEAVE("void");
}

struct ia_css_isp_dvs_statistics *
ia_css_isp_dvs2_statistics_allocate(
    const struct ia_css_dvs_grid_info *grid)
{
	struct ia_css_isp_dvs_statistics *me;
	int size;

	assert(grid);

	IA_CSS_ENTER("grid=%p", grid);

	if (!grid->enable)
		return NULL;

	me = kvcalloc(1, sizeof(*me), GFP_KERNEL);
	if (!me)
		goto err;

	/* on ISP 2 SDIS DMA model, every row of projection table width must be
	   aligned to HIVE_ISP_DDR_WORD_BYTES
	*/
	size = CEIL_MUL(sizeof(int) * grid->aligned_width, HIVE_ISP_DDR_WORD_BYTES)
	       * grid->aligned_height * IA_CSS_DVS2_NUM_COEF_TYPES;

	me->size = 2 * size;
	me->data_ptr = hmm_alloc(me->size, HMM_BO_PRIVATE, 0, NULL, 0);
	if (me->data_ptr == mmgr_NULL)
		goto err;
	me->hor_proj = me->data_ptr;
	me->hor_size = size;
	me->ver_proj = me->data_ptr + size;
	me->ver_size = size;

	IA_CSS_LEAVE("return=%p", me);
	return me;
err:
	ia_css_isp_dvs2_statistics_free(me);
	IA_CSS_LEAVE("return=%p", NULL);

	return NULL;
}

void
ia_css_isp_dvs2_statistics_free(struct ia_css_isp_dvs_statistics *me)
{
	if (me) {
		hmm_free(me->data_ptr);
		kvfree(me);
	}
}

void ia_css_sdis2_horicoef_debug_dtrace(
    const struct ia_css_dvs2_coefficients *config, unsigned int level)
{
	(void)config;
	(void)level;
}

void ia_css_sdis2_vertcoef_debug_dtrace(
    const struct ia_css_dvs2_coefficients *config, unsigned int level)
{
	(void)config;
	(void)level;
}

void ia_css_sdis2_horiproj_debug_dtrace(
    const struct ia_css_dvs2_coefficients *config, unsigned int level)
{
	(void)config;
	(void)level;
}

void ia_css_sdis2_vertproj_debug_dtrace(
    const struct ia_css_dvs2_coefficients *config, unsigned int level)
{
	(void)config;
	(void)level;
}
