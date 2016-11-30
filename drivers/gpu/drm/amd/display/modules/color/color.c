/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services.h"
#include "dc.h"
#include "mod_color.h"
#include "core_types.h"
#include "fixed31_32.h"
#include "core_dc.h"

#define MOD_COLOR_MAX_CONCURRENT_SINKS 32
#define DIVIDER 10000
/* S2D13 value in [-3.00...0.9999] */
#define S2D13_MIN (-3 * DIVIDER)
#define S2D13_MAX (3 * DIVIDER)
#define S0D13_MIN (-1 * DIVIDER)
#define S0D13_MAX (1 * DIVIDER)

struct sink_caps {
	const struct dc_sink *sink;
};

struct gamut_calculation_matrix {
	struct fixed31_32 MTransposed[9];
	struct fixed31_32 XYZtoRGB_Custom[9];
	struct fixed31_32 XYZtoRGB_Ref[9];
	struct fixed31_32 RGBtoXYZ_Final[9];

	struct fixed31_32 MResult[9];
	struct fixed31_32 fXYZofWhiteRef[9];
	struct fixed31_32 fXYZofRGBRef[9];
};

struct gamut_src_dst_matrix {
	struct fixed31_32 rgbCoeffDst[9];
	struct fixed31_32 whiteCoeffDst[3];
	struct fixed31_32 rgbCoeffSrc[9];
	struct fixed31_32 whiteCoeffSrc[3];
};

struct color_state {
	bool user_enable_color_temperature;
	int custom_color_temperature;
	struct color_space_coordinates source_gamut;
	struct color_space_coordinates destination_gamut;
	struct color_range contrast;
	struct color_range saturation;
	struct color_range brightness;
	struct color_range hue;
	enum dc_quantization_range preferred_quantization_range;
};

struct core_color {
	struct mod_color public;
	struct dc *dc;
	int num_sinks;
	struct sink_caps *caps;
	struct color_state *state;
};

#define MOD_COLOR_TO_CORE(mod_color)\
		container_of(mod_color, struct core_color, public)

#define COLOR_REGISTRY_NAME "color_v1"

/*Matrix Calculation Functions*/
/**
 *****************************************************************************
 *  Function: transposeMatrix
 *
 *  @brief
 *    rotate the matrix 90 degrees clockwise
 *    rows become a columns and columns to rows
 *  @param [ in ] M            - source matrix
 *  @param [ in ] Rows         - num of Rows of the original matrix
 *  @param [ in ] Cols         - num of Cols of the original matrix
 *  @param [ out] MTransposed  - result matrix
 *  @return  void
 *
 *****************************************************************************
 */
static void transpose_matrix(const struct fixed31_32 *M, unsigned int Rows,
		unsigned int Cols,  struct fixed31_32 *MTransposed)
{
	unsigned int i, j;

	for (i = 0; i < Rows; i++) {
		for (j = 0; j < Cols; j++)
			MTransposed[(j*Rows)+i] = M[(i*Cols)+j];
	}
}

/**
 *****************************************************************************
 *  Function: multiplyMatrices
 *
 *  @brief
 *    multiplies produce of two matrices: M =  M1[ulRows1 x ulCols1] *
 *    M2[ulCols1 x ulCols2].
 *
 *  @param [ in ] M1      - first Matrix.
 *  @param [ in ] M2      - second Matrix.
 *  @param [ in ] Rows1   - num of Rows of the first Matrix
 *  @param [ in ] Cols1   - num of Cols of the first Matrix/Num of Rows
 *  of the second Matrix
 *  @param [ in ] Cols2   - num of Cols of the second Matrix
 *  @param [out ] mResult - resulting matrix.
 *  @return  void
 *
 *****************************************************************************
 */
static void multiply_matrices(struct fixed31_32 *mResult,
		const struct fixed31_32 *M1,
		const struct fixed31_32 *M2, unsigned int Rows1,
		unsigned int Cols1, unsigned int Cols2)
{
	unsigned int i, j, k;

	for (i = 0; i < Rows1; i++) {
		for (j = 0; j < Cols2; j++) {
			mResult[(i * Cols2) + j] = dal_fixed31_32_zero;
			for (k = 0; k < Cols1; k++)
				mResult[(i * Cols2) + j] =
					dal_fixed31_32_add
					(mResult[(i * Cols2) + j],
					dal_fixed31_32_mul(M1[(i * Cols1) + k],
					M2[(k * Cols2) + j]));
		}
	}
}

/**
 *****************************************************************************
 *  Function: cFind3X3Det
 *
 *  @brief
 *    finds determinant of given 3x3 matrix
 *
 *  @param [ in  ] m     - matrix
 *  @return determinate whioch could not be zero
 *
 *****************************************************************************
 */
static struct fixed31_32 find_3X3_det(const struct fixed31_32 *m)
{
	struct fixed31_32 det, A1, A2, A3;

	A1 = dal_fixed31_32_mul(m[0],
			dal_fixed31_32_sub(dal_fixed31_32_mul(m[4], m[8]),
					dal_fixed31_32_mul(m[5], m[7])));
	A2 = dal_fixed31_32_mul(m[1],
			dal_fixed31_32_sub(dal_fixed31_32_mul(m[3], m[8]),
					dal_fixed31_32_mul(m[5], m[6])));
	A3 = dal_fixed31_32_mul(m[2],
			dal_fixed31_32_sub(dal_fixed31_32_mul(m[3], m[7]),
					dal_fixed31_32_mul(m[4], m[6])));
	det = dal_fixed31_32_add(dal_fixed31_32_sub(A1, A2), A3);
	return det;
}


/**
 *****************************************************************************
 *  Function: computeInverseMatrix_3x3
 *
 *  @brief
 *    builds inverse matrix
 *
 *  @param [ in   ] m     - matrix
 *  @param [ out  ] im    - result matrix
 *  @return true if success
 *
 *****************************************************************************
 */
static bool compute_inverse_matrix_3x3(const struct fixed31_32 *m,
		struct fixed31_32 *im)
{
	struct fixed31_32 determinant = find_3X3_det(m);

	if (dal_fixed31_32_eq(determinant, dal_fixed31_32_zero) == false) {
		im[0] = dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[4], m[8]),
				dal_fixed31_32_mul(m[5], m[7])), determinant);
		im[1] = dal_fixed31_32_neg(dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[1], m[8]),
				dal_fixed31_32_mul(m[2], m[7])), determinant));
		im[2] = dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[1], m[5]),
				dal_fixed31_32_mul(m[2], m[4])), determinant);
		im[3] = dal_fixed31_32_neg(dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[3], m[8]),
				dal_fixed31_32_mul(m[5], m[6])), determinant));
		im[4] = dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[0], m[8]),
				dal_fixed31_32_mul(m[2], m[6])), determinant);
		im[5] = dal_fixed31_32_neg(dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[0], m[5]),
				dal_fixed31_32_mul(m[2], m[3])), determinant));
		im[6] = dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[3], m[7]),
				dal_fixed31_32_mul(m[4], m[6])), determinant);
		im[7] = dal_fixed31_32_neg(dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[0], m[7]),
				dal_fixed31_32_mul(m[1], m[6])), determinant));
		im[8] = dal_fixed31_32_div(dal_fixed31_32_sub
				(dal_fixed31_32_mul(m[0], m[4]),
				dal_fixed31_32_mul(m[1], m[3])), determinant);
		return true;
	}
	return false;
}

/**
 *****************************************************************************
 *  Function: calculateXYZtoRGB_M3x3
 *
 *  @brief
 *    Calculates transformation matrix from XYZ coordinates to RBG
 *
 *  @param [ in  ] XYZofRGB     - primaries XYZ
 *  @param [ in  ] XYZofWhite   - white point.
 *  @param [ out ] XYZtoRGB     - RGB primires
 *  @return  true if success
 *
 *****************************************************************************
 */
static bool calculate_XYZ_to_RGB_3x3(const struct fixed31_32 *XYZofRGB,
		const struct fixed31_32 *XYZofWhite,
		struct fixed31_32 *XYZtoRGB)
{

	struct fixed31_32 MInversed[9];
	struct fixed31_32 SVector[3];

	/*1. Find Inverse matrix 3x3 of MTransposed*/
	if (!compute_inverse_matrix_3x3(XYZofRGB, MInversed))
	return false;

	/*2. Calculate vector: |Sr Sg Sb| = [MInversed] * |Wx Wy Wz|*/
	multiply_matrices(SVector, MInversed, XYZofWhite, 3, 3, 1);

	/*3. Calculate matrix XYZtoRGB 3x3*/
	XYZtoRGB[0] = dal_fixed31_32_mul(XYZofRGB[0], SVector[0]);
	XYZtoRGB[1] = dal_fixed31_32_mul(XYZofRGB[1], SVector[1]);
	XYZtoRGB[2] = dal_fixed31_32_mul(XYZofRGB[2], SVector[2]);

	XYZtoRGB[3] = dal_fixed31_32_mul(XYZofRGB[3], SVector[0]);
	XYZtoRGB[4] = dal_fixed31_32_mul(XYZofRGB[4], SVector[1]);
	XYZtoRGB[5] = dal_fixed31_32_mul(XYZofRGB[5], SVector[2]);

	XYZtoRGB[6] = dal_fixed31_32_mul(XYZofRGB[6], SVector[0]);
	XYZtoRGB[7] = dal_fixed31_32_mul(XYZofRGB[7], SVector[1]);
	XYZtoRGB[8] = dal_fixed31_32_mul(XYZofRGB[8], SVector[2]);

	return true;
}

static bool gamut_to_color_matrix(
	const struct fixed31_32 *pXYZofRGB,/*destination gamut*/
	const struct fixed31_32 *pXYZofWhite,/*destination of white point*/
	const struct fixed31_32 *pRefXYZofRGB,/*source gamut*/
	const struct fixed31_32 *pRefXYZofWhite,/*source of white point*/
	bool invert,
	struct fixed31_32 *tempMatrix3X3)
{
	int i = 0;
	struct gamut_calculation_matrix *matrix =
			dm_alloc(sizeof(struct gamut_calculation_matrix));

	struct fixed31_32 *pXYZtoRGB_Temp;
	struct fixed31_32 *pXYZtoRGB_Final;

	matrix->fXYZofWhiteRef[0] = pRefXYZofWhite[0];
	matrix->fXYZofWhiteRef[1] = pRefXYZofWhite[1];
	matrix->fXYZofWhiteRef[2] = pRefXYZofWhite[2];


	matrix->fXYZofRGBRef[0] = pRefXYZofRGB[0];
	matrix->fXYZofRGBRef[1] = pRefXYZofRGB[1];
	matrix->fXYZofRGBRef[2] = pRefXYZofRGB[2];

	matrix->fXYZofRGBRef[3] = pRefXYZofRGB[3];
	matrix->fXYZofRGBRef[4] = pRefXYZofRGB[4];
	matrix->fXYZofRGBRef[5] = pRefXYZofRGB[5];

	matrix->fXYZofRGBRef[6] = pRefXYZofRGB[6];
	matrix->fXYZofRGBRef[7] = pRefXYZofRGB[7];
	matrix->fXYZofRGBRef[8] = pRefXYZofRGB[8];

	/*default values -  unity matrix*/
	while (i < 9) {
		if (i == 0 || i == 4 || i == 8)
			tempMatrix3X3[i] = dal_fixed31_32_one;
		else
			tempMatrix3X3[i] = dal_fixed31_32_zero;
		i++;
	}

	/*1. Decide about the order of calculation.
	 * bInvert == FALSE --> RGBtoXYZ_Ref * XYZtoRGB_Custom
	 * bInvert == TRUE  --> RGBtoXYZ_Custom * XYZtoRGB_Ref */
	if (invert) {
		pXYZtoRGB_Temp = matrix->XYZtoRGB_Custom;
		pXYZtoRGB_Final = matrix->XYZtoRGB_Ref;
	} else {
		pXYZtoRGB_Temp = matrix->XYZtoRGB_Ref;
		pXYZtoRGB_Final = matrix->XYZtoRGB_Custom;
	}

	/*2. Calculate XYZtoRGB_Ref*/
	transpose_matrix(matrix->fXYZofRGBRef, 3, 3, matrix->MTransposed);

	if (!calculate_XYZ_to_RGB_3x3(
		matrix->MTransposed,
		matrix->fXYZofWhiteRef,
		matrix->XYZtoRGB_Ref))
		goto function_fail;

	/*3. Calculate XYZtoRGB_Custom*/
	transpose_matrix(pXYZofRGB, 3, 3, matrix->MTransposed);

	if (!calculate_XYZ_to_RGB_3x3(
		matrix->MTransposed,
		pXYZofWhite,
		matrix->XYZtoRGB_Custom))
		goto function_fail;

	/*4. Calculate RGBtoXYZ -
	 * inverse matrix 3x3 of XYZtoRGB_Ref or XYZtoRGB_Custom*/
	if (!compute_inverse_matrix_3x3(pXYZtoRGB_Temp, matrix->RGBtoXYZ_Final))
		goto function_fail;

	/*5. Calculate M(3x3) = RGBtoXYZ * XYZtoRGB*/
	multiply_matrices(matrix->MResult, matrix->RGBtoXYZ_Final,
			pXYZtoRGB_Final, 3, 3, 3);

	for (i = 0; i < 9; i++)
		tempMatrix3X3[i] = matrix->MResult[i];

	dm_free(matrix);

	return true;

function_fail:
	dm_free(matrix);
	return false;
}

static bool build_gamut_remap_matrix
		(struct color_space_coordinates gamut_description,
		struct fixed31_32 *rgb_matrix,
		struct fixed31_32 *white_point_matrix)
{
	struct fixed31_32 fixed_blueX = dal_fixed31_32_from_fraction
			(gamut_description.blueX, DIVIDER);
	struct fixed31_32 fixed_blueY = dal_fixed31_32_from_fraction
			(gamut_description.blueY, DIVIDER);
	struct fixed31_32 fixed_greenX = dal_fixed31_32_from_fraction
			(gamut_description.greenX, DIVIDER);
	struct fixed31_32 fixed_greenY = dal_fixed31_32_from_fraction
			(gamut_description.greenY, DIVIDER);
	struct fixed31_32 fixed_redX = dal_fixed31_32_from_fraction
			(gamut_description.redX, DIVIDER);
	struct fixed31_32 fixed_redY = dal_fixed31_32_from_fraction
			(gamut_description.redY, DIVIDER);
	struct fixed31_32 fixed_whiteX = dal_fixed31_32_from_fraction
			(gamut_description.whiteX, DIVIDER);
	struct fixed31_32 fixed_whiteY = dal_fixed31_32_from_fraction
			(gamut_description.whiteY, DIVIDER);

	rgb_matrix[0] = dal_fixed31_32_div(fixed_redX, fixed_redY);
	rgb_matrix[1] = dal_fixed31_32_one;
	rgb_matrix[2] = dal_fixed31_32_div(dal_fixed31_32_sub
			(dal_fixed31_32_sub(dal_fixed31_32_one, fixed_redX),
					fixed_redY), fixed_redY);

	rgb_matrix[3] = dal_fixed31_32_div(fixed_greenX, fixed_greenY);
	rgb_matrix[4] = dal_fixed31_32_one;
	rgb_matrix[5] = dal_fixed31_32_div(dal_fixed31_32_sub
			(dal_fixed31_32_sub(dal_fixed31_32_one, fixed_greenX),
					fixed_greenY), fixed_greenY);

	rgb_matrix[6] = dal_fixed31_32_div(fixed_blueX, fixed_blueY);
	rgb_matrix[7] = dal_fixed31_32_one;
	rgb_matrix[8] = dal_fixed31_32_div(dal_fixed31_32_sub
			(dal_fixed31_32_sub(dal_fixed31_32_one, fixed_blueX),
					fixed_blueY), fixed_blueY);

	white_point_matrix[0] = dal_fixed31_32_div(fixed_whiteX, fixed_whiteY);
	white_point_matrix[1] = dal_fixed31_32_one;
	white_point_matrix[2] = dal_fixed31_32_div(dal_fixed31_32_sub
			(dal_fixed31_32_sub(dal_fixed31_32_one, fixed_whiteX),
					fixed_whiteY), fixed_whiteY);

	return true;
}

static bool check_dc_support(const struct dc *dc)
{
	if (dc->stream_funcs.set_gamut_remap == NULL)
		return false;

	return true;
}

static uint16_t fixed_point_to_int_frac(
	struct fixed31_32 arg,
	uint8_t integer_bits,
	uint8_t fractional_bits)
{
	int32_t numerator;
	int32_t divisor = 1 << fractional_bits;

	uint16_t result;

	uint16_t d = (uint16_t)dal_fixed31_32_floor(
		dal_fixed31_32_abs(
			arg));

	if (d <= (uint16_t)(1 << integer_bits) - (1 / (uint16_t)divisor))
		numerator = (uint16_t)dal_fixed31_32_floor(
			dal_fixed31_32_mul_int(
				arg,
				divisor));
	else {
		numerator = dal_fixed31_32_floor(
			dal_fixed31_32_sub(
				dal_fixed31_32_from_int(
					1LL << integer_bits),
				dal_fixed31_32_recip(
					dal_fixed31_32_from_int(
						divisor))));
	}

	if (numerator >= 0)
		result = (uint16_t)numerator;
	else
		result = (uint16_t)(
		(1 << (integer_bits + fractional_bits + 1)) + numerator);

	if ((result != 0) && dal_fixed31_32_lt(
		arg, dal_fixed31_32_zero))
		result |= 1 << (integer_bits + fractional_bits);

	return result;
}

/**
* convert_float_matrix
* This converts a double into HW register spec defined format S2D13.
* @param :
* @return None
*/

static void convert_float_matrix_legacy(
	uint16_t *matrix,
	struct fixed31_32 *flt,
	uint32_t buffer_size)
{
	const struct fixed31_32 min_2_13 =
		dal_fixed31_32_from_fraction(S2D13_MIN, DIVIDER);
	const struct fixed31_32 max_2_13 =
		dal_fixed31_32_from_fraction(S2D13_MAX, DIVIDER);
	uint32_t i;

	for (i = 0; i < buffer_size; ++i) {
		uint32_t reg_value =
				fixed_point_to_int_frac(
					dal_fixed31_32_clamp(
						flt[i],
						min_2_13,
						max_2_13),
						2,
						13);

		matrix[i] = (uint16_t)reg_value;
	}
}

static void convert_float_matrix(
	uint16_t *matrix,
	struct fixed31_32 *flt,
	uint32_t buffer_size)
{
	const struct fixed31_32 min_0_13 =
		dal_fixed31_32_from_fraction(S0D13_MIN, DIVIDER);
	const struct fixed31_32 max_0_13 =
		dal_fixed31_32_from_fraction(S0D13_MAX, DIVIDER);
	const struct fixed31_32 min_2_13 =
		dal_fixed31_32_from_fraction(S2D13_MIN, DIVIDER);
	const struct fixed31_32 max_2_13 =
		dal_fixed31_32_from_fraction(S2D13_MAX, DIVIDER);
	uint32_t i;
	uint16_t temp_matrix[12];

	for (i = 0; i < buffer_size; ++i) {
		if (i == 3 || i == 7 || i == 11) {
			uint32_t reg_value =
					fixed_point_to_int_frac(
						dal_fixed31_32_clamp(
							flt[i],
							min_0_13,
							max_0_13),
							2,
							13);

			temp_matrix[i] = (uint16_t)reg_value;
		} else {
			uint32_t reg_value =
					fixed_point_to_int_frac(
						dal_fixed31_32_clamp(
							flt[i],
							min_2_13,
							max_2_13),
							2,
							13);

			temp_matrix[i] = (uint16_t)reg_value;
		}
	}

	matrix[4] = temp_matrix[0];
	matrix[5] = temp_matrix[1];
	matrix[6] = temp_matrix[2];
	matrix[7] = temp_matrix[3];

	matrix[8] = temp_matrix[4];
	matrix[9] = temp_matrix[5];
	matrix[10] = temp_matrix[6];
	matrix[11] = temp_matrix[7];

	matrix[0] = temp_matrix[8];
	matrix[1] = temp_matrix[9];
	matrix[2] = temp_matrix[10];
	matrix[3] = temp_matrix[11];
}

static int get_hw_value_from_sw_value(int swVal, int swMin,
		int swMax, int hwMin, int hwMax)
{
	int dSW = swMax - swMin; /*software adjustment range size*/
	int dHW = hwMax - hwMin; /*hardware adjustment range size*/
	int hwVal; /*HW adjustment value*/

	/* error case, I preserve the behavior from the predecessor
	 *getHwStepFromSwHwMinMaxValue (removed in Feb 2013)
	 *which was the FP version that only computed SCLF (i.e. dHW/dSW).
	 *it would return 0 in this case so
	 *hwVal = hwMin from the formula given in @brief
	*/
	if (dSW == 0)
		return hwMin;

	/*it's quite often that ranges match,
	 *e.g. for overlay colors currently (Feb 2013)
	 *only brightness has a different
	 *HW range, and in this case no multiplication or division is needed,
	 *and if minimums match, no calculation at all
	*/
	if (dSW != dHW) {
		hwVal = (swVal - swMin)*dHW/dSW + hwMin;
	} else {
		hwVal = swVal;
		if (swMin != hwMin)
			hwVal += (hwMin - swMin);
	}

	return hwVal;
}

static void initialize_fix_point_color_values(
	struct core_color *core_color,
	unsigned int sink_index,
	struct fixed31_32 *grph_cont,
	struct fixed31_32 *grph_sat,
	struct fixed31_32 *grph_bright,
	struct fixed31_32 *sin_grph_hue,
	struct fixed31_32 *cos_grph_hue)
{
	/* Hue adjustment could be negative. -45 ~ +45 */
	struct fixed31_32 hue =
		dal_fixed31_32_mul(
			dal_fixed31_32_from_fraction
			(get_hw_value_from_sw_value
				(core_color->state[sink_index].hue.current,
				core_color->state[sink_index].hue.min,
				core_color->state[sink_index].hue.max,
				-30, 30), 180),
			dal_fixed31_32_pi);

	*sin_grph_hue = dal_fixed31_32_sin(hue);
	*cos_grph_hue = dal_fixed31_32_cos(hue);

	*grph_cont =
		dal_fixed31_32_from_fraction(get_hw_value_from_sw_value
			(core_color->state[sink_index].contrast.current,
			core_color->state[sink_index].contrast.min,
			core_color->state[sink_index].contrast.max,
			50, 150), 100);
	*grph_sat =
		dal_fixed31_32_from_fraction(get_hw_value_from_sw_value
			(core_color->state[sink_index].saturation.current,
			core_color->state[sink_index].saturation.min,
			core_color->state[sink_index].saturation.max,
			0, 200), 100);
	*grph_bright =
		dal_fixed31_32_from_fraction(get_hw_value_from_sw_value
			(core_color->state[sink_index].brightness.current,
			core_color->state[sink_index].brightness.min,
			core_color->state[sink_index].brightness.max,
			-25, 25), 100);
}


/* Given a specific dc_sink* this function finds its equivalent
 * on the dc_sink array and returns the corresponding index
 */
static unsigned int sink_index_from_sink(struct core_color *core_color,
		const struct dc_sink *sink)
{
	unsigned int index = 0;

	for (index = 0; index < core_color->num_sinks; index++)
		if (core_color->caps[index].sink == sink)
			return index;

	/* Could not find sink requested */
	ASSERT(false);
	return index;
}

static void calculate_rgb_matrix_legacy(struct core_color *core_color,
		unsigned int sink_index,
		struct fixed31_32 *rgb_matrix)
{
	const struct fixed31_32 k1 =
		dal_fixed31_32_from_fraction(701000, 1000000);
	const struct fixed31_32 k2 =
		dal_fixed31_32_from_fraction(236568, 1000000);
	const struct fixed31_32 k3 =
		dal_fixed31_32_from_fraction(-587000, 1000000);
	const struct fixed31_32 k4 =
		dal_fixed31_32_from_fraction(464432, 1000000);
	const struct fixed31_32 k5 =
		dal_fixed31_32_from_fraction(-114000, 1000000);
	const struct fixed31_32 k6 =
		dal_fixed31_32_from_fraction(-701000, 1000000);
	const struct fixed31_32 k7 =
		dal_fixed31_32_from_fraction(-299000, 1000000);
	const struct fixed31_32 k8 =
		dal_fixed31_32_from_fraction(-292569, 1000000);
	const struct fixed31_32 k9 =
		dal_fixed31_32_from_fraction(413000, 1000000);
	const struct fixed31_32 k10 =
		dal_fixed31_32_from_fraction(-92482, 1000000);
	const struct fixed31_32 k11 =
		dal_fixed31_32_from_fraction(-114000, 1000000);
	const struct fixed31_32 k12 =
		dal_fixed31_32_from_fraction(385051, 1000000);
	const struct fixed31_32 k13 =
		dal_fixed31_32_from_fraction(-299000, 1000000);
	const struct fixed31_32 k14 =
		dal_fixed31_32_from_fraction(886000, 1000000);
	const struct fixed31_32 k15 =
		dal_fixed31_32_from_fraction(-587000, 1000000);
	const struct fixed31_32 k16 =
		dal_fixed31_32_from_fraction(-741914, 1000000);
	const struct fixed31_32 k17 =
		dal_fixed31_32_from_fraction(886000, 1000000);
	const struct fixed31_32 k18 =
		dal_fixed31_32_from_fraction(-144086, 1000000);

	const struct fixed31_32 luma_r =
		dal_fixed31_32_from_fraction(299, 1000);
	const struct fixed31_32 luma_g =
		dal_fixed31_32_from_fraction(587, 1000);
	const struct fixed31_32 luma_b =
		dal_fixed31_32_from_fraction(114, 1000);

	struct fixed31_32 grph_cont;
	struct fixed31_32 grph_sat;
	struct fixed31_32 grph_bright;
	struct fixed31_32 sin_grph_hue;
	struct fixed31_32 cos_grph_hue;

	initialize_fix_point_color_values(
		core_color, sink_index, &grph_cont, &grph_sat,
		&grph_bright, &sin_grph_hue, &cos_grph_hue);

	/* COEF_1_1 = GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K1 +*/
	/* Sin(GrphHue) * K2))*/
	/* (Cos(GrphHue) * K1 + Sin(GrphHue) * K2)*/
	rgb_matrix[0] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k1),
			dal_fixed31_32_mul(sin_grph_hue, k2));
	/* GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue) * K2 */
	rgb_matrix[0] = dal_fixed31_32_mul(grph_sat, rgb_matrix[0]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue) * K2))*/
	rgb_matrix[0] = dal_fixed31_32_add(luma_r, rgb_matrix[0]);
	/* GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue)**/
	/* K2))*/
	rgb_matrix[0] = dal_fixed31_32_mul(grph_cont, rgb_matrix[0]);

	/* COEF_1_2 = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K3 +*/
	/* Sin(GrphHue) * K4))*/
	/* (Cos(GrphHue) * K3 + Sin(GrphHue) * K4)*/
	rgb_matrix[1] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k3),
			dal_fixed31_32_mul(sin_grph_hue, k4));
	/* GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue) * K4)*/
	rgb_matrix[1] = dal_fixed31_32_mul(grph_sat, rgb_matrix[1]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue) * K4))*/
	rgb_matrix[1] = dal_fixed31_32_add(luma_g, rgb_matrix[1]);
	/* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue)**/
	/* K4))*/
	rgb_matrix[1] = dal_fixed31_32_mul(grph_cont, rgb_matrix[1]);

	/* COEF_1_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K5 +*/
	/* Sin(GrphHue) * K6))*/
	/* (Cos(GrphHue) * K5 + Sin(GrphHue) * K6)*/
	rgb_matrix[2] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k5),
			dal_fixed31_32_mul(sin_grph_hue, k6));
	/* GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue) * K6)*/
	rgb_matrix[2] = dal_fixed31_32_mul(grph_sat, rgb_matrix[2]);
	/* LumaB + GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue) * K6)*/
	rgb_matrix[2] = dal_fixed31_32_add(luma_b, rgb_matrix[2]);
	/* GrphCont  * (LumaB + GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue)**/
	/* K6))*/
	rgb_matrix[2] = dal_fixed31_32_mul(grph_cont, rgb_matrix[2]);

	/* COEF_1_4 = GrphBright*/
	rgb_matrix[3] = grph_bright;

	/* COEF_2_1 = GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K7 +*/
	/* Sin(GrphHue) * K8))*/
	/* (Cos(GrphHue) * K7 + Sin(GrphHue) * K8)*/
	rgb_matrix[4] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k7),
			dal_fixed31_32_mul(sin_grph_hue, k8));
	/* GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue) * K8)*/
	rgb_matrix[4] = dal_fixed31_32_mul(grph_sat, rgb_matrix[4]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue) * K8))*/
	rgb_matrix[4] = dal_fixed31_32_add(luma_r, rgb_matrix[4]);
	/* GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue)**/
	/* K8))*/
	rgb_matrix[4] = dal_fixed31_32_mul(grph_cont, rgb_matrix[4]);

	/* COEF_2_2 = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K9 +*/
	/* Sin(GrphHue) * K10))*/
	/* (Cos(GrphHue) * K9 + Sin(GrphHue) * K10))*/
	rgb_matrix[5] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k9),
			dal_fixed31_32_mul(sin_grph_hue, k10));
	/* GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue) * K10))*/
	rgb_matrix[5] = dal_fixed31_32_mul(grph_sat, rgb_matrix[5]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue) * K10))*/
	rgb_matrix[5] = dal_fixed31_32_add(luma_g, rgb_matrix[5]);
	/* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue)**/
	/* K10))*/
	rgb_matrix[5] = dal_fixed31_32_mul(grph_cont, rgb_matrix[5]);

	/* COEF_2_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K11 +*/
	/* Sin(GrphHue) * K12))*/
	/* (Cos(GrphHue) * K11 + Sin(GrphHue) * K12))*/
	rgb_matrix[6] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k11),
			dal_fixed31_32_mul(sin_grph_hue, k12));
	/* GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue) * K12))*/
	rgb_matrix[6] = dal_fixed31_32_mul(grph_sat, rgb_matrix[6]);
	/* (LumaB + GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue) * K12))*/
	rgb_matrix[6] = dal_fixed31_32_add(luma_b, rgb_matrix[6]);
	/* GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue)**/
	/* K12))*/
	rgb_matrix[6] = dal_fixed31_32_mul(grph_cont, rgb_matrix[6]);

	/* COEF_2_4 = GrphBright*/
	rgb_matrix[7] = grph_bright;

	/* COEF_3_1 = GrphCont  * (LumaR + GrphSat * (Cos(GrphHue) * K13 +*/
	/* Sin(GrphHue) * K14))*/
	/* (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	rgb_matrix[8] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k13),
			dal_fixed31_32_mul(sin_grph_hue, k14));
	/* GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	rgb_matrix[8] = dal_fixed31_32_mul(grph_sat, rgb_matrix[8]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	rgb_matrix[8] = dal_fixed31_32_add(luma_r, rgb_matrix[8]);
	/* GrphCont  * (LumaR + GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue)**/
	/* K14)) */
	rgb_matrix[8] = dal_fixed31_32_mul(grph_cont, rgb_matrix[8]);

	/* COEF_3_2    = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K15 +*/
	/* Sin(GrphHue) * K16)) */
	/* GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16) */
	rgb_matrix[9] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k15),
			dal_fixed31_32_mul(sin_grph_hue, k16));
	/* (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16)) */
	rgb_matrix[9] = dal_fixed31_32_mul(grph_sat, rgb_matrix[9]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16)) */
	rgb_matrix[9] = dal_fixed31_32_add(luma_g, rgb_matrix[9]);
	/* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue)**/
	/* K16)) */
	rgb_matrix[9] = dal_fixed31_32_mul(grph_cont, rgb_matrix[9]);

	/*  COEF_3_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K17 +*/
	/* Sin(GrphHue) * K18)) */
	/* (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	rgb_matrix[10] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k17),
			dal_fixed31_32_mul(sin_grph_hue, k18));
	/*  GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	rgb_matrix[10] = dal_fixed31_32_mul(grph_sat, rgb_matrix[10]);
	/* (LumaB + GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	rgb_matrix[10] = dal_fixed31_32_add(luma_b, rgb_matrix[10]);
	/* GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue)**/
	/* K18)) */
	rgb_matrix[10] = dal_fixed31_32_mul(grph_cont, rgb_matrix[10]);

	/* COEF_3_4 = GrphBright */
	rgb_matrix[11] = grph_bright;
}

static void calculate_rgb_limited_range_matrix_legacy(
		struct core_color *core_color, unsigned int sink_index,
		struct fixed31_32 *rgb_matrix)
{
	const struct fixed31_32 k1 =
		dal_fixed31_32_from_fraction(701000, 1000000);
	const struct fixed31_32 k2 =
		dal_fixed31_32_from_fraction(236568, 1000000);
	const struct fixed31_32 k3 =
		dal_fixed31_32_from_fraction(-587000, 1000000);
	const struct fixed31_32 k4 =
		dal_fixed31_32_from_fraction(464432, 1000000);
	const struct fixed31_32 k5 =
		dal_fixed31_32_from_fraction(-114000, 1000000);
	const struct fixed31_32 k6 =
		dal_fixed31_32_from_fraction(-701000, 1000000);
	const struct fixed31_32 k7 =
		dal_fixed31_32_from_fraction(-299000, 1000000);
	const struct fixed31_32 k8 =
		dal_fixed31_32_from_fraction(-292569, 1000000);
	const struct fixed31_32 k9 =
		dal_fixed31_32_from_fraction(413000, 1000000);
	const struct fixed31_32 k10 =
		dal_fixed31_32_from_fraction(-92482, 1000000);
	const struct fixed31_32 k11 =
		dal_fixed31_32_from_fraction(-114000, 1000000);
	const struct fixed31_32 k12 =
		dal_fixed31_32_from_fraction(385051, 1000000);
	const struct fixed31_32 k13 =
		dal_fixed31_32_from_fraction(-299000, 1000000);
	const struct fixed31_32 k14 =
		dal_fixed31_32_from_fraction(886000, 1000000);
	const struct fixed31_32 k15 =
		dal_fixed31_32_from_fraction(-587000, 1000000);
	const struct fixed31_32 k16 =
		dal_fixed31_32_from_fraction(-741914, 1000000);
	const struct fixed31_32 k17 =
		dal_fixed31_32_from_fraction(886000, 1000000);
	const struct fixed31_32 k18 =
		dal_fixed31_32_from_fraction(-144086, 1000000);

	const struct fixed31_32 luma_r =
		dal_fixed31_32_from_fraction(299, 1000);
	const struct fixed31_32 luma_g =
		dal_fixed31_32_from_fraction(587, 1000);
	const struct fixed31_32 luma_b =
		dal_fixed31_32_from_fraction(114, 1000);
	const struct fixed31_32 luma_scale =
		dal_fixed31_32_from_fraction(875855, 1000000);

	const struct fixed31_32 rgb_scale =
		dal_fixed31_32_from_fraction(85546875, 100000000);
	const struct fixed31_32 rgb_bias =
		dal_fixed31_32_from_fraction(625, 10000);

	struct fixed31_32 grph_cont;
	struct fixed31_32 grph_sat;
	struct fixed31_32 grph_bright;
	struct fixed31_32 sin_grph_hue;
	struct fixed31_32 cos_grph_hue;

	initialize_fix_point_color_values(
		core_color, sink_index, &grph_cont, &grph_sat,
		&grph_bright, &sin_grph_hue, &cos_grph_hue);

	/* COEF_1_1 = GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K1 +*/
	/* Sin(GrphHue) * K2))*/
	/* (Cos(GrphHue) * K1 + Sin(GrphHue) * K2)*/
	rgb_matrix[0] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k1),
			dal_fixed31_32_mul(sin_grph_hue, k2));
	/* GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue) * K2 */
	rgb_matrix[0] = dal_fixed31_32_mul(grph_sat, rgb_matrix[0]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue) * K2))*/
	rgb_matrix[0] = dal_fixed31_32_add(luma_r, rgb_matrix[0]);
	/* GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K1 + Sin(GrphHue)**/
	/* K2))*/
	rgb_matrix[0] = dal_fixed31_32_mul(grph_cont, rgb_matrix[0]);
	/* LumaScale * GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K1 + */
	/* Sin(GrphHue) * K2))*/
	rgb_matrix[0] = dal_fixed31_32_mul(luma_scale, rgb_matrix[0]);

	/* COEF_1_2 = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K3 +*/
	/* Sin(GrphHue) * K4))*/
	/* (Cos(GrphHue) * K3 + Sin(GrphHue) * K4)*/
	rgb_matrix[1] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k3),
			dal_fixed31_32_mul(sin_grph_hue, k4));
	/* GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue) * K4)*/
	rgb_matrix[1] = dal_fixed31_32_mul(grph_sat, rgb_matrix[1]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue) * K4))*/
	rgb_matrix[1] = dal_fixed31_32_add(luma_g, rgb_matrix[1]);
	/* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K3 + Sin(GrphHue)**/
	/* K4))*/
	rgb_matrix[1] = dal_fixed31_32_mul(grph_cont, rgb_matrix[1]);
	/* LumaScale * GrphCont * (LumaG + GrphSat *(Cos(GrphHue) * K3 + */
	/* Sin(GrphHue) * K4))*/
	rgb_matrix[1] = dal_fixed31_32_mul(luma_scale, rgb_matrix[1]);

	/* COEF_1_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K5 +*/
	/* Sin(GrphHue) * K6))*/
	/* (Cos(GrphHue) * K5 + Sin(GrphHue) * K6)*/
	rgb_matrix[2] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k5),
			dal_fixed31_32_mul(sin_grph_hue, k6));
	/* GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue) * K6)*/
	rgb_matrix[2] = dal_fixed31_32_mul(grph_sat, rgb_matrix[2]);
	/* LumaB + GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue) * K6)*/
	rgb_matrix[2] = dal_fixed31_32_add(luma_b, rgb_matrix[2]);
	/* GrphCont  * (LumaB + GrphSat * (Cos(GrphHue) * K5 + Sin(GrphHue)**/
	/* K6))*/
	rgb_matrix[2] = dal_fixed31_32_mul(grph_cont, rgb_matrix[2]);
	/* LumaScale * GrphCont  * (LumaB + GrphSat *(Cos(GrphHue) * K5 + */
	/* Sin(GrphHue) * K6))*/
	rgb_matrix[2] = dal_fixed31_32_mul(luma_scale, rgb_matrix[2]);

	/* COEF_1_4 = RGBBias + RGBScale * GrphBright*/
	rgb_matrix[3] = dal_fixed31_32_add(
			rgb_bias,
			dal_fixed31_32_mul(rgb_scale, grph_bright));

	/* COEF_2_1 = GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K7 +*/
	/* Sin(GrphHue) * K8))*/
	/* (Cos(GrphHue) * K7 + Sin(GrphHue) * K8)*/
	rgb_matrix[4] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k7),
			dal_fixed31_32_mul(sin_grph_hue, k8));
	/* GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue) * K8)*/
	rgb_matrix[4] = dal_fixed31_32_mul(grph_sat, rgb_matrix[4]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue) * K8))*/
	rgb_matrix[4] = dal_fixed31_32_add(luma_r, rgb_matrix[4]);
	/* GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K7 + Sin(GrphHue)**/
	/* K8))*/
	rgb_matrix[4] = dal_fixed31_32_mul(grph_cont, rgb_matrix[4]);
	/* LumaScale * GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K7 + */
	/* Sin(GrphHue) * K8))*/
	rgb_matrix[4] = dal_fixed31_32_mul(luma_scale, rgb_matrix[4]);

	/* COEF_2_2 = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K9 +*/
	/* Sin(GrphHue) * K10))*/
	/* (Cos(GrphHue) * K9 + Sin(GrphHue) * K10))*/
	rgb_matrix[5] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k9),
			dal_fixed31_32_mul(sin_grph_hue, k10));
	/* GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue) * K10))*/
	rgb_matrix[5] = dal_fixed31_32_mul(grph_sat, rgb_matrix[5]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue) * K10))*/
	rgb_matrix[5] = dal_fixed31_32_add(luma_g, rgb_matrix[5]);
	/* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K9 + Sin(GrphHue)**/
	/* K10))*/
	rgb_matrix[5] = dal_fixed31_32_mul(grph_cont, rgb_matrix[5]);
	/* LumaScale * GrphCont * (LumaG + GrphSat *(Cos(GrphHue) * K9 + */
	/* Sin(GrphHue) * K10))*/
	rgb_matrix[5] = dal_fixed31_32_mul(luma_scale, rgb_matrix[5]);

	/* COEF_2_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K11 +*/
	/* Sin(GrphHue) * K12))*/
	/* (Cos(GrphHue) * K11 + Sin(GrphHue) * K12))*/
	rgb_matrix[6] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k11),
			dal_fixed31_32_mul(sin_grph_hue, k12));
	/* GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue) * K12))*/
	rgb_matrix[6] = dal_fixed31_32_mul(grph_sat, rgb_matrix[6]);
	/* (LumaB + GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue) * K12))*/
	rgb_matrix[6] = dal_fixed31_32_add(luma_b, rgb_matrix[6]);
	/* GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K11 + Sin(GrphHue)**/
	/* K12))*/
	rgb_matrix[6] = dal_fixed31_32_mul(grph_cont, rgb_matrix[6]);
	/* LumaScale * GrphCont  * (LumaB + GrphSat *(Cos(GrphHue) * K11 +*/
	/* Sin(GrphHue) * K12)) */
	rgb_matrix[6] = dal_fixed31_32_mul(luma_scale, rgb_matrix[6]);

	/* COEF_2_4 = RGBBias + RGBScale * GrphBright*/
	rgb_matrix[7] = dal_fixed31_32_add(
			rgb_bias,
			dal_fixed31_32_mul(rgb_scale, grph_bright));

	/* COEF_3_1 = GrphCont  * (LumaR + GrphSat * (Cos(GrphHue) * K13 +*/
	/* Sin(GrphHue) * K14))*/
	/* (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	rgb_matrix[8] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k13),
			dal_fixed31_32_mul(sin_grph_hue, k14));
	/* GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	rgb_matrix[8] = dal_fixed31_32_mul(grph_sat, rgb_matrix[8]);
	/* (LumaR + GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue) * K14)) */
	rgb_matrix[8] = dal_fixed31_32_add(luma_r, rgb_matrix[8]);
	/* GrphCont  * (LumaR + GrphSat * (Cos(GrphHue) * K13 + Sin(GrphHue)**/
	/* K14)) */
	rgb_matrix[8] = dal_fixed31_32_mul(grph_cont, rgb_matrix[8]);
	/* LumaScale * GrphCont * (LumaR + GrphSat * (Cos(GrphHue) * K13 +*/
	/* Sin(GrphHue) * K14))*/
	rgb_matrix[8] = dal_fixed31_32_mul(luma_scale, rgb_matrix[8]);

	/* COEF_3_2    = GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K15 +*/
	/* Sin(GrphHue) * K16)) */
	/* GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16) */
	rgb_matrix[9] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k15),
			dal_fixed31_32_mul(sin_grph_hue, k16));
	/* (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16)) */
	rgb_matrix[9] = dal_fixed31_32_mul(grph_sat, rgb_matrix[9]);
	/* (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue) * K16)) */
	rgb_matrix[9] = dal_fixed31_32_add(luma_g, rgb_matrix[9]);
	/* GrphCont * (LumaG + GrphSat * (Cos(GrphHue) * K15 + Sin(GrphHue)**/
	/* K16)) */
	rgb_matrix[9] = dal_fixed31_32_mul(grph_cont, rgb_matrix[9]);
	/* LumaScale * GrphCont * (LumaG + GrphSat *(Cos(GrphHue) * K15 + */
	/* Sin(GrphHue) * K16))*/
	rgb_matrix[9] = dal_fixed31_32_mul(luma_scale, rgb_matrix[9]);

	/*  COEF_3_3 = GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K17 +*/
	/* Sin(GrphHue) * K18)) */
	/* (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	rgb_matrix[10] =
		dal_fixed31_32_add(
			dal_fixed31_32_mul(cos_grph_hue, k17),
			dal_fixed31_32_mul(sin_grph_hue, k18));
	/*  GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	rgb_matrix[10] = dal_fixed31_32_mul(grph_sat, rgb_matrix[10]);
	/* (LumaB + GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue) * K18)) */
	rgb_matrix[10] = dal_fixed31_32_add(luma_b, rgb_matrix[10]);
	/* GrphCont * (LumaB + GrphSat * (Cos(GrphHue) * K17 + Sin(GrphHue)**/
	/* K18)) */
	rgb_matrix[10] = dal_fixed31_32_mul(grph_cont, rgb_matrix[10]);
	/* LumaScale * GrphCont * (LumaB + GrphSat *(Cos(GrphHue) * */
	/* K17 + Sin(GrphHue) * K18))*/
	rgb_matrix[10] = dal_fixed31_32_mul(luma_scale, rgb_matrix[10]);

	/* COEF_3_4 = RGBBias + RGBScale * GrphBright */
	rgb_matrix[11] = dal_fixed31_32_add(
			rgb_bias,
			dal_fixed31_32_mul(rgb_scale, grph_bright));
}

static void calculate_yuv_matrix(struct core_color *core_color,
		unsigned int sink_index,
		enum dc_color_space color_space,
		struct fixed31_32 *yuv_matrix)
{
	struct fixed31_32 ideal[12];
	uint32_t i = 0;

	if ((color_space == COLOR_SPACE_YPBPR601) ||
			(color_space == COLOR_SPACE_YCBCR601) ||
			(color_space == COLOR_SPACE_YCBCR601_LIMITED)) {
		static const int32_t matrix_[] = {
				25578516, 50216016, 9752344, 6250000,
				-14764391, -28985609, 43750000, 50000000,
				43750000, -36635164, -7114836, 50000000
			};
		do {
			ideal[i] = dal_fixed31_32_from_fraction(
					matrix_[i],
				100000000);
			++i;
		} while (i != ARRAY_SIZE(matrix_));
	} else {
		static const int32_t matrix_[] = {
				18187266, 61183125, 6176484, 6250000,
				-10025059, -33724941, 43750000, 50000000,
				43750000, -39738379, -4011621, 50000000
			};
		do {
			ideal[i] = dal_fixed31_32_from_fraction(
					matrix_[i],
				100000000);
			++i;
		} while (i != ARRAY_SIZE(matrix_));
	}

	struct fixed31_32 grph_cont;
	struct fixed31_32 grph_sat;
	struct fixed31_32 grph_bright;
	struct fixed31_32 sin_grph_hue;
	struct fixed31_32 cos_grph_hue;

	initialize_fix_point_color_values(
		core_color, sink_index, &grph_cont, &grph_sat,
		&grph_bright, &sin_grph_hue, &cos_grph_hue);

	const struct fixed31_32 multiplier =
			dal_fixed31_32_mul(grph_cont, grph_sat);

	yuv_matrix[0] = dal_fixed31_32_mul(ideal[0], grph_cont);

	yuv_matrix[1] = dal_fixed31_32_mul(ideal[1], grph_cont);

	yuv_matrix[2] = dal_fixed31_32_mul(ideal[2], grph_cont);

	yuv_matrix[4] = dal_fixed31_32_mul(
			multiplier,
			dal_fixed31_32_add(
				dal_fixed31_32_mul(
					ideal[4],
					cos_grph_hue),
				dal_fixed31_32_mul(
					ideal[8],
					sin_grph_hue)));

	yuv_matrix[5] = dal_fixed31_32_mul(
			multiplier,
			dal_fixed31_32_add(
				dal_fixed31_32_mul(
					ideal[5],
					cos_grph_hue),
				dal_fixed31_32_mul(
					ideal[9],
					sin_grph_hue)));

	yuv_matrix[6] = dal_fixed31_32_mul(
			multiplier,
			dal_fixed31_32_add(
				dal_fixed31_32_mul(
					ideal[6],
					cos_grph_hue),
				dal_fixed31_32_mul(
					ideal[10],
					sin_grph_hue)));

	yuv_matrix[7] = ideal[7];

	yuv_matrix[8] = dal_fixed31_32_mul(
			multiplier,
			dal_fixed31_32_sub(
				dal_fixed31_32_mul(
					ideal[8],
					cos_grph_hue),
				dal_fixed31_32_mul(
					ideal[4],
					sin_grph_hue)));

	yuv_matrix[9] = dal_fixed31_32_mul(
			multiplier,
			dal_fixed31_32_sub(
				dal_fixed31_32_mul(
					ideal[9],
					cos_grph_hue),
				dal_fixed31_32_mul(
					ideal[5],
					sin_grph_hue)));

	yuv_matrix[10] = dal_fixed31_32_mul(
			multiplier,
			dal_fixed31_32_sub(
				dal_fixed31_32_mul(
					ideal[10],
					cos_grph_hue),
				dal_fixed31_32_mul(
					ideal[6],
					sin_grph_hue)));

	yuv_matrix[11] = ideal[11];

	if ((color_space == COLOR_SPACE_YCBCR601_LIMITED) ||
			(color_space == COLOR_SPACE_YCBCR709_LIMITED)) {
		yuv_matrix[3] = dal_fixed31_32_add(ideal[3], grph_bright);
	} else {
		yuv_matrix[3] = dal_fixed31_32_add(
			ideal[3],
			dal_fixed31_32_mul(
				grph_bright,
				dal_fixed31_32_from_fraction(86, 100)));
	}
}

static void calculate_csc_matrix(struct core_color *core_color,
		unsigned int sink_index,
		enum dc_color_space color_space,
		uint16_t *csc_matrix)
{
	struct fixed31_32 fixed_csc_matrix[12];
	switch (color_space) {
	case COLOR_SPACE_SRGB:
		calculate_rgb_matrix_legacy
			(core_color, sink_index, fixed_csc_matrix);
		convert_float_matrix_legacy
			(csc_matrix, fixed_csc_matrix, 12);
		break;
	case COLOR_SPACE_SRGB_LIMITED:
		calculate_rgb_limited_range_matrix_legacy(
				core_color, sink_index, fixed_csc_matrix);
		convert_float_matrix_legacy(csc_matrix, fixed_csc_matrix, 12);
		break;
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR601_LIMITED:
	case COLOR_SPACE_YCBCR709_LIMITED:
	case COLOR_SPACE_YPBPR601:
	case COLOR_SPACE_YPBPR709:
		calculate_yuv_matrix(core_color, sink_index, color_space,
				fixed_csc_matrix);
		convert_float_matrix(csc_matrix, fixed_csc_matrix, 12);
		break;
	default:
		calculate_rgb_matrix_legacy
			(core_color, sink_index, fixed_csc_matrix);
		convert_float_matrix_legacy
			(csc_matrix, fixed_csc_matrix, 12);
		break;
	}
}

struct mod_color *mod_color_create(struct dc *dc)
{
	int i = 0;
	struct core_color *core_color =
				dm_alloc(sizeof(struct core_color));
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct persistent_data_flag flag;

	if (core_color == NULL)
		goto fail_alloc_context;

	core_color->caps = dm_alloc(sizeof(struct sink_caps) *
			MOD_COLOR_MAX_CONCURRENT_SINKS);

	if (core_color->caps == NULL)
		goto fail_alloc_caps;

	for (i = 0; i < MOD_COLOR_MAX_CONCURRENT_SINKS; i++)
		core_color->caps[i].sink = NULL;

	core_color->state = dm_alloc(sizeof(struct color_state) *
			MOD_COLOR_MAX_CONCURRENT_SINKS);

	/*hardcoded to sRGB with 6500 color temperature*/
	for (i = 0; i < MOD_COLOR_MAX_CONCURRENT_SINKS; i++) {
		core_color->state[i].source_gamut.blueX = 1500;
		core_color->state[i].source_gamut.blueY = 600;
		core_color->state[i].source_gamut.greenX = 3000;
		core_color->state[i].source_gamut.greenY = 6000;
		core_color->state[i].source_gamut.redX = 6400;
		core_color->state[i].source_gamut.redY = 3300;
		core_color->state[i].source_gamut.whiteX = 3127;
		core_color->state[i].source_gamut.whiteY = 3290;

		core_color->state[i].destination_gamut.blueX = 1500;
		core_color->state[i].destination_gamut.blueY = 600;
		core_color->state[i].destination_gamut.greenX = 3000;
		core_color->state[i].destination_gamut.greenY = 6000;
		core_color->state[i].destination_gamut.redX = 6400;
		core_color->state[i].destination_gamut.redY = 3300;
		core_color->state[i].destination_gamut.whiteX = 3127;
		core_color->state[i].destination_gamut.whiteY = 3290;

		core_color->state[i].custom_color_temperature = 6500;

		core_color->state[i].contrast.current = 100;
		core_color->state[i].contrast.min = 0;
		core_color->state[i].contrast.max = 200;

		core_color->state[i].saturation.current = 100;
		core_color->state[i].saturation.min = 0;
		core_color->state[i].saturation.max = 200;

		core_color->state[i].brightness.current = 0;
		core_color->state[i].brightness.min = -100;
		core_color->state[i].brightness.max = 100;

		core_color->state[i].hue.current = 0;
		core_color->state[i].hue.min = -30;
		core_color->state[i].hue.max = 30;
	}

	if (core_color->state == NULL)
		goto fail_alloc_state;

	core_color->num_sinks = 0;

	if (dc == NULL)
		goto fail_construct;

	core_color->dc = dc;

	if (!check_dc_support(dc))
		goto fail_construct;

	/* Create initial module folder in registry for color adjustment */
	flag.save_per_edid = true;
	flag.save_per_link = false;

	dm_write_persistent_data(core_dc->ctx, NULL, COLOR_REGISTRY_NAME, NULL,
			NULL, 0, &flag);

	return &core_color->public;

fail_construct:
	dm_free(core_color->state);

fail_alloc_state:
	dm_free(core_color->caps);

fail_alloc_caps:
	dm_free(core_color);

fail_alloc_context:
	return NULL;
}

void mod_color_destroy(struct mod_color *mod_color)
{
	if (mod_color != NULL) {
		int i;
		struct core_color *core_color =
				MOD_COLOR_TO_CORE(mod_color);

		dm_free(core_color->state);

		for (i = 0; i < core_color->num_sinks; i++)
			dc_sink_release(core_color->caps[i].sink);

		dm_free(core_color->caps);

		dm_free(core_color);
	}
}

bool mod_color_add_sink(struct mod_color *mod_color, const struct dc_sink *sink)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);
	struct core_dc *core_dc = DC_TO_CORE(core_color->dc);
	bool persistent_color_temp_enable;
	int persistent_custom_color_temp = 0;
	struct color_space_coordinates persistent_source_gamut;
	struct color_space_coordinates persistent_destination_gamut;
	int persistent_brightness;
	int persistent_contrast;
	int persistent_hue;
	int persistent_saturation;
	enum dc_quantization_range persistent_quantization_range;
	struct persistent_data_flag flag;

	if (core_color->num_sinks < MOD_COLOR_MAX_CONCURRENT_SINKS) {
		dc_sink_retain(sink);
		core_color->caps[core_color->num_sinks].sink = sink;
		core_color->state[core_color->num_sinks].
				user_enable_color_temperature = true;

		/* get persistent data from registry */
		flag.save_per_edid = true;
		flag.save_per_link = false;


		if (dm_read_persistent_data(core_dc->ctx, sink,
						COLOR_REGISTRY_NAME,
						"enablecolortempadj",
						&persistent_color_temp_enable,
						sizeof(bool), &flag))
			core_color->state[core_color->num_sinks].
				user_enable_color_temperature =
						persistent_color_temp_enable;
		else
			core_color->state[core_color->num_sinks].
				user_enable_color_temperature = true;

		if (dm_read_persistent_data(core_dc->ctx, sink,
						COLOR_REGISTRY_NAME,
						"customcolortemp",
						&persistent_custom_color_temp,
						sizeof(int), &flag))
			core_color->state[core_color->num_sinks].
					custom_color_temperature
					= persistent_custom_color_temp;
		else
			core_color->state[core_color->num_sinks].
					custom_color_temperature = 6500;

		if (dm_read_persistent_data(core_dc->ctx, sink,
					COLOR_REGISTRY_NAME,
					"sourcegamut",
					&persistent_source_gamut,
					sizeof(struct color_space_coordinates),
					&flag)) {
			memcpy(&core_color->state[core_color->num_sinks].
				source_gamut, &persistent_source_gamut,
				sizeof(struct color_space_coordinates));
		} else {
			core_color->state[core_color->num_sinks].
					source_gamut.blueX = 1500;
			core_color->state[core_color->num_sinks].
					source_gamut.blueY = 600;
			core_color->state[core_color->num_sinks].
					source_gamut.greenX = 3000;
			core_color->state[core_color->num_sinks].
					source_gamut.greenY = 6000;
			core_color->state[core_color->num_sinks].
					source_gamut.redX = 6400;
			core_color->state[core_color->num_sinks].
					source_gamut.redY = 3300;
			core_color->state[core_color->num_sinks].
					source_gamut.whiteX = 3127;
			core_color->state[core_color->num_sinks].
					source_gamut.whiteY = 3290;
		}

		if (dm_read_persistent_data(core_dc->ctx, sink, COLOR_REGISTRY_NAME,
					"destgamut",
					&persistent_destination_gamut,
					sizeof(struct color_space_coordinates),
					&flag)) {
			memcpy(&core_color->state[core_color->num_sinks].
				destination_gamut,
				&persistent_destination_gamut,
				sizeof(struct color_space_coordinates));
		} else {
			core_color->state[core_color->num_sinks].
					destination_gamut.blueX = 1500;
			core_color->state[core_color->num_sinks].
					destination_gamut.blueY = 600;
			core_color->state[core_color->num_sinks].
					destination_gamut.greenX = 3000;
			core_color->state[core_color->num_sinks].
					destination_gamut.greenY = 6000;
			core_color->state[core_color->num_sinks].
					destination_gamut.redX = 6400;
			core_color->state[core_color->num_sinks].
					destination_gamut.redY = 3300;
			core_color->state[core_color->num_sinks].
					destination_gamut.whiteX = 3127;
			core_color->state[core_color->num_sinks].
					destination_gamut.whiteY = 3290;
		}

		if (dm_read_persistent_data(core_dc->ctx, sink, COLOR_REGISTRY_NAME,
						"brightness",
						&persistent_brightness,
						sizeof(int), &flag))
			core_color->state[core_color->num_sinks].
				brightness.current = persistent_brightness;
		else
			core_color->state[core_color->num_sinks].
				brightness.current = 0;

		if (dm_read_persistent_data(core_dc->ctx, sink, COLOR_REGISTRY_NAME,
						"contrast",
						&persistent_contrast,
						sizeof(int), &flag))
			core_color->state[core_color->num_sinks].
				contrast.current = persistent_contrast;
		else
			core_color->state[core_color->num_sinks].
				contrast.current = 100;

		if (dm_read_persistent_data(core_dc->ctx, sink, COLOR_REGISTRY_NAME,
						"hue",
						&persistent_hue,
						sizeof(int), &flag))
			core_color->state[core_color->num_sinks].
				hue.current = persistent_hue;
		else
			core_color->state[core_color->num_sinks].
				hue.current = 0;

		if (dm_read_persistent_data(core_dc->ctx, sink, COLOR_REGISTRY_NAME,
						"saturation",
						&persistent_saturation,
						sizeof(int), &flag))
			core_color->state[core_color->num_sinks].
				saturation.current = persistent_saturation;
		else
			core_color->state[core_color->num_sinks].
				saturation.current = 100;

		if (dm_read_persistent_data(core_dc->ctx, sink,
						COLOR_REGISTRY_NAME,
						"preferred_quantization_range",
						&persistent_quantization_range,
						sizeof(int), &flag))
			core_color->state[core_color->num_sinks].
			preferred_quantization_range =
					persistent_quantization_range;
		else
			core_color->state[core_color->num_sinks].
			preferred_quantization_range = QUANTIZATION_RANGE_FULL;

		core_color->num_sinks++;
		return true;
	}
	return false;
}

bool mod_color_remove_sink(struct mod_color *mod_color,
		const struct dc_sink *sink)
{
	int i = 0, j = 0;
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);

	for (i = 0; i < core_color->num_sinks; i++) {
		if (core_color->caps[i].sink == sink) {
			/* To remove this sink, shift everything after down */
			for (j = i; j < core_color->num_sinks - 1; j++) {
				core_color->caps[j].sink =
					core_color->caps[j + 1].sink;

				memcpy(&core_color->state[j],
					&core_color->state[j + 1],
					sizeof(struct color_state));
			}

			core_color->num_sinks--;

			dc_sink_release(sink);

			return true;
		}
	}

	return false;
}

bool mod_color_update_gamut_to_stream(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);
	struct core_dc *core_dc = DC_TO_CORE(core_color->dc);
	struct persistent_data_flag flag;
	struct gamut_src_dst_matrix *matrix =
			dm_alloc(sizeof(struct gamut_src_dst_matrix));

	unsigned int stream_index, sink_index, j;

	for (stream_index = 0; stream_index < num_streams; stream_index++) {
		sink_index = sink_index_from_sink(core_color,
				streams[stream_index]->sink);

		/* Write persistent data in registry*/
		flag.save_per_edid = true;
		flag.save_per_link = false;

		dm_write_persistent_data(core_dc->ctx,
					streams[stream_index]->sink,
					COLOR_REGISTRY_NAME,
					"sourcegamut",
					&core_color->state[sink_index].
							source_gamut,
					sizeof(struct color_space_coordinates),
					&flag);

		dm_write_persistent_data(core_dc->ctx,
					streams[stream_index]->sink,
					COLOR_REGISTRY_NAME,
					"destgamut",
					&core_color->state[sink_index].
							destination_gamut,
					sizeof(struct color_space_coordinates),
					&flag);

		if (!build_gamut_remap_matrix
				(core_color->state[sink_index].source_gamut,
						matrix->rgbCoeffSrc,
						matrix->whiteCoeffSrc))
			goto function_fail;

		if (!build_gamut_remap_matrix
				(core_color->state[sink_index].
				destination_gamut,
				matrix->rgbCoeffDst, matrix->whiteCoeffDst))
			goto function_fail;

		struct fixed31_32 gamut_result[12];
		struct fixed31_32 temp_matrix[9];

		if (!gamut_to_color_matrix(
				matrix->rgbCoeffDst,
				matrix->whiteCoeffDst,
				matrix->rgbCoeffSrc,
				matrix->whiteCoeffSrc,
				true,
				temp_matrix))
			goto function_fail;

		gamut_result[0] = temp_matrix[0];
		gamut_result[1] = temp_matrix[1];
		gamut_result[2] = temp_matrix[2];
		gamut_result[3] = matrix->whiteCoeffSrc[0];
		gamut_result[4] = temp_matrix[3];
		gamut_result[5] = temp_matrix[4];
		gamut_result[6] = temp_matrix[5];
		gamut_result[7] = matrix->whiteCoeffSrc[1];
		gamut_result[8] = temp_matrix[6];
		gamut_result[9] = temp_matrix[7];
		gamut_result[10] = temp_matrix[8];
		gamut_result[11] = matrix->whiteCoeffSrc[2];

		struct core_stream *core_stream =
				DC_STREAM_TO_CORE
				(streams[stream_index]);

		core_stream->public.gamut_remap_matrix.enable_remap = true;

		for (j = 0; j < 12; j++)
			core_stream->public.
			gamut_remap_matrix.matrix[j] =
					gamut_result[j];
	}

	dm_free(matrix);
	core_color->dc->stream_funcs.set_gamut_remap
			(core_color->dc, streams, num_streams);

	return true;

function_fail:
	dm_free(matrix);
	return false;
}

bool mod_color_adjust_source_gamut(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		struct gamut_space_coordinates *input_gamut_coordinates,
		struct white_point_coodinates *input_white_point_coordinates)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);

	unsigned int stream_index, sink_index;

	for (stream_index = 0; stream_index < num_streams; stream_index++) {
		sink_index = sink_index_from_sink(core_color,
				streams[stream_index]->sink);

		core_color->state[sink_index].source_gamut.blueX =
				input_gamut_coordinates->blueX;
		core_color->state[sink_index].source_gamut.blueY =
				input_gamut_coordinates->blueY;
		core_color->state[sink_index].source_gamut.greenX =
				input_gamut_coordinates->greenX;
		core_color->state[sink_index].source_gamut.greenY =
				input_gamut_coordinates->greenY;
		core_color->state[sink_index].source_gamut.redX =
				input_gamut_coordinates->redX;
		core_color->state[sink_index].source_gamut.redY =
				input_gamut_coordinates->redY;
		core_color->state[sink_index].source_gamut.whiteX =
				input_white_point_coordinates->whiteX;
		core_color->state[sink_index].source_gamut.whiteY =
				input_white_point_coordinates->whiteY;
	}

	if (!mod_color_update_gamut_to_stream(mod_color, streams, num_streams))
		return false;

	return true;
}

bool mod_color_adjust_destination_gamut(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		struct gamut_space_coordinates *input_gamut_coordinates,
		struct white_point_coodinates *input_white_point_coordinates)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);

	unsigned int stream_index, sink_index;

	for (stream_index = 0; stream_index < num_streams; stream_index++) {
		sink_index = sink_index_from_sink(core_color,
				streams[stream_index]->sink);

		core_color->state[sink_index].destination_gamut.blueX =
				input_gamut_coordinates->blueX;
		core_color->state[sink_index].destination_gamut.blueY =
				input_gamut_coordinates->blueY;
		core_color->state[sink_index].destination_gamut.greenX =
				input_gamut_coordinates->greenX;
		core_color->state[sink_index].destination_gamut.greenY =
				input_gamut_coordinates->greenY;
		core_color->state[sink_index].destination_gamut.redX =
				input_gamut_coordinates->redX;
		core_color->state[sink_index].destination_gamut.redY =
				input_gamut_coordinates->redY;
		core_color->state[sink_index].destination_gamut.whiteX =
				input_white_point_coordinates->whiteX;
		core_color->state[sink_index].destination_gamut.whiteY =
				input_white_point_coordinates->whiteY;
	}

	if (!mod_color_update_gamut_to_stream(mod_color, streams, num_streams))
		return false;

	return true;
}

bool mod_color_set_white_point(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		struct white_point_coodinates *white_point)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);

	unsigned int stream_index, sink_index;

	for (stream_index = 0; stream_index < num_streams;
			stream_index++) {
		sink_index = sink_index_from_sink(core_color,
				streams[stream_index]->sink);
		core_color->state[sink_index].source_gamut.whiteX =
				white_point->whiteX;
		core_color->state[sink_index].source_gamut.whiteY =
				white_point->whiteY;
	}

	if (!mod_color_update_gamut_to_stream(mod_color, streams, num_streams))
		return false;

	return true;
}

bool mod_color_set_user_enable(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		bool user_enable)
{
	struct core_color *core_color =
			MOD_COLOR_TO_CORE(mod_color);
	struct core_dc *core_dc = DC_TO_CORE(core_color->dc);
	struct persistent_data_flag flag;
	unsigned int stream_index, sink_index;

	for (stream_index = 0; stream_index < num_streams; stream_index++) {
		sink_index = sink_index_from_sink(core_color,
				streams[stream_index]->sink);
		core_color->state[sink_index].user_enable_color_temperature
				= user_enable;

		/* Write persistent data in registry*/
		flag.save_per_edid = true;
		flag.save_per_link = false;

		dm_write_persistent_data(core_dc->ctx,
					streams[stream_index]->sink,
					COLOR_REGISTRY_NAME,
					"enablecolortempadj",
					&user_enable,
					sizeof(bool),
					&flag);
	}
	return true;
}

bool mod_color_get_user_enable(struct mod_color *mod_color,
		const struct dc_sink *sink,
		bool *user_enable)
{
	struct core_color *core_color =
			MOD_COLOR_TO_CORE(mod_color);

	unsigned int sink_index = sink_index_from_sink(core_color, sink);

	*user_enable = core_color->state[sink_index].
					user_enable_color_temperature;

	return true;
}

bool mod_color_get_custom_color_temperature(struct mod_color *mod_color,
		const struct dc_sink *sink,
		int *color_temperature)
{
	struct core_color *core_color =
			MOD_COLOR_TO_CORE(mod_color);

	unsigned int sink_index = sink_index_from_sink(core_color, sink);

	*color_temperature = core_color->state[sink_index].
			custom_color_temperature;

	return true;
}

bool mod_color_set_custom_color_temperature(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		int color_temperature)
{
	struct core_color *core_color =
			MOD_COLOR_TO_CORE(mod_color);
	struct core_dc *core_dc = DC_TO_CORE(core_color->dc);
	struct persistent_data_flag flag;
	unsigned int stream_index, sink_index;

	for (stream_index = 0; stream_index < num_streams; stream_index++) {
		sink_index = sink_index_from_sink(core_color,
				streams[stream_index]->sink);
		core_color->state[sink_index].custom_color_temperature
				= color_temperature;

		/* Write persistent data in registry*/
		flag.save_per_edid = true;
		flag.save_per_link = false;

		dm_write_persistent_data(core_dc->ctx,
					streams[stream_index]->sink,
					COLOR_REGISTRY_NAME,
					"customcolortemp",
					&color_temperature,
					sizeof(int),
					&flag);
	}
	return true;
}

bool mod_color_get_color_saturation(struct mod_color *mod_color,
		const struct dc_sink *sink,
		struct color_range *color_saturation)
{
	struct core_color *core_color =
			MOD_COLOR_TO_CORE(mod_color);

	unsigned int sink_index = sink_index_from_sink(core_color, sink);

	*color_saturation = core_color->state[sink_index].saturation;

	return true;
}

bool mod_color_get_color_contrast(struct mod_color *mod_color,
		const struct dc_sink *sink,
		struct color_range *color_contrast)
{
	struct core_color *core_color =
			MOD_COLOR_TO_CORE(mod_color);

	unsigned int sink_index = sink_index_from_sink(core_color, sink);

	*color_contrast = core_color->state[sink_index].contrast;

	return true;
}

bool mod_color_get_color_brightness(struct mod_color *mod_color,
		const struct dc_sink *sink,
		struct color_range *color_brightness)
{
	struct core_color *core_color =
			MOD_COLOR_TO_CORE(mod_color);

	unsigned int sink_index = sink_index_from_sink(core_color, sink);

	*color_brightness = core_color->state[sink_index].brightness;

	return true;
}

bool mod_color_get_color_hue(struct mod_color *mod_color,
		const struct dc_sink *sink,
		struct color_range *color_hue)
{
	struct core_color *core_color =
			MOD_COLOR_TO_CORE(mod_color);

	unsigned int sink_index = sink_index_from_sink(core_color, sink);

	*color_hue = core_color->state[sink_index].hue;

	return true;
}

bool mod_color_get_source_gamut(struct mod_color *mod_color,
		const struct dc_sink *sink,
		struct color_space_coordinates *source_gamut)
{
	struct core_color *core_color =
			MOD_COLOR_TO_CORE(mod_color);

	unsigned int sink_index = sink_index_from_sink(core_color, sink);

	*source_gamut = core_color->state[sink_index].source_gamut;

	return true;
}

bool mod_color_notify_mode_change(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);

	struct gamut_src_dst_matrix *matrix =
			dm_alloc(sizeof(struct gamut_src_dst_matrix));

	unsigned int stream_index, sink_index, j;

	for (stream_index = 0; stream_index < num_streams; stream_index++) {
		sink_index = sink_index_from_sink(core_color,
				streams[stream_index]->sink);

		if (!build_gamut_remap_matrix
				(core_color->state[sink_index].source_gamut,
						matrix->rgbCoeffSrc,
						matrix->whiteCoeffSrc))
			goto function_fail;

		if (!build_gamut_remap_matrix
				(core_color->state[sink_index].
				destination_gamut,
				matrix->rgbCoeffDst, matrix->whiteCoeffDst))
			goto function_fail;

		struct fixed31_32 gamut_result[12];
		struct fixed31_32 temp_matrix[9];

		if (!gamut_to_color_matrix(
				matrix->rgbCoeffDst,
				matrix->whiteCoeffDst,
				matrix->rgbCoeffSrc,
				matrix->whiteCoeffSrc,
				true,
				temp_matrix))
			goto function_fail;

		gamut_result[0] = temp_matrix[0];
		gamut_result[1] = temp_matrix[1];
		gamut_result[2] = temp_matrix[2];
		gamut_result[3] = matrix->whiteCoeffSrc[0];
		gamut_result[4] = temp_matrix[3];
		gamut_result[5] = temp_matrix[4];
		gamut_result[6] = temp_matrix[5];
		gamut_result[7] = matrix->whiteCoeffSrc[1];
		gamut_result[8] = temp_matrix[6];
		gamut_result[9] = temp_matrix[7];
		gamut_result[10] = temp_matrix[8];
		gamut_result[11] = matrix->whiteCoeffSrc[2];


		struct core_stream *core_stream =
				DC_STREAM_TO_CORE
				(streams[stream_index]);

		core_stream->public.gamut_remap_matrix.enable_remap = true;

		for (j = 0; j < 12; j++)
			core_stream->public.
			gamut_remap_matrix.matrix[j] =
					gamut_result[j];

		calculate_csc_matrix(core_color, sink_index,
				core_stream->public.output_color_space,
				core_stream->public.csc_color_matrix.matrix);

		core_stream->public.csc_color_matrix.enable_adjustment = true;
	}

	dm_free(matrix);

	return true;

function_fail:
	dm_free(matrix);
	return false;
}

bool mod_color_set_brightness(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		int brightness_value)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);
	struct core_dc *core_dc = DC_TO_CORE(core_color->dc);
	struct persistent_data_flag flag;
	unsigned int stream_index, sink_index;

	for (stream_index = 0; stream_index < num_streams; stream_index++) {
		sink_index = sink_index_from_sink(core_color,
				streams[stream_index]->sink);

		struct core_stream *core_stream =
						DC_STREAM_TO_CORE
						(streams[stream_index]);

		core_color->state[sink_index].brightness.current =
				brightness_value;

		calculate_csc_matrix(core_color, sink_index,
				core_stream->public.output_color_space,
				core_stream->public.csc_color_matrix.matrix);

		core_stream->public.csc_color_matrix.enable_adjustment = true;

		/* Write persistent data in registry*/
		flag.save_per_edid = true;
		flag.save_per_link = false;
		dm_write_persistent_data(core_dc->ctx,
					streams[stream_index]->sink,
					COLOR_REGISTRY_NAME,
					"brightness",
					&brightness_value,
					sizeof(int),
					&flag);
	}

	core_color->dc->stream_funcs.set_gamut_remap
			(core_color->dc, streams, num_streams);

	return true;
}

bool mod_color_set_contrast(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		int contrast_value)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);
	struct core_dc *core_dc = DC_TO_CORE(core_color->dc);
	struct persistent_data_flag flag;
	unsigned int stream_index, sink_index;

	for (stream_index = 0; stream_index < num_streams; stream_index++) {
		sink_index = sink_index_from_sink(core_color,
				streams[stream_index]->sink);

		struct core_stream *core_stream =
						DC_STREAM_TO_CORE
						(streams[stream_index]);

		core_color->state[sink_index].contrast.current =
				contrast_value;

		calculate_csc_matrix(core_color, sink_index,
				core_stream->public.output_color_space,
				core_stream->public.csc_color_matrix.matrix);

		core_stream->public.csc_color_matrix.enable_adjustment = true;

		/* Write persistent data in registry*/
		flag.save_per_edid = true;
		flag.save_per_link = false;
		dm_write_persistent_data(core_dc->ctx,
					streams[stream_index]->sink,
					COLOR_REGISTRY_NAME,
					"contrast",
					&contrast_value,
					sizeof(int),
					&flag);
	}

	core_color->dc->stream_funcs.set_gamut_remap
			(core_color->dc, streams, num_streams);

	return true;
}

bool mod_color_set_hue(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		int hue_value)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);
	struct core_dc *core_dc = DC_TO_CORE(core_color->dc);
	struct persistent_data_flag flag;
	unsigned int stream_index, sink_index;

	for (stream_index = 0; stream_index < num_streams; stream_index++) {
		sink_index = sink_index_from_sink(core_color,
				streams[stream_index]->sink);

		struct core_stream *core_stream =
						DC_STREAM_TO_CORE
						(streams[stream_index]);

		core_color->state[sink_index].hue.current = hue_value;

		calculate_csc_matrix(core_color, sink_index,
				core_stream->public.output_color_space,
				core_stream->public.csc_color_matrix.matrix);

		core_stream->public.csc_color_matrix.enable_adjustment = true;

		/* Write persistent data in registry*/
		flag.save_per_edid = true;
		flag.save_per_link = false;
		dm_write_persistent_data(core_dc->ctx,
					streams[stream_index]->sink,
					COLOR_REGISTRY_NAME,
					"hue",
					&hue_value,
					sizeof(int),
					&flag);
	}

	core_color->dc->stream_funcs.set_gamut_remap
			(core_color->dc, streams, num_streams);

	return true;
}

bool mod_color_set_saturation(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		int saturation_value)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);
	struct core_dc *core_dc = DC_TO_CORE(core_color->dc);
	struct persistent_data_flag flag;
	unsigned int stream_index, sink_index;

	for (stream_index = 0; stream_index < num_streams; stream_index++) {
		sink_index = sink_index_from_sink(core_color,
				streams[stream_index]->sink);

		struct core_stream *core_stream =
						DC_STREAM_TO_CORE
						(streams[stream_index]);

		core_color->state[sink_index].saturation.current =
				saturation_value;

		calculate_csc_matrix(core_color, sink_index,
				core_stream->public.output_color_space,
				core_stream->public.csc_color_matrix.matrix);

		core_stream->public.csc_color_matrix.enable_adjustment = true;

		/* Write persistent data in registry*/
		flag.save_per_edid = true;
		flag.save_per_link = false;
		dm_write_persistent_data(core_dc->ctx,
					streams[stream_index]->sink,
					COLOR_REGISTRY_NAME,
					"saturation",
					&saturation_value,
					sizeof(int),
					&flag);
	}

	core_color->dc->stream_funcs.set_gamut_remap
			(core_color->dc, streams, num_streams);

	return true;
}

bool mod_color_persist_user_preferred_quantization_range(
		struct mod_color *mod_color,
		const struct dc_sink *sink,
		enum dc_quantization_range quantization_range)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);
	struct core_dc *core_dc = DC_TO_CORE(core_color->dc);
	struct persistent_data_flag flag;
	unsigned int sink_index;

	sink_index = sink_index_from_sink(core_color, sink);
	if (core_color->state[sink_index].
			preferred_quantization_range != quantization_range) {
		core_color->state[sink_index].preferred_quantization_range =
				quantization_range;
		flag.save_per_edid = true;
		flag.save_per_link = false;
		dm_write_persistent_data(core_dc->ctx,
					sink,
					COLOR_REGISTRY_NAME,
					"quantization_range",
					&quantization_range,
					sizeof(int),
					&flag);
	}

	return true;
}

bool mod_color_get_preferred_quantization_range(struct mod_color *mod_color,
		const struct dc_sink *sink,
		const struct dc_crtc_timing *timing,
		enum dc_quantization_range *quantization_range)
{
	struct core_color *core_color = MOD_COLOR_TO_CORE(mod_color);
	unsigned int sink_index = sink_index_from_sink(core_color, sink);
	enum dc_quantization_range user_preferred_quantization_range =
			core_color->state[sink_index].
				preferred_quantization_range;
	bool rgb_full_range_supported =
			mod_color_is_rgb_full_range_supported_for_timing(
				sink, timing);
	bool rgb_limited_range_supported =
			mod_color_is_rgb_limited_range_supported_for_timing(
				sink, timing);

	if (rgb_full_range_supported && rgb_limited_range_supported)
		*quantization_range = user_preferred_quantization_range;
	else if (rgb_full_range_supported && !rgb_limited_range_supported)
		*quantization_range = QUANTIZATION_RANGE_FULL;
	else if (!rgb_full_range_supported && rgb_limited_range_supported)
		*quantization_range = QUANTIZATION_RANGE_LIMITED;
	else
		*quantization_range = QUANTIZATION_RANGE_UNKNOWN;

	return true;
}

bool mod_color_is_rgb_full_range_supported_for_timing(
		const struct dc_sink *sink,
		const struct dc_crtc_timing *timing)
{
	bool result = false;

	if (!sink || !timing)
		return result;

	if (sink->sink_signal == SIGNAL_TYPE_HDMI_TYPE_A)
		if (timing->vic || timing->hdmi_vic)
			if (timing->h_addressable == 640 &&
				timing->v_addressable == 480 &&
				(timing->pix_clk_khz == 25200 ||
					timing->pix_clk_khz == 25170 ||
					timing->pix_clk_khz == 25175))
				result = true;
			else
				/* don't support full range rgb */
				/* for HDMI CEA861 timings except VGA mode */
				result = false;
		else
			result = true;
	else
		result = true;

	return result;
}

bool mod_color_is_rgb_limited_range_supported_for_timing(
		const struct dc_sink *sink,
		const struct dc_crtc_timing *timing)
{
	bool result = false;

	if (!sink || !timing)
		return result;

	if (sink->sink_signal == SIGNAL_TYPE_HDMI_TYPE_A)
		if (timing->vic || timing->hdmi_vic)
			if (timing->h_addressable == 640 &&
				timing->v_addressable == 480 &&
				(timing->pix_clk_khz == 25200 ||
						timing->pix_clk_khz == 25170 ||
						timing->pix_clk_khz == 25175))
				/* don't support rgb limited for */
				/* HDMI CEA VGA mode */
				result = false;
			else
				/* support rgb limited for non VGA CEA timing */
				result = true;
		else
			/* support rgb limited for non CEA HDMI timing */
			result = true;
	else
		/* don't support rgb limited for non HDMI signal */
		result = false;

	return result;
}
