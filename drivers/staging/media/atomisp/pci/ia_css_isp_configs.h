/* SPDX-License-Identifier: GPL-2.0 */
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

#ifdef IA_CSS_INCLUDE_CONFIGURATIONS
#include "isp/kernels/crop/crop_1.0/ia_css_crop.host.h"
#include "isp/kernels/dvs/dvs_1.0/ia_css_dvs.host.h"
#include "isp/kernels/fpn/fpn_1.0/ia_css_fpn.host.h"
#include "isp/kernels/ob/ob_1.0/ia_css_ob.host.h"
#include "isp/kernels/output/output_1.0/ia_css_output.host.h"
#include "isp/kernels/qplane/qplane_2/ia_css_qplane.host.h"
#include "isp/kernels/raw/raw_1.0/ia_css_raw.host.h"
#include "isp/kernels/ref/ref_1.0/ia_css_ref.host.h"
#include "isp/kernels/s3a/s3a_1.0/ia_css_s3a.host.h"
#include "isp/kernels/tnr/tnr_1.0/ia_css_tnr.host.h"
#include "isp/kernels/vf/vf_1.0/ia_css_vf.host.h"
#include "isp/kernels/iterator/iterator_1.0/ia_css_iterator.host.h"
#include "isp/kernels/copy_output/copy_output_1.0/ia_css_copy_output.host.h"
#endif

#ifndef _IA_CSS_ISP_CONFIG_H
#define _IA_CSS_ISP_CONFIG_H

enum ia_css_configuration_ids {
	IA_CSS_ITERATOR_CONFIG_ID,
	IA_CSS_COPY_OUTPUT_CONFIG_ID,
	IA_CSS_CROP_CONFIG_ID,
	IA_CSS_FPN_CONFIG_ID,
	IA_CSS_DVS_CONFIG_ID,
	IA_CSS_QPLANE_CONFIG_ID,
	IA_CSS_OUTPUT0_CONFIG_ID,
	IA_CSS_OUTPUT1_CONFIG_ID,
	IA_CSS_OUTPUT_CONFIG_ID,
	IA_CSS_RAW_CONFIG_ID,
	IA_CSS_TNR_CONFIG_ID,
	IA_CSS_REF_CONFIG_ID,
	IA_CSS_VF_CONFIG_ID,

	/* ISP 2401 */
	IA_CSS_SC_CONFIG_ID,

	IA_CSS_NUM_CONFIGURATION_IDS
};

struct ia_css_config_memory_offsets {
	struct {
		struct ia_css_isp_parameter iterator;
		struct ia_css_isp_parameter copy_output;
		struct ia_css_isp_parameter crop;
		struct ia_css_isp_parameter fpn;
		struct ia_css_isp_parameter dvs;
		struct ia_css_isp_parameter qplane;
		struct ia_css_isp_parameter output0;
		struct ia_css_isp_parameter output1;
		struct ia_css_isp_parameter output;
		struct ia_css_isp_parameter raw;
		struct ia_css_isp_parameter tnr;
		struct ia_css_isp_parameter ref;
		struct ia_css_isp_parameter vf;
	} dmem;
};

#if defined(IA_CSS_INCLUDE_CONFIGURATIONS)

#include "ia_css_stream.h"   /* struct ia_css_stream */
#include "ia_css_binary.h"   /* struct ia_css_binary */

int ia_css_configure_iterator(const struct ia_css_binary *binary,
			      const struct ia_css_iterator_configuration *config_dmem);

int ia_css_configure_copy_output(const struct ia_css_binary *binary,
				 const struct ia_css_copy_output_configuration *config_dmem);

int ia_css_configure_crop(const struct ia_css_binary *binary,
			  const struct ia_css_crop_configuration *config_dmem);

int ia_css_configure_fpn(const struct ia_css_binary *binary,
			 const struct ia_css_fpn_configuration *config_dmem);

int ia_css_configure_dvs(const struct ia_css_binary *binary,
			 const struct ia_css_dvs_configuration *config_dmem);

int ia_css_configure_qplane(const struct ia_css_binary *binary,
			    const struct ia_css_qplane_configuration *config_dmem);
int ia_css_configure_output0(const struct ia_css_binary *binary,
			     const struct ia_css_output0_configuration *config_dmem);

int ia_css_configure_output1(const struct ia_css_binary *binary,
			     const struct ia_css_output1_configuration *config_dmem);

int ia_css_configure_output(const struct ia_css_binary *binary,
			    const struct ia_css_output_configuration *config_dmem);

int ia_css_configure_raw(const struct ia_css_binary *binary,
			 const struct ia_css_raw_configuration *config_dmem);

int ia_css_configure_tnr(const struct ia_css_binary *binary,
			 const struct ia_css_tnr_configuration *config_dmem);

int ia_css_configure_ref(const struct ia_css_binary *binary,
			 const struct ia_css_ref_configuration *config_dmem);

int ia_css_configure_vf(const struct ia_css_binary *binary,
			const struct ia_css_vf_configuration *config_dmem);

#endif /* IA_CSS_INCLUDE_CONFIGURATION */

#endif /* _IA_CSS_ISP_CONFIG_H */
