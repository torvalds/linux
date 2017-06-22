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

#ifndef _SH_CSS_PARAMS_H_
#define _SH_CSS_PARAMS_H_

/*! \file */

/* Forward declaration to break mutual dependency */
struct ia_css_isp_parameters;

#include <type_support.h>
#include "ia_css_types.h"
#include "ia_css_binary.h"
#include "sh_css_legacy.h"

#include "sh_css_defs.h"	/* SH_CSS_MAX_STAGES */
#include "ia_css_pipeline.h"
#include "ia_css_isp_params.h"
#include "uds/uds_1.0/ia_css_uds_param.h"
#include "crop/crop_1.0/ia_css_crop_types.h"


#define PIX_SHIFT_FILTER_RUN_IN_X 12
#define PIX_SHIFT_FILTER_RUN_IN_Y 12

#include "ob/ob_1.0/ia_css_ob_param.h"
/* Isp configurations per stream */
struct sh_css_isp_param_configs {
	/* OB (Optical Black) */
	struct sh_css_isp_ob_stream_config ob;
};


/* Isp parameters per stream */
struct ia_css_isp_parameters {
	/* UDS */
	struct sh_css_sp_uds_params uds[SH_CSS_MAX_STAGES];
	struct sh_css_isp_param_configs stream_configs;
	struct ia_css_fpn_table     fpn_config;
	struct ia_css_vector	    motion_config;
	const struct ia_css_morph_table   *morph_table;
	const struct ia_css_shading_table *sc_table;
	struct ia_css_shading_table *sc_config;
	struct ia_css_macc_table    macc_table;
	struct ia_css_gamma_table   gc_table;
	struct ia_css_ctc_table     ctc_table;
	struct ia_css_xnr_table     xnr_table;

	struct ia_css_dz_config     dz_config;
	struct ia_css_3a_config     s3a_config;
	struct ia_css_wb_config     wb_config;
	struct ia_css_cc_config     cc_config;
	struct ia_css_cc_config     yuv2rgb_cc_config;
	struct ia_css_cc_config     rgb2yuv_cc_config;
	struct ia_css_tnr_config    tnr_config;
	struct ia_css_ob_config     ob_config;
	/*----- DPC configuration -----*/
	/* The default DPC configuration is retained and currently set
	 * using the stream configuration. The code generated from genparams
	 * uses this configuration to set the DPC parameters per stage but this
	 * will be overwritten by the per pipe configuration */
	struct ia_css_dp_config     dp_config;
	/* ------ pipe specific DPC configuration ------ */
	/* Please note that this implementation is a temporary solution and
	 * should be replaced by CSS per pipe configuration when the support
	 * is ready (HSD 1303967698)*/
	struct ia_css_dp_config     pipe_dp_config[IA_CSS_PIPE_ID_NUM];
	struct ia_css_nr_config     nr_config;
	struct ia_css_ee_config     ee_config;
	struct ia_css_de_config     de_config;
	struct ia_css_gc_config     gc_config;
	struct ia_css_anr_config    anr_config;
	struct ia_css_ce_config     ce_config;
	struct ia_css_formats_config     formats_config;
/* ---- deprecated: replaced with pipe_dvs_6axis_config---- */
	struct ia_css_dvs_6axis_config  *dvs_6axis_config;
	struct ia_css_ecd_config    ecd_config;
	struct ia_css_ynr_config    ynr_config;
	struct ia_css_yee_config    yee_config;
	struct ia_css_fc_config     fc_config;
	struct ia_css_cnr_config    cnr_config;
	struct ia_css_macc_config   macc_config;
	struct ia_css_ctc_config    ctc_config;
	struct ia_css_aa_config     aa_config;
	struct ia_css_aa_config     bds_config;
	struct ia_css_aa_config     raa_config;
	struct ia_css_rgb_gamma_table     r_gamma_table;
	struct ia_css_rgb_gamma_table     g_gamma_table;
	struct ia_css_rgb_gamma_table     b_gamma_table;
	struct ia_css_anr_thres     anr_thres;
	struct ia_css_xnr_config    xnr_config;
	struct ia_css_xnr3_config   xnr3_config;
	struct ia_css_uds_config    uds_config;
	struct ia_css_crop_config   crop_config;
	struct ia_css_output_config output_config;
	struct ia_css_dvs_6axis_config  *pipe_dvs_6axis_config[IA_CSS_PIPE_ID_NUM];
/* ------ deprecated(bz675) : from ------ */
	struct ia_css_shading_settings shading_settings;
/* ------ deprecated(bz675) : to ------ */
	struct ia_css_dvs_coefficients  dvs_coefs;
	struct ia_css_dvs2_coefficients dvs2_coefs;

	bool isp_params_changed;
	bool isp_mem_params_changed
		[IA_CSS_PIPE_ID_NUM][SH_CSS_MAX_STAGES][IA_CSS_NUM_MEMORIES];
	bool dz_config_changed;
	bool motion_config_changed;
	bool dis_coef_table_changed;
	bool dvs2_coef_table_changed;
	bool morph_table_changed;
	bool sc_table_changed;
	bool sc_table_dirty;
	unsigned int sc_table_last_pipe_num;
	bool anr_thres_changed;
/* ---- deprecated: replaced with pipe_dvs_6axis_config_changed ---- */
	bool dvs_6axis_config_changed;
	/* ------ pipe specific DPC configuration ------ */
	/* Please note that this implementation is a temporary solution and
	 * should be replaced by CSS per pipe configuration when the support
	 * is ready (HSD 1303967698) */
	bool pipe_dpc_config_changed[IA_CSS_PIPE_ID_NUM];
/* ------ deprecated(bz675) : from ------ */
	bool shading_settings_changed;
/* ------ deprecated(bz675) : to ------ */
	bool pipe_dvs_6axis_config_changed[IA_CSS_PIPE_ID_NUM];

	bool config_changed[IA_CSS_NUM_PARAMETER_IDS];

	unsigned int sensor_binning;
	/* local buffers, used to re-order the 3a statistics in vmem-format */
	struct sh_css_ddr_address_map pipe_ddr_ptrs[IA_CSS_PIPE_ID_NUM];
	struct sh_css_ddr_address_map_size pipe_ddr_ptrs_size[IA_CSS_PIPE_ID_NUM];
	struct sh_css_ddr_address_map ddr_ptrs;
	struct sh_css_ddr_address_map_size ddr_ptrs_size;
	struct ia_css_frame *output_frame; /**< Output frame the config is to be applied to (optional) */
	uint32_t isp_parameters_id; /**< Unique ID to track which config was actually applied to a particular frame */
};

void
ia_css_params_store_ia_css_host_data(
	hrt_vaddress ddr_addr,
	struct ia_css_host_data *data);

enum ia_css_err
ia_css_params_store_sctbl(
	    const struct ia_css_pipeline_stage *stage,
	    hrt_vaddress ddr_addr,
	    const struct ia_css_shading_table *shading_table);

struct ia_css_host_data *
ia_css_params_alloc_convert_sctbl(
	    const struct ia_css_pipeline_stage *stage,
	    const struct ia_css_shading_table *shading_table);

struct ia_css_isp_config *
sh_css_pipe_isp_config_get(struct ia_css_pipe *pipe);

/* ipu address allocation/free for gdc lut */
hrt_vaddress
sh_css_params_alloc_gdc_lut(void);
void
sh_css_params_free_gdc_lut(hrt_vaddress addr);

enum ia_css_err
sh_css_params_map_and_store_default_gdc_lut(void);

void
sh_css_params_free_default_gdc_lut(void);

hrt_vaddress
sh_css_params_get_default_gdc_lut(void);

hrt_vaddress
sh_css_pipe_get_pp_gdc_lut(const struct ia_css_pipe *pipe);

#endif /* _SH_CSS_PARAMS_H_ */
