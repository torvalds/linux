/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Intel Corporation */

#ifndef __IPU3_PARAMS_H
#define __IPU3_PARAMS_H

int ipu3_css_cfg_acc(struct ipu3_css *css, unsigned int pipe,
		     struct ipu3_uapi_flags *use,
		     struct imgu_abi_acc_param *acc,
		     struct imgu_abi_acc_param *acc_old,
		     struct ipu3_uapi_acc_param *acc_user);

int ipu3_css_cfg_vmem0(struct ipu3_css *css, unsigned int pipe,
		       struct ipu3_uapi_flags *use,
		       void *vmem0, void *vmem0_old,
		       struct ipu3_uapi_params *user);

int ipu3_css_cfg_dmem0(struct ipu3_css *css, unsigned int pipe,
		       struct ipu3_uapi_flags *use,
		       void *dmem0, void *dmem0_old,
		       struct ipu3_uapi_params *user);

void ipu3_css_cfg_gdc_table(struct imgu_abi_gdc_warp_param *gdc,
			    int frame_in_x, int frame_in_y,
			    int frame_out_x, int frame_out_y,
			    int env_w, int env_h);

#endif /*__IPU3_PARAMS_H */
