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

#ifndef __ISP_OP2W_H_INCLUDED__
#define __ISP_OP2W_H_INCLUDED__

/*
 * This file is part of the Multi-precision vector operations exstension package.
 */

/*
 * Double-precision vector operations
 */

/*
 * Prerequisites:
 *
 */

#ifdef INLINE_ISP_OP2W
#define STORAGE_CLASS_ISP_OP2W_FUNC_H static inline
#define STORAGE_CLASS_ISP_OP2W_DATA_H static inline_DATA
#else /* INLINE_ISP_OP2W */
#define STORAGE_CLASS_ISP_OP2W_FUNC_H extern
#define STORAGE_CLASS_ISP_OP2W_DATA_H extern_DATA
#endif  /* INLINE_ISP_OP2W */

/*
 * Double-precision data type specification
 */

#include "isp_op2w_types.h"

/*
 * Double-precision prototype specification
 */

/* Arithmetic */

/** @brief bitwise AND
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		bitwise and of both input arguments
 *
 * This function will calculate the bitwise and.
 * result = _a & _b
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_and(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief bitwise OR
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		bitwise or of both input arguments
 *
 * This function will calculate the bitwise or.
 * result = _a | _b
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_or(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief bitwise XOR
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		bitwise xor of both input arguments
 *
 * This function will calculate the bitwise xor.
 * result = _a ^ _b
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_xor(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief bitwise inverse
 *
 * @param[in] _a	first argument
 *
 * @return		bitwise inverse of both input arguments
 *
 * This function will calculate the bitwise inverse.
 * result = ~_a
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_inv(
    const tvector2w     _a);

/* Additive */

/** @brief addition
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		sum of both input arguments
 *
 * This function will calculate the sum of the input arguments.
 * in case of overflow it will wrap around.
 * result = _a + _b
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_add(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief subtraction
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_b subtracted from _a.
 *
 * This function will subtract _b from _a.
 * in case of overflow it will wrap around.
 * result = _a - _b
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_sub(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief saturated addition
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		saturated sum of both input arguments
 *
 * This function will calculate the sum of the input arguments.
 * in case of overflow it will saturate
 * result = CLIP(_a + _b, MIN_RANGE, MAX_RANGE);
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_addsat(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief saturated subtraction
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		saturated subtraction of both input arguments
 *
 * This function will subtract _b from _a.
 * in case of overflow it will saturate
 * result = CLIP(_a - _b, MIN_RANGE, MAX_RANGE);
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_subsat(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief subtraction with shift right and rounding
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		(a - b) >> 1
 *
 * This function subtracts _b from _a and right shifts
 * the result by 1 bit with rounding.
 * No overflow can occur.
 * result = (_a - _b) >> 1
 *
 * Note: This function will be deprecated due to
 * the naming confusion and it will be replaced
 * by "OP_2w_subhalfrnd".
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_subasr1(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief Subtraction with shift right and rounding
 *
 * @param[in] _a	first operand
 * @param[in] _b	second operand
 *
 * @return		(_a - _b) >> 1
 *
 * This function subtracts _b from _a and right shifts
 * the result by 1 bit with rounding.
 * No overflow can occur.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_subhalfrnd(
    const tvector2w	_a,
    const tvector2w	_b);

/** @brief Subtraction with shift right and no rounding
 *
 * @param[in] _a	first operand
 * @param[in] _b	second operand
 *
 * @return		(_a - _b) >> 1
 *
 * This function subtracts _b from _a and right shifts
 * the result by 1 bit without rounding (i.e. truncation).
 * No overflow can occur.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_subhalf(
    const tvector2w	_a,
    const tvector2w	_b);

/** @brief saturated absolute value
 *
 * @param[in] _a	input
 *
 * @return		saturated absolute value of the input
 *
 * This function will calculate the saturated absolute value of the input.
 * In case of overflow it will saturate.
 * if (_a > 0) return _a;<br>
 * else return CLIP(-_a, MIN_RANGE, MAX_RANGE);<br>
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_abs(
    const tvector2w     _a);

/** @brief saturated absolute difference
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		sat(abs(sat(a-b)));
 *
 * This function will calculate the saturated absolute value
 * of the saturated difference of both inputs.
 * result = sat(abs(sat(_a - _b)));
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_subabssat(
    const tvector2w     _a,
    const tvector2w     _b);

/* Multiplicative */

/** @brief integer multiply
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		product of _a and _b
 *
 * This function will calculate the product
 * of the input arguments and returns the LSB
 * aligned double precision result.
 * In case of overflow it will wrap around.
 * result = _a * _b;
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_mul(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief fractional saturating multiply
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		saturated product of _a and _b
 *
 * This function will calculate the fixed point
 * product of the input arguments
 * and returns a double precision result.
 * In case of overflow it will saturate.
 * result =((_a * _b) << 1) >> (2*NUM_BITS);
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_qmul(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief fractional saturating multiply with rounding
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		product of _a and _b
 *
 * This function will calculate the fixed point
 * product of the input arguments
 * and returns a double precision result.
 * Depending on the rounding mode of the core
 * it will round to nearest or to nearest even.
 * In case of overflow it will saturate.
 * result = ((_a * _b) << 1) >> (2*NUM_BITS);
 */

STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_qrmul(
    const tvector2w     _a,
    const tvector2w     _b);

/* Comparative */

/** @brief equal
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a == _b
 *
 * This function will return true if both inputs
 * are equal, and false if not equal.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tflags OP_2w_eq(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief not equal
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a != _b
 *
 * This function will return false if both inputs
 * are equal, and true if not equal.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tflags OP_2w_ne(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief less or equal
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a <= _b
 *
 * This function will return true if _a is smaller
 * or equal than _b.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tflags OP_2w_le(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief less then
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a < _b
 *
 * This function will return true if _a is smaller
 * than _b.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tflags OP_2w_lt(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief greater or equal
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a >= _b
 *
 * This function will return true if _a is greater
 * or equal than _b.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tflags OP_2w_ge(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief greater than
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a > _b
 *
 * This function will return true if _a is greater
 * than _b.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tflags OP_2w_gt(
    const tvector2w     _a,
    const tvector2w     _b);

/* Shift */

/** @brief aritmetic shift right
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a >> _b
 *
 * This function will shift _a with _b bits to the right,
 * preserving the sign bit.
 * It asserts 0 <= _b <= MAX_SHIFT_2W.
 * The operation count for this function assumes that
 * the shift amount is a cloned scalar input.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_asr(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief aritmetic shift right with rounding
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a >> _b
 *
 * If _b < 2*NUM_BITS, this function will shift _a with _b bits to the right,
 * preserving the sign bit, and depending on the rounding mode of the core
 * it will round to nearest or to nearest even.
 * If _b >= 2*NUM_BITS, this function will return 0.
 * It asserts 0 <= _b <= MAX_SHIFT_2W.
 * The operation count for this function assumes that
 * the shift amount is a cloned scalar input.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_asrrnd(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief saturating aritmetic shift left
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a << _b
 *
 * If _b < MAX_BITDEPTH, this function will shift _a with _b bits to the left,
 * saturating at MIN_RANGE/MAX_RANGE in case of overflow.
 * If _b >= MAX_BITDEPTH, this function will return MIN_RANGE if _a < 0,
 * MAX_RANGE if _a > 0, 0 if _a == 0.
 * (with MAX_BITDEPTH=64)
 * It asserts 0 <= _b <= MAX_SHIFT_2W.
 * The operation count for this function assumes that
 * the shift amount is a cloned scalar input.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_asl(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief saturating aritmetic shift left
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a << _b
 *
 * This function is identical to OP_2w_asl( )
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_aslsat(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief logical shift left
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a << _b
 *
 * This function will shift _a with _b bits to the left.
 * It will insert zeroes on the right.
 * It asserts 0 <= _b <= MAX_SHIFT_2W.
 * The operation count for this function assumes that
 * the shift amount is a cloned scalar input.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_lsl(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief logical shift right
 *
 * @param[in] _a	input
 * @param[in] _b	shift amount
 *
 * @return		_a >> _b
 *
 * This function will shift _a with _b bits to the right.
 * It will insert zeroes on the left.
 * It asserts 0 <= _b <= MAX_SHIFT_2W.
 * The operation count for this function assumes that
 * the shift amount is a cloned scalar input.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_lsr(
    const tvector2w     _a,
    const tvector2w     _b);

/* clipping */

/** @brief Clip asymmetrical
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a clipped between ~_b and b
 *
 * This function will clip the first argument between
 * (-_b - 1) and _b.
 * It asserts _b >= 0.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_clip_asym(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief Clip zero
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		_a clipped beteween 0 and _b
 *
 * This function will clip the first argument between
 * zero and _b.
 * It asserts _b >= 0.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_clipz(
    const tvector2w     _a,
    const tvector2w     _b);

/* division */

/** @brief Truncated division
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		trunc( _a / _b )
 *
 * This function will divide the first argument by
 * the second argument, with rounding toward 0.
 * If _b == 0 and _a <  0, the function will return MIN_RANGE.
 * If _b == 0 and _a == 0, the function will return 0.
 * If _b == 0 and _a >  0, the function will return MAX_RANGE.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_div(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief Saturating truncated division
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		CLIP( trunc( _a / _b ), MIN_RANGE1w, MAX_RANGE1w )
 *
 * This function will divide the first argument by
 * the second argument, with rounding toward 0, and
 * saturate the result to the range of single precision.
 * If _b == 0 and _a <  0, the function will return MIN_RANGE.
 * If _b == 0 and _a == 0, the function will return 0.
 * If _b == 0 and _a >  0, the function will return MAX_RANGE.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector1w OP_2w_divh(
    const tvector2w     _a,
    const tvector1w     _b);

/** @brief Modulo
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		n/a
 *
 * This function has not yet been implemented.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_mod(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief Unsigned Integer Square root
 *
 * @param[in] _a	input
 *
 * @return		square root of _a
 *
 * This function will calculate the unsigned integer square root of _a
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector1w_unsigned OP_2w_sqrt_u(
	const tvector2w_unsigned     _a);

/* Miscellaneous */

/** @brief Multiplexer
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 * @param[in] _c	condition
 *
 * @return		_c ? _a : _b
 *
 * This function will return _a if the condition _c
 * is true and _b otherwise.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_mux(
    const tvector2w     _a,
    const tvector2w     _b,
    const tflags           _c);

/** @brief Average without rounding
 *
 * @param[in] _a	first operand
 * @param[in] _b	second operand
 *
 * @return		(_a + _b) >> 1
 *
 * This function will add _a and _b, and right shift
 * the result by one without rounding. No overflow
 * will occur because addition is performed in the
 * proper precision.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w  OP_2w_avg(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief Average with rounding
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		(_a + _b) >> 1
 *
 * This function will add _a and _b at full precision,
 * and right shift with rounding the result with 1 bit.
 * Depending on the rounding mode of the core
 * it will round to nearest or to nearest even.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_avgrnd(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief Minimum
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		(_a < _b) ? _a : _b;
 *
 * This function will return the smallest of both
 * input arguments.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_min(
    const tvector2w     _a,
    const tvector2w     _b);

/** @brief Maximum
 *
 * @param[in] _a	first argument
 * @param[in] _b	second argument
 *
 * @return		(_a > _b) ? _a : _b;
 *
 * This function will return the largest of both
 * input arguments.
 */
STORAGE_CLASS_ISP_OP2W_FUNC_H tvector2w OP_2w_max(
    const tvector2w     _a,
    const tvector2w     _b);

#ifndef INLINE_ISP_OP2W
#define STORAGE_CLASS_ISP_OP2W_FUNC_C
#define STORAGE_CLASS_ISP_OP2W_DATA_C const
#else /* INLINE_ISP_OP2W */
#define STORAGE_CLASS_ISP_OP2W_FUNC_C STORAGE_CLASS_ISP_OP2W_FUNC_H
#define STORAGE_CLASS_ISP_OP2W_DATA_C STORAGE_CLASS_ISP_OP2W_DATA_H
#include "isp_op2w.c"
#define ISP_OP2W_INLINED
#endif  /* INLINE_ISP_OP2W */

#endif /* __ISP_OP2W_H_INCLUDED__ */
