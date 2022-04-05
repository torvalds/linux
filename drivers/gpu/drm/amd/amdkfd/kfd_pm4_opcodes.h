/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
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


#ifndef KFD_PM4_OPCODES_H
#define KFD_PM4_OPCODES_H

enum it_opcode_type {
	IT_NOP                               = 0x10,
	IT_SET_BASE                          = 0x11,
	IT_CLEAR_STATE                       = 0x12,
	IT_INDEX_BUFFER_SIZE                 = 0x13,
	IT_DISPATCH_DIRECT                   = 0x15,
	IT_DISPATCH_INDIRECT                 = 0x16,
	IT_ATOMIC_GDS                        = 0x1D,
	IT_OCCLUSION_QUERY                   = 0x1F,
	IT_SET_PREDICATION                   = 0x20,
	IT_REG_RMW                           = 0x21,
	IT_COND_EXEC                         = 0x22,
	IT_PRED_EXEC                         = 0x23,
	IT_DRAW_INDIRECT                     = 0x24,
	IT_DRAW_INDEX_INDIRECT               = 0x25,
	IT_INDEX_BASE                        = 0x26,
	IT_DRAW_INDEX_2                      = 0x27,
	IT_CONTEXT_CONTROL                   = 0x28,
	IT_INDEX_TYPE                        = 0x2A,
	IT_DRAW_INDIRECT_MULTI               = 0x2C,
	IT_DRAW_INDEX_AUTO                   = 0x2D,
	IT_NUM_INSTANCES                     = 0x2F,
	IT_DRAW_INDEX_MULTI_AUTO             = 0x30,
	IT_INDIRECT_BUFFER_CNST              = 0x33,
	IT_STRMOUT_BUFFER_UPDATE             = 0x34,
	IT_DRAW_INDEX_OFFSET_2               = 0x35,
	IT_DRAW_PREAMBLE                     = 0x36,
	IT_WRITE_DATA                        = 0x37,
	IT_DRAW_INDEX_INDIRECT_MULTI         = 0x38,
	IT_MEM_SEMAPHORE                     = 0x39,
	IT_COPY_DW                           = 0x3B,
	IT_WAIT_REG_MEM                      = 0x3C,
	IT_INDIRECT_BUFFER                   = 0x3F,
	IT_COPY_DATA                         = 0x40,
	IT_PFP_SYNC_ME                       = 0x42,
	IT_SURFACE_SYNC                      = 0x43,
	IT_COND_WRITE                        = 0x45,
	IT_EVENT_WRITE                       = 0x46,
	IT_EVENT_WRITE_EOP                   = 0x47,
	IT_EVENT_WRITE_EOS                   = 0x48,
	IT_RELEASE_MEM                       = 0x49,
	IT_PREAMBLE_CNTL                     = 0x4A,
	IT_DMA_DATA                          = 0x50,
	IT_ACQUIRE_MEM                       = 0x58,
	IT_REWIND                            = 0x59,
	IT_LOAD_UCONFIG_REG                  = 0x5E,
	IT_LOAD_SH_REG                       = 0x5F,
	IT_LOAD_CONFIG_REG                   = 0x60,
	IT_LOAD_CONTEXT_REG                  = 0x61,
	IT_SET_CONFIG_REG                    = 0x68,
	IT_SET_CONTEXT_REG                   = 0x69,
	IT_SET_CONTEXT_REG_INDIRECT          = 0x73,
	IT_SET_SH_REG                        = 0x76,
	IT_SET_SH_REG_OFFSET                 = 0x77,
	IT_SET_QUEUE_REG                     = 0x78,
	IT_SET_UCONFIG_REG                   = 0x79,
	IT_SCRATCH_RAM_WRITE                 = 0x7D,
	IT_SCRATCH_RAM_READ                  = 0x7E,
	IT_LOAD_CONST_RAM                    = 0x80,
	IT_WRITE_CONST_RAM                   = 0x81,
	IT_DUMP_CONST_RAM                    = 0x83,
	IT_INCREMENT_CE_COUNTER              = 0x84,
	IT_INCREMENT_DE_COUNTER              = 0x85,
	IT_WAIT_ON_CE_COUNTER                = 0x86,
	IT_WAIT_ON_DE_COUNTER_DIFF           = 0x88,
	IT_SWITCH_BUFFER                     = 0x8B,
	IT_SET_RESOURCES                     = 0xA0,
	IT_MAP_PROCESS                       = 0xA1,
	IT_MAP_QUEUES                        = 0xA2,
	IT_UNMAP_QUEUES                      = 0xA3,
	IT_QUERY_STATUS                      = 0xA4,
	IT_RUN_LIST                          = 0xA5,
};

#define PM4_TYPE_0 0
#define PM4_TYPE_2 2
#define PM4_TYPE_3 3

#endif /* KFD_PM4_OPCODES_H */

