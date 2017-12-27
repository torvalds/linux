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

#ifndef _IA_CSS_FRAC_H
#define _IA_CSS_FRAC_H

/* @file
 * This file contains typedefs used for fractional numbers
 */

#include <type_support.h>

/* Fixed point types.
 * NOTE: the 16 bit fixed point types actually occupy 32 bits
 * to save on extension operations in the ISP code.
 */
/* Unsigned fixed point value, 0 integer bits, 16 fractional bits */
typedef uint32_t ia_css_u0_16;
/* Unsigned fixed point value, 5 integer bits, 11 fractional bits */
typedef uint32_t ia_css_u5_11;
/* Unsigned fixed point value, 8 integer bits, 8 fractional bits */
typedef uint32_t ia_css_u8_8;
/* Signed fixed point value, 0 integer bits, 15 fractional bits */
typedef int32_t ia_css_s0_15;

#endif /* _IA_CSS_FRAC_H */
