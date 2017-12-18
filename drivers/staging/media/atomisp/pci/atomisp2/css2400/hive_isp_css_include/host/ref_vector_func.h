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

#ifndef _REF_VECTOR_FUNC_H_INCLUDED_
#define _REF_VECTOR_FUNC_H_INCLUDED_


#ifdef INLINE_VECTOR_FUNC
#define STORAGE_CLASS_REF_VECTOR_FUNC_H static inline
#define STORAGE_CLASS_REF_VECTOR_DATA_H static inline_DATA
#else /* INLINE_VECTOR_FUNC */
#define STORAGE_CLASS_REF_VECTOR_FUNC_H extern
#define STORAGE_CLASS_REF_VECTOR_DATA_H extern_DATA
#endif  /* INLINE_VECTOR_FUNC */


#include "ref_vector_func_types.h"

/** @brief Doubling multiply accumulate with saturation
 *
 * @param[in] acc accumulator
 * @param[in] a multiply input
 * @param[in] b multiply input
  *
 * @return		acc + (a*b)
 *
 * This function will do a doubling multiply ont
 * inputs a and b, and will add the result to acc.
 * in case of an overflow of acc, it will saturate.
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector2w OP_1w_maccd_sat(
	tvector2w acc,
	tvector1w a,
	tvector1w b );

/** @brief Doubling multiply accumulate
 *
 * @param[in] acc accumulator
 * @param[in] a multiply input
 * @param[in] b multiply input
  *
 * @return		acc + (a*b)
 *
 * This function will do a doubling multiply ont
 * inputs a and b, and will add the result to acc.
 * in case of overflow it will not saturate but wrap around.
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector2w OP_1w_maccd(
	tvector2w acc,
	tvector1w a,
	tvector1w b );

/** @brief Re-aligning multiply
 *
 * @param[in] a multiply input
 * @param[in] b multiply input
 * @param[in] shift shift amount
 *
 * @return		(a*b)>>shift
 *
 * This function will multiply a with b, followed by a right
 * shift with rounding. the result is saturated and casted
 * to single precision.
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w OP_1w_mul_realigning(
	tvector1w a,
	tvector1w b,
	tscalar1w shift );

/** @brief Leading bit index
 *
 * @param[in] a 	input
 *
 * @return		index of the leading bit of each element
 *
 * This function finds the index of leading one (set) bit of the
 * input. The index starts with 0 for the LSB and can go upto
 * ISP_VEC_ELEMBITS-1 for the MSB. For an input equal to zero,
 * the returned index is -1.
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w OP_1w_lod(
		tvector1w a);

/** @brief Config Unit Input Processing
 *
 * @param[in] a 	    input
 * @param[in] input_scale   input scaling factor
 * @param[in] input_offset  input offset factor
 *
 * @return		    scaled & offset added input	clamped to MAXVALUE
 *
 * As part of input processing for piecewise linear estimation config unit,
 * this function will perform scaling followed by adding offset and
 * then clamping to the MAX InputValue
 * It asserts -MAX_SHIFT_1W <= input_scale <= MAX_SHIFT_1W, and
 * -MAX_SHIFT_1W <= input_offset <= MAX_SHIFT_1W
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w OP_1w_input_scaling_offset_clamping(
	tvector1w a,
	tscalar1w_5bit_signed input_scale,
	tscalar1w_5bit_signed input_offset);

/** @brief Config Unit Output Processing
 *
 * @param[in] a 	     output
 * @param[in] output_scale   output scaling factor
 *
 * @return		     scaled & clamped output value
 *
 * As part of output processing for piecewise linear estimation config unit,
 * This function will perform scaling and then clamping to output
 * MAX value.
 * It asserts -MAX_SHIFT_1W <= output_scale <= MAX_SHIFT_1W
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w OP_1w_output_scaling_clamping(
	tvector1w a,
	tscalar1w_5bit_signed output_scale);

/** @brief Config Unit Piecewiselinear estimation
 *
 * @param[in] a 	          input
 * @param[in] config_points   config parameter structure
 *
 * @return		     	   piecewise linear estimated output
 *
 * Given a set of N points {(x1,y1),()x2,y2), ....,(xn,yn)}, to find
 * the functional value at an arbitrary point around the input set,
 * this function will perform input processing followed by piecewise
 * linear estimation and then output processing to yield the final value.
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w OP_1w_piecewise_estimation(
	tvector1w a,
	ref_config_points config_points);

/** @brief Fast Config Unit
 *
 * @param[in] x 		input
 * @param[in] init_vectors	LUT data structure
 *
 * @return	piecewise linear estimated output
 * This block gets an input x and a set of input configuration points stored in a look-up
 * table of 32 elements. First, the x input is clipped to be within the range [x1, xn+1].
 * Then, it computes the interval in which the input lies. Finally, the output is computed
 * by performing linear interpolation based on the interval properties (i.e. x_prev, slope,
 * and offset). This block assumes that the points are equally spaced and that the interval
 * size is a power of 2.
 **/
STORAGE_CLASS_REF_VECTOR_FUNC_H  tvector1w OP_1w_XCU(
	tvector1w x,
	xcu_ref_init_vectors init_vectors);


/** @brief LXCU
 *
 * @param[in] x 		input
 * @param[in] init_vectors 	LUT data structure
 *
 * @return   logarithmic piecewise linear estimated output.
 * This block gets an input x and a set of input configuration points stored in a look-up
 * table of 32 elements. It computes the interval in which the input lies.
 * Then output is computed by performing linear interpolation based on the interval
 * properties (i.e. x_prev, slope, * and offset).
 * This BBB assumes spacing x-coordinates of "init vectors" increase exponentially as
 * shown below.
 * interval size :   2^0    2^1      2^2    2^3
 * x-coordinates: x0<--->x1<---->x2<---->x3<---->
 **/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w OP_1w_LXCU(
	tvector1w x,
	xcu_ref_init_vectors init_vectors);

/** @brief Coring
 *
 * @param[in] coring_vec   Amount of coring based on brightness level
 * @param[in] filt_input   Vector of input pixels on which Coring is applied
 * @param[in] m_CnrCoring0 Coring Level0
 *
 * @return                 vector of filtered pixels after coring is applied
 *
 * This function will perform adaptive coring based on brightness level to
 * remove noise
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w coring(
	tvector1w coring_vec,
	tvector1w filt_input,
	tscalar1w m_CnrCoring0 );

/** @brief Normalised FIR with coefficients [3,4,1]
 *
 * @param[in] m	1x3 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will calculate the
 * Normalised FIR with coefficients [3,4,1],
 *-5dB at Fs/2, -90 degree phase shift (quarter pixel)
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir1x3m_5dB_m90_nrm (
	const s_1w_1x3_matrix		m);

/** @brief Normalised FIR with coefficients [1,4,3]
 *
 * @param[in] m	1x3 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will calculate the
 * Normalised FIR with coefficients [1,4,3],
 *-5dB at Fs/2, +90 degree phase shift (quarter pixel)
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir1x3m_5dB_p90_nrm (
	const s_1w_1x3_matrix		m);

/** @brief Normalised FIR with coefficients [1,2,1]
 *
 * @param[in] m	1x3 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will calculate the
 * Normalised FIR with coefficients [1,2,1], -6dB at Fs/2
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir1x3m_6dB_nrm (
	const s_1w_1x3_matrix		m);

/** @brief Normalised FIR with coefficients [13,16,3]
 *
 * @param[in] m	1x3 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will calculate the
 * Normalised FIR with coefficients [13,16,3],
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir1x3m_6dB_nrm_ph0 (
	const s_1w_1x3_matrix		m);

/** @brief Normalised FIR with coefficients [9,16,7]
 *
 * @param[in] m	1x3 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will calculate the
 * Normalised FIR with coefficients [9,16,7],
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir1x3m_6dB_nrm_ph1 (
	const s_1w_1x3_matrix		m);

/** @brief Normalised FIR with coefficients [5,16,11]
 *
 * @param[in] m	1x3 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will calculate the
 * Normalised FIR with coefficients [5,16,11],
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir1x3m_6dB_nrm_ph2 (
	const s_1w_1x3_matrix		m);

/** @brief Normalised FIR with coefficients [1,16,15]
 *
 * @param[in] m	1x3 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will calculate the
 * Normalised FIR with coefficients [1,16,15],
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir1x3m_6dB_nrm_ph3 (
	const s_1w_1x3_matrix		m);

/** @brief Normalised FIR with programable phase shift
 *
 * @param[in] m	1x3 matrix with pixels
 * @param[in] coeff	phase shift
 *
 * @return		filtered output
 *
 * This function will calculate the
 * Normalised FIR with coefficients [8-coeff,16,8+coeff],
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir1x3m_6dB_nrm_calc_coeff (
	const s_1w_1x3_matrix		m, tscalar1w_3bit coeff);

/** @brief 3 tap FIR with coefficients [1,1,1]
 *
 * @param[in] m	1x3 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will calculate the
 * FIR with coefficients [1,1,1], -9dB at Fs/2 normalized with factor 1/2
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir1x3m_9dB_nrm (
	const s_1w_1x3_matrix		m);

#ifdef ISP2401
/** @brief      symmetric 3 tap FIR acts as LPF or BSF
 *
 * @param[in] m 1x3 matrix with pixels
 * @param[in] k filter coefficient shift
 * @param[in] bsf_flag 1 for BSF and 0 for LPF
 *
 * @return    filtered output
 *
 * This function performs variable coefficient symmetric 3 tap filter which can
 * be either used as Low Pass Filter or Band Stop Filter.
 * Symmetric 3tap tap filter with DC gain 1 has filter coefficients [a, 1-2a, a]
 * For LPF 'a' can be approximated as (1 - 2^(-k))/4, k = 0, 1, 2, ...
 * and filter output can be approximated as:
 * out_LPF = ((v00 + v02) - ((v00 + v02) >> k) + (2 * (v01 + (v01 >> k)))) >> 2
 * For BSF 'a' can be approximated as (1 + 2^(-k))/4, k = 0, 1, 2, ...
 * and filter output can be approximated as:
 * out_BSF = ((v00 + v02) + ((v00 + v02) >> k) + (2 * (v01 - (v01 >> k)))) >> 2
 * For a given filter coefficient shift 'k' and bsf_flag this function
 * behaves either as LPF or BSF.
 * All computation is done using 1w arithmetic and implementation does not use
 * any multiplication.
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w
sym_fir1x3m_lpf_bsf(s_1w_1x3_matrix m,
		    tscalar1w k,
		    tscalar_bool bsf_flag);
#endif

/** @brief Normalised 2D FIR with coefficients  [1;2;1] * [1,2,1]
 *
 * @param[in] m	3x3 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will calculate the
 * Normalised FIR with coefficients  [1;2;1] * [1,2,1]
 * Unity gain filter through repeated scaling and rounding
 *	- 6 rotate operations per output
 *	- 8 vector operations per output
 * _______
 *   14 total operations
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir3x3m_6dB_nrm (
	const s_1w_3x3_matrix		m);

/** @brief Normalised 2D FIR with coefficients  [1;1;1] * [1,1,1]
 *
 * @param[in] m	3x3 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will calculate the
 * Normalised FIR with coefficients [1;1;1] * [1,1,1]
 *
 * (near) Unity gain filter through repeated scaling and rounding
 *	- 6 rotate operations per output
 *	- 8 vector operations per output
 * _______
 *   14 operations
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir3x3m_9dB_nrm (
	const s_1w_3x3_matrix		m);

/** @brief Normalised dual output 2D FIR with coefficients  [1;2;1] * [1,2,1]
 *
 * @param[in] m	4x3 matrix with pixels
 *
 * @return		two filtered outputs (2x1 matrix)
 *
 * This function will calculate the
 * Normalised FIR with coefficients  [1;2;1] * [1,2,1]
 * and produce two outputs (vertical)
 * Unity gain filter through repeated scaling and rounding
 * compute two outputs per call to re-use common intermediates
 *	- 4 rotate operations per output
 *	- 6 vector operations per output (alternative possible, but in this
 *	    form it's not obvious to re-use variables)
 * _______
 *   10 total operations
 */
 STORAGE_CLASS_REF_VECTOR_FUNC_H s_1w_2x1_matrix fir3x3m_6dB_out2x1_nrm (
	const s_1w_4x3_matrix		m);

/** @brief Normalised dual output 2D FIR with coefficients [1;1;1] * [1,1,1]
 *
 * @param[in] m	4x3 matrix with pixels
 *
 * @return		two filtered outputs (2x1 matrix)
 *
 * This function will calculate the
 * Normalised FIR with coefficients [1;1;1] * [1,1,1]
 * and produce two outputs (vertical)
 * (near) Unity gain filter through repeated scaling and rounding
 * compute two outputs per call to re-use common intermediates
 *	- 4 rotate operations per output
 *	- 7 vector operations per output (alternative possible, but in this
 *	    form it's not obvious to re-use variables)
 * _______
 *   11 total operations
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H s_1w_2x1_matrix fir3x3m_9dB_out2x1_nrm (
	const s_1w_4x3_matrix		m);

/** @brief Normalised 2D FIR 5x5
 *
 * @param[in] m	5x5 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will calculate the
 * Normalised FIR with coefficients [1;1;1] * [1;2;1] * [1,2,1] * [1,1,1]
 * and produce a filtered output
 * (near) Unity gain filter through repeated scaling and rounding
 *	- 20 rotate operations per output
 *	- 28 vector operations per output
 * _______
 *   48 total operations
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir5x5m_15dB_nrm (
	const s_1w_5x5_matrix	m);

/** @brief Normalised FIR 1x5
 *
 * @param[in] m	1x5 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will calculate the
 * Normalised FIR with coefficients [1,2,1] * [1,1,1] = [1,4,6,4,1]
 * and produce a filtered output
 * (near) Unity gain filter through repeated scaling and rounding
 *	- 4 rotate operations per output
 *	- 5 vector operations per output
 * _______
 *   9 total operations
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir1x5m_12dB_nrm (
	const s_1w_1x5_matrix m);

/** @brief Normalised 2D FIR 5x5
 *
 * @param[in] m	5x5 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will calculate the
 * Normalised FIR with coefficients [1;2;1] * [1;2;1] * [1,2,1] * [1,2,1]
 * and produce a filtered output
 * (near) Unity gain filter through repeated scaling and rounding
 *	- 20 rotate operations per output
 *	- 30 vector operations per output
 * _______
 *   50 total operations
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir5x5m_12dB_nrm (
	const s_1w_5x5_matrix m);

/** @brief Approximate averaging FIR 1x5
 *
 * @param[in] m	1x5 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will produce filtered output by
 * applying the filter coefficients (1/8) * [1,1,1,1,1]
 * _______
 *   5 vector operations
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir1x5m_box (
	s_1w_1x5_matrix m);

/** @brief Approximate averaging FIR 1x9
 *
 * @param[in] m	1x9 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will produce filtered output by
 * applying the filter coefficients (1/16) * [1,1,1,1,1,1,1,1,1]
 * _______
 *   9 vector operations
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir1x9m_box (
	s_1w_1x9_matrix m);

/** @brief Approximate averaging FIR 1x11
 *
 * @param[in] m	1x11 matrix with pixels
 *
 * @return		filtered output
 *
 * This function will produce filtered output by
 * applying the filter coefficients (1/16) * [1,1,1,1,1,1,1,1,1,1,1]
 * _______
 *   12 vector operations
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w fir1x11m_box (
	s_1w_1x11_matrix m);

/** @brief Symmetric 7 tap filter with normalization
 *
 *  @param[in] in 1x7 matrix with pixels
 *  @param[in] coeff 1x4 matrix with coefficients
 *  @param[in] out_shift output pixel shift value for normalization
 *
 *  @return symmetric 7 tap filter output
 *
 * This function performs symmetric 7 tap filter over input pixels.
 * Filter sum is normalized by shifting out_shift bits.
 * Filter sum: p0*c3 + p1*c2 + p2*c1 + p3*c0 + p4*c1 + p5*c2 + p6*c3
 * is implemented as: (p0 + p6)*c3 + (p1 + p5)*c2 + (p2 + p4)*c1 + p3*c0 to
 * reduce multiplication.
 * Input pixels should to be scaled, otherwise overflow is possible during
 * addition
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w
fir1x7m_sym_nrm(s_1w_1x7_matrix in,
		s_1w_1x4_matrix coeff,
		tvector1w out_shift);

/** @brief Symmetric 7 tap filter with normalization at input side
 *
 *  @param[in] in 1x7 matrix with pixels
 *  @param[in] coeff 1x4 matrix with coefficients
 *
 *  @return symmetric 7 tap filter output
 *
 * This function performs symmetric 7 tap filter over input pixels.
 * Filter sum: p0*c3 + p1*c2 + p2*c1 + p3*c0 + p4*c1 + p5*c2 + p6*c3
 *          = (p0 + p6)*c3 + (p1 + p5)*c2 + (p2 + p4)*c1 + p3*c0
 * Input pixels and coefficients are in Qn format, where n =
 * ISP_VEC_ELEMBITS - 1 (ie Q15 for Broxton)
 * To avoid double precision arithmetic input pixel sum and final sum is
 * implemented using avgrnd and coefficient multiplication using qrmul.
 * Final result is in Qm format where m = ISP_VEC_ELEMBITS - 2 (ie Q14 for
 * Broxton)
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w
fir1x7m_sym_innrm_approx(s_1w_1x7_matrix in,
			 s_1w_1x4_matrix coeff);

/** @brief Symmetric 7 tap filter with normalization at output side
 *
 *  @param[in] in 1x7 matrix with pixels
 *  @param[in] coeff 1x4 matrix with coefficients
 *
 *  @return symmetric 7 tap filter output
 *
 * This function performs symmetric 7 tap filter over input pixels.
 * Filter sum: p0*c3 + p1*c2 + p2*c1 + p3*c0 + p4*c1 + p5*c2 + p6*c3
 *          = (p0 + p6)*c3 + (p1 + p5)*c2 + (p2 + p4)*c1 + p3*c0
 * Input pixels are in Qn and coefficients are in Qm format, where n =
 * ISP_VEC_ELEMBITS - 2 and m = ISP_VEC_ELEMBITS - 1 (ie Q14 and Q15
 * respectively for Broxton)
 * To avoid double precision arithmetic input pixel sum and final sum is
 * implemented using addsat and coefficient multiplication using qrmul.
 * Final sum is left shifted by 2 and saturated to produce result is Qm format
 * (ie Q15 for Broxton)
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w
fir1x7m_sym_outnrm_approx(s_1w_1x7_matrix in,
			 s_1w_1x4_matrix coeff);

/** @brief 4 tap filter with normalization
 *
 *  @param[in] in 1x4 matrix with pixels
 *  @param[in] coeff 1x4 matrix with coefficients
 *  @param[in] out_shift output pixel shift value for normalization
 *
 *  @return 4 tap filter output
 *
 * This function performs 4 tap filter over input pixels.
 * Filter sum is normalized by shifting out_shift bits.
 * Filter sum: p0*c0 + p1*c1 + p2*c2 + p3*c3
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w
fir1x4m_nrm(s_1w_1x4_matrix in,
		s_1w_1x4_matrix coeff,
		tvector1w out_shift);

/** @brief 4 tap filter with normalization for half pixel interpolation
 *
 *  @param[in] in 1x4 matrix with pixels
 *
 *  @return 4 tap filter output with filter tap [-1 9 9 -1]/16
 *
 * This function performs 4 tap filter over input pixels.
 * Filter sum: -p0 + 9*p1 + 9*p2 - p3
 * This filter implementation is completely free from multiplication and double
 * precision arithmetic.
 * Typical usage of this filter is to half pixel interpolation of Bezier
 * surface
 * */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w
fir1x4m_bicubic_bezier_half(s_1w_1x4_matrix in);

/** @brief 4 tap filter with normalization for quarter pixel interpolation
 *
 *  @param[in] in 1x4 matrix with pixels
 *  @param[in] coeff 1x4 matrix with coefficients
 *
 *  @return 4 tap filter output
 *
 * This function performs 4 tap filter over input pixels.
 * Filter sum: p0*c0 + p1*c1 + p2*c2 + p3*c3
 * To avoid double precision arithmetic we implemented multiplication using
 * qrmul and addition using avgrnd. Coefficients( c0 to c3) formats are assumed
 * to be: Qm, Qn, Qo, Qm, where m = n + 2 and o = n + 1.
 * Typical usage of this filter is to quarter pixel interpolation of Bezier
 * surface with filter coefficients:[-9 111 29 -3]/128. For which coefficient
 * values should be: [-9216/2^17  28416/2^15  1484/2^16 -3072/2^17] for
 * ISP_VEC_ELEMBITS = 16.
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w
fir1x4m_bicubic_bezier_quarter(s_1w_1x4_matrix in,
			s_1w_1x4_matrix coeff);


/** @brief Symmetric 3 tap filter with normalization
 *
 *  @param[in] in 1x3 matrix with pixels
 *  @param[in] coeff 1x2 matrix with coefficients
 *  @param[in] out_shift output pixel shift value for normalization
 *
 *  @return symmetric 3 tap filter output
 *
 * This function performs symmetric 3 tap filter input pixels.
 * Filter sum is normalized by shifting out_shift bits.
 * Filter sum: p0*c1 + p1*c0 + p2*c1
 * is implemented as: (p0 + p2)*c1 + p1*c0 to reduce multiplication.
 * Input pixels should to be scaled, otherwise overflow is possible during
 * addition
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w
fir1x3m_sym_nrm(s_1w_1x3_matrix in,
		s_1w_1x2_matrix coeff,
		tvector1w out_shift);

/** @brief Symmetric 3 tap filter with normalization
 *
 *  @param[in] in 1x3 matrix with pixels
 *  @param[in] coeff 1x2 matrix with coefficients
 *
 *  @return symmetric 3 tap filter output
 *
 * This function performs symmetric 3 tap filter over input pixels.
 * Filter sum: p0*c1 + p1*c0 + p2*c1 = (p0 + p2)*c1 + p1*c0
 * Input pixels are in Qn and coefficient c0 is in Qm and c1 is in Qn format,
 * where n = ISP_VEC_ELEMBITS - 1 and m = ISP_VEC_ELEMBITS - 2 ( ie Q15 and Q14
 * respectively for Broxton)
 * To avoid double precision arithmetic input pixel sum is implemented using
 * avgrnd, coefficient multiplication using qrmul and final sum using addsat
 * Final sum is Qm format (ie Q14 for Broxton)
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w
fir1x3m_sym_nrm_approx(s_1w_1x3_matrix in,
		       s_1w_1x2_matrix coeff);

/** @brief Mean of 1x3 matrix
 *
 *  @param[in] m 1x3 matrix with pixels
 *
 *  @return mean of 1x3 matrix
 *
 * This function calculates the mean of 1x3 pixels,
 * with a factor of 4/3.
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w mean1x3m(
	s_1w_1x3_matrix m);

/** @brief Mean of 3x3 matrix
 *
 *  @param[in] m 3x3 matrix with pixels
 *
 *  @return mean of 3x3 matrix
 *
 * This function calculates the mean of 3x3 pixels,
 * with a factor of 16/9.
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w mean3x3m(
	s_1w_3x3_matrix m);

/** @brief Mean of 1x4 matrix
 *
 *  @param[in] m 1x4 matrix with pixels
 *
 *  @return mean of 1x4 matrix
 *
 * This function calculates the mean of 1x4 pixels
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w mean1x4m(
	s_1w_1x4_matrix m);

/** @brief Mean of 4x4 matrix
 *
 *  @param[in] m 4x4 matrix with pixels
 *
 *  @return mean of 4x4 matrix
 *
 * This function calculates the mean of 4x4 matrix with pixels
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w mean4x4m(
	s_1w_4x4_matrix m);

/** @brief Mean of 2x3 matrix
 *
 *  @param[in] m 2x3 matrix with pixels
 *
 *  @return mean of 2x3 matrix
 *
 * This function calculates the mean of 2x3 matrix with pixels
 * with a factor of 8/6.
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w mean2x3m(
	s_1w_2x3_matrix m);

/** @brief Mean of 1x5 matrix
 *
 *  @param[in] m 1x5 matrix with pixels
 *
 *  @return mean of 1x5 matrix
 *
 * This function calculates the mean of 1x5 matrix with pixels
 * with a factor of 8/5.
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w mean1x5m(s_1w_1x5_matrix m);

/** @brief Mean of 1x6 matrix
 *
 *  @param[in] m 1x6 matrix with pixels
 *
 *  @return mean of 1x6 matrix
 *
 * This function calculates the mean of 1x6 matrix with pixels
 * with a factor of 8/6.
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w mean1x6m(
	s_1w_1x6_matrix m);

/** @brief Mean of 5x5 matrix
 *
 *  @param[in] m 5x5 matrix with pixels
 *
 *  @return mean of 5x5 matrix
 *
 * This function calculates the mean of 5x5 matrix with pixels
 * with a factor of 32/25.
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w mean5x5m(
	s_1w_5x5_matrix m);

/** @brief Mean of 6x6 matrix
 *
 *  @param[in] m 6x6 matrix with pixels
 *
 *  @return mean of 6x6 matrix
 *
 * This function calculates the mean of 6x6 matrix with pixels
 * with a factor of 64/36.
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w mean6x6m(
	s_1w_6x6_matrix m);

/** @brief Minimum of 4x4 matrix
 *
 *  @param[in] m 4x4 matrix with pixels
 *
 *  @return minimum of 4x4 matrix
 *
 * This function calculates the  minimum of
 * 4x4 matrix with pixels.
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w min4x4m(
	s_1w_4x4_matrix m);

/** @brief Maximum of 4x4 matrix
 *
 *  @param[in] m 4x4 matrix with pixels
 *
 *  @return maximum of 4x4 matrix
 *
 * This function calculates the  maximum of
 * 4x4 matrix with pixels.
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w max4x4m(
	s_1w_4x4_matrix m);

/** @brief SAD between two 3x3 matrices
 *
 *  @param[in] a 3x3 matrix with pixels
 *
 *  @param[in] b 3x3 matrix with pixels
 *
 *  @return 3x3 matrix SAD
 *
 * This function calculates the sum of absolute difference between two matrices.
 * Both input pixels and SAD are normalized by a factor of SAD3x3_IN_SHIFT and
 * SAD3x3_OUT_SHIFT respectively.
 * Computed SAD is 1/(2 ^ (SAD3x3_IN_SHIFT + SAD3x3_OUT_SHIFT)) ie 1/16 factor
 * of original SAD and it's more precise than sad3x3m()
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w sad3x3m_precise(
	s_1w_3x3_matrix a,
	s_1w_3x3_matrix b);

/** @brief SAD between two 3x3 matrices
 *
 *  @param[in] a 3x3 matrix with pixels
 *
 *  @param[in] b 3x3 matrix with pixels
 *
 *  @return 3x3 matrix SAD
 *
 * This function calculates the sum of absolute difference between two matrices.
 * This version saves cycles by avoiding input normalization and wide vector
 * operation during sum computation
 * Input pixel differences are computed by absolute of rounded, halved
 * subtraction. Normalized sum is computed by rounded averages.
 * Computed SAD is (1/2)*(1/16) = 1/32 factor of original SAD. Factor 1/2 comes
 * from input halving operation and factor 1/16 comes from mean operation
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w sad3x3m(
	s_1w_3x3_matrix a,
	s_1w_3x3_matrix b);

/** @brief SAD between two 5x5 matrices
 *
 *  @param[in] a 5x5 matrix with pixels
 *
 *  @param[in] b 5x5 matrix with pixels
 *
 *  @return 5x5 matrix SAD
 *
 * Computed SAD is = 1/32 factor of original SAD.
*/
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w sad5x5m(
	s_1w_5x5_matrix a,
	s_1w_5x5_matrix b);

/** @brief Absolute gradient between two sets of 1x5 matrices
 *
 *  @param[in] m0 first set of 1x5 matrix with pixels
 *  @param[in] m1 second set of 1x5 matrix with pixels
 *
 *  @return absolute gradient between two 1x5 matrices
 *
 * This function computes mean of two input 1x5 matrices and returns
 * absolute difference between two mean values.
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w
absgrad1x5m(s_1w_1x5_matrix m0, s_1w_1x5_matrix m1);

/** @brief Bi-linear Interpolation optimized(approximate)
 *
 * @param[in] a input0
 * @param[in] b input1
 * @param[in] c cloned weight factor
  *
 * @return		(a-b)*c + b
 *
 * This function will do bi-linear Interpolation on
 * inputs a and b using constant weight factor c
 *
 * Inputs a,b are assumed in S1.15 format
 * Weight factor has to be in range [0,1] and is assumed to be in S2.14 format
 *
 * The bilinear interpolation equation is (a*c) + b*(1-c),
 * But this is implemented as (a-b)*c + b for optimization
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w OP_1w_bilinear_interpol_approx_c(
	tvector1w a,
	tvector1w b,
	tscalar1w_weight c);

/** @brief Bi-linear Interpolation optimized(approximate)
 *
 * @param[in] a input0
 * @param[in] b input1
 * @param[in] c weight factor
  *
 * @return		(a-b)*c + b
 *
 * This function will do bi-linear Interpolation on
 * inputs a and b using weight factor c
 *
 * Inputs a,b are assumed in S1.15 format
 * Weight factor has to be in range [0,1] and is assumed to be in S2.14 format
 *
 * The bilinear interpolation equation is (a*c) + b*(1-c),
 * But this is implemented as (a-b)*c + b for optimization
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w OP_1w_bilinear_interpol_approx(
	tvector1w a,
	tvector1w b,
	tvector1w_weight c);

/** @brief Bi-linear Interpolation
 *
 * @param[in] a input0
 * @param[in] b input1
 * @param[in] c weight factor
  *
 * @return		(a*c) + b*(1-c)
 *
 * This function will do bi-linear Interpolation on
 * inputs a and b using weight factor c
 *
 * Inputs a,b are assumed in S1.15 format
 * Weight factor has to be in range [0,1] and is assumed to be in S2.14 format
 *
 * The bilinear interpolation equation is (a*c) + b*(1-c),
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w OP_1w_bilinear_interpol(
	tvector1w a,
	tvector1w b,
	tscalar1w_weight c);

/** @brief Generic Block Matching Algorithm
 * @param[in] search_window pointer to input search window of 16x16 pixels
 * @param[in] ref_block pointer to input reference block of 8x8 pixels, where N<=M
 * @param[in] output pointer to output sads
 * @param[in] search_sz search size for SAD computation
 * @param[in] ref_sz block size
 * @param[in] pixel_shift pixel shift to search the data
 * @param[in] search_block_sz search window block size
 * @param[in] shift shift value, with which the output is shifted right
 *
 * @return   0 when the computation is successful.

 * * This function compares the reference block with a block of size NxN in the search
 * window. Sum of absolute differences for each pixel in the reference block and the
 * corresponding pixel in the search block. Whole search window os traversed with the
 * reference block with the given pixel shift.
 *
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H int generic_block_matching_algorithm(
	tscalar1w **search_window,
	tscalar1w **ref_block,
	tscalar1w *output,
	int search_sz,
	int ref_sz,
	int pixel_shift,
	int search_block_sz,
	tscalar1w_4bit_bma_shift shift);

#ifndef ISP2401
/** @brief OP_1w_asp_bma_16_1_32way
#else
/** @brief OP_1w_asp_bma_16_1_32way_nomask
#endif
 *
 * @param[in] search_area input search window of 16x16 pixels
 * @param[in] input_block input reference block of 8x8 pixels, where N<=M
 * @param[in] shift shift value, with which the output is shifted right
 *
 * @return   81 SADs for all the search blocks.

 * This function compares the reference block with a block of size 8x8 pixels in the
 * search window of 16x16 pixels. Sum of absolute differences for each pixel in the
 * reference block and the corresponding pixel in the search block is calculated.
 * Whole search window is traversed with the reference block with the pixel shift of 1
 * pixels. The output is right shifted with the given shift value. The shift value is
 * a 4 bit value.
 *
 */

#ifndef ISP2401
STORAGE_CLASS_REF_VECTOR_FUNC_H bma_output_16_1 OP_1w_asp_bma_16_1_32way(
#else
STORAGE_CLASS_REF_VECTOR_FUNC_H bma_output_16_1 OP_1w_asp_bma_16_1_32way_nomask(
#endif
	bma_16x16_search_window search_area,
	ref_block_8x8 input_block,
	tscalar1w_4bit_bma_shift shift);

#ifndef ISP2401
/** @brief OP_1w_asp_bma_16_2_32way
#else
/** @brief OP_1w_asp_bma_16_2_32way_nomask
#endif
 *
 * @param[in] search_area input search window of 16x16 pixels
 * @param[in] input_block input reference block of 8x8 pixels, where N<=M
 * @param[in] shift shift value, with which the output is shifted right
 *
 * @return   25 SADs for all the search blocks.
 * This function compares the reference block with a block of size 8x8 in the search
 * window of 16x61. Sum of absolute differences for each pixel in the reference block
 * and the corresponding pixel in the search block is computed. Whole search window is
 * traversed with the reference block with the given pixel shift of 2 pixels. The output
 * is right shifted with the given shift value. The shift value is a 4 bit value.
 *
 */

#ifndef ISP2401
STORAGE_CLASS_REF_VECTOR_FUNC_H bma_output_16_2 OP_1w_asp_bma_16_2_32way(
#else
STORAGE_CLASS_REF_VECTOR_FUNC_H bma_output_16_2 OP_1w_asp_bma_16_2_32way_nomask(
#endif
	bma_16x16_search_window search_area,
	ref_block_8x8 input_block,
	tscalar1w_4bit_bma_shift shift);
#ifndef ISP2401
/** @brief OP_1w_asp_bma_14_1_32way
#else
/** @brief OP_1w_asp_bma_14_1_32way_nomask
#endif
 *
 * @param[in] search_area input search block of 16x16 pixels with search window of 14x14 pixels
 * @param[in] input_block input reference block of 8x8 pixels, where N<=M
 * @param[in] shift shift value, with which the output is shifted right
 *
 * @return   49 SADs for all the search blocks.
 * This function compares the reference block with a block of size 8x8 in the search
 * window of 14x14. Sum of absolute differences for each pixel in the reference block
 * and the corresponding pixel in the search block. Whole search window is traversed
 * with the reference block with 2 pixel shift. The output is right shifted with the
 * given shift value. The shift value is a 4 bit value. Input is always a 16x16 block
 * but the search window is 14x14, with last 2 pixels of row and column are not used
 * for computation.
 *
 */

#ifndef ISP2401
STORAGE_CLASS_REF_VECTOR_FUNC_H bma_output_14_1 OP_1w_asp_bma_14_1_32way(
#else
STORAGE_CLASS_REF_VECTOR_FUNC_H bma_output_14_1 OP_1w_asp_bma_14_1_32way_nomask(
#endif
	bma_16x16_search_window search_area,
	ref_block_8x8 input_block,
	tscalar1w_4bit_bma_shift shift);

#ifndef ISP2401
/** @brief OP_1w_asp_bma_14_2_32way
#else
/** @brief OP_1w_asp_bma_14_2_32way_nomask
#endif
 *
 * @param[in] search_area input search block of 16x16 pixels with search window of 14x14 pixels
 * @param[in] input_block input reference block of 8x8 pixels, where N<=M
 * @param[in] shift shift value, with which the output is shifted right
 *
 * @return   16 SADs for all the search blocks.
 * This function compares the reference block with a block of size 8x8 in the search
 * window of 14x14. Sum of absolute differences for each pixel in the reference block
 * and the corresponding pixel in the search block. Whole search window is traversed
 * with the reference block with 2 pixels shift. The output is right shifted with the
 * given shift value. The shift value is a 4 bit value.
 *
 */

#ifndef ISP2401
STORAGE_CLASS_REF_VECTOR_FUNC_H bma_output_14_2 OP_1w_asp_bma_14_2_32way(
#else
STORAGE_CLASS_REF_VECTOR_FUNC_H bma_output_14_2 OP_1w_asp_bma_14_2_32way_nomask(
#endif
	bma_16x16_search_window search_area,
	ref_block_8x8 input_block,
	tscalar1w_4bit_bma_shift shift);

#ifdef ISP2401
/** @brief multiplex addition and passing
 *
 *  @param[in] _a first pixel
 *  @param[in] _b second pixel
 *  @param[in] _c condition flag
 *
 *  @return (_a + _b) if condition flag is true
 *	    _a if condition flag is false
 *
 * This function does multiplex addition depending on the input condition flag
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H tvector1w OP_1w_cond_add(
	tvector1w _a,
	tvector1w _b,
	tflags _c);

#endif
#ifdef HAS_bfa_unit
/** @brief OP_1w_single_bfa_7x7
 *
 * @param[in] weights - spatial and range weight lut
 * @param[in] threshold - threshold plane, for range weight scaling
 * @param[in] central_pix - central pixel plane
 * @param[in] src_plane - src pixel plane
 *
 * @return   Bilateral filter output
 *
 * This function implements, 7x7 single bilateral filter.
 * Output = {sum(pixel * weight), sum(weight)}
 * Where sum is summation over 7x7 block set.
 * weight = spatial weight * range weight
 * spatial weights are loaded from spatial_weight_lut depending on src pixel
 * position in the 7x7 block
 * range weights are computed by table look up from range_weight_lut depending
 * on scaled absolute difference between src and central pixels.
 * threshold is used as scaling factor. range_weight_lut consists of
 * BFA_RW_LUT_SIZE numbers of LUT entries to model any distribution function.
 * Piecewise linear approximation technique is used to compute range weight
 * It computes absolute difference between central pixel and 61 src pixels.
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H bfa_7x7_output OP_1w_single_bfa_7x7(
	bfa_weights weights,
	tvector1w threshold,
	tvector1w central_pix,
	s_1w_7x7_matrix src_plane);

/** @brief OP_1w_joint_bfa_7x7
 *
 * @param[in] weights - spatial and range weight lut
 * @param[in] threshold0 - 1st threshold plane, for range weight scaling
 * @param[in] central_pix0 - 1st central pixel plane
 * @param[in] src0_plane - 1st pixel plane
 * @param[in] threshold1 - 2nd threshold plane, for range weight scaling
 * @param[in] central_pix1 - 2nd central pixel plane
 * @param[in] src1_plane - 2nd pixel plane
 *
 * @return   Joint bilateral filter output
 *
 * This function implements, 7x7 joint bilateral filter.
 * Output = {sum(pixel * weight), sum(weight)}
 * Where sum is summation over 7x7 block set.
 * weight = spatial weight * range weight
 * spatial weights are loaded from spatial_weight_lut depending on src pixel
 * position in the 7x7 block
 * range weights are computed by table look up from range_weight_lut depending
 * on sum of scaled absolute difference between central pixel and two src pixel
 * planes. threshold is used as scaling factor. range_weight_lut consists of
 * BFA_RW_LUT_SIZE numbers of LUT entries to model any distribution function.
 * Piecewise linear approximation technique is used to compute range weight
 * It computes absolute difference between central pixel and 61 src pixels.
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H bfa_7x7_output OP_1w_joint_bfa_7x7(
	bfa_weights weights,
	tvector1w threshold0,
	tvector1w central_pix0,
	s_1w_7x7_matrix src0_plane,
	tvector1w threshold1,
	tvector1w central_pix1,
	s_1w_7x7_matrix src1_plane);

/** @brief bbb_bfa_gen_spatial_weight_lut
 *
 * @param[in] in - 7x7 matrix of spatial weights
 * @param[in] out - generated LUT
 *
 * @return   None
 *
 * This function implements, creates spatial weight look up table used
 * for bilaterl filter instruction.
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H void bbb_bfa_gen_spatial_weight_lut(
	s_1w_7x7_matrix in,
	tvector1w out[BFA_MAX_KWAY]);

/** @brief bbb_bfa_gen_range_weight_lut
 *
 * @param[in] in - input range weight,
 * @param[in] out - generated LUT
 *
 * @return   None
 *
 * This function implements, creates range weight look up table used
 * for bilaterl filter instruction.
 * 8 unsigned 7b weights are represented in 7 16bits LUT
 * LUT formation is done as follows:
 * higher 8 bit: Point(N) = Point(N+1) - Point(N)
 * lower 8 bit: Point(N) = Point(N)
 * Weight function can be any monotonic decreasing function for x >= 0
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H void bbb_bfa_gen_range_weight_lut(
	tvector1w in[BFA_RW_LUT_SIZE+1],
	tvector1w out[BFA_RW_LUT_SIZE]);
#endif

#ifdef ISP2401
/** @brief OP_1w_imax32
 *
 * @param[in] src - structure that holds an array of 32 elements.
 *
 * @return  maximum element among input array.
 *
 *This function gets maximum element from an array of 32 elements.
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H int OP_1w_imax32(
	imax32_ref_in_vector src);

/** @brief OP_1w_imaxidx32
 *
 * @param[in] src - structure that holds a vector of elements.
 *
 * @return  index of first element with maximum value among array.
 *
 * This function gets index of first element with maximum value
 * from 32 elements.
 */
STORAGE_CLASS_REF_VECTOR_FUNC_H int OP_1w_imaxidx32(
	imax32_ref_in_vector src);

#endif
#ifndef INLINE_VECTOR_FUNC
#define STORAGE_CLASS_REF_VECTOR_FUNC_C
#define STORAGE_CLASS_REF_VECTOR_DATA_C const
#else /* INLINE_VECTOR_FUNC */
#define STORAGE_CLASS_REF_VECTOR_FUNC_C STORAGE_CLASS_REF_VECTOR_FUNC_H
#define STORAGE_CLASS_REF_VECTOR_DATA_C STORAGE_CLASS_REF_VECTOR_DATA_H
#include "ref_vector_func.c"
#define VECTOR_FUNC_INLINED
#endif  /* INLINE_VECTOR_FUNC */

#endif /*_REF_VECTOR_FUNC_H_INCLUDED_*/
