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

#include "ia_css_vf.host.h"
#include <assert_support.h>
#include <ia_css_err.h>
#include <ia_css_frame.h>
#include <ia_css_frame_public.h>
#include <ia_css_pipeline.h>
#define IA_CSS_INCLUDE_CONFIGURATIONS
#include "ia_css_isp_configs.h"

#include "isp.h"

void
ia_css_vf_config(
	struct sh_css_isp_vf_isp_config      *to,
	const struct ia_css_vf_configuration *from,
	unsigned size)
{
	unsigned elems_a = ISP_VEC_NELEMS;

	(void)size;
	to->vf_downscale_bits = from->vf_downscale_bits;
	to->enable = from->info != NULL;

	if (from->info) {
		ia_css_frame_info_to_frame_sp_info(&to->info, from->info);
		ia_css_dma_configure_from_info(&to->dma.port_b, from->info);
		to->dma.width_a_over_b = elems_a / to->dma.port_b.elems;

		/* Assume divisiblity here, may need to generalize to fixed point. */
		assert (elems_a % to->dma.port_b.elems == 0);
	}
}

/* compute the log2 of the downscale factor needed to get closest
 * to the requested viewfinder resolution on the upper side. The output cannot
 * be smaller than the requested viewfinder resolution.
 */
enum ia_css_err
sh_css_vf_downscale_log2(
	const struct ia_css_frame_info *out_info,
	const struct ia_css_frame_info *vf_info,
	unsigned int *downscale_log2)
{
       unsigned int ds_log2 = 0;
       unsigned int out_width;

       if ((out_info == NULL) | (vf_info == NULL))
	       return IA_CSS_ERR_INVALID_ARGUMENTS;

       out_width = out_info->res.width;

       if (out_width == 0)
	       return IA_CSS_ERR_INVALID_ARGUMENTS;

       /* downscale until width smaller than the viewfinder width. We don't
	* test for the height since the vmem buffers only put restrictions on
	* the width of a line, not on the number of lines in a frame.
	*/
       while (out_width >= vf_info->res.width) {
	       ds_log2++;
	       out_width /= 2;
       }
       /* now width is smaller, so we go up one step */
       if ((ds_log2 > 0) && (out_width < ia_css_binary_max_vf_width()))
	       ds_log2--;
       /* TODO: use actual max input resolution of vf_pp binary */
       if ((out_info->res.width >> ds_log2) >= 2 * ia_css_binary_max_vf_width())
	       return IA_CSS_ERR_INVALID_ARGUMENTS;
       *downscale_log2 = ds_log2;
       return IA_CSS_SUCCESS;
}

static enum ia_css_err
configure_kernel(
	const struct ia_css_binary_info *info,
	const struct ia_css_frame_info *out_info,
	const struct ia_css_frame_info *vf_info,
	unsigned int *downscale_log2,
	struct ia_css_vf_configuration *config)
{
       enum ia_css_err err;
       unsigned vf_log_ds = 0;

       /* First compute value */
       if (vf_info) {
	       err = sh_css_vf_downscale_log2(out_info, vf_info, &vf_log_ds);
	       if (err != IA_CSS_SUCCESS)
		       return err;
       }
       vf_log_ds = min(vf_log_ds, info->vf_dec.max_log_downscale);
       *downscale_log2 = vf_log_ds;

       /* Then store it in isp config section */
       config->vf_downscale_bits = vf_log_ds;
       return IA_CSS_SUCCESS;
}

static void
configure_dma(
	struct ia_css_vf_configuration *config,
	const struct ia_css_frame_info *vf_info)
{
	config->info = vf_info;
}

enum ia_css_err
ia_css_vf_configure(
	const struct ia_css_binary *binary,
	const struct ia_css_frame_info *out_info,
	struct ia_css_frame_info *vf_info,
	unsigned int *downscale_log2)
{
	enum ia_css_err err;
	struct ia_css_vf_configuration config;
	const struct ia_css_binary_info *info = &binary->info->sp;

	err = configure_kernel(info, out_info, vf_info, downscale_log2, &config);
	configure_dma(&config, vf_info);

	if (vf_info)
		vf_info->raw_bit_depth = info->dma.vfdec_bits_per_pixel;
	ia_css_configure_vf (binary, &config);

	return IA_CSS_SUCCESS;
}

