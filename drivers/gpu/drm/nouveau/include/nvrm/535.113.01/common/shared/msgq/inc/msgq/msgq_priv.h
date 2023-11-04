#ifndef __src_common_shared_msgq_inc_msgq_msgq_priv_h__
#define __src_common_shared_msgq_inc_msgq_msgq_priv_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2018-2019 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

typedef struct
{
    NvU32 version;   // queue version
    NvU32 size;      // bytes, page aligned
    NvU32 msgSize;   // entry size, bytes, must be power-of-2, 16 is minimum
    NvU32 msgCount;  // number of entries in queue
    NvU32 writePtr;  // message id of next slot
    NvU32 flags;     // if set it means "i want to swap RX"
    NvU32 rxHdrOff;  // Offset of msgqRxHeader from start of backing store.
    NvU32 entryOff;  // Offset of entries from start of backing store.
} msgqTxHeader;

typedef struct
{
    NvU32 readPtr; // message id of last message read
} msgqRxHeader;

#endif
