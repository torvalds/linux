/*******************************************************************************
    Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.

*******************************************************************************/
#ifndef _cl826f_h_
#define _cl826f_h_

#define NV826F_SEMAPHOREA                                          (0x00000010)
#define NV826F_SEMAPHOREA_OFFSET_UPPER                                     7:0
#define NV826F_SEMAPHOREB                                          (0x00000014)
#define NV826F_SEMAPHOREB_OFFSET_LOWER                                   31:00
#define NV826F_SEMAPHOREC                                          (0x00000018)
#define NV826F_SEMAPHOREC_PAYLOAD                                         31:0
#define NV826F_SEMAPHORED                                          (0x0000001C)
#define NV826F_SEMAPHORED_OPERATION                                        2:0
#define NV826F_SEMAPHORED_OPERATION_ACQUIRE                         0x00000001
#define NV826F_SEMAPHORED_OPERATION_RELEASE                         0x00000002
#define NV826F_SEMAPHORED_OPERATION_ACQ_GEQ                         0x00000004
#define NV826F_NON_STALLED_INTERRUPT                               (0x00000020)
#define NV826F_SET_CONTEXT_DMA_SEMAPHORE                           (0x00000060)
#endif /* _cl826f_h_ */
