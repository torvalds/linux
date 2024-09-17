/*
 * Copyright 2010 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#ifndef __SI_REG_H__
#define __SI_REG_H__

/* SI */
#define SI_DC_GPIO_HPD_MASK                      0x65b0
#define SI_DC_GPIO_HPD_A                         0x65b4
#define SI_DC_GPIO_HPD_EN                        0x65b8
#define SI_DC_GPIO_HPD_Y                         0x65bc

#define SI_GRPH_CONTROL                          0x6804
#       define SI_GRPH_DEPTH(x)                  (((x) & 0x3) << 0)
#       define SI_GRPH_DEPTH_8BPP                0
#       define SI_GRPH_DEPTH_16BPP               1
#       define SI_GRPH_DEPTH_32BPP               2
#       define SI_GRPH_NUM_BANKS(x)              (((x) & 0x3) << 2)
#       define SI_ADDR_SURF_2_BANK               0
#       define SI_ADDR_SURF_4_BANK               1
#       define SI_ADDR_SURF_8_BANK               2
#       define SI_ADDR_SURF_16_BANK              3
#       define SI_GRPH_Z(x)                      (((x) & 0x3) << 4)
#       define SI_GRPH_BANK_WIDTH(x)             (((x) & 0x3) << 6)
#       define SI_ADDR_SURF_BANK_WIDTH_1         0
#       define SI_ADDR_SURF_BANK_WIDTH_2         1
#       define SI_ADDR_SURF_BANK_WIDTH_4         2
#       define SI_ADDR_SURF_BANK_WIDTH_8         3
#       define SI_GRPH_FORMAT(x)                 (((x) & 0x7) << 8)
/* 8 BPP */
#       define SI_GRPH_FORMAT_INDEXED            0
/* 16 BPP */
#       define SI_GRPH_FORMAT_ARGB1555           0
#       define SI_GRPH_FORMAT_ARGB565            1
#       define SI_GRPH_FORMAT_ARGB4444           2
#       define SI_GRPH_FORMAT_AI88               3
#       define SI_GRPH_FORMAT_MONO16             4
#       define SI_GRPH_FORMAT_BGRA5551           5
/* 32 BPP */
#       define SI_GRPH_FORMAT_ARGB8888           0
#       define SI_GRPH_FORMAT_ARGB2101010        1
#       define SI_GRPH_FORMAT_32BPP_DIG          2
#       define SI_GRPH_FORMAT_8B_ARGB2101010     3
#       define SI_GRPH_FORMAT_BGRA1010102        4
#       define SI_GRPH_FORMAT_8B_BGRA1010102     5
#       define SI_GRPH_FORMAT_RGB111110          6
#       define SI_GRPH_FORMAT_BGR101111          7
#       define SI_GRPH_BANK_HEIGHT(x)            (((x) & 0x3) << 11)
#       define SI_ADDR_SURF_BANK_HEIGHT_1        0
#       define SI_ADDR_SURF_BANK_HEIGHT_2        1
#       define SI_ADDR_SURF_BANK_HEIGHT_4        2
#       define SI_ADDR_SURF_BANK_HEIGHT_8        3
#       define SI_GRPH_TILE_SPLIT(x)             (((x) & 0x7) << 13)
#       define SI_ADDR_SURF_TILE_SPLIT_64B       0
#       define SI_ADDR_SURF_TILE_SPLIT_128B      1
#       define SI_ADDR_SURF_TILE_SPLIT_256B      2
#       define SI_ADDR_SURF_TILE_SPLIT_512B      3
#       define SI_ADDR_SURF_TILE_SPLIT_1KB       4
#       define SI_ADDR_SURF_TILE_SPLIT_2KB       5
#       define SI_ADDR_SURF_TILE_SPLIT_4KB       6
#       define SI_GRPH_MACRO_TILE_ASPECT(x)      (((x) & 0x3) << 18)
#       define SI_ADDR_SURF_MACRO_TILE_ASPECT_1  0
#       define SI_ADDR_SURF_MACRO_TILE_ASPECT_2  1
#       define SI_ADDR_SURF_MACRO_TILE_ASPECT_4  2
#       define SI_ADDR_SURF_MACRO_TILE_ASPECT_8  3
#       define SI_GRPH_ARRAY_MODE(x)             (((x) & 0x7) << 20)
#       define SI_GRPH_ARRAY_LINEAR_GENERAL      0
#       define SI_GRPH_ARRAY_LINEAR_ALIGNED      1
#       define SI_GRPH_ARRAY_1D_TILED_THIN1      2
#       define SI_GRPH_ARRAY_2D_TILED_THIN1      4
#       define SI_GRPH_PIPE_CONFIG(x)		 (((x) & 0x1f) << 24)
#       define SI_ADDR_SURF_P2			 0
#       define SI_ADDR_SURF_P4_8x16		 4
#       define SI_ADDR_SURF_P4_16x16		 5
#       define SI_ADDR_SURF_P4_16x32		 6
#       define SI_ADDR_SURF_P4_32x32		 7
#       define SI_ADDR_SURF_P8_16x16_8x16	 8
#       define SI_ADDR_SURF_P8_16x32_8x16	 9
#       define SI_ADDR_SURF_P8_32x32_8x16	 10
#       define SI_ADDR_SURF_P8_16x32_16x16	 11
#       define SI_ADDR_SURF_P8_32x32_16x16	 12
#       define SI_ADDR_SURF_P8_32x32_16x32	 13
#       define SI_ADDR_SURF_P8_32x64_32x32	 14

#endif
