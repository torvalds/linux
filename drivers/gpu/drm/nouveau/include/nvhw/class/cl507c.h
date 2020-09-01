/*
 * Copyright (c) 1993-2014, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */


#ifndef _cl507c_h_
#define _cl507c_h_

#define NV_DISP_BASE_NOTIFIER_1                                                      0x00000000
#define NV_DISP_BASE_NOTIFIER_1_SIZEOF                                               0x00000004
#define NV_DISP_BASE_NOTIFIER_1__0                                                   0x00000000
#define NV_DISP_BASE_NOTIFIER_1__0_PRESENTATION_COUNT                                15:0
#define NV_DISP_BASE_NOTIFIER_1__0_TIMESTAMP                                         29:16
#define NV_DISP_BASE_NOTIFIER_1__0_STATUS                                            31:30
#define NV_DISP_BASE_NOTIFIER_1__0_STATUS_NOT_BEGUN                                  0x00000000
#define NV_DISP_BASE_NOTIFIER_1__0_STATUS_BEGUN                                      0x00000001
#define NV_DISP_BASE_NOTIFIER_1__0_STATUS_FINISHED                                   0x00000002


// dma opcode instructions
#define NV507C_DMA                                     0x00000000
#define NV507C_DMA_OPCODE                                   31:29
#define NV507C_DMA_OPCODE_METHOD                       0x00000000
#define NV507C_DMA_OPCODE_JUMP                         0x00000001
#define NV507C_DMA_OPCODE_NONINC_METHOD                0x00000002
#define NV507C_DMA_OPCODE_SET_SUBDEVICE_MASK           0x00000003
#define NV507C_DMA_OPCODE                                   31:29
#define NV507C_DMA_OPCODE_METHOD                       0x00000000
#define NV507C_DMA_OPCODE_NONINC_METHOD                0x00000002
#define NV507C_DMA_METHOD_COUNT                             27:18
#define NV507C_DMA_METHOD_OFFSET                             11:2
#define NV507C_DMA_DATA                                      31:0
#define NV507C_DMA_NOP                                 0x00000000
#define NV507C_DMA_OPCODE                                   31:29
#define NV507C_DMA_OPCODE_JUMP                         0x00000001
#define NV507C_DMA_JUMP_OFFSET                               11:2
#define NV507C_DMA_OPCODE                                   31:29
#define NV507C_DMA_OPCODE_SET_SUBDEVICE_MASK           0x00000003
#define NV507C_DMA_SET_SUBDEVICE_MASK_VALUE                  11:0

// class methods
#define NV507C_PUT                                                              (0x00000000)
#define NV507C_PUT_PTR                                                          11:2
#define NV507C_GET                                                              (0x00000004)
#define NV507C_GET_PTR                                                          11:2
#define NV507C_UPDATE                                                           (0x00000080)
#define NV507C_UPDATE_INTERLOCK_WITH_CORE                                       0:0
#define NV507C_UPDATE_INTERLOCK_WITH_CORE_DISABLE                               (0x00000000)
#define NV507C_UPDATE_INTERLOCK_WITH_CORE_ENABLE                                (0x00000001)
#define NV507C_SET_PRESENT_CONTROL                                              (0x00000084)
#define NV507C_SET_PRESENT_CONTROL_BEGIN_MODE                                   9:8
#define NV507C_SET_PRESENT_CONTROL_BEGIN_MODE_NON_TEARING                       (0x00000000)
#define NV507C_SET_PRESENT_CONTROL_BEGIN_MODE_IMMEDIATE                         (0x00000001)
#define NV507C_SET_PRESENT_CONTROL_BEGIN_MODE_ON_LINE                           (0x00000002)
#define NV507C_SET_PRESENT_CONTROL_MIN_PRESENT_INTERVAL                         7:4
#define NV507C_SET_PRESENT_CONTROL_BEGIN_LINE                                   30:16
#define NV507C_SET_PRESENT_CONTROL_ON_LINE_MARGIN                               15:10
#define NV507C_SET_SEMAPHORE_CONTROL                                            (0x00000088)
#define NV507C_SET_SEMAPHORE_CONTROL_OFFSET                                     11:2
#define NV507C_SET_SEMAPHORE_ACQUIRE                                            (0x0000008C)
#define NV507C_SET_SEMAPHORE_ACQUIRE_VALUE                                      31:0
#define NV507C_SET_SEMAPHORE_RELEASE                                            (0x00000090)
#define NV507C_SET_SEMAPHORE_RELEASE_VALUE                                      31:0
#define NV507C_SET_CONTEXT_DMA_SEMAPHORE                                        (0x00000094)
#define NV507C_SET_CONTEXT_DMA_SEMAPHORE_HANDLE                                 31:0
#define NV507C_SET_NOTIFIER_CONTROL                                             (0x000000A0)
#define NV507C_SET_NOTIFIER_CONTROL_MODE                                        30:30
#define NV507C_SET_NOTIFIER_CONTROL_MODE_WRITE                                  (0x00000000)
#define NV507C_SET_NOTIFIER_CONTROL_MODE_WRITE_AWAKEN                           (0x00000001)
#define NV507C_SET_NOTIFIER_CONTROL_OFFSET                                      11:2
#define NV507C_SET_CONTEXT_DMA_NOTIFIER                                         (0x000000A4)
#define NV507C_SET_CONTEXT_DMA_NOTIFIER_HANDLE                                  31:0
#define NV507C_SET_CONTEXT_DMA_ISO                                              (0x000000C0)
#define NV507C_SET_CONTEXT_DMA_ISO_HANDLE                                       31:0
#define NV507C_SET_BASE_LUT_LO                                                  (0x000000E0)
#define NV507C_SET_BASE_LUT_LO_ENABLE                                           31:30
#define NV507C_SET_BASE_LUT_LO_ENABLE_DISABLE                                   (0x00000000)
#define NV507C_SET_BASE_LUT_LO_ENABLE_USE_CORE_LUT                              (0x00000001)
#define NV507C_SET_BASE_LUT_LO_ENABLE_ENABLE                                    (0x00000003)
#define NV507C_SET_BASE_LUT_LO_MODE                                             29:29
#define NV507C_SET_BASE_LUT_LO_MODE_LORES                                       (0x00000000)
#define NV507C_SET_BASE_LUT_LO_MODE_HIRES                                       (0x00000001)
#define NV507C_SET_BASE_LUT_LO_ORIGIN                                           7:2
#define NV507C_SET_PROCESSING                                                   (0x00000110)
#define NV507C_SET_PROCESSING_USE_GAIN_OFS                                      0:0
#define NV507C_SET_PROCESSING_USE_GAIN_OFS_DISABLE                              (0x00000000)
#define NV507C_SET_PROCESSING_USE_GAIN_OFS_ENABLE                               (0x00000001)
#define NV507C_SET_CONVERSION                                                   (0x00000114)
#define NV507C_SET_CONVERSION_GAIN                                              15:0
#define NV507C_SET_CONVERSION_OFS                                               31:16

#define NV507C_SURFACE_SET_OFFSET(a,b)                                          (0x00000800 + (a)*0x00000020 + (b)*0x00000004)
#define NV507C_SURFACE_SET_OFFSET_ORIGIN                                        31:0
#define NV507C_SURFACE_SET_SIZE(a)                                              (0x00000808 + (a)*0x00000020)
#define NV507C_SURFACE_SET_SIZE_WIDTH                                           14:0
#define NV507C_SURFACE_SET_SIZE_HEIGHT                                          30:16
#define NV507C_SURFACE_SET_STORAGE(a)                                           (0x0000080C + (a)*0x00000020)
#define NV507C_SURFACE_SET_STORAGE_BLOCK_HEIGHT                                 3:0
#define NV507C_SURFACE_SET_STORAGE_BLOCK_HEIGHT_ONE_GOB                         (0x00000000)
#define NV507C_SURFACE_SET_STORAGE_BLOCK_HEIGHT_TWO_GOBS                        (0x00000001)
#define NV507C_SURFACE_SET_STORAGE_BLOCK_HEIGHT_FOUR_GOBS                       (0x00000002)
#define NV507C_SURFACE_SET_STORAGE_BLOCK_HEIGHT_EIGHT_GOBS                      (0x00000003)
#define NV507C_SURFACE_SET_STORAGE_BLOCK_HEIGHT_SIXTEEN_GOBS                    (0x00000004)
#define NV507C_SURFACE_SET_STORAGE_BLOCK_HEIGHT_THIRTYTWO_GOBS                  (0x00000005)
#define NV507C_SURFACE_SET_STORAGE_PITCH                                        17:8
#define NV507C_SURFACE_SET_STORAGE_MEMORY_LAYOUT                                20:20
#define NV507C_SURFACE_SET_STORAGE_MEMORY_LAYOUT_BLOCKLINEAR                    (0x00000000)
#define NV507C_SURFACE_SET_STORAGE_MEMORY_LAYOUT_PITCH                          (0x00000001)
#define NV507C_SURFACE_SET_PARAMS(a)                                            (0x00000810 + (a)*0x00000020)
#define NV507C_SURFACE_SET_PARAMS_FORMAT                                        15:8
#define NV507C_SURFACE_SET_PARAMS_FORMAT_I8                                     (0x0000001E)
#define NV507C_SURFACE_SET_PARAMS_FORMAT_VOID16                                 (0x0000001F)
#define NV507C_SURFACE_SET_PARAMS_FORMAT_VOID32                                 (0x0000002E)
#define NV507C_SURFACE_SET_PARAMS_FORMAT_RF16_GF16_BF16_AF16                    (0x000000CA)
#define NV507C_SURFACE_SET_PARAMS_FORMAT_A8R8G8B8                               (0x000000CF)
#define NV507C_SURFACE_SET_PARAMS_FORMAT_A2B10G10R10                            (0x000000D1)
#define NV507C_SURFACE_SET_PARAMS_FORMAT_A8B8G8R8                               (0x000000D5)
#define NV507C_SURFACE_SET_PARAMS_FORMAT_R5G6B5                                 (0x000000E8)
#define NV507C_SURFACE_SET_PARAMS_FORMAT_A1R5G5B5                               (0x000000E9)
#define NV507C_SURFACE_SET_PARAMS_SUPER_SAMPLE                                  1:0
#define NV507C_SURFACE_SET_PARAMS_SUPER_SAMPLE_X1_AA                            (0x00000000)
#define NV507C_SURFACE_SET_PARAMS_SUPER_SAMPLE_X4_AA                            (0x00000002)
#define NV507C_SURFACE_SET_PARAMS_GAMMA                                         2:2
#define NV507C_SURFACE_SET_PARAMS_GAMMA_LINEAR                                  (0x00000000)
#define NV507C_SURFACE_SET_PARAMS_GAMMA_SRGB                                    (0x00000001)
#define NV507C_SURFACE_SET_PARAMS_LAYOUT                                        5:4
#define NV507C_SURFACE_SET_PARAMS_LAYOUT_FRM                                    (0x00000000)
#define NV507C_SURFACE_SET_PARAMS_LAYOUT_FLD1                                   (0x00000001)
#define NV507C_SURFACE_SET_PARAMS_LAYOUT_FLD2                                   (0x00000002)
#define NV507C_SURFACE_SET_PARAMS_KIND                                          22:16
#define NV507C_SURFACE_SET_PARAMS_KIND_KIND_PITCH                               (0x00000000)
#define NV507C_SURFACE_SET_PARAMS_KIND_KIND_GENERIC_8BX2                        (0x00000070)
#define NV507C_SURFACE_SET_PARAMS_KIND_KIND_GENERIC_8BX2_BANKSWIZ               (0x00000072)
#define NV507C_SURFACE_SET_PARAMS_KIND_KIND_GENERIC_16BX1                       (0x00000074)
#define NV507C_SURFACE_SET_PARAMS_KIND_KIND_GENERIC_16BX1_BANKSWIZ              (0x00000076)
#define NV507C_SURFACE_SET_PARAMS_KIND_KIND_C32_MS4                             (0x00000078)
#define NV507C_SURFACE_SET_PARAMS_KIND_KIND_C32_MS8                             (0x00000079)
#define NV507C_SURFACE_SET_PARAMS_KIND_KIND_C32_MS4_BANKSWIZ                    (0x0000007A)
#define NV507C_SURFACE_SET_PARAMS_KIND_KIND_C32_MS8_BANKSWIZ                    (0x0000007B)
#define NV507C_SURFACE_SET_PARAMS_KIND_KIND_C64_MS4                             (0x0000007C)
#define NV507C_SURFACE_SET_PARAMS_KIND_KIND_C64_MS8                             (0x0000007D)
#define NV507C_SURFACE_SET_PARAMS_KIND_KIND_C128_MS4                            (0x0000007E)
#define NV507C_SURFACE_SET_PARAMS_KIND_FROM_PTE                                 (0x0000007F)
#define NV507C_SURFACE_SET_PARAMS_PART_STRIDE                                   24:24
#define NV507C_SURFACE_SET_PARAMS_PART_STRIDE_PARTSTRIDE_256                    (0x00000000)
#define NV507C_SURFACE_SET_PARAMS_PART_STRIDE_PARTSTRIDE_1024                   (0x00000001)
#endif // _cl507c_h
