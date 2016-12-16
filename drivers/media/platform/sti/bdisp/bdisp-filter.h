/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Fabien Dessenne <fabien.dessenne@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#define BDISP_HF_NB             64
#define BDISP_VF_NB             40

/**
 * struct bdisp_filter_h_spec - Horizontal filter specification
 *
 * @min:        min scale factor for this filter (6.10 fixed point)
 * @max:        max scale factor for this filter (6.10 fixed point)
 * coef:        filter coefficients
 */
struct bdisp_filter_h_spec {
	const u16 min;
	const u16 max;
	const u8 coef[BDISP_HF_NB];
};
/**
 * struct bdisp_filter_v_spec - Vertical filter specification
 *
 * @min:	min scale factor for this filter (6.10 fixed point)
 * @max:	max scale factor for this filter (6.10 fixed point)
 * coef:	filter coefficients
 */
struct bdisp_filter_v_spec {
	const u16 min;
	const u16 max;
	const u8 coef[BDISP_VF_NB];
};

/* RGB YUV 601 standard conversion */
static const u32 bdisp_rgb_to_yuv[] = {
		0x0e1e8bee, 0x08420419, 0xfb5ed471, 0x08004080,
};

static const u32 bdisp_yuv_to_rgb[] = {
		0x3324a800, 0xe604ab9c, 0x0004a957, 0x32121eeb,
};
