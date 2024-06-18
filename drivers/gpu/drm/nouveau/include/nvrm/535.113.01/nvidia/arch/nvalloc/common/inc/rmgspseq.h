#ifndef __src_nvidia_arch_nvalloc_common_inc_rmgspseq_h__
#define __src_nvidia_arch_nvalloc_common_inc_rmgspseq_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2020 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

typedef enum GSP_SEQ_BUF_OPCODE
{
    GSP_SEQ_BUF_OPCODE_REG_WRITE = 0,
    GSP_SEQ_BUF_OPCODE_REG_MODIFY,
    GSP_SEQ_BUF_OPCODE_REG_POLL,
    GSP_SEQ_BUF_OPCODE_DELAY_US,
    GSP_SEQ_BUF_OPCODE_REG_STORE,
    GSP_SEQ_BUF_OPCODE_CORE_RESET,
    GSP_SEQ_BUF_OPCODE_CORE_START,
    GSP_SEQ_BUF_OPCODE_CORE_WAIT_FOR_HALT,
    GSP_SEQ_BUF_OPCODE_CORE_RESUME,
} GSP_SEQ_BUF_OPCODE;

#define GSP_SEQUENCER_PAYLOAD_SIZE_DWORDS(opcode)                       \
    ((opcode == GSP_SEQ_BUF_OPCODE_REG_WRITE)  ? (sizeof(GSP_SEQ_BUF_PAYLOAD_REG_WRITE)  / sizeof(NvU32)) : \
     (opcode == GSP_SEQ_BUF_OPCODE_REG_MODIFY) ? (sizeof(GSP_SEQ_BUF_PAYLOAD_REG_MODIFY) / sizeof(NvU32)) : \
     (opcode == GSP_SEQ_BUF_OPCODE_REG_POLL)   ? (sizeof(GSP_SEQ_BUF_PAYLOAD_REG_POLL)   / sizeof(NvU32)) : \
     (opcode == GSP_SEQ_BUF_OPCODE_DELAY_US)   ? (sizeof(GSP_SEQ_BUF_PAYLOAD_DELAY_US)   / sizeof(NvU32)) : \
     (opcode == GSP_SEQ_BUF_OPCODE_REG_STORE)  ? (sizeof(GSP_SEQ_BUF_PAYLOAD_REG_STORE)  / sizeof(NvU32)) : \
    /* GSP_SEQ_BUF_OPCODE_CORE_RESET */                                 \
    /* GSP_SEQ_BUF_OPCODE_CORE_START */                                 \
    /* GSP_SEQ_BUF_OPCODE_CORE_WAIT_FOR_HALT */                         \
    /* GSP_SEQ_BUF_OPCODE_CORE_RESUME */                                \
    0)

typedef struct
{
    NvU32 addr;
    NvU32 val;
} GSP_SEQ_BUF_PAYLOAD_REG_WRITE;

typedef struct
{
    NvU32 addr;
    NvU32 mask;
    NvU32 val;
} GSP_SEQ_BUF_PAYLOAD_REG_MODIFY;

typedef struct
{
    NvU32 addr;
    NvU32 mask;
    NvU32 val;
    NvU32 timeout;
    NvU32 error;
} GSP_SEQ_BUF_PAYLOAD_REG_POLL;

typedef struct
{
    NvU32 val;
} GSP_SEQ_BUF_PAYLOAD_DELAY_US;

typedef struct
{
    NvU32 addr;
    NvU32 index;
} GSP_SEQ_BUF_PAYLOAD_REG_STORE;

typedef struct GSP_SEQUENCER_BUFFER_CMD
{
    GSP_SEQ_BUF_OPCODE opCode;
    union
    {
        GSP_SEQ_BUF_PAYLOAD_REG_WRITE regWrite;
        GSP_SEQ_BUF_PAYLOAD_REG_MODIFY regModify;
        GSP_SEQ_BUF_PAYLOAD_REG_POLL regPoll;
        GSP_SEQ_BUF_PAYLOAD_DELAY_US delayUs;
        GSP_SEQ_BUF_PAYLOAD_REG_STORE regStore;
    } payload;
} GSP_SEQUENCER_BUFFER_CMD;

#endif
