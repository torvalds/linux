/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_PIPE_BINARYDESC_H__
#define __IA_CSS_PIPE_BINARYDESC_H__

#include <linux/math.h>

#include <ia_css_types.h>		/* ia_css_pipe */
#include <ia_css_frame_public.h>	/* ia_css_frame_info */
#include <ia_css_binary.h>		/* ia_css_binary_descr */

/* @brief Get a binary descriptor for copy.
 *
 * @param[in] pipe
 * @param[out] copy_desc
 * @param[in/out] in_info
 * @param[in/out] out_info
 * @param[in/out] vf_info
 * @return    None
 *
 */
void ia_css_pipe_get_copy_binarydesc(
    struct ia_css_pipe const *const pipe,
    struct ia_css_binary_descr *copy_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *out_info,
    struct ia_css_frame_info *vf_info);

/* @brief Get a binary descriptor for vfpp.
 *
 * @param[in] pipe
 * @param[out] vfpp_descr
 * @param[in/out] in_info
 * @param[in/out] out_info
 * @return    None
 *
 */
void ia_css_pipe_get_vfpp_binarydesc(
    struct ia_css_pipe const *const pipe,
    struct ia_css_binary_descr *vf_pp_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *out_info);

/* @brief Get numerator and denominator of bayer downscaling factor.
 *
 * @param[in] bds_factor: The bayer downscaling factor.
 *		(= The bds_factor member in the sh_css_bds_factor structure.)
 * @param[out] bds: The rational fraction of the bayer downscaling factor.
 *		(= The respective member in the sh_css_bds_factor structure.)
 * @return	0 or error code upon error.
 *
 */
int sh_css_bds_factor_get_fract(unsigned int bds_factor, struct u32_fract *bds);

/* @brief Get a binary descriptor for preview stage.
 *
 * @param[in] pipe
 * @param[out] preview_descr
 * @param[in/out] in_info
 * @param[in/out] bds_out_info
 * @param[in/out] out_info
 * @param[in/out] vf_info
 * @return	0 or error code upon error.
 *
 */
int ia_css_pipe_get_preview_binarydesc(
    struct ia_css_pipe *const pipe,
    struct ia_css_binary_descr *preview_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *bds_out_info,
    struct ia_css_frame_info *out_info,
    struct ia_css_frame_info *vf_info);

/* @brief Get a binary descriptor for video stage.
 *
 * @param[in/out] pipe
 * @param[out] video_descr
 * @param[in/out] in_info
 * @param[in/out] bds_out_info
 * @param[in/out] vf_info
 * @return	0 or error code upon error.
 *
 */
int ia_css_pipe_get_video_binarydesc(
    struct ia_css_pipe *const pipe,
    struct ia_css_binary_descr *video_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *bds_out_info,
    struct ia_css_frame_info *out_info,
    struct ia_css_frame_info *vf_info,
    int stream_config_left_padding);

/* @brief Get a binary descriptor for yuv scaler stage.
 *
 * @param[in/out] pipe
 * @param[out] yuv_scaler_descr
 * @param[in/out] in_info
 * @param[in/out] out_info
 * @param[in/out] internal_out_info
 * @param[in/out] vf_info
 * @return    None
 *
 */
void ia_css_pipe_get_yuvscaler_binarydesc(
    struct ia_css_pipe const *const pipe,
    struct ia_css_binary_descr *yuv_scaler_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *out_info,
    struct ia_css_frame_info *internal_out_info,
    struct ia_css_frame_info *vf_info);

/* @brief Get a binary descriptor for capture pp stage.
 *
 * @param[in/out] pipe
 * @param[out] capture_pp_descr
 * @param[in/out] in_info
 * @param[in/out] vf_info
 * @return    None
 *
 */
void ia_css_pipe_get_capturepp_binarydesc(
    struct ia_css_pipe *const pipe,
    struct ia_css_binary_descr *capture_pp_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *out_info,
    struct ia_css_frame_info *vf_info);

/* @brief Get a binary descriptor for primary capture.
 *
 * @param[in] pipe
 * @param[out] prim_descr
 * @param[in/out] in_info
 * @param[in/out] out_info
 * @param[in/out] vf_info
 * @return    None
 *
 */
void ia_css_pipe_get_primary_binarydesc(
    struct ia_css_pipe const *const pipe,
    struct ia_css_binary_descr *prim_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *out_info,
    struct ia_css_frame_info *vf_info,
    unsigned int stage_idx);

/* @brief Get a binary descriptor for pre gdc stage.
 *
 * @param[in] pipe
 * @param[out] pre_gdc_descr
 * @param[in/out] in_info
 * @param[in/out] out_info
 * @return    None
 *
 */
void ia_css_pipe_get_pre_gdc_binarydesc(
    struct ia_css_pipe const *const pipe,
    struct ia_css_binary_descr *gdc_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *out_info);

/* @brief Get a binary descriptor for gdc stage.
 *
 * @param[in] pipe
 * @param[out] gdc_descr
 * @param[in/out] in_info
 * @param[in/out] out_info
 * @return    None
 *
 */
void ia_css_pipe_get_gdc_binarydesc(
    struct ia_css_pipe const *const pipe,
    struct ia_css_binary_descr *gdc_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *out_info);

/* @brief Get a binary descriptor for post gdc.
 *
 * @param[in] pipe
 * @param[out] post_gdc_descr
 * @param[in/out] in_info
 * @param[in/out] out_info
 * @param[in/out] vf_info
 * @return    None
 *
 */
void ia_css_pipe_get_post_gdc_binarydesc(
    struct ia_css_pipe const *const pipe,
    struct ia_css_binary_descr *post_gdc_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *out_info,
    struct ia_css_frame_info *vf_info);

/* @brief Get a binary descriptor for de.
 *
 * @param[in] pipe
 * @param[out] pre_de_descr
 * @param[in/out] in_info
 * @param[in/out] out_info
 * @return    None
 *
 */
void ia_css_pipe_get_pre_de_binarydesc(
    struct ia_css_pipe const *const pipe,
    struct ia_css_binary_descr *pre_de_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *out_info);

/* @brief Get a binary descriptor for pre anr stage.
 *
 * @param[in] pipe
 * @param[out] pre_anr_descr
 * @param[in/out] in_info
 * @param[in/out] out_info
 * @return    None
 *
 */
void ia_css_pipe_get_pre_anr_binarydesc(
    struct ia_css_pipe const *const pipe,
    struct ia_css_binary_descr *pre_anr_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *out_info);

/* @brief Get a binary descriptor for ANR stage.
 *
 * @param[in] pipe
 * @param[out] anr_descr
 * @param[in/out] in_info
 * @param[in/out] out_info
 * @return    None
 *
 */
void ia_css_pipe_get_anr_binarydesc(
    struct ia_css_pipe const *const pipe,
    struct ia_css_binary_descr *anr_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *out_info);

/* @brief Get a binary descriptor for post anr stage.
 *
 * @param[in] pipe
 * @param[out] post_anr_descr
 * @param[in/out] in_info
 * @param[in/out] out_info
 * @param[in/out] vf_info
 * @return    None
 *
 */
void ia_css_pipe_get_post_anr_binarydesc(
    struct ia_css_pipe const *const pipe,
    struct ia_css_binary_descr *post_anr_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *out_info,
    struct ia_css_frame_info *vf_info);

/* @brief Get a binary descriptor for ldc stage.
 *
 * @param[in/out] pipe
 * @param[out] capture_pp_descr
 * @param[in/out] in_info
 * @param[in/out] vf_info
 * @return    None
 *
 */
void ia_css_pipe_get_ldc_binarydesc(
    struct ia_css_pipe const *const pipe,
    struct ia_css_binary_descr *ldc_descr,
    struct ia_css_frame_info *in_info,
    struct ia_css_frame_info *out_info);

/* @brief Calculates the required BDS factor
 *
 * @param[in] input_res
 * @param[in] output_res
 * @param[in/out] bds_factor
 * @return	0 or error code upon error.
 */
int binarydesc_calculate_bds_factor(
    struct ia_css_resolution input_res,
    struct ia_css_resolution output_res,
    unsigned int *bds_factor);

#endif /* __IA_CSS_PIPE_BINARYDESC_H__ */
