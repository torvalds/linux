/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 */
#ifndef SI_ENUMS_H
#define SI_ENUMS_H

#define VBLANK_ACK                     (1 << 4)
#define VLINE_ACK                      (1 << 4)

#define CURSOR_WIDTH 64
#define CURSOR_HEIGHT 64

#define PRIORITY_MARK_MASK             0x7fff
#define PRIORITY_OFF                   (1 << 16)
#define PRIORITY_ALWAYS_ON             (1 << 20)

#define LATENCY_WATERMARK_MASK(x)      ((x) << 16)
#define DC_LB_MEMORY_CONFIG(x)         ((x) << 20)

#define GRPH_ENDIAN_SWAP(x)            (((x) & 0x3) << 0)
#define GRPH_ENDIAN_NONE               0
#define GRPH_ENDIAN_8IN16              1
#define GRPH_ENDIAN_8IN32              2
#define GRPH_ENDIAN_8IN64              3
#define GRPH_RED_CROSSBAR(x)           (((x) & 0x3) << 4)
#define GRPH_RED_SEL_R                 0
#define GRPH_RED_SEL_G                 1
#define GRPH_RED_SEL_B                 2
#define GRPH_RED_SEL_A                 3
#define GRPH_GREEN_CROSSBAR(x)         (((x) & 0x3) << 6)
#define GRPH_GREEN_SEL_G               0
#define GRPH_GREEN_SEL_B               1
#define GRPH_GREEN_SEL_A               2
#define GRPH_GREEN_SEL_R               3
#define GRPH_BLUE_CROSSBAR(x)          (((x) & 0x3) << 8)
#define GRPH_BLUE_SEL_B                0
#define GRPH_BLUE_SEL_A                1
#define GRPH_BLUE_SEL_R                2
#define GRPH_BLUE_SEL_G                3
#define GRPH_ALPHA_CROSSBAR(x)         (((x) & 0x3) << 10)
#define GRPH_ALPHA_SEL_A               0
#define GRPH_ALPHA_SEL_R               1
#define GRPH_ALPHA_SEL_G               2
#define GRPH_ALPHA_SEL_B               3

#define GRPH_DEPTH(x)                  (((x) & 0x3) << 0)
#define GRPH_DEPTH_8BPP                0
#define GRPH_DEPTH_16BPP               1
#define GRPH_DEPTH_32BPP               2

#define GRPH_FORMAT(x)                 (((x) & 0x7) << 8)
#define GRPH_FORMAT_INDEXED            0
#define GRPH_FORMAT_ARGB1555           0
#define GRPH_FORMAT_ARGB565            1
#define GRPH_FORMAT_ARGB4444           2
#define GRPH_FORMAT_AI88               3
#define GRPH_FORMAT_MONO16             4
#define GRPH_FORMAT_BGRA5551           5
#define GRPH_FORMAT_ARGB8888           0
#define GRPH_FORMAT_ARGB2101010        1
#define GRPH_FORMAT_32BPP_DIG          2
#define GRPH_FORMAT_8B_ARGB2101010     3
#define GRPH_FORMAT_BGRA1010102        4
#define GRPH_FORMAT_8B_BGRA1010102     5
#define GRPH_FORMAT_RGB111110          6
#define GRPH_FORMAT_BGR101111          7

#define GRPH_NUM_BANKS(x)              (((x) & 0x3) << 2)
#define GRPH_ARRAY_MODE(x)             (((x) & 0x7) << 20)
#define GRPH_ARRAY_LINEAR_GENERAL      0
#define GRPH_ARRAY_LINEAR_ALIGNED      1
#define GRPH_ARRAY_1D_TILED_THIN1      2
#define GRPH_ARRAY_2D_TILED_THIN1      4
#define GRPH_TILE_SPLIT(x)             (((x) & 0x7) << 13)
#define GRPH_BANK_WIDTH(x)             (((x) & 0x3) << 6)
#define GRPH_BANK_HEIGHT(x)            (((x) & 0x3) << 11)
#define GRPH_MACRO_TILE_ASPECT(x)      (((x) & 0x3) << 18)
#define GRPH_ARRAY_MODE(x)             (((x) & 0x7) << 20)
#define GRPH_PIPE_CONFIG(x)                   (((x) & 0x1f) << 24)

#define CURSOR_EN                      (1 << 0)
#define CURSOR_MODE(x)                 (((x) & 0x3) << 8)
#define CURSOR_MONO                    0
#define CURSOR_24_1                    1
#define CURSOR_24_8_PRE_MULT           2
#define CURSOR_24_8_UNPRE_MULT         3
#define CURSOR_2X_MAGNIFY              (1 << 16)
#define CURSOR_FORCE_MC_ON             (1 << 20)
#define CURSOR_URGENT_CONTROL(x)       (((x) & 0x7) << 24)
#define CURSOR_URGENT_ALWAYS           0
#define CURSOR_URGENT_1_8              1
#define CURSOR_URGENT_1_4              2
#define CURSOR_URGENT_3_8              3
#define CURSOR_URGENT_1_2              4
#define CURSOR_UPDATE_PENDING          (1 << 0)
#define CURSOR_UPDATE_TAKEN            (1 << 1)
#define CURSOR_UPDATE_LOCK             (1 << 16)
#define CURSOR_DISABLE_MULTIPLE_UPDATE (1 << 24)


#define ES_AND_GS_AUTO       3
#define RADEON_PACKET_TYPE3  3
#define CE_PARTITION_BASE    3
#define BUF_SWAP_32BIT       (2 << 16)

#define GFX_POWER_STATUS                           (1 << 1)
#define GFX_CLOCK_STATUS                           (1 << 2)
#define GFX_LS_STATUS                              (1 << 3)
#define RLC_BUSY_STATUS                            (1 << 0)

#define RLC_PUD(x)                               ((x) << 0)
#define RLC_PUD_MASK                             (0xff << 0)
#define RLC_PDD(x)                               ((x) << 8)
#define RLC_PDD_MASK                             (0xff << 8)
#define RLC_TTPD(x)                              ((x) << 16)
#define RLC_TTPD_MASK                            (0xff << 16)
#define RLC_MSD(x)                               ((x) << 24)
#define RLC_MSD_MASK                             (0xff << 24)
#define WRITE_DATA_ENGINE_SEL(x) ((x) << 30)
#define WRITE_DATA_DST_SEL(x) ((x) << 8)
#define EVENT_TYPE(x) ((x) << 0)
#define EVENT_INDEX(x) ((x) << 8)
#define WAIT_REG_MEM_MEM_SPACE(x)               ((x) << 4)
#define WAIT_REG_MEM_FUNCTION(x)                ((x) << 0)
#define WAIT_REG_MEM_ENGINE(x)                  ((x) << 8)

#define RLC_SAVE_AND_RESTORE_STARTING_OFFSET 0x90
#define RLC_CLEAR_STATE_DESCRIPTOR_OFFSET    0x3D

#endif
