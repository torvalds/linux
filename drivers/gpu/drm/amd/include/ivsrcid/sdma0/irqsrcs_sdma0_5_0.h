/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
#ifndef __IRQSRCS_SDMA0_5_0_H__
#define __IRQSRCS_SDMA0_5_0_H__

#define SDMA0_5_0__SRCID__SDMA_ATOMIC_RTN_DONE				217		// 0xD9 SDMA atomic*_rtn ops complete
#define SDMA0_5_0__SRCID__SDMA_ATOMIC_TIMEOUT				218		// 0xDA SDMA atomic CMPSWAP loop timeout
#define SDMA0_5_0__SRCID__SDMA_IB_PREEMPT				219		// 0xDB sdma mid-command buffer preempt interrupt
#define SDMA0_5_0__SRCID__SDMA_ECC					220		// 0xDC ECC  Error
#define SDMA0_5_0__SRCID__SDMA_PAGE_FAULT				221		// 0xDD Page Fault Error from UTCL2 when nack=3
#define SDMA0_5_0__SRCID__SDMA_PAGE_NULL				222		// 0xDE Page Null from UTCL2 when nack=2
#define SDMA0_5_0__SRCID__SDMA_XNACK					223		// 0xDF Page retry  timeout after UTCL2 return nack=1
#define SDMA0_5_0__SRCID__SDMA_TRAP					224		// 0xE0 Trap
#define SDMA0_5_0__SRCID__SDMA_SEM_INCOMPLETE_TIMEOUT			225		// 0xE1 0xDAGPF (Sem incomplete timeout)
#define SDMA0_5_0__SRCID__SDMA_SEM_WAIT_FAIL_TIMEOUT			226		// 0xE2 Semaphore wait fail timeout
#define SDMA0_5_0__SRCID__SDMA_SRAM_ECC					228		// 0xE4 SRAM ECC Error
#define SDMA0_5_0__SRCID__SDMA_PREEMPT					240		// 0xF0 SDMA New Run List
#define SDMA0_5_0__SRCID__SDMA_VM_HOLE					242		// 0xF2 MC or SEM address in VM hole
#define SDMA0_5_0__SRCID__SDMA_CTXEMPTY					243		// 0xF3 Context Empty
#define SDMA0_5_0__SRCID__SDMA_DOORBELL_INVALID				244		// 0xF4 Doorbell BE invalid
#define SDMA0_5_0__SRCID__SDMA_FROZEN					245		// 0xF5 SDMA Frozen
#define SDMA0_5_0__SRCID__SDMA_POLL_TIMEOUT				246		// 0xF6 SRBM read poll timeout
#define SDMA0_5_0__SRCID__SDMA_SRBMWRITE				247		// 0xF7 SRBM write Protection
#endif
