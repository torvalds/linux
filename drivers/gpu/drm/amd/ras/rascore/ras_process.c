// SPDX-License-Identifier: MIT
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
#include "ras.h"
#include "ras_process.h"

#define RAS_EVENT_FIFO_SIZE (128 * sizeof(struct ras_event_req))

#define RAS_POLLING_ECC_TIMEOUT  300

static int ras_process_put_event(struct ras_core_context *ras_core,
		struct ras_event_req *req)
{
	struct ras_process *ras_proc = &ras_core->ras_proc;
	int ret;

	ret = kfifo_in_spinlocked(&ras_proc->event_fifo,
			req, sizeof(*req), &ras_proc->fifo_spinlock);
	if (!ret) {
		RAS_DEV_ERR(ras_core->dev, "Poison message fifo is full!\n");
		return -ENOSPC;
	}

	return 0;
}

static int ras_process_add_reset_gpu_event(struct ras_core_context *ras_core,
			uint32_t reset_cause)
{
	struct ras_event_req req = {0};

	req.reset = reset_cause;

	return ras_process_put_event(ras_core, &req);
}

static int ras_process_get_event(struct ras_core_context *ras_core,
		struct ras_event_req *req)
{
	struct ras_process *ras_proc = &ras_core->ras_proc;

	return kfifo_out_spinlocked(&ras_proc->event_fifo,
				req, sizeof(*req), &ras_proc->fifo_spinlock);
}

static void ras_process_clear_event_fifo(struct ras_core_context *ras_core)
{
	struct ras_event_req req;
	int ret;

	do {
		ret = ras_process_get_event(ras_core, &req);
	} while (ret);
}

#define AMDGPU_RAS_WAITING_DATA_READY  200
static int ras_process_umc_event(struct ras_core_context *ras_core,
				uint32_t event_count)
{
	struct ras_ecc_count ecc_data;
	int ret = 0;
	uint32_t timeout = 0;
	uint32_t detected_de_count = 0;

	do {
		memset(&ecc_data, 0, sizeof(ecc_data));
		ret = ras_core_update_ecc_info(ras_core);
		if (ret)
			return ret;

		ret = ras_core_query_block_ecc_data(ras_core, RAS_BLOCK_ID__UMC, &ecc_data);
		if (ret)
			return ret;

		if (ecc_data.new_de_count) {
			detected_de_count += ecc_data.new_de_count;
			timeout = 0;
		} else {
			if (!timeout && event_count)
				timeout = AMDGPU_RAS_WAITING_DATA_READY;

			if (timeout) {
				if (!--timeout)
					break;

				msleep(1);
			}
		}
	} while (detected_de_count < event_count);

	if (detected_de_count && ras_core_gpu_is_rma(ras_core))
		ras_process_add_reset_gpu_event(ras_core, GPU_RESET_CAUSE_RMA);

	return 0;
}

static int ras_process_non_umc_event(struct ras_core_context *ras_core)
{
	struct ras_process *ras_proc = &ras_core->ras_proc;
	struct ras_event_req req;
	uint32_t event_count = kfifo_len(&ras_proc->event_fifo);
	uint32_t reset_flags = 0;
	int ret = 0, i;

	for (i = 0; i < event_count; i++) {
		memset(&req, 0, sizeof(req));
		ret = ras_process_get_event(ras_core, &req);
		if (!ret)
			continue;

		ras_core_event_notify(ras_core,
			RAS_EVENT_ID__POISON_CONSUMPTION, &req);

		reset_flags |= req.reset;

		if (req.reset == GPU_RESET_CAUSE_RMA)
			continue;

		if (req.reset)
			RAS_DEV_INFO(ras_core->dev,
				"{%llu} GPU reset for %s RAS poison consumption is issued!\n",
				req.seqno, ras_core_get_ras_block_name(req.block));
		else
			RAS_DEV_INFO(ras_core->dev,
				"{%llu} %s RAS poison consumption is issued!\n",
				req.seqno, ras_core_get_ras_block_name(req.block));
	}

	if (reset_flags) {
		ret = ras_core_event_notify(ras_core,
				RAS_EVENT_ID__RESET_GPU, &reset_flags);
		if (!ret && (reset_flags & GPU_RESET_CAUSE_RMA))
			return -RAS_CORE_GPU_IN_MODE1_RESET;
	}

	return ret;
}

int ras_process_handle_ras_event(struct ras_core_context *ras_core)
{
	struct ras_process *ras_proc = &ras_core->ras_proc;
	uint32_t umc_event_count;
	int ret;

	ret = ras_core_event_notify(ras_core,
			RAS_EVENT_ID__RAS_EVENT_PROC_BEGIN, NULL);
	if (ret)
		return ret;

	ras_aca_clear_fatal_flag(ras_core);
	ras_umc_log_pending_bad_bank(ras_core);

	do {
		umc_event_count = atomic_read(&ras_proc->umc_interrupt_count);
		ret = ras_process_umc_event(ras_core, umc_event_count);
		if (ret == -RAS_CORE_GPU_IN_MODE1_RESET)
			break;

		if (umc_event_count)
			atomic_sub(umc_event_count, &ras_proc->umc_interrupt_count);
	} while (atomic_read(&ras_proc->umc_interrupt_count));

	if ((ret != -RAS_CORE_GPU_IN_MODE1_RESET) &&
			(kfifo_len(&ras_proc->event_fifo)))
		ret = ras_process_non_umc_event(ras_core);

	if (ret == -RAS_CORE_GPU_IN_MODE1_RESET) {
		/* Clear poison fifo */
		ras_process_clear_event_fifo(ras_core);
		atomic_set(&ras_proc->umc_interrupt_count, 0);
	}

	ras_core_event_notify(ras_core,
			RAS_EVENT_ID__RAS_EVENT_PROC_END, NULL);
	return ret;
}

static int thread_wait_condition(void *param)
{
	struct ras_process *ras_proc = (struct ras_process *)param;

	return (kthread_should_stop() ||
		atomic_read(&ras_proc->ras_interrupt_req));
}

static int ras_process_thread(void *context)
{
	struct ras_core_context *ras_core = (struct ras_core_context *)context;
	struct ras_process *ras_proc = &ras_core->ras_proc;

	while (!kthread_should_stop()) {
		ras_wait_event_interruptible_timeout(&ras_proc->ras_process_wq,
			thread_wait_condition, ras_proc,
			msecs_to_jiffies(RAS_POLLING_ECC_TIMEOUT));

		if (kthread_should_stop())
			break;

		if (!ras_core->is_initialized)
			continue;

		atomic_set(&ras_proc->ras_interrupt_req, 0);

		if (ras_core_gpu_in_reset(ras_core))
			continue;

		if (ras_core->sys_fn && ras_core->sys_fn->async_handle_ras_event)
			ras_core->sys_fn->async_handle_ras_event(ras_core, NULL);
		else
			ras_process_handle_ras_event(ras_core);
	}

	return 0;
}

int ras_process_init(struct ras_core_context *ras_core)
{
	struct ras_process *ras_proc = &ras_core->ras_proc;
	int ret;

	ret = kfifo_alloc(&ras_proc->event_fifo, RAS_EVENT_FIFO_SIZE, GFP_KERNEL);
	if (ret)
		return ret;

	spin_lock_init(&ras_proc->fifo_spinlock);

	init_waitqueue_head(&ras_proc->ras_process_wq);

	ras_proc->ras_process_thread = kthread_run(ras_process_thread,
							(void *)ras_core, "ras_process_thread");
	if (!ras_proc->ras_process_thread) {
		RAS_DEV_ERR(ras_core->dev, "Failed to create ras_process_thread.\n");
		ret =  -ENOMEM;
		goto err;
	}

	return 0;

err:
	ras_process_fini(ras_core);
	return ret;
}

int ras_process_fini(struct ras_core_context *ras_core)
{
	struct ras_process *ras_proc = &ras_core->ras_proc;

	if (ras_proc->ras_process_thread) {
		kthread_stop(ras_proc->ras_process_thread);
		ras_proc->ras_process_thread = NULL;
	}

	kfifo_free(&ras_proc->event_fifo);

	return 0;
}

static int ras_process_add_umc_interrupt_req(struct ras_core_context *ras_core,
			struct ras_event_req *req)
{
	struct ras_process *ras_proc = &ras_core->ras_proc;

	atomic_inc(&ras_proc->umc_interrupt_count);
	atomic_inc(&ras_proc->ras_interrupt_req);

	wake_up(&ras_proc->ras_process_wq);
	return 0;
}

static int ras_process_add_non_umc_interrupt_req(struct ras_core_context *ras_core,
		struct ras_event_req *req)
{
	struct ras_process *ras_proc = &ras_core->ras_proc;
	int ret;

	ret = ras_process_put_event(ras_core, req);
	if (!ret) {
		atomic_inc(&ras_proc->ras_interrupt_req);
		wake_up(&ras_proc->ras_process_wq);
	}

	return ret;
}

int ras_process_add_interrupt_req(struct ras_core_context *ras_core,
	struct ras_event_req *req, bool is_umc)
{
	int ret;

	if (!ras_core)
		return -EINVAL;

	if (!ras_core->is_initialized)
		return -EPERM;

	if (is_umc)
		ret = ras_process_add_umc_interrupt_req(ras_core, req);
	else
		ret = ras_process_add_non_umc_interrupt_req(ras_core, req);

	return ret;
}
