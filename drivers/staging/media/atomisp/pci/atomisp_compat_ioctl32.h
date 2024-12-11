/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 */
#ifndef __ATOMISP_COMPAT_IOCTL32_H__
#define __ATOMISP_COMPAT_IOCTL32_H__

#include <linux/compat.h>
#include <linux/videodev2.h>

#include "atomisp_compat.h"

struct atomisp_histogram32 {
	unsigned int num_elements;
	compat_uptr_t data;
};

struct atomisp_dvs2_stat_types32 {
	compat_uptr_t odd_real; /** real part of the odd statistics*/
	compat_uptr_t odd_imag; /** imaginary part of the odd statistics*/
	compat_uptr_t even_real;/** real part of the even statistics*/
	compat_uptr_t even_imag;/** imaginary part of the even statistics*/
};

struct atomisp_dvs2_coef_types32 {
	compat_uptr_t odd_real; /** real part of the odd coefficients*/
	compat_uptr_t odd_imag; /** imaginary part of the odd coefficients*/
	compat_uptr_t even_real;/** real part of the even coefficients*/
	compat_uptr_t even_imag;/** imaginary part of the even coefficients*/
};

struct atomisp_dvs2_statistics32 {
	struct atomisp_dvs_grid_info grid_info;
	struct atomisp_dvs2_stat_types32 hor_prod;
	struct atomisp_dvs2_stat_types32 ver_prod;
};

struct atomisp_dis_statistics32 {
	struct atomisp_dvs2_statistics32 dvs2_stat;
	u32 exp_id;
};

struct atomisp_dis_coefficients32 {
	struct atomisp_dvs_grid_info grid_info;
	struct atomisp_dvs2_coef_types32 hor_coefs;
	struct atomisp_dvs2_coef_types32 ver_coefs;
};

struct atomisp_3a_statistics32 {
	struct atomisp_grid_info  grid_info;
	compat_uptr_t data;
	compat_uptr_t rgby_data;
	u32 exp_id;
	u32 isp_config_id;
};

struct atomisp_morph_table32 {
	unsigned int enabled;
	unsigned int height;
	unsigned int width;	/* number of valid elements per line */
	compat_uptr_t coordinates_x[ATOMISP_MORPH_TABLE_NUM_PLANES];
	compat_uptr_t coordinates_y[ATOMISP_MORPH_TABLE_NUM_PLANES];
};

struct v4l2_framebuffer32 {
	__u32			capability;
	__u32			flags;
	compat_uptr_t		base;
	struct v4l2_pix_format	fmt;
};

struct atomisp_overlay32 {
	/* the frame containing the overlay data The overlay frame width should
	 * be the multiples of 2*ISP_VEC_NELEMS. The overlay frame height
	 * should be the multiples of 2.
	 */
	compat_uptr_t frame;
	/* Y value of overlay background */
	unsigned char bg_y;
	/* U value of overlay background */
	char bg_u;
	/* V value of overlay background */
	char bg_v;
	/* the blending percent of input data for Y subpixels */
	unsigned char blend_input_perc_y;
	/* the blending percent of input data for U subpixels */
	unsigned char blend_input_perc_u;
	/* the blending percent of input data for V subpixels */
	unsigned char blend_input_perc_v;
	/* the blending percent of overlay data for Y subpixels */
	unsigned char blend_overlay_perc_y;
	/* the blending percent of overlay data for U subpixels */
	unsigned char blend_overlay_perc_u;
	/* the blending percent of overlay data for V subpixels */
	unsigned char blend_overlay_perc_v;
	/* the overlay start x pixel position on output frame It should be the
	   multiples of 2*ISP_VEC_NELEMS. */
	unsigned int overlay_start_x;
	/* the overlay start y pixel position on output frame It should be the
	   multiples of 2. */
	unsigned int overlay_start_y;
};

struct atomisp_shading_table32 {
	__u32 enable;
	__u32 sensor_width;
	__u32 sensor_height;
	__u32 width;
	__u32 height;
	__u32 fraction_bits;

	compat_uptr_t data[ATOMISP_NUM_SC_COLORS];
};

struct atomisp_parameters32 {
	compat_uptr_t wb_config;  /* White Balance config */
	compat_uptr_t cc_config;  /* Color Correction config */
	compat_uptr_t tnr_config; /* Temporal Noise Reduction */
	compat_uptr_t ecd_config; /* Eigen Color Demosaicing */
	compat_uptr_t ynr_config; /* Y(Luma) Noise Reduction */
	compat_uptr_t fc_config;  /* Fringe Control */
	compat_uptr_t formats_config;  /* Formats Control */
	compat_uptr_t cnr_config; /* Chroma Noise Reduction */
	compat_uptr_t macc_config;  /* MACC */
	compat_uptr_t ctc_config; /* Chroma Tone Control */
	compat_uptr_t aa_config;  /* Anti-Aliasing */
	compat_uptr_t baa_config;  /* Anti-Aliasing */
	compat_uptr_t ce_config;
	compat_uptr_t dvs_6axis_config;
	compat_uptr_t ob_config;  /* Objective Black config */
	compat_uptr_t dp_config;  /* Dead Pixel config */
	compat_uptr_t nr_config;  /* Noise Reduction config */
	compat_uptr_t ee_config;  /* Edge Enhancement config */
	compat_uptr_t de_config;  /* Demosaic config */
	compat_uptr_t gc_config;  /* Gamma Correction config */
	compat_uptr_t anr_config; /* Advanced Noise Reduction */
	compat_uptr_t a3a_config; /* 3A Statistics config */
	compat_uptr_t xnr_config; /* eXtra Noise Reduction */
	compat_uptr_t dz_config;  /* Digital Zoom */
	compat_uptr_t yuv2rgb_cc_config; /* Color
							Correction config */
	compat_uptr_t rgb2yuv_cc_config; /* Color
							Correction config */
	compat_uptr_t macc_table;
	compat_uptr_t gamma_table;
	compat_uptr_t ctc_table;
	compat_uptr_t xnr_table;
	compat_uptr_t r_gamma_table;
	compat_uptr_t g_gamma_table;
	compat_uptr_t b_gamma_table;
	compat_uptr_t motion_vector; /* For 2-axis DVS */
	compat_uptr_t shading_table;
	compat_uptr_t morph_table;
	compat_uptr_t dvs_coefs; /* DVS 1.0 coefficients */
	compat_uptr_t dvs2_coefs; /* DVS 2.0 coefficients */
	compat_uptr_t capture_config;
	compat_uptr_t anr_thres;

	compat_uptr_t	lin_2500_config;       /* Skylake: Linearization config */
	compat_uptr_t	obgrid_2500_config;    /* Skylake: OBGRID config */
	compat_uptr_t	bnr_2500_config;       /* Skylake: bayer denoise config */
	compat_uptr_t	shd_2500_config;       /* Skylake: shading config */
	compat_uptr_t	dm_2500_config;        /* Skylake: demosaic config */
	compat_uptr_t	rgbpp_2500_config;     /* Skylake: RGBPP config */
	compat_uptr_t	dvs_stat_2500_config;  /* Skylake: DVS STAT config */
	compat_uptr_t	lace_stat_2500_config; /* Skylake: LACE STAT config */
	compat_uptr_t	yuvp1_2500_config;     /* Skylake: yuvp1 config */
	compat_uptr_t	yuvp2_2500_config;     /* Skylake: yuvp2 config */
	compat_uptr_t	tnr_2500_config;       /* Skylake: TNR config */
	compat_uptr_t	dpc_2500_config;       /* Skylake: DPC config */
	compat_uptr_t	awb_2500_config;       /* Skylake: auto white balance config */
	compat_uptr_t
	awb_fr_2500_config;    /* Skylake: auto white balance filter response config */
	compat_uptr_t	anr_2500_config;       /* Skylake: ANR config */
	compat_uptr_t	af_2500_config;        /* Skylake: auto focus config */
	compat_uptr_t	ae_2500_config;        /* Skylake: auto exposure config */
	compat_uptr_t	bds_2500_config;       /* Skylake: bayer downscaler config */
	compat_uptr_t
	dvs_2500_config;       /* Skylake: digital video stabilization config */
	compat_uptr_t	res_mgr_2500_config;

	/*
	 * Output frame pointer the config is to be applied to (optional),
	 * set to NULL to make this config is applied as global.
	 */
	compat_uptr_t	output_frame;
	/*
	 * Unique ID to track which config was actually applied to a particular
	 * frame, driver will send this id back with output frame together.
	 */
	u32	isp_config_id;
	u32	per_frame_setting;
};

struct atomisp_dvs_6axis_config32 {
	u32 exp_id;
	u32 width_y;
	u32 height_y;
	u32 width_uv;
	u32 height_uv;
	compat_uptr_t xcoords_y;
	compat_uptr_t ycoords_y;
	compat_uptr_t xcoords_uv;
	compat_uptr_t ycoords_uv;
};

#define ATOMISP_IOC_G_HISTOGRAM32 \
	_IOWR('v', BASE_VIDIOC_PRIVATE + 3, struct atomisp_histogram32)
#define ATOMISP_IOC_S_HISTOGRAM32 \
	_IOW('v', BASE_VIDIOC_PRIVATE + 3, struct atomisp_histogram32)

#define ATOMISP_IOC_G_DIS_STAT32 \
	_IOWR('v', BASE_VIDIOC_PRIVATE + 6, struct atomisp_dis_statistics32)
#define ATOMISP_IOC_S_DIS_COEFS32 \
	_IOW('v', BASE_VIDIOC_PRIVATE + 6, struct atomisp_dis_coefficients32)

#define ATOMISP_IOC_S_DIS_VECTOR32 \
	_IOW('v', BASE_VIDIOC_PRIVATE + 6, struct atomisp_dvs_6axis_config32)

#define ATOMISP_IOC_G_3A_STAT32 \
	_IOWR('v', BASE_VIDIOC_PRIVATE + 7, struct atomisp_3a_statistics32)

#define ATOMISP_IOC_G_ISP_GDC_TAB32 \
	_IOR('v', BASE_VIDIOC_PRIVATE + 10, struct atomisp_morph_table32)
#define ATOMISP_IOC_S_ISP_GDC_TAB32 \
	_IOW('v', BASE_VIDIOC_PRIVATE + 10, struct atomisp_morph_table32)

#define ATOMISP_IOC_S_ISP_FPN_TABLE32 \
	_IOW('v', BASE_VIDIOC_PRIVATE + 17, struct v4l2_framebuffer32)

#define ATOMISP_IOC_G_ISP_OVERLAY32 \
	_IOWR('v', BASE_VIDIOC_PRIVATE + 18, struct atomisp_overlay32)
#define ATOMISP_IOC_S_ISP_OVERLAY32 \
	_IOW('v', BASE_VIDIOC_PRIVATE + 18, struct atomisp_overlay32)

#define ATOMISP_IOC_S_ISP_SHD_TAB32 \
	_IOWR('v', BASE_VIDIOC_PRIVATE + 27, struct atomisp_shading_table32)

#define ATOMISP_IOC_S_PARAMETERS32 \
	_IOW('v', BASE_VIDIOC_PRIVATE + 32, struct atomisp_parameters32)

#endif /* __ATOMISP_COMPAT_IOCTL32_H__ */
