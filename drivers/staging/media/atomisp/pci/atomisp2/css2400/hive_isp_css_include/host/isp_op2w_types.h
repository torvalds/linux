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

#ifndef __ISP_OP2W_TYPES_H_INCLUDED__
#define __ISP_OP2W_TYPES_H_INCLUDED__

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
#include "mpmath.h"
#include "isp_op1w_types.h"

/*
 * Single-precision data type specification
 */


typedef mpsdata_t       tvector2w;
typedef mpsdata_t       tscalar2w;
typedef mpsdata_t       tvector2w_signed_positive;
typedef mpudata_t       tvector2w_unsigned;


typedef struct {
  tvector2w       d;
  tflags        f;
} tvector2w_tflags;

#endif /* __ISP_OP2W_TYPES_H_INCLUDED__ */
