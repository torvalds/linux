/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
 */

#ifndef __IRQSRCS_VPE_6_1_H__
#define __IRQSRCS_VPE_6_1_H__

#define VPE_6_1_SRCID__VPE_ATOMIC_RTN_DONE              0               // 0x0 VPE atomic*_rtn ops complete
#define VPE_6_1_SRCID__VPE_TRAP                         1               // 0x1 Trap
#define VPE_6_1_SRCID__VPE_SRBMWRITE                    2               // 0x2 SRBM write protection
#define VPE_6_1_SRCID__VPE_CTXEMPTY                     3               // 0x3 Context Empty
#define VPE_6_1_SRCID__VPE_PREEMPT                      4               // 0x4 Preemption
#define VPE_6_1_SRCID__VPE_QUEUE_HANG                   5               // 0x5 Queue hang or Command timeout
#define VPE_6_1_SRCID__VPE_ATOMIC_TIMEOUT               6               // 0x6 Atomic CMPSWAP loop timeout
#define VPE_6_1_SRCID__VPE_POLL_TIMEOUT                 7               // 0x7 SRBM read poll timeout
#define VPE_6_1_SRCID__VPE_VM_HOLE                      8               // 0x8 Address in VM hole
#define VPE_6_1_SRCID__VPE_NACK_GEN_ERR                 9               // 0x9 MMHUB return general error (nack = 3)
#define VPE_6_1_SRCID__VPE_NACK_PRT                     10              // 0xA MMHUB return PRT (nack = 2)
#define VPE_6_1_SRCID__VPE_DOORBELL_INVALID             11              // 0xB Doorbell BE invalid
#define VPE_6_1_SRCID__VPE_IB_PREEMPT                   12              // 0xC IB preemption

#endif
