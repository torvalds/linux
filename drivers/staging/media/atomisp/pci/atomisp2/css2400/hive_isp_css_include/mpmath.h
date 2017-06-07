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

#ifndef __MPMATH_H_INCLUDED__
#define __MPMATH_H_INCLUDED__

#include "storage_class.h"

#ifdef INLINE_MPMATH
#define STORAGE_CLASS_MPMATH_FUNC_H STORAGE_CLASS_INLINE
#define STORAGE_CLASS_MPMATH_DATA_H STORAGE_CLASS_INLINE_DATA
#else /* INLINE_MPMATH */
#define STORAGE_CLASS_MPMATH_FUNC_H STORAGE_CLASS_EXTERN
#define STORAGE_CLASS_MPMATH_DATA_H STORAGE_CLASS_EXTERN_DATA
#endif  /* INLINE_MPMATH */

#include <type_support.h>

/*
 * Implementation limits
 */
#define MIN_BITDEPTH            1
#define MAX_BITDEPTH            64

#define ROUND_NEAREST_EVEN  0
#define ROUND_NEAREST       1

/*
 * The MP types
 *
 * "vector lane data" is scalar. With "scalar data" for limited range shift and address values
 */
typedef unsigned long long      mpudata_t;   /* Type of reference MP scalar / vector lane data; unsigned */
typedef long long               mpsdata_t;   /* Type of reference MP scalar / vector lane data; signed */
typedef unsigned short          spudata_t;   /* Type of reference SP scalar / vector lane data; unsigned */
typedef short                   spsdata_t;   /* Type of reference SP scalar / vector lane data; signed */
typedef unsigned short          bitdepth_t;

typedef enum {
    mp_zero_ID,
    mp_one_ID,
    mp_mone_ID,
    mp_smin_ID,
    mp_smax_ID,
    mp_umin_ID,
    mp_umax_ID,
    N_mp_const_ID
} mp_const_ID_t;

#ifdef ISP2401
/* _isValidMpudata is for internal use by mpmath and bbb's.
 * isValidMpudata is for external use by functions on top.
 */
#ifndef ENABLE_VALID_MP_DATA_CHECK
#define _isValidMpsdata(data,bitdepth) (1)
#define _isValidMpudata(data,bitdepth) (1)
#else
#define _isValidMpsdata(data,bitdepth) isValidMpsdata(data,bitdepth)
#define _isValidMpudata(data,bitdepth) isValidMpsdata(data,bitdepth)

#endif
#endif
STORAGE_CLASS_MPMATH_FUNC_H bool isValidMpsdata(
    const mpsdata_t             data,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H bool isValidMpudata(
    const mpudata_t             data,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_castd (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_casth (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_scasth (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_qcastd (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_qcasth (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_qrcasth (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_abs (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_limit (
    const mpsdata_t             bnd_low,
    const mpsdata_t             in0,
    const mpsdata_t             bnd_high,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_max (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_min (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_mux (
    const spudata_t             sel,
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_rmux (
    const spudata_t             sel,
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_add (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_sadd (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_sub (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_ssub (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_addasr1 (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_subasr1 (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_lsr (
    const mpsdata_t             in0,
    const spsdata_t             shft,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_asr (
    const mpsdata_t             in0,
    const spsdata_t             shft,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_rasr (
    const mpsdata_t             in0,
    const spsdata_t             shft,
    const bitdepth_t            bitdepth);

/* "mp_rasr_u()" is implemented by "mp_rasr()" */
STORAGE_CLASS_MPMATH_FUNC_H mpudata_t mp_rasr_u (
    const mpudata_t             in0,
    const spsdata_t             shft,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_lsl (
    const mpsdata_t             in0,
    const spsdata_t             shft,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_asl (
    const mpsdata_t             in0,
    const spsdata_t             shft,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_muld (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_mul (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_qmul (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_qrmul (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_qdiv (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_qdivh (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_div (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_divh (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_and (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_compl (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_or (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_xor (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H spudata_t mp_isEQ (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H spudata_t mp_isNE (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H spudata_t mp_isGT (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H spudata_t mp_isGE (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H spudata_t mp_isLT (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H spudata_t mp_isLE (
    const mpsdata_t             in0,
    const mpsdata_t             in1,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H spudata_t mp_isEQZ (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H spudata_t mp_isNEZ (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H spudata_t mp_isGTZ (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H spudata_t mp_isGEZ (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H spudata_t mp_isLTZ (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H spudata_t mp_isLEZ (
    const mpsdata_t             in0,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpsdata_t mp_const (
    const mp_const_ID_t         ID,
    const bitdepth_t            bitdepth);

STORAGE_CLASS_MPMATH_FUNC_H mpudata_t mp_sqrt_u(
	const mpudata_t     in0,
	const bitdepth_t    bitdepth);

#ifndef INLINE_MPMATH
#define STORAGE_CLASS_MPMATH_FUNC_C 
#define STORAGE_CLASS_MPMATH_DATA_C const
#else /* INLINE_MPMATH */
#define STORAGE_CLASS_MPMATH_FUNC_C STORAGE_CLASS_MPMATH_FUNC_H
#define STORAGE_CLASS_MPMATH_DATA_C STORAGE_CLASS_MPMATH_DATA_H
#include "mpmath.c"
#define MPMATH_INLINED
#endif  /* INLINE_MPMATH */

#endif /* __MPMATH_H_INCLUDED__ */
