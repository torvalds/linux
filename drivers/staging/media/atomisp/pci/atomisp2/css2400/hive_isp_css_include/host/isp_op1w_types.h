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

#ifndef __ISP_OP1W_TYPES_H_INCLUDED__
#define __ISP_OP1W_TYPES_H_INCLUDED__

/*
 * This file is part of the Multi-precision vector operations exstension package.
 */
 
/* 
 * Single-precision vector operations
 */
 
/*  
 * Prerequisites:
 *
 */

#include "mpmath.h"

/*
 * Single-precision data type specification
 */


typedef mpsdata_t       tvector1w;
typedef mpsdata_t       tscalar1w;
typedef spsdata_t       tflags;
typedef mpudata_t       tvector1w_unsigned;
typedef mpsdata_t       tscalar1w_weight;
typedef mpsdata_t       tvector1w_signed_positive;
typedef mpsdata_t       tvector1w_weight;
#ifdef ISP2401
typedef bool            tscalar_bool;
#endif

typedef  struct {
  tvector1w       d;
  tflags        f;
} tvector1w_tflags1w;

#endif /* __ISP_OP1W_TYPES_H_INCLUDED__ */
