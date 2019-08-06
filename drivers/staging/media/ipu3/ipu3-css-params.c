// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Intel Corporation

#include <linux/device.h>

#include "ipu3-css.h"
#include "ipu3-css-fw.h"
#include "ipu3-tables.h"

#define DIV_ROUND_CLOSEST_DOWN(a, b)	(((a) + ((b) / 2) - 1) / (b))
#define roundclosest_down(a, b)		(DIV_ROUND_CLOSEST_DOWN(a, b) * (b))

#define IPU3_UAPI_ANR_MAX_RESET		((1 << 12) - 1)
#define IPU3_UAPI_ANR_MIN_RESET		(((-1) << 12) + 1)

struct ipu3_css_scaler_info {
	unsigned int phase_step;	/* Same for luma/chroma */
	int exp_shift;

	unsigned int phase_init;	/* luma/chroma dependent */
	int pad_left;
	int pad_right;
	int crop_left;
	int crop_top;
};

static unsigned int ipu3_css_scaler_get_exp(unsigned int counter,
					    unsigned int divider)
{
	int i = fls(divider) - fls(counter);

	if (i <= 0)
		return 0;

	if (divider >> i < counter)
		i = i - 1;

	return i;
}

/* Set up the CSS scaler look up table */
static void
ipu3_css_scaler_setup_lut(unsigned int taps, unsigned int input_width,
			  unsigned int output_width, int phase_step_correction,
			  const int *coeffs, unsigned int coeffs_size,
			  s8 coeff_lut[], struct ipu3_css_scaler_info *info)
{
	int tap, phase, phase_sum_left, phase_sum_right;
	int exponent = ipu3_css_scaler_get_exp(output_width, input_width);
	int mantissa = (1 << exponent) * output_width;
	unsigned int phase_step;

	if (input_width == output_width) {
		for (phase = 0; phase < IMGU_SCALER_PHASES; phase++) {
			for (tap = 0; tap < taps; tap++) {
				coeff_lut[phase * IMGU_SCALER_FILTER_TAPS + tap]
					= 0;
			}
		}

		info->phase_step = IMGU_SCALER_PHASES *
			(1 << IMGU_SCALER_PHASE_COUNTER_PREC_REF);
		info->exp_shift = 0;
		info->pad_left = 0;
		info->pad_right = 0;
		info->phase_init = 0;
		info->crop_left = 0;
		info->crop_top = 0;
		return;
	}

	for (phase = 0; phase < IMGU_SCALER_PHASES; phase++) {
		for (tap = 0; tap < taps; tap++) {
			/* flip table to for convolution reverse indexing */
			s64 coeff = coeffs[coeffs_size -
				((tap * (coeffs_size / taps)) + phase) - 1];
			coeff *= mantissa;
			coeff = div64_long(coeff, input_width);

			/* Add +"0.5" */
			coeff += 1 << (IMGU_SCALER_COEFF_BITS - 1);
			coeff >>= IMGU_SCALER_COEFF_BITS;

			coeff_lut[phase * IMGU_SCALER_FILTER_TAPS + tap] =
				coeff;
		}
	}

	phase_step = IMGU_SCALER_PHASES *
			(1 << IMGU_SCALER_PHASE_COUNTER_PREC_REF) *
			output_width / input_width;
	phase_step += phase_step_correction;
	phase_sum_left = (taps / 2 * IMGU_SCALER_PHASES *
			(1 << IMGU_SCALER_PHASE_COUNTER_PREC_REF)) -
			(1 << (IMGU_SCALER_PHASE_COUNTER_PREC_REF - 1));
	phase_sum_right = (taps / 2 * IMGU_SCALER_PHASES *
			(1 << IMGU_SCALER_PHASE_COUNTER_PREC_REF)) +
			(1 << (IMGU_SCALER_PHASE_COUNTER_PREC_REF - 1));

	info->exp_shift = IMGU_SCALER_MAX_EXPONENT_SHIFT - exponent;
	info->pad_left = (phase_sum_left % phase_step == 0) ?
		phase_sum_left / phase_step - 1 : phase_sum_left / phase_step;
	info->pad_right = (phase_sum_right % phase_step == 0) ?
		phase_sum_right / phase_step - 1 : phase_sum_right / phase_step;
	info->phase_init = phase_sum_left - phase_step * info->pad_left;
	info->phase_step = phase_step;
	info->crop_left = taps - 1;
	info->crop_top = taps - 1;
}

/*
 * Calculates the exact output image width/height, based on phase_step setting
 * (must be perfectly aligned with hardware).
 */
static unsigned int
ipu3_css_scaler_calc_scaled_output(unsigned int input,
				   struct ipu3_css_scaler_info *info)
{
	unsigned int arg1 = input * info->phase_step +
			(1 - IMGU_SCALER_TAPS_Y / 2) * IMGU_SCALER_FIR_PHASES -
			IMGU_SCALER_FIR_PHASES / (2 * IMGU_SCALER_PHASES);
	unsigned int arg2 = ((IMGU_SCALER_TAPS_Y / 2) * IMGU_SCALER_FIR_PHASES +
			IMGU_SCALER_FIR_PHASES / (2 * IMGU_SCALER_PHASES)) *
			IMGU_SCALER_FIR_PHASES + info->phase_step / 2;

	return ((arg1 + (arg2 - IMGU_SCALER_FIR_PHASES * info->phase_step) /
		IMGU_SCALER_FIR_PHASES) / (2 * IMGU_SCALER_FIR_PHASES)) * 2;
}

/*
 * Calculate the output width and height, given the luma
 * and chroma details of a scaler
 */
static void
ipu3_css_scaler_calc(u32 input_width, u32 input_height, u32 target_width,
		     u32 target_height, struct imgu_abi_osys_config *cfg,
		     struct ipu3_css_scaler_info *info_luma,
		     struct ipu3_css_scaler_info *info_chroma,
		     unsigned int *output_width, unsigned int *output_height,
		     unsigned int *procmode)
{
	u32 out_width = target_width;
	u32 out_height = target_height;
	const unsigned int height_alignment = 2;
	int phase_step_correction = -1;

	/*
	 * Calculate scaled output width. If the horizontal and vertical scaling
	 * factor is different, then choose the biggest and crop off excess
	 * lines or columns after formatting.
	 */
	if (target_height * input_width > target_width * input_height)
		target_width = DIV_ROUND_UP(target_height * input_width,
					    input_height);

	if (input_width == target_width)
		*procmode = IMGU_ABI_OSYS_PROCMODE_BYPASS;
	else
		*procmode = IMGU_ABI_OSYS_PROCMODE_DOWNSCALE;

	memset(&cfg->scaler_coeffs_chroma, 0,
	       sizeof(cfg->scaler_coeffs_chroma));
	memset(&cfg->scaler_coeffs_luma, 0, sizeof(*cfg->scaler_coeffs_luma));
	do {
		phase_step_correction++;

		ipu3_css_scaler_setup_lut(IMGU_SCALER_TAPS_Y,
					  input_width, target_width,
					  phase_step_correction,
					  ipu3_css_downscale_4taps,
					  IMGU_SCALER_DOWNSCALE_4TAPS_LEN,
					  cfg->scaler_coeffs_luma, info_luma);

		ipu3_css_scaler_setup_lut(IMGU_SCALER_TAPS_UV,
					  input_width, target_width,
					  phase_step_correction,
					  ipu3_css_downscale_2taps,
					  IMGU_SCALER_DOWNSCALE_2TAPS_LEN,
					  cfg->scaler_coeffs_chroma,
					  info_chroma);

		out_width = ipu3_css_scaler_calc_scaled_output(input_width,
							       info_luma);
		out_height = ipu3_css_scaler_calc_scaled_output(input_height,
								info_luma);
	} while ((out_width < target_width || out_height < target_height ||
		 !IS_ALIGNED(out_height, height_alignment)) &&
		 phase_step_correction <= 5);

	*output_width = out_width;
	*output_height = out_height;
}

/********************** Osys routines for scaler****************************/

static void ipu3_css_osys_set_format(enum imgu_abi_frame_format host_format,
				     unsigned int *osys_format,
				     unsigned int *osys_tiling)
{
	*osys_format = IMGU_ABI_OSYS_FORMAT_YUV420;
	*osys_tiling = IMGU_ABI_OSYS_TILING_NONE;

	switch (host_format) {
	case IMGU_ABI_FRAME_FORMAT_YUV420:
		*osys_format = IMGU_ABI_OSYS_FORMAT_YUV420;
		break;
	case IMGU_ABI_FRAME_FORMAT_YV12:
		*osys_format = IMGU_ABI_OSYS_FORMAT_YV12;
		break;
	case IMGU_ABI_FRAME_FORMAT_NV12:
		*osys_format = IMGU_ABI_OSYS_FORMAT_NV12;
		break;
	case IMGU_ABI_FRAME_FORMAT_NV16:
		*osys_format = IMGU_ABI_OSYS_FORMAT_NV16;
		break;
	case IMGU_ABI_FRAME_FORMAT_NV21:
		*osys_format = IMGU_ABI_OSYS_FORMAT_NV21;
		break;
	case IMGU_ABI_FRAME_FORMAT_NV12_TILEY:
		*osys_format = IMGU_ABI_OSYS_FORMAT_NV12;
		*osys_tiling = IMGU_ABI_OSYS_TILING_Y;
		break;
	default:
		/* For now, assume use default values */
		break;
	}
}

/*
 * Function calculates input frame stripe offset, based
 * on output frame stripe offset and filter parameters.
 */
static int ipu3_css_osys_calc_stripe_offset(int stripe_offset_out,
					    int fir_phases, int phase_init,
					    int phase_step, int pad_left)
{
	int stripe_offset_inp = stripe_offset_out * fir_phases -
				pad_left * phase_step;

	return DIV_ROUND_UP(stripe_offset_inp - phase_init, phase_step);
}

/*
 * Calculate input frame phase, given the output frame
 * stripe offset and filter parameters
 */
static int ipu3_css_osys_calc_stripe_phase_init(int stripe_offset_out,
						int fir_phases, int phase_init,
						int phase_step, int pad_left)
{
	int stripe_offset_inp =
		ipu3_css_osys_calc_stripe_offset(stripe_offset_out,
						 fir_phases, phase_init,
						 phase_step, pad_left);

	return phase_init + ((pad_left + stripe_offset_inp) * phase_step) -
		stripe_offset_out * fir_phases;
}

/*
 * This function calculates input frame stripe width,
 * based on output frame stripe offset and filter parameters
 */
static int ipu3_css_osys_calc_inp_stripe_width(int stripe_width_out,
					       int fir_phases, int phase_init,
					       int phase_step, int fir_taps,
					       int pad_left, int pad_right)
{
	int stripe_width_inp = (stripe_width_out + fir_taps - 1) * fir_phases;

	stripe_width_inp = DIV_ROUND_UP(stripe_width_inp - phase_init,
					phase_step);

	return stripe_width_inp - pad_left - pad_right;
}

/*
 * This function calculates output frame stripe width, basedi
 * on output frame stripe offset and filter parameters
 */
static int ipu3_css_osys_out_stripe_width(int stripe_width_inp, int fir_phases,
					  int phase_init, int phase_step,
					  int fir_taps, int pad_left,
					  int pad_right, int column_offset)
{
	int stripe_width_out = (pad_left + stripe_width_inp +
				pad_right - column_offset) * phase_step;

	stripe_width_out = (stripe_width_out + phase_init) / fir_phases;

	return stripe_width_out - (fir_taps - 1);
}

struct ipu3_css_reso {
	unsigned int input_width;
	unsigned int input_height;
	enum imgu_abi_frame_format input_format;
	unsigned int pin_width[IMGU_ABI_OSYS_PINS];
	unsigned int pin_height[IMGU_ABI_OSYS_PINS];
	unsigned int pin_stride[IMGU_ABI_OSYS_PINS];
	enum imgu_abi_frame_format pin_format[IMGU_ABI_OSYS_PINS];
	int chunk_width;
	int chunk_height;
	int block_height;
	int block_width;
};

struct ipu3_css_frame_params {
	/* Output pins */
	unsigned int enable;
	unsigned int format;
	unsigned int flip;
	unsigned int mirror;
	unsigned int tiling;
	unsigned int reduce_range;
	unsigned int width;
	unsigned int height;
	unsigned int stride;
	unsigned int scaled;
	unsigned int crop_left;
	unsigned int crop_top;
};

struct ipu3_css_stripe_params {
	unsigned int processing_mode;
	unsigned int phase_step;
	unsigned int exp_shift;
	unsigned int phase_init_left_y;
	unsigned int phase_init_left_uv;
	unsigned int phase_init_top_y;
	unsigned int phase_init_top_uv;
	unsigned int pad_left_y;
	unsigned int pad_left_uv;
	unsigned int pad_right_y;
	unsigned int pad_right_uv;
	unsigned int pad_top_y;
	unsigned int pad_top_uv;
	unsigned int pad_bottom_y;
	unsigned int pad_bottom_uv;
	unsigned int crop_left_y;
	unsigned int crop_top_y;
	unsigned int crop_left_uv;
	unsigned int crop_top_uv;
	unsigned int start_column_y;
	unsigned int start_column_uv;
	unsigned int chunk_width;
	unsigned int chunk_height;
	unsigned int block_width;
	unsigned int block_height;
	unsigned int input_width;
	unsigned int input_height;
	int output_width[IMGU_ABI_OSYS_PINS];
	int output_height[IMGU_ABI_OSYS_PINS];
	int output_offset[IMGU_ABI_OSYS_PINS];
};

/*
 * frame_params - size IMGU_ABI_OSYS_PINS
 * stripe_params - size IPU3_UAPI_MAX_STRIPES
 */
static int ipu3_css_osys_calc_frame_and_stripe_params(
		struct ipu3_css *css, unsigned int stripes,
		struct imgu_abi_osys_config *osys,
		struct ipu3_css_scaler_info *scaler_luma,
		struct ipu3_css_scaler_info *scaler_chroma,
		struct ipu3_css_frame_params frame_params[],
		struct ipu3_css_stripe_params stripe_params[],
		unsigned int pipe)
{
	struct ipu3_css_reso reso;
	unsigned int output_width, pin, s;
	u32 input_width, input_height, target_width, target_height;
	unsigned int procmode = 0;
	struct ipu3_css_pipe *css_pipe = &css->pipes[pipe];

	input_width = css_pipe->rect[IPU3_CSS_RECT_GDC].width;
	input_height = css_pipe->rect[IPU3_CSS_RECT_GDC].height;
	target_width = css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.width;
	target_height = css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.height;

	/* Frame parameters */

	/* Input width for Output System is output width of DVS (with GDC) */
	reso.input_width = css_pipe->rect[IPU3_CSS_RECT_GDC].width;

	/* Input height for Output System is output height of DVS (with GDC) */
	reso.input_height = css_pipe->rect[IPU3_CSS_RECT_GDC].height;

	reso.input_format =
		css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->frame_format;

	reso.pin_width[IMGU_ABI_OSYS_PIN_OUT] =
		css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.width;
	reso.pin_height[IMGU_ABI_OSYS_PIN_OUT] =
		css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.height;
	reso.pin_stride[IMGU_ABI_OSYS_PIN_OUT] =
		css_pipe->queue[IPU3_CSS_QUEUE_OUT].width_pad;
	reso.pin_format[IMGU_ABI_OSYS_PIN_OUT] =
		css_pipe->queue[IPU3_CSS_QUEUE_OUT].css_fmt->frame_format;

	reso.pin_width[IMGU_ABI_OSYS_PIN_VF] =
		css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.width;
	reso.pin_height[IMGU_ABI_OSYS_PIN_VF] =
		css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.height;
	reso.pin_stride[IMGU_ABI_OSYS_PIN_VF] =
		css_pipe->queue[IPU3_CSS_QUEUE_VF].width_pad;
	reso.pin_format[IMGU_ABI_OSYS_PIN_VF] =
		css_pipe->queue[IPU3_CSS_QUEUE_VF].css_fmt->frame_format;

	/* Configure the frame parameters for all output pins */

	frame_params[IMGU_ABI_OSYS_PIN_OUT].width =
		css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.width;
	frame_params[IMGU_ABI_OSYS_PIN_OUT].height =
		css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.height;
	frame_params[IMGU_ABI_OSYS_PIN_VF].width =
		css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.width;
	frame_params[IMGU_ABI_OSYS_PIN_VF].height =
		css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.height;
	frame_params[IMGU_ABI_OSYS_PIN_VF].crop_top = 0;
	frame_params[IMGU_ABI_OSYS_PIN_VF].crop_left = 0;

	for (pin = 0; pin < IMGU_ABI_OSYS_PINS; pin++) {
		int enable = 0;
		int scaled = 0;
		unsigned int format = 0;
		unsigned int tiling = 0;

		frame_params[pin].flip = 0;
		frame_params[pin].mirror = 0;
		frame_params[pin].reduce_range = 0;
		if (reso.pin_width[pin] != 0 && reso.pin_height[pin] != 0) {
			enable = 1;
			if (pin == IMGU_ABI_OSYS_PIN_OUT) {
				if (reso.input_width < reso.pin_width[pin] ||
				    reso.input_height < reso.pin_height[pin])
					return -EINVAL;
				/*
				 * When input and output resolution is
				 * different instead of scaling, cropping
				 * should happen. Determine the crop factor
				 * to do the symmetric cropping
				 */
				frame_params[pin].crop_left = roundclosest_down(
						(reso.input_width -
						 reso.pin_width[pin]) / 2,
						 IMGU_OSYS_DMA_CROP_W_LIMIT);
				frame_params[pin].crop_top = roundclosest_down(
						(reso.input_height -
						 reso.pin_height[pin]) / 2,
						 IMGU_OSYS_DMA_CROP_H_LIMIT);
			} else {
				if (reso.pin_width[pin] != reso.input_width ||
				    reso.pin_height[pin] != reso.input_height) {
					/*
					 * If resolution is different at input
					 * and output of OSYS, scaling is
					 * considered except when pin is MAIN.
					 * Later it will be decide whether
					 * scaler factor is 1 or other
					 * and cropping has to be done or not.
					 */
					scaled = 1;
				}
			}
			ipu3_css_osys_set_format(reso.pin_format[pin], &format,
						 &tiling);
		} else {
			enable = 0;
		}
		frame_params[pin].enable = enable;
		frame_params[pin].format = format;
		frame_params[pin].tiling = tiling;
		frame_params[pin].stride = reso.pin_stride[pin];
		frame_params[pin].scaled = scaled;
	}

	ipu3_css_scaler_calc(input_width, input_height, target_width,
			     target_height, osys, scaler_luma, scaler_chroma,
			     &reso.pin_width[IMGU_ABI_OSYS_PIN_VF],
			     &reso.pin_height[IMGU_ABI_OSYS_PIN_VF], &procmode);
	dev_dbg(css->dev, "osys scaler procmode is %u", procmode);
	output_width = reso.pin_width[IMGU_ABI_OSYS_PIN_VF];

	if (output_width < reso.input_width / 2) {
		/* Scaling factor <= 0.5 */
		reso.chunk_width = IMGU_OSYS_BLOCK_WIDTH;
		reso.block_width = IMGU_OSYS_BLOCK_WIDTH;
	} else { /* 0.5 <= Scaling factor <= 1.0 */
		reso.chunk_width = IMGU_OSYS_BLOCK_WIDTH / 2;
		reso.block_width = IMGU_OSYS_BLOCK_WIDTH;
	}

	if (output_width <= reso.input_width * 7 / 8) {
		/* Scaling factor <= 0.875 */
		reso.chunk_height = IMGU_OSYS_BLOCK_HEIGHT;
		reso.block_height = IMGU_OSYS_BLOCK_HEIGHT;
	} else { /* 1.0 <= Scaling factor <= 1.75 */
		reso.chunk_height = IMGU_OSYS_BLOCK_HEIGHT / 2;
		reso.block_height = IMGU_OSYS_BLOCK_HEIGHT;
	}

	/*
	 * Calculate scaler configuration parameters based on input and output
	 * resolution.
	 */

	if (frame_params[IMGU_ABI_OSYS_PIN_VF].enable) {
		/*
		 * When aspect ratio is different between target resolution and
		 * required resolution, determine the crop factor to do
		 * symmetric cropping
		 */
		u32 w = reso.pin_width[IMGU_ABI_OSYS_PIN_VF] -
			frame_params[IMGU_ABI_OSYS_PIN_VF].width;
		u32 h = reso.pin_height[IMGU_ABI_OSYS_PIN_VF] -
			frame_params[IMGU_ABI_OSYS_PIN_VF].height;

		frame_params[IMGU_ABI_OSYS_PIN_VF].crop_left =
			roundclosest_down(w / 2, IMGU_OSYS_DMA_CROP_W_LIMIT);
		frame_params[IMGU_ABI_OSYS_PIN_VF].crop_top =
			roundclosest_down(h / 2, IMGU_OSYS_DMA_CROP_H_LIMIT);

		if (reso.input_height % 4 || reso.input_width % 8) {
			dev_err(css->dev, "OSYS input width is not multiple of 8 or\n");
			dev_err(css->dev, "height is not multiple of 4\n");
			return -EINVAL;
		}
	}

	/* Stripe parameters */

	if (frame_params[IMGU_ABI_OSYS_PIN_VF].enable) {
		output_width = reso.pin_width[IMGU_ABI_OSYS_PIN_VF];
	} else {
		/*
		 * in case scaler output is not enabled
		 * take output width as input width since
		 * there is no scaling at main pin.
		 * Due to the fact that main pin can be different
		 * from input resolution to osys in the case of cropping,
		 * main pin resolution is not taken.
		 */
		output_width = reso.input_width;
	}

	for (s = 0; s < stripes; s++) {
		int stripe_offset_inp_y = 0;
		int stripe_offset_inp_uv = 0;
		int stripe_offset_out_y = 0;
		int stripe_offset_out_uv = 0;
		int stripe_phase_init_y = scaler_luma->phase_init;
		int stripe_phase_init_uv = scaler_chroma->phase_init;
		int stripe_offset_blk_y = 0;
		int stripe_offset_blk_uv = 0;
		int stripe_offset_col_y = 0;
		int stripe_offset_col_uv = 0;
		int stripe_pad_left_y = scaler_luma->pad_left;
		int stripe_pad_left_uv = scaler_chroma->pad_left;
		int stripe_pad_right_y = scaler_luma->pad_right;
		int stripe_pad_right_uv = scaler_chroma->pad_right;
		int stripe_crop_left_y = scaler_luma->crop_left;
		int stripe_crop_left_uv = scaler_chroma->crop_left;
		int stripe_input_width_y = reso.input_width;
		int stripe_input_width_uv = 0;
		int stripe_output_width_y = output_width;
		int stripe_output_width_uv = 0;
		int chunk_floor_y = 0;
		int chunk_floor_uv = 0;
		int chunk_ceil_uv = 0;

		if (stripes > 1) {
			if (s > 0) {
				/* Calculate stripe offsets */
				stripe_offset_out_y =
					output_width * s / stripes;
				stripe_offset_out_y =
					rounddown(stripe_offset_out_y,
						  IPU3_UAPI_ISP_VEC_ELEMS);
				stripe_offset_out_uv = stripe_offset_out_y /
						IMGU_LUMA_TO_CHROMA_RATIO;
				stripe_offset_inp_y =
					ipu3_css_osys_calc_stripe_offset(
						stripe_offset_out_y,
						IMGU_OSYS_FIR_PHASES,
						scaler_luma->phase_init,
						scaler_luma->phase_step,
						scaler_luma->pad_left);
				stripe_offset_inp_uv =
					ipu3_css_osys_calc_stripe_offset(
						stripe_offset_out_uv,
						IMGU_OSYS_FIR_PHASES,
						scaler_chroma->phase_init,
						scaler_chroma->phase_step,
						scaler_chroma->pad_left);

				/* Calculate stripe phase init */
				stripe_phase_init_y =
					ipu3_css_osys_calc_stripe_phase_init(
						stripe_offset_out_y,
						IMGU_OSYS_FIR_PHASES,
						scaler_luma->phase_init,
						scaler_luma->phase_step,
						scaler_luma->pad_left);
				stripe_phase_init_uv =
					ipu3_css_osys_calc_stripe_phase_init(
						stripe_offset_out_uv,
						IMGU_OSYS_FIR_PHASES,
						scaler_chroma->phase_init,
						scaler_chroma->phase_step,
						scaler_chroma->pad_left);

				/*
				 * Chunk boundary corner case - luma and chroma
				 * start from different input chunks.
				 */
				chunk_floor_y = rounddown(stripe_offset_inp_y,
							  reso.chunk_width);
				chunk_floor_uv =
					rounddown(stripe_offset_inp_uv,
						  reso.chunk_width /
						  IMGU_LUMA_TO_CHROMA_RATIO);

				if (chunk_floor_y != chunk_floor_uv *
				    IMGU_LUMA_TO_CHROMA_RATIO) {
					/*
					 * Match starting luma/chroma chunks.
					 * Decrease offset for UV and add output
					 * cropping.
					 */
					stripe_offset_inp_uv -= 1;
					stripe_crop_left_uv += 1;
					stripe_phase_init_uv -=
						scaler_luma->phase_step;
					if (stripe_phase_init_uv < 0)
						stripe_phase_init_uv =
							stripe_phase_init_uv +
							IMGU_OSYS_FIR_PHASES;
				}
				/*
				 * FW workaround for a HW bug: if the first
				 * chroma pixel is generated exactly at the end
				 * of chunck scaler HW may not output the pixel
				 * for downscale factors smaller than 1.5
				 * (timing issue).
				 */
				chunk_ceil_uv =
					roundup(stripe_offset_inp_uv,
						reso.chunk_width /
						IMGU_LUMA_TO_CHROMA_RATIO);

				if (stripe_offset_inp_uv ==
				    chunk_ceil_uv - IMGU_OSYS_TAPS_UV) {
					/*
					 * Decrease input offset and add
					 * output cropping
					 */
					stripe_offset_inp_uv -= 1;
					stripe_phase_init_uv -=
						scaler_luma->phase_step;
					if (stripe_phase_init_uv < 0) {
						stripe_phase_init_uv +=
							IMGU_OSYS_FIR_PHASES;
						stripe_crop_left_uv += 1;
					}
				}

				/*
				 * Calculate block and column offsets for the
				 * input stripe
				 */
				stripe_offset_blk_y =
					rounddown(stripe_offset_inp_y,
						  IMGU_INPUT_BLOCK_WIDTH);
				stripe_offset_blk_uv =
					rounddown(stripe_offset_inp_uv,
						  IMGU_INPUT_BLOCK_WIDTH /
						  IMGU_LUMA_TO_CHROMA_RATIO);
				stripe_offset_col_y = stripe_offset_inp_y -
							stripe_offset_blk_y;
				stripe_offset_col_uv = stripe_offset_inp_uv -
							stripe_offset_blk_uv;

				/* Left padding is only for the first stripe */
				stripe_pad_left_y = 0;
				stripe_pad_left_uv = 0;
			}

			/* Right padding is only for the last stripe */
			if (s < stripes - 1) {
				int next_offset;

				stripe_pad_right_y = 0;
				stripe_pad_right_uv = 0;

				next_offset = output_width * (s + 1) / stripes;
				next_offset = rounddown(next_offset, 64);
				stripe_output_width_y = next_offset -
							stripe_offset_out_y;
			} else {
				stripe_output_width_y = output_width -
							stripe_offset_out_y;
			}

			/* Calculate target output stripe width */
			stripe_output_width_uv = stripe_output_width_y /
						IMGU_LUMA_TO_CHROMA_RATIO;
			/* Calculate input stripe width */
			stripe_input_width_y = stripe_offset_col_y +
				ipu3_css_osys_calc_inp_stripe_width(
						stripe_output_width_y,
						IMGU_OSYS_FIR_PHASES,
						stripe_phase_init_y,
						scaler_luma->phase_step,
						IMGU_OSYS_TAPS_Y,
						stripe_pad_left_y,
						stripe_pad_right_y);

			stripe_input_width_uv = stripe_offset_col_uv +
				ipu3_css_osys_calc_inp_stripe_width(
						stripe_output_width_uv,
						IMGU_OSYS_FIR_PHASES,
						stripe_phase_init_uv,
						scaler_chroma->phase_step,
						IMGU_OSYS_TAPS_UV,
						stripe_pad_left_uv,
						stripe_pad_right_uv);

			stripe_input_width_uv = max(DIV_ROUND_UP(
						    stripe_input_width_y,
						    IMGU_LUMA_TO_CHROMA_RATIO),
						    stripe_input_width_uv);

			stripe_input_width_y = stripe_input_width_uv *
						IMGU_LUMA_TO_CHROMA_RATIO;

			if (s >= stripes - 1) {
				stripe_input_width_y = reso.input_width -
					stripe_offset_blk_y;
				/*
				 * The scaler requires that the last stripe
				 * spans at least two input blocks.
				 */
			}

			/*
			 * Spec: input stripe width must be a multiple of 8.
			 * Increase the input width and recalculate the output
			 * width. This may produce an extra column of junk
			 * blocks which will be overwritten by the
			 * next stripe.
			 */
			stripe_input_width_y = ALIGN(stripe_input_width_y, 8);
			stripe_output_width_y =
				ipu3_css_osys_out_stripe_width(
						stripe_input_width_y,
						IMGU_OSYS_FIR_PHASES,
						stripe_phase_init_y,
						scaler_luma->phase_step,
						IMGU_OSYS_TAPS_Y,
						stripe_pad_left_y,
						stripe_pad_right_y,
						stripe_offset_col_y);

			stripe_output_width_y =
					rounddown(stripe_output_width_y,
						  IMGU_LUMA_TO_CHROMA_RATIO);
		}
		/*
		 * Following section executes and process parameters
		 * for both cases - Striping or No Striping.
		 */
		{
			unsigned int i;
			int pin_scale = 0;
			/*Input resolution */

			stripe_params[s].input_width = stripe_input_width_y;
			stripe_params[s].input_height = reso.input_height;

			for (i = 0; i < IMGU_ABI_OSYS_PINS; i++) {
				if (frame_params[i].scaled) {
					/*
					 * Output stripe resolution and offset
					 * as produced by the scaler; actual
					 * output resolution may be slightly
					 * smaller.
					 */
					stripe_params[s].output_width[i] =
						stripe_output_width_y;
					stripe_params[s].output_height[i] =
						reso.pin_height[i];
					stripe_params[s].output_offset[i] =
						stripe_offset_out_y;

					pin_scale += frame_params[i].scaled;
				} else {
					/* Unscaled pin */
					stripe_params[s].output_width[i] =
						stripe_params[s].input_width;
					stripe_params[s].output_height[i] =
						stripe_params[s].input_height;
					stripe_params[s].output_offset[i] =
						stripe_offset_blk_y;
				}
			}

			/* If no pin use scale, we use BYPASS mode */
			stripe_params[s].processing_mode = procmode;
			stripe_params[s].phase_step = scaler_luma->phase_step;
			stripe_params[s].exp_shift = scaler_luma->exp_shift;
			stripe_params[s].phase_init_left_y =
				stripe_phase_init_y;
			stripe_params[s].phase_init_left_uv =
				stripe_phase_init_uv;
			stripe_params[s].phase_init_top_y =
				scaler_luma->phase_init;
			stripe_params[s].phase_init_top_uv =
				scaler_chroma->phase_init;
			stripe_params[s].pad_left_y = stripe_pad_left_y;
			stripe_params[s].pad_left_uv = stripe_pad_left_uv;
			stripe_params[s].pad_right_y = stripe_pad_right_y;
			stripe_params[s].pad_right_uv = stripe_pad_right_uv;
			stripe_params[s].pad_top_y = scaler_luma->pad_left;
			stripe_params[s].pad_top_uv = scaler_chroma->pad_left;
			stripe_params[s].pad_bottom_y = scaler_luma->pad_right;
			stripe_params[s].pad_bottom_uv =
				scaler_chroma->pad_right;
			stripe_params[s].crop_left_y = stripe_crop_left_y;
			stripe_params[s].crop_top_y = scaler_luma->crop_top;
			stripe_params[s].crop_left_uv = stripe_crop_left_uv;
			stripe_params[s].crop_top_uv = scaler_chroma->crop_top;
			stripe_params[s].start_column_y = stripe_offset_col_y;
			stripe_params[s].start_column_uv = stripe_offset_col_uv;
			stripe_params[s].chunk_width = reso.chunk_width;
			stripe_params[s].chunk_height = reso.chunk_height;
			stripe_params[s].block_width = reso.block_width;
			stripe_params[s].block_height = reso.block_height;
		}
	}

	return 0;
}

/*
 * This function configures the Output Formatter System, given the number of
 * stripes, scaler luma and chrome parameters
 */
static int ipu3_css_osys_calc(struct ipu3_css *css, unsigned int pipe,
			      unsigned int stripes,
			      struct imgu_abi_osys_config *osys,
			      struct ipu3_css_scaler_info *scaler_luma,
			      struct ipu3_css_scaler_info *scaler_chroma,
			      struct imgu_abi_stripes block_stripes[])
{
	struct ipu3_css_frame_params frame_params[IMGU_ABI_OSYS_PINS];
	struct ipu3_css_stripe_params stripe_params[IPU3_UAPI_MAX_STRIPES];
	struct imgu_abi_osys_formatter_params *param;
	unsigned int pin, s;
	struct ipu3_css_pipe *css_pipe = &css->pipes[pipe];

	memset(osys, 0, sizeof(*osys));

	/* Compute the frame and stripe params */
	if (ipu3_css_osys_calc_frame_and_stripe_params(css, stripes, osys,
						       scaler_luma,
						       scaler_chroma,
						       frame_params,
						       stripe_params, pipe))
		return -EINVAL;

	/* Output formatter system parameters */

	for (s = 0; s < stripes; s++) {
		struct imgu_abi_osys_scaler_params *scaler =
					&osys->scaler[s].param;
		int fifo_addr_fmt = IMGU_FIFO_ADDR_SCALER_TO_FMT;
		int fifo_addr_ack = IMGU_FIFO_ADDR_SCALER_TO_SP;

		/* OUTPUT 0 / PIN 0 is only Scaler output */
		scaler->inp_buf_y_st_addr = IMGU_VMEM1_INP_BUF_ADDR;

		/*
		 * = (IMGU_OSYS_BLOCK_WIDTH / IMGU_VMEM1_ELEMS_PER_VEC)
		 * = (2 * IPU3_UAPI_ISP_VEC_ELEMS) /
		 *   (IMGU_HIVE_OF_SYS_OF_SYSTEM_NWAYS)
		 * = 2 * 64 / 32 = 4
		 */
		scaler->inp_buf_y_line_stride = IMGU_VMEM1_Y_STRIDE;
		/*
		 * = (IMGU_VMEM1_V_OFFSET + VMEM1_uv_size)
		 * = (IMGU_VMEM1_U_OFFSET + VMEM1_uv_size) +
		 *	(VMEM1_y_size / 4)
		 * = (VMEM1_y_size) + (VMEM1_y_size / 4) +
		 * (IMGU_OSYS_BLOCK_HEIGHT * IMGU_VMEM1_Y_STRIDE)/4
		 * = (IMGU_OSYS_BLOCK_HEIGHT * IMGU_VMEM1_Y_STRIDE)
		 */
		scaler->inp_buf_y_buffer_stride = IMGU_VMEM1_BUF_SIZE;
		scaler->inp_buf_u_st_addr = IMGU_VMEM1_INP_BUF_ADDR +
						IMGU_VMEM1_U_OFFSET;
		scaler->inp_buf_v_st_addr = IMGU_VMEM1_INP_BUF_ADDR +
						IMGU_VMEM1_V_OFFSET;
		scaler->inp_buf_uv_line_stride = IMGU_VMEM1_UV_STRIDE;
		scaler->inp_buf_uv_buffer_stride = IMGU_VMEM1_BUF_SIZE;
		scaler->inp_buf_chunk_width = stripe_params[s].chunk_width;
		scaler->inp_buf_nr_buffers = IMGU_OSYS_NUM_INPUT_BUFFERS;

		/* Output buffers */
		scaler->out_buf_y_st_addr = IMGU_VMEM1_INT_BUF_ADDR;
		scaler->out_buf_y_line_stride = stripe_params[s].block_width /
						IMGU_VMEM1_ELEMS_PER_VEC;
		scaler->out_buf_y_buffer_stride = IMGU_VMEM1_BUF_SIZE;
		scaler->out_buf_u_st_addr = IMGU_VMEM1_INT_BUF_ADDR +
						IMGU_VMEM1_U_OFFSET;
		scaler->out_buf_v_st_addr = IMGU_VMEM1_INT_BUF_ADDR +
						IMGU_VMEM1_V_OFFSET;
		scaler->out_buf_uv_line_stride = stripe_params[s].block_width /
						IMGU_VMEM1_ELEMS_PER_VEC / 2;
		scaler->out_buf_uv_buffer_stride = IMGU_VMEM1_BUF_SIZE;
		scaler->out_buf_nr_buffers = IMGU_OSYS_NUM_INTERM_BUFFERS;

		/* Intermediate buffers */
		scaler->int_buf_y_st_addr = IMGU_VMEM2_BUF_Y_ADDR;
		scaler->int_buf_y_line_stride = IMGU_VMEM2_BUF_Y_STRIDE;
		scaler->int_buf_u_st_addr = IMGU_VMEM2_BUF_U_ADDR;
		scaler->int_buf_v_st_addr = IMGU_VMEM2_BUF_V_ADDR;
		scaler->int_buf_uv_line_stride = IMGU_VMEM2_BUF_UV_STRIDE;
		scaler->int_buf_height = IMGU_VMEM2_LINES_PER_BLOCK;
		scaler->int_buf_chunk_width = stripe_params[s].chunk_height;
		scaler->int_buf_chunk_height = stripe_params[s].block_width;

		/* Context buffers */
		scaler->ctx_buf_hor_y_st_addr = IMGU_VMEM3_HOR_Y_ADDR;
		scaler->ctx_buf_hor_u_st_addr = IMGU_VMEM3_HOR_U_ADDR;
		scaler->ctx_buf_hor_v_st_addr = IMGU_VMEM3_HOR_V_ADDR;
		scaler->ctx_buf_ver_y_st_addr = IMGU_VMEM3_VER_Y_ADDR;
		scaler->ctx_buf_ver_u_st_addr = IMGU_VMEM3_VER_U_ADDR;
		scaler->ctx_buf_ver_v_st_addr = IMGU_VMEM3_VER_V_ADDR;

		/* Addresses for release-input and process-output tokens */
		scaler->release_inp_buf_addr = fifo_addr_ack;
		scaler->release_inp_buf_en = 1;
		scaler->release_out_buf_en = 1;
		scaler->process_out_buf_addr = fifo_addr_fmt;

		/* Settings dimensions, padding, cropping */
		scaler->input_image_y_width = stripe_params[s].input_width;
		scaler->input_image_y_height = stripe_params[s].input_height;
		scaler->input_image_y_start_column =
					stripe_params[s].start_column_y;
		scaler->input_image_uv_start_column =
					stripe_params[s].start_column_uv;
		scaler->input_image_y_left_pad = stripe_params[s].pad_left_y;
		scaler->input_image_uv_left_pad = stripe_params[s].pad_left_uv;
		scaler->input_image_y_right_pad = stripe_params[s].pad_right_y;
		scaler->input_image_uv_right_pad =
					stripe_params[s].pad_right_uv;
		scaler->input_image_y_top_pad = stripe_params[s].pad_top_y;
		scaler->input_image_uv_top_pad = stripe_params[s].pad_top_uv;
		scaler->input_image_y_bottom_pad =
					stripe_params[s].pad_bottom_y;
		scaler->input_image_uv_bottom_pad =
					stripe_params[s].pad_bottom_uv;
		scaler->processing_mode = stripe_params[s].processing_mode;
		scaler->scaling_ratio = stripe_params[s].phase_step;
		scaler->y_left_phase_init = stripe_params[s].phase_init_left_y;
		scaler->uv_left_phase_init =
					stripe_params[s].phase_init_left_uv;
		scaler->y_top_phase_init = stripe_params[s].phase_init_top_y;
		scaler->uv_top_phase_init = stripe_params[s].phase_init_top_uv;
		scaler->coeffs_exp_shift = stripe_params[s].exp_shift;
		scaler->out_y_left_crop = stripe_params[s].crop_left_y;
		scaler->out_uv_left_crop = stripe_params[s].crop_left_uv;
		scaler->out_y_top_crop = stripe_params[s].crop_top_y;
		scaler->out_uv_top_crop = stripe_params[s].crop_top_uv;

		for (pin = 0; pin < IMGU_ABI_OSYS_PINS; pin++) {
			int in_fifo_addr;
			int out_fifo_addr;
			int block_width_vecs;
			int input_width_s;
			int input_width_vecs;
			int input_buf_y_st_addr;
			int input_buf_u_st_addr;
			int input_buf_v_st_addr;
			int input_buf_y_line_stride;
			int input_buf_uv_line_stride;
			int output_buf_y_line_stride;
			int output_buf_uv_line_stride;
			int output_buf_nr_y_lines;
			int block_height;
			int block_width;
			struct imgu_abi_osys_frame_params *fr_pr;

			fr_pr = &osys->frame[pin].param;

			/* Frame parameters */
			fr_pr->enable = frame_params[pin].enable;
			fr_pr->format = frame_params[pin].format;
			fr_pr->mirror = frame_params[pin].mirror;
			fr_pr->flip = frame_params[pin].flip;
			fr_pr->tiling = frame_params[pin].tiling;
			fr_pr->width = frame_params[pin].width;
			fr_pr->height = frame_params[pin].height;
			fr_pr->stride = frame_params[pin].stride;
			fr_pr->scaled = frame_params[pin].scaled;

			/* Stripe parameters */
			osys->stripe[s].crop_top[pin] =
				frame_params[pin].crop_top;
			osys->stripe[s].input_width =
				stripe_params[s].input_width;
			osys->stripe[s].input_height =
				stripe_params[s].input_height;
			osys->stripe[s].block_height =
				stripe_params[s].block_height;
			osys->stripe[s].block_width =
				stripe_params[s].block_width;
			osys->stripe[s].output_width[pin] =
				stripe_params[s].output_width[pin];
			osys->stripe[s].output_height[pin] =
				stripe_params[s].output_height[pin];

			if (s == 0) {
				/* Only first stripe should do left cropping */
				osys->stripe[s].crop_left[pin] =
					frame_params[pin].crop_left;
				osys->stripe[s].output_offset[pin] =
					stripe_params[s].output_offset[pin];
			} else {
				/*
				 * Stripe offset for other strips should be
				 * adjusted according to the cropping done
				 * at the first strip
				 */
				osys->stripe[s].crop_left[pin] = 0;
				osys->stripe[s].output_offset[pin] =
					(stripe_params[s].output_offset[pin] -
					 osys->stripe[0].crop_left[pin]);
			}

			if (!frame_params[pin].enable)
				continue;

			/* Formatter: configurations */

			/*
			 * Get the dimensions of the input blocks of the
			 * formatter, which is the same as the output
			 * blocks of the scaler.
			 */
			if (frame_params[pin].scaled) {
				block_height = stripe_params[s].block_height;
				block_width = stripe_params[s].block_width;
			} else {
				block_height = IMGU_OSYS_BLOCK_HEIGHT;
				block_width = IMGU_OSYS_BLOCK_WIDTH;
			}
			block_width_vecs =
					block_width / IMGU_VMEM1_ELEMS_PER_VEC;
			/*
			 * The input/output line stride depends on the
			 * block size.
			 */
			input_buf_y_line_stride = block_width_vecs;
			input_buf_uv_line_stride = block_width_vecs / 2;
			output_buf_y_line_stride = block_width_vecs;
			output_buf_uv_line_stride = block_width_vecs / 2;
			output_buf_nr_y_lines = block_height;
			if (frame_params[pin].format ==
			    IMGU_ABI_OSYS_FORMAT_NV12 ||
			    frame_params[pin].format ==
			    IMGU_ABI_OSYS_FORMAT_NV21)
				output_buf_uv_line_stride =
					output_buf_y_line_stride;

			/*
			 * Tiled outputs use a different output buffer
			 * configuration. The input (= scaler output) block
			 * width translates to a tile height, and the block
			 * height to the tile width. The default block size of
			 * 128x32 maps exactly onto a 4kB tile (512x8) for Y.
			 * For UV, the tile width is always half.
			 */
			if (frame_params[pin].tiling) {
				output_buf_nr_y_lines = 8;
				output_buf_y_line_stride = 512 /
					IMGU_VMEM1_ELEMS_PER_VEC;
				output_buf_uv_line_stride = 256 /
					IMGU_VMEM1_ELEMS_PER_VEC;
			}

			/*
			 * Store the output buffer line stride. Will be
			 * used to compute buffer offsets in boundary
			 * conditions when output blocks are partially
			 * outside the image.
			 */
			osys->stripe[s].buf_stride[pin] =
				output_buf_y_line_stride *
				IMGU_HIVE_OF_SYS_OF_SYSTEM_NWAYS;
			if (frame_params[pin].scaled) {
				/*
				 * The input buffs are the intermediate
				 * buffers (scalers' output)
				 */
				input_buf_y_st_addr = IMGU_VMEM1_INT_BUF_ADDR;
				input_buf_u_st_addr = IMGU_VMEM1_INT_BUF_ADDR +
							IMGU_VMEM1_U_OFFSET;
				input_buf_v_st_addr = IMGU_VMEM1_INT_BUF_ADDR +
							IMGU_VMEM1_V_OFFSET;
			} else {
				/*
				 * The input bufferss are the buffers
				 * filled by the SP
				 */
				input_buf_y_st_addr = IMGU_VMEM1_INP_BUF_ADDR;
				input_buf_u_st_addr = IMGU_VMEM1_INP_BUF_ADDR +
							IMGU_VMEM1_U_OFFSET;
				input_buf_v_st_addr = IMGU_VMEM1_INP_BUF_ADDR +
							IMGU_VMEM1_V_OFFSET;
			}

			/*
			 * The formatter input width must be rounded to
			 * the block width. Otherwise the formatter will
			 * not recognize the end of the line, resulting
			 * in incorrect tiling (system may hang!) and
			 * possibly other problems.
			 */
			input_width_s =
				roundup(stripe_params[s].output_width[pin],
					block_width);
			input_width_vecs = input_width_s /
					IMGU_VMEM1_ELEMS_PER_VEC;
			out_fifo_addr = IMGU_FIFO_ADDR_FMT_TO_SP;
			/*
			 * Process-output tokens must be sent to the SP.
			 * When scaling, the release-input tokens can be
			 * sent directly to the scaler, otherwise the
			 * formatter should send them to the SP.
			 */
			if (frame_params[pin].scaled)
				in_fifo_addr = IMGU_FIFO_ADDR_FMT_TO_SCALER;
			else
				in_fifo_addr = IMGU_FIFO_ADDR_FMT_TO_SP;

			/* Formatter */
			param = &osys->formatter[s][pin].param;

			param->format = frame_params[pin].format;
			param->flip = frame_params[pin].flip;
			param->mirror = frame_params[pin].mirror;
			param->tiling = frame_params[pin].tiling;
			param->reduce_range = frame_params[pin].reduce_range;
			param->alpha_blending = 0;
			param->release_inp_addr = in_fifo_addr;
			param->release_inp_en = 1;
			param->process_out_buf_addr = out_fifo_addr;
			param->image_width_vecs = input_width_vecs;
			param->image_height_lines =
				stripe_params[s].output_height[pin];
			param->inp_buff_y_st_addr = input_buf_y_st_addr;
			param->inp_buff_y_line_stride = input_buf_y_line_stride;
			param->inp_buff_y_buffer_stride = IMGU_VMEM1_BUF_SIZE;
			param->int_buff_u_st_addr = input_buf_u_st_addr;
			param->int_buff_v_st_addr = input_buf_v_st_addr;
			param->inp_buff_uv_line_stride =
				input_buf_uv_line_stride;
			param->inp_buff_uv_buffer_stride = IMGU_VMEM1_BUF_SIZE;
			param->out_buff_level = 0;
			param->out_buff_nr_y_lines = output_buf_nr_y_lines;
			param->out_buff_u_st_offset = IMGU_VMEM1_U_OFFSET;
			param->out_buff_v_st_offset = IMGU_VMEM1_V_OFFSET;
			param->out_buff_y_line_stride =
				output_buf_y_line_stride;
			param->out_buff_uv_line_stride =
				output_buf_uv_line_stride;
			param->hist_buff_st_addr = IMGU_VMEM1_HST_BUF_ADDR;
			param->hist_buff_line_stride =
				IMGU_VMEM1_HST_BUF_STRIDE;
			param->hist_buff_nr_lines = IMGU_VMEM1_HST_BUF_NLINES;
		}
	}

	block_stripes[0].offset = 0;
	if (stripes <= 1) {
		block_stripes[0].width = stripe_params[0].input_width;
		block_stripes[0].height = stripe_params[0].input_height;
	} else {
		struct imgu_fw_info *bi =
			&css->fwp->binary_header[css_pipe->bindex];
		unsigned int sp_block_width =
				bi->info.isp.sp.block.block_width *
				IPU3_UAPI_ISP_VEC_ELEMS;

		block_stripes[0].width = roundup(stripe_params[0].input_width,
						 sp_block_width);
		block_stripes[1].offset =
			rounddown(css_pipe->rect[IPU3_CSS_RECT_GDC].width -
				  stripe_params[1].input_width, sp_block_width);
		block_stripes[1].width =
			roundup(css_pipe->rect[IPU3_CSS_RECT_GDC].width -
				block_stripes[1].offset, sp_block_width);
		block_stripes[0].height = css_pipe->rect[IPU3_CSS_RECT_GDC].height;
		block_stripes[1].height = block_stripes[0].height;
	}

	return 0;
}

/*********************** Mostly 3A operations ******************************/

/*
 * This function creates a "TO-DO list" (operations) for the sp code.
 *
 * There are 2 types of operations:
 * 1. Transfer: Issue DMA transfer request for copying grid cells from DDR to
 *    accelerator space (NOTE that this space is limited) associated data:
 *    DDR address + accelerator's config set index(acc's address).
 *
 * 2. Issue "Process Lines Command" to shd accelerator
 *    associated data: #lines + which config set to use (actually, accelerator
 *    will use x AND (x+1)%num_of_sets - NOTE that this implies the restriction
 *    of not touching config sets x & (x+1)%num_of_sets when process_lines(x)
 *    is active).
 *
 * Basically there are 2 types of operations "chunks":
 * 1. "initial chunk": Initially, we do as much transfers as we can (and need)
 *    [0 - max sets(3) ] followed by 1 or 2 "process lines" operations.
 *
 * 2. "regular chunk" - 1 transfer followed by 1 process line operation.
 *    (in some cases we might need additional transfer ate the last chunk).
 *
 * for some case:
 * --> init
 *	tr (0)
 *	tr (1)
 *	tr (2)
 *	pl (0)
 *	pl (1)
 * --> ack (0)
 *	tr (3)
 *	pl (2)
 * --> ack (1)
 *	pl (3)
 * --> ack (2)
 *	do nothing
 * --> ack (3)
 *	do nothing
 */

static int
ipu3_css_shd_ops_calc(struct imgu_abi_shd_intra_frame_operations_data *ops,
		      const struct ipu3_uapi_shd_grid_config *grid,
		      unsigned int image_height)
{
	unsigned int block_height = 1 << grid->block_height_log2;
	unsigned int grid_height_per_slice = grid->grid_height_per_slice;
	unsigned int set_height = grid_height_per_slice * block_height;

	/* We currently support only abs(y_start) > grid_height_per_slice */
	unsigned int positive_y_start = (unsigned int)-grid->y_start;
	unsigned int first_process_lines =
				set_height - (positive_y_start % set_height);
	unsigned int last_set_height;
	unsigned int num_of_sets;

	struct imgu_abi_acc_operation *p_op;
	struct imgu_abi_acc_process_lines_cmd_data *p_pl;
	struct imgu_abi_shd_transfer_luts_set_data *p_tr;

	unsigned int op_idx, pl_idx, tr_idx;
	unsigned char tr_set_num, pl_cfg_set;

	/*
	 * When the number of lines for the last process lines command
	 * is equal to a set height, we need another line of grid cell -
	 * additional transfer is required.
	 */
	unsigned char last_tr = 0;

	/* Add "process lines" command to the list of operations */
	bool add_pl;
	/* Add DMA xfer (config set) command to the list of ops */
	bool add_tr;

	/*
	 * Available partial grid (the part that fits into #IMGU_SHD_SETS sets)
	 * doesn't cover whole frame - need to process in chunks
	 */
	if (image_height > first_process_lines) {
		last_set_height =
			(image_height - first_process_lines) % set_height;
		num_of_sets = last_set_height > 0 ?
			(image_height - first_process_lines) / set_height + 2 :
			(image_height - first_process_lines) / set_height + 1;
		last_tr = (set_height - last_set_height <= block_height ||
			   last_set_height == 0) ? 1 : 0;
	} else { /* partial grid covers whole frame */
		last_set_height = 0;
		num_of_sets = 1;
		first_process_lines = image_height;
		last_tr = set_height - image_height <= block_height ? 1 : 0;
	}

	/* Init operations lists and counters */
	p_op = ops->operation_list;
	op_idx = 0;
	p_pl = ops->process_lines_data;
	pl_idx = 0;
	p_tr = ops->transfer_data;
	tr_idx = 0;

	memset(ops, 0, sizeof(*ops));

	/* Cyclic counters that holds config set number [0,IMGU_SHD_SETS) */
	tr_set_num = 0;
	pl_cfg_set = 0;

	/*
	 * Always start with a transfer - process lines command must be
	 * initiated only after appropriate config sets are in place
	 * (2 configuration sets per process line command, except for last one).
	 */
	add_pl = false;
	add_tr = true;

	while (add_pl || add_tr) {
		/* Transfer ops */
		if (add_tr) {
			if (op_idx >= IMGU_ABI_SHD_MAX_OPERATIONS ||
			    tr_idx >= IMGU_ABI_SHD_MAX_TRANSFERS)
				return -EINVAL;
			p_op[op_idx].op_type =
				IMGU_ABI_ACC_OPTYPE_TRANSFER_DATA;
			p_op[op_idx].op_indicator = IMGU_ABI_ACC_OP_IDLE;
			op_idx++;
			p_tr[tr_idx].set_number = tr_set_num;
			tr_idx++;
			tr_set_num = (tr_set_num + 1) % IMGU_SHD_SETS;
		}

		/* Process-lines ops */
		if (add_pl) {
			if (op_idx >= IMGU_ABI_SHD_MAX_OPERATIONS ||
			    pl_idx >= IMGU_ABI_SHD_MAX_PROCESS_LINES)
				return -EINVAL;
			p_op[op_idx].op_type =
				IMGU_ABI_ACC_OPTYPE_PROCESS_LINES;

			/*
			 * In case we have 2 process lines commands -
			 * don't stop after the first one
			 */
			if (pl_idx == 0 && num_of_sets != 1)
				p_op[op_idx].op_indicator =
					IMGU_ABI_ACC_OP_IDLE;
			/*
			 * Initiate last process lines command -
			 * end of operation list.
			 */
			else if (pl_idx == num_of_sets - 1)
				p_op[op_idx].op_indicator =
					IMGU_ABI_ACC_OP_END_OF_OPS;
			/*
			 * Intermediate process line command - end of operation
			 * "chunk" (meaning few "transfers" followed by few
			 * "process lines" commands).
			 */
			else
				p_op[op_idx].op_indicator =
					IMGU_ABI_ACC_OP_END_OF_ACK;

			op_idx++;

			/* first process line operation */
			if (pl_idx == 0)
				p_pl[pl_idx].lines = first_process_lines;
			/* Last process line operation */
			else if (pl_idx == num_of_sets - 1 &&
				 last_set_height > 0)
				p_pl[pl_idx].lines = last_set_height;
			else	/* "regular" process lines operation */
				p_pl[pl_idx].lines = set_height;

			p_pl[pl_idx].cfg_set = pl_cfg_set;
			pl_idx++;
			pl_cfg_set = (pl_cfg_set + 1) % IMGU_SHD_SETS;
		}

		/*
		 * Initially, we always transfer
		 * min(IMGU_SHD_SETS, num_of_sets) - after that we fill in the
		 * corresponding process lines commands.
		 */
		if (tr_idx == IMGU_SHD_SETS ||
		    tr_idx == num_of_sets + last_tr) {
			add_tr = false;
			add_pl = true;
		}

		/*
		 * We have finished the "initial" operations chunk -
		 * be ready to get more chunks.
		 */
		if (pl_idx == 2) {
			add_tr = true;
			add_pl = true;
		}

		/* Stop conditions for each operation type */
		if (tr_idx == num_of_sets + last_tr)
			add_tr = false;
		if (pl_idx == num_of_sets)
			add_pl = false;
	}

	return 0;
}

/*
 * The follow handshake procotol is the same for AF, AWB and AWB FR.
 *
 * for n sets of meta-data, the flow is:
 * --> init
 *  process-lines  (0)
 *  process-lines  (1)	 eoc
 *  --> ack (0)
 *  read-meta-data (0)
 *  process-lines  (2)	 eoc
 *  --> ack (1)
 *  read-meta-data (1)
 *  process-lines  (3)	 eoc
 *  ...
 *
 *  --> ack (n-3)
 *  read-meta-data (n-3)
 *  process-lines  (n-1) eoc
 *  --> ack (n-2)
 *  read-meta-data (n-2) eoc
 *  --> ack (n-1)
 *  read-meta-data (n-1) eof
 *
 * for 2 sets we get:
 * --> init
 * pl (0)
 * pl (1) eoc
 * --> ack (0)
 * pl (2) - rest of image, if applicable)
 * rmd (0) eoc
 * --> ack (1)
 * rmd (1) eof
 * --> (ack (2))
 * do nothing
 *
 * for only one set:
 *
 * --> init
 * pl(0)   eoc
 * --> ack (0)
 * rmd (0) eof
 *
 * grid smaller than image case
 * for example 128x128 grid (block size 8x8, 16x16 num of blocks)
 * start at (0,0)
 * 1st set holds 160 cells - 10 blocks vertical, 16 horizontal
 * => 1st process lines = 80
 * we're left with 128-80=48 lines (6 blocks vertical)
 * => 2nd process lines = 48
 * last process lines to cover the image - image_height - 128
 *
 * --> init
 * pl (0) first
 * pl (1) last-in-grid
 * --> ack (0)
 * rmd (0)
 * pl (2) after-grid
 * --> ack (1)
 * rmd (1) eof
 * --> ack (2)
 * do nothing
 */
struct process_lines {
	unsigned int image_height;
	unsigned short grid_height;
	unsigned short block_height;
	unsigned short y_start;
	unsigned char grid_height_per_slice;

	unsigned short max_op; /* max operation */
	unsigned short max_tr; /* max transaction */
	unsigned char acc_enable;
};

/* Helper to config intra_frame_operations_data. */
static int
ipu3_css_acc_process_lines(const struct process_lines *pl,
			   struct imgu_abi_acc_operation *p_op,
			   struct imgu_abi_acc_process_lines_cmd_data *p_pl,
			   struct imgu_abi_acc_transfer_op_data *p_tr)
{
	unsigned short op_idx = 0, pl_idx = 0, tr_idx = 0;
	unsigned char tr_set_num = 0, pl_cfg_set = 0;
	const unsigned short grid_last_line =
			pl->y_start + pl->grid_height * pl->block_height;
	const unsigned short process_lines =
			pl->grid_height_per_slice * pl->block_height;

	unsigned int process_lines_after_grid;
	unsigned short first_process_lines;
	unsigned short last_process_lines_in_grid;

	unsigned short num_of_process_lines;
	unsigned short num_of_sets;

	if (pl->grid_height_per_slice == 0)
		return -EINVAL;

	if (pl->acc_enable && grid_last_line > pl->image_height)
		return -EINVAL;

	num_of_sets = pl->grid_height / pl->grid_height_per_slice;
	if (num_of_sets * pl->grid_height_per_slice < pl->grid_height)
		num_of_sets++;

	/* Account for two line delay inside the FF */
	if (pl->max_op == IMGU_ABI_AF_MAX_OPERATIONS) {
		first_process_lines = process_lines + pl->y_start + 2;
		last_process_lines_in_grid =
			(grid_last_line - first_process_lines) -
			((num_of_sets - 2) * process_lines) + 4;
		process_lines_after_grid =
			pl->image_height - grid_last_line - 4;
	} else {
		first_process_lines = process_lines + pl->y_start;
		last_process_lines_in_grid =
			(grid_last_line - first_process_lines) -
			((num_of_sets - 2) * process_lines);
		process_lines_after_grid = pl->image_height - grid_last_line;
	}

	num_of_process_lines = num_of_sets;
	if (process_lines_after_grid > 0)
		num_of_process_lines++;

	while (tr_idx < num_of_sets || pl_idx < num_of_process_lines) {
		/* Read meta-data */
		if (pl_idx >= 2 || (pl_idx == 1 && num_of_sets == 1)) {
			if (op_idx >= pl->max_op || tr_idx >= pl->max_tr)
				return -EINVAL;

			p_op[op_idx].op_type =
				IMGU_ABI_ACC_OPTYPE_TRANSFER_DATA;

			if (tr_idx == num_of_sets - 1)
				/* The last operation is always a tr */
				p_op[op_idx].op_indicator =
					IMGU_ABI_ACC_OP_END_OF_OPS;
			else if (tr_idx == num_of_sets - 2)
				if (process_lines_after_grid == 0)
					/*
					 * No additional pl op left -
					 * this op is left as lats of cycle
					 */
					p_op[op_idx].op_indicator =
						IMGU_ABI_ACC_OP_END_OF_ACK;
				else
					/*
					 * We still have to process-lines after
					 * the grid so have one more pl op
					 */
					p_op[op_idx].op_indicator =
						IMGU_ABI_ACC_OP_IDLE;
			else
				/* Default - usually there's a pl after a tr */
				p_op[op_idx].op_indicator =
					IMGU_ABI_ACC_OP_IDLE;

			op_idx++;
			if (p_tr) {
				p_tr[tr_idx].set_number = tr_set_num;
				tr_set_num = 1 - tr_set_num;
			}
			tr_idx++;
		}

		/* process_lines */
		if (pl_idx < num_of_process_lines) {
			if (op_idx >= pl->max_op || pl_idx >= pl->max_tr)
				return -EINVAL;

			p_op[op_idx].op_type =
				IMGU_ABI_ACC_OPTYPE_PROCESS_LINES;
			if (pl_idx == 0)
				if (num_of_process_lines == 1)
					/* Only one pl op */
					p_op[op_idx].op_indicator =
						IMGU_ABI_ACC_OP_END_OF_ACK;
				else
					/* On init - do two pl ops */
					p_op[op_idx].op_indicator =
						IMGU_ABI_ACC_OP_IDLE;
			else
				/* Usually pl is the end of the ack cycle */
				p_op[op_idx].op_indicator =
					IMGU_ABI_ACC_OP_END_OF_ACK;

			op_idx++;

			if (pl_idx == 0)
				/* First process line */
				p_pl[pl_idx].lines = first_process_lines;
			else if (pl_idx == num_of_sets - 1)
				/* Last in grid */
				p_pl[pl_idx].lines = last_process_lines_in_grid;
			else if (pl_idx == num_of_process_lines - 1)
				/* After the grid */
				p_pl[pl_idx].lines = process_lines_after_grid;
			else
				/* Inside the grid */
				p_pl[pl_idx].lines = process_lines;

			if (p_tr) {
				p_pl[pl_idx].cfg_set = pl_cfg_set;
				pl_cfg_set = 1 - pl_cfg_set;
			}
			pl_idx++;
		}
	}

	return 0;
}

static int ipu3_css_af_ops_calc(struct ipu3_css *css, unsigned int pipe,
				struct imgu_abi_af_config *af_config)
{
	struct imgu_abi_af_intra_frame_operations_data *to =
		&af_config->operations_data;
	struct ipu3_css_pipe *css_pipe = &css->pipes[pipe];
	struct imgu_fw_info *bi =
		&css->fwp->binary_header[css_pipe->bindex];

	struct process_lines pl = {
		.image_height = css_pipe->rect[IPU3_CSS_RECT_BDS].height,
		.grid_height = af_config->config.grid_cfg.height,
		.block_height =
			1 << af_config->config.grid_cfg.block_height_log2,
		.y_start = af_config->config.grid_cfg.y_start &
			IPU3_UAPI_GRID_START_MASK,
		.grid_height_per_slice =
			af_config->stripes[0].grid_cfg.height_per_slice,
		.max_op = IMGU_ABI_AF_MAX_OPERATIONS,
		.max_tr = IMGU_ABI_AF_MAX_TRANSFERS,
		.acc_enable = bi->info.isp.sp.enable.af,
	};

	return ipu3_css_acc_process_lines(&pl, to->ops, to->process_lines_data,
					  NULL);
}

static int
ipu3_css_awb_fr_ops_calc(struct ipu3_css *css, unsigned int pipe,
			 struct imgu_abi_awb_fr_config *awb_fr_config)
{
	struct imgu_abi_awb_fr_intra_frame_operations_data *to =
		&awb_fr_config->operations_data;
	struct ipu3_css_pipe *css_pipe = &css->pipes[pipe];
	struct imgu_fw_info *bi =
		&css->fwp->binary_header[css_pipe->bindex];
	struct process_lines pl = {
		.image_height = css_pipe->rect[IPU3_CSS_RECT_BDS].height,
		.grid_height = awb_fr_config->config.grid_cfg.height,
		.block_height =
			1 << awb_fr_config->config.grid_cfg.block_height_log2,
		.y_start = awb_fr_config->config.grid_cfg.y_start &
			IPU3_UAPI_GRID_START_MASK,
		.grid_height_per_slice =
			awb_fr_config->stripes[0].grid_cfg.height_per_slice,
		.max_op = IMGU_ABI_AWB_FR_MAX_OPERATIONS,
		.max_tr = IMGU_ABI_AWB_FR_MAX_PROCESS_LINES,
		.acc_enable = bi->info.isp.sp.enable.awb_fr_acc,
	};

	return ipu3_css_acc_process_lines(&pl, to->ops, to->process_lines_data,
					  NULL);
}

static int ipu3_css_awb_ops_calc(struct ipu3_css *css, unsigned int pipe,
				 struct imgu_abi_awb_config *awb_config)
{
	struct imgu_abi_awb_intra_frame_operations_data *to =
		&awb_config->operations_data;
	struct ipu3_css_pipe *css_pipe = &css->pipes[pipe];
	struct imgu_fw_info *bi =
		&css->fwp->binary_header[css_pipe->bindex];

	struct process_lines pl = {
		.image_height = css_pipe->rect[IPU3_CSS_RECT_BDS].height,
		.grid_height = awb_config->config.grid.height,
		.block_height =
			1 << awb_config->config.grid.block_height_log2,
		.y_start = awb_config->config.grid.y_start,
		.grid_height_per_slice =
			awb_config->stripes[0].grid.height_per_slice,
		.max_op = IMGU_ABI_AWB_MAX_OPERATIONS,
		.max_tr = IMGU_ABI_AWB_MAX_TRANSFERS,
		.acc_enable = bi->info.isp.sp.enable.awb_acc,
	};

	return ipu3_css_acc_process_lines(&pl, to->ops, to->process_lines_data,
					  to->transfer_data);
}

static u16 ipu3_css_grid_end(u16 start, u8 width, u8 block_width_log2)
{
	return (start & IPU3_UAPI_GRID_START_MASK) +
		(width << block_width_log2) - 1;
}

static void ipu3_css_grid_end_calc(struct ipu3_uapi_grid_config *grid_cfg)
{
	grid_cfg->x_end = ipu3_css_grid_end(grid_cfg->x_start, grid_cfg->width,
					    grid_cfg->block_width_log2);
	grid_cfg->y_end = ipu3_css_grid_end(grid_cfg->y_start, grid_cfg->height,
					    grid_cfg->block_height_log2);
}

/****************** config computation *****************************/

static int ipu3_css_cfg_acc_stripe(struct ipu3_css *css, unsigned int pipe,
				   struct imgu_abi_acc_param *acc)
{
	struct ipu3_css_pipe *css_pipe = &css->pipes[pipe];
	const struct imgu_fw_info *bi =
		&css->fwp->binary_header[css_pipe->bindex];
	struct ipu3_css_scaler_info scaler_luma, scaler_chroma;
	const unsigned int stripes = bi->info.isp.sp.iterator.num_stripes;
	const unsigned int f = IPU3_UAPI_ISP_VEC_ELEMS * 2;
	unsigned int bds_ds, i;

	memset(acc, 0, sizeof(*acc));

	/* acc_param: osys_config */

	if (ipu3_css_osys_calc(css, pipe, stripes, &acc->osys, &scaler_luma,
			       &scaler_chroma, acc->stripe.block_stripes))
		return -EINVAL;

	/* acc_param: stripe data */

	/*
	 * For the striped case the approach is as follows:
	 * 1. down-scaled stripes are calculated - with 128 overlap
	 *    (this is the main limiter therefore it's first)
	 * 2. input stripes are derived by up-scaling the down-scaled stripes
	 *    (there are no alignment requirements on input stripes)
	 * 3. output stripes are derived from down-scaled stripes too
	 */

	acc->stripe.num_of_stripes = stripes;
	acc->stripe.input_frame.width =
		css_pipe->queue[IPU3_CSS_QUEUE_IN].fmt.mpix.width;
	acc->stripe.input_frame.height =
		css_pipe->queue[IPU3_CSS_QUEUE_IN].fmt.mpix.height;
	acc->stripe.input_frame.bayer_order =
		css_pipe->queue[IPU3_CSS_QUEUE_IN].css_fmt->bayer_order;

	for (i = 0; i < stripes; i++)
		acc->stripe.bds_out_stripes[i].height =
					css_pipe->rect[IPU3_CSS_RECT_BDS].height;
	acc->stripe.bds_out_stripes[0].offset = 0;
	if (stripes <= 1) {
		acc->stripe.bds_out_stripes[0].width =
			ALIGN(css_pipe->rect[IPU3_CSS_RECT_BDS].width, f);
	} else {
		/* Image processing is divided into two stripes */
		acc->stripe.bds_out_stripes[0].width =
			acc->stripe.bds_out_stripes[1].width =
			(css_pipe->rect[IPU3_CSS_RECT_BDS].width / 2 & ~(f - 1)) + f;
		/*
		 * Sum of width of the two stripes should not be smaller
		 * than output width and must be even times of overlapping
		 * unit f.
		 */
		if ((css_pipe->rect[IPU3_CSS_RECT_BDS].width / f & 1) !=
		    !!(css_pipe->rect[IPU3_CSS_RECT_BDS].width & (f - 1)))
			acc->stripe.bds_out_stripes[0].width += f;
		if ((css_pipe->rect[IPU3_CSS_RECT_BDS].width / f & 1) &&
		    (css_pipe->rect[IPU3_CSS_RECT_BDS].width & (f - 1))) {
			acc->stripe.bds_out_stripes[0].width += f;
			acc->stripe.bds_out_stripes[1].width += f;
		}
		/* Overlap between stripes is IPU3_UAPI_ISP_VEC_ELEMS * 4 */
		acc->stripe.bds_out_stripes[1].offset =
			acc->stripe.bds_out_stripes[0].width - 2 * f;
	}

	acc->stripe.effective_stripes[0].height =
				css_pipe->rect[IPU3_CSS_RECT_EFFECTIVE].height;
	acc->stripe.effective_stripes[0].offset = 0;
	acc->stripe.bds_out_stripes_no_overlap[0].height =
				css_pipe->rect[IPU3_CSS_RECT_BDS].height;
	acc->stripe.bds_out_stripes_no_overlap[0].offset = 0;
	acc->stripe.output_stripes[0].height =
				css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.height;
	acc->stripe.output_stripes[0].offset = 0;
	if (stripes <= 1) {
		acc->stripe.down_scaled_stripes[0].width =
				css_pipe->rect[IPU3_CSS_RECT_BDS].width;
		acc->stripe.down_scaled_stripes[0].height =
				css_pipe->rect[IPU3_CSS_RECT_BDS].height;
		acc->stripe.down_scaled_stripes[0].offset = 0;

		acc->stripe.effective_stripes[0].width =
				css_pipe->rect[IPU3_CSS_RECT_EFFECTIVE].width;
		acc->stripe.bds_out_stripes_no_overlap[0].width =
			ALIGN(css_pipe->rect[IPU3_CSS_RECT_BDS].width, f);

		acc->stripe.output_stripes[0].width =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.width;
	} else { /* Two stripes */
		bds_ds = css_pipe->rect[IPU3_CSS_RECT_EFFECTIVE].width *
				IMGU_BDS_GRANULARITY /
				css_pipe->rect[IPU3_CSS_RECT_BDS].width;

		acc->stripe.down_scaled_stripes[0] =
			acc->stripe.bds_out_stripes[0];
		acc->stripe.down_scaled_stripes[1] =
			acc->stripe.bds_out_stripes[1];
		if (!IS_ALIGNED(css_pipe->rect[IPU3_CSS_RECT_BDS].width, f))
			acc->stripe.down_scaled_stripes[1].width +=
				(css_pipe->rect[IPU3_CSS_RECT_BDS].width
				& (f - 1)) - f;

		acc->stripe.effective_stripes[0].width = bds_ds *
			acc->stripe.down_scaled_stripes[0].width /
			IMGU_BDS_GRANULARITY;
		acc->stripe.effective_stripes[1].width = bds_ds *
			acc->stripe.down_scaled_stripes[1].width /
			IMGU_BDS_GRANULARITY;
		acc->stripe.effective_stripes[1].height =
			css_pipe->rect[IPU3_CSS_RECT_EFFECTIVE].height;
		acc->stripe.effective_stripes[1].offset = bds_ds *
			acc->stripe.down_scaled_stripes[1].offset /
			IMGU_BDS_GRANULARITY;

		acc->stripe.bds_out_stripes_no_overlap[0].width =
		acc->stripe.bds_out_stripes_no_overlap[1].offset =
			ALIGN(css_pipe->rect[IPU3_CSS_RECT_BDS].width, 2 * f) / 2;
		acc->stripe.bds_out_stripes_no_overlap[1].width =
			DIV_ROUND_UP(css_pipe->rect[IPU3_CSS_RECT_BDS].width, f)
			/ 2 * f;
		acc->stripe.bds_out_stripes_no_overlap[1].height =
			css_pipe->rect[IPU3_CSS_RECT_BDS].height;

		acc->stripe.output_stripes[0].width =
			acc->stripe.down_scaled_stripes[0].width - f;
		acc->stripe.output_stripes[1].width =
			acc->stripe.down_scaled_stripes[1].width - f;
		acc->stripe.output_stripes[1].height =
			css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.height;
		acc->stripe.output_stripes[1].offset =
			acc->stripe.output_stripes[0].width;
	}

	acc->stripe.output_system_in_frame_width =
		css_pipe->rect[IPU3_CSS_RECT_GDC].width;
	acc->stripe.output_system_in_frame_height =
		css_pipe->rect[IPU3_CSS_RECT_GDC].height;

	acc->stripe.effective_frame_width =
				css_pipe->rect[IPU3_CSS_RECT_EFFECTIVE].width;
	acc->stripe.bds_frame_width = css_pipe->rect[IPU3_CSS_RECT_BDS].width;
	acc->stripe.out_frame_width =
		css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.width;
	acc->stripe.out_frame_height =
		css_pipe->queue[IPU3_CSS_QUEUE_OUT].fmt.mpix.height;
	acc->stripe.gdc_in_buffer_width =
		css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].bytesperline /
		css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].bytesperpixel;
	acc->stripe.gdc_in_buffer_height =
		css_pipe->aux_frames[IPU3_CSS_AUX_FRAME_REF].height;
	acc->stripe.gdc_in_buffer_offset_x = IMGU_GDC_BUF_X;
	acc->stripe.gdc_in_buffer_offset_y = IMGU_GDC_BUF_Y;
	acc->stripe.display_frame_width =
		css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.width;
	acc->stripe.display_frame_height =
		css_pipe->queue[IPU3_CSS_QUEUE_VF].fmt.mpix.height;
	acc->stripe.bds_aligned_frame_width =
		roundup(css_pipe->rect[IPU3_CSS_RECT_BDS].width,
			2 * IPU3_UAPI_ISP_VEC_ELEMS);

	if (stripes > 1)
		acc->stripe.half_overlap_vectors =
			IMGU_STRIPE_FIXED_HALF_OVERLAP;
	else
		acc->stripe.half_overlap_vectors = 0;

	return 0;
}

static void ipu3_css_cfg_acc_dvs(struct ipu3_css *css,
				 struct imgu_abi_acc_param *acc,
				 unsigned int pipe)
{
	unsigned int i;
	struct ipu3_css_pipe *css_pipe = &css->pipes[pipe];

	/* Disable DVS statistics */
	acc->dvs_stat.operations_data.process_lines_data[0].lines =
				css_pipe->rect[IPU3_CSS_RECT_BDS].height;
	acc->dvs_stat.operations_data.process_lines_data[0].cfg_set = 0;
	acc->dvs_stat.operations_data.ops[0].op_type =
		IMGU_ABI_ACC_OPTYPE_PROCESS_LINES;
	acc->dvs_stat.operations_data.ops[0].op_indicator =
		IMGU_ABI_ACC_OP_NO_OPS;
	for (i = 0; i < IMGU_ABI_DVS_STAT_LEVELS; i++)
		acc->dvs_stat.cfg.grd_config[i].enable = 0;
}

static void acc_bds_per_stripe_data(struct ipu3_css *css,
				    struct imgu_abi_acc_param *acc,
				    const int i, unsigned int pipe)
{
	struct ipu3_css_pipe *css_pipe = &css->pipes[pipe];

	acc->bds.per_stripe.aligned_data[i].data.crop.hor_crop_en = 0;
	acc->bds.per_stripe.aligned_data[i].data.crop.hor_crop_start = 0;
	acc->bds.per_stripe.aligned_data[i].data.crop.hor_crop_end = 0;
	acc->bds.per_stripe.aligned_data[i].data.hor_ctrl0 =
		acc->bds.hor.hor_ctrl0;
	acc->bds.per_stripe.aligned_data[i].data.hor_ctrl0.out_frame_width =
		acc->stripe.down_scaled_stripes[i].width;
	acc->bds.per_stripe.aligned_data[i].data.ver_ctrl1.out_frame_width =
		acc->stripe.down_scaled_stripes[i].width;
	acc->bds.per_stripe.aligned_data[i].data.ver_ctrl1.out_frame_height =
		css_pipe->rect[IPU3_CSS_RECT_BDS].height;
}

/*
 * Configure `acc' parameters. `acc_old' contains the old values (or is NULL)
 * and `acc_user' contains new prospective values. `use' contains flags
 * telling which fields to take from the old values (or generate if it is NULL)
 * and which to take from the new user values.
 */
int ipu3_css_cfg_acc(struct ipu3_css *css, unsigned int pipe,
		     struct ipu3_uapi_flags *use,
		     struct imgu_abi_acc_param *acc,
		     struct imgu_abi_acc_param *acc_old,
		     struct ipu3_uapi_acc_param *acc_user)
{
	struct ipu3_css_pipe *css_pipe = &css->pipes[pipe];
	const struct imgu_fw_info *bi =
		&css->fwp->binary_header[css_pipe->bindex];
	const unsigned int stripes = bi->info.isp.sp.iterator.num_stripes;
	const unsigned int tnr_frame_width =
		acc->stripe.bds_aligned_frame_width;
	const unsigned int min_overlap = 10;
	const struct v4l2_pix_format_mplane *pixm =
		&css_pipe->queue[IPU3_CSS_QUEUE_IN].fmt.mpix;
	const struct ipu3_css_bds_config *cfg_bds;
	struct imgu_abi_input_feeder_data *feeder_data;

	unsigned int bds_ds, ofs_x, ofs_y, i, width, height;
	u8 b_w_log2; /* Block width log2 */

	/* Update stripe using chroma and luma */

	if (ipu3_css_cfg_acc_stripe(css, pipe, acc))
		return -EINVAL;

	/* acc_param: input_feeder_config */

	ofs_x = ((pixm->width -
		  css_pipe->rect[IPU3_CSS_RECT_EFFECTIVE].width) >> 1) & ~1;
	ofs_x += css_pipe->queue[IPU3_CSS_QUEUE_IN].css_fmt->bayer_order ==
		IMGU_ABI_BAYER_ORDER_RGGB ||
		css_pipe->queue[IPU3_CSS_QUEUE_IN].css_fmt->bayer_order ==
		IMGU_ABI_BAYER_ORDER_GBRG ? 1 : 0;
	ofs_y = ((pixm->height -
		  css_pipe->rect[IPU3_CSS_RECT_EFFECTIVE].height) >> 1) & ~1;
	ofs_y += css_pipe->queue[IPU3_CSS_QUEUE_IN].css_fmt->bayer_order ==
		IMGU_ABI_BAYER_ORDER_BGGR ||
		css_pipe->queue[IPU3_CSS_QUEUE_IN].css_fmt->bayer_order ==
		IMGU_ABI_BAYER_ORDER_GBRG ? 1 : 0;
	acc->input_feeder.data.row_stride = pixm->plane_fmt[0].bytesperline;
	acc->input_feeder.data.start_row_address =
		ofs_x / IMGU_PIXELS_PER_WORD * IMGU_BYTES_PER_WORD +
		ofs_y * acc->input_feeder.data.row_stride;
	acc->input_feeder.data.start_pixel = ofs_x % IMGU_PIXELS_PER_WORD;

	acc->input_feeder.data_per_stripe.input_feeder_data[0].data =
		acc->input_feeder.data;

	ofs_x += acc->stripe.effective_stripes[1].offset;

	feeder_data =
		&acc->input_feeder.data_per_stripe.input_feeder_data[1].data;
	feeder_data->row_stride = acc->input_feeder.data.row_stride;
	feeder_data->start_row_address =
		ofs_x / IMGU_PIXELS_PER_WORD * IMGU_BYTES_PER_WORD +
		ofs_y * acc->input_feeder.data.row_stride;
	feeder_data->start_pixel = ofs_x % IMGU_PIXELS_PER_WORD;

	/* acc_param: bnr_static_config */

	/*
	 * Originate from user or be the original default values if user has
	 * never set them before, when user gives a new set of parameters,
	 * for each chunk in the parameter structure there is a flag use->xxx
	 * whether to use the user-provided parameter or not. If not, the
	 * parameter remains unchanged in the driver:
	 * it's value is taken from acc_old.
	 */
	if (use && use->acc_bnr) {
		/* Take values from user */
		acc->bnr = acc_user->bnr;
	} else if (acc_old) {
		/* Use old value */
		acc->bnr = acc_old->bnr;
	} else {
		/* Calculate from scratch */
		acc->bnr = ipu3_css_bnr_defaults;
	}

	acc->bnr.column_size = tnr_frame_width;

	/* acc_param: bnr_static_config_green_disparity */

	if (use && use->acc_green_disparity) {
		/* Take values from user */
		acc->green_disparity = acc_user->green_disparity;
	} else if (acc_old) {
		/* Use old value */
		acc->green_disparity = acc_old->green_disparity;
	} else {
		/* Calculate from scratch */
		memset(&acc->green_disparity, 0, sizeof(acc->green_disparity));
	}

	/* acc_param: dm_config */

	if (use && use->acc_dm) {
		/* Take values from user */
		acc->dm = acc_user->dm;
	} else if (acc_old) {
		/* Use old value */
		acc->dm = acc_old->dm;
	} else {
		/* Calculate from scratch */
		acc->dm = ipu3_css_dm_defaults;
	}

	acc->dm.frame_width = tnr_frame_width;

	/* acc_param: ccm_mat_config */

	if (use && use->acc_ccm) {
		/* Take values from user */
		acc->ccm = acc_user->ccm;
	} else if (acc_old) {
		/* Use old value */
		acc->ccm = acc_old->ccm;
	} else {
		/* Calculate from scratch */
		acc->ccm = ipu3_css_ccm_defaults;
	}

	/* acc_param: gamma_config */

	if (use && use->acc_gamma) {
		/* Take values from user */
		acc->gamma = acc_user->gamma;
	} else if (acc_old) {
		/* Use old value */
		acc->gamma = acc_old->gamma;
	} else {
		/* Calculate from scratch */
		acc->gamma.gc_ctrl.enable = 1;
		acc->gamma.gc_lut = ipu3_css_gamma_lut;
	}

	/* acc_param: csc_mat_config */

	if (use && use->acc_csc) {
		/* Take values from user */
		acc->csc = acc_user->csc;
	} else if (acc_old) {
		/* Use old value */
		acc->csc = acc_old->csc;
	} else {
		/* Calculate from scratch */
		acc->csc = ipu3_css_csc_defaults;
	}

	/* acc_param: cds_params */

	if (use && use->acc_cds) {
		/* Take values from user */
		acc->cds = acc_user->cds;
	} else if (acc_old) {
		/* Use old value */
		acc->cds = acc_old->cds;
	} else {
		/* Calculate from scratch */
		acc->cds = ipu3_css_cds_defaults;
	}

	/* acc_param: shd_config */

	if (use && use->acc_shd) {
		/* Take values from user */
		acc->shd.shd = acc_user->shd.shd;
		acc->shd.shd_lut = acc_user->shd.shd_lut;
	} else if (acc_old) {
		/* Use old value */
		acc->shd.shd = acc_old->shd.shd;
		acc->shd.shd_lut = acc_old->shd.shd_lut;
	} else {
		/* Calculate from scratch */
		acc->shd.shd = ipu3_css_shd_defaults;
		memset(&acc->shd.shd_lut, 0, sizeof(acc->shd.shd_lut));
	}

	if (acc->shd.shd.grid.width <= 0)
		return -EINVAL;

	acc->shd.shd.grid.grid_height_per_slice =
		IMGU_ABI_SHD_MAX_CELLS_PER_SET / acc->shd.shd.grid.width;

	if (acc->shd.shd.grid.grid_height_per_slice <= 0)
		return -EINVAL;

	acc->shd.shd.general.init_set_vrt_offst_ul =
				(-acc->shd.shd.grid.y_start >>
				 acc->shd.shd.grid.block_height_log2) %
				acc->shd.shd.grid.grid_height_per_slice;

	if (ipu3_css_shd_ops_calc(&acc->shd.shd_ops, &acc->shd.shd.grid,
				  css_pipe->rect[IPU3_CSS_RECT_BDS].height))
		return -EINVAL;

	/* acc_param: dvs_stat_config */
	ipu3_css_cfg_acc_dvs(css, acc, pipe);

	/* acc_param: yuvp1_iefd_config */

	if (use && use->acc_iefd) {
		/* Take values from user */
		acc->iefd = acc_user->iefd;
	} else if (acc_old) {
		/* Use old value */
		acc->iefd = acc_old->iefd;
	} else {
		/* Calculate from scratch */
		acc->iefd = ipu3_css_iefd_defaults;
	}

	/* acc_param: yuvp1_yds_config yds_c0 */

	if (use && use->acc_yds_c0) {
		/* Take values from user */
		acc->yds_c0 = acc_user->yds_c0;
	} else if (acc_old) {
		/* Use old value */
		acc->yds_c0 = acc_old->yds_c0;
	} else {
		/* Calculate from scratch */
		acc->yds_c0 = ipu3_css_yds_defaults;
	}

	/* acc_param: yuvp1_chnr_config chnr_c0 */

	if (use && use->acc_chnr_c0) {
		/* Take values from user */
		acc->chnr_c0 = acc_user->chnr_c0;
	} else if (acc_old) {
		/* Use old value */
		acc->chnr_c0 = acc_old->chnr_c0;
	} else {
		/* Calculate from scratch */
		acc->chnr_c0 = ipu3_css_chnr_defaults;
	}

	/* acc_param: yuvp1_y_ee_nr_config */

	if (use && use->acc_y_ee_nr) {
		/* Take values from user */
		acc->y_ee_nr = acc_user->y_ee_nr;
	} else if (acc_old) {
		/* Use old value */
		acc->y_ee_nr = acc_old->y_ee_nr;
	} else {
		/* Calculate from scratch */
		acc->y_ee_nr = ipu3_css_y_ee_nr_defaults;
	}

	/* acc_param: yuvp1_yds_config yds */

	if (use && use->acc_yds) {
		/* Take values from user */
		acc->yds = acc_user->yds;
	} else if (acc_old) {
		/* Use old value */
		acc->yds = acc_old->yds;
	} else {
		/* Calculate from scratch */
		acc->yds = ipu3_css_yds_defaults;
	}

	/* acc_param: yuvp1_chnr_config chnr */

	if (use && use->acc_chnr) {
		/* Take values from user */
		acc->chnr = acc_user->chnr;
	} else if (acc_old) {
		/* Use old value */
		acc->chnr = acc_old->chnr;
	} else {
		/* Calculate from scratch */
		acc->chnr = ipu3_css_chnr_defaults;
	}

	/* acc_param: yuvp2_y_tm_lut_static_config */

	for (i = 0; i < IMGU_ABI_YUVP2_YTM_LUT_ENTRIES; i++)
		acc->ytm.entries[i] = i * 32;
	acc->ytm.enable = 0;	/* Always disabled on IPU3 */

	/* acc_param: yuvp1_yds_config yds2 */

	if (use && use->acc_yds2) {
		/* Take values from user */
		acc->yds2 = acc_user->yds2;
	} else if (acc_old) {
		/* Use old value */
		acc->yds2 = acc_old->yds2;
	} else {
		/* Calculate from scratch */
		acc->yds2 = ipu3_css_yds_defaults;
	}

	/* acc_param: yuvp2_tcc_static_config */

	if (use && use->acc_tcc) {
		/* Take values from user */
		acc->tcc = acc_user->tcc;
	} else if (acc_old) {
		/* Use old value */
		acc->tcc = acc_old->tcc;
	} else {
		/* Calculate from scratch */
		memset(&acc->tcc, 0, sizeof(acc->tcc));

		acc->tcc.gen_control.en = 1;
		acc->tcc.gen_control.blend_shift = 3;
		acc->tcc.gen_control.gain_according_to_y_only = 1;
		acc->tcc.gen_control.gamma = 8;
		acc->tcc.gen_control.delta = 0;

		for (i = 0; i < IPU3_UAPI_YUVP2_TCC_MACC_TABLE_ELEMENTS; i++) {
			acc->tcc.macc_table.entries[i].a = 1024;
			acc->tcc.macc_table.entries[i].b = 0;
			acc->tcc.macc_table.entries[i].c = 0;
			acc->tcc.macc_table.entries[i].d = 1024;
		}

		acc->tcc.inv_y_lut.entries[6] = 1023;
		for (i = 7; i < IPU3_UAPI_YUVP2_TCC_INV_Y_LUT_ELEMENTS; i++)
			acc->tcc.inv_y_lut.entries[i] = 1024 >> (i - 6);

		acc->tcc.gain_pcwl = ipu3_css_tcc_gain_pcwl_lut;
		acc->tcc.r_sqr_lut = ipu3_css_tcc_r_sqr_lut;
	}

	/* acc_param: dpc_config */

	if (use && use->acc_dpc)
		return -EINVAL;	/* Not supported yet */

	/* Just disable by default */
	memset(&acc->dpc, 0, sizeof(acc->dpc));

	/* acc_param: bds_config */

	bds_ds = (css_pipe->rect[IPU3_CSS_RECT_EFFECTIVE].height *
		  IMGU_BDS_GRANULARITY) / css_pipe->rect[IPU3_CSS_RECT_BDS].height;
	if (bds_ds < IMGU_BDS_MIN_SF_INV ||
	    bds_ds - IMGU_BDS_MIN_SF_INV >= ARRAY_SIZE(ipu3_css_bds_configs))
		return -EINVAL;

	cfg_bds = &ipu3_css_bds_configs[bds_ds - IMGU_BDS_MIN_SF_INV];
	acc->bds.hor.hor_ctrl1.hor_crop_en = 0;
	acc->bds.hor.hor_ctrl1.hor_crop_start = 0;
	acc->bds.hor.hor_ctrl1.hor_crop_end = 0;
	acc->bds.hor.hor_ctrl0.sample_patrn_length =
				cfg_bds->sample_patrn_length;
	acc->bds.hor.hor_ctrl0.hor_ds_en = cfg_bds->hor_ds_en;
	acc->bds.hor.hor_ctrl0.min_clip_val = IMGU_BDS_MIN_CLIP_VAL;
	acc->bds.hor.hor_ctrl0.max_clip_val = IMGU_BDS_MAX_CLIP_VAL;
	acc->bds.hor.hor_ctrl0.out_frame_width =
				css_pipe->rect[IPU3_CSS_RECT_BDS].width;
	acc->bds.hor.hor_ptrn_arr = cfg_bds->ptrn_arr;
	acc->bds.hor.hor_phase_arr = cfg_bds->hor_phase_arr;
	acc->bds.hor.hor_ctrl2.input_frame_height =
				css_pipe->rect[IPU3_CSS_RECT_EFFECTIVE].height;
	acc->bds.ver.ver_ctrl0.min_clip_val = IMGU_BDS_MIN_CLIP_VAL;
	acc->bds.ver.ver_ctrl0.max_clip_val = IMGU_BDS_MAX_CLIP_VAL;
	acc->bds.ver.ver_ctrl0.sample_patrn_length =
				cfg_bds->sample_patrn_length;
	acc->bds.ver.ver_ctrl0.ver_ds_en = cfg_bds->ver_ds_en;
	acc->bds.ver.ver_ptrn_arr = cfg_bds->ptrn_arr;
	acc->bds.ver.ver_phase_arr = cfg_bds->ver_phase_arr;
	acc->bds.ver.ver_ctrl1.out_frame_width =
				css_pipe->rect[IPU3_CSS_RECT_BDS].width;
	acc->bds.ver.ver_ctrl1.out_frame_height =
				css_pipe->rect[IPU3_CSS_RECT_BDS].height;
	for (i = 0; i < stripes; i++)
		acc_bds_per_stripe_data(css, acc, i, pipe);

	acc->bds.enabled = cfg_bds->hor_ds_en || cfg_bds->ver_ds_en;

	/* acc_param: anr_config */

	if (use && use->acc_anr) {
		/* Take values from user */
		acc->anr.transform = acc_user->anr.transform;
		acc->anr.stitch.anr_stitch_en =
			acc_user->anr.stitch.anr_stitch_en;
		memcpy(acc->anr.stitch.pyramid, acc_user->anr.stitch.pyramid,
		       sizeof(acc->anr.stitch.pyramid));
	} else if (acc_old) {
		/* Use old value */
		acc->anr.transform = acc_old->anr.transform;
		acc->anr.stitch.anr_stitch_en =
			acc_old->anr.stitch.anr_stitch_en;
		memcpy(acc->anr.stitch.pyramid, acc_old->anr.stitch.pyramid,
		       sizeof(acc->anr.stitch.pyramid));
	} else {
		/* Calculate from scratch */
		acc->anr = ipu3_css_anr_defaults;
	}

	/* Always enabled */
	acc->anr.search.enable = 1;
	acc->anr.transform.enable = 1;
	acc->anr.tile2strm.enable = 1;
	acc->anr.tile2strm.frame_width =
		ALIGN(css_pipe->rect[IPU3_CSS_RECT_BDS].width, IMGU_ISP_VMEM_ALIGN);
	acc->anr.search.frame_width = acc->anr.tile2strm.frame_width;
	acc->anr.stitch.frame_width = acc->anr.tile2strm.frame_width;
	acc->anr.tile2strm.frame_height = css_pipe->rect[IPU3_CSS_RECT_BDS].height;
	acc->anr.search.frame_height = acc->anr.tile2strm.frame_height;
	acc->anr.stitch.frame_height = acc->anr.tile2strm.frame_height;

	width = ALIGN(css_pipe->rect[IPU3_CSS_RECT_BDS].width, IMGU_ISP_VMEM_ALIGN);
	height = css_pipe->rect[IPU3_CSS_RECT_BDS].height;

	if (acc->anr.transform.xreset + width > IPU3_UAPI_ANR_MAX_RESET)
		acc->anr.transform.xreset = IPU3_UAPI_ANR_MAX_RESET - width;
	if (acc->anr.transform.xreset < IPU3_UAPI_ANR_MIN_RESET)
		acc->anr.transform.xreset = IPU3_UAPI_ANR_MIN_RESET;

	if (acc->anr.transform.yreset + height > IPU3_UAPI_ANR_MAX_RESET)
		acc->anr.transform.yreset = IPU3_UAPI_ANR_MAX_RESET - height;
	if (acc->anr.transform.yreset < IPU3_UAPI_ANR_MIN_RESET)
		acc->anr.transform.yreset = IPU3_UAPI_ANR_MIN_RESET;

	/* acc_param: awb_fr_config */

	if (use && use->acc_awb_fr) {
		/* Take values from user */
		acc->awb_fr.config = acc_user->awb_fr;
	} else if (acc_old) {
		/* Use old value */
		acc->awb_fr.config = acc_old->awb_fr.config;
	} else {
		/* Set from scratch */
		acc->awb_fr.config = ipu3_css_awb_fr_defaults;
	}

	ipu3_css_grid_end_calc(&acc->awb_fr.config.grid_cfg);

	if (acc->awb_fr.config.grid_cfg.width <= 0)
		return -EINVAL;

	acc->awb_fr.config.grid_cfg.height_per_slice =
		IMGU_ABI_AWB_FR_MAX_CELLS_PER_SET /
		acc->awb_fr.config.grid_cfg.width;

	for (i = 0; i < stripes; i++)
		acc->awb_fr.stripes[i] = acc->awb_fr.config;

	if (acc->awb_fr.config.grid_cfg.x_start >=
	    acc->stripe.down_scaled_stripes[1].offset + min_overlap) {
		/* Enable only for rightmost stripe, disable left */
		acc->awb_fr.stripes[0].grid_cfg.y_start &=
					~IPU3_UAPI_GRID_Y_START_EN;
	} else if (acc->awb_fr.config.grid_cfg.x_end <=
		   acc->stripe.bds_out_stripes[0].width - min_overlap) {
		/* Enable only for leftmost stripe, disable right */
		acc->awb_fr.stripes[1].grid_cfg.y_start &=
					~IPU3_UAPI_GRID_Y_START_EN;
	} else {
		/* Enable for both stripes */
		u16 end; /* width for grid end */

		acc->awb_fr.stripes[0].grid_cfg.width =
			(acc->stripe.bds_out_stripes[0].width - min_overlap -
			 acc->awb_fr.config.grid_cfg.x_start + 1) >>
			acc->awb_fr.config.grid_cfg.block_width_log2;
		acc->awb_fr.stripes[1].grid_cfg.width =
			acc->awb_fr.config.grid_cfg.width -
			acc->awb_fr.stripes[0].grid_cfg.width;

		b_w_log2 = acc->awb_fr.stripes[0].grid_cfg.block_width_log2;
		end = ipu3_css_grid_end(acc->awb_fr.stripes[0].grid_cfg.x_start,
					acc->awb_fr.stripes[0].grid_cfg.width,
					b_w_log2);
		acc->awb_fr.stripes[0].grid_cfg.x_end = end;

		acc->awb_fr.stripes[1].grid_cfg.x_start =
			(acc->awb_fr.stripes[0].grid_cfg.x_end + 1 -
			 acc->stripe.down_scaled_stripes[1].offset) &
			IPU3_UAPI_GRID_START_MASK;
		b_w_log2 = acc->awb_fr.stripes[1].grid_cfg.block_width_log2;
		end = ipu3_css_grid_end(acc->awb_fr.stripes[1].grid_cfg.x_start,
					acc->awb_fr.stripes[1].grid_cfg.width,
					b_w_log2);
		acc->awb_fr.stripes[1].grid_cfg.x_end = end;

		/*
		 * To reduce complexity of debubbling and loading
		 * statistics fix grid_height_per_slice to 1 for both
		 * stripes.
		 */
		for (i = 0; i < stripes; i++)
			acc->awb_fr.stripes[i].grid_cfg.height_per_slice = 1;
	}

	if (ipu3_css_awb_fr_ops_calc(css, pipe, &acc->awb_fr))
		return -EINVAL;

	/* acc_param: ae_config */

	if (use && use->acc_ae) {
		/* Take values from user */
		acc->ae.grid_cfg = acc_user->ae.grid_cfg;
		acc->ae.ae_ccm = acc_user->ae.ae_ccm;
		for (i = 0; i < IPU3_UAPI_AE_WEIGHTS; i++)
			acc->ae.weights[i] = acc_user->ae.weights[i];
	} else if (acc_old) {
		/* Use old value */
		acc->ae.grid_cfg = acc_old->ae.grid_cfg;
		acc->ae.ae_ccm = acc_old->ae.ae_ccm;
		for (i = 0; i < IPU3_UAPI_AE_WEIGHTS; i++)
			acc->ae.weights[i] = acc_old->ae.weights[i];
	} else {
		/* Set from scratch */
		static const struct ipu3_uapi_ae_weight_elem
			weight_def = { 1, 1, 1, 1, 1, 1, 1, 1 };

		acc->ae.grid_cfg = ipu3_css_ae_grid_defaults;
		acc->ae.ae_ccm = ipu3_css_ae_ccm_defaults;
		for (i = 0; i < IPU3_UAPI_AE_WEIGHTS; i++)
			acc->ae.weights[i] = weight_def;
	}

	b_w_log2 = acc->ae.grid_cfg.block_width_log2;
	acc->ae.grid_cfg.x_end = ipu3_css_grid_end(acc->ae.grid_cfg.x_start,
						   acc->ae.grid_cfg.width,
						   b_w_log2);
	b_w_log2 = acc->ae.grid_cfg.block_height_log2;
	acc->ae.grid_cfg.y_end = ipu3_css_grid_end(acc->ae.grid_cfg.y_start,
						   acc->ae.grid_cfg.height,
						   b_w_log2);

	for (i = 0; i < stripes; i++)
		acc->ae.stripes[i].grid = acc->ae.grid_cfg;

	if (acc->ae.grid_cfg.x_start >=
	    acc->stripe.down_scaled_stripes[1].offset) {
		/* Enable only for rightmost stripe, disable left */
		acc->ae.stripes[0].grid.ae_en = 0;
	} else if (acc->ae.grid_cfg.x_end <=
		   acc->stripe.bds_out_stripes[0].width) {
		/* Enable only for leftmost stripe, disable right */
		acc->ae.stripes[1].grid.ae_en = 0;
	} else {
		/* Enable for both stripes */
		u8 b_w_log2;

		acc->ae.stripes[0].grid.width =
			(acc->stripe.bds_out_stripes[0].width -
			 acc->ae.grid_cfg.x_start + 1) >>
			acc->ae.grid_cfg.block_width_log2;

		acc->ae.stripes[1].grid.width =
			acc->ae.grid_cfg.width - acc->ae.stripes[0].grid.width;

		b_w_log2 = acc->ae.stripes[0].grid.block_width_log2;
		acc->ae.stripes[0].grid.x_end =
			ipu3_css_grid_end(acc->ae.stripes[0].grid.x_start,
					  acc->ae.stripes[0].grid.width,
					  b_w_log2);

		acc->ae.stripes[1].grid.x_start =
			(acc->ae.stripes[0].grid.x_end + 1 -
			 acc->stripe.down_scaled_stripes[1].offset) &
			IPU3_UAPI_GRID_START_MASK;
		b_w_log2 = acc->ae.stripes[1].grid.block_width_log2;
		acc->ae.stripes[1].grid.x_end =
			ipu3_css_grid_end(acc->ae.stripes[1].grid.x_start,
					  acc->ae.stripes[1].grid.width,
					  b_w_log2);
	}

	/* acc_param: af_config */

	if (use && use->acc_af) {
		/* Take values from user */
		acc->af.config.filter_config = acc_user->af.filter_config;
		acc->af.config.grid_cfg = acc_user->af.grid_cfg;
	} else if (acc_old) {
		/* Use old value */
		acc->af.config = acc_old->af.config;
	} else {
		/* Set from scratch */
		acc->af.config.filter_config =
				ipu3_css_af_defaults.filter_config;
		acc->af.config.grid_cfg = ipu3_css_af_defaults.grid_cfg;
	}

	ipu3_css_grid_end_calc(&acc->af.config.grid_cfg);

	if (acc->af.config.grid_cfg.width <= 0)
		return -EINVAL;

	acc->af.config.grid_cfg.height_per_slice =
		IMGU_ABI_AF_MAX_CELLS_PER_SET / acc->af.config.grid_cfg.width;
	acc->af.config.frame_size.width =
		ALIGN(css_pipe->rect[IPU3_CSS_RECT_BDS].width, IMGU_ISP_VMEM_ALIGN);
	acc->af.config.frame_size.height =
		css_pipe->rect[IPU3_CSS_RECT_BDS].height;

	if (acc->stripe.bds_out_stripes[0].width <= min_overlap)
		return -EINVAL;

	for (i = 0; i < stripes; i++) {
		acc->af.stripes[i].grid_cfg = acc->af.config.grid_cfg;
		acc->af.stripes[i].frame_size.height =
				css_pipe->rect[IPU3_CSS_RECT_BDS].height;
		acc->af.stripes[i].frame_size.width =
			acc->stripe.bds_out_stripes[i].width;
	}

	if (acc->af.config.grid_cfg.x_start >=
	    acc->stripe.down_scaled_stripes[1].offset + min_overlap) {
		/* Enable only for rightmost stripe, disable left */
		acc->af.stripes[0].grid_cfg.y_start &=
			~IPU3_UAPI_GRID_Y_START_EN;
	} else if (acc->af.config.grid_cfg.x_end <=
		   acc->stripe.bds_out_stripes[0].width - min_overlap) {
		/* Enable only for leftmost stripe, disable right */
		acc->af.stripes[1].grid_cfg.y_start &=
			~IPU3_UAPI_GRID_Y_START_EN;
	} else {
		/* Enable for both stripes */

		acc->af.stripes[0].grid_cfg.width =
			(acc->stripe.bds_out_stripes[0].width - min_overlap -
			 acc->af.config.grid_cfg.x_start + 1) >>
			acc->af.config.grid_cfg.block_width_log2;
		acc->af.stripes[1].grid_cfg.width =
			acc->af.config.grid_cfg.width -
			acc->af.stripes[0].grid_cfg.width;

		b_w_log2 = acc->af.stripes[0].grid_cfg.block_width_log2;
		acc->af.stripes[0].grid_cfg.x_end =
			ipu3_css_grid_end(acc->af.stripes[0].grid_cfg.x_start,
					  acc->af.stripes[0].grid_cfg.width,
					  b_w_log2);

		acc->af.stripes[1].grid_cfg.x_start =
			(acc->af.stripes[0].grid_cfg.x_end + 1 -
			 acc->stripe.down_scaled_stripes[1].offset) &
			IPU3_UAPI_GRID_START_MASK;

		b_w_log2 = acc->af.stripes[1].grid_cfg.block_width_log2;
		acc->af.stripes[1].grid_cfg.x_end =
			ipu3_css_grid_end(acc->af.stripes[1].grid_cfg.x_start,
					  acc->af.stripes[1].grid_cfg.width,
					  b_w_log2);

		/*
		 * To reduce complexity of debubbling and loading statistics
		 * fix grid_height_per_slice to 1 for both stripes
		 */
		for (i = 0; i < stripes; i++)
			acc->af.stripes[i].grid_cfg.height_per_slice = 1;
	}

	if (ipu3_css_af_ops_calc(css, pipe, &acc->af))
		return -EINVAL;

	/* acc_param: awb_config */

	if (use && use->acc_awb) {
		/* Take values from user */
		acc->awb.config = acc_user->awb.config;
	} else if (acc_old) {
		/* Use old value */
		acc->awb.config = acc_old->awb.config;
	} else {
		/* Set from scratch */
		acc->awb.config = ipu3_css_awb_defaults;
	}

	if (acc->awb.config.grid.width <= 0)
		return -EINVAL;

	acc->awb.config.grid.height_per_slice =
		IMGU_ABI_AWB_MAX_CELLS_PER_SET / acc->awb.config.grid.width,
	ipu3_css_grid_end_calc(&acc->awb.config.grid);

	for (i = 0; i < stripes; i++)
		acc->awb.stripes[i] = acc->awb.config;

	if (acc->awb.config.grid.x_start >=
	    acc->stripe.down_scaled_stripes[1].offset + min_overlap) {
		/* Enable only for rightmost stripe, disable left */
		acc->awb.stripes[0].rgbs_thr_b &= ~IPU3_UAPI_AWB_RGBS_THR_B_EN;
	} else if (acc->awb.config.grid.x_end <=
		   acc->stripe.bds_out_stripes[0].width - min_overlap) {
		/* Enable only for leftmost stripe, disable right */
		acc->awb.stripes[1].rgbs_thr_b &= ~IPU3_UAPI_AWB_RGBS_THR_B_EN;
	} else {
		/* Enable for both stripes */

		acc->awb.stripes[0].grid.width =
			(acc->stripe.bds_out_stripes[0].width -
			 acc->awb.config.grid.x_start + 1) >>
			acc->awb.config.grid.block_width_log2;
		acc->awb.stripes[1].grid.width = acc->awb.config.grid.width -
				acc->awb.stripes[0].grid.width;

		b_w_log2 = acc->awb.stripes[0].grid.block_width_log2;
		acc->awb.stripes[0].grid.x_end =
			ipu3_css_grid_end(acc->awb.stripes[0].grid.x_start,
					  acc->awb.stripes[0].grid.width,
					  b_w_log2);

		acc->awb.stripes[1].grid.x_start =
			(acc->awb.stripes[0].grid.x_end + 1 -
			 acc->stripe.down_scaled_stripes[1].offset) &
			IPU3_UAPI_GRID_START_MASK;

		b_w_log2 = acc->awb.stripes[1].grid.block_width_log2;
		acc->awb.stripes[1].grid.x_end =
			ipu3_css_grid_end(acc->awb.stripes[1].grid.x_start,
					  acc->awb.stripes[1].grid.width,
					  b_w_log2);

		/*
		 * To reduce complexity of debubbling and loading statistics
		 * fix grid_height_per_slice to 1 for both stripes
		 */
		for (i = 0; i < stripes; i++)
			acc->awb.stripes[i].grid.height_per_slice = 1;
	}

	if (ipu3_css_awb_ops_calc(css, pipe, &acc->awb))
		return -EINVAL;

	return 0;
}

/*
 * Fill the indicated structure in `new_binary_params' from the possible
 * sources based on `use_user' flag: if the flag is false, copy from
 * `old_binary_params', or if the flag is true, copy from `user_setting'
 * and return NULL (or error pointer on error).
 * If the flag is false and `old_binary_params' is NULL, return pointer
 * to the structure inside `new_binary_params'. In that case the caller
 * should calculate and fill the structure from scratch.
 */
static void *ipu3_css_cfg_copy(struct ipu3_css *css,
			       unsigned int pipe, bool use_user,
			       void *user_setting, void *old_binary_params,
			       void *new_binary_params,
			       enum imgu_abi_memories m,
			       struct imgu_fw_isp_parameter *par,
			       size_t par_size)
{
	const enum imgu_abi_param_class c = IMGU_ABI_PARAM_CLASS_PARAM;
	void *new_setting, *old_setting;

	new_setting = ipu3_css_fw_pipeline_params(css, pipe, c, m, par,
						  par_size, new_binary_params);
	if (!new_setting)
		return ERR_PTR(-EPROTO);	/* Corrupted firmware */

	if (use_user) {
		/* Take new user parameters */
		memcpy(new_setting, user_setting, par_size);
	} else if (old_binary_params) {
		/* Take previous value */
		old_setting = ipu3_css_fw_pipeline_params(css, pipe, c, m, par,
							  par_size,
							  old_binary_params);
		if (!old_setting)
			return ERR_PTR(-EPROTO);
		memcpy(new_setting, old_setting, par_size);
	} else {
		return new_setting;	/* Need to calculate */
	}

	return NULL;		/* Copied from other value */
}

/*
 * Configure VMEM0 parameters (late binding parameters).
 */
int ipu3_css_cfg_vmem0(struct ipu3_css *css, unsigned int pipe,
		       struct ipu3_uapi_flags *use,
		       void *vmem0, void *vmem0_old,
		       struct ipu3_uapi_params *user)
{
	const struct imgu_fw_info *bi =
		&css->fwp->binary_header[css->pipes[pipe].bindex];
	struct imgu_fw_param_memory_offsets *pofs = (void *)css->fwp +
		bi->blob.memory_offsets.offsets[IMGU_ABI_PARAM_CLASS_PARAM];
	struct ipu3_uapi_isp_lin_vmem_params *lin_vmem = NULL;
	struct ipu3_uapi_isp_tnr3_vmem_params *tnr_vmem = NULL;
	struct ipu3_uapi_isp_xnr3_vmem_params *xnr_vmem = NULL;
	const enum imgu_abi_param_class c = IMGU_ABI_PARAM_CLASS_PARAM;
	const enum imgu_abi_memories m = IMGU_ABI_MEM_ISP_VMEM0;
	unsigned int i;

	/* Configure VMEM0 */

	memset(vmem0, 0, bi->info.isp.sp.mem_initializers.params[c][m].size);

	/* Configure Linearization VMEM0 parameters */

	lin_vmem = ipu3_css_cfg_copy(css, pipe, use && use->lin_vmem_params,
				     &user->lin_vmem_params, vmem0_old, vmem0,
				     m, &pofs->vmem.lin, sizeof(*lin_vmem));
	if (!IS_ERR_OR_NULL(lin_vmem)) {
		/* Generate parameter from scratch */
		for (i = 0; i < IPU3_UAPI_LIN_LUT_SIZE; i++) {
			lin_vmem->lin_lutlow_gr[i] = 32 * i;
			lin_vmem->lin_lutlow_r[i] = 32 * i;
			lin_vmem->lin_lutlow_b[i] = 32 * i;
			lin_vmem->lin_lutlow_gb[i] = 32 * i;

			lin_vmem->lin_lutdif_gr[i] = 32;
			lin_vmem->lin_lutdif_r[i] = 32;
			lin_vmem->lin_lutdif_b[i] = 32;
			lin_vmem->lin_lutdif_gb[i] = 32;
		}
	}

	/* Configure TNR3 VMEM parameters */
	if (css->pipes[pipe].pipe_id == IPU3_CSS_PIPE_ID_VIDEO) {
		tnr_vmem = ipu3_css_cfg_copy(css, pipe,
					     use && use->tnr3_vmem_params,
					     &user->tnr3_vmem_params,
					     vmem0_old, vmem0, m,
					     &pofs->vmem.tnr3,
					     sizeof(*tnr_vmem));
		if (!IS_ERR_OR_NULL(tnr_vmem)) {
			/* Generate parameter from scratch */
			for (i = 0; i < IPU3_UAPI_ISP_TNR3_VMEM_LEN; i++)
				tnr_vmem->sigma[i] = 256;
		}
	}
	i = IPU3_UAPI_ISP_TNR3_VMEM_LEN;

	/* Configure XNR3 VMEM parameters */

	xnr_vmem = ipu3_css_cfg_copy(css, pipe, use && use->xnr3_vmem_params,
				     &user->xnr3_vmem_params, vmem0_old, vmem0,
				     m, &pofs->vmem.xnr3, sizeof(*xnr_vmem));
	if (!IS_ERR_OR_NULL(xnr_vmem)) {
		xnr_vmem->x[i] = ipu3_css_xnr3_vmem_defaults.x
			[i % IMGU_XNR3_VMEM_LUT_LEN];
		xnr_vmem->a[i] = ipu3_css_xnr3_vmem_defaults.a
			[i % IMGU_XNR3_VMEM_LUT_LEN];
		xnr_vmem->b[i] = ipu3_css_xnr3_vmem_defaults.b
			[i % IMGU_XNR3_VMEM_LUT_LEN];
		xnr_vmem->c[i] = ipu3_css_xnr3_vmem_defaults.c
			[i % IMGU_XNR3_VMEM_LUT_LEN];
	}

	return IS_ERR(lin_vmem) || IS_ERR(tnr_vmem) || IS_ERR(xnr_vmem) ?
		-EPROTO : 0;
}

/*
 * Configure DMEM0 parameters (late binding parameters).
 */
int ipu3_css_cfg_dmem0(struct ipu3_css *css, unsigned int pipe,
		       struct ipu3_uapi_flags *use,
		       void *dmem0, void *dmem0_old,
		       struct ipu3_uapi_params *user)
{
	struct ipu3_css_pipe *css_pipe = &css->pipes[pipe];
	const struct imgu_fw_info *bi =
		&css->fwp->binary_header[css_pipe->bindex];
	struct imgu_fw_param_memory_offsets *pofs = (void *)css->fwp +
		bi->blob.memory_offsets.offsets[IMGU_ABI_PARAM_CLASS_PARAM];

	struct ipu3_uapi_isp_tnr3_params *tnr_dmem = NULL;
	struct ipu3_uapi_isp_xnr3_params *xnr_dmem;

	const enum imgu_abi_param_class c = IMGU_ABI_PARAM_CLASS_PARAM;
	const enum imgu_abi_memories m = IMGU_ABI_MEM_ISP_DMEM0;

	/* Configure DMEM0 */

	memset(dmem0, 0, bi->info.isp.sp.mem_initializers.params[c][m].size);

	/* Configure TNR3 DMEM0 parameters */
	if (css_pipe->pipe_id == IPU3_CSS_PIPE_ID_VIDEO) {
		tnr_dmem = ipu3_css_cfg_copy(css, pipe,
					     use && use->tnr3_dmem_params,
					     &user->tnr3_dmem_params,
					     dmem0_old, dmem0, m,
					     &pofs->dmem.tnr3,
					     sizeof(*tnr_dmem));
		if (!IS_ERR_OR_NULL(tnr_dmem)) {
			/* Generate parameter from scratch */
			tnr_dmem->knee_y1 = 768;
			tnr_dmem->knee_y2 = 1280;
		}
	}

	/* Configure XNR3 DMEM0 parameters */

	xnr_dmem = ipu3_css_cfg_copy(css, pipe, use && use->xnr3_dmem_params,
				     &user->xnr3_dmem_params, dmem0_old, dmem0,
				     m, &pofs->dmem.xnr3, sizeof(*xnr_dmem));
	if (!IS_ERR_OR_NULL(xnr_dmem)) {
		/* Generate parameter from scratch */
		xnr_dmem->alpha.y0 = 2047;
		xnr_dmem->alpha.u0 = 2047;
		xnr_dmem->alpha.v0 = 2047;
	}

	return IS_ERR(tnr_dmem) || IS_ERR(xnr_dmem) ? -EPROTO : 0;
}

/* Generate unity morphing table without morphing effect */
void ipu3_css_cfg_gdc_table(struct imgu_abi_gdc_warp_param *gdc,
			    int frame_in_x, int frame_in_y,
			    int frame_out_x, int frame_out_y,
			    int env_w, int env_h)
{
	static const unsigned int FRAC_BITS = IMGU_ABI_GDC_FRAC_BITS;
	static const unsigned int XMEM_ALIGN = 1 << 4;
	const unsigned int XMEM_ALIGN_MASK = ~(XMEM_ALIGN - 1);
	static const unsigned int BCI_ENV = 4;
	static const unsigned int BYP = 2;	/* Bytes per pixel */
	const unsigned int OFFSET_X = 2 * IMGU_DVS_BLOCK_W + env_w + 1;
	const unsigned int OFFSET_Y = IMGU_DVS_BLOCK_H + env_h + 1;

	struct imgu_abi_gdc_warp_param gdc_luma, gdc_chroma;

	unsigned int blocks_x = ALIGN(DIV_ROUND_UP(frame_out_x,
						   IMGU_DVS_BLOCK_W), 2);
	unsigned int blocks_y = DIV_ROUND_UP(frame_out_y, IMGU_DVS_BLOCK_H);
	unsigned int y0, x0, x1, x, y;

	/* Global luma settings */
	gdc_luma.origin_x = 0;
	gdc_luma.origin_y = 0;
	gdc_luma.p0_x = (OFFSET_X - (OFFSET_X & XMEM_ALIGN_MASK)) << FRAC_BITS;
	gdc_luma.p0_y = 0;
	gdc_luma.p1_x = gdc_luma.p0_x + (IMGU_DVS_BLOCK_W << FRAC_BITS);
	gdc_luma.p1_y = gdc_luma.p0_y;
	gdc_luma.p2_x = gdc_luma.p0_x;
	gdc_luma.p2_y = gdc_luma.p0_y + (IMGU_DVS_BLOCK_H << FRAC_BITS);
	gdc_luma.p3_x = gdc_luma.p1_x;
	gdc_luma.p3_y = gdc_luma.p2_y;

	gdc_luma.in_block_width = IMGU_DVS_BLOCK_W + BCI_ENV +
					OFFSET_X - (OFFSET_X & XMEM_ALIGN_MASK);
	gdc_luma.in_block_width_a = DIV_ROUND_UP(gdc_luma.in_block_width,
						 IPU3_UAPI_ISP_VEC_ELEMS);
	gdc_luma.in_block_width_b = DIV_ROUND_UP(gdc_luma.in_block_width,
						 IMGU_ABI_ISP_DDR_WORD_BYTES /
						 BYP);
	gdc_luma.in_block_height = IMGU_DVS_BLOCK_H + BCI_ENV;
	gdc_luma.padding = 0;

	/* Global chroma settings */
	gdc_chroma.origin_x = 0;
	gdc_chroma.origin_y = 0;
	gdc_chroma.p0_x = (OFFSET_X / 2 - (OFFSET_X / 2 & XMEM_ALIGN_MASK)) <<
			   FRAC_BITS;
	gdc_chroma.p0_y = 0;
	gdc_chroma.p1_x = gdc_chroma.p0_x + (IMGU_DVS_BLOCK_W << FRAC_BITS);
	gdc_chroma.p1_y = gdc_chroma.p0_y;
	gdc_chroma.p2_x = gdc_chroma.p0_x;
	gdc_chroma.p2_y = gdc_chroma.p0_y + (IMGU_DVS_BLOCK_H / 2 << FRAC_BITS);
	gdc_chroma.p3_x = gdc_chroma.p1_x;
	gdc_chroma.p3_y = gdc_chroma.p2_y;

	gdc_chroma.in_block_width = IMGU_DVS_BLOCK_W + BCI_ENV;
	gdc_chroma.in_block_width_a = DIV_ROUND_UP(gdc_chroma.in_block_width,
						   IPU3_UAPI_ISP_VEC_ELEMS);
	gdc_chroma.in_block_width_b = DIV_ROUND_UP(gdc_chroma.in_block_width,
						   IMGU_ABI_ISP_DDR_WORD_BYTES /
						   BYP);
	gdc_chroma.in_block_height = IMGU_DVS_BLOCK_H / 2 + BCI_ENV;
	gdc_chroma.padding = 0;

	/* Calculate block offsets for luma and chroma */
	for (y0 = 0; y0 < blocks_y; y0++) {
		for (x0 = 0; x0 < blocks_x / 2; x0++) {
			for (x1 = 0; x1 < 2; x1++) {
				/* Luma blocks */
				x = (x0 * 2 + x1) * IMGU_DVS_BLOCK_W + OFFSET_X;
				x &= XMEM_ALIGN_MASK;
				y = y0 * IMGU_DVS_BLOCK_H + OFFSET_Y;
				*gdc = gdc_luma;
				gdc->in_addr_offset =
					(y * frame_in_x + x) * BYP;
				gdc++;
			}

			/* Chroma block */
			x = x0 * IMGU_DVS_BLOCK_W + OFFSET_X / 2;
			x &= XMEM_ALIGN_MASK;
			y = y0 * (IMGU_DVS_BLOCK_H / 2) + OFFSET_Y / 2;
			*gdc = gdc_chroma;
			gdc->in_addr_offset = (y * frame_in_x + x) * BYP;
			gdc++;
		}
	}
}
