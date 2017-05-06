#ifndef ISP2401
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

#ifndef __ISP2601_CONFIG_H_INCLUDED__
#define __ISP2601_CONFIG_H_INCLUDED__

#define NUM_BITS 16
#define ISP_VEC_ELEMBITS NUM_BITS
#define ISP_NWAY		32
#define NUM_SLICE_ELEMS 4
#define ROUNDMODE           ROUND_NEAREST_EVEN
#define MAX_SHIFT_1W        (NUM_BITS-1)   /* Max number of bits a 1w input can be shifted */
#define MAX_SHIFT_2W        (2*NUM_BITS-1) /* Max number of bits a 2w input can be shifted */

#define HAS_div_unit
#define HAS_bfa_unit
#define HAS_1w_sqrt_u_unit
#define HAS_2w_sqrt_u_unit

#define HAS_vec_sub

/* Bit widths and element widths defined in HW implementation of BFA */
#define BFA_THRESHOLD_BIT_CNT       (8)
#define BFA_THRESHOLD_MASK          ((1<<BFA_THRESHOLD_BIT_CNT)-1)
#define BFA_SW_BIT_CNT              (7)
#define BFA_SW_MASK                 ((1<<BFA_SW_BIT_CNT)-1)

#define BFA_RW_BIT_CNT              (7)
#define BFA_RW_MASK                 ((1<<BFA_RW_BIT_CNT)-1)
#define BFA_RW_SLOPE_BIT_POS        (8)
#define BFA_RW_SLOPE_BIT_SHIFT      (5)

#define BFA_RW_IDX_BIT_CNT          (3)
#define BFA_RW_FRAC_BIT_CNT         (5)
#define BFA_RW_LUT0_FRAC_START_BIT  (0)
#define BFA_RW_LUT0_FRAC_END_BIT    (BFA_RW_LUT0_FRAC_START_BIT+BFA_RW_FRAC_BIT_CNT-1) /* 4 */
#define BFA_RW_LUT1_FRAC_START_BIT  (2)
#define BFA_RW_LUT1_FRAC_END_BIT    (BFA_RW_LUT1_FRAC_START_BIT+BFA_RW_FRAC_BIT_CNT-1) /* 6 */
/* LUT IDX end bit computation, start+idx_bit_cnt-2, one -1 comes as we count
 * bits from 0, another -1 comes as we use 2 lut table, so idx_bit_cnt is one
 * bit more */
#define BFA_RW_LUT0_IDX_START_BIT   (BFA_RW_LUT0_FRAC_END_BIT+1) /* 5 */
#define BFA_RW_LUT0_IDX_END_BIT     (BFA_RW_LUT0_IDX_START_BIT+BFA_RW_IDX_BIT_CNT-2) /* 6 */
#define BFA_RW_LUT1_IDX_START_BIT   (BFA_RW_LUT1_FRAC_END_BIT + 1) /* 7 */
#define BFA_RW_LUT1_IDX_END_BIT     (BFA_RW_LUT1_IDX_START_BIT+BFA_RW_IDX_BIT_CNT-2) /* 8 */
#define BFA_RW_LUT_THRESHOLD        (1<<(BFA_RW_LUT1_IDX_END_BIT-1)) /* 0x80 : next bit after lut1 end is set */
#define BFA_RW_LUT1_IDX_OFFSET      ((1<<(BFA_RW_IDX_BIT_CNT-1))-1) /* 3 */

#define BFA_CP_MASK                 (0xFFFFFF80)
#define BFA_SUBABS_SHIFT            (6)
#define BFA_SUBABS_BIT_CNT          (8)
#define BFA_SUBABS_MAX              ((1<<BFA_SUBABS_BIT_CNT)-1)
#define BFA_SUBABSSAT_BIT_CNT       (9)
#define BFA_SUBABSSAT_MAX           ((1<<BFA_SUBABSSAT_BIT_CNT)-1)
#define BFA_WEIGHT_SHIFT            (6)

#endif /* __ISP2601_CONFIG_H_INCLUDED__ */
#endif
