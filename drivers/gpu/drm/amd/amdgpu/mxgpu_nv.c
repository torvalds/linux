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
#include "nbio/nbio_2_3_offset.h"
#include "nbio/nbio_2_3_sh_mask.h"
#include "gc/gc_10_1_0_offset.h"
#include "gc/gc_10_1_0_sh_mask.h"
#include "soc15.h"
#include "navi10_ih.h"
#include "soc15_common.h"
#include "mxgpu_nv.h"

static void xgpu_nv_mailbox_send_ack(struct amdgpu_device *adev)
{
	WREG8(NV_MAIBOX_CONTROL_RCV_OFFSET_BYTE, 2);
}

static void xgpu_nv_mailbox_set_valid(struct amdgpu_device *adev, bool val)
{
	WREG8(NV_MAIBOX_CONTROL_TRN_OFFSET_BYTE, val ? 1 : 0);
}

/*
 * this peek_msg could *only* be called in IRQ routine becuase in IRQ routine
 * RCV_MSG_VALID filed of BIF_BX_PF_MAILBOX_CONTROL must already be set to 1
 * by host.
 *
 * if called no in IRQ routine, this peek_msg cannot guaranteed to return the
 * correct value since it doesn't return the RCV_DW0 under the case that
 * RCV_MSG_VALID is set by host.
 */
static enum idh_event xgpu_nv_mailbox_peek_msg(struct amdgpu_device *adev)
{
	return RREG32_NO_KIQ(mmMAILBOX_MSGBUF_RCV_DW0);
}


static int xgpu_nv_mailbox_rcv_msg(struct amdgpu_device *adev,
				   enum idh_event event)
{
	u32 reg;

	reg = RREG32_NO_KIQ(mmMAILBOX_MSGBUF_RCV_DW0);
	if (reg != event)
		return -ENOENT;

	xgpu_nv_mailbox_send_ack(adev);

	return 0;
}

static uint8_t xgpu_nv_peek_ack(struct amdgpu_device *adev)
{
	return RREG8(NV_MAIBOX_CONTROL_TRN_OFFSET_BYTE) & 2;
}

static int xgpu_nv_poll_ack(struct amdgpu_device *adev)
{
	int timeout  = NV_MAILBOX_POLL_ACK_TIMEDOUT;
	u8 reg;

	do {
		reg = RREG8(NV_MAIBOX_CONTROL_TRN_OFFSET_BYTE);
		if (reg & 2)
			return 0;

		mdelay(5);
		timeout -= 5;
	} while (timeout > 1);

	pr_err("Doesn't get TRN_MSG_ACK from pf in %d msec\n", NV_MAILBOX_POLL_ACK_TIMEDOUT);

	return -ETIME;
}

static int xgpu_nv_poll_msg(struct amdgpu_device *adev, enum idh_event event)
{
	int r;
	uint64_t timeout, now;

	now = (uint64_t)ktime_to_ms(ktime_get());
	timeout = now + NV_MAILBOX_POLL_MSG_TIMEDOUT;

	do {
		r = xgpu_nv_mailbox_rcv_msg(adev, event);
		if (!r)
			return 0;

		msleep(10);
		now = (uint64_t)ktime_to_ms(ktime_get());
	} while (timeout > now);


	return -ETIME;
}

static void xgpu_nv_mailbox_trans_msg (struct amdgpu_device *adev,
	      enum idh_request req, u32 data1, u32 data2, u32 data3)
{
	int r;
	uint8_t trn;

	/* IMPORTANT:
	 * clear TRN_MSG_VALID valid to clear host's RCV_MSG_ACK
	 * and with host's RCV_MSG_ACK cleared hw automatically clear host's RCV_MSG_ACK
	 * which lead to VF's TRN_MSG_ACK cleared, otherwise below xgpu_nv_poll_ack()
	 * will return immediatly
	 */
	do {
		xgpu_nv_mailbox_set_valid(adev, false);
		trn = xgpu_nv_peek_ack(adev);
		if (trn) {
			pr_err("trn=%x ACK should not assert! wait again !\n", trn);
			msleep(1);
		}
	} while (trn);

	WREG32_NO_KIQ(mmMAILBOX_MSGBUF_TRN_DW0, req);
	WREG32_NO_KIQ(mmMAILBOX_MSGBUF_TRN_DW1, data1);
	WREG32_NO_KIQ(mmMAILBOX_MSGBUF_TRN_DW2, data2);
	WREG32_NO_KIQ(mmMAILBOX_MSGBUF_TRN_DW3, data3);
	xgpu_nv_mailbox_set_valid(adev, true);

	/* start to poll ack */
	r = xgpu_nv_poll_ack(adev);
	if (r)
		pr_err("Doesn't get ack from pf, continue\n");

	xgpu_nv_mailbox_set_valid(adev, false);
}

static int xgpu_nv_send_access_requests(struct amdgpu_device *adev,
					enum idh_request req)
{
	int r, retry = 1;
	enum idh_event event = -1;

send_request:
	xgpu_nv_mailbox_trans_msg(adev, req, 0, 0, 0);

	switch (req) {
	case IDH_REQ_GPU_INIT_ACCESS:
	case IDH_REQ_GPU_FINI_ACCESS:
	case IDH_REQ_GPU_RESET_ACCESS:
		event = IDH_READY_TO_ACCESS_GPU;
		break;
	case IDH_REQ_GPU_INIT_DATA:
		event = IDH_REQ_GPU_INIT_DATA_READY;
		break;
	default:
		break;
	}

	if (event != -1) {
		r = xgpu_nv_poll_msg(adev, event);
		if (r) {
			if (retry++ < 2)
				goto send_request;

			if (req != IDH_REQ_GPU_INIT_DATA) {
				pr_err("Doesn't get msg:%d from pf, error=%d\n", event, r);
				return r;
			}
			else /* host doesn't support REQ_GPU_INIT_DATA handshake */
				adev->virt.req_init_data_ver = 0;
		} else {
			if (req == IDH_REQ_GPU_INIT_DATA)
			{
				adev->virt.req_init_data_ver =
					RREG32_NO_KIQ(mmMAILBOX_MSGBUF_RCV_DW1);

				/* assume V1 in case host doesn't set version number */
				if (adev->virt.req_init_data_ver < 1)
					adev->virt.req_init_data_ver = 1;
			}
		}

		/* Retrieve checksum from mailbox2 */
		if (req == IDH_REQ_GPU_INIT_ACCESS || req == IDH_REQ_GPU_RESET_ACCESS) {
			adev->virt.fw_reserve.checksum_key =
				RREG32_NO_KIQ(mmMAILBOX_MSGBUF_RCV_DW2);
		}
	}

	return 0;
}

static int xgpu_nv_request_reset(struct amdgpu_device *adev)
{
	int ret, i = 0;

	while (i < NV_MAILBOX_POLL_MSG_REP_MAX) {
		ret = xgpu_nv_send_access_requests(adev, IDH_REQ_GPU_RESET_ACCESS);
		if (!ret)
			break;
		i++;
	}

	return ret;
}

static int xgpu_nv_request_full_gpu_access(struct amdgpu_device *adev,
					   bool init)
{
	enum idh_request req;

	req = init ? IDH_REQ_GPU_INIT_ACCESS : IDH_REQ_GPU_FINI_ACCESS;
	return xgpu_nv_send_access_requests(adev, req);
}

static int xgpu_nv_release_full_gpu_access(struct amdgpu_device *adev,
					   bool init)
{
	enum idh_request req;
	int r = 0;

	req = init ? IDH_REL_GPU_INIT_ACCESS : IDH_REL_GPU_FINI_ACCESS;
	r = xgpu_nv_send_access_requests(adev, req);

	return r;
}

static int xgpu_nv_request_init_data(struct amdgpu_device *adev)
{
	return xgpu_nv_send_access_requests(adev, IDH_REQ_GPU_INIT_DATA);
}

static int xgpu_nv_mailbox_ack_irq(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("get ack intr and do nothing.\n");
	return 0;
}

static int xgpu_nv_set_mailbox_ack_irq(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	u32 tmp = RREG32_NO_KIQ(mmMAILBOX_INT_CNTL);

	if (state == AMDGPU_IRQ_STATE_ENABLE)
		tmp |= 2;
	else
		tmp &= ~2;

	WREG32_NO_KIQ(mmMAILBOX_INT_CNTL, tmp);

	return 0;
}

static void xgpu_nv_mailbox_flr_work(struct work_struct *work)
{
	struct amdgpu_virt *virt = container_of(work, struct amdgpu_virt, flr_work);
	struct amdgpu_device *adev = container_of(virt, struct amdgpu_device, virt);
	int timeout = NV_MAILBOX_POLL_FLR_TIMEDOUT;

	/* block amdgpu_gpu_recover till msg FLR COMPLETE received,
	 * otherwise the mailbox msg will be ruined/reseted by
	 * the VF FLR.
	 */
	if (atomic_cmpxchg(&adev->in_gpu_reset, 0, 1) != 0)
		return;

	down_write(&adev->reset_sem);

	amdgpu_virt_fini_data_exchange(adev);

	xgpu_nv_mailbox_trans_msg(adev, IDH_READY_TO_RESET, 0, 0, 0);

	do {
		if (xgpu_nv_mailbox_peek_msg(adev) == IDH_FLR_NOTIFICATION_CMPL)
			goto flr_done;

		msleep(10);
		timeout -= 10;
	} while (timeout > 1);

flr_done:
	atomic_set(&adev->in_gpu_reset, 0);
	up_write(&adev->reset_sem);

	/* Trigger recovery for world switch failure if no TDR */
	if (amdgpu_device_should_recover_gpu(adev)
		&& (!amdgpu_device_has_job_running(adev) ||
		adev->sdma_timeout == MAX_SCHEDULE_TIMEOUT ||
		adev->gfx_timeout == MAX_SCHEDULE_TIMEOUT ||
		adev->compute_timeout == MAX_SCHEDULE_TIMEOUT ||
		adev->video_timeout == MAX_SCHEDULE_TIMEOUT))
		amdgpu_device_gpu_recover_imp(adev, NULL);
}

static int xgpu_nv_set_mailbox_rcv_irq(struct amdgpu_device *adev,
				       struct amdgpu_irq_src *src,
				       unsigned type,
				       enum amdgpu_interrupt_state state)
{
	u32 tmp = RREG32_NO_KIQ(mmMAILBOX_INT_CNTL);

	if (state == AMDGPU_IRQ_STATE_ENABLE)
		tmp |= 1;
	else
		tmp &= ~1;

	WREG32_NO_KIQ(mmMAILBOX_INT_CNTL, tmp);

	return 0;
}

static int xgpu_nv_mailbox_rcv_irq(struct amdgpu_device *adev,
				   struct amdgpu_irq_src *source,
				   struct amdgpu_iv_entry *entry)
{
	enum idh_event event = xgpu_nv_mailbox_peek_msg(adev);

	switch (event) {
	case IDH_FLR_NOTIFICATION:
		if (amdgpu_sriov_runtime(adev) && !amdgpu_in_reset(adev))
			WARN_ONCE(!queue_work(adev->reset_domain.wq,
					      &adev->virt.flr_work),
				  "Failed to queue work! at %s",
				  __func__);
		break;
		/* READY_TO_ACCESS_GPU is fetched by kernel polling, IRQ can ignore
		 * it byfar since that polling thread will handle it,
		 * other msg like flr complete is not handled here.
		 */
	case IDH_CLR_MSG_BUF:
	case IDH_FLR_NOTIFICATION_CMPL:
	case IDH_READY_TO_ACCESS_GPU:
	default:
		break;
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs xgpu_nv_mailbox_ack_irq_funcs = {
	.set = xgpu_nv_set_mailbox_ack_irq,
	.process = xgpu_nv_mailbox_ack_irq,
};

static const struct amdgpu_irq_src_funcs xgpu_nv_mailbox_rcv_irq_funcs = {
	.set = xgpu_nv_set_mailbox_rcv_irq,
	.process = xgpu_nv_mailbox_rcv_irq,
};

void xgpu_nv_mailbox_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->virt.ack_irq.num_types = 1;
	adev->virt.ack_irq.funcs = &xgpu_nv_mailbox_ack_irq_funcs;
	adev->virt.rcv_irq.num_types = 1;
	adev->virt.rcv_irq.funcs = &xgpu_nv_mailbox_rcv_irq_funcs;
}

int xgpu_nv_mailbox_add_irq_id(struct amdgpu_device *adev)
{
	int r;

	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_BIF, 135, &adev->virt.rcv_irq);
	if (r)
		return r;

	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_BIF, 138, &adev->virt.ack_irq);
	if (r) {
		amdgpu_irq_put(adev, &adev->virt.rcv_irq, 0);
		return r;
	}

	return 0;
}

int xgpu_nv_mailbox_get_irq(struct amdgpu_device *adev)
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

	INIT_WORK(&adev->virt.flr_work, xgpu_nv_mailbox_flr_work);

	return 0;
}

void xgpu_nv_mailbox_put_irq(struct amdgpu_device *adev)
{
	amdgpu_irq_put(adev, &adev->virt.ack_irq, 0);
	amdgpu_irq_put(adev, &adev->virt.rcv_irq, 0);
}

const struct amdgpu_virt_ops xgpu_nv_virt_ops = {
	.req_full_gpu	= xgpu_nv_request_full_gpu_access,
	.rel_full_gpu	= xgpu_nv_release_full_gpu_access,
	.req_init_data  = xgpu_nv_request_init_data,
	.reset_gpu = xgpu_nv_request_reset,
	.wait_reset = NULL,
	.trans_msg = xgpu_nv_mailbox_trans_msg,
};
