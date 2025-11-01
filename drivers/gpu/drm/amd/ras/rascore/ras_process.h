/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#ifndef __RAS_PROCESS_H__
#define __RAS_PROCESS_H__

struct ras_event_req {
	uint64_t seqno;
	uint32_t idx_vf;
	uint32_t block;
	uint16_t pasid;
	uint32_t reset;
	void *pasid_fn;
	void *data;
};

struct ras_process {
	void *dev;
	void *ras_process_thread;
	wait_queue_head_t ras_process_wq;
	atomic_t ras_interrupt_req;
	atomic_t umc_interrupt_count;
	struct kfifo event_fifo;
	spinlock_t fifo_spinlock;
};

struct ras_core_context;
int ras_process_init(struct ras_core_context *ras_core);
int ras_process_fini(struct ras_core_context *ras_core);
int ras_process_handle_ras_event(struct ras_core_context *ras_core);
int ras_process_add_interrupt_req(struct ras_core_context *ras_core,
		struct ras_event_req *req, bool is_umc);
#endif
