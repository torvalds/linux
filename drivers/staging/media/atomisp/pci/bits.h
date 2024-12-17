/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef _HRT_BITS_H
#define _HRT_BITS_H

#include <linux/args.h>

#define _hrt_ones(n)	CONCATENATE(_hrt_ones_, n)
#define _hrt_ones_0x0  0x00000000U
#define _hrt_ones_0x1  0x00000001U
#define _hrt_ones_0x2  0x00000003U
#define _hrt_ones_0x3  0x00000007U
#define _hrt_ones_0x4  0x0000000FU
#define _hrt_ones_0x5  0x0000001FU
#define _hrt_ones_0x6  0x0000003FU
#define _hrt_ones_0x7  0x0000007FU
#define _hrt_ones_0x8  0x000000FFU
#define _hrt_ones_0x9  0x000001FFU
#define _hrt_ones_0xA  0x000003FFU
#define _hrt_ones_0xB  0x000007FFU
#define _hrt_ones_0xC  0x00000FFFU
#define _hrt_ones_0xD  0x00001FFFU
#define _hrt_ones_0xE  0x00003FFFU
#define _hrt_ones_0xF  0x00007FFFU
#define _hrt_ones_0x10 0x0000FFFFU
#define _hrt_ones_0x11 0x0001FFFFU
#define _hrt_ones_0x12 0x0003FFFFU
#define _hrt_ones_0x13 0x0007FFFFU
#define _hrt_ones_0x14 0x000FFFFFU
#define _hrt_ones_0x15 0x001FFFFFU
#define _hrt_ones_0x16 0x003FFFFFU
#define _hrt_ones_0x17 0x007FFFFFU
#define _hrt_ones_0x18 0x00FFFFFFU
#define _hrt_ones_0x19 0x01FFFFFFU
#define _hrt_ones_0x1A 0x03FFFFFFU
#define _hrt_ones_0x1B 0x07FFFFFFU
#define _hrt_ones_0x1C 0x0FFFFFFFU
#define _hrt_ones_0x1D 0x1FFFFFFFU
#define _hrt_ones_0x1E 0x3FFFFFFFU
#define _hrt_ones_0x1F 0x7FFFFFFFU
#define _hrt_ones_0x20 0xFFFFFFFFU

#define _hrt_ones_0  _hrt_ones_0x0
#define _hrt_ones_1  _hrt_ones_0x1
#define _hrt_ones_2  _hrt_ones_0x2
#define _hrt_ones_3  _hrt_ones_0x3
#define _hrt_ones_4  _hrt_ones_0x4
#define _hrt_ones_5  _hrt_ones_0x5
#define _hrt_ones_6  _hrt_ones_0x6
#define _hrt_ones_7  _hrt_ones_0x7
#define _hrt_ones_8  _hrt_ones_0x8
#define _hrt_ones_9  _hrt_ones_0x9
#define _hrt_ones_10 _hrt_ones_0xA
#define _hrt_ones_11 _hrt_ones_0xB
#define _hrt_ones_12 _hrt_ones_0xC
#define _hrt_ones_13 _hrt_ones_0xD
#define _hrt_ones_14 _hrt_ones_0xE
#define _hrt_ones_15 _hrt_ones_0xF
#define _hrt_ones_16 _hrt_ones_0x10
#define _hrt_ones_17 _hrt_ones_0x11
#define _hrt_ones_18 _hrt_ones_0x12
#define _hrt_ones_19 _hrt_ones_0x13
#define _hrt_ones_20 _hrt_ones_0x14
#define _hrt_ones_21 _hrt_ones_0x15
#define _hrt_ones_22 _hrt_ones_0x16
#define _hrt_ones_23 _hrt_ones_0x17
#define _hrt_ones_24 _hrt_ones_0x18
#define _hrt_ones_25 _hrt_ones_0x19
#define _hrt_ones_26 _hrt_ones_0x1A
#define _hrt_ones_27 _hrt_ones_0x1B
#define _hrt_ones_28 _hrt_ones_0x1C
#define _hrt_ones_29 _hrt_ones_0x1D
#define _hrt_ones_30 _hrt_ones_0x1E
#define _hrt_ones_31 _hrt_ones_0x1F
#define _hrt_ones_32 _hrt_ones_0x20

#define _hrt_mask(b, n) \
  (_hrt_ones(n) << (b))
#define _hrt_get_bits(w, b, n) \
  (((w) >> (b)) & _hrt_ones(n))
#define _hrt_set_bits(w, b, n, v) \
  (((w) & ~_hrt_mask(b, n)) | (((v) & _hrt_ones(n)) << (b)))
#define _hrt_get_bit(w, b) \
  (((w) >> (b)) & 1)
#define _hrt_set_bit(w, b, v) \
  (((w) & (~(1 << (b)))) | (((v) & 1) << (b)))
#define _hrt_set_lower_half(w, v) \
  _hrt_set_bits(w, 0, 16, v)
#define _hrt_set_upper_half(w, v) \
  _hrt_set_bits(w, 16, 16, v)

#endif /* _HRT_BITS_H */
