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
#else
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif

#ifndef _COMMON_ISP_CONST_H_
#define _COMMON_ISP_CONST_H_

/*#include "isp.h"*/	/* ISP_VEC_NELEMS */

/* Binary independent constants */

#ifndef NO_HOIST
#  define		NO_HOIST 	HIVE_ATTRIBUTE (( no_hoist ))
#endif

#define NO_HOIST_CSE HIVE_ATTRIBUTE ((no_hoist, no_cse))

#define UNION struct /* Union constructors not allowed in C++ */

/* ISP binary identifiers.
   These determine the order in which the binaries are looked up, do not change
   this!
   Also, the SP firmware uses this same order (isp_loader.hive.c).
   Also, gen_firmware.c uses this order in its firmware_header.
*/
/* The binary id is used in pre-processor expressions so we cannot
 * use an enum here. */
 /* 24xx pipelines*/
#define SH_CSS_BINARY_ID_COPY                      0
#define SH_CSS_BINARY_ID_BAYER_DS                  1
#define SH_CSS_BINARY_ID_VF_PP_FULL                2
#define SH_CSS_BINARY_ID_VF_PP_OPT                 3
#define SH_CSS_BINARY_ID_YUV_SCALE                 4
#define SH_CSS_BINARY_ID_CAPTURE_PP                5
#define SH_CSS_BINARY_ID_PRE_ISP                   6
#define SH_CSS_BINARY_ID_PRE_ISP_ISP2              7
#define SH_CSS_BINARY_ID_GDC                       8
#define SH_CSS_BINARY_ID_POST_ISP                  9
#define SH_CSS_BINARY_ID_POST_ISP_ISP2            10
#define SH_CSS_BINARY_ID_ANR                      11
#define SH_CSS_BINARY_ID_ANR_ISP2                 12
#define SH_CSS_BINARY_ID_PREVIEW_CONT_DS          13
#define SH_CSS_BINARY_ID_PREVIEW_DS               14
#define SH_CSS_BINARY_ID_PREVIEW_DEC              15
#define SH_CSS_BINARY_ID_PREVIEW_CONT_BDS125_ISP2 16
#define SH_CSS_BINARY_ID_PREVIEW_CONT_DPC_BDS150_ISP2 17
#define SH_CSS_BINARY_ID_PREVIEW_CONT_BDS150_ISP2 18
#define SH_CSS_BINARY_ID_PREVIEW_CONT_DPC_BDS200_ISP2 19
#define SH_CSS_BINARY_ID_PREVIEW_CONT_BDS200_ISP2 20
#define SH_CSS_BINARY_ID_PREVIEW_DZ               21
#define SH_CSS_BINARY_ID_PREVIEW_DZ_ISP2          22
#define SH_CSS_BINARY_ID_PRIMARY_DS               23
#define SH_CSS_BINARY_ID_PRIMARY_VAR              24
#define SH_CSS_BINARY_ID_PRIMARY_VAR_ISP2         25
#define SH_CSS_BINARY_ID_PRIMARY_SMALL            26
#define SH_CSS_BINARY_ID_PRIMARY_STRIPED          27
#define SH_CSS_BINARY_ID_PRIMARY_STRIPED_ISP2     28
#define SH_CSS_BINARY_ID_PRIMARY_8MP              29
#define SH_CSS_BINARY_ID_PRIMARY_14MP             30
#define SH_CSS_BINARY_ID_PRIMARY_16MP             31
#define SH_CSS_BINARY_ID_PRIMARY_REF              32
#define SH_CSS_BINARY_ID_PRIMARY_ISP261_STAGE0        33
#define SH_CSS_BINARY_ID_PRIMARY_ISP261_STAGE1        34
#define SH_CSS_BINARY_ID_PRIMARY_ISP261_STAGE2        35
#define SH_CSS_BINARY_ID_PRIMARY_ISP261_STAGE3        36
#define SH_CSS_BINARY_ID_PRIMARY_ISP261_STAGE4        37
#define SH_CSS_BINARY_ID_PRIMARY_ISP261_STAGE5        38
#define SH_CSS_BINARY_ID_VIDEO_OFFLINE            39
#define SH_CSS_BINARY_ID_VIDEO_DS                 40
#define SH_CSS_BINARY_ID_VIDEO_YUV_DS             41
#define SH_CSS_BINARY_ID_VIDEO_DZ                 42
#define SH_CSS_BINARY_ID_VIDEO_DZ_2400_ONLY       43
#define SH_CSS_BINARY_ID_VIDEO_HIGH               44
#define SH_CSS_BINARY_ID_VIDEO_NODZ               45
#define SH_CSS_BINARY_ID_VIDEO_CONT_MULTIBDS_ISP2_MIN 46
#define SH_CSS_BINARY_ID_VIDEO_CONT_BDS_300_600_ISP2_MIN 47
#define SH_CSS_BINARY_ID_VIDEO_CONT_DPC_BDS150_ISP2_MIN 48
#define SH_CSS_BINARY_ID_VIDEO_CONT_BDS150_ISP2_MIN   49
#define SH_CSS_BINARY_ID_VIDEO_CONT_DPC_BDS200_ISP2_MIN   50
#define SH_CSS_BINARY_ID_VIDEO_CONT_BDS200_ISP2_MIN   51
#define SH_CSS_BINARY_ID_VIDEO_CONT_NOBDS_ISP2_MIN    52
#define SH_CSS_BINARY_ID_VIDEO_DZ_ISP2_MIN      53
#define SH_CSS_BINARY_ID_VIDEO_DZ_ISP2          54
#define SH_CSS_BINARY_ID_VIDEO_LP_ISP2          55
#define SH_CSS_BINARY_ID_RESERVED1              56
#define SH_CSS_BINARY_ID_ACCELERATION           57
#define SH_CSS_BINARY_ID_PRE_DE_ISP2            58
#define SH_CSS_BINARY_ID_KERNEL_TEST_LOAD_STORE 59
#define SH_CSS_BINARY_ID_CAPTURE_PP_BLI         60
#define SH_CSS_BINARY_ID_CAPTURE_PP_LDC         61
#ifdef ISP2401
#define SH_CSS_BINARY_ID_PRIMARY_STRIPED_ISP2_XNR      62
#endif

/* skycam kerneltest pipelines */
#ifndef ISP2401
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_NORM              120
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_NORM_STRIPED      121
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_LIN               122
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_LIN_STRIPED       123
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_ACC_SHD           124
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_ACC_SHD_STRIPED   125
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_ACC_AWB           126
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_3A                127
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_3A_STRIPED        128
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_ACC_AF            129
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OBGRID            130
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_BAYER_DENOISE       131
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_BAYER_DENOISE_STRIPED 132
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_DEMOSAIC            133
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_YUVP1_C0            134
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_YUVP2               135
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_REF               136
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_REF_STRIPED       137
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_XNR_REF           138
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_DVS               139
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_XNR               140
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_XNR_STRIPED       141
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_XNR_BLENDING      142
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_TNR_BLOCK         143
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_ACC_AE            144
#define SH_CSS_BINARY_ID_VIDEO_RAW                          145
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_ACC_AWB_FR        146
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_DM_RGBPP          147
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_DM_RGBPP_STRIPED  148
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_ANR                 149
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_IF                150
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_IF_STRIPED        151
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OUTPUT_SYSTEM     152
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_TNR_STRIPED       153
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_DVS_STRIPED       154
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OBGRID_STRIPED    155
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_COPY_YUV          156
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_COPY_YUV_BLOCK    157
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_COPY_YUV16_BLOCK  158
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_COPY_YUV16_STRIPED 159
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_COPY_BLOCK_STRIPED 160
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_INPUT_YUV         161
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OUTPUT_YUV        162
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OUTPUT_YUV_16     163
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OUTPUT_SPLIT      164
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OUTPUT_SYSTEM_STRIPED 165

#else
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_NORM              121
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_NORM_STRIPED      122
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OBGRID            123
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OBGRID_STRIPED    124
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_LIN               125
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_LIN_STRIPED       126
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_ACC_SHD           127
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_ACC_SHD_STRIPED   128
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_ACC_AE            129
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_ACC_AWB           130
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_ACC_AF            131
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_ACC_AWB_FR        132
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_3A                133
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_3A_STRIPED        134
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_BAYER_DENOISE       135
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_BAYER_DENOISE_STRIPED 136
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_ANR                 137
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_ANR_STRIPED         138
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_DEMOSAIC            139
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_DM_RGBPP          140
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_DM_RGBPP_STRIPED  141
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_YUVP1_C0            142
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_YUVP2               143
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_YUVP2_STRIPED       144
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_XNR_REF           145
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_XNR               146
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_XNR_STRIPED       147
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_XNR_BLENDING      148
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_REF               149
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_REF_STRIPED       150
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_DVS               151
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_DVS_STRIPED       152
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_DVS_STAT_C0         153
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_TNR_BLOCK         154
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_TNR_STRIPED       155
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OUTPUT_SYSTEM     156
#define SH_CSS_BINARY_ID_VIDEO_RAW                          157
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_COPY_YUV          158
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_COPY_YUV_BLOCK    159
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_COPY_YUV16_BLOCK  160
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_COPY_YUV16_STRIPED 161
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_COPY_BLOCK_STRIPED 162
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_INPUT_YUV         163
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OUTPUT_YUV        164
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OUTPUT_YUV_16     165
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OUTPUT_SPLIT      166
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_OUTPUT_SYSTEM_STRIPED 167
#define SH_CSS_BINARY_ID_COPY_KERNELTEST_OUTPUT_SYSTEM      168
#endif

/* skycam partial test pipelines*/
#ifndef ISP2401
#define SH_CSS_BINARY_ID_IF_TO_DPC                          201
#define SH_CSS_BINARY_ID_IF_TO_BDS                          202
#else
#define SH_CSS_BINARY_ID_IF_TO_BDS                          201
#define SH_CSS_BINARY_ID_IF_TO_BDS_STRIPED                  202
#endif
#define SH_CSS_BINARY_ID_IF_TO_NORM                         203
#ifndef ISP2401
#define SH_CSS_BINARY_ID_IF_TO_OB                           204
#define SH_CSS_BINARY_ID_IF_TO_LIN                          205
#define SH_CSS_BINARY_ID_IF_TO_SHD                          206
#define SH_CSS_BINARY_ID_IF_TO_BNR                          207
#define SH_CSS_BINARY_ID_IF_TO_RGBPP_NV12_16                208
#define SH_CSS_BINARY_ID_IF_TO_RGBPP                        210
#define SH_CSS_BINARY_ID_IF_TO_YUVP1                        211
#define SH_CSS_BINARY_ID_IF_TO_DM                           214
#define SH_CSS_BINARY_ID_IF_TO_YUVP2_C0                     216
#define SH_CSS_BINARY_ID_IF_TO_YUVP2_ANR_VIA_ISP            217
#define SH_CSS_BINARY_ID_VIDEO_IF_TO_DVS                    218
#define SH_CSS_BINARY_ID_VIDEO_IF_TO_TNR                    219
#define SH_CSS_BINARY_ID_IF_TO_BDS_STRIPED                  224
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_ANR_STRIPED         225
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_YUVP2_STRIPED       227
#define SH_CSS_BINARY_ID_IF_TO_BDS_RGBP_DVS_STAT_C0         228
#define SH_CSS_BINARY_ID_IF_TO_BDS_RGBP_DVS_STAT_C0_STRIPED 229
#define SH_CSS_BINARY_ID_IF_TO_REF                          236
#define SH_CSS_BINARY_ID_IF_TO_DVS_STRIPED                  237
#define SH_CSS_BINARY_ID_IF_TO_YUVP2_STRIPED                238
#define SH_CSS_BINARY_ID_IF_TO_YUVP1_STRIPED                239
#define SH_CSS_BINARY_ID_IF_TO_RGBPP_STRIPED                240
#define SH_CSS_BINARY_ID_IF_TO_ANR_STRIPED                  241
#define SH_CSS_BINARY_ID_IF_TO_BNR_STRIPED                  242
#define SH_CSS_BINARY_ID_IF_TO_SHD_STRIPED                  243
#define SH_CSS_BINARY_ID_IF_TO_LIN_STRIPED                  244
#define SH_CSS_BINARY_ID_IF_TO_OB_STRIPED                   245
#define SH_CSS_BINARY_ID_IF_TO_NORM_STRIPED                 248
#define SH_CSS_BINARY_ID_COPY_KERNELTEST_OUTPUT_SYSTEM      253
#define SH_CSS_BINARY_ID_IF_TO_XNR                          256
#define SH_CSS_BINARY_ID_IF_TO_XNR_STRIPED                  257
#define SH_CSS_BINARY_ID_IF_TO_REF_STRIPED                  258
#define SH_CSS_BINARY_ID_VIDEO_IF_TO_OSYS                   259
#define SH_CSS_BINARY_ID_IF_TO_YUVP1_C0                     262
#define SH_CSS_BINARY_ID_IF_TO_XNR_PRIMARY                  263
#define SH_CSS_BINARY_ID_IF_TO_XNR_PRIMARY_STRIPED          264
#define SH_CSS_BINARY_ID_IF_TO_ANR                          265
#define SH_CSS_BINARY_ID_VIDEO_TEST_ACC_DVS_STAT_C0         266
#define SH_CSS_BINARY_ID_VIDEO_IF_TO_OSYS_STRIPED           270
#define SH_CSS_BINARY_ID_IF_TO_OSYS_PRIMARY                 276
#define SH_CSS_BINARY_ID_IF_TO_OSYS_PRIMARY_STRIPED         277
#define SH_CSS_BINARY_ID_IF_TO_YUVP1_C0_STRIPED             278
#else
#define SH_CSS_BINARY_ID_IF_TO_NORM_STRIPED                 204
#define SH_CSS_BINARY_ID_IF_TO_OB                           205
#define SH_CSS_BINARY_ID_IF_TO_OB_STRIPED                   206
#define SH_CSS_BINARY_ID_IF_TO_LIN                          207
#define SH_CSS_BINARY_ID_IF_TO_LIN_STRIPED                  208
#define SH_CSS_BINARY_ID_IF_TO_SHD                          209
#define SH_CSS_BINARY_ID_IF_TO_SHD_STRIPED                  210
#define SH_CSS_BINARY_ID_IF_TO_BNR                          211
#define SH_CSS_BINARY_ID_IF_TO_BNR_STRIPED                  212
#define SH_CSS_BINARY_ID_IF_TO_ANR                          213
#define SH_CSS_BINARY_ID_IF_TO_ANR_STRIPED                  214
#define SH_CSS_BINARY_ID_IF_TO_DM                           215
#define SH_CSS_BINARY_ID_IF_TO_BDS_RGBP_DVS_STAT_C0         216
#define SH_CSS_BINARY_ID_IF_TO_BDS_RGBP_DVS_STAT_C0_STRIPED 217
#define SH_CSS_BINARY_ID_IF_TO_RGBPP                        218
#define SH_CSS_BINARY_ID_IF_TO_RGBPP_NV12_16                219
#define SH_CSS_BINARY_ID_IF_TO_RGBPP_STRIPED                220
#define SH_CSS_BINARY_ID_IF_TO_YUVP1                        221
#define SH_CSS_BINARY_ID_IF_TO_YUVP1_STRIPED                222
#define SH_CSS_BINARY_ID_IF_TO_YUVP1_C0                     223
#define SH_CSS_BINARY_ID_IF_TO_YUVP2_C0                     224
#define SH_CSS_BINARY_ID_IF_TO_YUVP2_STRIPED                225
#define SH_CSS_BINARY_ID_IF_TO_XNR                          226
#define SH_CSS_BINARY_ID_IF_TO_XNR_STRIPED                  227
#define SH_CSS_BINARY_ID_IF_TO_XNR_PRIMARY                  228
#define SH_CSS_BINARY_ID_IF_TO_XNR_PRIMARY_STRIPED          229
#define SH_CSS_BINARY_ID_IF_TO_REF                          230
#define SH_CSS_BINARY_ID_IF_TO_REF_STRIPED                  231
#define SH_CSS_BINARY_ID_VIDEO_IF_TO_DVS                    232
#define SH_CSS_BINARY_ID_IF_TO_DVS_STRIPED                  233
#define SH_CSS_BINARY_ID_VIDEO_IF_TO_TNR                    234
#define SH_CSS_BINARY_ID_VIDEO_IF_TO_OSYS                   235
#define SH_CSS_BINARY_ID_VIDEO_IF_TO_OSYS_STRIPED           236
#define SH_CSS_BINARY_ID_IF_TO_OSYS_PRIMARY                 237
#define SH_CSS_BINARY_ID_IF_TO_OSYS_PRIMARY_STRIPED         238
#define SH_CSS_BINARY_ID_IF_TO_YUVP1_C0_STRIPED             239
#define SH_CSS_BINARY_ID_VIDEO_YUVP1_TO_OSYS                240
#define SH_CSS_BINARY_ID_IF_TO_OSYS_PREVIEW                 241
#define SH_CSS_BINARY_ID_IF_TO_OSYS_PREVIEW_STRIPED         242
#endif

/* Skycam IR camera binaries */
#ifndef ISP2401
#define SH_CSS_BINARY_ID_IR_IF_TO_OSYS_NO_XNR               300
#define SH_CSS_BINARY_ID_VIDEO_IR_IF_TO_OSYS_NO_DVS_NO_TNR_NO_XNR    301
#define SH_CSS_BINARY_ID_IR_IF_TO_OSYS_NO_XNR_NO_DVS_PRIMARY         302
#else
#define SH_CSS_BINARY_ID_IR_IF_TO_OSYS                      300
#define SH_CSS_BINARY_ID_IR_IF_TO_OSYS_NO_TNR3              301
#define SH_CSS_BINARY_ID_IR_IF_TO_OSYS_PRIMARY              302

/* Binaries under development */
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_TNR3              401
#define SH_CSS_BINARY_ID_VIDEO_KERNELTEST_TNR3_STRIPED      402

#endif

#define XMEM_WIDTH_BITS              HIVE_ISP_DDR_WORD_BITS
#define XMEM_SHORTS_PER_WORD         (HIVE_ISP_DDR_WORD_BITS/16)
#define XMEM_INTS_PER_WORD           (HIVE_ISP_DDR_WORD_BITS/32)
#define XMEM_POW2_BYTES_PER_WORD      HIVE_ISP_DDR_WORD_BYTES

#define BITS8_ELEMENTS_PER_XMEM_ADDR    CEIL_DIV(XMEM_WIDTH_BITS, 8)
#define BITS16_ELEMENTS_PER_XMEM_ADDR    CEIL_DIV(XMEM_WIDTH_BITS, 16)

#if ISP_VEC_NELEMS == 64
#define ISP_NWAY_LOG2  6
#elif ISP_VEC_NELEMS == 32
#define ISP_NWAY_LOG2  5
#elif ISP_VEC_NELEMS == 16
#define ISP_NWAY_LOG2  4
#elif ISP_VEC_NELEMS == 8
#define ISP_NWAY_LOG2  3
#else
#error "isp_const.h ISP_VEC_NELEMS must be one of {8, 16, 32, 64}"
#endif

/* *****************************
 * ISP input/output buffer sizes
 * ****************************/
/* input image */
#define INPUT_BUF_DMA_HEIGHT          2
#define INPUT_BUF_HEIGHT              2 /* double buffer */
#define OUTPUT_BUF_DMA_HEIGHT         2
#define OUTPUT_BUF_HEIGHT             2 /* double buffer */
#define OUTPUT_NUM_TRANSFERS	      4

/* GDC accelerator: Up/Down Scaling */
/* These should be moved to the gdc_defs.h in the device */
#define UDS_SCALING_N                 HRT_GDC_N
/* AB: This should cover the zooming up to 16MP */
#define UDS_MAX_OXDIM                 5000
/* We support maximally 2 planes with different parameters
       - luma and chroma (YUV420) */
#define UDS_MAX_PLANES                2
#define UDS_BLI_BLOCK_HEIGHT          2
#define UDS_BCI_BLOCK_HEIGHT          4
#define UDS_BLI_INTERP_ENVELOPE       1
#define UDS_BCI_INTERP_ENVELOPE       3
#define UDS_MAX_ZOOM_FAC              64
/* Make it always one FPGA vector.
   Four FPGA vectors are required and
   four of them fit in one ASIC vector.*/
#define UDS_MAX_CHUNKS                16

#define ISP_LEFT_PADDING	_ISP_LEFT_CROP_EXTRA(ISP_LEFT_CROPPING)
#define ISP_LEFT_PADDING_VECS	CEIL_DIV(ISP_LEFT_PADDING, ISP_VEC_NELEMS)
/* in case of continuous the croppong of the current binary doesn't matter for the buffer calculation, but the cropping of the sp copy should be used */
#define ISP_LEFT_PADDING_CONT	_ISP_LEFT_CROP_EXTRA(SH_CSS_MAX_LEFT_CROPPING)
#define ISP_LEFT_PADDING_VECS_CONT	CEIL_DIV(ISP_LEFT_PADDING_CONT, ISP_VEC_NELEMS)

#define CEIL_ROUND_DIV_STRIPE(width, stripe, padding) \
	CEIL_MUL(padding + CEIL_DIV(width - padding, stripe), ((ENABLE_RAW_BINNING || ENABLE_FIXED_BAYER_DS)?4:2))

/* output (Y,U,V) image, 4:2:0 */
#define MAX_VECTORS_PER_LINE \
	CEIL_ROUND_DIV_STRIPE(CEIL_DIV(ISP_MAX_INTERNAL_WIDTH, ISP_VEC_NELEMS), \
			      ISP_NUM_STRIPES, \
			      ISP_LEFT_PADDING_VECS)

/*
 * ITERATOR_VECTOR_INCREMENT' explanation:
 * when striping an even number of iterations, one of the stripes is
 * one iteration wider than the other to account for overlap
 * so the calc for the output buffer vmem size is:
 * ((width[vectors]/num_of_stripes) + 2[vectors])
 */
#if defined(HAS_RES_MGR)
#define MAX_VECTORS_PER_OUTPUT_LINE \
	(CEIL_DIV(CEIL_DIV(ISP_MAX_OUTPUT_WIDTH, ISP_NUM_STRIPES) + ISP_LEFT_PADDING, ISP_VEC_NELEMS) + \
	ITERATOR_VECTOR_INCREMENT)

#define MAX_VECTORS_PER_INPUT_LINE	CEIL_DIV(ISP_MAX_INPUT_WIDTH, ISP_VEC_NELEMS)
#define MAX_VECTORS_PER_INPUT_STRIPE	(CEIL_ROUND_DIV_STRIPE(CEIL_DIV(ISP_MAX_INPUT_WIDTH, ISP_VEC_NELEMS) , \
							      ISP_NUM_STRIPES, \
							      ISP_LEFT_PADDING_VECS) + \
							      ITERATOR_VECTOR_INCREMENT)
#else /* !defined(HAS_RES_MGR)*/
#define MAX_VECTORS_PER_OUTPUT_LINE \
	CEIL_DIV(CEIL_DIV(ISP_MAX_OUTPUT_WIDTH, ISP_NUM_STRIPES) + ISP_LEFT_PADDING, ISP_VEC_NELEMS)

/* Must be even due to interlaced bayer input */
#define MAX_VECTORS_PER_INPUT_LINE	CEIL_MUL((CEIL_DIV(ISP_MAX_INPUT_WIDTH, ISP_VEC_NELEMS) + ISP_LEFT_PADDING_VECS), 2)
#define MAX_VECTORS_PER_INPUT_STRIPE	CEIL_ROUND_DIV_STRIPE(MAX_VECTORS_PER_INPUT_LINE, \
							      ISP_NUM_STRIPES, \
							      ISP_LEFT_PADDING_VECS)
#endif /* HAS_RES_MGR */


/* Add 2 for left croppping */
#define MAX_SP_RAW_COPY_VECTORS_PER_INPUT_LINE	(CEIL_DIV(ISP_MAX_INPUT_WIDTH, ISP_VEC_NELEMS) + 2)

#define MAX_VECTORS_PER_BUF_LINE \
	(MAX_VECTORS_PER_LINE + DUMMY_BUF_VECTORS)
#define MAX_VECTORS_PER_BUF_INPUT_LINE \
	(MAX_VECTORS_PER_INPUT_STRIPE + DUMMY_BUF_VECTORS)
#define MAX_OUTPUT_Y_FRAME_WIDTH \
	(MAX_VECTORS_PER_LINE * ISP_VEC_NELEMS)
#define MAX_OUTPUT_Y_FRAME_SIMDWIDTH \
	MAX_VECTORS_PER_LINE
#define MAX_OUTPUT_C_FRAME_WIDTH \
	(MAX_OUTPUT_Y_FRAME_WIDTH / 2)
#define MAX_OUTPUT_C_FRAME_SIMDWIDTH \
	CEIL_DIV(MAX_OUTPUT_C_FRAME_WIDTH, ISP_VEC_NELEMS)

/* should be even */
#define NO_CHUNKING (OUTPUT_NUM_CHUNKS == 1)

#define MAX_VECTORS_PER_CHUNK \
	(NO_CHUNKING ? MAX_VECTORS_PER_LINE \
				: 2*CEIL_DIV(MAX_VECTORS_PER_LINE, \
					     2*OUTPUT_NUM_CHUNKS))

#define MAX_C_VECTORS_PER_CHUNK \
	(MAX_VECTORS_PER_CHUNK/2)

/* should be even */
#define MAX_VECTORS_PER_OUTPUT_CHUNK \
	(NO_CHUNKING ? MAX_VECTORS_PER_OUTPUT_LINE \
				: 2*CEIL_DIV(MAX_VECTORS_PER_OUTPUT_LINE, \
					     2*OUTPUT_NUM_CHUNKS))

#define MAX_C_VECTORS_PER_OUTPUT_CHUNK \
	(MAX_VECTORS_PER_OUTPUT_CHUNK/2)



/* should be even */
#define MAX_VECTORS_PER_INPUT_CHUNK \
	(INPUT_NUM_CHUNKS == 1 ? MAX_VECTORS_PER_INPUT_STRIPE \
			       : 2*CEIL_DIV(MAX_VECTORS_PER_INPUT_STRIPE, \
					    2*OUTPUT_NUM_CHUNKS))

#define DEFAULT_C_SUBSAMPLING      2

/****** DMA buffer properties */

#define RAW_BUF_LINES ((ENABLE_RAW_BINNING || ENABLE_FIXED_BAYER_DS) ? 4 : 2)

#if defined(HAS_RES_MGR)
#define RAW_BUF_STRIDE (MAX_VECTORS_PER_INPUT_STRIPE)
#else /* !defined(HAS_RES_MGR) */
#define RAW_BUF_STRIDE \
	(BINARY_ID == SH_CSS_BINARY_ID_POST_ISP ? MAX_VECTORS_PER_INPUT_CHUNK : \
	 ISP_NUM_STRIPES > 1 ? MAX_VECTORS_PER_INPUT_STRIPE+_ISP_EXTRA_PADDING_VECS : \
	 !ENABLE_CONTINUOUS ? MAX_VECTORS_PER_INPUT_LINE : \
	 MAX_VECTORS_PER_INPUT_CHUNK)
#endif /* HAS_RES_MGR */

/* [isp vmem] table size[vectors] per line per color (GR,R,B,GB),
   multiples of NWAY */
#define SCTBL_VECTORS_PER_LINE_PER_COLOR \
	CEIL_DIV(SH_CSS_MAX_SCTBL_WIDTH_PER_COLOR, ISP_VEC_NELEMS)
/* [isp vmem] table size[vectors] per line for 4colors (GR,R,B,GB),
   multiples of NWAY */
#define SCTBL_VECTORS_PER_LINE \
	(SCTBL_VECTORS_PER_LINE_PER_COLOR * IA_CSS_SC_NUM_COLORS)

/*************/

/* Format for fixed primaries */

#define ISP_FIXED_PRIMARY_FORMAT IA_CSS_FRAME_FORMAT_NV12

#endif /* _COMMON_ISP_CONST_H_ */
