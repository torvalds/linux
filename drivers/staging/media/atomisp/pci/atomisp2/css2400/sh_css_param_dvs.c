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

#include "sh_css_param_dvs.h"
#include <assert_support.h>
#include <type_support.h>
#include <ia_css_err.h>
#include <ia_css_types.h>
#include "ia_css_debug.h"
#include "memory_access.h"

static struct ia_css_dvs_6axis_config *
alloc_dvs_6axis_table(const struct ia_css_resolution *frame_res, struct ia_css_dvs_6axis_config  *dvs_config_src)
{
	unsigned int width_y = 0;
	unsigned int height_y = 0;
	unsigned int width_uv = 0;
	unsigned int height_uv = 0;
	enum ia_css_err err = IA_CSS_SUCCESS;
	struct ia_css_dvs_6axis_config  *dvs_config = NULL;

	dvs_config = (struct ia_css_dvs_6axis_config *)sh_css_malloc(sizeof(struct ia_css_dvs_6axis_config));
	if (dvs_config == NULL)	{
		IA_CSS_ERROR("out of memory");
		err = IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
	}
	else
	{	/*Initialize new struct with latest config settings*/
		if (NULL != dvs_config_src) {
			dvs_config->width_y = width_y = dvs_config_src->width_y;
			dvs_config->height_y = height_y = dvs_config_src->height_y;
			dvs_config->width_uv = width_uv = dvs_config_src->width_uv;
			dvs_config->height_uv = height_uv = dvs_config_src->height_uv;
			IA_CSS_LOG("alloc_dvs_6axis_table Y: W %d H %d", width_y, height_y);
		}
		else if (NULL != frame_res) {
			dvs_config->width_y = width_y = DVS_TABLE_IN_BLOCKDIM_X_LUMA(frame_res->width);
			dvs_config->height_y = height_y = DVS_TABLE_IN_BLOCKDIM_Y_LUMA(frame_res->height);
			dvs_config->width_uv = width_uv = DVS_TABLE_IN_BLOCKDIM_X_CHROMA(frame_res->width / 2); /* UV = Y/2, depens on colour format YUV 4.2.0*/
			dvs_config->height_uv = height_uv = DVS_TABLE_IN_BLOCKDIM_Y_CHROMA(frame_res->height / 2);/* UV = Y/2, depens on colour format YUV 4.2.0*/
			IA_CSS_LOG("alloc_dvs_6axis_table Y: W %d H %d", width_y, height_y);
		}

		/* Generate Y buffers  */
		dvs_config->xcoords_y = (uint32_t *)sh_css_malloc(width_y * height_y * sizeof(uint32_t));
		if (dvs_config->xcoords_y == NULL) {
			IA_CSS_ERROR("out of memory");
			err = IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
			goto exit;
		}

		dvs_config->ycoords_y = (uint32_t *)sh_css_malloc(width_y * height_y * sizeof(uint32_t));
		if (dvs_config->ycoords_y == NULL) {
			IA_CSS_ERROR("out of memory");
			err = IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
			goto exit;
		}

		/* Generate UV buffers  */
		IA_CSS_LOG("UV W %d H %d", width_uv, height_uv);

		dvs_config->xcoords_uv = (uint32_t *)sh_css_malloc(width_uv * height_uv * sizeof(uint32_t));
		if (dvs_config->xcoords_uv == NULL) {
			IA_CSS_ERROR("out of memory");
			err = IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
			goto exit;
		}

		dvs_config->ycoords_uv = (uint32_t *)sh_css_malloc(width_uv * height_uv * sizeof(uint32_t));
		if (dvs_config->ycoords_uv == NULL) {
			IA_CSS_ERROR("out of memory");
			err = IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
		}
exit:
		if (err != IA_CSS_SUCCESS) {
			free_dvs_6axis_table(&dvs_config); /* we might have allocated some memory, release this */
			dvs_config = NULL;
		}
	}

	IA_CSS_LEAVE("dvs_config=%p", dvs_config);
	return dvs_config;
}

static void
init_dvs_6axis_table_from_default(struct ia_css_dvs_6axis_config *dvs_config, const struct ia_css_resolution *dvs_offset)
{
	unsigned int x, y;
	unsigned int width_y = dvs_config->width_y;
	unsigned int height_y = dvs_config->height_y;
	unsigned int width_uv = dvs_config->width_uv;
	unsigned int height_uv = dvs_config->height_uv;

	IA_CSS_LOG("Env_X=%d, Env_Y=%d, width_y=%d, height_y=%d",
			   dvs_offset->width, dvs_offset->height, width_y, height_y);
	for (y = 0; y < height_y; y++) {
		for (x = 0; x < width_y; x++) {
			dvs_config->xcoords_y[y*width_y + x] =  (dvs_offset->width + x*DVS_BLOCKDIM_X) << DVS_COORD_FRAC_BITS;
		}
	}

	for (y = 0; y < height_y; y++) {
		for (x = 0; x < width_y; x++) {
			dvs_config->ycoords_y[y*width_y + x] =  (dvs_offset->height + y*DVS_BLOCKDIM_Y_LUMA) << DVS_COORD_FRAC_BITS;
		}
	}

	for (y = 0; y < height_uv; y++) {
		for (x = 0; x < width_uv; x++) { /* Envelope dimensions set in Ypixels hence offset UV = offset Y/2 */
			dvs_config->xcoords_uv[y*width_uv + x] =  ((dvs_offset->width / 2) + x*DVS_BLOCKDIM_X) << DVS_COORD_FRAC_BITS;
		}
	}

	for (y = 0; y < height_uv; y++) {
		for (x = 0; x < width_uv; x++) { /* Envelope dimensions set in Ypixels hence offset UV = offset Y/2 */
			dvs_config->ycoords_uv[y*width_uv + x] =  ((dvs_offset->height / 2) + y*DVS_BLOCKDIM_Y_CHROMA) << DVS_COORD_FRAC_BITS;
		}
	}

}

static void
init_dvs_6axis_table_from_config(struct ia_css_dvs_6axis_config *dvs_config, struct ia_css_dvs_6axis_config  *dvs_config_src)
{
	unsigned int width_y = dvs_config->width_y;
	unsigned int height_y = dvs_config->height_y;
	unsigned int width_uv = dvs_config->width_uv;
	unsigned int height_uv = dvs_config->height_uv;

	memcpy(dvs_config->xcoords_y, dvs_config_src->xcoords_y, (width_y * height_y * sizeof(uint32_t)));
	memcpy(dvs_config->ycoords_y, dvs_config_src->ycoords_y, (width_y * height_y * sizeof(uint32_t)));
	memcpy(dvs_config->xcoords_uv, dvs_config_src->xcoords_uv, (width_uv * height_uv * sizeof(uint32_t)));
	memcpy(dvs_config->ycoords_uv, dvs_config_src->ycoords_uv, (width_uv * height_uv * sizeof(uint32_t)));
}

struct ia_css_dvs_6axis_config *
generate_dvs_6axis_table(const struct ia_css_resolution *frame_res, const struct ia_css_resolution *dvs_offset)
{
	struct ia_css_dvs_6axis_config *dvs_6axis_table;

	assert(frame_res != NULL);
	assert(dvs_offset != NULL);

	dvs_6axis_table = alloc_dvs_6axis_table(frame_res, NULL);
	if (dvs_6axis_table) {
		init_dvs_6axis_table_from_default(dvs_6axis_table, dvs_offset);
		return dvs_6axis_table;
	}
	return NULL;
}

struct ia_css_dvs_6axis_config *
generate_dvs_6axis_table_from_config(struct ia_css_dvs_6axis_config  *dvs_config_src)
{
	struct ia_css_dvs_6axis_config *dvs_6axis_table;

	assert(NULL != dvs_config_src);

	dvs_6axis_table = alloc_dvs_6axis_table(NULL, dvs_config_src);
	if (dvs_6axis_table) {
		init_dvs_6axis_table_from_config(dvs_6axis_table, dvs_config_src);
		return dvs_6axis_table;
	}
	return NULL;
}

void
free_dvs_6axis_table(struct ia_css_dvs_6axis_config  **dvs_6axis_config)
{
	assert(dvs_6axis_config != NULL);
	assert(*dvs_6axis_config != NULL);

	if ((dvs_6axis_config != NULL) && (*dvs_6axis_config != NULL))
	{
		IA_CSS_ENTER_PRIVATE("dvs_6axis_config %p", (*dvs_6axis_config));
		if ((*dvs_6axis_config)->xcoords_y != NULL)
		{
			sh_css_free((*dvs_6axis_config)->xcoords_y);
			(*dvs_6axis_config)->xcoords_y = NULL;
		}

		if ((*dvs_6axis_config)->ycoords_y != NULL)
		{
			sh_css_free((*dvs_6axis_config)->ycoords_y);
			(*dvs_6axis_config)->ycoords_y = NULL;
		}

		/* Free up UV buffers */
		if ((*dvs_6axis_config)->xcoords_uv != NULL)
		{
			sh_css_free((*dvs_6axis_config)->xcoords_uv);
			(*dvs_6axis_config)->xcoords_uv = NULL;
		}

		if ((*dvs_6axis_config)->ycoords_uv != NULL)
		{
			sh_css_free((*dvs_6axis_config)->ycoords_uv);
			(*dvs_6axis_config)->ycoords_uv = NULL;
		}

		IA_CSS_LEAVE_PRIVATE("dvs_6axis_config %p", (*dvs_6axis_config));
		sh_css_free(*dvs_6axis_config);
		*dvs_6axis_config = NULL;
	}
}

void copy_dvs_6axis_table(struct ia_css_dvs_6axis_config *dvs_config_dst,
			const struct ia_css_dvs_6axis_config *dvs_config_src)
{
	unsigned int width_y;
	unsigned int height_y;
	unsigned int width_uv;
	unsigned int height_uv;

	assert(dvs_config_src != NULL);
	assert(dvs_config_dst != NULL);
	assert(dvs_config_src->xcoords_y != NULL);
	assert(dvs_config_src->xcoords_uv != NULL);
	assert(dvs_config_src->ycoords_y != NULL);
	assert(dvs_config_src->ycoords_uv != NULL);
	assert(dvs_config_src->width_y == dvs_config_dst->width_y);
	assert(dvs_config_src->width_uv == dvs_config_dst->width_uv);
	assert(dvs_config_src->height_y == dvs_config_dst->height_y);
	assert(dvs_config_src->height_uv == dvs_config_dst->height_uv);

	width_y = dvs_config_src->width_y;
	height_y = dvs_config_src->height_y;
	width_uv = dvs_config_src->width_uv; /* = Y/2, depens on colour format YUV 4.2.0*/
	height_uv = dvs_config_src->height_uv;

	memcpy(dvs_config_dst->xcoords_y, dvs_config_src->xcoords_y, (width_y * height_y * sizeof(uint32_t)));
	memcpy(dvs_config_dst->ycoords_y, dvs_config_src->ycoords_y, (width_y * height_y * sizeof(uint32_t)));

	memcpy(dvs_config_dst->xcoords_uv, dvs_config_src->xcoords_uv, (width_uv * height_uv * sizeof(uint32_t)));
	memcpy(dvs_config_dst->ycoords_uv, dvs_config_src->ycoords_uv, (width_uv * height_uv * sizeof(uint32_t)));

}

void
ia_css_dvs_statistics_get(enum dvs_statistics_type type,
			  union ia_css_dvs_statistics_host  *host_stats,
			  const union ia_css_dvs_statistics_isp *isp_stats)
{

	if (DVS_STATISTICS == type)
	{
		ia_css_get_dvs_statistics(host_stats->p_dvs_statistics_host,
			isp_stats->p_dvs_statistics_isp);
	} else if (DVS2_STATISTICS == type)
	{
		ia_css_get_dvs2_statistics(host_stats->p_dvs2_statistics_host,
			isp_stats->p_dvs_statistics_isp);
	}
	return;
}

