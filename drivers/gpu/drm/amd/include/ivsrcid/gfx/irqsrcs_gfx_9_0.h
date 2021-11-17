/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef __IRQSRCS_GFX_9_0_H__
#define __IRQSRCS_GFX_9_0_H__


#define GFX_9_0__SRCID__CP_RB_INTERRUPT_PKT					176		/* B0 CP_INTERRUPT pkt in RB */
#define GFX_9_0__SRCID__CP_IB1_INTERRUPT_PKT				177		/* B1 CP_INTERRUPT pkt in IB1 */
#define GFX_9_0__SRCID__CP_IB2_INTERRUPT_PKT				178		/* B2 CP_INTERRUPT pkt in IB2 */
#define GFX_9_0__SRCID__CP_PM4_PKT_RSVD_BIT_ERROR			180		/* B4 PM4 Pkt Rsvd Bits Error */
#define GFX_9_0__SRCID__CP_EOP_INTERRUPT					181		/* B5 End-of-Pipe Interrupt */
#define GFX_9_0__SRCID__CP_BAD_OPCODE_ERROR					183		/* B7 Bad Opcode Error */
#define GFX_9_0__SRCID__CP_PRIV_REG_FAULT					184		/* B8 Privileged Register Fault */
#define GFX_9_0__SRCID__CP_PRIV_INSTR_FAULT					185		/* B9 Privileged Instr Fault */
#define GFX_9_0__SRCID__CP_WAIT_MEM_SEM_FAULT				186		/* BA Wait Memory Semaphore Fault (Synchronization Object Fault) */
#define GFX_9_0__SRCID__CP_CTX_EMPTY_INTERRUPT				187		/* BB Context Empty Interrupt */
#define GFX_9_0__SRCID__CP_CTX_BUSY_INTERRUPT				188		/* BC Context Busy Interrupt */
#define GFX_9_0__SRCID__CP_ME_WAIT_REG_MEM_POLL_TIMEOUT		192		/* C0 CP.ME Wait_Reg_Mem Poll Timeout */
#define GFX_9_0__SRCID__CP_SIG_INCOMPLETE					193		/* C1 "Surface Probe Fault Signal Incomplete" */
#define GFX_9_0__SRCID__CP_PREEMPT_ACK					    194		/* C2 Preemption Ack-wledge */
#define GFX_9_0__SRCID__CP_GPF					            195		/* C3 General Protection Fault (GPF) */
#define GFX_9_0__SRCID__CP_GDS_ALLOC_ERROR					196		/* C4 GDS Alloc Error */
#define GFX_9_0__SRCID__CP_ECC_ERROR					    197		/* C5 ECC  Error */
#define GFX_9_0__SRCID__CP_COMPUTE_QUERY_STATUS             199     /* C7 Compute query status */
#define GFX_9_0__SRCID__CP_VM_DOORBELL					    200		/* C8 Unattached VM Doorbell Received */
#define GFX_9_0__SRCID__CP_FUE_ERROR					    201		/* C9 ECC FUE Error */
#define GFX_9_0__SRCID__RLC_STRM_PERF_MONITOR_INTERRUPT		202		/* CA Streaming Perf Monitor Interrupt */
#define GFX_9_0__SRCID__GRBM_RD_TIMEOUT_ERROR				232		/* E8 CRead timeout error */
#define GFX_9_0__SRCID__GRBM_REG_GUI_IDLE					233		/* E9 Register GUI Idle */
#define GFX_9_0__SRCID__SQ_INTERRUPT_ID					    239		/* EF SQ Interrupt (ttrace wrap, errors) */

#endif /* __IRQSRCS_GFX_9_0_H__ */
