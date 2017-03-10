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

#include "amdgpu.h"
#include "vega10/soc15ip.h"
#include "vega10/NBIO/nbio_6_1_offset.h"
#include "vega10/NBIO/nbio_6_1_sh_mask.h"
#include "vega10/GC/gc_9_0_offset.h"
#include "vega10/GC/gc_9_0_sh_mask.h"
#include "soc15.h"
#include "soc15_common.h"
#include "mxgpu_ai.h"

static void xgpu_ai_mailbox_send_ack(struct amdgpu_device *adev)
{
	u32 reg;
	int timeout = AI_MAILBOX_TIMEDOUT;
	u32 mask = REG_FIELD_MASK(BIF_BX_PF0_MAILBOX_CONTROL, RCV_MSG_VALID);

	reg = RREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0,
					     mmBIF_BX_PF0_MAILBOX_CONTROL));
	reg = REG_SET_FIELD(reg, BIF_BX_PF0_MAILBOX_CONTROL, RCV_MSG_ACK, 1);
	WREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0,
				       mmBIF_BX_PF0_MAILBOX_CONTROL), reg);

	/*Wait for RCV_MSG_VALID to be 0*/
	reg = RREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0,
					     mmBIF_BX_PF0_MAILBOX_CONTROL));
	while (reg & mask) {
		if (timeout <= 0) {
			pr_err("RCV_MSG_VALID is not cleared\n");
			break;
		}
		mdelay(1);
		timeout -=1;

		reg = RREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0,
						     mmBIF_BX_PF0_MAILBOX_CONTROL));
	}
}

static void xgpu_ai_mailbox_set_valid(struct amdgpu_device *adev, bool val)
{
	u32 reg;

	reg = RREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0,
					     mmBIF_BX_PF0_MAILBOX_CONTROL));
	reg = REG_SET_FIELD(reg, BIF_BX_PF0_MAILBOX_CONTROL,
			    TRN_MSG_VALID, val ? 1 : 0);
	WREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_MAILBOX_CONTROL),
		      reg);
}

static void xgpu_ai_mailbox_trans_msg(struct amdgpu_device *adev,
				      enum idh_request req)
{
	u32 reg;

	reg = RREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0,
					     mmBIF_BX_PF0_MAILBOX_MSGBUF_TRN_DW0));
	reg = REG_SET_FIELD(reg, BIF_BX_PF0_MAILBOX_MSGBUF_TRN_DW0,
			    MSGBUF_DATA, req);
	WREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_MAILBOX_MSGBUF_TRN_DW0),
		      reg);

	xgpu_ai_mailbox_set_valid(adev, true);
}

static int xgpu_ai_mailbox_rcv_msg(struct amdgpu_device *adev,
				   enum idh_event event)
{
	u32 reg;
	u32 mask = REG_FIELD_MASK(BIF_BX_PF0_MAILBOX_CONTROL, RCV_MSG_VALID);

	if (event != IDH_FLR_NOTIFICATION_CMPL) {
		reg = RREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0,
						     mmBIF_BX_PF0_MAILBOX_CONTROL));
		if (!(reg & mask))
			return -ENOENT;
	}

	reg = RREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0,
					     mmBIF_BX_PF0_MAILBOX_MSGBUF_RCV_DW0));
	if (reg != event)
		return -ENOENT;

	xgpu_ai_mailbox_send_ack(adev);

	return 0;
}

static int xgpu_ai_poll_ack(struct amdgpu_device *adev)
{
	int r = 0, timeout = AI_MAILBOX_TIMEDOUT;
	u32 mask = REG_FIELD_MASK(BIF_BX_PF0_MAILBOX_CONTROL, TRN_MSG_ACK);
	u32 reg;

	reg = RREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0,
					     mmBIF_BX_PF0_MAILBOX_CONTROL));
	while (!(reg & mask)) {
		if (timeout <= 0) {
			pr_err("Doesn't get ack from pf.\n");
			r = -ETIME;
			break;
		}
		msleep(1);
		timeout -= 1;

		reg = RREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0,
						     mmBIF_BX_PF0_MAILBOX_CONTROL));
	}

	return r;
}

static int xgpu_vi_poll_msg(struct amdgpu_device *adev, enum idh_event event)
{
	int r = 0, timeout = AI_MAILBOX_TIMEDOUT;

	r = xgpu_ai_mailbox_rcv_msg(adev, event);
	while (r) {
		if (timeout <= 0) {
			pr_err("Doesn't get ack from pf.\n");
			r = -ETIME;
			break;
		}
		msleep(1);
		timeout -= 1;

		r = xgpu_ai_mailbox_rcv_msg(adev, event);
	}

	return r;
}


static int xgpu_ai_send_access_requests(struct amdgpu_device *adev,
					enum idh_request req)
{
	int r;

	xgpu_ai_mailbox_trans_msg(adev, req);

	/* start to poll ack */
	r = xgpu_ai_poll_ack(adev);
	if (r)
		return r;

	xgpu_ai_mailbox_set_valid(adev, false);

	/* start to check msg if request is idh_req_gpu_init_access */
	if (req == IDH_REQ_GPU_INIT_ACCESS ||
		req == IDH_REQ_GPU_FINI_ACCESS ||
		req == IDH_REQ_GPU_RESET_ACCESS) {
		r = xgpu_vi_poll_msg(adev, IDH_READY_TO_ACCESS_GPU);
		if (r)
			return r;
	}

	return 0;
}

static int xgpu_ai_request_full_gpu_access(struct amdgpu_device *adev,
					   bool init)
{
	enum idh_request req;

	req = init ? IDH_REQ_GPU_INIT_ACCESS : IDH_REQ_GPU_FINI_ACCESS;
	return xgpu_ai_send_access_requests(adev, req);
}

static int xgpu_ai_release_full_gpu_access(struct amdgpu_device *adev,
					   bool init)
{
	enum idh_request req;
	int r = 0;

	req = init ? IDH_REL_GPU_INIT_ACCESS : IDH_REL_GPU_FINI_ACCESS;
	r = xgpu_ai_send_access_requests(adev, req);

	return r;
}

const struct amdgpu_virt_ops xgpu_ai_virt_ops = {
	.req_full_gpu	= xgpu_ai_request_full_gpu_access,
	.rel_full_gpu	= xgpu_ai_release_full_gpu_access,
};
