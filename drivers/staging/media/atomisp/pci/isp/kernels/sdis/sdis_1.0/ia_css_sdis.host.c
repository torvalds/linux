// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "hmm.h"

#include "assert_support.h"
#include "ia_css_debug.h"
#include "ia_css_sdis_types.h"
#include "sdis/common/ia_css_sdis_common.host.h"
#include "ia_css_sdis.host.h"

const struct ia_css_dvs_coefficients default_sdis_config = {
	.grid = { 0, 0, 0, 0, 0, 0, 0, 0 },
	.hor_coefs = NULL,
	.ver_coefs = NULL
};

static void
fill_row(short *private, const short *public, unsigned int width,
	 unsigned int padding)
{
	assert((int)width >= 0);
	assert((int)padding >= 0);
	memcpy(private, public, width * sizeof(short));
	memset(&private[width], 0, padding * sizeof(short));
}

void ia_css_sdis_horicoef_vmem_encode(
    struct sh_css_isp_sdis_hori_coef_tbl *to,
    const struct ia_css_dvs_coefficients *from,
    unsigned int size)
{
	unsigned int aligned_width = from->grid.aligned_width *
				     from->grid.bqs_per_grid_cell;
	unsigned int width         = from->grid.num_hor_coefs;
	int      padding       = aligned_width - width;
	unsigned int stride        = size / IA_CSS_DVS_NUM_COEF_TYPES / sizeof(short);
	unsigned int total_bytes   = aligned_width * IA_CSS_DVS_NUM_COEF_TYPES * sizeof(
					 short);
	short   *public        = from->hor_coefs;
	short   *private       = (short *)to;
	unsigned int type;

	/* Copy the table, add padding */
	assert(padding >= 0);
	assert(total_bytes <= size);
	assert(size % (IA_CSS_DVS_NUM_COEF_TYPES * ISP_VEC_NELEMS * sizeof(
			   short)) == 0);

	for (type = 0; type < IA_CSS_DVS_NUM_COEF_TYPES; type++) {
		fill_row(&private[type * stride], &public[type * width], width, padding);
	}
}

void ia_css_sdis_vertcoef_vmem_encode(
    struct sh_css_isp_sdis_vert_coef_tbl *to,
    const struct ia_css_dvs_coefficients *from,
    unsigned int size)
{
	unsigned int aligned_height = from->grid.aligned_height *
				      from->grid.bqs_per_grid_cell;
	unsigned int height         = from->grid.num_ver_coefs;
	int      padding        = aligned_height - height;
	unsigned int stride         = size / IA_CSS_DVS_NUM_COEF_TYPES / sizeof(short);
	unsigned int total_bytes    = aligned_height * IA_CSS_DVS_NUM_COEF_TYPES *
				      sizeof(short);
	short   *public         = from->ver_coefs;
	short   *private        = (short *)to;
	unsigned int type;

	/* Copy the table, add padding */
	assert(padding >= 0);
	assert(total_bytes <= size);
	assert(size % (IA_CSS_DVS_NUM_COEF_TYPES * ISP_VEC_NELEMS * sizeof(
			   short)) == 0);

	for (type = 0; type < IA_CSS_DVS_NUM_COEF_TYPES; type++) {
		fill_row(&private[type * stride], &public[type * height], height, padding);
	}
}

void ia_css_sdis_horiproj_encode(
    struct sh_css_isp_sdis_hori_proj_tbl *to,
    const struct ia_css_dvs_coefficients *from,
    unsigned int size)
{
	(void)to;
	(void)from;
	(void)size;
}

void ia_css_sdis_vertproj_encode(
    struct sh_css_isp_sdis_vert_proj_tbl *to,
    const struct ia_css_dvs_coefficients *from,
    unsigned int size)
{
	(void)to;
	(void)from;
	(void)size;
}

void ia_css_get_isp_dis_coefficients(
    struct ia_css_stream *stream,
    short *horizontal_coefficients,
    short *vertical_coefficients)
{
	struct ia_css_isp_parameters *params;
	unsigned int hor_num_isp, ver_num_isp;
	unsigned int hor_num_3a,  ver_num_3a;
	int i;
	struct ia_css_binary *dvs_binary;

	IA_CSS_ENTER("void");

	assert(horizontal_coefficients);
	assert(vertical_coefficients);

	params = stream->isp_params_configs;

	/* Only video pipe supports DVS */
	dvs_binary = ia_css_stream_get_dvs_binary(stream);
	if (!dvs_binary)
		return;

	hor_num_isp = dvs_binary->dis.coef.pad.width;
	ver_num_isp = dvs_binary->dis.coef.pad.height;
	hor_num_3a  = dvs_binary->dis.coef.dim.width;
	ver_num_3a  = dvs_binary->dis.coef.dim.height;

	for (i = 0; i < IA_CSS_DVS_NUM_COEF_TYPES; i++) {
		fill_row(&horizontal_coefficients[i * hor_num_isp],
			 &params->dvs_coefs.hor_coefs[i * hor_num_3a], hor_num_3a,
			 hor_num_isp - hor_num_3a);
	}
	for (i = 0; i < SH_CSS_DIS_VER_NUM_COEF_TYPES(dvs_binary); i++) {
		fill_row(&vertical_coefficients[i * ver_num_isp],
			 &params->dvs_coefs.ver_coefs[i * ver_num_3a], ver_num_3a,
			 ver_num_isp - ver_num_3a);
	}

	IA_CSS_LEAVE("void");
}

size_t
ia_css_sdis_hor_coef_tbl_bytes(
    const struct ia_css_binary *binary)
{
	if (binary->info->sp.pipeline.isp_pipe_version == 1)
		return sizeof(short) * IA_CSS_DVS_NUM_COEF_TYPES  * binary->dis.coef.pad.width;
	else
		return sizeof(short) * IA_CSS_DVS2_NUM_COEF_TYPES * binary->dis.coef.pad.width;
}

size_t
ia_css_sdis_ver_coef_tbl_bytes(
    const struct ia_css_binary *binary)
{
	return sizeof(short) * SH_CSS_DIS_VER_NUM_COEF_TYPES(binary) *
	       binary->dis.coef.pad.height;
}

void
ia_css_sdis_init_info(
    struct ia_css_sdis_info *dis,
    unsigned int sc_3a_dis_width,
    unsigned int sc_3a_dis_padded_width,
    unsigned int sc_3a_dis_height,
    unsigned int isp_pipe_version,
    unsigned int enabled)
{
	if (!enabled) {
		*dis = (struct ia_css_sdis_info) { };
		return;
	}

	dis->deci_factor_log2 = SH_CSS_DIS_DECI_FACTOR_LOG2;

	dis->grid.dim.width  =
	    _ISP_BQS(sc_3a_dis_width) >> SH_CSS_DIS_DECI_FACTOR_LOG2;
	dis->grid.dim.height =
	    _ISP_BQS(sc_3a_dis_height) >> SH_CSS_DIS_DECI_FACTOR_LOG2;
	dis->grid.pad.width  =
	    CEIL_SHIFT(_ISP_BQS(sc_3a_dis_padded_width), SH_CSS_DIS_DECI_FACTOR_LOG2);
	dis->grid.pad.height =
	    CEIL_SHIFT(_ISP_BQS(sc_3a_dis_height), SH_CSS_DIS_DECI_FACTOR_LOG2);

	dis->coef.dim.width  =
	    (_ISP_BQS(sc_3a_dis_width)  >> SH_CSS_DIS_DECI_FACTOR_LOG2) <<
	    SH_CSS_DIS_DECI_FACTOR_LOG2;
	dis->coef.dim.height =
	    (_ISP_BQS(sc_3a_dis_height) >> SH_CSS_DIS_DECI_FACTOR_LOG2) <<
	    SH_CSS_DIS_DECI_FACTOR_LOG2;
	dis->coef.pad.width  =
	    __ISP_SDIS_HOR_COEF_NUM_VECS(sc_3a_dis_padded_width) * ISP_VEC_NELEMS;
	dis->coef.pad.height =
	    __ISP_SDIS_VER_COEF_NUM_VECS(sc_3a_dis_height) * ISP_VEC_NELEMS;
	if (isp_pipe_version == 1) {
		dis->proj.dim.width  =
		    _ISP_BQS(sc_3a_dis_height) >> SH_CSS_DIS_DECI_FACTOR_LOG2;
		dis->proj.dim.height =
		    _ISP_BQS(sc_3a_dis_width)  >> SH_CSS_DIS_DECI_FACTOR_LOG2;
	} else {
		dis->proj.dim.width  =
		    (_ISP_BQS(sc_3a_dis_width)  >> SH_CSS_DIS_DECI_FACTOR_LOG2) *
		    (_ISP_BQS(sc_3a_dis_height) >> SH_CSS_DIS_DECI_FACTOR_LOG2);
		dis->proj.dim.height =
		    (_ISP_BQS(sc_3a_dis_width)  >> SH_CSS_DIS_DECI_FACTOR_LOG2) *
		    (_ISP_BQS(sc_3a_dis_height) >> SH_CSS_DIS_DECI_FACTOR_LOG2);
	}
	dis->proj.pad.width  =
	    __ISP_SDIS_HOR_PROJ_NUM_ISP(sc_3a_dis_padded_width,
					sc_3a_dis_height,
					SH_CSS_DIS_DECI_FACTOR_LOG2,
					isp_pipe_version);
	dis->proj.pad.height =
	    __ISP_SDIS_VER_PROJ_NUM_ISP(sc_3a_dis_padded_width,
					SH_CSS_DIS_DECI_FACTOR_LOG2);
}

void ia_css_sdis_clear_coefficients(
    struct ia_css_dvs_coefficients *dvs_coefs)
{
	dvs_coefs->hor_coefs = NULL;
	dvs_coefs->ver_coefs = NULL;
}

int
ia_css_get_dvs_statistics(
    struct ia_css_dvs_statistics	       *host_stats,
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
		ia_css_translate_dvs_statistics(host_stats, map);
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
ia_css_translate_dvs_statistics(
    struct ia_css_dvs_statistics               *host_stats,
    const struct ia_css_isp_dvs_statistics_map *isp_stats)
{
	unsigned int hor_num_isp, ver_num_isp, hor_num_dvs, ver_num_dvs, i;
	s32 *hor_ptr_dvs, *ver_ptr_dvs, *hor_ptr_isp, *ver_ptr_isp;

	assert(host_stats);
	assert(host_stats->hor_proj);
	assert(host_stats->ver_proj);
	assert(isp_stats);
	assert(isp_stats->hor_proj);
	assert(isp_stats->ver_proj);

	IA_CSS_ENTER("hproj=%p, vproj=%p, haddr=%p, vaddr=%p",
		     host_stats->hor_proj, host_stats->ver_proj,
		     isp_stats->hor_proj, isp_stats->ver_proj);

	hor_num_isp = host_stats->grid.aligned_height;
	ver_num_isp = host_stats->grid.aligned_width;
	hor_ptr_isp = isp_stats->hor_proj;
	ver_ptr_isp = isp_stats->ver_proj;
	hor_num_dvs = host_stats->grid.height;
	ver_num_dvs = host_stats->grid.width;
	hor_ptr_dvs = host_stats->hor_proj;
	ver_ptr_dvs = host_stats->ver_proj;

	for (i = 0; i < IA_CSS_DVS_NUM_COEF_TYPES; i++) {
		memcpy(hor_ptr_dvs, hor_ptr_isp, hor_num_dvs * sizeof(int32_t));
		hor_ptr_isp += hor_num_isp;
		hor_ptr_dvs += hor_num_dvs;

		memcpy(ver_ptr_dvs, ver_ptr_isp, ver_num_dvs * sizeof(int32_t));
		ver_ptr_isp += ver_num_isp;
		ver_ptr_dvs += ver_num_dvs;
	}

	IA_CSS_LEAVE("void");
}

struct ia_css_isp_dvs_statistics *
ia_css_isp_dvs_statistics_allocate(
    const struct ia_css_dvs_grid_info *grid)
{
	struct ia_css_isp_dvs_statistics *me;
	int hor_size, ver_size;

	assert(grid);

	IA_CSS_ENTER("grid=%p", grid);

	if (!grid->enable)
		return NULL;

	me = kvcalloc(1, sizeof(*me), GFP_KERNEL);
	if (!me)
		goto err;

	hor_size = CEIL_MUL(sizeof(int) * IA_CSS_DVS_NUM_COEF_TYPES *
			    grid->aligned_height,
			    HIVE_ISP_DDR_WORD_BYTES);
	ver_size = CEIL_MUL(sizeof(int) * IA_CSS_DVS_NUM_COEF_TYPES *
			    grid->aligned_width,
			    HIVE_ISP_DDR_WORD_BYTES);

	me->size = hor_size + ver_size;
	me->data_ptr = hmm_alloc(me->size);
	if (me->data_ptr == mmgr_NULL)
		goto err;
	me->hor_size = hor_size;
	me->hor_proj = me->data_ptr;
	me->ver_size = ver_size;
	me->ver_proj = me->data_ptr + hor_size;

	IA_CSS_LEAVE("return=%p", me);

	return me;
err:
	ia_css_isp_dvs_statistics_free(me);

	IA_CSS_LEAVE("return=%p", NULL);

	return NULL;
}

struct ia_css_isp_dvs_statistics_map *
ia_css_isp_dvs_statistics_map_allocate(
    const struct ia_css_isp_dvs_statistics *isp_stats,
    void *data_ptr)
{
	struct ia_css_isp_dvs_statistics_map *me;
	/* Windows compiler does not like adding sizes to a void *
	 * so we use a local char * instead. */
	char *base_ptr;

	me = kvmalloc(sizeof(*me), GFP_KERNEL);
	if (!me) {
		IA_CSS_LOG("cannot allocate memory");
		goto err;
	}

	me->data_ptr = data_ptr;
	me->data_allocated = !data_ptr;

	if (!me->data_ptr) {
		me->data_ptr = kvmalloc(isp_stats->size, GFP_KERNEL);
		if (!me->data_ptr) {
			IA_CSS_LOG("cannot allocate memory");
			goto err;
		}
	}
	base_ptr = me->data_ptr;

	me->size = isp_stats->size;
	/* GCC complains when we assign a char * to a void *, so these
	 * casts are necessary unfortunately. */
	me->hor_proj = (void *)base_ptr;
	me->ver_proj = (void *)(base_ptr + isp_stats->hor_size);

	return me;
err:
	kvfree(me);
	return NULL;
}

void
ia_css_isp_dvs_statistics_map_free(struct ia_css_isp_dvs_statistics_map *me)
{
	if (me) {
		if (me->data_allocated)
			kvfree(me->data_ptr);
		kvfree(me);
	}
}

void
ia_css_isp_dvs_statistics_free(struct ia_css_isp_dvs_statistics *me)
{
	if (me) {
		hmm_free(me->data_ptr);
		kvfree(me);
	}
}

void ia_css_sdis_horicoef_debug_dtrace(
    const struct ia_css_dvs_coefficients *config, unsigned int level)
{
	(void)config;
	(void)level;
}

void ia_css_sdis_vertcoef_debug_dtrace(
    const struct ia_css_dvs_coefficients *config, unsigned int level)
{
	(void)config;
	(void)level;
}

void ia_css_sdis_horiproj_debug_dtrace(
    const struct ia_css_dvs_coefficients *config, unsigned int level)
{
	(void)config;
	(void)level;
}

void ia_css_sdis_vertproj_debug_dtrace(
    const struct ia_css_dvs_coefficients *config, unsigned int level)
{
	(void)config;
	(void)level;
}
