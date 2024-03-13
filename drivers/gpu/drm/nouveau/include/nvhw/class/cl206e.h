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
#ifndef _cl206e_h_
#define _cl206e_h_

/* dma opcode2 format */
#define NV206E_DMA_OPCODE2                                         1:0
#define NV206E_DMA_OPCODE2_NONE                                    (0x00000000)
/* dma jump_long format */
#define NV206E_DMA_OPCODE2_JUMP_LONG                               (0x00000001)
#define NV206E_DMA_JUMP_LONG_OFFSET                                31:2
/* dma call format */
#define NV206E_DMA_OPCODE2_CALL                                    (0x00000002)
#define NV206E_DMA_CALL_OFFSET                                     31:2
#endif /* _cl206e_h_ */
