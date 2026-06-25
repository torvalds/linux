// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#include "ixd.h"
#include "ixd_ctlq.h"
#include "ixd_virtchnl.h"

#define IXD_CTLQ_RX_TASK_DELAY_JIFFIES	msecs_to_jiffies(300)

/**
 * ixd_ctlq_clean_sq - Clean the send control queue after sending the message
 * @adapter: The adapter that sent the messages
 * @force: Clean regardless of send status
 *
 * Free the libie send resources after sending the message and handling
 * the response.
 */
void ixd_ctlq_clean_sq(struct ixd_adapter *adapter, bool force)
{
	libie_ctlq_xn_send_clean(adapter->asq, kfree, force);
}

/**
 * ixd_ctlq_init_sparams - Initialize control queue send parameters
 * @adapter: The adapter with initialized mailbox
 * @sparams: Parameters to initialize
 * @msg_buf: DMA-mappable pointer to the message being sent
 * @msg_size: Message size
 */
static void ixd_ctlq_init_sparams(struct ixd_adapter *adapter,
				  struct libie_ctlq_xn_send_params *sparams,
				  void *msg_buf, size_t msg_size)
{
	*sparams = (struct libie_ctlq_xn_send_params) {
		.rel_tx_buf = kfree,
		.xnm = adapter->xnm,
		.ctlq = adapter->asq,
		.timeout_ms = IXD_CTLQ_TIMEOUT,
		.send_buf = (struct kvec) {
			.iov_base = msg_buf,
			.iov_len = msg_size,
		},
	};
}

/**
 * ixd_ctlq_do_req - Perform a standard virtchnl request
 * @adapter: The adapter with initialized mailbox
 * @req: virtchnl request description
 *
 * Return: %0 if a message was sent and received a response
 * that was successfully handled by the custom callback,
 * negative error otherwise.
 */
int ixd_ctlq_do_req(struct ixd_adapter *adapter, const struct ixd_ctlq_req *req)
{
	u8 onstack_send_buff[LIBIE_CP_TX_COPYBREAK] __aligned_largest = {};
	struct libie_ctlq_xn_send_params send_params = {};
	struct kvec *recv_mem;
	void *send_buff;
	int err;

	send_buff = libie_cp_can_send_onstack(req->send_size) ?
		    &onstack_send_buff : kzalloc(req->send_size, GFP_KERNEL);
	if (!send_buff)
		return -ENOMEM;

	ixd_ctlq_init_sparams(adapter, &send_params, send_buff,
			      req->send_size);

	send_params.chnl_opcode = req->opcode;

	if (req->send_buff_init)
		req->send_buff_init(adapter, send_buff, req->ctx);

	ixd_ctlq_clean_sq(adapter, false);
	err = libie_ctlq_xn_send(&send_params);
	if (err)
		return err;

	recv_mem = &send_params.recv_mem;
	if (req->recv_process)
		err = req->recv_process(adapter, recv_mem->iov_base,
					recv_mem->iov_len, req->ctx);

	libie_ctlq_release_rx_buf(recv_mem);

	return err;
}

/**
 * ixd_ctlq_handle_msg - Default control queue message handler
 * @ctx: Control plane communication context
 * @msg: Message received
 */
static void ixd_ctlq_handle_msg(struct libie_ctlq_ctx *ctx,
				struct libie_ctlq_msg *msg)
{
	struct ixd_adapter *adapter = pci_get_drvdata(ctx->mmio_info.pdev);

	if (ixd_vc_can_handle_msg(msg))
		ixd_vc_recv_event_msg(adapter, msg);
	else
		dev_dbg_ratelimited(ixd_to_dev(adapter),
				    "Received an unsupported opcode 0x%x from the CP\n",
				    msg->chnl_opcode);

	libie_ctlq_release_rx_buf(&msg->recv_mem);
}

/**
 * ixd_ctlq_recv_mb_msg - Receive a potential message over mailbox periodically
 * @adapter: The adapter with initialized mailbox
 */
static void ixd_ctlq_recv_mb_msg(struct ixd_adapter *adapter)
{
	struct libie_ctlq_xn_recv_params xn_params = {
		.xnm = adapter->xnm,
		.ctlq = adapter->arq,
		.ctlq_msg_handler = ixd_ctlq_handle_msg,
		.budget = LIBIE_CTLQ_MAX_XN_ENTRIES,
	};

	libie_ctlq_xn_recv(&xn_params);
}

/**
 * ixd_ctlq_rx_task - Periodically check for mailbox responses and events
 * @work: work handle
 */
void ixd_ctlq_rx_task(struct work_struct *work)
{
	struct ixd_adapter *adapter;

	adapter = container_of(work, struct ixd_adapter, mbx_task.work);

	queue_delayed_work(system_dfl_wq, &adapter->mbx_task,
			   IXD_CTLQ_RX_TASK_DELAY_JIFFIES);

	ixd_ctlq_recv_mb_msg(adapter);
}
