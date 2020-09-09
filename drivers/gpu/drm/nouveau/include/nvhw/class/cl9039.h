/*
 * Copyright (c) 2003-2004, NVIDIA CORPORATION. All rights reserved.
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

#ifndef _cl_fermi_memory_to_memory_format_a_h_
#define _cl_fermi_memory_to_memory_format_a_h_

#define NV9039_SET_OBJECT                                                                                  0x0000
#define NV9039_SET_OBJECT_CLASS_ID                                                                           15:0
#define NV9039_SET_OBJECT_ENGINE_ID                                                                         20:16

#define NV9039_OFFSET_OUT_UPPER                                                                            0x0238
#define NV9039_OFFSET_OUT_UPPER_VALUE                                                                         7:0

#define NV9039_OFFSET_OUT                                                                                  0x023c
#define NV9039_OFFSET_OUT_VALUE                                                                              31:0

#define NV9039_LAUNCH_DMA                                                                                  0x0300
#define NV9039_LAUNCH_DMA_SRC_INLINE                                                                          0:0
#define NV9039_LAUNCH_DMA_SRC_INLINE_FALSE                                                             0x00000000
#define NV9039_LAUNCH_DMA_SRC_INLINE_TRUE                                                              0x00000001
#define NV9039_LAUNCH_DMA_SRC_MEMORY_LAYOUT                                                                   4:4
#define NV9039_LAUNCH_DMA_SRC_MEMORY_LAYOUT_BLOCKLINEAR                                                0x00000000
#define NV9039_LAUNCH_DMA_SRC_MEMORY_LAYOUT_PITCH                                                      0x00000001
#define NV9039_LAUNCH_DMA_DST_MEMORY_LAYOUT                                                                   8:8
#define NV9039_LAUNCH_DMA_DST_MEMORY_LAYOUT_BLOCKLINEAR                                                0x00000000
#define NV9039_LAUNCH_DMA_DST_MEMORY_LAYOUT_PITCH                                                      0x00000001
#define NV9039_LAUNCH_DMA_COMPLETION_TYPE                                                                   13:12
#define NV9039_LAUNCH_DMA_COMPLETION_TYPE_FLUSH_DISABLE                                                0x00000000
#define NV9039_LAUNCH_DMA_COMPLETION_TYPE_FLUSH_ONLY                                                   0x00000001
#define NV9039_LAUNCH_DMA_COMPLETION_TYPE_RELEASE_SEMAPHORE                                            0x00000002
#define NV9039_LAUNCH_DMA_INTERRUPT_TYPE                                                                    17:16
#define NV9039_LAUNCH_DMA_INTERRUPT_TYPE_NONE                                                          0x00000000
#define NV9039_LAUNCH_DMA_INTERRUPT_TYPE_INTERRUPT                                                     0x00000001
#define NV9039_LAUNCH_DMA_SEMAPHORE_STRUCT_SIZE                                                             20:20
#define NV9039_LAUNCH_DMA_SEMAPHORE_STRUCT_SIZE_FOUR_WORDS                                             0x00000000
#define NV9039_LAUNCH_DMA_SEMAPHORE_STRUCT_SIZE_ONE_WORD                                               0x00000001

#define NV9039_OFFSET_IN_UPPER                                                                             0x030c
#define NV9039_OFFSET_IN_UPPER_VALUE                                                                          7:0

#define NV9039_OFFSET_IN                                                                                   0x0310
#define NV9039_OFFSET_IN_VALUE                                                                               31:0

#define NV9039_PITCH_IN                                                                                    0x0314
#define NV9039_PITCH_IN_VALUE                                                                                31:0

#define NV9039_PITCH_OUT                                                                                   0x0318
#define NV9039_PITCH_OUT_VALUE                                                                               31:0

#define NV9039_LINE_LENGTH_IN                                                                              0x031c
#define NV9039_LINE_LENGTH_IN_VALUE                                                                          31:0

#define NV9039_LINE_COUNT                                                                                  0x0320
#define NV9039_LINE_COUNT_VALUE                                                                              31:0
#endif /* _cl_fermi_memory_to_memory_format_a_h_ */
