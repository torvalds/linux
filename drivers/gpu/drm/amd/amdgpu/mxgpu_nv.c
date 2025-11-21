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

#include "amdgpu_reset.h"

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
	int r = 0;
	u32 reg;

	reg = RREG32_NO_KIQ(mmMAILBOX_MSGBUF_RCV_DW0);
	if (reg == IDH_FAIL)
		r = -EINVAL;
	if (reg == IDH_UNRECOV_ERR_NOTIFICATION)
		r = -ENODEV;
	else if (reg != event)
		return -ENOENT;

	xgpu_nv_mailbox_send_ack(adev);

	return r;
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

	dev_err(adev->dev, "Doesn't get TRN_MSG_ACK from pf in %d msec \n", NV_MAILBOX_POLL_ACK_TIMEDOUT);

	return -ETIME;
}

static int xgpu_nv_poll_msg(struct amdgpu_device *adev, enum idh_event event)
{
	int r;
	uint64_t timeout, now;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	now = (uint64_t)ktime_to_ms(ktime_get());
	timeout = now + NV_MAILBOX_POLL_MSG_TIMEDOUT;

	do {
		r = xgpu_nv_mailbox_rcv_msg(adev, event);
		if (!r) {
			dev_dbg(adev->dev, "rcv_msg 0x%x after %llu ms\n",
					event, NV_MAILBOX_POLL_MSG_TIMEDOUT - timeout + now);
			return 0;
		} else if (r == -ENODEV) {
			if (!amdgpu_ras_is_rma(adev)) {
				ras->is_rma = true;
				dev_err(adev->dev, "VF is in an unrecoverable state. "
						"Runtime Services are halted.\n");
			}
			return r;
		}

		msleep(10);
		now = (uint64_t)ktime_to_ms(ktime_get());
	} while (timeout > now);

	dev_dbg(adev->dev, "nv_poll_msg timed out\n");

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
			dev_err_ratelimited(adev->dev, "trn=%x ACK should not assert! wait again !\n", trn);
			msleep(1);
		}
	} while (trn);

	dev_dbg(adev->dev, "trans_msg req = 0x%x, data1 = 0x%x\n", req, data1);
	WREG32_NO_KIQ(mmMAILBOX_MSGBUF_TRN_DW0, req);
	WREG32_NO_KIQ(mmMAILBOX_MSGBUF_TRN_DW1, data1);
	WREG32_NO_KIQ(mmMAILBOX_MSGBUF_TRN_DW2, data2);
	WREG32_NO_KIQ(mmMAILBOX_MSGBUF_TRN_DW3, data3);
	xgpu_nv_mailbox_set_valid(adev, true);

	/* start to poll ack */
	r = xgpu_nv_poll_ack(adev);
	if (r)
		dev_err(adev->dev, "Doesn't get ack from pf, continue\n");

	xgpu_nv_mailbox_set_valid(adev, false);
}

static int xgpu_nv_send_access_requests_with_param(struct amdgpu_device *adev,
			enum idh_request req, u32 data1, u32 data2, u32 data3)
{
	int r, retry = 1;
	enum idh_event event = -1;

send_request:

	if (amdgpu_ras_is_rma(adev))
		return -ENODEV;

	xgpu_nv_mailbox_trans_msg(adev, req, data1, data2, data3);

	switch (req) {
	case IDH_REQ_GPU_INIT_ACCESS:
	case IDH_REQ_GPU_FINI_ACCESS:
	case IDH_REQ_GPU_RESET_ACCESS:
		event = IDH_READY_TO_ACCESS_GPU;
		break;
	case IDH_REQ_GPU_INIT_DATA:
		event = IDH_REQ_GPU_INIT_DATA_READY;
		break;
	case IDH_RAS_POISON:
		if (data1 != 0)
			event = IDH_RAS_POISON_READY;
		break;
	case IDH_REQ_RAS_ERROR_COUNT:
		event = IDH_RAS_ERROR_COUNT_READY;
		break;
	case IDH_REQ_RAS_CPER_DUMP:
		event = IDH_RAS_CPER_DUMP_READY;
		break;
	case IDH_REQ_RAS_CHK_CRITI:
		event = IDH_REQ_RAS_CHK_CRITI_READY;
		break;
	default:
		break;
	}

	if (event != -1) {
		r = xgpu_nv_poll_msg(adev, event);
		if (r) {
			if (retry++ < 5)
				goto send_request;

			if (req != IDH_REQ_GPU_INIT_DATA) {
				dev_err(adev->dev, "Doesn't get msg:%d from pf, error=%d\n", event, r);
				return r;
			} else /* host doesn't support REQ_GPU_INIT_DATA handshake */
				adev->virt.req_init_data_ver = 0;
		} else {
			if (req == IDH_REQ_GPU_INIT_DATA) {
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

static int xgpu_nv_send_access_requests(struct amdgpu_device *adev,
					enum idh_request req)
{
	return xgpu_nv_send_access_requests_with_param(adev,
						req, 0, 0, 0);
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
	dev_dbg(adev->dev, "get ack intr and do nothing.\n");
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

static void xgpu_nv_ready_to_reset(struct amdgpu_device *adev)
{
	xgpu_nv_mailbox_trans_msg(adev, IDH_READY_TO_RESET, 0, 0, 0);
}

static int xgpu_nv_wait_reset(struct amdgpu_device *adev)
{
	int timeout = NV_MAILBOX_POLL_FLR_TIMEDOUT;
	do {
		if (xgpu_nv_mailbox_peek_msg(adev) == IDH_FLR_NOTIFICATION_CMPL) {
			dev_dbg(adev->dev, "Got NV IDH_FLR_NOTIFICATION_CMPL after %d ms\n", NV_MAILBOX_POLL_FLR_TIMEDOUT - timeout);
			return 0;
		}
		msleep(10);
		timeout -= 10;
	} while (timeout > 1);

	dev_dbg(adev->dev, "waiting NV IDH_FLR_NOTIFICATION_CMPL timeout\n");
	return -ETIME;
}

static void xgpu_nv_mailbox_flr_work(struct work_struct *work)
{
	struct amdgpu_virt *virt = container_of(work, struct amdgpu_virt, flr_work);
	struct amdgpu_device *adev = container_of(virt, struct amdgpu_device, virt);
	struct amdgpu_reset_context reset_context = { 0 };

	amdgpu_virt_fini_data_exchange(adev);

	/* Trigger recovery for world switch failure if no TDR */
	if (amdgpu_device_should_recover_gpu(adev)
		&& (!amdgpu_device_has_job_running(adev) ||
		adev->sdma_timeout == MAX_SCHEDULE_TIMEOUT ||
		adev->gfx_timeout == MAX_SCHEDULE_TIMEOUT ||
		adev->compute_timeout == MAX_SCHEDULE_TIMEOUT ||
		adev->video_timeout == MAX_SCHEDULE_TIMEOUT)) {

		reset_context.method = AMD_RESET_METHOD_NONE;
		reset_context.reset_req_dev = adev;
		clear_bit(AMDGPU_NEED_FULL_RESET, &reset_context.flags);
		set_bit(AMDGPU_HOST_FLR, &reset_context.flags);

		amdgpu_device_gpu_recover(adev, NULL, &reset_context);
	}
}

static void xgpu_nv_mailbox_req_bad_pages_work(struct work_struct *work)
{
	struct amdgpu_virt *virt = container_of(work, struct amdgpu_virt, req_bad_pages_work);
	struct amdgpu_device *adev = container_of(virt, struct amdgpu_device, virt);

	if (down_read_trylock(&adev->reset_domain->sem)) {
		amdgpu_virt_fini_data_exchange(adev);
		amdgpu_virt_request_bad_pages(adev);
		up_read(&adev->reset_domain->sem);
	}
}

/**
 * xgpu_nv_mailbox_handle_bad_pages_work - Reinitialize the data exchange region to get fresh bad page information
 * @work: pointer to the work_struct
 *
 * This work handler is triggered when bad pages are ready, and it reinitializes
 * the data exchange region to retrieve updated bad page information from the host.
 */
static void xgpu_nv_mailbox_handle_bad_pages_work(struct work_struct *work)
{
	struct amdgpu_virt *virt = container_of(work, struct amdgpu_virt, handle_bad_pages_work);
	struct amdgpu_device *adev = container_of(virt, struct amdgpu_device, virt);

	if (down_read_trylock(&adev->reset_domain->sem)) {
		amdgpu_virt_fini_data_exchange(adev);
		amdgpu_virt_init_data_exchange(adev);
		up_read(&adev->reset_domain->sem);
	}
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
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	switch (event) {
	case IDH_RAS_BAD_PAGES_READY:
		xgpu_nv_mailbox_send_ack(adev);
		if (amdgpu_sriov_runtime(adev))
			schedule_work(&adev->virt.handle_bad_pages_work);
		break;
	case IDH_RAS_BAD_PAGES_NOTIFICATION:
		xgpu_nv_mailbox_send_ack(adev);
		if (amdgpu_sriov_runtime(adev))
			schedule_work(&adev->virt.req_bad_pages_work);
		break;
	case IDH_UNRECOV_ERR_NOTIFICATION:
		xgpu_nv_mailbox_send_ack(adev);
		if (!amdgpu_ras_is_rma(adev)) {
			ras->is_rma = true;
			dev_err(adev->dev, "VF is in an unrecoverable state. Runtime Services are halted.\n");
		}

		if (amdgpu_sriov_runtime(adev))
			WARN_ONCE(!amdgpu_reset_domain_schedule(adev->reset_domain,
						&adev->virt.flr_work),
					"Failed to queue work! at %s",
					__func__);
		break;
	case IDH_FLR_NOTIFICATION:
		if (amdgpu_sriov_runtime(adev))
			WARN_ONCE(!amdgpu_reset_domain_schedule(adev->reset_domain,
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
	INIT_WORK(&adev->virt.req_bad_pages_work, xgpu_nv_mailbox_req_bad_pages_work);
	INIT_WORK(&adev->virt.handle_bad_pages_work, xgpu_nv_mailbox_handle_bad_pages_work);

	return 0;
}

void xgpu_nv_mailbox_put_irq(struct amdgpu_device *adev)
{
	amdgpu_irq_put(adev, &adev->virt.ack_irq, 0);
	amdgpu_irq_put(adev, &adev->virt.rcv_irq, 0);
}

static void xgpu_nv_ras_poison_handler(struct amdgpu_device *adev,
		enum amdgpu_ras_block block)
{
	if (amdgpu_ip_version(adev, UMC_HWIP, 0) < IP_VERSION(12, 0, 0)) {
		xgpu_nv_send_access_requests(adev, IDH_RAS_POISON);
	} else {
		amdgpu_virt_fini_data_exchange(adev);
		xgpu_nv_send_access_requests_with_param(adev,
					IDH_RAS_POISON,	block, 0, 0);
	}
}

static bool xgpu_nv_rcvd_ras_intr(struct amdgpu_device *adev)
{
	enum idh_event msg = xgpu_nv_mailbox_peek_msg(adev);

	return (msg == IDH_RAS_ERROR_DETECTED || msg == 0xFFFFFFFF);
}

static int xgpu_nv_req_ras_err_count(struct amdgpu_device *adev)
{
	return xgpu_nv_send_access_requests(adev, IDH_REQ_RAS_ERROR_COUNT);
}

static int xgpu_nv_req_ras_cper_dump(struct amdgpu_device *adev, u64 vf_rptr)
{
	uint32_t vf_rptr_hi, vf_rptr_lo;

	vf_rptr_hi = (uint32_t)(vf_rptr >> 32);
	vf_rptr_lo = (uint32_t)(vf_rptr & 0xFFFFFFFF);
	return xgpu_nv_send_access_requests_with_param(
		adev, IDH_REQ_RAS_CPER_DUMP, vf_rptr_hi, vf_rptr_lo, 0);
}

static int xgpu_nv_req_ras_bad_pages(struct amdgpu_device *adev)
{
	return xgpu_nv_send_access_requests(adev, IDH_REQ_RAS_BAD_PAGES);
}

static int xgpu_nv_check_vf_critical_region(struct amdgpu_device *adev, u64 addr)
{
	uint32_t addr_hi, addr_lo;

	addr_hi = (uint32_t)(addr >> 32);
	addr_lo = (uint32_t)(addr & 0xFFFFFFFF);
	return xgpu_nv_send_access_requests_with_param(
		adev, IDH_REQ_RAS_CHK_CRITI, addr_hi, addr_lo, 0);
}

const struct amdgpu_virt_ops xgpu_nv_virt_ops = {
	.req_full_gpu	= xgpu_nv_request_full_gpu_access,
	.rel_full_gpu	= xgpu_nv_release_full_gpu_access,
	.req_init_data  = xgpu_nv_request_init_data,
	.reset_gpu = xgpu_nv_request_reset,
	.ready_to_reset = xgpu_nv_ready_to_reset,
	.wait_reset = xgpu_nv_wait_reset,
	.trans_msg = xgpu_nv_mailbox_trans_msg,
	.ras_poison_handler = xgpu_nv_ras_poison_handler,
	.rcvd_ras_intr = xgpu_nv_rcvd_ras_intr,
	.req_ras_err_count = xgpu_nv_req_ras_err_count,
	.req_ras_cper_dump = xgpu_nv_req_ras_cper_dump,
	.req_bad_pages = xgpu_nv_req_ras_bad_pages,
	.req_ras_chk_criti = xgpu_nv_check_vf_critical_region
};
