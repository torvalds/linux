/*
 * Copyright (c) 2001-2001, NVIDIA CORPORATION. All rights reserved.
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

#ifndef _cl0039_h_
#define _cl0039_h_

/* dma method offsets, fields, and values */
#define NV039_SET_OBJECT                                           (0x00000000)
#define NV039_NO_OPERATION                                         (0x00000100)
#define NV039_SET_CONTEXT_DMA_NOTIFIES                             (0x00000180)
#define NV039_SET_CONTEXT_DMA_BUFFER_IN                            (0x00000184)
#define NV039_SET_CONTEXT_DMA_BUFFER_OUT                           (0x00000188)

#define NV039_OFFSET_IN                                            (0x0000030C)
#define NV039_OFFSET_OUT                                           (0x00000310)
#define NV039_PITCH_IN                                             (0x00000314)
#define NV039_PITCH_OUT                                            (0x00000318)
#define NV039_LINE_LENGTH_IN                                       (0x0000031C)
#define NV039_LINE_COUNT                                           (0x00000320)
#define NV039_FORMAT                                               (0x00000324)
#define NV039_FORMAT_IN                                            7:0
#define NV039_FORMAT_OUT                                           31:8
#define NV039_BUFFER_NOTIFY                                        (0x00000328)
#define NV039_BUFFER_NOTIFY_WRITE_ONLY                             (0x00000000)
#define NV039_BUFFER_NOTIFY_WRITE_THEN_AWAKEN                      (0x00000001)
#endif /* _cl0039_h_ */
