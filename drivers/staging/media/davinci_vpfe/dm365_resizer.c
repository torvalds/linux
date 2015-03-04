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
 *
 *
 * Resizer allows upscaling or downscaling a image to a desired
 * resolution. There are 2 resizer modules. both operating on the
 * same input image, but can have different output resolution.
 */

#include "dm365_ipipe_hw.h"
#include "dm365_resizer.h"

#define MIN_IN_WIDTH		32
#define MIN_IN_HEIGHT		32
#define MAX_IN_WIDTH		4095
#define MAX_IN_HEIGHT		4095
#define MIN_OUT_WIDTH		16
#define MIN_OUT_HEIGHT		2

static const unsigned int resizer_input_formats[] = {
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_Y8_1X8,
	MEDIA_BUS_FMT_UV8_1X8,
	MEDIA_BUS_FMT_SGRBG12_1X12,
};

static const unsigned int resizer_output_formats[] = {
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_Y8_1X8,
	MEDIA_BUS_FMT_UV8_1X8,
	MEDIA_BUS_FMT_YDYUYDYV8_1X16,
	MEDIA_BUS_FMT_SGRBG12_1X12,
};

/* resizer_calculate_line_length() - This function calculates the line length of
 *				     various image planes at the input and
 *				     output.
 */
static void
resizer_calculate_line_length(u32 pix, int width, int height,
			      int *line_len, int *line_len_c)
{
	*line_len = 0;
	*line_len_c = 0;

	if (pix == MEDIA_BUS_FMT_UYVY8_2X8 ||
	    pix == MEDIA_BUS_FMT_SGRBG12_1X12) {
		*line_len = width << 1;
	} else if (pix == MEDIA_BUS_FMT_Y8_1X8 ||
		   pix == MEDIA_BUS_FMT_UV8_1X8) {
		*line_len = width;
		*line_len_c = width;
	} else {
		/* YUV 420 */
		/* round width to upper 32 byte boundary */
		*line_len = width;
		*line_len_c = width;
	}
	/* adjust the line len to be a multiple of 32 */
	*line_len += 31;
	*line_len &= ~0x1f;
	*line_len_c += 31;
	*line_len_c &= ~0x1f;
}

static inline int
resizer_validate_output_image_format(struct device *dev,
				     struct v4l2_mbus_framefmt *format,
				     int *in_line_len, int *in_line_len_c)
{
	if (format->code != MEDIA_BUS_FMT_UYVY8_2X8 &&
	    format->code != MEDIA_BUS_FMT_Y8_1X8 &&
	    format->code != MEDIA_BUS_FMT_UV8_1X8 &&
	    format->code != MEDIA_BUS_FMT_YDYUYDYV8_1X16 &&
	    format->code != MEDIA_BUS_FMT_SGRBG12_1X12) {
		dev_err(dev, "Invalid Mbus format, %d\n", format->code);
		return -EINVAL;
	}
	if (!format->width || !format->height) {
		dev_err(dev, "invalid width or height\n");
		return -EINVAL;
	}
	resizer_calculate_line_length(format->code, format->width,
		format->height, in_line_len, in_line_len_c);
	return 0;
}

static void
resizer_configure_passthru(struct vpfe_resizer_device *resizer, int bypass)
{
	struct resizer_params *param = &resizer->config;

	param->rsz_rsc_param[RSZ_A].cen = DISABLE;
	param->rsz_rsc_param[RSZ_A].yen = DISABLE;
	param->rsz_rsc_param[RSZ_A].v_phs_y = 0;
	param->rsz_rsc_param[RSZ_A].v_phs_c = 0;
	param->rsz_rsc_param[RSZ_A].v_dif = 256;
	param->rsz_rsc_param[RSZ_A].v_lpf_int_y = 0;
	param->rsz_rsc_param[RSZ_A].v_lpf_int_c = 0;
	param->rsz_rsc_param[RSZ_A].h_phs = 0;
	param->rsz_rsc_param[RSZ_A].h_dif = 256;
	param->rsz_rsc_param[RSZ_A].h_lpf_int_y = 0;
	param->rsz_rsc_param[RSZ_A].h_lpf_int_c = 0;
	param->rsz_rsc_param[RSZ_A].dscale_en = DISABLE;
	param->rsz2rgb[RSZ_A].rgb_en = DISABLE;
	param->rsz_en[RSZ_A] = ENABLE;
	param->rsz_en[RSZ_B] = DISABLE;
	if (bypass) {
		param->rsz_rsc_param[RSZ_A].i_vps = 0;
		param->rsz_rsc_param[RSZ_A].i_hps = 0;
		/* Raw Bypass */
		param->rsz_common.passthrough = BYPASS_ON;
	}
}

static void
configure_resizer_out_params(struct vpfe_resizer_device *resizer, int index,
			     void *output_spec, unsigned char partial,
			     unsigned flag)
{
	struct resizer_params *param = &resizer->config;
	struct v4l2_mbus_framefmt *outformat;
	struct vpfe_rsz_output_spec *output;

	if (index == RSZ_A &&
	    resizer->resizer_a.output == RESIZER_OUTPUT_NONE) {
		param->rsz_en[index] = DISABLE;
		return;
	}
	if (index == RSZ_B &&
	    resizer->resizer_b.output == RESIZER_OUTPUT_NONE) {
		param->rsz_en[index] = DISABLE;
		return;
	}
	output = output_spec;
	param->rsz_en[index] = ENABLE;
	if (partial) {
		param->rsz_rsc_param[index].h_flip = output->h_flip;
		param->rsz_rsc_param[index].v_flip = output->v_flip;
		param->rsz_rsc_param[index].v_typ_y = output->v_typ_y;
		param->rsz_rsc_param[index].v_typ_c = output->v_typ_c;
		param->rsz_rsc_param[index].v_lpf_int_y =
						output->v_lpf_int_y;
		param->rsz_rsc_param[index].v_lpf_int_c =
						output->v_lpf_int_c;
		param->rsz_rsc_param[index].h_typ_y = output->h_typ_y;
		param->rsz_rsc_param[index].h_typ_c = output->h_typ_c;
		param->rsz_rsc_param[index].h_lpf_int_y =
						output->h_lpf_int_y;
		param->rsz_rsc_param[index].h_lpf_int_c =
						output->h_lpf_int_c;
		param->rsz_rsc_param[index].dscale_en =
						output->en_down_scale;
		param->rsz_rsc_param[index].h_dscale_ave_sz =
						output->h_dscale_ave_sz;
		param->rsz_rsc_param[index].v_dscale_ave_sz =
						output->v_dscale_ave_sz;
		param->ext_mem_param[index].user_y_ofst =
				    (output->user_y_ofst + 31) & ~0x1f;
		param->ext_mem_param[index].user_c_ofst =
				    (output->user_c_ofst + 31) & ~0x1f;
		return;
	}

	if (index == RSZ_A)
		outformat = &resizer->resizer_a.formats[RESIZER_PAD_SOURCE];
	else
		outformat = &resizer->resizer_b.formats[RESIZER_PAD_SOURCE];
	param->rsz_rsc_param[index].o_vsz = outformat->height - 1;
	param->rsz_rsc_param[index].o_hsz = outformat->width - 1;
	param->ext_mem_param[index].rsz_sdr_ptr_s_y = output->vst_y;
	param->ext_mem_param[index].rsz_sdr_ptr_e_y = outformat->height;
	param->ext_mem_param[index].rsz_sdr_ptr_s_c = output->vst_c;
	param->ext_mem_param[index].rsz_sdr_ptr_e_c = outformat->height;

	if (!flag)
		return;
	/* update common parameters */
	param->rsz_rsc_param[index].h_flip = output->h_flip;
	param->rsz_rsc_param[index].v_flip = output->v_flip;
	param->rsz_rsc_param[index].v_typ_y = output->v_typ_y;
	param->rsz_rsc_param[index].v_typ_c = output->v_typ_c;
	param->rsz_rsc_param[index].v_lpf_int_y = output->v_lpf_int_y;
	param->rsz_rsc_param[index].v_lpf_int_c = output->v_lpf_int_c;
	param->rsz_rsc_param[index].h_typ_y = output->h_typ_y;
	param->rsz_rsc_param[index].h_typ_c = output->h_typ_c;
	param->rsz_rsc_param[index].h_lpf_int_y = output->h_lpf_int_y;
	param->rsz_rsc_param[index].h_lpf_int_c = output->h_lpf_int_c;
	param->rsz_rsc_param[index].dscale_en = output->en_down_scale;
	param->rsz_rsc_param[index].h_dscale_ave_sz = output->h_dscale_ave_sz;
	param->rsz_rsc_param[index].v_dscale_ave_sz = output->h_dscale_ave_sz;
	param->ext_mem_param[index].user_y_ofst =
					(output->user_y_ofst + 31) & ~0x1f;
	param->ext_mem_param[index].user_c_ofst =
					(output->user_c_ofst + 31) & ~0x1f;
}

/*
 * resizer_calculate_resize_ratios() - Calculates resize ratio for resizer
 *				      A or B. This is called after setting
 *				     the input size or output size.
 * @resizer: Pointer to VPFE resizer subdevice.
 * @index: index RSZ_A-resizer-A RSZ_B-resizer-B.
 */
static void
resizer_calculate_resize_ratios(struct vpfe_resizer_device *resizer, int index)
{
	struct resizer_params *param = &resizer->config;
	struct v4l2_mbus_framefmt *informat, *outformat;

	informat = &resizer->crop_resizer.formats[RESIZER_CROP_PAD_SINK];

	if (index == RSZ_A)
		outformat = &resizer->resizer_a.formats[RESIZER_PAD_SOURCE];
	else
		outformat = &resizer->resizer_b.formats[RESIZER_PAD_SOURCE];

	if (outformat->field != V4L2_FIELD_INTERLACED)
		param->rsz_rsc_param[index].v_dif =
			((informat->height) * 256) / (outformat->height);
	else
		param->rsz_rsc_param[index].v_dif =
			((informat->height >> 1) * 256) / (outformat->height);
	param->rsz_rsc_param[index].h_dif =
			((informat->width) * 256) / (outformat->width);
}

void
static resizer_enable_422_420_conversion(struct resizer_params *param,
					 int index, bool en)
{
	param->rsz_rsc_param[index].cen = en;
	param->rsz_rsc_param[index].yen = en;
}

/* resizer_calculate_sdram_offsets() - This function calculates the offsets from
 *				       start of buffer for the C plane when
 *				       output format is YUV420SP. It also
 *				       calculates the offsets from the start of
 *				       the buffer when the image is flipped
 *				       vertically or horizontally for ycbcr/y/c
 *				       planes.
 * @resizer: Pointer to resizer subdevice.
 * @index: index RSZ_A-resizer-A RSZ_B-resizer-B.
 */
static int
resizer_calculate_sdram_offsets(struct vpfe_resizer_device *resizer, int index)
{
	struct resizer_params *param = &resizer->config;
	struct v4l2_mbus_framefmt *outformat;
	int bytesperpixel = 2;
	int image_height;
	int image_width;
	int yuv_420 = 0;
	int offset = 0;

	if (index == RSZ_A)
		outformat = &resizer->resizer_a.formats[RESIZER_PAD_SOURCE];
	else
		outformat = &resizer->resizer_b.formats[RESIZER_PAD_SOURCE];

	image_height = outformat->height + 1;
	image_width = outformat->width + 1;
	param->ext_mem_param[index].c_offset = 0;
	param->ext_mem_param[index].flip_ofst_y = 0;
	param->ext_mem_param[index].flip_ofst_c = 0;
	if (outformat->code == MEDIA_BUS_FMT_YDYUYDYV8_1X16) {
		/* YUV 420 */
		yuv_420 = 1;
		bytesperpixel = 1;
	}

	if (param->rsz_rsc_param[index].h_flip)
		/* width * bytesperpixel - 1 */
		offset = (image_width * bytesperpixel) - 1;
	if (param->rsz_rsc_param[index].v_flip)
		offset += (image_height - 1) *
			param->ext_mem_param[index].rsz_sdr_oft_y;
	param->ext_mem_param[index].flip_ofst_y = offset;
	if (!yuv_420)
		return 0;
	offset = 0;
	/* half height for c-plane */
	if (param->rsz_rsc_param[index].h_flip)
		/* width * bytesperpixel - 1 */
		offset = image_width - 1;
	if (param->rsz_rsc_param[index].v_flip)
		offset += (((image_height >> 1) - 1) *
			   param->ext_mem_param[index].rsz_sdr_oft_c);
	param->ext_mem_param[index].flip_ofst_c = offset;
	param->ext_mem_param[index].c_offset =
		      param->ext_mem_param[index].rsz_sdr_oft_y * image_height;
	return 0;
}

static int resizer_configure_output_win(struct vpfe_resizer_device *resizer)
{
	struct resizer_params *param = &resizer->config;
	struct vpfe_rsz_output_spec output_specs;
	struct v4l2_mbus_framefmt *outformat;
	int line_len_c;
	int line_len;
	int ret;

	outformat = &resizer->resizer_a.formats[RESIZER_PAD_SOURCE];

	output_specs.vst_y = param->user_config.vst;
	if (outformat->code == MEDIA_BUS_FMT_YDYUYDYV8_1X16)
		output_specs.vst_c = param->user_config.vst;

	configure_resizer_out_params(resizer, RSZ_A, &output_specs, 0, 0);
	resizer_calculate_line_length(outformat->code,
				      param->rsz_rsc_param[0].o_hsz + 1,
				      param->rsz_rsc_param[0].o_vsz + 1,
				      &line_len, &line_len_c);
	param->ext_mem_param[0].rsz_sdr_oft_y = line_len;
	param->ext_mem_param[0].rsz_sdr_oft_c = line_len_c;
	resizer_calculate_resize_ratios(resizer, RSZ_A);
	if (param->rsz_en[RSZ_B])
		resizer_calculate_resize_ratios(resizer, RSZ_B);

	if (outformat->code == MEDIA_BUS_FMT_YDYUYDYV8_1X16)
		resizer_enable_422_420_conversion(param, RSZ_A, ENABLE);
	else
		resizer_enable_422_420_conversion(param, RSZ_A, DISABLE);

	ret = resizer_calculate_sdram_offsets(resizer, RSZ_A);
	if (!ret && param->rsz_en[RSZ_B])
		ret = resizer_calculate_sdram_offsets(resizer, RSZ_B);

	if (ret)
		pr_err("Error in calculating sdram offsets\n");
	return ret;
}

static int
resizer_calculate_down_scale_f_div_param(struct device *dev,
					 int input_width, int output_width,
					 struct resizer_scale_param *param)
{
	/* rsz = R, input_width = H, output width = h in the equation */
	unsigned int two_power;
	unsigned int upper_h1;
	unsigned int upper_h2;
	unsigned int val1;
	unsigned int val;
	unsigned int rsz;
	unsigned int h1;
	unsigned int h2;
	unsigned int o;
	unsigned int n;

	upper_h1 = input_width >> 1;
	n = param->h_dscale_ave_sz;
	/* 2 ^ (scale+1) */
	two_power = 1 << (n + 1);
	upper_h1 = (upper_h1 >> (n + 1)) << (n + 1);
	upper_h2 = input_width - upper_h1;
	if (upper_h2 % two_power) {
		dev_err(dev, "frame halves to be a multiple of 2 power n+1\n");
		return -EINVAL;
	}
	two_power = 1 << n;
	rsz = (input_width << 8) / output_width;
	val = rsz * two_power;
	val = ((upper_h1 << 8) / val) + 1;
	if (!(val % 2)) {
		h1 = val;
	} else {
		val = upper_h1 << 8;
		val >>= n + 1;
		val -= rsz >> 1;
		val /= rsz << 1;
		val <<= 1;
		val += 2;
		h1 = val;
	}
	o = 10 + (two_power << 2);
	if (((input_width << 7) / rsz) % 2)
		o += (((CEIL(rsz, 1024)) << 1) << n);
	h2 = output_width - h1;
	/* phi */
	val = (h1 * rsz) - (((upper_h1 - (o - 10)) / two_power) << 8);
	/* skip */
	val1 = ((val - 1024) >> 9) << 1;
	param->f_div.num_passes = MAX_PASSES;
	param->f_div.pass[0].o_hsz = h1 - 1;
	param->f_div.pass[0].i_hps = 0;
	param->f_div.pass[0].h_phs = 0;
	param->f_div.pass[0].src_hps = 0;
	param->f_div.pass[0].src_hsz = upper_h1 + o;
	param->f_div.pass[1].o_hsz = h2 - 1;
	param->f_div.pass[1].i_hps = 10 + (val1 * two_power);
	param->f_div.pass[1].h_phs = (val - (val1 << 8));
	param->f_div.pass[1].src_hps = upper_h1 - o;
	param->f_div.pass[1].src_hsz = upper_h2 + o;

	return 0;
}

static int
resizer_configure_common_in_params(struct vpfe_resizer_device *resizer)
{
	struct vpfe_device *vpfe_dev = to_vpfe_device(resizer);
	struct resizer_params *param = &resizer->config;
	struct vpfe_rsz_config_params *user_config;
	struct v4l2_mbus_framefmt *informat;

	informat = &resizer->crop_resizer.formats[RESIZER_CROP_PAD_SINK];
	user_config = &resizer->config.user_config;
	param->rsz_common.vps = param->user_config.vst;
	param->rsz_common.hps = param->user_config.hst;

	if (vpfe_ipipeif_decimation_enabled(vpfe_dev))
		param->rsz_common.hsz = (((informat->width - 1) *
			IPIPEIF_RSZ_CONST) / vpfe_ipipeif_get_rsz(vpfe_dev));
	else
		param->rsz_common.hsz = informat->width - 1;

	if (informat->field == V4L2_FIELD_INTERLACED)
		param->rsz_common.vsz  = (informat->height - 1) >> 1;
	else
		param->rsz_common.vsz  = informat->height - 1;

	param->rsz_common.raw_flip = 0;

	if (resizer->crop_resizer.input == RESIZER_CROP_INPUT_IPIPEIF)
		param->rsz_common.source = IPIPEIF_DATA;
	else
		param->rsz_common.source = IPIPE_DATA;

	switch (informat->code) {
	case MEDIA_BUS_FMT_UYVY8_2X8:
		param->rsz_common.src_img_fmt = RSZ_IMG_422;
		param->rsz_common.raw_flip = 0;
		break;

	case MEDIA_BUS_FMT_Y8_1X8:
		param->rsz_common.src_img_fmt = RSZ_IMG_420;
		/* Select y */
		param->rsz_common.y_c = 0;
		param->rsz_common.raw_flip = 0;
		break;

	case MEDIA_BUS_FMT_UV8_1X8:
		param->rsz_common.src_img_fmt = RSZ_IMG_420;
		/* Select y */
		param->rsz_common.y_c = 1;
		param->rsz_common.raw_flip = 0;
		break;

	case MEDIA_BUS_FMT_SGRBG12_1X12:
		param->rsz_common.raw_flip = 1;
		break;

	default:
		param->rsz_common.src_img_fmt = RSZ_IMG_422;
		param->rsz_common.source = IPIPE_DATA;
	}

	param->rsz_common.yuv_y_min = user_config->yuv_y_min;
	param->rsz_common.yuv_y_max = user_config->yuv_y_max;
	param->rsz_common.yuv_c_min = user_config->yuv_c_min;
	param->rsz_common.yuv_c_max = user_config->yuv_c_max;
	param->rsz_common.out_chr_pos = user_config->out_chr_pos;
	param->rsz_common.rsz_seq_crv = user_config->chroma_sample_even;

	return 0;
}
static int
resizer_configure_in_continious_mode(struct vpfe_resizer_device *resizer)
{
	struct device *dev = resizer->crop_resizer.subdev.v4l2_dev->dev;
	struct resizer_params *param = &resizer->config;
	struct vpfe_rsz_config_params *cont_config;
	int line_len_c;
	int line_len;
	int ret;

	if (resizer->resizer_a.output != RESIZER_OUPUT_MEMORY) {
		dev_err(dev, "enable resizer - Resizer-A\n");
		return -EINVAL;
	}

	cont_config = &resizer->config.user_config;
	param->rsz_en[RSZ_A] = ENABLE;
	configure_resizer_out_params(resizer, RSZ_A,
				     &cont_config->output1, 1, 0);
	param->rsz_en[RSZ_B] = DISABLE;
	param->oper_mode = RESIZER_MODE_CONTINIOUS;

	if (resizer->resizer_b.output == RESIZER_OUPUT_MEMORY) {
		struct v4l2_mbus_framefmt *outformat2;

		param->rsz_en[RSZ_B] = ENABLE;
		outformat2 = &resizer->resizer_b.formats[RESIZER_PAD_SOURCE];
		ret = resizer_validate_output_image_format(dev, outformat2,
				&line_len, &line_len_c);
		if (ret)
			return ret;
		param->ext_mem_param[RSZ_B].rsz_sdr_oft_y = line_len;
		param->ext_mem_param[RSZ_B].rsz_sdr_oft_c = line_len_c;
		configure_resizer_out_params(resizer, RSZ_B,
						&cont_config->output2, 0, 1);
		if (outformat2->code == MEDIA_BUS_FMT_YDYUYDYV8_1X16)
			resizer_enable_422_420_conversion(param,
							  RSZ_B, ENABLE);
		else
			resizer_enable_422_420_conversion(param,
							  RSZ_B, DISABLE);
	}
	resizer_configure_common_in_params(resizer);
	ret = resizer_configure_output_win(resizer);
	if (ret)
		return ret;

	param->rsz_common.passthrough = cont_config->bypass;
	if (cont_config->bypass)
		resizer_configure_passthru(resizer, 1);

	return 0;
}

static inline int
resizer_validate_input_image_format(struct device *dev,
				    u32 pix,
				    int width, int height, int *line_len)
{
	int val;

	if (pix != MEDIA_BUS_FMT_UYVY8_2X8 &&
	    pix != MEDIA_BUS_FMT_Y8_1X8 &&
	    pix != MEDIA_BUS_FMT_UV8_1X8 &&
	    pix != MEDIA_BUS_FMT_SGRBG12_1X12) {
		dev_err(dev,
		"resizer validate output: pix format not supported, %d\n", pix);
		return -EINVAL;
	}

	if (!width || !height) {
		dev_err(dev,
			"resizer validate input: invalid width or height\n");
		return -EINVAL;
	}

	if (pix == MEDIA_BUS_FMT_UV8_1X8)
		resizer_calculate_line_length(pix, width,
					      height, &val, line_len);
	else
		resizer_calculate_line_length(pix, width,
					      height, line_len, &val);

	return 0;
}

static int
resizer_validate_decimation(struct device *dev, enum ipipeif_decimation dec_en,
			    unsigned char rsz, unsigned char frame_div_mode_en,
			    int width)
{
	if (dec_en && frame_div_mode_en) {
		dev_err(dev,
		 "dec_en & frame_div_mode_en can not enabled simultaneously\n");
		return -EINVAL;
	}

	if (frame_div_mode_en) {
		dev_err(dev, "frame_div_mode mode not supported\n");
		return -EINVAL;
	}

	if (!dec_en)
		return 0;

	if (width <= VPFE_IPIPE_MAX_INPUT_WIDTH) {
		dev_err(dev,
			"image width to be more than %d for decimation\n",
			VPFE_IPIPE_MAX_INPUT_WIDTH);
		return -EINVAL;
	}

	if (rsz < IPIPEIF_RSZ_MIN || rsz > IPIPEIF_RSZ_MAX) {
		dev_err(dev, "rsz range is %d to %d\n",
			IPIPEIF_RSZ_MIN, IPIPEIF_RSZ_MAX);
		return -EINVAL;
	}

	return 0;
}

/* resizer_calculate_normal_f_div_param() - Algorithm to calculate the frame
 *					    division parameters for resizer.
 *					    in normal mode.
 */
static int
resizer_calculate_normal_f_div_param(struct device *dev, int input_width,
		int output_width, struct resizer_scale_param *param)
{
	/* rsz = R, input_width = H, output width = h in the equation */
	unsigned int val1;
	unsigned int rsz;
	unsigned int val;
	unsigned int h1;
	unsigned int h2;
	unsigned int o;

	if (output_width > input_width) {
		dev_err(dev, "frame div mode is used for scale down only\n");
		return -EINVAL;
	}

	rsz = (input_width << 8) / output_width;
	val = rsz << 1;
	val = ((input_width << 8) / val) + 1;
	o = 14;
	if (!(val % 2)) {
		h1 = val;
	} else {
		val = input_width << 7;
		val -= rsz >> 1;
		val /= rsz << 1;
		val <<= 1;
		val += 2;
		o += ((CEIL(rsz, 1024)) << 1);
		h1 = val;
	}
	h2 = output_width - h1;
	/* phi */
	val = (h1 * rsz) - (((input_width >> 1) - o) << 8);
	/* skip */
	val1 = ((val - 1024) >> 9) << 1;
	param->f_div.num_passes = MAX_PASSES;
	param->f_div.pass[0].o_hsz = h1 - 1;
	param->f_div.pass[0].i_hps = 0;
	param->f_div.pass[0].h_phs = 0;
	param->f_div.pass[0].src_hps = 0;
	param->f_div.pass[0].src_hsz = (input_width >> 2) + o;
	param->f_div.pass[1].o_hsz = h2 - 1;
	param->f_div.pass[1].i_hps = val1;
	param->f_div.pass[1].h_phs = (val - (val1 << 8));
	param->f_div.pass[1].src_hps = (input_width >> 2) - o;
	param->f_div.pass[1].src_hsz = (input_width >> 2) + o;

	return 0;
}

static int
resizer_configure_in_single_shot_mode(struct vpfe_resizer_device *resizer)
{
	struct vpfe_rsz_config_params *config = &resizer->config.user_config;
	struct device *dev = resizer->crop_resizer.subdev.v4l2_dev->dev;
	struct vpfe_device *vpfe_dev = to_vpfe_device(resizer);
	struct v4l2_mbus_framefmt *outformat1, *outformat2;
	struct resizer_params *param = &resizer->config;
	struct v4l2_mbus_framefmt *informat;
	int decimation;
	int line_len_c;
	int line_len;
	int rsz;
	int ret;

	informat = &resizer->crop_resizer.formats[RESIZER_CROP_PAD_SINK];
	outformat1 = &resizer->resizer_a.formats[RESIZER_PAD_SOURCE];
	outformat2 = &resizer->resizer_b.formats[RESIZER_PAD_SOURCE];

	decimation = vpfe_ipipeif_decimation_enabled(vpfe_dev);
	rsz = vpfe_ipipeif_get_rsz(vpfe_dev);
	if (decimation && param->user_config.frame_div_mode_en) {
		dev_err(dev,
		"dec_en & frame_div_mode_en cannot enabled simultaneously\n");
		return -EINVAL;
	}

	ret = resizer_validate_decimation(dev, decimation, rsz,
	      param->user_config.frame_div_mode_en, informat->width);
	if (ret)
		return -EINVAL;

	ret = resizer_validate_input_image_format(dev, informat->code,
		informat->width, informat->height, &line_len);
	if (ret)
		return -EINVAL;

	if (resizer->resizer_a.output != RESIZER_OUTPUT_NONE) {
		param->rsz_en[RSZ_A] = ENABLE;
		ret = resizer_validate_output_image_format(dev, outformat1,
					&line_len, &line_len_c);
		if (ret)
			return ret;
		param->ext_mem_param[RSZ_A].rsz_sdr_oft_y = line_len;
		param->ext_mem_param[RSZ_A].rsz_sdr_oft_c = line_len_c;
		configure_resizer_out_params(resizer, RSZ_A,
					&param->user_config.output1, 0, 1);

		if (outformat1->code == MEDIA_BUS_FMT_SGRBG12_1X12)
			param->rsz_common.raw_flip = 1;
		else
			param->rsz_common.raw_flip = 0;

		if (outformat1->code == MEDIA_BUS_FMT_YDYUYDYV8_1X16)
			resizer_enable_422_420_conversion(param,
							  RSZ_A, ENABLE);
		else
			resizer_enable_422_420_conversion(param,
							  RSZ_A, DISABLE);
	}

	if (resizer->resizer_b.output != RESIZER_OUTPUT_NONE) {
		param->rsz_en[RSZ_B] = ENABLE;
		ret = resizer_validate_output_image_format(dev, outformat2,
				&line_len, &line_len_c);
		if (ret)
			return ret;
		param->ext_mem_param[RSZ_B].rsz_sdr_oft_y = line_len;
		param->ext_mem_param[RSZ_B].rsz_sdr_oft_c = line_len_c;
		configure_resizer_out_params(resizer, RSZ_B,
					&param->user_config.output2, 0, 1);
		if (outformat2->code == MEDIA_BUS_FMT_YDYUYDYV8_1X16)
			resizer_enable_422_420_conversion(param,
							  RSZ_B, ENABLE);
		else
			resizer_enable_422_420_conversion(param,
							  RSZ_B, DISABLE);
	}

	resizer_configure_common_in_params(resizer);
	if (resizer->resizer_a.output != RESIZER_OUTPUT_NONE) {
		resizer_calculate_resize_ratios(resizer, RSZ_A);
		resizer_calculate_sdram_offsets(resizer, RSZ_A);
		/* Overriding resize ratio calculation */
		if (informat->code == MEDIA_BUS_FMT_UV8_1X8) {
			param->rsz_rsc_param[RSZ_A].v_dif =
				(((informat->height + 1) * 2) * 256) /
				(param->rsz_rsc_param[RSZ_A].o_vsz + 1);
		}
	}

	if (resizer->resizer_b.output != RESIZER_OUTPUT_NONE) {
		resizer_calculate_resize_ratios(resizer, RSZ_B);
		resizer_calculate_sdram_offsets(resizer, RSZ_B);
		/* Overriding resize ratio calculation */
		if (informat->code == MEDIA_BUS_FMT_UV8_1X8) {
			param->rsz_rsc_param[RSZ_B].v_dif =
				(((informat->height + 1) * 2) * 256) /
				(param->rsz_rsc_param[RSZ_B].o_vsz + 1);
		}
	}
	if (param->user_config.frame_div_mode_en &&
		param->rsz_en[RSZ_A]) {
		if (!param->rsz_rsc_param[RSZ_A].dscale_en)
			ret = resizer_calculate_normal_f_div_param(dev,
			      informat->width,
			      param->rsz_rsc_param[RSZ_A].o_vsz + 1,
			      &param->rsz_rsc_param[RSZ_A]);
		else
			ret = resizer_calculate_down_scale_f_div_param(dev,
			      informat->width,
			      param->rsz_rsc_param[RSZ_A].o_vsz + 1,
			      &param->rsz_rsc_param[RSZ_A]);
		if (ret)
			return -EINVAL;
	}
	if (param->user_config.frame_div_mode_en &&
		param->rsz_en[RSZ_B]) {
		if (!param->rsz_rsc_param[RSZ_B].dscale_en)
			ret = resizer_calculate_normal_f_div_param(dev,
			      informat->width,
			      param->rsz_rsc_param[RSZ_B].o_vsz + 1,
			      &param->rsz_rsc_param[RSZ_B]);
		else
			ret = resizer_calculate_down_scale_f_div_param(dev,
			      informat->width,
			      param->rsz_rsc_param[RSZ_B].o_vsz + 1,
			      &param->rsz_rsc_param[RSZ_B]);
		if (ret)
			return -EINVAL;
	}
	param->rsz_common.passthrough = config->bypass;
	if (config->bypass)
		resizer_configure_passthru(resizer, 1);
	return 0;
}

static void
resizer_set_defualt_configuration(struct vpfe_resizer_device *resizer)
{
#define  WIDTH_I 640
#define  HEIGHT_I 480
#define  WIDTH_O 640
#define  HEIGHT_O 480
	const struct resizer_params rsz_default_config = {
		.oper_mode = RESIZER_MODE_ONE_SHOT,
		.rsz_common = {
			.vsz = HEIGHT_I - 1,
			.hsz = WIDTH_I - 1,
			.src_img_fmt = RSZ_IMG_422,
			.raw_flip = 1,	/* flip preserve Raw format */
			.source = IPIPE_DATA,
			.passthrough = BYPASS_OFF,
			.yuv_y_max = 255,
			.yuv_c_max = 255,
			.rsz_seq_crv = DISABLE,
			.out_chr_pos = VPFE_IPIPE_YUV422_CHR_POS_COSITE,
		},
		.rsz_rsc_param = {
			{
				.h_flip = DISABLE,
				.v_flip = DISABLE,
				.cen = DISABLE,
				.yen = DISABLE,
				.o_vsz = HEIGHT_O - 1,
				.o_hsz = WIDTH_O - 1,
				.v_dif = 256,
				.v_typ_y = VPFE_RSZ_INTP_CUBIC,
				.h_typ_c = VPFE_RSZ_INTP_CUBIC,
				.h_dif = 256,
				.h_typ_y = VPFE_RSZ_INTP_CUBIC,
				.h_typ_c = VPFE_RSZ_INTP_CUBIC,
				.h_dscale_ave_sz =
					VPFE_IPIPE_DWN_SCALE_1_OVER_2,
				.v_dscale_ave_sz =
					VPFE_IPIPE_DWN_SCALE_1_OVER_2,
			},
			{
				.h_flip = DISABLE,
				.v_flip = DISABLE,
				.cen = DISABLE,
				.yen = DISABLE,
				.o_vsz = HEIGHT_O - 1,
				.o_hsz = WIDTH_O - 1,
				.v_dif = 256,
				.v_typ_y = VPFE_RSZ_INTP_CUBIC,
				.h_typ_c = VPFE_RSZ_INTP_CUBIC,
				.h_dif = 256,
				.h_typ_y = VPFE_RSZ_INTP_CUBIC,
				.h_typ_c = VPFE_RSZ_INTP_CUBIC,
				.h_dscale_ave_sz =
					VPFE_IPIPE_DWN_SCALE_1_OVER_2,
				.v_dscale_ave_sz =
					VPFE_IPIPE_DWN_SCALE_1_OVER_2,
			},
		},
		.rsz2rgb = {
			{
				.rgb_en = DISABLE
			},
			{
				.rgb_en = DISABLE
			}
		},
		.ext_mem_param = {
			{
				.rsz_sdr_oft_y = WIDTH_O << 1,
				.rsz_sdr_ptr_e_y = HEIGHT_O,
				.rsz_sdr_oft_c = WIDTH_O,
				.rsz_sdr_ptr_e_c = HEIGHT_O >> 1,
			},
			{
				.rsz_sdr_oft_y = WIDTH_O << 1,
				.rsz_sdr_ptr_e_y = HEIGHT_O,
				.rsz_sdr_oft_c = WIDTH_O,
				.rsz_sdr_ptr_e_c = HEIGHT_O,
			},
		},
		.rsz_en[0] = ENABLE,
		.rsz_en[1] = DISABLE,
		.user_config = {
			.output1 = {
				.v_typ_y = VPFE_RSZ_INTP_CUBIC,
				.v_typ_c = VPFE_RSZ_INTP_CUBIC,
				.h_typ_y = VPFE_RSZ_INTP_CUBIC,
				.h_typ_c = VPFE_RSZ_INTP_CUBIC,
				.h_dscale_ave_sz =
					VPFE_IPIPE_DWN_SCALE_1_OVER_2,
				.v_dscale_ave_sz =
					VPFE_IPIPE_DWN_SCALE_1_OVER_2,
			},
			.output2 = {
				.v_typ_y = VPFE_RSZ_INTP_CUBIC,
				.v_typ_c = VPFE_RSZ_INTP_CUBIC,
				.h_typ_y = VPFE_RSZ_INTP_CUBIC,
				.h_typ_c = VPFE_RSZ_INTP_CUBIC,
				.h_dscale_ave_sz =
					VPFE_IPIPE_DWN_SCALE_1_OVER_2,
				.v_dscale_ave_sz =
					VPFE_IPIPE_DWN_SCALE_1_OVER_2,
			},
			.yuv_y_max = 255,
			.yuv_c_max = 255,
			.out_chr_pos = VPFE_IPIPE_YUV422_CHR_POS_COSITE,
		},
	};
	memset(&resizer->config, 0, sizeof(struct resizer_params));
	memcpy(&resizer->config, &rsz_default_config,
	       sizeof(struct resizer_params));
}

/*
 * resizer_set_configuration() - set resizer config
 * @resizer: vpfe resizer device pointer.
 * @chan_config: resizer channel configuration.
 */
static int
resizer_set_configuration(struct vpfe_resizer_device *resizer,
			  struct vpfe_rsz_config *chan_config)
{
	if (!chan_config->config)
		resizer_set_defualt_configuration(resizer);
	else
		if (copy_from_user(&resizer->config.user_config,
		    chan_config->config, sizeof(struct vpfe_rsz_config_params)))
			return -EFAULT;

	return 0;
}

/*
 * resizer_get_configuration() - get resizer config
 * @resizer: vpfe resizer device pointer.
 * @channel: image processor logical channel.
 * @chan_config: resizer channel configuration.
 */
static int
resizer_get_configuration(struct vpfe_resizer_device *resizer,
		   struct vpfe_rsz_config *chan_config)
{
	struct device *dev = resizer->crop_resizer.subdev.v4l2_dev->dev;

	if (!chan_config->config) {
		dev_err(dev, "Resizer channel invalid pointer\n");
		return -EINVAL;
	}

	if (copy_to_user((void *)chan_config->config,
	   (void *)&resizer->config.user_config,
	   sizeof(struct vpfe_rsz_config_params))) {
		dev_err(dev, "resizer_get_configuration: Error in copy to user\n");
		return -EFAULT;
	}

	return 0;
}

/*
 * VPFE video operations
 */

/*
 * resizer_a_video_out_queue() - RESIZER-A video out queue
 * @vpfe_dev: vpfe device pointer.
 * @addr: buffer address.
 */
static int resizer_a_video_out_queue(struct vpfe_device *vpfe_dev,
				     unsigned long addr)
{
	struct vpfe_resizer_device *resizer = &vpfe_dev->vpfe_resizer;

	return resizer_set_outaddr(resizer->base_addr,
				      &resizer->config, RSZ_A, addr);
}

/*
 * resizer_b_video_out_queue() - RESIZER-B video out queue
 * @vpfe_dev: vpfe device pointer.
 * @addr: buffer address.
 */
static int resizer_b_video_out_queue(struct vpfe_device *vpfe_dev,
				     unsigned long addr)
{
	struct vpfe_resizer_device *resizer = &vpfe_dev->vpfe_resizer;

	return resizer_set_outaddr(resizer->base_addr,
				   &resizer->config, RSZ_B, addr);
}

static const struct vpfe_video_operations resizer_a_video_ops = {
	.queue = resizer_a_video_out_queue,
};

static const struct vpfe_video_operations resizer_b_video_ops = {
	.queue = resizer_b_video_out_queue,
};

static void resizer_enable(struct vpfe_resizer_device *resizer, int en)
{
	struct vpfe_device *vpfe_dev = to_vpfe_device(resizer);
	u16 ipipeif_sink = vpfe_dev->vpfe_ipipeif.input;
	unsigned char val;

	if (resizer->crop_resizer.input == RESIZER_CROP_INPUT_NONE)
		return;

	if (resizer->crop_resizer.input == RESIZER_CROP_INPUT_IPIPEIF &&
	   ipipeif_sink == IPIPEIF_INPUT_MEMORY) {
		do {
			val = regr_rsz(resizer->base_addr, RSZ_SRC_EN);
		} while (val);

		if (resizer->resizer_a.output != RESIZER_OUTPUT_NONE) {
			do {
				val = regr_rsz(resizer->base_addr, RSZ_A);
			} while (val);
		}
		if (resizer->resizer_b.output != RESIZER_OUTPUT_NONE) {
			do {
				val = regr_rsz(resizer->base_addr, RSZ_B);
			} while (val);
		}
	}
	if (resizer->resizer_a.output != RESIZER_OUTPUT_NONE)
		rsz_enable(resizer->base_addr, RSZ_A, en);

	if (resizer->resizer_b.output != RESIZER_OUTPUT_NONE)
		rsz_enable(resizer->base_addr, RSZ_B, en);
}


/*
 * resizer_ss_isr() - resizer module single-shot buffer scheduling isr
 * @resizer: vpfe resizer device pointer.
 */
static void resizer_ss_isr(struct vpfe_resizer_device *resizer)
{
	struct vpfe_video_device *video_out = &resizer->resizer_a.video_out;
	struct vpfe_video_device *video_out2 = &resizer->resizer_b.video_out;
	struct vpfe_device *vpfe_dev = to_vpfe_device(resizer);
	struct vpfe_pipeline *pipe = &video_out->pipe;
	u16 ipipeif_sink = vpfe_dev->vpfe_ipipeif.input;
	u32 val;

	if (ipipeif_sink != IPIPEIF_INPUT_MEMORY)
		return;

	if (resizer->resizer_a.output == RESIZER_OUPUT_MEMORY) {
		val = vpss_dma_complete_interrupt();
		if (val != 0 && val != 2)
			return;
	}

	if (resizer->resizer_a.output == RESIZER_OUPUT_MEMORY) {
		spin_lock(&video_out->dma_queue_lock);
		vpfe_video_process_buffer_complete(video_out);
		video_out->state = VPFE_VIDEO_BUFFER_NOT_QUEUED;
		vpfe_video_schedule_next_buffer(video_out);
		spin_unlock(&video_out->dma_queue_lock);
	}

	/* If resizer B is enabled */
	if (pipe->output_num > 1 && resizer->resizer_b.output ==
	    RESIZER_OUPUT_MEMORY) {
		spin_lock(&video_out->dma_queue_lock);
		vpfe_video_process_buffer_complete(video_out2);
		video_out2->state = VPFE_VIDEO_BUFFER_NOT_QUEUED;
		vpfe_video_schedule_next_buffer(video_out2);
		spin_unlock(&video_out2->dma_queue_lock);
	}

	/* start HW if buffers are queued */
	if (vpfe_video_is_pipe_ready(pipe) &&
	    resizer->resizer_a.output == RESIZER_OUPUT_MEMORY) {
		resizer_enable(resizer, 1);
		vpfe_ipipe_enable(vpfe_dev, 1);
		vpfe_ipipeif_enable(vpfe_dev);
	}
}

/*
 * vpfe_resizer_buffer_isr() - resizer module buffer scheduling isr
 * @resizer: vpfe resizer device pointer.
 */
void vpfe_resizer_buffer_isr(struct vpfe_resizer_device *resizer)
{
	struct vpfe_device *vpfe_dev = to_vpfe_device(resizer);
	struct vpfe_video_device *video_out = &resizer->resizer_a.video_out;
	struct vpfe_video_device *video_out2 = &resizer->resizer_b.video_out;
	struct vpfe_pipeline *pipe = &resizer->resizer_a.video_out.pipe;
	enum v4l2_field field;
	int fid;

	if (!video_out->started)
		return;

	if (resizer->crop_resizer.input == RESIZER_CROP_INPUT_NONE)
		return;

	field = video_out->fmt.fmt.pix.field;
	if (field == V4L2_FIELD_NONE) {
		/* handle progressive frame capture */
		if (video_out->cur_frm != video_out->next_frm) {
			vpfe_video_process_buffer_complete(video_out);
			if (pipe->output_num > 1)
				vpfe_video_process_buffer_complete(video_out2);
		}

		video_out->skip_frame_count--;
		if (!video_out->skip_frame_count) {
			video_out->skip_frame_count =
				video_out->skip_frame_count_init;
			rsz_src_enable(resizer->base_addr, 1);
		} else {
			rsz_src_enable(resizer->base_addr, 0);
		}
		return;
	}

	/* handle interlaced frame capture */
	fid = vpfe_isif_get_fid(vpfe_dev);

	/* switch the software maintained field id */
	video_out->field_id ^= 1;
	if (fid == video_out->field_id) {
		/*
		 * we are in-sync here,continue.
		 * One frame is just being captured. If the
		 * next frame is available, release the current
		 * frame and move on
		 */
		if (fid == 0 && video_out->cur_frm != video_out->next_frm) {
			vpfe_video_process_buffer_complete(video_out);
			if (pipe->output_num > 1)
				vpfe_video_process_buffer_complete(video_out2);
		}
	} else if (fid == 0) {
		/*
		* out of sync. Recover from any hardware out-of-sync.
		* May loose one frame
		*/
		video_out->field_id = fid;
	}
}

/*
 * vpfe_resizer_dma_isr() - resizer module dma isr
 * @resizer: vpfe resizer device pointer.
 */
void vpfe_resizer_dma_isr(struct vpfe_resizer_device *resizer)
{
	struct vpfe_video_device *video_out2 = &resizer->resizer_b.video_out;
	struct vpfe_video_device *video_out = &resizer->resizer_a.video_out;
	struct vpfe_device *vpfe_dev = to_vpfe_device(resizer);
	struct vpfe_pipeline *pipe = &video_out->pipe;
	int schedule_capture = 0;
	enum v4l2_field field;
	int fid;

	if (!video_out->started)
		return;

	if (pipe->state == VPFE_PIPELINE_STREAM_SINGLESHOT) {
		resizer_ss_isr(resizer);
		return;
	}

	field = video_out->fmt.fmt.pix.field;
	if (field == V4L2_FIELD_NONE) {
		if (!list_empty(&video_out->dma_queue) &&
			video_out->cur_frm == video_out->next_frm)
			schedule_capture = 1;
	} else {
		fid = vpfe_isif_get_fid(vpfe_dev);
		if (fid == video_out->field_id) {
			/* we are in-sync here,continue */
			if (fid == 1 && !list_empty(&video_out->dma_queue) &&
			    video_out->cur_frm == video_out->next_frm)
				schedule_capture = 1;
		}
	}

	if (!schedule_capture)
		return;

	spin_lock(&video_out->dma_queue_lock);
	vpfe_video_schedule_next_buffer(video_out);
	spin_unlock(&video_out->dma_queue_lock);
	if (pipe->output_num > 1) {
		spin_lock(&video_out2->dma_queue_lock);
		vpfe_video_schedule_next_buffer(video_out2);
		spin_unlock(&video_out2->dma_queue_lock);
	}
}

/*
 * V4L2 subdev operations
 */

/*
 * resizer_ioctl() - Handle resizer module private ioctl's
 * @sd: pointer to v4l2 subdev structure
 * @cmd: configuration command
 * @arg: configuration argument
 */
static long resizer_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct vpfe_resizer_device *resizer = v4l2_get_subdevdata(sd);
	struct device *dev = resizer->crop_resizer.subdev.v4l2_dev->dev;
	struct vpfe_rsz_config *user_config;
	int ret = -ENOIOCTLCMD;

	if (&resizer->crop_resizer.subdev != sd)
		return ret;

	switch (cmd) {
	case VIDIOC_VPFE_RSZ_S_CONFIG:
		user_config = arg;
		ret = resizer_set_configuration(resizer, user_config);
		break;

	case VIDIOC_VPFE_RSZ_G_CONFIG:
		user_config = arg;
		if (!user_config->config) {
			dev_err(dev, "error in VIDIOC_VPFE_RSZ_G_CONFIG\n");
			return -EINVAL;
		}
		ret = resizer_get_configuration(resizer, user_config);
		break;
	}
	return ret;
}

static int resizer_do_hw_setup(struct vpfe_resizer_device *resizer)
{
	struct vpfe_device *vpfe_dev = to_vpfe_device(resizer);
	u16 ipipeif_sink = vpfe_dev->vpfe_ipipeif.input;
	u16 ipipeif_source = vpfe_dev->vpfe_ipipeif.output;
	struct resizer_params *param = &resizer->config;
	int ret = 0;

	if (resizer->resizer_a.output == RESIZER_OUPUT_MEMORY ||
	    resizer->resizer_b.output == RESIZER_OUPUT_MEMORY) {
		if (ipipeif_sink == IPIPEIF_INPUT_MEMORY &&
		    ipipeif_source == IPIPEIF_OUTPUT_RESIZER)
			ret = resizer_configure_in_single_shot_mode(resizer);
		else
			ret =  resizer_configure_in_continious_mode(resizer);
		if (ret)
			return ret;
		ret = config_rsz_hw(resizer, param);
	}
	return ret;
}

/*
 * resizer_set_stream() - Enable/Disable streaming on resizer subdev
 * @sd: pointer to v4l2 subdev structure
 * @enable: 1 == Enable, 0 == Disable
 */
static int resizer_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct vpfe_resizer_device *resizer = v4l2_get_subdevdata(sd);

	if (&resizer->crop_resizer.subdev != sd)
		return 0;

	if (resizer->resizer_a.output != RESIZER_OUPUT_MEMORY)
		return 0;

	switch (enable) {
	case 1:
		if (resizer_do_hw_setup(resizer) < 0)
			return -EINVAL;
		resizer_enable(resizer, enable);
		break;

	case 0:
		resizer_enable(resizer, enable);
		break;
	}

	return 0;
}

/*
 * __resizer_get_format() - helper function for getting resizer format
 * @sd: pointer to subdev.
 * @cfg: V4L2 subdev pad config
 * @pad: pad number.
 * @which: wanted subdev format.
 * Retun wanted mbus frame format.
 */
static struct v4l2_mbus_framefmt *
__resizer_get_format(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
		     unsigned int pad, enum v4l2_subdev_format_whence which)
{
	struct vpfe_resizer_device *resizer = v4l2_get_subdevdata(sd);

	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(sd, cfg, pad);
	if (&resizer->crop_resizer.subdev == sd)
		return &resizer->crop_resizer.formats[pad];
	if (&resizer->resizer_a.subdev == sd)
		return &resizer->resizer_a.formats[pad];
	if (&resizer->resizer_b.subdev == sd)
		return &resizer->resizer_b.formats[pad];
	return NULL;
}

/*
 * resizer_try_format() - Handle try format by pad subdev method
 * @sd: pointer to subdev.
 * @cfg: V4L2 subdev pad config
 * @pad: pad num.
 * @fmt: pointer to v4l2 format structure.
 * @which: wanted subdev format.
 */
static void
resizer_try_format(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
	unsigned int pad, struct v4l2_mbus_framefmt *fmt,
	enum v4l2_subdev_format_whence which)
{
	struct vpfe_resizer_device *resizer = v4l2_get_subdevdata(sd);
	unsigned int max_out_height;
	unsigned int max_out_width;
	unsigned int i;

	if ((&resizer->resizer_a.subdev == sd && pad == RESIZER_PAD_SINK) ||
	    (&resizer->resizer_b.subdev == sd && pad == RESIZER_PAD_SINK) ||
	    (&resizer->crop_resizer.subdev == sd &&
	    (pad == RESIZER_CROP_PAD_SOURCE ||
	    pad == RESIZER_CROP_PAD_SOURCE2 || pad == RESIZER_CROP_PAD_SINK))) {
		for (i = 0; i < ARRAY_SIZE(resizer_input_formats); i++) {
			if (fmt->code == resizer_input_formats[i])
				break;
		}
		/* If not found, use UYVY as default */
		if (i >= ARRAY_SIZE(resizer_input_formats))
			fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;

		fmt->width = clamp_t(u32, fmt->width, MIN_IN_WIDTH,
					MAX_IN_WIDTH);
		fmt->height = clamp_t(u32, fmt->height, MIN_IN_HEIGHT,
				MAX_IN_HEIGHT);
	} else if (&resizer->resizer_a.subdev == sd &&
		   pad == RESIZER_PAD_SOURCE) {
		max_out_width = IPIPE_MAX_OUTPUT_WIDTH_A;
		max_out_height = IPIPE_MAX_OUTPUT_HEIGHT_A;

		for (i = 0; i < ARRAY_SIZE(resizer_output_formats); i++) {
			if (fmt->code == resizer_output_formats[i])
				break;
		}
		/* If not found, use UYVY as default */
		if (i >= ARRAY_SIZE(resizer_output_formats))
			fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;

		fmt->width = clamp_t(u32, fmt->width, MIN_OUT_WIDTH,
					max_out_width);
		fmt->width &= ~15;
		fmt->height = clamp_t(u32, fmt->height, MIN_OUT_HEIGHT,
				max_out_height);
	} else if (&resizer->resizer_b.subdev == sd &&
		   pad == RESIZER_PAD_SOURCE) {
		max_out_width = IPIPE_MAX_OUTPUT_WIDTH_B;
		max_out_height = IPIPE_MAX_OUTPUT_HEIGHT_B;

		for (i = 0; i < ARRAY_SIZE(resizer_output_formats); i++) {
			if (fmt->code == resizer_output_formats[i])
				break;
		}
		/* If not found, use UYVY as default */
		if (i >= ARRAY_SIZE(resizer_output_formats))
			fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;

		fmt->width = clamp_t(u32, fmt->width, MIN_OUT_WIDTH,
					max_out_width);
		fmt->width &= ~15;
		fmt->height = clamp_t(u32, fmt->height, MIN_OUT_HEIGHT,
				max_out_height);
	}
}

/*
 * resizer_set_format() - Handle set format by pads subdev method
 * @sd: pointer to v4l2 subdev structure
 * @cfg: V4L2 subdev pad config
 * @fmt: pointer to v4l2 subdev format structure
 * return -EINVAL or zero on success
 */
static int resizer_set_format(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct vpfe_resizer_device *resizer = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __resizer_get_format(sd, cfg, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	resizer_try_format(sd, cfg, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	if (&resizer->crop_resizer.subdev == sd) {
		if (fmt->pad == RESIZER_CROP_PAD_SINK) {
			resizer->crop_resizer.formats[fmt->pad] = fmt->format;
		} else if (fmt->pad == RESIZER_CROP_PAD_SOURCE &&
				resizer->crop_resizer.output == RESIZER_A) {
			resizer->crop_resizer.formats[fmt->pad] = fmt->format;
			resizer->crop_resizer.
			formats[RESIZER_CROP_PAD_SOURCE2] = fmt->format;
		} else if (fmt->pad == RESIZER_CROP_PAD_SOURCE2 &&
			resizer->crop_resizer.output2 == RESIZER_B) {
			resizer->crop_resizer.formats[fmt->pad] = fmt->format;
			resizer->crop_resizer.
			formats[RESIZER_CROP_PAD_SOURCE] = fmt->format;
		} else {
			return -EINVAL;
		}
	} else if (&resizer->resizer_a.subdev == sd) {
		if (fmt->pad == RESIZER_PAD_SINK)
			resizer->resizer_a.formats[fmt->pad] = fmt->format;
		else if (fmt->pad == RESIZER_PAD_SOURCE)
			resizer->resizer_a.formats[fmt->pad] = fmt->format;
		else
			return -EINVAL;
	} else if (&resizer->resizer_b.subdev == sd) {
		if (fmt->pad == RESIZER_PAD_SINK)
			resizer->resizer_b.formats[fmt->pad] = fmt->format;
		else if (fmt->pad == RESIZER_PAD_SOURCE)
			resizer->resizer_b.formats[fmt->pad] = fmt->format;
		else
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	return 0;
}

/*
 * resizer_get_format() - Retrieve the video format on a pad
 * @sd: pointer to v4l2 subdev structure.
 * @cfg: V4L2 subdev pad config
 * @fmt: pointer to v4l2 subdev format structure
 * return -EINVAL or zero on success
 */
static int resizer_get_format(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *format;

	format = __resizer_get_format(sd, cfg, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

/*
 * resizer_enum_frame_size() - enum frame sizes on pads
 * @sd: Pointer to subdevice.
 * @cfg: V4L2 subdev pad config
 * @code: pointer to v4l2_subdev_frame_size_enum structure.
 */
static int resizer_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	resizer_try_format(sd, cfg, fse->pad, &format,
			    V4L2_SUBDEV_FORMAT_TRY);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	resizer_try_format(sd, cfg, fse->pad, &format,
			   V4L2_SUBDEV_FORMAT_TRY);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

/*
 * resizer_enum_mbus_code() - enum mbus codes for pads
 * @sd: Pointer to subdevice.
 * @cfg: V4L2 subdev pad config
 * @code: pointer to v4l2_subdev_mbus_code_enum structure
 */
static int resizer_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad == RESIZER_PAD_SINK) {
		if (code->index >= ARRAY_SIZE(resizer_input_formats))
			return -EINVAL;

		code->code = resizer_input_formats[code->index];
	} else if (code->pad == RESIZER_PAD_SOURCE) {
		if (code->index >= ARRAY_SIZE(resizer_output_formats))
			return -EINVAL;

		code->code = resizer_output_formats[code->index];
	}

	return 0;
}

/*
 * resizer_init_formats() - Initialize formats on all pads
 * @sd: Pointer to subdevice.
 * @fh: V4L2 subdev file handle.
 *
 * Initialize all pad formats with default values. Try formats are
 * initialized on the file handle.
 */
static int resizer_init_formats(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	__u32 which = V4L2_SUBDEV_FORMAT_TRY;
	struct vpfe_resizer_device *resizer = v4l2_get_subdevdata(sd);
	struct v4l2_subdev_format format;

	if (&resizer->crop_resizer.subdev == sd) {
		memset(&format, 0, sizeof(format));
		format.pad = RESIZER_CROP_PAD_SINK;
		format.which = which;
		format.format.code = MEDIA_BUS_FMT_YUYV8_2X8;
		format.format.width = MAX_IN_WIDTH;
		format.format.height = MAX_IN_HEIGHT;
		resizer_set_format(sd, fh->pad, &format);

		memset(&format, 0, sizeof(format));
		format.pad = RESIZER_CROP_PAD_SOURCE;
		format.which = which;
		format.format.code = MEDIA_BUS_FMT_UYVY8_2X8;
		format.format.width = MAX_IN_WIDTH;
		format.format.height = MAX_IN_WIDTH;
		resizer_set_format(sd, fh->pad, &format);

		memset(&format, 0, sizeof(format));
		format.pad = RESIZER_CROP_PAD_SOURCE2;
		format.which = which;
		format.format.code = MEDIA_BUS_FMT_UYVY8_2X8;
		format.format.width = MAX_IN_WIDTH;
		format.format.height = MAX_IN_WIDTH;
		resizer_set_format(sd, fh->pad, &format);
	} else if (&resizer->resizer_a.subdev == sd) {
		memset(&format, 0, sizeof(format));
		format.pad = RESIZER_PAD_SINK;
		format.which = which;
		format.format.code = MEDIA_BUS_FMT_YUYV8_2X8;
		format.format.width = MAX_IN_WIDTH;
		format.format.height = MAX_IN_HEIGHT;
		resizer_set_format(sd, fh->pad, &format);

		memset(&format, 0, sizeof(format));
		format.pad = RESIZER_PAD_SOURCE;
		format.which = which;
		format.format.code = MEDIA_BUS_FMT_UYVY8_2X8;
		format.format.width = IPIPE_MAX_OUTPUT_WIDTH_A;
		format.format.height = IPIPE_MAX_OUTPUT_HEIGHT_A;
		resizer_set_format(sd, fh->pad, &format);
	} else if (&resizer->resizer_b.subdev == sd) {
		memset(&format, 0, sizeof(format));
		format.pad = RESIZER_PAD_SINK;
		format.which = which;
		format.format.code = MEDIA_BUS_FMT_YUYV8_2X8;
		format.format.width = MAX_IN_WIDTH;
		format.format.height = MAX_IN_HEIGHT;
		resizer_set_format(sd, fh->pad, &format);

		memset(&format, 0, sizeof(format));
		format.pad = RESIZER_PAD_SOURCE;
		format.which = which;
		format.format.code = MEDIA_BUS_FMT_UYVY8_2X8;
		format.format.width = IPIPE_MAX_OUTPUT_WIDTH_B;
		format.format.height = IPIPE_MAX_OUTPUT_HEIGHT_B;
		resizer_set_format(sd, fh->pad, &format);
	}

	return 0;
}

/* subdev core operations */
static const struct v4l2_subdev_core_ops resizer_v4l2_core_ops = {
	.ioctl = resizer_ioctl,
};

/* subdev internal operations */
static const struct v4l2_subdev_internal_ops resizer_v4l2_internal_ops = {
	.open = resizer_init_formats,
};

/* subdev video operations */
static const struct v4l2_subdev_video_ops resizer_v4l2_video_ops = {
	.s_stream = resizer_set_stream,
};

/* subdev pad operations */
static const struct v4l2_subdev_pad_ops resizer_v4l2_pad_ops = {
	.enum_mbus_code = resizer_enum_mbus_code,
	.enum_frame_size = resizer_enum_frame_size,
	.get_fmt = resizer_get_format,
	.set_fmt = resizer_set_format,
};

/* subdev operations */
static const struct v4l2_subdev_ops resizer_v4l2_ops = {
	.core = &resizer_v4l2_core_ops,
	.video = &resizer_v4l2_video_ops,
	.pad = &resizer_v4l2_pad_ops,
};

/*
 * Media entity operations
 */

/*
 * resizer_link_setup() - Setup resizer connections
 * @entity: Pointer to media entity structure
 * @local: Pointer to local pad array
 * @remote: Pointer to remote pad array
 * @flags: Link flags
 * return -EINVAL or zero on success
 */
static int resizer_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct vpfe_resizer_device *resizer = v4l2_get_subdevdata(sd);
	struct vpfe_device *vpfe_dev = to_vpfe_device(resizer);
	u16 ipipeif_source = vpfe_dev->vpfe_ipipeif.output;
	u16 ipipe_source = vpfe_dev->vpfe_ipipe.output;

	if (&resizer->crop_resizer.subdev == sd) {
		switch (local->index | media_entity_type(remote->entity)) {
		case RESIZER_CROP_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
			if (!(flags & MEDIA_LNK_FL_ENABLED)) {
				resizer->crop_resizer.input =
					RESIZER_CROP_INPUT_NONE;
				break;
			}

			if (resizer->crop_resizer.input !=
			   RESIZER_CROP_INPUT_NONE)
				return -EBUSY;
			if (ipipeif_source == IPIPEIF_OUTPUT_RESIZER)
				resizer->crop_resizer.input =
						RESIZER_CROP_INPUT_IPIPEIF;
			else if (ipipe_source == IPIPE_OUTPUT_RESIZER)
					resizer->crop_resizer.input =
						RESIZER_CROP_INPUT_IPIPE;
			else
				return -EINVAL;
			break;

		case RESIZER_CROP_PAD_SOURCE | MEDIA_ENT_T_V4L2_SUBDEV:
			if (!(flags & MEDIA_LNK_FL_ENABLED)) {
				resizer->crop_resizer.output =
				RESIZER_CROP_OUTPUT_NONE;
				break;
			}
			if (resizer->crop_resizer.output !=
			    RESIZER_CROP_OUTPUT_NONE)
				return -EBUSY;
			resizer->crop_resizer.output = RESIZER_A;
			break;

		case RESIZER_CROP_PAD_SOURCE2 | MEDIA_ENT_T_V4L2_SUBDEV:
			if (!(flags & MEDIA_LNK_FL_ENABLED)) {
				resizer->crop_resizer.output2 =
					RESIZER_CROP_OUTPUT_NONE;
				break;
			}
			if (resizer->crop_resizer.output2 !=
			    RESIZER_CROP_OUTPUT_NONE)
				return -EBUSY;
			resizer->crop_resizer.output2 = RESIZER_B;
			break;

		default:
			return -EINVAL;
		}
	} else if (&resizer->resizer_a.subdev == sd) {
		switch (local->index | media_entity_type(remote->entity)) {
		case RESIZER_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
			if (!(flags & MEDIA_LNK_FL_ENABLED)) {
				resizer->resizer_a.input = RESIZER_INPUT_NONE;
				break;
			}
			if (resizer->resizer_a.input != RESIZER_INPUT_NONE)
				return -EBUSY;
			resizer->resizer_a.input = RESIZER_INPUT_CROP_RESIZER;
			break;

		case RESIZER_PAD_SOURCE | MEDIA_ENT_T_DEVNODE:
			if (!(flags & MEDIA_LNK_FL_ENABLED)) {
				resizer->resizer_a.output = RESIZER_OUTPUT_NONE;
				break;
			}
			if (resizer->resizer_a.output != RESIZER_OUTPUT_NONE)
				return -EBUSY;
			resizer->resizer_a.output = RESIZER_OUPUT_MEMORY;
			break;

		default:
			return -EINVAL;
		}
	} else if (&resizer->resizer_b.subdev == sd) {
		switch (local->index | media_entity_type(remote->entity)) {
		case RESIZER_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
			if (!(flags & MEDIA_LNK_FL_ENABLED)) {
				resizer->resizer_b.input = RESIZER_INPUT_NONE;
				break;
			}
			if (resizer->resizer_b.input != RESIZER_INPUT_NONE)
				return -EBUSY;
			resizer->resizer_b.input = RESIZER_INPUT_CROP_RESIZER;
			break;

		case RESIZER_PAD_SOURCE | MEDIA_ENT_T_DEVNODE:
			if (!(flags & MEDIA_LNK_FL_ENABLED)) {
				resizer->resizer_b.output = RESIZER_OUTPUT_NONE;
				break;
			}
			if (resizer->resizer_b.output != RESIZER_OUTPUT_NONE)
				return -EBUSY;
			resizer->resizer_b.output = RESIZER_OUPUT_MEMORY;
			break;

		default:
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

static const struct media_entity_operations resizer_media_ops = {
	.link_setup = resizer_link_setup,
};

/*
 * vpfe_resizer_unregister_entities() - Unregister entity
 * @vpfe_rsz - pointer to resizer subdevice structure.
 */
void vpfe_resizer_unregister_entities(struct vpfe_resizer_device *vpfe_rsz)
{
	/* unregister video devices */
	vpfe_video_unregister(&vpfe_rsz->resizer_a.video_out);
	vpfe_video_unregister(&vpfe_rsz->resizer_b.video_out);

	/* unregister subdev */
	v4l2_device_unregister_subdev(&vpfe_rsz->crop_resizer.subdev);
	v4l2_device_unregister_subdev(&vpfe_rsz->resizer_a.subdev);
	v4l2_device_unregister_subdev(&vpfe_rsz->resizer_b.subdev);
	/* cleanup entity */
	media_entity_cleanup(&vpfe_rsz->crop_resizer.subdev.entity);
	media_entity_cleanup(&vpfe_rsz->resizer_a.subdev.entity);
	media_entity_cleanup(&vpfe_rsz->resizer_b.subdev.entity);
}

/*
 * vpfe_resizer_register_entities() - Register entity
 * @resizer - pointer to resizer devive.
 * @vdev: pointer to v4l2 device structure.
 */
int vpfe_resizer_register_entities(struct vpfe_resizer_device *resizer,
				   struct v4l2_device *vdev)
{
	struct vpfe_device *vpfe_dev = to_vpfe_device(resizer);
	unsigned int flags = 0;
	int ret;

	/* Register the crop resizer subdev */
	ret = v4l2_device_register_subdev(vdev, &resizer->crop_resizer.subdev);
	if (ret < 0) {
		pr_err("Failed to register crop resizer as v4l2-subdev\n");
		return ret;
	}
	/* Register Resizer-A subdev */
	ret = v4l2_device_register_subdev(vdev, &resizer->resizer_a.subdev);
	if (ret < 0) {
		pr_err("Failed to register resizer-a as v4l2-subdev\n");
		return ret;
	}
	/* Register Resizer-B subdev */
	ret = v4l2_device_register_subdev(vdev, &resizer->resizer_b.subdev);
	if (ret < 0) {
		pr_err("Failed to register resizer-b as v4l2-subdev\n");
		return ret;
	}
	/* Register video-out device for resizer-a */
	ret = vpfe_video_register(&resizer->resizer_a.video_out, vdev);
	if (ret) {
		pr_err("Failed to register RSZ-A video-out device\n");
		goto out_video_out2_register;
	}
	resizer->resizer_a.video_out.vpfe_dev = vpfe_dev;

	/* Register video-out device for resizer-b */
	ret = vpfe_video_register(&resizer->resizer_b.video_out, vdev);
	if (ret) {
		pr_err("Failed to register RSZ-B video-out device\n");
		goto out_video_out2_register;
	}
	resizer->resizer_b.video_out.vpfe_dev = vpfe_dev;

	/* create link between Resizer Crop----> Resizer A*/
	ret = media_entity_create_link(&resizer->crop_resizer.subdev.entity, 1,
				&resizer->resizer_a.subdev.entity,
				0, flags);
	if (ret < 0)
		goto out_create_link;

	/* create link between Resizer Crop----> Resizer B*/
	ret = media_entity_create_link(&resizer->crop_resizer.subdev.entity, 2,
				&resizer->resizer_b.subdev.entity,
				0, flags);
	if (ret < 0)
		goto out_create_link;

	/* create link between Resizer A ----> video out */
	ret = media_entity_create_link(&resizer->resizer_a.subdev.entity, 1,
		&resizer->resizer_a.video_out.video_dev.entity, 0, flags);
	if (ret < 0)
		goto out_create_link;

	/* create link between Resizer B ----> video out */
	ret = media_entity_create_link(&resizer->resizer_b.subdev.entity, 1,
		&resizer->resizer_b.video_out.video_dev.entity, 0, flags);
	if (ret < 0)
		goto out_create_link;

	return 0;

out_create_link:
	vpfe_video_unregister(&resizer->resizer_b.video_out);
out_video_out2_register:
	vpfe_video_unregister(&resizer->resizer_a.video_out);
	v4l2_device_unregister_subdev(&resizer->crop_resizer.subdev);
	v4l2_device_unregister_subdev(&resizer->resizer_a.subdev);
	v4l2_device_unregister_subdev(&resizer->resizer_b.subdev);
	media_entity_cleanup(&resizer->crop_resizer.subdev.entity);
	media_entity_cleanup(&resizer->resizer_a.subdev.entity);
	media_entity_cleanup(&resizer->resizer_b.subdev.entity);
	return ret;
}

/*
 * vpfe_resizer_init() - resizer device initialization.
 * @vpfe_rsz - pointer to resizer device
 * @pdev: platform device pointer.
 */
int vpfe_resizer_init(struct vpfe_resizer_device *vpfe_rsz,
		      struct platform_device *pdev)
{
	struct v4l2_subdev *sd = &vpfe_rsz->crop_resizer.subdev;
	struct media_pad *pads = &vpfe_rsz->crop_resizer.pads[0];
	struct media_entity *me = &sd->entity;
	static resource_size_t  res_len;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 5);
	if (!res)
		return -ENOENT;

	res_len = resource_size(res);
	res = request_mem_region(res->start, res_len, res->name);
	if (!res)
		return -EBUSY;

	vpfe_rsz->base_addr = ioremap_nocache(res->start, res_len);
	if (!vpfe_rsz->base_addr)
		return -EBUSY;

	v4l2_subdev_init(sd, &resizer_v4l2_ops);
	sd->internal_ops = &resizer_v4l2_internal_ops;
	strlcpy(sd->name, "DAVINCI RESIZER CROP", sizeof(sd->name));
	sd->grp_id = 1 << 16;	/* group ID for davinci subdevs */
	v4l2_set_subdevdata(sd, vpfe_rsz);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	pads[RESIZER_CROP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[RESIZER_CROP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	pads[RESIZER_CROP_PAD_SOURCE2].flags = MEDIA_PAD_FL_SOURCE;

	vpfe_rsz->crop_resizer.input = RESIZER_CROP_INPUT_NONE;
	vpfe_rsz->crop_resizer.output = RESIZER_CROP_OUTPUT_NONE;
	vpfe_rsz->crop_resizer.output2 = RESIZER_CROP_OUTPUT_NONE;
	vpfe_rsz->crop_resizer.rsz_device = vpfe_rsz;
	me->ops = &resizer_media_ops;
	ret = media_entity_init(me, RESIZER_CROP_PADS_NUM, pads, 0);
	if (ret)
		return ret;

	sd = &vpfe_rsz->resizer_a.subdev;
	pads = &vpfe_rsz->resizer_a.pads[0];
	me = &sd->entity;

	v4l2_subdev_init(sd, &resizer_v4l2_ops);
	sd->internal_ops = &resizer_v4l2_internal_ops;
	strlcpy(sd->name, "DAVINCI RESIZER A", sizeof(sd->name));
	sd->grp_id = 1 << 16;	/* group ID for davinci subdevs */
	v4l2_set_subdevdata(sd, vpfe_rsz);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	pads[RESIZER_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[RESIZER_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	vpfe_rsz->resizer_a.input = RESIZER_INPUT_NONE;
	vpfe_rsz->resizer_a.output = RESIZER_OUTPUT_NONE;
	vpfe_rsz->resizer_a.rsz_device = vpfe_rsz;
	me->ops = &resizer_media_ops;
	ret = media_entity_init(me, RESIZER_PADS_NUM, pads, 0);
	if (ret)
		return ret;

	sd = &vpfe_rsz->resizer_b.subdev;
	pads = &vpfe_rsz->resizer_b.pads[0];
	me = &sd->entity;

	v4l2_subdev_init(sd, &resizer_v4l2_ops);
	sd->internal_ops = &resizer_v4l2_internal_ops;
	strlcpy(sd->name, "DAVINCI RESIZER B", sizeof(sd->name));
	sd->grp_id = 1 << 16;	/* group ID for davinci subdevs */
	v4l2_set_subdevdata(sd, vpfe_rsz);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	pads[RESIZER_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[RESIZER_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	vpfe_rsz->resizer_b.input = RESIZER_INPUT_NONE;
	vpfe_rsz->resizer_b.output = RESIZER_OUTPUT_NONE;
	vpfe_rsz->resizer_b.rsz_device = vpfe_rsz;
	me->ops = &resizer_media_ops;
	ret = media_entity_init(me, RESIZER_PADS_NUM, pads, 0);
	if (ret)
		return ret;

	vpfe_rsz->resizer_a.video_out.ops = &resizer_a_video_ops;
	vpfe_rsz->resizer_a.video_out.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = vpfe_video_init(&vpfe_rsz->resizer_a.video_out, "RSZ-A");
	if (ret) {
		pr_err("Failed to init RSZ video-out device\n");
		return ret;
	}
	vpfe_rsz->resizer_b.video_out.ops = &resizer_b_video_ops;
	vpfe_rsz->resizer_b.video_out.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = vpfe_video_init(&vpfe_rsz->resizer_b.video_out, "RSZ-B");
	if (ret) {
		pr_err("Failed to init RSZ video-out2 device\n");
		return ret;
	}
	memset(&vpfe_rsz->config, 0, sizeof(struct resizer_params));

	return 0;
}

void
vpfe_resizer_cleanup(struct vpfe_resizer_device *vpfe_rsz,
		     struct platform_device *pdev)
{
	struct resource *res;

	iounmap(vpfe_rsz->base_addr);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 5);
	if (res)
		release_mem_region(res->start,
					resource_size(res));
}
