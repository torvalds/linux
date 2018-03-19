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
#include "vega10_ih.h"
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
		mdelay(5);
		timeout -= 5;

		reg = RREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0,
						     mmBIF_BX_PF0_MAILBOX_CONTROL));
	}

	return r;
}

static int xgpu_ai_poll_msg(struct amdgpu_device *adev, enum idh_event event)
{
	int r = 0, timeout = AI_MAILBOX_TIMEDOUT;

	r = xgpu_ai_mailbox_rcv_msg(adev, event);
	while (r) {
		if (timeout <= 0) {
			pr_err("Doesn't get msg:%d from pf.\n", event);
			r = -ETIME;
			break;
		}
		mdelay(5);
		timeout -= 5;

		r = xgpu_ai_mailbox_rcv_msg(adev, event);
	}

	return r;
}

static void xgpu_ai_mailbox_trans_msg (struct amdgpu_device *adev,
	      enum idh_request req, u32 data1, u32 data2, u32 data3) {
	u32 reg;
	int r;

	reg = RREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0,
					     mmBIF_BX_PF0_MAILBOX_MSGBUF_TRN_DW0));
	reg = REG_SET_FIELD(reg, BIF_BX_PF0_MAILBOX_MSGBUF_TRN_DW0,
			    MSGBUF_DATA, req);
	WREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_MAILBOX_MSGBUF_TRN_DW0),
		      reg);
	WREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_MAILBOX_MSGBUF_TRN_DW1),
				data1);
	WREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_MAILBOX_MSGBUF_TRN_DW2),
				data2);
	WREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_MAILBOX_MSGBUF_TRN_DW3),
				data3);

	xgpu_ai_mailbox_set_valid(adev, true);

	/* start to poll ack */
	r = xgpu_ai_poll_ack(adev);
	if (r)
		pr_err("Doesn't get ack from pf, continue\n");

	xgpu_ai_mailbox_set_valid(adev, false);
}

static int xgpu_ai_send_access_requests(struct amdgpu_device *adev,
					enum idh_request req)
{
	int r;

	xgpu_ai_mailbox_trans_msg(adev, req, 0, 0, 0);

	/* start to check msg if request is idh_req_gpu_init_access */
	if (req == IDH_REQ_GPU_INIT_ACCESS ||
		req == IDH_REQ_GPU_FINI_ACCESS ||
		req == IDH_REQ_GPU_RESET_ACCESS) {
		r = xgpu_ai_poll_msg(adev, IDH_READY_TO_ACCESS_GPU);
		if (r) {
			pr_err("Doesn't get READY_TO_ACCESS_GPU from pf, give up\n");
			return r;
		}
	}

	return 0;
}

static int xgpu_ai_request_reset(struct amdgpu_device *adev)
{
	return xgpu_ai_send_access_requests(adev, IDH_REQ_GPU_RESET_ACCESS);
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

static int xgpu_ai_mailbox_ack_irq(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("get ack intr and do nothing.\n");
	return 0;
}

static int xgpu_ai_set_mailbox_ack_irq(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	u32 tmp = RREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_MAILBOX_INT_CNTL));

	tmp = REG_SET_FIELD(tmp, BIF_BX_PF0_MAILBOX_INT_CNTL, ACK_INT_EN,
				(state == AMDGPU_IRQ_STATE_ENABLE) ? 1 : 0);
	WREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_MAILBOX_INT_CNTL), tmp);

	return 0;
}

static void xgpu_ai_mailbox_flr_work(struct work_struct *work)
{
	struct amdgpu_virt *virt = container_of(work, struct amdgpu_virt, flr_work);
	struct amdgpu_device *adev = container_of(virt, struct amdgpu_device, virt);

	/* wait until RCV_MSG become 3 */
	if (xgpu_ai_poll_msg(adev, IDH_FLR_NOTIFICATION_CMPL)) {
		pr_err("failed to recieve FLR_CMPL\n");
		return;
	}

	/* Trigger recovery due to world switch failure */
	amdgpu_sriov_gpu_reset(adev, NULL);
}

static int xgpu_ai_set_mailbox_rcv_irq(struct amdgpu_device *adev,
				       struct amdgpu_irq_src *src,
				       unsigned type,
				       enum amdgpu_interrupt_state state)
{
	u32 tmp = RREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_MAILBOX_INT_CNTL));

	tmp = REG_SET_FIELD(tmp, BIF_BX_PF0_MAILBOX_INT_CNTL, VALID_INT_EN,
			    (state == AMDGPU_IRQ_STATE_ENABLE) ? 1 : 0);
	WREG32_NO_KIQ(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_MAILBOX_INT_CNTL), tmp);

	return 0;
}

static int xgpu_ai_mailbox_rcv_irq(struct amdgpu_device *adev,
				   struct amdgpu_irq_src *source,
				   struct amdgpu_iv_entry *entry)
{
	int r;

	/* trigger gpu-reset by hypervisor only if TDR disbaled */
	if (amdgpu_lockup_timeout == 0) {
		/* see what event we get */
		r = xgpu_ai_mailbox_rcv_msg(adev, IDH_FLR_NOTIFICATION);

		/* sometimes the interrupt is delayed to inject to VM, so under such case
		 * the IDH_FLR_NOTIFICATION is overwritten by VF FLR from GIM side, thus
		 * above recieve message could be failed, we should schedule the flr_work
		 * anyway
		 */
		if (r) {
			DRM_ERROR("FLR_NOTIFICATION is missed\n");
			xgpu_ai_mailbox_send_ack(adev);
		}

		schedule_work(&adev->virt.flr_work);
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs xgpu_ai_mailbox_ack_irq_funcs = {
	.set = xgpu_ai_set_mailbox_ack_irq,
	.process = xgpu_ai_mailbox_ack_irq,
};

static const struct amdgpu_irq_src_funcs xgpu_ai_mailbox_rcv_irq_funcs = {
	.set = xgpu_ai_set_mailbox_rcv_irq,
	.process = xgpu_ai_mailbox_rcv_irq,
};

void xgpu_ai_mailbox_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->virt.ack_irq.num_types = 1;
	adev->virt.ack_irq.funcs = &xgpu_ai_mailbox_ack_irq_funcs;
	adev->virt.rcv_irq.num_types = 1;
	adev->virt.rcv_irq.funcs = &xgpu_ai_mailbox_rcv_irq_funcs;
}

int xgpu_ai_mailbox_add_irq_id(struct amdgpu_device *adev)
{
	int r;

	r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_BIF, 135, &adev->virt.rcv_irq);
	if (r)
		return r;

	r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_BIF, 138, &adev->virt.ack_irq);
	if (r) {
		amdgpu_irq_put(adev, &adev->virt.rcv_irq, 0);
		return r;
	}

	return 0;
}

int xgpu_ai_mailbox_get_irq(struct amdgpu_device *adev)
{
	int r;

	r = amdgpu_irq_get(adev, &adev->virt.rcv_irq, 0);
	if (r)
		return r;
	r = amdgpu_irq_get(adev, &adev->virt.ack_irq, 0);
	if (r) {
		amdgpu_irq_put(adev, &adev->virt.rcv_irq, 0);
		return r;
	}

	INIT_WORK(&adev->virt.flr_work, xgpu_ai_mailbox_flr_work);

	return 0;
}

void xgpu_ai_mailbox_put_irq(struct amdgpu_device *adev)
{
	amdgpu_irq_put(adev, &adev->virt.ack_irq, 0);
	amdgpu_irq_put(adev, &adev->virt.rcv_irq, 0);
}

const struct amdgpu_virt_ops xgpu_ai_virt_ops = {
	.req_full_gpu	= xgpu_ai_request_full_gpu_access,
	.rel_full_gpu	= xgpu_ai_release_full_gpu_access,
	.reset_gpu = xgpu_ai_request_reset,
	.trans_msg = xgpu_ai_mailbox_trans_msg,
};
