/*
 * Copyright (C) 2012 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Contributors:
 *      Manjunath Hadli <manjunath.hadli@ti.com>
 *      Prabhakar Lad <prabhakar.lad@ti.com>
 */

#ifndef _DAVINCI_VPFE_DM365_RESIZER_H
#define _DAVINCI_VPFE_DM365_RESIZER_H

enum resizer_oper_mode {
	RESIZER_MODE_CONTINIOUS = 0,
	RESIZER_MODE_ONE_SHOT = 1,
};

struct f_div_pass {
	unsigned int o_hsz;
	unsigned int i_hps;
	unsigned int h_phs;
	unsigned int src_hps;
	unsigned int src_hsz;
};

#define MAX_PASSES		2

struct f_div_param {
	unsigned char en;
	unsigned int num_passes;
	struct f_div_pass pass[MAX_PASSES];
};

/* Resizer Rescale Parameters*/
struct resizer_scale_param {
	bool h_flip;
	bool v_flip;
	bool cen;
	bool yen;
	unsigned short i_vps;
	unsigned short i_hps;
	unsigned short o_vsz;
	unsigned short o_hsz;
	unsigned short v_phs_y;
	unsigned short v_phs_c;
	unsigned short v_dif;
	/* resize method - Luminance */
	enum vpfe_rsz_intp_t v_typ_y;
	/* resize method - Chrominance */
	enum vpfe_rsz_intp_t v_typ_c;
	/* vertical lpf intensity - Luminance */
	unsigned char v_lpf_int_y;
	/* vertical lpf intensity - Chrominance */
	unsigned char v_lpf_int_c;
	unsigned short h_phs;
	unsigned short h_dif;
	/* resize method - Luminance */
	enum vpfe_rsz_intp_t h_typ_y;
	/* resize method - Chrominance */
	enum vpfe_rsz_intp_t h_typ_c;
	/* horizontal lpf intensity - Luminance */
	unsigned char h_lpf_int_y;
	/* horizontal lpf intensity - Chrominance */
	unsigned char h_lpf_int_c;
	bool dscale_en;
	enum vpfe_rsz_down_scale_ave_sz h_dscale_ave_sz;
	enum vpfe_rsz_down_scale_ave_sz v_dscale_ave_sz;
	/* store the calculated frame division parameter */
	struct f_div_param f_div;
};

enum resizer_rgb_t {
	OUTPUT_32BIT,
	OUTPUT_16BIT
};

enum resizer_rgb_msk_t {
	NOMASK = 0,
	MASKLAST2 = 1,
};

/* Resizer RGB Conversion Parameters */
struct resizer_rgb {
	bool rgb_en;
	enum resizer_rgb_t rgb_typ;
	enum resizer_rgb_msk_t rgb_msk0;
	enum resizer_rgb_msk_t rgb_msk1;
	unsigned int rgb_alpha_val;
};

/* Resizer External Memory Parameters */
struct rsz_ext_mem_param {
	unsigned int rsz_sdr_oft_y;
	unsigned int rsz_sdr_ptr_s_y;
	unsigned int rsz_sdr_ptr_e_y;
	unsigned int rsz_sdr_oft_c;
	unsigned int rsz_sdr_ptr_s_c;
	unsigned int rsz_sdr_ptr_e_c;
	/* offset to be added to buffer start when flipping for y/ycbcr */
	unsigned int flip_ofst_y;
	/* offset to be added to buffer start when flipping for c */
	unsigned int flip_ofst_c;
	/* c offset for YUV 420SP */
	unsigned int c_offset;
	/* User Defined Y offset for YUV 420SP or YUV420ILE data */
	unsigned int user_y_ofst;
	/* User Defined C offset for YUV 420SP data */
	unsigned int user_c_ofst;
};

enum rsz_data_source {
	IPIPE_DATA,
	IPIPEIF_DATA
};

enum rsz_src_img_fmt {
	RSZ_IMG_422,
	RSZ_IMG_420
};

enum rsz_dpaths_bypass_t {
	BYPASS_OFF = 0,
	BYPASS_ON = 1,
};

struct rsz_common_params {
	unsigned int vps;
	unsigned int vsz;
	unsigned int hps;
	unsigned int hsz;
	/* 420 or 422 */
	enum rsz_src_img_fmt src_img_fmt;
	/* Y or C when src_fmt is 420, 0 - y, 1 - c */
	unsigned char y_c;
	/* flip raw or ycbcr */
	unsigned char raw_flip;
	/* IPIPE or IPIPEIF data */
	enum rsz_data_source source;
	enum rsz_dpaths_bypass_t passthrough;
	unsigned char yuv_y_min;
	unsigned char yuv_y_max;
	unsigned char yuv_c_min;
	unsigned char yuv_c_max;
	bool rsz_seq_crv;
	enum vpfe_chr_pos out_chr_pos;
};

struct resizer_params {
	enum resizer_oper_mode oper_mode;
	struct rsz_common_params rsz_common;
	struct resizer_scale_param rsz_rsc_param[2];
	struct resizer_rgb rsz2rgb[2];
	struct rsz_ext_mem_param ext_mem_param[2];
	bool rsz_en[2];
	struct vpfe_rsz_config_params user_config;
};

#define ENABLE			1
#define DISABLE			(!ENABLE)

#define RESIZER_CROP_PAD_SINK		0
#define RESIZER_CROP_PAD_SOURCE		1
#define RESIZER_CROP_PAD_SOURCE2	2

#define RESIZER_CROP_PADS_NUM		3

enum resizer_crop_input_entity {
	RESIZER_CROP_INPUT_NONE = 0,
	RESIZER_CROP_INPUT_IPIPEIF = 1,
	RESIZER_CROP_INPUT_IPIPE = 2,
};

enum resizer_crop_output_entity {
	RESIZER_CROP_OUTPUT_NONE,
	RESIZER_A,
	RESIZER_B,
};

struct dm365_crop_resizer_device {
	struct v4l2_subdev			subdev;
	struct media_pad			pads[RESIZER_CROP_PADS_NUM];
	struct v4l2_mbus_framefmt		formats[RESIZER_CROP_PADS_NUM];
	enum resizer_crop_input_entity		input;
	enum resizer_crop_output_entity		output;
	enum resizer_crop_output_entity		output2;
	struct vpfe_resizer_device		*rsz_device;
};

#define RESIZER_PAD_SINK		0
#define RESIZER_PAD_SOURCE		1

#define RESIZER_PADS_NUM		2

enum resizer_input_entity {
	RESIZER_INPUT_NONE = 0,
	RESIZER_INPUT_CROP_RESIZER = 1,
};

enum resizer_output_entity {
	RESIZER_OUTPUT_NONE = 0,
	RESIZER_OUPUT_MEMORY = 1,
};

struct dm365_resizer_device {
	struct v4l2_subdev		subdev;
	struct media_pad		pads[RESIZER_PADS_NUM];
	struct v4l2_mbus_framefmt	formats[RESIZER_PADS_NUM];
	enum resizer_input_entity	input;
	enum resizer_output_entity	output;
	struct vpfe_video_device	video_out;
	struct vpfe_resizer_device	*rsz_device;
};

struct vpfe_resizer_device {
	struct dm365_crop_resizer_device	crop_resizer;
	struct dm365_resizer_device		resizer_a;
	struct dm365_resizer_device		resizer_b;
	struct resizer_params			config;
	void *__iomem base_addr;
};

int vpfe_resizer_init(struct vpfe_resizer_device *vpfe_rsz,
		      struct platform_device *pdev);
int vpfe_resizer_register_entities(struct vpfe_resizer_device *vpfe_rsz,
				   struct v4l2_device *v4l2_dev);
void vpfe_resizer_unregister_entities(struct vpfe_resizer_device *vpfe_rsz);
void vpfe_resizer_cleanup(struct vpfe_resizer_device *vpfe_rsz,
			  struct platform_device *pdev);
void vpfe_resizer_buffer_isr(struct vpfe_resizer_device *resizer);
void vpfe_resizer_dma_isr(struct vpfe_resizer_device *resizer);

#endif		/* _DAVINCI_VPFE_DM365_RESIZER_H */
