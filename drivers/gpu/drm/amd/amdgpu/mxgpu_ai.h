/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef __MXGPU_AI_H__
#define __MXGPU_AI_H__

#define AI_MAILBOX_TIMEDOUT	5000

enum idh_request {
	IDH_REQ_GPU_INIT_ACCESS = 1,
	IDH_REL_GPU_INIT_ACCESS,
	IDH_REQ_GPU_FINI_ACCESS,
	IDH_REL_GPU_FINI_ACCESS,
	IDH_REQ_GPU_RESET_ACCESS
};

enum idh_event {
	IDH_CLR_MSG_BUF	= 0,
	IDH_READY_TO_ACCESS_GPU,
	IDH_FLR_NOTIFICATION,
	IDH_FLR_NOTIFICATION_CMPL,
	IDH_EVENT_MAX
};

extern const struct amdgpu_virt_ops xgpu_ai_virt_ops;

void xgpu_ai_mailbox_set_irq_funcs(struct amdgpu_device *adev);
int xgpu_ai_mailbox_add_irq_id(struct amdgpu_device *adev);
int xgpu_ai_mailbox_get_irq(struct amdgpu_device *adev);
void xgpu_ai_mailbox_put_irq(struct amdgpu_device *adev);

#endif
