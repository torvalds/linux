/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
#ifndef __CIK_REG_H__
#define __CIK_REG_H__

#define CIK_DC_GPIO_HPD_MASK                      0x65b0
#define CIK_DC_GPIO_HPD_A                         0x65b4
#define CIK_DC_GPIO_HPD_EN                        0x65b8
#define CIK_DC_GPIO_HPD_Y                         0x65bc

#define CIK_GRPH_CONTROL                          0x6804
#       define CIK_GRPH_DEPTH(x)                  (((x) & 0x3) << 0)
#       define CIK_GRPH_DEPTH_8BPP                0
#       define CIK_GRPH_DEPTH_16BPP               1
#       define CIK_GRPH_DEPTH_32BPP               2
#       define CIK_GRPH_NUM_BANKS(x)              (((x) & 0x3) << 2)
#       define CIK_ADDR_SURF_2_BANK               0
#       define CIK_ADDR_SURF_4_BANK               1
#       define CIK_ADDR_SURF_8_BANK               2
#       define CIK_ADDR_SURF_16_BANK              3
#       define CIK_GRPH_Z(x)                      (((x) & 0x3) << 4)
#       define CIK_GRPH_BANK_WIDTH(x)             (((x) & 0x3) << 6)
#       define CIK_ADDR_SURF_BANK_WIDTH_1         0
#       define CIK_ADDR_SURF_BANK_WIDTH_2         1
#       define CIK_ADDR_SURF_BANK_WIDTH_4         2
#       define CIK_ADDR_SURF_BANK_WIDTH_8         3
#       define CIK_GRPH_FORMAT(x)                 (((x) & 0x7) << 8)
/* 8 BPP */
#       define CIK_GRPH_FORMAT_INDEXED            0
/* 16 BPP */
#       define CIK_GRPH_FORMAT_ARGB1555           0
#       define CIK_GRPH_FORMAT_ARGB565            1
#       define CIK_GRPH_FORMAT_ARGB4444           2
#       define CIK_GRPH_FORMAT_AI88               3
#       define CIK_GRPH_FORMAT_MONO16             4
#       define CIK_GRPH_FORMAT_BGRA5551           5
/* 32 BPP */
#       define CIK_GRPH_FORMAT_ARGB8888           0
#       define CIK_GRPH_FORMAT_ARGB2101010        1
#       define CIK_GRPH_FORMAT_32BPP_DIG          2
#       define CIK_GRPH_FORMAT_8B_ARGB2101010     3
#       define CIK_GRPH_FORMAT_BGRA1010102        4
#       define CIK_GRPH_FORMAT_8B_BGRA1010102     5
#       define CIK_GRPH_FORMAT_RGB111110          6
#       define CIK_GRPH_FORMAT_BGR101111          7
#       define CIK_GRPH_BANK_HEIGHT(x)            (((x) & 0x3) << 11)
#       define CIK_ADDR_SURF_BANK_HEIGHT_1        0
#       define CIK_ADDR_SURF_BANK_HEIGHT_2        1
#       define CIK_ADDR_SURF_BANK_HEIGHT_4        2
#       define CIK_ADDR_SURF_BANK_HEIGHT_8        3
#       define CIK_GRPH_TILE_SPLIT(x)             (((x) & 0x7) << 13)
#       define CIK_ADDR_SURF_TILE_SPLIT_64B       0
#       define CIK_ADDR_SURF_TILE_SPLIT_128B      1
#       define CIK_ADDR_SURF_TILE_SPLIT_256B      2
#       define CIK_ADDR_SURF_TILE_SPLIT_512B      3
#       define CIK_ADDR_SURF_TILE_SPLIT_1KB       4
#       define CIK_ADDR_SURF_TILE_SPLIT_2KB       5
#       define CIK_ADDR_SURF_TILE_SPLIT_4KB       6
#       define CIK_GRPH_MACRO_TILE_ASPECT(x)      (((x) & 0x3) << 18)
#       define CIK_ADDR_SURF_MACRO_TILE_ASPECT_1  0
#       define CIK_ADDR_SURF_MACRO_TILE_ASPECT_2  1
#       define CIK_ADDR_SURF_MACRO_TILE_ASPECT_4  2
#       define CIK_ADDR_SURF_MACRO_TILE_ASPECT_8  3
#       define CIK_GRPH_ARRAY_MODE(x)             (((x) & 0x7) << 20)
#       define CIK_GRPH_ARRAY_LINEAR_GENERAL      0
#       define CIK_GRPH_ARRAY_LINEAR_ALIGNED      1
#       define CIK_GRPH_ARRAY_1D_TILED_THIN1      2
#       define CIK_GRPH_ARRAY_2D_TILED_THIN1      4
#       define CIK_GRPH_PIPE_CONFIG(x)		 (((x) & 0x1f) << 24)
#       define CIK_ADDR_SURF_P2			 0
#       define CIK_ADDR_SURF_P4_8x16		 4
#       define CIK_ADDR_SURF_P4_16x16		 5
#       define CIK_ADDR_SURF_P4_16x32		 6
#       define CIK_ADDR_SURF_P4_32x32		 7
#       define CIK_ADDR_SURF_P8_16x16_8x16	 8
#       define CIK_ADDR_SURF_P8_16x32_8x16	 9
#       define CIK_ADDR_SURF_P8_32x32_8x16	 10
#       define CIK_ADDR_SURF_P8_16x32_16x16	 11
#       define CIK_ADDR_SURF_P8_32x32_16x16	 12
#       define CIK_ADDR_SURF_P8_32x32_16x32	 13
#       define CIK_ADDR_SURF_P8_32x64_32x32	 14
#       define CIK_GRPH_MICRO_TILE_MODE(x)       (((x) & 0x7) << 29)
#       define CIK_DISPLAY_MICRO_TILING          0
#       define CIK_THIN_MICRO_TILING             1
#       define CIK_DEPTH_MICRO_TILING            2
#       define CIK_ROTATED_MICRO_TILING          4

/* CUR blocks at 0x6998, 0x7598, 0x10198, 0x10d98, 0x11998, 0x12598 */
#define CIK_CUR_CONTROL                           0x6998
#       define CIK_CURSOR_EN                      (1 << 0)
#       define CIK_CURSOR_MODE(x)                 (((x) & 0x3) << 8)
#       define CIK_CURSOR_MONO                    0
#       define CIK_CURSOR_24_1                    1
#       define CIK_CURSOR_24_8_PRE_MULT           2
#       define CIK_CURSOR_24_8_UNPRE_MULT         3
#       define CIK_CURSOR_2X_MAGNIFY              (1 << 16)
#       define CIK_CURSOR_FORCE_MC_ON             (1 << 20)
#       define CIK_CURSOR_URGENT_CONTROL(x)       (((x) & 0x7) << 24)
#       define CIK_CURSOR_URGENT_ALWAYS           0
#       define CIK_CURSOR_URGENT_1_8              1
#       define CIK_CURSOR_URGENT_1_4              2
#       define CIK_CURSOR_URGENT_3_8              3
#       define CIK_CURSOR_URGENT_1_2              4
#define CIK_CUR_SURFACE_ADDRESS                   0x699c
#       define CIK_CUR_SURFACE_ADDRESS_MASK       0xfffff000
#define CIK_CUR_SIZE                              0x69a0
#define CIK_CUR_SURFACE_ADDRESS_HIGH              0x69a4
#define CIK_CUR_POSITION                          0x69a8
#define CIK_CUR_HOT_SPOT                          0x69ac
#define CIK_CUR_COLOR1                            0x69b0
#define CIK_CUR_COLOR2                            0x69b4
#define CIK_CUR_UPDATE                            0x69b8
#       define CIK_CURSOR_UPDATE_PENDING          (1 << 0)
#       define CIK_CURSOR_UPDATE_TAKEN            (1 << 1)
#       define CIK_CURSOR_UPDATE_LOCK             (1 << 16)
#       define CIK_CURSOR_DISABLE_MULTIPLE_UPDATE (1 << 24)

#define CIK_ALPHA_CONTROL                         0x6af0
#       define CIK_CURSOR_ALPHA_BLND_ENA          (1 << 1)

#define CIK_LB_DATA_FORMAT                        0x6b00
#       define CIK_INTERLEAVE_EN                  (1 << 3)

#define CIK_LB_DESKTOP_HEIGHT                     0x6b0c

#endif
