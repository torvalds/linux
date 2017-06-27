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

#ifndef __ISP2500_CONFIG_H_INCLUDED__
#define __ISP2500_CONFIG_H_INCLUDED__

#define NUM_BITS            12
#define NUM_SLICE_ELEMS     4
#define ROUNDMODE           ROUND_NEAREST_EVEN
#define MAX_SHIFT_1W        (NUM_BITS-1)   /* Max number of bits a 1w input can be shifted */
#define MAX_SHIFT_2W        (2*NUM_BITS-1) /* Max number of bits a 2w input can be shifted */


#define HAS_div_unit

#define HAS_vec_sub

#endif /* __ISP2500_CONFIG_H_INCLUDED__ */
